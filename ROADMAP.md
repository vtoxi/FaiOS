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
- [x] Visual check of these screens in the live window: user approved

## M6/M7 — Hardware discovery & firmware preservation (documents only, no device work yet)
- [x] HARDWARE_DISCOVERY.md: fields marked UNKNOWN with web LEADS kept separate from evidence; tiered plan; battery safety notes; decision gates
- [x] FIRMWARE_PRESERVATION.md: backup/verify/restore-path procedure (nothing executed)
- [x] `build/scripts/usb-inventory.ps1` (read-only); first run saw a MediaTek Preloader device (E-2), identity unconfirmed
- [x] Replug test with `usb-watch.ps1`: no enumeration seen (E-4), so E-2/E-3 remain unconfirmed
- [ ] Try: data-capable cable, battery removed vs old battery inserted; then photograph the label under the battery; FCC ID lookup
- [ ] Nothing flashed, read back or modified. No compatibility claim.

## Grouped menu
- [x] Launcher is 7 tiles: Phone (Dialer, Contacts, Call history), Messages, Camera, Clock (Clock, Alarms), Network (Connectivity, Hotspot, SIM manager), Tools (Calculator, File manager), System (Settings, Battery). Tiles with several apps open a short icon list; keys 1..n jump in. Tests open apps by name through these paths.
- [x] Visual check of grouped menu: user approved

- Device work paused by owner (2026-11-11): cannot connect the phone for now. Resume with label photo, then data-cable USB retry. Dual SIM noted (E-5).

## Menu changes
- [x] Network (Connectivity, Hotspot, SIM manager) moved into System; System = Settings, Connectivity, Hotspot, SIM manager, Battery. Top level: Phone, Messages, Camera, Clock, Tools, System (Games/Calendar/Media will fill the grid)
- [x] Fixed: status-bar clock overlapped the icon group; truncated tile labels; text cards overflowing

## Accounts & Google sync
- [x] Phase 1 core (mock-tested): JSON, PKCE/OAuth, token refresh/revoke, account manager, fail-closed secret box, Settings -> Accounts screens (+ simulator-only `[MOCK]` sign-in with `--mock-google`)
- [x] AES-GCM secret box tests run and pass (OpenSSL installed 2026-11-11)
- [ ] Phase 1 gaps: real HTTP/TLS transport (libcurl dev files still missing), external-browser + loopback redirect, PIN-derived key, a real Google OAuth client (owner must create)
- [ ] Phase 2 Contacts sync, Phase 3 Calendar sync (+ native calendar app), Phase 4 offline queue/conflicts, Phase 5 Gmail/Drive. **None started.** Needs SQLite dev files.
- Google rule found: device flow cannot request Contacts/Calendar scopes (docs/GOOGLE_AUTH.md)

## Queued, not started
- Network manager + WireGuard VPN core; browser engine abstraction + tab memory manager; YouTube evaluation; Media apps (simulated); Games + Calendar app wiring (logic written, untested); multi-tap text entry
- Buildroot QEMU image: built and booted (Linux 6.18.7, generic x86-64); boot self-test PASS (needed a fix: core library is now static)

## Network + VPN core (2026-11-11)
- [x] IP/endpoint parsing, WireGuard config parser (script hooks refused), command + kill-switch + DNS generators, NetworkManager, VPN store (sealed keys), VPN state machine, command backend, Connectivity -> VPN screens. **738 checks pass; a mutation check (build/scripts/mutation-check.py) confirmed the tests catch 13 deliberate breakages in the secret, kill-switch, routing and DNS logic.**
- [x] Real WireGuard tunnel + nftables kill switch verified in the generic QEMU image (20 checks, mutation-checked). [ ] Still not verified on the Digit 4G or with a real provider.
- [ ] Real link provider, real DNS writer, userspace WireGuard decision (needs phone kernel/RAM), OpenVPN, browser abstraction

## Games + Calendar (2026-11-11)
- [x] Games: Snake (speeds up, pause, retry), Tic-Tac-Toe (computer wins/blocks/takes centre, otherwise random: beatable), Number guess. Logic in `core/games.*`, deterministic by seed. Menu: Games group
- [x] Calendar: Monday-first month grid, today/selection, day agenda, local events (add by time, delete, persisted in `calendar/local.json`, each event records its source for later account sync). **Event titles cannot be typed yet** (no text entry): new events are titled "Event"
- [x] 2962 checks pass (found and fixed: whitespace-only event titles)
- [ ] Not built: event titles/editing, recurring events, reminders, time zones, Google Calendar sync
