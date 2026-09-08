#ifndef APP_UART_H
#define APP_UART_H

#include <stdint.h>

extern volatile uint32_t app_uart_pause_request_ms;

void APP_UART_Init(void);
void APP_UART_Run(void);
void APP_UART_PauseConsumer(uint32_t duration_ms);

#endif
