// Included by test_core.cpp: IP parsing, WireGuard config, command/ruleset generation, network manager, VPN manager.
// Backends, resolver and link provider here are FAKES. No real tunnel is created by any test in this file.
#include <algorithm>
#include "network.h"
#include "vpn.h"
#include "wg_commands.h"
#include "wireguard.h"

static std::string wgKey(char c) { return base64Encode(std::string(32, c)); }  // valid 32-byte base64 key

static std::string wgText(const std::string& allowed = "0.0.0.0/0, ::/0", const std::string& extraPeer = "PersistentKeepalive = 25\n",
                          const std::string& extraIface = "MTU = 1380\n") {
  return "# my vpn\n[Interface]\nPrivateKey = " + wgKey('a') + "\nAddress = 10.8.0.2/32, fd00::2/128\nDNS = 10.8.0.1, vpn.example\n" + extraIface +
         "\n[Peer]\nPublicKey = " + wgKey('b') + "\nPresharedKey = " + wgKey('c') + "\nAllowedIPs = " + allowed +
         "\nEndpoint = vpn.example.org:51820  # server\n" + extraPeer;
}

static void test_ip_parsing() {
  IpAddr a;
  CHECK(parseIp("192.168.1.10", a) && !a.v6 && a.b[0] == 192 && a.b[3] == 10 && formatIp(a) == "192.168.1.10");
  for (const char* bad : {"", "1.2.3", "1.2.3.4.5", "256.1.1.1", "01.2.3.4", "1..2.3", "a.b.c.d", "1.2.3.4 ", "-1.2.3.4"}) CHECK(!parseIp(bad, a));
  CHECK(parseIp("::", a) && a.v6 && formatIp(a) == "::");
  CHECK(parseIp("::1", a) && formatIp(a) == "::1");
  CHECK(parseIp("2001:DB8:0:0:0:0:0:1", a) && formatIp(a) == "2001:db8::1");   // canonical lowercase, zero run compressed
  CHECK(parseIp("fe80::1:0:0:2", a) && formatIp(a) == "fe80::1:0:0:2");        // longest run wins; the first wins ties
  CHECK(parseIp("1:2:3:4:5:6:7:8", a) && formatIp(a) == "1:2:3:4:5:6:7:8");
  CHECK(parseIp("1::", a) && formatIp(a) == "1::");
  CHECK(parseIp("1:0:3:4:5:6:7:8", a) && formatIp(a) == "1:0:3:4:5:6:7:8");    // a single zero group is not compressed
  for (const char* bad : {":::", "1::2::3", "12345::1", "::g", "1:2:3:4:5:6:7", "1:2:3:4:5:6:7:8:9", ":1::2", "1::2:", "::1.2.3.4", "fe80::1%eth0", "1:2:3:4:5:6:7::8"})
    CHECK(!parseIp(bad, a));

  Cidr c;
  CHECK(parseCidr("10.0.0.2/32", c) && c.prefix == 32 && formatCidr(c) == "10.0.0.2/32");
  CHECK(parseCidr("0.0.0.0/0", c) && isDefaultRoute(c) && parseCidr("::/0", c) && isDefaultRoute(c) && c.ip.v6);
  CHECK(parseCidr("10.1.2.3", c) && c.prefix == 32 && parseCidr("fd00::2", c) && c.prefix == 128);
  for (const char* bad : {"10.0.0.0/33", "::/129", "10.0.0.0/", "10.0.0.0/-1", "10.0.0.0/ab", "/24", "10.0.0.0/1000"}) CHECK(!parseCidr(bad, c));

  std::string h;
  int port = 0;
  CHECK(parseEndpoint("vpn.example.org:51820", h, port) && h == "vpn.example.org" && port == 51820);
  CHECK(parseEndpoint("203.0.113.7:1", h, port) && h == "203.0.113.7" && port == 1);
  CHECK(parseEndpoint("[2001:db8::1]:443", h, port) && h == "2001:db8::1" && port == 443);
  for (const char* bad : {"", "host", "host:", ":51820", "host:0", "host:65536", "host:abc", "2001:db8::1:51820", "[2001:db8::1]51820", "[1.2.3.4]:80",
                          "-bad.example:80", "bad_name.example:80", "a..b:80", "999.1.1.1:80", "host:123456"})
    CHECK(!parseEndpoint(bad, h, port));
  CHECK(validIfaceName("wg0") && validIfaceName("faios-vpn_1") && !validIfaceName("") && !validIfaceName("a b") && !validIfaceName("wg0;rm") &&
        !validIfaceName("0123456789abcdef") && !validIfaceName("wg0\""));
}

