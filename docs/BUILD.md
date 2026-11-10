# Build

Build and ctest verified on Ubuntu 25.x WSL2. First configure is slow (~4 min, downloads LVGL). Window via WSLg verified.

## Toolchain (Ubuntu in WSL2)
Windows currently has WSL2 but no distro. In PowerShell (admin, may need reboot):
```
wsl --install -d Ubuntu
```
Then inside Ubuntu, from the repo (`/mnt/c/Users/faisals/Desktop/Claude/FaiOS`; copying into `~/` builds faster):
```
bash build/scripts/setup-wsl.sh
```

## Build and test
```
cmake -S . -B out -G Ninja
cmake --build out
ctest --test-dir out --output-on-failure
```
Tests only (no network, no SDL2): `cmake -S . -B out -DFAIOS_BUILD_SIMULATOR=OFF`

## Run
```
./out/faios_sim [--keymap keymap.txt]
```
Window shows via WSLg (Windows 11). keymap lines: `<SDL key name> <FaiOS key>` e.g. `F5 Call`.
