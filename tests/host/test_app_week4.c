#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app_week4.h"
#include "bsp_week4.h"
#include "bsp_uart_tx.h"
#include "frame_codec.h"
static W4HardwareStats hw;
static uint32_t now, starts, pwm_calls, polls, sample_available;
static TxQueue queue;
static uint8_t sent[64][251]; static uint16_t lengths[64];static unsigned sent_count;
uint32_t HAL_GetTick(void){return now;}
uint32_t BSP_Micros(void){return now*1000U;}
const W4HardwareStats *W4HW_Stats(void){return &hw;}
void W4HW_Init(void){memset(&hw,0,sizeof(hw));}
void W4HW_Run(void){}
int W4HW_StartAdc(void){starts++;hw.running=1;return 0;}
int W4HW_StopAdc(void){hw.running=0;return 0;}
int W4HW_TakeBlock(W4AdcBlock *b){unsigned i;if(!sample_available)return 0;sample_available=0;memset(b,0,sizeof(*b));b->count=100;b->flags=1;for(i=0;i<100;i++)b->raw[i]=2048;hw.samples_completed+=100;hw.blocks_completed++;return 1;}
int W4HW_Pwm(uint32_t h,uint16_t d,uint8_t c,uint8_t e){(void)c;(void)e;pwm_calls++;hw.pwm_hz=h;hw.pwm_arr=99;hw.pwm_ccr=d/10;return 0;}
int W4HW_EncoderStart(uint32_t h,uint32_t n,int32_t d,uint32_t i){(void)h;(void)n;(void)d;(void)i;return 0;}
void W4HW_EncoderStop(void){}
int W4HW_AdcCycles(uint32_t n){return (n==84||n==480)?0:-1;}
int W4HW_Poll(uint32_t *r){polls++;*r=2048;return 0;}
const TxQueue *BSP_UART_TxStats(void){return &queue;}
uint32_t BSP_UART_TxErrors(void){return 0;}
HAL_StatusTypeDef BSP_UART_SendQueued(const uint8_t *p,uint16_t n){assert(sent_count<64);memcpy(sent[sent_count],p,n);lengths[sent_count++]=n;return HAL_OK;}
static ParsedFrame f;
static void command(unsigned cmd,const uint8_t *p,unsigned n){memset(&f,0,sizeof(f));f.command=(uint8_t)cmd;f.sequence=42;f.payload_length=(uint16_t)n;f.payload=p;assert(APP_Week4_Command(&f)==1);}
static int32_t result(void){uint32_t r;memcpy(&r,sent[sent_count-1]+9,4);return (int32_t)r;}
int main(void){uint8_t on=1,off=0,bad=2,pwm[8]={0x10,0x27,0,0,0xf4,1,0,1};uint8_t pause[4]={250,0,0,0};unsigned before;
 APP_Week4_Init();assert(!hw.running&&!starts);
 command(0x20,&bad,1);assert(result()<0&&!starts);
 command(0x20,&on,1);assert(result()==0&&starts==1);
 command(0x20,&on,1);assert(result()<0&&starts==1);
 command(0x27,0,0);assert(result()<0&&!polls);
 sample_available=1;APP_Week4_Run();assert(sent[sent_count-1][8]==0xb0);assert(lengths[sent_count-1]==231);
 command(0x25,pause,4);sample_available=1;before=sent_count;now=200;APP_Week4_Run();assert(sample_available&&sent_count==before);now=251;APP_Week4_Run();assert(!sample_available);
 sample_available=1;command(0x20,&off,1);assert(!hw.running&&sent[sent_count-2][8]==0xb0&&sent[sent_count-1][8]==0xa8);
 command(0x27,0,0);assert(result()==0&&polls==1);
 command(0x22,pwm,8);assert(result()==0&&pwm_calls==1);
 pwm[7]=9;command(0x22,pwm,8);assert(result()<0&&pwm_calls==1);
 command(0x21,0,0);assert(sent[sent_count-1][8]==0xa9&&lengths[sent_count-1]==139);
 puts("test_app_week4: PASS");return 0;
}
