# Contributing to XNIC

XNIC accepts narrowly scoped changes that improve driver correctness,
observability, reproducibility, or validated hardware coverage.

## Before opening a change

1. Read `docs/e1000-contract.md`, `docs/concurrency.md`, and
   `docs/limitations.md`.
2. Keep the v1 exclusions intact unless the change includes a new contract,
   tests, and evidence for the expanded behavior.
3. Do not edit raw logs or packet captures. Generate a new timestamped evidence
   directory from exact commands.
4. Distinguish implemented code, emulated execution, and physical validation in
   every document and result.

## Local checks

```sh
make check
make docs
git diff --check
```

Kernel changes should also build with GCC and sparse against the recorded ARM64
headers and the current CI kernel headers. Runtime changes require a minimal
reproducer, expected-versus-observed output, regression coverage, and a scan for
kernel warnings. DPDK changes require the PCAP-PMD functional test at minimum.

## Review expectations

A useful change explains the device contract or invariant it affects, the
failure mode it prevents, and the evidence that distinguishes the fix from a
plausible-looking implementation. Physical-performance claims require topology,
tool versions, repetitions, raw counters, and preserved traffic artifacts.
