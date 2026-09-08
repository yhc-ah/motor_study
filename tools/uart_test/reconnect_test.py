from __future__ import annotations

import argparse
import random
import time
from typing import Any

from uart_test import (
    CMD_GET_STATS,
    CMD_GET_STATS_RSP,
    CMD_LED_SET,
    CMD_PING,
    CMD_PING_RSP,
    Expected,
    FrameDecoder,
    decode_stats,
    encode_frame,
    open_port,
    receive_expected,
    send_fragmented,
)


def reopen(port_name: str, baud: int, timeout_s: float = 20.0) -> Any:
    deadline = time.monotonic() + timeout_s
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            return open_port(port_name, baud)
        except (RuntimeError, OSError) as error:
            last_error = error
            time.sleep(0.5)
    raise RuntimeError(f"cannot reopen {port_name}: {last_error}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--cycles", type=int, default=5)
    parser.add_argument("--seed", type=int, default=20260829)
    args = parser.parse_args()

    rng = random.Random(args.seed)
    sequence = 0x70000000
    port = reopen(args.port, args.baud)
    decoder = FrameDecoder()

    try:
        for cycle in range(1, args.cycles + 1):
            partial = encode_frame(sequence, CMD_LED_SET, b"\x00\x01")
            sequence += 1
            send_fragmented(port, partial[:len(partial) // 2], rng)
            port.close()

            input(
                f"Cycle {cycle}: unplug USB-UART, wait 3 seconds, "
                "then press Enter..."
            )
            time.sleep(3.0)
            input(
                f"Cycle {cycle}: reconnect USB-UART, wait for the COM port, "
                "then press Enter..."
            )

            port = reopen(args.port, args.baud)
            decoder.clear()
            ping_sequence = sequence
            sequence += 1
            stats_sequence = sequence
            sequence += 1
            payload = f"reconnect-{cycle}".encode("ascii")
            wire = (
                encode_frame(ping_sequence, CMD_PING, payload)
                + encode_frame(stats_sequence, CMD_GET_STATS)
            )
            send_fragmented(port, wire, rng)
            frames = receive_expected(
                port,
                decoder,
                [Expected(ping_sequence, CMD_PING_RSP, payload),
                 Expected(stats_sequence, CMD_GET_STATS_RSP, None)],
                timeout_s=2.0,
            )
            stats = decode_stats(frames[1].payload)
            print(
                f"Cycle {cycle}: PASS, uptime={stats['uptime_ms']} ms, "
                f"timeouts={stats['timeout_errors']}, "
                f"valid={stats['valid_frames']}"
            )

        print("RECONNECT PASS")
        return 0
    finally:
        if port.is_open:
            port.close()


if __name__ == "__main__":
    raise SystemExit(main())
