# Concurrency and recovery design

## Contexts

| Path | Context | Serialization |
|---|---|---|
| transmit enqueue | networking softirq/process caller | `tx_lock` |
| TX completion | NAPI softirq | `tx_lock` |
| RX completion | NAPI softirq | single NAPI instance |
| interrupt | hard IRQ | device interrupt mask + NAPI state |
| open/close | RTNL process context | RTNL + `reset_lock` |
| recovery | workqueue process context | RTNL + `reset_lock` |

## Interrupt-to-NAPI handoff

The interrupt handler reads `ICR`, which acknowledges asserted causes. If NAPI
can be scheduled, it masks device interrupts before calling `__napi_schedule`.
NAPI cleans TX and receives up to its budget. On budget exhaustion it remains
scheduled and interrupts remain masked. Otherwise `napi_complete_done` changes
the NAPI state before `IMS` re-enables causes. A cause asserted after completion
therefore produces a new interrupt rather than a lost wakeup.

The networking core may call poll with a zero budget for TX-only cleanup. XNIC
cleans TX but performs no RX work and does not call `napi_complete_done()` in
that case, as required by the NAPI contract.

## TX ring invariant

One entry is reserved. `next_to_use == next_to_clean` means empty; advancing use
to one entry before clean means full. Both enqueue and cleanup hold `tx_lock`,
so reset cannot encounter partially published CPU indices after TX has been
stopped and the interrupt/NAPI paths have been synchronized.

## Reset state machine

```text
RUNNING -> DETACHED -> IRQ_QUIESCED -> NAPI_QUIESCED -> HW_STOPPED
        -> DMA_FREED -> HW_RESET -> RINGS_CONFIGURED -> RUNNING
```

The watchdog and RX-refill failure only schedule work. Recovery executes in
process context, acquires RTNL and `reset_lock`, detaches the netdev, performs a
full down/up cycle, and reattaches only on success. Close does not synchronously
cancel reset work while holding RTNL; doing so would deadlock against recovery
waiting for RTNL. Device removal cancels work after `unregister_netdev` returns.

## Failure cleanup

Probe has one owner for every acquired resource and unwind labels release them
in reverse order: NAPI, IRQ vector, BAR mapping, netdev, PCI regions, PCI device.
Runtime ring allocation uses the same principle and tolerates partially filled
RX arrays.
