// IP address, CIDR and endpoint parsing. Strict: anything ambiguous is rejected, never guessed.
#pragma once
#include <cstdint>
#include <string>

namespace fai {

struct IpAddr {
  bool v6 = false;
  uint8_t b[16] = {0};  // IPv4 uses b[0..3]
  bool operator==(const IpAddr& o) const;
};

bool parseIp(const std::string& s, IpAddr& out);  // dotted-quad (no leading zeros) or IPv6 (with "::", no embedded IPv4)
std::string formatIp(const IpAddr& a);            // IPv4 dotted, IPv6 per RFC 5952 (lowercase, longest zero run compressed)

struct Cidr {
  IpAddr ip;
  int prefix = 0;
};

bool parseCidr(const std::string& s, Cidr& out);  // "10.0.0.2/32", "::/0"; a bare address means /32 or /128
std::string formatCidr(const Cidr& c);
inline bool isDefaultRoute(const Cidr& c) { return c.prefix == 0; }

// "host:port", "1.2.3.4:51820" or "[2001:db8::1]:51820". Host may be a hostname or an IP.
bool parseEndpoint(const std::string& s, std::string& host, int& port);
bool validHostname(const std::string& h);  // RFC 1123 labels, no underscores, no leading/trailing hyphen
bool validIfaceName(const std::string& n);  // 1..15 chars of [A-Za-z0-9_-]: safe to place in commands and rulesets

}  // namespace fai
