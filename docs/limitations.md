# Limitations and claim boundary

XNIC is educational software exercised against QEMU's emulated 82540EM device.
It is not production-ready and has not yet been validated on physical silicon.

Not implemented:

- physical board bring-up or electrical validation
- jumbo frames or multi-descriptor receive assembly
- checksum/segmentation/VLAN/timestamp offloads
- multiqueue, RSS, MSI-X, SR-IOV, power management
- EEPROM variants beyond the QEMU-compatible read path
- multicast filter programming beyond default broadcast/unicast behavior
- live ring resizing
- compatibility with the full 8254x family

Safe public wording is “clean-room Linux PCI Ethernet driver against an Intel
82540EM-compatible interface in QEMU.” Do not claim physical hardware, board
bring-up, production deployment, or upstream-driver parity.

DPDK and RDMA are separate gates. Their names must not appear in résumé project
bullets until corresponding raw execution evidence exists.

The QEMU user-mode network used by the reproducible harness does not expose a
physical carrier control or a host TAP endpoint. Software interface down/up,
PCI remove/rescan, and device reset are covered; electrical link transitions
and hostile malformed RX injection are not represented as validated. The QEMU
82540EM function selected a legacy interrupt in the recorded run, so the MSI
allocation path is implemented but not execution-validated.
