#include "bsp_led.h"
#include "main.h"

void BSP_LED_Init(void)
{
    BSP_LED_Off();
}

void BSP_LED_On(void)
{
    /* 霸天虎 LED 低电平点亮 */
    HAL_GPIO_WritePin(LED_R_GPIO_Port, LED_R_Pin, GPIO_PIN_RESET);
}

void BSP_LED_Off(void)
{
    HAL_GPIO_WritePin(LED_R_GPIO_Port, LED_R_Pin, GPIO_PIN_SET);
}

void BSP_LED_Toggle(void)
{
    HAL_GPIO_TogglePin(LED_R_GPIO_Port, LED_R_Pin);
}
