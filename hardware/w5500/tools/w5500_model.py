"""Pure model of the W5500 contracts that do not require hardware."""

MEMORY_SIZE = 16 * 1024
MEMORY_MASK = MEMORY_SIZE - 1
ETH_HLEN = 14
ETH_FRAME_LEN = 1514


def control_byte(block: int, write: bool) -> int:
    if not 0 <= block <= 0x1F:
        raise ValueError("block must fit BSB[4:0]")
    return (block << 3) | (0x04 if write else 0)


def split_buffer_transfer(pointer: int, length: int):
    """Return (offset, length) SPI transfers for one 16 KiB ring access."""
    if length <= 0 or length > MEMORY_SIZE:
        raise ValueError("length must be between 1 and 16 KiB")
    offset = pointer & MEMORY_MASK
    first = min(length, MEMORY_SIZE - offset)
    segments = [(offset, first)]
    if first != length:
        segments.append((0, length - first))
    return segments


def stable_u16(samples, retries=8):
    """Model the datasheet's repeat-until-two-equal size-register read."""
    values = iter(samples)
    try:
        previous = next(values)
    except StopIteration as exc:
        raise ValueError("at least two samples are required") from exc
    for attempt, current in enumerate(values):
        if attempt >= retries:
            break
        if current == previous:
            return current
        previous = current
    raise RuntimeError("size register did not stabilize")


def decode_macraw_prefix(prefix: bytes, available: int) -> int:
    """Return Ethernet-frame length; W5500 prefix length includes its two bytes."""
    if len(prefix) != 2:
        raise ValueError("MACRAW prefix is exactly two bytes")
    packet_length = int.from_bytes(prefix, "big")
    frame_length = packet_length - 2
    if packet_length > available:
        raise ValueError("packet exceeds received-size snapshot")
    if not ETH_HLEN <= frame_length <= ETH_FRAME_LEN:
        raise ValueError("invalid Ethernet frame length")
    return frame_length


def advance_u16(pointer: int, amount: int) -> int:
    return (pointer + amount) & 0xFFFF
