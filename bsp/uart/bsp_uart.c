#include "bsp_uart.h"
#include "bsp_probe.h"
#include "usart.h"

#include <stddef.h>

#define UART_IRQ_SOURCE_NONE  0U
#define UART_IRQ_SOURCE_DMA   1U
#define UART_IRQ_SOURCE_USART 2U

ByteRing g_uart_rx_ring;

static uint8_t s_dma_rx_buffer[UART_DMA_RX_SIZE];
static volatile uint16_t s_old_pos;
static volatile uint8_t s_recover_pending;
static volatile uint8_t s_parser_reset_pending;
static volatile uint8_t s_irq_source;
static BspUartStats s_stats;

static uint32_t BSP_UART_EnterCritical(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    return primask;
}

static void BSP_UART_ExitCritical(uint32_t primask)
{
    if (primask == 0U) {
        __enable_irq();
    }
}

static void BSP_UART_PublishSpan(const uint8_t *data, uint16_t length)
{
    uint32_t written;

    if (length == 0U) {
        return;
    }

    s_stats.rx_dma_bytes += length;
    written = ByteRing_WriteFromISR(&g_uart_rx_ring, data, length);
    if (written != length) {
        s_parser_reset_pending = 1U;
    }
}

void BSP_UART_Init(void)
{
    ByteRing_Init(&g_uart_rx_ring);
    s_old_pos = 0U;
    s_recover_pending = 0U;
    s_parser_reset_pending = 0U;
    s_irq_source = UART_IRQ_SOURCE_NONE;

    s_stats.rx_dma_bytes = 0U;
    s_stats.rx_event_idle = 0U;
    s_stats.rx_event_ht = 0U;
    s_stats.rx_event_tc = 0U;
    s_stats.rx_event_other = 0U;
    s_stats.error_ore = 0U;
    s_stats.error_fe = 0U;
    s_stats.error_ne = 0U;
    s_stats.error_pe = 0U;
    s_stats.restart_count = 0U;
    s_stats.invalid_position_count = 0U;
}

HAL_StatusTypeDef BSP_UART_StartRxDMA(void)
{
    s_old_pos = 0U;
    return HAL_UARTEx_ReceiveToIdle_DMA(&huart1,
                                        s_dma_rx_buffer,
                                        UART_DMA_RX_SIZE);
}

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

const BspUartStats *BSP_UART_GetStats(void)
{
    return &s_stats;
}

uint8_t BSP_UART_TakeParserResetRequest(void)
{
    uint32_t primask;
    uint8_t requested;

    primask = BSP_UART_EnterCritical();
    requested = s_parser_reset_pending;
    s_parser_reset_pending = 0U;
    BSP_UART_ExitCritical(primask);
    return requested;
}

void BSP_UART_Service(void)
{
    uint32_t primask;
    uint8_t recover;

    primask = BSP_UART_EnterCritical();
    recover = s_recover_pending;
    s_recover_pending = 0U;
    BSP_UART_ExitCritical(primask);

    if (recover == 0U) {
        return;
    }

    (void)HAL_UART_AbortReceive(&huart1);
    ByteRing_DiscardAll(&g_uart_rx_ring);
    s_parser_reset_pending = 1U;
    s_old_pos = 0U;

    if (BSP_UART_StartRxDMA() == HAL_OK) {
        s_stats.restart_count++;
    } else {
        s_recover_pending = 1U;
    }
}

void BSP_UART_DMA_IRQHandler(void)
{
    s_irq_source = UART_IRQ_SOURCE_DMA;
    HAL_DMA_IRQHandler(&hdma_usart1_rx);
    s_irq_source = UART_IRQ_SOURCE_NONE;
}

void BSP_UART_USART_IRQHandler(void)
{
    s_irq_source = UART_IRQ_SOURCE_USART;
    HAL_UART_IRQHandler(&huart1);
    s_irq_source = UART_IRQ_SOURCE_NONE;
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
    if (huart->Instance != USART1) {
        return;
    }

    BSP_UART_ProbeHigh();

    if (s_irq_source == UART_IRQ_SOURCE_USART) {
        s_stats.rx_event_idle++;
    } else if ((s_irq_source == UART_IRQ_SOURCE_DMA) &&
               (size == (UART_DMA_RX_SIZE / 2U))) {
        s_stats.rx_event_ht++;
    } else if ((s_irq_source == UART_IRQ_SOURCE_DMA) &&
               (size == UART_DMA_RX_SIZE)) {
        s_stats.rx_event_tc++;
    } else {
        s_stats.rx_event_other++;
    }

    if (size > UART_DMA_RX_SIZE) {
        s_stats.invalid_position_count++;
        s_recover_pending = 1U;
        BSP_UART_ProbeLow();
        return;
    }

    if (size > s_old_pos) {
        BSP_UART_PublishSpan(&s_dma_rx_buffer[s_old_pos],
                             (uint16_t)(size - s_old_pos));
    } else if (size < s_old_pos) {
        BSP_UART_PublishSpan(&s_dma_rx_buffer[s_old_pos],
                             (uint16_t)(UART_DMA_RX_SIZE - s_old_pos));
        BSP_UART_PublishSpan(&s_dma_rx_buffer[0], size);
    } else {
        /* A duplicate event at the same DMA position has no new bytes. */
    }

    s_old_pos = size;
    BSP_UART_ProbeLow();
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    uint32_t error;

    if (huart->Instance != USART1) {
        return;
    }

    error = HAL_UART_GetError(huart);
    if ((error & HAL_UART_ERROR_ORE) != 0U) {
        s_stats.error_ore++;
    }
    if ((error & HAL_UART_ERROR_FE) != 0U) {
        s_stats.error_fe++;
    }
    if ((error & HAL_UART_ERROR_NE) != 0U) {
        s_stats.error_ne++;
    }
    if ((error & HAL_UART_ERROR_PE) != 0U) {
        s_stats.error_pe++;
    }

    s_recover_pending = 1U;
}
