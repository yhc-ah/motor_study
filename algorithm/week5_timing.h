#ifndef WEEK5_TIMING_H
#define WEEK5_TIMING_H
#include <stdint.h>
/* All timestamps are modulo-2^32 microseconds. Service at least every 2^31 us.
 * Histogram bins 0..199 are exact; bin 200 is overflow. P99 returns the
 * observed maximum if its rank lands in overflow (a conservative upper bound). */
typedef struct {
    uint32_t next,period,last,calls,missed,max_error,violations,intervals;
    uint64_t interval_sum;
    uint32_t histogram[201];
} W5Period;
typedef struct {
    uint32_t calls,max_us,over_budget;
    uint64_t total_us;
} W5Profile;
void W5Period_Init(W5Period *,uint32_t now,uint32_t period);
int W5Period_Due(W5Period *,uint32_t now);
uint32_t W5Period_P99(const W5Period *);
void W5Profile_Add(W5Profile *,uint32_t start,uint32_t end,uint32_t budget);
#endif
