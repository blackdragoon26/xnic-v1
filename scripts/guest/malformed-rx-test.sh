#!/bin/sh
set -eu

ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
. "$ROOT/scripts/guest/common.sh"
[ "$(id -u)" -eq 0 ] || { echo "Run with sudo" >&2; exit 1; }
BDF=$(xnic_find_bdf)
INJECT="/sys/bus/pci/devices/$BDF/xnic_rx_malformed"
[ -w "$INJECT" ] || { echo "Missing malformed RX injector" >&2; exit 1; }

NON_EOP_BEFORE=$(ethtool -S xnic0 | awk '/rx_non_eop:/ {print $2}')
ERROR_BEFORE=$(ethtool -S xnic0 | awk '/rx_descriptor_errors:/ {print $2}')

for mode in 1 2 3; do
	echo "$mode" > "$INJECT"
	# The injected reply is expected to be dropped.
	ping -I xnic0 -c 1 -W 1 10.11.0.2 >/dev/null 2>&1 || true
done

NON_EOP_AFTER=$(ethtool -S xnic0 | awk '/rx_non_eop:/ {print $2}')
ERROR_AFTER=$(ethtool -S xnic0 | awk '/rx_descriptor_errors:/ {print $2}')
[ $((NON_EOP_AFTER - NON_EOP_BEFORE)) -eq 1 ]
[ $((ERROR_AFTER - ERROR_BEFORE)) -eq 2 ]
ping -I xnic0 -c 5 -W 2 10.11.0.2 >/dev/null
echo "PASS malformed_rx non_eop=$NON_EOP_BEFORE->$NON_EOP_AFTER descriptor_errors=$ERROR_BEFORE->$ERROR_AFTER recovery=5/5"
ethtool -S xnic0