static void test_wireguard_parse() {
  WgParseResult r = parseWgConfig(wgText());
  CHECK(r.ok && r.errors.empty());
  CHECK(r.cfg.privateKey == wgKey('a') && r.cfg.addresses.size() == 2 && formatCidr(r.cfg.addresses[1]) == "fd00::2/128");
  CHECK(r.cfg.dns.size() == 1 && r.cfg.dns[0] == "10.8.0.1" && r.cfg.dnsSearch.size() == 1 && r.cfg.dnsSearch[0] == "vpn.example");
  CHECK(r.cfg.mtu == 1380 && r.cfg.peers.size() == 1);
  const WgPeer& p = r.cfg.peers[0];
  CHECK(p.publicKey == wgKey('b') && p.presharedKey == wgKey('c') && p.allowedIps.size() == 2 && p.endpointHost == "vpn.example.org" &&
        p.endpointPort == 51820 && p.keepalive == 25);
  CHECK(isFullTunnel(r.cfg) && hasEndpoint(r.cfg));
  CHECK(!isFullTunnel(parseWgConfig(wgText("10.0.0.0/24")).cfg) && parseWgConfig(wgText("10.0.0.0/24")).ok);

  // case-insensitive names, repeated keys, ipv6 endpoint, keepalive off
  std::string t = "[interface]\nprivatekey=" + wgKey('a') + "\naddress=10.0.0.2/24\naddress = fd00::2/64\n[PEER]\npublickey=" + wgKey('b') +
                  "\nallowedips=10.0.0.0/24\nAllowedIPs = fd00::/64\nendpoint=[2001:db8::7]:51820\nPersistentKeepalive = off\n";
  r = parseWgConfig(t);
  CHECK(r.ok && r.cfg.addresses.size() == 2 && r.cfg.peers[0].allowedIps.size() == 2 && r.cfg.peers[0].endpointHost == "2001:db8::7" && r.cfg.peers[0].keepalive == 0);
  CHECK(r.cfg.peers[0].presharedKey.empty());
  CHECK(parseWgConfig(wgText("10.0.0.0/24", "")).ok && !hasEndpoint(parseWgConfig("[Interface]\nPrivateKey=" + wgKey('a') + "\nAddress=10.0.0.2/32\n[Peer]\nPublicKey=" + wgKey('b') + "\nAllowedIPs=10.0.0.0/24\n").cfg));

  // errors never echo key material
  std::string badKey = "[Interface]\nPrivateKey = SECRET-NOT-BASE64\nAddress = 10.0.0.2/32\n[Peer]\nPublicKey = " + wgKey('b') + "\nAllowedIPs = 0.0.0.0/0\n";
  r = parseWgConfig(badKey);
  CHECK(!r.ok && describeErrors(r).find("SECRET-NOT-BASE64") == std::string::npos && describeErrors(r).find("line 2: PrivateKey") == 0);
  CHECK(r.cfg.privateKey.empty() && r.cfg.peers.empty());  // a failed parse hands back nothing
  // a key pasted into the wrong field is not echoed back either
  r = parseWgConfig("[Interface]\nPrivateKey = " + wgKey('a') + "\nAddress = " + wgKey('z') + "\n[Peer]\nPublicKey = " + wgKey('b') + "\nAllowedIPs = 0.0.0.0/0\n");
  CHECK(!r.ok && describeErrors(r).find(wgKey('z')) == std::string::npos);

  // script hooks are refused, never run
  for (const char* hook : {"PostUp = rm -rf /\n", "PreUp = x\n", "postdown = y\n", "PreDown = z\n"}) {
    r = parseWgConfig(wgText("0.0.0.0/0", "PersistentKeepalive = 25\n", hook));
    CHECK(!r.ok && describeErrors(r).find("never run") != std::string::npos);
  }
  CHECK(!parseWgConfig(wgText("0.0.0.0/0", "", "Table = off\n")).ok && !parseWgConfig(wgText("0.0.0.0/0", "", "SaveConfig = true\n")).ok);

  // structure
  CHECK(!parseWgConfig("").ok && !parseWgConfig("# nothing\n").ok);
  CHECK(!parseWgConfig("[Interface]\nAddress = 10.0.0.2/32\n").ok);                                                             // no key, no peer
  CHECK(!parseWgConfig("PrivateKey = " + wgKey('a') + "\n").ok);                                                                // outside a section
  CHECK(!parseWgConfig("[Interface]\n[Interface]\n").ok && !parseWgConfig("[Bogus]\n").ok && !parseWgConfig("[Interface]\njunk line\n").ok);
  CHECK(!parseWgConfig(wgText("0.0.0.0/0", "Endpoint = nope\n")).ok);
  CHECK(!parseWgConfig(wgText("0.0.0.0/0", "PersistentKeepalive = 99999999\n")).ok && !parseWgConfig(wgText("0.0.0.0/0", "", "MTU = 10\n")).ok);
  CHECK(!parseWgConfig(wgText("0.0.0.0/0", "", "ListenPort = 70000\n")).ok && !parseWgConfig(wgText("0.0.0.0/0", "", "DNS = not an ip!\n")).ok);
  CHECK(!parseWgConfig(wgText("0.0.0.0/0", "", "Bogus = 1\n")).ok && !parseWgConfig(wgText("999.0.0.0/8")).ok);
  CHECK(!parseWgConfig(std::string(70000, '#')).ok);  // size limit
  std::string many = "[Interface]\nPrivateKey=" + wgKey('a') + "\nAddress=10.0.0.2/32\n";
  for (int i = 0; i < 40; i++) many += "[Peer]\nPublicKey=" + wgKey('b') + "\nAllowedIPs=10.0.0." + std::to_string(i) + "/32\n";
  CHECK(!parseWgConfig(many).ok);  // peer limit
  CHECK(!validWgKey("") && !validWgKey("short") && !validWgKey(std::string(44, 'A')) && validWgKey(wgKey('a')));
}

static bool anyArgvContains(const std::vector<Command>& cmds, const std::string& needle) {
  for (auto& c : cmds) for (auto& a : c.argv) if (a.find(needle) != std::string::npos) return true;
  return false;
}
static std::vector<std::string> describeAll(const std::vector<Command>& cmds) {
  std::vector<std::string> v;
  for (auto& c : cmds) v.push_back(c.describe());
  return v;
}

static void test_tunnel_commands() {
  WgConfig full = parseWgConfig(wgText()).cfg;
  auto up = tunnelUpCommands(full, "wg0", 51820, {"203.0.113.7"});
  std::vector<std::string> want = {
      "ip link add dev wg0 type wireguard", "wg setconf wg0 /dev/stdin", "ip -4 address add 10.8.0.2/32 dev wg0", "ip -6 address add fd00::2/128 dev wg0",
      "ip link set mtu 1380 up dev wg0", "wg set wg0 fwmark 51820",
      "ip -4 route add 0.0.0.0/0 dev wg0 table 51820", "ip -4 rule add not fwmark 51820 table 51820", "ip -4 rule add table main suppress_prefixlength 0",
      "ip -6 route add ::/0 dev wg0 table 51820", "ip -6 rule add not fwmark 51820 table 51820", "ip -6 rule add table main suppress_prefixlength 0"};
  CHECK(describeAll(up) == want);

  // secrets: only on stdin of `wg setconf`, never in any argument, never in describe()
  CHECK(!anyArgvContains(up, wgKey('a')) && !anyArgvContains(up, wgKey('c')));
  int withStdin = 0;
  for (auto& c : up) { withStdin += !c.stdinData.empty(); CHECK(c.describe().find(wgKey('a')) == std::string::npos); }
  CHECK(withStdin == 1 && up[1].stdinData.find("PrivateKey = " + wgKey('a')) != std::string::npos && up[1].stdinData.find("PresharedKey = " + wgKey('c')) != std::string::npos);
  CHECK(up[1].stdinData.find("Endpoint = 203.0.113.7:51820") != std::string::npos && up[1].stdinData.find("vpn.example.org") == std::string::npos);  // resolved IP, not the name
  CHECK(up[1].stdinData.find("PersistentKeepalive = 25") != std::string::npos && up[1].stdinData.find("AllowedIPs = 0.0.0.0/0, ::/0") != std::string::npos);
  CHECK(wgSetconf(full, {"2001:db8::7"}).find("Endpoint = [2001:db8::7]:51820") != std::string::npos);
  CHECK(wgSetconf(full, {}).find("Endpoint") == std::string::npos);  // unresolved: leave it out rather than guess

  // split tunnel: routes in the main table, no policy routing, no fwmark
  WgConfig split = parseWgConfig(wgText("10.0.0.0/24, 192.168.5.0/24, fd00::/64", "", "")).cfg;
  want = {"ip link add dev wg0 type wireguard", "wg setconf wg0 /dev/stdin", "ip -4 address add 10.8.0.2/32 dev wg0", "ip -6 address add fd00::2/128 dev wg0",
          "ip link set up dev wg0", "ip -4 route add 10.0.0.0/24 dev wg0", "ip -4 route add 192.168.5.0/24 dev wg0", "ip -6 route add fd00::/64 dev wg0"};
  CHECK(describeAll(tunnelUpCommands(split, "wg0", 51820, {"203.0.113.7"})) == want);
  // IPv4-only full tunnel adds no IPv6 policy rules (and leaves v6 alone)
  auto v4only = describeAll(tunnelUpCommands(parseWgConfig(wgText("0.0.0.0/0", "", "")).cfg, "wg0", 51820, {"203.0.113.7"}));
  bool anyV6Rule = false;
  for (auto& d : v4only) anyV6Rule |= d.find("ip -6 rule") == 0 || d.find("ip -6 route") == 0;
  CHECK(!anyV6Rule && std::find(v4only.begin(), v4only.end(), "ip -4 rule add not fwmark 51820 table 51820") != v4only.end());

  want = {"ip -6 rule del table main suppress_prefixlength 0", "ip -6 rule del not fwmark 51820 table 51820", "ip -6 route flush table 51820",
          "ip -4 rule del table main suppress_prefixlength 0", "ip -4 rule del not fwmark 51820 table 51820", "ip -4 route flush table 51820", "ip link del dev wg0"};
  auto down = tunnelDownCommands(full, "wg0", 51820);
  CHECK(describeAll(down) == want);
  for (auto& c : down) CHECK(c.ignoreFailure);  // teardown must not abort halfway
  CHECK(describeAll(tunnelDownCommands(split, "wg0", 51820)) == std::vector<std::string>{"ip link del dev wg0"});

  CHECK(tunnelUpCommands(full, "wg0; reboot", 51820, {}).empty() && tunnelDownCommands(full, "a b", 51820).empty());  // injection attempt
}

