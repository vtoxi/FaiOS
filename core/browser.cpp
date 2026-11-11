#include "browser.h"

#include <algorithm>
#include <cctype>

namespace fai {

static std::string lower(std::string s) {
  for (char& c : s) c = char(std::tolower((unsigned char)c));
  return s;
}

UrlResult normalizeUrl(const std::string& in) {
  UrlResult r;
  size_t a = in.find_first_not_of(" \t"), b = in.find_last_not_of(" \t");
  if (a == std::string::npos) { r.error = "Empty address"; return r; }
  std::string s = in.substr(a, b - a + 1);
  if (s.size() > 2048) { r.error = "Address too long"; return r; }
  for (unsigned char c : s)
    if (c < 0x21 || c == 0x7F) { r.error = "Invalid characters in address"; return r; }  // control chars and spaces
  size_t colon = s.find(':'), slash = s.find('/');
  bool hasScheme = colon != std::string::npos && (slash == std::string::npos || colon < slash) && s.compare(colon, 3, "://") == 0;
  if (!hasScheme && s[0] != '[' && colon != std::string::npos && (slash == std::string::npos || colon < slash)) {
    // "host:8080" is a host and port; anything else with a colon is a scheme (javascript:, tel:, about:...). A bare word
    // before the colon counts as a host only if it is "localhost" or contains a dot, so "tel:123" is not mistaken for one.
    std::string word = s.substr(0, colon);
    bool portOnly = colon + 1 < s.size() && (word == "localhost" || word.find('.') != std::string::npos);
    for (size_t i = colon + 1; i < s.size() && s[i] != '/'; i++) portOnly &= std::isdigit((unsigned char)s[i]) != 0;
    if (!portOnly) { r.error = "Only http and https addresses are supported"; return r; }
  }
  if (hasScheme) {
    std::string sch = lower(s.substr(0, colon));
    if (sch != "http" && sch != "https") { r.error = "Only http and https addresses are supported"; return r; }
  } else {
    s = "https://" + s;  // a bare host defaults to the secure scheme
  }
  size_t hs = s.find("://") + 3, he = s.find_first_of("/?#", hs);
  std::string auth = s.substr(hs, he == std::string::npos ? std::string::npos : he - hs);
  if (auth.find('@') != std::string::npos) { r.error = "Addresses with a user name or password are not allowed"; return r; }
  if (auth.empty() || auth[0] == ':' || auth[0] == '.') { r.error = "Missing host"; return r; }
  for (char c : auth)
    if (!(std::isalnum((unsigned char)c) || c == '.' || c == '-' || c == ':' || c == '[' || c == ']')) { r.error = "Invalid host name"; return r; }
  r.ok = true;
  r.url = s;
  return r;
}

std::string hostOf(const std::string& url) {
  UrlResult r = normalizeUrl(url);
  if (!r.ok) return "";
  size_t hs = r.url.find("://") + 3, he = r.url.find_first_of("/?#:", hs);
  return lower(r.url.substr(hs, he == std::string::npos ? std::string::npos : he - hs));
}

Tab* TabManager::find(int id) {
  for (auto& t : tabs_) if (t.id == id) return &t;
  return nullptr;
}

void TabManager::suspend(Tab& t) {
  if (t.state != TabState::Active) return;
  e_.destroy(t.handle);
  t.handle = -1;
  t.state = TabState::Suspended;
}

bool TabManager::resume(Tab& t) {
  if (t.state == TabState::Active) return true;
  int h = e_.create(t.url);
  if (h < 0) return false;
  t.handle = h;
  t.state = TabState::Active;
  return true;
}

long TabManager::totalKb() {
  long n = 0;
  for (auto& t : tabs_) if (t.state == TabState::Active) n += e_.memoryKb(t.handle);
  return n;
}

OpenResult TabManager::open(const std::string& input, int* idOut, std::string* err) {
  UrlResult u = normalizeUrl(input);
  if (!u.ok) { if (err) *err = u.error; return OpenResult::BadUrl; }
  if (int(tabs_.size()) >= b_.maxTabs) { if (err) *err = "Too many tabs. Close one first."; return OpenResult::TooManyTabs; }
  // make room BEFORE creating another renderer: background tabs are suspended down to maxActiveTabs - 1
  std::vector<Tab*> active;
  for (auto& t : tabs_) if (t.state == TabState::Active) active.push_back(&t);
  std::sort(active.begin(), active.end(), [](Tab* a, Tab* c) { return a->lastUsedMs < c->lastUsedMs; });
  while (long(active.size()) >= b_.maxActiveTabs && !active.empty()) { suspend(*active.front()); active.erase(active.begin()); }
  int h = e_.create(u.url);
  if (h < 0) { if (err) *err = "The browser engine could not open the page"; return OpenResult::EngineFailed; }
  Tab t{nextId_++, u.url, TabState::Active, h, now_()};
  tabs_.push_back(t);
  fg_ = t.id;
  if (idOut) *idOut = t.id;
  enforce();
  return OpenResult::Ok;
}

bool TabManager::close(int id) {
  for (size_t i = 0; i < tabs_.size(); i++)
    if (tabs_[i].id == id) {
      if (tabs_[i].state == TabState::Active) e_.destroy(tabs_[i].handle);
      tabs_.erase(tabs_.begin() + long(i));
      if (fg_ == id) fg_ = tabs_.empty() ? -1 : tabs_.back().id;
      return true;
    }
  return false;
}

bool TabManager::switchTo(int id) {
  Tab* t = find(id);
  if (!t) return false;
  if (t->state == TabState::Suspended) {
    // free a slot first so the live-renderer limit is never exceeded, even briefly
    std::vector<Tab*> active;
    for (auto& x : tabs_) if (x.state == TabState::Active) active.push_back(&x);
    std::sort(active.begin(), active.end(), [](Tab* a, Tab* c) { return a->lastUsedMs < c->lastUsedMs; });
    while (long(active.size()) >= b_.maxActiveTabs && !active.empty()) { suspend(*active.front()); active.erase(active.begin()); }
    if (!resume(*t)) return false;
  }
  t->lastUsedMs = now_();
  fg_ = id;
  enforce();
  return true;
}

void TabManager::enforce() {
  // 1. one tab over its own limit is discarded (even the foreground one: it must not eat the whole device)
  for (auto& t : tabs_)
    if (t.state == TabState::Active && e_.memoryKb(t.handle) > b_.maxTabKb) { suspend(t); discarded_++; }
  // 2. total over budget: suspend least-recently-used background tabs until it fits
  while (totalKb() > b_.maxTotalKb) {
    Tab* victim = nullptr;
    for (auto& t : tabs_)
      if (t.state == TabState::Active && t.id != fg_ && (!victim || t.lastUsedMs < victim->lastUsedMs)) victim = &t;
    if (!victim) break;  // only the foreground tab is left and it is within its own limit
    suspend(*victim);
  }
}

}  // namespace fai
