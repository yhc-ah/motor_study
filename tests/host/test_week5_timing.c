#include "week5_timing.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    W5Period p; W5Profile m={0}; unsigned i;
    W5Period_Init(&p,0xfffffff0U,2000);
    assert(!W5Period_Due(&p,0xfffffff0U));
    assert(W5Period_Due(&p,1984));
    assert(p.calls==1 && p.missed==0);
    assert(W5Period_Due(&p,3989));
    assert(p.max_error==5 && p.missed==0);
    assert(W5Period_Due(&p,10000));
    assert(p.missed==2 && p.calls==3 && p.max_error==4011);
    assert(!W5Period_Due(&p,10000));
    W5Period_Init(&p,0,2000);
    for(i=1;i<=900000;i++) assert(W5Period_Due(&p,i*2000U));
    assert(p.calls==900000 && !p.missed && !p.max_error);
    assert(W5Period_P99(&p)==0);
    W5Profile_Add(&m,0xfffffff0U,24,20);
    assert(m.calls==1 && m.total_us==40 && m.max_us==40 && m.over_budget==1);
    puts("week5 timing: wrap, absolute deadlines, skipped slots, 30min, profile PASS");
    return 0;
}
