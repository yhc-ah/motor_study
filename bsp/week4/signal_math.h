#ifndef WEEK4_SIGNAL_MATH_H
#define WEEK4_SIGNAL_MATH_H
#include <stdint.h>

typedef struct {
    uint32_t psc;
    uint32_t arr;
    uint32_t ccr;
    uint32_t hz;
    uint8_t center;
} W4PwmConfig;

/* Return 1 on success, 0 on invalid input; output is unchanged on failure.
 * Timer input must be an integer multiple of 1 MHz. PWM supports 1/10/20 kHz.
 * Duty is 0..1000 permille; compare is rounded to the nearest counter tick.
 */
int W4_PwmCalculate(uint32_t timer_hz, uint32_t hz,
                    uint16_t duty_permille, uint8_t center, W4PwmConfig *out);
/* Shortest signed 16-bit modular displacement. Exactly half a turn is ambiguous. */
int W4_EncoderDelta(uint16_t previous, uint16_t current, int32_t *out);
/* Caller supplies finite state and alpha in [0,1], and initializes from first raw sample. */
float W4_Filter(float previous, uint16_t raw, float alpha);
#endif
