#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
required='README.md docs/e1000-contract.md docs/concurrency.md docs/qualification.md docs/bug-diary.md docs/limitations.md docs/interview-notes.md docs/technical-summary.md evidence/release-gate.md'
for file in $required; do
	[ -s "$ROOT/$file" ] || { echo "Missing or empty: $file" >&2; exit 1; }
done
echo "Documentation set complete"
