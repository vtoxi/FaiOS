# Development

- Add logic to `core/` and test it in `tests/test_core.cpp`; keep LVGL/SDL out of core.
- New screen: write a factory in `core/screens.cpp`, declare in `screens.h`, link it from the launcher.
- Simulated data lives in `Device`; mark anything fake as simulated.
- Build/test loop: see [BUILD.md](BUILD.md). Run `ctest` before committing.
- Never claim hardware support without evidence in HARDWARE_DISCOVERY.md.

## Screenshots without a window
`faios_sim --script "Ok,shot:menu,Num1,shot:phone" --shots DIR` renders each `shot:` to `DIR/<name>.ppm` (240x320) and exits. Look at the result before and after any UI change (convert PPM to PNG with any tool). Use an isolated `HOME=/tmp/x` so real saved settings do not affect the picture.

## Accounts in the simulator
`faios_sim --mock-google` enables a `[MOCK] Test account` row under Settings -> Accounts -> Add account. It signs in against `simulator/mock_google.h` (fake Google). Needs the secret box (OpenSSL) or it will report "no secure storage". Never use it as evidence that Google sign-in works.
