// Included by test_core.cpp: File manager, Connectivity, Battery, Alarms, Notifications, storage listing.
#include <ctime>

static void test_storage_list_remove() {
  MemStorage s;
  s.write("config.ini", "a");
  s.write("photos/a.txt", "1");
  s.write("photos/b.txt", "2");
  s.write("photos/sub/c.txt", "3");
  auto root = s.list("");
  CHECK(root.size() == 2 && root[0] == "config.ini" && root[1] == "photos/");
  auto ph = s.list("photos/");
  CHECK(ph.size() == 3 && ph[0] == "a.txt" && ph[2] == "sub/");
  CHECK(s.list("../").empty());
  CHECK(s.remove("photos/a.txt") && !s.remove("photos/a.txt") && s.list("photos/").size() == 2);

  namespace fs = std::filesystem;
  fs::path dir = fs::temp_directory_path() / "faios-test-list";
  fs::remove_all(dir);
  FileStorage f(dir.string());
  f.write("x/y.txt", "hello");
  f.write("z.txt", "q");
  auto fr = f.list("");
  CHECK(fr.size() == 2 && fr[0] == "x/" && fr[1] == "z.txt");
  CHECK(f.remove("z.txt") && !f.exists("z.txt"));
  CHECK(!f.remove("x"));  // directories are not removable
  fs::remove_all(dir);
}

static void test_file_manager() {
  auto* m = boot();
  MemStorage fs;
  fs.write("config.ini", "wifi=true\nbrightness=60\n");
  fs.write("notes/todo.txt", "buy milk\n");
  m->files = &fs;
  launch(*m, "Files");
  CHECK(title(*m) == "Files");
  auto v = m->top()->view();
  CHECK(v.rows.size() == 2 && v.rows[0].text == "config.ini" && v.rows[1].lead == "folder");
  press(*m, Key::Down);
  press(*m, Key::Ok);  // into notes/
  CHECK(title(*m) == "notes/" && m->top()->view().rows[0].text == "todo.txt");
  press(*m, Key::Ok);  // view the file
  CHECK(title(*m) == "todo.txt" && m->top()->view().lines[1] == "buy milk" && m->top()->view().keys.left == "Delete");
  press(*m, Key::Soft1);  // delete -> confirm
  CHECK(title(*m) == "Delete file?");
  press(*m, Key::Down);
  press(*m, Key::Ok);     // Cancel: back to the viewer
  CHECK(title(*m) == "todo.txt" && fs.exists("notes/todo.txt"));
  press(*m, Key::Soft1);
  press(*m, Key::Ok);     // Delete: confirm and viewer both close, list is live
  CHECK(title(*m) == "notes/" && !fs.exists("notes/todo.txt"));
  CHECK(m->top()->view().rows[0].text == "Empty");
  delete m;

  auto* n = boot();  // no storage attached
  launch(*n, "Files");
  CHECK(n->top()->view().rows[0].text == "Storage unavailable");
  delete n;
}

static void test_connectivity() {
  auto* m = boot();
  launch(*m, "Connectivity");
  CHECK(title(*m) == "Connectivity" && m->top()->view().rows.size() == 6);
  press(*m, Key::Ok);  // Wi-Fi off
  CHECK(!m->device.wifi && m->top()->view().rows[0].sub == "Off");
  press(*m, Key::Down);
  press(*m, Key::Ok);  // networks
  CHECK(title(*m) == "Wi-Fi networks");
  press(*m, Key::Ok);  // wifi is off -> refused
  CHECK(m->notifications.count() == 1);
  press(*m, Key::Back);
  press(*m, Key::Up);
  press(*m, Key::Ok);  // Wi-Fi on again
  press(*m, Key::Down);
  press(*m, Key::Ok);
  press(*m, Key::Down); press(*m, Key::Down); press(*m, Key::Down);
  press(*m, Key::Ok);  // "Neighbor": secured and not saved -> refused
  CHECK(m->device.wifiSsid == "Home-5G" && m->notifications.count() == 2);
  press(*m, Key::Up);
  press(*m, Key::Ok);  // "CafeFree": open network -> connects
  CHECK(m->device.wifiSsid == "CafeFree" && m->top()->view().rows[2].sub == "Connected");
  press(*m, Key::Back);
  // airplane mode: radios off, no signal
  m->device.bluetooth = true;
  press(*m, Key::Down); press(*m, Key::Down); press(*m, Key::Down);  // Airplane mode row
  press(*m, Key::Ok);
  CHECK(m->device.airplane && !m->device.wifi && !m->device.bluetooth && m->status().signal == 0 && m->status().airplane);
  CHECK(!m->device.dataUsable());
  delete m;
}

