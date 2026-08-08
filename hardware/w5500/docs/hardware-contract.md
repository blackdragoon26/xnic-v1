# W5500 hardware contract

Source of truth: WIZnet W5500 Datasheet v1.1.0. The implementation was written
from the datasheet and the WIZnet-owned ioLibrary's MACRAW length convention,
not from Linux's in-tree W5100/W5500 driver.

## Electrical and SPI contract

- Logic and supply are 3.3 V for the selected WIZ850io module.
- `RSTn` and `INTn` are active low.
- Reset is asserted for at least 500 microseconds, followed by at least 1
  millisecond for PLL lock.
- SPI mode 0 is used, MSB first. The overlay starts at 4 MHz so a 24 MHz logic
  analyzer can capture six samples per clock. Speed is raised only after the
  signal-integrity capture is clean.
- Each variable-length SPI frame holds chip-select low across a two-byte offset,
  one-byte control field, and the complete data phase.
- The control field is `(BSB << 3) | (write << 2)`. Operation-mode bits remain
  zero for VDM.

## Register and memory contract

Only Socket 0 is used. It is assigned all 16 KiB of TX memory and all 16 KiB of
RX memory; sockets 1-7 receive zero. Socket 0 is opened in MACRAW mode without
the chip filter so Linux can apply its normal unicast, multicast, broadcast,
and promiscuous-mode filtering after `eth_type_trans()`.

| Block | BSB | Purpose |
|---|---:|---|
| Common registers | `0x00` | reset, MAC, PHY/link, interrupt mask, version |
| Socket 0 registers | `0x01` | command/status, interrupts, buffer sizes and pointers |
| Socket 0 TX memory | `0x02` | outgoing Ethernet frames |
| Socket 0 RX memory | `0x03` | received MACRAW records |

The 16-bit TX and RX pointers are monotonically advanced modulo 65536. Physical
buffer addressing is modulo 16 KiB. The driver splits SPI transfers that cross
the 16 KiB boundary instead of assuming one sequential transfer wraps.

`Sn_TX_FSR` and `Sn_RX_RSR` can change between their high-byte and low-byte
reads. The driver accepts a value only after two consecutive 16-bit reads
match, with a bounded retry count.

## TX ownership

1. Linux queues an skb to process context.
2. The worker observes enough stable `Sn_TX_FSR` space.
3. The frame is copied to TX memory at `Sn_TX_WR`.
4. The new write pointer is published and `SEND` is issued.
5. Ownership returns only through `SEND_OK` or `TIMEOUT` in the threaded IRQ.
6. The skb is freed and the Linux queue is woken below its low watermark.

The driver supports one hardware transmission at a time. This matches the
single MACRAW socket and keeps SEND completion ownership unambiguous.

## RX ownership

A MACRAW record begins with a two-byte big-endian length that includes the two
prefix bytes. The remaining bytes are one Ethernet frame without its FCS.

1. The threaded IRQ observes `RECV` and a stable `Sn_RX_RSR`.
2. The driver reads and validates the prefix before allocating an skb.
3. It copies exactly one frame and rejects lengths outside 14-1514 bytes.
4. It advances `Sn_RX_RD` by the complete record length.
5. `RECV` publishes the consumed pointer back to the device.
6. The skb enters the Linux network stack through `netif_rx()`.

Invalid or internally inconsistent records trigger serialized recovery rather
than leaving the hardware and driver with different pointer ownership.

## Execution-context contract

Synchronous Linux SPI calls may sleep. Consequently:

- the hard IRQ handler is empty;
- the threaded IRQ performs RX and completion register I/O;
- `ndo_start_xmit` only validates and queues an skb;
- a work item performs TX SPI I/O;
- NAPI is deliberately not used for synchronous SPI transfers;
- lifecycle and reset paths disable/synchronize IRQ, wake a waiting TX worker,
  cancel works, close/rebuild the socket, and only then reattach the netdev.

The write-only `force_reset` attribute on the bound SPI device accepts only
`1`, requires a running netdev, and schedules this same serialized recovery
path. It exists for deterministic lab qualification; it does not reset hardware
directly from the sysfs write context.
