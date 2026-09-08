#ifndef APP_SENSORS_H
#define APP_SENSORS_H
#include "frame_parser.h"
#include <stdint.h>
void APP_Sensors_Init(void);
void APP_Sensors_Run(void);
/* 1 when this extension owns command; errors still receive a framed reply. */
int APP_Sensors_Command(const ParsedFrame *);
/* Integrate an actual board display adapter only after its controller/pins are
 * verified. Return1 only when real hardware is initialized; default returns0. */
uint8_t APP_DisplayAvailable(void);
void APP_DisplayStatus(const uint32_t *fields,uint32_t field_count);
#endif
