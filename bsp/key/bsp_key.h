#ifndef BSP_KEY_H
#define BSP_KEY_H

#include <stdbool.h>
#include <stdint.h>

extern volatile uint32_t key_irq_count;

bool BSP_Key_IsPressed(void);

#endif
