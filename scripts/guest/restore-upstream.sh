#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
. "$ROOT/scripts/guest/common.sh"
[ "$(id -u)" -eq 0 ] || { echo "Run with sudo" >&2; exit 1; }
BDF=$(xnic_find_bdf)
ip link set xnic0 down 2>/dev/null || true
if [ -L "/sys/bus/pci/devices/$BDF/driver" ]; then
	echo "$BDF" > "/sys/bus/pci/devices/$BDF/driver/unbind"
fi
echo > "/sys/bus/pci/devices/$BDF/driver_override"
rmmod xnic_e1000 2>/dev/null || true
modprobe e1000
echo "$BDF" > /sys/bus/pci/drivers_probe
echo "Restored upstream driver for $BDF"
