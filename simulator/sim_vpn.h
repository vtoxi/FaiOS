// SIMULATOR ONLY. A SIMULATED VPN backend and resolver for exercising the VPN screens.
// There is no tunnel, no encryption and no routing change; the kill switch blocks nothing.
// VpnStatus::simulated is true for it, so the UI shows "Simulated tunnel" and NEVER "Protected".
#pragma once
#include <ctime>

#include "vpn.h"

namespace fai {

class SimVpnBackend : public IVpnBackend {
 public:
  const char* name() const override { return "simulated (no real tunnel)"; }
  bool simulated() const override { return true; }
  bool setKillSwitch(const KillSwitchSpec*, std::string&) override { return true; }  // pretends; blocks nothing
  bool bringUp(const WgConfig&, const std::vector<std::string>&, std::string&) override { up_ = true; upAtMs_ = nowMs(); return true; }
  void tearDown() override { up_ = false; }
  bool stats(TunnelStats& o) override {
    long long now = nowMs();
    o = TunnelStats();
    o.up = up_;
    if (up_ && now - upAtMs_ >= 1200) {  // a "handshake" 1.2 s after bring-up, then it stays fresh
      o.lastHandshakeMs = now - 500;
      o.rxBytes = (unsigned long long)(now - upAtMs_) * 40;
      o.txBytes = (unsigned long long)(now - upAtMs_) * 12;
    }
    return true;
  }
  bool routesThroughTunnel(const std::string&) override { return up_; }
  static long long nowMs() { return static_cast<long long>(std::time(nullptr)) * 1000 + 0; }
 private:
  bool up_ = false;
  long long upAtMs_ = 0;
};

class SimResolver : public IResolver {  // every name resolves to a documentation address; nothing is looked up
 public:
  bool resolve(const std::string& host, IpAddr& out) override { return parseIp(host, out) || parseIp("203.0.113.9", out); }
};

// Obviously fake demo profile (public documentation addresses, filler keys). Written by `faios_sim --vpn-demo`.
inline std::string demoWgConfig() {
  std::string k = base64Encode(std::string(32, 'D')), p = base64Encode(std::string(32, 'E'));
  return "# FaiOS DEMO profile: fake keys and a documentation-range server. Connects to nothing.\n[Interface]\nPrivateKey = " + k +
         "\nAddress = 10.99.0.2/32\nDNS = 10.99.0.1\n\n[Peer]\nPublicKey = " + p +
         "\nAllowedIPs = 0.0.0.0/0, ::/0\nEndpoint = vpn.demo.example:51820\nPersistentKeepalive = 25\n";
}

}  // namespace fai
