#include "screens.h"
#include "calc.h"
#include "calendar.h"
#include "games.h"
#include "media.h"
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

class CameraScreen : public Screen {  // SIMULATED: no camera hardware. A capture writes a generated placeholder image to DCIM/
 public:
  explicit CameraScreen(AppManager& m) : m_(m) {}
  bool onKey(Key k) override {
    if (k == Key::Left || k == Key::Right) { if (!rec_) video_ = !video_; else return true; }  // mode cannot change mid-recording
    else if (k == Key::Back && rec_) stopVideo();                                              // Back saves the take first, then a second Back leaves
    else if (k == Key::Ok && video_) {
      if (!m_.files) note_ = "Storage unavailable";
      else if (!rec_) { rec_ = true; start_ = m_.nowMs(); note_.clear(); }
      else stopVideo();
    } else if (k == Key::Ok) {
      if (!m_.files) note_ = "Storage unavailable";
      else {
        std::string name = nextPhotoName(*m_.files);
        std::string bmp = makeBmp(160, 120, uint32_t(m_.nowMs() & 0xFFFF) + uint32_t(name.size()) * 31u);
        note_ = !bmp.empty() && m_.files->write(kPhotoDir + name, bmp) ? "Saved " + name + " (simulated)" : "Could not save the photo";
      }
    } else if (k == Key::Soft1) m_.push(makeGallery(m_));
    else return false;
    return true;
  }
  View view() const override {
    View v;
    v.title = "Camera";
    v.layout = Layout::Camera;
    int n = 0;
    if (m_.files) for (auto& f : listGallery(*m_.files)) n += f.kind == MediaKind::Image;
    int vids = 0;
    if (m_.files) for (auto& f : listGallery(*m_.files)) vids += f.kind == MediaKind::Video;
    v.lines = {note_, std::to_string(n), video_ ? "Video" : "Photo", rec_ ? "REC " + formatClock(m_.nowMs() - start_) : "", std::to_string(vids)};
    v.keys = {"Gallery", video_ ? (rec_ ? "Stop" : "Record") : "Capture", "Back"};
    return v;
  }
 private:
  void stopVideo() {
    rec_ = false;
    int sec = int(std::min<long long>(600, (m_.nowMs() - start_) / 1000));
    std::string name = nextVideoName(*m_.files), stub = makeVideoStub(std::max(1, sec));
    note_ = !stub.empty() && m_.files->write(kVideoDir + name, stub) ? "Saved " + name + " (simulated, not real video)" : "Could not save the video";
  }
  AppManager& m_;
  std::string note_;
  bool video_ = false, rec_ = false;
  long long start_ = 0;
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

const char* statusText(AccountStatus s) { return s == AccountStatus::Active ? "OK" : s == AccountStatus::NeedsReauth ? "Sign in" : "Error"; }

std::string lastSyncText(long long ms) {
  if (ms <= 0) return "Never";
  std::time_t t = std::time_t(ms / 1000);
  char b[24];
  std::strftime(b, sizeof b, "%d %b %H:%M", std::localtime(&t));
  return b;
}

// Why real Google sign-in is not possible right now, in plain words (never pretends it works).
std::vector<std::string> signInBlockers(AppManager& m) {
  std::vector<std::string> v;
  if (!m.accounts) { v.push_back("Accounts service is not running."); return v; }
  if (!m.accounts->configured()) v.push_back("No Google OAuth client ID set up (see docs/GOOGLE_AUTH.md).");
  if (!m.accounts->secureStorage()) v.push_back("No secure storage: tokens cannot be stored safely.");
  v.push_back("Sign-in must open in an external browser, which FaiOS does not have yet.");
  return v;
}

std::unique_ptr<Screen> makeAccountDetail(AppManager& m, const std::string& id) {
  auto acct = [&m, id]() -> const Account* { return m.accounts ? m.accounts->find(id) : nullptr; };
  Item who = fixed("Account");
  who.label = [acct] { const Account* a = acct(); return a ? a->name : std::string("(removed)"); };
  who.sub = [acct] { const Account* a = acct(); return a ? a->email : std::string(); };
  { const Account* a0 = acct(); who.lead = avatar(a0 ? a0->name : "?"); }
  Item st = fixed("Status", {}, "info");
  st.trail = [acct] { const Account* a = acct(); return std::string(a ? statusText(a->status) : "-"); };
  Item last = fixed("Last sync", {}, "clock");
  last.trail = [acct] { const Account* a = acct(); return a ? lastSyncText(a->lastSyncMs) : std::string("-"); };
  auto toggleItem = [&m, id, acct](const char* label, SyncService svc, const char* need) {
    Item i = fixed(label, [&m, id, acct, svc, need] {
      const Account* a = acct();
      if (!a || !m.accounts) return;
      bool now = svc == SyncService::Contacts ? a->syncContacts : a->syncCalendar;
      if (!m.accounts->setSync(id, svc, !now))
        m.notifications.add("Accounts", std::string(need) + " permission not granted for this account");
    }, svc == SyncService::Contacts ? "contacts" : "calendar");
    i.toggle = [acct, svc] { const Account* a = acct(); return a && (svc == SyncService::Contacts ? a->syncContacts : a->syncCalendar) ? 1 : 0; };
    return i;
  };
  Item syncNow = fixed("Sync now", [&m, id] {
    if (!m.sync) return;
    SyncResult r = m.sync->syncNow(id);
    m.notifications.add("Sync", r.ok() ? "Synced: " + std::to_string(r.pulled) + " received, " + std::to_string(r.pushed) + " sent" + (r.conflicts ? ", " + std::to_string(r.conflicts) + " conflict(s) kept as copies" : std::string())
                                       : std::string("Sync failed: ") + syncStatusName(r.status) + (r.detail.empty() ? "" : " (" + r.detail + ")"));
  }, "connect");
  syncNow.sub = [&m, id] {
    const SyncManager::State* s = m.sync ? m.sync->state(id) : nullptr;
    if (!m.sync) return std::string("Not available");
    if (!s || !s->everRan) return std::string("Not synced yet");
    return s->last == SyncStatus::Ok ? std::string("Up to date") : std::string(syncStatusName(s->last)) + ", will retry";
  };
  Item out = fixed("Sign out", [&m, id] {
    Item yes = fixed("Sign out and revoke access", [&m, id] { if (m.accounts) m.accounts->remove(id); if (m.gsync) m.gsync->forget(id); m.requestPop(2); }, "alert");
    yes.tone = 1;
    Items it;
    it.push_back(std::move(yes));
    it.push_back(fixed("Cancel", [&m] { m.requestPop(1); }));
    m.push(std::make_unique<ListScreen>("Sign out?", std::move(it), "Select"));
  }, "alert");
  out.tone = 1;
  Items it;
  it.push_back(std::move(who));
  it.push_back(std::move(st));
  it.push_back(std::move(last));
  it.push_back(toggleItem("Sync contacts", SyncService::Contacts, "Contacts"));
  it.push_back(toggleItem("Sync calendar", SyncService::Calendar, "Calendar"));
  it.push_back(std::move(syncNow));
  it.push_back(std::move(out));
  return std::make_unique<ListScreen>("Account", std::move(it), "Select");
}

std::unique_ptr<Screen> makeAddAccount(AppManager& m) {
  Item google = fixed("Google", [&m] {
    std::vector<std::string> lines = signInBlockers(m);
    lines.insert(lines.begin(), "Sign in with Google:");
    m.push(std::make_unique<TextScreen>("Google", lines, "info"));
  }, "info", "Real sign-in");
  Items it;
  it.push_back(std::move(google));
  if (m.mockSignIn)  // present only when the simulator was started with --mock-google
    it.push_back(fixed("[MOCK] Test account", [&m] { m.mockSignIn(); m.requestPop(); }, "alert", "Simulator only, fake data"));
  return std::make_unique<ListScreen>("Add account", std::move(it), "Select");
}

// Live list: "Add account" first, then each account (re-read every draw).
class AccountsScreen : public Screen {
 public:
  explicit AccountsScreen(AppManager& m) : m_(m) {}
  bool onKey(Key k) override {
    int n = count();
    sel_ = std::min(sel_, n - 1);
    if (k == Key::Up) sel_ = (sel_ + n - 1) % n;
    else if (k == Key::Down) sel_ = (sel_ + 1) % n;
    else if (k == Key::Ok || k == Key::Soft1) {
      if (!m_.accounts) return true;
      if (sel_ == 0) m_.push(makeAddAccount(m_));
      else m_.push(makeAccountDetail(m_, m_.accounts->accounts()[sel_ - 1].id));
    } else return false;
    return true;
  }
  View view() const override {
    View v;
    v.title = "Accounts";
    Row add;
    add.text = "Add account";
    add.sub = m_.accounts ? "Google" : "Service unavailable";
    add.lead = "contacts";
    v.rows.push_back(add);
    if (m_.accounts)
      for (auto& a : m_.accounts->accounts()) {
        Row r;
        r.text = a.name;
        r.sub = a.email;
        r.lead = avatar(a.name);
        r.trail = statusText(a.status);
        r.tone = a.status == AccountStatus::Active ? 0 : 1;
        v.rows.push_back(std::move(r));
      }
    v.selected = std::min(sel_, int(v.rows.size()) - 1);
    v.keys = {"", "Open", "Back"};
    return v;
  }
 private:
  int count() const { return 1 + (m_.accounts ? int(m_.accounts->accounts().size()) : 0); }
  AppManager& m_;
  int sel_ = 0;
};

// ---- VPN screens (real VpnManager; the backend may be simulated, and then the UI says so) ----
std::string bytesText(unsigned long long b) {
  char s[24];
  if (b < 1024) std::snprintf(s, sizeof s, "%llu B", b);
  else if (b < 1024ULL * 1024) std::snprintf(s, sizeof s, "%.1f KB", b / 1024.0);
  else std::snprintf(s, sizeof s, "%.1f MB", b / 1048576.0);
  return s;
}

// "Protected" is shown ONLY for a verified, non-simulated tunnel.
std::string vpnHeadline(const VpnStatus& s) {
  switch (s.state) {
    case VpnState::Disconnected: return "Not connected";
    case VpnState::Connecting: return "Connecting...";
    case VpnState::Reconnecting: return "Reconnecting...";
    case VpnState::Error: return "Not protected";
    case VpnState::Connected:
      if (s.simulated) return "Simulated tunnel";
      return s.protectionVerified ? "Protected" : "Connected, not verified";
  }
  return "";
}

std::string vpnDetail(const VpnStatus& s) {
  if (s.state == VpnState::Disconnected) return "Choose a profile";
  if (s.simulated) return "SIMULATED (not real)";
  if (s.state == VpnState::Connected && s.error.empty()) return "rx " + bytesText(s.rxBytes) + "  tx " + bytesText(s.txBytes);
  return s.error.empty() ? "" : s.error;
}

std::string profileFlags(const VpnProfile& p) {
  if (!p.secretsOk) return "Key unavailable: import again";
  std::string f = isFullTunnel(p.cfg) ? "Full tunnel" : "Split tunnel";
  if (p.killSwitch) f += ", kill switch";
  return f;
}

std::unique_ptr<Screen> makeVpnImport(AppManager& m);
std::unique_ptr<Screen> makeVpnProfile(AppManager& m, const std::string& id);

class VpnScreen : public Screen {
 public:
  explicit VpnScreen(AppManager& m) : m_(m) {}
  bool onKey(Key k) override {
    if (!m_.vpn || !m_.vpnStore) return false;
    int n = 2 + int(m_.vpnStore->profiles().size());  // status row, profiles..., import row
    sel_ = std::min(sel_, n - 1);
    const auto& profs = m_.vpnStore->profiles();
    if (k == Key::Up) sel_ = (sel_ + n - 1) % n;
    else if (k == Key::Down) sel_ = (sel_ + 1) % n;
    else if (k == Key::Ok) {
      if (sel_ == 0) { if (m_.vpn->status().state != VpnState::Disconnected) m_.vpn->disconnect(); }
      else if (sel_ == n - 1) m_.push(makeVpnImport(m_));
      else {
        const std::string id = profs[sel_ - 1].id;
        const VpnStatus& st = m_.vpn->status();
        if (st.profileId == id && st.state != VpnState::Disconnected) m_.vpn->disconnect();
        else m_.vpn->connect(id);
      }
    } else if (k == Key::Soft1 && sel_ >= 1 && sel_ < n - 1) m_.push(makeVpnProfile(m_, profs[sel_ - 1].id));
    else return false;
    return true;
  }
  View view() const override {
    View v;
    v.title = "VPN";
    if (!m_.vpn || !m_.vpnStore) {
      Row r;
      r.text = "VPN service unavailable";
      v.rows.push_back(r);
      v.keys = {"", "", "Back"};
      return v;
    }
    const VpnStatus& st = m_.vpn->status();
    Row head;
    head.text = vpnHeadline(st);
    head.sub = vpnDetail(st);
    head.lead = "vpn";
    head.trail = st.trafficBlocked ? "Blocked" : "";
    head.tone = (st.state == VpnState::Error || st.state == VpnState::Reconnecting) ? 1 : 0;
    v.rows.push_back(head);
    for (auto& p : m_.vpnStore->profiles()) {
      Row r;
      r.text = p.name;
      r.sub = profileFlags(p);
      r.lead = "vpn";
      r.tone = p.secretsOk ? 0 : 1;
      r.trail = st.profileId == p.id && st.state != VpnState::Disconnected ? "On" : "";
      v.rows.push_back(std::move(r));
    }
    Row imp;
    imp.text = "Import profile";
    imp.sub = "WireGuard .conf file";
    imp.lead = "files";
    v.rows.push_back(imp);
    int n = int(v.rows.size());
    v.selected = std::min(sel_, n - 1);
    std::string center = "Open";
    if (v.selected == 0) center = st.state == VpnState::Disconnected ? "" : "Disconnect";
    else if (v.selected < n - 1) center = st.profileId == m_.vpnStore->profiles()[v.selected - 1].id && st.state != VpnState::Disconnected ? "Disconnect" : "Connect";
    v.keys = {v.selected >= 1 && v.selected < n - 1 ? "Options" : "", center, "Back"};
    return v;
  }
 private:
  AppManager& m_;
  int sel_ = 0;
};

std::unique_ptr<Screen> makeVpnProfile(AppManager& m, const std::string& id) {
  auto prof = [&m, id]() -> const VpnProfile* { return m.vpnStore ? m.vpnStore->find(id) : nullptr; };
  auto flag = [&m, id, prof](const char* label, const char* sub, bool VpnProfile::*field) {
    Item i = fixed(label, [&m, id, prof, field] {
      if (const VpnProfile* p = prof()) {
        VpnProfile c = *p;
        c.*field = !(c.*field);
        m.vpnStore->update(c);
        if (m.vpn && m.vpn->status().profileId == id && m.vpn->status().state != VpnState::Disconnected)
          m.notifications.add("VPN", "Applies the next time you connect");
      }
    }, "vpn", sub);
    i.toggle = [prof, field] { const VpnProfile* p = prof(); return p && (*p).*field ? 1 : 0; };
    return i;
  };
  Item server = fixed("Server", {}, "info");
  server.sub = [prof] {
    const VpnProfile* p = prof();
    if (!p || p->cfg.peers.empty() || p->cfg.peers[0].endpointHost.empty()) return std::string("(none)");
    return p->cfg.peers[0].endpointHost + ":" + std::to_string(p->cfg.peers[0].endpointPort);
  };
  Item del = fixed("Delete profile", [&m, id] {
    Item yes = fixed("Delete profile and key", [&m, id] {
      if (m.vpn && m.vpn->status().profileId == id) m.vpn->disconnect();
      if (m.vpnStore) m.vpnStore->remove(id);
      m.requestPop(2);
    }, "alert");
    yes.tone = 1;
    Items it;
    it.push_back(std::move(yes));
    it.push_back(fixed("Cancel", [&m] { m.requestPop(1); }));
    m.push(std::make_unique<ListScreen>("Delete profile?", std::move(it), "Select"));
  }, "alert");
  del.tone = 1;
  Items it;
  it.push_back(std::move(server));
  it.push_back(flag("Kill switch", "Block if VPN drops", &VpnProfile::killSwitch));
  it.push_back(flag("Reconnect", "Retry automatically", &VpnProfile::autoReconnect));
  it.push_back(flag("LAN access", "Printers, router", &VpnProfile::allowLanWhenBlocked));
  it.push_back(std::move(del));
  const VpnProfile* p0 = prof();
  return std::make_unique<ListScreen>(p0 ? p0->name : std::string("Profile"), std::move(it), "Change");
}

// Lists import/*.conf. A successful import deletes the source file so the key is not left unencrypted on flash.
class VpnImportScreen : public Screen {
 public:
  explicit VpnImportScreen(AppManager& m) : m_(m) {}
  bool onKey(Key k) override {
    auto f = files();
    int n = int(f.size());
    if (!n) return false;
    sel_ = std::min(sel_, n - 1);
    if (k == Key::Up) sel_ = (sel_ + n - 1) % n;
    else if (k == Key::Down) sel_ = (sel_ + 1) % n;
    else if (k == Key::Ok) doImport(f[sel_]);
    else return false;
    return true;
  }
  View view() const override {
    View v;
    v.title = "Import VPN";
    auto f = files();
    for (auto& name : f) { Row r; r.text = name; r.lead = "file"; r.sub = "WireGuard config"; v.rows.push_back(r); }
    if (f.empty()) { Row r; r.text = "No .conf files"; r.sub = m_.files ? "Put them in the import folder" : "Storage unavailable"; v.rows.push_back(r); }
    else v.selected = std::min(sel_, int(f.size()) - 1);
    v.keys = {"", f.empty() ? "" : "Import", "Back"};
    return v;
  }
 private:
  std::vector<std::string> files() const {
    std::vector<std::string> out;
    if (!m_.files) return out;
    for (auto& n : m_.files->list("import/"))
      if (n.size() > 5 && n.compare(n.size() - 5, 5, ".conf") == 0) out.push_back(n);
    return out;
  }
  void doImport(const std::string& file) {
    std::string text;
    if (!m_.vpnStore || !m_.files->read("import/" + file, text)) { m_.notifications.add("VPN", "Could not read " + file); return; }
    VpnImportResult r = m_.vpnStore->import(file.substr(0, file.size() - 5), text);
    if (!r.ok) { m_.notifications.add("VPN import failed", r.error); return; }
    bool wiped = m_.files->remove("import/" + file);
    m_.notifications.add("VPN imported", wiped ? "Source file deleted so the key is not left unencrypted" : "Delete the source file yourself: it still holds the key");
    m_.requestPop();
  }
  AppManager& m_;
  int sel_ = 0;
};

std::unique_ptr<Screen> makeVpnImport(AppManager& m) { return std::make_unique<VpnImportScreen>(m); }

// ---- Gallery: photos from DCIM/ and videos from Videos/ on storage ----
std::string fileSize(size_t b) {
  char s[24];
  if (b < 1024) std::snprintf(s, sizeof s, "%zu B", b);
  else if (b < 1024 * 1024) std::snprintf(s, sizeof s, "%.1f KB", b / 1024.0);
  else std::snprintf(s, sizeof s, "%.1f MB", b / 1048576.0);
  return s;
}

std::string stem(const std::string& n) {
  size_t d = n.rfind('.');
  return d == std::string::npos || d == 0 ? n : n.substr(0, d);
}

class MediaViewerScreen : public Screen {
 public:
  MediaViewerScreen(AppManager& m, std::string key) : m_(m), key_(std::move(key)) {}
  bool onKey(Key k) override {
    auto f = items();
    int i = indexOf(f);
    if (i < 0) return false;
    int n = int(f.size());
    if (k == Key::Left || k == Key::Up) key_ = f[(i + n - 1) % n].key;
    else if (k == Key::Right || k == Key::Down) key_ = f[(i + 1) % n].key;
    else if (k == Key::Soft1) {
      std::string key = key_;
      Item del = fixed("Delete", [this, key] { if (m_.files) m_.files->remove(key); m_.requestPop(2); }, "alert");  // confirm + viewer
      del.tone = 1;
      Items it;
      it.push_back(std::move(del));
      it.push_back(fixed("Cancel", [this] { m_.requestPop(1); }));
      m_.push(std::make_unique<ListScreen>("Delete file?", std::move(it), "Select"));
    } else return false;
    return true;
  }
  View view() const override {
    View v;
    auto f = items();
    int i = indexOf(f);
    v.title = i < 0 ? "Gallery" : stem(f[i].name);
    v.keys = {i < 0 ? "" : "Delete", "", "Back"};
    if (i < 0) { v.layout = Layout::Text; v.lines = {"File not found"}; return v; }
    std::string data;
    m_.files->read(f[i].key, data);
    if (f[i].kind == MediaKind::Image) {
      v.layout = Layout::Photo;
      v.image = f[i].key;
      v.lines = {f[i].name, std::to_string(i + 1) + " / " + std::to_string(f.size()) + "   " + fileSize(data.size())};
    } else {
      v.layout = Layout::Text;
      v.lead = "video";
      v.lines = {f[i].name, fileSize(data.size()), "Video playback is not available yet."};
    }
    return v;
  }
 private:
  std::vector<MediaFile> items() const { return m_.files ? listGallery(*m_.files) : std::vector<MediaFile>(); }
  int indexOf(const std::vector<MediaFile>& f) const {
    for (size_t i = 0; i < f.size(); i++) if (f[i].key == key_) return int(i);
    return -1;
  }
  AppManager& m_;
  std::string key_;
};

class GalleryScreen : public Screen {  // live thumbnail grid (re-read every draw)
 public:
  explicit GalleryScreen(AppManager& m) : m_(m) {}
  bool onKey(Key k) override {
    auto f = items();
    int n = int(f.size());
    if (!n) return false;
    const int cols = 3;
    sel_ = std::min(sel_, n - 1);
    auto lastInCol = [&](int c) { int i = c; while (i + cols < n) i += cols; return i; };
    if (k == Key::Up) sel_ = sel_ >= cols ? sel_ - cols : lastInCol(sel_ % cols);
    else if (k == Key::Down) sel_ = sel_ + cols < n ? sel_ + cols : sel_ % cols;
    else if (k == Key::Left) sel_ = (sel_ + n - 1) % n;
    else if (k == Key::Right) sel_ = (sel_ + 1) % n;
    else if (k == Key::Ok || k == Key::Soft1) m_.push(std::make_unique<MediaViewerScreen>(m_, f[sel_].key));
    else return false;
    return true;
  }
  View view() const override {
    View v;
    v.title = "Gallery";
    v.keys = {"", "", "Back"};
    if (!m_.files) { v.layout = Layout::Text; v.lines = {"Storage unavailable"}; return v; }
    auto f = items();
    if (f.empty()) {
      v.layout = Layout::Text;
      v.lead = "image";
      v.lines = {"No photos or videos yet.", "Take a photo with Camera, or copy files into the DCIM and Videos folders."};
      return v;
    }
    v.layout = Layout::Grid;
    for (auto& x : f) {
      Row r;
      r.text = stem(x.name);
      r.sub = x.name;
      r.lead = x.kind == MediaKind::Video ? "video" : "image";
      if (x.kind == MediaKind::Image) r.image = x.key;
      v.rows.push_back(std::move(r));
    }
    v.selected = std::min(sel_, int(f.size()) - 1);
    v.keys = {"", "Open", "Back"};
    return v;
  }
 private:
  std::vector<MediaFile> items() const { return m_.files ? listGallery(*m_.files) : std::vector<MediaFile>(); }
  AppManager& m_;
  int sel_ = 0;
};

// ---- Media apps. ALL SIMULATED: no audio/video output, tuner or microphone is assumed (hardware unknown). ----
std::string volumeBar(int v) { return "Volume " + std::to_string(v) + "/10"; }

class PlayerScreen : public Screen {  // simulated playback: a timer only. Duration is unknown without a decoder.
 public:
  PlayerScreen(AppManager& m, std::string dir, MediaKind kind, std::string key) : m_(m), dir_(std::move(dir)), kind_(kind), key_(std::move(key)) {}
  bool onKey(Key k) override {
    auto f = items();
    int i = indexOf(f);
    if (i < 0) return false;
    int n = int(f.size());
    if (k == Key::Ok) {
      long long pos = position(f[i]);
      if (playing_) { accum_ = pos; playing_ = false; }
      else { if (dur_ >= 0 && pos >= dur_) pos = 0; accum_ = pos; start_ = m_.nowMs(); playing_ = true; }  // play at the end restarts
    } else if (k == Key::Left || k == Key::Right) {  // seek 10 s; the end of a known-length file stops there
      long long pos = std::max(0LL, position(f[i]) + (k == Key::Right ? 10000 : -10000));
      if (dur_ >= 0) pos = std::min(pos, dur_);
      accum_ = pos;
      start_ = m_.nowMs();
    } else if (k == Key::Num1 || k == Key::Num3) {
      key_ = f[(i + (k == Key::Num3 ? 1 : n - 1)) % n].key;
      accum_ = 0;
      start_ = m_.nowMs();
      dur_ = -2;
    } else if (k == Key::Up) vol_ = std::min(10, vol_ + 1);
    else if (k == Key::Down) vol_ = std::max(0, vol_ - 1);
    else return false;
    return true;
  }
  View view() const override {
    View v;
    v.layout = Layout::Text;
    v.lead = kind_ == MediaKind::Video ? "video" : "music";
    auto f = items();
    int i = indexOf(f);
    v.keys = {"", "", "Back"};
    if (i < 0) { v.title = kind_ == MediaKind::Video ? "Videos" : "Music"; v.lines = {"File not found"}; return v; }
    v.title = stem(f[i].name);
    long long el = position(f[i]);
    bool done = dur_ >= 0 && el >= dur_, live = playing_ && !done;
    v.keys.center = live ? "Pause" : "Play";
    v.lines = {f[i].name, std::string(done ? "Finished " : live ? "Playing " : "Paused ") + formatClock(el) + (dur_ >= 0 ? " / " + formatClock(dur_) : "") + "   " + std::to_string(i + 1) + "/" + std::to_string(f.size()), volumeBar(vol_),
               kind_ == MediaKind::Video ? "SIMULATED: timer only, no video decoder." : "SIMULATED: timer only, no audio output.",
               dur_ >= 0 ? "Left/Right seek 10 s, 1/3 track" : "Length unknown. Left/Right seek 10 s, 1/3 track"};
    return v;
  }
 private:
  std::vector<MediaFile> items() const { return m_.files ? listMedia(*m_.files, dir_, kind_) : std::vector<MediaFile>(); }
  int indexOf(const std::vector<MediaFile>& f) const {
    for (size_t i = 0; i < f.size(); i++) if (f[i].key == key_) return int(i);
    return -1;
  }
  // Current position in ms. The length is known only for WAV (read once per track); everything else has no decoder, so no length.
  long long position(const MediaFile& f) const {
    if (dur_ == -2) { std::string d; dur_ = m_.files && m_.files->read(f.key, d) ? wavDurationMs(d) : -1; }
    long long p = accum_ + (playing_ ? m_.nowMs() - start_ : 0);
    return dur_ >= 0 ? std::min(p, dur_) : p;
  }
  AppManager& m_;
  std::string dir_;
  MediaKind kind_;
  std::string key_;
  mutable long long dur_ = -2;  // -2 not read yet, -1 unknown
  bool playing_ = false;
  long long start_ = 0, accum_ = 0;
  int vol_ = 5;
};

std::unique_ptr<Screen> makeMediaList(AppManager& m, const std::string& title, const std::string& dir, MediaKind kind, const std::string& emptyHint) {
  if (!m.files) return std::make_unique<TextScreen>(title, std::vector<std::string>{"Storage unavailable"}, "alert");
  auto files = listMedia(*m.files, dir, kind);
  if (files.empty()) return std::make_unique<TextScreen>(title, std::vector<std::string>{emptyHint}, kind == MediaKind::Video ? "video" : "music");
  Items it;
  for (auto& f : files) {
    std::string key = f.key;
    it.push_back(fixed(stem(f.name), [&m, dir, kind, key] { m.push(std::make_unique<PlayerScreen>(m, dir, kind, key)); }, kind == MediaKind::Video ? "video" : "music", f.name));
  }
  return std::make_unique<ListScreen>(title, std::move(it), "Play");
}

class RadioScreen : public Screen {  // SIMULATED tuner: remembers a frequency, never receives anything
 public:
  explicit RadioScreen(AppManager&) {}
  bool onKey(Key k) override {
    int d = digitOf(k);
    if (k == Key::Ok) on_ = !on_;
    else if (k == Key::Left) freq_ = freq_ <= 875 ? 1080 : freq_ - 1;
    else if (k == Key::Right) freq_ = freq_ >= 1080 ? 875 : freq_ + 1;
    else if (k == Key::Up) vol_ = std::min(10, vol_ + 1);
    else if (k == Key::Down) vol_ = std::max(0, vol_ - 1);
    else if (k == Key::Soft1) { presets_[next_] = freq_; next_ = (next_ + 1) % 9; }
    else if (d >= 1 && presets_[d - 1]) freq_ = presets_[d - 1];
    else return false;
    return true;
  }
  View view() const override {
    View v;
    v.title = "FM Radio";
    v.layout = Layout::Text;
    v.lead = "radio";
    char f[24];
    std::snprintf(f, sizeof f, "%d.%d MHz", freq_ / 10, freq_ % 10);
    std::string p;
    for (int i = 0; i < 9; i++)
      if (presets_[i]) {
        char b[24];
        std::snprintf(b, sizeof b, "%s%d: %d.%d", p.empty() ? "" : "   ", i + 1, presets_[i] / 10, presets_[i] % 10);
        p += b;
      }
    v.lines = {f, std::string(on_ ? "On" : "Off") + "   " + volumeBar(vol_), p.empty() ? "No presets. Save adds one." : p, "SIMULATED: no FM tuner assumed; nothing is received."};
    v.keys = {"Save", on_ ? "Off" : "On", "Back"};
    return v;
  }
 private:
  bool on_ = false;
  int freq_ = 987, vol_ = 5, next_ = 0;  // tenths of MHz, 87.5-108.0
  int presets_[9] = {};
};

class RecorderScreen : public Screen {  // SIMULATED: writes a valid WAV that holds silence
 public:
  explicit RecorderScreen(AppManager& m) : m_(m) {}
  bool onKey(Key k) override {
    if (k == Key::Ok) {
      if (!m_.files) note_ = "Storage unavailable";
      else if (!rec_) { rec_ = true; start_ = m_.nowMs(); note_.clear(); }
      else {
        rec_ = false;
        save();
      }
    } else if ((k == Key::Left || k == Key::Right) && !rec_) rate_ = rate_ == 8000 ? 16000 : 8000;   // quality cannot change mid-recording
    else if (k == Key::Back && rec_) { rec_ = false; save(); }                                      // Back saves the take, a second Back leaves
    else if (k == Key::Soft1) m_.push(makeMediaList(m_, "Recordings", kRecDir, MediaKind::Audio, "No recordings yet."));
    else return false;
    return true;
  }
  View view() const override {
    View v;
    v.title = "Recorder";
    v.layout = Layout::Text;
    v.lead = "mic";
    v.lines = {rec_ ? "Recording " + formatClock(m_.nowMs() - start_) : "Ready", note_.empty() ? "SIMULATED: no microphone assumed; saves silence." : note_,
               std::string("Quality: ") + (rate_ == 8000 ? "Voice 8 kHz" : "Standard 16 kHz") + (rec_ ? "" : "  (Left/Right)")};
    v.keys = {"Recordings", rec_ ? "Stop" : "Record", "Back"};
    return v;
  }
 private:
  void save() {
    int sec = int(std::min<long long>(600, (m_.nowMs() - start_) / 1000));
    std::string name = nextRecordingName(*m_.files), wav = makeWav(std::max(1, sec), rate_);
    note_ = !wav.empty() && m_.files->write(kRecDir + name, wav) ? "Saved " + name + " (simulated silence)" : "Could not save the recording";
  }
  AppManager& m_;
  bool rec_ = false;
  int rate_ = 8000;
  long long start_ = 0;
  std::string note_;
};

// ---- Calendar (local events; Google Calendar sync is a later phase) ----
const char* const kMonthNames[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
const char* const kDayNames[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

Date todayOf(AppManager& m) {
  std::time_t t = m.wall();
  std::tm* tm = std::localtime(&t);
  return {tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday};
}

std::string dateCaption(const Date& d) {
  return std::string(kDayNames[weekday(d.y, d.m, d.d)]) + " " + std::to_string(d.d) + " " + kMonthNames[d.m - 1] + " " + std::to_string(d.y);
}

class EventEditScreen : public Screen {  // time on the keypad (HHMM). Titles need text entry, which is not built yet.
 public:
  EventEditScreen(AppManager& m, Date d) : m_(m), d_(d) {}
  bool onKey(Key k) override {
    int d = digitOf(k);
    if (d >= 0 && buf_.size() < 4) { buf_ += char('0' + d); note_.clear(); }
    else if ((k == Key::Soft2 || k == Key::Back) && !buf_.empty()) { buf_.pop_back(); note_.clear(); }
    else if (k == Key::Ok || k == Key::Soft1) {
      if (buf_.size() != 4) { note_ = "Enter 4 digits (HHMM)"; return true; }
      int h = std::atoi(buf_.substr(0, 2).c_str()), mi = std::atoi(buf_.substr(2).c_str());
      if (h > 23 || mi > 59) { note_ = "Invalid time"; return true; }
      if (!m_.events || !m_.events->add(d_, h * 60 + mi, 60, "Event")) { note_ = "Could not save the event"; return true; }
      m_.requestPop();
    } else return false;
    return true;
  }
  View view() const override {
    View v;
    v.title = "New event";
    v.layout = Layout::Dial;
    std::string p = buf_;
    while (p.size() < 4) p += '_';
    v.lines = {p.substr(0, 2) + ":" + p.substr(2), note_.empty() ? "Time only for now (no text entry yet)" : note_};
    v.keys = {"Save", "", buf_.empty() ? "Back" : "Clear"};
    return v;
  }
 private:
  AppManager& m_;
  Date d_;
  std::string buf_, note_;
};

// Live list of one day's events (re-read every draw) with "Add event" first.
class AgendaScreen : public Screen {
 public:
  AgendaScreen(AppManager& m, Date d) : m_(m), d_(d) {}
  bool onKey(Key k) override {
    int n = 1 + int(list().size());
    sel_ = std::min(sel_, n - 1);
    if (k == Key::Up) sel_ = (sel_ + n - 1) % n;
    else if (k == Key::Down) sel_ = (sel_ + 1) % n;
    else if (k == Key::Soft1 || (k == Key::Ok && sel_ == 0)) { if (m_.events) m_.push(std::make_unique<EventEditScreen>(m_, d_)); }
    else if (k == Key::Ok) {
      int id = list()[sel_ - 1].id;
      Item del = fixed("Delete event", [this, id] { if (m_.events) m_.events->remove(id); m_.requestPop(1); }, "alert");
      del.tone = 1;
      Items it;
      it.push_back(std::move(del));
      it.push_back(fixed("Cancel", [this] { m_.requestPop(1); }));
      m_.push(std::make_unique<ListScreen>("Delete event?", std::move(it), "Select"));
    } else return false;
    return true;
  }
  View view() const override {
    View v;
    v.title = std::to_string(d_.d) + " " + std::string(kMonthNames[d_.m - 1]).substr(0, 3) + " " + std::to_string(d_.y);
    Row add;
    add.text = "Add event";
    add.sub = m_.events ? "Set a time" : "Events unavailable";
    add.lead = "calendar";
    v.rows.push_back(add);
    for (auto& e : list()) {
      Row r;
      r.text = e.startMin < 0 ? "All day" : hhmm(e.startMin);
      r.sub = e.title;
      r.lead = "clock";
      v.rows.push_back(std::move(r));
    }
    v.selected = std::min(sel_, int(v.rows.size()) - 1);
    v.keys = {"", v.selected == 0 ? "Add" : "Delete", "Back"};
    return v;
  }
 private:
  std::vector<Event> list() const { return m_.events ? m_.events->forDay(d_) : std::vector<Event>(); }
  AppManager& m_;
  Date d_;
  int sel_ = 0;
};

class CalendarScreen : public Screen {  // Mon-first month grid. Left/Right = day, Up/Down = week, OK = that day's events
 public:
  explicit CalendarScreen(AppManager& m) : m_(m), cur_(todayOf(m)) {}
  bool onKey(Key k) override {
    if (k == Key::Left) cur_ = addDays(cur_, -1);
    else if (k == Key::Right) cur_ = addDays(cur_, 1);
    else if (k == Key::Up) cur_ = addDays(cur_, -7);
    else if (k == Key::Down) cur_ = addDays(cur_, 7);
    else if (k == Key::Soft1) cur_ = todayOf(m_);
    else if (k == Key::Ok) m_.push(std::make_unique<AgendaScreen>(m_, cur_));
    else return false;
    return true;
  }
  View view() const override {
    View v;
    v.title = "Calendar";
    v.layout = Layout::Calendar;
    Date t = todayOf(m_);
    std::string mask;
    for (int d = 1; d <= daysInMonth(cur_.y, cur_.m); d++) mask += m_.events && m_.events->hasEvents({cur_.y, cur_.m, d}) ? '1' : '0';
    v.lines = {std::string(kMonthNames[cur_.m - 1]) + " " + std::to_string(cur_.y),
               std::to_string((weekday(cur_.y, cur_.m, 1) + 6) % 7),  // Monday-first offset of the 1st
               std::to_string(daysInMonth(cur_.y, cur_.m)),
               std::to_string(cur_.d),
               std::to_string(t.y == cur_.y && t.m == cur_.m ? t.d : 0),
               dateCaption(cur_),
               mask};
    v.keys = {"Today", "Day", "Back"};
    return v;
  }
 private:
  AppManager& m_;
  Date cur_;
};

// ---- Games (logic in core/games.cpp; these screens only translate keys and draw boards) ----
class SnakeScreen : public Screen {
 public:
  explicit SnakeScreen(AppManager& m) : m_(m), s_(kW, kH, seed(m)) {}
  bool onKey(Key k) override {
    if (k == Key::Up) s_.turn(Dir::Up);
    else if (k == Key::Down) s_.turn(Dir::Down);
    else if (k == Key::Left) s_.turn(Dir::Left);
    else if (k == Key::Right) s_.turn(Dir::Right);
    else if (k == Key::Ok || k == Key::Soft1) {
      if (!s_.alive()) { s_ = Snake(kW, kH, seed(m_)); started_ = false; paused_ = false; }
      else paused_ = !paused_;
    } else return false;
    return true;
  }
  bool onTick(long long now) override {
    if (!started_) { started_ = true; last_ = now; return false; }
    if (paused_ || !s_.alive()) { last_ = now; return false; }
    const long long period = std::max(90, 200 - 6 * s_.score());  // gets faster with every food
    bool changed = false;
    for (int steps = 0; now - last_ >= period && steps < 5; steps++) {
      s_.step();
      last_ += period;
      changed = true;
      if (!s_.alive()) break;
    }
    if (now - last_ >= period) last_ = now;  // after a long stall, do not fast-forward
    return changed;
  }
  View view() const override {
    View v;
    v.title = "Snake";
    v.layout = Layout::Board;
    v.lines.push_back("Score " + std::to_string(s_.score()) + (!s_.alive() ? "   Game over" : paused_ ? "   Paused" : ""));
    for (auto& r : s_.board()) v.lines.push_back(r);
    v.keys = {"", !s_.alive() ? "Retry" : paused_ ? "Resume" : "Pause", "Back"};
    return v;
  }
 private:
  static constexpr int kW = 14, kH = 13;
  static uint32_t seed(AppManager& m) { return uint32_t(m.nowMs() & 0xFFFFFFFFLL) | 1u; }
  AppManager& m_;
  Snake s_;
  bool started_ = false, paused_ = false;
  long long last_ = 0;
};

class TicTacToeScreen : public Screen {
 public:
  explicit TicTacToeScreen(AppManager& m) : g_(uint32_t(m.nowMs() & 0xFFFFFFFFLL) | 1u) {}
  bool onKey(Key k) override {
    int col = sel_ % 3, row = sel_ / 3;
    if (k == Key::Left) col = (col + 2) % 3;
    else if (k == Key::Right) col = (col + 1) % 3;
    else if (k == Key::Up) row = (row + 2) % 3;
    else if (k == Key::Down) row = (row + 1) % 3;
    else if (k == Key::Ok || k == Key::Soft1) { if (g_.winner()) g_.reset(); else g_.play(sel_); return true; }
    else return false;
    sel_ = row * 3 + col;
    return true;
  }
  View view() const override {
    View v;
    v.title = "Tic-Tac-Toe";
    v.layout = Layout::Board;
    char w = g_.winner();
    v.lines.push_back(w == 'X' ? "You win!" : w == 'O' ? "Computer wins" : w == 'D' ? "Draw" : "Your turn (X)");
    for (int r = 0; r < 3; r++) { std::string row; for (int c = 0; c < 3; c++) row += g_.at(r * 3 + c); v.lines.push_back(row); }
    v.selected = sel_;
    v.keys = {"", w ? "Again" : "Place", "Back"};
    return v;
  }
 private:
  TicTacToe g_;
  int sel_ = 4;
};

class GuessScreen : public Screen {  // number keys are the natural input here
 public:
  explicit GuessScreen(AppManager& m) : m_(m), g_(seed(m)) {}
  bool onKey(Key k) override {
    int d = digitOf(k);
    if (d >= 0 && !won_ && buf_.size() < 3) { buf_ += char('0' + d); note_.clear(); }
    else if ((k == Key::Soft2 || k == Key::Back) && !buf_.empty()) { buf_.pop_back(); note_.clear(); }
    else if (k == Key::Ok || k == Key::Soft1) {
      if (won_) { g_ = Guess(seed(m_) + ++rounds_); won_ = false; buf_.clear(); note_.clear(); return true; }
      if (buf_.empty()) return true;
      Guess::Result r = g_.guess(std::atoi(buf_.c_str()));
      buf_.clear();
      if (r == Guess::Invalid) note_ = "Pick 1 to 100";
      else if (r == Guess::Low) note_ = "Higher";
      else if (r == Guess::High) note_ = "Lower";
      else { won_ = true; note_ = "Correct! " + std::to_string(g_.tries()) + (g_.tries() == 1 ? " try" : " tries"); }
    } else return false;
    return true;
  }
  View view() const override {
    View v;
    v.title = "Number guess";
    v.layout = Layout::Dial;
    v.lines = {buf_, note_.empty() && !won_ ? "I am thinking of 1 to 100" : note_};
    v.keys = {won_ ? "Again" : "Guess", "", buf_.empty() ? "Back" : "Clear"};
    return v;
  }
 private:
  static uint32_t seed(AppManager& m) { return uint32_t(m.nowMs() & 0xFFFFFFFFLL) | 1u; }
  AppManager& m_;
  Guess g_;
  std::string buf_, note_;
  bool won_ = false;
  uint32_t rounds_ = 0;
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
      tile("Media", "music", "Gallery, Music, Radio", group("Media", {{fixed("Gallery", {}, "image", "Photos & videos"), [&m] { return makeGallery(m); }},
                                                                       {fixed("Music", {}, "music"), [&m] { return makeMusic(m); }},
                                                                       {fixed("Videos", {}, "video"), [&m] { return makeVideos(m); }},
                                                                       {fixed("FM Radio", {}, "radio", "Simulated"), [&m] { return makeRadio(m); }},
                                                                       {fixed("Recorder", {}, "mic", "Simulated"), [&m] { return makeRecorder(m); }}})),
      tile("Calendar", "calendar", "Calendar", [&m] { return makeCalendar(m); }),
      tile("Clock", "clock", "Clock & Alarms", group("Clock", {{fixed("Clock", {}, "clock", "Time and stopwatch"), [&m] { return makeClock(m); }},
                                                                {fixed("Alarms", {}, "alarm"), [&m] { return makeAlarms(m); }}})),
      tile("Games", "games", "Snake & more", group("Games", {{fixed("Snake", {}, "games"), [&m] { return makeSnake(m); }},
                                                             {fixed("Tic-Tac-Toe", {}, "games"), [&m] { return makeTicTacToe(m); }},
                                                             {fixed("Number guess", {}, "games"), [&m] { return makeGuess(m); }}})),
      tile("Tools", "calc", "Calculator & Files", group("Tools", {{fixed("Calculator", {}, "calc"), [&m] { return makeCalculator(m); }},
                                                                  {fixed("File manager", {}, "files"), [&m] { return makeFiles(m); }}})),
      tile("System", "settings", "Settings & Connectivity", group("System", {{fixed("Settings", {}, "settings"), [&m] { return makeSettings(m); }},
                                                                              {fixed("Connectivity", {}, "connect", "Wi-Fi, Bluetooth, data, SIM"), [&m] { return makeConnectivity(m); }},
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
  if (m.contacts)  // contacts on the phone, including ones synced from an account
    for (auto& c : m.contacts->visible()) {
      std::string name = c.name, number = c.number;
      Item r = fixed(name, [&m, name, number] {
        m.push(std::make_unique<TextScreen>(name, std::vector<std::string>{number.empty() ? "No number" : number}, avatar(name), number.empty() ? "" : "Call",
                                            [&m, number] { m.push(makePhone(m, digitsOf(number))); }));
      }, avatar(name), number.empty() ? "No number" : number);
      if (c.source != "local") r.trail = [] { return std::string("G"); };  // synced from an account
      it.push_back(std::move(r));
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

// Device preferences only. Radios (Wi-Fi, Bluetooth, mobile data, airplane, hotspot, SIM) live in Connectivity, one owner each.
std::unique_ptr<Screen> makeSettings(AppManager& m) {
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
  Item acct = fixed("Accounts", [&m] { m.push(makeAccounts(m)); }, "accounts");
  acct.trail = [&m] { return m.accounts ? std::to_string(m.accounts->accounts().size()) : std::string("-"); };
  Items it;
  it.push_back(std::move(br));
  it.push_back(std::move(acct));
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

std::unique_ptr<Screen> makeAccounts(AppManager& m) { return std::make_unique<AccountsScreen>(m); }
std::unique_ptr<Screen> makeVpn(AppManager& m) { return std::make_unique<VpnScreen>(m); }
std::unique_ptr<Screen> makeCalendar(AppManager& m) { return std::make_unique<CalendarScreen>(m); }
std::unique_ptr<Screen> makeGallery(AppManager& m) { return std::make_unique<GalleryScreen>(m); }
std::unique_ptr<Screen> makeMusic(AppManager& m) { return makeMediaList(m, "Music", kMusicDir, MediaKind::Audio, "No music yet. Copy audio files into the Music folder."); }
std::unique_ptr<Screen> makeVideos(AppManager& m) { return makeMediaList(m, "Videos", kVideoDir, MediaKind::Video, "No videos yet. Copy video files into the Videos folder."); }
std::unique_ptr<Screen> makeRadio(AppManager& m) { return std::make_unique<RadioScreen>(m); }
std::unique_ptr<Screen> makeRecorder(AppManager& m) { return std::make_unique<RecorderScreen>(m); }
std::unique_ptr<Screen> makeSnake(AppManager& m) { return std::make_unique<SnakeScreen>(m); }
std::unique_ptr<Screen> makeTicTacToe(AppManager& m) { return std::make_unique<TicTacToeScreen>(m); }
std::unique_ptr<Screen> makeGuess(AppManager& m) { return std::make_unique<GuessScreen>(m); }
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
  Item bt = fixed("Bluetooth", [&m] { m.device.bluetooth = !m.device.bluetooth; m.saveSettings(); }, "bluetooth", "Pairing: not built yet");
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
  Item vpn = fixed("VPN", [&m] { m.push(makeVpn(m)); }, "vpn");
  vpn.trail = [&m] {
    if (!m.vpn) return std::string("-");
    if (m.vpn->status().state == VpnState::Disconnected) return std::string("Off");
    return std::string(m.vpn->status().protectionVerified ? "On" : "...");
  };
  Item hs = fixed("Hotspot", [&m] { m.push(makeHotspot(m)); }, "hotspot");
  hs.trail = [&m] { return std::string(m.device.hotspot ? "On" : "Off"); };
  Items it;
  Item sim = fixed("SIM manager", [&m] { m.push(makeSimManager(m)); }, "sim");
  sim.trail = [&m] { int n = 0; for (auto& x : m.device.sims) n += x.present && x.enabled; return std::to_string(n) + "/" + std::to_string(m.device.sims.size()); };  // active / slots
  for (Item* i : {&wifi, &nets, &bt, &data, &air, &vpn, &hs, &sim}) it.push_back(std::move(*i));
  return std::make_unique<ListScreen>("Connectivity", std::move(it), "Select");
}

}  // namespace fai
