#include "tx_queue.h"
#include <string.h>
void TxQueue_Init(TxQueue *q){memset(q,0,sizeof(*q));}
int TxQueue_Push(TxQueue *q,const uint8_t *p,uint16_t n){
    if(!p || !n || n>TX_FRAME_MAX)return 0;
    if(q->count==TX_QUEUE_DEPTH){q->dropped++;return 0;}
    memcpy(q->data[q->head],p,n);q->lengths[q->head]=n;
    q->head=(uint8_t)((q->head+1U)%TX_QUEUE_DEPTH);q->count++;
    if(q->count>q->high_watermark)q->high_watermark=q->count;
    return 1;
}
const uint8_t *TxQueue_Peek(TxQueue *q,uint16_t *n){
    if(!q->count){*n=0;return 0;}*n=q->lengths[q->tail];return q->data[q->tail];
}
void TxQueue_Pop(TxQueue *q){
    if(q->count){q->tail=(uint8_t)((q->tail+1U)%TX_QUEUE_DEPTH);q->count--;}
}
