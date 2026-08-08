#!/bin/sh
set -eu

if [ "$(uname -s)" != Darwin ]; then
	echo "This bootstrap is for macOS; install QEMU through your OS package manager." >&2
	exit 1
fi
if ! command -v brew >/dev/null 2>&1; then
	echo "Homebrew is required: https://brew.sh" >&2
	exit 1
fi

if ! command -v qemu-system-aarch64 >/dev/null 2>&1; then
	brew install qemu
fi

qemu-system-aarch64 --version | sed -n '1p'
if qemu-system-aarch64 -accel help 2>/dev/null | grep -q '^hvf$'; then
	echo "HVF advertised: yes"
else
	echo "HVF advertised: no; scripts will use TCG"
fi
