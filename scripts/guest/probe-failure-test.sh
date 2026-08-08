#!/bin/sh
set -eu

ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
. "$ROOT/scripts/guest/common.sh"
[ "$(id -u)" -eq 0 ] || { echo "Run with sudo" >&2; exit 1; }
[ -f "$ROOT/driver/xnic_e1000.ko" ] || make -C "$ROOT/driver"
BDF=$(xnic_find_bdf)
if [ "$(basename "$(readlink "/sys/bus/pci/devices/$BDF/driver")")" != e1000 ]; then
	"$ROOT/scripts/guest/restore-upstream.sh"
fi
ORIGINAL_DRIVER=$(basename "$(readlink "/sys/bus/pci/devices/$BDF/driver")")
IFACE=$(xnic_interface_for_bdf "$BDF" 2>/dev/null || true)
[ -z "$IFACE" ] || xnic_assert_not_default_route "$IFACE"

ip link set "$IFACE" down 2>/dev/null || true
echo "$BDF" > "/sys/bus/pci/devices/$BDF/driver/unbind"
echo xnic_e1000 > "/sys/bus/pci/devices/$BDF/driver_override"

stage=1
while [ "$stage" -le 5 ]; do
	rmmod xnic_e1000 2>/dev/null || true
	insmod "$ROOT/driver/xnic_e1000.ko" "fail_probe_stage=$stage"
	if echo "$BDF" > /sys/bus/pci/drivers/xnic_e1000/bind 2>/dev/null; then
		echo "Stage $stage unexpectedly bound" >&2
		exit 1
	fi
	[ ! -L "/sys/bus/pci/devices/$BDF/driver" ] || {
		echo "Stage $stage retained a driver binding" >&2; exit 1;
	}
	stage=$((stage + 1))
done

rmmod xnic_e1000 2>/dev/null || true
echo > "/sys/bus/pci/devices/$BDF/driver_override"
modprobe "$ORIGINAL_DRIVER"
echo "$BDF" > /sys/bus/pci/drivers_probe
echo "PASS probe failure stages 1..5"
