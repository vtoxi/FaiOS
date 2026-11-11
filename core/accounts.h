// FaiOS account manager (Google accounts). UI-independent.
//   metadata (name, email, status, sync flags)  -> "accounts.json" in IStorage (not secret, but personal)
//   OAuth tokens                                  -> sealed with ISecretBox, "tokens/<id>.bin"; refused if no box
// Network goes through IHttp. Tokens are never logged; only a short hash of the account id appears in logs.
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "oauth.h"
#include "secret_box.h"
#include "services.h"

namespace fai {

enum class AccountStatus { Active, NeedsReauth, Error };
enum class SyncService { Contacts, Calendar };

struct Account {
  std::string id;  // Google "sub"
  std::string email, name, picture;
  AccountStatus status = AccountStatus::Active;
  std::vector<std::string> scopes;  // as granted (may shrink if the user changes consent)
  long long lastSyncMs = 0;         // 0 = never
  bool syncContacts = false, syncCalendar = false;  // off until the user enables them AND the scope is granted
};

enum class AddStatus { Ok, NotConfigured, BadRedirect, StateMismatch, Denied, NoSecureStorage, Network, TokenError, BadIdToken };
struct AddResult { AddStatus status = AddStatus::Ok; std::string accountId, detail; };

struct TokenFetch {
  enum Status { Ok, Offline, NeedsReauth, Error, NoAccount } status = NoAccount;
  std::string token;  // valid access token when status == Ok
};

class AccountManager {
 public:
  AccountManager(IStorage& storage, ISecretBox& box, IHttp& http, IRandom& rng, Logger& log, std::function<long long()> nowMs)
      : s_(storage), box_(box), http_(http), rng_(rng), log_(log), now_(std::move(nowMs)) {}

  void configure(OAuthClient c) { client_ = std::move(c); }
  bool configured() const { return !client_.clientId.empty() && !client_.redirectUri.empty(); }
  bool secureStorage() const { return box_.available(); }

  bool load();  // reads accounts.json; missing file = no accounts (true)
  const std::vector<Account>& accounts() const { return accounts_; }
  const Account* find(const std::string& id) const;

  // Step 1: build the URL the user opens in an EXTERNAL browser. Empty url = not configured / no entropy.
  AuthRequest beginSignIn(const std::vector<std::string>& scopes, const std::string& loginHint = "");
  // Step 2: the browser was redirected here (loopback). Validates state, exchanges the code, stores the account.
  AddResult completeSignIn(const AuthRequest& req, const std::string& redirectedUrl);

  TokenFetch accessToken(const std::string& id);  // refreshes when within 60 s of expiry
  bool remove(const std::string& id, std::string* detail = nullptr);  // revoke (best effort) + wipe local tokens
  bool setSync(const std::string& id, SyncService s, bool on);        // refuses if the scope is not granted
  void markSynced(const std::string& id);
  static bool hasScope(const Account& a, SyncService s);

 private:
  Account* mut(const std::string& id);
  bool save();
  static std::string tokenKey(const std::string& id);
  static std::string tag(const std::string& id);  // short non-reversible id for logs
  bool loadTokens(const std::string& id, TokenSet& t);
  bool storeTokens(const std::string& id, const TokenSet& t);

  IStorage& s_;
  ISecretBox& box_;
  IHttp& http_;
  IRandom& rng_;
  Logger& log_;
  std::function<long long()> now_;
  OAuthClient client_;
  std::vector<Account> accounts_;
  std::map<std::string, TokenSet> cache_;  // in-memory only
};

}  // namespace fai
