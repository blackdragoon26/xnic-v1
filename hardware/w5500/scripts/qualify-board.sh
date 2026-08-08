#!/bin/sh
set -eu

IFACE=${XW5_IFACE:-}
PEER=${XW5_PEER:-}
DURATION=${XW5_DURATION:-300}
RESET_CYCLES=${XW5_RESET_CYCLES:-100}
ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
STAMP=$(date -u '+%Y%m%dT%H%M%SZ')
OUT="$ROOT/evidence/$STAMP"

if [ "$(id -u)" -ne 0 ]; then
	echo "run as root on the board" >&2
	exit 1
fi
if [ -z "$IFACE" ] || [ -z "$PEER" ]; then
	echo "set XW5_IFACE and XW5_PEER" >&2
	exit 1
fi
if [ ! -d "/sys/class/net/$IFACE" ]; then
	echo "interface $IFACE does not exist" >&2
	exit 1
fi

mkdir -p "$OUT"
exec >"$OUT/qualification.log" 2>&1

echo "timestamp=$STAMP"
echo "interface=$IFACE"
echo "peer=$PEER"
echo "duration=$DURATION"
uname -a
cat /proc/device-tree/model 2>/dev/null || true
printf '\n'
ethtool -i "$IFACE"
ethtool "$IFACE"
ethtool -S "$IFACE"
ip -details link show dev "$IFACE"

echo "[traffic]"
ping -I "$IFACE" -c 100 "$PEER"
if command -v iperf3 >/dev/null 2>&1; then
	iperf3 -c "$PEER" -t "$DURATION" -P 2
fi

echo "[interface lifecycle]"
i=0
while [ "$i" -lt 100 ]; do
	ip link set dev "$IFACE" down
	ip link set dev "$IFACE" up
	i=$((i + 1))
done
ping -I "$IFACE" -c 10 "$PEER"

echo "[serialized reset under traffic]"
RESET_ATTR=$(readlink -f "/sys/class/net/$IFACE/device")/force_reset
if [ ! -w "$RESET_ATTR" ]; then
	echo "missing writable reset trigger: $RESET_ATTR" >&2
	exit 1
fi
ping -I "$IFACE" -i 0.2 "$PEER" \
	>"$OUT/reset-ping.log" 2>&1 &
PING_PID=$!
trap 'kill "$PING_PID" 2>/dev/null || true' EXIT INT TERM
i=0
while [ "$i" -lt "$RESET_CYCLES" ]; do
	echo 1 > "$RESET_ATTR"
	sleep 1
	i=$((i + 1))
done
kill -INT "$PING_PID" 2>/dev/null || true
wait "$PING_PID" || true
trap - EXIT INT TERM
ping -I "$IFACE" -c 10 "$PEER"

echo "[final state]"
ethtool -S "$IFACE"
ip -s link show dev "$IFACE"
dmesg --level=alert,crit,err,warn >"$OUT/kernel-warnings.log"

echo "qualification commands completed; this is not a PASS until logs and captures are reviewed"
