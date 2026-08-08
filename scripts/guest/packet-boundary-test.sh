#!/bin/sh
set -eu

ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
[ "$(id -u)" -eq 0 ] || { echo "Run with sudo" >&2; exit 1; }

# Minimum legal Ethernet frame without FCS; e1000's PSP bit supplies padding.
BEFORE=$(cat /sys/class/net/xnic0/statistics/tx_packets)
python3 "$ROOT/scripts/guest/raw-tx-burst.py" xnic0 1 60
sleep 1
AFTER=$(cat /sys/class/net/xnic0/statistics/tx_packets)
[ "$AFTER" -gt "$BEFORE" ] || { echo "Minimum frame did not complete" >&2; exit 1; }

# 1472 bytes of ICMP data plus IPv4/ICMP headers exercises the 1500-byte MTU.
ping -I xnic0 -M 'do' -s 1472 -c 5 -W 2 10.11.0.2 >/dev/null

# netdev max_mtu is part of the driver contract and must reject jumbo setup.
if ip link set xnic0 mtu 1501 2>/tmp/xnic-oversize-error.log; then
	echo "Oversized MTU unexpectedly accepted" >&2
	exit 1
fi
[ "$(cat /sys/class/net/xnic0/mtu)" = 1500 ]

echo "PASS packet_boundaries min_frame=60 standard_mtu=1500 oversized_mtu=rejected"