static void test_kill_switch_ruleset() {
  KillSwitchSpec s;
  IpAddr v4, v6;
  parseIp("203.0.113.7", v4);
  parseIp("2001:db8::7", v6);
  s.endpoints = {{v4, 51820}, {v6, 443}};
  std::string r = killSwitchRuleset(s);
  CHECK(r.find("table inet faios_killswitch") == 0);
  CHECK(r.find("chain output {\n    type filter hook output priority 0; policy drop;") != std::string::npos);
  CHECK(r.find("chain input {\n    type filter hook input priority 0; policy drop;") != std::string::npos);  // default-drop both ways
  for (const char* rule : {"oifname \"lo\" accept", "oifname \"wg0\" accept", "iifname \"wg0\" accept", "ip daddr 203.0.113.7 udp dport 51820 accept",
                           "ip6 daddr 2001:db8::7 udp dport 443 accept", "udp sport 68 udp dport 67 accept"})
    CHECK(r.find(rule) != std::string::npos);
  CHECK(r.find("192.168.0.0/16") == std::string::npos && r.find("fe80::/10") == std::string::npos);  // LAN stays blocked unless asked
  CHECK(r.find("accept\n  }\n  chain input") != std::string::npos);                                  // output chain closes after its accepts
  s.allowLan = true;
  std::string lan = killSwitchRuleset(s);
  CHECK(lan.find("ip daddr { 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16, 169.254.0.0/16 } accept") != std::string::npos && lan.find("fe80::/10") != std::string::npos);
  // nothing in the output chain may accept "everything"
  CHECK(r.find("accept\n    accept") == std::string::npos);

  KillSwitchSpec bad = s;
  bad.iface = "wg0\" accept #";
  CHECK(killSwitchRuleset(bad).empty() && killSwitchInstall(bad).empty());  // injection attempt through the interface name
  bad = s;
  bad.endpoints.clear();
  CHECK(killSwitchRuleset(bad).empty());  // no endpoint would make the tunnel itself unreachable
  auto inst = killSwitchInstall(s);
  CHECK(inst.size() == 2 && inst[0].describe() == "nft delete table inet faios_killswitch" && inst[0].ignoreFailure);
  CHECK(inst[1].describe() == "nft -f /dev/stdin" && inst[1].stdinData == lan);
  CHECK(killSwitchRemove().size() == 1 && killSwitchRemove()[0].describe() == "nft delete table inet faios_killswitch");
}

static void test_dns_text() {
  std::string t = resolvConf({"10.8.0.1", "2001:DB8:0:0:0:0:0:53", "not-an-ip", "1.1.1.1\nnameserver 6.6.6.6"}, {"corp.example", "bad host", "x\nsearch evil"});
  CHECK(t.find("nameserver 10.8.0.1\n") != std::string::npos && t.find("nameserver 2001:db8::53\n") != std::string::npos);
  CHECK(t.find("not-an-ip") == std::string::npos && t.find("6.6.6.6") == std::string::npos);  // newline injection rejected
  CHECK(t.find("search corp.example\n") != std::string::npos && t.find("evil") == std::string::npos && t.find("bad host") == std::string::npos);
  CHECK(resolvConf({}, {}).find("nameserver") == std::string::npos);
}

struct FakeLinks : ILinkProvider {
  LinkInfo w, c;
  LinkInfo wifi() override { return w; }
  LinkInfo cellular() override { return c; }
};

static void test_network_manager() {
  FakeLinks l;
  Logger log;
  NetworkManager n(l, log);
  CHECK(n.active() == LinkKind::None && n.linkDns().empty());
  l.c.up = true; l.c.dns = {"10.64.0.1"};
  CHECK(n.active() == LinkKind::Cellular && n.linkDns() == std::vector<std::string>{"10.64.0.1"});
  l.w.up = true; l.w.dns = {"192.168.1.1"};
  CHECK(n.active() == LinkKind::WiFi && n.linkDns()[0] == "192.168.1.1");  // Wi-Fi preferred
  n.prefs.preferWifi = false;
  CHECK(n.active() == LinkKind::Cellular);
  n.prefs.allowCellular = false;
  CHECK(n.active() == LinkKind::WiFi);                                      // Wi-Fi only
  l.w.up = false;
  CHECK(n.active() == LinkKind::None);                                      // cellular forbidden, wifi down

  // change events
  Logger log2;
  FakeLinks m;
  NetworkManager n2(m, log2);
  std::vector<std::pair<LinkKind, LinkKind>> ev;
  n2.addListener([&](LinkKind a, LinkKind b) { ev.push_back({a, b}); });
  CHECK(!n2.poll() && ev.empty());                  // the first poll only records the starting state
  m.w.up = true;
  CHECK(n2.poll() && ev.size() == 1 && ev[0].first == LinkKind::None && ev[0].second == LinkKind::WiFi);
  CHECK(!n2.poll() && ev.size() == 1);              // nothing changed
  m.c.up = true;
  CHECK(!n2.poll());                                // a second link appearing does not change the active one
  m.w.up = false;
  CHECK(n2.poll() && ev.back().second == LinkKind::Cellular);
  m.c.up = false;
  CHECK(n2.poll() && ev.back().second == LinkKind::None && ev.size() == 3);

  // DNS policy
  m.w.up = true; m.w.dns = {"192.168.1.1"};
  CHECK(n2.effectiveDns(false, {"10.8.0.1"}) == std::vector<std::string>{"192.168.1.1"});   // VPN not verified: ordinary DNS
  CHECK(n2.effectiveDns(true, {"10.8.0.1"}) == std::vector<std::string>{"10.8.0.1"});       // verified VPN: only its DNS
  CHECK(n2.effectiveDns(true, {}) == std::vector<std::string>{"192.168.1.1"});               // VPN without DNS: falls back
  n2.prefs.customDns = {"9.9.9.9"};
  CHECK(n2.effectiveDns(false, {}) == std::vector<std::string>{"9.9.9.9"});                  // user override beats link DNS
  CHECK(n2.effectiveDns(true, {"10.8.0.1"}) == std::vector<std::string>{"10.8.0.1"});       // ...but never beats a verified VPN
}

