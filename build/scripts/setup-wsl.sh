#!/usr/bin/env bash
# Install the FaiOS toolchain on Ubuntu (WSL2). Run inside Ubuntu. Safe to re-run.
#   - simulator: build-essential cmake ninja-build git libsdl2-dev
#   - Buildroot image + QEMU: wget cpio unzip rsync bc file bzip2 xz-utils qemu-system-x86
set -euo pipefail
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build git libsdl2-dev \
  wget cpio unzip rsync bc file bzip2 xz-utils qemu-system-x86
gcc --version | head -1; cmake --version | head -1; sdl2-config --version
command -v qemu-system-x86_64 cpio unzip
