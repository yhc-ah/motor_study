#include <assert.h>
#include <stdio.h>
#include "adc_handover.h"
int main(void){W4Handover s;uint32_t ticket;
 W4H_Reset(&s);s.running=1;assert(!W4H_Acquire(&s,200,0,&ticket));
 W4H_Publish(&s,0,100000);assert(s.ht==1&&s.completed==1);
 assert(W4H_Acquire(&s,80,120000,&ticket)&&ticket==1);
 assert(W4H_Validate(&s,ticket,79,120010));assert(s.max_delay==20010);
 assert(!W4H_Acquire(&s,79,120011,&ticket));
 W4H_Publish(&s,1,200000);W4H_Publish(&s,0,300000);
 assert(W4H_Acquire(&s,90,310000,&ticket)&&ticket==3&&s.overwritten==1);
 W4H_Publish(&s,1,400000);assert(!W4H_Validate(&s,ticket,199,400001)&&s.races==1);
 assert(W4H_Acquire(&s,198,400002,&ticket)&&ticket==4);
 assert(!W4H_Validate(&s,ticket,99,500001));
 W4H_Reset(&s);s.running=1;W4H_Publish(&s,0,0xfffffff0U);
 assert(W4H_Acquire(&s,50,10,&ticket));assert(W4H_Validate(&s,ticket,49,20));
 W4H_Reset(&s);s.running=1;W4H_Publish(&s,0,100000);
 assert(!W4H_Acquire(&s,150,200001,&ticket)&&s.overwritten==1);
 W4H_Reset(&s);s.running=1;W4H_Publish(&s,1,200000);assert(s.order_errors==1);
 W4H_Reset(&s);W4H_Publish(&s,0,100000);assert(W4H_Acquire(&s,199,900000,&ticket));assert(W4H_Validate(&s,ticket,199,900001));
 W4H_Reset(&s);W4H_Publish(&s,0,100000);W4H_Publish(&s,1,200000);
 assert(W4H_Acquire(&s,200,200001,&ticket)&&ticket==1);
 assert(W4H_Acquire(&s,200,200002,&ticket)&&ticket==2&&s.overwritten==0);
 assert(!W4H_Acquire(&s,200,200003,&ticket));
 puts("test_week4_handover: PASS");return 0;
}
