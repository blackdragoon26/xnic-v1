# Qualification protocol

No release claim is valid until `evidence/release-gate.md` contains commands,
timestamps, versions, and linked raw output.

| Profile | Mixed stress | Interface cycles | Module cycles | Reset-under-traffic |
|---|---:|---:|---:|---:|
| HVF/KVM | 30 minutes | 1,000 | 100 | 100 |
| TCG | 15 minutes | 250 | 50 | 50 |

Both profiles require 10,000 RX and TX ring wraparounds, probe failure stages
1–5, NAPI below/at/above budget, ring-full recovery, RX allocation failure,
timeout recovery, and concurrent close/reset/traffic.

The TCG profile is behavioral coverage at reduced repetition, not equivalent
performance validation.

## Diagnostic kernel

Prefer KASAN or KFENCE, lockdep, debug lists/objects, and kmemleak. Record every
unavailable facility rather than silently omitting it. Build with GCC and Clang
where headers/toolchains permit; run sparse separately.

## Release gate

- no oops, sanitizer report, lock warning, leaked allocation or stuck queue;
- required scenarios have raw logs and exact commands;
- the bug diary contains at least one runtime-reproduced investigation;
- contract, concurrency notes, limitations and expected/observed matrix agree;
- a clean checkout builds and executes the demo.

Failure keeps the project in driver work. It does not authorize a DPDK résumé
claim.
