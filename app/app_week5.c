#include "app_week5.h"
#include "week5_config.h"
#include "app_week4.h"
#include "app_uart.h"
#include "bsp_sensors.h"
#include "bsp_uart_tx.h"
#include "mpu6050.h"
#include "week5_timing.h"
#include "week5_journal.h"
#include "frame_codec.h"
#include <string.h>
enum {P_IMU,P_ADC,P_UART,P_TX,P_FLASH,P_TH,P_LCD,P_COUNT};
static Mpu6050 imu;static MpuSample sample;static SpiFlash flash;static W5Journal journal;
static W5Period period;static W5Profile prof[P_COUNT];
static uint32_t stage,logging,status_at,display_at,journal_at,start_us,done_us,last_valid_us;
static uint32_t sent,first_sequence,invalid_at,streak,recovered_at,inject_us,inject_count,low_turn;
static uint32_t th_updates,lcd_updates,extra_errors,flash_missed,window_at,window_occupancy;
static uint64_t window_total;
__weak uint32_t W5Extras_Capabilities(void){return 0;}
__weak int W5Extras_ThStep(uint32_t now){(void)now;return -1;}
__weak int W5Extras_DisplayStep(uint32_t now,const uint32_t *v,unsigned n){(void)now;(void)v;(void)n;return -1;}
static void put32(uint8_t *p,uint32_t v){unsigned i;for(i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static int send(uint32_t seq,uint8_t cmd,const uint8_t *p,uint16_t n){
    uint8_t wire[FRAME_MAX_ENCODED_SIZE];uint16_t len=Frame_Encode(seq,cmd,p,n,wire,sizeof(wire));
    return len && BSP_UART_SendQueued(wire,len)==HAL_OK;
}
static void measure(unsigned id,uint32_t from,uint32_t budget){W5Profile_Add(&prof[id],from,BSP_Micros(),budget);}
static void fields(uint32_t *v){
    const TxQueue *q=BSP_UART_TxStats();unsigned i;memset(v,0,48*sizeof(*v));
    v[0]=1;v[1]=HAL_GetTick();v[2]=stage;v[3]=logging;v[4]=W5Extras_Capabilities()|(journal.state==W5J_READY?4U:0U);
    v[5]=imu.valid;v[6]=imu.samples?v[1]-imu.last_success_ms:0xffffffffU;
    v[7]=period.calls;v[8]=imu.samples;v[9]=imu.errors;v[10]=imu.recoveries;
    v[11]=period.missed;v[12]=period.max_error;v[13]=W5Period_P99(&period);v[14]=period.intervals;
    v[15]=period.intervals?(uint32_t)(period.interval_sum/period.intervals):0;
    v[16]=start_us;v[17]=done_us;v[18]=last_valid_us;v[19]=prof[P_IMU].max_us;
    v[20]=sent;v[21]=first_sequence;v[22]=journal.state;v[23]=journal.records;v[24]=journal.errors;v[25]=(uint32_t)journal.result;
    v[26]=q->dropped;v[27]=BSP_UART_TxErrors();v[28]=q->high_watermark;v[29]=period.violations;
    v[30]=inject_count;v[31]=invalid_at;v[32]=streak;v[33]=recovered_at;
    v[34]=th_updates;v[35]=lcd_updates;v[36]=extra_errors;v[37]=flash_missed;v[38]=window_occupancy;
    v[39]=BSP_SensorStats()->last_hal_error;v[40]=journal.base;v[41]=journal.size;v[42]=imu.online;
    for(i=0;i<P_COUNT;i++)v[43]+=prof[i].over_budget;
    v[44]=(uint32_t)imu.last_error;
}
static void status(uint32_t seq){uint32_t v[48];uint8_t p[192];unsigned i;fields(v);for(i=0;i<48;i++)put32(p+4*i,v[i]);send(seq,0xb9,p,sizeof(p));}
static void profiles(void){
    uint8_t p[172];unsigned i;uint64_t total=0;uint32_t now=BSP_Micros(),dt=now-window_at;
    put32(p,1);put32(p+4,HAL_GetTick());
    for(i=0;i<P_COUNT;i++){
        uint8_t *b=p+8+20*i;put32(b,prof[i].calls);put32(b+4,(uint32_t)prof[i].total_us);
        put32(b+8,(uint32_t)(prof[i].total_us>>32));put32(b+12,prof[i].max_us);put32(b+16,prof[i].over_budget);total+=prof[i].total_us;
    }
    window_occupancy=dt?(uint32_t)((total-window_total)*1000U/dt):0;
    window_at=now;window_total=total;
    send(0,0xb3,p,148);
}
void APP_Week5_Init(void){
    uint32_t now=HAL_GetTick();APP_Week4_Init();Mpu6050_Init(&imu,BSP_SensorRead,BSP_SensorWrite,0,now);
    /* No Flash access or mutation until explicit prepare command. */
    memset(&flash,0,sizeof(flash));W5Journal_Init(&journal,&flash);
    W5Period_Init(&period,BSP_Micros(),W5_IMU_PERIOD_US);memset(prof,0,sizeof(prof));
    stage=logging=sent=first_sequence=0;status_at=display_at=journal_at=now;window_at=BSP_Micros();
}
static void imu_step(void){
    uint32_t us=BSP_Micros(),before,drdy,event;uint8_t p[30];unsigned i;int rc;uint8_t was_valid;
    if(!W5Period_Due(&period,us))return;
    start_us=us;before=imu.errors;was_valid=imu.valid;
    rc=Mpu6050_Service(&imu,HAL_GetTick(),us,&sample);done_us=BSP_Micros();
    measure(P_IMU,us,1000);
    if(rc==1){
        last_valid_us=done_us;if(streak<10)streak++;if(streak==10 && !recovered_at)recovered_at=done_us;
        if(logging){
            BSP_ImuEventSnapshot(&drdy,&event);(void)event;
            put32(p,start_us);put32(p+4,drdy);put32(p+8,done_us-start_us);
            for(i=0;i<7;i++){p[12+2*i]=(uint8_t)sample.raw[i];p[13+2*i]=(uint8_t)((uint16_t)sample.raw[i]>>8);}
            p[26]=1;p[27]=p[28]=p[29]=0;
            if(send(sample.sequence,0xa0,p,30)){sent++;if(!first_sequence)first_sequence=sample.sequence;}
        }
    }
    if(!imu.valid){streak=0;recovered_at=0;if(was_valid)invalid_at=done_us;}
    if(imu.errors!=before){us=BSP_Micros();BSP_SensorRecover();measure(P_IMU,us,1000);}
}
void APP_Week5_Run(void){
    uint32_t t,now,v[48];int rc;uint8_t record[48];unsigned i;
    imu_step();t=BSP_Micros();APP_Week4_Run();measure(P_ADC,t,1000);
    imu_step();t=BSP_Micros();APP_UART_Run();measure(P_UART,t,150);
    imu_step();t=BSP_Micros();BSP_UART_TxService();measure(P_TX,t,100);
    /* One low priority step only when it fits before the next IMU slot.
     * Synchronous transfers are bounded; device conversion/program waits are not busy-waits. */
    now=HAL_GetTick();t=BSP_Micros();
    if((int32_t)(period.next-t)>(int32_t)W5_LOW_SLOT_US){
        if(inject_us){uint32_t delay=inject_us;inject_us=0;inject_count++;while((uint32_t)(BSP_Micros()-t)<delay){}measure(P_LCD,t,1000);}
        else {
            low_turn=(low_turn+1U)%3U;
            if(low_turn==0){
                if(stage>=5 && journal.state==W5J_READY && (uint32_t)(now-journal_at)>=10000U){
                    uint32_t periods=(now-journal_at)/10000U;journal_at+=periods*10000U;flash_missed+=periods-1;
                    fields(v);for(i=0;i<12;i++)put32(record+4*i,v[i]);
                    if(W5Journal_Append(&journal,record,now))flash_missed++;
                }
                W5Journal_Service(&journal,now);measure(P_FLASH,t,1000);
            }else if(low_turn==1 && stage>=4){rc=W5Extras_ThStep(now);if(rc>0)th_updates++;if(rc<0)extra_errors++;measure(P_TH,t,1000);}
            else if(low_turn==2 && stage>=3 && (uint32_t)(now-display_at)>=500U){
                display_at=now;fields(v);rc=W5Extras_DisplayStep(now,v,48);if(rc>0)lcd_updates++;if(rc<0)extra_errors++;measure(P_LCD,t,1000);
            }
        }
    }
    if(stage && (uint32_t)(now-status_at)>=1000U){status_at=now;profiles();status(0);}
}
int APP_Week5_Command(const ParsedFrame *f){
    int rc=-1;uint8_t p[4];uint32_t caps=W5Extras_Capabilities();
    if(f->command<0x30 || f->command>0x35)return 0;
    switch(f->command){
    case 0x30:
        if(f->payload_length!=2 || f->payload[0]>5 || f->payload[1]>1)break;
        if((f->payload[0]>=3 && !(caps&1)) || (f->payload[0]>=4 && !(caps&2)) || (f->payload[0]>=5 && journal.state!=W5J_READY)){rc=-3;break;}
        if(f->payload[0] && !imu.valid){rc=-2;break;}
        stage=f->payload[0];logging=stage?f->payload[1]:0;
        if(stage){W5Period_Init(&period,BSP_Micros(),2000);memset(prof,0,sizeof(prof));sent=first_sequence=0;window_total=0;window_at=BSP_Micros();journal_at=status_at=HAL_GetTick();}
        rc=0;break;
    case 0x31:if(!f->payload_length){status(f->sequence);return 1;}break;
    case 0x32:
        if(f->payload_length!=12 || memcmp(f->payload,"W5OK",4) || stage)break;
        if(journal.state!=W5J_OFF && journal.state!=W5J_READY && journal.state!=W5J_ERROR){rc=-2;break;}
        /* Reinitialization clears the lower driver's reservation. Invalidate
         * readiness before any failure can leave the old journal advertised. */
        W5Journal_Init(&journal,&flash);
        rc=SpiFlash_Init(&flash,BSP_FlashTransfer,BSP_FlashSelect,0);
        if(!rc)rc=W5Journal_Prepare(&journal,get32(f->payload+4),get32(f->payload+8),HAL_GetTick());
        break;
    case 0x34:
        if(f->payload_length!=4 || !get32(f->payload) || get32(f->payload)>10000 || inject_us)break;
        inject_us=get32(f->payload);rc=0;break;
    case 0x35:if(!f->payload_length)rc=W5Journal_BackgroundErase(&journal,HAL_GetTick());break;
    default:break;
    }
    put32(p,(uint32_t)rc);send(f->sequence,(uint8_t)(f->command+0x88),p,4);return 1;
}
