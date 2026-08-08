# XNIC release gate

Status: **DRIVER TESTS PASS WITH RECORDED ENVIRONMENT LIMITATIONS**

| Requirement | Status | Evidence |
|---|---|---|
| module builds against recorded kernel | pass | [GCC build](runs/20260808T080600Z/build-gcc.log), [sparse](runs/20260808T080600Z/sparse.log) |
| probe stages 1–5 unwind cleanly | pass | [stages 1–5](runs/20260808T080600Z/probe-failures.log) |
| bidirectional ICMP/UDP/TCP | pass | ICMP, sustained TCP and host-received UDP pass |
| 10,000 RX/TX ring wraparounds | pass | [646,400 packets, 10,100 wraps each](runs/20260808T080600Z/wraparound.log) |
| NAPI budget boundary cases | pass | below-budget completion and 328 exact-budget exhaustions under an above-budget stream |
| ring-full recovery | pass | [deterministic 64-entry stall/kick](runs/20260808T080600Z/tx-ring-full.log) |
| RX allocation failure recovery | pass | [one-shot failure and recovery](runs/20260808T080600Z/rx-allocation.log) |
| TX timeout/reset recovery | pass | [watchdog/reset/recovery](runs/20260808T080600Z/tx-timeout.log) |
| concurrent close/reset/traffic | pass | [30-minute HVF mixed run](runs/20260808T080600Z/mixed-stress.log) |
| lifecycle profile completed | pass | [1,000 interface, 100 rebind cycles](runs/20260808T080600Z/lifecycle-hvf.log) |
| malformed RX validation | pass | [missing EOP, error bits, impossible length; recovery](runs/20260808T080600Z/malformed-rx.log) |
| KASAN/KFENCE clean | pass | KFENCE enabled and no report; KASAN unavailable |
| lockdep clean | unavailable | guest kernel does not enable lockdep |
| runtime bug diary complete | pass | [ring-full ownership investigation](../docs/bug-diary.md) |

The [PCAP](runs/20260808T080600Z/icmp.pcap), [decoded summary](runs/20260808T080600Z/icmp-pcap-summary.txt),
and empty [kernel-alert scan](runs/20260808T080600Z/kernel-alerts.txt) accompany
the scenario logs. [Guest reboot evidence](runs/20260808T080600Z/guest-reboot.log)
also records a clean rebind and 5/5 replies. A `pass` records observed QEMU
behavior, not silicon proof.

Driver scenario gate: **PASS**. Lockdep, KASAN, MSI execution, and physical
silicon remain explicit environment limitations rather than silent passes.

DPDK remains **CLOSED** until a clean-checkout demo is recorded and reviewed;
the driver test results alone do not authorize a DPDK résumé claim.
