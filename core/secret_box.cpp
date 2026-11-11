#include "secret_box.h"

#include <fstream>

#ifdef FAIOS_HAVE_OPENSSL
#include <openssl/evp.h>
#endif

namespace fai {

std::string machineKeyMaterial() {
  std::ifstream f("/etc/machine-id");
  std::string id;
  if (!f || !std::getline(f, id) || id.empty()) return "";
  return "faios-machine-key-v1:" + id;
}

#ifdef FAIOS_HAVE_OPENSSL

namespace {
const char kMagic[4] = {'F', 'S', 'B', '1'};
const size_t kSalt = 16, kIv = 12, kTag = 16, kHeader = 4 + 4 + kSalt + kIv;

bool deriveKey(const std::string& material, const unsigned char* salt, int iters, unsigned char key[32]) {
  return PKCS5_PBKDF2_HMAC(material.data(), int(material.size()), salt, int(kSalt), iters, EVP_sha256(), 32, key) == 1;
}
}  // namespace

bool AesGcmSecretBox::seal(const std::string& plain, std::string& blob) {
  if (!available() || iters_ < 1) return false;
  std::string header(kMagic, 4);
  for (int i = 3; i >= 0; i--) header += char((iters_ >> (i * 8)) & 0xFF);
  unsigned char salt[kSalt], iv[kIv], key[32];
  if (!rng_.bytes(salt, kSalt) || !rng_.bytes(iv, kIv)) return false;
  header.append(reinterpret_cast<char*>(salt), kSalt);
  header.append(reinterpret_cast<char*>(iv), kIv);
  if (!deriveKey(material_, salt, iters_, key)) return false;

  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (!ctx) return false;
  std::string ct(plain.size(), '\0');
  unsigned char tag[kTag];
  int n = 0, total = 0;
  bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
            EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, int(kIv), nullptr) == 1 &&
            EVP_EncryptInit_ex(ctx, nullptr, nullptr, key, iv) == 1 &&
            EVP_EncryptUpdate(ctx, nullptr, &n, reinterpret_cast<const unsigned char*>(header.data()), int(header.size())) == 1;  // AAD
  if (ok && !plain.empty())
    ok = EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(&ct[0]), &n, reinterpret_cast<const unsigned char*>(plain.data()), int(plain.size())) == 1;
  total = n;
  if (ok) ok = EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(&ct[0]) + total, &n) == 1;
  if (ok) ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, int(kTag), tag) == 1;
  EVP_CIPHER_CTX_free(ctx);
  OPENSSL_cleanse(key, sizeof key);
  if (!ok) return false;
  blob = base64Encode(header + ct + std::string(reinterpret_cast<char*>(tag), kTag));
  return true;
}

bool AesGcmSecretBox::open(const std::string& blob, std::string& plain) {
  std::string raw;
  if (!available() || !base64Decode(blob, raw) || raw.size() < kHeader + kTag || raw.compare(0, 4, kMagic, 4) != 0) return false;
  int iters = 0;
  for (int i = 0; i < 4; i++) iters = (iters << 8) | static_cast<unsigned char>(raw[4 + i]);
  if (iters < 1 || iters > 5000000) return false;  // refuse absurd work factors from a tampered header
  const unsigned char* salt = reinterpret_cast<const unsigned char*>(raw.data()) + 8;
  const unsigned char* iv = salt + kSalt;
  size_t ctLen = raw.size() - kHeader - kTag;
  unsigned char key[32];
  if (!deriveKey(material_, salt, iters, key)) return false;

  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (!ctx) return false;
  std::string out(ctLen, '\0');
  int n = 0, total = 0;
  bool ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
            EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, int(kIv), nullptr) == 1 &&
            EVP_DecryptInit_ex(ctx, nullptr, nullptr, key, iv) == 1 &&
            EVP_DecryptUpdate(ctx, nullptr, &n, reinterpret_cast<const unsigned char*>(raw.data()), int(kHeader)) == 1;  // AAD
  if (ok && ctLen)
    ok = EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(&out[0]), &n, reinterpret_cast<const unsigned char*>(raw.data()) + kHeader, int(ctLen)) == 1;
  total = n;
  if (ok) ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, int(kTag), const_cast<char*>(raw.data()) + kHeader + ctLen) == 1;
  if (ok) ok = EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(&out[0]) + total, &n) == 1;  // verifies the tag
  EVP_CIPHER_CTX_free(ctx);
  OPENSSL_cleanse(key, sizeof key);
  if (!ok) return false;
  plain = std::move(out);
  return true;
}

#endif  // FAIOS_HAVE_OPENSSL

}  // namespace fai
