#include "wg_commands.h"

namespace fai {

std::string wgSetconf(const WgConfig& c, const std::vector<std::string>& endpointIps) {
  std::string o = "[Interface]\nPrivateKey = " + c.privateKey + "\n";
  if (c.listenPort) o += "ListenPort = " + std::to_string(c.listenPort) + "\n";
  for (size_t i = 0; i < c.peers.size(); i++) {
    const WgPeer& p = c.peers[i];
    o += "\n[Peer]\nPublicKey = " + p.publicKey + "\n";
    if (!p.presharedKey.empty()) o += "PresharedKey = " + p.presharedKey + "\n";
    std::string ips;
    for (auto& a : p.allowedIps) ips += (ips.empty() ? "" : ", ") + formatCidr(a);
    o += "AllowedIPs = " + ips + "\n";
    std::string ip = i < endpointIps.size() ? endpointIps[i] : "";
    if (!ip.empty() && p.endpointPort) {
      IpAddr a;
      bool v6 = parseIp(ip, a) && a.v6;
      o += "Endpoint = " + (v6 ? "[" + ip + "]" : ip) + ":" + std::to_string(p.endpointPort) + "\n";
    }
    if (p.keepalive) o += "PersistentKeepalive = " + std::to_string(p.keepalive) + "\n";
  }
  return o;
}

static Command cmd(std::initializer_list<const char*> argv, bool ignore = false) {
  Command c;
  for (auto a : argv) c.argv.push_back(a);
  c.ignoreFailure = ignore;
  return c;
}
static Command cmdv(std::vector<std::string> argv, bool ignore = false) { Command c; c.argv = std::move(argv); c.ignoreFailure = ignore; return c; }

static bool hasDefault(const WgConfig& c, bool v6) {
  for (auto& p : c.peers) for (auto& a : p.allowedIps) if (isDefaultRoute(a) && a.ip.v6 == v6) return true;
  return false;
}

std::vector<Command> tunnelUpCommands(const WgConfig& c, const std::string& iface, int fwmark, const std::vector<std::string>& endpointIps) {
  if (!validIfaceName(iface)) return {};
  std::vector<Command> v;
  std::string table = std::to_string(fwmark), mark = std::to_string(fwmark);
  v.push_back(cmd({"ip", "link", "add", "dev", iface.c_str(), "type", "wireguard"}));
  Command set = cmd({"wg", "setconf", iface.c_str(), "/dev/stdin"});
  set.stdinData = wgSetconf(c, endpointIps);  // the only place the private key is used
  set.secretStdin = true;
  v.push_back(set);
  for (auto& a : c.addresses) v.push_back(cmdv({"ip", a.ip.v6 ? "-6" : "-4", "address", "add", formatCidr(a), "dev", iface}));
  if (c.mtu) v.push_back(cmdv({"ip", "link", "set", "mtu", std::to_string(c.mtu), "up", "dev", iface}));
  else v.push_back(cmdv({"ip", "link", "set", "up", "dev", iface}));

  bool full4 = hasDefault(c, false), full6 = hasDefault(c, true);
  if (full4 || full6) v.push_back(cmdv({"wg", "set", iface, "fwmark", mark}));
  for (bool v6 : {false, true}) {
    const char* fam = v6 ? "-6" : "-4";
    if (v6 ? full6 : full4) {  // full tunnel: everything except WireGuard's own (marked) packets goes through the tunnel
      v.push_back(cmdv({"ip", fam, "route", "add", v6 ? "::/0" : "0.0.0.0/0", "dev", iface, "table", table}));
      v.push_back(cmdv({"ip", fam, "rule", "add", "not", "fwmark", mark, "table", table}));
      v.push_back(cmdv({"ip", fam, "rule", "add", "table", "main", "suppress_prefixlength", "0"}));
    } else {  // split tunnel: only the listed prefixes
      for (auto& p : c.peers)
        for (auto& a : p.allowedIps)
          if (a.ip.v6 == v6) v.push_back(cmdv({"ip", fam, "route", "add", formatCidr(a), "dev", iface}));
    }
  }
  return v;
}

std::vector<Command> tunnelDownCommands(const WgConfig& c, const std::string& iface, int fwmark) {
  if (!validIfaceName(iface)) return {};
  std::vector<Command> v;
  std::string table = std::to_string(fwmark), mark = std::to_string(fwmark);
  for (bool v6 : {true, false}) {
    if (!hasDefault(c, v6)) continue;
    const char* fam = v6 ? "-6" : "-4";
    v.push_back(cmdv({"ip", fam, "rule", "del", "table", "main", "suppress_prefixlength", "0"}, true));
    v.push_back(cmdv({"ip", fam, "rule", "del", "not", "fwmark", mark, "table", table}, true));
    v.push_back(cmdv({"ip", fam, "route", "flush", "table", table}, true));
  }
  v.push_back(cmdv({"ip", "link", "del", "dev", iface}, true));  // also drops the split-tunnel routes bound to it
  return v;
}

std::string killSwitchRuleset(const KillSwitchSpec& s) {
  if (!validIfaceName(s.iface) || s.endpoints.empty()) return "";
  std::string o = "table inet faios_killswitch {\n  chain output {\n    type filter hook output priority 0; policy drop;\n";
  o += "    oifname \"lo\" accept\n    oifname \"" + s.iface + "\" accept\n    udp sport 68 udp dport 67 accept\n";
  for (auto& e : s.endpoints)
    o += std::string("    ") + (e.first.v6 ? "ip6" : "ip") + " daddr " + formatIp(e.first) + " udp dport " + std::to_string(e.second) + " accept\n";
  if (s.allowLan) {
    o += "    ip daddr { 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16, 169.254.0.0/16 } accept\n";
    o += "    ip6 daddr { fc00::/7, fe80::/10 } accept\n";
  }
  o += "  }\n  chain input {\n    type filter hook input priority 0; policy drop;\n";
  o += "    iifname \"lo\" accept\n    iifname \"" + s.iface + "\" accept\n    ct state established,related accept\n    udp sport 67 udp dport 68 accept\n  }\n}\n";
  return o;
}

std::vector<Command> killSwitchInstall(const KillSwitchSpec& s) {
  std::string rules = killSwitchRuleset(s);
  if (rules.empty()) return {};
  Command remove = cmd({"nft", "delete", "table", "inet", "faios_killswitch"}, true);  // replace any stale table
  Command add = cmd({"nft", "-f", "/dev/stdin"});
  add.stdinData = rules;
  return {remove, add};
}

std::vector<Command> killSwitchRemove() { return {cmd({"nft", "delete", "table", "inet", "faios_killswitch"}, true)}; }

std::string resolvConf(const std::vector<std::string>& dns, const std::vector<std::string>& search) {
  std::string o = "# Generated by FaiOS network manager. Do not edit.\n";
  for (auto& d : dns) { IpAddr a; if (parseIp(d, a)) o += "nameserver " + formatIp(a) + "\n"; }
  std::string s;
  for (auto& d : search) if (validHostname(d)) s += (s.empty() ? "" : " ") + d;
  if (!s.empty()) o += "search " + s + "\n";
  return o;
}

}  // namespace fai
