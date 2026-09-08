#include "adc_handover.h"
#include <string.h>
void W4H_Reset(W4Handover *s){memset(s,0,sizeof(*s));}
void W4H_Publish(W4Handover *s,uint8_t half,uint32_t now){
    if(half!=(s->completed&1U)){s->order_errors++;return;}
    if(half)s->tc++;else s->ht++;
    s->half_done[half]=now;s->done_us=now;s->completed++;
}
/* NDTR=0 is the wrap boundary: conservatively treat first half as active. */
static uint32_t active(uint32_t ndtr){return (!ndtr||ndtr>100U)?0U:1U;}
int W4H_Acquire(W4Handover *s,uint32_t ndtr,uint32_t now,uint32_t *ticket){
    uint32_t sequence=s->completed, pending=sequence-s->consumed;
    if(!pending)return 0;
    if(!s->running){
        uint32_t valid=(ndtr==0U||ndtr==100U||ndtr==200U)?2U:1U;
        if(pending>valid){s->overwritten+=pending-valid;s->consumed+=pending-valid;}
        *ticket=++s->consumed;return 1;
    }
    s->consumed=sequence;
    if(s->running && (ndtr>200U || ((sequence-1U)&1U)==active(ndtr) || (uint32_t)(now-s->done_us)>=100000U)){
        s->overwritten+=pending;return 0;
    }
    s->overwritten+=pending-1U;*ticket=sequence;return 1;
}
int W4H_Validate(W4Handover *s,uint32_t ticket,uint32_t ndtr,uint32_t now){
    uint32_t delay=now-s->done_us;
    if(s->running && (s->completed!=ticket || ndtr>200U || ((ticket-1U)&1U)==active(ndtr) || delay>=100000U)){
        s->races++;s->overwritten++;return 0;
    }
    if(s->running && delay>s->max_delay)s->max_delay=delay;
    return 1;
}
