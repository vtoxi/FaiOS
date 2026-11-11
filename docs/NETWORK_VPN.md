# Network manager and VPN

Status words used here: **implemented + tested** (unit tests, fakes only), **simulated** (UI works, nothing real behind it), **not built**, **unverified** (written, never run against the real thing).

## What exists

| Piece | Status |
|---|---|
| IP / CIDR / endpoint parsing (strict; leading zeros, bad IPv6, bare IPv6 endpoints all rejected) | implemented + tested |
| WireGuard (`wg-quick` format) parser. Validates keys, addresses, DNS, ports, MTU, limits. **Script hooks (PreUp/PostUp/PreDown/PostDown) are rejected and never run**; `Table`, `SaveConfig`, `FwMark` refused. Errors never echo key material | implemented + tested |
| Tunnel command generator (`ip link add ... type wireguard`, `wg setconf` on stdin, addresses, MTU, full-tunnel policy routing with fwmark + suppress_prefixlength like wg-quick, split-tunnel routes, teardown) | implemented + tested (assertions on the exact command list) |
| Secret handling in commands: private key only on stdin of `wg setconf`; never in argv, `describe()`, logs or error text (a tool that echoes the bad line is not reported) | implemented + tested |
| Kill switch: nftables ruleset, default-drop in both directions; allows only loopback, the tunnel interface, DHCP, the VPN server ip:port, optionally LAN. Installed **before** the tunnel; stays up through reconnects and errors; lifted only by an explicit user disconnect. Interface names are validated (no injection) | ruleset + state machine implemented + tested. **Not run against a real firewall** |
| DNS policy: verified VPN with DNS -> only the VPN's servers; else user override; else the link's own. `resolv.conf` writer filters non-IP entries (no newline injection) | implemented + tested |
| `NetworkManager`: picks the active link (Wi-Fi/Cellular, preference, Wi-Fi-only), change events | implemented + tested; link state is **simulated** |
| `ConnectivityMonitor` (204 probe: online / offline / captive portal). No default URL, so nothing is contacted until the owner configures one | implemented + tested with a fake transport |
| VPN profile store: metadata in `vpn/profiles.json`, keys only sealed (`vpn/<id>.sec`); import refused when no secure storage | implemented + tested |
| `VpnManager` state machine: Disconnected / Connecting / Connected / Reconnecting / Error. **"Protected" only after tunnel up + a handshake seen (<=180 s old) + the routing table sends traffic via the tunnel** (re-checked every 5 s). Handshake timeout 15 s, backoff 1-2-4-8-16-32-60 s (cap), 30 attempts then Error, reconnect on network change, uses last good endpoint address if our own kill switch blocks DNS | implemented + tested with a fake backend |
| `CommandVpnBackend` (runs the commands through `PosixRunner`, no shell) | implemented; command construction, failure handling and output parsing tested with a fake runner. **No real tunnel has ever been created with it** |
| Settings UI: Connectivity -> VPN, profile options (kill switch, reconnect, LAN), import (`import/*.conf`; the source file is deleted after a successful import so the key is not left unencrypted), delete (also deletes the key) | implemented + tested; simulator backend is **simulated** |
| Simulator backend (`--vpn-demo` adds an obviously fake demo profile) | **simulated**: no tunnel, no encryption, kill switch blocks nothing. UI shows "Simulated tunnel" and `sVPN`, never "Protected" |

## What is NOT built
- A real tunnel test. Needs root, a kernel with WireGuard, `wireguard-tools`, `iproute2`, `nftables`. Plan: the Buildroot QEMU image (root available) with two network namespaces and a WireGuard pair, asserting actual routing, the handshake and a blocked leak with the kill switch. Until that passes, none of the "real" paths above are verified.
- A real link provider (netlink / modem HAL), real DNS file writing, reading the OS routing table beyond `ip route get`.
- Userspace WireGuard, OpenVPN backend, per-app VPN, IPv6-only networks beyond what the command generator covers, split DNS, captive-portal handling in the UI.
- Credential storage for username/password VPNs (WireGuard uses keys only).

## Kernel WireGuard vs userspace (decision pending hardware)
- **Kernel WireGuard** (in mainline Linux since 5.6): fastest, smallest footprint, what `CommandVpnBackend` drives. Needs the phone's kernel to have it. **The phone's kernel version is unknown** (HARDWARE_DISCOVERY.md). A kernel older than 5.6 would need an out-of-tree module built for that exact kernel, which depends on getting vendor kernel sources and a working build.
- **Userspace** (wireguard-go, BoringTun): works without a kernel module via a TUN device, at the cost of RAM/CPU on a small device. Not measured here; no numbers are claimed.
- **Rule:** the `IVpnBackend` interface keeps both options open. Choose after the SoC, kernel and RAM are verified.

## OpenVPN (future optional backend)
Would implement `IVpnBackend` the same way (spawn `openvpn` with a generated config, same state machine, same verification). Larger (TLS stack), username/password and certificates add credential storage requirements. Deferred until WireGuard is proven on a real tunnel.

## Honesty rules enforced in code
1. `protectionVerified` is true only for a just-verified tunnel; the UI says "Protected" only if the backend is not simulated.
2. No key material in logs, errors, argv or plain files (tests scan for it).
3. No scripts from imported profiles are ever executed.
4. A kill switch that cannot be installed means the tunnel is **not** started.
