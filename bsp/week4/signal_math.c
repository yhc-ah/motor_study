#include "signal_math.h"

int W4_PwmCalculate(uint32_t timer_hz, uint32_t hz,
                    uint16_t duty_permille, uint8_t center, W4PwmConfig *out)
{
    uint32_t period;
    W4PwmConfig result;

    if (out == 0 || duty_permille > 1000u || center > 1u ||
        (hz != 1000u && hz != 10000u && hz != 20000u) ||
        timer_hz < 1000000u || timer_hz % 1000000u != 0u) {
        return 0;
    }
    /* Validation bounds all arithmetic: period <= 1000, duty <= 1000.
     * The uint32_t timer clock limits PSC to 4293, safely within 16 bits.
     */
    period = 1000000u / (hz * (center ? 2u : 1u));
    result.psc = timer_hz / 1000000u - 1u;
    result.arr = period - (center ? 0u : 1u);
    result.ccr = (period * duty_permille + 500u) / 1000u;
    result.hz = hz;
    result.center = center;
    *out = result;
    return 1;
}

int W4_EncoderDelta(uint16_t previous, uint16_t current, int32_t *out)
{
    uint16_t distance = (uint16_t)((uint32_t)current - (uint32_t)previous);
    if (out == 0 || distance == 32768u) {
        return 0;
    }
    /* Avoid implementation-defined unsigned-to-signed narrowing. */
    *out = distance < 32768u ? (int32_t)distance : (int32_t)distance - 65536;
    return 1;
}

float W4_Filter(float previous, uint16_t raw, float alpha)
{
    return previous + alpha * ((float)raw - previous);
}
