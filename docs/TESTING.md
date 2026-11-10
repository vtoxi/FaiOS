# Testing

`tests/test_core.cpp` (plain asserts, run with `ctest`): launcher open/back, root never popped, five apps via digit shortcuts, End→home, list wrap, dialer entry/clear, no faked calls, message read state, settings toggle, keymap name round-trip.

**Results (2026-11-11, Ubuntu WSL2, gcc 15.2, cmake 4.2.3):** `ctest` passes (navigation + M3 services: logger, storage, config, notifications, power, service lifecycle, settings persistence). Simulator headless smoke-run starts services and exits only on timeout, no crash.
Not covered: the LVGL renderer and SDL input (verified manually by user in a WSLg window).

M3 tests cover: logger ring/filter, storage atomic overwrite and path-traversal rejection, config round-trip and junk values, notifications, power clamping, service start order/rollback, settings persisting through Config.
