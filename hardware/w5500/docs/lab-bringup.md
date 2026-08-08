# Physical lab bring-up

Status: **hardware not yet connected; none of these gates are claimed**.

## Recommended lab bill of materials

- Raspberry Pi 4B or 5 with Raspberry Pi OS 64-bit and matching kernel headers
- WIZnet WIZ850io module (W5500, magnetics, and RJ45 on a documented 3.3 V module)
- female-to-female jumpers and a short known-good Ethernet cable
- second Linux host or USB Ethernet adapter as the traffic peer
- 8-channel, 24 MHz-or-faster sigrok-compatible logic analyzer
- two-channel oscilloscope with 100 MHz-or-better bandwidth and 10x probes
- optional bench supply with current limiting for power measurements

Use a documented module rather than an unknown marketplace board whose voltage
regulator and level shifting are unclear. Never drive a bare W5500 with 5 V
logic.

## Raspberry Pi wiring

| Pi physical pin | BCM signal | WIZ850io |
|---:|---|---|
| 17 | 3.3 V | 3.3 V |
| 20 | ground | ground |
| 19 | GPIO10 / MOSI | MOSI |
| 21 | GPIO9 / MISO | MISO |
| 23 | GPIO11 / SCLK | SCLK |
| 24 | GPIO8 / CE0 | SCSn |
| 22 | GPIO25 | INTn |
| 18 | GPIO24 | RSTn |

Confirm the exact module pinout before power is applied. With power removed,
check ground continuity and verify there is no short between 3.3 V and ground.

## Bring-up sequence

1. Boot the Pi without the module. Record board model, OS, kernel, kernel config,
   and installed tool versions.
2. Build the module against the running kernel and compile the DT overlay.
3. Power down completely, wire the module, and perform continuity checks.
4. Connect oscilloscope ground to board ground. Probe 3.3 V and RSTn for the
   first powered boot. Verify rail stability, at least 500 microseconds reset
   low, and a clean release.
5. Load the overlay and module at 4 MHz SPI. Read `VERSIONR`; the only accepted
   value is `0x04`.
6. Capture SCLK, MOSI, MISO, CS, INT, and RESET with the logic analyzer. Decode
   mode 0 and confirm address/control/data framing against the register log.
7. Bring the netdev up with a static address. Verify carrier transitions, ARP,
   bidirectional ping, UDP, TCP, broadcast, and packet capture on both ends.
8. Exercise link removal, interface cycling, module removal, TX saturation,
   100 writes to the bound SPI device's `force_reset` attribute under traffic,
   and intentional SPI speed reduction/increase.
9. Run qualification under KASAN/KFENCE and lockdep when the board kernel
   supports them. Preserve dmesg, ethtool counters, PCAPs, scope screenshots,
   sigrok sessions, and the exact source commit.

## Oscilloscope checks

- 3.3 V droop and ripple during link negotiation and sustained traffic
- RSTn low pulse and rise time
- SCLK overshoot, ringing, and edge integrity at 4 MHz and the chosen final rate
- CS setup/hold around the three-byte header and data phase
- INTn low assertion and deassertion after RCW1 status clear

The scope is evidence about the electrical interface; the logic analyzer is
evidence about protocol framing. Neither substitutes for the other.
