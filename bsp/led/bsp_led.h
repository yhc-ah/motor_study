#ifndef BSP_LED_H
#define BSP_LED_H

#include <stdint.h>

void BSP_LED_Init(void);
void BSP_LED_On(void);
void BSP_LED_Off(void);
void BSP_LED_Toggle(void);
uint8_t BSP_LED_SetById(uint8_t led_id, uint8_t on);

#endif
