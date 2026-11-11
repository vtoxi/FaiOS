#!/usr/bin/env bash
# Re-apply the FaiOS config fragments and rebuild what changed (kernel config + the local faios package).
# Use after editing build/buildroot/external/configs/* or FaiOS sources. Full first build: br-build.sh.
set -euo pipefail
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
WORK="${FAIOS_BR_WORK:-$HOME/faios-br}"
[ -d "$WORK/hostbin" ] && export PATH="$WORK/hostbin:$PATH"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
EXT="$REPO/build/buildroot/external"
SRC="$WORK/buildroot-2026.02.3"; OUT="$WORK/out"
make -C "$SRC" O="$OUT" BR2_EXTERNAL="$EXT" qemu_x86_64_defconfig
cat "$EXT/configs/faios_qemu_x86_64.fragment" >> "$OUT/.config"
make -C "$SRC" O="$OUT" BR2_EXTERNAL="$EXT" olddefconfig
grep -E '^BR2_PACKAGE_(IPROUTE2|WIREGUARD_TOOLS|NFTABLES|FAIOS)=|^BR2_LINUX_KERNEL_CONFIG_FRAGMENT' "$OUT/.config"
make -C "$SRC" O="$OUT" BR2_EXTERNAL="$EXT" linux-dirclean faios-dirclean
make -C "$SRC" O="$OUT" BR2_EXTERNAL="$EXT" -j"$(nproc)"
ls -lh "$OUT/images"
