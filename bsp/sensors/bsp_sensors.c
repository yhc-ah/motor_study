#include "bsp_sensors.h"
#include "i2c.h"
#include "spi.h"
#include "tim.h"
#include "week3_config.h"
#include "mpu6050.h"
#include <string.h>
static SensorBusStats stats;
static volatile uint32_t ready_count,ready_us;
uint32_t BSP_Micros(void){return TIM2->CNT;}
void BSP_Sensors_Init(void) {
    GPIO_InitTypeDef g={0};HAL_TIM_Base_Start(&htim2);
    __HAL_RCC_GPIOC_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOC,GPIO_PIN_5,GPIO_PIN_RESET);
    g.Pin=GPIO_PIN_5;g.Mode=GPIO_MODE_OUTPUT_PP;g.Pull=GPIO_NOPULL;
    g.Speed=GPIO_SPEED_FREQ_VERY_HIGH;HAL_GPIO_Init(GPIOC,&g);
#if WEEK3_MPU_PC4_INT
    g.Pin=GPIO_PIN_4;g.Mode=GPIO_MODE_IT_RISING;HAL_GPIO_Init(GPIOC,&g);
    HAL_NVIC_SetPriority(EXTI4_IRQn,4,0);HAL_NVIC_EnableIRQ(EXTI4_IRQn);
#endif
}
static int idle(void) {
    /* Reject disconnected/stuck buses before HAL's internal BUSY wait. */
    return HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_8)==GPIO_PIN_SET &&
           HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_9)==GPIO_PIN_SET &&
           __HAL_I2C_GET_FLAG(&hi2c1,I2C_FLAG_BUSY)==RESET;
}
static int finish(HAL_StatusTypeDef rc,uint32_t start) {
    uint32_t dt=BSP_Micros()-start;
    HAL_GPIO_WritePin(GPIOC,GPIO_PIN_5,GPIO_PIN_RESET);stats.transactions++;
    if(dt>stats.max_transaction_us)stats.max_transaction_us=dt;
    if(rc!=HAL_OK){
        stats.errors++;stats.last_hal_error=HAL_I2C_GetError(&hi2c1);
        if(rc==HAL_BUSY)return MPU_BUS_BUSY;
        if(rc==HAL_TIMEOUT)return MPU_BUS_TIMEOUT;
        if(stats.last_hal_error&HAL_I2C_ERROR_AF)return MPU_BUS_NACK;
        return MPU_BUS_ERROR;
    }
    return 0;
}
int BSP_SensorRead(void *ctx,uint8_t a,uint8_t r,uint8_t *p,uint16_t n) {
    uint32_t start=BSP_Micros();HAL_StatusTypeDef rc;(void)ctx;
    if(!p || !n || n>64)return -1;
    HAL_GPIO_WritePin(GPIOC,GPIO_PIN_5,GPIO_PIN_SET);
    rc=idle()?HAL_I2C_Mem_Read(&hi2c1,(uint16_t)(a<<1),r,I2C_MEMADD_SIZE_8BIT,p,n,3):HAL_BUSY;
    return finish(rc,start);
}
int BSP_SensorWrite(void *ctx,uint8_t a,uint8_t r,uint8_t v) {
    uint32_t start=BSP_Micros();HAL_StatusTypeDef rc;(void)ctx;
    HAL_GPIO_WritePin(GPIOC,GPIO_PIN_5,GPIO_PIN_SET);
    rc=idle()?HAL_I2C_Mem_Write(&hi2c1,(uint16_t)(a<<1),r,I2C_MEMADD_SIZE_8BIT,&v,1,3):HAL_BUSY;
    return finish(rc,start);
}
int BSP_SensorRawTx(void *ctx,uint8_t a,const uint8_t *p,uint16_t n) {
    uint32_t start=BSP_Micros();(void)ctx;
    if(!p || !n || n>64)return -1;
    return finish(idle()?HAL_I2C_Master_Transmit(&hi2c1,(uint16_t)(a<<1),(uint8_t*)p,n,3):HAL_BUSY,start);
}
int BSP_SensorRawRx(void *ctx,uint8_t a,uint8_t *p,uint16_t n) {
    uint32_t start=BSP_Micros();(void)ctx;
    if(!p || !n || n>64)return -1;
    return finish(idle()?HAL_I2C_Master_Receive(&hi2c1,(uint16_t)(a<<1),p,n,3):HAL_BUSY,start);
}
static void short_wait(void){uint32_t t=BSP_Micros();while((uint32_t)(BSP_Micros()-t)<5U){}}
void BSP_SensorRecover(void) {
    GPIO_InitTypeDef g={0};unsigned n;stats.recovery_attempts++;
    HAL_I2C_DeInit(&hi2c1);__HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOB,GPIO_PIN_8|GPIO_PIN_9,GPIO_PIN_SET);
    g.Pin=GPIO_PIN_8|GPIO_PIN_9;g.Mode=GPIO_MODE_OUTPUT_OD;
    g.Pull=GPIO_NOPULL;g.Speed=GPIO_SPEED_FREQ_LOW;HAL_GPIO_Init(GPIOB,&g);
    for(n=0;n<9 && HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_9)==GPIO_PIN_RESET;n++) {
        if(HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_8)==GPIO_PIN_RESET)break;
        HAL_GPIO_WritePin(GPIOB,GPIO_PIN_8,GPIO_PIN_RESET);short_wait();
        HAL_GPIO_WritePin(GPIOB,GPIO_PIN_8,GPIO_PIN_SET);short_wait();
    }
    if(HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_8)==GPIO_PIN_SET &&
       HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_9)==GPIO_PIN_SET) {
        HAL_GPIO_WritePin(GPIOB,GPIO_PIN_8,GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOB,GPIO_PIN_9,GPIO_PIN_RESET);short_wait();
        HAL_GPIO_WritePin(GPIOB,GPIO_PIN_8,GPIO_PIN_SET);short_wait();
        HAL_GPIO_WritePin(GPIOB,GPIO_PIN_9,GPIO_PIN_SET);short_wait();
    }else stats.recovery_failures++;
    __HAL_RCC_I2C1_FORCE_RESET();__HAL_RCC_I2C1_RELEASE_RESET();MX_I2C1_Init();
}
int BSP_FlashTransfer(void *ctx,const uint8_t *tx,uint8_t *rx,uint16_t n) {
    uint8_t dummy[68];HAL_StatusTypeDef rc;(void)ctx;
    if(!n || n>sizeof(dummy))return -1;
    memset(dummy,0xff,n);
    rc=HAL_SPI_TransmitReceive(&hspi1,(uint8_t*)(tx?tx:dummy),rx?rx:dummy,n,3);
    return rc==HAL_OK?0:-1;
}
void BSP_FlashSelect(void *ctx,uint8_t active){(void)ctx;HAL_GPIO_WritePin(GPIOG,GPIO_PIN_6,active?GPIO_PIN_RESET:GPIO_PIN_SET);}
void BSP_ImuIRQ(void){ready_us=BSP_Micros();ready_count++;}
void BSP_ImuEventSnapshot(uint32_t *count,uint32_t *us) {
    uint32_t mask=__get_PRIMASK();__disable_irq();*count=ready_count;*us=ready_us;
    if(!mask)__enable_irq();
}
const SensorBusStats *BSP_SensorStats(void){return &stats;}
