#include "app_manager.h"

#include <cstdlib>

namespace fai {

static const char* const kNames[] = {"Up", "Down", "Left", "Right", "Ok", "Back", "Soft1", "Soft2",
                                     "Call", "End", "Star", "Pound", "Num0", "Num1", "Num2", "Num3",
                                     "Num4", "Num5", "Num6", "Num7", "Num8", "Num9"};

const char* keyName(Key k) { return k < Key::Count ? kNames[int(k)] : "?"; }

bool keyFromName(const std::string& name, Key& out) {
  for (int i = 0; i < int(Key::Count); i++)
    if (name == kNames[i]) { out = Key(i); return true; }
  return false;
}

void AppManager::push(std::unique_ptr<Screen> s) {
  log.log(Level::Info, "nav", "push " + s->view().title);
  stack_.push_back(std::move(s));
  changed();
}

bool AppManager::pop() {
  if (stack_.size() <= 1) return false;
  stack_.pop_back();
  changed();
  return true;
}

void AppManager::goHome() {
  if (stack_.size() > 1) { stack_.resize(1); changed(); }
}

void AppManager::handleKey(Key k) {
  if (!top()) return;
  if (k == Key::End) { goHome(); return; }
  // Screens may push() while handling a key; popping is done here, after onKey returns.
  bool used = top()->onKey(k);
  while (pendingPops_ > 0) { pendingPops_--; used = true; pop(); }
  if (!used && (k == Key::Back || k == Key::Soft2)) { pop(); return; }
  changed();
}

void AppManager::tick(std::time_t now) {
  long minute = long(now / 60);
  if (minute == lastMinute_) return;  // at most once per minute
  lastMinute_ = minute;
  std::tm* t = std::localtime(&now);
  int mins = t->tm_hour * 60 + t->tm_min;
  for (auto& a : device.alarms)
    if (a.enabled && a.minutes == mins) {
      notifications.add("Alarm", a.label.empty() ? "Alarm" : a.label);
      log.log(Level::Info, "alarm", "fired " + a.label);
      changed();
    }
}

static std::string clean(std::string s) {  // keep the alarms string parseable
  for (char& c : s) if (c == ';' || c == ':') c = ' ';
  return s;
}

SystemStatus AppManager::status() const {
  SystemStatus st;
  st.signal = (device.simActive() && !device.airplane) ? device.signal : 0;  // no SIM / airplane = no signal
  st.airplane = device.airplane;
  st.hotspot = device.hotspot;
  st.battery = power->batteryPercent();
  st.charging = power->charging();
  st.brightness = power->brightness();
  st.wifi = device.wifi;
  st.bluetooth = device.bluetooth;
  st.badge = device.unread() + int(notifications.count());
  return st;
}

void AppManager::loadSettings() {
  if (!config) return;
  device.wifi = config->getBool("wifi", device.wifi);
  device.bluetooth = config->getBool("bluetooth", device.bluetooth);
  power->setBrightness(config->getInt("brightness", power->brightness()));
  device.hotspot = config->getBool("hotspot", device.hotspot);
  for (size_t i = 0; i < device.sims.size(); i++)
    if (device.sims[i].present) device.sims[i].enabled = config->getBool("sim" + std::to_string(i) + "_enabled", device.sims[i].enabled);
  device.dataSim = config->getInt("data_sim", device.dataSim);
  device.mobileData = config->getBool("mobile_data", device.mobileData);
  device.airplane = config->getBool("airplane", device.airplane);
  device.wifiSsid = config->getString("wifi_ssid", device.wifiSsid);
  std::string a = config->getString("alarms", "?");  // "MMMM:E:label;..." ("?" = never saved)
  if (a != "?") {
    device.alarms.clear();
    size_t pos = 0;
    while (pos <= a.size() && !a.empty()) {
      size_t end = a.find(';', pos);
      std::string e = a.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
      size_t c1 = e.find(':'), c2 = c1 == std::string::npos ? c1 : e.find(':', c1 + 1);
      if (c2 != std::string::npos) {
        int mins = std::atoi(e.substr(0, c1).c_str());
        if (mins >= 0 && mins < 1440) device.alarms.push_back({mins, e.substr(c2 + 1), e.substr(c1 + 1, c2 - c1 - 1) == "1"});
      }
      if (end == std::string::npos) break;
      pos = end + 1;
    }
  }
}

void AppManager::saveSettings() {
  if (!config) return;
  config->setBool("wifi", device.wifi);
  config->setBool("bluetooth", device.bluetooth);
  config->setInt("brightness", power->brightness());
  config->setBool("hotspot", device.hotspot);
  for (size_t i = 0; i < device.sims.size(); i++) config->setBool("sim" + std::to_string(i) + "_enabled", device.sims[i].enabled);
  config->setInt("data_sim", device.dataSim);
  config->setBool("mobile_data", device.mobileData);
  config->setBool("airplane", device.airplane);
  config->setString("wifi_ssid", device.wifiSsid);
  std::string a;
  for (auto& al : device.alarms) a += (a.empty() ? "" : ";") + std::to_string(al.minutes) + ":" + (al.enabled ? "1" : "0") + ":" + clean(al.label);
  config->setString("alarms", a);
  if (!config->save()) log.log(Level::Warn, "config", "save failed");
}

}  // namespace fai
