#include "contacts.h"

#include <algorithm>

#include "json.h"

namespace fai {

static const char* kFile = "contacts/store.json";

std::string ContactStore::cleanName(const std::string& s) {
  std::string o;
  for (unsigned char c : s) if (c >= 0x20 && c != 0x7f) o += char(c);
  size_t a = o.find_first_not_of(' ');
  if (a == std::string::npos) return "";
  o = o.substr(a, o.find_last_not_of(' ') - a + 1);
  if (o.size() > 64) o.resize(64);
  return o;
}

std::string ContactStore::cleanNumber(const std::string& s) {
  std::string o;
  for (char c : s) if ((c >= '0' && c <= '9') || c == '+' || c == ' ' || c == '-' || c == '(' || c == ')' || c == '*' || c == '#') o += c;
  if (o.size() > 32) o.resize(32);
  return o;
}

bool ContactStore::save() {
  Json arr = Json::makeArray();
  for (auto& c : items_) {
    Json o = Json::makeObject();
    o.set("id", Json::makeNumber(c.id)).set("name", Json::makeString(c.name)).set("number", Json::makeString(c.number));
    o.set("source", Json::makeString(c.source)).set("rid", Json::makeString(c.remoteId)).set("etag", Json::makeString(c.etag));
    o.set("dirty", Json::makeBool(c.dirty)).set("deleted", Json::makeBool(c.deleted));
    arr.push(o);
  }
  Json root = Json::makeObject();
  root.set("next_id", Json::makeNumber(next_)).set("contacts", arr);
  return s_.write(kFile, root.dump());
}

bool ContactStore::load() {
  items_.clear();
  next_ = 1;
  std::string text;
  if (!s_.read(kFile, text)) return true;
  Json j;
  if (!Json::parse(text, j) || !j.isObject()) return false;
  for (auto& o : j["contacts"].items()) {
    StoredContact c;
    c.id = int(o["id"].asNumber());
    c.name = cleanName(o["name"].asString());
    c.number = cleanNumber(o["number"].asString());
    c.source = o["source"].asString("local");
    c.remoteId = o["rid"].asString();
    c.etag = o["etag"].asString();
    c.dirty = o["dirty"].asBool();
    c.deleted = o["deleted"].asBool();
    if (c.id <= 0 || c.name.empty() || items_.size() >= kMax) continue;
    items_.push_back(std::move(c));
  }
  next_ = std::max(1, int(j["next_id"].asNumber(1)));
  for (auto& c : items_) next_ = std::max(next_, c.id + 1);
  return true;
}

int ContactStore::add(const std::string& nameIn, const std::string& numberIn) {
  std::string name = cleanName(nameIn);
  if (name.empty() || items_.size() >= kMax) return 0;
  StoredContact c;
  c.id = next_++;
  c.name = name;
  c.number = cleanNumber(numberIn);
  c.dirty = true;
  items_.push_back(c);
  if (!save()) { items_.pop_back(); next_--; return 0; }
  return c.id;
}

bool ContactStore::update(int id, const std::string& nameIn, const std::string& numberIn) {
  std::string name = cleanName(nameIn);
  if (name.empty()) return false;
  for (auto& c : items_)
    if (c.id == id && !c.deleted) {
      StoredContact old = c;
      c.name = name;
      c.number = cleanNumber(numberIn);
      c.dirty = true;
      if (!save()) { c = old; return false; }
      return true;
    }
  return false;
}

bool ContactStore::remove(int id) {
  auto it = std::find_if(items_.begin(), items_.end(), [id](const StoredContact& c) { return c.id == id; });
  if (it == items_.end() || it->deleted) return false;
  StoredContact gone = *it;
  const bool tomb = !it->remoteId.empty();
  if (tomb) { it->deleted = true; it->dirty = true; }
  else items_.erase(it);
  if (!save()) {
    if (tomb) { for (auto& c : items_) if (c.id == gone.id) c = gone; }
    else items_.push_back(gone);
    return false;
  }
  return true;
}

std::vector<StoredContact> ContactStore::visible() const {
  std::vector<StoredContact> v;
  for (auto& c : items_) if (!c.deleted) v.push_back(c);
  std::stable_sort(v.begin(), v.end(), [](const StoredContact& a, const StoredContact& b) { return a.name < b.name; });
  return v;
}

}  // namespace fai
