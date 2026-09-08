from __future__ import annotations

import argparse
import json
import random
import struct
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

SOF = b"\xAA\x55"
MIN_LEN = 5
MAX_LEN = 245
MAX_PAYLOAD = 240

CMD_PING = 0x01
CMD_GET_STATS = 0x02
CMD_LED_SET = 0x03
CMD_PING_RSP = 0x81
CMD_GET_STATS_RSP = 0x82
CMD_LED_SET_RSP = 0x83
CMD_ERROR_RSP = 0xFF

STATS_FIELDS = (
    "uptime_ms",
    "rx_dma_bytes",
    "rx_event_idle",
    "rx_event_ht",
    "rx_event_tc",
    "error_ore",
    "error_fe",
    "error_ne",
    "error_pe",
    "ring_occupancy",
    "ring_high_watermark",
    "ring_overflow_count",
    "ring_dropped_bytes",
    "valid_frames",
    "crc_errors",
    "length_errors",
    "unknown_command_count",
    "timeout_errors",
    "ping_count",
    "led_set_count",
    "restart_count",
)


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc & 0xFFFF


def encode_frame(sequence: int, command: int, payload: bytes = b"") -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload too large")
    body = struct.pack("<IB", sequence & 0xFFFFFFFF, command) + payload
    length = struct.pack("<H", len(body))
    crc = crc16_modbus(length + body)
    return SOF + length + body + struct.pack("<H", crc)


@dataclass(frozen=True)
class Frame:
    sequence: int
    command: int
    payload: bytes
    raw: bytes


class FrameDecoder:
    def __init__(self) -> None:
        self.buffer = bytearray()

    def clear(self) -> None:
        self.buffer.clear()

    def feed(self, data: bytes) -> list[Frame]:
        self.buffer.extend(data)
        frames: list[Frame] = []

        while True:
            start = self.buffer.find(SOF)
            if start < 0:
                if self.buffer[-1:] == b"\xAA":
                    self.buffer[:] = b"\xAA"
                else:
                    self.buffer.clear()
                break
            if start > 0:
                del self.buffer[:start]
            if len(self.buffer) < 4:
                break

            body_length = struct.unpack_from("<H", self.buffer, 2)[0]
            if not MIN_LEN <= body_length <= MAX_LEN:
                del self.buffer[0]
                continue

            total_length = 2 + 2 + body_length + 2
            if len(self.buffer) < total_length:
                break

            raw = bytes(self.buffer[:total_length])
            del self.buffer[:total_length]
            received_crc = struct.unpack_from("<H", raw, total_length - 2)[0]
            calculated_crc = crc16_modbus(raw[2:-2])
            if received_crc != calculated_crc:
                raise RuntimeError(f"MCU response CRC error: {raw.hex(' ')}")

            sequence, command = struct.unpack_from("<IB", raw, 4)
            frames.append(Frame(sequence, command, raw[9:-2], raw))

        return frames


@dataclass(frozen=True)
class Expected:
    sequence: int
    command: int
    payload: bytes | None


def random_payload(rng: random.Random, force_header: bool = False) -> bytes:
    length = rng.randint(0, 64)
    data = bytearray(rng.getrandbits(8) for _ in range(length))
    if force_header:
        position = rng.randint(0, len(data))
        data[position:position] = SOF
    return bytes(data[:MAX_PAYLOAD])


def send_fragmented(port: Any,
                    data: bytes,
                    rng: random.Random,
                    maximum_chunk: int = 32) -> list[int]:
    position = 0
    chunks: list[int] = []
    while position < len(data):
        size = rng.randint(1, min(maximum_chunk, len(data) - position))
        written = port.write(data[position:position + size])
        if written != size:
            raise RuntimeError(f"short serial write: {written}/{size}")
        chunks.append(size)
        position += size
    port.flush()
    return chunks


