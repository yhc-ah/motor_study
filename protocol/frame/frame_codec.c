#include "frame_codec.h"

#include "crc16_modbus.h"
#include <stddef.h>

uint16_t Frame_Encode(uint32_t sequence,
                      uint8_t command,
                      const uint8_t *payload,
                      uint16_t payload_length,
                      uint8_t *output,
                      uint16_t output_capacity)
{
    uint16_t body_length;
    uint16_t total_length;
    uint16_t crc;
    uint16_t i;

    if ((output == NULL) ||
        ((payload == NULL) && (payload_length != 0U)) ||
        (payload_length > FRAME_MAX_PAYLOAD)) {
        return 0U;
    }

    body_length = (uint16_t)(FRAME_MIN_LEN + payload_length);
    total_length = (uint16_t)(2U + 2U + body_length + 2U);
    if (output_capacity < total_length) {
        return 0U;
    }

    output[0] = FRAME_SOF1;
    output[1] = FRAME_SOF2;
    output[2] = (uint8_t)(body_length & 0xFFU);
    output[3] = (uint8_t)(body_length >> 8U);
    output[4] = (uint8_t)(sequence & 0xFFU);
    output[5] = (uint8_t)((sequence >> 8U) & 0xFFU);
    output[6] = (uint8_t)((sequence >> 16U) & 0xFFU);
    output[7] = (uint8_t)((sequence >> 24U) & 0xFFU);
    output[8] = command;

    for (i = 0U; i < payload_length; ++i) {
        output[9U + i] = payload[i];
    }

    crc = CRC16_Modbus(&output[2], (uint32_t)(2U + body_length));
    output[9U + payload_length] = (uint8_t)(crc & 0xFFU);
    output[10U + payload_length] = (uint8_t)(crc >> 8U);
    return total_length;
}
