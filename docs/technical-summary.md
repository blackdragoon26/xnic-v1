# XNIC v1 — technical summary

XNIC is a clean-room Linux PCI Ethernet driver in C targeting QEMU's Intel
82540EM-compatible `8086:100e` function. It deliberately implements one legacy
RX ring and one TX ring, no offloads, and no production-family compatibility.
The project is evidence of driver mechanics and debugging—not a claim of
physical silicon or board bring-up.

## Architecture

Probe enables the PCI memory function, owns BAR0, negotiates a 64/32-bit DMA
mask, allocates one IRQ vector, resets the device, discovers its MAC, and
registers a `net_device`. Open allocates coherent 64-entry descriptor rings and
streaming skb mappings, programs the e1000 ring registers, enables NAPI and the
IRQ, then exposes carrier and TX.

TX reserves one descriptor so full and empty indices cannot alias. Enqueue and
cleanup share `tx_lock`; a `dma_wmb()` publishes a descriptor before `TDT`, and
`dma_rmb()` orders completion state after observing `DD`. The queue stops on
the transition to full and wakes after more than one quarter of the ring is
free. RX validates `EOP`, error bits, and length before GRO delivery, then maps
a replacement buffer before returning the descriptor through `RDT`.

The interrupt handler acknowledges `ICR`, masks causes, and schedules NAPI.
NAPI owns RX processing while scheduled, enforces budget, cleans TX, and only
re-enables interrupts after successful completion. Watchdog and RX-refill
failure schedule process-context recovery. Recovery takes RTNL plus
`reset_lock`, detaches the netdev, synchronizes/free IRQ, disables NAPI, frees
DMA ownership, resets/rebuilds, and reattaches on success.

## Verified results

- QEMU 11.0.3, Apple HVF, Ubuntu ARM64, Linux 6.8.0-136-generic
- GCC 13 module build and sparse `C=2`
- ICMP, TCP, UDP, tcpdump PCAP and decoded packet inspection
- 646,400/646,400 ICMP packets, zero loss, 10,100 RX and TX wrap deltas
- 30-minute mixed traffic/reset/interface run; 100 separate traffic resets
- 1,000 interface cycles, 100 full rebind cycles, probe failures 1–5
- deterministic ring-full recovery, TX watchdog recovery, RX allocation fault
- PCI remove/rescan/rebind and guest reboot/rebind recovery
- no oops, sanitizer report, stuck queue, DMA error, or descriptor error

## Debugging result and claim boundary

The bug diary explains why ring-full state must be acted on immediately after
the producer consumes the final usable descriptor—not deferred to a future
`ndo_start_xmit` call Linux may never issue—and how a misleading ping-based
reproducer was replaced with deterministic AF_PACKET traffic.

KFENCE was enabled and clean. KASAN and lockdep were unavailable. The recorded
QEMU function used legacy interrupts, so MSI is implemented but not execution
validated. Malformed RX validation uses explicit descriptor-boundary fault
injection, not a hostile physical link. DPDK, RDMA, physical link behavior,
board bring-up, and production driver experience are not claimed.
