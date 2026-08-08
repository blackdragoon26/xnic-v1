#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CACHE="$ROOT/.cache"
BASE_URL=https://cloud-images.ubuntu.com/noble/current
IMAGE=noble-server-cloudimg-arm64.img

mkdir -p "$CACHE"
curl -fL --retry 3 -o "$CACHE/SHA256SUMS" "$BASE_URL/SHA256SUMS"
curl -fL --retry 3 -o "$CACHE/$IMAGE.download" "$BASE_URL/$IMAGE"

EXPECTED=$(awk -v name="*$IMAGE" '$2 == name || $2 == substr(name, 2) { print $1; exit }' "$CACHE/SHA256SUMS")
if [ -z "$EXPECTED" ]; then
	echo "Could not locate $IMAGE in signed checksum manifest" >&2
	exit 1
fi
ACTUAL=$(shasum -a 256 "$CACHE/$IMAGE.download" | awk '{print $1}')
if [ "$EXPECTED" != "$ACTUAL" ]; then
	echo "Image checksum mismatch" >&2
	exit 1
fi
mv "$CACHE/$IMAGE.download" "$CACHE/$IMAGE"

if [ ! -f "$CACHE/xnic-guest.qcow2" ]; then
	qemu-img create -f qcow2 -F qcow2 -b "$CACHE/$IMAGE" \
		"$CACHE/xnic-guest.qcow2" 24G
fi
echo "Verified guest image: $CACHE/$IMAGE"
