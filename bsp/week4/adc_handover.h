#ifndef ADC_HANDOVER_H
#define ADC_HANDOVER_H
#include <stdint.h>
typedef struct {
    volatile uint32_t completed, done_us, ht, tc, order_errors;
    volatile uint32_t half_done[2];
    uint32_t consumed, overwritten, races, max_delay;
    uint8_t running;
} W4Handover;
void W4H_Reset(W4Handover *s);
void W4H_Publish(W4Handover *s,uint8_t half,uint32_t now);
int W4H_Acquire(W4Handover *s,uint32_t ndtr,uint32_t now,uint32_t *ticket);
int W4H_Validate(W4Handover *s,uint32_t ticket,uint32_t ndtr,uint32_t now);
#endif
