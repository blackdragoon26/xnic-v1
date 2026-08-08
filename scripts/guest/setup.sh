#!/bin/sh
set -eu

if [ "$(id -u)" -ne 0 ]; then
	echo "Run with sudo" >&2
	exit 1
fi
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y build-essential "linux-headers-$(uname -r)" ethtool \
	iperf3 tcpdump pciutils sparse clang llvm make git

echo "Kernel diagnostics:"
for option in KASAN KFENCE PROVE_LOCKING DEBUG_LIST DEBUG_OBJECTS KMEMLEAK; do
	if [ -r /proc/config.gz ]; then
		zgrep -E "^CONFIG_${option}=" /proc/config.gz || true
	elif [ -r "/boot/config-$(uname -r)" ]; then
		grep -E "^CONFIG_${option}=" "/boot/config-$(uname -r)" || true
	fi
done
