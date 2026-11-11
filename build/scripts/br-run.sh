#!/usr/bin/env bash
# Boot the FaiOS QEMU image built by br-build.sh.
#   br-run.sh          interactive serial console (Ctrl-A X quits QEMU)
#   br-run.sh --test   headless: boot, wait for the self-test marker, print result, exit 0 on PASS
set -euo pipefail
WORK="${FAIOS_BR_WORK:-$HOME/faios-br}"
IMG="$WORK/out/images"
[ -f "$IMG/bzImage" ] || { echo "no image: run build/scripts/br-build.sh first"; exit 2; }
KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"

args=(-M pc $KVM -m 256 -kernel "$IMG/bzImage"
      -drive "file=$IMG/rootfs.ext2,if=virtio,format=raw"
      -append "rootwait root=/dev/vda console=ttyS0 ${FAIOS_QEMU_APPEND:-}"
      -nic user,model=virtio-net-pci)

if [ "${1:-}" = "--test" ]; then
  LOG="$(mktemp)"
  qemu-system-x86_64 "${args[@]}" -display none -serial "file:$LOG" </dev/null &
  QPID=$!
  trap 'kill $QPID 2>/dev/null || true' EXIT
  for _ in $(seq 1 240); do
    grep -q 'FAIOS-DONE' "$LOG" && break
    sleep 1
  done
  grep -E 'FAIOS|^ok  |^FAIL|checks,' "$LOG" || true
  grep -q 'FAIOS-SELFTEST: PASS' "$LOG" && grep -q 'FAIOS-TUNNEL: PASS' "$LOG"
else
  exec qemu-system-x86_64 "${args[@]}" -nographic
fi
