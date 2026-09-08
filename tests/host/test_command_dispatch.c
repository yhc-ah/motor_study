#include "command_dispatch.h"

#include "bsp_uart.h"
#include "byte_ring.h"
#include "frame_codec.h"
#include "frame_parser.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

ByteRing g_uart_rx_ring;

static BspUartStats s_uart_stats;
static uint8_t s_tx[FRAME_MAX_ENCODED_SIZE];
static uint16_t s_tx_length;
static uint32_t s_tick = 123456U;
static uint32_t s_led_calls;
static uint8_t s_led_id;
static uint8_t s_led_on;

typedef struct {
    uint32_t calls;
    uint32_t sequence;
    uint8_t command;
    uint8_t payload[FRAME_MAX_PAYLOAD];
    uint16_t payload_length;
} Response;

uint32_t HAL_GetTick(void)
{
    return s_tick;
}

uint8_t BSP_LED_SetById(uint8_t led_id, uint8_t on)
{
    s_led_calls++;
    s_led_id = led_id;
    s_led_on = on;
    return (led_id <= 2U && on <= 1U) ? 1U : 0U;
}

const BspUartStats *BSP_UART_GetStats(void)
{
    return &s_uart_stats;
}

HAL_StatusTypeDef BSP_UART_SendQueued(const uint8_t *data,
                                        uint16_t length)
{
    assert(length <= sizeof(s_tx));
    memcpy(s_tx, data, length);
    s_tx_length = length;
    return HAL_OK;
}

static void capture_response(const ParsedFrame *frame, void *context)
{
    Response *response = (Response *)context;

    response->calls++;
    response->sequence = frame->sequence;
    response->command = frame->command;
    response->payload_length = frame->payload_length;
    memcpy(response->payload, frame->payload, frame->payload_length);
}

static Response decode_response(void)
{
    FrameParser parser;
    Response response = {0};
    uint16_t i;

    FrameParser_Init(&parser);
    for (i = 0U; i < s_tx_length; ++i) {
        FrameParser_PushByte(&parser, s_tx[i], i,
                             capture_response, &response);
    }
    assert(response.calls == 1U);
    return response;
}

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

int main(void)
{
    FrameParser parser;
    CommandDispatcher dispatcher;
    ParsedFrame frame;
    Response response;
    const uint8_t ping_payload[] = {0x10U, 0xAAU, 0x55U, 0x20U};
    const uint8_t led_payload[] = {1U, 1U};
    const uint8_t bad_led_payload[] = {1U};

    ByteRing_Init(&g_uart_rx_ring);
    FrameParser_Init(&parser);
    CommandDispatcher_Init(&dispatcher, &parser);

    frame.sequence = 11U;
    frame.command = CMD_PING;
    frame.payload = ping_payload;
    frame.payload_length = sizeof(ping_payload);
    CommandDispatcher_Handle(&frame, &dispatcher);
    response = decode_response();
    assert(response.sequence == 11U);
    assert(response.command == CMD_PING_RSP);
    assert(response.payload_length == sizeof(ping_payload));
    assert(memcmp(response.payload, ping_payload, sizeof(ping_payload)) == 0);

    frame.sequence = 12U;
    frame.command = CMD_LED_SET;
    frame.payload = led_payload;
    frame.payload_length = sizeof(led_payload);
    CommandDispatcher_Handle(&frame, &dispatcher);
    response = decode_response();
    assert(s_led_calls == 1U && s_led_id == 1U && s_led_on == 1U);
    assert(response.command == CMD_LED_SET_RSP);

    frame.sequence = 13U;
    frame.payload = bad_led_payload;
    frame.payload_length = sizeof(bad_led_payload);
    CommandDispatcher_Handle(&frame, &dispatcher);
    response = decode_response();
    assert(s_led_calls == 1U);
    assert(response.command == CMD_ERROR_RSP);
    assert(response.payload_length == 2U);
    assert(response.payload[0] == COMMAND_ERROR_BAD_PAYLOAD);
    assert(response.payload[1] == CMD_LED_SET);

    frame.sequence = 14U;
    frame.command = 0x40U;
    frame.payload = NULL;
    frame.payload_length = 0U;
    CommandDispatcher_Handle(&frame, &dispatcher);
    response = decode_response();
    assert(response.command == CMD_ERROR_RSP);
    assert(response.payload[0] == COMMAND_ERROR_UNKNOWN);
    assert(response.payload[1] == 0x40U);

    s_uart_stats.rx_dma_bytes = 600U;
    s_uart_stats.rx_event_idle = 5U;
    s_uart_stats.rx_event_ht = 4U;
    s_uart_stats.rx_event_tc = 2U;
    s_uart_stats.restart_count = 1U;
    g_uart_rx_ring.high_watermark = 128U;
    frame.sequence = 15U;
    frame.command = CMD_GET_STATS;
    CommandDispatcher_Handle(&frame, &dispatcher);
    response = decode_response();
    assert(response.command == CMD_GET_STATS_RSP);
    assert(response.payload_length == COMMAND_STATS_PAYLOAD_SIZE);
    assert(read_le32(&response.payload[0]) == s_tick);
    assert(read_le32(&response.payload[4]) == 600U);
    assert(read_le32(&response.payload[8]) == 5U);
    assert(read_le32(&response.payload[12]) == 4U);
    assert(read_le32(&response.payload[16]) == 2U);
    assert(read_le32(&response.payload[40]) == 128U);
    assert(read_le32(&response.payload[64]) == 1U);
    assert(read_le32(&response.payload[72]) == 1U);
    assert(read_le32(&response.payload[76]) == 1U);
    assert(read_le32(&response.payload[80]) == 1U);

    puts("test_command_dispatch: PASS");
    return 0;
}
