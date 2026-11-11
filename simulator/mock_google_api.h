// SIMULATOR/TEST ONLY. A MOCK of the Google People and Calendar APIs: an in-memory "account" that answers the
// handful of endpoints FaiOS sync uses, with etags, incremental sync tokens, paging and tombstones, and switches to
// inject failures (offline, expired token, conflicts). It is NOT Google, never contacts Google and is not part of any
// device image. Its behaviour follows Google's public documentation as understood, so passing tests against it does
// NOT prove the real service accepts the same requests.
#pragma once
#include <algorithm>
#include <string>
#include <vector>

#include "crypto.h"
#include "http.h"
#include "json.h"

namespace fai {

class MockGoogleApi : public IHttp {
 public:
  struct Person { std::string rn, name, number; long ver; bool deleted = false; };
  struct Ev { std::string id, summary, startKey, startVal, endKey, endVal; long ver; bool deleted = false; };

  // --- failure injection / observation ---
  bool offline = false;
  int forceStatus = 0;          // answer every request with this status while non-zero
  bool expireSyncTokens = false;  // the next sync-token request is answered as expired, then this clears
  std::string requiredToken;    // if set, "Authorization: Bearer <this>" is required (else 401)
  int pageSize = 100;
  std::vector<std::string> requests;  // "METHOD url" of every request, in order (never the token)

  // --- server-side edits (as if made on another device) ---
  std::string addPerson(const std::string& name, const std::string& number) {
    people.push_back({"people/c" + std::to_string(++nextId_), name, number, ++ver_});
    return people.back().rn;
  }
  void editPerson(const std::string& rn, const std::string& name, const std::string& number) { if (Person* p = person(rn)) { p->name = name; p->number = number; p->ver = ++ver_; } }
  void removePerson(const std::string& rn) { if (Person* p = person(rn)) { p->deleted = true; p->ver = ++ver_; } }
  std::string addEvent(const std::string& summary, const std::string& startKey, const std::string& startVal, const std::string& endKey, const std::string& endVal) {
    events.push_back({"ev" + std::to_string(++nextId_), summary, startKey, startVal, endKey, endVal, ++ver_});
    return events.back().id;
  }
  void editEvent(const std::string& id, const std::string& summary) { if (Ev* e = ev(id)) { e->summary = summary; e->ver = ++ver_; } }
  void removeEvent(const std::string& id) { if (Ev* e = ev(id)) { e->deleted = true; e->ver = ++ver_; } }
  Person* person(const std::string& rn) { for (auto& p : people) if (p.rn == rn) return &p; return nullptr; }
  Ev* ev(const std::string& id) { for (auto& e : events) if (e.id == id) return &e; return nullptr; }
  int livePeople() const { int n = 0; for (auto& p : people) n += !p.deleted; return n; }
  int liveEvents() const { int n = 0; for (auto& e : events) n += !e.deleted; return n; }
  std::vector<Person> people;
  std::vector<Ev> events;

  HttpResponse send(const HttpRequest& r) override {
    requests.push_back(r.method + " " + r.url);
    if (offline) return {0, "", "mock: offline"};
    if (!requiredToken.empty()) {
      bool ok = false;
      for (auto& h : r.headers) if (h.first == "Authorization" && h.second == "Bearer " + requiredToken) ok = true;
      if (!ok) return err(401, "UNAUTHENTICATED");
    }
    if (forceStatus) return err(forceStatus, "FORCED");
    std::string path = r.url, query;
    size_t q = path.find('?');
    if (q != std::string::npos) { query = path.substr(q + 1); path = path.substr(0, q); }
    const std::string people = "https://people.googleapis.com/v1/", cal = "https://www.googleapis.com/calendar/v3/calendars/primary/events";
    if (path.compare(0, people.size(), people) == 0) return peopleApi(r, path.substr(people.size()), query);
    if (path.compare(0, cal.size(), cal) == 0) return calendarApi(r, path.substr(cal.size()), query);
    return err(404, "NOT_FOUND");
  }

