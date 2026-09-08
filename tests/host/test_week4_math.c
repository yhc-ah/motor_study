#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include "signal_math.h"

static unsigned checks;
static unsigned failures;
#define CHECK(expr) do { checks++; if (!(expr)) { failures++; \
    if (failures <= 30u) printf("FAIL line %d: %s\n", __LINE__, #expr); } } while (0)

static void test_pwm(void)
{
    const uint32_t frequencies[] = {1000u, 10000u, 20000u};
    W4PwmConfig cfg = {0u, 0u, 0u, 0u, 0u};
    unsigned i, center, duty;
    for (i = 0; i < 3; ++i) {
        for (center = 0; center <= 1; ++center) {
            uint32_t period = 1000000u / (frequencies[i] * (center ? 2u : 1u));
            for (duty = 0; duty <= 1000; ++duty) {
                if (!W4_PwmCalculate(84000000u, frequencies[i], (uint16_t)duty,
                                     (uint8_t)center, &cfg)) {
                    CHECK(0 && "valid PWM rejected");
                    break;
                }
                CHECK(cfg.psc == 83u);
                CHECK(cfg.arr == period - (center ? 0u : 1u));
                CHECK(cfg.ccr == (period * duty + 500u) / 1000u);
                CHECK(cfg.hz == frequencies[i] && cfg.center == center);
            }
        }
    }
    CHECK(W4_PwmCalculate(84000000u, 20000u, 250u, 0u, &cfg));
    CHECK(cfg.ccr == 13u); /* 13/50 = 26%, nearest available tick to 25%. */
    cfg.psc = 99; cfg.arr = 98; cfg.ccr = 97; cfg.hz = 96; cfg.center = 1;
    CHECK(!W4_PwmCalculate(84000000u, 999u, 500u, 0u, &cfg));
    CHECK(!W4_PwmCalculate(84000000u, 0u, 500u, 0u, &cfg));
    CHECK(!W4_PwmCalculate(84000000u, UINT32_MAX, 500u, 0u, &cfg));
    CHECK(!W4_PwmCalculate(0u, 1000u, 500u, 0u, &cfg));
    CHECK(!W4_PwmCalculate(999999u, 1000u, 500u, 0u, &cfg));
    CHECK(!W4_PwmCalculate(84000001u, 1000u, 500u, 0u, &cfg));
    CHECK(!W4_PwmCalculate(UINT32_MAX, 1000u, 500u, 0u, &cfg));
    CHECK(!W4_PwmCalculate(84000000u, 1000u, 1001u, 0u, &cfg));
    CHECK(!W4_PwmCalculate(84000000u, 1000u, 500u, 2u, &cfg));
    CHECK(!W4_PwmCalculate(84000000u, 1000u, 500u, 0u, 0));
    CHECK(cfg.psc == 99 && cfg.arr == 98 && cfg.ccr == 97 && cfg.hz == 96 && cfg.center == 1);
    CHECK(W4_PwmCalculate(1000000u, 1000u, 1000u, 0u, &cfg));
    CHECK(cfg.psc == 0u && cfg.arr == 999u && cfg.ccr == 1000u);
    CHECK(W4_PwmCalculate(4294000000u, 20000u, 1000u, 1u, &cfg));
    CHECK(cfg.psc == 4293u && cfg.arr == 25u && cfg.ccr == 25u);
}

static void test_encoder(void)
{
    int32_t delta = 123;
    CHECK(W4_EncoderDelta(65530u, 4u, &delta) && delta == 10);
    CHECK(W4_EncoderDelta(4u, 65530u, &delta) && delta == -10);
    CHECK(W4_EncoderDelta(100u, 100u, &delta) && delta == 0);
    CHECK(W4_EncoderDelta(0u, 32767u, &delta) && delta == 32767);
    CHECK(W4_EncoderDelta(0u, 32769u, &delta) && delta == -32767);
    delta = 123;
    CHECK(!W4_EncoderDelta(0u, 32768u, &delta));
    CHECK(!W4_EncoderDelta(32768u, 0u, &delta));
    CHECK(delta == 123);
    CHECK(!W4_EncoderDelta(0u, 1u, 0));
}

static void test_filter(void)
{
    float value = 0.0f;
    unsigned block, sample;
    CHECK(W4_Filter(100.0f, 300u, 0.25f) == 150.0f);
    CHECK(W4_Filter(100.0f, 300u, 0.0f) == 100.0f);
    CHECK(W4_Filter(100.0f, 300u, 1.0f) == 300.0f);
    /* A 1 kHz sample rate and alpha=.1 reaches ~41% at 5 ms, ~88% at 20 ms. */
    for (sample = 1; sample <= 20; ++sample) {
        value = W4_Filter(value, 1000u, 0.1f);
        if (sample == 5) CHECK(fabsf(value - 409.51f) < 0.01f);
    }
    CHECK(fabsf(value - 878.4233f) < 0.01f);
    /* DMA block boundaries must not introduce a filter state reset. */
    value = 0.0f;
    for (block = 0; block < 100; ++block) {
        for (sample = 0; sample < 32; ++sample) {
            unsigned n = block * 32 + sample + 1;
            double expected = 1000.0 * (1.0 - pow(0.99, (double)n));
            value = W4_Filter(value, 1000u, 0.01f);
            CHECK(fabs((double)value - expected) < 0.004);
        }
    }
}
int main(void)
{
    test_pwm(); test_encoder(); test_filter();
    printf("Week4 signal math: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
