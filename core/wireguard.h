// WireGuard configuration (wg-quick format) parsing and validation.
// Secrets: PrivateKey/PresharedKey are held in the struct only as long as needed. They are NEVER included in
// error messages, summaries or logs. Script hooks (PreUp/PostUp/PreDown/PostDown) are rejected, not executed:
// importing a profile must never be able to run commands.
#pragma once
#include <string>
#include <vector>

#include "ip.h"

namespace fai {

struct WgPeer {
  std::string publicKey, presharedKey;  // base64, 32 bytes
  std::vector<Cidr> allowedIps;
  std::string endpointHost;             // hostname or IP; empty = no endpoint (roaming peer)
  int endpointPort = 0;
  int keepalive = 0;                    // seconds, 0 = off
};

struct WgConfig {
  std::string privateKey;               // base64, 32 bytes. SECRET.
  std::vector<Cidr> addresses;
  std::vector<std::string> dns;         // IP addresses
  std::vector<std::string> dnsSearch;   // search domains
  int mtu = 0, listenPort = 0;
  std::vector<WgPeer> peers;
};

struct WgParseError { int line; std::string message; };
struct WgParseResult {
  bool ok = false;
  WgConfig cfg;
  std::vector<WgParseError> errors;  // line 0 = whole-file problem
};

bool validWgKey(const std::string& base64Key);  // standard base64 of exactly 32 bytes
WgParseResult parseWgConfig(const std::string& text);

bool isFullTunnel(const WgConfig& c);            // some peer routes 0.0.0.0/0 or ::/0
bool hasEndpoint(const WgConfig& c);             // at least one peer can be dialled
std::string describeErrors(const WgParseResult& r);  // "line 4: ..." lines; contains no key material

}  // namespace fai
