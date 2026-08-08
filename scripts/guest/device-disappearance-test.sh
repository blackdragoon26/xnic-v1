#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
. "$ROOT/scripts/guest/common.sh"
[ "$(id -u)" -eq 0 ] || { echo "Run with sudo" >&2; exit 1; }
BDF=$(xnic_find_bdf)

echo 1 > "/sys/bus/pci/devices/$BDF/remove"
[ ! -e "/sys/bus/pci/devices/$BDF" ] || {
	echo "PCI function remained after remove" >&2
	exit 1
}
echo 1 > /sys/bus/pci/rescan

deadline=$(( $(date +%s) + 10 ))
until xnic_find_bdf >/dev/null 2>&1; do
	[ "$(date +%s)" -lt "$deadline" ] || {
		echo "PCI function did not return after rescan" >&2
		exit 1
	}
	sleep 1
done
"$ROOT/scripts/guest/bind-driver.sh" >/tmp/xnic-device-rebind.log
ping -I xnic0 -c 5 -W 2 10.11.0.2 >/dev/null
echo "PASS device_disappearance bdf=$BDF rescan_and_traffic_recovered"
