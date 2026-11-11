#include "screens.h"
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
    else if (numbered_ && d >= 1 && d <= n) { sel_ = d - 1; run(); }
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
           fixed("Settings", [&m] { m.push(makeSettings(m)); }, "settings", "Settings")};
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

}  // namespace fai
