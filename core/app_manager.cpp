#include "app_manager.h"

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
  if (!top()->onKey(k) && (k == Key::Back || k == Key::Soft2)) { pop(); return; }
  changed();
}

SystemStatus AppManager::status() const {
  SystemStatus st;
  st.signal = device.simActive() ? device.signal : 0;  // no active SIM = no signal
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
}

void AppManager::saveSettings() {
  if (!config) return;
  config->setBool("wifi", device.wifi);
  config->setBool("bluetooth", device.bluetooth);
  config->setInt("brightness", power->brightness());
  config->setBool("hotspot", device.hotspot);
  for (size_t i = 0; i < device.sims.size(); i++) config->setBool("sim" + std::to_string(i) + "_enabled", device.sims[i].enabled);
  config->setInt("data_sim", device.dataSim);
  if (!config->save()) log.log(Level::Warn, "config", "save failed");
}

}  // namespace fai
