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
- [x] Visual check in the live window: user approved the look
- [ ] No animations/transitions yet (renderer rebuilds the whole screen per change)

## More apps (simulated)
- [x] Clock (time/date + stopwatch), Calculator (logic class + screen), Camera (simulated viewfinder, capture, gallery), Hotspot (toggle, needs an active data SIM, shows SSID/clients), SIM manager (slots, enable, data SIM, PIN-lock flag). Unit-tested; launcher is now a scrolling 10-app grid (key 0 = tenth)
- All data is simulated. Camera has no hardware; SIM slot count (2 shown, 1 empty) is a placeholder until the modem HAL reports real slots. Hotspot/SIM state persists in `config.ini`; PIN lock stores no PIN.
- [ ] Not built: File manager, Connectivity, Battery info apps; alarms; real camera/modem

- [x] New apps visually checked in the live window: user approved (2026-11-11)

## Remaining apps (simulated, 14 apps total)
- [x] Alarms (add/edit/enable/delete, fire as notifications once per minute, persisted), File manager (browses `~/.faios`, view, delete with confirm), Connectivity (Wi-Fi, networks, Bluetooth, mobile data, airplane mode, hotspot), Battery info (percent, state, voltage, temperature; all SIMULATED), Notifications screen (Up on Home)
- [x] Camera is capture-only: Gallery removed per request (no gallery, no photo files)
- Not built: Bluetooth device pairing, Wi-Fi password entry, alarm labels/ringtones, alarm ring screen (alarms appear as notifications)
- [ ] Visual check of these screens in the live window — pending
