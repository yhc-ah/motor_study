#include "tim.h"
TIM_HandleTypeDef htim2;
void MX_TIM2_Init(void) {
    uint32_t clk=HAL_RCC_GetPCLK1Freq();
    if((RCC->CFGR & RCC_CFGR_PPRE1)!=0)clk*=2U;
    htim2.Instance=TIM2;htim2.Init.Prescaler=clk/1000000U-1U;
    htim2.Init.CounterMode=TIM_COUNTERMODE_UP;htim2.Init.Period=0xffffffffU;
    htim2.Init.ClockDivision=TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload=TIM_AUTORELOAD_PRELOAD_DISABLE;
    if(HAL_TIM_Base_Init(&htim2)!=HAL_OK)Error_Handler();
}
void HAL_TIM_Base_MspInit(TIM_HandleTypeDef *h) {
    if(h->Instance==TIM2)__HAL_RCC_TIM2_CLK_ENABLE();
}
