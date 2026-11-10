# Decisions

1. **Core separate from LVGL** — screens expose a `View`; LVGL only renders. Makes navigation testable without SDL/network and keeps UI independent of drivers.
2. **No HAL interfaces yet** — spec wants them, but with one (simulated) implementation they are speculative. Add per subsystem when a second backend appears.
3. **LVGL v9.2.2 via CMake FetchContent** — no vendoring. SDL2 key events are captured with `SDL_AddEventWatch` instead of LVGL's keyboard indev, so F1/F2/Call/End mappings don't depend on LVGL key translation.
4. **Placeholder 240x320 display** — generic QVGA, not a Jazz Digit 4G spec.
5. **Wrote code before having a compiler** (user's choice, 2026-11-11). All code unverified until first build.
6. **Whole-UI rebuild per change** in the renderer — simple; optimise only if measured slow.
