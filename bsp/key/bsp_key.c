#include "bsp_key.h"
#include "bsp_probe.h"
#include "main.h"
#include "bsp_sensors.h"
#include "week3_config.h"

volatile uint32_t key_irq_count = 0U;

bool BSP_Key_IsPressed(void)
{
    /*
     * PA0 松开为低，按下为高。
     * 若实际板卡极性相反，只改这里。
     */
    return HAL_GPIO_ReadPin(KEY1_GPIO_Port,
                            KEY1_Pin) == GPIO_PIN_SET;
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
#if WEEK3_MPU_PC4_INT
    if (GPIO_Pin == GPIO_PIN_4) { BSP_ImuIRQ(); return; }
#endif
    /*
     * 测试脚只包围回调函数内的处理。
     * 不能在这里打印、延时或处理 LED 业务。
     */
    BSP_Probe_High();

    if (GPIO_Pin == KEY1_Pin)
    {
        key_irq_count++;
    }

    BSP_Probe_Low();
}
