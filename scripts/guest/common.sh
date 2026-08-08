#!/bin/sh

xnic_find_bdf() {
	for dev in /sys/bus/pci/devices/*; do
		[ -r "$dev/vendor" ] || continue
		[ "$(cat "$dev/vendor")" = 0x8086 ] || continue
		[ "$(cat "$dev/device")" = 0x100e ] || continue
		basename "$dev"
		return 0
	done
	return 1
}
xnic_interface_for_bdf() {
	BDF=$1
	set -- "/sys/bus/pci/devices/$BDF/net/"*
	[ -e "$1" ] || return 1
	basename "$1"
}

xnic_assert_not_default_route() {
	IFACE=$1
	DEFAULT_IFACE=$(ip route show default | awk 'NR == 1 { for (i=1; i<=NF; i++) if ($i == "dev") print $(i+1) }')
	if [ -n "$DEFAULT_IFACE" ] && [ "$IFACE" = "$DEFAULT_IFACE" ]; then
		echo "Refusing to rebind default-route interface $IFACE" >&2
		return 1
	fi
}
