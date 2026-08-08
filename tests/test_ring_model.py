import unittest


class Ring:
    """Executable model of the one-reserved-entry XNIC TX invariant."""

    def __init__(self, count: int):
        if count < 2 or count & (count - 1):
            raise ValueError("count must be a power of two")
        self.count = count
        self.use = 0
        self.clean = 0

    @property
    def unused(self) -> int:
        return (self.clean - self.use - 1) & (self.count - 1)

    def enqueue(self) -> int:
        if self.unused == 0:
            raise BufferError("full")
        current = self.use
        self.use = (self.use + 1) & (self.count - 1)
        return current

    def complete(self) -> int:
        if self.clean == self.use:
            raise BufferError("empty")
        current = self.clean
        self.clean = (self.clean + 1) & (self.count - 1)
        return current


class RingInvariantTests(unittest.TestCase):
    def test_empty_has_count_minus_one_usable_entries(self):
        self.assertEqual(Ring(16).unused, 15)

    def test_full_and_empty_never_alias(self):
        ring = Ring(16)
        slots = [ring.enqueue() for _ in range(15)]
        self.assertEqual(slots, list(range(15)))
        self.assertEqual(ring.unused, 0)
        self.assertNotEqual(ring.use, ring.clean)
        with self.assertRaises(BufferError):
            ring.enqueue()

    def test_wraparound_preserves_fifo_ownership(self):
        ring = Ring(16)
        for expected in range(10_000):
            slot = ring.enqueue()
            self.assertEqual(slot, expected & 15)
            self.assertEqual(ring.complete(), slot)
            self.assertEqual(ring.unused, 15)

    def test_backpressure_recovers_after_completion(self):
        ring = Ring(16)
        for _ in range(15):
            ring.enqueue()
        with self.assertRaises(BufferError):
            ring.enqueue()
        ring.complete()
        ring.enqueue()
        self.assertEqual(ring.unused, 0)


if __name__ == "__main__":
    unittest.main()
