#include "app_sensors.h"
#include "bsp_sensors.h"
#include "bsp_uart_tx.h"
#include "frame_codec.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t tick,micro,imu_frames,cmd;
static uint8_t regs[128],out[240];
static uint16_t outlen;
static TxQueue q;
static SensorBusStats bus;
uint32_t HAL_GetTick(void){return tick;}
uint32_t BSP_Micros(void){return micro;}
void BSP_UART_TxService(void){}
const TxQueue *BSP_UART_TxStats(void){return &q;}
uint32_t BSP_UART_TxErrors(void){return 0;}
uint8_t BSP_LED_SetById(uint8_t id,uint8_t on){assert(id==2&&on<=1);return 1;}
static void capture(const ParsedFrame *f,void *ctx){
    (void)ctx;cmd=f->command;outlen=f->payload_length;memcpy(out,f->payload,outlen);
    if(cmd==0xa0){imu_frames++;assert(outlen==30);}
}
HAL_StatusTypeDef BSP_UART_SendQueued(const uint8_t *p,uint16_t n){
    FrameParser parser;unsigned i;FrameParser_Init(&parser);
    for(i=0;i<n;i++)FrameParser_PushByte(&parser,p[i],tick,capture,0);
    assert(parser.stats.crc_errors==0);return HAL_OK;
}
int BSP_SensorRead(void *ctx,uint8_t a,uint8_t reg,uint8_t *p,uint16_t n){
    (void)ctx;(void)a;if(reg==0x3a)regs[reg]=(tick%2==0);memcpy(p,regs+reg,n);micro+=400;return 0;
}
int BSP_SensorWrite(void *ctx,uint8_t a,uint8_t r,uint8_t v){
    (void)ctx;(void)a;regs[r]=(r==0x6b&&v==0x80)?0:v;return 0;
}
void BSP_SensorRecover(void){}
void BSP_ImuEventSnapshot(uint32_t *c,uint32_t *u){*c=0;*u=0;}
const SensorBusStats *BSP_SensorStats(void){return &bus;}
int BSP_FlashTransfer(void *ctx,const uint8_t *tx,uint8_t *rx,uint16_t n){
    (void)ctx;assert(tx[0]==0x9f && n==4);rx[0]=0;rx[1]=0xef;rx[2]=0x40;rx[3]=0x18;return 0;
}
void BSP_FlashSelect(void *ctx,uint8_t v){(void)ctx;(void)v;}
static void command(uint8_t c,const uint8_t *p,uint16_t n){
    ParsedFrame f={42,c,p,n};assert(APP_Sensors_Command(&f));
}
int main(void){
    uint8_t enable=1,disable=0;regs[0x75]=0x68;APP_Sensors_Init();
    command(0x10,&enable,1);assert(cmd==0x90&&outlen==1&&out[0]==1);
    for(tick=0;tick<1200;tick++){micro=tick*1000;APP_Sensors_Run();}
    assert(imu_frames>450 && imu_frames<510);
    command(0x11,0,0);assert(cmd==0x91&&outlen==128);
    assert(out[24*4]==8); /* only explicitly configured host flash capability */
    command(0x12,(const uint8_t*)"W3OK",4);assert(cmd==0x92&&(int32_t)(uint32_t)out[0]!=0);
    command(0x10,&disable,1);command(0x12,(const uint8_t*)"W3OK",4);
    assert(cmd==0x92&&out[0]==0);
    command(0x10,&enable,1);assert(cmd==0xff);
    puts("test_app_sensors: PASS (host model)");return 0;
}
