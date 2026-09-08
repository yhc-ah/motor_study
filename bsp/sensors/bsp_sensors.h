#ifndef BSP_SENSORS_H
#define BSP_SENSORS_H
#include <stdint.h>
typedef struct {
    uint32_t transactions,errors,last_hal_error,recovery_attempts,recovery_failures;
    uint32_t max_transaction_us;
} SensorBusStats;
void BSP_Sensors_Init(void);
uint32_t BSP_Micros(void);
int BSP_SensorRead(void *,uint8_t,uint8_t,uint8_t *,uint16_t);
int BSP_SensorWrite(void *,uint8_t,uint8_t,uint8_t);
int BSP_SensorRawTx(void *,uint8_t,const uint8_t *,uint16_t);
int BSP_SensorRawRx(void *,uint8_t,uint8_t *,uint16_t);
void BSP_SensorRecover(void);
int BSP_FlashTransfer(void *,const uint8_t *,uint8_t *,uint16_t);
void BSP_FlashSelect(void *,uint8_t);
void BSP_ImuIRQ(void);
void BSP_ImuEventSnapshot(uint32_t *,uint32_t *);
const SensorBusStats *BSP_SensorStats(void);
#endif
