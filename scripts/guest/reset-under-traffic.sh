#!/bin/sh
set -eu

ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
. "$ROOT/scripts/guest/common.sh"
[ "$(id -u)" -eq 0 ] || { echo "Run with sudo" >&2; exit 1; }
CYCLES=${1:-50}
BDF=$(xnic_find_bdf)
RESET="/sys/bus/pci/devices/$BDF/xnic_reset"
[ -w "$RESET" ] || { echo "Missing XNIC reset control: $RESET" >&2; exit 1; }

ping -I xnic0 -i 0.02 10.11.0.2 > /tmp/xnic-reset-ping.log 2>&1 &
PING_PID=$!
trap 'kill "$PING_PID" 2>/dev/null || true' EXIT INT TERM

i=1
while [ "$i" -le "$CYCLES" ]; do
	echo 1 > "$RESET"
	deadline=$(( $(date +%s) + 10 ))
	while [ "$(cat "/sys/class/net/xnic0/carrier" 2>/dev/null || echo 0)" != 1 ]; do
		[ "$(date +%s)" -lt "$deadline" ] || {
			echo "Carrier did not recover after reset $i" >&2
			exit 1
		}
		sleep 1
	done
	i=$((i + 1))
done

kill "$PING_PID" 2>/dev/null || true
wait "$PING_PID" 2>/dev/null || true
trap - EXIT INT TERM
echo "PASS reset_under_traffic=$CYCLES"
ethtool -S xnic0
