# W5500 bug diary

## SPI modalias warning during module lifecycle

### Symptom and reproduction

The ARM64 Linux module compiled and sparse passed, but each unbound
`insmod`/`rmmod` cycle wrote this warning:

```text
SPI driver xnic_w5500 has no spi_device_id for xnic,w5500-lab
```

The minimal reproducer was one module load with no SPI device present. A
100-cycle run made the warning deterministic.

### Expected versus observed

Expected: registering an unbound SPI driver is silent and its module alias can
match the project Device Tree compatible.

Observed: the driver registered and unloaded successfully, but the SPI core
reported that the OF compatible had no corresponding `spi_device_id`.

### Initial hypothesis

The initial suspicion was that the module lacked an OF alias despite
`MODULE_DEVICE_TABLE(of, ...)`.

### Rejected hypotheses

`modinfo` showed both generated OF aliases. The OF table was present and was
not the missing metadata.

The first fix added the complete `xnic,w5500-lab` string to the SPI ID table.
The warning remained for another 100-cycle run, proving that the SPI core did
not compare the complete OF string against that table.

### Root cause

The OF compatible is `xnic,w5500-lab`, while the SPI ID table contained only
`xnic-w5500-lab`. Inspection of upstream `drivers/spi/spi.c` showed that Linux
strips the vendor prefix up to the comma and searches the SPI ID table for the
remaining `w5500-lab` name. Neither attempted entry matched that suffix.

### Fix and rationale

Add `w5500-lab` to the SPI ID table while retaining the hyphenated ID for
explicit non-DT board registration. This follows the SPI core's vendor-prefix
rule without weakening the driver to bind to WIZnet's generic upstream
compatible.

### Regression test

Rebuild, clear the relevant dmesg cursor, perform repeated unbound module
load/unload cycles, and require zero new `xnic_w5500` warnings. The final raw
software-only result belongs in `hardware/w5500/evidence/software/`.

### Remaining limitation

This validates driver registration metadata only. It does not exercise SPI
probe, register I/O, interrupts, traffic, or physical teardown.
