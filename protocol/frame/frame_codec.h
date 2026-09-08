#ifndef FRAME_CODEC_H
#define FRAME_CODEC_H

#include <stdint.h>

#define FRAME_SOF1             0xAAU
#define FRAME_SOF2             0x55U
#define FRAME_MIN_LEN          5U
#define FRAME_MAX_LEN          245U
#define FRAME_MAX_PAYLOAD      240U
#define FRAME_MAX_ENCODED_SIZE 251U

uint16_t Frame_Encode(uint32_t sequence,
                      uint8_t command,
                      const uint8_t *payload,
                      uint16_t payload_length,
                      uint8_t *output,
                      uint16_t output_capacity);

#endif
