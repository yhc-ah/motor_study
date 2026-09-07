#ifndef FRAME_PARSER_H
#define FRAME_PARSER_H

#include "frame_codec.h"
#include <stdint.h>

#define FRAME_TIMEOUT_MS 100U

typedef struct {
    uint32_t sequence;
    uint8_t command;
    const uint8_t *payload;
    uint16_t payload_length;
} ParsedFrame;

typedef void (*FrameHandler)(const ParsedFrame *frame, void *context);

typedef struct {
    uint32_t valid_frames;
    uint32_t crc_errors;
    uint32_t length_errors;
    uint32_t timeout_errors;
    uint32_t noise_bytes;
} FrameParserStats;

typedef enum {
    FRAME_WAIT_AA = 0,
    FRAME_WAIT_55,
    FRAME_READ_LEN_LO,
    FRAME_READ_LEN_HI,
    FRAME_READ_BODY,
    FRAME_READ_CRC_LO,
    FRAME_READ_CRC_HI
} FrameParserState;

typedef struct {
    FrameParserState state;
    uint16_t body_length;
    uint16_t body_pos;
    uint16_t received_crc;
    uint8_t body[FRAME_MAX_LEN];
    uint32_t last_byte_tick;
    FrameParserStats stats;
} FrameParser;

void FrameParser_Init(FrameParser *parser);
void FrameParser_Reset(FrameParser *parser);
void FrameParser_PushByte(FrameParser *parser,
                          uint8_t byte,
                          uint32_t now_ms,
                          FrameHandler handler,
                          void *context);
void FrameParser_Service(FrameParser *parser, uint32_t now_ms);
const FrameParserStats *FrameParser_GetStats(const FrameParser *parser);

#endif
