#!/usr/bin/env python3
"""Emit a deterministic L2 burst for TX ring/backpressure tests."""

import socket
import sys


def mac_bytes(text: str) -> bytes:
    return bytes.fromhex(text.replace(":", ""))


iface = sys.argv[1] if len(sys.argv) > 1 else "xnic0"
count = int(sys.argv[2]) if len(sys.argv) > 2 else 4096
frame_length = int(sys.argv[3]) if len(sys.argv) > 3 else 60
if frame_length < 14:
    raise SystemExit("Ethernet frame length must be at least 14")

with open(f"/sys/class/net/{iface}/address", encoding="ascii") as source:
    src = mac_bytes(source.read().strip())

# Locally administered unicast destination.  QEMU may discard it after DMA,
# which is fine: this helper tests host-side descriptor ownership/backpressure.
dst = mac_bytes("02:00:00:00:00:01")
frame = dst + src + bytes.fromhex("88b5") + bytes(frame_length - 14)

sock = socket.socket(socket.AF_PACKET, socket.SOCK_RAW)
sock.bind((iface, 0))
for _ in range(count):
    sock.send(frame)
