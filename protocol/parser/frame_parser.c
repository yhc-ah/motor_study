#include "frame_parser.h"

#include "crc16_modbus.h"
#include <stddef.h>

static uint32_t ReadLE32(const uint8_t *data)
{
    return ((uint32_t)data[0]) |
           ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

void FrameParser_Init(FrameParser *parser)
{
    if (parser == NULL) {
        return;
    }

    parser->stats.valid_frames = 0U;
    parser->stats.crc_errors = 0U;
    parser->stats.length_errors = 0U;
    parser->stats.timeout_errors = 0U;
    parser->stats.noise_bytes = 0U;
    FrameParser_Reset(parser);
}

void FrameParser_Reset(FrameParser *parser)
{
    if (parser == NULL) {
        return;
    }

    parser->state = FRAME_WAIT_AA;
    parser->body_length = 0U;
    parser->body_pos = 0U;
    parser->received_crc = 0U;
    parser->last_byte_tick = 0U;
}

void FrameParser_PushByte(FrameParser *parser,
                          uint8_t byte,
                          uint32_t now_ms,
                          FrameHandler handler,
                          void *context)
{
    uint8_t crc_input[2U + FRAME_MAX_LEN];
    uint16_t calculated_crc;
    uint16_t i;
    ParsedFrame frame;

    if (parser == NULL) {
        return;
    }

    switch (parser->state) {
    case FRAME_WAIT_AA:
        if (byte == FRAME_SOF1) {
            parser->state = FRAME_WAIT_55;
            parser->last_byte_tick = now_ms;
        } else {
            parser->stats.noise_bytes++;
        }
        break;

    case FRAME_WAIT_55:
        parser->last_byte_tick = now_ms;
        if (byte == FRAME_SOF2) {
            parser->state = FRAME_READ_LEN_LO;
        } else if (byte != FRAME_SOF1) {
            parser->stats.noise_bytes++;
            FrameParser_Reset(parser);
        }
        break;

    case FRAME_READ_LEN_LO:
        parser->body_length = byte;
        parser->last_byte_tick = now_ms;
        parser->state = FRAME_READ_LEN_HI;
        break;

    case FRAME_READ_LEN_HI:
        parser->body_length |= (uint16_t)((uint16_t)byte << 8U);
        parser->last_byte_tick = now_ms;
        if ((parser->body_length < FRAME_MIN_LEN) ||
            (parser->body_length > FRAME_MAX_LEN)) {
            parser->stats.length_errors++;
            FrameParser_Reset(parser);
        } else {
            parser->body_pos = 0U;
            parser->state = FRAME_READ_BODY;
        }
        break;

    case FRAME_READ_BODY:
        parser->body[parser->body_pos++] = byte;
        parser->last_byte_tick = now_ms;
        if (parser->body_pos == parser->body_length) {
            parser->state = FRAME_READ_CRC_LO;
        }
        break;

    case FRAME_READ_CRC_LO:
        parser->received_crc = byte;
        parser->last_byte_tick = now_ms;
        parser->state = FRAME_READ_CRC_HI;
        break;

    case FRAME_READ_CRC_HI:
        parser->received_crc |= (uint16_t)((uint16_t)byte << 8U);
        parser->last_byte_tick = now_ms;
        crc_input[0] = (uint8_t)(parser->body_length & 0xFFU);
        crc_input[1] = (uint8_t)(parser->body_length >> 8U);
        for (i = 0U; i < parser->body_length; ++i) {
            crc_input[2U + i] = parser->body[i];
        }
        calculated_crc = CRC16_Modbus(crc_input,
                                      (uint32_t)(2U + parser->body_length));

        if (calculated_crc == parser->received_crc) {
            parser->stats.valid_frames++;
            frame.sequence = ReadLE32(&parser->body[0]);
            frame.command = parser->body[4];
            frame.payload = &parser->body[5];
            frame.payload_length =
                (uint16_t)(parser->body_length - FRAME_MIN_LEN);
            if (handler != NULL) {
                handler(&frame, context);
            }
        } else {
            parser->stats.crc_errors++;
        }
        FrameParser_Reset(parser);
        break;

    default:
        FrameParser_Reset(parser);
        break;
    }
}

void FrameParser_Service(FrameParser *parser, uint32_t now_ms)
{
    if ((parser == NULL) || (parser->state == FRAME_WAIT_AA)) {
        return;
    }

    if ((uint32_t)(now_ms - parser->last_byte_tick) >= FRAME_TIMEOUT_MS) {
        parser->stats.timeout_errors++;
        FrameParser_Reset(parser);
    }
}

const FrameParserStats *FrameParser_GetStats(const FrameParser *parser)
{
    return (parser == NULL) ? NULL : &parser->stats;
}
