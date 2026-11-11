// Contacts that live on the phone ("contacts/store.json"). Synced contacts keep their Google resourceName and etag;
// `dirty` marks a local change not yet sent (this is the offline queue), `deleted` is a tombstone for a synced contact.
#pragma once
#include <string>
#include <vector>

#include "services.h"

namespace fai {

struct StoredContact {
  int id = 0;
  std::string name, number;
  std::string source = "local";  // "local", or the account id it is synced with
  std::string remoteId, etag;    // Google resourceName ("people/c123") and etag
  bool dirty = false, deleted = false;
};

class ContactStore {
 public:
  explicit ContactStore(IStorage& s) : s_(s) {}
  bool load();  // missing file = empty (true); corrupt file = ignored and NOT overwritten (false)
  bool save();
  int add(const std::string& name, const std::string& number);  // local contact, dirty; 0 = rejected
  bool update(int id, const std::string& name, const std::string& number);
  bool remove(int id);  // synced contacts become tombstones until the deletion reaches the account
  std::vector<StoredContact> visible() const;  // not deleted, sorted by name
  std::vector<StoredContact>& raw() { return items_; }  // for the sync engine only
  int nextId() { return next_++; }
  static std::string cleanName(const std::string& s);
  static std::string cleanNumber(const std::string& s);
  static constexpr size_t kMax = 1000;
 private:
  IStorage& s_;
  std::vector<StoredContact> items_;
  int next_ = 1;
};

}  // namespace fai
