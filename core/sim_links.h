// SIMULATED link provider: derives link state from the simulated Device. A real provider (netlink / modem HAL)
// replaces this on hardware.
#pragma once
#include "app_manager.h"
#include "network.h"

namespace fai {

class SimLinkProvider : public ILinkProvider {
 public:
  explicit SimLinkProvider(const Device& d) : d_(d) {}
  LinkInfo wifi() override {
    LinkInfo i;
    i.up = d_.wifi && !d_.hotspot && !d_.wifiSsid.empty();  // the radio is an access point while the hotspot is on
    i.dns = {"192.168.1.1"};
    return i;
  }
  LinkInfo cellular() override {
    LinkInfo i;
    i.up = d_.dataUsable();
    i.dns = {"10.64.0.1"};
    return i;
  }
 private:
  const Device& d_;
};

}  // namespace fai
