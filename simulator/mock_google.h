// SIMULATOR ONLY. A MOCK of Google's OAuth endpoints returning canned, fake responses.
// It is not Google, it never contacts Google, and it is not part of any device image.
// Enabled only by `faios_sim --mock-google`; every account it creates is marked [MOCK] in the UI.
#pragma once
#include <ctime>
#include <string>

#include "crypto.h"
#include "http.h"
#include "json.h"

namespace fai {

class MockGoogleHttp : public IHttp {
 public:
  explicit MockGoogleHttp(std::string clientId) : client_(std::move(clientId)) {}

  HttpResponse send(const HttpRequest& r) override {
    if (r.url.find("/revoke") != std::string::npos) return {200, "", ""};
    if (r.url.find("/token") == std::string::npos) return {404, "{\"error\":\"mock_unknown_endpoint\"}", ""};
    bool firstGrant = r.body.find("grant_type=authorization_code") != std::string::npos;
    Json j = Json::makeObject();
    j.set("access_token", Json::makeString("mock-access-" + std::to_string(++n_)));
    j.set("expires_in", Json::makeNumber(3600));
    j.set("scope", Json::makeString("openid email profile"));
    if (firstGrant) {  // like Google, only the first exchange returns a refresh token and an ID token
      j.set("refresh_token", Json::makeString("mock-refresh"));
      j.set("id_token", Json::makeString(idToken()));
    }
    return {200, j.dump(), ""};
  }

 private:
  std::string idToken() const {
    Json c = Json::makeObject();
    c.set("iss", Json::makeString("https://accounts.google.com")).set("aud", Json::makeString(client_));
    c.set("sub", Json::makeString("mock-0001")).set("email", Json::makeString("mock.user@example.invalid"));
    c.set("email_verified", Json::makeBool(true)).set("name", Json::makeString("Mock User"));
    c.set("exp", Json::makeNumber(double(std::time(nullptr) + 3600)));
    return base64UrlEncode("{\"alg\":\"none\"}") + "." + base64UrlEncode(c.dump()) + ".mock";
  }
  std::string client_;
  int n_ = 0;
};

}  // namespace fai
