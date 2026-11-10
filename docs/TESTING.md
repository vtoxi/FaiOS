# Testing

`tests/test_core.cpp` (plain asserts, run with `ctest`): launcher open/back, root never popped, five apps via digit shortcuts, End→home, list wrap, dialer entry/clear, no faked calls, message read state, settings toggle, keymap name round-trip.

**Results (2026-11-11, Ubuntu WSL2, gcc 15.2, cmake 4.2.3):** `ctest` 1/1 passed. Simulator launched with `SDL_VIDEODRIVER=dummy` and ran 4-5 s until killed by timeout, no crash.
Not covered: the LVGL renderer and SDL input (a real-window visual check is still pending).
