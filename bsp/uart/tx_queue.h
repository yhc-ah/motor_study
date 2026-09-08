#ifndef TX_QUEUE_H
#define TX_QUEUE_H
#include <stdint.h>
#define TX_FRAME_MAX 251U
#define TX_QUEUE_DEPTH 16U
/* Foreground owns queue. ISR only signals completion; it never mutates it. */
typedef struct {
    uint8_t data[TX_QUEUE_DEPTH][TX_FRAME_MAX];
    uint16_t lengths[TX_QUEUE_DEPTH];
    uint32_t dropped,high_watermark;
    uint8_t head,tail,count;
} TxQueue;
void TxQueue_Init(TxQueue *);
int TxQueue_Push(TxQueue *,const uint8_t *,uint16_t);
const uint8_t *TxQueue_Peek(TxQueue *,uint16_t *);
void TxQueue_Pop(TxQueue *);
#endif
