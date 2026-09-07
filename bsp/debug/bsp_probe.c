#include "bsp_probe.h"
#include "main.h"

void BSP_Probe_Init(void)
{
    BSP_Probe_Low();
}

void BSP_Probe_High(void)
{
    HAL_GPIO_WritePin(TEST_OUT_GPIO_Port,
                      TEST_OUT_Pin,
                      GPIO_PIN_SET);
}

void BSP_Probe_Low(void)
{
    HAL_GPIO_WritePin(TEST_OUT_GPIO_Port,
                      TEST_OUT_Pin,
                      GPIO_PIN_RESET);
}

void BSP_Probe_Toggle(void)
{
    HAL_GPIO_TogglePin(TEST_OUT_GPIO_Port, TEST_OUT_Pin);
}

void BSP_UART_ProbeHigh(void)
{
    TEST_OUT_GPIO_Port->BSRR = TEST_OUT_Pin;
}

void BSP_UART_ProbeLow(void)
{
    TEST_OUT_GPIO_Port->BSRR = ((uint32_t)TEST_OUT_Pin << 16U);
}
