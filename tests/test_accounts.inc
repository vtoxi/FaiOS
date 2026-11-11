// Included by test_core.cpp: JSON, crypto helpers, OAuth/PKCE, account manager.
// FakeHttp is a MOCK transport. Nothing here talks to Google.
#include <deque>

#include "accounts.h"
#include "json.h"
#include "oauth.h"

struct FakeHttp : IHttp {
  std::deque<HttpResponse> script;
  std::vector<HttpRequest> seen;
  HttpResponse send(const HttpRequest& r) override {
    seen.push_back(r);
    if (script.empty()) return {0, "", "script exhausted"};
    HttpResponse x = script.front();
    script.pop_front();
    return x;
  }
};

struct FakeRandom : IRandom {  // deterministic, NOT secure: tests only
  uint8_t n = 1;
  bool fail = false;
  bool bytes(uint8_t* out, size_t len) override {
    if (fail) return false;
    for (size_t i = 0; i < len; i++) out[i] = n++;
    return true;
  }
};

// Reversible test-only box: the stored form does not contain the plaintext, which lets tests check that
// secrets never reach storage in the clear. It is NOT encryption.
struct ObfuscatingBox : ISecretBox {
  const char* name() const override { return "test-xor"; }
  bool available() const override { return true; }
  bool seal(const std::string& p, std::string& b) override { std::string x = p; for (char& c : x) c ^= 0x5A; b = base64Encode("T1" + x); return true; }
  bool open(const std::string& b, std::string& p) override {
    std::string raw;
    if (!base64Decode(b, raw) || raw.compare(0, 2, "T1") != 0) return false;
    p = raw.substr(2);
    for (char& c : p) c ^= 0x5A;
    return true;
  }
};

static std::string makeJwt(const std::string& aud, long long expSec, const std::string& iss = "https://accounts.google.com") {
  Json p = Json::makeObject();
  p.set("iss", Json::makeString(iss)).set("aud", Json::makeString(aud)).set("sub", Json::makeString("1100223344"));
  p.set("email", Json::makeString("ayesha@example.com")).set("email_verified", Json::makeBool(true));
  p.set("name", Json::makeString("Ayesha Khan")).set("picture", Json::makeString("https://example.com/a.png"));
  p.set("exp", Json::makeNumber(double(expSec)));
  return base64UrlEncode("{\"alg\":\"RS256\"}") + "." + base64UrlEncode(p.dump()) + ".sig";
}

static HttpResponse tokenOk(const std::string& jwt, const std::string& access = "ACCESS-1", const std::string& refresh = "REFRESH-SECRET-1",
                            const std::string& scopes = "openid email profile") {
  Json j = Json::makeObject();
  j.set("access_token", Json::makeString(access)).set("expires_in", Json::makeNumber(3600)).set("scope", Json::makeString(scopes));
  if (!refresh.empty()) j.set("refresh_token", Json::makeString(refresh));
  if (!jwt.empty()) j.set("id_token", Json::makeString(jwt));
  return {200, j.dump(), ""};
}

static HttpResponse oauthError(int status, const std::string& code) { return {status, "{\"error\":\"" + code + "\"}", ""}; }

static OAuthClient testClient() {
  OAuthClient c;
  c.clientId = "client-123.apps.example";
  c.redirectUri = "http://127.0.0.1:8765/";
  return c;
}

struct Rig {
  MemStorage store;
  FakeHttp http;
  FakeRandom rng;
  ObfuscatingBox box;
  Logger log{500};
  long long now = 1000000;
  std::vector<std::string> logged;
  std::unique_ptr<AccountManager> am;
  explicit Rig(ISecretBox* custom = nullptr) {
    log.min = Level::Debug;
    log.sink = [this](const LogEntry& e) { logged.push_back(e.tag + ": " + e.msg); };
    am = std::make_unique<AccountManager>(store, custom ? *custom : static_cast<ISecretBox&>(box), http, rng, log, [this] { return now; });
    am->configure(testClient());
  }
  // Full sign-in using the mock transport. Returns the new account id.
  std::string signIn(const std::string& scopes = "openid email profile") {
    AuthRequest req = am->beginSignIn({"openid", "email", "profile"});
    http.script.push_back(tokenOk(makeJwt("client-123.apps.example", now / 1000 + 3600), "ACCESS-1", "REFRESH-SECRET-1", scopes));
    AddResult r = am->completeSignIn(req, "http://127.0.0.1:8765/?code=AUTHCODE&state=" + req.state);
    return r.status == AddStatus::Ok ? r.accountId : "";
  }
};

