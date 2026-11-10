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
