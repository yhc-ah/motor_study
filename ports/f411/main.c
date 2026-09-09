/* STM32F411CEU6: HSI -> PLL 96 MHz, no external crystal assumption. */
#include "stm32f4xx_hal.h"
#include "imu_adapter.h"
#include "frame_codec.h"
#include "week5_timing.h"
static W5Period period;
static I2C_HandleTypeDef bus;
static uint8_t tx[1024];
static volatile uint32_t tx_head,tx_tail;
static uint32_t tx_dropped,service_max;
static uint32_t time_start,time_done,last_valid_start;
static uint32_t micros(void){return TIM2->CNT;}
static void delay_us(uint32_t n){uint32_t s=micros();while((uint32_t)(micros()-s)<n){}}
static void fail(void){__disable_irq();for(;;){}}
void SysTick_Handler(void){HAL_IncTick();}
void USART1_IRQHandler(void){
    if((USART1->SR&USART_SR_TXE) && (USART1->CR1&USART_CR1_TXEIE)){
        if(tx_tail!=tx_head){USART1->DR=tx[tx_tail&1023U];tx_tail++;}
        else USART1->CR1&=~USART_CR1_TXEIE;
    }
}
static void send_frame(uint32_t seq,uint8_t cmd,const uint8_t *p,uint16_t len){
    uint8_t frame[96];uint16_t n=Frame_Encode(seq,cmd,p,len,frame,sizeof(frame));unsigned i;
    if(!n || n>1024U-(uint32_t)(tx_head-tx_tail)){tx_dropped++;return;}
    /* Single producer writes bytes before publishing head to IRQ consumer. */
    for(i=0;i<n;i++)tx[(tx_head+i)&1023U]=frame[i];
    __DMB();tx_head+=n;USART1->CR1|=USART_CR1_TXEIE;
}
static void put16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void put32(uint8_t *p,uint32_t v){unsigned i;for(i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static void i2c_init(void){
    GPIO_InitTypeDef g={0};
    g.Pin=GPIO_PIN_8|GPIO_PIN_9;g.Mode=GPIO_MODE_AF_OD;g.Pull=GPIO_PULLUP;
    g.Speed=GPIO_SPEED_FREQ_VERY_HIGH;g.Alternate=GPIO_AF4_I2C1;HAL_GPIO_Init(GPIOB,&g);
    bus.Instance=I2C1;bus.Init.ClockSpeed=400000;bus.Init.DutyCycle=I2C_DUTYCYCLE_2;
    bus.Init.OwnAddress1=0;bus.Init.AddressingMode=I2C_ADDRESSINGMODE_7BIT;
    bus.Init.DualAddressMode=I2C_DUALADDRESS_DISABLE;bus.Init.OwnAddress2=0;
    bus.Init.GeneralCallMode=I2C_GENERALCALL_DISABLE;bus.Init.NoStretchMode=I2C_NOSTRETCH_DISABLE;
    if(HAL_I2C_Init(&bus)!=HAL_OK)fail();
}
static void recover(void){
    GPIO_InitTypeDef g={0};unsigned i;
    __HAL_I2C_DISABLE(&bus);HAL_I2C_DeInit(&bus);
    g.Pin=GPIO_PIN_8|GPIO_PIN_9;g.Mode=GPIO_MODE_OUTPUT_OD;g.Pull=GPIO_PULLUP;
    g.Speed=GPIO_SPEED_FREQ_HIGH;HAL_GPIO_Init(GPIOB,&g);
    HAL_GPIO_WritePin(GPIOB,GPIO_PIN_8|GPIO_PIN_9,GPIO_PIN_SET);
    /* Bounded nine clocks + STOP. Never wait forever for a shorted line. */
    for(i=0;i<9;i++){
        HAL_GPIO_WritePin(GPIOB,GPIO_PIN_8,GPIO_PIN_RESET);delay_us(5);
        HAL_GPIO_WritePin(GPIOB,GPIO_PIN_8,GPIO_PIN_SET);delay_us(5);
    }
    HAL_GPIO_WritePin(GPIOB,GPIO_PIN_9,GPIO_PIN_RESET);delay_us(5);
    HAL_GPIO_WritePin(GPIOB,GPIO_PIN_8,GPIO_PIN_SET);delay_us(5);
    HAL_GPIO_WritePin(GPIOB,GPIO_PIN_9,GPIO_PIN_SET);delay_us(5);
    __HAL_RCC_I2C1_FORCE_RESET();__HAL_RCC_I2C1_RELEASE_RESET();i2c_init();
}
static int rd(void *ctx,uint8_t addr,uint8_t reg,uint8_t *p,uint16_t n){
    HAL_StatusTypeDef rc;(void)ctx;
    if(__HAL_I2C_GET_FLAG(&bus,I2C_FLAG_BUSY))return MPU_BUS_BUSY;
    rc=HAL_I2C_Mem_Read(&bus,(uint16_t)(addr<<1),reg,I2C_MEMADD_SIZE_8BIT,p,n,2);
    if(rc==HAL_OK)return 0;
    if(rc==HAL_TIMEOUT)return MPU_BUS_TIMEOUT;
    if(HAL_I2C_GetError(&bus)&HAL_I2C_ERROR_AF)return MPU_BUS_NACK;
    return MPU_BUS_ERROR;
}
static int wr(void *ctx,uint8_t addr,uint8_t reg,uint8_t v){
    HAL_StatusTypeDef rc;(void)ctx;
    if(__HAL_I2C_GET_FLAG(&bus,I2C_FLAG_BUSY))return MPU_BUS_BUSY;
    rc=HAL_I2C_Mem_Write(&bus,(uint16_t)(addr<<1),reg,I2C_MEMADD_SIZE_8BIT,&v,1,2);
    if(rc==HAL_OK)return 0;
    if(rc==HAL_TIMEOUT)return MPU_BUS_TIMEOUT;
    if(HAL_I2C_GetError(&bus)&HAL_I2C_ERROR_AF)return MPU_BUS_NACK;
    return MPU_BUS_ERROR;
}
static void hardware_init(void){
    RCC_OscInitTypeDef o={0};RCC_ClkInitTypeDef c={0};GPIO_InitTypeDef g={0};
    HAL_Init();__HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    o.OscillatorType=RCC_OSCILLATORTYPE_HSI;o.HSIState=RCC_HSI_ON;
    o.HSICalibrationValue=RCC_HSICALIBRATION_DEFAULT;o.PLL.PLLState=RCC_PLL_ON;
    o.PLL.PLLSource=RCC_PLLSOURCE_HSI;o.PLL.PLLM=16;o.PLL.PLLN=192;
    o.PLL.PLLP=RCC_PLLP_DIV2;o.PLL.PLLQ=4;
    if(HAL_RCC_OscConfig(&o)!=HAL_OK)fail();
    c.ClockType=RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    c.SYSCLKSource=RCC_SYSCLKSOURCE_PLLCLK;c.AHBCLKDivider=RCC_SYSCLK_DIV1;
    c.APB1CLKDivider=RCC_HCLK_DIV2;c.APB2CLKDivider=RCC_HCLK_DIV1;
    if(HAL_RCC_ClockConfig(&c,FLASH_LATENCY_3)!=HAL_OK)fail();
    __HAL_RCC_GPIOA_CLK_ENABLE();__HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_TIM2_CLK_ENABLE();__HAL_RCC_USART1_CLK_ENABLE();__HAL_RCC_I2C1_CLK_ENABLE();
    TIM2->PSC=95;TIM2->ARR=0xffffffffU;TIM2->EGR=TIM_EGR_UG;TIM2->CR1=TIM_CR1_CEN;
    g.Pin=GPIO_PIN_9|GPIO_PIN_10;g.Mode=GPIO_MODE_AF_PP;g.Pull=GPIO_PULLUP;
    g.Speed=GPIO_SPEED_FREQ_VERY_HIGH;g.Alternate=GPIO_AF7_USART1;HAL_GPIO_Init(GPIOA,&g);
    USART1->BRR=208; /* 96 MHz / 208 = 461538 baud, +0.16% nominal. */
    USART1->CR1=USART_CR1_UE|USART_CR1_TE|USART_CR1_RE;
    HAL_NVIC_SetPriority(USART1_IRQn,3,0);HAL_NVIC_EnableIRQ(USART1_IRQn);i2c_init();
}
int main(void){
    uint32_t status_at,old_errors,old_missed,duration,us;unsigned i;int got;
    MpuSample sample;const Mpu6050 *m;uint8_t p[56];uint32_t fields[14];
    hardware_init();PortImu_Init(rd,wr,0,HAL_GetTick());m=PortImu_State();
    W5Period_Init(&period,micros(),2000U);status_at=HAL_GetTick();
    for(;;){
        us=micros();old_missed=period.missed;
        if(!W5Period_Due(&period,us))continue;
        time_start=us;old_errors=m->errors;
        got=PortImu_Service(HAL_GetTick(),time_start,&sample);time_done=micros();
        duration=time_done-time_start;if(duration>service_max)service_max=duration;
        if(got==1){
            last_valid_start=time_start;put32(p,time_start);put32(p+4,0);put32(p+8,duration);
            for(i=0;i<7;i++)put16(p+12+2*i,(uint16_t)sample.raw[i]);
            put16(p+26,(uint16_t)(1U|((period.missed!=old_missed || duration>=2000U)?4U:0U)));put16(p+28,0);
            send_frame(sample.sequence,0xa0,p,30);
        }
        if(m->errors!=old_errors)recover(); /* driver imposes 500 ms retry backoff */
        if((uint32_t)(HAL_GetTick()-status_at)>=1000U){
            status_at=HAL_GetTick();fields[0]=1;fields[1]=time_start;fields[2]=time_done;
            fields[3]=last_valid_start;fields[4]=m->samples;fields[5]=m->errors;
            fields[6]=m->recoveries;fields[7]=period.missed;fields[8]=period.max_error;
            fields[9]=period.violations;fields[10]=service_max;fields[11]=tx_dropped;
            fields[12]=m->online;fields[13]=m->valid;
            for(i=0;i<14;i++)put32(p+4*i,fields[i]);send_frame(m->samples,0xa5,p,56);
            fields[0]=1;fields[1]=period.calls;fields[2]=period.intervals;
            fields[3]=period.intervals?(uint32_t)(period.interval_sum/period.intervals):0;
            fields[4]=period.max_error;fields[5]=W5Period_P99(&period);
            fields[6]=period.missed;fields[7]=period.violations;
            fields[8]=(uint32_t)period.interval_sum;fields[9]=(uint32_t)(period.interval_sum>>32);
            for(i=0;i<10;i++)put32(p+4*i,fields[i]);send_frame(m->samples,0xa6,p,40);
        }
    }
}
