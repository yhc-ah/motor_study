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

uint8_t BSP_LED_SetById(uint8_t led_id, uint8_t on)
{
    GPIO_TypeDef *port;
    uint16_t pin;
    GPIO_PinState level;

    switch (led_id) {
    case 0U:
        port = LED_R_GPIO_Port;
        pin = LED_R_Pin;
        break;
    case 1U:
        port = LED_G_GPIO_Port;
        pin = LED_G_Pin;
        break;
    case 2U:
        port = LED_B_GPIO_Port;
        pin = LED_B_Pin;
        break;
    default:
        return 0U;
    }

    level = (on != 0U) ? GPIO_PIN_RESET : GPIO_PIN_SET;
    HAL_GPIO_WritePin(port, pin, level);
    return 1U;
}
