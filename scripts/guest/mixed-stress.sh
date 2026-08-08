#!/bin/sh
set -eu

ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
. "$ROOT/scripts/guest/common.sh"
[ "$(id -u)" -eq 0 ] || { echo "Run with sudo" >&2; exit 1; }
DURATION=${1:-1800}
RESET_INTERVAL=${2:-18}
BDF=$(xnic_find_bdf)
RESET="/sys/bus/pci/devices/$BDF/xnic_reset"
END=$(( $(date +%s) + DURATION ))
RESETS=0

ping -I xnic0 -i 0.01 10.11.0.2 >/tmp/xnic-mixed-ping.log 2>&1 &
PING_PID=$!
trap 'kill "$PING_PID" 2>/dev/null || true' EXIT INT TERM

while [ "$(date +%s)" -lt "$END" ]; do
	echo 1 > "$RESET"
	RESETS=$((RESETS + 1))
	# Every tenth iteration overlaps lifecycle teardown with queued recovery.
	if [ $((RESETS % 10)) -eq 0 ]; then
		ip link set xnic0 down
		ip link set xnic0 up
	fi
	deadline=$(( $(date +%s) + 10 ))
	until ping -I xnic0 -c 1 -W 1 10.11.0.2 >/dev/null 2>&1; do
		[ "$(date +%s)" -lt "$deadline" ] || {
			echo "Traffic did not recover after mixed iteration $RESETS" >&2
			exit 1
		}
	done
	sleep "$RESET_INTERVAL"
done

kill "$PING_PID" 2>/dev/null || true
wait "$PING_PID" 2>/dev/null || true
trap - EXIT INT TERM
echo "PASS mixed_stress duration=$DURATION resets=$RESETS"
ethtool -S xnic0
dmesg | grep -E 'BUG:|WARNING:|KASAN:|KFENCE:|lockdep|deadlock|Call trace:' && exit 1
exit 0
