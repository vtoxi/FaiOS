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
| Encrypted token storage | Code written; AES-GCM tests pending OpenSSL dev files. Key protection is weak without a user PIN (docs/SECURITY.md). |
| Browser, YouTube, VPN | Not started. Engine choice depends on the phone's unknown RAM/CPU. |
