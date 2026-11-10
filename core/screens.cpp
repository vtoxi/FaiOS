#include "screens.h"
#include <ctime>
#include <functional>

namespace fai {
namespace {

struct Item {
  std::function<std::string()> label;
  std::function<void()> action;  // may be empty
};
using Items = std::vector<Item>;

Item fixed(std::string s, std::function<void()> a = {}) {
  return {[s] { return s; }, std::move(a)};
}

class ListScreen : public Screen {
 public:
  ListScreen(std::string title, Items items, std::string left, bool numbered = false)
      : title_(std::move(title)), items_(std::move(items)), left_(std::move(left)), numbered_(numbered) {}

  bool onKey(Key k) override {
    int n = int(items_.size());
    if (n == 0) return false;
    int d = digitOf(k);
    if (k == Key::Up) sel_ = (sel_ + n - 1) % n;
    else if (k == Key::Down) sel_ = (sel_ + 1) % n;
    else if (k == Key::Ok || k == Key::Soft1) run();
    else if (numbered_ && d >= 1 && d <= n) { sel_ = d - 1; run(); }
    else return false;
    return true;
  }

  View view() const override {
    View v;
    v.title = title_;
    for (size_t i = 0; i < items_.size(); i++)
      v.lines.push_back((numbered_ ? std::to_string(i + 1) + ". " : "") + items_[i].label());
    v.selected = items_.empty() ? -1 : sel_;
    v.keys = {left_, "", "Back"};
    return v;
  }

 private:
  void run() { if (items_[sel_].action) items_[sel_].action(); }
  std::string title_;
  Items items_;
  std::string left_;
  bool numbered_;
  int sel_ = 0;
};

class TextScreen : public Screen {
 public:
  TextScreen(std::string title, std::vector<std::string> lines) : title_(std::move(title)), lines_(std::move(lines)) {}
  bool onKey(Key) override { return false; }
  View view() const override { View v; v.title = title_; v.lines = lines_; v.keys = {"", "", "Back"}; return v; }
 private:
  std::string title_;
  std::vector<std::string> lines_;
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
    v.lines = {num_.empty() ? "Enter number" : num_, note_};
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
    v.home = true;
    std::time_t t = std::time(nullptr);
    char hm[8], date[24];
    std::strftime(hm, sizeof hm, "%H:%M", std::localtime(&t));
    std::strftime(date, sizeof date, "%a %d %b", std::localtime(&t));
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
const char* const kCalls[] = {"Ali (missed)", "Mum (incoming 2m)", "Office (outgoing 5m)", "Zara (missed)"};

}  // namespace

std::unique_ptr<Screen> makeHome(AppManager& m) { return std::make_unique<HomeScreen>(m); }

std::unique_ptr<Screen> makePhone(AppManager&, const std::string& initial) { return std::make_unique<DialScreen>(initial); }

std::unique_ptr<Screen> makeLauncher(AppManager& m) {
  Items it{fixed("Phone", [&m] { m.push(makePhone(m)); }),
           fixed("Contacts", [&m] { m.push(makeContacts(m)); }),
           fixed("Messages", [&m] { m.push(makeMessages(m)); }),
           fixed("Call history", [&m] { m.push(makeCallHistory(m)); }),
           fixed("Settings", [&m] { m.push(makeSettings(m)); })};
  return std::make_unique<ListScreen>("Menu", std::move(it), "Open", true);
}

std::unique_ptr<Screen> makeContacts(AppManager& m) {
  Items it;
  for (auto& c : kContacts)
    it.push_back(fixed(c.name, [&m, c] { m.push(std::make_unique<TextScreen>(c.name, std::vector<std::string>{c.number})); }));
  return std::make_unique<ListScreen>("Contacts", std::move(it), "View");
}

std::unique_ptr<Screen> makeMessages(AppManager& m) {
  Items it;
  for (size_t i = 0; i < m.device.messages.size(); i++)
    it.push_back({[&m, i] { auto& g = m.device.messages[i]; return (g.unread ? "* " : "") + g.from; },
                  [&m, i] {
                    auto& g = m.device.messages[i];
                    g.unread = false;
                    m.push(std::make_unique<TextScreen>(g.from, std::vector<std::string>{g.body}));
                  }});
  return std::make_unique<ListScreen>("Messages", std::move(it), "Read");
}

std::unique_ptr<Screen> makeCallHistory(AppManager&) {
  Items it;
  for (auto c : kCalls) it.push_back(fixed(c));
  return std::make_unique<ListScreen>("Call history", std::move(it), "");
}

std::unique_ptr<Screen> makeSettings(AppManager& m) {
  auto onoff = [](bool b) { return b ? "On" : "Off"; };
  Items it{
      {[&m, onoff] { return std::string("Wi-Fi: ") + onoff(m.device.wifi); }, [&m] { m.device.wifi = !m.device.wifi; m.saveSettings(); }},
      {[&m, onoff] { return std::string("Bluetooth: ") + onoff(m.device.bluetooth); }, [&m] { m.device.bluetooth = !m.device.bluetooth; m.saveSettings(); }},
      {[&m] { return "Brightness: " + std::to_string(m.power->brightness()) + "%"; },
       [&m] { int b = m.power->brightness(); m.power->setBrightness(b >= 100 ? 20 : b + 20); m.saveSettings(); }},
      fixed("About", [&m] {
        m.push(std::make_unique<TextScreen>("About", std::vector<std::string>{
                   "FaiOS 0.1", "Desktop simulator", "No hardware target", "Settings saved to storage"}));
      })};
  return std::make_unique<ListScreen>("Settings", std::move(it), "Change");
}

}  // namespace fai
