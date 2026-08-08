#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CACHE="$ROOT/.cache"
SEED="$CACHE/seed"
PUBKEY=${XNIC_SSH_PUBLIC_KEY:-$HOME/.ssh/id_ed25519.pub}

if [ ! -f "$PUBKEY" ]; then
	echo "SSH public key not found at $PUBKEY" >&2
	echo "Set XNIC_SSH_PUBLIC_KEY or create an ed25519 key." >&2
	exit 1
fi
mkdir -p "$SEED"
KEY=$(sed 's/[\\&|]/\\&/g' "$PUBKEY")
sed "s|@SSH_PUBLIC_KEY@|$KEY|" "$ROOT/scripts/host/user-data.in" > "$SEED/user-data"
cp "$ROOT/scripts/host/meta-data" "$SEED/meta-data"

rm -f "$CACHE/seed.iso"
hdiutil makehybrid -quiet -iso -joliet -default-volume-name cidata \
	-o "$CACHE/seed.iso" "$SEED"
echo "Created $CACHE/seed.iso"
