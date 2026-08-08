#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if command -v shellcheck >/dev/null 2>&1; then
	find "$ROOT/scripts" -type f -name '*.sh' -exec shellcheck {} +
else
	echo "SKIP shellcheck (not installed)"
fi
for script in "$ROOT"/scripts/host/*.sh "$ROOT"/scripts/guest/*.sh; do
	sh -n "$script"
done

if [ -d "/lib/modules/$(uname -r)/build" ]; then
	make -C "$ROOT/driver"
	if command -v sparse >/dev/null 2>&1; then
		make -C "/lib/modules/$(uname -r)/build" M="$ROOT/driver" C=2 modules
	fi
else
	echo "SKIP kernel build (Linux kernel headers unavailable on this host)"
fi
