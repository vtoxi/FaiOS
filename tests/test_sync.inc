// Included by test_core.cpp: Contacts/Calendar sync, offline queue, conflicts, scheduler.
// Everything runs against MockGoogleApi (a scripted FAKE of the Google APIs). Nothing here talks to Google.
#include "../simulator/mock_google_api.h"
#include "contacts.h"
#include "sync.h"

namespace {
struct SyncRig {
  MemStorage st;
  Logger log{500};
  std::vector<std::string> logged;
  MockGoogleApi api;
  std::unique_ptr<ContactStore> cs;
  std::unique_ptr<EventStore> es;
  std::unique_ptr<GoogleSync> gs;
  SyncRig() {
    log.min = Level::Debug;
    log.sink = [this](const LogEntry& e) { logged.push_back(e.tag + ": " + e.msg); };
    api.requiredToken = "SECRET-ACCESS-TOKEN";
    reopen();
  }
  void reopen() {  // simulates a restart: stores are re-read from storage
    cs = std::make_unique<ContactStore>(st);
    es = std::make_unique<EventStore>(st);
    cs->load();
    es->load();
    gs = std::make_unique<GoogleSync>(api, st, *cs, *es, log);
    gs->setUtcOffset("+05:00");
  }
  SyncResult contacts() { return gs->syncContacts("acct1", "SECRET-ACCESS-TOKEN"); }
  SyncResult calendar() { return gs->syncCalendar("acct1", "SECRET-ACCESS-TOKEN", {2026, 11, 11}); }
  const StoredContact* contact(const std::string& name) {
    for (auto& c : cs->raw()) if (c.name == name && !c.deleted) return &c;
    return nullptr;
  }
  const Event* event(const std::string& title) {
    for (auto& e : es->raw()) if (e.title == title && !e.deleted) return &e;
    return nullptr;
  }
  int gets() { int n = 0; for (auto& r : api.requests) n += r.compare(0, 3, "GET") == 0; return n; }
};
}  // namespace

static void test_contact_store() {
  MemStorage s;
  ContactStore c(s);
  CHECK(c.load() && c.visible().empty());
  CHECK(c.add("  Zed \x01 ", "0300-1234 abc") != 0 && c.visible()[0].name == "Zed" && c.visible()[0].number == "0300-1234 ");
  CHECK(c.add("", "1") == 0 && c.add("   ", "1") == 0);
  int b = c.add("Amy", "+92 300 1");
  CHECK(c.visible()[0].name == "Amy" && c.visible()[1].name == "Zed");  // sorted by name
  CHECK(c.update(b, "Amy B", "555") && c.visible()[0].name == "Amy B" && !c.update(b, "", "5") && !c.update(999, "x", "1"));
  ContactStore c2(s);
  CHECK(c2.load() && c2.visible().size() == 2 && c2.visible()[0].number == "555" && c2.visible()[0].dirty);
  CHECK(c.remove(b) && c.visible().size() == 1 && c.raw().size() == 1);      // never synced: really gone
  c.raw()[0].remoteId = "people/c1";
  int zid = c.raw()[0].id;
  CHECK(c.remove(zid) && c.visible().empty() && c.raw().size() == 1 && c.raw()[0].deleted && c.raw()[0].dirty);   // synced: tombstone
  CHECK(!c.remove(zid));
  s.write("contacts/store.json", "{broken");
  ContactStore bad(s);
  CHECK(!bad.load() && bad.visible().empty());
}

