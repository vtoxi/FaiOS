// HTTP transport seam. The real implementation (libcurl + TLS, certificate verification ON) is NOT built yet.
// Everything above this interface is tested against scripted fakes, which are labelled as mocks.
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace fai {

struct HttpRequest {
  std::string method = "GET", url, body;
  std::vector<std::pair<std::string, std::string>> headers;
};

struct HttpResponse {
  int status = 0;     // 0 = transport failure (offline, DNS, TLS); see error
  std::string body, error;
};

class IHttp {
 public:
  virtual ~IHttp() = default;
  virtual HttpResponse send(const HttpRequest& req) = 0;
};

class NullHttp : public IHttp {  // used until a real transport exists: every call fails as "offline"
 public:
  HttpResponse send(const HttpRequest&) override { return {0, "", "no network transport available"}; }
};

}  // namespace fai
