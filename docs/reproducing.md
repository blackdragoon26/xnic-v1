# Reproducing XNIC

This guide rebuilds the recorded QEMU/HVF path from a clean checkout. It does
not reproduce the pending physical W5500, ENA, EFA, or RDMA tracks.

## Host requirements

- Apple Silicon Mac with Homebrew, 4 GiB available RAM, and about 8 GiB free
  disk space for the downloaded base image and sparse guest disk
- `~/.ssh/id_ed25519.pub`, or `XNIC_SSH_PUBLIC_KEY` pointing to another public
  key
- outbound HTTPS for the verified Ubuntu 24.04 ARM64 cloud image
- QEMU with HVF; TCG works as a slower behavioral fallback

The shell scripts print the selected accelerator and record the exact QEMU
command in `.cache/qemu-command.log`.

## 1. Prepare and launch the guest

```sh
git clone https://github.com/blackdragoon26/xnic-v1.git
cd xnic-v1
./scripts/host/bootstrap-macos.sh
./scripts/host/fetch-guest.sh
./scripts/host/run-qemu.sh
```

`fetch-guest.sh` verifies the Ubuntu image against its published SHA-256
manifest before creating a 24 GiB sparse overlay. Keep this terminal running.

## 2. Copy the exact checkout

In a second host terminal:

```sh
cd xnic-v1
./scripts/host/sync-to-guest.sh
ssh -p 2222 xnic@127.0.0.1
cd ~/xnic-v1
```

The sync helper excludes `.git`, `.cache`, and previous timestamped run output,
and refuses to overwrite an existing guest directory. Set `XNIC_GUEST_DIR` for
a second clean copy, or set `XNIC_SSH_PORT` / `XNIC_GUEST_TARGET` if the
connection defaults conflict locally.

## 3. Build and bind

Inside the guest:

```sh
sudo ./scripts/guest/setup.sh
make
sudo ./scripts/guest/bind-driver.sh
ip -br link show xnic0
ethtool -i xnic0
ethtool -S xnic0
ping -c 5 10.11.0.2
```

Expected observations are a loaded `xnic_e1000` module, interface `xnic0`, PCI
ID `8086:100e`, increasing RX/TX counters, and successful traffic through
QEMU's test network. The bind helper rejects any other PCI ID and refuses to
replace the interface carrying the default route.

## 4. Run qualification

For a short functional check:

```sh
sudo ./scripts/guest/qualification-suite.sh
```

For the recorded 30-minute HVF stress profile first run:

```sh
sudo ./scripts/guest/mixed-stress.sh 1800 18
sudo ./scripts/guest/qualification-suite.sh
```

The suite writes timestamped raw logs and packet captures under
`evidence/runs/`. Compare the new run against `evidence/expected-observed.md`;
do not treat command completion alone as a pass.

## 5. Restore and stop

Inside the guest:

```sh
sudo ./scripts/guest/restore-upstream.sh
sudo poweroff
```

The QEMU host process exits after the guest powers off. Cached images remain in
`.cache/` for later runs and are intentionally excluded from version control.

## Linux hosts

The driver, model tests, and guest scripts are ordinary Linux code. The
provided host launcher is specifically pinned to an ARM64 `virt` guest and
Apple HVF/TCG. A Linux/KVM reproduction should retain the same two-NIC topology,
PCI ID, kernel/tool versions, and evidence collection, while recording any
launcher differences rather than presenting them as the baseline environment.
