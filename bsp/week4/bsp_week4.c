#include "bsp_week4.h"
#include "week4_config.h"
#include "adc_handover.h"
#include "signal_math.h"
#include "stm32f4xx_hal.h"
#include "bsp_sensors.h"
#include "bsp_led.h"
#include <string.h>
#if APP_WEEK4
static ADC_HandleTypeDef adc;
static DMA_HandleTypeDef dma;
static TIM_HandleTypeDef encoder;
/* Ordinary SRAM (.bss), not CCM. DMA width and length are halfword elements. */
static volatile uint16_t adc_buffer[200];
static W4Handover handover;
static W4HardwareStats state;
static volatile uint32_t generator_steps,generator_target,adc_error,dma_error,generator_finished_us;
static volatile uint8_t generator_active,generator_phase,generator_finished;
static int32_t generator_direction;
static uint32_t adc_cycles=84, last_encoder_us, heartbeat_ms;
static uint16_t last_encoder;
static uint16_t terminal_count,terminal_offset;
static uint32_t terminal_done;
static uint8_t pwm_center,pwm_enabled,initialized,heartbeat,adc_fault_latched;
static uint32_t enter(void){uint32_t key=__get_PRIMASK();__disable_irq();return key;}
static void leave(uint32_t key){__DMB();if(!key)__enable_irq();}
static uint32_t timer_clock(void){uint32_t c=HAL_RCC_GetPCLK1Freq();if(RCC->CFGR&RCC_CFGR_PPRE1)c*=2U;return c;}
static void gpio(GPIO_TypeDef *port,uint32_t pins,uint32_t mode,uint32_t alternate){
    GPIO_InitTypeDef g={0};g.Pin=pins;g.Mode=mode;g.Pull=GPIO_NOPULL;g.Speed=GPIO_SPEED_FREQ_HIGH;g.Alternate=alternate;HAL_GPIO_Init(port,&g);
}
static int configure_adc(uint8_t external){
    ADC_ChannelConfTypeDef channel={0};
    adc.Instance=ADC1;adc.Init.ClockPrescaler=ADC_CLOCK_SYNC_PCLK_DIV4;
    adc.Init.Resolution=ADC_RESOLUTION_12B;adc.Init.ScanConvMode=DISABLE;
    adc.Init.ContinuousConvMode=DISABLE;adc.Init.DiscontinuousConvMode=DISABLE;
    adc.Init.ExternalTrigConvEdge=external?ADC_EXTERNALTRIGCONVEDGE_RISING:ADC_EXTERNALTRIGCONVEDGE_NONE;
    adc.Init.ExternalTrigConv=external?ADC_EXTERNALTRIGCONV_T3_TRGO:ADC_SOFTWARE_START;
    adc.Init.DataAlign=ADC_DATAALIGN_RIGHT;adc.Init.NbrOfConversion=1;
    adc.Init.DMAContinuousRequests=external?ENABLE:DISABLE;adc.Init.EOCSelection=ADC_EOC_SINGLE_CONV;
    if(HAL_ADC_Init(&adc)!=HAL_OK)return -3;
    channel.Channel=ADC_CHANNEL_10;channel.Rank=1;
    channel.SamplingTime=adc_cycles==84?ADC_SAMPLETIME_84CYCLES:ADC_SAMPLETIME_480CYCLES;
    return HAL_ADC_ConfigChannel(&adc,&channel)==HAL_OK?0:-3;
}
void W4HW_Init(void){
    TIM_Encoder_InitTypeDef e={0};memset(&state,0,sizeof(state));W4H_Reset(&handover);
    __HAL_RCC_GPIOA_CLK_ENABLE();__HAL_RCC_GPIOB_CLK_ENABLE();__HAL_RCC_GPIOC_CLK_ENABLE();__HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();__HAL_RCC_TIM4_CLK_ENABLE();__HAL_RCC_TIM6_CLK_ENABLE();__HAL_RCC_TIM8_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();__HAL_RCC_DMA2_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOB,GPIO_PIN_6,GPIO_PIN_RESET);gpio(GPIOB,GPIO_PIN_6,GPIO_MODE_OUTPUT_PP,0);
    HAL_GPIO_WritePin(GPIOG,GPIO_PIN_2|GPIO_PIN_3,GPIO_PIN_RESET);gpio(GPIOG,GPIO_PIN_2|GPIO_PIN_3,GPIO_MODE_OUTPUT_PP,0);
    HAL_GPIO_WritePin(GPIOC,GPIO_PIN_2,GPIO_PIN_RESET);gpio(GPIOC,GPIO_PIN_2,GPIO_MODE_OUTPUT_PP,0);
    gpio(GPIOC,GPIO_PIN_0,GPIO_MODE_ANALOG,0);
    gpio(GPIOC,GPIO_PIN_6|GPIO_PIN_7,GPIO_MODE_AF_PP,GPIO_AF3_TIM8);
    gpio(GPIOA,GPIO_PIN_6,GPIO_MODE_AF_PP,GPIO_AF2_TIM3);
    encoder.Instance=TIM8;encoder.Init.Prescaler=0;encoder.Init.CounterMode=TIM_COUNTERMODE_UP;
    encoder.Init.Period=65535;encoder.Init.ClockDivision=TIM_CLOCKDIVISION_DIV1;
    e.EncoderMode=TIM_ENCODERMODE_TI12;e.IC1Polarity=e.IC2Polarity=TIM_ICPOLARITY_RISING;
    e.IC1Selection=e.IC2Selection=TIM_ICSELECTION_DIRECTTI;e.IC1Prescaler=e.IC2Prescaler=TIM_ICPSC_DIV1;
    if(HAL_TIM_Encoder_Init(&encoder,&e)!=HAL_OK || HAL_TIM_Encoder_Start(&encoder,TIM_CHANNEL_ALL)!=HAL_OK){state.hardware_errors++;return;}
    TIM3->CR1=TIM_CR1_ARPE;TIM3->PSC=timer_clock()/1000000U-1U;TIM3->ARR=999;TIM3->CCR1=500;
    TIM3->CCMR1=TIM_CCMR1_OC1M_1|TIM_CCMR1_OC1M_2|TIM_CCMR1_OC1PE;TIM3->CCER=TIM_CCER_CC1E;
    /* Set TRGO only AFTER UG; initialization must not create an ADC sample. */
    TIM3->CR2=0;TIM3->EGR=TIM_EGR_UG;TIM3->SR=0;TIM3->CR2=TIM_CR2_MMS_1;
    dma.Instance=DMA2_Stream0;dma.Init.Channel=DMA_CHANNEL_0;dma.Init.Direction=DMA_PERIPH_TO_MEMORY;
    dma.Init.PeriphInc=DMA_PINC_DISABLE;dma.Init.MemInc=DMA_MINC_ENABLE;
    dma.Init.PeriphDataAlignment=DMA_PDATAALIGN_HALFWORD;dma.Init.MemDataAlignment=DMA_MDATAALIGN_HALFWORD;
    dma.Init.Mode=DMA_CIRCULAR;dma.Init.Priority=DMA_PRIORITY_HIGH;dma.Init.FIFOMode=DMA_FIFOMODE_DISABLE;
    if(HAL_DMA_Init(&dma)!=HAL_OK){state.hardware_errors++;return;}
    __HAL_LINKDMA(&adc,DMA_Handle,dma);
    if(configure_adc(1)){state.hardware_errors++;return;}
    HAL_NVIC_SetPriority(DMA2_Stream0_IRQn,4,0);HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);
    HAL_NVIC_SetPriority(ADC_IRQn,4,0);HAL_NVIC_EnableIRQ(ADC_IRQn);
    HAL_NVIC_SetPriority(TIM6_DAC_IRQn,6,0);HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
    last_encoder=(uint16_t)TIM8->CNT;last_encoder_us=BSP_Micros();heartbeat_ms=HAL_GetTick();initialized=1;
}
const W4HardwareStats *W4HW_Stats(void){
    uint32_t key=enter();state.blocks_completed=handover.completed;
    state.samples_completed=handover.completed*100U+(state.running?0U:state.tail_samples);
    state.overwritten_blocks=handover.overwritten;state.copy_races=handover.races;
    state.ht_count=handover.ht;state.tc_count=handover.tc;state.max_consume_us=handover.max_delay;
    state.adc_overruns=adc_error;state.dma_errors=dma_error+handover.order_errors;
    state.generator_steps=generator_steps;state.generator_target=generator_target;state.generator_active=generator_active;
    state.encoder_cnt=TIM8->CNT;leave(key);return &state;
}
int W4HW_StartAdc(void){
    if(!initialized||adc_fault_latched)return -3;if(state.running)return -2;
    if(dma.Instance->CR&DMA_SxCR_EN){state.hardware_errors++;return -3;}
    W4H_Reset(&handover);terminal_count=0;state.tail_samples=0;adc_error=dma_error=0;
    TIM3->CR1&=~TIM_CR1_CEN;TIM3->CR2=0;TIM3->CNT=0;TIM3->EGR=TIM_EGR_UG;TIM3->SR=0;
    if(configure_adc(1)){state.hardware_errors++;return -3;}
    __HAL_ADC_CLEAR_FLAG(&adc,ADC_FLAG_EOC|ADC_FLAG_OVR|ADC_FLAG_STRT);
    if(HAL_ADC_Start_DMA(&adc,(uint32_t *)(void *)adc_buffer,200)!=HAL_OK){state.hardware_errors++;return -3;}
    handover.running=1;state.running=1;TIM3->CR2=TIM_CR2_MMS_1;TIM3->CR1|=TIM_CR1_CEN;return 0;
}
int W4HW_StopAdc(void){
    uint32_t start,remaining,key;if(!state.running)return 0;
    TIM3->CR1&=~TIM_CR1_CEN;TIM3->CR2=0;
    /* Allow the last <=24us conversion to finish, with DMA interrupts enabled. */
    start=BSP_Micros();while((uint32_t)(BSP_Micros()-start)<100U){}
    HAL_NVIC_DisableIRQ(DMA2_Stream0_IRQn);HAL_NVIC_DisableIRQ(ADC_IRQn);
    /* Service pending HT/TC before HAL abort clears their flags. No conversions remain. */
    HAL_DMA_IRQHandler(&dma);remaining=__HAL_DMA_GET_COUNTER(&dma);
    /* HAL abort timeout needs SysTick: never mask global IRQs across this call. */
    if(HAL_ADC_Stop_DMA(&adc)!=HAL_OK){
        state.hardware_errors++;adc_fault_latched=1;state.running=0;handover.running=0;
        handover.overwritten+=handover.completed-handover.consumed;handover.consumed=handover.completed;
        terminal_count=0;state.tail_samples=0;return -3;
    }
    key=enter();
    handover.running=0;state.running=0;
    terminal_count=(uint16_t)((200U-remaining)%100U);
    terminal_offset=(uint16_t)((handover.completed&1U)*100U);terminal_done=BSP_Micros();
    if(adc_error||dma_error||handover.order_errors)terminal_count=0;
    state.tail_samples=terminal_count;leave(key);
    HAL_NVIC_ClearPendingIRQ(DMA2_Stream0_IRQn);HAL_NVIC_ClearPendingIRQ(ADC_IRQn);
    HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);HAL_NVIC_EnableIRQ(ADC_IRQn);return 0;
}
int W4HW_TakeBlock(W4AdcBlock *out){
    uint32_t key,ticket,ndtr,done;unsigned i;int ok;
    if(!initialized||adc_fault_latched)return 0;
    if(adc_error||dma_error||handover.order_errors){
        key=enter();handover.overwritten+=handover.completed-handover.consumed;handover.consumed=handover.completed;leave(key);return 0;
    }
    key=enter();ndtr=__HAL_DMA_GET_COUNTER(&dma);
    ok=W4H_Acquire(&handover,ndtr,BSP_Micros(),&ticket);done=ok?handover.half_done[(ticket-1U)&1U]:0;leave(key);
    if(ok){
        uint32_t offset=((ticket-1U)&1U)*100U;
        for(i=0;i<100;i++)out->raw[i]=adc_buffer[offset+i];
        __DMB();key=enter();ok=W4H_Validate(&handover,ticket,__HAL_DMA_GET_COUNTER(&dma),BSP_Micros());leave(key);
        if(!ok)return 0;
        out->sequence=ticket-1U;out->first_sample=(ticket-1U)*100U;out->done_us=done;out->count=100;out->flags=1;return 1;
    }
    if(!state.running && terminal_count){
        out->sequence=handover.completed;out->first_sample=handover.completed*100U;out->done_us=terminal_done;
        out->count=terminal_count;out->flags=3;
        for(i=0;i<terminal_count;i++)out->raw[i]=adc_buffer[terminal_offset+i];terminal_count=0;return 1;
    }
    return 0;
}
int W4HW_AdcCycles(uint32_t cycles){
    if(state.running)return -2;if(cycles!=84 && cycles!=480)return -1;adc_cycles=cycles;return 0;
}
int W4HW_Poll(uint32_t *raw){
    HAL_StatusTypeDef rc;if(!initialized||adc_fault_latched)return -3;if(state.running)return -2;
    if(configure_adc(0)){state.hardware_errors++;return -3;}
    rc=HAL_ADC_Start(&adc);if(rc==HAL_OK)rc=HAL_ADC_PollForConversion(&adc,2);
    if(rc==HAL_OK)*raw=HAL_ADC_GetValue(&adc);
    if(HAL_ADC_Stop(&adc)!=HAL_OK)rc=HAL_ERROR;
    if(rc!=HAL_OK){state.hardware_errors++;return -3;}return 0;
}
int W4HW_Pwm(uint32_t hz,uint16_t duty,uint8_t center,uint8_t enabled){
    W4PwmConfig cfg;if(!initialized)return -3;
    if(!W4_PwmCalculate(timer_clock(),hz,duty,center,&cfg))return -1;
    /* Keep compare beyond BOTH count directions at the full-high endpoint. */
    if(center && duty==1000U)cfg.ccr=cfg.arr+1U;
    if(!enabled){TIM4->CCER=0;TIM4->CR1&=~TIM_CR1_CEN;HAL_GPIO_WritePin(GPIOB,GPIO_PIN_6,GPIO_PIN_RESET);gpio(GPIOB,GPIO_PIN_6,GPIO_MODE_OUTPUT_PP,0);pwm_enabled=0;return 0;}
    if(pwm_enabled && state.pwm_hz==hz && pwm_center==center){TIM4->CCR1=cfg.ccr;}
    else{
        TIM4->CCER=0;TIM4->CR1=TIM_CR1_ARPE|(center?TIM_CR1_CMS_0:0U);
        TIM4->PSC=cfg.psc;TIM4->ARR=cfg.arr;TIM4->CCR1=cfg.ccr;TIM4->CNT=0;
        TIM4->CCMR1=TIM_CCMR1_OC1M_1|TIM_CCMR1_OC1M_2|TIM_CCMR1_OC1PE;
        TIM4->EGR=TIM_EGR_UG;TIM4->SR=0;gpio(GPIOB,GPIO_PIN_6,GPIO_MODE_AF_PP,GPIO_AF2_TIM4);
        TIM4->CCER=TIM_CCER_CC1E;TIM4->CR1|=TIM_CR1_CEN;
    }
    state.pwm_hz=hz;state.pwm_arr=cfg.arr;state.pwm_ccr=cfg.ccr;pwm_center=center;pwm_enabled=1;return 0;
}
int W4HW_EncoderStart(uint32_t hz,uint32_t cycles,int32_t direction,uint32_t initial){
    uint32_t key;if(!initialized)return -3;if(generator_active)return -2;
    if((hz!=100 && hz!=1000)||!cycles||cycles>100000||(direction!=1 && direction!=-1)||initial>65535)return -1;
    TIM6->CR1=0;TIM6->DIER=0;
    /* Disconnect receiver while resetting simulator phase; no startup edge is counted. */
    TIM8->CR1&=~TIM_CR1_CEN;GPIOG->BSRR=(GPIO_PIN_2|GPIO_PIN_3)<<16;
    TIM8->CNT=initial;TIM8->SR=0;last_encoder=(uint16_t)initial;state.encoder_position=state.encoder_speed=0;
    last_encoder_us=BSP_Micros();generator_phase=0;generator_direction=direction;generator_finished=0;
    key=enter();generator_steps=0;generator_target=cycles*4U;generator_active=1;leave(key);
    TIM6->PSC=timer_clock()/1000000U-1U;TIM6->ARR=1000000U/(hz*4U)-1U;
    TIM6->EGR=TIM_EGR_UG;TIM6->SR=0;TIM6->CNT=0;TIM8->CR1|=TIM_CR1_CEN;
    TIM6->DIER=TIM_DIER_UIE;TIM6->CR1=TIM_CR1_CEN;return 0;
}
void W4HW_EncoderStop(void){
    uint32_t key=enter();TIM6->CR1=0;TIM6->DIER=0;
    if(generator_active){generator_finished_us=BSP_Micros();generator_finished=1;}
    leave(key);
}
void W4HW_Run(void){
    uint32_t now=BSP_Micros(),dt=now-last_encoder_us,key;int32_t delta;uint16_t cnt;uint8_t final;
    if(!initialized)return;
    if(state.running && (adc_error||dma_error||handover.order_errors))W4HW_StopAdc();
    key=enter();now=BSP_Micros();dt=now-last_encoder_us;
    final=(uint8_t)(generator_finished && (uint32_t)(now-generator_finished_us)>=2U);
    /* Snapshot/count/finalize atomically against the last generator edge. */
    if(dt>=10000U || (final && dt)){cnt=(uint16_t)TIM8->CNT;
        if(dt>1000000U||!W4_EncoderDelta(last_encoder,cnt,&delta)){state.hardware_errors++;state.encoder_speed=0;}
        else{state.encoder_position=(int32_t)((uint32_t)state.encoder_position+(uint32_t)delta);state.encoder_speed=(int32_t)(((int64_t)delta*1000000)/dt);}
        last_encoder=cnt;last_encoder_us=now;
        if(final){generator_finished=0;generator_active=0;}
    }
    leave(key);
    now=HAL_GetTick();if((uint32_t)(now-heartbeat_ms)>=500U){heartbeat_ms=now;heartbeat^=1U;BSP_LED_SetById(2,heartbeat);}
}
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *h){if(h==&adc){GPIOC->BSRR=GPIO_PIN_2;W4H_Publish(&handover,0,BSP_Micros());GPIOC->BSRR=GPIO_PIN_2<<16;}}
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *h){if(h==&adc){GPIOC->BSRR=GPIO_PIN_2;W4H_Publish(&handover,1,BSP_Micros());GPIOC->BSRR=GPIO_PIN_2<<16;}}
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *h){if(h==&adc){if(h->ErrorCode&HAL_ADC_ERROR_OVR)adc_error++;if(h->ErrorCode&HAL_ADC_ERROR_DMA)dma_error++;}}
void W4HW_DMA_IRQHandler(void){HAL_DMA_IRQHandler(&dma);}
void W4HW_ADC_IRQHandler(void){HAL_ADC_IRQHandler(&adc);}
void W4HW_GeneratorIRQ(void){
    static const uint8_t pattern[4]={0,1,3,2};uint32_t set;
    if(!(TIM6->SR&TIM_SR_UIF))return;TIM6->SR=~TIM_SR_UIF;
    if(!generator_active)return;
    generator_phase=(uint8_t)((generator_phase+(generator_direction>0?1U:3U))&3U);
    set=((pattern[generator_phase]&2U)?GPIO_PIN_2:0U)|((pattern[generator_phase]&1U)?GPIO_PIN_3:0U);
    GPIOG->BSRR=(((GPIO_PIN_2|GPIO_PIN_3)&~set)<<16)|set;
    generator_steps++;if(generator_steps>=generator_target){TIM6->CR1=0;TIM6->DIER=0;generator_finished_us=BSP_Micros();generator_finished=1;}
}
#endif
