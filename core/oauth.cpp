#include "oauth.h"

#include <map>
#include <sstream>

#include "json.h"

namespace fai {

bool makePkce(IRandom& rng, Pkce& out) {
  uint8_t b[32];
  if (!rng.bytes(b, sizeof b)) return false;
  out.verifier = base64UrlEncode(std::string(reinterpret_cast<char*>(b), sizeof b));  // 43 chars, RFC 7636 section 4.1
  out.challenge = base64UrlEncode(sha256(out.verifier));                              // S256
  return true;
}

static std::string join(const std::vector<std::string>& v, char sep) {
  std::string o;
  for (size_t i = 0; i < v.size(); i++) o += (i ? std::string(1, sep) : "") + v[i];
  return o;
}

static std::string form(const std::vector<std::pair<std::string, std::string>>& kv) {
  std::string o;
  for (auto& p : kv) o += (o.empty() ? "" : "&") + percentEncode(p.first) + "=" + percentEncode(p.second);
  return o;
}

bool buildAuthRequest(const OAuthClient& c, const std::vector<std::string>& scopes, IRandom& rng, const std::string& loginHint, AuthRequest& out) {
  if (c.clientId.empty() || c.redirectUri.empty()) return false;
  uint8_t sb[16];
  if (!rng.bytes(sb, sizeof sb) || !makePkce(rng, out.pkce)) return false;
  out.state = base64UrlEncode(std::string(reinterpret_cast<char*>(sb), sizeof sb));
  std::vector<std::pair<std::string, std::string>> q = {
      {"client_id", c.clientId},
      {"redirect_uri", c.redirectUri},
      {"response_type", "code"},
      {"scope", join(scopes, ' ')},
      {"state", out.state},
      {"code_challenge", out.pkce.challenge},
      {"code_challenge_method", "S256"},
      {"access_type", "offline"},  // needed to receive a refresh token
      {"include_granted_scopes", "true"},
      {"prompt", "consent"}};
  if (!loginHint.empty()) q.push_back({"login_hint", loginHint});
  out.url = c.authUrl + "?" + form(q);
  return true;
}

RedirectResult parseRedirect(const std::string& url, const std::string& expectedState, std::string& codeOut) {
  size_t qm = url.find('?');
  if (qm == std::string::npos) return RedirectResult::Malformed;
  std::string q = url.substr(qm + 1);
  size_t hash = q.find('#');
  if (hash != std::string::npos) q = q.substr(0, hash);
  std::map<std::string, std::string> kv;
  std::stringstream ss(q);
  for (std::string part; std::getline(ss, part, '&');) {
    size_t eq = part.find('=');
    if (eq == std::string::npos) continue;
    kv[percentDecode(part.substr(0, eq))] = percentDecode(part.substr(eq + 1));
  }
  if (kv.count("error")) return RedirectResult::Denied;
  if (!kv.count("state") || expectedState.empty() || kv["state"] != expectedState) return RedirectResult::StateMismatch;
  if (!kv.count("code") || kv["code"].empty()) return RedirectResult::Malformed;
  codeOut = kv["code"];
  return RedirectResult::Code;
}

static HttpRequest post(const std::string& url, const std::string& body) {
  HttpRequest r;
  r.method = "POST";
  r.url = url;
  r.body = body;
  r.headers = {{"Content-Type", "application/x-www-form-urlencoded"}, {"Accept", "application/json"}};
  return r;
}

static TokenResult parseTokenResponse(const HttpResponse& resp, const std::string& oldRefresh, long long nowMs) {
  TokenResult r;
  if (resp.status == 0) { r.err = AuthError::Network; r.detail = "network"; return r; }
  Json j;
  if (!Json::parse(resp.body, j) || !j.isObject()) {
    r.err = resp.status >= 500 ? AuthError::Server : AuthError::Parse;
    r.detail = "unreadable response (HTTP " + std::to_string(resp.status) + ")";
    return r;
  }
  if (resp.status != 200) {
    std::string e = j["error"].asString();
    r.detail = e.empty() ? "http_" + std::to_string(resp.status) : e;
    r.err = e == "invalid_grant" ? AuthError::InvalidGrant
            : (e == "invalid_client" || e == "unauthorized_client" || resp.status == 401) ? AuthError::InvalidClient
            : resp.status >= 500 ? AuthError::Server : AuthError::Parse;
    return r;
  }
  r.tokens.access = j["access_token"].asString();
  if (r.tokens.access.empty()) { r.err = AuthError::Parse; r.detail = "no access_token"; return r; }
  r.tokens.refresh = j["refresh_token"].asString(oldRefresh);  // Google omits it on refresh: keep the old one
  r.tokens.idToken = j["id_token"].asString();
  r.tokens.expiresAtMs = nowMs + static_cast<long long>(j["expires_in"].asNumber(3600)) * 1000;
  std::stringstream ss(j["scope"].asString());
  for (std::string s; ss >> s;) r.tokens.scopes.push_back(s);
  r.ok = true;
  return r;
}

TokenResult exchangeCode(IHttp& http, const OAuthClient& c, const std::string& code, const std::string& verifier, long long nowMs) {
  std::vector<std::pair<std::string, std::string>> f = {{"grant_type", "authorization_code"}, {"code", code}, {"client_id", c.clientId},
                                                        {"redirect_uri", c.redirectUri}, {"code_verifier", verifier}};
  if (!c.clientSecret.empty()) f.push_back({"client_secret", c.clientSecret});
  return parseTokenResponse(http.send(post(c.tokenUrl, form(f))), "", nowMs);
}

TokenResult refreshTokens(IHttp& http, const OAuthClient& c, const std::string& refreshToken, long long nowMs) {
  std::vector<std::pair<std::string, std::string>> f = {{"grant_type", "refresh_token"}, {"refresh_token", refreshToken}, {"client_id", c.clientId}};
  if (!c.clientSecret.empty()) f.push_back({"client_secret", c.clientSecret});
  return parseTokenResponse(http.send(post(c.tokenUrl, form(f))), refreshToken, nowMs);
}

bool revokeToken(IHttp& http, const OAuthClient& c, const std::string& token, std::string* detail) {
  HttpResponse r = http.send(post(c.revokeUrl, form({{"token", token}})));
  if (detail) *detail = r.status == 0 ? "network" : "http_" + std::to_string(r.status);
  return r.status == 200;
}

IdClaims parseIdToken(const std::string& jwt, const std::string& expectedClientId, long long nowMs) {
  IdClaims c;
  size_t d1 = jwt.find('.');
  size_t d2 = d1 == std::string::npos ? d1 : jwt.find('.', d1 + 1);
  std::string payload, text;
  Json j;
  if (d2 == std::string::npos || !base64UrlDecode(jwt.substr(d1 + 1, d2 - d1 - 1), payload) || !Json::parse(payload, j) || !j.isObject()) {
    c.error = "malformed id_token";
    return c;
  }
  std::string iss = j["iss"].asString();
  if (iss != "https://accounts.google.com" && iss != "accounts.google.com") { c.error = "wrong issuer"; return c; }
  if (j["aud"].asString() != expectedClientId) { c.error = "wrong audience"; return c; }
  if (j["exp"].asNumber(0) * 1000 <= double(nowMs)) { c.error = "id_token expired"; return c; }
  c.sub = j["sub"].asString();
  if (c.sub.empty()) { c.error = "no subject"; return c; }
  c.email = j["email"].asString();
  c.name = j["name"].asString();
  c.picture = j["picture"].asString();
  c.emailVerified = j["email_verified"].asBool();
  c.ok = true;
  return c;
}

}  // namespace fai
