#!/bin/sh
set -eu

ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
STAMP=$(date -u '+%Y%m%dT%H%M%SZ')
OUT="$ROOT/evidence/runs/$STAMP"
mkdir -p "$OUT"

uname -a > "$OUT/uname.txt"
cat /proc/cmdline > "$OUT/kernel-cmdline.txt"
lspci -nnvv > "$OUT/lspci.txt"
ip -details link > "$OUT/ip-link.txt"
ip addr > "$OUT/ip-addr.txt"
dmesg > "$OUT/dmesg.txt"
ethtool -i xnic0 > "$OUT/ethtool-driver.txt" 2>&1 || true
ethtool -S xnic0 > "$OUT/ethtool-stats.txt" 2>&1 || true
modinfo "$ROOT/driver/xnic_e1000.ko" > "$OUT/modinfo.txt" 2>&1 || true
if [ -r "/boot/config-$(uname -r)" ]; then
	cp "/boot/config-$(uname -r)" "$OUT/kernel-config.txt"
fi
git -C "$ROOT" status --short > "$OUT/git-status.txt" 2>/dev/null || true
git -C "$ROOT" rev-parse HEAD > "$OUT/git-revision.txt" 2>/dev/null || true
qemu-system-aarch64 --version > "$OUT/qemu-version.txt" 2>&1 || true
gcc --version > "$OUT/gcc-version.txt" 2>&1 || true
clang --version > "$OUT/clang-version.txt" 2>&1 || true
sparse --version > "$OUT/sparse-version.txt" 2>&1 || true
cp /tmp/xnic-build.log "$OUT/build.log" 2>/dev/null || true
cp /tmp/xnic-sparse.log "$OUT/sparse.log" 2>/dev/null || true
cp /tmp/xnic-mixed-ping.log "$OUT/mixed-ping.log" 2>/dev/null || true
echo "$OUT"
