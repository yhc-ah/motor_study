#ifndef WEEK4_BOARD_STUBS_H
#define WEEK4_BOARD_STUBS_H
#include "stm32f4xx_hal.h"
extern GPIO_TypeDef test_gpioa,test_gpiob,test_gpioc,test_gpiog;
extern TIM_TypeDef test_tim3,test_tim4,test_tim6,test_tim8;
extern ADC_TypeDef test_adc;
extern DMA_Stream_TypeDef test_dma;
extern RCC_TypeDef test_rcc;
extern uint32_t test_primask;
#undef GPIOA
#undef GPIOB
#undef GPIOC
#undef GPIOG
#undef TIM3
#undef TIM4
#undef TIM6
#undef TIM8
#undef ADC1
#undef DMA2_Stream0
#undef RCC
#define GPIOA (&test_gpioa)
#define GPIOB (&test_gpiob)
#define GPIOC (&test_gpioc)
#define GPIOG (&test_gpiog)
#define TIM3 (&test_tim3)
#define TIM4 (&test_tim4)
#define TIM6 (&test_tim6)
#define TIM8 (&test_tim8)
#define ADC1 (&test_adc)
#define DMA2_Stream0 (&test_dma)
#define RCC (&test_rcc)
#define __get_PRIMASK() (test_primask)
#define __disable_irq() (test_primask=1U)
#define __enable_irq() (test_primask=0U)
#define __DMB() ((void)0)
#endif
