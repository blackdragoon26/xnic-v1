#!/bin/sh
set -eu

ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
PORT=${XNIC_SSH_PORT:-2222}
TARGET=${XNIC_GUEST_TARGET:-xnic@127.0.0.1}
GUEST_DIR=${XNIC_GUEST_DIR:-xnic-v1}

case "$GUEST_DIR" in
	''|*[!A-Za-z0-9._-]*)
		echo "XNIC_GUEST_DIR must be one safe directory name" >&2
		exit 1
		;;
esac

ARCHIVE=$(mktemp "${TMPDIR:-/tmp}/xnic-source.XXXXXX")
cleanup()
{
	rm -f "$ARCHIVE"
}
trap cleanup EXIT HUP INT TERM

# Prevent macOS metadata from becoming noisy, irrelevant PAX records in Linux.
export COPYFILE_DISABLE=1
tar -C "$ROOT" \
	--no-xattrs \
	--exclude='./.cache' \
	--exclude='./.git' \
	--exclude='./evidence/runs' \
	-cf "$ARCHIVE" .

ssh -p "$PORT" "$TARGET" \
	"set -eu; target=\$HOME/$GUEST_DIR; \
	if [ -e \"\$target\" ]; then \
		echo \"refusing to overwrite existing \$target; set XNIC_GUEST_DIR\" >&2; \
		exit 1; \
	fi; \
	mkdir -p \"\$target\"; tar -xf - -C \"\$target\"" \
	< "$ARCHIVE"

echo "Copied source to $TARGET:~/$GUEST_DIR"
