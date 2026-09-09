#include "week5_timing.h"
#include <string.h>
void W5Period_Init(W5Period *p,uint32_t now,uint32_t period) {
    memset(p,0,sizeof(*p));p->period=period;p->next=now+period;
}
int W5Period_Due(W5Period *p,uint32_t now) {
    uint32_t skip,dt,e;
    if(!p->period || (int32_t)(now-p->next)<0)return 0;
    skip=(now-p->next)/p->period;
    p->missed+=skip;p->next+=(skip+1U)*p->period;
    if(p->calls) {
        dt=now-p->last;e=dt>p->period?dt-p->period:p->period-dt;
        if(e>p->max_error)p->max_error=e;
        if(e>=200U)p->violations++;
        p->histogram[e<200U?e:200U]++;p->intervals++;p->interval_sum+=dt;
    }
    p->last=now;p->calls++;return 1;
}
uint32_t W5Period_P99(const W5Period *p) {
    uint32_t i,total=0,rank=(uint32_t)(((uint64_t)p->intervals*99U+99U)/100U);
    if(!rank)return 0;
    for(i=0;i<201;i++){total+=p->histogram[i];if(total>=rank)return i<200?i:p->max_error;}
    return p->max_error;
}
void W5Profile_Add(W5Profile *p,uint32_t start,uint32_t end,uint32_t budget) {
    uint32_t dt=end-start;p->calls++;p->total_us+=dt;
    if(dt>p->max_us)p->max_us=dt;
    if(dt>budget)p->over_budget++;
}
