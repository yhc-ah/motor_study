#include "byte_ring.h"

#include <stddef.h>

#if defined(BYTE_RING_HOST_TEST)
#define BYTE_RING_BARRIER() ((void)0)
#else
#include "cmsis_compiler.h"
#define BYTE_RING_BARRIER() __DMB()
#endif

void ByteRing_Init(ByteRing *ring)
{
    if (ring == NULL) {
        return;
    }

    ring->head = 0U;
    ring->tail = 0U;
    ring->high_watermark = 0U;
    ring->overflow_count = 0U;
    ring->dropped_bytes = 0U;
}

uint32_t ByteRing_Size(const ByteRing *ring)
{
    if (ring == NULL) {
        return 0U;
    }

    return ring->head - ring->tail;
}

uint32_t ByteRing_Free(const ByteRing *ring)
{
    uint32_t used = ByteRing_Size(ring);

    if (used > BYTE_RING_CAPACITY) {
        return 0U;
    }

    return BYTE_RING_CAPACITY - used;
}

uint32_t ByteRing_WriteFromISR(ByteRing *ring,
                               const uint8_t *data,
                               uint32_t length)
{
    uint32_t head;
    uint32_t tail;
    uint32_t used;
    uint32_t free_space;
    uint32_t write_length;
    uint32_t occupancy;
    uint32_t i;

    if ((ring == NULL) || ((data == NULL) && (length != 0U))) {
        return 0U;
    }

    head = ring->head;
    tail = ring->tail;
    used = head - tail;
    free_space = (used < BYTE_RING_CAPACITY) ?
                 (BYTE_RING_CAPACITY - used) : 0U;
    write_length = (length < free_space) ? length : free_space;

    for (i = 0U; i < write_length; ++i) {
        ring->data[(head + i) & BYTE_RING_MASK] = data[i];
    }

    BYTE_RING_BARRIER();
    ring->head = head + write_length;

    occupancy = used + write_length;
    if (occupancy > ring->high_watermark) {
        ring->high_watermark = occupancy;
    }

    if (write_length < length) {
        ring->overflow_count++;
        ring->dropped_bytes += length - write_length;
    }

    return write_length;
}

uint32_t ByteRing_Read(ByteRing *ring, uint8_t *data, uint32_t length)
{
    uint32_t head;
    uint32_t tail;
    uint32_t available;
    uint32_t read_length;
    uint32_t i;

    if ((ring == NULL) || ((data == NULL) && (length != 0U))) {
        return 0U;
    }

    tail = ring->tail;
    head = ring->head;
    available = head - tail;
    if (available > BYTE_RING_CAPACITY) {
        available = BYTE_RING_CAPACITY;
    }
    read_length = (length < available) ? length : available;

    for (i = 0U; i < read_length; ++i) {
        data[i] = ring->data[(tail + i) & BYTE_RING_MASK];
    }

    BYTE_RING_BARRIER();
    ring->tail = tail + read_length;
    return read_length;
}

void ByteRing_DiscardAll(ByteRing *ring)
{
    if (ring == NULL) {
        return;
    }

    BYTE_RING_BARRIER();
    ring->tail = ring->head;
}