static void test_event_store_sync_fields() {
  MemStorage s;
  EventStore e(s);
  e.load();
  int a = e.add({2026, 11, 12}, 600, 30, "Standup"), b = e.add({2026, 11, 12}, 700, 30, "Local only");
  e.raw()[0].remoteId = "ev1"; e.raw()[0].etag = "\"e1\""; e.raw()[0].source = "acct1";
  CHECK(e.setTitle(a, "Standup v2") && e.raw()[0].dirty && e.forDay({2026, 11, 12})[0].title == "Standup v2");
  CHECK(e.remove(b) && e.all().size() == 1);                                          // local-only: really gone
  CHECK(e.remove(a) && e.all().size() == 1 && e.raw()[0].deleted && e.forDay({2026, 11, 12}).empty() && !e.hasEvents({2026, 11, 12}));  // tombstone hidden from the UI
  CHECK(!e.remove(a) && !e.setTitle(a, "x"));
  EventStore e2(s);
  CHECK(e2.load() && e2.raw().size() == 1 && e2.raw()[0].deleted && e2.raw()[0].remoteId == "ev1" && e2.raw()[0].etag == "\"e1\"" && e2.raw()[0].source == "acct1");
}

static void test_contacts_pull_and_incremental() {
  SyncRig t;
  t.api.pageSize = 2;
  t.api.addPerson("Ali", "0300 111"); t.api.addPerson("Mum", "0321 222"); t.api.addPerson("Zara", "0333 333");
  SyncResult r = t.contacts();
  CHECK(r.ok() && r.pulled == 3 && t.cs->visible().size() == 3 && t.gets() == 2);          // two pages
  const StoredContact* ali = t.contact("Ali");
  CHECK(ali && !ali->dirty && ali->source == "acct1" && ali->remoteId.find("people/c") == 0 && !ali->etag.empty() && ali->number == "0300 111");
  r = t.contacts();                                                                        // nothing changed: incremental, nothing pulled
  CHECK(r.ok() && r.pulled == 0 && r.pushed == 0 && t.cs->visible().size() == 3);
  CHECK(t.api.requests.back().find("syncToken=st") != std::string::npos);                  // really incremental
  std::string rn = ali->remoteId;
  t.api.editPerson(rn, "Ali Khan", "0300 999");
  t.api.removePerson(t.contact("Mum")->remoteId);
  t.api.addPerson("Newbie", "0345 4");
  r = t.contacts();
  CHECK(r.ok() && t.cs->visible().size() == 3 && t.contact("Ali Khan") && t.contact("Ali Khan")->number == "0300 999" && !t.contact("Mum") && t.contact("Newbie") && r.removed == 1);
  CHECK(t.cs->raw().size() == 3);                                                          // no duplicates, remote deletion removed the row
}

static void test_contacts_push_edit_delete() {
  SyncRig t;
  t.api.addPerson("Ali", "1");
  t.contacts();
  int mine = t.cs->add("Fresh", "0301-5");
  SyncResult r = t.contacts();
  CHECK(r.ok() && r.pushed == 1 && t.api.livePeople() == 2 && t.contact("Fresh")->remoteId.find("people/c") == 0 && t.contact("Fresh")->source == "acct1" && !t.contact("Fresh")->dirty);
  CHECK(t.api.person(t.contact("Fresh")->remoteId)->name == "Fresh" && t.api.person(t.contact("Fresh")->remoteId)->number == "0301-5");
  const std::string aliRn = t.contact("Ali")->remoteId;
  CHECK(t.cs->update(t.contact("Ali")->id, "Ali Edited", "77"));
  r = t.contacts();
  CHECK(r.ok() && r.pushed == 1 && r.conflicts == 0 && t.api.person(aliRn)->name == "Ali Edited" && !t.contact("Ali Edited")->dirty);
  CHECK(t.contact("Ali Edited")->etag != "");
  // a second edit right away uses the NEW etag: no conflict
  t.cs->update(t.contact("Ali Edited")->id, "Ali Again", "78");
  r = t.contacts();
  CHECK(r.ok() && r.conflicts == 0 && t.api.person(aliRn)->name == "Ali Again");
  CHECK(t.cs->remove(t.contact("Fresh")->id) && t.cs->visible().size() == 1);
  r = t.contacts();
  CHECK(r.ok() && r.pushed == 1 && t.api.livePeople() == 1 && t.cs->raw().size() == 1);   // tombstone sent and removed
  (void)mine;
}

