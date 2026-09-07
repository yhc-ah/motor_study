#include "byte_ring.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void fill_pattern(uint8_t *data, uint32_t length, uint8_t base)
{
    uint32_t i;

    for (i = 0U; i < length; ++i) {
        data[i] = (uint8_t)(base + i);
    }
}

int main(void)
{
    ByteRing ring;
    uint8_t input[BYTE_RING_CAPACITY + 1U];
    uint8_t output[BYTE_RING_CAPACITY];
    uint32_t i;

    ByteRing_Init(&ring);
    assert(ByteRing_Size(&ring) == 0U);
    assert(ByteRing_Free(&ring) == BYTE_RING_CAPACITY);
    assert(ByteRing_WriteFromISR(&ring, input, 0U) == 0U);
    assert(ByteRing_Read(&ring, output, 0U) == 0U);

    input[0] = 0x5AU;
    assert(ByteRing_WriteFromISR(&ring, input, 1U) == 1U);
    assert(ByteRing_Read(&ring, output, 1U) == 1U);
    assert(output[0] == 0x5AU);

    ByteRing_Init(&ring);
    fill_pattern(input, BYTE_RING_CAPACITY - 1U, 0x10U);
    assert(ByteRing_WriteFromISR(&ring, input, BYTE_RING_CAPACITY - 1U) ==
           BYTE_RING_CAPACITY - 1U);
    assert(ByteRing_Size(&ring) == BYTE_RING_CAPACITY - 1U);

    assert(ByteRing_Read(&ring, output, 900U) == 900U);
    for (i = 0U; i < 900U; ++i) {
        assert(output[i] == input[i]);
    }

    fill_pattern(input, 901U, 0x80U);
    assert(ByteRing_WriteFromISR(&ring, input, 901U) == 901U);
    assert(ByteRing_Size(&ring) == BYTE_RING_CAPACITY);
    assert(ByteRing_Read(&ring, output, BYTE_RING_CAPACITY) ==
           BYTE_RING_CAPACITY);
    for (i = 0U; i < 123U; ++i) {
        assert(output[i] == (uint8_t)(0x10U + 900U + i));
    }
    for (i = 0U; i < 901U; ++i) {
        assert(output[123U + i] == input[i]);
    }

    ByteRing_Init(&ring);
    fill_pattern(input, BYTE_RING_CAPACITY + 1U, 0x20U);
    assert(ByteRing_WriteFromISR(&ring, input, BYTE_RING_CAPACITY + 1U) ==
           BYTE_RING_CAPACITY);
    assert(ring.high_watermark == BYTE_RING_CAPACITY);
    assert(ring.overflow_count == 1U);
    assert(ring.dropped_bytes == 1U);
    assert(ByteRing_Read(&ring, output, BYTE_RING_CAPACITY) ==
           BYTE_RING_CAPACITY);
    for (i = 0U; i < BYTE_RING_CAPACITY; ++i) {
        assert(output[i] == input[i]);
    }

    ByteRing_Init(&ring);
    fill_pattern(input, 16U, 0x40U);
    assert(ByteRing_WriteFromISR(&ring, input, 16U) == 16U);
    ByteRing_DiscardAll(&ring);
    assert(ByteRing_Size(&ring) == 0U);
    assert(ByteRing_WriteFromISR(NULL, input, 1U) == 0U);
    assert(ByteRing_WriteFromISR(&ring, NULL, 1U) == 0U);
    assert(ByteRing_Read(NULL, output, 1U) == 0U);
    assert(ByteRing_Read(&ring, NULL, 1U) == 0U);

    puts("test_byte_ring: PASS");
    return 0;
}
