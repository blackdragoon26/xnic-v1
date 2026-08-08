# Expected versus observed matrix

Last updated: 2026-08-08. Raw files are under `runs/20260808T080600Z/`.

| Scenario | Expected | Observed |
|---|---|---|
| bind/open | `8086:100e` registers as `xnic0` | pass; legacy interrupt selected |
| ICMP | bidirectional traffic without persistent loss | pass; 646,400/646,400 flood replies |
| UDP | host receives guest payload | pass; `xnic-udp-validation` received |
| TCP | sustained stream completes | pass; 64 MiB guest-to-host stream |
| TX/RX wrap | indices wrap without ownership corruption | pass; 10,100 wraps each, 646,400 packets, 0% loss |
| NAPI burst | budget exhaustion reschedules safely | pass; 328 exhaustions during TCP stream |
| malformed RX descriptors | invalid EOP/error/length is dropped, then traffic recovers | pass; 1 non-EOP, 2 descriptor errors, 5/5 recovery |
| ring full | queue stops then wakes after completions | pass; deterministic stall/kick regression |
| RX allocation failure | descriptor withheld, serialized rebuild recovers | pass; failure 0→1, reset 2→3 |
| TX watchdog | timeout schedules serialized rebuild | pass; timeout 0→1, reset 2→3 |
| probe failure 1–5 | reverse-order cleanup permits immediate rebind | pass |
| reset under traffic | carrier and packets recover without reload | pass; 100 cycles |
| lifecycle | no leak/warning across repeated close and rebind | pass; 1,000/100 HVF profile |
| PCI disappearance | remove callback cleans up; rescan/rebind restores traffic | pass; remove/rescan/rebind/ping recovered |
| guest reboot | clean boot, rebind, and test-network recovery | pass; 5/5 replies after reboot |
| mixed 30-minute HVF | reset/close/traffic concurrency remains live | pass; 95 iterations, 107 cumulative resets, no error counters |
| KFENCE | no report on diagnostic kernel | pass for observed runs; KASAN unavailable |
| lockdep | no inversion report | unavailable in guest kernel; no claim |
| Clang external module | warning-clean build | unavailable: GCC-only kernel-header flags |