static void test_contacts_offline_queue() {
  SyncRig t;
  t.api.addPerson("Ali", "1");
  t.contacts();
  t.cs->add("Offline add", "5");
  t.cs->update(t.contact("Ali")->id, "Ali offline edit", "6");
  t.api.offline = true;
  SyncResult r = t.contacts();
  CHECK(r.status == SyncStatus::Offline && !r.ok() && t.contact("Offline add")->dirty && t.contact("Ali offline edit")->dirty);   // queue intact
  t.reopen();                                                                              // restart while still offline: queue survives
  CHECK(t.contact("Offline add") && t.contact("Offline add")->dirty && t.contact("Ali offline edit")->dirty);
  CHECK(t.contacts().status == SyncStatus::Offline);
  t.api.offline = false;
  r = t.contacts();
  CHECK(r.ok() && r.pushed == 2 && t.api.livePeople() == 2 && !t.contact("Offline add")->dirty && t.api.person(t.contact("Ali offline edit")->remoteId)->name == "Ali offline edit");
  // delete while offline, then online
  t.cs->remove(t.contact("Offline add")->id);
  t.api.offline = true;
  CHECK(t.contacts().status == SyncStatus::Offline && t.cs->raw().size() == 2);            // tombstone kept
  t.api.offline = false;
  CHECK(t.contacts().ok() && t.api.livePeople() == 1 && t.cs->raw().size() == 1);
}

static void test_contacts_conflicts() {
  // both sides edited the same contact: the account's version wins, the local edit survives as a copy
  SyncRig t;
  t.api.addPerson("Ali", "1");
  t.contacts();
  const std::string rn = t.contact("Ali")->remoteId;
  t.cs->update(t.contact("Ali")->id, "Ali local", "100");
  t.api.editPerson(rn, "Ali server", "200");
  SyncResult r = t.contacts();
  CHECK(r.ok() && r.conflicts == 1 && t.contact("Ali server") && t.contact("Ali server")->number == "200" && !t.contact("Ali server")->dirty);
  const StoredContact* copy = t.contact("Ali local (local)");
  CHECK(copy && copy->number == "100" && !copy->dirty && copy->remoteId.find("people/c") == 0);   // the copy was uploaded as a new contact
  CHECK(t.api.livePeople() == 2 && t.cs->visible().size() == 2);                                  // nothing lost, nothing duplicated

  // conflict discovered at push time (stale etag, no pull change): same outcome
  SyncRig u;
  u.api.addPerson("Bo", "1");
  u.contacts();
  const std::string bn = u.contact("Bo")->remoteId;
  u.cs->update(u.contact("Bo")->id, "Bo local", "9");
  u.cs->raw()[0].etag = "\"stale\"";
  u.api.editPerson(bn, "Bo server", "8");           // also moves the server on; the pull sees it, so force the push path:
  u.contacts();                                     // (pull-time conflict)
  CHECK(u.contact("Bo server") && u.contact("Bo local (local)"));
  SyncRig v;
  v.api.addPerson("Cy", "1");
  v.contacts();
  v.cs->update(v.contact("Cy")->id, "Cy local", "9");
  v.cs->raw()[0].etag = "\"stale\"";                // server did not change since the last pull, only our etag is wrong
  r = v.contacts();
  CHECK(r.ok() && r.conflicts == 1 && v.contact("Cy") && v.contact("Cy local (local)") && v.api.livePeople() == 2);

  // remote deleted, local edited: the local edit is kept and re-uploaded as a new contact
  SyncRig w;
  w.api.addPerson("Di", "1");
  w.contacts();
  w.cs->update(w.contact("Di")->id, "Di edited", "2");
  w.api.removePerson(w.contact("Di edited")->remoteId);
  r = w.contacts();
  CHECK(r.ok() && r.conflicts == 1 && w.contact("Di edited") && w.api.livePeople() == 1 && w.api.people.back().name == "Di edited" && !w.contact("Di edited")->dirty);

  // remote deleted, local untouched: it just disappears
  SyncRig x;
  x.api.addPerson("Ed", "1");
  x.contacts();
  x.api.removePerson(x.contact("Ed")->remoteId);
  CHECK(x.contacts().ok() && x.cs->raw().empty());
  // locally deleted while the remote edited it: the deletion wins (tombstone is pushed, not resurrected)
  SyncRig y;
  y.api.addPerson("Fay", "1");
  y.contacts();
  const std::string fr = y.contact("Fay")->remoteId;
  y.cs->remove(y.contact("Fay")->id);
  y.api.editPerson(fr, "Fay moved", "2");
  CHECK(y.contacts().ok() && y.cs->raw().empty() && y.api.livePeople() == 0);
}

