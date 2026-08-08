#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
. "$ROOT/scripts/guest/common.sh"
[ "$(id -u)" -eq 0 ] || { echo "Run with sudo" >&2; exit 1; }
BDF=$(xnic_find_bdf)
STALL="/sys/bus/pci/devices/$BDF/xnic_tx_stall"
[ -w "$STALL" ] || { echo "Missing TX stall control" >&2; exit 1; }

BEFORE=$(ethtool -S xnic0 | awk '/tx_ring_full:/ {print $2}')
# Resolve the peer before suppressing the TX doorbell.  Otherwise the test
# stalls on a single unanswered ARP request instead of filling the ring.
ping -I xnic0 -c 1 -W 2 10.11.0.2 >/dev/null
echo 1 > "$STALL"
python3 "$ROOT/scripts/guest/raw-tx-burst.py" xnic0 4096 \
	>/tmp/xnic-ring-full-burst.log 2>&1 &
BURST_PID=$!
trap 'echo 0 > "$STALL"; kill "$BURST_PID" 2>/dev/null || true' EXIT INT TERM

deadline=$(( $(date +%s) + 10 ))
while :; do
	AFTER=$(ethtool -S xnic0 | awk '/tx_ring_full:/ {print $2}')
	[ "$AFTER" -gt "$BEFORE" ] && break
	[ "$(date +%s)" -lt "$deadline" ] || {
		echo "TX ring did not reach full state" >&2
		exit 1
	}
	sleep 1
done

echo 0 > "$STALL"
wait "$BURST_PID" 2>/dev/null || true
trap - EXIT INT TERM
ping -I xnic0 -c 5 -W 2 10.11.0.2 >/dev/null
echo "PASS tx_ring_full before=$BEFORE after=$AFTER"
ethtool -S xnic0