static void test_sha256_and_base64() {
  CHECK(hex(sha256("")) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(hex(sha256("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  // 56-byte message crosses the padding boundary (FIPS 180-2 test vector)
  CHECK(hex(sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  CHECK(base64Encode("Man") == "TWFu" && base64Encode("Ma") == "TWE=" && base64Encode("M") == "TQ==");
  CHECK(base64UrlEncode(std::string("\xfb\xff\xfe", 3)) == "-__-");
  std::string out;
  CHECK(base64UrlDecode("-__-", out) && out == std::string("\xfb\xff\xfe", 3));
  CHECK(base64Decode("TWE=", out) && out == "Ma");
  CHECK(!base64Decode("TW!u", out));
  CHECK(percentEncode("a b&c=d~") == "a%20b%26c%3Dd~" && percentDecode("a%20b+c%3D") == "a b c=");
}

static void test_pkce_rfc7636_vector() {
  // RFC 7636 appendix B
  CHECK(base64UrlEncode(sha256("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk")) == "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
  FakeRandom r;
  Pkce p;
  CHECK(makePkce(r, p) && p.verifier.size() == 43 && p.challenge.size() == 43);
  CHECK(base64UrlEncode(sha256(p.verifier)) == p.challenge);
  r.fail = true;
  CHECK(!makePkce(r, p));  // no entropy -> fail closed
}

static void test_json() {
  Json j;
  std::string err;
  CHECK(Json::parse("{\"a\":[1,2.5,-3e2,true,null,\"x\"],\"b\":{\"c\":\"d\"}}", j, &err) && j["a"].size() == 6);
  CHECK(j["a"].at(1).asNumber() == 2.5 && j["a"].at(2).asNumber() == -300 && j["a"].at(3).asBool() && j["a"].at(4).isNull());
  CHECK(j["b"]["c"].asString() == "d" && j["missing"]["deeper"].asString("dflt") == "dflt" && j["a"].at(99).isNull());
  CHECK(Json::parse("\"caf\\u00e9 \\ud83d\\ude00 \\n\"", j) && j.asString() == "caf\xc3\xa9 \xf0\x9f\x98\x80 \n");
  Json round;
  CHECK(Json::parse(Json::makeObject().set("k", Json::makeString("q\"\\\n\t")).set("n", Json::makeNumber(42)).dump(), round));
  CHECK(round["k"].asString() == "q\"\\\n\t" && round["n"].asNumber() == 42);
  for (const char* bad : {"", "{", "[1,]", "{\"a\":}", "01", "1.", "\"\\ud800\"", "\"\\x\"", "tru", "[1] x", "{\"a\" 1}", "\"unterminated"})
    CHECK(!Json::parse(bad, j));
  std::string deep(200, '[');
  CHECK(!Json::parse(deep, j));  // nesting limit instead of a stack overflow
}

static void test_auth_url_and_redirect() {
  FakeRandom rng;
  AuthRequest r;
  CHECK(buildAuthRequest(testClient(), {"openid", "email"}, rng, "ayesha@example.com", r));
  CHECK(r.url.rfind("https://accounts.google.com/o/oauth2/v2/auth?", 0) == 0);
  for (const char* part : {"client_id=client-123.apps.example", "response_type=code", "scope=openid%20email", "code_challenge_method=S256",
                           "access_type=offline", "redirect_uri=http%3A%2F%2F127.0.0.1%3A8765%2F", "login_hint=ayesha%40example.com"})
    CHECK(r.url.find(part) != std::string::npos);
  CHECK(r.url.find("code_challenge=" + r.pkce.challenge) != std::string::npos && r.url.find("state=" + r.state) != std::string::npos);
  CHECK(r.url.find(r.pkce.verifier) == std::string::npos);  // the verifier never leaves the device in the URL
  CHECK(r.url.find("client_secret") == std::string::npos && r.url.find("password") == std::string::npos);
  OAuthClient none;
  CHECK(!buildAuthRequest(none, {"openid"}, rng, "", r));  // not configured

  std::string code;
  CHECK(parseRedirect("http://127.0.0.1:8765/?state=S1&code=4%2FabC", "S1", code) == RedirectResult::Code && code == "4/abC");
  CHECK(parseRedirect("http://127.0.0.1:8765/?state=OTHER&code=x", "S1", code) == RedirectResult::StateMismatch);
  CHECK(parseRedirect("http://127.0.0.1:8765/?code=x", "S1", code) == RedirectResult::StateMismatch);  // missing state
  CHECK(parseRedirect("http://127.0.0.1:8765/?error=access_denied&state=S1", "S1", code) == RedirectResult::Denied);
  CHECK(parseRedirect("http://127.0.0.1:8765/", "S1", code) == RedirectResult::Malformed);
  CHECK(parseRedirect("http://127.0.0.1:8765/?state=S1", "S1", code) == RedirectResult::Malformed);
}

static void test_token_endpoint() {
  FakeHttp h;
  OAuthClient c = testClient();
  h.script.push_back(tokenOk("jwt"));
  TokenResult r = exchangeCode(h, c, "CODE", "VERIFIER", 5000);
  CHECK(r.ok && r.tokens.access == "ACCESS-1" && r.tokens.refresh == "REFRESH-SECRET-1" && r.tokens.expiresAtMs == 5000 + 3600 * 1000);
  CHECK(r.tokens.scopes.size() == 3 && r.tokens.scopes[2] == "profile");
  const HttpRequest& q = h.seen[0];
  CHECK(q.method == "POST" && q.url == "https://oauth2.googleapis.com/token");
  for (const char* part : {"grant_type=authorization_code", "code=CODE", "code_verifier=VERIFIER", "client_id=client-123.apps.example"})
    CHECK(q.body.find(part) != std::string::npos);
  CHECK(q.body.find("client_secret") == std::string::npos);  // none configured -> none sent
  c.clientSecret = "NONCONFIDENTIAL";
  h.script.push_back(tokenOk("jwt", "A2", ""));
  r = refreshTokens(h, c, "OLD-REFRESH", 0);
  CHECK(r.ok && r.tokens.refresh == "OLD-REFRESH" && r.tokens.access == "A2");  // Google omits it on refresh
  CHECK(h.seen[1].body.find("grant_type=refresh_token") != std::string::npos && h.seen[1].body.find("client_secret=NONCONFIDENTIAL") != std::string::npos);

  h.script.push_back(oauthError(400, "invalid_grant"));
  r = refreshTokens(h, c, "x", 0);
  CHECK(!r.ok && r.err == AuthError::InvalidGrant && r.detail == "invalid_grant");
  h.script.push_back(oauthError(401, "invalid_client"));
  CHECK(refreshTokens(h, c, "x", 0).err == AuthError::InvalidClient);
  h.script.push_back({503, "oops", ""});
  CHECK(refreshTokens(h, c, "x", 0).err == AuthError::Server);
  h.script.push_back({200, "not json", ""});
  CHECK(refreshTokens(h, c, "x", 0).err == AuthError::Parse);
  h.script.push_back({200, "{\"expires_in\":5}", ""});
  CHECK(refreshTokens(h, c, "x", 0).err == AuthError::Parse);  // 200 but no access token
  CHECK(refreshTokens(h, c, "x", 0).err == AuthError::Network);  // empty script = transport failure

  h.script.push_back({200, "", ""});
  std::string d;
  CHECK(revokeToken(h, c, "TOK", &d) && h.seen.back().url == "https://oauth2.googleapis.com/revoke" && h.seen.back().body == "token=TOK");
  h.script.push_back({400, "", ""});
  CHECK(!revokeToken(h, c, "TOK", &d) && d == "http_400");
}

static void test_id_token_validation() {
  long long now = 2000000;
  IdClaims c = parseIdToken(makeJwt("client-123.apps.example", now / 1000 + 100), "client-123.apps.example", now);
  CHECK(c.ok && c.sub == "1100223344" && c.email == "ayesha@example.com" && c.name == "Ayesha Khan" && c.emailVerified);
  CHECK(!parseIdToken(makeJwt("other-client", now / 1000 + 100), "client-123.apps.example", now).ok);          // wrong audience
  CHECK(!parseIdToken(makeJwt("client-123.apps.example", now / 1000 - 1), "client-123.apps.example", now).ok); // expired
  CHECK(!parseIdToken(makeJwt("client-123.apps.example", now / 1000 + 100, "https://evil.example"), "client-123.apps.example", now).ok);
  CHECK(!parseIdToken("garbage", "client-123.apps.example", now).ok);
  CHECK(!parseIdToken("a.!!!.c", "client-123.apps.example", now).ok);
}

static void test_account_sign_in_and_storage() {
  Rig t;
  std::string id = t.signIn();
  CHECK(id == "1100223344" && t.am->accounts().size() == 1);
  const Account* a = t.am->find(id);
  CHECK(a && a->email == "ayesha@example.com" && a->name == "Ayesha Khan" && a->status == AccountStatus::Active && !a->syncContacts);
  // secrets never reach storage in the clear
  std::string meta;
  t.store.read("accounts.json", meta);
  CHECK(meta.find("REFRESH-SECRET-1") == std::string::npos && meta.find("ACCESS-1") == std::string::npos);
  auto blobs = t.store.list("tokens/");
  CHECK(blobs.size() == 1);
  std::string blob;
  t.store.read("tokens/" + blobs[0], blob);
  CHECK(!blob.empty() && blob.find("REFRESH-SECRET-1") == std::string::npos);
  // the token request carried the PKCE verifier and no password
  CHECK(t.http.seen[0].body.find("code_verifier=") != std::string::npos && t.http.seen[0].body.find("password") == std::string::npos);
  // tokens never appear in logs
  for (auto& line : t.logged) CHECK(line.find("REFRESH-SECRET") == std::string::npos && line.find("ACCESS-1") == std::string::npos && line.find("ayesha") == std::string::npos);
  // persistence: a new manager on the same storage sees the account and can read its tokens
  Rig t2;
  t2.store = t.store;
  t2.am->load();
  CHECK(t2.am->accounts().size() == 1 && t2.am->accounts()[0].name == "Ayesha Khan");
  TokenFetch f = t2.am->accessToken(id);
  CHECK(f.status == TokenFetch::Ok && f.token == "ACCESS-1");
}

static void test_account_sign_in_failures() {
  Rig t;
  AuthRequest req = t.am->beginSignIn({"openid"});
  CHECK(t.am->completeSignIn(req, "http://127.0.0.1:8765/?code=C&state=WRONG").status == AddStatus::StateMismatch);
  CHECK(t.am->completeSignIn(req, "http://127.0.0.1:8765/?error=access_denied&state=" + req.state).status == AddStatus::Denied);
  CHECK(t.am->completeSignIn(req, "http://127.0.0.1:8765/").status == AddStatus::BadRedirect);
  CHECK(t.http.seen.empty());  // none of those reached the network
  CHECK(t.am->completeSignIn(req, "http://127.0.0.1:8765/?code=C&state=" + req.state).status == AddStatus::Network);  // offline
  t.http.script.push_back(oauthError(400, "invalid_grant"));
  CHECK(t.am->completeSignIn(req, "http://127.0.0.1:8765/?code=C&state=" + req.state).status == AddStatus::TokenError);
  t.http.script.push_back(tokenOk(makeJwt("someone-else", t.now / 1000 + 100)));
  CHECK(t.am->completeSignIn(req, "http://127.0.0.1:8765/?code=C&state=" + req.state).status == AddStatus::BadIdToken);
  t.http.script.push_back(tokenOk(makeJwt("client-123.apps.example", t.now / 1000 + 100), "A", ""));  // no refresh token
  CHECK(t.am->completeSignIn(req, "http://127.0.0.1:8765/?code=C&state=" + req.state).status == AddStatus::TokenError);
  CHECK(t.am->accounts().empty() && t.store.list("tokens/").empty());  // nothing half-saved

  Rig unconfigured;
  unconfigured.am->configure(OAuthClient());
  CHECK(unconfigured.am->beginSignIn({"openid"}).url.empty());
  CHECK(unconfigured.am->completeSignIn(AuthRequest(), "x").status == AddStatus::NotConfigured);

  Rig noRandom;
  noRandom.rng.fail = true;
  CHECK(noRandom.am->beginSignIn({"openid"}).url.empty());  // no entropy -> no sign-in
}

static void test_no_secure_storage_fails_closed() {
  NoSecretBox none;
  Rig t(&none);
  CHECK(!t.am->secureStorage());
  AuthRequest req = t.am->beginSignIn({"openid"});
  AddResult r = t.am->completeSignIn(req, "http://127.0.0.1:8765/?code=C&state=" + req.state);
  CHECK(r.status == AddStatus::NoSecureStorage);
  CHECK(t.http.seen.empty());                                // the authorization code was not even exchanged
  CHECK(t.store.list("").empty());                           // nothing written anywhere
}

static void test_token_refresh_and_reauth() {
  Rig t;
  std::string id = t.signIn();
  t.http.seen.clear();
  CHECK(t.am->accessToken(id).token == "ACCESS-1" && t.http.seen.empty());  // still valid: no network call
  t.now += 3550 * 1000;                                                   // within 60 s of expiry
  t.http.script.push_back(tokenOk("", "ACCESS-2", "", "openid email profile"));
  TokenFetch f = t.am->accessToken(id);
  CHECK(f.status == TokenFetch::Ok && f.token == "ACCESS-2" && t.http.seen.size() == 1);
  CHECK(t.http.seen[0].body.find("refresh_token=REFRESH-SECRET-1") != std::string::npos);  // old refresh token reused
  CHECK(t.am->accessToken(id).token == "ACCESS-2" && t.http.seen.size() == 1);             // cached again

  t.now += 3600 * 1000;  // offline during refresh: account untouched, retry later
  CHECK(t.am->accessToken(id).status == TokenFetch::Offline && t.am->find(id)->status == AccountStatus::Active);
  t.http.script.push_back(oauthError(500, "backend_error"));
  CHECK(t.am->accessToken(id).status == TokenFetch::Error && t.am->find(id)->status == AccountStatus::Active);
  t.http.script.push_back(oauthError(400, "invalid_grant"));  // user revoked access in their Google account
  CHECK(t.am->accessToken(id).status == TokenFetch::NeedsReauth && t.am->find(id)->status == AccountStatus::NeedsReauth);
  CHECK(t.store.list("tokens/").empty());                      // dead tokens wiped
  size_t calls = t.http.seen.size();
  CHECK(t.am->accessToken(id).status == TokenFetch::NeedsReauth && t.http.seen.size() == calls);  // no retry storm
  CHECK(t.am->accessToken("nobody").status == TokenFetch::NoAccount);
  Rig t3;  // re-auth path: signing in again revives the same account instead of duplicating it
  t3.store = t.store;
  t3.am->load();
  CHECK(t3.signIn() == id && t3.am->accounts().size() == 1 && t3.am->accounts()[0].status == AccountStatus::Active);
}

static void test_tampered_tokens_need_reauth() {
  Rig t;
  std::string id = t.signIn();
  auto blobs = t.store.list("tokens/");
  t.store.write("tokens/" + blobs[0], "AAAA");  // corrupted / wrong key
  Rig t2;
  t2.store = t.store;
  t2.am->load();
  CHECK(t2.am->accessToken(id).status == TokenFetch::NeedsReauth && t2.http.seen.empty());
}

static void test_scope_changes_and_sync_flags() {
  Rig t;
  std::string id = t.signIn("openid email profile https://www.googleapis.com/auth/contacts.readonly");
  CHECK(!t.am->setSync(id, SyncService::Calendar, true));  // never enable what was not consented to
  CHECK(t.am->setSync(id, SyncService::Contacts, true) && t.am->find(id)->syncContacts);
  t.now += 4000 * 1000;
  t.http.script.push_back(tokenOk("", "A3", "", "openid email profile"));  // consent narrowed: contacts scope gone
  CHECK(t.am->accessToken(id).status == TokenFetch::Ok);
  CHECK(!t.am->find(id)->syncContacts && !AccountManager::hasScope(*t.am->find(id), SyncService::Contacts));
  CHECK(t.am->find(id)->lastSyncMs == 0);
  t.am->markSynced(id);
  CHECK(t.am->find(id)->lastSyncMs == t.now);
}

static void test_remove_account() {
  Rig t;
  std::string id = t.signIn();
  t.http.script.push_back({200, "", ""});
  std::string d;
  CHECK(t.am->remove(id, &d) && t.am->accounts().empty() && t.store.list("tokens/").empty());
  CHECK(t.http.seen.back().url.find("/revoke") != std::string::npos && t.http.seen.back().body == "token=REFRESH-SECRET-1");
  Rig t2;
  id = t2.signIn();
  CHECK(!t2.am->remove(id, &d));                                    // offline: revocation not confirmed...
  CHECK(t2.am->accounts().empty() && t2.store.list("tokens/").empty());  // ...but local data is gone regardless
  bool warned = false;
  for (auto& l : t2.logged) warned |= l.find("NOT confirmed") != std::string::npos;
  CHECK(warned);
  CHECK(!t2.am->remove("nobody"));
}

#ifdef FAIOS_HAVE_OPENSSL
static void test_aes_gcm_secret_box() {
  FakeRandom rng;
  AesGcmSecretBox box("correct horse battery staple", rng, 1000);  // low iteration count: tests only
  std::string blob, out;
  CHECK(box.available() && box.seal("refresh-token-xyz", blob) && blob.find("refresh-token-xyz") == std::string::npos);
  CHECK(box.open(blob, out) && out == "refresh-token-xyz");
  std::string blob2;
  CHECK(box.seal("refresh-token-xyz", blob2) && blob2 != blob);  // fresh salt and IV each time
  AesGcmSecretBox wrong("wrong key material", rng, 1000);
  CHECK(!wrong.open(blob, out));                                  // wrong key
  std::string raw;
  base64Decode(blob, raw);
  raw[raw.size() - 20] ^= 1;                                      // flip a ciphertext bit
  CHECK(!box.open(base64Encode(raw), out));                       // tamper detected
  base64Decode(blob, raw);
  raw[10] ^= 1;                                                   // flip a header (AAD) bit
  CHECK(!box.open(base64Encode(raw), out));
  CHECK(!box.open("", out) && !box.open("not base64!!", out));
  CHECK(box.seal("", blob) && box.open(blob, out) && out.empty());  // empty secret round-trips
  AesGcmSecretBox empty("", rng, 1000);
  CHECK(!empty.available() && !empty.seal("x", blob));            // no key material -> refuses
  Rig t(&box);                                                    // and the real box works under the account manager
  CHECK(t.signIn() == "1100223344");
  CHECK(t.am->accessToken("1100223344").token == "ACCESS-1");
}
#endif

// ---- Settings -> Accounts screens (real AccountManager, MOCK transport) ----
static void openAccounts(AppManager& m) {
  launch(m, "Settings");
  for (int i = 0; i < 3; i++) press(m, Key::Down);  // Wi-Fi, Bluetooth, Brightness, then Accounts
  press(m, Key::Ok);
}

static void test_accounts_screens() {
  Rig t;
  auto* m = boot();
  m->accounts = t.am.get();
  launch(*m, "Settings");
  for (int i = 0; i < 3; i++) press(*m, Key::Down);
  CHECK(m->top()->view().rows[3].text == "Accounts" && m->top()->view().rows[3].trail == "0");
  press(*m, Key::Ok);
  CHECK(title(*m) == "Accounts" && m->top()->view().rows.size() == 1 && m->top()->view().rows[0].text == "Add account");
  press(*m, Key::Ok);
  CHECK(title(*m) == "Add account" && m->top()->view().rows.size() == 1);  // no [MOCK] row without the simulator flag
  press(*m, Key::Ok);
  CHECK(title(*m) == "Google");
  auto lines = m->top()->view().lines;
  CHECK(lines.back().find("external browser") != std::string::npos && lines.size() == 2);  // configured + secure storage OK
  delete m;

  // each missing prerequisite is reported, not hidden
  Rig bare;
  bare.am->configure(OAuthClient());
  auto* b = boot();
  b->accounts = bare.am.get();
  openAccounts(*b);
  press(*b, Key::Ok);
  press(*b, Key::Ok);
  std::string all;
  for (auto& l : b->top()->view().lines) all += l + "|";
  CHECK(all.find("client ID") != std::string::npos);
  delete b;
  NoSecretBox none;
  Rig nosec(&none);
  auto* c = boot();
  c->accounts = nosec.am.get();
  openAccounts(*c);
  press(*c, Key::Ok);
  press(*c, Key::Ok);
  all.clear();
  for (auto& l : c->top()->view().lines) all += l + "|";
  CHECK(all.find("No secure storage") != std::string::npos);
  delete c;

  auto* d = boot();  // service unavailable
  launch(*d, "Settings");
  for (int i = 0; i < 3; i++) press(*d, Key::Down);
  CHECK(d->top()->view().rows[3].trail == "-");
  press(*d, Key::Ok);
  CHECK(d->top()->view().rows[0].sub == "Service unavailable");
  press(*d, Key::Ok);
  CHECK(title(*d) == "Accounts");  // nothing to open
  delete d;
}

static void test_account_detail_and_sign_out() {
  Rig t;
  std::string id = t.signIn("openid email profile https://www.googleapis.com/auth/contacts.readonly");
  auto* m = boot();
  m->accounts = t.am.get();
  openAccounts(*m);
  auto v = m->top()->view();
  CHECK(v.rows.size() == 2 && v.rows[1].text == "Ayesha Khan" && v.rows[1].sub == "ayesha@example.com" && v.rows[1].trail == "OK");
  press(*m, Key::Down);
  press(*m, Key::Ok);
  v = m->top()->view();
  CHECK(title(*m) == "Account" && v.rows.size() == 6 && v.rows[0].text == "Ayesha Khan" && v.rows[0].lead == "@A");
  CHECK(v.rows[1].trail == "OK" && v.rows[2].trail == "Never" && v.rows[3].toggle == 0 && v.rows[4].toggle == 0);
  for (int i = 0; i < 4; i++) press(*m, Key::Down);  // Sync calendar: scope was NOT granted
  press(*m, Key::Ok);
  CHECK(!t.am->find(id)->syncCalendar && m->notifications.count() == 1 && m->notifications.list()[0].body.find("Calendar") != std::string::npos);
  press(*m, Key::Up);
  press(*m, Key::Ok);  // Sync contacts: granted
  CHECK(t.am->find(id)->syncContacts && m->top()->view().rows[3].toggle == 1);
  t.am->markSynced(id);
  CHECK(m->top()->view().rows[2].trail != "Never");
  press(*m, Key::Down); press(*m, Key::Down);
  press(*m, Key::Ok);  // Sign out -> confirmation
  CHECK(title(*m) == "Sign out?");
  press(*m, Key::Down);
  press(*m, Key::Ok);  // Cancel
  CHECK(title(*m) == "Account" && t.am->accounts().size() == 1);
  press(*m, Key::Ok);
  t.http.script.push_back({200, "", ""});
  press(*m, Key::Ok);  // confirm
  CHECK(title(*m) == "Accounts" && t.am->accounts().empty() && m->top()->view().rows.size() == 1);
  CHECK(t.store.list("tokens/").empty());
  delete m;
}

static void test_mock_signin_hook_is_visible_only_when_set() {
  Rig t;
  auto* m = boot();
  m->accounts = t.am.get();
  bool called = false;
  m->mockSignIn = [&] { called = true; };
  openAccounts(*m);
  press(*m, Key::Ok);
  auto v = m->top()->view();
  CHECK(v.rows.size() == 2 && v.rows[1].text.find("[MOCK]") == 0);  // always labelled
  press(*m, Key::Down);
  press(*m, Key::Ok);
  CHECK(called && title(*m) == "Accounts");
  delete m;
}