static void test_contacts_failures_and_safety() {
  SyncRig t;
  t.api.addPerson("Ali", "1");
  t.api.forceStatus = 401;
  SyncResult r = t.contacts();
  CHECK(r.status == SyncStatus::AuthFailed && t.cs->raw().empty());
  t.api.forceStatus = 403;
  CHECK(t.contacts().status == SyncStatus::Forbidden);
  t.api.forceStatus = 500;
  CHECK(t.contacts().status == SyncStatus::Error && t.cs->raw().empty());
  t.api.forceStatus = 0;
  t.api.requiredToken = "OTHER";
  CHECK(t.contacts().status == SyncStatus::AuthFailed);                                    // wrong token is rejected by the server
  t.api.requiredToken = "SECRET-ACCESS-TOKEN";

  // expired sync token: full resync, no duplicates
  CHECK(t.contacts().ok() && t.cs->visible().size() == 1);
  t.api.addPerson("Two", "2");
  t.api.expireSyncTokens = true;
  r = t.contacts();
  CHECK(r.ok() && t.cs->visible().size() == 2 && t.cs->raw().size() == 2);

  // an interrupted pull applies nothing and keeps the old token
  SyncRig u;
  u.api.pageSize = 1;
  u.api.addPerson("A", "1"); u.api.addPerson("B", "2");
  struct Cut : IHttp { MockGoogleApi& a; int n = 0; explicit Cut(MockGoogleApi& x) : a(x) {} HttpResponse send(const HttpRequest& r) override { return ++n >= 2 ? HttpResponse{0, "", "cut"} : a.send(r); } } cut(u.api);
  GoogleSync gs(cut, u.st, *u.cs, *u.es, u.log);
  CHECK(gs.syncContacts("acct1", "SECRET-ACCESS-TOKEN").status == SyncStatus::Offline && u.cs->raw().empty());
  CHECK(u.contacts().ok() && u.cs->visible().size() == 2);                                 // the retry is a clean full sync

  // hostile server data: identifiers that could rewrite a URL are skipped, never used
  SyncRig v;
  v.api.people.push_back({"people/../evil?x=1", "Evil", "1", 1});
  v.api.people.push_back({"people/c9/../../x", "Evil2", "1", 2});
  v.api.people.push_back({"notpeople/c1", "Evil3", "1", 3});
  v.api.people.push_back({"people/c77", "Good\x01\x02 Name", "0300 xx 1", 4});
  SyncResult rv = v.contacts();
  CHECK(rv.ok() && rv.skipped == 3 && v.cs->raw().size() == 1 && v.cs->raw()[0].name == "Good Name" && v.cs->raw()[0].number == "0300  1");

  // the token never appears in logs, and requests do not carry it in the URL
  bool leak = false;
  for (auto& l : t.logged) leak |= l.find("SECRET-ACCESS-TOKEN") != std::string::npos;
  for (auto& q : t.api.requests) leak |= q.find("SECRET-ACCESS-TOKEN") != std::string::npos;
  CHECK(!leak);

  // account removal wipes its synced data and tokens, but not local-only contacts
  SyncRig w;
  w.api.addPerson("Ali", "1");
  w.contacts();
  w.cs->add("Mine", "2");
  w.gs->forget("acct1");
  CHECK(w.cs->raw().size() == 1 && w.cs->raw()[0].name == "Mine" && !w.st.exists("sync/acct1/contacts.json") == false);
  std::string tokenJson;
  w.st.read("sync/acct1/contacts.json", tokenJson);
  CHECK(tokenJson.find("\"token\":\"\"") != std::string::npos);
}

