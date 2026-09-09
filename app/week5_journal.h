#ifndef WEEK5_JOURNAL_H
#define WEEK5_JOURNAL_H
#include "spi_flash.h"
enum {W5J_OFF,W5J_ERASE_START,W5J_ERASE_WAIT,W5J_BLANK,W5J_READY,W5J_PROGRAM,W5J_VERIFY,W5J_ERROR};
typedef struct {
    SpiFlash *flash;
    uint32_t base,size,cursor,records,errors;
    int result;
    uint8_t state,background,record[64];
} W5Journal;
void W5Journal_Init(W5Journal *,SpiFlash *);
/* Explicit caller-confirmed free region: exactly 16 KiB journal + 4 KiB scratch.
 * Destructively erases and checks every byte in small service steps. */
int W5Journal_Prepare(W5Journal *,uint32_t base,uint32_t size,uint32_t now);
int W5Journal_Append(W5Journal *,const uint8_t payload[48],uint32_t now);
int W5Journal_BackgroundErase(W5Journal *,uint32_t now);
/* At most one bounded SPI transaction per call; no waiting for internal BUSY. */
void W5Journal_Service(W5Journal *,uint32_t now);
#endif
