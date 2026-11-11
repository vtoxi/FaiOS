// Local calendar events (the local-first store). Every event records where it came from, so events synced from
// an account later stay identifiable and are never mixed up with local ones.
#pragma once
#include <string>
#include <vector>

#include "calendar.h"
#include "services.h"

namespace fai {

struct Event {
  int id = 0;
  Date date{2000, 1, 1};
  int startMin = -1;        // minutes since midnight; -1 = all day
  int durationMin = 60;
  std::string title;
  std::string source = "local";  // "local", or the account id the event is synced with
  std::string remoteId, etag;    // Google event id / etag once synced
  bool dirty = false;            // changed locally since the last sync (this IS the offline queue)
  bool deleted = false;          // tombstone: deleted locally, waiting to be deleted remotely
};

inline bool sameDay(const Date& a, const Date& b) { return a.y == b.y && a.m == b.m && a.d == b.d; }
bool validDate(const Date& d);

class EventStore {
 public:
  explicit EventStore(IStorage& s) : s_(s) {}
  bool load();  // "calendar/local.json"; missing file = no events; corrupt file = ignored (false)
  int add(Date d, int startMin, int durationMin, const std::string& title, const std::string& source = "local");  // 0 = rejected
  bool remove(int id);  // a synced event becomes a tombstone until the deletion reaches the account
  bool setTitle(int id, const std::string& title);  // marks a synced event dirty
  bool save();
  std::vector<Event>& raw() { return events_; }  // for the sync engine only
  int nextId() { return next_++; }
  std::vector<Event> forDay(const Date& d) const;  // all-day first, then by start time, then id
  bool hasEvents(const Date& d) const;
  const std::vector<Event>& all() const { return events_; }
  static constexpr size_t kMax = 500;
 private:
  IStorage& s_;
  std::vector<Event> events_;
  int next_ = 1;
};

}  // namespace fai
