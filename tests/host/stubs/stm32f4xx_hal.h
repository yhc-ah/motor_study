#ifndef STM32F4XX_HAL_H
#define STM32F4XX_HAL_H

#include <stdint.h>

typedef enum {
    HAL_OK = 0x00U,
    HAL_ERROR = 0x01U
} HAL_StatusTypeDef;

uint32_t HAL_GetTick(void);

#endif
