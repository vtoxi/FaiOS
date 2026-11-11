// Non-secret helpers: SHA-256 (for the PKCE challenge), base64/base64url, percent-encoding, random bytes.
// NOT used to protect stored secrets. At-rest encryption uses a vetted library (see secret_box.h).
#pragma once
#include <cstdint>
#include <string>

namespace fai {

std::string sha256(const std::string& data);  // 32 raw bytes
std::string hex(const std::string& bytes);

std::string base64Encode(const std::string& bytes);       // standard alphabet, padded
bool base64Decode(const std::string& text, std::string& out);
std::string base64UrlEncode(const std::string& bytes);    // RFC 4648 section 5, no padding
bool base64UrlDecode(const std::string& text, std::string& out);

std::string percentEncode(const std::string& s);          // RFC 3986 unreserved kept
std::string percentDecode(const std::string& s);          // also maps '+' to space (form encoding)

class IRandom {
 public:
  virtual ~IRandom() = default;
  virtual bool bytes(uint8_t* out, size_t n) = 0;         // false = no entropy: callers must fail closed
};

class SystemRandom : public IRandom {  // /dev/urandom
 public:
  bool bytes(uint8_t* out, size_t n) override;
};

}  // namespace fai
