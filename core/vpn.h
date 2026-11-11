// FaiOS VPN manager (WireGuard). UI-independent.
//   VpnStore    : persistent profiles. Metadata in "vpn/profiles.json"; private/preshared keys ONLY sealed
//                 (ISecretBox) in "vpn/<id>.sec". No secure storage => import is refused.
//   IVpnBackend : what actually creates the tunnel. CommandVpnBackend (ip/wg/nft commands) is the real one;
//                 simulator/tests supply simulated backends, flagged via simulated().
//   VpnManager  : the state machine. It reports "protected" ONLY after: tunnel up + a WireGuard handshake seen +
//                 the OS routing table sends traffic via the tunnel. Anything less is shown as unverified.
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "network.h"
#include "secret_box.h"
#include "services.h"
#include "wg_commands.h"

namespace fai {

struct VpnProfile {
  std::string id, name;
  WgConfig cfg;               // private/preshared keys are empty when secretsOk is false
  bool secretsOk = true;
  bool autoReconnect = true;
  bool killSwitch = false;
  bool allowLanWhenBlocked = false;
};

struct VpnImportResult { bool ok = false; std::string id, error; };

class VpnStore {
 public:
  VpnStore(IStorage& s, ISecretBox& box, IRandom& rng, Logger& log) : s_(s), box_(box), rng_(rng), log_(log) {}
  bool load();
  VpnImportResult import(const std::string& name, const std::string& configText);
  bool remove(const std::string& id);
  bool update(const VpnProfile& p);  // flags and name only; the tunnel configuration never changes this way
  const std::vector<VpnProfile>& profiles() const { return profiles_; }
  const VpnProfile* find(const std::string& id) const;
 private:
  bool save();
  IStorage& s_;
  ISecretBox& box_;
  IRandom& rng_;
  Logger& log_;
  std::vector<VpnProfile> profiles_;
};

struct TunnelStats {
  bool up = false;               // interface exists
  long long lastHandshakeMs = 0; // epoch ms, 0 = no handshake yet
  unsigned long long rxBytes = 0, txBytes = 0;
};

class IVpnBackend {
 public:
  virtual ~IVpnBackend() = default;
  virtual const char* name() const = 0;
  virtual bool simulated() const = 0;
  virtual bool setKillSwitch(const KillSwitchSpec* spec, std::string& err) = 0;  // nullptr removes it
  virtual bool bringUp(const WgConfig& cfg, const std::vector<std::string>& endpointIps, std::string& err) = 0;
  virtual void tearDown() = 0;
  virtual bool stats(TunnelStats& out) = 0;
  virtual bool routesThroughTunnel(const std::string& probeIp) = 0;
};

// Real backend: runs the commands from wg_commands.h through an ICommandRunner. Needs root and a kernel with
// WireGuard. Command generation is unit-tested, and a real tunnel + kill switch was verified in the generic QEMU image (tools/tunnel_test.cpp). Not verified on the Digit 4G.
class CommandVpnBackend : public IVpnBackend {
 public:
  explicit CommandVpnBackend(ICommandRunner& r, std::string iface = "wg0", int fwmark = kDefaultFwmark) : r_(r), iface_(std::move(iface)), mark_(fwmark) {}
  const char* name() const override { return "wireguard-kernel (ip/wg/nft)"; }
  bool simulated() const override { return false; }
  bool setKillSwitch(const KillSwitchSpec* spec, std::string& err) override;
  bool bringUp(const WgConfig& cfg, const std::vector<std::string>& endpointIps, std::string& err) override;
  void tearDown() override;
  bool stats(TunnelStats& out) override;
  bool routesThroughTunnel(const std::string& probeIp) override;
 private:
  bool runAll(const std::vector<Command>& cmds, std::string& err);
  ICommandRunner& r_;
  std::string iface_;
  int mark_;
  WgConfig active_;  // kept for teardown (which routes/rules to remove); holds the key, never exposed
};

enum class VpnState { Disconnected, Connecting, Connected, Reconnecting, Error };
const char* vpnStateName(VpnState s);

struct VpnStatus {
  VpnState state = VpnState::Disconnected;
  std::string profileId, error;
  bool protectionVerified = false;  // true ONLY when tunnel + handshake + routing were all verified just now
  bool trafficBlocked = false;      // kill switch is dropping non-tunnel traffic
  bool simulated = false;           // backend is a simulation: there is no real protection
  int attempt = 0;
  long long connectedSinceMs = 0, lastHandshakeMs = 0, nextAttemptMs = 0;
  unsigned long long rxBytes = 0, txBytes = 0;
};

class VpnManager {
 public:
  VpnManager(VpnStore& store, IVpnBackend& backend, IResolver& resolver, NetworkManager& net, Logger& log, std::function<long long()> nowMs)
      : store_(store), be_(backend), res_(resolver), net_(net), log_(log), now_(std::move(nowMs)) {}

  bool connect(const std::string& profileId);
  void disconnect();            // user action: tunnel AND kill switch are removed
  void tick();                  // drive the state machine, about once a second
  void onNetworkChange();       // wire to NetworkManager::addListener
  const VpnStatus& status() const { return st_; }
  std::vector<std::string> dnsServers() const;  // the profile's DNS while verified, else empty

  static constexpr long long kHandshakeTimeoutMs = 15000;  // from bring-up to first handshake
  static constexpr long long kHandshakeStaleMs = 180000;   // WireGuard re-keys about every 2 minutes under traffic
  static constexpr long long kCheckIntervalMs = 5000;      // routing check spawns a process: not every tick
  static constexpr int kMaxAttempts = 30;

 private:
  bool startTunnel();                              // true if the tunnel is now in Connecting
  void failure(const std::string& why, bool retriable);
  void setState(VpnState s);
  static long long backoffMs(int attempt);
  std::string probeFor(const WgConfig& c) const;

  VpnStore& store_;
  IVpnBackend& be_;
  IResolver& res_;
  NetworkManager& net_;
  Logger& log_;
  std::function<long long()> now_;
  VpnStatus st_;
  VpnProfile prof_;          // a copy: the store may reallocate when profiles are imported
  bool haveProf_ = false;
  long long deadlineMs_ = 0, lastCheckMs_ = 0;
  bool tunnelUp_ = false, routed_ = false;
  std::map<std::string, std::string> resolved_;  // last good DNS answer per endpoint host
};

}  // namespace fai
