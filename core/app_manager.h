#pragma once
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