static void test_calendar_pull() {
  SyncRig t;
  t.api.pageSize = 2;
  t.api.addEvent("Standup", "dateTime", "2026-11-12T09:30:00+05:00", "dateTime", "2026-11-12T10:15:00+05:00");
  t.api.addEvent("Holiday", "date", "2026-11-14", "date", "2026-11-15");
  t.api.addEvent("Late night", "dateTime", "2026-11-13T23:00:00+05:00", "dateTime", "2026-11-14T01:00:00+05:00");
  std::string bad = t.api.addEvent("Broken", "dateTime", "garbage", "dateTime", "x");
  t.api.addEvent("   ", "dateTime", "2026-11-15T08:00:00Z", "dateTime", "2026-11-15T08:30:00Z");
  SyncResult r = t.calendar();
  CHECK(r.ok() && r.pulled == 4 && r.skipped == 1 && t.es->raw().size() == 4);
  const Event* s = t.event("Standup");
  CHECK(s && s->date.d == 12 && s->startMin == 570 && s->durationMin == 45 && s->source == "acct1" && !s->dirty && !s->etag.empty());
  const Event* h = t.event("Holiday");
  CHECK(h && h->startMin == -1 && h->date.d == 14);
  const Event* ln = t.event("Late night");
  CHECK(ln && ln->startMin == 1380 && ln->durationMin == 60);                                // crosses midnight: shown as it starts, 23:00 + (01:00 next day) clipped sensibly
  CHECK(t.event("Event") != nullptr);                                                        // blank title gets the default
  CHECK(t.es->hasEvents({2026, 11, 12}));
  CHECK(t.api.requests[0].find("timeMin=") != std::string::npos && t.api.requests[0].find("2026-10-12") != std::string::npos);   // first sync: the last 30 days on
  // incremental
  t.api.editEvent(t.api.events[0].id, "Standup moved");
  t.api.removeEvent(t.api.events[1].id);
  r = t.calendar();
  CHECK(r.ok() && r.removed == 1 && t.event("Standup moved") && !t.event("Holiday") && t.es->raw().size() == 3);
  CHECK(t.api.requests.back().find("syncToken=st") != std::string::npos && t.api.requests.back().find("timeMin") == std::string::npos);
  (void)bad;
}

