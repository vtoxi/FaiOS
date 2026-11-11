#!/usr/bin/env bash
# Build the generic FaiOS QEMU development image with Buildroot (Ubuntu/WSL2).
# Output goes under $FAIOS_BR_WORK (default ~/faios-br), NOT inside the repo or /mnt/c.
# NOT a Jazz Digit 4G image.
set -euo pipefail

BR_VERSION="${BR_VERSION:-2026.02.3}"        # Buildroot LTS line
WORK="${FAIOS_BR_WORK:-$HOME/faios-br}"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
EXT="$REPO/build/buildroot/external"
SRC="$WORK/buildroot-$BR_VERSION"
OUT="$WORK/out"

mkdir -p "$WORK"
cd "$WORK"
if [ ! -d "$SRC" ]; then
  wget -nc "https://buildroot.org/downloads/buildroot-$BR_VERSION.tar.gz"
  wget -nc "https://buildroot.org/downloads/buildroot-$BR_VERSION.tar.gz.sha256"
  sha256sum -c "buildroot-$BR_VERSION.tar.gz.sha256"   # refuse unverified sources
  tar xf "buildroot-$BR_VERSION.tar.gz"
fi

make -C "$SRC" O="$OUT" BR2_EXTERNAL="$EXT" qemu_x86_64_defconfig
cat "$EXT/configs/faios_qemu_x86_64.fragment" >> "$OUT/.config"   # later assignments win
make -C "$SRC" O="$OUT" BR2_EXTERNAL="$EXT" olddefconfig
grep -q '^BR2_PACKAGE_FAIOS=y' "$OUT/.config" || { echo "FaiOS package not enabled (needs C++ toolchain)"; exit 1; }
make -C "$SRC" O="$OUT" BR2_EXTERNAL="$EXT" -j"$(nproc)"
ls -lh "$OUT/images"
