// Unified network management: which link is active, DNS policy, internet reachability, hostname resolution.
// Link state comes from an ILinkProvider. Today that is SIMULATED (from the Device); a real provider (netlink /
// NetworkManager / modem HAL) replaces it later.
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "http.h"
#include "ip.h"
#include "services.h"

namespace fai {

enum class LinkKind { None, WiFi, Cellular };
const char* linkName(LinkKind k);

struct LinkInfo {
  bool up = false;
  std::vector<std::string> dns;  // servers handed out by that link (DHCP / modem)
};

class ILinkProvider {
 public:
  virtual ~ILinkProvider() = default;
  virtual LinkInfo wifi() = 0;
  virtual LinkInfo cellular() = 0;
};

class NetworkManager {
 public:
  struct Prefs {
    bool preferWifi = true;               // when both are up
    bool allowCellular = true;            // false = Wi-Fi only
    std::vector<std::string> customDns;   // user override, applied when no VPN DNS is in force
  };
  Prefs prefs;

  NetworkManager(ILinkProvider& p, Logger& log) : p_(p), log_(log) {}

  LinkKind active();                                   // current best link
  std::vector<std::string> linkDns();                  // DNS of the active link
  bool poll();                                         // true if the active link changed since the last poll; notifies listeners
  void addListener(std::function<void(LinkKind from, LinkKind to)> f) { listeners_.push_back(std::move(f)); }

  // DNS policy, strictest first:
  //  1. a verified VPN with DNS servers   -> ONLY those (no leak to the local network's resolver)
  //  2. user custom DNS                    -> those
  //  3. otherwise                          -> the active link's own DNS
  std::vector<std::string> effectiveDns(bool vpnProtected, const std::vector<std::string>& vpnDns);

 private:
  ILinkProvider& p_;
  Logger& log_;
  LinkKind last_ = LinkKind::None;
  bool primed_ = false;
  std::vector<std::function<void(LinkKind, LinkKind)>> listeners_;
};

class IResolver {
 public:
  virtual ~IResolver() = default;
  virtual bool resolve(const std::string& host, IpAddr& out) = 0;  // IP literals must pass through unchanged
};

class SystemResolver : public IResolver {  // getaddrinfo, prefers IPv4. Uses the SYSTEM resolver: with a kill switch up it will be blocked.
 public:
  bool resolve(const std::string& host, IpAddr& out) override;
};

enum class Internet { Unknown, Online, Offline, CaptivePortal };

// Probes a configurable URL that returns HTTP 204 when the internet is reachable. No default URL: probing a
// third-party server is a privacy decision the owner makes. Empty URL = monitoring off (Unknown).
class ConnectivityMonitor {
 public:
  ConnectivityMonitor(IHttp& h, std::string probeUrl) : h_(h), url_(std::move(probeUrl)) {}
  Internet check();
 private:
  IHttp& h_;
  std::string url_;
};

}  // namespace fai
