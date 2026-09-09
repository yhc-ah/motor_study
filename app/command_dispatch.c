#include "command_dispatch.h"

#include "bsp_led.h"
#include "bsp_uart.h"
#include "byte_ring.h"
#include "frame_codec.h"
#include "stm32f4xx_hal.h"
#include <stddef.h>
#ifndef COMMAND_DISPATCH_HOST_TEST
#include "app_sensors.h"
#include "app_week4.h"
#include "week4_config.h"
#include "week5_config.h"
#include "app_week5.h"
#include "bsp_uart_tx.h"
#endif

static uint8_t s_tx_buffer[FRAME_MAX_ENCODED_SIZE];

static void WriteLE32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value & 0xFFU);
    output[1] = (uint8_t)((value >> 8U) & 0xFFU);
    output[2] = (uint8_t)((value >> 16U) & 0xFFU);
    output[3] = (uint8_t)((value >> 24U) & 0xFFU);
}

static void SendFrame(CommandDispatcher *dispatcher,
                      uint32_t sequence,
                      uint8_t command,
                      const uint8_t *payload,
                      uint16_t payload_length)
{
    uint16_t encoded_length;

    encoded_length = Frame_Encode(sequence, command, payload,
                                  payload_length, s_tx_buffer,
                                  (uint16_t)sizeof(s_tx_buffer));
    if ((encoded_length == 0U) ||
        (BSP_UART_SendQueued(s_tx_buffer, encoded_length) != HAL_OK)) {
        dispatcher->stats.tx_error_count++;
    }
}

static void SendError(CommandDispatcher *dispatcher,
                      uint32_t sequence,
                      uint8_t original_command,
                      uint8_t error_code)
{
    uint8_t payload[2];

    payload[0] = error_code;
    payload[1] = original_command;
    SendFrame(dispatcher, sequence, CMD_ERROR_RSP,
              payload, (uint16_t)sizeof(payload));
}

static void SendStats(CommandDispatcher *dispatcher, uint32_t sequence)
{
    uint8_t payload[COMMAND_STATS_PAYLOAD_SIZE];
    uint32_t offset = 0U;
    const BspUartStats *uart_stats = BSP_UART_GetStats();
    const FrameParserStats *parser_stats =
        FrameParser_GetStats(dispatcher->parser);

#define PUT32(value)                           \
    do {                                       \
        WriteLE32(&payload[offset],             \
                  (uint32_t)(value));           \
        offset += 4U;                           \
    } while (0)

    PUT32(HAL_GetTick());
    PUT32(uart_stats->rx_dma_bytes);
    PUT32(uart_stats->rx_event_idle);
    PUT32(uart_stats->rx_event_ht);
    PUT32(uart_stats->rx_event_tc);
    PUT32(uart_stats->error_ore);
    PUT32(uart_stats->error_fe);
    PUT32(uart_stats->error_ne);
    PUT32(uart_stats->error_pe);
    PUT32(ByteRing_Size(&g_uart_rx_ring));
    PUT32(g_uart_rx_ring.high_watermark);
    PUT32(g_uart_rx_ring.overflow_count);
    PUT32(g_uart_rx_ring.dropped_bytes);
    PUT32(parser_stats->valid_frames);
    PUT32(parser_stats->crc_errors);
    PUT32(parser_stats->length_errors);
    PUT32(dispatcher->stats.unknown_command_count);
    PUT32(parser_stats->timeout_errors);
    PUT32(dispatcher->stats.ping_count);
    PUT32(dispatcher->stats.led_set_count);
    PUT32(uart_stats->restart_count);

#undef PUT32

    SendFrame(dispatcher, sequence, CMD_GET_STATS_RSP,
              payload, (uint16_t)offset);
}

void CommandDispatcher_Init(CommandDispatcher *dispatcher,
                            FrameParser *parser)
{
    if (dispatcher == NULL) {
        return;
    }

    dispatcher->parser = parser;
    dispatcher->stats.ping_count = 0U;
    dispatcher->stats.get_stats_count = 0U;
    dispatcher->stats.led_set_count = 0U;
    dispatcher->stats.unknown_command_count = 0U;
    dispatcher->stats.invalid_payload_count = 0U;
    dispatcher->stats.tx_error_count = 0U;
}

void CommandDispatcher_Handle(const ParsedFrame *frame, void *context)
{
    CommandDispatcher *dispatcher = (CommandDispatcher *)context;

    if ((frame == NULL) || (dispatcher == NULL)) {
        return;
    }
#ifndef COMMAND_DISPATCH_HOST_TEST
#if APP_WEEK4
#if APP_WEEK5
    if (APP_Week5_Command(frame)) { return; }
#endif
    if (APP_Week4_Command(frame)) { return; }
#else
    if (APP_Sensors_Command(frame)) { return; }
#endif
#endif

    switch (frame->command) {
    case CMD_PING:
        dispatcher->stats.ping_count++;
        SendFrame(dispatcher, frame->sequence, CMD_PING_RSP,
                  frame->payload, frame->payload_length);
        break;

    case CMD_GET_STATS:
        if (frame->payload_length != 0U) {
            dispatcher->stats.invalid_payload_count++;
            SendError(dispatcher, frame->sequence, frame->command,
                      COMMAND_ERROR_BAD_PAYLOAD);
            break;
        }
        dispatcher->stats.get_stats_count++;
        SendStats(dispatcher, frame->sequence);
        break;

    case CMD_LED_SET:
        if ((frame->payload_length != 2U) ||
            (frame->payload[0] > 2U) ||
            (frame->payload[1] > 1U) ||
            (BSP_LED_SetById(frame->payload[0], frame->payload[1]) == 0U)) {
            dispatcher->stats.invalid_payload_count++;
            SendError(dispatcher, frame->sequence, frame->command,
                      COMMAND_ERROR_BAD_PAYLOAD);
            break;
        }
        dispatcher->stats.led_set_count++;
        SendFrame(dispatcher, frame->sequence, CMD_LED_SET_RSP,
                  frame->payload, frame->payload_length);
        break;

    default:
        dispatcher->stats.unknown_command_count++;
        SendError(dispatcher, frame->sequence, frame->command,
                  COMMAND_ERROR_UNKNOWN);
        break;
    }
}

const CommandStats *CommandDispatcher_GetStats(
    const CommandDispatcher *dispatcher)
{
    return (dispatcher == NULL) ? NULL : &dispatcher->stats;
}
