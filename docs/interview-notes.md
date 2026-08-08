# Interview defense notes

Be ready to draw the two rings and answer these without looking at the code:

- Why reserve one TX descriptor?
- Why is `dma_wmb()` before `TDT`, and `dma_rmb()` after observing `DD`?
- Why are descriptor and packet-buffer DMA mappings different?
- What closes the interrupt/NAPI lost-wakeup window?
- What happens when RX refill fails after consuming a packet?
- What prevents reset from freeing an skb while NAPI cleans it?
- Why can `cancel_work_sync` under RTNL deadlock?
- What does QEMU validation fail to prove about physical silicon?
- How would multiqueue change ring ownership and interrupt affinity?
- Which experiment produced the bug-diary entry, and what hypothesis was wrong?

The best answer includes an invariant, observed evidence, and the limitation of
that evidence—not merely the kernel API name.
