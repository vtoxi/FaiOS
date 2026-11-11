#include "screens.h"
#include "calc.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <functional>

namespace fai {
namespace {

struct Item {
  std::function<std::string()> label;
  std::function<void()> action;  // may be empty
  std::string lead;
  std::function<std::string()> sub, trail;
  std::function<int()> toggle;
  std::function<bool()> unread;
  int tone = 0;
};
using Items = std::vector<Item>;

Item fixed(std::string s, std::function<void()> a = {}, std::string lead = "", std::string sub = "") {
  Item i;
  i.label = [s] { return s; };
  i.action = std::move(a);
  i.lead = std::move(lead);
  if (!sub.empty()) i.sub = [sub] { return sub; };
  return i;
}

std::string avatar(const std::string& name) { return std::string("@") + (name.empty() ? '?' : name[0]); }

std::string digitsOf(const std::string& s) {
  std::string d;
  for (char c : s) if (c >= '0' && c <= '9') d += c;
  return d;
}

// Vertical list, or a grid when cols > 1.
class ListScreen : public Screen {
 public:
  ListScreen(std::string title, Items items, std::string center, bool numbered = false, int cols = 1)
      : title_(std::move(title)), items_(std::move(items)), center_(std::move(center)), numbered_(numbered), cols_(cols) {}

  bool onKey(Key k) override {
    int n = int(items_.size());
    if (n == 0) return false;
    int d = digitOf(k);
    bool grid = cols_ > 1;
    if (k == Key::Up) sel_ = grid ? (sel_ >= cols_ ? sel_ - cols_ : lastInCol(sel_ % cols_)) : (sel_ + n - 1) % n;
    else if (k == Key::Down) sel_ = grid ? (sel_ + cols_ < n ? sel_ + cols_ : sel_ % cols_) : (sel_ + 1) % n;
    else if (grid && k == Key::Left) sel_ = (sel_ + n - 1) % n;
    else if (grid && k == Key::Right) sel_ = (sel_ + 1) % n;
    else if (k == Key::Ok || k == Key::Soft1) run();
    else if (numbered_ && d >= 0 && (d ? d - 1 : 9) < n) { sel_ = d ? d - 1 : 9; run(); }  // 1..9, 0 = tenth
    else return false;
    return true;
  }

  View view() const override {
    View v;
    v.title = title_;
    v.layout = cols_ > 1 ? Layout::Grid : Layout::List;
    for (auto& it : items_) {
      Row r;
      r.text = it.label();
      r.lead = it.lead;
      r.tone = it.tone;
      if (it.sub) r.sub = it.sub();
      if (it.trail) r.trail = it.trail();
      if (it.toggle) r.toggle = it.toggle();
      if (it.unread) r.unread = it.unread();
      v.rows.push_back(std::move(r));
    }
    v.selected = items_.empty() ? -1 : sel_;
    v.keys = {"", center_, "Back"};
    return v;
  }

