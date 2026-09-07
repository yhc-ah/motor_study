#ifndef BYTE_RING_H
#define BYTE_RING_H

#include <stdint.h>

#define BYTE_RING_CAPACITY 1024U
#define BYTE_RING_MASK     (BYTE_RING_CAPACITY - 1U)

typedef struct {
    uint8_t data[BYTE_RING_CAPACITY];
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t high_watermark;
    volatile uint32_t overflow_count;
    volatile uint32_t dropped_bytes;
} ByteRing;

void ByteRing_Init(ByteRing *ring);
uint32_t ByteRing_Size(const ByteRing *ring);
uint32_t ByteRing_Free(const ByteRing *ring);
uint32_t ByteRing_WriteFromISR(ByteRing *ring,
                               const uint8_t *data,
                               uint32_t length);
uint32_t ByteRing_Read(ByteRing *ring, uint8_t *data, uint32_t length);
void ByteRing_DiscardAll(ByteRing *ring);

#endif
