// faios-tunnel-test: brings up a REAL WireGuard tunnel with the product's CommandVpnBackend, inside a generic
// QEMU/Linux guest, against a WireGuard "server" in a network namespace on the same machine. Needs root, `ip`,
// `wg`, `nft`, a kernel with WireGuard + nftables. Prints one line per check and FAIOS-TUNNEL: PASS/FAIL.
//
// What this proves: the generated ip/wg/nft commands work on a real Linux kernel (handshake, routing, kill switch).
// What it does NOT prove: anything about the Jazz Digit 4G, its kernel, or a real VPN provider.
// Fixture setup below uses sh and `wg genkey` (test scaffolding only, never product code). Keys are generated per run
// and never printed.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "command.h"
#include "vpn.h"
#include "wg_commands.h"
#include "wireguard.h"

using namespace fai;

static int failures = 0, checks = 0;
static void check(bool ok, const char* what) {
  checks++;
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) failures++;
  std::fflush(stdout);
}

static int sh(const std::string& cmd) { return std::system(("exec " + cmd + " >/dev/null 2>&1").c_str()); }
static std::string capture(const std::string& cmd) {
  std::string out;
  FILE* p = popen((cmd + " 2>/dev/null").c_str(), "r");
  if (!p) return out;
  char b[256];
  while (std::fgets(b, sizeof b, p)) out += b;
  pclose(p);
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
  return out;
}
static bool ping(const char* ip) { return sh(std::string("ping -c 1 -W 3 ") + ip) == 0; }

static void fixtureDown() {
  sh("nft delete table inet faios_killswitch");
  sh("ip link del wg0");
  sh("ip netns del srv");
  sh("ip link del veth-c");
}

