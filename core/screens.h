#pragma once
#include <memory>
#include <string>
#include "app_manager.h"

namespace fai {

// Screen factories. All data shown is SIMULATED (see Device in app_manager.h).
std::unique_ptr<Screen> makeHome(AppManager& m);
std::unique_ptr<Screen> makeLauncher(AppManager& m);
std::unique_ptr<Screen> makePhone(AppManager& m, const std::string& initial = "");
std::unique_ptr<Screen> makeContacts(AppManager& m);
std::unique_ptr<Screen> makeMessages(AppManager& m);
std::unique_ptr<Screen> makeCallHistory(AppManager& m);
std::unique_ptr<Screen> makeSettings(AppManager& m);
std::unique_ptr<Screen> makeClock(AppManager& m);
std::unique_ptr<Screen> makeCalculator(AppManager& m);
std::unique_ptr<Screen> makeCamera(AppManager& m);
std::unique_ptr<Screen> makeHotspot(AppManager& m);
std::unique_ptr<Screen> makeSimManager(AppManager& m);
std::unique_ptr<Screen> makeNotifications(AppManager& m);
std::unique_ptr<Screen> makeBattery(AppManager& m);
std::unique_ptr<Screen> makeFiles(AppManager& m);
std::unique_ptr<Screen> makeAlarms(AppManager& m);
std::unique_ptr<Screen> makeWifiNetworks(AppManager& m);
std::unique_ptr<Screen> makeConnectivity(AppManager& m);
std::unique_ptr<Screen> makeAccounts(AppManager& m);

}  // namespace fai
