#pragma once
#include <chrono>
#include <functional>
#include <memory>
#include <vector>
#include "screen.h"
#include "services.h"

namespace fai {

struct Message { std::string from, body; bool unread; };

// SIMULATED device state. Replaced by HAL-backed providers on real hardware.
struct Device {
  int signal = 4;  // 0..4 bars
  bool wifi = true, bluetooth = false;
  struct Sim { std::string carrier, number; bool present, enabled, pinLock; };
  std::vector<Sim> sims{{"Jazz", "0300 1112233", true, true, false}, {"", "", false, false, false}};
  int dataSim = 0;  // slot used for mobile data, -1 = none
  bool hotspot = false;
  std::string hotspotName = "FaiOS-Hotspot", hotspotPass = "faios1234";
  int hotspotClients() const { return hotspot ? 2 : 0; }  // SIMULATED
  std::vector<std::string> photos;                         // SIMULATED captures (names only)
  bool simActive() const { for (auto& s : sims) if (s.present && s.enabled) return true; return false; }
  std::vector<Message> messages{
      {"Ali", "Meet at 6?", true},
      {"Jazz", "Your balance is Rs. 120", true},
      {"Mum", "Call me when free", false}};
  int unread() const { int n = 0; for (auto& m : messages) n += m.unread; return n; }
};

// Screen stack + key routing. The "application lifecycle" for milestone 1.
class AppManager {
 public:
  Device device;
  Logger log;
  Notifications notifications;
  std::unique_ptr<IPower> power = std::make_unique<SimPower>();
  std::function<long long()> nowMs = [] {  // monotonic ms; tests inject a fake
    return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  };
  Config* config = nullptr;  // optional; when set, Settings changes persist
  std::function<void()> onChange;  // fired after any stack/key change (renderer hook)

  void push(std::unique_ptr<Screen> s);
  bool pop();           // never pops the root (home) screen
  void goHome();
  void handleKey(Key k);
  SystemStatus status() const;
  void loadSettings();  // config -> device/power
  void saveSettings();  // device/power -> config, written to storage
  Screen* top() const { return stack_.empty() ? nullptr : stack_.back().get(); }
  size_t depth() const { return stack_.size(); }

 private:
  std::vector<std::unique_ptr<Screen>> stack_;
  void changed() { if (onChange) onChange(); }
};

}  // namespace fai