static void test_connectivity_monitor() {
  FakeHttp h;
  CHECK(ConnectivityMonitor(h, "").check() == Internet::Unknown && h.seen.empty());  // no default URL: nothing is contacted
  ConnectivityMonitor m(h, "http://probe.example/generate_204");
  h.script.push_back({204, "", ""});
  CHECK(m.check() == Internet::Online && h.seen[0].url == "http://probe.example/generate_204");
  h.script.push_back({200, "<html>login</html>", ""});
  CHECK(m.check() == Internet::CaptivePortal);
  h.script.push_back({302, "", ""});
  CHECK(m.check() == Internet::CaptivePortal);
  h.script.push_back({0, "", "timeout"});
  CHECK(m.check() == Internet::Offline);
  h.script.push_back({500, "", ""});
  CHECK(m.check() == Internet::Unknown);
}

// ---- VPN store ----
static void test_vpn_store() {
  MemStorage s;
  ObfuscatingBox box;
  FakeRandom rng;
  Logger log;
  VpnStore st(s, box, rng, log);
  CHECK(st.load() && st.profiles().empty());
  VpnImportResult r = st.import("  Work VPN  ", wgText());
  CHECK(r.ok && !r.id.empty() && st.profiles().size() == 1 && st.profiles()[0].name == "Work VPN" && st.profiles()[0].secretsOk);

  // no key material in any stored value
  std::string all;
  for (auto& name : s.list("vpn/")) { std::string v; s.read("vpn/" + name, v); all += v; }
  CHECK(!all.empty() && all.find(wgKey('a')) == std::string::npos && all.find(wgKey('c')) == std::string::npos);
  std::string meta;
  s.read("vpn/profiles.json", meta);
  CHECK(meta.find("vpn.example.org") != std::string::npos && meta.find(wgKey('b')) != std::string::npos);  // public data is fine
  CHECK(s.exists("vpn/" + r.id + ".sec"));

  // reload restores everything including the sealed keys
  Logger log2;
  VpnStore st2(s, box, rng, log2);
  CHECK(st2.load() && st2.profiles().size() == 1);
  const VpnProfile& p = st2.profiles()[0];
  CHECK(p.secretsOk && p.cfg.privateKey == wgKey('a') && p.cfg.peers[0].presharedKey == wgKey('c') && p.cfg.peers[0].endpointHost == "vpn.example.org" &&
        p.cfg.peers[0].keepalive == 25 && p.cfg.mtu == 1380 && p.cfg.addresses.size() == 2 && p.cfg.dns[0] == "10.8.0.1" && p.autoReconnect && !p.killSwitch);

  // flags
  VpnProfile edit = p;
  edit.killSwitch = true;
  edit.autoReconnect = false;
  edit.name = "Renamed";
  edit.cfg.privateKey = "ignored: update() never changes tunnel configuration";
  CHECK(st2.update(edit) && st2.find(p.id)->killSwitch && !st2.find(p.id)->autoReconnect && st2.find(p.id)->cfg.privateKey == wgKey('a'));
  Logger log3;
  VpnStore st3(s, box, rng, log3);
  st3.load();
  CHECK(st3.find(p.id)->killSwitch && st3.find(p.id)->name == "Renamed");

  // refused imports leave nothing behind
  size_t keys = s.list("vpn/").size();
  CHECK(!st.import("x", "[Interface]\nPrivateKey = bad\n").ok && !st.import("x", wgText("0.0.0.0/0", "", "PostUp = reboot\n")).ok);
  CHECK(s.list("vpn/").size() == keys && st.profiles().size() == 1);
  CHECK(st.import("", wgText()).ok && st.profiles()[1].name == "VPN 2");  // default name
  NoSecretBox none;
  Logger log4;
  VpnStore st4(s, none, rng, log4);
  VpnImportResult nr = st4.import("nope", wgText());
  CHECK(!nr.ok && nr.error.find("secure storage") != std::string::npos);  // fails closed: no plaintext VPN key, ever
  for (int i = 0; i < 20; i++) st.import("n", wgText());
  CHECK(st.profiles().size() == 16);  // limit

  // unreadable key: the profile is kept but cannot connect
  s.write("vpn/" + r.id + ".sec", "garbage");
  Logger log5;
  VpnStore st5(s, box, rng, log5);
  st5.load();
  CHECK(st5.find(r.id) && !st5.find(r.id)->secretsOk && st5.find(r.id)->cfg.privateKey.empty());

  // removal wipes the key
  CHECK(st.remove(r.id) && !s.exists("vpn/" + r.id + ".sec") && !st.find(r.id) && !st.remove("nope"));
  // corrupt metadata is ignored, not trusted
  s.write("vpn/profiles.json", "{not json");
  Logger log6;
  VpnStore st6(s, box, rng, log6);
  CHECK(!st6.load() && st6.profiles().empty());
}

// ---- VPN manager ----
struct FakeBackend : IVpnBackend {
  bool sim = false, failKill = false, failUp = false, upFlag = true, routed = true;
  TunnelStats ts;
  std::vector<std::string> calls, probes, lastIps;
  KillSwitchSpec lastSpec;
  const char* name() const override { return "fake"; }
  bool simulated() const override { return sim; }
  bool setKillSwitch(const KillSwitchSpec* spec, std::string& err) override {
    calls.push_back(spec ? "ks_on" : "ks_off");
    if (spec) lastSpec = *spec;
    if (failKill && spec) { err = "nft missing"; return false; }
    return true;
  }
  bool bringUp(const WgConfig&, const std::vector<std::string>& ips, std::string& err) override {
    calls.push_back("up");
    lastIps = ips;
    if (failUp) { err = "ip link add failed"; return false; }
    return true;
  }
  void tearDown() override { calls.push_back("down"); }
  bool stats(TunnelStats& o) override { o = ts; o.up = upFlag; return true; }
  bool routesThroughTunnel(const std::string& probe) override { probes.push_back(probe); return routed; }
  int count(const std::string& c) const { return int(std::count(calls.begin(), calls.end(), c)); }
};

struct FakeResolver : IResolver {
  std::map<std::string, std::string> table{{"vpn.example.org", "203.0.113.7"}};
  bool resolve(const std::string& host, IpAddr& out) override {
    if (parseIp(host, out)) return true;
    auto it = table.find(host);
    return it != table.end() && parseIp(it->second, out);
  }
};

struct VpnRig {
  MemStorage store;
  ObfuscatingBox box;
  FakeRandom rng;
  Logger log{500};
  FakeLinks links;
  FakeBackend be;
  FakeResolver res;
  long long now = 1700000000000LL;
  std::vector<std::string> logged;
  std::unique_ptr<VpnStore> vs;
  std::unique_ptr<NetworkManager> net;
  std::unique_ptr<VpnManager> vpn;
  std::string id;
  explicit VpnRig(const std::string& cfgText = wgText(), bool killSwitch = false, bool autoReconnect = true) {
    log.min = Level::Debug;
    log.sink = [this](const LogEntry& e) { logged.push_back(e.tag + ": " + e.msg); };
    links.w.up = true;
    links.w.dns = {"192.168.1.1"};
    vs = std::make_unique<VpnStore>(store, box, rng, log);
    net = std::make_unique<NetworkManager>(links, log);
    vpn = std::make_unique<VpnManager>(*vs, be, res, *net, log, [this] { return now; });
    net->addListener([this](LinkKind, LinkKind) { vpn->onNetworkChange(); });
    net->poll();  // prime
    id = vs->import("test", cfgText).id;
    VpnProfile p = *vs->find(id);
    p.killSwitch = killSwitch;
    p.autoReconnect = autoReconnect;
    vs->update(p);
  }
  void handshake() { be.ts.lastHandshakeMs = now - 500; }
  void advance(long long ms) { now += ms; }
  void tick(long long ms = 1000) { advance(ms); vpn->tick(); }
  void tickHs(long long ms = 1000) { advance(ms); handshake(); vpn->tick(); }  // time passes, the server answers, the manager looks
  void connectAndVerify() {
    CHECK(vpn->connect(id));
    handshake();
    tick();
    CHECK(vpn->status().state == VpnState::Connected);
  }
};

