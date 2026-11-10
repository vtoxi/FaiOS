# Development

- Add logic to `core/` and test it in `tests/test_core.cpp`; keep LVGL/SDL out of core.
- New screen: write a factory in `core/screens.cpp`, declare in `screens.h`, link it from the launcher.
- Simulated data lives in `Device`; mark anything fake as simulated.
- Build/test loop: see [BUILD.md](BUILD.md). Run `ctest` before committing.
- Never claim hardware support without evidence in HARDWARE_DISCOVERY.md.
