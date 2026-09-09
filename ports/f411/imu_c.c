#include "imu_adapter.h"
static Mpu6050 imu;
void PortImu_Init(MpuRead r,MpuWrite w,void *c,uint32_t t){Mpu6050_Init(&imu,r,w,c,t);}
int PortImu_Service(uint32_t t,uint32_t us,MpuSample *s){return Mpu6050_Service(&imu,t,us,s);}
const Mpu6050 *PortImu_State(void){return &imu;}