static void test_vpn_connect_verifies_before_claiming_protection() {
  VpnRig t;
  CHECK(t.vpn->status().state == VpnState::Disconnected && !t.vpn->status().protectionVerified);
  CHECK(t.vpn->connect(t.id) && t.vpn->status().state == VpnState::Connecting);
  CHECK(t.be.calls == std::vector<std::string>{"up"} && t.be.lastIps == std::vector<std::string>{"203.0.113.7"});  // resolved before dialling
  t.tick();                                                  // tunnel exists, but no handshake yet
  CHECK(t.vpn->status().state == VpnState::Connecting && !t.vpn->status().protectionVerified && t.vpn->dnsServers().empty());
  t.be.routed = false;                                       // handshake done but the OS does NOT send traffic through it
  t.tickHs(5000);                                            // (routing is re-checked every 5 s, not every tick)
  CHECK(t.vpn->status().state == VpnState::Connecting && !t.vpn->status().protectionVerified && t.vpn->dnsServers().empty());
  t.be.routed = true;
  t.tickHs(5000);
  CHECK(t.vpn->status().state == VpnState::Connected && t.vpn->status().protectionVerified && t.vpn->status().error.empty());
  CHECK(t.vpn->status().connectedSinceMs == t.now && t.vpn->status().attempt == 0);
  CHECK(t.vpn->dnsServers() == std::vector<std::string>{"10.8.0.1"});
  CHECK(t.be.probes.back() == "192.0.2.1");                  // full tunnel: a documentation address, only a table lookup

  t.be.ts.rxBytes = 1000; t.be.ts.txBytes = 500;             // statistics
  t.tickHs(1000);
  CHECK(t.vpn->status().rxBytes == 1000 && t.vpn->status().txBytes == 500 && t.vpn->status().lastHandshakeMs == t.now - 500);

  // split tunnel probes an address inside the first allowed range
  VpnRig s(wgText("10.50.0.0/16", "", ""));
  s.vpn->connect(s.id);
  s.handshake();
  s.tick();
  CHECK(s.be.probes.back() == "10.50.0.0" && s.vpn->status().state == VpnState::Connected);

  t.vpn->disconnect();
  CHECK(t.vpn->status().state == VpnState::Disconnected && !t.vpn->status().protectionVerified && t.be.calls.back() == "down" && t.vpn->dnsServers().empty());
  CHECK(t.be.count("ks_off") == 0);  // no kill switch was ever installed for this profile
}

static void test_vpn_simulated_backend_is_flagged() {
  VpnRig t;
  t.be.sim = true;
  t.vpn->connect(t.id);
  CHECK(t.vpn->status().simulated);
  t.vpn->disconnect();
  CHECK(t.vpn->status().simulated);
}

static void test_vpn_connect_failures() {
  VpnRig t;
  CHECK(!t.vpn->connect("nope") && t.vpn->status().state == VpnState::Error && t.vpn->status().error == "no such profile");
  VpnRig u;
  u.store.write("vpn/" + u.id + ".sec", "garbage");
  Logger l;
  VpnStore broken(u.store, u.box, u.rng, l);
  broken.load();
  VpnManager vm(broken, u.be, u.res, *u.net, l, [&] { return u.now; });
  CHECK(!vm.connect(u.id) && vm.status().error.find("import the profile again") != std::string::npos && u.be.calls.empty());
  VpnRig n(std::string("[Interface]\nPrivateKey=") + wgKey('a') + "\nAddress=10.0.0.2/32\n[Peer]\nPublicKey=" + wgKey('b') + "\nAllowedIPs=10.0.0.0/24\n");
  CHECK(!n.vpn->connect(n.id) && n.vpn->status().error == "profile has no server endpoint" && n.be.calls.empty());
  VpnRig f;
  f.be.failUp = true;
  CHECK(!f.vpn->connect(f.id) && f.vpn->status().state == VpnState::Reconnecting && f.vpn->status().error.find("tunnel setup failed") == 0);
  VpnRig d;
  d.res.table.clear();
  CHECK(!d.vpn->connect(d.id) && d.vpn->status().error == "could not resolve the server name" && d.be.calls.empty());
}

static void test_vpn_handshake_timeout_and_backoff() {
  VpnRig t;
  t.vpn->connect(t.id);
  t.tick(14000);                                    // inside the 15 s window: still waiting
  CHECK(t.vpn->status().state == VpnState::Connecting);
  t.tick(2000);                                     // past it
  CHECK(t.vpn->status().state == VpnState::Reconnecting && t.vpn->status().attempt == 1 && t.vpn->status().error.find("no handshake") == 0);
  CHECK(t.be.count("down") == 1 && !t.vpn->status().protectionVerified);
  long long expect[] = {2000, 4000, 8000, 16000, 32000, 60000, 60000};
  for (long long e : expect) {
    long long wait = t.vpn->status().nextAttemptMs - t.now;
    t.tick(wait - 1);                                // one millisecond early: no new attempt
    CHECK(t.vpn->status().state == VpnState::Reconnecting);
    int ups = t.be.count("up");
    t.tick(1);                                       // due: dials again
    CHECK(t.be.count("up") == ups + 1 && t.vpn->status().state == VpnState::Connecting);
    t.tick(15001);                                   // and fails again
    CHECK(t.vpn->status().state == VpnState::Reconnecting);
    CHECK(t.vpn->status().nextAttemptMs - t.now == e);
  }
  // gives up eventually
  for (int i = 0; i < 40 && t.vpn->status().state != VpnState::Error; i++) {
    t.tick(t.vpn->status().nextAttemptMs - t.now + 1);
    t.tick(15001);
  }
  CHECK(t.vpn->status().state == VpnState::Error && t.vpn->status().attempt == VpnManager::kMaxAttempts);
  // a late handshake after a retry still connects
  VpnRig r;
  r.vpn->connect(r.id);
  r.tick(16000);
  r.tick(r.vpn->status().nextAttemptMs - r.now + 1);
  r.handshake();
  r.tick();
  CHECK(r.vpn->status().state == VpnState::Connected && r.vpn->status().attempt == 0);
}

static void test_vpn_without_auto_reconnect_stops_in_error() {
  VpnRig t(wgText(), false, false);
  t.vpn->connect(t.id);
  t.tick(16000);
  CHECK(t.vpn->status().state == VpnState::Error && t.be.count("up") == 1);
  t.tick(120000);
  CHECK(t.be.count("up") == 1);                    // no silent retries
  CHECK(t.vpn->connect(t.id) && t.be.count("up") == 2);  // an explicit retry works
}

