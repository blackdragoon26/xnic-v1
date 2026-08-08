#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DTC=${DTC:-dtc}

command -v "$DTC" >/dev/null 2>&1 || {
	echo "dtc is required" >&2
	exit 1
}

"$DTC" -@ -I dts -O dtb \
	-o "$ROOT/dts/xnic-w5500.dtbo" \
	"$ROOT/dts/xnic-w5500-overlay.dts"
echo "built $ROOT/dts/xnic-w5500.dtbo"
