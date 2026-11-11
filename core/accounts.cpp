#include "accounts.h"

#include <algorithm>

#include "json.h"

namespace fai {

static const char* kAccountsFile = "accounts.json";

std::string AccountManager::tokenKey(const std::string& id) { return "tokens/" + hex(sha256("faios-token:" + id)).substr(0, 32) + ".bin"; }
std::string AccountManager::tag(const std::string& id) { return hex(sha256("faios-log:" + id)).substr(0, 8); }

const Account* AccountManager::find(const std::string& id) const {
  for (auto& a : accounts_) if (a.id == id) return &a;
  return nullptr;
}
Account* AccountManager::mut(const std::string& id) {
  for (auto& a : accounts_) if (a.id == id) return &a;
  return nullptr;
}

bool AccountManager::hasScope(const Account& a, SyncService s) {
  const char* want[2] = {s == SyncService::Contacts ? scope::kContactsRead : scope::kCalendarRead,
                         s == SyncService::Contacts ? scope::kContacts : scope::kCalendarEvents};
  for (auto& g : a.scopes) if (g == want[0] || g == want[1]) return true;
  return false;
}

static const char* statusName(AccountStatus s) { return s == AccountStatus::Active ? "active" : s == AccountStatus::NeedsReauth ? "needs_reauth" : "error"; }
static AccountStatus statusFrom(const std::string& s) { return s == "needs_reauth" ? AccountStatus::NeedsReauth : s == "error" ? AccountStatus::Error : AccountStatus::Active; }

bool AccountManager::save() {
  Json root = Json::makeObject();
  Json arr = Json::makeArray();
  for (auto& a : accounts_) {
    Json o = Json::makeObject();
    o.set("id", Json::makeString(a.id)).set("email", Json::makeString(a.email)).set("name", Json::makeString(a.name));
    o.set("picture", Json::makeString(a.picture)).set("status", Json::makeString(statusName(a.status)));
    Json sc = Json::makeArray();
    for (auto& g : a.scopes) sc.push(Json::makeString(g));
    o.set("scopes", sc).set("last_sync_ms", Json::makeNumber(double(a.lastSyncMs)));
    o.set("sync_contacts", Json::makeBool(a.syncContacts)).set("sync_calendar", Json::makeBool(a.syncCalendar));
    arr.push(o);
  }
  root.set("accounts", arr);
  bool ok = s_.write(kAccountsFile, root.dump());
  if (!ok) log_.log(Level::Warn, "acct", "could not write accounts.json");
  return ok;
}

bool AccountManager::load() {
  accounts_.clear();
  cache_.clear();
  std::string text;
  if (!s_.read(kAccountsFile, text)) return true;  // first run
  Json j;
  if (!Json::parse(text, j) || !j.isObject()) { log_.log(Level::Error, "acct", "accounts.json is corrupt; ignoring it"); return false; }
  for (auto& o : j["accounts"].items()) {
    Account a;
    a.id = o["id"].asString();
    if (a.id.empty()) continue;
    a.email = o["email"].asString();
    a.name = o["name"].asString();
    a.picture = o["picture"].asString();
    a.status = statusFrom(o["status"].asString());
    for (auto& g : o["scopes"].items()) a.scopes.push_back(g.asString());
    a.lastSyncMs = static_cast<long long>(o["last_sync_ms"].asNumber());
    a.syncContacts = o["sync_contacts"].asBool();
    a.syncCalendar = o["sync_calendar"].asBool();
    accounts_.push_back(std::move(a));
  }
  return true;
}

bool AccountManager::storeTokens(const std::string& id, const TokenSet& t) {
  Json j = Json::makeObject();
  j.set("access", Json::makeString(t.access)).set("refresh", Json::makeString(t.refresh));
  j.set("expires_ms", Json::makeNumber(double(t.expiresAtMs)));
  Json sc = Json::makeArray();
  for (auto& g : t.scopes) sc.push(Json::makeString(g));
  j.set("scopes", sc);
  std::string blob;
  if (!box_.seal(j.dump(), blob) || !s_.write(tokenKey(id), blob)) return false;
  cache_[id] = t;
  return true;
}

bool AccountManager::loadTokens(const std::string& id, TokenSet& t) {
  auto c = cache_.find(id);
  if (c != cache_.end()) { t = c->second; return true; }
  std::string blob, plain;
  Json j;
  if (!s_.read(tokenKey(id), blob) || !box_.open(blob, plain) || !Json::parse(plain, j)) return false;
  t = TokenSet();
  t.access = j["access"].asString();
  t.refresh = j["refresh"].asString();
  t.expiresAtMs = static_cast<long long>(j["expires_ms"].asNumber());
  for (auto& g : j["scopes"].items()) t.scopes.push_back(g.asString());
  if (t.refresh.empty()) return false;
  cache_[id] = t;
  return true;
}

AuthRequest AccountManager::beginSignIn(const std::vector<std::string>& scopes, const std::string& loginHint) {
  AuthRequest r;
  if (!buildAuthRequest(client_, scopes, rng_, loginHint, r)) r = AuthRequest();
  return r;
}

AddResult AccountManager::completeSignIn(const AuthRequest& req, const std::string& redirectedUrl) {
  AddResult res;
  if (!configured() || req.url.empty()) { res.status = AddStatus::NotConfigured; return res; }
  if (!box_.available()) { res.status = AddStatus::NoSecureStorage; res.detail = "no secure storage backend; refusing to store tokens"; return res; }

  std::string code;
  switch (parseRedirect(redirectedUrl, req.state, code)) {
    case RedirectResult::Code: break;
    case RedirectResult::StateMismatch: res.status = AddStatus::StateMismatch; return res;
    case RedirectResult::Denied: res.status = AddStatus::Denied; return res;
    default: res.status = AddStatus::BadRedirect; return res;
  }

  long long now = now_();
  TokenResult tr = exchangeCode(http_, client_, code, req.pkce.verifier, now);
  if (!tr.ok) {
    res.status = tr.err == AuthError::Network ? AddStatus::Network : AddStatus::TokenError;
    res.detail = tr.detail;
    return res;
  }
  if (tr.tokens.refresh.empty()) { res.status = AddStatus::TokenError; res.detail = "no refresh token returned"; return res; }
  IdClaims claims = parseIdToken(tr.tokens.idToken, client_.clientId, now);
  if (!claims.ok) { res.status = AddStatus::BadIdToken; res.detail = claims.error; return res; }

  if (!storeTokens(claims.sub, tr.tokens)) { res.status = AddStatus::NoSecureStorage; res.detail = "could not seal tokens"; return res; }
  Account* a = mut(claims.sub);
  if (!a) { accounts_.push_back(Account()); a = &accounts_.back(); a->id = claims.sub; }
  a->email = claims.email;
  a->name = claims.name.empty() ? claims.email : claims.name;
  a->picture = claims.picture;
  a->scopes = tr.tokens.scopes;
  a->status = AccountStatus::Active;
  save();
  res.accountId = claims.sub;
  log_.log(Level::Info, "acct", "signed in account " + tag(claims.sub));
  return res;
}

TokenFetch AccountManager::accessToken(const std::string& id) {
  TokenFetch f;
  Account* a = mut(id);
  if (!a) return f;
  if (a->status == AccountStatus::NeedsReauth) { f.status = TokenFetch::NeedsReauth; return f; }
  TokenSet t;
  if (!loadTokens(id, t)) {  // unreadable (wrong key, tampered, deleted): cannot continue without the user
    a->status = AccountStatus::NeedsReauth;
    save();
    f.status = TokenFetch::NeedsReauth;
    return f;
  }
  long long now = now_();
  if (!t.access.empty() && t.expiresAtMs - now > 60000) { f.status = TokenFetch::Ok; f.token = t.access; return f; }

  TokenResult r = refreshTokens(http_, client_, t.refresh, now);
  if (r.ok) {
    if (!r.tokens.scopes.empty()) a->scopes = r.tokens.scopes;  // consent may have changed
    if (!storeTokens(id, r.tokens)) { f.status = TokenFetch::Error; return f; }
    a->status = AccountStatus::Active;
    if (!hasScope(*a, SyncService::Contacts)) a->syncContacts = false;
    if (!hasScope(*a, SyncService::Calendar)) a->syncCalendar = false;
    save();
    f.status = TokenFetch::Ok;
    f.token = r.tokens.access;
    return f;
  }
  if (r.err == AuthError::Network) { f.status = TokenFetch::Offline; return f; }  // keep everything; try again later
  if (r.err == AuthError::InvalidGrant) {  // revoked, expired, or password changed: user must sign in again
    s_.remove(tokenKey(id));
    cache_.erase(id);
    a->status = AccountStatus::NeedsReauth;
    save();
    log_.log(Level::Warn, "acct", "account " + tag(id) + " needs re-authentication");
    f.status = TokenFetch::NeedsReauth;
    return f;
  }
  log_.log(Level::Warn, "acct", "token refresh failed for " + tag(id) + ": " + r.detail);
  f.status = TokenFetch::Error;
  return f;
}

bool AccountManager::remove(const std::string& id, std::string* detail) {
  if (!find(id)) return false;
  bool revoked = false;
  TokenSet t;
  if (loadTokens(id, t) && !t.refresh.empty()) revoked = revokeToken(http_, client_, t.refresh, detail);
  else if (detail) *detail = "no stored token to revoke";
  s_.remove(tokenKey(id));  // always wipe locally, even when revocation could not be confirmed
  cache_.erase(id);
  accounts_.erase(std::remove_if(accounts_.begin(), accounts_.end(), [&](const Account& a) { return a.id == id; }), accounts_.end());
  save();
  log_.log(Level::Info, "acct", std::string("removed account ") + tag(id) + (revoked ? " (token revoked)" : " (revocation NOT confirmed)"));
  return revoked;
}

bool AccountManager::setSync(const std::string& id, SyncService s, bool on) {
  Account* a = mut(id);
  if (!a) return false;
  if (on && !hasScope(*a, s)) return false;  // never claim to sync something the user did not consent to
  (s == SyncService::Contacts ? a->syncContacts : a->syncCalendar) = on;
  return save();
}

void AccountManager::markSynced(const std::string& id) {
  if (Account* a = mut(id)) { a->lastSyncMs = now_(); save(); }
}

}  // namespace fai
