import importlib.util
import pathlib
import unittest


MODEL_PATH = pathlib.Path(__file__).parents[1] / "tools" / "w5500_model.py"
SPEC = importlib.util.spec_from_file_location("w5500_model", MODEL_PATH)
model = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(model)


class ControlByteTests(unittest.TestCase):
    def test_datasheet_block_and_direction_encoding(self):
        self.assertEqual(model.control_byte(0, False), 0x00)
        self.assertEqual(model.control_byte(1, True), 0x0C)
        self.assertEqual(model.control_byte(2, True), 0x14)
        self.assertEqual(model.control_byte(3, False), 0x18)

    def test_reserved_width_is_rejected(self):
        with self.assertRaises(ValueError):
            model.control_byte(32, False)


class BufferWrapTests(unittest.TestCase):
    def test_non_wrapping_transfer(self):
        self.assertEqual(model.split_buffer_transfer(0x0100, 64), [(0x0100, 64)])

    def test_ring_boundary_is_split(self):
        self.assertEqual(
            model.split_buffer_transfer(0x3FF0, 64),
            [(0x3FF0, 16), (0, 48)],
        )

    def test_16_bit_pointer_wrap(self):
        self.assertEqual(model.advance_u16(0xFFF0, 64), 0x0030)


class StableRegisterTests(unittest.TestCase):
    def test_retries_until_consecutive_values_match(self):
        self.assertEqual(model.stable_u16([9, 10, 11, 11]), 11)

    def test_never_stable_is_error(self):
        with self.assertRaises(RuntimeError):
            model.stable_u16(range(10))


class MacrawReceiveTests(unittest.TestCase):
    def test_length_prefix_includes_itself(self):
        self.assertEqual(model.decode_macraw_prefix(b"\x00@", 64), 62)

    def test_standard_frame(self):
        self.assertEqual(model.decode_macraw_prefix(b"\x05\xec", 1516), 1514)

    def test_truncated_and_oversized_frames_are_rejected(self):
        for prefix, available in ((b"\x00\x0f", 15), (b"\x05\xed", 1517), (b"\x00@", 20)):
            with self.subTest(prefix=prefix, available=available):
                with self.assertRaises(ValueError):
                    model.decode_macraw_prefix(prefix, available)


if __name__ == "__main__":
    unittest.main()
