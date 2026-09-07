#include "bsp_uart.h"
#include "usart.h"

HAL_StatusTypeDef BSP_UART_SendBlocking(const uint8_t *data,
                                        uint16_t length,
                                        uint32_t timeout_ms)
{
    if ((data == NULL) || (length == 0U)) {
        return HAL_ERROR;
    }

    return HAL_UART_Transmit(&huart1,
                             (uint8_t *)data,
                             length,
                             timeout_ms);
}

void BSP_UART_PollingEchoTask(void)
{
    uint8_t byte;

    /* Keep the polling wait short so the main loop remains responsive. */
    if (HAL_UART_Receive(&huart1, &byte, 1U, 2U) == HAL_OK) {
        (void)HAL_UART_Transmit(&huart1, &byte, 1U, 20U);
    }
}
