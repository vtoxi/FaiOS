# Roadmap

Legend: [x] done and verified · [~] written, unverified · [ ] not started

## M1 — Desktop simulator
- [x] Inspect environment: WSL2 present but **no distro**; Windows has no gcc/cmake/ninja (git, Python only)
- [~] Project structure, CMake (core lib + tests + optional LVGL/SDL2 simulator)
- [~] Home screen: clock, date, signal, Wi-Fi, battery, unread indicator, softkeys
- [~] Keyboard navigation + configurable keymap
- [~] Launcher + Phone, Contacts, Messages, Call history, Settings screens
- [~] Core tests (ctest)
- [ ] **Build, run tests, launch simulator, fix errors** — blocked on toolchain
- [ ] Verify against completion criteria (see below)

M1 is complete only when it compiles, tests pass, the window launches and keys work. None of that has been observed yet.

## Later (not started)
- M3 core services (config, logging, notifications, power, storage)
- M4 HAL interfaces + simulator backends (add when a second implementation exists)
- M5 Buildroot generic QEMU image (not device-compatible)
- M6 Hardware discovery · M7 Firmware preservation (blocked: phone not powering on)

## Not supported
Real telephony, SMS, camera, audio, radios, persistence, Calculator/Clock/File manager/Connectivity/Battery apps.
