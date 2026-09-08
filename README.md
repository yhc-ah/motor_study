# STM32F407 real-time BSP experiments

## Week 03 — current branch

**Software implementation available; full week03 hardware acceptance is still pending.**
This branch includes week2 plus MPU6050 initialization/raw sampling/recovery,
I2C/SPI/TIM2 board support, queued UART TX DMA at 460800 baud, restricted cooperative
NOR self-test, and PC raw capture/statistics/SVG tools. Actual temperature/humidity
and LCD drivers remain unimplemented until their exact hardware is supplied.
No hardware measurements or week-03-pass tag are claimed.

- [Third-week setup and remaining prerequisites](docs/week-03-setup.md)
- [Third-week verification record](docs/week-03-test.md)
- [PC capture / calibration / explicit Flash command](tools/sensor_test/README.md)
- [Flash driver interface](device/spi_flash/README.md)

Build the committed Keil project directly. Run `./tests/host/run_week3_tests.ps1`
for old and new host tests. Flash reserved size defaults to 0, so boot only reads
ID and the destructive test is disabled. Streaming defaults off. The defaults
below describe the inherited **week2** firmware; use `--baud 460800` for its PC
tools against **week3**. Pin/profile details and CubeMX regeneration notes are in
the third-week setup guide.

STM32CubeMX + Keil MDK project for the EmbedFire/Wildfire Batianhu V2 board
with an STM32F407ZGTx. The `week1` branch contains the first-week GPIO, EXTI,
timing, and memory exercises. The `week2` branch adds a measured UART receive
path, circular DMA, a software ring, a framed protocol, and reproducible PC
stress tools.

## Week 02 architecture

```text
USART1 RX (PA10)
  -> DMA2 Stream2 Channel4, circular 256-byte buffer
  -> HAL_UARTEx_RxEventCallback (copy/count/probe only)
  -> 1024-byte SPSC software ring (drop-new on overflow)
  -> main-loop parser
  -> command dispatcher
  -> PING / GET_STATS / LED_SET response on USART1 TX (PA9)
```

The callback never parses, prints, delays, drives an LCD, executes commands, or
transmits. PA1 is high only while the RX-event callback runs so its duration can
be measured directly.

## Hardware setup

- Fit J80: PA9/USART1_TX to CH340 RXD.
- Fit J81: CH340 TXD to PA10/USART1_RX.
- Use 115200 baud, 8 data bits, no parity, one stop bit, no flow control.
- Connect the logic-analyzer ground to board ground.
- Observe PA9 (TX), PA10 (RX), and PA1 (callback probe).

See [pin map](docs/pinmap.md) and [clock tree](docs/clock_tree.md).

## Build

1. Open `stm32-realtime-bsp.ioc` in STM32CubeMX.
2. Confirm USART1 RX DMA is DMA2 Stream2 Channel4, circular, byte aligned,
   memory increment enabled, high priority.
3. Confirm both USART1 and DMA2 Stream2 interrupts are enabled.
4. Generate the MDK-ARM project without deleting user-code blocks.
5. Open `MDK-ARM/stm32-realtime-bsp.uvprojx` in Keil 5.
6. Select target `stm32-realtime-bsp` and rebuild.
7. Program the board with a CMSIS-DAP/J-Link and reset it.

The project currently targets ARMCC 5.06 update 7. A verified local rebuild
completed with 0 errors and 0 warnings.

## Host tests

With MinGW GCC and Python available:

```powershell
powershell -ExecutionPolicy Bypass -File tests\host\run_tests.ps1
```

The suite validates the ring boundaries and overflow policy, CRC-16/MODBUS
standard vector, frame resynchronization/timeouts/gluing, command dispatch, and
the PC codec's fixed-seed behavior.

## UART hardware tests

```powershell
cd tools\uart_test
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
python uart_test.py --list-ports
python uart_test.py --port COM5 --mode smoke --seed 20260829
python uart_test.py --port COM5 --mode dma --seed 20260829
python uart_test.py --port COM5 --mode random --cases 10000 --seed 20260829
python uart_test.py --port COM5 --mode stress --stress-bytes 100000 --seed 20260829
python reconnect_test.py --port COM5 --cycles 5 --seed 20260829
```

For waveform capture:

```powershell
python uart_test.py --port COM5 --baud 115200 --mode waveform
```

Replace `COM5` with the actual CH340 port. Use the same random seed after a
failure so the JSONL log reproduces the exact sequence and fragmentation.

## Protocol

Frames start with `AA 55`; `LEN`, sequence, and CRC fields are little endian.
CRC is CRC-16/MODBUS and `"123456789"` must produce `0x4B37`. Maximum payload is
240 bytes and incomplete frames expire after 100 ms. The exact frame layout,
commands, errors, and GET_STATS offsets are in the [protocol ICD](docs/protocol-icd.md).

## Reports and Git milestones

- [Week 02 test report](docs/week-02-test.md)
- [UART/DMA timing worksheet](docs/uart-dma-timing.md)
- `week-01-pass`: first-week accepted revision.
- `week-02-pass`: create only after all physical UART, logic-analyzer, stress,
  and reconnect rows in the report pass.

The intermediate polling echo remains in commit `294dab9`; the final firmware
uses DMA and must not call polling receive concurrently.