static void test_vpn_kill_switch_behaviour() {
  VpnRig t(wgText(), true, true);
  CHECK(t.vpn->connect(t.id));
  CHECK((t.be.calls == std::vector<std::string>{"ks_on", "up"}));                     // the block goes up BEFORE the tunnel
  CHECK(t.vpn->status().trafficBlocked && t.be.lastSpec.endpoints.size() == 1 && formatIp(t.be.lastSpec.endpoints[0].first) == "203.0.113.7" &&
        t.be.lastSpec.endpoints[0].second == 51820);
  t.handshake();
  t.tick();
  CHECK(t.vpn->status().state == VpnState::Connected);

  t.be.upFlag = false;                                                                // tunnel dies unexpectedly
  t.tick();
  CHECK(t.vpn->status().state == VpnState::Reconnecting && t.vpn->status().trafficBlocked && !t.vpn->status().protectionVerified);
  CHECK(t.be.count("ks_off") == 0);                                                   // traffic stays blocked while retrying: no leak window
  t.be.upFlag = true;
  t.tick(t.vpn->status().nextAttemptMs - t.now);
  t.handshake();
  t.tick();
  CHECK(t.vpn->status().state == VpnState::Connected && t.be.count("ks_on") == 2 && t.be.count("ks_off") == 0);

  t.vpn->disconnect();                                                                // only the user can lift the block
  CHECK(t.be.calls.back() == "ks_off" && !t.vpn->status().trafficBlocked && t.vpn->status().state == VpnState::Disconnected);

  VpnRig f(wgText(), true, false);                                                    // kill switch + no auto-reconnect: block persists in Error
  f.vpn->connect(f.id);
  f.tick(16000);
  CHECK(f.vpn->status().state == VpnState::Error && f.vpn->status().trafficBlocked && f.be.count("ks_off") == 0);
  f.vpn->disconnect();
  CHECK(f.be.count("ks_off") == 1);

  VpnRig k(wgText(), true, true);                                                     // kill switch cannot be installed: tunnel is NOT started
  k.be.failKill = true;
  CHECK(!k.vpn->connect(k.id) && k.be.count("up") == 0 && k.vpn->status().state == VpnState::Error && k.vpn->status().error.find("kill switch could not be installed") == 0);
  CHECK(!k.vpn->status().trafficBlocked);

  VpnRig lan(wgText(), false, true);                                                  // LAN exception travels into the ruleset spec
  VpnProfile p = *lan.vs->find(lan.id);
  p.killSwitch = true;
  p.allowLanWhenBlocked = true;
  lan.vs->update(p);
  lan.vpn->connect(lan.id);
  CHECK(lan.be.lastSpec.allowLan);
}

static void test_vpn_network_loss_and_return() {
  VpnRig t(wgText(), true, true);
  t.connectAndVerify();
  t.links.w.up = false;
  t.links.c.up = true;                               // Wi-Fi lost, cellular available: the tunnel must follow
  t.net->poll();
  CHECK(t.vpn->status().state == VpnState::Reconnecting && t.vpn->status().error == "network changed" && !t.vpn->status().protectionVerified);
  CHECK(t.vpn->status().nextAttemptMs == t.now);     // immediate, not after a backoff
  int ups = t.be.count("up");
  t.tick(0);
  CHECK(t.be.count("up") == ups + 1 && t.vpn->status().state == VpnState::Connecting);
  t.handshake();
  t.tick();
  CHECK(t.vpn->status().state == VpnState::Connected);

  t.links.c.up = false;                              // everything is gone
  t.net->poll();
  CHECK(t.vpn->status().state == VpnState::Reconnecting && t.vpn->status().trafficBlocked);
  t.tick(0);
  CHECK(t.vpn->status().state == VpnState::Reconnecting && t.vpn->status().error == "no network connection");
  ups = t.be.count("up");
  t.tick(30000);
  CHECK(t.be.count("up") == ups);                    // no tunnel attempts while offline
  t.links.w.up = true;                               // Wi-Fi comes back
  t.net->poll();
  t.tick(0);
  CHECK(t.vpn->status().state == VpnState::Connecting && t.be.count("up") == ups + 1);
  t.handshake();
  t.tick();
  CHECK(t.vpn->status().state == VpnState::Connected && t.vpn->status().protectionVerified && t.vpn->status().attempt == 0);
  CHECK(t.be.count("ks_off") == 0);                  // the block never lifted while the network was away

  VpnRig d;                                          // network change while disconnected: nothing happens
  d.links.w.up = false;
  d.net->poll();
  CHECK(d.vpn->status().state == VpnState::Disconnected && d.be.calls.empty());
  VpnRig n(wgText(), false, false);                  // no auto-reconnect: a network change is an Error, not a retry
  n.connectAndVerify();
  n.links.w.up = false;
  n.links.c.up = true;
  n.net->poll();
  CHECK(n.vpn->status().state == VpnState::Error && n.vpn->status().error == "network changed");
  VpnRig o;                                          // connecting with no network at all
  o.links.w.up = false;
  CHECK(!o.vpn->connect(o.id) && o.vpn->status().error == "no network connection" && o.be.calls.empty());
}

static void test_vpn_route_and_handshake_health() {
  VpnRig t;
  t.connectAndVerify();
  t.be.routed = false;                               // routing rule vanished (another process changed it)
  t.handshake();
  t.tick(1000);
  CHECK(t.vpn->status().state == VpnState::Connected);   // routing is only re-checked every 5 s
  t.handshake();
  t.tick(5000);
  CHECK(t.vpn->status().state == VpnState::Reconnecting && t.vpn->status().error == "traffic is not routed through the tunnel" && !t.vpn->status().protectionVerified);

  VpnRig a(wgText("0.0.0.0/0, ::/0", "", ""));       // no keepalive: an idle tunnel is "unverified", not "broken"
  a.connectAndVerify();
  a.tick(200000);
  CHECK(a.vpn->status().state == VpnState::Connected && !a.vpn->status().protectionVerified && a.vpn->status().error == "no recent handshake");
  CHECK(a.vpn->dnsServers().empty());                // and the VPN DNS is no longer enforced as "verified"
  a.handshake();
  a.tick();
  CHECK(a.vpn->status().protectionVerified && a.vpn->status().error.empty());  // a fresh handshake restores it

  VpnRig k;                                          // keepalive configured but the server went silent: reconnect
  k.connectAndVerify();
  k.tick(200000);
  CHECK(k.vpn->status().state == VpnState::Connected && !k.vpn->status().protectionVerified);
  k.tick(20000);
  CHECK(k.vpn->status().state == VpnState::Reconnecting && k.vpn->status().error == "lost contact with the server");

  VpnRig g;                                          // interface disappeared
  g.connectAndVerify();
  g.be.upFlag = false;
  g.tick();
  CHECK(g.vpn->status().state == VpnState::Reconnecting && g.vpn->status().error == "tunnel interface is gone");
}

static void test_vpn_endpoint_resolution_cache() {
  VpnRig t(wgText(), true, true);
  t.connectAndVerify();
  t.res.table.clear();                               // our own kill switch now blocks DNS: the old answer must be reused
  t.be.upFlag = false;
  t.tick();
  t.be.upFlag = true;
  t.tick(t.vpn->status().nextAttemptMs - t.now);
  CHECK(t.vpn->status().state == VpnState::Connecting && t.be.lastIps == std::vector<std::string>{"203.0.113.7"});
}