int main() {
  if (geteuid() != 0) { std::printf("FAIL need root\nFAIOS-TUNNEL: FAIL\n"); return 1; }
  fixtureDown();

  // ---- fixture: client = this namespace, server = netns "srv", joined by a veth pair (the "underlay") ----
  std::string srvKey = capture("wg genkey"), cliKey = capture("wg genkey");
  std::string srvPub = capture("echo " + srvKey + " | wg pubkey"), cliPub = capture("echo " + cliKey + " | wg pubkey");
  check(srvKey.size() == 44 && cliKey.size() == 44 && srvPub.size() == 44 && cliPub.size() == 44, "wg genkey/pubkey work (kernel tools present)");
  bool fx = sh("ip netns add srv") == 0 && sh("ip link add veth-c type veth peer name veth-s") == 0 && sh("ip link set veth-s netns srv") == 0 &&
            sh("ip addr add 192.168.50.1/24 dev veth-c") == 0 && sh("ip link set veth-c up") == 0 &&
            sh("ip netns exec srv ip addr add 192.168.50.2/24 dev veth-s") == 0 && sh("ip netns exec srv ip link set veth-s up") == 0 &&
            sh("ip netns exec srv ip link set lo up") == 0 &&
            sh("ip netns exec srv ip addr add 203.0.113.5/32 dev lo") == 0;  // the "internet": only the server namespace has it
  check(fx, "fixture: network namespace + veth underlay created");
  // server WireGuard interface; the key goes in a 0600 file under /run, not on a command line
  std::string keyFile = "/run/faios-srv.key";
  FILE* kf = std::fopen(keyFile.c_str(), "w");
  if (kf) { fchmod(fileno(kf), 0600); std::fputs((srvKey + "\n").c_str(), kf); std::fclose(kf); }
  bool srv = sh("ip netns exec srv ip link add wg0 type wireguard") == 0 &&
             sh("ip netns exec srv wg set wg0 private-key " + keyFile + " listen-port 51820 peer " + cliPub + " allowed-ips 10.9.0.2/32") == 0 &&
             sh("ip netns exec srv ip addr add 10.9.0.1/24 dev wg0") == 0 && sh("ip netns exec srv ip link set wg0 up") == 0;
  std::remove(keyFile.c_str());
  check(srv, "server: wireguard interface created in the namespace (kernel has WireGuard)");
  if (!fx || !srv) { std::printf("FAIOS-TUNNEL: FAIL\n"); fixtureDown(); return 1; }

  std::string baseDev = capture("ip -4 route get 203.0.113.5");
  check(baseDev.find("dev wg0") == std::string::npos, "before connecting, traffic does not go through wg0");
  bool gwBefore = ping("10.0.2.2");  // QEMU user-net gateway = our stand-in for "the normal network"
  check(gwBefore, "before connecting, the normal network (10.0.2.2) is reachable");

  // ---- the product code under test ----
  std::string text = "[Interface]\nPrivateKey = " + cliKey + "\nAddress = 10.9.0.2/32\n[Peer]\nPublicKey = " + srvPub +
                     "\nEndpoint = 192.168.50.2:51820\nAllowedIPs = 0.0.0.0/0\nPersistentKeepalive = 5\n";
  WgParseResult pr = parseWgConfig(text);
  check(pr.ok && isFullTunnel(pr.cfg), "profile parses as a full-tunnel config");
  PosixRunner runner;
  CommandVpnBackend be(runner, "wg0");
  KillSwitchSpec ks;
  IpAddr ep;
  parseIp("192.168.50.2", ep);
  ks.endpoints.push_back({ep, 51820});
  std::string err;
  // Mutation check: with "faios.mutate=nokillswitch" on the kernel command line the kill switch is skipped, and the
  // leak checks below MUST then fail. This proves those checks can actually detect a leak.
  const bool mutate = capture("cat /proc/cmdline").find("faios.mutate=nokillswitch") != std::string::npos;
  if (mutate) std::printf("note: MUTATION RUN, kill switch deliberately not installed; leak checks are expected to FAIL\n");
  else check(be.setKillSwitch(&ks, err), "kill switch installed BEFORE the tunnel (nft)");
  check(be.bringUp(pr.cfg, {"192.168.50.2"}, err), "tunnel up: link, keys via stdin, address, routes");
  if (!err.empty()) std::printf("     error: %s\n", err.c_str());

  bool pingSrv = ping("10.9.0.1");  // first packet triggers the handshake
  check(pingSrv, "ping through the tunnel to the server's tunnel address");
  TunnelStats st;
  be.stats(st);
  check(st.up && st.lastHandshakeMs > 0, "handshake completed (wg show latest-handshakes)");
  check(st.rxBytes > 0 && st.txBytes > 0, "bytes moved both ways through the tunnel");
  check(be.routesThroughTunnel("203.0.113.5"), "routing check: 203.0.113.5 is routed via wg0");
  check(ping("203.0.113.5"), "an address only the server can reach answers through the tunnel (full tunnel works)");
  check(!ping("10.0.2.2"), "the normal network is NOT reachable around the tunnel (kill switch + routes)");

  // ---- leak test: the tunnel dies abruptly; the kill switch must keep traffic from leaking ----
  sh("ip link del wg0");
  check(!ping("10.0.2.2"), "tunnel destroyed behind the app's back: still no traffic to the normal network (kill switch holds)");
  check(!ping("203.0.113.5"), "tunnel destroyed: no traffic to the internet address either");

  // ---- clean disconnect restores normal networking ----
  be.tearDown();
  check(be.setKillSwitch(nullptr, err), "user disconnect: kill switch removed");
  std::string after = capture("ip -4 route get 203.0.113.5");
  check(after.find("dev wg0") == std::string::npos && capture("ip rule show").find("51820") == std::string::npos, "policy routing rules and routes removed");
  check(ping("10.0.2.2"), "after disconnect, the normal network is reachable again");
  check(capture("nft list tables").find("faios_killswitch") == std::string::npos, "no kill-switch table left behind");

  fixtureDown();
  std::printf("%d checks, %d failed\nFAIOS-TUNNEL: %s\n", checks, failures, failures ? "FAIL" : "PASS");
  return failures ? 1 : 0;
}
