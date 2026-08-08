#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
. "$ROOT/scripts/guest/common.sh"
[ "$(id -u)" -eq 0 ] || { echo "Run with sudo" >&2; exit 1; }
BDF=$(xnic_find_bdf)
STALL="/sys/bus/pci/devices/$BDF/xnic_tx_stall"
[ -w "$STALL" ] || { echo "Missing TX stall control" >&2; exit 1; }

BEFORE_TIMEOUT=$(ethtool -S xnic0 | awk '/tx_timeouts:/ {print $2}')
BEFORE_RESET=$(ethtool -S xnic0 | awk '/resets:/ {print $2}')
echo 1 > "$STALL"
python3 "$ROOT/scripts/guest/raw-tx-burst.py" xnic0 4096 \
	>/tmp/xnic-timeout-burst.log 2>&1 &
BURST_PID=$!
trap 'echo 0 > "$STALL" 2>/dev/null || true; kill "$BURST_PID" 2>/dev/null || true' EXIT INT TERM

deadline=$(( $(date +%s) + 15 ))
while :; do
	AFTER_TIMEOUT=$(ethtool -S xnic0 | awk '/tx_timeouts:/ {print $2}')
	AFTER_RESET=$(ethtool -S xnic0 | awk '/resets:/ {print $2}')
	[ "$AFTER_TIMEOUT" -gt "$BEFORE_TIMEOUT" ] && \
		[ "$AFTER_RESET" -gt "$BEFORE_RESET" ] && break
	[ "$(date +%s)" -lt "$deadline" ] || {
		echo "TX watchdog/reset did not fire" >&2
		exit 1
	}
	sleep 1
done

# xnic_up() clears the deliberate stall while rebuilding the rings.
wait "$BURST_PID" 2>/dev/null || true
trap - EXIT INT TERM
ping -I xnic0 -c 5 -W 2 10.11.0.2 >/dev/null
echo "PASS tx_timeout timeout=$BEFORE_TIMEOUT->$AFTER_TIMEOUT reset=$BEFORE_RESET->$AFTER_RESET"
ethtool -S xnic0
