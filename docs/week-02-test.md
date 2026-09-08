# Week 02 UART test report

## Configuration under test

- Board: EmbedFire/Wildfire STM32F407 Batianhu V2, STM32F407ZGTx.
- Toolchain: STM32CubeMX project, Keil MDK-ARM 5 with ARMCC 5.06 update 7.
- Firmware link: USART1, PA9 TX, PA10 RX, 115200 8N1, no flow control.
- RX: DMA2 Stream2 Channel 4, circular 256-byte DMA buffer.
- Software ring: 1024 bytes, single producer/single consumer, drop-new policy.
- Test seed: 20260829.

## Automated verification completed

| Check | Result | Evidence |
|---|---|---|
| Ring boundary/order/overflow policy | PASS | `test_byte_ring` |
| CRC standard vector | PASS (`0x4B37`) | `test_crc16` |
| Embedded `AA 55`, CRC rejection, timeout, glued frames | PASS | `test_frame_parser` |
| PING/GET_STATS/LED_SET/error dispatch | PASS | `test_command_dispatch` |
| PC codec/resync/fixed-seed behavior | PASS (4 tests) | `test_uart_tool.py` |
| Keil full firmware build | PASS | 0 errors, 0 warnings |
| Keil static maximum stack | PASS | 216 bytes + untraceable function-pointer paths |

Run all host checks with `tests\host\run_tests.ps1`. Build the target
`stm32-realtime-bsp` in Keil after regenerating from CubeMX.

## UART polling loopback

The polling implementation is preserved in Git commit `294dab9`. It calls
`BSP_UART_PollingEchoTask()` from the main loop with a 2 ms receive timeout.
The final firmware intentionally does not call it because polling RX and DMA RX
must never compete for USART1.

| Input | Expected | Actual | Status |
|---|---|---|---|
| `Hello` | `Hello` | PENDING | PENDING HARDWARE |

## UART waveform and throughput

| Baud | Test | Theoretical | Measured | Error | Status |
|---:|---|---:|---:|---:|---|
| 9600 | one `0x55` byte | 1041.667 us | PENDING | PENDING | PENDING HARDWARE |
| 9600 | 1000 bytes | 1041.667 ms | PENDING | PENDING | PENDING HARDWARE |
| 115200 | one `0x55` byte | 86.806 us | PENDING | PENDING | PENDING HARDWARE |
| 115200 | 1000 bytes | 86.806 ms | PENDING | PENDING | PENDING HARDWARE |

## UART ReceiveToIdle circular DMA

Run `uart_test.py --port COMx --mode dma --seed 20260829`. Each row sends
ordered PING payload bytes; the tool checks every echoed payload, RX byte delta,
and overflow delta. Event counts are observational because IDLE may coincide
with half/full positions.

| Payload bytes | Final bytes/order | Duplicate | IDLE delta | HT delta | TC delta | Status |
|---:|---|---|---:|---:|---:|---|
| 1 | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING HARDWARE |
| 10 | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING HARDWARE |
| 127 | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING HARDWARE |
| 128 | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING HARDWARE |
| 255 | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING HARDWARE |
| 256 | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING HARDWARE |
| 257 | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING HARDWARE |
| 600 | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING HARDWARE |

`Size` is treated as the current DMA write position, not as the number of new
bytes. `old_pos` selects either one linear span or two spans across wraparound.

## Software ring boundary tests

| Case | Expected | Actual | Overflow delta | Result |
|---|---|---|---:|---|
| Write 0 | 0 written | 0 | 0 | PASS |
| Write 1 | 1 written/read | 1 | 0 | PASS |
| Write 1023 | preserve order | preserved | 0 | PASS |
| Wrap write | preserve order | preserved | 0 | PASS |
| Fill 1024 | occupancy 1024 | 1024 | 0 | PASS |
| Write one more | reject new byte | rejected | 1 | PASS |
| Wrap read | preserve order | preserved | 0 | PASS |

For the live occupancy test, set the exported Watch variable
`app_uart_pause_request_ms` to 50 in the Keil debugger and transmit data.
`high_watermark` must rise without normal overflow.
For deliberate flooding, pause for longer than the approximately 88.9 ms needed
to fill 1024 bytes at 115200 8N1; overflow counters must rise and a later PING
must resynchronize.

## Protocol tests

Run `uart_test.py --port COMx --mode smoke --seed 20260829` and visually confirm
the valid LED_SET action. The script covers valid PING, embedded header bytes,
glued frames, valid LED_SET, bad-CRC LED_SET, illegal length, truncated frame,
unknown command, noise, and recovery after every injected fault.

| Test | Automated host result | Board result |
|---|---|---|
| CRC `123456789` | PASS | PENDING |
| Payload contains `AA 55` | PASS | PENDING |
| Two glued frames | PASS | PENDING |
| Bad CRC cannot dispatch LED_SET | PASS | PENDING visual confirmation |
| Unknown command remains stable | PASS | PENDING |

## Random, stress, and disconnect tests

| Test | Required | Result |
|---|---|---|
| Random fragmented/fault stream | 10000 cases, seed 20260829 | PENDING HARDWARE |
| Continuous normal traffic | at least 100000 RX bytes | PENDING HARDWARE |
| UART ORE/FE/NE/PE delta | 0/0/0/0 | PENDING HARDWARE |
| Ring overflow/drop delta | 0/0 | PENDING HARDWARE |
| Disconnect/reconnect | 5 cycles, no reset/reflash | PENDING HARDWARE |
| Old partial frame after reconnect | never executed | PENDING HARDWARE |
| SysTick/main loop after reconnect | uptime continues | PENDING HARDWARE |

The current host had no present CH340 COM device during automated work, so no
physical measurements are claimed. Fill the PENDING cells from the saved JSONL
logs and logic-analyzer capture. Create `week-02-pass` only after every hardware
acceptance item passes.
