#!/bin/sh
set -eu

ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
PROFILE=${1:-tcg}
case "$PROFILE" in
	hvf|kvm) IF_CYCLES=1000; MOD_CYCLES=100 ;;
	tcg) IF_CYCLES=250; MOD_CYCLES=50 ;;
	*) echo "Usage: $0 {tcg|hvf|kvm}" >&2; exit 2 ;;
esac

[ "$(id -u)" -eq 0 ] || { echo "Run with sudo" >&2; exit 1; }

i=1
while [ "$i" -le "$IF_CYCLES" ]; do
	ip link set xnic0 down
	ip link set xnic0 up
	i=$((i + 1))
done

i=1
while [ "$i" -le "$MOD_CYCLES" ]; do
	"$ROOT/scripts/guest/restore-upstream.sh"
	"$ROOT/scripts/guest/bind-driver.sh"
	i=$((i + 1))
done

echo "PASS profile=$PROFILE interface_cycles=$IF_CYCLES module_cycles=$MOD_CYCLES"
