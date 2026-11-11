# Compatibility

| Target | Status |
|---|---|
| Desktop simulator (WSL2/Ubuntu) | Builds, unit tests pass; window verified by user |
| Jazz Digit 4G | **Not established.** Hardware unknown; bootloader/flash path unverified. No claim made. |
| Generic QEMU image | Not started |

| Feature | Status |
|---|---|
| Google sign-in (real) | **Not available**: no HTTP transport, no browser, no OAuth client configured. Tested only against a mock. |
| Contacts / Calendar sync with Google | Not built. Google's device flow cannot grant these scopes; needs external-browser sign-in. |
| Encrypted token storage | AES-256-GCM verified in the dev environment (OpenSSL). Key protection is weak without a user PIN (docs/SECURITY.md). |
| Browser, YouTube, VPN | Not started. Engine choice depends on the phone's unknown RAM/CPU. |

| VPN (WireGuard) | Logic and command generation tested with fakes. **No real tunnel created yet.** Phone kernel version unknown; kernel WireGuard needs 5.6+ or an out-of-tree module. Simulator shows a clearly labelled simulated tunnel. |
| Kill switch | nftables ruleset generated and tested as text; never applied to a real firewall yet. |
