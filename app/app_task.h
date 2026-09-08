#ifndef APP_TASK_H
#define APP_TASK_H

#include <stdint.h>

extern volatile uint32_t app_key_press_count;

void APP_Task_Init(void);
void APP_Task_Run(void);

#endif
