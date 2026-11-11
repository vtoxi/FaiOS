# Architecture

```
core/       Toolkit-independent. Screen model, AppManager (screen stack + key routing), screens, simulated Device state.
simulator/  LVGL + SDL2 renderer; maps SDL keys to core Keys. Only place that includes LVGL/SDL.
tests/      ctest, links core only.
```
- `Screen::view()` returns a `View` (title, lines, selection, softkeys); the renderer only draws it. A different renderer (framebuffer on device) can reuse core unchanged.
- `AppManager` = lifecycle: push/pop/goHome; unconsumed Back/Soft2 pops; End goes home.
- `Device` holds **simulated** data; it is the seam where real HAL providers plug in later.

Deliberately not built yet (no second implementation exists): HAL interfaces, service layer, IPC, SQLite, Buildroot. The spec's full directory tree is not scaffolded; folders are added with the code that fills them.

## Services (core/services.h)
Logger, IStorage (FileStorage/MemStorage), Config, Notifications, IPower (SimPower), Service/ServiceManager, SystemStatus. `AppManager` owns logger, notifications and power, and holds an optional `Config*`; the renderer reads `AppManager::status()`. On device, IPower/IStorage get real implementations. Settings persist as `wifi`, `bluetooth`, `brightness` keys.

## Accounts and sync (Phase 1: accounts only)
```
AppManager --> AccountManager --> OAuth (PKCE, token calls) --> IHttp  (MOCK today; real TLS transport not built)
                    |--> ISecretBox (AES-256-GCM via OpenSSL; NoSecretBox fails closed)
                    '--> IStorage  (accounts.json metadata, tokens/<hash>.bin sealed blobs)
```
Files in `core/`: `json`, `crypto` (SHA-256/base64/percent/random, non-secret helpers), `http` (interface), `oauth`, `secret_box`, `accounts`. The UI (`Settings -> Accounts`) only reads `AccountManager`; it never sees tokens. Planned and **not built**: `SyncManager` with per-service adapters (Contacts, Calendar), SQLite local repository, offline queue/backoff, conflict handling. The spec's separate `services/accounts`, `services/google-*` folders are not created yet: modules stay flat in `core/` until there is enough code to split.

Network, VPN, browser engine abstraction, YouTube: **designed in the request, not started.**