def receive_expected(port: Any,
                     decoder: FrameDecoder,
                     expected: list[Expected],
                     timeout_s: float = 1.0) -> list[Frame]:
    deadline = time.monotonic() + timeout_s
    received: list[Frame] = []

    while len(received) < len(expected) and time.monotonic() < deadline:
        waiting = port.in_waiting
        data = port.read(waiting if waiting else 1)
        if data:
            received.extend(decoder.feed(data))

    if len(received) != len(expected):
        raise AssertionError(
            f"response count mismatch expected={len(expected)} "
            f"actual={len(received)}"
        )

    for actual, wanted in zip(received, expected):
        if actual.sequence != wanted.sequence:
            raise AssertionError(
                f"sequence mismatch expected={wanted.sequence} "
                f"actual={actual.sequence}"
            )
        if actual.command != wanted.command:
            raise AssertionError(
                f"command mismatch seq={actual.sequence} "
                f"expected=0x{wanted.command:02X} "
                f"actual=0x{actual.command:02X}"
            )
        if wanted.payload is not None and actual.payload != wanted.payload:
            raise AssertionError(f"payload mismatch seq={actual.sequence}")

    return received


def decode_stats(payload: bytes) -> dict[str, int]:
    if len(payload) != 4 * len(STATS_FIELDS):
        raise AssertionError(
            f"GET_STATS payload length {len(payload)} != "
            f"{4 * len(STATS_FIELDS)}"
        )
    values = struct.unpack("<" + "I" * len(STATS_FIELDS), payload)
    return dict(zip(STATS_FIELDS, values))


