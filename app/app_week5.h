#ifndef APP_WEEK5_H
#define APP_WEEK5_H
#include "frame_parser.h"
void APP_Week5_Init(void);
void APP_Week5_Run(void);
int APP_Week5_Command(const ParsedFrame *);
/* Optional physical adapters. Foreground only, no retained caller buffers.
 * Capabilities: bit0 LCD, bit1 TH. Defaults absent; never simulated as present.
 * Step must return within 1000 us. TH owns I2C only during its one transaction,
 * uses deadlines for conversion, and invalidates stale readings internally.
 * Return 1=new observation/update, 0=pending, negative=error. */
uint32_t W5Extras_Capabilities(void);
int W5Extras_ThStep(uint32_t now_ms);
int W5Extras_DisplayStep(uint32_t now_ms,const uint32_t *status,unsigned count);
#endif