static void test_calendar_push_and_conflicts() {
  SyncRig t;
  int timed = t.es->add({2026, 11, 20}, 600, 90, "Dentist");
  int allDay = t.es->add({2026, 11, 21}, -1, 60, "Eid");
  SyncResult r = t.calendar();
  CHECK(r.ok() && r.pushed == 2 && t.api.liveEvents() == 2);
  const Event* d = t.event("Dentist");
  CHECK(d && !d->dirty && d->source == "acct1" && !d->remoteId.empty());
  auto* srv = t.api.ev(d->remoteId);
  CHECK(srv && srv->startKey == "dateTime" && srv->startVal == "2026-11-20T10:00:00+05:00" && srv->endVal == "2026-11-20T11:30:00+05:00");   // 90 minutes, with the offset
  auto* srv2 = t.api.ev(t.event("Eid")->remoteId);
  CHECK(srv2 && srv2->startKey == "date" && srv2->startVal == "2026-11-21" && srv2->endVal == "2026-11-22");                                  // all-day end is exclusive
  // an event ending after midnight
  t.es->add({2026, 11, 30}, 1410, 60, "Night");
  t.calendar();
  CHECK(t.api.ev(t.event("Night")->remoteId)->endVal == "2026-12-01T00:30:00+05:00");
  // edit pushes with If-Match and updates the etag
  t.es->setTitle(timed, "Dentist (moved)");
  r = t.calendar();
  CHECK(r.ok() && r.pushed == 1 && r.conflicts == 0 && t.api.ev(t.event("Dentist (moved)")->remoteId)->summary == "Dentist (moved)");
  t.es->setTitle(timed, "Dentist again");                      // second edit: must use the NEW etag
  r = t.calendar();
  CHECK(r.ok() && r.conflicts == 0 && t.api.ev(t.event("Dentist again")->remoteId)->summary == "Dentist again");
  // conflict: both sides changed
  const std::string rid = t.event("Dentist again")->remoteId;
  t.es->setTitle(timed, "Dentist local");
  t.api.editEvent(rid, "Dentist server");
  r = t.calendar();
  CHECK(r.ok() && r.conflicts == 1 && t.event("Dentist server") && t.event("Dentist local (local)") && !t.event("Dentist local (local)")->dirty && t.api.liveEvents() == 4);
  // delete: tombstone, offline, then online
  t.es->remove(allDay);
  t.api.offline = true;
  CHECK(t.calendar().status == SyncStatus::Offline && t.es->raw().size() == 5);
  t.api.offline = false;
  r = t.calendar();
  CHECK(r.ok() && t.api.liveEvents() == 3 && t.es->raw().size() == 4);
  // stale etag found at push time
  t.es->setTitle(timed, "Stale edit");
  for (auto& e : t.es->raw()) if (e.id == timed) e.etag = "\"stale\"";
  r = t.calendar();
  CHECK(r.ok() && r.conflicts == 1 && t.event("Stale edit (local)") != nullptr);
  // remote deleted a dirty local event: re-created
  SyncRig u;
  int id = u.es->add({2026, 12, 1}, 480, 30, "Gym");
  u.calendar();
  u.es->setTitle(id, "Gym edited");
  u.api.removeEvent(u.event("Gym edited")->remoteId);
  r = u.calendar();
  CHECK(r.ok() && u.api.liveEvents() == 1 && u.event("Gym edited") && !u.event("Gym edited")->dirty);
}

static void test_calendar_failures_and_forget() {
  SyncRig t;
  t.api.addEvent("A", "dateTime", "2026-11-12T09:00:00Z", "dateTime", "2026-11-12T10:00:00Z");
  t.api.forceStatus = 401;
  CHECK(t.calendar().status == SyncStatus::AuthFailed && t.es->raw().empty());
  t.api.forceStatus = 0;
  CHECK(t.calendar().ok() && t.es->raw().size() == 1);
  t.api.addEvent("B", "dateTime", "2026-11-13T09:00:00Z", "dateTime", "2026-11-13T10:00:00Z");
  t.api.expireSyncTokens = true;                                   // Calendar answers an expired token with 410
  SyncResult r = t.calendar();
  CHECK(r.ok() && t.es->raw().size() == 2);
  t.es->add({2026, 12, 25}, 600, 60, "Local only");
  t.gs->forget("acct1");
  CHECK(t.es->raw().size() == 1 && t.es->raw()[0].title == "Local only");
  // a corrupt server reply never crashes or half-applies
  struct Junk : IHttp { HttpResponse send(const HttpRequest&) override { return {200, "<html>not json", ""}; } } junk;
  GoogleSync gs(junk, t.st, *t.cs, *t.es, t.log);
  CHECK(gs.syncCalendar("acct1", "x", {2026, 11, 11}).status == SyncStatus::Error && gs.syncContacts("acct1", "x").status == SyncStatus::Error);
  // a server that never ends paging is cut off
  struct Endless : IHttp { HttpResponse send(const HttpRequest&) override { return {200, "{\"items\":[],\"nextPageToken\":\"1\"}", ""}; } } endless;
  GoogleSync g2(endless, t.st, *t.cs, *t.es, t.log);
  CHECK(g2.syncCalendar("acct1", "x", {2026, 11, 11}).status == SyncStatus::Error);
  // NullHttp (no transport built yet) = offline, not a crash
  NullHttp none;
  GoogleSync g3(none, t.st, *t.cs, *t.es, t.log);
  CHECK(g3.syncContacts("acct1", "x").status == SyncStatus::Offline);
}

