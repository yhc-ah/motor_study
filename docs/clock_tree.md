# Clock tree

The checked-in CubeMX configuration uses the 25 MHz external crystal.

| Clock | Configuration | Frequency |
|---|---|---:|
| PLL input | HSE / PLLM = 25 MHz / 25 | 1 MHz |
| PLL VCO | 1 MHz x PLLN 336 | 336 MHz |
| SYSCLK | VCO / PLLP 2 | 168 MHz |
| AHB / HCLK | SYSCLK / 1 | 168 MHz |
| APB1 / PCLK1 | HCLK / 4 | 42 MHz |
| APB2 / PCLK2 | HCLK / 2 | 84 MHz |
| USART1 kernel clock | PCLK2 | 84 MHz |
| SysTick | HAL 1 ms tick | 1 kHz |

USART1 uses 16-times oversampling. At 115200 baud, the closest BRR divisor is
729, giving about 115226.3 baud (approximately +0.023%). At 9600 baud the
divisor is 8750 and the nominal error is 0%.
