// Pure generators: WireGuard tunnel setup commands, nftables kill-switch ruleset, resolv.conf text.
// Nothing here runs anything, so tests can assert exactly what WOULD run.
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "command.h"
#include "wireguard.h"

namespace fai {

constexpr int kDefaultFwmark = 51820;  // also the routing table number, same convention as wg-quick

// Text for `wg setconf`. endpointIps[i] is the already-resolved IP (or "") for peer i. Contains the private key:
// send it on stdin only.
std::string wgSetconf(const WgConfig& cfg, const std::vector<std::string>& endpointIps);

// Order matters: link, keys (stdin), addresses, up, then routes. Empty result = invalid interface name.
std::vector<Command> tunnelUpCommands(const WgConfig& cfg, const std::string& iface, int fwmark, const std::vector<std::string>& endpointIps);
std::vector<Command> tunnelDownCommands(const WgConfig& cfg, const std::string& iface, int fwmark);

struct KillSwitchSpec {
  std::string iface = "wg0";
  std::vector<std::pair<IpAddr, int>> endpoints;  // VPN server ip:port that must stay reachable outside the tunnel
  bool allowLan = false;                          // private ranges (printer, router) stay reachable
};

// nftables ruleset: default-drop in both directions except loopback, the tunnel interface, DHCP, the VPN
// endpoints, and (optionally) LAN. Empty string = invalid spec (bad interface name or no endpoint).
std::string killSwitchRuleset(const KillSwitchSpec& s);
std::vector<Command> killSwitchInstall(const KillSwitchSpec& s);  // empty if invalid
std::vector<Command> killSwitchRemove();

// DNS file content. Only valid IPs are written (no injection through a crafted profile).
std::string resolvConf(const std::vector<std::string>& dns, const std::vector<std::string>& search);

}  // namespace fai
