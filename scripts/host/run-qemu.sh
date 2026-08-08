#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CACHE="$ROOT/.cache"
DISK="$CACHE/xnic-guest.qcow2"
SEED="$CACHE/seed.iso"
LOG="$CACHE/qemu-command.log"
QEMU=${QEMU:-qemu-system-aarch64}
FIRMWARE=${XNIC_QEMU_FIRMWARE:-}

if [ ! -f "$DISK" ]; then
	echo "Missing $DISK; run scripts/host/fetch-guest.sh" >&2
	exit 1
fi
if [ ! -f "$SEED" ]; then
	"$ROOT/scripts/host/make-seed.sh"
fi

if [ -z "$FIRMWARE" ]; then
	for candidate in \
		/opt/homebrew/share/qemu/edk2-aarch64-code.fd \
		/usr/local/share/qemu/edk2-aarch64-code.fd \
		/usr/share/qemu-efi-aarch64/QEMU_EFI.fd \
		/usr/share/AAVMF/AAVMF_CODE.fd; do
		if [ -f "$candidate" ]; then
			FIRMWARE=$candidate
			break
		fi
	done
fi
if [ -z "$FIRMWARE" ] || [ ! -f "$FIRMWARE" ]; then
	echo "AArch64 UEFI firmware not found; set XNIC_QEMU_FIRMWARE" >&2
	exit 1
fi

ACCEL=tcg
CPU=max
if "$QEMU" -accel help 2>/dev/null | grep -q '^hvf$' &&
   [ "${XNIC_FORCE_TCG:-0}" != 1 ]; then
	ACCEL=hvf
	CPU=host
fi

set -- "$QEMU" \
	-machine "virt,accel=$ACCEL" -cpu "$CPU" -smp 4 -m 4096 \
	-bios "$FIRMWARE" \
	-drive "if=virtio,format=qcow2,file=$DISK" \
	-drive "if=virtio,format=raw,readonly=on,file=$SEED" \
	-netdev "user,id=mgmt,net=10.10.0.0/24,dhcpstart=10.10.0.15,hostfwd=tcp::2222-:22" \
	-device "virtio-net-pci,netdev=mgmt,mac=52:54:00:10:00:02" \
	-netdev "user,id=test,net=10.11.0.0/24,dhcpstart=10.11.0.15" \
	-device "e1000,netdev=test,mac=52:54:00:11:00:02" \
	-nographic

{
	date -u '+timestamp=%Y-%m-%dT%H:%M:%SZ'
	echo "accelerator=$ACCEL"
	echo "cpu=$CPU"
	echo "firmware=$FIRMWARE"
	printf 'command='
	printf '%s ' "$@"
	printf '\n'
} > "$LOG"
echo "XNIC accelerator: $ACCEL (recorded in $LOG)"
echo "SSH after cloud-init: ssh -p 2222 xnic@127.0.0.1"
exec "$@"