static void test_vpn_logs_never_contain_secrets() {
  VpnRig t(wgText(), true, true);
  t.connectAndVerify();
  t.be.upFlag = false;
  t.tick();
  t.vpn->disconnect();
  CHECK(!t.logged.empty());
  for (auto& line : t.logged)
    CHECK(line.find(wgKey('a')) == std::string::npos && line.find(wgKey('c')) == std::string::npos && line.find("PrivateKey") == std::string::npos);
}

// ---- real backend, driven through a FAKE command runner ----
struct FakeRunner : ICommandRunner {
  std::vector<Command> ran;
  std::function<CommandResult(const Command&)> handler;
  CommandResult run(const Command& c) override {
    ran.push_back(c);
    return handler ? handler(c) : CommandResult{0, "", ""};
  }
  bool did(const std::string& d) const { for (auto& c : ran) if (c.describe() == d) return true; return false; }
};

static void test_command_backend() {
  WgConfig cfg = parseWgConfig(wgText()).cfg;
  FakeRunner r;
  CommandVpnBackend be(r);
  std::string err;
  CHECK(!be.simulated());
  CHECK(be.bringUp(cfg, {"203.0.113.7"}, err) && r.ran.size() == 12 && r.ran[0].describe() == "ip link add dev wg0 type wireguard" && !r.ran[1].stdinData.empty());

  // failure part-way: report which step, clean up what exists
  FakeRunner f;
  f.handler = [](const Command& c) { return c.describe().rfind("ip -4 address", 0) == 0 ? CommandResult{2, "", "RTNETLINK answers: File exists\nmore"} : CommandResult{0, "", ""}; };
  CommandVpnBackend be2(f);
  CHECK(!be2.bringUp(cfg, {"203.0.113.7"}, err) && err == "'ip -4' failed (exit 2): RTNETLINK answers: File exists");
  CHECK(f.did("ip link del dev wg0") && f.ran.size() < 12 + 8);

  // `wg setconf` echoes the offending line, which contains the private key: it must never reach the error text
  FakeRunner w;
  w.handler = [&](const Command& c) {
    return c.argv.size() > 1 && c.argv[1] == "setconf" ? CommandResult{1, "", "Line unrecognized: `PrivateKey=" + wgKey('a') + "'"} : CommandResult{0, "", ""};
  };
  CommandVpnBackend be3(w);
  CHECK(!be3.bringUp(cfg, {"203.0.113.7"}, err) && err.find(wgKey('a')) == std::string::npos && err == "'wg setconf' failed (exit 1)");
  for (auto& c : w.ran) CHECK(c.describe().find(wgKey('a')) == std::string::npos);

  CommandVpnBackend bad(r, "wg0; reboot");
  CHECK(!bad.bringUp(cfg, {}, err) && err == "invalid interface name");

  // statistics come from the public-key-only subcommands
  FakeRunner s;
  s.handler = [](const Command& c) {
    if (c.describe() == "wg show wg0 latest-handshakes") return CommandResult{0, "PUB1\t1700000000\nPUB2\t1700000100\n", ""};
    if (c.describe() == "wg show wg0 transfer") return CommandResult{0, "PUB1\t100\t200\nPUB2\t1\t2\n", ""};
    return CommandResult{0, "", ""};
  };
  CommandVpnBackend be4(s);
  TunnelStats ts;
  CHECK(be4.stats(ts) && ts.up && ts.lastHandshakeMs == 1700000100000LL && ts.rxBytes == 101 && ts.txBytes == 202);
  for (auto& c : s.ran) CHECK(c.describe().find("dump") == std::string::npos);  // `wg show dump` would print the private key
  FakeRunner none;
  none.handler = [](const Command&) { return CommandResult{1, "", "Unable to access interface"}; };
  CommandVpnBackend be5(none);
  CHECK(be5.stats(ts) && !ts.up && ts.lastHandshakeMs == 0);
  FakeRunner nohs;
  nohs.handler = [](const Command& c) { return c.describe().find("latest") != std::string::npos ? CommandResult{0, "PUB1\t0\n", ""} : CommandResult{0, "PUB1\t0\t0\n", ""}; };
  CommandVpnBackend be6(nohs);
  CHECK(be6.stats(ts) && ts.up && ts.lastHandshakeMs == 0);  // up, but no handshake yet

  // routing check parses `ip route get`
  FakeRunner rt;
  std::string out;
  rt.handler = [&](const Command&) { return CommandResult{0, out, ""}; };
  CommandVpnBackend be7(rt);
  out = "192.0.2.1 dev wg0 table 51820 src 10.8.0.2 uid 0\n    cache\n";
  CHECK(be7.routesThroughTunnel("192.0.2.1") && rt.ran.back().describe() == "ip -4 route get 192.0.2.1");
  out = "192.0.2.1 via 192.168.1.1 dev wlan0 src 192.168.1.20 uid 0\n";
  CHECK(!be7.routesThroughTunnel("192.0.2.1"));
  out = "192.0.2.1 dev wg01 src 10.8.0.2\n";
  CHECK(!be7.routesThroughTunnel("192.0.2.1"));        // wg01 is not wg0
  out = "2001:db8::1 from :: dev wg0 table 51820 src fd00::2 metric 1024\n";
  CHECK(be7.routesThroughTunnel("2001:db8::1") && rt.ran.back().describe() == "ip -6 route get 2001:db8::1");
  out = "192.0.2.1 dev wg0";
  CHECK(be7.routesThroughTunnel("192.0.2.1"));         // end of output counts as a boundary
  CHECK(!be7.routesThroughTunnel("not an ip"));

  // kill switch goes through nft with the ruleset on stdin
  FakeRunner ks;
  CommandVpnBackend be8(ks);
  KillSwitchSpec spec;
  IpAddr ip;
  parseIp("203.0.113.7", ip);
  spec.endpoints = {{ip, 51820}};
  spec.iface = "something-else";  // the backend's own interface name wins
  CHECK(be8.setKillSwitch(&spec, err) && ks.ran.size() == 2 && ks.ran[1].describe() == "nft -f /dev/stdin" &&
        ks.ran[1].stdinData.find("oifname \"wg0\" accept") != std::string::npos && ks.ran[1].stdinData.find("something-else") == std::string::npos);
  CHECK(be8.setKillSwitch(nullptr, err) && ks.ran.back().describe() == "nft delete table inet faios_killswitch");
  FakeRunner kf;
  kf.handler = [](const Command& c) { return c.ignoreFailure ? CommandResult{1, "", ""} : CommandResult{1, "", "nft: command not found"}; };
  CommandVpnBackend be9(kf);
  CHECK(!be9.setKillSwitch(&spec, err) && err.find("nft: command not found") != std::string::npos);
  KillSwitchSpec empty;
  CHECK(!be9.setKillSwitch(&empty, err) && err == "invalid kill switch specification");

  // teardown removes rules and link, and tolerates failures
  FakeRunner td;
  td.handler = [](const Command&) { return CommandResult{1, "", "no such device"}; };
  CommandVpnBackend be10(td);
  CHECK(!be10.bringUp(cfg, {"203.0.113.7"}, err));  // fails at the very first command...
  CHECK(td.did("ip link del dev wg0") && td.did("ip -4 rule del not fwmark 51820 table 51820"));  // ...and still cleans up, ignoring those failures
}

