# Roadmap

Legend: [x] done and verified · [~] written, unverified · [ ] not started

## M1 â€” Desktop simulator
- [x] Inspect environment: WSL2 present but **no distro**; Windows has no gcc/cmake/ninja (git, Python only)
- [x] Project structure, CMake (core lib + tests + optional LVGL/SDL2 simulator) — builds on Ubuntu/WSL2
- [~] (compiled, not seen) Home screen: clock, date, signal, Wi-Fi, battery, unread indicator, softkeys
- [~] (compiled, not seen) Keyboard navigation + configurable keymap
- [~] (compiled, not seen) Launcher + Phone, Contacts, Messages, Call history, Settings screens
- [x] Core tests (ctest): 1/1 pass
- [x] Build + ctest pass; simulator runs headless without crashing
- [ ] Verify against completion criteria (see below)

M1 complete: builds, tests pass, window launches, keys work (user-confirmed).

## Later (not started)
- M3 core services (config, logging, notifications, power, storage)
- M4 HAL interfaces + simulator backends (add when a second implementation exists)
- M5 Buildroot generic QEMU image (not device-compatible)
- M6 Hardware discovery · M7 Firmware preservation (blocked: phone not powering on)

## Not supported
Real telephony, SMS, camera, audio, radios, persistence, Calculator/Clock/File manager/Connectivity/Battery apps.

## M3 — Core services
- [x] Logger (bounded ring + sink), Storage (File atomic + Mem, rejects `..`/absolute keys), Config (key=value), Notifications, IPower (+SimPower), Service lifecycle (ordered start, rollback on failure, reverse stop), SystemStatus — unit-tested, all pass
- [x] Settings (Wi-Fi, Bluetooth, brightness) persist via Config; simulator stores them in `~/.faios/config.ini`
- [ ] Not yet seen in the live window: settings surviving a simulator restart
- [ ] Notifications have no UI beyond the status-bar badge count

## UI polish pass
- [x] Themed renderer: navy theme, gradient home, icon-grid launcher, card rows with letter avatars / icons / toggle switches / unread dots, on-screen dial keypad, detail cards, 3-slot softkey bar, scrollbar
- [x] Contact card has a Call softkey that opens the dialer prefilled
- [ ] Visual check in the live window — pending user feedback
- [ ] No animations/transitions yet (renderer rebuilds the whole screen per change)