 private:
  int lastInCol(int c) const { int i = c, n = int(items_.size()); while (i + cols_ < n) i += cols_; return i; }
  void run() { if (items_[sel_].action) items_[sel_].action(); }
  std::string title_;
  Items items_;
  std::string center_;
  bool numbered_;
  int cols_;
  int sel_ = 0;
};

// Read-only detail card. Optional Call softkey (e.g. contact details).
class TextScreen : public Screen {
 public:
  TextScreen(std::string title, std::vector<std::string> lines, std::string lead = "", std::string softLabel = "",
             std::function<void()> soft = {})
      : title_(std::move(title)), lines_(std::move(lines)), lead_(std::move(lead)), softLabel_(std::move(softLabel)), soft_(std::move(soft)) {}
  bool onKey(Key k) override {
    if (soft_ && (k == Key::Soft1 || k == Key::Ok || (k == Key::Call && softLabel_ == "Call"))) { soft_(); return true; }
    return false;
  }
  View view() const override {
    View v;
    v.title = title_;
    v.layout = Layout::Text;
    v.lines = lines_;
    v.lead = lead_;
    v.keys = {soft_ ? softLabel_ : "", "", "Back"};
    return v;
  }
 private:
  std::string title_;
  std::vector<std::string> lines_;
  std::string lead_;
  std::string softLabel_;
  std::function<void()> soft_;
};

class DialScreen : public Screen {
 public:
  explicit DialScreen(std::string initial) : num_(std::move(initial)) {}
  bool onKey(Key k) override {
    int d = digitOf(k);
    if (d >= 0 && num_.size() < 20) num_ += char('0' + d);
    else if (k == Key::Star && num_.size() < 20) num_ += '*';
    else if (k == Key::Pound && num_.size() < 20) num_ += '#';
    else if ((k == Key::Soft2 || k == Key::Back) && !num_.empty()) num_.pop_back();
    else if (k == Key::Call || k == Key::Soft1 || k == Key::Ok)
      note_ = num_.empty() ? "" : "No modem: call not placed";  // honest: no telephony yet
    else return false;
    if (k != Key::Call && k != Key::Soft1 && k != Key::Ok) note_.clear();
    return true;
  }
  View view() const override {
    View v;
    v.title = "Phone";
    v.layout = Layout::Dial;
    v.lines = {num_, note_};
    v.keys = {"Call", "", num_.empty() ? "Back" : "Clear"};
    return v;
  }
 private:
  std::string num_, note_;
};

class HomeScreen : public Screen {
 public:
  explicit HomeScreen(AppManager& m) : m_(m) {}
  bool onKey(Key k) override {
    int d = digitOf(k);
    if (k == Key::Ok || k == Key::Soft1) m_.push(makeLauncher(m_));
    else if (k == Key::Soft2) m_.push(makeContacts(m_));
    else if (k == Key::Up) m_.push(makeNotifications(m_));
    else if (k == Key::Call) m_.push(makePhone(m_));
    else if (d >= 0) m_.push(makePhone(m_, std::string(1, char('0' + d))));
    else if (k == Key::Star) m_.push(makePhone(m_, "*"));
    else if (k == Key::Pound) m_.push(makePhone(m_, "#"));
    else return false;
    return true;
  }
  View view() const override {
    View v;
    v.title = "FaiOS";
    v.layout = Layout::Home;
    std::time_t t = std::time(nullptr);
    char hm[8], date[32];
    std::strftime(hm, sizeof hm, "%H:%M", std::localtime(&t));
    std::strftime(date, sizeof date, "%A, %d %B", std::localtime(&t));
    v.lines = {hm, date};
    std::string badge;
    if (int n = m_.device.unread()) badge = std::to_string(n) + " unread";
    if (size_t n = m_.notifications.count()) badge += std::string(badge.empty() ? "" : "  |  ") + std::to_string(n) + (n > 1 ? " alerts" : " alert");
    if (!badge.empty()) v.lines.push_back(badge);
    v.keys = {"Menu", "", "Contacts"};
    return v;
  }
 private:
  AppManager& m_;
};

class ClockScreen : public Screen {  // time of day + stopwatch (injectable clock via AppManager::nowMs)
 public:
  explicit ClockScreen(AppManager& m) : m_(m) {}
  bool onKey(Key k) override {
    if (k == Key::Ok) {
      if (running_) { accum_ += m_.nowMs() - start_; running_ = false; }
      else { start_ = m_.nowMs(); running_ = true; }
    } else if (k == Key::Soft1 && !running_) accum_ = 0;
    else return false;
    return true;
  }
  View view() const override {
    View v;
    v.title = "Clock";
    v.layout = Layout::Clock;
    std::time_t t = std::time(nullptr);
    char hm[8], date[32], sw[24];
    std::strftime(hm, sizeof hm, "%H:%M", std::localtime(&t));
    std::strftime(date, sizeof date, "%A, %d %B", std::localtime(&t));
    long long ms = accum_ + (running_ ? m_.nowMs() - start_ : 0);
    std::snprintf(sw, sizeof sw, "%02lld:%02lld.%lld", ms / 60000, ms / 1000 % 60, ms / 100 % 10);
    v.lines = {hm, date, sw};
    v.keys = {"Reset", running_ ? "Stop" : "Start", "Back"};
    return v;
  }
 private:
  AppManager& m_;
  bool running_ = false;
  long long start_ = 0, accum_ = 0;
};

// Keys: digits, * = ".", Up + / Down - / Left x / Right /, OK or # = equals.
class CalcScreen : public Screen {
 public:
  bool onKey(Key k) override {
    int d = digitOf(k);
    if (d >= 0) c_.digit(d);
    else if (k == Key::Star) c_.dot();
    else if (k == Key::Up) c_.op('+');
    else if (k == Key::Down) c_.op('-');
    else if (k == Key::Left) c_.op('*');
    else if (k == Key::Right) c_.op('/');
    else if (k == Key::Ok || k == Key::Pound) c_.equals();
    else if (k == Key::Soft1) c_.clear();
    else if ((k == Key::Soft2 || k == Key::Back) && c_.canBackspace()) c_.backspace();
    else return false;
    return true;
  }
  View view() const override {
    View v;
    v.title = "Calculator";
    v.layout = Layout::Calc;
    v.lines = {c_.expr(), c_.display()};
    v.keys = {"Clear", "=", c_.canBackspace() ? "Del" : "Back"};
    return v;
  }
 private:
  Calc c_;
};

class CameraScreen : public Screen {  // SIMULATED: no camera hardware; "captures" are names only
 public:
  explicit CameraScreen(AppManager& m) : m_(m) {}
  bool onKey(Key k) override {
    if (k == Key::Ok) {
      char name[16];
      std::snprintf(name, sizeof name, "IMG_%04d", int(m_.device.photos.size()) + 1);
      m_.device.photos.push_back(name);
      note_ = std::string("Saved ") + name + " (simulated)";
    } else return false;
    return true;
  }
  View view() const override {
    View v;
    v.title = "Camera";
    v.layout = Layout::Camera;
    v.lines = {note_, std::to_string(m_.device.photos.size())};
    v.keys = {"", "Capture", "Back"};
    return v;
  }
 private:
  AppManager& m_;
  std::string note_;
};

// Shared rule: the Wi-Fi radio can't be a client and a hotspot at once (simulated).
void setWifi(AppManager& m, bool on) {
  m.device.wifi = on;
  if (on) m.device.hotspot = false;
  m.saveSettings();
}

std::string hhmm(int mins) {
  char b[8];
  std::snprintf(b, sizeof b, "%02d:%02d", mins / 60, mins % 60);
  return b;
}

class NotificationsScreen : public Screen {
 public:
  explicit NotificationsScreen(AppManager& m) : m_(m) {}
  bool onKey(Key k) override {
    int n = int(m_.notifications.count());
    if (!n) return false;
    sel_ = std::min(sel_, n - 1);
    if (k == Key::Up) sel_ = (sel_ + n - 1) % n;
    else if (k == Key::Down) sel_ = (sel_ + 1) % n;
    else if (k == Key::Ok) m_.notifications.dismiss(m_.notifications.list()[sel_].id);
    else if (k == Key::Soft1) m_.notifications.clear();
    else return false;
    return true;
  }
  View view() const override {
    View v;
    v.title = "Notifications";
    auto& l = m_.notifications.list();
    for (auto& x : l) { Row r; r.text = x.title; r.sub = x.body; r.lead = "alert"; v.rows.push_back(r); }
    if (l.empty()) { Row r; r.text = "No notifications"; v.rows.push_back(r); }
    else v.selected = std::min(sel_, int(l.size()) - 1);
    v.keys = {l.empty() ? "" : "Clear all", l.empty() ? "" : "Dismiss", "Back"};
    return v;
  }
 private:
  AppManager& m_;
  int sel_ = 0;
};

class BatteryScreen : public Screen {  // values come from IPower (SimPower today = SIMULATED)
 public:
  explicit BatteryScreen(AppManager& m) : m_(m) {}
  bool onKey(Key) override { return false; }
  View view() const override {
    View v;
    v.title = "Battery";
    v.layout = Layout::Battery;
    char volt[16], temp[16];
    int mv = m_.power->voltageMv(), dc = m_.power->tempDeciC();
    std::snprintf(volt, sizeof volt, "%d.%02d V", mv / 1000, mv % 1000 / 10);
    std::snprintf(temp, sizeof temp, "%d.%d C", dc / 10, dc % 10);
    v.lines = {std::to_string(m_.power->batteryPercent()), m_.power->charging() ? "Charging" : "Not charging", volt, temp,
               "Simulated values"};
    v.keys = {"", "", "Back"};
    return v;
  }
 private:
  AppManager& m_;
};

// Live directory listing of AppManager::files (re-read on every draw, so deletes show immediately).
class FilesScreen : public Screen {
 public:
  FilesScreen(AppManager& m, std::string dir) : m_(m), dir_(std::move(dir)) {}
  bool onKey(Key k) override {
    auto e = entries();
    int n = int(e.size());
    if (!n) return false;
    sel_ = std::min(sel_, n - 1);
    if (k == Key::Up) sel_ = (sel_ + n - 1) % n;
    else if (k == Key::Down) sel_ = (sel_ + 1) % n;
    else if (k == Key::Ok || k == Key::Soft1) open(e[sel_]);
    else return false;
    return true;
  }
  View view() const override {
    View v;
    v.title = dir_.empty() ? "Files" : dir_;
    auto e = entries();
    if (!m_.files) { Row r; r.text = "Storage unavailable"; v.rows.push_back(r); }
    else if (e.empty()) { Row r; r.text = "Empty"; v.rows.push_back(r); }
    for (auto& name : e) {
      Row r;
      bool dir = name.back() == '/';
      r.text = dir ? name.substr(0, name.size() - 1) : name;
      r.lead = dir ? "folder" : "file";
      if (dir) r.sub = "Folder";
      else { std::string d; m_.files->read(dir_ + name, d); r.sub = size(d.size()); }
      v.rows.push_back(std::move(r));
    }
    if (!e.empty()) v.selected = std::min(sel_, int(e.size()) - 1);
    v.keys = {"", e.empty() ? "" : "Open", "Back"};
    return v;
  }
 private:
  std::vector<std::string> entries() const { return m_.files ? m_.files->list(dir_) : std::vector<std::string>{}; }
  static std::string size(size_t b) {
    char s[24];
    if (b < 1024) std::snprintf(s, sizeof s, "%zu B", b);
    else std::snprintf(s, sizeof s, "%.1f KB", b / 1024.0);
    return s;
  }
  void open(const std::string& name) {
    if (name.back() == '/') { m_.push(std::make_unique<FilesScreen>(m_, dir_ + name)); return; }
    std::string path = dir_ + name, data;
    if (!m_.files->read(path, data)) return;
    std::vector<std::string> lines{size(data.size())};
    size_t pos = 0;
    while (pos < data.size() && lines.size() < 9) {  // preview: first 8 lines, 36 chars each
      size_t nl = data.find('\n', pos);
      std::string l = data.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
      if (!l.empty() && l.back() == '\r') l.pop_back();
      if (l.size() > 36) l = l.substr(0, 36);
      lines.push_back(l);
      if (nl == std::string::npos) break;
      pos = nl + 1;
    }
    AppManager& m = m_;
    m.push(std::make_unique<TextScreen>(name, lines, "file", "Delete", [&m, path] {
      Item yes = fixed("Delete", [&m, path] { m.files->remove(path); m.requestPop(2); }, "alert");  // confirm + viewer
      yes.tone = 1;
      Items it;
      it.push_back(std::move(yes));
      it.push_back(fixed("Cancel", [&m] { m.requestPop(1); }));
      m.push(std::make_unique<ListScreen>("Delete file?", std::move(it), "Select"));
    }));
  }
  AppManager& m_;
  std::string dir_;
  int sel_ = 0;
};

class AlarmEditScreen : public Screen {  // enter HHMM on the keypad
 public:
  AlarmEditScreen(AppManager& m, int idx) : m_(m), idx_(idx) {}
  bool onKey(Key k) override {
    int d = digitOf(k);
    if (d >= 0 && buf_.size() < 4) { buf_ += char('0' + d); note_.clear(); }
    else if ((k == Key::Soft2 || k == Key::Back) && !buf_.empty()) { buf_.pop_back(); note_.clear(); }
    else if (k == Key::Ok || k == Key::Soft1) {
      if (buf_.size() != 4) { note_ = "Enter 4 digits (HHMM)"; return true; }
      int h = std::atoi(buf_.substr(0, 2).c_str()), mi = std::atoi(buf_.substr(2).c_str());
      if (h > 23 || mi > 59) { note_ = "Invalid time"; return true; }
      auto& al = m_.device.alarms;
      if (idx_ < 0) al.push_back({h * 60 + mi, "Alarm", true});
      else if (idx_ < int(al.size())) al[idx_].minutes = h * 60 + mi;
      m_.saveSettings();
      m_.requestPop();
    } else return false;
    return true;
  }
  View view() const override {
    View v;
    v.title = idx_ < 0 ? "New alarm" : "Edit alarm";
    v.layout = Layout::Dial;
    std::string p = buf_;
    while (p.size() < 4) p += '_';
    v.lines = {p.substr(0, 2) + ":" + p.substr(2), note_};
    v.keys = {"Save", "", buf_.empty() ? "Back" : "Clear"};
    return v;
  }
 private:
  AppManager& m_;
  int idx_;
  std::string buf_, note_;
};

std::unique_ptr<Screen> makeAlarmDetail(AppManager& m, int idx) {
  auto al = [&m, idx]() -> Device::Alarm& { return m.device.alarms[idx]; };
  Item en = fixed("Enabled", [&m, al] { al().enabled = !al().enabled; m.saveSettings(); }, "alarm");
  en.toggle = [al] { return al().enabled ? 1 : 0; };
  Item tm = fixed("Time", [&m, idx] { m.push(std::make_unique<AlarmEditScreen>(m, idx)); }, "clock");
  tm.trail = [al] { return hhmm(al().minutes); };
  Item del = fixed("Delete", [&m, idx] {
    m.device.alarms.erase(m.device.alarms.begin() + idx);
    m.saveSettings();
    m.requestPop();
  }, "alert");
  del.tone = 1;
  Items it;
  it.push_back(std::move(en));
  it.push_back(std::move(tm));
  it.push_back(std::move(del));
  return std::make_unique<ListScreen>(hhmm(al().minutes), std::move(it), "Select");
}

class AlarmsScreen : public Screen {
 public:
  explicit AlarmsScreen(AppManager& m) : m_(m) {}
  bool onKey(Key k) override {
    int n = int(m_.device.alarms.size());
    sel_ = n ? std::min(sel_, n - 1) : 0;
    if (k == Key::Soft1) {
      if (n >= 10) m_.notifications.add("Alarms", "Limit of 10 alarms reached");
      else m_.push(std::make_unique<AlarmEditScreen>(m_, -1));
    } else if (!n) return false;
    else if (k == Key::Up) sel_ = (sel_ + n - 1) % n;
    else if (k == Key::Down) sel_ = (sel_ + 1) % n;
    else if (k == Key::Ok) m_.push(makeAlarmDetail(m_, sel_));
    else return false;
    return true;
  }
  View view() const override {
    View v;
    v.title = "Alarms";
    for (auto& a : m_.device.alarms) {
      Row r;
      r.text = hhmm(a.minutes);
      r.sub = a.label;
      r.lead = "alarm";
      r.toggle = a.enabled ? 1 : 0;
      v.rows.push_back(std::move(r));
    }
    if (v.rows.empty()) { Row r; r.text = "No alarms"; v.rows.push_back(r); }
    else v.selected = std::min(sel_, int(v.rows.size()) - 1);
    v.keys = {"Add", m_.device.alarms.empty() ? "" : "Edit", "Back"};
    return v;
  }
 private:
  AppManager& m_;
  int sel_ = 0;
};

struct Net { const char* ssid; bool secured, saved; };
const Net kNets[] = {{"Home-5G", true, true}, {"Office", true, true}, {"CafeFree", false, false}, {"Neighbor", true, false}};

struct Contact { const char *name, *number; };
const Contact kContacts[] = {{"Ali", "0300 1112233"}, {"Mum", "0321 4445566"}, {"Office", "042 35001234"}, {"Zara", "0333 7778899"}};

struct Call { const char *name, *when, *dur, *kind; };  // kind: call_in / call_out / call_miss
const Call kCalls[] = {{"Ali", "Today 09:12", "", "call_miss"},
                       {"Mum", "Today 08:30", "2:14", "call_in"},
                       {"Office", "Yesterday", "5:03", "call_out"},
                       {"Zara", "Yesterday", "", "call_miss"},
                       {"Mum", "Mon", "11:40", "call_out"}};

}  // namespace

std::unique_ptr<Screen> makeHome(AppManager& m) { return std::make_unique<HomeScreen>(m); }

std::unique_ptr<Screen> makePhone(AppManager&, const std::string& initial) { return std::make_unique<DialScreen>(initial); }

// Related apps share one launcher tile that opens a short list (keys 1..n jump straight in).
static std::unique_ptr<Screen> makeGroup(AppManager& m, const std::string& title, std::vector<std::pair<Item, std::function<std::unique_ptr<Screen>()>>> apps) {
  Items it;
  for (auto& [item, make] : apps) {
    Item i = std::move(item);
    i.action = [&m, make] { m.push(make()); };
    it.push_back(std::move(i));
  }
  return std::make_unique<ListScreen>(title, std::move(it), "Open", true);
}

std::unique_ptr<Screen> makeLauncher(AppManager& m) {
  using Make = std::function<std::unique_ptr<Screen>()>;
  auto tile = [&m](const char* label, const char* lead, const char* caption, Make make) {
    return fixed(label, [&m, make] { m.push(make()); }, lead, caption);
  };
  auto group = [&m](const char* title, std::vector<std::pair<Item, Make>> apps) { return [&m, title, apps] { return makeGroup(m, title, apps); }; };
  Items it{
      tile("Phone", "call", "Phone & Contacts", group("Phone", {{fixed("Dialer", {}, "call"), [&m] { return makePhone(m); }},
                                                                {fixed("Contacts", {}, "contacts"), [&m] { return makeContacts(m); }},
                                                                {fixed("Call history", {}, "history"), [&m] { return makeCallHistory(m); }}})),
      tile("Messages", "mail", "Messages", [&m] { return makeMessages(m); }),
      tile("Camera", "camera", "Camera", [&m] { return makeCamera(m); }),
      tile("Clock", "clock", "Clock & Alarms", group("Clock", {{fixed("Clock", {}, "clock", "Time and stopwatch"), [&m] { return makeClock(m); }},
                                                                {fixed("Alarms", {}, "alarm"), [&m] { return makeAlarms(m); }}})),
      tile("Network", "connect", "Network & SIM", group("Network", {{fixed("Connectivity", {}, "connect", "Wi-Fi, Bluetooth, data"), [&m] { return makeConnectivity(m); }},
                                                                    {fixed("Hotspot", {}, "hotspot"), [&m] { return makeHotspot(m); }},
                                                                    {fixed("SIM manager", {}, "sim"), [&m] { return makeSimManager(m); }}})),
      tile("Tools", "calc", "Calculator & Files", group("Tools", {{fixed("Calculator", {}, "calc"), [&m] { return makeCalculator(m); }},
                                                                  {fixed("File manager", {}, "files"), [&m] { return makeFiles(m); }}})),
      tile("System", "settings", "Settings & Battery", group("System", {{fixed("Settings", {}, "settings"), [&m] { return makeSettings(m); }},
                                                                        {fixed("Battery", {}, "battery"), [&m] { return makeBattery(m); }}}))};
  return std::make_unique<ListScreen>("Menu", std::move(it), "Open", true, 3);
}

std::unique_ptr<Screen> makeContacts(AppManager& m) {
  Items it;
  for (auto& c : kContacts) {
    std::string name = c.name, number = c.number;
    it.push_back(fixed(name, [&m, name, number] {
      m.push(std::make_unique<TextScreen>(name, std::vector<std::string>{number}, avatar(name), "Call",
                                          [&m, number] { m.push(makePhone(m, digitsOf(number))); }));
    }, avatar(name), number));
  }
  return std::make_unique<ListScreen>("Contacts", std::move(it), "View");
}

std::unique_ptr<Screen> makeMessages(AppManager& m) {
  Items it;
  for (size_t i = 0; i < m.device.messages.size(); i++) {
    Item r = fixed(m.device.messages[i].from, [&m, i] {
      auto& g = m.device.messages[i];
      g.unread = false;
      m.push(std::make_unique<TextScreen>(g.from, std::vector<std::string>{g.body}, avatar(g.from)));
    }, avatar(m.device.messages[i].from), m.device.messages[i].body);
    r.unread = [&m, i] { return m.device.messages[i].unread; };
    it.push_back(std::move(r));
  }
  return std::make_unique<ListScreen>("Messages", std::move(it), "Read");
}

std::unique_ptr<Screen> makeCallHistory(AppManager&) {
  Items it;
  for (auto& c : kCalls) {
    Item r = fixed(c.name, {}, c.kind, c.when);
    std::string dur = c.dur;
    if (!dur.empty()) r.trail = [dur] { return dur; };
    r.tone = std::string(c.kind) == "call_miss" ? 1 : 0;
    it.push_back(std::move(r));
  }
  return std::make_unique<ListScreen>("Call history", std::move(it), "");
}

std::unique_ptr<Screen> makeSettings(AppManager& m) {
  Item wifi = fixed("Wi-Fi", [&m] { setWifi(m, !m.device.wifi); }, "wifi");
  wifi.toggle = [&m] { return m.device.wifi ? 1 : 0; };
  Item bt = fixed("Bluetooth", [&m] { m.device.bluetooth = !m.device.bluetooth; m.saveSettings(); }, "bluetooth");
  bt.toggle = [&m] { return m.device.bluetooth ? 1 : 0; };
  Item br = fixed("Brightness", [&m] {
    int b = m.power->brightness();
    m.power->setBrightness(b >= 100 ? 20 : b + 20);
    m.saveSettings();
  }, "brightness");
  br.trail = [&m] { return std::to_string(m.power->brightness()) + "%"; };
  Item about = fixed("About", [&m] {
    m.push(std::make_unique<TextScreen>("About", std::vector<std::string>{
               "FaiOS 0.1", "Desktop simulator", "No hardware target", "Settings saved to storage"}, "info"));
  }, "info");
  Items it;
  it.push_back(std::move(wifi));
  it.push_back(std::move(bt));
  it.push_back(std::move(br));
  it.push_back(std::move(about));
  return std::make_unique<ListScreen>("Settings", std::move(it), "Change");
}

std::unique_ptr<Screen> makeClock(AppManager& m) { return std::make_unique<ClockScreen>(m); }
std::unique_ptr<Screen> makeCalculator(AppManager&) { return std::make_unique<CalcScreen>(); }
std::unique_ptr<Screen> makeCamera(AppManager& m) { return std::make_unique<CameraScreen>(m); }

std::unique_ptr<Screen> makeHotspot(AppManager& m) {
  Item on = fixed("Hotspot", [&m] {
    auto& d = m.device;
    if (!d.hotspot && !d.dataUsable()) {  // needs a SIM with usable mobile data
      m.notifications.add("Hotspot", "No usable mobile data (check SIM, data, airplane mode)");
      m.log.log(Level::Warn, "hotspot", "refused: no usable mobile data");
      return;
    }
    d.hotspot = !d.hotspot;
    if (d.hotspot) d.wifi = false;  // the Wi-Fi radio can't be client and AP at once (simulated rule)
    m.saveSettings();
  }, "hotspot", "Shares mobile data");
  on.toggle = [&m] { return m.device.hotspot ? 1 : 0; };
  Item name = fixed("Network name", {}, "info");
  name.sub = [&m] { return m.device.hotspotName; };
  Item pass = fixed("Password", {}, "info", "********");
  Item clients = fixed("Connected devices", {}, "info");
  clients.trail = [&m] { return std::to_string(m.device.hotspotClients()); };
  Items it;
  it.push_back(std::move(on));
  it.push_back(std::move(name));
  it.push_back(std::move(pass));
  it.push_back(std::move(clients));
  return std::make_unique<ListScreen>("Hotspot", std::move(it), "Toggle");
}

static std::unique_ptr<Screen> makeSimDetail(AppManager& m, int slot) {
  auto sim = [&m, slot]() -> Device::Sim& { return m.device.sims[slot]; };
  Item en = fixed("Enabled", [&m, slot, sim] {
    sim().enabled = !sim().enabled;
    if (!sim().enabled && m.device.dataSim == slot) {  // hand data to another active SIM, else none
      m.device.dataSim = -1;
      for (size_t i = 0; i < m.device.sims.size(); i++)
        if (int(i) != slot && m.device.sims[i].present && m.device.sims[i].enabled) m.device.dataSim = int(i);
    }
    m.saveSettings();
  }, "sim");
  en.toggle = [sim] { return sim().enabled ? 1 : 0; };
  Item data = fixed("Use for data", [&m, slot, sim] {
    if (sim().enabled) { m.device.dataSim = slot; m.saveSettings(); }
  }, "sim");
  data.toggle = [&m, slot] { return m.device.dataSim == slot ? 1 : 0; };
  Item pin = fixed("PIN lock", [sim] { sim().pinLock = !sim().pinLock; }, "sim", "Simulated: no PIN stored");
  pin.toggle = [sim] { return sim().pinLock ? 1 : 0; };
  Item num = fixed("Number", {}, "info");
  num.sub = [sim] { return sim().number; };
  Items it;
  it.push_back(std::move(en));
  it.push_back(std::move(data));
  it.push_back(std::move(pin));
  it.push_back(std::move(num));
  return std::make_unique<ListScreen>("SIM " + std::to_string(slot + 1), std::move(it), "Toggle");
}

std::unique_ptr<Screen> makeSimManager(AppManager& m) {
  Items it;
  for (size_t i = 0; i < m.device.sims.size(); i++) {
    int slot = int(i);
    Item r = fixed("SIM " + std::to_string(i + 1), [&m, slot] {
      if (m.device.sims[slot].present) m.push(makeSimDetail(m, slot));
      else m.push(std::make_unique<TextScreen>("SIM " + std::to_string(slot + 1), std::vector<std::string>{"No SIM inserted"}, "sim"));
    }, "sim");
    r.sub = [&m, slot] {
      auto& s = m.device.sims[slot];
      return s.present ? s.carrier + "  " + s.number : std::string("Empty");
    };
    r.trail = [&m, slot] {
      auto& s = m.device.sims[slot];
      if (!s.present) return std::string();
      return m.device.dataSim == slot && s.enabled ? std::string("Data") : std::string(s.enabled ? "On" : "Off");
    };
    it.push_back(std::move(r));
  }
  return std::make_unique<ListScreen>("SIM manager", std::move(it), "Open");
}

std::unique_ptr<Screen> makeNotifications(AppManager& m) { return std::make_unique<NotificationsScreen>(m); }
std::unique_ptr<Screen> makeBattery(AppManager& m) { return std::make_unique<BatteryScreen>(m); }
std::unique_ptr<Screen> makeFiles(AppManager& m) { return std::make_unique<FilesScreen>(m, ""); }
std::unique_ptr<Screen> makeAlarms(AppManager& m) { return std::make_unique<AlarmsScreen>(m); }

std::unique_ptr<Screen> makeWifiNetworks(AppManager& m) {
  Items it;
  for (auto& n : kNets) {
    std::string ssid = n.ssid;
    bool secured = n.secured, saved = n.saved;
    Item r = fixed(ssid, [&m, ssid, secured, saved] {
      if (!m.device.wifi) { m.notifications.add("Wi-Fi", "Turn Wi-Fi on first"); return; }
      if (secured && !saved) { m.notifications.add("Wi-Fi", "Password entry is not supported yet"); return; }
      m.device.wifiSsid = ssid;
      m.saveSettings();
    }, "wifi");
    r.sub = [&m, ssid, secured, saved] {
      if (m.device.wifi && m.device.wifiSsid == ssid) return std::string("Connected");
      return std::string(saved ? "Saved" : secured ? "Secured" : "Open");
    };
    it.push_back(std::move(r));
  }
  return std::make_unique<ListScreen>("Wi-Fi networks", std::move(it), "Connect");
}

std::unique_ptr<Screen> makeConnectivity(AppManager& m) {
  Item wifi = fixed("Wi-Fi", [&m] { setWifi(m, !m.device.wifi); }, "wifi");
  wifi.toggle = [&m] { return m.device.wifi ? 1 : 0; };
  wifi.sub = [&m] { return !m.device.wifi ? std::string("Off") : m.device.wifiSsid.empty() ? std::string("Not connected") : m.device.wifiSsid; };
  Item nets = fixed("Wi-Fi networks", [&m] { m.push(makeWifiNetworks(m)); }, "wifi", "Scan and connect");
  Item bt = fixed("Bluetooth", [&m] { m.device.bluetooth = !m.device.bluetooth; m.saveSettings(); }, "bluetooth", "Device pairing not built yet");
  bt.toggle = [&m] { return m.device.bluetooth ? 1 : 0; };
  Item data = fixed("Mobile data", [&m] { m.device.mobileData = !m.device.mobileData; m.saveSettings(); }, "mobile");
  data.toggle = [&m] { return m.device.mobileData ? 1 : 0; };
  data.sub = [&m] {
    return !m.device.simActive() ? std::string("No active SIM") : m.device.dataUsable() ? "Using SIM " + std::to_string(m.device.dataSim + 1) : std::string("Not in use");
  };
  Item air = fixed("Airplane mode", [&m] {
    auto& d = m.device;
    d.airplane = !d.airplane;
    if (d.airplane) d.wifi = d.bluetooth = d.hotspot = false;  // radios off (they can be switched back on individually)
    m.saveSettings();
  }, "airplane");
  air.toggle = [&m] { return m.device.airplane ? 1 : 0; };
  Item hs = fixed("Hotspot", [&m] { m.push(makeHotspot(m)); }, "hotspot");
  hs.trail = [&m] { return std::string(m.device.hotspot ? "On" : "Off"); };
  Items it;
  for (Item* i : {&wifi, &nets, &bt, &data, &air, &hs}) it.push_back(std::move(*i));
  return std::make_unique<ListScreen>("Connectivity", std::move(it), "Select");
}

}  // namespace fai
