// Google Contacts (People API) and Calendar (Calendar API v3) sync, plus the scheduler that runs it.
//
// STATUS: tested only against a scripted FAKE Google API (simulator/mock_google_api.h). It has never talked to Google:
// no OAuth client exists yet and there is no real HTTP/TLS transport. The request and response shapes follow Google's
// public API documentation, but treat the real behaviour as UNVERIFIED until a real account has been used.
//
// Model (offline-first):
//   * The phone's stores are the source the UI reads. The network is never needed to read or edit.
//   * A local change sets `dirty` (or a tombstone) on the item. That flag IS the persistent offline queue: it survives
//     restarts, and a sync that cannot reach the network leaves it untouched.
//   * Pull first (incremental, using Google's sync tokens), then push. A sync token is saved only after a whole pull
//     was applied, so an interrupted sync repeats instead of losing changes.
//   * Conflicts never destroy data: when both sides changed an item, the account's version wins the original item and
//     the local edit is kept as a separate "(local)" copy that is then uploaded.
//   * Tokens/keys never go into logs or error text.
#pragma once
#include <functional>
#include <map>
#include <string>

#include "accounts.h"
#include "contacts.h"
#include "events.h"
#include "http.h"
#include "json.h"
#include "services.h"

namespace fai {

enum class SyncStatus { Ok, Offline, AuthFailed, Forbidden, Error };
const char* syncStatusName(SyncStatus s);

struct SyncResult {
  SyncStatus status = SyncStatus::Ok;
  int pulled = 0, pushed = 0, removed = 0, conflicts = 0, skipped = 0;  // skipped: invalid rows or over the store limit
  std::string detail;  // short, safe to show; never contains tokens
  bool ok() const { return status == SyncStatus::Ok; }
};

class GoogleSync {
 public:
  GoogleSync(IHttp& http, IStorage& storage, ContactStore& contacts, EventStore& events, Logger& log)
      : http_(http), s_(storage), contacts_(contacts), events_(events), log_(log) {}

  // `token` is a valid OAuth access token for `accountId`.
  SyncResult syncContacts(const std::string& accountId, const std::string& token);
  SyncResult syncCalendar(const std::string& accountId, const std::string& token, Date today);

  // Account removed: drop its sync tokens and every item synced from it.
  void forget(const std::string& accountId);

  // UTC offset used when uploading timed events, "Z" or "+05:00". Time zones are NOT otherwise handled: downloaded
  // events keep the wall-clock time of their own zone.
  void setUtcOffset(const std::string& o) { utcOffset_ = o; }

 private:
  bool getAllPages(const std::string& baseUrl, const std::string& token, const char* itemsKey, std::vector<Json>& items, std::string& nextSync, SyncResult& r, bool& expired);
  HttpResponse call(const std::string& method, const std::string& url, const std::string& token, const std::string& body = "", const std::string& ifMatch = "");
  static bool fail(const HttpResponse& h, SyncResult& r);  // classifies a non-2xx / transport failure into r.status
  std::string loadToken(const std::string& id, const char* svc);
  void saveToken(const std::string& id, const char* svc, const std::string& t);

  void applyContact(const std::string& id, const Json& c, SyncResult& r);
  bool pushContacts(const std::string& id, const std::string& token, SyncResult& r);
  bool resolveContactConflict(StoredContact& local, const std::string& id, const std::string& token, SyncResult& r);

  void applyEvent(const std::string& id, const Json& e, SyncResult& r);
  bool pushEvents(const std::string& id, const std::string& token, SyncResult& r);
  bool resolveEventConflict(Event& local, const std::string& id, const std::string& token, SyncResult& r);
  Json eventBody(const Event& e) const;

  IHttp& http_;
  IStorage& s_;
  ContactStore& contacts_;
  EventStore& events_;
  Logger& log_;
  std::string utcOffset_ = "Z";
};

// Runs syncs on a schedule: every 15 minutes while healthy, exponential backoff (30 s up to 15 min) when offline or on
// a server error, and no retries at all after an authorisation problem until the user signs in again.
class SyncManager {
 public:
  struct State {
    long long nextMs = 0, lastOkMs = 0;
    int failures = 0;
    SyncStatus last = SyncStatus::Ok;
    bool everRan = false;
    std::string detail;
  };
  SyncManager(AccountManager& a, GoogleSync& g, Logger& log, std::function<long long()> nowMs, std::function<bool()> online = {})
      : accounts_(a), sync_(g), log_(log), now_(std::move(nowMs)), online_(std::move(online)) {}

  void tick();                                    // call about once a second; cheap when nothing is due
  SyncResult syncNow(const std::string& accountId);  // runs every enabled service now, ignoring the schedule
  const State* state(const std::string& accountId) const;
  static long long backoffMs(int failures);
  static constexpr long long kIntervalMs = 15LL * 60 * 1000;

 private:
  SyncResult run(const Account& a);
  AccountManager& accounts_;
  GoogleSync& sync_;
  Logger& log_;
  std::function<long long()> now_;
  std::function<bool()> online_;
  std::map<std::string, State> st_;
};

}  // namespace fai
