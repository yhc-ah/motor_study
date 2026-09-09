#include "week5_journal.h"
#include "crc16_modbus.h"
#include <string.h>
static int fail(W5Journal *j,int rc){j->errors++;j->result=rc;j->state=W5J_ERROR;return rc;}
static void put32(uint8_t *p,uint32_t v){unsigned i;for(i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
void W5Journal_Init(W5Journal *j,SpiFlash *f){memset(j,0,sizeof(*j));j->flash=f;}
int W5Journal_Prepare(W5Journal *j,uint32_t base,uint32_t size,uint32_t now){
    int rc;(void)now;
    if(!j || !j->flash || size!=20480U || (j->state!=W5J_OFF && j->state!=W5J_READY && j->state!=W5J_ERROR))return -1;
    rc=SpiFlash_Reserve(j->flash,base,size);if(rc)return rc;
    j->base=base;j->size=size;j->records=j->errors=j->cursor=0;j->background=0;j->result=0;j->state=W5J_ERASE_START;return 0;
}
int W5Journal_Append(W5Journal *j,const uint8_t p[48],uint32_t now){
    int rc;uint16_t crc;
    if(!j || !p || j->state!=W5J_READY || j->records>=256)return -1;
    memset(j->record,0,64);memcpy(j->record,"W5JR",4);put32(j->record+4,j->records);
    put32(j->record+8,now);memcpy(j->record+12,p,48);
    crc=CRC16_Modbus(j->record,62);j->record[62]=(uint8_t)crc;j->record[63]=(uint8_t)(crc>>8);
    rc=SpiFlash_StartProgram(j->flash,j->base+j->records*64,j->record,64,now);
    if(rc!=SPI_FLASH_BUSY)return fail(j,rc);
    j->state=W5J_PROGRAM;return 0;
}
int W5Journal_BackgroundErase(W5Journal *j,uint32_t now){
    (void)now;if(!j || j->state!=W5J_READY)return -1;
    j->background=1;j->cursor=16384;j->state=W5J_ERASE_START;return 0;
}
void W5Journal_Service(W5Journal *j,uint32_t now){
    uint8_t b[64];unsigned i;int rc;
    switch(j->state){
    case W5J_ERASE_START:
        rc=SpiFlash_StartErase(j->flash,j->base+j->cursor,now);
        if(rc!=SPI_FLASH_BUSY){fail(j,rc);return;}j->state=W5J_ERASE_WAIT;break;
    case W5J_ERASE_WAIT:
        rc=SpiFlash_Service(j->flash,now);if(rc<0){fail(j,rc);return;}
        if(!rc)j->state=W5J_BLANK;
        break;
    case W5J_BLANK:
        rc=SpiFlash_Read(j->flash,j->base+j->cursor,b,64);
        if(rc){fail(j,rc);return;}
        for(i=0;i<64;i++)if(b[i]!=255){fail(j,SPI_FLASH_E_VERIFY);return;}
        j->cursor+=64;
        if(j->cursor==j->size){j->state=W5J_READY;j->background=0;}
        else if(!(j->cursor%4096))j->state=W5J_ERASE_START;
        break;
    case W5J_PROGRAM:
        rc=SpiFlash_Service(j->flash,now);if(rc<0){fail(j,rc);return;}
        if(!rc)j->state=W5J_VERIFY;
        break;
    case W5J_VERIFY:
        rc=SpiFlash_Read(j->flash,j->base+j->records*64,b,64);
        if(rc || memcmp(b,j->record,64)){fail(j,rc?rc:SPI_FLASH_E_VERIFY);return;}
        j->records++;j->state=W5J_READY;break;
    default:break;
    }
}
