#ifndef BSP_UART_H
#define BSP_UART_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

HAL_StatusTypeDef BSP_UART_SendBlocking(const uint8_t *data,
                                        uint16_t length,
                                        uint32_t timeout_ms);
void BSP_UART_PollingEchoTask(void);

#endif
