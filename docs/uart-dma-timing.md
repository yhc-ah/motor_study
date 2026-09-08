# UART and DMA timing

## Theoretical UART timing

8N1 transmits 10 bits per byte: one start bit, eight data bits, one stop bit.

| Baud | Bit width | One byte | 1000 bytes |
|---:|---:|---:|---:|
| 9600 | 104.167 us | 1041.667 us | 1041.667 ms |
| 115200 | 8.681 us | 86.806 us | 86.806 ms |

Use `0x55` (`01010101`, LSB first on the wire) to make every data-bit boundary
easy to identify. Run:

```powershell
python tools\uart_test\uart_test.py --port COMx --baud 115200 --mode waveform
```

Repeat at 9600 after changing the CubeMX USART baud and rebuilding firmware.
Measure from the first start-bit edge to the end of the stop bit. For the block,
measure the first start edge to the last stop edge. Calculate:

```text
error_percent = abs(measured - theoretical) / theoretical * 100
```

The acceptance limit is less than 2%. PC host elapsed time printed by the tool
is not a substitute for a logic-analyzer measurement.

## RX event timing

PA1 is driven high on entry to `HAL_UARTEx_RxEventCallback()` and low before
every exit. A sample rate of at least 50 MHz is recommended.

| Build | Minimum | Maximum | Average | Samples | Target | Status |
|---|---:|---:|---:|---:|---:|---|
| Keil Debug | PENDING | PENDING | PENDING | PENDING | <100 us | PENDING HARDWARE |
| Keil Release | PENDING | PENDING | PENDING | PENDING | <100 us | PENDING HARDWARE |

The checked-in Keil build reports a maximum static stack usage of 216 bytes,
plus paths through function pointers that the linker cannot trace. RX callbacks
only classify the event, copy one or two DMA spans into the ring, update
counters, and toggle PA1; they never parse, print, delay, or transmit.
