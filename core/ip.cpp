#include "ip.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace fai {

bool IpAddr::operator==(const IpAddr& o) const { return v6 == o.v6 && std::memcmp(b, o.b, v6 ? 16 : 4) == 0; }

static bool parseV4(const std::string& s, IpAddr& out) {
  IpAddr a;
  int part = 0, digits = 0, val = 0;
  for (size_t i = 0; i <= s.size(); i++) {
    char c = i < s.size() ? s[i] : '.';
    if (c >= '0' && c <= '9') {
      if (digits == 1 && val == 0) return false;  // leading zero ("01"): ambiguous (octal?), reject
      val = val * 10 + (c - '0');
      if (++digits > 3 || val > 255) return false;
    } else if (c == '.') {
      if (digits == 0 || part > 3) return false;
      a.b[part++] = uint8_t(val);
      val = digits = 0;
      if (i == s.size()) break;
    } else return false;
  }
  if (part != 4) return false;
  out = a;
  return true;
}

static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Colon-separated groups of 1-4 hex digits. Empty text = zero groups.
static bool groups(const std::string& s, std::vector<uint16_t>& out) {
  if (s.empty()) return true;
  size_t i = 0;
  for (;;) {
    size_t j = s.find(':', i);
    std::string g = s.substr(i, j == std::string::npos ? std::string::npos : j - i);
    if (g.empty() || g.size() > 4) return false;
    int v = 0;
    for (char c : g) { int h = hexVal(c); if (h < 0) return false; v = v * 16 + h; }
    out.push_back(uint16_t(v));
    if (j == std::string::npos) return true;
    i = j + 1;
  }
}

static bool parseV6(const std::string& s, IpAddr& out) {
  if (s.empty() || s.find('.') != std::string::npos || s.find('%') != std::string::npos) return false;  // no embedded v4, no zone ids
  std::vector<uint16_t> head, tail;
  size_t dc = s.find("::");
  if (dc == std::string::npos) {
    if (!groups(s, head) || head.size() != 8) return false;
  } else {
    if (s.find("::", dc + 1) != std::string::npos) return false;  // a second "::" (this also rejects ":::")
    if (!groups(s.substr(0, dc), head) || !groups(s.substr(dc + 2), tail) || head.size() + tail.size() > 7) return false;
  }
  std::vector<uint16_t> all = head;
  all.insert(all.end(), 8 - head.size() - tail.size(), 0);
  all.insert(all.end(), tail.begin(), tail.end());
  IpAddr a;
  a.v6 = true;
  for (int k = 0; k < 8; k++) { a.b[k * 2] = uint8_t(all[k] >> 8); a.b[k * 2 + 1] = uint8_t(all[k] & 0xFF); }
  out = a;
  return true;
}

bool parseIp(const std::string& s, IpAddr& out) { return s.find(':') != std::string::npos ? parseV6(s, out) : parseV4(s, out); }

std::string formatIp(const IpAddr& a) {
  char buf[48];
  if (!a.v6) { std::snprintf(buf, sizeof buf, "%u.%u.%u.%u", a.b[0], a.b[1], a.b[2], a.b[3]); return buf; }
  uint16_t g[8];
  for (int i = 0; i < 8; i++) g[i] = uint16_t(a.b[i * 2] << 8 | a.b[i * 2 + 1]);
  int bestStart = -1, bestLen = 0;
  for (int i = 0; i < 8;) {  // longest run of zero groups (length >= 2); first wins on ties
    if (g[i] != 0) { i++; continue; }
    int j = i;
    while (j < 8 && g[j] == 0) j++;
    if (j - i > bestLen) { bestLen = j - i; bestStart = i; }
    i = j;
  }
  if (bestLen < 2) bestStart = -1;
  std::string o;
  for (int i = 0; i < 8; i++) {
    if (i == bestStart) { o += "::"; i += bestLen - 1; continue; }
    if (!o.empty() && o.back() != ':') o += ':';
    std::snprintf(buf, sizeof buf, "%x", g[i]);
    o += buf;
  }
  return o;
}

bool parseCidr(const std::string& s, Cidr& out) {
  size_t slash = s.find('/');
  IpAddr ip;
  if (!parseIp(s.substr(0, slash), ip)) return false;
  int max = ip.v6 ? 128 : 32, prefix = max;
  if (slash != std::string::npos) {
    std::string p = s.substr(slash + 1);
    if (p.empty() || p.size() > 3) return false;
    prefix = 0;
    for (char c : p) { if (c < '0' || c > '9') return false; prefix = prefix * 10 + (c - '0'); }
    if (prefix > max) return false;
  }
  out.ip = ip;
  out.prefix = prefix;
  return true;
}

std::string formatCidr(const Cidr& c) { return formatIp(c.ip) + "/" + std::to_string(c.prefix); }

bool validHostname(const std::string& h) {
  if (h.empty() || h.size() > 253) return false;
  size_t start = 0;
  for (size_t i = 0; i <= h.size(); i++) {
    if (i == h.size() || h[i] == '.') {
      size_t len = i - start;
      if (len == 0 || len > 63 || h[start] == '-' || h[i - 1] == '-') return false;
      start = i + 1;
    } else if (!((h[i] >= 'a' && h[i] <= 'z') || (h[i] >= 'A' && h[i] <= 'Z') || (h[i] >= '0' && h[i] <= '9') || h[i] == '-')) return false;
  }
  return true;
}

bool validIfaceName(const std::string& n) {
  if (n.empty() || n.size() > 15) return false;
  for (char c : n)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
  return true;
}

bool parseEndpoint(const std::string& s, std::string& host, int& port) {
  std::string h, p;
  if (!s.empty() && s[0] == '[') {
    size_t close = s.find(']');
    if (close == std::string::npos || close + 1 >= s.size() || s[close + 1] != ':') return false;
    h = s.substr(1, close - 1);
    p = s.substr(close + 2);
    IpAddr a;
    if (!parseIp(h, a) || !a.v6) return false;
  } else {
    size_t colon = s.rfind(':');
    if (colon == std::string::npos || s.find(':') != colon) return false;  // bare IPv6 without brackets is ambiguous
    h = s.substr(0, colon);
    p = s.substr(colon + 1);
    IpAddr a;
    bool numeric = !h.empty() && h.find_first_not_of("0123456789.") == std::string::npos;
    if (numeric ? !parseIp(h, a) : !validHostname(h)) return false;
  }
  if (p.empty() || p.size() > 5) return false;
  int v = 0;
  for (char c : p) { if (c < '0' || c > '9') return false; v = v * 10 + (c - '0'); }
  if (v < 1 || v > 65535) return false;
  host = h;
  port = v;
  return true;
}

}  // namespace fai
