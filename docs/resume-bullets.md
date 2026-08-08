# Defensible résumé bullets

- Developed a clean-room Linux PCI Ethernet driver in C for an Intel
  82540EM-compatible QEMU interface, implementing BAR0 MMIO initialization,
  DMA descriptor rings, interrupt-driven NAPI, TX backpressure, ethtool
  telemetry, and serialized watchdog/reset recovery.
- Qualified 10,100 RX/TX ring wraps with 646,400 packets at zero loss, 100
  reset-under-traffic cycles, 1,000 interface cycles, 100 driver rebinds, and
  staged probe/runtime faults; debugged ownership and recovery using kernel
  logs, sparse, tcpdump, and Wireshark-compatible PCAPs under KFENCE.
- Built a DPDK 23.11 single-port L2 forwarder using `rte_ethdev`, mempool-backed
  bursts, partial-TX cleanup, counters, and signal-safe shutdown; validated the
  functional path with the `net_pcap` virtual PMD (20 RX/20 TX, zero drops).

Do not rewrite the DPDK line as ENA, hardware PMD, zero-copy throughput, or
kernel-bypass performance. Do not claim physical silicon, board bring-up,
KASAN/lockdep, MSI execution, RDMA, or production-driver experience.
