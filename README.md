# XNIC v1

XNIC is a deliberately constrained, clean-room Linux network driver for the
Intel 82540EM-compatible PCI interface exposed by QEMU's `e1000` device.

Current build/runtime truth is tracked in
[IMPLEMENTATION_STATUS.md](IMPLEMENTATION_STATUS.md); do not infer validation
from the presence of source code.

It exists to make low-level driver behavior observable and testable: MMIO,
DMA descriptor ownership, interrupt/NAPI handoff, backpressure, reset ordering,
failure cleanup, and packet-level diagnosis. It is not a replacement for the
upstream `e1000` driver and must not be used on a production system.

## Implemented scope

- PCI probe/remove and BAR0 MMIO
- one legacy RX and TX descriptor ring
- coherent descriptor DMA and streaming packet DMA
- MSI with INTx fallback
- NAPI receive processing
- TX queue stop/wake and watchdog recovery
- serialized reset work
- link state and `ethtool -S` diagnostics
- deterministic probe and one-shot RX-allocation fault injection

The intentionally excluded scope is recorded in [limitations](docs/limitations.md).

## Quick start

The host harness creates an Ubuntu ARM64 cloud image, starts QEMU with the
upstream driver initially active, and exposes SSH on port 2222:

```sh
./scripts/host/bootstrap-macos.sh
./scripts/host/fetch-guest.sh
./scripts/host/run-qemu.sh
```

Copy this repository into the guest, then:

```sh
sudo ./scripts/guest/setup.sh
make
sudo ./scripts/guest/bind-driver.sh
ip -br link
ethtool -i xnic0
ethtool -S xnic0
```

The deterministic qualification entry points are:

```sh
sudo ./scripts/guest/mixed-stress.sh 1800 18
sudo ./scripts/guest/qualification-suite.sh
```

The first command is the 30-minute HVF mixed traffic/reset/lifecycle profile.
The second rebuilds with GCC and sparse, reruns fault and recovery scenarios,
captures a PCAP, and writes timestamped raw evidence. Neither command makes a
release claim by itself; compare the output with `evidence/release-gate.md`.

`run-qemu.sh` prefers Apple HVF and falls back to TCG. It prints and records the
chosen accelerator. It locates Homebrew's AArch64 EDK2 firmware automatically;
other installations can set `XNIC_QEMU_FIRMWARE`. See
[qualification](docs/qualification.md) before making any validation claim.

## Safety

The bind script refuses any device other than Intel `8086:100e` and refuses to
touch the interface carrying the default route. QEMU uses two NICs: a VirtIO
management NIC for SSH and a separate e1000 test NIC.

## Documentation

- [Register and descriptor contract](docs/e1000-contract.md)
- [Concurrency and reset design](docs/concurrency.md)
- [Qualification protocol](docs/qualification.md)
- [Bug diary](docs/bug-diary.md)
- [Limitations and honest claims](docs/limitations.md)
- [Interview notes](docs/interview-notes.md)
- [One-page technical summary](docs/technical-summary.md)
