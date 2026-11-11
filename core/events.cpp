#include "events.h"

#include <algorithm>

#include "json.h"

namespace fai {

static const char* kFile = "calendar/local.json";

bool validDate(const Date& d) { return d.y >= 1970 && d.y <= 2100 && d.m >= 1 && d.m <= 12 && d.d >= 1 && d.d <= daysInMonth(d.y, d.m); }

bool EventStore::save() {
  Json arr = Json::makeArray();
  for (auto& e : events_) {
    Json o = Json::makeObject();
    o.set("id", Json::makeNumber(e.id)).set("y", Json::makeNumber(e.date.y)).set("m", Json::makeNumber(e.date.m)).set("d", Json::makeNumber(e.date.d));
    o.set("start", Json::makeNumber(e.startMin)).set("dur", Json::makeNumber(e.durationMin));
    o.set("title", Json::makeString(e.title)).set("source", Json::makeString(e.source));
    arr.push(o);
  }
  Json root = Json::makeObject();
  root.set("next_id", Json::makeNumber(next_)).set("events", arr);
  return s_.write(kFile, root.dump());
}

bool EventStore::load() {
  events_.clear();
  next_ = 1;
  std::string text;
  if (!s_.read(kFile, text)) return true;
  Json j;
  if (!Json::parse(text, j) || !j.isObject()) return false;  // never trust or overwrite a file we cannot read
  for (auto& o : j["events"].items()) {
    Event e;
    e.id = int(o["id"].asNumber());
    e.date = {int(o["y"].asNumber()), int(o["m"].asNumber()), int(o["d"].asNumber())};
    e.startMin = int(o["start"].asNumber(-1));
    e.durationMin = int(o["dur"].asNumber(60));
    e.title = o["title"].asString();
    e.source = o["source"].asString("local");
    if (e.id <= 0 || !validDate(e.date) || e.startMin < -1 || e.startMin >= 1440 || events_.size() >= kMax) continue;  // drop bad rows, keep the rest
    events_.push_back(std::move(e));
  }
  next_ = std::max(1, int(j["next_id"].asNumber(1)));
  for (auto& e : events_) next_ = std::max(next_, e.id + 1);
  return true;
}

int EventStore::add(Date d, int startMin, int durationMin, const std::string& titleIn, const std::string& source) {
  if (!validDate(d) || startMin < -1 || startMin >= 1440 || durationMin < 0 || durationMin > 24 * 60 || events_.size() >= kMax) return 0;
  Event e;
  e.id = next_++;
  e.date = d;
  e.startMin = startMin;
  e.durationMin = durationMin;
  for (unsigned char c : titleIn) if (c >= 0x20 && c != 0x7f) e.title += char(c);
  if (e.title.size() > 60) e.title.resize(60);
  while (!e.title.empty() && e.title.back() == ' ') e.title.pop_back();
  e.title.erase(0, e.title.find_first_not_of(' '));  // whitespace-only titles become the default
  if (e.title.empty()) e.title = "Event";
  e.source = source.empty() ? "local" : source;
  events_.push_back(e);
  if (!save()) { events_.pop_back(); next_--; return 0; }  // not persisted = not added
  return e.id;
}

bool EventStore::remove(int id) {
  auto it = std::find_if(events_.begin(), events_.end(), [id](const Event& e) { return e.id == id; });
  if (it == events_.end()) return false;
  Event gone = *it;
  events_.erase(it);
  if (!save()) { events_.push_back(gone); return false; }
  return true;
}

std::vector<Event> EventStore::forDay(const Date& d) const {
  std::vector<Event> v;
  for (auto& e : events_) if (sameDay(e.date, d)) v.push_back(e);
  std::stable_sort(v.begin(), v.end(), [](const Event& a, const Event& b) { return a.startMin != b.startMin ? a.startMin < b.startMin : a.id < b.id; });
  return v;  // startMin -1 (all day) sorts first
}

bool EventStore::hasEvents(const Date& d) const {
  for (auto& e : events_) if (sameDay(e.date, d)) return true;
  return false;
}

}  // namespace fai
