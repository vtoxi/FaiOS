#include "network.h"

#ifdef __linux__
#include <netdb.h>
#include <sys/socket.h>
#include <cstring>
#endif

namespace fai {

const char* linkName(LinkKind k) { return k == LinkKind::WiFi ? "Wi-Fi" : k == LinkKind::Cellular ? "Cellular" : "None"; }

LinkKind NetworkManager::active() {
  LinkInfo w = p_.wifi(), c = p_.cellular();
  bool cell = c.up && prefs.allowCellular;
  if (w.up && cell) return prefs.preferWifi ? LinkKind::WiFi : LinkKind::Cellular;
  if (w.up) return LinkKind::WiFi;
  if (cell) return LinkKind::Cellular;
  return LinkKind::None;
}

std::vector<std::string> NetworkManager::linkDns() {
  switch (active()) {
    case LinkKind::WiFi: return p_.wifi().dns;
    case LinkKind::Cellular: return p_.cellular().dns;
    default: return {};
  }
}

bool NetworkManager::poll() {
  LinkKind now = active();
  if (!primed_) { primed_ = true; last_ = now; return false; }
  if (now == last_) return false;
  LinkKind from = last_;
  last_ = now;
  log_.log(Level::Info, "net", std::string("active link ") + linkName(from) + " -> " + linkName(now));
  for (auto& f : listeners_) f(from, now);
  return true;
}

std::vector<std::string> NetworkManager::effectiveDns(bool vpnProtected, const std::vector<std::string>& vpnDns) {
  if (vpnProtected && !vpnDns.empty()) return vpnDns;
  if (!prefs.customDns.empty()) return prefs.customDns;
  return linkDns();
}

bool SystemResolver::resolve(const std::string& host, IpAddr& out) {
  if (parseIp(host, out)) return true;
#ifdef __linux__
  addrinfo hints;
  std::memset(&hints, 0, sizeof hints);
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo* res = nullptr;
  if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
  const addrinfo* pick = nullptr;
  for (const addrinfo* a = res; a; a = a->ai_next) {
    if (a->ai_family == AF_INET) { pick = a; break; }
    if (!pick && a->ai_family == AF_INET6) pick = a;
  }
  bool ok = false;
  if (pick && pick->ai_family == AF_INET) {
    out = IpAddr();
    std::memcpy(out.b, &reinterpret_cast<const sockaddr_in*>(pick->ai_addr)->sin_addr, 4);
    ok = true;
  } else if (pick) {
    out = IpAddr();
    out.v6 = true;
    std::memcpy(out.b, &reinterpret_cast<const sockaddr_in6*>(pick->ai_addr)->sin6_addr, 16);
    ok = true;
  }
  freeaddrinfo(res);
  return ok;
#else
  return false;
#endif
}

Internet ConnectivityMonitor::check() {
  if (url_.empty()) return Internet::Unknown;
  HttpRequest r;
  r.url = url_;
  HttpResponse x = h_.send(r);
  if (x.status == 0) return Internet::Offline;
  if (x.status == 204) return Internet::Online;
  if ((x.status >= 200 && x.status < 400)) return Internet::CaptivePortal;  // got *something* other than the expected 204: a login page or redirect
  return Internet::Unknown;
}

}  // namespace fai
