# Verified environment

Captured 2026-08-08.

- host: Apple Silicon macOS
- emulator: QEMU 11.0.3
- accelerator: Apple HVF (`-machine virt,accel=hvf -cpu host`)
- guest: Ubuntu 24.04.4 LTS ARM64
- guest kernel: Linux 6.8.0-136-generic
- test PCI function: Intel `8086:100e` (QEMU `e1000` / 82540EM)
- guest resources: 4 vCPU, 4 GiB RAM
- management NIC: VirtIO on 10.10.0.0/24 with host SSH forwarding
- test NIC: e1000 on isolated QEMU user network 10.11.0.0/24
- compiler: GCC 13.3.0
- diagnostics available: KFENCE
- diagnostics unavailable in this guest kernel: KASAN, lockdep

The recorded host launch selected HVF; this qualification therefore uses the
HVF repetition profile. QEMU exposed a legacy interrupt for the e1000 function.
The driver contains MSI allocation/fallback logic, but MSI execution is not
claimed from this run.

Clang external-module compilation is unavailable with the installed Ubuntu
kernel headers: its generated flags contain GCC-only `-fconserve-stack` and
`-fsanitize=bounds-strict`. GCC compilation and sparse `C=2` are the verified
static build paths.
