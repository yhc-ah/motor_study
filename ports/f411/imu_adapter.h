#ifndef F411_ADAPTER_H
#define F411_ADAPTER_H
#include "mpu6050.h"
void PortImu_Init(MpuRead,MpuWrite,void *,uint32_t);
int PortImu_Service(uint32_t,uint32_t,MpuSample *);
const Mpu6050 *PortImu_State(void);
#endif
