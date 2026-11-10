#!/usr/bin/env bash
# Install the FaiOS desktop-simulator toolchain on Ubuntu (WSL2). Run inside Ubuntu.
set -euo pipefail
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build git libsdl2-dev
gcc --version | head -1; cmake --version | head -1; sdl2-config --version