 private:
  long ver_ = 0;
  int nextId_ = 100;

  static HttpResponse err(int status, const std::string& st) {
    Json e = Json::makeObject();
    e.set("code", Json::makeNumber(status)).set("status", Json::makeString(st)).set("message", Json::makeString(st));
    Json o = Json::makeObject();
    o.set("error", e);
    return {status, o.dump(), ""};
  }
  static std::string param(const std::string& query, const std::string& key) {
    size_t pos = 0;
    while (pos <= query.size()) {
      size_t amp = query.find('&', pos);
      std::string kv = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
      size_t eq = kv.find('=');
      if (eq != std::string::npos && kv.substr(0, eq) == key) return percentDecode(kv.substr(eq + 1));
      if (amp == std::string::npos) break;
      pos = amp + 1;
    }
    return "";
  }
  static std::string etagOf(long v) { return "\"e" + std::to_string(v) + "\""; }
  static std::string tokenOf(long v) { return "st" + std::to_string(v); }
  static bool parseToken(const std::string& t, long& v) { return t.size() > 2 && t.compare(0, 2, "st") == 0 && (v = std::atol(t.c_str() + 2)) >= 0; }

  Json personJson(const Person& p) const {
    Json o = Json::makeObject();
    o.set("resourceName", Json::makeString(p.rn)).set("etag", Json::makeString(etagOf(p.ver)));
    if (p.deleted) { Json m = Json::makeObject(); m.set("deleted", Json::makeBool(true)); o.set("metadata", m); return o; }
    Json names = Json::makeArray(), n = Json::makeObject();
    n.set("displayName", Json::makeString(p.name));
    names.push(n);
    o.set("names", names);
    if (!p.number.empty()) { Json nums = Json::makeArray(), v = Json::makeObject(); v.set("value", Json::makeString(p.number)); nums.push(v); o.set("phoneNumbers", nums); }
    return o;
  }
  Json eventJson(const Ev& e) const {
    Json o = Json::makeObject();
    o.set("id", Json::makeString(e.id)).set("etag", Json::makeString(etagOf(e.ver)));
    if (e.deleted) { o.set("status", Json::makeString("cancelled")); return o; }
    o.set("status", Json::makeString("confirmed")).set("summary", Json::makeString(e.summary));
    Json s = Json::makeObject(), en = Json::makeObject();
    s.set(e.startKey, Json::makeString(e.startVal));
    en.set(e.endKey, Json::makeString(e.endVal));
    o.set("start", s).set("end", en);
    return o;
  }

  template <class T, class F>
  HttpResponse listing(const std::vector<T>& all, const std::string& query, const char* itemsKey, bool expiredIs410, F toJson) {
    long since = -1;
    const std::string tok = param(query, "syncToken");
    if (!tok.empty()) {
      if (expireSyncTokens) { expireSyncTokens = false; return expiredIs410 ? err(410, "GONE") : err(400, "FAILED_PRECONDITION EXPIRED_SYNC_TOKEN"); }
      if (!parseToken(tok, since)) return err(400, "INVALID_ARGUMENT");
    }
    size_t offset = 0;
    const std::string pt = param(query, "pageToken");
    if (!pt.empty()) offset = size_t(std::atol(pt.c_str()));
    std::vector<const T*> sel;
    for (auto& x : all) {
      if (since < 0 ? x.deleted : x.ver <= since) continue;  // full sync omits deleted items; incremental reports them
      sel.push_back(&x);
    }
    Json out = Json::makeObject(), arr = Json::makeArray();
    size_t end = std::min(sel.size(), offset + size_t(pageSize));
    for (size_t i = offset; i < end; i++) arr.push(toJson(*sel[i]));
    out.set(itemsKey, arr);
    if (end < sel.size()) out.set("nextPageToken", Json::makeString(std::to_string(end)));
    else out.set("nextSyncToken", Json::makeString(tokenOf(ver_)));
    return {200, out.dump(), ""};
  }

