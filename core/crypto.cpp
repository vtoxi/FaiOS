#include "crypto.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace fai {

namespace {
const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
    0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
    0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
    0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
    0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void block(uint32_t h[8], const uint8_t* p) {
  uint32_t w[64];
  for (int i = 0; i < 16; i++) w[i] = uint32_t(p[i * 4]) << 24 | uint32_t(p[i * 4 + 1]) << 16 | uint32_t(p[i * 4 + 2]) << 8 | p[i * 4 + 3];
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
  for (int i = 0; i < 64; i++) {
    uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25), ch = (e & f) ^ (~e & g);
    uint32_t t1 = hh + S1 + ch + K[i] + w[i];
    uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22), mj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t t2 = S0 + mj;
    hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
  }
  h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}
}  // namespace

std::string sha256(const std::string& data) {
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  std::string m = data;
  uint64_t bits = uint64_t(data.size()) * 8;
  m += char(0x80);
  while (m.size() % 64 != 56) m += char(0);
  for (int i = 7; i >= 0; i--) m += char((bits >> (i * 8)) & 0xFF);
  for (size_t i = 0; i < m.size(); i += 64) block(h, reinterpret_cast<const uint8_t*>(m.data()) + i);
  std::string out(32, '\0');
  for (int i = 0; i < 8; i++) for (int j = 0; j < 4; j++) out[i * 4 + j] = char((h[i] >> (24 - j * 8)) & 0xFF);
  return out;
}

std::string hex(const std::string& bytes) {
  static const char* d = "0123456789abcdef";
  std::string o;
  for (unsigned char c : bytes) { o += d[c >> 4]; o += d[c & 15]; }
  return o;
}

static const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const std::string& in) {
  std::string o;
  size_t i = 0;
  for (; i + 2 < in.size(); i += 3) {
    uint32_t v = uint8_t(in[i]) << 16 | uint8_t(in[i + 1]) << 8 | uint8_t(in[i + 2]);
    o += kB64[v >> 18]; o += kB64[(v >> 12) & 63]; o += kB64[(v >> 6) & 63]; o += kB64[v & 63];
  }
  if (i + 1 == in.size()) {
    uint32_t v = uint8_t(in[i]) << 16;
    o += kB64[v >> 18]; o += kB64[(v >> 12) & 63]; o += "==";
  } else if (i + 2 == in.size()) {
    uint32_t v = uint8_t(in[i]) << 16 | uint8_t(in[i + 1]) << 8;
    o += kB64[v >> 18]; o += kB64[(v >> 12) & 63]; o += kB64[(v >> 6) & 63]; o += '=';
  }
  return o;
}

bool base64Decode(const std::string& in, std::string& out) {
  std::string o;
  uint32_t acc = 0;
  int bits = 0, pad = 0;
  for (char c : in) {
    if (c == '=') { pad++; continue; }
    if (pad) return false;  // data after padding
    const char* p = std::strchr(kB64, c);
    if (!p || c == '\0') return false;
    acc = acc << 6 | uint32_t(p - kB64);
    bits += 6;
    if (bits >= 8) { bits -= 8; o += char((acc >> bits) & 0xFF); }
  }
  if (pad > 2) return false;
  out = std::move(o);
  return true;
}

std::string base64UrlEncode(const std::string& in) {
  std::string o = base64Encode(in);
  for (char& c : o) { if (c == '+') c = '-'; else if (c == '/') c = '_'; }
  while (!o.empty() && o.back() == '=') o.pop_back();
  return o;
}

bool base64UrlDecode(const std::string& in, std::string& out) {
  std::string s = in;
  for (char& c : s) { if (c == '-') c = '+'; else if (c == '_') c = '/'; }
  while (s.size() % 4) s += '=';
  return base64Decode(s, out);
}

std::string percentEncode(const std::string& s) {
  std::string o;
  for (unsigned char c : s) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~') o += char(c);
    else { char b[4]; std::snprintf(b, sizeof b, "%%%02X", c); o += b; }
  }
  return o;
}

std::string percentDecode(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '+') o += ' ';
    else if (s[i] == '%' && i + 2 < s.size() + 0 && std::isxdigit((unsigned char)s[i + 1]) && std::isxdigit((unsigned char)s[i + 2])) {
      o += char(std::stoi(s.substr(i + 1, 2), nullptr, 16));
      i += 2;
    } else o += s[i];
  }
  return o;
}

bool SystemRandom::bytes(uint8_t* out, size_t n) {
  FILE* f = std::fopen("/dev/urandom", "rb");
  if (!f) return false;
  bool ok = std::fread(out, 1, n, f) == n;
  std::fclose(f);
  return ok;
}

}  // namespace fai
