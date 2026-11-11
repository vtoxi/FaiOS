# Decisions

1. **Core separate from LVGL** — screens expose a `View`; LVGL only renders. Makes navigation testable without SDL/network and keeps UI independent of drivers.
2. **No HAL interfaces yet** — spec wants them, but with one (simulated) implementation they are speculative. Add per subsystem when a second backend appears.
3. **LVGL v9.2.2 via CMake FetchContent** — no vendoring. SDL2 key events are captured with `SDL_AddEventWatch` instead of LVGL's keyboard indev, so F1/F2/Call/End mappings don't depend on LVGL key translation.
4. **Placeholder 240x320 display** — generic QVGA, not a Jazz Digit 4G spec.
5. **Wrote code before having a compiler** (user's choice, 2026-11-11). All code unverified until first build.
6. **Whole-UI rebuild per change** in the renderer — simple; optimise only if measured slow.
7. **Services in one header** (`services.h`) — each is tiny; split per file when one grows. Storage keys are relative names only (no `..`, no absolute) so a bad caller can't escape the data dir.
8. **Battery/brightness moved from `Device` to `IPower`** so real hardware can replace the simulated source behind one interface.
9. **Richer View model** (`Layout` + `Row`: lead icon/avatar, sub-text, trailing text, toggle, unread, tone) so the renderer stays dumb while screens look professional. Icons are LVGL built-in symbols, avatars are coloured letter circles: no image assets to ship. Theme colours are constants at the top of `simulator/main.cpp`.
10. **Calculator is a separate `Calc` class** (core/calc.cpp) with left-to-right chaining, so the maths is unit-tested without any UI. Operators sit on the arrow keys because feature phones have no operator keys.
11. **Hotspot refuses without an active data SIM** and raises a notification; enabling it turns the simulated Wi-Fi client off. Rules are simulated stand-ins for the real network stack.
12. **No signal when no SIM is enabled** (`AppManager::status()`), so the SIM manager visibly affects the status bar.
13. **`AppManager::requestPop()`** defers pops until the current key handler returns, so an action can close its own screen safely (delete confirm, alarm editor). Live-data screens (Files, Alarms, Notifications) recompute rows each draw instead of caching them.
14. **IStorage gained `list`/`remove`** (children listing, files-only delete) to back the File manager. Keys stay relative-only.
15. **Alarms fire through `AppManager::tick`** at most once per minute as notifications; no audio or full-screen ring until audio HAL exists.
16. **Grouped launcher** (7 tiles instead of 14): related apps share a tile that opens a short list. Fits one screen without scrolling, keeps digit shortcuts meaningful (1-7, then 1-3 inside a group), and mirrors how feature-phone menus are usually organised. Groups are defined in `makeLauncher`; moving an app is a one-line change.
17. **One owner per setting.** Radios and network state (Wi-Fi, Bluetooth, mobile data, airplane mode, hotspot, SIM manager) are in Connectivity only. Settings holds device preferences (Brightness, Accounts, About), has no switches, and never repeats a radio. System = Settings, Connectivity, Battery. A test (`test_one_owner_per_setting`) fails if a duplicate appears. Rule for new features: before adding a toggle, check which screen already owns that state and link to it instead.
18. **Wi-Fi vs hotspot rule lives in one function** (`setWifi`): turning Wi-Fi on turns the hotspot off, because the radio cannot be client and access point at once (simulated rule; real behaviour depends on the chipset).
19. **Gallery reads real files** from `DCIM/` (images) and `Videos/` (videos) on storage; the Camera writes SIMULATED placeholder BMPs there (no camera hardware is assumed). Decoding lives in the simulator renderer: BMP (24-bit, `core/media.cpp`) and PNG (8-bit, via zlib). JPEG and video playback are NOT built; such files show an honest "Preview not available" / "Video playback is not available yet." message. LVGL's bundled LodePNG returned corrupt pixels in this build, so it is disabled.
