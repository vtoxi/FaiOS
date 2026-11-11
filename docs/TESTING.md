# Testing

`tests/test_core.cpp` (plain asserts, run with `ctest`): launcher open/back, root never popped, five apps via digit shortcuts, End→home, list wrap, dialer entry/clear, no faked calls, message read state, settings toggle, keymap name round-trip.

**Results (2026-11-11, Ubuntu WSL2, gcc 15.2, cmake 4.2.3):** `ctest` passes (navigation + M3 services: logger, storage, config, notifications, power, service lifecycle, settings persistence). Simulator headless smoke-run starts services and exits only on timeout, no crash.
Not covered: the LVGL renderer and SDL input (verified manually by user in a WSLg window).

M3 tests cover: logger ring/filter, storage atomic overwrite and path-traversal rejection, config round-trip and junk values, notifications, power clamping, service start order/rollback, settings persisting through Config.

App tests: calculator maths (chaining, divide-by-zero, decimals, backspace, length cap), calculator keys, stopwatch with a fake clock, camera/gallery, SIM manager + hotspot rules, launcher reaches all 10 apps.

More app tests: storage list/remove (mem + file), file manager browse/view/delete-confirm, connectivity + airplane, battery screen, alarms (edit validation, persistence, firing once per minute), notifications screen.

## Real-kernel tunnel test (generic QEMU image)

`build/scripts/br-build.sh` (first time) or `br-rebuild.sh` (after changes), then `build/scripts/br-run.sh --test`. Boots the image, runs the self-test, then `faios-tunnel-test`, which brings up a real WireGuard tunnel to a namespace "server" using `CommandVpnBackend` and checks handshake, routing, kill-switch leak blocking and clean teardown. Passes only if both `FAIOS-SELFTEST: PASS` and `FAIOS-TUNNEL: PASS` appear. Never run `faios-tunnel-test` on a normal host: it installs a default-drop firewall table (inside QEMU that is the point). Mutation run: `FAIOS_QEMU_APPEND=faios.mutate=nokillswitch br-run.sh --test` must FAIL the two leak checks.

## Sync tests

`tests/test_sync.inc` runs the sync engine against `MockGoogleApi` (paging, incremental tokens, expired tokens, 401/403/500, offline, interrupted pulls, hostile ids, conflicts of every kind, tombstones, restart with a pending queue, scheduler backoff, token never in logs). Run the whole suite under AddressSanitizer with: `cmake -G Ninja -DFAIOS_BUILD_SIMULATOR=OFF -DCMAKE_CXX_FLAGS="-fsanitize=address -g" <repo> && ninja test_core && ./test_core`. (ASan found and we fixed a dangling temporary in an older games test.)
