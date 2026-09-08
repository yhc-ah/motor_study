#include "tx_queue.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    TxQueue q;uint8_t b[TX_FRAME_MAX];uint16_t n;unsigned i;
    memset(b,0x55,sizeof(b));TxQueue_Init(&q);
    assert(!TxQueue_Push(&q,b,0));assert(!TxQueue_Push(&q,b,TX_FRAME_MAX+1));
    for(i=0;i<TX_QUEUE_DEPTH;i++){b[0]=(uint8_t)i;assert(TxQueue_Push(&q,b,sizeof(b)));}
    assert(!TxQueue_Push(&q,b,1));assert(q.dropped==1);
    for(i=0;i<TX_QUEUE_DEPTH;i++) {
        const uint8_t *p=TxQueue_Peek(&q,&n);assert(n==sizeof(b)&&p[0]==i);
        assert(TxQueue_Peek(&q,&n)==p);TxQueue_Pop(&q);
    }
    assert(TxQueue_Peek(&q,&n)==0);TxQueue_Pop(&q);
    b[0]=91;assert(TxQueue_Push(&q,b,3));b[0]=0;
    assert(TxQueue_Peek(&q,&n)[0]==91 && n==3);
    assert(q.high_watermark==TX_QUEUE_DEPTH);puts("test_tx_queue: PASS");return 0;
}
