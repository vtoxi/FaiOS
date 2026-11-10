# Architecture

```
core/       Toolkit-independent. Screen model, AppManager (screen stack + key routing), screens, simulated Device state.
simulator/  LVGL + SDL2 renderer; maps SDL keys to core Keys. Only place that includes LVGL/SDL.
tests/      ctest, links core only.
```
- `Screen::view()` returns a `View` (title, lines, selection, softkeys); the renderer only draws it. A different renderer (framebuffer on device) can reuse core unchanged.
- `AppManager` = lifecycle: push/pop/goHome; unconsumed Back/Soft2 pops; End goes home.
- `Device` holds **simulated** data; it is the seam where real HAL providers plug in later.

Deliberately not built yet (no second implementation exists): HAL interfaces, service layer, IPC, SQLite, Buildroot. The spec's full directory tree is not scaffolded; folders are added with the code that fills them.
