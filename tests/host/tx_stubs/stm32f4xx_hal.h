#ifndef TX_STUB_HAL_H
#define TX_STUB_HAL_H
#include <stdint.h>
typedef enum {HAL_OK,HAL_ERROR,HAL_BUSY} HAL_StatusTypeDef;
typedef struct {void *Instance;} UART_HandleTypeDef;
#define USART1 ((void *)1)
uint32_t HAL_GetTick(void);
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *,uint8_t *,uint16_t);
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *);
#endif