  HttpResponse peopleApi(const HttpRequest& r, const std::string& rest, const std::string& query) {
    if (r.method == "GET" && rest == "people/me/connections")
      return listing(people, query, "connections", false, [this](const Person& p) { return personJson(p); });
    if (r.method == "POST" && rest == "people:createContact") {
      Json b;
      if (!Json::parse(r.body, b)) return err(400, "INVALID_ARGUMENT");
      std::string rn = addPerson(b["names"].at(0)["givenName"].asString(), b["phoneNumbers"].at(0)["value"].asString());
      return {200, personJson(*person(rn)).dump(), ""};
    }
    size_t colon = rest.find(':');
    std::string rn = colon == std::string::npos ? rest : rest.substr(0, colon), verb = colon == std::string::npos ? "" : rest.substr(colon + 1);
    Person* p = person(rn);
    if (!p || p->deleted) return err(404, "NOT_FOUND");
    if (r.method == "GET" && verb.empty()) return {200, personJson(*p).dump(), ""};
    if (r.method == "PATCH" && verb == "updateContact") {
      Json b;
      if (!Json::parse(r.body, b)) return err(400, "INVALID_ARGUMENT");
      if (b["etag"].asString() != etagOf(p->ver)) return err(400, "FAILED_PRECONDITION");  // etag mismatch, as Google reports it
      editPerson(rn, b["names"].at(0)["givenName"].asString(), b["phoneNumbers"].at(0)["value"].asString());
      return {200, personJson(*person(rn)).dump(), ""};
    }
    if (r.method == "DELETE" && verb == "deleteContact") { removePerson(rn); return {200, "{}", ""}; }
    return err(404, "NOT_FOUND");
  }

  HttpResponse calendarApi(const HttpRequest& r, const std::string& rest, const std::string& query) {
    if (r.method == "GET" && rest.empty()) return listing(events, query, "items", true, [this](const Ev& e) { return eventJson(e); });
    if (r.method == "POST" && rest.empty()) {
      Json b;
      if (!Json::parse(r.body, b)) return err(400, "INVALID_ARGUMENT");
      std::string id = readEvent(b, nullptr);
      if (id.empty()) return err(400, "INVALID_ARGUMENT");
      return {200, eventJson(*ev(id)).dump(), ""};
    }
    if (rest.size() < 2 || rest[0] != '/') return err(404, "NOT_FOUND");
    std::string id = rest.substr(1);
    Ev* e = ev(id);
    if (!e) return err(404, "NOT_FOUND");
    if (e->deleted && r.method != "GET") return err(410, "GONE");
    if (r.method == "GET") return {200, eventJson(*e).dump(), ""};
    if (r.method == "DELETE") { removeEvent(id); return {204, "", ""}; }
    if (r.method == "PUT") {
      std::string im;
      for (auto& h : r.headers) if (h.first == "If-Match") im = h.second;
      if (!im.empty() && im != etagOf(e->ver)) return err(412, "conditionNotMet");
      Json b;
      if (!Json::parse(r.body, b) || readEvent(b, e).empty()) return err(400, "INVALID_ARGUMENT");
      return {200, eventJson(*e).dump(), ""};
    }
    return err(404, "NOT_FOUND");
  }

  // creates (existing == nullptr) or updates an event from a request body; returns its id, "" if invalid
  std::string readEvent(const Json& b, Ev* existing) {
    const Json& s = b["start"];
    const Json& en = b["end"];
    std::string sk = s["date"].asString().empty() ? "dateTime" : "date", ek = en["date"].asString().empty() ? "dateTime" : "date";
    std::string sv = s[sk].asString(), evv = en[ek].asString();
    if (sv.empty() || evv.empty()) return "";
    if (!existing) return addEvent(b["summary"].asString(), sk, sv, ek, evv);
    existing->summary = b["summary"].asString();
    existing->startKey = sk; existing->startVal = sv; existing->endKey = ek; existing->endVal = evv;
    existing->ver = ++ver_;
    return existing->id;
  }
};

}  // namespace fai
