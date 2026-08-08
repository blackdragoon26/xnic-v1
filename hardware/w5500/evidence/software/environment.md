# Software-only environment

Recorded: 2026-08-08.

- host: Apple Silicon macOS 26.4.1
- overlay compiler: Homebrew `dtc`
- guest: Ubuntu 24.04 ARM64 under QEMU 11.0.3 with Apple HVF
- guest kernel: `6.8.0-136-generic`
- guest compiler: GCC 13.3.0
- static analyzer: sparse through kernel `C=2`

Observed software-only results:

- 10 W5500 contract-model tests passed
- DT overlay compiled without warnings after declaring the SPI child address and
  size cell geometry
- the kernel module built with GCC and passed sparse
- 100 unbound module load/unload cycles completed
- zero new kernel lines were emitted during the final lifecycle run
- a Clang attempt was blocked by GCC-only flags in the Ubuntu external-module
  headers (`-fconserve-stack` and `-fsanitize=bounds-strict`)

These results do not exercise a W5500, SPI controller, GPIO, interrupt, PHY,
Ethernet cable, or packet path.
