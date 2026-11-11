// Encryption of secrets (OAuth refresh tokens) at rest.
// Policy: if no working backend exists the box is "unavailable" and callers must REFUSE to store the secret.
// There is deliberately no plaintext fallback.
//
// LIMITATION (document, do not hide): there is no hardware keystore known for the target phone. The key is
// derived from key material that the caller supplies (user PIN, or a device identifier). Material that lives on
// the same flash as the ciphertext only protects against casual file copying, not against someone who can
// read the whole device. A user PIN gives real protection; the machine-id provider does not.
#pragma once
#include <string>

#include "crypto.h"

namespace fai {

class ISecretBox {
 public:
  virtual ~ISecretBox() = default;
  virtual const char* name() const = 0;
  virtual bool available() const = 0;
  virtual bool seal(const std::string& plain, std::string& blob) = 0;  // blob is text-safe (base64)
  virtual bool open(const std::string& blob, std::string& plain) = 0;  // false on tamper or wrong key
};

class NoSecretBox : public ISecretBox {  // fails closed
 public:
  const char* name() const override { return "none"; }
  bool available() const override { return false; }
  bool seal(const std::string&, std::string&) override { return false; }
  bool open(const std::string&, std::string&) override { return false; }
};

std::string machineKeyMaterial();  // /etc/machine-id based; weak (see LIMITATION). Empty if unreadable.

#ifdef FAIOS_HAVE_OPENSSL
// AES-256-GCM (OpenSSL EVP) with a PBKDF2-HMAC-SHA256 derived key and a random salt and IV per blob.
class AesGcmSecretBox : public ISecretBox {
 public:
  AesGcmSecretBox(std::string keyMaterial, IRandom& rng, int pbkdf2Iterations = 200000)
      : material_(std::move(keyMaterial)), rng_(rng), iters_(pbkdf2Iterations) {}
  const char* name() const override { return "aes-256-gcm"; }
  bool available() const override { return !material_.empty(); }
  bool seal(const std::string& plain, std::string& blob) override;
  bool open(const std::string& blob, std::string& plain) override;
 private:
  std::string material_;
  IRandom& rng_;
  int iters_;
};
#endif

}  // namespace fai
