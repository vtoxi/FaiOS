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

}  // namespace fai
