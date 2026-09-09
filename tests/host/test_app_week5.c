#include "app_week5.h"
#include "bsp_sensors.h"
#include "bsp_uart_tx.h"
#include "frame_codec.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t micro,frames;static uint8_t regs[128],out[240],cmd;static TxQueue q;static SensorBusStats bus;static int flash_ok;
uint32_t HAL_GetTick(void){return micro/1000;}
uint32_t BSP_Micros(void){return micro;}
void BSP_UART_TxService(void){}
void APP_UART_Run(void){}
void APP_Week4_Init(void){}
void APP_Week4_Run(void){}
const TxQueue *BSP_UART_TxStats(void){return &q;}
uint32_t BSP_UART_TxErrors(void){return 0;}
const SensorBusStats *BSP_SensorStats(void){return &bus;}
int BSP_SensorRead(void *c,uint8_t a,uint8_t r,uint8_t *p,uint16_t n){(void)c;(void)a;regs[0x3a]=1;memcpy(p,regs+r,n);micro+=400;return 0;}
int BSP_SensorWrite(void *c,uint8_t a,uint8_t r,uint8_t v){(void)c;(void)a;regs[r]=v;micro+=100;return 0;}
void BSP_SensorRecover(void){}
void BSP_ImuEventSnapshot(uint32_t *n,uint32_t *t){*n=*t=0;}
int BSP_FlashTransfer(void *c,const uint8_t *t,uint8_t *r,uint16_t n){(void)c;memset(r,0,n);if(!flash_ok)return -1;if(t[0]==0x9f){r[1]=0xef;r[2]=0x40;r[3]=0x18;}if(t[0]==5)r[1]=2;if(t[0]==3)memset(r+4,255,n-4);return 0;}
void BSP_FlashSelect(void *c,uint8_t v){(void)c;(void)v;}
static void receive(const ParsedFrame *f,void *c){(void)c;cmd=f->command;memcpy(out,f->payload,f->payload_length);if(cmd==0xa0)frames++;}
HAL_StatusTypeDef BSP_UART_SendQueued(const uint8_t *p,uint16_t n){FrameParser d;unsigned i;FrameParser_Init(&d);for(i=0;i<n;i++)FrameParser_PushByte(&d,p[i],HAL_GetTick(),receive,0);return HAL_OK;}
static uint32_t field(unsigned n){return (uint32_t)out[4*n]|((uint32_t)out[4*n+1]<<8)|((uint32_t)out[4*n+2]<<16)|((uint32_t)out[4*n+3]<<24);}
int main(void){
    uint8_t p[2]={5,1};ParsedFrame f={1,0x30,p,2};unsigned i;
    regs[0x75]=0x68;APP_Week5_Init();assert(APP_Week5_Command(&f));assert(cmd==0xb8 && (int32_t)field(0)<0);
    for(i=0;i<10000;i++){micro+=50;APP_Week5_Run();}
    p[0]=2;assert(APP_Week5_Command(&f));assert((int32_t)field(0)==0);
    for(i=0;i<20000;i++){micro+=25;APP_Week5_Run();}
    f.command=0x31;f.payload_length=0;APP_Week5_Command(&f);
    assert(cmd==0xb9 && field(0)==1 && field(5)==1 && field(7)>100 && field(11)==0 && frames>100);
    assert(field(17)-field(16)==400 && field(12)<200);
    micro+=10000;APP_Week5_Run();APP_Week5_Command(&f);assert(field(11)>=4);
    p[0]=p[1]=0;f.command=0x30;f.payload_length=2;APP_Week5_Command(&f);
    {uint8_t prep[12]={'W','5','O','K',0,0,0,0,0,80,0,0};
     flash_ok=1;f.command=0x32;f.payload=prep;f.payload_length=12;APP_Week5_Command(&f);assert((int32_t)field(0)==0);
     for(i=0;i<100000;i++){micro+=50;APP_Week5_Run();}
     f.command=0x31;f.payload_length=0;APP_Week5_Command(&f);assert(field(4)&4);
     flash_ok=0;f.command=0x32;f.payload_length=12;APP_Week5_Command(&f);assert((int32_t)field(0)<0);
     f.command=0x31;f.payload_length=0;APP_Week5_Command(&f);assert(!(field(4)&4));}
    puts("week5 supervisor: missing hardware rejected, fresh stream, timestamps, overload, prepare failure PASS");return 0;
}
