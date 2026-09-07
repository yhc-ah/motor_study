import random
import sys
import unittest
from pathlib import Path

TOOL_DIR = Path(__file__).resolve().parents[2] / "tools" / "uart_test"
sys.path.insert(0, str(TOOL_DIR))

from uart_test import FrameDecoder, crc16_modbus, encode_frame, random_payload


class UartToolTests(unittest.TestCase):
    def test_crc_standard_vector(self) -> None:
        self.assertEqual(crc16_modbus(b"123456789"), 0x4B37)

    def test_embedded_header_and_glued_frames(self) -> None:
        decoder = FrameDecoder()
        wire = (
            encode_frame(1, 0x01, b"left\xAA\x55right")
            + encode_frame(2, 0x03, b"\x01\x01")
        )
        frames = []
        for byte in wire:
            frames.extend(decoder.feed(bytes((byte,))))
        self.assertEqual([frame.sequence for frame in frames], [1, 2])
        self.assertEqual(frames[0].payload, b"left\xAA\x55right")

    def test_invalid_length_resynchronizes(self) -> None:
        decoder = FrameDecoder()
        valid = encode_frame(9, 0x01, b"recovery")
        frames = decoder.feed(b"noise\xAA\x55\x00\x00" + valid)
        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0].sequence, 9)

    def test_fixed_seed_is_reproducible(self) -> None:
        first = random.Random(20260829)
        second = random.Random(20260829)
        self.assertEqual(
            [random_payload(first, True) for _ in range(20)],
            [random_payload(second, True) for _ in range(20)],
        )


if __name__ == "__main__":
    unittest.main()
