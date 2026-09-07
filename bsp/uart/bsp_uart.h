#ifndef BSP_UART_H
#define BSP_UART_H

#include "byte_ring.h"
#include "stm32f4xx_hal.h"
#include <stdint.h>

#define UART_DMA_RX_SIZE 256U

typedef struct {
    volatile uint32_t rx_dma_bytes;
    volatile uint32_t rx_event_idle;
    volatile uint32_t rx_event_ht;
    volatile uint32_t rx_event_tc;
    volatile uint32_t rx_event_other;
    volatile uint32_t error_ore;
    volatile uint32_t error_fe;
    volatile uint32_t error_ne;
    volatile uint32_t error_pe;
    volatile uint32_t restart_count;
    volatile uint32_t invalid_position_count;
} BspUartStats;

extern ByteRing g_uart_rx_ring;

void BSP_UART_Init(void);
HAL_StatusTypeDef BSP_UART_StartRxDMA(void);
void BSP_UART_Service(void);
uint8_t BSP_UART_TakeParserResetRequest(void);
const BspUartStats *BSP_UART_GetStats(void);
HAL_StatusTypeDef BSP_UART_SendBlocking(const uint8_t *data,
                                        uint16_t length,
                                        uint32_t timeout_ms);
void BSP_UART_PollingEchoTask(void);
void BSP_UART_DMA_IRQHandler(void);
void BSP_UART_USART_IRQHandler(void);

#endif
