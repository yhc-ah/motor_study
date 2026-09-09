#ifndef MPU6050_H
#define MPU6050_H
#include <stdint.h>
/* 7-bit address; register access is SYNCHRONOUS: 0 means fully completed,
 * never merely accepted. Caller owns buffers; adapter must not retain them.
 * Negative bus codes propagate to last_error. Retry is one attempt per call;
 * device re-probes after 500 ms offline (no loop waiting for reconnection). */
typedef int (*MpuRead)(void *,uint8_t,uint8_t,uint8_t *,uint16_t);
typedef int (*MpuWrite)(void *,uint8_t,uint8_t,uint8_t);
enum { MPU_BUS_ERROR=-1, MPU_ID_ERROR=-2, MPU_CONFIG_ERROR=-3, MPU_STALE_ERROR=-4 };
enum { MPU_BUS_BUSY=-10, MPU_BUS_TIMEOUT=-11, MPU_BUS_NACK=-12,
       MPU_BUS_INVALID=-13 };
typedef struct {
    uint32_t sequence,time_us; /* caller's transaction-start timestamp, NOT DRDY */
    /* ax, ay, az, chip temperature, gx, gy, gz; signed sensor counts. */
    int16_t raw[7];
} MpuSample;
typedef struct {
    MpuRead read; MpuWrite write; void *context;
    uint32_t deadline_ms,last_success_ms,ready_since_ms,samples,errors,recoveries;
    int last_error;
    uint8_t state,step,address,online,valid,ever_online;
} Mpu6050;
void Mpu6050_Init(Mpu6050 *,MpuRead,MpuWrite,void *,uint32_t now_ms);
/* Call only in foreground. Returns 1 only for a complete fresh sample. */
int Mpu6050_Service(Mpu6050 *,uint32_t now_ms,uint32_t time_us,MpuSample *);
#endif
