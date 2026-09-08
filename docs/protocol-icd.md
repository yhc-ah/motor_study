# UART frame protocol ICD

## Transport

- USART1 on PA9/PA10 through jumpers J80/J81 and the on-board CH340.
- Default link: 115200 baud, 8 data bits, no parity, 1 stop bit, no flow control.
- Multi-byte integers use little-endian byte order.

## Frame format

| Offset | Size | Field | Description |
|---:|---:|---|---|
| 0 | 1 | SOF1 | `0xAA` |
| 1 | 1 | SOF2 | `0x55` |
| 2 | 2 | LEN | `SEQ + CMD + PAYLOAD`, range 5..245 |
| 4 | 4 | SEQ | Request sequence number |
| 8 | 1 | CMD | Command or response identifier |
| 9 | 0..240 | PAYLOAD | Command-specific bytes |
| 9+N | 2 | CRC | CRC-16/MODBUS, low byte first |

CRC covers `LEN` (both bytes), `SEQ`, `CMD`, and `PAYLOAD`; it does not cover
the two SOF bytes or the CRC field. Parameters are initial value `0xFFFF`,
reflected polynomial `0xA001`, no final XOR. The standard vector
`"123456789"` evaluates to `0x4B37`.

An incomplete frame is discarded when no next byte arrives for 100 ms. SOF
bytes inside payload are ordinary payload and do not restart the parser.

## Commands

| Request | Value | Payload | Success response |
|---|---:|---|---|
| PING | `0x01` | 0..240 arbitrary bytes | `0x81`, identical payload |
| GET_STATS | `0x02` | Empty | `0x82`, 84-byte statistics payload |
| LED_SET | `0x03` | `[led_id, state]` | `0x83`, identical two bytes |

`led_id`: 0 red, 1 green, 2 blue. `state`: 0 off, 1 on. LEDs are active low,
but protocol values express logical state.

Unknown commands and invalid payloads return command `0xFF` with payload
`[error_code, original_command]`. Error 1 means unknown command; error 2 means
invalid payload. A bad length or CRC is rejected before dispatch and produces
no command response.

## GET_STATS payload

All fields are unsigned 32-bit little-endian integers.

| Offset | Field |
|---:|---|
| 0 | `uptime_ms` |
| 4 | `rx_dma_bytes` |
| 8 | `rx_event_idle` |
| 12 | `rx_event_ht` |
| 16 | `rx_event_tc` |
| 20 | `error_ore` |
| 24 | `error_fe` |
| 28 | `error_ne` |
| 32 | `error_pe` |
| 36 | `ring_occupancy` |
| 40 | `ring_high_watermark` |
| 44 | `ring_overflow_count` |
| 48 | `ring_dropped_bytes` |
| 52 | `valid_frames` |
| 56 | `crc_errors` |
| 60 | `length_errors` |
| 64 | `unknown_command_count` |
| 68 | `timeout_errors` |
| 72 | `ping_count` |
| 76 | `led_set_count` |
| 80 | `restart_count` |

Counters wrap naturally at `UINT32_MAX`. The software ring uses a drop-new
overflow policy: unread data is never overwritten; overflow and dropped-byte
counters increase and the parser is reset in the main loop.
