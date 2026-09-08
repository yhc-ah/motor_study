#include "week4_board_stubs.h"
#include "bsp_week4.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
GPIO_TypeDef test_gpioa,test_gpiob,test_gpioc,test_gpiog;
TIM_TypeDef test_tim3,test_tim4,test_tim6,test_tim8;
ADC_TypeDef test_adc;DMA_Stream_TypeDef test_dma;RCC_TypeDef test_rcc;
uint32_t test_primask;
static uint32_t micros;static int init_fail,abort_fail;
static ADC_HandleTypeDef *active_adc;static uint16_t *buffer;
uint32_t BSP_Micros(void){return ++micros;}
uint32_t HAL_GetTick(void){return micros/1000;}
uint32_t HAL_RCC_GetPCLK1Freq(void){return 42000000;}
uint8_t BSP_LED_SetById(uint8_t a,uint8_t b){(void)a;(void)b;return 1;}
void HAL_GPIO_Init(GPIO_TypeDef *g,GPIO_InitTypeDef *p){(void)g;(void)p;}
void HAL_GPIO_WritePin(GPIO_TypeDef *g,uint16_t p,GPIO_PinState s){if(s)g->ODR|=p;else g->ODR&=~p;}
void HAL_NVIC_SetPriority(IRQn_Type n,uint32_t p,uint32_t s){(void)n;(void)p;(void)s;}
void HAL_NVIC_EnableIRQ(IRQn_Type n){(void)n;}
void HAL_NVIC_DisableIRQ(IRQn_Type n){(void)n;}
void HAL_NVIC_ClearPendingIRQ(IRQn_Type n){(void)n;}
HAL_StatusTypeDef HAL_TIM_Encoder_Init(TIM_HandleTypeDef *h,TIM_Encoder_InitTypeDef *c){(void)h;(void)c;return init_fail?HAL_ERROR:HAL_OK;}
HAL_StatusTypeDef HAL_TIM_Encoder_Start(TIM_HandleTypeDef *h,uint32_t c){(void)h;(void)c;return HAL_OK;}
HAL_StatusTypeDef HAL_ADC_Init(ADC_HandleTypeDef *h){(void)h;return HAL_OK;}
HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *h,ADC_ChannelConfTypeDef *c){(void)h;(void)c;return HAL_OK;}
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *h){(void)h;return HAL_OK;}
HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *h,uint32_t *p,uint32_t n){assert(n==200);active_adc=h;buffer=(uint16_t*)p;test_dma.NDTR=n;test_dma.CR|=DMA_SxCR_EN;return HAL_OK;}
HAL_StatusTypeDef HAL_ADC_Stop_DMA(ADC_HandleTypeDef *h){(void)h;assert(!test_primask);if(abort_fail)return HAL_TIMEOUT;test_dma.CR&=~DMA_SxCR_EN;return HAL_OK;}
void HAL_DMA_IRQHandler(DMA_HandleTypeDef *h){(void)h;}
void HAL_ADC_IRQHandler(ADC_HandleTypeDef *h){(void)h;}
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *h){(void)h;return HAL_OK;}
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *h){(void)h;return HAL_OK;}
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *h,uint32_t t){(void)h;(void)t;return HAL_OK;}
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *h){(void)h;return 2048;}
static void half(unsigned h){unsigned i;for(i=0;i<100;i++)buffer[h*100+i]=(uint16_t)(h*100+i);micros+=100000;test_dma.NDTR=h?200:100;if(h)HAL_ADC_ConvCpltCallback(active_adc);else HAL_ADC_ConvHalfCpltCallback(active_adc);}
int main(void){W4AdcBlock b;const W4HardwareStats *s;unsigned i;
 test_rcc.CFGR=RCC_CFGR_PPRE1_DIV4;
 init_fail=1;W4HW_Init();assert(W4HW_StartAdc()<0);assert(!W4HW_TakeBlock(&b));
 init_fail=0;W4HW_Init();assert(!W4HW_StartAdc());half(0);test_dma.NDTR=90;
 assert(W4HW_TakeBlock(&b)&&b.count==100&&b.raw[99]==99&&b.sequence==0);
 for(i=0;i<7;i++)buffer[100+i]=(uint16_t)(3000+i);test_dma.NDTR=93;
 assert(!W4HW_StopAdc());assert(W4HW_TakeBlock(&b)&&b.flags==3&&b.count==7&&b.first_sample==100&&b.raw[6]==3006);
 assert(!W4HW_TakeBlock(&b));s=W4HW_Stats();assert(s->samples_completed==107&&s->tail_samples==7);
 assert(!W4HW_StartAdc());half(0);half(1);assert(!W4HW_StopAdc());
 assert(W4HW_TakeBlock(&b)&&b.sequence==0&&b.raw[99]==99);
 assert(W4HW_TakeBlock(&b)&&b.sequence==1&&b.raw[99]==199);assert(!W4HW_TakeBlock(&b));
 assert(W4HW_Stats()->overwritten_blocks==0);
 assert(!W4HW_EncoderStart(1000,1,1,65534));
 for(i=0;i<4;i++){test_tim6.SR=TIM_SR_UIF;W4HW_GeneratorIRQ();test_tim8.CNT=(test_tim8.CNT+1)&65535;}
 micros+=10;W4HW_Run();s=W4HW_Stats();assert(!s->generator_active&&s->generator_steps==4&&s->encoder_position==4&&s->encoder_cnt==2);
 assert(!W4HW_Pwm(10000,500,0,1));assert(test_tim4.ARR==99&&test_tim4.CCR1==50);
 assert(!W4HW_Pwm(10000,1000,1,1));assert(test_tim4.CCR1>test_tim4.ARR);
 assert(!W4HW_Pwm(10000,0,0,0));assert(!(test_tim4.CR1&TIM_CR1_CEN)&&!(test_gpiob.ODR&GPIO_PIN_6));
 assert(!W4HW_StartAdc());abort_fail=1;assert(W4HW_StopAdc()<0);assert(!W4HW_TakeBlock(&b)&&W4HW_StartAdc()<0);
 puts("test_week4_board: PASS (real BSP, simulated HAL/registers)");return 0;
}
