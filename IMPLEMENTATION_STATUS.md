# Implementation status

Last updated: 2026-08-08 (Ubuntu 24.04.4 ARM64 guest, Linux 6.8.0-136-generic,
QEMU 11.0.3 with Apple HVF).

## Implemented and runtime-verified

- clean-room 82540EM-compatible PCI driver, BAR0 MMIO, reset and MAC discovery
- one 64-entry RX/TX ring with coherent descriptors and streaming packet DMA
- legacy interrupt handling, NAPI RX/TX cleanup, queue stop/wake and watchdog
- serialized reset recovery, ethtool diagnostics and controlled fault injection
- QEMU guest binding and clean restoration of the upstream `e1000` driver
- bidirectional ICMP, sustained TCP, and host-received UDP packet movement
- 646,400-packet ICMP run with zero loss and 10,100 RX/TX wrap deltas
- above-budget NAPI operation (328 budget exhaustions under sustained TCP)
- deterministic TX ring-full stop/wake recovery
- deterministic TX watchdog followed by automatic reset and traffic recovery
- one-shot RX allocation failure followed by reset and traffic recovery
- 100 reset-under-traffic cycles plus a 30-minute mixed HVF run
- 1,000 interface cycles and 100 complete driver rebind cycles
- staged probe failures 1–5 with successful reverse-order cleanup
- one-shot missing-EOP, descriptor-error, and impossible-length RX rejection
- GCC module build and sparse `C=2` analysis

No oops, warning, KFENCE report, stuck queue, or driver error counter was
observed in the completed runs. The guest kernel has KFENCE enabled; KASAN and
lockdep are not enabled and therefore are not claimed.

## Environment limitations and remaining delivery work

- KASAN and lockdep require a different diagnostic kernel
- recorded QEMU execution used legacy interrupts; MSI remains unexecuted
- the scripted 5–7 minute clean-checkout demonstration has not been recorded

Clang cannot build against this Ubuntu kernel's GCC-configured external-module
headers because they inject GCC-only `-fconserve-stack` and
`-fsanitize=bounds-strict`. This is recorded as a toolchain/kernel-header
limitation, not represented as a successful Clang build.

The exact baseline commit passed a clean-clone guest build, sparse analysis,
bind, packet, ring-full, and malformed-RX flow. A compact DPDK 23.11
`rte_ethdev` forwarder subsequently passed the `net_pcap` PMD with 20/20
packets, deterministic partial-TX cleanup, counters, and SIGTERM shutdown.
ENA and physical-PMD execution remain pending; RDMA remains outside v1.
