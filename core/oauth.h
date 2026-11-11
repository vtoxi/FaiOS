// Google OAuth 2.0 / OpenID Connect: Authorization Code flow with PKCE, token refresh, revocation.
// No passwords are ever requested. The client ID (and secret, if the client type has one) come from
// configuration, never from source code. Tokens must never be logged.
#pragma once
#include <string>
#include <vector>

#include "crypto.h"
#include "http.h"

namespace fai {

struct OAuthClient {
  std::string clientId;
  std::string clientSecret;  // empty if none. Google "desktop" clients issue one, but it is not confidential.
  std::string redirectUri;   // e.g. http://127.0.0.1:PORT/ (loopback). Never a webview-intercepted URL.
  std::string authUrl = "https://accounts.google.com/o/oauth2/v2/auth";
  std::string tokenUrl = "https://oauth2.googleapis.com/token";
  std::string revokeUrl = "https://oauth2.googleapis.com/revoke";
};

// Least-privilege scopes. Sign-in only asks for the first three; service scopes are requested later,
// incrementally, when the matching sync feature actually exists.
namespace scope {
inline const char* const kOpenId = "openid";
inline const char* const kEmail = "email";
inline const char* const kProfile = "profile";
inline const char* const kContactsRead = "https://www.googleapis.com/auth/contacts.readonly";
inline const char* const kContacts = "https://www.googleapis.com/auth/contacts";
inline const char* const kCalendarRead = "https://www.googleapis.com/auth/calendar.readonly";
inline const char* const kCalendarEvents = "https://www.googleapis.com/auth/calendar.events";
}  // namespace scope

struct Pkce { std::string verifier, challenge; };
bool makePkce(IRandom& rng, Pkce& out);  // false if no entropy

struct AuthRequest { std::string url, state; Pkce pkce; };
bool buildAuthRequest(const OAuthClient& c, const std::vector<std::string>& scopes, IRandom& rng, const std::string& loginHint, AuthRequest& out);

enum class RedirectResult { Code, StateMismatch, Denied, Malformed };
RedirectResult parseRedirect(const std::string& redirectedUrl, const std::string& expectedState, std::string& codeOut);

struct TokenSet {
  std::string access, refresh, idToken;
  long long expiresAtMs = 0;
  std::vector<std::string> scopes;
};

enum class AuthError { None, Network, InvalidGrant, InvalidClient, Server, Parse };
struct TokenResult {
  bool ok = false;
  TokenSet tokens;
  AuthError err = AuthError::None;
  std::string detail;  // short error code, never a token
};

TokenResult exchangeCode(IHttp& http, const OAuthClient& c, const std::string& code, const std::string& verifier, long long nowMs);
TokenResult refreshTokens(IHttp& http, const OAuthClient& c, const std::string& refreshToken, long long nowMs);
bool revokeToken(IHttp& http, const OAuthClient& c, const std::string& token, std::string* detail = nullptr);

struct IdClaims { bool ok = false; std::string sub, email, name, picture, error; bool emailVerified = false; };
// Reads the claims of an ID token received directly from Google's token endpoint over TLS.
// Checks iss, aud and exp. The JWT signature is NOT verified (allowed by OIDC Core 3.1.3.7 for this case).
IdClaims parseIdToken(const std::string& jwt, const std::string& expectedClientId, long long nowMs);

}  // namespace fai
