# Conditional DPDK forwarder

This compact DPDK 23.11 application uses one `rte_ethdev` port and one RX/TX
queue, a mempool, burst I/O, L2 address swapping, partial-TX cleanup, counters,
and signal-safe shutdown. It exists only because the XNIC driver scenario gate
passed. It is not an ENA result.

Build and exercise the PCAP PMD in Linux:

```sh
make -C dpdk
sudo ./dpdk/xnic-forwarder --no-huge --no-pci \
  --vdev='net_pcap0,rx_pcap=evidence/runs/20260808T080600Z/icmp.pcap,tx_pcap=/tmp/xnic-dpdk-output.pcap' \
  -- -p 0 -i 100
tcpdump -nn -r /tmp/xnic-dpdk-output.pcap
```

`-i 0` (the default) runs until SIGINT/SIGTERM. A positive value exits after
that many consecutive one-millisecond empty polls, which makes finite-PCAP
tests reproducible. `-t N` limits how many packets from a received burst are
offered to the PMD; the remainder exercises the same unsent-mbuf cleanup and
drop accounting used when `rte_eth_tx_burst()` returns partially. It is a
deterministic test control, not a PMD-performance claim.
