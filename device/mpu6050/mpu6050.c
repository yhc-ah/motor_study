#include "mpu6050.h"
#include <string.h>
static const uint8_t config[][2]={
    {0x6c,0x00},{0x1a,0x03},{0x19,0x01},{0x1b,0x00},
    {0x1c,0x00},{0x23,0x00},{0x6a,0x00},{0x37,0x00},{0x38,0x01}
};
static int error(Mpu6050 *m,uint32_t now,int code) {
    m->errors++;m->last_error=code;m->online=0;m->valid=0;
    m->state=0;m->step=0;m->deadline_ms=now+500U;return code;
}
void Mpu6050_Init(Mpu6050 *m,MpuRead rd,MpuWrite wr,void *ctx,uint32_t now) {
    memset(m,0,sizeof(*m));m->read=rd;m->write=wr;m->context=ctx;m->deadline_ms=now;
}
int Mpu6050_Service(Mpu6050 *m,uint32_t now,uint32_t us,MpuSample *s) {
    uint8_t id,b[15];unsigned i;int rc;
    if(m->state!=3 && (int32_t)(now-m->deadline_ms)<0)return 0;
    if(m->state==0) {
        m->address=0x68;rc=m->read(m->context,m->address,0x75,&id,1);
        if(rc) {m->address=0x69;rc=m->read(m->context,m->address,0x75,&id,1);}
        if(rc)return error(m,now,rc<0?rc:MPU_BUS_ERROR);
        if(id!=0x68)return error(m,now,MPU_ID_ERROR);
        rc=m->write(m->context,m->address,0x6b,0x80);
        if(rc)return error(m,now,rc<0?rc:MPU_BUS_ERROR);
        m->state=1;m->deadline_ms=now+100U;return 0;
    }
    if(m->state==1) {
        rc=m->write(m->context,m->address,0x6b,1);
        if(rc)return error(m,now,rc<0?rc:MPU_BUS_ERROR);
        m->state=2;m->deadline_ms=now+100U;return 0;
    }
    if(m->state==2) {
        if(m->step<sizeof(config)/sizeof(config[0])) {
            uint8_t reg=config[m->step][0],value=config[m->step][1];
            rc=m->write(m->context,m->address,reg,value);
            if(!rc)rc=m->read(m->context,m->address,reg,&id,1);
            if(rc)return error(m,now,rc<0?rc:MPU_BUS_ERROR);
            if(id!=value)return error(m,now,MPU_CONFIG_ERROR);
            m->step++;return 0;
        }
        rc=m->read(m->context,m->address,0x6b,&id,1);
        if(rc)return error(m,now,rc<0?rc:MPU_BUS_ERROR);
        if(id!=1)return error(m,now,MPU_CONFIG_ERROR);
        if(m->ever_online)m->recoveries++;
        m->ever_online=1;m->online=1;m->state=3;m->ready_since_ms=now;return 0;
    }
    /* One burst includes INT_STATUS and all seven values; no separate axis reads. */
    rc=m->read(m->context,m->address,0x3a,b,sizeof(b));
    if(rc)return error(m,now,rc<0?rc:MPU_BUS_ERROR);
    if(!(b[0]&1U)) {
        uint32_t since=m->valid?m->last_success_ms:m->ready_since_ms;
        if((uint32_t)(now-since)>100U)return error(m,now,MPU_STALE_ERROR);
        return 0;
    }
    m->samples++;m->last_success_ms=now;m->valid=1;m->last_error=0;
    if(s) {
        s->sequence=m->samples;s->time_us=us;
        for(i=0;i<7;i++) {
            uint16_t u=(uint16_t)(((uint16_t)b[1+2*i]<<8)|b[2+2*i]);
            s->raw[i]=(int16_t)((u&0x8000U)?(int32_t)u-65536:(int32_t)u);
        }
    }
    return 1;
}