class TestRunner:
    def __init__(self,
                 port: Any,
                 seed: int,
                 log_path: Path,
                 maximum_chunk: int = 32) -> None:
        self.port = port
        self.rng = random.Random(seed)
        self.seed = seed
        self.decoder = FrameDecoder()
        self.sequence = 1
        self.case_index = 0
        self.maximum_chunk = maximum_chunk
        self.log_file = log_path.open("w", encoding="utf-8")

    def close(self) -> None:
        self.log_file.close()

    def next_sequence(self) -> int:
        value = self.sequence
        self.sequence = (self.sequence + 1) & 0xFFFFFFFF
        return value

    def log(self, record: dict[str, Any]) -> None:
        record["seed"] = self.seed
        record["case"] = self.case_index
        self.log_file.write(json.dumps(record, ensure_ascii=False) + "\n")
        self.log_file.flush()

    def send_case(self,
                  injection: str,
                  wire: bytes,
                  expected: list[Expected],
                  timeout_s: float = 1.0) -> list[Frame]:
        self.case_index += 1
        chunks: list[int] = []
        frames: list[Frame] = []
        try:
            chunks = send_fragmented(
                self.port, wire, self.rng, self.maximum_chunk
            )
            frames = receive_expected(
                self.port, self.decoder, expected, timeout_s
            )
        except Exception as error:
            self.log({
                "injection": injection,
                "tx_hex": wire.hex(),
                "chunks": chunks,
                "expected": [
                    {"sequence": item.sequence,
                     "command": item.command,
                     "payload_hex": (None if item.payload is None
                                     else item.payload.hex())}
                    for item in expected
                ],
                "actual": [frame.raw.hex() for frame in frames],
                "result": "FAIL",
                "error": repr(error),
            })
            raise

        self.log({
            "injection": injection,
            "tx_hex": wire.hex(),
            "chunks": chunks,
            "expected": [
                {"sequence": item.sequence, "command": item.command}
                for item in expected
            ],
            "actual": [
                {"sequence": item.sequence,
                 "command": item.command,
                 "payload_hex": item.payload.hex()}
                for item in frames
            ],
            "result": "PASS",
        })
        return frames

    def valid_ping(self, force_header: bool = False) -> None:
        sequence = self.next_sequence()
        payload = random_payload(self.rng, force_header)
        self.send_case(
            "valid_ping_with_AA55" if force_header else "valid_ping",
            encode_frame(sequence, CMD_PING, payload),
            [Expected(sequence, CMD_PING_RSP, payload)],
        )

    def glued_valid_frames(self) -> None:
        count = self.rng.randint(2, 8)
        wire = bytearray()
        expected: list[Expected] = []
        for _ in range(count):
            sequence = self.next_sequence()
            payload = random_payload(self.rng, self.rng.random() < 0.1)
            wire.extend(encode_frame(sequence, CMD_PING, payload))
            expected.append(Expected(sequence, CMD_PING_RSP, payload))
        self.send_case("glued_valid_frames", bytes(wire), expected, 2.0)

    def get_stats(self) -> dict[str, int]:
        sequence = self.next_sequence()
        frames = self.send_case(
            "get_stats",
            encode_frame(sequence, CMD_GET_STATS),
            [Expected(sequence, CMD_GET_STATS_RSP, None)],
        )
        return decode_stats(frames[0].payload)

    def led_set(self) -> None:
        sequence = self.next_sequence()
        payload = bytes((self.rng.randrange(3), self.rng.randrange(2)))
        self.send_case(
            "valid_led_set",
            encode_frame(sequence, CMD_LED_SET, payload),
            [Expected(sequence, CMD_LED_SET_RSP, payload)],
        )

    def bad_crc_then_recovery(self) -> None:
        bad = bytearray(
            encode_frame(self.next_sequence(), CMD_LED_SET, b"\x00\x01")
        )
        bad[-1] ^= 0x01
        sequence = self.next_sequence()
        payload = b"recover-crc"
        self.send_case(
            "bad_crc_then_valid",
            bytes(bad) + encode_frame(sequence, CMD_PING, payload),
            [Expected(sequence, CMD_PING_RSP, payload)],
        )

    def illegal_length_then_recovery(self) -> None:
        malformed = SOF + b"\x00\x00"
        sequence = self.next_sequence()
        payload = b"recover-length"
        self.send_case(
            "illegal_length_then_valid",
            malformed + encode_frame(sequence, CMD_PING, payload),
            [Expected(sequence, CMD_PING_RSP, payload)],
        )

    def truncated_then_recovery(self) -> None:
        self.case_index += 1
        full = encode_frame(
            self.next_sequence(), CMD_LED_SET, b"\x00\x01"
        )
        truncated = full[:len(full) // 2]
        chunks1 = send_fragmented(
            self.port, truncated, self.rng, self.maximum_chunk
        )
        time.sleep(0.15)
        sequence = self.next_sequence()
        payload = b"recover-timeout"
        recovery = encode_frame(sequence, CMD_PING, payload)
        chunks2 = send_fragmented(
            self.port, recovery, self.rng, self.maximum_chunk
        )
        frames = receive_expected(
            self.port, self.decoder,
            [Expected(sequence, CMD_PING_RSP, payload)]
        )
        self.log({
            "injection": "truncated_then_valid",
            "tx_hex": (truncated + recovery).hex(),
            "chunks": chunks1 + chunks2,
            "expected": [{"sequence": sequence,
                          "command": CMD_PING_RSP}],
            "actual": [{"sequence": frames[0].sequence,
                        "command": frames[0].command}],
            "result": "PASS",
        })

    def unknown_then_recovery(self) -> None:
        unknown_sequence = self.next_sequence()
        recovery_sequence = self.next_sequence()
        payload = b"recover-unknown"
        self.send_case(
            "unknown_then_valid",
            encode_frame(unknown_sequence, 0x40, b"unknown")
            + encode_frame(recovery_sequence, CMD_PING, payload),
            [Expected(unknown_sequence, CMD_ERROR_RSP, None),
             Expected(recovery_sequence, CMD_PING_RSP, payload)],
        )

    def noise_then_recovery(self) -> None:
        noise = bytes(
            value for value in
            (self.rng.getrandbits(8) for _ in range(self.rng.randint(1, 32)))
            if value != 0xAA
        ) or b"\x00"
        sequence = self.next_sequence()
        payload = b"recover-noise"
        self.send_case(
            "noise_then_valid",
            noise + encode_frame(sequence, CMD_PING, payload),
            [Expected(sequence, CMD_PING_RSP, payload)],
        )


def open_port(name: str, baud: int) -> Any:
    try:
        import serial
    except ImportError as error:
        raise RuntimeError(
            "pyserial is required: python -m pip install -r requirements.txt"
        ) from error

    port = serial.Serial(
        port=name,
        baudrate=baud,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=0.01,
        write_timeout=1.0,
        rtscts=False,
        dsrdtr=False,
        xonxoff=False,
    )
    port.reset_input_buffer()
    port.reset_output_buffer()
    return port


def run_smoke(runner: TestRunner) -> None:
    runner.valid_ping()
    runner.valid_ping(force_header=True)
    runner.glued_valid_frames()
    runner.led_set()
    runner.bad_crc_then_recovery()
    runner.illegal_length_then_recovery()
    runner.truncated_then_recovery()
    runner.unknown_then_recovery()
    runner.noise_then_recovery()
    print(json.dumps(runner.get_stats(), indent=2))


def run_random(runner: TestRunner, cases: int) -> None:
    choices = (
        "ping", "stats", "led", "bad_crc", "bad_length",
        "truncated", "unknown", "noise", "glued"
    )
    weights = (70, 8, 5, 4, 3, 3, 3, 2, 2)

    for index in range(cases):
        kind = runner.rng.choices(choices, weights=weights, k=1)[0]
        if kind == "ping":
            runner.valid_ping(force_header=runner.rng.random() < 0.1)
        elif kind == "stats":
            runner.get_stats()
        elif kind == "led":
            runner.led_set()
        elif kind == "bad_crc":
            runner.bad_crc_then_recovery()
        elif kind == "bad_length":
            runner.illegal_length_then_recovery()
        elif kind == "truncated":
            runner.truncated_then_recovery()
        elif kind == "unknown":
            runner.unknown_then_recovery()
        elif kind == "noise":
            runner.noise_then_recovery()
        else:
            runner.glued_valid_frames()

        if (index + 1) % 100 == 0:
            print(f"random progress: {index + 1}/{cases}")

    print(json.dumps(runner.get_stats(), indent=2))


def run_dma_lengths(runner: TestRunner) -> None:
    lengths = (1, 10, 127, 128, 255, 256, 257, 600)
    for total_payload in lengths:
        before = runner.get_stats()
        wire = bytearray()
        expected: list[Expected] = []
        offset = 0
        while offset < total_payload:
            count = min(MAX_PAYLOAD, total_payload - offset)
            payload = bytes((index & 0xFF)
                            for index in range(offset, offset + count))
            sequence = runner.next_sequence()
            wire.extend(encode_frame(sequence, CMD_PING, payload))
            expected.append(Expected(sequence, CMD_PING_RSP, payload))
            offset += count
        runner.send_case(
            f"dma_length_{total_payload}", bytes(wire), expected, 3.0
        )
        after = runner.get_stats()
        stats_request_size = len(encode_frame(0, CMD_GET_STATS))
        rx_delta = after["rx_dma_bytes"] - before["rx_dma_bytes"]
        expected_rx_delta = len(wire) + stats_request_size
        if rx_delta != expected_rx_delta:
            raise AssertionError(
                f"DMA length {total_payload}: expected RX delta "
                f"{expected_rx_delta}, actual {rx_delta}"
            )
        overflow_delta = (
            after["ring_overflow_count"] - before["ring_overflow_count"]
        )
        if overflow_delta != 0:
            raise AssertionError(
                f"DMA length {total_payload}: ring overflow delta "
                f"{overflow_delta}"
            )
        print(json.dumps({
            "payload_bytes": total_payload,
            "wire_bytes": len(wire),
            "expected_rx_delta": expected_rx_delta,
            "rx_dma_delta": rx_delta,
            "idle_delta": after["rx_event_idle"]
                          - before["rx_event_idle"],
            "ht_delta": after["rx_event_ht"] - before["rx_event_ht"],
            "tc_delta": after["rx_event_tc"] - before["rx_event_tc"],
            "overflow_delta": overflow_delta,
        }))


def run_stress(runner: TestRunner, target_bytes: int) -> None:
    before = runner.get_stats()
    sent = 0
    while sent < target_bytes:
        wire = bytearray()
        expected: list[Expected] = []
        for _ in range(8):
            sequence = runner.next_sequence()
            payload = struct.pack("<I", sequence) + b"stress"
            encoded = encode_frame(sequence, CMD_PING, payload)
            wire.extend(encoded)
            expected.append(Expected(sequence, CMD_PING_RSP, payload))
        runner.send_case("stress_batch", bytes(wire), expected, 3.0)
        sent += len(wire)
        if sent % 10000 < len(wire):
            print(f"stress bytes: {sent}/{target_bytes}")

    after = runner.get_stats()
    print(json.dumps(after, indent=2))
    for key in (
        "error_ore", "error_fe", "error_ne", "error_pe",
        "ring_overflow_count", "ring_dropped_bytes"
    ):
        delta = after[key] - before[key]
        if delta != 0:
            raise AssertionError(f"{key} delta is not zero: {delta}")


def run_waveform(port: Any, baud: int) -> None:
    one_byte_seconds = 10.0 / baud
    block_seconds = 1000.0 * one_byte_seconds
    port.write(b"\x55")
    port.flush()
    time.sleep(max(0.1, one_byte_seconds * 5.0))
    started = time.perf_counter()
    written = port.write(b"\x55" * 1000)
    port.flush()
    elapsed = time.perf_counter() - started
    if written != 1000:
        raise RuntimeError(f"short serial write: {written}/1000")
    print(
        f"baud={baud}, theoretical byte={one_byte_seconds * 1e6:.3f} us, "
        f"theoretical 1000 bytes={block_seconds * 1e3:.3f} ms, "
        f"host elapsed={elapsed * 1e3:.3f} ms"
    )
    print("Use the logic analyzer measurement, not host elapsed, for grading.")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", help="for example COM5")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--seed", type=int, default=20260829)
    parser.add_argument(
        "--mode",
        choices=("smoke", "random", "dma", "stress", "waveform"),
        default="smoke",
    )
    parser.add_argument("--cases", type=int, default=10000)
    parser.add_argument("--stress-bytes", type=int, default=100000)
    parser.add_argument("--max-chunk", type=int, default=32)
    parser.add_argument("--log", type=Path,
                        default=Path("uart-test-log.jsonl"))
    parser.add_argument("--list-ports", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.self_test:
        assert crc16_modbus(b"123456789") == 0x4B37
        decoder = FrameDecoder()
        expected = encode_frame(1, CMD_PING, b"AA\xAA\x55ZZ")
        assert decoder.feed(expected)[0].payload == b"AA\xAA\x55ZZ"
        print("SELF-TEST PASS")
        return 0
    if args.list_ports:
        try:
            from serial.tools import list_ports
        except ImportError as error:
            raise RuntimeError("pyserial is required") from error
        for item in list_ports.comports():
            print(f"{item.device}: {item.description}")
        return 0
    if not args.port:
        print("--port is required", file=sys.stderr)
        return 2
    if args.max_chunk < 1:
        print("--max-chunk must be >= 1", file=sys.stderr)
        return 2

    port = open_port(args.port, args.baud)
    if args.mode == "waveform":
        try:
            run_waveform(port, args.baud)
            return 0
        finally:
            port.close()

    runner = TestRunner(port, args.seed, args.log, args.max_chunk)
    try:
        if args.mode == "smoke":
            run_smoke(runner)
        elif args.mode == "random":
            run_random(runner, args.cases)
        elif args.mode == "dma":
            run_dma_lengths(runner)
        else:
            run_stress(runner, args.stress_bytes)
        print("PASS")
        return 0
    finally:
        runner.close()
        port.close()


if __name__ == "__main__":
    raise SystemExit(main())
