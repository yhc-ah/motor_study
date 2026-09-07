## UART

| Function | Peripheral | MCU Pin | Board Route | CubeMX AF |
|---|---|---|---|---|
| USB-UART TX | USART1_TX | PA9 | PA9 -> J80 -> CH340 RXD | AF7 |
| USB-UART RX | USART1_RX | PA10 | CH340 TXD -> J81 -> PA10 | AF7 |

J80 and J81 must both be fitted when using the board USB-to-UART connector.
