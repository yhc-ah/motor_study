# Pin map

## UART and measurement

| Function | Peripheral/resource | MCU pin | Board route/configuration |
|---|---|---|---|
| USB-UART TX | USART1_TX, AF7 | PA9 | PA9 -> J80 -> CH340 RXD |
| USB-UART RX | USART1_RX, AF7 | PA10 | CH340 TXD -> J81 -> PA10 |
| RX DMA | DMA2 Stream2 Channel 4 | n/a | Circular, byte/byte, memory increment, high priority |
| UART callback probe | GPIO output | PA1 | Push-pull, no pull, very high speed |
| Red LED | GPIO output, active low | PF6 | On-board RGB LED R |
| Green LED | GPIO output, active low | PF7 | On-board RGB LED G |
| Blue LED | GPIO output, active low | PF8 | On-board RGB LED B |

J80 and J81 must both be fitted when using the board USB-to-UART connector.

Logic-analyzer ground must be connected to board GND. Observe PA9 for MCU TX,
PA10 for MCU RX, and PA1 for the RX-event callback pulse. PA1 is shared with
the first-week interrupt probe, so do not measure key EXTI and UART callback
timing simultaneously.
