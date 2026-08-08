#!/bin/sh
set -eu

ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
OVERLAY="$ROOT/dts/xnic-w5500.dtbo"
BOOT_OVERLAYS=/boot/firmware/overlays
CONFIG=/boot/firmware/config.txt

if [ "$(id -u)" -ne 0 ]; then
	echo "run as root on the Raspberry Pi" >&2
	exit 1
fi
if [ ! -d /proc/device-tree ] || ! grep -qa "Raspberry Pi" /proc/device-tree/model; then
	echo "refusing: this is not a detected Raspberry Pi" >&2
	exit 1
fi
if [ ! -f "$OVERLAY" ]; then
	echo "missing $OVERLAY; run build-overlay.sh first" >&2
	exit 1
fi
if [ ! -d "$BOOT_OVERLAYS" ] || [ ! -f "$CONFIG" ]; then
	echo "Raspberry Pi boot firmware paths were not found" >&2
	exit 1
fi

install -m 0644 "$OVERLAY" "$BOOT_OVERLAYS/xnic-w5500.dtbo"
grep -qxF 'dtparam=spi=on' "$CONFIG" || printf '\ndtparam=spi=on\n' >> "$CONFIG"
grep -qxF 'dtoverlay=xnic-w5500' "$CONFIG" || printf 'dtoverlay=xnic-w5500\n' >> "$CONFIG"

echo "overlay installed; reboot is required"
