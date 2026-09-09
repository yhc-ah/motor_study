#include "app_week4.h"
#include "week4_config.h"
#include "bsp_week4.h"
#include "signal_math.h"
#include "bsp_uart_tx.h"
#include "frame_codec.h"
#include <string.h>
static uint32_t run_id, blocks_sent, samples_sent, pause_events;
static uint32_t status_at, pwm_at, pause_at, pause_ms;
static uint32_t pwm_hz=10000U, filter_next;
static uint8_t pwm_mode, pwm_center, cycle_index, filter_valid;
static float filter5,filter20;
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint16_t get16(const uint8_t *p){return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static void put32(uint8_t *p,uint32_t v){unsigned i;for(i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static void put16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static int send(uint32_t seq,uint8_t cmd,const uint8_t *p,uint16_t n){
    uint8_t wire[FRAME_MAX_ENCODED_SIZE];uint16_t size=Frame_Encode(seq,cmd,p,n,wire,sizeof(wire));
    return size && BSP_UART_SendQueued(wire,size)==HAL_OK;
}
static void ack(const ParsedFrame *f,int rc){uint8_t p[4];put32(p,(uint32_t)rc);send(f->sequence,(uint8_t)(f->command+0x88U),p,4);}
static void status(uint32_t seq){
    const W4HardwareStats *s=W4HW_Stats();const TxQueue *q=BSP_UART_TxStats();
    uint32_t v[32];uint8_t p[128];unsigned i;
    v[0]=WEEK4_PROTOCOL_VERSION;v[1]=HAL_GetTick();v[2]=run_id;v[3]=s->running;
    v[4]=s->samples_completed;v[5]=s->blocks_completed;v[6]=blocks_sent;v[7]=samples_sent;
    v[8]=s->overwritten_blocks;v[9]=s->copy_races;v[10]=s->adc_overruns;v[11]=s->dma_errors;
    v[12]=q->dropped;v[13]=BSP_UART_TxErrors();v[14]=s->max_consume_us;
    v[15]=s->ht_count;v[16]=s->tc_count;v[17]=s->tail_samples;
    v[18]=s->encoder_cnt;v[19]=(uint32_t)s->encoder_position;v[20]=(uint32_t)s->encoder_speed;
    v[21]=s->generator_steps;v[22]=s->generator_target;v[23]=s->generator_active;
    v[24]=s->pwm_hz;v[25]=s->pwm_arr;v[26]=s->pwm_ccr;v[27]=pwm_mode;
    v[28]=filter_valid?(uint32_t)(filter5*1000.0f):0;v[29]=filter_valid?(uint32_t)(filter20*1000.0f):0;
    v[30]=pause_events;v[31]=s->hardware_errors;
    for(i=0;i<32;i++){put32(p+4*i,v[i]);}
    send(seq,0xa9,p,sizeof(p));
}
static void consume(void){
    W4AdcBlock b;uint8_t p[220];unsigned i;
    /* At most the latest complete half and terminal partial block are available. */
    while(W4HW_TakeBlock(&b)){
        if(!(b.flags&1U) || !b.count || b.count>100U){filter_valid=0;continue;}
        if(!filter_valid || b.first_sample!=filter_next){filter5=filter20=(float)b.raw[0];filter_valid=1;}
        for(i=0;i<b.count;i++){
            filter5=W4_Filter(filter5,b.raw[i],0.181269247f);
            filter20=W4_Filter(filter20,b.raw[i],0.0487705755f);
            put16(p+20+2*i,b.raw[i]);
        }
        filter_next=b.first_sample+b.count;
        put32(p,run_id);put32(p+4,b.sequence);put32(p+8,b.first_sample);put32(p+12,b.done_us);
        put16(p+16,b.count);put16(p+18,b.flags);
        if(send(b.sequence,0xb0,p,(uint16_t)(20U+2U*b.count))){blocks_sent++;samples_sent+=b.count;}
    }
}
void APP_Week4_Init(void){
    run_id=blocks_sent=samples_sent=pause_events=pause_ms=0;
    filter_valid=pwm_mode=pwm_center=cycle_index=0;filter5=filter20=0;
    status_at=pwm_at=HAL_GetTick();W4HW_Init();
}
void APP_Week4_Run(void){
    static const uint16_t duty[3]={100,500,900};uint32_t now=HAL_GetTick();
    W4HW_Run();
    if(!pause_ms || (uint32_t)(now-pause_at)>=pause_ms){pause_ms=0;consume();}
    if(pwm_mode==2 && (uint32_t)(now-pwm_at)>=10U){
        pwm_at=now;cycle_index=(uint8_t)((cycle_index+1U)%3U);
        W4HW_Pwm(pwm_hz,duty[cycle_index],pwm_center,1);
    }else if(pwm_mode==3 && filter_valid && (uint32_t)(now-pwm_at)>=100U){
        pwm_at=now;W4HW_Pwm(pwm_hz,(uint16_t)(100.0f+800.0f*filter20/4095.0f),pwm_center,1);
    }
    if(W4HW_Stats()->running && (uint32_t)(now-status_at)>=1000U){status_at=now;status(0);}
}
int APP_Week4_Command(const ParsedFrame *f){
    const uint8_t *p=f->payload;uint16_t n=f->payload_length;int rc=-1;uint32_t hz,cycles;uint16_t duty;
    if(f->command<0x20 || f->command>0x27)return 0;
    switch(f->command){
    case 0x20:
        if(n!=1 || p[0]>1)break;
        if(p[0]){
            if(W4HW_Stats()->running){rc=-2;break;}
            rc=W4HW_StartAdc();if(!rc){run_id++;blocks_sent=samples_sent=pause_events=pause_ms=0;filter_valid=0;status_at=HAL_GetTick();}
        }else{rc=W4HW_StopAdc();pause_ms=0;consume();}
        break;
    case 0x21:if(!n){status(f->sequence);return 1;}break;
    case 0x22:
        if(n!=8)break;
        hz=get32(p);duty=get16(p+4);
        if((hz!=1000 && hz!=10000 && hz!=20000)||duty>1000||p[6]>1||p[7]>3)break;
        rc=W4HW_Pwm(hz,p[7]==2?100:duty,p[6],p[7]!=0);
        if(!rc){pwm_hz=hz;pwm_center=p[6];pwm_mode=p[7];cycle_index=0;pwm_at=HAL_GetTick();}break;
    case 0x23:
        if(n!=16)break;
        hz=get32(p);cycles=get32(p+4);
        if((hz!=100 && hz!=1000)||!cycles||cycles>1000000||get32(p+12)>65535||
          (get32(p+8)!=1 && get32(p+8)!=0xffffffffU))break;
        rc=W4HW_EncoderStart(hz,cycles,(int32_t)get32(p+8),get32(p+12));break;
    case 0x24:if(!n){W4HW_EncoderStop();rc=0;}break;
    case 0x25:
        if(n!=4||!get32(p)||get32(p)>1000)break;
        if(!W4HW_Stats()->running){rc=-2;break;}
        pause_ms=get32(p);pause_at=HAL_GetTick();pause_events++;rc=0;break;
    case 0x26:
        if(n!=4)break;
        if(W4HW_Stats()->running){rc=-2;break;}
        rc=W4HW_AdcCycles(get32(p));break;
    case 0x27:
        if(!n){uint32_t raw=0;uint8_t response[8];
            rc=W4HW_Stats()->running?-2:W4HW_Poll(&raw);put32(response,(uint32_t)rc);put32(response+4,raw);
            send(f->sequence,0xaf,response,8);return 1;
        }break;
    default:break;
    }
    ack(f,rc);return 1;
}
