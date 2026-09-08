#ifndef BSP_UART_TX_H
#define BSP_UART_TX_H
#include "tx_queue.h"
#include "stm32f4xx_hal.h"
void BSP_UART_TxInit(void);
void BSP_UART_TxService(void);
HAL_StatusTypeDef BSP_UART_SendQueued(const uint8_t *,uint16_t);
const TxQueue *BSP_UART_TxStats(void);
uint32_t BSP_UART_TxErrors(void);
#endif
