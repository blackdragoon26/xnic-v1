#!/bin/sh
set -eu

ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
[ "$(id -u)" -eq 0 ] || { echo "Run with sudo" >&2; exit 1; }
INPUT="$ROOT/evidence/runs/20260808T080600Z/icmp.pcap"
APP="$ROOT/dpdk/xnic-forwarder"
OUT=/tmp/xnic-dpdk-root
mkdir -p "$OUT"

make -C "$ROOT/dpdk" clean all
"$APP" -l 0 --no-huge --no-pci \
	--vdev="net_pcap0,rx_pcap=$INPUT,tx_pcap=$OUT/output.pcap" \
	-- -p 0 -i 100 > "$OUT/normal.log"
grep -q 'XNIC_DPDK_RESULT rx=20 tx=20 dropped=0 signal=0' \
	"$OUT/normal.log"
[ "$(tcpdump -nn -r "$OUT/output.pcap" 2>/dev/null | wc -l)" -eq 20 ]

if "$APP" -l 0 --file-prefix=xnic-partial --no-huge --no-pci \
	--vdev="net_pcap0,rx_pcap=$INPUT,tx_pcap=$OUT/partial.pcap" \
	-- -p 0 -i 100 -t 4 > "$OUT/partial.log"; then
	echo "Partial-TX test unexpectedly returned success" >&2
	exit 1
fi
grep -Eq 'XNIC_DPDK_RESULT rx=20 tx=4 dropped=16 signal=0' \
	"$OUT/partial.log"

"$APP" -l 0 --file-prefix=xnic-signal --no-huge --no-pci \
	--vdev="net_pcap0,tx_pcap=$OUT/signal.pcap" \
	-- -p 0 -i 0 > "$OUT/signal.log" &
APP_PID=$!
trap 'kill "$APP_PID" 2>/dev/null || true' EXIT INT TERM
sleep 1
kill -TERM "$APP_PID"
wait "$APP_PID"
trap - EXIT INT TERM
grep -q 'XNIC_DPDK_RESULT rx=0 tx=0 dropped=0 signal=1' \
	"$OUT/signal.log"

echo "PASS dpdk_pcap normal=20/20 partial=4/20 signal_shutdown=clean"
