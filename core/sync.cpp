#include "sync.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>

#include "crypto.h"

namespace fai {

const char* syncStatusName(SyncStatus s) {
  switch (s) {
    case SyncStatus::Ok: return "ok";
    case SyncStatus::Offline: return "offline";
    case SyncStatus::AuthFailed: return "sign-in needed";
    case SyncStatus::Forbidden: return "permission denied";
    default: return "error";
  }
}

namespace {
const char* kPeople = "https://people.googleapis.com/v1/";
const char* kCalendar = "https://www.googleapis.com/calendar/v3/calendars/primary/events";
const int kMaxPages = 50;

std::string safeId(const std::string& id) {  // account id as a storage path component
  std::string o;
  for (char c : id) o += (std::isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
  return o.empty() ? "_" : o;
}

// Server-supplied identifiers go into URLs, so only plain characters are accepted (no path or query injection).
bool plainId(const std::string& s) {
  if (s.empty() || s.size() > 1024) return false;
  for (char c : s) if (!(std::isalnum((unsigned char)c) || c == '-' || c == '_')) return false;
  return true;
}
bool personName(const std::string& rn) { return rn.compare(0, 7, "people/") == 0 && plainId(rn.substr(7)); }

std::string clip(const std::string& in, size_t n) {
  std::string o;
  for (unsigned char c : in) if (c >= 0x20 && c != 0x7f) o += char(c);
  if (o.size() > n) o.resize(n);
  return o;
}

bool isConflict(const HttpResponse& h) {
  return h.status == 409 || h.status == 412 || (h.status == 400 && h.body.find("FAILED_PRECONDITION") != std::string::npos);
}
bool isExpired(const HttpResponse& h) {
  return h.status == 410 || (h.status == 400 && h.body.find("EXPIRED_SYNC_TOKEN") != std::string::npos);
}
bool ok2xx(const HttpResponse& h) { return h.status >= 200 && h.status < 300; }
}  // namespace

HttpResponse GoogleSync::call(const std::string& method, const std::string& url, const std::string& token, const std::string& body, const std::string& ifMatch) {
  HttpRequest r;
  r.method = method;
  r.url = url;
  r.body = body;
  r.headers.push_back({"Authorization", "Bearer " + token});  // never logged
  if (!body.empty()) r.headers.push_back({"Content-Type", "application/json"});
  if (!ifMatch.empty()) r.headers.push_back({"If-Match", ifMatch});
  return http_.send(r);
}

bool GoogleSync::fail(const HttpResponse& h, SyncResult& r) {
  if (ok2xx(h)) return false;
  if (h.status == 0) { r.status = SyncStatus::Offline; r.detail = "No connection"; }
  else if (h.status == 401) { r.status = SyncStatus::AuthFailed; r.detail = "Sign in again"; }
  else if (h.status == 403) { r.status = SyncStatus::Forbidden; r.detail = "Permission denied by the account"; }
  else { r.status = SyncStatus::Error; r.detail = "Server error " + std::to_string(h.status); }
  return true;
}

std::string GoogleSync::loadToken(const std::string& id, const char* svc) {
  std::string t;
  Json j;
  if (s_.read("sync/" + safeId(id) + "/" + svc + ".json", t) && Json::parse(t, j)) return j["token"].asString();
  return "";
}

void GoogleSync::saveToken(const std::string& id, const char* svc, const std::string& tok) {
  Json j = Json::makeObject();
  j.set("token", Json::makeString(tok));
  s_.write("sync/" + safeId(id) + "/" + svc + ".json", j.dump());
}

// Fetches every page before anything is applied, so a failure half way changes nothing.
bool GoogleSync::getAllPages(const std::string& baseUrl, const std::string& token, const char* itemsKey, std::vector<Json>& items, std::string& nextSync, SyncResult& r, bool& expired) {
  expired = false;
  std::string page;
  for (int n = 0; n < kMaxPages; n++) {
    HttpResponse h = call("GET", baseUrl + (page.empty() ? "" : "&pageToken=" + percentEncode(page)), token);
    if (isExpired(h)) { expired = true; return true; }
    if (fail(h, r)) return false;
    Json j;
    if (!Json::parse(h.body, j) || !j.isObject()) { r.status = SyncStatus::Error; r.detail = "Unreadable server reply"; return false; }
    for (auto& it : j[itemsKey].items()) items.push_back(it);
    page = j["nextPageToken"].asString();
    if (page.empty()) {
      nextSync = j["nextSyncToken"].asString();
      if (nextSync.empty()) { r.status = SyncStatus::Error; r.detail = "No sync token from server"; return false; }
      return true;
    }
  }
  r.status = SyncStatus::Error;
  r.detail = "Too many pages";
  return false;
}

void GoogleSync::forget(const std::string& id) {
  auto& c = contacts_.raw();
  c.erase(std::remove_if(c.begin(), c.end(), [&](const StoredContact& x) { return x.source == id; }), c.end());
  contacts_.save();
  auto& e = events_.raw();
  e.erase(std::remove_if(e.begin(), e.end(), [&](const Event& x) { return x.source == id; }), e.end());
  events_.save();
  saveToken(id, "contacts", "");
  saveToken(id, "calendar", "");
}

// ------------------------------------------------------------------ contacts

static Json personBody(const std::string& name, const std::string& number, const std::string& etag) {
  Json p = Json::makeObject();
  if (!etag.empty()) p.set("etag", Json::makeString(etag));
  Json names = Json::makeArray();
  Json n = Json::makeObject();
  n.set("givenName", Json::makeString(name));  // the whole name goes in givenName: simple and always writable
  names.push(n);
  p.set("names", names);
  Json nums = Json::makeArray();
  if (!number.empty()) {
    Json v = Json::makeObject();
    v.set("value", Json::makeString(number));
    nums.push(v);
  }
  p.set("phoneNumbers", nums);
  return p;
}

static void personFields(const Json& c, std::string& name, std::string& number) {
  name = ContactStore::cleanName(c["names"].at(0)["displayName"].asString());
  if (name.empty()) name = ContactStore::cleanName(c["names"].at(0)["givenName"].asString());
  number = ContactStore::cleanNumber(c["phoneNumbers"].at(0)["value"].asString());
  if (name.empty()) name = number;  // a contact with only a number is still useful
}

void GoogleSync::applyContact(const std::string& id, const Json& c, SyncResult& r) {
  const std::string rid = c["resourceName"].asString();
  if (!personName(rid)) { r.skipped++; return; }
  auto& all = contacts_.raw();
  auto it = std::find_if(all.begin(), all.end(), [&](const StoredContact& x) { return x.remoteId == rid && x.source == id; });
  const bool gone = c["metadata"]["deleted"].asBool();
  if (gone) {
    if (it == all.end()) return;
    if (!it->dirty) { all.erase(it); r.removed++; }
    else { it->remoteId.clear(); it->etag.clear(); it->source = "local"; r.conflicts++; }  // deleted remotely but edited here: keep it, re-upload as new
    return;
  }
  std::string name, number;
  personFields(c, name, number);
  if (name.empty()) { r.skipped++; return; }
  const std::string etag = c["etag"].asString();
  if (it == all.end()) {
    if (all.size() >= ContactStore::kMax) { r.skipped++; return; }
    StoredContact n;
    n.id = contacts_.nextId();
    n.name = name; n.number = number; n.source = id; n.remoteId = rid; n.etag = etag;
    all.push_back(n);
    r.pulled++;
    return;
  }
  if (it->deleted) return;  // deleted locally: the tombstone is pushed below, do not resurrect it
  if (!it->dirty) { it->name = name; it->number = number; it->etag = etag; r.pulled++; return; }
  if (it->etag == etag) return;  // remote unchanged since we last saw it: our edit stands and will be pushed
  // both sides changed: remote wins the original, the local edit becomes a separate copy
  if (all.size() < ContactStore::kMax) {
    StoredContact copy;
    copy.id = contacts_.nextId();
    copy.name = ContactStore::cleanName(it->name + " (local)");
    copy.number = it->number;
    copy.dirty = true;
    it->name = name; it->number = number; it->etag = etag; it->dirty = false;
    all.push_back(copy);  // may invalidate `it`; not used after this
  } else { it->name = name; it->number = number; it->etag = etag; it->dirty = false; }
  r.conflicts++;
  r.pulled++;
}

bool GoogleSync::resolveContactConflict(StoredContact& local, const std::string&, const std::string& token, SyncResult& r) {
  HttpResponse h = call("GET", std::string(kPeople) + local.remoteId + "?personFields=names,phoneNumbers", token);
  if (h.status == 404) { local.remoteId.clear(); local.etag.clear(); local.source = "local"; return true; }  // gone remotely: re-create from the local copy
  if (fail(h, r)) return false;
  Json j;
  if (!Json::parse(h.body, j)) { r.status = SyncStatus::Error; r.detail = "Unreadable server reply"; return false; }
  std::string name, number;
  personFields(j, name, number);
  StoredContact copy;
  copy.name = ContactStore::cleanName(local.name + " (local)");
  copy.number = local.number;
  copy.dirty = true;
  local.etag = j["etag"].asString();  // the edit is retried (or kept as a copy) against the current version
  if (!name.empty()) { local.name = name; local.number = number; local.dirty = false; }
  else copy.dirty = false;  // remote version has no usable fields: no copy needed, the original is retried below
  if (!copy.dirty) { r.conflicts++; return true; }
  if (contacts_.raw().size() < ContactStore::kMax) { copy.id = contacts_.nextId(); contacts_.raw().push_back(copy); }  // push_back invalidates `local`
  r.conflicts++;
  return true;
}

bool GoogleSync::pushContacts(const std::string& id, const std::string& token, SyncResult& r) {
  // index-based: resolveContactConflict may append to the vector
  std::map<int, int> tries;  // bounds retries of one item per sync, so a misbehaving server cannot loop us
  for (size_t i = 0; i < contacts_.raw().size(); i++) {
    StoredContact c = contacts_.raw()[i];
    if (!c.dirty) continue;
    if (c.source != "local" && c.source != id) continue;  // belongs to another account
    if (++tries[c.id] > 3) { r.skipped++; continue; }
    auto store = [&](const StoredContact& n) { contacts_.raw()[i] = n; };
    if (c.deleted) {
      if (c.remoteId.empty()) { contacts_.raw().erase(contacts_.raw().begin() + long(i)); i--; continue; }
      HttpResponse h = call("DELETE", std::string(kPeople) + c.remoteId + ":deleteContact", token);
      if (h.status == 404 || ok2xx(h)) { contacts_.raw().erase(contacts_.raw().begin() + long(i)); i--; r.pushed++; continue; }
      fail(h, r);
      return false;
    }
    if (c.remoteId.empty()) {
      HttpResponse h = call("POST", std::string(kPeople) + "people:createContact", token, personBody(c.name, c.number, "").dump());
      if (fail(h, r)) return false;
      Json j;
      std::string rid;
      if (Json::parse(h.body, j)) rid = j["resourceName"].asString();
      if (!personName(rid)) { r.status = SyncStatus::Error; r.detail = "Unexpected reply to create"; return false; }
      c.remoteId = rid; c.etag = j["etag"].asString(); c.source = id; c.dirty = false;
      store(c);
      r.pushed++;
      continue;
    }
    HttpResponse h = call("PATCH", std::string(kPeople) + c.remoteId + ":updateContact?updatePersonFields=names,phoneNumbers", token, personBody(c.name, c.number, c.etag).dump());
    if (isConflict(h)) {
      StoredContact work = c;
      if (!resolveContactConflict(work, id, token, r)) return false;
      if (i < contacts_.raw().size()) contacts_.raw()[i] = work;
      if (work.dirty) i--;  // the original was re-created locally (remote gone): try it again as a create
      continue;
    }
    if (h.status == 404) { c.remoteId.clear(); c.etag.clear(); c.source = "local"; store(c); i--; continue; }
    if (fail(h, r)) return false;
    Json j;
    if (Json::parse(h.body, j) && !j["etag"].asString().empty()) c.etag = j["etag"].asString();
    c.dirty = false;
    store(c);
    r.pushed++;
  }
  return true;
}

SyncResult GoogleSync::syncContacts(const std::string& id, const std::string& token) {
  SyncResult r;
  std::string st = loadToken(id, "contacts");
  std::vector<Json> items;
  std::string next;
  for (int attempt = 0; attempt < 2; attempt++) {
    items.clear();
    bool expired = false;
    std::string url = std::string(kPeople) + "people/me/connections?personFields=names,phoneNumbers&pageSize=100&requestSyncToken=true";
    if (!st.empty()) url += "&syncToken=" + percentEncode(st);
    if (!getAllPages(url, token, "connections", items, next, r, expired)) return r;
    if (!expired) break;
    log_.log(Level::Info, "sync", "contacts sync token expired, doing a full sync");
    st.clear();
    saveToken(id, "contacts", "");
    if (attempt == 1) { r.status = SyncStatus::Error; r.detail = "Sync token keeps expiring"; return r; }
  }
  for (auto& c : items) applyContact(id, c, r);
  if (!contacts_.save()) { r.status = SyncStatus::Error; r.detail = "Could not save contacts"; return r; }
  saveToken(id, "contacts", next);  // only after everything was applied and saved
  pushContacts(id, token, r) ;
  if (!contacts_.save() && r.ok()) { r.status = SyncStatus::Error; r.detail = "Could not save contacts"; }
  log_.log(r.ok() ? Level::Info : Level::Warn, "sync", "contacts: pulled " + std::to_string(r.pulled) + " pushed " + std::to_string(r.pushed) + " conflicts " + std::to_string(r.conflicts) + " (" + syncStatusName(r.status) + ")");
  return r;
}

// ------------------------------------------------------------------ calendar

static std::string two(int n) { char b[8]; std::snprintf(b, sizeof b, "%02d", n); return b; }
static std::string dateStr(const Date& d) { char b[16]; std::snprintf(b, sizeof b, "%04d-%02d-%02d", d.y, d.m, d.d); return b; }

static bool parseDate(const std::string& s, Date& d) {
  return s.size() >= 10 && std::sscanf(s.c_str(), "%4d-%2d-%2d", &d.y, &d.m, &d.d) == 3 && validDate(d);
}
static bool parseMin(const std::string& s, int& m) {  // "....T09:30..." wall-clock part; the zone offset is ignored
  int h = 0, mi = 0;
  if (s.size() < 16 || s[10] != 'T' || std::sscanf(s.c_str() + 11, "%2d:%2d", &h, &mi) != 2 || h > 23 || mi > 59) return false;
  m = h * 60 + mi;
  return true;
}

Json GoogleSync::eventBody(const Event& e) const {
  Json o = Json::makeObject();
  o.set("summary", Json::makeString(e.title));
  Json st = Json::makeObject(), en = Json::makeObject();
  if (e.startMin < 0) {
    st.set("date", Json::makeString(dateStr(e.date)));
    en.set("date", Json::makeString(dateStr(addDays(e.date, 1))));  // Google's all-day end date is exclusive
  } else {
    int end = e.startMin + std::max(e.durationMin, 1);
    Date ed = e.date;
    if (end >= 1440) { ed = addDays(ed, end / 1440); end %= 1440; }
    st.set("dateTime", Json::makeString(dateStr(e.date) + "T" + two(e.startMin / 60) + ":" + two(e.startMin % 60) + ":00" + utcOffset_));
    en.set("dateTime", Json::makeString(dateStr(ed) + "T" + two(end / 60) + ":" + two(end % 60) + ":00" + utcOffset_));
  }
  o.set("start", st).set("end", en);
  return o;
}

namespace {
struct Parsed { Date date{2000, 1, 1}; int startMin = -1, dur = 60; std::string title, etag, rid; bool cancelled = false, ok = false; };

Parsed parseEvent(const Json& e) {
  Parsed p;
  p.rid = e["id"].asString();
  p.etag = e["etag"].asString();
  p.cancelled = e["status"].asString() == "cancelled";
  p.title = clip(e["summary"].asString(), 60);
  if (p.title.find_first_not_of(' ') == std::string::npos) p.title = "Event";
  if (!plainId(p.rid)) return p;
  if (p.cancelled) { p.ok = true; return p; }
  const Json& s = e["start"];
  const Json& en = e["end"];
  if (!s["date"].asString().empty()) {
    if (!parseDate(s["date"].asString(), p.date)) return p;
    p.startMin = -1;
    p.dur = 1440;
  } else {
    if (!parseDate(s["dateTime"].asString(), p.date) || !parseMin(s["dateTime"].asString(), p.startMin)) return p;
    Date ed;
    int em = 0;
    p.dur = 60;
    if (parseDate(en["dateTime"].asString(), ed) && parseMin(en["dateTime"].asString(), em)) {
      int days = sameDay(ed, p.date) ? 0 : 1;  // an event spanning days is clipped to the next midnight region
      p.dur = std::min(1440, std::max(0, days * 1440 + em - p.startMin));
    }
  }
  p.ok = true;
  return p;
}
}  // namespace

void GoogleSync::applyEvent(const std::string& id, const Json& j, SyncResult& r) {
  Parsed p = parseEvent(j);
  if (!p.ok) { r.skipped++; return; }
  auto& all = events_.raw();
  auto it = std::find_if(all.begin(), all.end(), [&](const Event& x) { return x.remoteId == p.rid && x.source == id; });
  if (p.cancelled) {
    if (it == all.end()) return;
    if (!it->dirty) { all.erase(it); r.removed++; }
    else { it->remoteId.clear(); it->etag.clear(); it->source = "local"; r.conflicts++; }
    return;
  }
  if (it == all.end()) {
    if (all.size() >= EventStore::kMax) { r.skipped++; return; }
    Event n;
    n.id = events_.nextId();
    n.date = p.date; n.startMin = p.startMin; n.durationMin = p.dur; n.title = p.title; n.source = id; n.remoteId = p.rid; n.etag = p.etag;
    all.push_back(n);
    r.pulled++;
    return;
  }
  if (it->deleted) return;
  if (!it->dirty) { it->date = p.date; it->startMin = p.startMin; it->durationMin = p.dur; it->title = p.title; it->etag = p.etag; r.pulled++; return; }
  if (it->etag == p.etag) return;
  if (all.size() < EventStore::kMax) {
    Event copy = *it;
    copy.id = events_.nextId();
    copy.title = clip(it->title + " (local)", 60);
    copy.remoteId.clear(); copy.etag.clear(); copy.source = "local"; copy.dirty = true;
    it->date = p.date; it->startMin = p.startMin; it->durationMin = p.dur; it->title = p.title; it->etag = p.etag; it->dirty = false;
    all.push_back(copy);
  } else { it->date = p.date; it->startMin = p.startMin; it->durationMin = p.dur; it->title = p.title; it->etag = p.etag; it->dirty = false; }
  r.conflicts++;
  r.pulled++;
}

bool GoogleSync::resolveEventConflict(Event& local, const std::string&, const std::string& token, SyncResult& r) {
  HttpResponse h = call("GET", std::string(kCalendar) + "/" + local.remoteId, token);
  if (h.status == 404 || h.status == 410) { local.remoteId.clear(); local.etag.clear(); local.source = "local"; return true; }
  if (fail(h, r)) return false;
  Json j;
  if (!Json::parse(h.body, j)) { r.status = SyncStatus::Error; r.detail = "Unreadable server reply"; return false; }
  Parsed p = parseEvent(j);
  local.etag = p.ok ? p.etag : local.etag;
  if (p.ok && p.cancelled) { local.remoteId.clear(); local.etag.clear(); local.source = "local"; return true; }  // deleted remotely: re-create
  Event copy = local;
  copy.title = clip(local.title + " (local)", 60);
  copy.remoteId.clear(); copy.etag.clear(); copy.source = "local"; copy.dirty = true;
  if (p.ok) { local.date = p.date; local.startMin = p.startMin; local.durationMin = p.dur; local.title = p.title; local.dirty = false; }
  else copy.dirty = false;
  if (!copy.dirty) { r.conflicts++; return true; }
  if (events_.raw().size() < EventStore::kMax) { copy.id = events_.nextId(); events_.raw().push_back(copy); }
  r.conflicts++;
  return true;
}

bool GoogleSync::pushEvents(const std::string& id, const std::string& token, SyncResult& r) {
  std::map<int, int> tries;
  for (size_t i = 0; i < events_.raw().size(); i++) {
    Event e = events_.raw()[i];
    if (!e.dirty) continue;
    if (e.source != "local" && e.source != id) continue;
    if (++tries[e.id] > 3) { r.skipped++; continue; }
    auto store = [&](const Event& n) { events_.raw()[i] = n; };
    if (e.deleted) {
      if (e.remoteId.empty()) { events_.raw().erase(events_.raw().begin() + long(i)); i--; continue; }
      HttpResponse h = call("DELETE", std::string(kCalendar) + "/" + e.remoteId, token);
      if (h.status == 404 || h.status == 410 || ok2xx(h)) { events_.raw().erase(events_.raw().begin() + long(i)); i--; r.pushed++; continue; }
      fail(h, r);
      return false;
    }
    if (e.remoteId.empty()) {
      HttpResponse h = call("POST", kCalendar, token, eventBody(e).dump());
      if (fail(h, r)) return false;
      Json j;
      Parsed p;
      if (Json::parse(h.body, j)) p = parseEvent(j);
      if (!plainId(p.rid)) { r.status = SyncStatus::Error; r.detail = "Unexpected reply to create"; return false; }
      e.remoteId = p.rid; e.etag = p.etag; e.source = id; e.dirty = false;
      store(e);
      r.pushed++;
      continue;
    }
    HttpResponse h = call("PUT", std::string(kCalendar) + "/" + e.remoteId, token, eventBody(e).dump(), e.etag);
    if (isConflict(h)) {
      Event work = e;
      if (!resolveEventConflict(work, id, token, r)) return false;
      if (i < events_.raw().size()) events_.raw()[i] = work;
      if (work.dirty) i--;
      continue;
    }
    if (h.status == 404 || h.status == 410) { e.remoteId.clear(); e.etag.clear(); e.source = "local"; store(e); i--; continue; }
    if (fail(h, r)) return false;
    Json j;
    if (Json::parse(h.body, j) && !j["etag"].asString().empty()) e.etag = j["etag"].asString();
    e.dirty = false;
    store(e);
    r.pushed++;
  }
  return true;
}

SyncResult GoogleSync::syncCalendar(const std::string& id, const std::string& token, Date today) {
  SyncResult r;
  std::string st = loadToken(id, "calendar");
  std::vector<Json> items;
  std::string next;
  for (int attempt = 0; attempt < 2; attempt++) {
    items.clear();
    bool expired = false;
    std::string url = std::string(kCalendar) + "?singleEvents=true&showDeleted=true&maxResults=250";
    if (!st.empty()) url += "&syncToken=" + percentEncode(st);
    else url += "&timeMin=" + percentEncode(dateStr(addDays(today, -30)) + "T00:00:00Z");  // first sync: the last 30 days onwards
    if (!getAllPages(url, token, "items", items, next, r, expired)) return r;
    if (!expired) break;
    log_.log(Level::Info, "sync", "calendar sync token expired, doing a full sync");
    st.clear();
    saveToken(id, "calendar", "");
    if (attempt == 1) { r.status = SyncStatus::Error; r.detail = "Sync token keeps expiring"; return r; }
  }
  for (auto& e : items) applyEvent(id, e, r);
  if (!events_.save()) { r.status = SyncStatus::Error; r.detail = "Could not save events"; return r; }
  saveToken(id, "calendar", next);
  pushEvents(id, token, r);
  if (!events_.save() && r.ok()) { r.status = SyncStatus::Error; r.detail = "Could not save events"; }
  log_.log(r.ok() ? Level::Info : Level::Warn, "sync", "calendar: pulled " + std::to_string(r.pulled) + " pushed " + std::to_string(r.pushed) + " conflicts " + std::to_string(r.conflicts) + " (" + syncStatusName(r.status) + ")");
  return r;
}

// ------------------------------------------------------------------ scheduler

long long SyncManager::backoffMs(int failures) {
  int shift = std::min(std::max(failures - 1, 0), 5);
  return std::min(kIntervalMs, 30000LL << shift);  // 30 s, 60 s, ... capped at 15 min
}

SyncResult SyncManager::run(const Account& a) {
  SyncResult total;
  TokenFetch t = accounts_.accessToken(a.id);
  if (t.status == TokenFetch::Offline) { total.status = SyncStatus::Offline; total.detail = "No connection"; return total; }
  if (t.status != TokenFetch::Ok) { total.status = t.status == TokenFetch::NeedsReauth ? SyncStatus::AuthFailed : SyncStatus::Error; total.detail = total.status == SyncStatus::AuthFailed ? "Sign in again" : "Could not get access"; return total; }
  auto merge = [&](const SyncResult& x) {
    total.pulled += x.pulled; total.pushed += x.pushed; total.removed += x.removed; total.conflicts += x.conflicts; total.skipped += x.skipped;
    if (total.ok() && !x.ok()) { total.status = x.status; total.detail = x.detail; }
  };
  if (a.syncContacts) merge(sync_.syncContacts(a.id, t.token));
  if (a.syncCalendar && total.status != SyncStatus::AuthFailed) {
    std::time_t w = std::time(nullptr);
    std::tm* tm = std::localtime(&w);
    merge(sync_.syncCalendar(a.id, t.token, {tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday}));
  }
  return total;
}

SyncResult SyncManager::syncNow(const std::string& id) {
  const Account* a = accounts_.find(id);
  if (!a) { SyncResult r; r.status = SyncStatus::Error; r.detail = "No such account"; return r; }
  State& s = st_[id];
  SyncResult r = run(*a);
  s.everRan = true;
  s.last = r.status;
  s.detail = r.detail;
  if (r.ok()) {
    s.failures = 0;
    s.lastOkMs = now_();
    s.nextMs = now_() + kIntervalMs;
    accounts_.markSynced(id);
  } else if (r.status == SyncStatus::AuthFailed || r.status == SyncStatus::Forbidden) {
    s.nextMs = now_() + 3600LL * 1000 * 24;  // no retries until the user acts (a manual "Sync now" still works)
  } else {
    s.failures++;
    s.nextMs = now_() + backoffMs(s.failures);
  }
  return r;
}

void SyncManager::tick() {
  if (online_ && !online_()) return;  // no link: not a failure, nothing to back off from
  const long long now = now_();
  for (auto& a : accounts_.accounts()) {
    if (a.status != AccountStatus::Active || (!a.syncContacts && !a.syncCalendar)) continue;
    State& s = st_[a.id];
    if (now < s.nextMs) continue;
    syncNow(a.id);
    return;  // one account per tick keeps the UI responsive
  }
}

const SyncManager::State* SyncManager::state(const std::string& id) const {
  auto it = st_.find(id);
  return it == st_.end() ? nullptr : &it->second;
}

}  // namespace fai
