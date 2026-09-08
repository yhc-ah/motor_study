#include "app_sensors.h"
#include "week3_config.h"
#include "bsp_sensors.h"
#include "bsp_uart_tx.h"
#include "mpu6050.h"
#include "spi_flash.h"
#include "spi_flash_test.h"
#include "frame_codec.h"
#include "stm32f4xx_hal.h"
#include "bsp_led.h"
#include <string.h>
static Mpu6050 imu;
static MpuSample sample;
static SpiFlash flash;
static SpiFlashTest flash_test;
static uint8_t stream;
static uint32_t poll_at,status_at,display_at,last_sample_us;
#if WEEK3_MPU_PC4_INT
static uint32_t seen_drdy;
#endif
static uint32_t deadline_misses,missed_drdy,max_response,last_recovery;
static uint8_t have_sample;
static uint32_t heartbeat_at;
static uint8_t heartbeat_on;
static uint8_t tx[FRAME_MAX_ENCODED_SIZE];
static void put32(uint8_t *p,uint32_t v){unsigned i;for(i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static void put16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void send(uint32_t seq,uint8_t cmd,const uint8_t *p,uint16_t n) {
    uint16_t len=Frame_Encode(seq,cmd,p,n,tx,sizeof(tx));
    if(len)BSP_UART_SendQueued(tx,len);
}
__weak uint8_t APP_DisplayAvailable(void){return 0;}
__weak void APP_DisplayStatus(const uint32_t *p,uint32_t n){(void)p;(void)n;}
static void status(uint32_t *v) {
    uint32_t drdy,t;const SensorBusStats *b=BSP_SensorStats();
    const TxQueue *q=BSP_UART_TxStats();BSP_ImuEventSnapshot(&drdy,&t);
    v[0]=HAL_GetTick();v[1]=imu.samples;v[2]=imu.errors;v[3]=imu.recoveries;
    v[4]=imu.valid;v[5]=imu.samples?v[0]-imu.last_success_ms:0xffffffffU;
    v[6]=b->errors;v[7]=b->recovery_attempts;v[8]=b->recovery_failures;
    v[9]=b->max_transaction_us;v[10]=max_response;v[11]=deadline_misses;
    v[12]=drdy;v[13]=missed_drdy;v[14]=q->dropped;v[15]=BSP_UART_TxErrors();
    v[16]=q->count;v[17]=q->high_watermark;v[18]=flash.jedec_id;
    v[19]=(uint32_t)flash_test.result;v[20]=flash_test.rounds_completed;
    v[21]=flash_test.mismatches;v[22]=flash_test.active;v[23]=stream;
    v[24]=(WEEK3_MPU_PC4_INT?1U:0U)|(APP_DisplayAvailable()?4U:0U)|
          (flash.reserved_size?8U:0U);
    v[25]=0;v[26]=0;v[27]=0;v[28]=0xffffffffU;
    v[29]=flash_test.cross_page_rounds;v[30]=flash_test.elapsed_ms;
    v[31]=flash_test.first_mismatch_address;
}
static void send_status(uint32_t seq) {
    uint32_t fields[32];uint8_t payload[128];unsigned i;status(fields);
    for(i=0;i<32;i++)put32(payload+i*4,fields[i]);
    send(seq,0x91,payload,sizeof(payload));
}
void APP_Sensors_Init(void) {
    uint32_t now=HAL_GetTick();
    Mpu6050_Init(&imu,BSP_SensorRead,BSP_SensorWrite,0,now);
    SpiFlash_Init(&flash,BSP_FlashTransfer,BSP_FlashSelect,0);
    if(WEEK3_FLASH_TEST_SIZE)SpiFlash_Reserve(&flash,WEEK3_FLASH_TEST_BASE,WEEK3_FLASH_TEST_SIZE);
    SpiFlashTest_Init(&flash_test,&flash);
    flash_test.result=flash.result;
    poll_at=BSP_Micros();status_at=now;display_at=now;last_recovery=now;
}
int APP_Sensors_Command(const ParsedFrame *f) {
    uint8_t response[4];int rc=0;
    if(f->command==0x10) {
        if(f->payload_length!=1 || f->payload[0]>1 ||
           (f->payload[0] && flash_test.active))rc=-1;
        else {stream=f->payload[0];send(f->sequence,0x90,f->payload,1);return 1;}
    }else if(f->command==0x11) {
        if(f->payload_length)rc=-1;else{send_status(f->sequence);return 1;}
    }else if(f->command==0x12) {
        if(f->payload_length!=4 || memcmp(f->payload,"W3OK",4))rc=-1;
        else if(stream || flash_test.active || flash.active)rc=-2;
        else if(!flash.reserved_size)rc=-3;
        else {rc=SpiFlashTest_Start(&flash_test,WEEK3_FLASH_TEST_BASE,HAL_GetTick());
              if(rc==SPI_FLASH_BUSY)rc=0;}
        put32(response,(uint32_t)rc);send(f->sequence,0x92,response,4);return 1;
    }else return 0;
    response[0]=2;response[1]=f->command;send(f->sequence,0xff,response,2);return 1;
}
void APP_Sensors_Run(void) {
    uint32_t now=HAL_GetTick(),us=BSP_Micros(),drdy,event_us,dt,old_errors;
    uint8_t payload[30];uint16_t flags=1;unsigned i;int got;
    BSP_UART_TxService();
    if((uint32_t)(now-heartbeat_at)>=500U){
        heartbeat_at=now;heartbeat_on^=1U;BSP_LED_SetById(2,heartbeat_on);
    }
    if(flash_test.active){imu.valid=0;have_sample=0;}
    if((int32_t)(us-poll_at)>=0 && !flash_test.active) {
        poll_at=us+1000U;BSP_ImuEventSnapshot(&drdy,&event_us);
#if WEEK3_MPU_PC4_INT
        /* Wake on events below; watchdog poll remains for missing INT wiring. */
        if(drdy!=seen_drdy) {
            if((uint32_t)(drdy-seen_drdy)>1U){missed_drdy+=drdy-seen_drdy-1U;flags|=4;}
            flags|=2;
        }
#endif
        old_errors=imu.errors;
        got=Mpu6050_Service(&imu,now,us,&sample);
        dt=BSP_Micros()-us;
        if(got==1) {
            uint32_t response=dt;
#if WEEK3_MPU_PC4_INT
            if(flags&2){sample.time_us=event_us;response=BSP_Micros()-event_us;}
#endif
            if(response>max_response)max_response=response;
            if(response>=2000U || (have_sample && (uint32_t)(us-last_sample_us)>3000U)){
                deadline_misses++;flags|=4;
            }
            have_sample=1;last_sample_us=us;
            put32(payload,sample.time_us);put32(payload+4,drdy);put32(payload+8,dt);
            for(i=0;i<7;i++)put16(payload+12+2*i,(uint16_t)sample.raw[i]);
            put16(payload+26,flags);put16(payload+28,0);
            if(stream)send(sample.sequence,0xa0,payload,sizeof(payload));
        }
#if WEEK3_MPU_PC4_INT
        seen_drdy=drdy;
#endif
        if(imu.errors!=old_errors && (uint32_t)(now-last_recovery)>=500U) {
            last_recovery=now;BSP_SensorRecover();
        }
    }
#if WEEK3_MPU_PC4_INT
    BSP_ImuEventSnapshot(&drdy,&event_us);
    if(drdy!=seen_drdy)poll_at=BSP_Micros();
#endif
    if(flash_test.active)SpiFlashTest_Service(&flash_test,now);
    if(stream && (uint32_t)(now-status_at)>=1000U){status_at=now;send_status(0);}
    if(APP_DisplayAvailable() && (uint32_t)(now-display_at)>=500U) {
        uint32_t fields[32];display_at=now;status(fields);APP_DisplayStatus(fields,32);
    }
}
