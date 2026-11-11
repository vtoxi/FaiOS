#include "screens.h"
#include "calc.h"
#include <cstdio>
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
  TextScreen(std::string title, std::vector<std::string> lines, std::string lead = "", std::function<void()> call = {})
      : title_(std::move(title)), lines_(std::move(lines)), lead_(std::move(lead)), call_(std::move(call)) {}
  bool onKey(Key k) override {
    if (call_ && (k == Key::Soft1 || k == Key::Call || k == Key::Ok)) { call_(); return true; }
    return false;
  }
  View view() const override {
    View v;
    v.title = title_;
    v.layout = Layout::Text;
    v.lines = lines_;
    v.lead = lead_;
    v.keys = {call_ ? "Call" : "", "", "Back"};
    return v;
  }
 private:
  std::string title_;
  std::vector<std::string> lines_;
  std::string lead_;
  std::function<void()> call_;
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
    if (int n = m_.status().badge) v.lines.push_back(std::to_string(n) + " unread");
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
    } else if (k == Key::Soft1) m_.push(makeGallery(m_));
    else return false;
    return true;
  }
  View view() const override {
    View v;
    v.title = "Camera";
    v.layout = Layout::Camera;
    v.lines = {note_, std::to_string(m_.device.photos.size())};
    v.keys = {"Gallery", "Capture", "Back"};
    return v;
  }
 private:
  AppManager& m_;
  std::string note_;
};

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

std::unique_ptr<Screen> makeLauncher(AppManager& m) {
  Items it{fixed("Phone", [&m] { m.push(makePhone(m)); }, "call", "Phone"),
           fixed("Contacts", [&m] { m.push(makeContacts(m)); }, "contacts", "Contacts"),
           fixed("Messages", [&m] { m.push(makeMessages(m)); }, "mail", "Messages"),
           fixed("History", [&m] { m.push(makeCallHistory(m)); }, "history", "Call history"),
           fixed("Settings", [&m] { m.push(makeSettings(m)); }, "settings", "Settings"),
           fixed("Clock", [&m] { m.push(makeClock(m)); }, "clock", "Clock"),
           fixed("Calc", [&m] { m.push(makeCalculator(m)); }, "calc", "Calculator"),
           fixed("Camera", [&m] { m.push(makeCamera(m)); }, "camera", "Camera"),
           fixed("Hotspot", [&m] { m.push(makeHotspot(m)); }, "hotspot", "Hotspot"),
           fixed("SIM", [&m] { m.push(makeSimManager(m)); }, "sim", "SIM manager")};
  return std::make_unique<ListScreen>("Menu", std::move(it), "Open", true, 3);
}

std::unique_ptr<Screen> makeContacts(AppManager& m) {
  Items it;
  for (auto& c : kContacts) {
    std::string name = c.name, number = c.number;
    it.push_back(fixed(name, [&m, name, number] {
      m.push(std::make_unique<TextScreen>(name, std::vector<std::string>{number}, avatar(name),
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
  Item wifi = fixed("Wi-Fi", [&m] { m.device.wifi = !m.device.wifi; m.saveSettings(); }, "wifi");
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

std::unique_ptr<Screen> makeGallery(AppManager& m) {
  if (m.device.photos.empty()) return std::make_unique<TextScreen>("Gallery", std::vector<std::string>{"No photos yet"}, "camera");
  Items it;
  for (auto& p : m.device.photos) it.push_back(fixed(p, {}, "image", "Simulated placeholder"));
  return std::make_unique<ListScreen>("Gallery", std::move(it), "");
}

std::unique_ptr<Screen> makeHotspot(AppManager& m) {
  Item on = fixed("Hotspot", [&m] {
    auto& d = m.device;
    if (!d.hotspot && !(d.simActive() && d.dataSim >= 0)) {  // needs an active data SIM
      m.notifications.add("Hotspot", "No active SIM for mobile data");
      m.log.log(Level::Warn, "hotspot", "refused: no active data SIM");
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

}  // namespace fai
