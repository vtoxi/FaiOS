# FaiOS

Lightweight feature-phone OS (Linux + LVGL), developed desktop-first. Target: Jazz Digit 4G — **hardware unknown, no compatibility claimed** (see [HARDWARE_DISCOVERY.md](docs/HARDWARE_DISCOVERY.md)).

**Status:** Milestone 1 code written, **not yet compiled or run** (no toolchain on the dev machine yet). See [ROADMAP.md](ROADMAP.md).

Quick start (Ubuntu in WSL2): `bash build/scripts/setup-wsl.sh`, then see [BUILD.md](docs/BUILD.md).

Keys in the simulator: arrows, Enter=OK, Esc/Backspace=Back, F1/F2=softkeys, F3=Call, F4=End, 0–9, `*`/`#` (or keypad `*` `/`). Override with `--keymap file`.

Docs: [ARCHITECTURE](docs/ARCHITECTURE.md) · [BUILD](docs/BUILD.md) · [DEVELOPMENT](docs/DEVELOPMENT.md) · [DECISIONS](docs/DECISIONS.md) · [TESTING](docs/TESTING.md) · [COMPATIBILITY](docs/COMPATIBILITY.md)
