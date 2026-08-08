#!/bin/sh
set -eu

ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
[ "$(id -u)" -eq 0 ] || { echo "Run with sudo" >&2; exit 1; }
STAMP=$(date -u '+%Y%m%dT%H%M%SZ')
OUT="$ROOT/evidence/runs/$STAMP"
mkdir -p "$OUT"

make -C "$ROOT/driver" clean >"$OUT/build-clean.log" 2>&1
make -C "$ROOT/driver" >"$OUT/build-gcc.log" 2>&1
make -C "/lib/modules/$(uname -r)/build" M="$ROOT/driver" C=2 modules \
	>"$OUT/sparse.log" 2>&1

"$ROOT/scripts/guest/probe-failure-test.sh" >"$OUT/probe-failures.log" 2>&1
"$ROOT/scripts/guest/bind-driver.sh" >"$OUT/bind.log" 2>&1
"$ROOT/scripts/guest/packet-boundary-test.sh" >"$OUT/packet-boundaries.log" 2>&1
"$ROOT/scripts/guest/wraparound-test.sh" 10000 >"$OUT/wraparound.log" 2>&1
"$ROOT/scripts/guest/tx-ring-full-test.sh" >"$OUT/tx-ring-full.log" 2>&1
"$ROOT/scripts/guest/tx-timeout-test.sh" >"$OUT/tx-timeout.log" 2>&1
"$ROOT/scripts/guest/rx-allocation-failure-test.sh" >"$OUT/rx-allocation.log" 2>&1
"$ROOT/scripts/guest/malformed-rx-test.sh" >"$OUT/malformed-rx.log" 2>&1
"$ROOT/scripts/guest/device-disappearance-test.sh" >"$OUT/device-disappearance.log" 2>&1
"$ROOT/scripts/guest/reset-under-traffic.sh" 100 >"$OUT/reset-under-traffic.log" 2>&1
"$ROOT/scripts/guest/lifecycle-test.sh" hvf >"$OUT/lifecycle-hvf.log" 2>&1

tcpdump -i xnic0 -U -c 20 -w "$OUT/icmp.pcap" icmp >"$OUT/tcpdump.log" 2>&1 &
TCPDUMP_PID=$!
trap 'kill "$TCPDUMP_PID" 2>/dev/null || true' EXIT INT TERM
sleep 1
ping -I xnic0 -c 20 -W 2 10.11.0.2 >"$OUT/icmp-ping.log" 2>&1
wait "$TCPDUMP_PID"
trap - EXIT INT TERM
tcpdump -nn -r "$OUT/icmp.pcap" >"$OUT/icmp-pcap-summary.txt" 2>&1

cp /tmp/xnic-mixed-stress.log "$OUT/mixed-stress.log" 2>/dev/null || true
cp /tmp/xnic-mixed-ping.log "$OUT/mixed-ping.log" 2>/dev/null || true
"$ROOT/scripts/guest/collect-evidence.sh" >"$OUT/collector-path.txt"

ethtool -S xnic0 >"$OUT/final-ethtool-stats.txt"
dmesg | grep -E 'BUG:|WARNING:|KASAN:|KFENCE:|lockdep|deadlock|Call trace:' \
	>"$OUT/kernel-alerts.txt" || true
[ ! -s "$OUT/kernel-alerts.txt" ] || {
	echo "Kernel diagnostic alerts found in $OUT/kernel-alerts.txt" >&2
	exit 1
}

echo "PASS qualification evidence=$OUT"
