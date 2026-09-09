/* Trivial static storage: hardware is touched only by explicit Init/Service. */
extern "C" {
#include "imu_adapter.h"
}
class Imu {
public:
    void Init(MpuRead r,MpuWrite w,void *c,uint32_t t){Mpu6050_Init(&state,r,w,c,t);}
    int Service(uint32_t t,uint32_t us,MpuSample *s){return Mpu6050_Service(&state,t,us,s);}
    Mpu6050 state;
};
static Imu imu;
extern "C" void PortImu_Init(MpuRead r,MpuWrite w,void *c,uint32_t t){imu.Init(r,w,c,t);}
extern "C" int PortImu_Service(uint32_t t,uint32_t us,MpuSample *s){return imu.Service(t,us,s);}
extern "C" const Mpu6050 *PortImu_State(void){return &imu.state;}
