#!/bin/sh
set -eu

ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
. "$ROOT/scripts/guest/common.sh"

[ "$(id -u)" -eq 0 ] || { echo "Run with sudo" >&2; exit 1; }
[ -f "$ROOT/driver/xnic_e1000.ko" ] || make -C "$ROOT/driver"
BDF=$(xnic_find_bdf) || { echo "Intel 8086:100e not found" >&2; exit 1; }
IFACE=$(xnic_interface_for_bdf "$BDF" 2>/dev/null || true)
[ -z "$IFACE" ] || xnic_assert_not_default_route "$IFACE"
ip link set "$IFACE" down 2>/dev/null || true

rmmod xnic_e1000 2>/dev/null || true
insmod "$ROOT/driver/xnic_e1000.ko"
echo xnic_e1000 > "/sys/bus/pci/devices/$BDF/driver_override"
if [ -L "/sys/bus/pci/devices/$BDF/driver" ]; then
	echo "$BDF" > "/sys/bus/pci/devices/$BDF/driver/unbind"
fi
echo "$BDF" > /sys/bus/pci/drivers/xnic_e1000/bind
# Predictable-name udev rules may rename eth0 asynchronously after probe.
# Wait for those rules, rediscover the current name from PCI sysfs, then apply
# the stable test-only name used by the qualification scripts.
udevadm settle
NEW_IFACE=$(xnic_interface_for_bdf "$BDF")
if [ "$NEW_IFACE" != xnic0 ]; then
	ip link set "$NEW_IFACE" down
	ip link set "$NEW_IFACE" name xnic0
fi
ip link set xnic0 up

if command -v dhclient >/dev/null 2>&1; then
	dhclient -1 xnic0 || true
else
	ip addr add 10.11.0.15/24 dev xnic0 2>/dev/null || true
fi
echo "Bound $BDF to xnic_e1000 as xnic0"
ethtool -i xnic0
