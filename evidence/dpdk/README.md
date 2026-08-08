# DPDK execution evidence

Environment: Ubuntu 24.04 ARM64 guest, DPDK 23.11.4, `net_pcap` virtual PMD.

- `normal.log`: 20 RX, 20 TX, zero drops
- `output.pcap`: forwarded output containing all 20 input frames
- `partial.log`: deterministic offer limit produced 4 TX and freed/counted 16
- `partial.pcap`: the four transmitted frames
- `signal.log`: SIGTERM-controlled shutdown with zero outstanding traffic

These artifacts validate the application and PCAP PMD path. They do not prove
ENA, a physical NIC PMD, throughput, zero-copy behavior, or cloud performance.
