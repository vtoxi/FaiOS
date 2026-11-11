// Included by test_core.cpp: tests for Clock, Calculator, Camera, Hotspot, SIM manager.

static void type(Calc& c, const char* s) {  // digits, '.', and + - * / =
  for (; *s; s++) {
    if (*s >= '0' && *s <= '9') c.digit(*s - '0');
    else if (*s == '.') c.dot();
    else if (*s == '=') c.equals();
    else c.op(*s);
  }
}

static void test_calc_logic() {
  Calc c;
  type(c, "12+3=");
  CHECK(c.display() == "15");
  c.clear();
  type(c, "2+3*4=");  // chain, left to right
  CHECK(c.display() == "20");
  c.clear();
  type(c, "0.1+0.2=");
  CHECK(c.display() == "0.3");
  c.clear();
  type(c, "1/0=");
  CHECK(c.display() == "Error");
  c.digit(5);  // any digit recovers from error
  CHECK(c.display() == "5");
  c.clear();
  type(c, "7=");  // equals with no operator is a no-op
  CHECK(c.display() == "7");
  type(c, "+2=");  // continue from a result
  CHECK(c.display() == "9");
  type(c, "5");    // typing after equals starts a new number
  CHECK(c.display() == "5");
  c.clear();
  type(c, "123");
  c.backspace();
  CHECK(c.display() == "12");
  c.backspace(); c.backspace();
  CHECK(c.display() == "0");
  type(c, "9-");
  CHECK(c.expr() == "9 -");
  c.op('*');  // changing the operator before the 2nd operand
  CHECK(c.expr() == "9 x");
  c.clear();
  type(c, "1.5.5");  // second dot ignored
  CHECK(c.display() == "1.55");
  c.clear();
  for (int i = 0; i < 20; i++) c.digit(9);
  CHECK(c.display().size() == 12);
}

static void test_calc_screen_keys() {
  auto* m = boot();
  press(*m, Key::Ok);
  press(*m, Key::Num7);  // Calculator
  CHECK(title(*m) == "Calculator" && m->top()->view().layout == Layout::Calc);
  press(*m, Key::Num4); press(*m, Key::Num2);
  press(*m, Key::Up);    // +
  press(*m, Key::Num8);
  press(*m, Key::Ok);    // =
  CHECK(m->top()->view().lines[1] == "50");
  press(*m, Key::Back);  // nothing to delete after '=' -> leaves the app
  CHECK(title(*m) == "Menu");
  delete m;
}

static void test_clock_stopwatch() {
  auto* m = boot();
  long long now = 1000;
  m->nowMs = [&] { return now; };
  press(*m, Key::Ok);
  press(*m, Key::Num6);  // Clock
  CHECK(title(*m) == "Clock" && m->top()->view().lines[2] == "00:00.0");
  press(*m, Key::Ok);    // start
  now += 65300;
  CHECK(m->top()->view().lines[2] == "01:05.3");
  press(*m, Key::Soft1); // reset ignored while running
  CHECK(m->top()->view().lines[2] == "01:05.3");
  press(*m, Key::Ok);    // stop
  now += 5000;
  CHECK(m->top()->view().lines[2] == "01:05.3" && m->top()->view().keys.center == "Start");
  press(*m, Key::Ok);    // resume accumulates
  now += 700;
  CHECK(m->top()->view().lines[2] == "01:06.0");
  press(*m, Key::Ok);
  press(*m, Key::Soft1); // reset when stopped
  CHECK(m->top()->view().lines[2] == "00:00.0");
  delete m;
}

static void test_camera() {
  auto* m = boot();
  press(*m, Key::Ok);
  press(*m, Key::Num8);  // Camera
  CHECK(title(*m) == "Camera" && m->top()->view().layout == Layout::Camera);
  CHECK(m->top()->view().keys.left.empty());   // no gallery
  press(*m, Key::Soft1);                       // nothing happens
  CHECK(title(*m) == "Camera");
  press(*m, Key::Ok);
  press(*m, Key::Ok);
  CHECK(m->device.photos.size() == 2 && m->device.photos[1] == "IMG_0002");
  CHECK(m->top()->view().lines[0].find("simulated") != std::string::npos);  // never pretends to be real
  delete m;
}

static void test_sim_manager_and_hotspot() {
  auto* m = boot();
  CHECK(m->status().signal == 4);
  press(*m, Key::Ok);
  press(*m, Key::Num0);  // SIM manager is the 10th item (key 0)
  CHECK(title(*m) == "SIM manager" && m->top()->view().rows.size() == 2);
  CHECK(m->top()->view().rows[0].trail == "Data" && m->top()->view().rows[1].sub == "Empty");
  press(*m, Key::Down);
  press(*m, Key::Ok);    // empty slot
  CHECK(m->top()->view().lines[0] == "No SIM inserted");
  press(*m, Key::Back);
  press(*m, Key::Up);
  press(*m, Key::Ok);    // SIM 1 detail
  CHECK(title(*m) == "SIM 1" && m->top()->view().rows[0].toggle == 1);
  press(*m, Key::Ok);    // disable SIM 1
  CHECK(!m->device.sims[0].enabled && m->device.dataSim == -1);
  CHECK(m->status().signal == 0);  // no active SIM = no signal bars
  press(*m, Key::Back); press(*m, Key::Back);

  // hotspot is refused without an active data SIM, and says why
  press(*m, Key::Num9);
  CHECK(title(*m) == "Hotspot");
  press(*m, Key::Ok);
  CHECK(!m->device.hotspot && m->notifications.count() == 1);
  press(*m, Key::Back);
  press(*m, Key::Num0); press(*m, Key::Ok);
  press(*m, Key::Ok);    // re-enable SIM 1
  press(*m, Key::Down);
  press(*m, Key::Ok);    // use for data
  CHECK(m->device.sims[0].enabled && m->device.dataSim == 0);
  press(*m, Key::Back); press(*m, Key::Back);
  press(*m, Key::Num9);
  press(*m, Key::Ok);
  CHECK(m->device.hotspot && !m->device.wifi && m->status().hotspot);
  CHECK(m->top()->view().rows[3].trail == "2");
  delete m;
}

static void test_launcher_has_all_apps() {
  auto* m = boot();
  press(*m, Key::Ok);
  const char* names[] = {"Phone", "Contacts", "Messages", "Call history", "Settings", "Clock", "Calculator", "Camera", "Hotspot", "SIM manager"};
  for (int i = 0; i < 10; i++) {
    press(*m, i < 9 ? Key(int(Key::Num1) + i) : Key::Num0);  // keys 1..9 then 0
    CHECK(title(*m) == names[i]);
    press(*m, Key::Back);
  }
  delete m;
}
