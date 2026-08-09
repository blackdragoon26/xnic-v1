# XNIC

XNIC is a clean-room Linux networking project built to make driver ownership,
ordering, recovery, and debugging concrete.

[![xnic-ci](https://github.com/blackdragoon26/xnic-v1/actions/workflows/ci.yml/badge.svg)](https://github.com/blackdragoon26/xnic-v1/actions/workflows/ci.yml)

**[Project overview](https://xnic-v1.vercel.app)** ·
**[Technical documentation](https://xnic-v1.vercel.app/docs)** ·
**[Implementation status](IMPLEMENTATION_STATUS.md)** ·
**[Release evidence](evidence/release-gate.md)**

Designed and developed by **[Sankalp Jha](https://github.com/blackdragoon26)**.

The primary implementation is a Linux PCI Ethernet driver in C for QEMU's
Intel 82540EM-compatible `8086:100e` interface. A separate post-v1 track adds a
W5500 SPI netdev and physical Raspberry Pi bring-up contract. The repository
distinguishes executed evidence from prepared work; source presence alone is
never treated as validation.

## Current status

| Track | Status | What the status means |
|---|---|---|
| PCI driver | **Validated in QEMU/HVF** | Traffic, wraparound, faults, lifecycle, reset, teardown, and recovery executed against the emulated device |
| DPDK forwarder | **Virtual PMD passed** | `rte_ethdev` application executed with the PCAP PMD, including partial-TX cleanup and signal shutdown |
| W5500 SPI driver | **Software preflight passed** | ARM64 build, sparse, model tests, overlay build, and unbound module lifecycle passed; no board is connected |
| ENA / EFA | **Not executed** | No cloud runtime evidence is published; the preflight creates no resources |

## What the PCI driver implements

- PCI probe/remove, BAR0 MMIO, reset, MAC discovery, and `net_device` lifecycle
- one coherent legacy RX ring and one TX ring with streaming skb DMA mappings
- interrupt acknowledgement and masking with NAPI receive processing
- descriptor ownership barriers, TX queue stop/wake, and completion cleanup
- watchdog and RX-refill recovery serialized against close and removal
- link reporting, `ethtool -S` diagnostics, tracepoints, and fault injection
- MSI allocation with INTx fallback

Deliberately excluded: jumbo frames, multiqueue/RSS, MSI-X, SR-IOV, offloads,
power management, and full 8254x-family compatibility.

## Recorded results

- 646,400/646,400 ICMP replies with zero loss
- 10,100 RX and TX descriptor-ring wraparounds
- 1,000 interface cycles and 100 complete driver rebind cycles
- 100 resets under traffic with automatic recovery and no module reload
- deterministic ring-full, RX-allocation, timeout, malformed-descriptor, and
  staged-probe-failure coverage
- 30-minute mixed traffic/reset/lifecycle run without driver error counters
- GCC and sparse builds, KFENCE-enabled runtime, tcpdump capture, and decoded
  PCAP inspection
- DPDK PCAP PMD: 20 RX, 20 TX, zero drops, plus partial-TX and SIGTERM paths

KASAN and lockdep were unavailable in the recorded guest kernel. The QEMU
device selected legacy INTx, so the MSI path is implemented but not
execution-validated. These are visible limitations, not implied passes.

## Repository map

| Path | Purpose |
|---|---|
| [`driver/`](driver/) | clean-room e1000-subset PCI driver |
| [`dpdk/`](dpdk/) | compact one-port/one-queue L2 forwarder |
| [`hardware/w5500/`](hardware/w5500/) | W5500 SPI driver, Device Tree overlay, model tests, wiring, and physical gate |
| [`scripts/`](scripts/) | reproducible QEMU, lifecycle, fault, traffic, and evidence workflows |
| [`evidence/`](evidence/) | raw logs, PCAPs, expected/observed matrix, and release decision |
| [`docs/`](docs/) | device contract, concurrency design, reproduction guide, qualification, and bug diary |
| [`cloud/`](cloud/) | read-only AWS preflight and gated ENA/EFA procedures |
| [`site/`](site/) | dependency-free project website and on-site documentation |

## Quick start

The host harness runs an Ubuntu ARM64 guest with separate management and test
NICs. The recorded path uses an Apple Silicon Mac, QEMU/HVF, 4 GiB guest RAM,
an Ed25519 SSH key, and roughly 8 GiB of free host storage.

```sh
git clone https://github.com/blackdragoon26/xnic-v1.git
cd xnic-v1
./scripts/host/bootstrap-macos.sh
./scripts/host/fetch-guest.sh
./scripts/host/run-qemu.sh
```

Leave QEMU running. In a second host terminal, copy the checkout and enter the
guest:

```sh
./scripts/host/sync-to-guest.sh
ssh -p 2222 xnic@127.0.0.1
cd ~/xnic-v1
```

Inside the guest, install the toolchain, build, and bind the driver:

```sh
sudo ./scripts/guest/setup.sh
make
sudo ./scripts/guest/bind-driver.sh
ip -br link
ethtool -i xnic0
ethtool -S xnic0
```

`run-qemu.sh` prefers Apple HVF and falls back to TCG. The bind script accepts
only `8086:100e` and refuses to touch the interface carrying the default route.
The complete prerequisites, expected output, cleanup commands, and Linux-host
notes are in [`docs/reproducing.md`](docs/reproducing.md).

## Qualification

```sh
sudo ./scripts/guest/mixed-stress.sh 1800 18
sudo ./scripts/guest/qualification-suite.sh
```

The first command runs the 30-minute HVF mixed profile. The second rebuilds,
runs static analysis and fault/recovery scenarios, captures traffic, and writes
timestamped artifacts. A command completing does not create a release claim;
the results must satisfy [`evidence/release-gate.md`](evidence/release-gate.md).

## Physical and cloud follow-up

The [W5500 lab track](hardware/w5500/README.md) is implemented and
software-qualified, but its scope, logic-analyzer, electrical, traffic, and
board-lifecycle gates remain pending hardware. Do not describe it as physical
bring-up yet.

The [cloud gate](cloud/README.md) defines cost-bounded ENA DPDK and two-node
EFA/Libfabric experiments. It creates nothing automatically, and neither path
is represented as executed until raw runtime evidence is published.

## Validation boundary

The executed evidence covers a clean-room Linux PCI Ethernet driver against
QEMU's Intel 82540EM-compatible interface: DMA rings, interrupt/NAPI processing,
backpressure, fault injection, lifecycle stress, and serialized reset recovery.

It does not establish physical-silicon behavior, production readiness,
real-NIC DPDK performance, ENA execution, or RDMA operation.

## Next validation milestones

1. Repeat the complete qualification suite on a fresh machine operated by an
   independent reproducer.
2. Run the PCI path under a KASAN/lockdep-enabled diagnostic kernel and execute
   MSI rather than legacy INTx.
3. Complete the W5500 Raspberry Pi bring-up with electrical, logic-analyzer,
   packet-capture, lifecycle, and stress artifacts.
4. Validate the DPDK forwarder against a physical PMD with a controlled traffic
   peer before publishing performance numbers.
5. Publish a signed release and short end-to-end demonstration tied to an exact
   commit and evidence manifest.

Contributions that preserve the device-contract and evidence-first approach are
welcome; see [`CONTRIBUTING.md`](CONTRIBUTING.md). Source is distributed under
GPL-2.0-only; see [`LICENSE`](LICENSE).

See the [website documentation](https://xnic-v1.vercel.app/docs) for the
readable walkthrough and the repository evidence for raw artifacts.