static void test_sync_manager() {
  Rig acct;                                                        // account manager with a signed-in (mock) account
  std::string id = acct.signIn("openid email profile https://www.googleapis.com/auth/contacts https://www.googleapis.com/auth/calendar.events");
  CHECK(!id.empty() && acct.am->setSync(id, SyncService::Contacts, true) && acct.am->setSync(id, SyncService::Calendar, true));
  SyncRig t;
  t.api.requiredToken = "ACCESS-1";
  t.api.addPerson("Ali", "1");
  t.api.addEvent("Meet", "dateTime", "2026-11-12T09:00:00Z", "dateTime", "2026-11-12T10:00:00Z");
  ContactStore& cs = *t.cs;
  GoogleSync gsync(t.api, t.st, cs, *t.es, t.log);
  long long now = acct.now;
  bool online = true;
  SyncManager sm(*acct.am, gsync, t.log, [&] { return now; }, [&] { return online; });
  CHECK(SyncManager::backoffMs(1) == 30000 && SyncManager::backoffMs(2) == 60000 && SyncManager::backoffMs(5) == 480000 && SyncManager::backoffMs(6) == 900000 && SyncManager::backoffMs(50) == 900000);
  sm.tick();                                                       // first tick: due at once
  CHECK(cs.visible().size() == 1 && t.es->raw().size() == 1 && sm.state(id) && sm.state(id)->last == SyncStatus::Ok && acct.am->find(id)->lastSyncMs == now);
  size_t reqs = t.api.requests.size();
  now += 60 * 1000;
  sm.tick();
  CHECK(t.api.requests.size() == reqs);                            // not due for 15 minutes
  now += SyncManager::kIntervalMs;
  cs.add("New", "5");
  sm.tick();
  CHECK(t.api.livePeople() == 2);
  // offline: backoff, queue kept
  cs.add("Queued", "6");
  now += SyncManager::kIntervalMs;
  t.api.offline = true;
  sm.tick();
  CHECK(sm.state(id)->last == SyncStatus::Offline && sm.state(id)->failures == 1 && sm.state(id)->nextMs == now + 30000 && t.api.livePeople() == 2);
  size_t r2 = t.api.requests.size();
  now += 10000;
  sm.tick();
  CHECK(t.api.requests.size() == r2);                              // backing off
  now += 30000;
  sm.tick();
  CHECK(sm.state(id)->failures == 2 && sm.state(id)->nextMs == now + 60000);
  t.api.offline = false;
  now += 60000;
  sm.tick();
  CHECK(sm.state(id)->last == SyncStatus::Ok && sm.state(id)->failures == 0 && t.api.livePeople() == 3);
  // no link at all: nothing attempted, nothing counted
  now += SyncManager::kIntervalMs;
  online = false;
  size_t r3 = t.api.requests.size();
  sm.tick();
  CHECK(t.api.requests.size() == r3 && sm.state(id)->failures == 0);
  online = true;
  // sign-in problem: stop retrying
  t.api.forceStatus = 401;
  sm.tick();
  CHECK(sm.state(id)->last == SyncStatus::AuthFailed && sm.state(id)->nextMs > now + 3600LL * 1000);
  size_t r4 = t.api.requests.size();
  now += 3 * SyncManager::kIntervalMs;
  sm.tick();
  CHECK(t.api.requests.size() == r4);
  t.api.forceStatus = 0;
  CHECK(sm.syncNow(id).ok() && sm.state(id)->last == SyncStatus::Ok);   // manual sync still works
  CHECK(sm.syncNow("nobody").status == SyncStatus::Error);
  // disabled services and accounts needing sign-in are skipped
  acct.am->setSync(id, SyncService::Contacts, false);
  acct.am->setSync(id, SyncService::Calendar, false);
  now += 2 * SyncManager::kIntervalMs;
  size_t r5 = t.api.requests.size();
  sm.tick();
  CHECK(t.api.requests.size() == r5);
}
