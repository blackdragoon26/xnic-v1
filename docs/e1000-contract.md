# e1000 subset contract

This document is the implementation contract for XNIC. The hardware authority
is Intel's public 8254x Software Developer's Manual; the QEMU `e1000` model is
the execution target. The source was written independently of Linux's upstream
e1000 driver.

## PCI and MMIO

XNIC binds only to `8086:100e`. PCI BAR0 is mapped with `pci_iomap`; all device
registers are accessed with `readl`/`writel`. A `STATUS` read flushes posted MMIO
writes when ordering against subsequent device behavior matters.

The implemented register subset is defined in `driver/xnic_e1000.h`:

| Group | Registers | Purpose |
|---|---|---|
| global | `CTRL`, `STATUS` | reset, set-link-up, link observation |
| interrupt | `ICR`, `IMS`, `IMC`, `ITR` | cause/acknowledge, mask, moderation |
| receive | `RCTL`, `RDBAL/H`, `RDLEN`, `RDH`, `RDT` | one RX ring |
| transmit | `TCTL`, `TIPG`, `TDBAL/H`, `TDLEN`, `TDH`, `TDT` | one TX ring |
| address | `RAL0`, `RAH0`, `EERD` | station MAC and EEPROM fallback |

## Descriptor rings

Both rings contain a power-of-two number of 16-byte legacy descriptors. XNIC
accepts 16–256 entries and defaults to 64. Index increment is therefore:

```text
next = (current + 1) & (count - 1)
```

### TX ownership

1. Software owns unused descriptors between `next_to_use` and the reserved
   empty slot before `next_to_clean`.
2. Software maps an skb for `DMA_TO_DEVICE` and fills address/length/command.
3. `dma_wmb()` publishes the descriptor before the `TDT` doorbell.
4. Hardware owns it until it writes descriptor-done (`DD`).
5. Software observes `DD`, executes `dma_rmb()`, unmaps the buffer, consumes the
   skb, and advances `next_to_clean`.
6. One descriptor is always unused so full and empty states cannot alias.

### RX ownership

1. Software allocates an skb, maps its data for `DMA_FROM_DEVICE`, clears
   status, and publishes the descriptor.
2. `RDT` makes descriptors through that index available to hardware.
3. Hardware owns the descriptor until it writes `DD`.
4. Software observes `DD`, executes `dma_rmb()`, unmaps, validates `EOP`, error
   bits and length, then passes a valid skb to GRO.
5. Software installs a new mapped skb before returning the slot through `RDT`.
6. Refill failure leaves the slot unavailable and schedules serialized reset.

XNIC does not assemble multi-descriptor packets; a non-`EOP` descriptor is
counted and dropped. Jumbo frames are outside the v1 contract.

## Reset contract

Reset order is:

1. mask all interrupts;
2. disable RX and TX engines;
3. flush posted writes;
4. request global reset and poll for completion;
5. mask and acknowledge causes again;
6. program rings, MAC, RX/TX controls and link state;
7. enable NAPI, register the interrupt handler, enable interrupts, then start
   the TX queue.

Shutdown reverses externally visible activity: stop TX, mask/synchronize/free
the interrupt, disable NAPI, stop engines, drop carrier, and free DMA resources.

## Memory-ordering rationale

- `dma_wmb()` prevents the descriptor doorbell from becoming visible before
  descriptor contents.
- `dma_rmb()` prevents reads of completion-dependent descriptor/buffer state
  from being reordered before observing `DD`.
- MMIO accessors provide architecture-appropriate I/O ordering; a `STATUS` read
  flushes posted writes at explicit hardware state boundaries.
- CPU ownership of TX indices is protected by `tx_lock`; reset/open/close are
  serialized by `reset_lock` and RTNL lifecycle serialization.
