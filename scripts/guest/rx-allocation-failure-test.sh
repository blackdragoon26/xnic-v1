#!/bin/sh
set -eu

ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
. "$ROOT/scripts/guest/common.sh"
[ "$(id -u)" -eq 0 ] || { echo "Run with sudo" >&2; exit 1; }
BDF=$(xnic_find_bdf)
PARAM=/sys/module/xnic_e1000/parameters/fail_rx_alloc_after
[ -w "$PARAM" ] || { echo "Missing writable fault-injection parameter" >&2; exit 1; }

BEFORE=$(ethtool -S xnic0 | awk '/resets:/ {print $2}')
echo 8 > "$PARAM"
ping -I xnic0 -f -q -c 32 -W 1 10.11.0.2 \
	>/tmp/xnic-rx-failure-ping.log 2>&1 || true

deadline=$(( $(date +%s) + 10 ))
while :; do
	AFTER=$(ethtool -S xnic0 | awk '/resets:/ {print $2}')
	FAILURES=$(ethtool -S xnic0 | awk '/rx_alloc_failures:/ {print $2}')
	if [ "$AFTER" -gt "$BEFORE" ] && [ "$FAILURES" -gt 0 ] &&
	   [ "$(cat /sys/class/net/xnic0/carrier)" = 1 ]; then
		break
	fi
	[ "$(date +%s)" -lt "$deadline" ] || {
		echo "RX allocation failure did not recover" >&2
		exit 1
	}
	sleep 1
done

ping -I xnic0 -c 5 -W 2 10.11.0.2 >/dev/null
echo "PASS rx_allocation_failure reset_before=$BEFORE reset_after=$AFTER"
ethtool -S xnic0