// ---- VPN screens ----
static void test_vpn_screens() {
  VpnRig t;
  auto* m = boot();
  m->vpn = t.vpn.get();
  m->vpnStore = t.vs.get();
  m->net = t.net.get();
  m->files = &t.store;
  launch(*m, "VPN");
  CHECK(title(*m) == "VPN");
  auto v = m->top()->view();
  CHECK(v.rows.size() == 3 && v.rows[0].text == "Not connected" && v.rows[1].text == "test" && v.rows[1].sub == "Full tunnel" && v.rows[2].text == "Import profile");
  CHECK(v.keys.center.empty() && m->status().vpnProtected == false);
  press(*m, Key::Down);
  CHECK(m->top()->view().keys.center == "Connect" && m->top()->view().keys.left == "Options");
  press(*m, Key::Ok);                                                  // connect
  v = m->top()->view();
  CHECK(v.rows[0].text == "Connecting..." && v.rows[1].trail == "On" && m->top()->view().keys.center == "Disconnect");
  CHECK(!m->status().vpnProtected);                                    // nothing verified yet: the status bar must not say protected
  t.tickHs(5000);
  v = m->top()->view();
  CHECK(v.rows[0].text == "Protected" && v.rows[0].sub.rfind("rx ", 0) == 0 && m->status().vpnProtected && !m->status().vpnSimulated);
  press(*m, Key::Ok);                                                  // same row again = disconnect
  CHECK(m->top()->view().rows[0].text == "Not connected" && !m->status().vpnProtected);

  // a simulated backend can never produce the word "Protected"
  t.be.sim = true;
  press(*m, Key::Ok);
  t.tickHs(5000);
  v = m->top()->view();
  CHECK(v.rows[0].text == "Simulated tunnel" && v.rows[0].sub == "SIMULATED (not real)" && m->status().vpnSimulated);
  press(*m, Key::Up);
  press(*m, Key::Ok);                                                  // status row disconnects
  CHECK(m->top()->view().rows[0].text == "Not connected");
  t.be.sim = false;

  // failure and kill switch are visible
  VpnProfile p = *t.vs->find(t.id);
  p.killSwitch = true;
  t.vs->update(p);
  t.be.upFlag = false;
  press(*m, Key::Down);
  press(*m, Key::Ok);
  t.tick(5000);                                                        // the interface is not there: failure, and the kill switch keeps blocking
  v = m->top()->view();
  CHECK(v.rows[0].text == "Reconnecting..." && v.rows[0].trail == "Blocked" && v.rows[0].tone == 1 && m->status().vpnBlocked);
  t.be.upFlag = true;
  press(*m, Key::Up);
  press(*m, Key::Ok);                                                  // disconnect lifts the block
  CHECK(!m->status().vpnBlocked && m->top()->view().rows[0].text == "Not connected");

  // per-profile options
  press(*m, Key::Down);
  press(*m, Key::Soft1);
  CHECK(title(*m) == "test");
  v = m->top()->view();
  CHECK(v.rows.size() == 5 && v.rows[0].sub == "vpn.example.org:51820" && v.rows[1].toggle == 1 && v.rows[2].toggle == 1 && v.rows[3].toggle == 0);
  press(*m, Key::Down);
  press(*m, Key::Ok);                                                  // kill switch off
  CHECK(!t.vs->find(t.id)->killSwitch && m->top()->view().rows[1].toggle == 0);
  press(*m, Key::Down);
  press(*m, Key::Ok);                                                  // auto-reconnect off
  CHECK(!t.vs->find(t.id)->autoReconnect);
  press(*m, Key::Down); press(*m, Key::Down);
  press(*m, Key::Ok);                                                  // delete -> confirmation
  CHECK(title(*m) == "Delete profile?");
  press(*m, Key::Down);
  press(*m, Key::Ok);                                                  // cancel
  CHECK(title(*m) == "test" && t.vs->profiles().size() == 1);
  press(*m, Key::Ok);                                                  // ask again...
  press(*m, Key::Ok);                                                  // ...and confirm
  CHECK(title(*m) == "VPN" && t.vs->profiles().empty() && m->top()->view().rows.size() == 2);
  bool keyLeft = false;
  for (auto& n : t.store.list("vpn/")) keyLeft |= n.size() > 4 && n.compare(n.size() - 4, 4, ".sec") == 0;
  CHECK(!keyLeft);                                                     // deleting a profile deletes its key
  delete m;
}

static void test_vpn_import_screen() {
  VpnRig t;
  auto* m = boot();
  m->vpn = t.vpn.get();
  m->vpnStore = t.vs.get();
  m->net = t.net.get();
  m->files = &t.store;
  launch(*m, "VPN");
  press(*m, Key::Down); press(*m, Key::Down);
  press(*m, Key::Ok);                                                  // Import profile
  CHECK(title(*m) == "Import VPN" && m->top()->view().rows[0].text == "No .conf files");
  press(*m, Key::Back);
  t.store.write("import/work.conf", wgText());
  t.store.write("import/readme.txt", "not a config");
  t.store.write("import/broken.conf", "[Interface]\nPrivateKey = nope\n");
  press(*m, Key::Ok);
  auto v = m->top()->view();
  CHECK(v.rows.size() == 2 && v.rows[0].text == "broken.conf" && v.rows[1].text == "work.conf");   // only .conf files, sorted
  press(*m, Key::Ok);                                                  // broken one: refused, file stays, reason shown
  CHECK(title(*m) == "Import VPN" && t.vs->profiles().size() == 1 && t.store.exists("import/broken.conf"));
  CHECK(m->notifications.count() == 1 && m->notifications.list()[0].title == "VPN import failed" && m->notifications.list()[0].body.find("PrivateKey") != std::string::npos);
  CHECK(m->notifications.list()[0].body.find("nope") == std::string::npos);
  press(*m, Key::Down);
  press(*m, Key::Ok);                                                  // good one
  CHECK(title(*m) == "VPN" && t.vs->profiles().size() == 2 && t.vs->profiles()[1].name == "work");
  CHECK(!t.store.exists("import/work.conf") && t.store.exists("import/readme.txt"));  // the plaintext key is not left on disk
  CHECK(m->notifications.list().back().body.find("deleted") != std::string::npos);
  CHECK(m->top()->view().rows.size() == 4);
  delete m;

  auto* n = boot();                                                    // no service
  launch(*n, "VPN");
  CHECK(title(*n) == "VPN" && n->top()->view().rows[0].text == "VPN service unavailable");
  press(*n, Key::Ok);
  CHECK(title(*n) == "VPN");
  delete n;
}

static void test_vpn_tick_runs_through_app_manager() {
  VpnRig t;
  AppManager m;
  m.push(makeHome(m));
  m.vpn = t.vpn.get();
  m.net = t.net.get();
  t.vpn->connect(t.id);
  CHECK(m.status().vpnProtected == false);
  t.handshake();
  t.now += 1000;
  m.tick(std::time(nullptr));                                          // AppManager drives the manager (and polls the link)
  CHECK(t.vpn->status().state == VpnState::Connected && m.status().vpnProtected);
  t.links.w.up = false;                                                // link change reaches the VPN through the listener
  m.tick(std::time(nullptr) + 60);
  CHECK(t.vpn->status().state == VpnState::Reconnecting && !m.status().vpnProtected);
}
