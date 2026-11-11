#include "wireguard.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "crypto.h"

namespace fai {

bool validWgKey(const std::string& k) {
  std::string raw;
  return k.size() == 44 && k[43] == '=' && base64Decode(k, raw) && raw.size() == 32;
}

static std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
static std::string lower(std::string s) { for (char& c : s) c = char(std::tolower((unsigned char)c)); return s; }

static std::vector<std::string> splitList(const std::string& v) {
  std::vector<std::string> out;
  std::stringstream ss(v);
  for (std::string item; std::getline(ss, item, ',');) { item = trim(item); if (!item.empty()) out.push_back(item); }
  return out;
}

static bool parseInt(const std::string& s, int lo, int hi, int& out) {
  if (s.empty() || s.size() > 6) return false;
  int v = 0;
  for (char c : s) { if (c < '0' || c > '9') return false; v = v * 10 + (c - '0'); }
  if (v < lo || v > hi) return false;
  out = v;
  return true;
}

WgParseResult parseWgConfig(const std::string& text) {
  WgParseResult r;
  auto err = [&](int line, const std::string& m) { r.errors.push_back({line, m}); };
  if (text.size() > 64 * 1024) { err(0, "file is too large (limit 64 KB)"); return r; }

  enum { None, Iface, Peer } section = None;
  bool sawInterface = false, hasKey = false;
  int ifaceLine = 0;
  WgPeer* peer = nullptr;
  std::vector<int> peerLines;
  std::stringstream in(text);
  int n = 0;
  for (std::string raw; std::getline(in, raw);) {
    n++;
    size_t hash = raw.find('#');
    std::string line = trim(hash == std::string::npos ? raw : raw.substr(0, hash));
    if (line.empty()) continue;
    if (line[0] == '[') {
      std::string name = lower(line);
      if (name == "[interface]") {
        if (sawInterface) err(n, "more than one [Interface] section");
        sawInterface = true; section = Iface; ifaceLine = n; peer = nullptr;
      } else if (name == "[peer]") {
        if (r.cfg.peers.size() >= 32) { err(n, "too many peers (limit 32)"); return r; }
        r.cfg.peers.push_back(WgPeer());
        peer = &r.cfg.peers.back();
        peerLines.push_back(n);
        section = Peer;
      } else { err(n, "unknown section"); section = None; }
      continue;
    }
    size_t eq = line.find('=');
    if (eq == std::string::npos) { err(n, "expected Key = Value"); continue; }
    std::string key = lower(trim(line.substr(0, eq))), val = trim(line.substr(eq + 1));
    if (section == None) { err(n, "setting outside a section"); continue; }

    if (section == Iface) {
      if (key == "privatekey") {
        if (!validWgKey(val)) err(n, "PrivateKey is not a valid 32-byte base64 key");  // value deliberately not echoed
        else { r.cfg.privateKey = val; hasKey = true; }
      } else if (key == "address") {
        for (auto& a : splitList(val)) {
          Cidr c;
          if (!parseCidr(a, c)) err(n, "invalid Address entry");
          else if (r.cfg.addresses.size() >= 8) err(n, "too many addresses");
          else r.cfg.addresses.push_back(c);
        }
      } else if (key == "dns") {
        for (auto& d : splitList(val)) {
          IpAddr ip;
          if (parseIp(d, ip)) r.cfg.dns.push_back(formatIp(ip));
          else if (validHostname(d)) r.cfg.dnsSearch.push_back(d);
          else err(n, "invalid DNS entry");
        }
        if (r.cfg.dns.size() > 8) err(n, "too many DNS servers");
      } else if (key == "mtu") {
        if (!parseInt(val, 576, 9000, r.cfg.mtu)) err(n, "MTU must be 576-9000");
      } else if (key == "listenport") {
        if (!parseInt(val, 1, 65535, r.cfg.listenPort)) err(n, "ListenPort must be 1-65535");
      } else if (key == "preup" || key == "postup" || key == "predown" || key == "postdown") {
        err(n, "script hooks (" + trim(line.substr(0, eq)) + ") are not supported and are never run");
      } else if (key == "table" || key == "saveconfig" || key == "fwmark") {
        err(n, "setting '" + trim(line.substr(0, eq)) + "' is not supported");
      } else err(n, "unknown Interface setting '" + trim(line.substr(0, eq)) + "'");
    } else {
      if (key == "publickey") {
        if (!validWgKey(val)) err(n, "PublicKey is not a valid 32-byte base64 key");
        else peer->publicKey = val;
      } else if (key == "presharedkey") {
        if (!validWgKey(val)) err(n, "PresharedKey is not a valid 32-byte base64 key");
        else peer->presharedKey = val;
      } else if (key == "allowedips") {
        for (auto& a : splitList(val)) {
          Cidr c;
          if (!parseCidr(a, c)) err(n, "invalid AllowedIPs entry");
          else if (peer->allowedIps.size() >= 256) err(n, "too many AllowedIPs");
          else peer->allowedIps.push_back(c);
        }
      } else if (key == "endpoint") {
        if (!parseEndpoint(val, peer->endpointHost, peer->endpointPort)) err(n, "invalid Endpoint (use host:port or [v6]:port)");
      } else if (key == "persistentkeepalive") {
        if (val == "off") peer->keepalive = 0;
        else if (!parseInt(val, 0, 65535, peer->keepalive)) err(n, "PersistentKeepalive must be 0-65535 or off");
      } else err(n, "unknown Peer setting '" + trim(line.substr(0, eq)) + "'");
    }
  }

  if (!sawInterface) err(0, "missing [Interface] section");
  else {
    if (!hasKey) err(ifaceLine, "[Interface] has no (valid) PrivateKey");
    if (r.cfg.addresses.empty()) err(ifaceLine, "[Interface] has no Address");
  }
  if (r.cfg.peers.empty()) err(0, "no [Peer] section");
  for (size_t i = 0; i < r.cfg.peers.size(); i++) {
    if (r.cfg.peers[i].publicKey.empty()) err(peerLines[i], "[Peer] has no (valid) PublicKey");
    if (r.cfg.peers[i].allowedIps.empty()) err(peerLines[i], "[Peer] has no AllowedIPs");
  }
  r.ok = r.errors.empty();
  if (!r.ok) r.cfg = WgConfig();  // never hand a half-valid config (or its key) to callers
  return r;
}

bool isFullTunnel(const WgConfig& c) {
  for (auto& p : c.peers) for (auto& a : p.allowedIps) if (isDefaultRoute(a)) return true;
  return false;
}

bool hasEndpoint(const WgConfig& c) {
  for (auto& p : c.peers) if (!p.endpointHost.empty()) return true;
  return false;
}

std::string describeErrors(const WgParseResult& r) {
  std::string o;
  for (auto& e : r.errors) o += (o.empty() ? "" : "\n") + (e.line ? "line " + std::to_string(e.line) + ": " : std::string()) + e.message;
  return o;
}

}  // namespace fai
