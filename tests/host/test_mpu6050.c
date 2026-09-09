#include "mpu6050.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t regs[128];
static int fail;
static int rd(void *ctx,uint8_t addr,uint8_t reg,uint8_t *out,uint16_t n) {
    (void)ctx; assert(addr==0x68 || addr==0x69);
    if(fail) return fail<0?fail:-1;
    memcpy(out,regs+reg,n); if(reg==0x3a) regs[reg]=0; return 0;
}
static int wr(void *ctx,uint8_t addr,uint8_t reg,uint8_t val) {
    (void)ctx;(void)addr;if(fail)return -1;
    regs[reg]=(reg==0x6b && val==0x80)?0:val;return 0;
}
static void boot(Mpu6050 *m,uint32_t base) {
    uint32_t t; for(t=0;t<250;t++) Mpu6050_Service(m,base+t,0,0);
    assert(m->online); assert(regs[0x19]==1); assert(regs[0x1a]==3);
}
int main(void) {
    Mpu6050 m; MpuSample s;
    memset(regs,0,sizeof(regs));regs[0x75]=0x68;
    Mpu6050_Init(&m,rd,wr,0,0);boot(&m,0);
    regs[0x3a]=1;regs[0x3b]=0x80;regs[0x3c]=0;
    regs[0x3d]=0x7f;regs[0x3e]=0xff;regs[0x40]=1;
    assert(Mpu6050_Service(&m,250,250000,&s)==1);
    assert(s.raw[0]==-32768 && s.raw[1]==32767 && s.raw[2]==1);
    assert(s.sequence==1 && m.samples==1 && m.valid);
    assert(Mpu6050_Service(&m,251,251000,&s)==0);
    assert(m.samples==1);
    fail=1; Mpu6050_Service(&m,252,252000,&s);
    assert(!m.online && !m.valid && m.errors==1 && m.last_success_ms==250);
    fail=0;boot(&m,752); assert(m.recoveries==1);
    assert(m.last_success_ms==250 && !m.valid);
    regs[0x3a]=1; assert(Mpu6050_Service(&m,1003,1003000,&s)==1);
    assert(s.sequence==2);
    regs[0x3a]=1;assert(Mpu6050_Service(&m,0x80001000U,123,&s)==1);
    regs[0x75]=0x70;Mpu6050_Init(&m,rd,wr,0,0);
    Mpu6050_Service(&m,0,0,&s);assert(!m.online && m.last_error==MPU_ID_ERROR);
    regs[0x75]=0x68; Mpu6050_Init(&m,rd,wr,0,0xffffff80U);boot(&m,0xffffff80U);
    fail=-11;Mpu6050_Service(&m,250,250000,&s);
    assert(m.last_error==-11 && !m.valid);
    puts("test_mpu6050: PASS");return 0;
}
