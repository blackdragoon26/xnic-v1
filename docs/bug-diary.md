# Bug diary

This is a live evidence document, not a prewritten success story. Entries must
come from observed behavior and retain the failing log or trace under
`evidence/`.

## Runtime investigation: ring-full transition was not observable

### Symptom and minimal reproduction

With the test-only TX doorbell stall enabled, a burst was queued until Linux
stopped `xnic0`, but `ethtool -S xnic0` did not reliably report a
`tx_ring_full` transition. The first reproducer used flood ping; the corrected
reproducer is `scripts/guest/tx-ring-full-test.sh`, which emits 4,096 raw L2
frames into the deliberately stalled 64-entry ring.

### Expected and observed behavior

The invariant is that one descriptor remains unused, and consuming the final
usable descriptor must atomically stop the netdev queue. The original code
only diagnosed a full ring when `ndo_start_xmit` was entered with no free
descriptor. Linux can honor the preceding queue stop without calling the
driver again, so the transition itself was absent from diagnostics.

### Evidence

The initial test emitted “TX ring did not reach full state.” The final
regression reports a positive transition and then completes queued traffic;
the observed run advanced `tx_ring_full` and completed 64 TX wraparounds with
zero TX DMA errors. Raw failing and passing output belongs in the curated run
directory before this entry is considered release evidence.

### Initial hypothesis

The first hypothesis was that descriptor completion or the queue wake path was
racing the deliberate stall.

### Rejected or corrected hypothesis

Only seven interrupts occurred in the flood-ping reproducer. The neighbor was
resolved, but ping's own outstanding-request behavior still did not create a
deterministic descriptor burst. Replacing it with an AF_PACKET raw-L2 sender
separated traffic-generation flow control from driver backpressure.

### Root cause

The driver checked the ownership invariant before enqueue but did not record
and act on the transition to zero usable descriptors immediately after
advancing `tx_next_to_use`. Full state was therefore defined by a future call
that the networking core was not required to make.

### Fix and synchronization rationale

After publishing the descriptor with `dma_wmb()` and advancing the producer,
the driver now checks `xnic_tx_unused()` while still holding `tx_lock`. At zero
it increments the diagnostic and calls `netif_stop_queue()`. Cleanup evaluates
the same producer/consumer state under the same lock and wakes only above the
quarter-ring threshold. The lock makes the stop/wake decision consistent with
descriptor ownership; the DMA barrier makes the descriptor visible before a
doorbell can expose the producer index to the device.

### Regression test

`sudo ./scripts/guest/tx-ring-full-test.sh` stalls doorbells, sends 4,096 raw
frames, waits for the full transition, releases the doorbell, waits for the
sender, and proves recovery with five ICMP replies.

### Remaining limitations

The stall is deterministic fault injection, not physical PCIe congestion. It
validates software ownership and backpressure sequencing against QEMU but does
not establish timing behavior on silicon.

## Build investigation: Linux 6.17 removed the legacy IRQ flag name

### Symptom and minimal reproduction

GitHub Actions run `31255245869` failed while compiling `xnic_e1000.c` against
Ubuntu's Linux 6.17 Azure headers: `PCI_IRQ_LEGACY` was undeclared at the
`pci_alloc_irq_vectors()` call. The same source still built against the ARM64
Linux 6.8 headers used for runtime qualification.

### Expected and observed behavior

The driver requests one vector and permits MSI with an INTx fallback. That
policy is unchanged, but the obsolete spelling made the source dependent on an
older kernel API.

### Initial and corrected hypothesis

The first suspicion was an Ubuntu header configuration difference. Inspection
of upstream Linux v6.17 `include/linux/pci.h` instead showed the supported flag
is `PCI_IRQ_INTX`; `PCI_IRQ_LEGACY` is absent.

### Root cause and fix

The driver used an old compatibility name rather than the API's explicit INTx
flag. Replacing it with `PCI_IRQ_INTX` preserves identical allocation semantics
and avoids a version conditional.

### Regression test and limitations

The fix builds with GCC and sparse against ARM64 Linux 6.8, and CI compiles it
against x86-64 Linux 6.17 headers. Header compatibility does not substitute for
executing both MSI and INTx fallback paths on physical hardware.

## Entry template

### Symptom and minimal reproduction

Record the smallest command sequence that fails.

### Expected and observed behavior

State the invariant and the exact deviation.

### Evidence

Link dmesg, trace, register dump, PCAP, or sanitizer report.

### Initial hypothesis

Write what was believed before changing code.

### Rejected or corrected hypothesis

Show the observation that invalidated it.

### Root cause

Identify ownership, ordering, lifetime, or device-contract failure precisely.

### Fix and synchronization rationale

Explain why the fix closes the race rather than merely hiding the symptom.

### Regression test

Give a deterministic test and its expected signal.

### Remaining limitations

State what the test does not establish.

## Design-review finding: RTNL/work cancellation deadlock

During static concurrency review, `ndo_stop` originally called
`cancel_work_sync(reset_work)` while network lifecycle operations normally hold
RTNL. The reset worker acquires RTNL before recovery. If queued recovery were
waiting for RTNL, close would wait for the worker while holding the lock the
worker required.

The initial assumption was that synchronously cancelling recovery in close was
the safest way to protect freed rings. Lock-order analysis rejected that
assumption. `ndo_stop` now performs the serialized down path without cancelling
work. A later worker observes `!netif_running()` under RTNL and exits. Removal
cancels work only after `unregister_netdev` completes. This is a design-review
finding; it must not be presented as a runtime-reproduced bug unless a lockdep
trace is captured.
