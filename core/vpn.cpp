#include "vpn.h"

#include <algorithm>
#include <cstdlib>

#include "json.h"

namespace fai {

// ---------------------------------------------------------------- store

static const char* kProfilesFile = "vpn/profiles.json";
static std::string secretKey(const std::string& id) { return "vpn/" + id + ".sec"; }

const VpnProfile* VpnStore::find(const std::string& id) const {
  for (auto& p : profiles_) if (p.id == id) return &p;
  return nullptr;
}

static Json cidrArray(const std::vector<Cidr>& v) {
  Json a = Json::makeArray();
  for (auto& c : v) a.push(Json::makeString(formatCidr(c)));
  return a;
}
static Json strArray(const std::vector<std::string>& v) {
  Json a = Json::makeArray();
  for (auto& s : v) a.push(Json::makeString(s));
  return a;
}

bool VpnStore::save() {
  Json arr = Json::makeArray();
  for (auto& p : profiles_) {
    Json o = Json::makeObject();
    o.set("id", Json::makeString(p.id)).set("name", Json::makeString(p.name));
    o.set("auto_reconnect", Json::makeBool(p.autoReconnect)).set("kill_switch", Json::makeBool(p.killSwitch));
    o.set("allow_lan_blocked", Json::makeBool(p.allowLanWhenBlocked));
    o.set("addresses", cidrArray(p.cfg.addresses)).set("dns", strArray(p.cfg.dns)).set("search", strArray(p.cfg.dnsSearch));
    o.set("mtu", Json::makeNumber(p.cfg.mtu)).set("listen_port", Json::makeNumber(p.cfg.listenPort));
    Json peers = Json::makeArray();
    for (auto& q : p.cfg.peers) {
      Json x = Json::makeObject();
      x.set("public_key", Json::makeString(q.publicKey)).set("allowed_ips", cidrArray(q.allowedIps));
      x.set("endpoint_host", Json::makeString(q.endpointHost)).set("endpoint_port", Json::makeNumber(q.endpointPort));
      x.set("keepalive", Json::makeNumber(q.keepalive)).set("has_psk", Json::makeBool(!q.presharedKey.empty()));
      peers.push(x);
    }
    o.set("peers", peers);
    arr.push(o);
  }
  Json root = Json::makeObject();
  root.set("profiles", arr);
  return s_.write(kProfilesFile, root.dump());  // contains no private or preshared keys
}

bool VpnStore::load() {
  profiles_.clear();
  std::string text;
  if (!s_.read(kProfilesFile, text)) return true;
  Json j;
  if (!Json::parse(text, j) || !j.isObject()) { log_.log(Level::Error, "vpn", "profiles.json is corrupt; ignoring it"); return false; }
  for (auto& o : j["profiles"].items()) {
    VpnProfile p;
    p.id = o["id"].asString();
    if (p.id.empty() || !validIfaceName(p.id)) continue;  // ids become file names: only safe characters
    p.name = o["name"].asString();
    p.autoReconnect = o["auto_reconnect"].asBool(true);
    p.killSwitch = o["kill_switch"].asBool();
    p.allowLanWhenBlocked = o["allow_lan_blocked"].asBool();
    for (auto& a : o["addresses"].items()) { Cidr c; if (parseCidr(a.asString(), c)) p.cfg.addresses.push_back(c); }
    for (auto& d : o["dns"].items()) p.cfg.dns.push_back(d.asString());
    for (auto& d : o["search"].items()) p.cfg.dnsSearch.push_back(d.asString());
    p.cfg.mtu = int(o["mtu"].asNumber());
    p.cfg.listenPort = int(o["listen_port"].asNumber());
    for (auto& x : o["peers"].items()) {
      WgPeer q;
      q.publicKey = x["public_key"].asString();
      for (auto& a : x["allowed_ips"].items()) { Cidr c; if (parseCidr(a.asString(), c)) q.allowedIps.push_back(c); }
      q.endpointHost = x["endpoint_host"].asString();
      q.endpointPort = int(x["endpoint_port"].asNumber());
      q.keepalive = int(x["keepalive"].asNumber());
      p.cfg.peers.push_back(std::move(q));
    }
    std::string blob, plain;
    Json sec;
    if (box_.available() && s_.read(secretKey(p.id), blob) && box_.open(blob, plain) && Json::parse(plain, sec) && validWgKey(sec["private"].asString())) {
      p.cfg.privateKey = sec["private"].asString();
      for (size_t i = 0; i < p.cfg.peers.size(); i++) p.cfg.peers[i].presharedKey = sec["psk"].at(i).asString();
      p.secretsOk = true;
    } else {
      p.secretsOk = false;  // key unreadable: the profile stays listed but cannot connect
    }
    profiles_.push_back(std::move(p));
  }
  return true;
}

VpnImportResult VpnStore::import(const std::string& nameIn, const std::string& text) {
  VpnImportResult r;
  if (!box_.available()) { r.error = "no secure storage: refusing to store a VPN private key"; return r; }
  if (profiles_.size() >= 16) { r.error = "too many profiles (limit 16)"; return r; }
  WgParseResult pr = parseWgConfig(text);
  if (!pr.ok) { r.error = describeErrors(pr); return r; }
  uint8_t idb[6];
  if (!rng_.bytes(idb, sizeof idb)) { r.error = "no entropy available"; return r; }

  VpnProfile p;
  p.id = "p" + hex(std::string(reinterpret_cast<char*>(idb), sizeof idb));
  std::string name;
  for (unsigned char c : nameIn) if (c >= 0x20 && c != 0x7f) name += char(c);
  while (!name.empty() && name.front() == ' ') name.erase(name.begin());
  while (!name.empty() && name.back() == ' ') name.pop_back();
  if (name.size() > 40) name.resize(40);
  p.name = name.empty() ? "VPN " + std::to_string(profiles_.size() + 1) : name;
  p.cfg = pr.cfg;

  Json sec = Json::makeObject();
  sec.set("private", Json::makeString(p.cfg.privateKey));
  Json psk = Json::makeArray();
  for (auto& q : p.cfg.peers) psk.push(Json::makeString(q.presharedKey));
  sec.set("psk", psk);
  std::string blob;
  if (!box_.seal(sec.dump(), blob) || !s_.write(secretKey(p.id), blob)) { r.error = "could not store the key securely"; return r; }
  profiles_.push_back(p);
  if (!save()) {  // roll back: never leave an orphaned key behind
    profiles_.pop_back();
    s_.remove(secretKey(p.id));
    r.error = "could not save the profile";
    return r;
  }
  log_.log(Level::Info, "vpn", "imported profile " + p.id);
  r.ok = true;
  r.id = p.id;
  return r;
}

bool VpnStore::remove(const std::string& id) {
  auto it = std::find_if(profiles_.begin(), profiles_.end(), [&](const VpnProfile& p) { return p.id == id; });
  if (it == profiles_.end()) return false;
  s_.remove(secretKey(id));
  profiles_.erase(it);
  save();
  log_.log(Level::Info, "vpn", "removed profile " + id);
  return true;
}

bool VpnStore::update(const VpnProfile& np) {
  for (auto& p : profiles_)
    if (p.id == np.id) {
      p.name = np.name;
      p.autoReconnect = np.autoReconnect;
      p.killSwitch = np.killSwitch;
      p.allowLanWhenBlocked = np.allowLanWhenBlocked;
      return save();
    }
  return false;
}

// ---------------------------------------------------------------- command backend

static std::string firstLine(const std::string& s, size_t max = 100) {
  std::string l = s.substr(0, s.find('\n'));
  return l.size() > max ? l.substr(0, max) : l;
}

bool CommandVpnBackend::runAll(const std::vector<Command>& cmds, std::string& err) {
  for (auto& c : cmds) {
    CommandResult x = r_.run(c);
    if (x.ok() || c.ignoreFailure) continue;
    std::string d = c.argv.size() > 1 ? c.argv[0] + " " + c.argv[1] : c.argv[0];
    err = "'" + d + "' failed (exit " + std::to_string(x.exitCode) + ")";
    // A command that carries a secret on stdin may echo it in diagnostics (wg prints the offending line): report no output.
    if (!c.secretStdin && !x.err.empty()) err += ": " + firstLine(x.err);
    return false;
  }
  return true;
}

bool CommandVpnBackend::setKillSwitch(const KillSwitchSpec* spec, std::string& err) {
  if (!spec) return runAll(killSwitchRemove(), err);
  KillSwitchSpec s = *spec;
  s.iface = iface_;
  auto cmds = killSwitchInstall(s);
  if (cmds.empty()) { err = "invalid kill switch specification"; return false; }
  return runAll(cmds, err);
}

bool CommandVpnBackend::bringUp(const WgConfig& cfg, const std::vector<std::string>& endpointIps, std::string& err) {
  auto cmds = tunnelUpCommands(cfg, iface_, mark_, endpointIps);
  if (cmds.empty()) { err = "invalid interface name"; return false; }
  active_ = cfg;
  if (!runAll(cmds, err)) { tearDown(); return false; }
  return true;
}

void CommandVpnBackend::tearDown() {
  std::string ignored;
  runAll(tunnelDownCommands(active_, iface_, mark_), ignored);
  std::fill(active_.privateKey.begin(), active_.privateKey.end(), '\0');
  active_ = WgConfig();
}

static std::vector<std::vector<std::string>> tabLines(const std::string& out) {
  std::vector<std::vector<std::string>> rows;
  size_t pos = 0;
  while (pos < out.size()) {
    size_t nl = out.find('\n', pos);
    std::string line = out.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
    pos = nl == std::string::npos ? out.size() : nl + 1;
    if (line.empty()) continue;
    std::vector<std::string> cols;
    size_t c = 0;
    for (;;) {
      size_t t = line.find('\t', c);
      cols.push_back(line.substr(c, t == std::string::npos ? std::string::npos : t - c));
      if (t == std::string::npos) break;
      c = t + 1;
    }
    rows.push_back(std::move(cols));
  }
  return rows;
}

bool CommandVpnBackend::stats(TunnelStats& out) {
  Command hs, tr;  // these subcommands print public keys only (never the private key, unlike `wg show dump`)
  hs.argv = {"wg", "show", iface_, "latest-handshakes"};
  tr.argv = {"wg", "show", iface_, "transfer"};
  CommandResult a = r_.run(hs), b = r_.run(tr);
  out = TunnelStats();
  if (!a.ok() || !b.ok()) return true;  // interface not there: up stays false
  out.up = true;
  for (auto& row : tabLines(a.out))
    if (row.size() >= 2) out.lastHandshakeMs = std::max(out.lastHandshakeMs, std::atoll(row[1].c_str()) * 1000);
  for (auto& row : tabLines(b.out))
    if (row.size() >= 3) { out.rxBytes += std::strtoull(row[1].c_str(), nullptr, 10); out.txBytes += std::strtoull(row[2].c_str(), nullptr, 10); }
  return true;
}

bool CommandVpnBackend::routesThroughTunnel(const std::string& probeIp) {
  IpAddr ip;
  if (!parseIp(probeIp, ip)) return false;
  Command c;
  c.argv = {"ip", ip.v6 ? "-6" : "-4", "route", "get", formatIp(ip)};
  CommandResult x = r_.run(c);
  if (!x.ok()) return false;
  std::string want = " dev " + iface_;
  size_t p = x.out.find(want);
  return p != std::string::npos && (p + want.size() == x.out.size() || x.out[p + want.size()] == ' ' || x.out[p + want.size()] == '\n');
}

// ---------------------------------------------------------------- manager

const char* vpnStateName(VpnState s) {
  switch (s) {
    case VpnState::Disconnected: return "Disconnected";
    case VpnState::Connecting: return "Connecting";
    case VpnState::Connected: return "Connected";
    case VpnState::Reconnecting: return "Reconnecting";
    default: return "Error";
  }
}

long long VpnManager::backoffMs(int attempt) {
  int shift = std::min(std::max(attempt - 1, 0), 6);
  return std::min(60000LL, 1000LL << shift);  // 1,2,4,8,16,32,60 s
}

void VpnManager::setState(VpnState s) {
  if (st_.state != s) log_.log(Level::Info, "vpn", std::string("state ") + vpnStateName(st_.state) + " -> " + vpnStateName(s));
  st_.state = s;
  if (s != VpnState::Connected) st_.protectionVerified = false;
}

std::string VpnManager::probeFor(const WgConfig& c) const {
  for (auto& p : c.peers)
    for (auto& a : p.allowedIps) {
      if (!isDefaultRoute(a)) return formatIp(a.ip);
      return a.ip.v6 ? "2001:db8::1" : "192.0.2.1";  // documentation ranges: only used for a routing-table lookup, nothing is sent
    }
  return "";
}

void VpnManager::failure(const std::string& why, bool retriable) {
  if (tunnelUp_) { be_.tearDown(); tunnelUp_ = false; }
  st_.error = why;
  log_.log(Level::Warn, "vpn", "failure: " + why);
  if (retriable && haveProf_ && prof_.autoReconnect && st_.attempt < kMaxAttempts) {
    st_.attempt++;
    st_.nextAttemptMs = now_() + backoffMs(st_.attempt);
    setState(VpnState::Reconnecting);
  } else {
    setState(VpnState::Error);  // a kill switch, if installed, keeps blocking until the user disconnects
  }
}

bool VpnManager::connect(const std::string& id) {
  if (st_.state != VpnState::Disconnected) disconnect();  // switching profile or retrying: start clean
  const VpnProfile* found = store_.find(id);
  haveProf_ = found != nullptr;
  if (found) prof_ = *found;
  st_ = VpnStatus();
  st_.profileId = id;
  st_.simulated = be_.simulated();
  if (!haveProf_) { st_.state = VpnState::Error; st_.error = "no such profile"; return false; }
  if (!prof_.secretsOk) { st_.state = VpnState::Error; st_.error = "profile key is unavailable; import the profile again"; return false; }
  if (!hasEndpoint(prof_.cfg)) { st_.state = VpnState::Error; st_.error = "profile has no server endpoint"; return false; }
  return startTunnel();
}

bool VpnManager::startTunnel() {
  const WgConfig& c = prof_.cfg;
  if (net_.active() == LinkKind::None) { failure("no network connection", true); return false; }

  std::vector<std::string> ips;
  KillSwitchSpec ks;
  ks.allowLan = prof_.allowLanWhenBlocked;
  for (auto& p : c.peers) {
    if (p.endpointHost.empty()) { ips.push_back(""); continue; }
    IpAddr ip;
    if (res_.resolve(p.endpointHost, ip)) resolved_[p.endpointHost] = formatIp(ip);
    else if (resolved_.count(p.endpointHost)) parseIp(resolved_[p.endpointHost], ip);  // DNS may be blocked by our own kill switch: use the last good answer
    else { failure("could not resolve the server name", true); return false; }
    ips.push_back(formatIp(ip));
    ks.endpoints.push_back({ip, p.endpointPort});
  }

  std::string err;
  if (prof_.killSwitch) {  // fail closed: the block goes up BEFORE the tunnel does
    if (!be_.setKillSwitch(&ks, err)) { failure("kill switch could not be installed: " + err, false); return false; }
    st_.trafficBlocked = true;
  }
  if (!be_.bringUp(c, ips, err)) { failure("tunnel setup failed: " + err, true); return false; }
  tunnelUp_ = true;
  deadlineMs_ = now_() + kHandshakeTimeoutMs;
  lastCheckMs_ = 0;
  setState(VpnState::Connecting);
  return true;
}

void VpnManager::disconnect() {
  if (tunnelUp_) { be_.tearDown(); tunnelUp_ = false; }
  if (st_.trafficBlocked || (haveProf_ && prof_.killSwitch)) { std::string e; be_.setKillSwitch(nullptr, e); }
  if (st_.state != VpnState::Disconnected) log_.log(Level::Info, "vpn", "disconnected by user");
  st_ = VpnStatus();
  st_.simulated = be_.simulated();
  haveProf_ = false;
  prof_ = VpnProfile();
}

void VpnManager::onNetworkChange() {
  long long now = now_();
  if (st_.state == VpnState::Connected || st_.state == VpnState::Connecting) {
    failure("network changed", true);
    if (st_.state == VpnState::Reconnecting) { st_.attempt = 1; st_.nextAttemptMs = now; }  // follow the new link right away
  } else if (st_.state == VpnState::Reconnecting) {
    st_.nextAttemptMs = now;
  }
}

std::vector<std::string> VpnManager::dnsServers() const {
  return st_.protectionVerified && haveProf_ ? prof_.cfg.dns : std::vector<std::string>();
}

void VpnManager::tick() {
  long long now = now_();
  if (st_.state == VpnState::Reconnecting) {
    if (now >= st_.nextAttemptMs) startTunnel();  // on failure startTunnel() reschedules via failure()
    return;
  }
  if (st_.state != VpnState::Connecting && st_.state != VpnState::Connected) return;

  TunnelStats ts;
  if (!be_.stats(ts) || !ts.up) { failure("tunnel interface is gone", true); return; }
  st_.rxBytes = ts.rxBytes;
  st_.txBytes = ts.txBytes;
  st_.lastHandshakeMs = ts.lastHandshakeMs;
  bool fresh = ts.lastHandshakeMs > 0 && now - ts.lastHandshakeMs <= kHandshakeStaleMs;
  if (lastCheckMs_ == 0 || now - lastCheckMs_ >= kCheckIntervalMs) {
    routed_ = be_.routesThroughTunnel(probeFor(prof_.cfg));
    lastCheckMs_ = now ? now : 1;
  }
  bool verified = fresh && routed_;

  if (st_.state == VpnState::Connecting) {
    if (verified) {
      setState(VpnState::Connected);
      st_.protectionVerified = true;
      st_.connectedSinceMs = now;
      st_.attempt = 0;
      st_.error.clear();
    } else if (now > deadlineMs_) {
      failure(ts.lastHandshakeMs == 0 ? "no handshake from the server (unreachable, or wrong keys)" : "traffic is not routed through the tunnel", true);
    }
    return;
  }

  // Connected
  if (!routed_) { failure("traffic is not routed through the tunnel", true); return; }
  st_.protectionVerified = fresh;
  if (fresh) { st_.error.clear(); return; }
  st_.error = "no recent handshake";
  bool keepalive = false;
  for (auto& p : prof_.cfg.peers) keepalive |= p.keepalive > 0;
  if (keepalive && now - ts.lastHandshakeMs > kHandshakeStaleMs + 30000) failure("lost contact with the server", true);  // keepalives should have re-keyed
}

}  // namespace fai
