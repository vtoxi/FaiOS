// Plain-assert tests (no framework). Run via `ctest`.
#include <cstdio>
#include <cstdlib>
#include "screens.h"

using namespace fai;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static std::string title(AppManager& m) { return m.top()->view().title; }
static void press(AppManager& m, Key k) { m.handleKey(k); }

static AppManager* boot() {
  auto* m = new AppManager;
  m->push(makeHome(*m));
  return m;
}

static void test_launcher_and_back() {
  auto* m = boot();
  CHECK(title(*m) == "FaiOS");
  press(*m, Key::Ok);
  CHECK(title(*m) == "Menu");
  CHECK(m->depth() == 2);
  press(*m, Key::Back);
  CHECK(title(*m) == "FaiOS");
  press(*m, Key::Back);  // root is never popped
  CHECK(m->depth() == 1);
  delete m;
}

static void test_five_apps_via_digit_shortcuts() {
  const char* names[] = {"Phone", "Contacts", "Messages", "Call history", "Settings"};
  for (int i = 0; i < 5; i++) {
    auto* m = boot();
    press(*m, Key::Ok);
    press(*m, Key(int(Key::Num1) + i));
    CHECK(title(*m) == names[i]);
    press(*m, Key::End);  // End returns home from anywhere
    CHECK(m->depth() == 1);
    delete m;
  }
}

static void test_list_navigation_wraps() {
  auto* m = boot();
  press(*m, Key::Ok);
  CHECK(m->top()->view().selected == 0);
  press(*m, Key::Up);
  CHECK(m->top()->view().selected == 4);
  press(*m, Key::Down);
  CHECK(m->top()->view().selected == 0);
  press(*m, Key::Down);
  press(*m, Key::Ok);  // selected index 1 = Contacts
  CHECK(title(*m) == "Contacts");
  delete m;
}

static void test_dialer() {
  auto* m = boot();
  press(*m, Key::Num0);  // digit on home opens dialer prefilled
  CHECK(title(*m) == "Phone");
  press(*m, Key::Num3);
  CHECK(m->top()->view().lines[0] == "03");
  press(*m, Key::Soft2);  // Clear removes last digit, screen stays
  CHECK(m->top()->view().lines[0] == "0");
  CHECK(m->depth() == 2);
  press(*m, Key::Back);
  press(*m, Key::Back);  // empty -> now pops
  CHECK(m->depth() == 1);
  delete m;
}

static void test_dialer_does_not_fake_calls() {
  auto* m = boot();
  press(*m, Key::Num5);
  press(*m, Key::Call);
  CHECK(m->top()->view().lines[1].find("not placed") != std::string::npos);
  delete m;
}

static void test_messages_mark_read() {
  auto* m = boot();
  CHECK(m->device.unread() == 2);
  press(*m, Key::Ok);
  press(*m, Key::Num3);  // Messages
  press(*m, Key::Ok);    // open first (unread)
  CHECK(m->device.unread() == 1);
  CHECK(title(*m) == "Ali");
  delete m;
}

static void test_settings_toggle() {
  auto* m = boot();
  press(*m, Key::Ok);
  press(*m, Key::Num5);
  bool before = m->device.wifi;
  press(*m, Key::Ok);  // first item = Wi-Fi
  CHECK(m->device.wifi != before);
  delete m;
}

static void test_keymap_names() {
  for (int i = 0; i < int(Key::Count); i++) {
    Key out;
    CHECK(keyFromName(keyName(Key(i)), out) && out == Key(i));
  }
  Key out;
  CHECK(!keyFromName("Bogus", out));
}

int main() {
  test_launcher_and_back();
  test_five_apps_via_digit_shortcuts();
  test_list_navigation_wraps();
  test_dialer();
  test_dialer_does_not_fake_calls();
  test_messages_mark_read();
  test_settings_toggle();
  test_keymap_names();
  std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
  return failures ? 1 : 0;
}
