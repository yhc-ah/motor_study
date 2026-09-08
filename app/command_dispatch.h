#ifndef COMMAND_DISPATCH_H
#define COMMAND_DISPATCH_H

#include "frame_parser.h"
#include <stdint.h>

#define CMD_PING          0x01U
#define CMD_GET_STATS     0x02U
#define CMD_LED_SET       0x03U
#define CMD_PING_RSP      0x81U
#define CMD_GET_STATS_RSP 0x82U
#define CMD_LED_SET_RSP   0x83U
#define CMD_ERROR_RSP     0xFFU

#define COMMAND_ERROR_UNKNOWN     1U
#define COMMAND_ERROR_BAD_PAYLOAD 2U
#define COMMAND_STATS_PAYLOAD_SIZE 84U

typedef struct {
    uint32_t ping_count;
    uint32_t get_stats_count;
    uint32_t led_set_count;
    uint32_t unknown_command_count;
    uint32_t invalid_payload_count;
    uint32_t tx_error_count;
} CommandStats;

typedef struct {
    FrameParser *parser;
    CommandStats stats;
} CommandDispatcher;

void CommandDispatcher_Init(CommandDispatcher *dispatcher,
                            FrameParser *parser);
void CommandDispatcher_Handle(const ParsedFrame *frame, void *context);
const CommandStats *CommandDispatcher_GetStats(
    const CommandDispatcher *dispatcher);

#endif
