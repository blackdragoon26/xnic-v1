# XNIC W5500 physical-lab track

This directory is the post-v1 bridge from QEMU PCI driver work to an actual
board-level interface. It contains an independent Linux SPI Ethernet driver for
the W5500's MACRAW mode, a Raspberry Pi Device Tree overlay, pure contract
tests, and a physical qualification protocol.

Current status: **source implemented; physical hardware validation pending**.
The code must not be described as physically validated until the qualification
gate is complete.

## Why this is separate from the PCI driver

The original XNIC uses PCI MMIO, DMA, interrupts, and NAPI. The W5500 is an SPI
peripheral with internal packet memory. Linux synchronous SPI calls may sleep,
so RX runs in a threaded interrupt and TX in a work item. This is a different
hardware/software boundary, not a cosmetic port.

## Build-only checks

On a Linux board or guest with kernel headers:

```sh
make -C hardware/w5500/driver
python3 -m unittest discover -s hardware/w5500/tests -p 'test_*.py'
./hardware/w5500/scripts/build-overlay.sh
```

The overlay binds only to `xnic,w5500-lab`; it does not claim compatibility
with or replace Linux's upstream W5100-family driver.

Read these before wiring anything:

- [hardware contract](docs/hardware-contract.md)
- [procurement decision](docs/procurement.md)
- [physical lab bring-up](docs/lab-bringup.md)
- [qualification gate](docs/qualification.md)

The lab starts at 4 MHz SPI for observable captures. Faster rates are an
experiment performed only after clean scope and logic-analyzer evidence.