static void test_battery_screen() {
  auto* m = boot();
  launch(*m, "Battery");
  CHECK(title(*m) == "Battery" && m->top()->view().layout == Layout::Battery);
  auto v = m->top()->view();
  CHECK(v.lines[0] == "78" && v.lines[1] == "Not charging" && v.lines[2] == "3.95 V" && v.lines[3] == "31.0 C");
  CHECK(v.lines[4] == "Simulated values");
  delete m;
}

static void test_alarms() {
  auto* m = boot();
  MemStorage store;
  Config cfg(store);
  m->config = &cfg;
  launch(*m, "Alarms");
  CHECK(title(*m) == "Alarms" && m->top()->view().rows.size() == 1 && m->top()->view().rows[0].text == "07:00");
  press(*m, Key::Soft1);  // add
  press(*m, Key::Num0); press(*m, Key::Num6); press(*m, Key::Num3);
  press(*m, Key::Ok);     // only 3 digits
  CHECK(title(*m) == "New alarm" && m->top()->view().lines[1] != "");
  press(*m, Key::Num9);   // 0639
  press(*m, Key::Soft2);  // clear last digit -> 063
  press(*m, Key::Num7);   // 0637
  for (int i = 0; i < 4; i++) press(*m, Key::Back);  // Back deletes digits before it closes the editor
  CHECK(title(*m) == "New alarm");
  press(*m, Key::Num9); press(*m, Key::Num9); press(*m, Key::Num1); press(*m, Key::Num0);
  press(*m, Key::Ok);
  CHECK(m->top()->view().lines[1] == "Invalid time" && m->device.alarms.size() == 1);
  for (int i = 0; i < 4; i++) press(*m, Key::Back);
  press(*m, Key::Num0); press(*m, Key::Num6); press(*m, Key::Num3); press(*m, Key::Num0);
  press(*m, Key::Ok);     // saved, editor closes
  CHECK(title(*m) == "Alarms" && m->device.alarms.size() == 2 && m->device.alarms[1].minutes == 6 * 60 + 30 && m->device.alarms[1].enabled);

  // persistence round trip
  Config cfg2(store);
  cfg2.load();
  AppManager m2;
  m2.config = &cfg2;
  m2.loadSettings();
  CHECK(m2.device.alarms.size() == 2 && m2.device.alarms[1].minutes == 390 && m2.device.alarms[0].label == "Wake up" && !m2.device.alarms[0].enabled);

  // toggle + delete from the detail screen
  press(*m, Key::Ok);  // detail of alarm 0
  CHECK(m->top()->view().rows.size() == 3);
  press(*m, Key::Ok);  // enable
  CHECK(m->device.alarms[0].enabled);
  press(*m, Key::Down); press(*m, Key::Down);
  press(*m, Key::Ok);  // delete
  CHECK(title(*m) == "Alarms" && m->device.alarms.size() == 1 && m->device.alarms[0].minutes == 390);

  // firing: once per minute, only matching enabled alarms
  std::time_t now = std::time(nullptr);
  std::tm tmv = *std::localtime(&now);
  tmv.tm_hour = 6; tmv.tm_min = 30; tmv.tm_sec = 0;
  std::time_t t = std::mktime(&tmv);
  m->tick(t);
  CHECK(m->notifications.count() == 1 && m->notifications.list()[0].title == "Alarm");
  m->tick(t + 20);        // same minute: no repeat
  CHECK(m->notifications.count() == 1);
  m->tick(t + 60);        // 06:31: nothing due
  CHECK(m->notifications.count() == 1);
  m->device.alarms[0].enabled = false;
  m->tick(t + 24 * 3600); // next day 06:30 but disabled
  CHECK(m->notifications.count() == 1);
  delete m;
}

static void test_notifications_screen() {
  auto* m = boot();
  m->notifications.add("Alarm", "Wake up");
  m->notifications.add("Wi-Fi", "Turn Wi-Fi on first");
  CHECK(m->top()->view().lines.size() == 3 && m->top()->view().lines[2].find("2 alerts") != std::string::npos);
  press(*m, Key::Up);  // Home: Up opens notifications
  CHECK(title(*m) == "Notifications" && m->top()->view().rows.size() == 2);
  press(*m, Key::Ok);  // dismiss selected
  CHECK(m->notifications.count() == 1 && m->top()->view().rows[0].text == "Wi-Fi");
  press(*m, Key::Soft1);  // clear all
  CHECK(m->notifications.count() == 0 && m->top()->view().rows[0].text == "No notifications");
  press(*m, Key::Back);
  CHECK(m->top()->view().lines.size() == 3);  // two unread messages remain
  delete m;
}
