#!/bin/sh
set -eu

[ "$(id -u)" -eq 0 ] || { echo "Run with sudo" >&2; exit 1; }
TARGET=${1:-10000}
PACKETS=$(( (TARGET + 100) * 64 ))
TX_BEFORE=$(ethtool -S xnic0 | awk '/tx_ring_wraps:/ {print $2}')
RX_BEFORE=$(ethtool -S xnic0 | awk '/rx_ring_wraps:/ {print $2}')

ping -I xnic0 -f -q -c "$PACKETS" -W 1 10.11.0.2

TX_AFTER=$(ethtool -S xnic0 | awk '/tx_ring_wraps:/ {print $2}')
RX_AFTER=$(ethtool -S xnic0 | awk '/rx_ring_wraps:/ {print $2}')
TX_DELTA=$((TX_AFTER - TX_BEFORE))
RX_DELTA=$((RX_AFTER - RX_BEFORE))
[ "$TX_DELTA" -ge "$TARGET" ] || { echo "TX wraps $TX_DELTA < $TARGET" >&2; exit 1; }
[ "$RX_DELTA" -ge "$TARGET" ] || { echo "RX wraps $RX_DELTA < $TARGET" >&2; exit 1; }
echo "PASS wraparound target=$TARGET tx_delta=$TX_DELTA rx_delta=$RX_DELTA packets=$PACKETS"
ethtool -S xnic0
