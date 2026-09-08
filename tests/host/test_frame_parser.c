#include "frame_codec.h"
#include "frame_parser.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t calls;
    uint32_t sequence;
    uint8_t command;
    uint8_t payload[FRAME_MAX_PAYLOAD];
    uint16_t payload_length;
} Capture;

static void capture_frame(const ParsedFrame *frame, void *context)
{
    Capture *capture = (Capture *)context;

    capture->calls++;
    capture->sequence = frame->sequence;
    capture->command = frame->command;
    capture->payload_length = frame->payload_length;
    memcpy(capture->payload, frame->payload, frame->payload_length);
}

static uint32_t feed(FrameParser *parser,
                     const uint8_t *data,
                     uint16_t length,
                     uint32_t now,
                     Capture *capture)
{
    uint16_t i;

    for (i = 0U; i < length; ++i) {
        FrameParser_PushByte(parser, data[i], now++, capture_frame, capture);
    }
    return now;
}

int main(void)
{
    FrameParser parser;
    Capture capture = {0};
    uint8_t encoded[FRAME_MAX_ENCODED_SIZE];
    uint8_t joined[2U * FRAME_MAX_ENCODED_SIZE];
    const uint8_t payload[] = {0x11U, 0xAAU, 0x55U, 0x22U};
    uint16_t length;
    uint32_t now = 0U;

    FrameParser_Init(&parser);
    length = Frame_Encode(0x12345678U, 0x01U, payload, sizeof(payload),
                          encoded, sizeof(encoded));
    assert(length == 15U);
    now = feed(&parser, encoded, length, now, &capture);
    assert(capture.calls == 1U);
    assert(capture.sequence == 0x12345678U);
    assert(capture.command == 0x01U);
    assert(capture.payload_length == sizeof(payload));
    assert(memcmp(capture.payload, payload, sizeof(payload)) == 0);

    encoded[length - 1U] ^= 0x01U;
    now = feed(&parser, encoded, length, now, &capture);
    assert(capture.calls == 1U);
    assert(parser.stats.crc_errors == 1U);

    FrameParser_PushByte(&parser, 0xAAU, 1000U, capture_frame, &capture);
    FrameParser_Service(&parser, 1099U);
    assert(parser.stats.timeout_errors == 0U);
    FrameParser_Service(&parser, 1100U);
    assert(parser.stats.timeout_errors == 1U);

    FrameParser_PushByte(&parser, 0xAAU, 1200U, capture_frame, &capture);
    FrameParser_PushByte(&parser, 0x55U, 1201U, capture_frame, &capture);
    FrameParser_PushByte(&parser, 0x00U, 1202U, capture_frame, &capture);
    FrameParser_PushByte(&parser, 0x00U, 1203U, capture_frame, &capture);
    assert(parser.stats.length_errors == 1U);

    length = Frame_Encode(7U, 0x01U, payload, sizeof(payload),
                          encoded, sizeof(encoded));
    memcpy(joined, encoded, length);
    memcpy(&joined[length], encoded, length);
    now = feed(&parser, joined, (uint16_t)(2U * length), now, &capture);
    assert(capture.calls == 3U);

    FrameParser_PushByte(&parser, 0x13U, now++, capture_frame, &capture);
    FrameParser_PushByte(&parser, 0xAAU, now++, capture_frame, &capture);
    FrameParser_PushByte(&parser, 0xAAU, now++, capture_frame, &capture);
    now = feed(&parser, &encoded[1], (uint16_t)(length - 1U), now,
               &capture);
    assert(capture.calls == 4U);
    assert(parser.stats.noise_bytes >= 1U);

    assert(Frame_Encode(1U, 1U, NULL, 1U, encoded, sizeof(encoded)) == 0U);
    assert(Frame_Encode(1U, 1U, payload, FRAME_MAX_PAYLOAD + 1U,
                        encoded, sizeof(encoded)) == 0U);
    assert(Frame_Encode(1U, 1U, payload, sizeof(payload), encoded,
                        length - 1U) == 0U);

    puts("test_frame_parser: PASS");
    return 0;
}
