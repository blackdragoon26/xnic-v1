#!/bin/sh
set -eu

ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
required='README.md docs/e1000-contract.md docs/concurrency.md docs/qualification.md docs/bug-diary.md docs/limitations.md docs/interview-notes.md docs/technical-summary.md docs/resume-bullets.md evidence/release-gate.md evidence/dpdk/README.md hardware/w5500/README.md hardware/w5500/docs/hardware-contract.md hardware/w5500/docs/procurement.md hardware/w5500/docs/lab-bringup.md hardware/w5500/docs/qualification.md hardware/w5500/docs/bug-diary.md hardware/w5500/evidence/README.md hardware/w5500/evidence/software/environment.md hardware/w5500/evidence/software/arm64-build-sparse.log hardware/w5500/evidence/software/model-overlay.log hardware/w5500/evidence/software/module-lifecycle.log cloud/README.md cloud/evidence/README.md site/index.html site/docs/index.html site/theme.js'
for file in $required; do
	[ -s "$ROOT/$file" ] || { echo "Missing or empty: $file" >&2; exit 1; }
done
[ -f "$ROOT/hardware/w5500/evidence/software/module-lifecycle-kernel.log" ] || {
	echo "Missing: hardware/w5500/evidence/software/module-lifecycle-kernel.log" >&2
	exit 1
}
echo "Documentation set complete"
