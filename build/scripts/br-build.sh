#!/usr/bin/env bash
# Build the generic FaiOS QEMU development image with Buildroot (Ubuntu/WSL2).
# Output goes under $FAIOS_BR_WORK (default ~/faios-br), NOT inside the repo or /mnt/c.
# NOT a Jazz Digit 4G image.
set -euo pipefail
# WSL appends Windows PATH entries containing spaces, which Buildroot rejects. Use a clean PATH.
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
# Ubuntu 26.04 ships uutils (Rust) coreutils; Buildroot demands GNU `install`. Shim it for this build only (no sudo).
mkdir -p "${FAIOS_BR_WORK:-$HOME/faios-br}/hostbin"
if /usr/bin/install --version 2>&1 | grep -qi uutils && [ -x /usr/bin/gnuinstall ]; then
  ln -sf /usr/bin/gnuinstall "${FAIOS_BR_WORK:-$HOME/faios-br}/hostbin/install"
  export PATH="${FAIOS_BR_WORK:-$HOME/faios-br}/hostbin:$PATH"
fi

BR_VERSION="2026.02.3"                         # Buildroot LTS line
# SHA-256 from the release's .sign file (fetched over HTTPS 2026-11-11). Pinned here, so a changed tarball is
# refused. The PGP signature itself is not verified (needs Buildroot's signing key); do that if you need more.
BR_SHA256="65528a544f1e07c2f5ec487beca483bd380a6af8351a45f3649a19a0e8b63de2"
WORK="${FAIOS_BR_WORK:-$HOME/faios-br}"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
EXT="$REPO/build/buildroot/external"
SRC="$WORK/buildroot-$BR_VERSION"
OUT="$WORK/out"

mkdir -p "$WORK"
cd "$WORK"
if [ ! -d "$SRC" ]; then
  wget -nc "https://buildroot.org/downloads/buildroot-$BR_VERSION.tar.gz"
  echo "$BR_SHA256  buildroot-$BR_VERSION.tar.gz" | sha256sum -c -   # refuse a tarball that doesn't match the pin
  tar xf "buildroot-$BR_VERSION.tar.gz"
fi

make -C "$SRC" O="$OUT" BR2_EXTERNAL="$EXT" qemu_x86_64_defconfig
cat "$EXT/configs/faios_qemu_x86_64.fragment" >> "$OUT/.config"   # later assignments win
make -C "$SRC" O="$OUT" BR2_EXTERNAL="$EXT" olddefconfig
grep -q '^BR2_PACKAGE_FAIOS=y' "$OUT/.config" || { echo "FaiOS package not enabled (needs C++ toolchain)"; exit 1; }
make -C "$SRC" O="$OUT" BR2_EXTERNAL="$EXT" -j"$(nproc)"
ls -lh "$OUT/images"
