#ifndef BSP_WEEK4_H
#define BSP_WEEK4_H
#include <stdint.h>
#define W4_BLOCK_POINTS 100U
typedef struct {
    uint32_t sequence, first_sample, done_us;
    uint16_t count, flags, raw[W4_BLOCK_POINTS];
} W4AdcBlock;
typedef struct {
    uint32_t running, samples_completed, blocks_completed;
    uint32_t overwritten_blocks, copy_races, adc_overruns, dma_errors;
    uint32_t max_consume_us, ht_count, tc_count, tail_samples;
    uint32_t encoder_cnt; int32_t encoder_position, encoder_speed;
    uint32_t generator_steps, generator_target, generator_active;
    uint32_t pwm_hz, pwm_arr, pwm_ccr, hardware_errors;
} W4HardwareStats;
void W4HW_Init(void);
void W4HW_Run(void);
int W4HW_StartAdc(void);
int W4HW_StopAdc(void);
int W4HW_TakeBlock(W4AdcBlock *out);
const W4HardwareStats *W4HW_Stats(void);
int W4HW_Pwm(uint32_t hz,uint16_t duty,uint8_t center,uint8_t enabled);
int W4HW_EncoderStart(uint32_t hz,uint32_t cycles,int32_t direction,uint32_t initial);
void W4HW_EncoderStop(void);
int W4HW_AdcCycles(uint32_t cycles);
int W4HW_Poll(uint32_t *raw);
void W4HW_DMA_IRQHandler(void);
void W4HW_ADC_IRQHandler(void);
void W4HW_GeneratorIRQ(void);
#endif
