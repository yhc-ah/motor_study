#include "app_uart.h"

#include "bsp_uart.h"
#include "byte_ring.h"
#include "command_dispatch.h"
#include "frame_parser.h"
#include "stm32f4xx_hal.h"

#define APP_UART_CHUNK_SIZE      64U
#define APP_UART_BUDGET_PER_RUN 256U

static FrameParser s_parser;
static CommandDispatcher s_dispatcher;
static uint32_t s_pause_deadline;
static uint8_t s_pause_active;

volatile uint32_t app_uart_pause_request_ms;

void APP_UART_Init(void)
{
    FrameParser_Init(&s_parser);
    CommandDispatcher_Init(&s_dispatcher, &s_parser);
    s_pause_deadline = 0U;
    s_pause_active = 0U;
    app_uart_pause_request_ms = 0U;
}

void APP_UART_PauseConsumer(uint32_t duration_ms)
{
    s_pause_deadline = HAL_GetTick() + duration_ms;
    s_pause_active = 1U;
}

void APP_UART_Run(void)
{
    uint8_t chunk[APP_UART_CHUNK_SIZE];
    uint32_t length;
    uint32_t processed = 0U;
    uint32_t i;
    uint32_t now = HAL_GetTick();
    uint32_t pause_request = app_uart_pause_request_ms;

    if (pause_request != 0U) {
        app_uart_pause_request_ms = 0U;
        APP_UART_PauseConsumer(pause_request);
    }

    BSP_UART_Service();

    if (BSP_UART_TakeParserResetRequest() != 0U) {
        ByteRing_DiscardAll(&g_uart_rx_ring);
        FrameParser_Reset(&s_parser);
    }

    if (s_pause_active != 0U) {
        if ((int32_t)(now - s_pause_deadline) < 0) {
            FrameParser_Service(&s_parser, now);
            return;
        }
        s_pause_active = 0U;
    }

    while (processed < APP_UART_BUDGET_PER_RUN) {
        length = ByteRing_Read(&g_uart_rx_ring, chunk,
                               (uint32_t)sizeof(chunk));
        if (length == 0U) {
            break;
        }

        for (i = 0U; i < length; ++i) {
            FrameParser_PushByte(&s_parser, chunk[i], now,
                                 CommandDispatcher_Handle,
                                 &s_dispatcher);
        }
        processed += length;
    }

    FrameParser_Service(&s_parser, now);
}
