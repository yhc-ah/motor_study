#include "i2c.h"
#include "week3_config.h"
I2C_HandleTypeDef hi2c1;
void MX_I2C1_Init(void) {
    hi2c1.Instance=I2C1;hi2c1.Init.ClockSpeed=WEEK3_I2C_HZ;
    hi2c1.Init.DutyCycle=I2C_DUTYCYCLE_2;hi2c1.Init.OwnAddress1=0;
    hi2c1.Init.AddressingMode=I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode=I2C_DUALADDRESS_DISABLE;hi2c1.Init.OwnAddress2=0;
    hi2c1.Init.GeneralCallMode=I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode=I2C_NOSTRETCH_DISABLE;
    if(HAL_I2C_Init(&hi2c1)!=HAL_OK)Error_Handler();
}
void HAL_I2C_MspInit(I2C_HandleTypeDef *h) {
    GPIO_InitTypeDef g={0};if(h->Instance!=I2C1)return;
    __HAL_RCC_GPIOB_CLK_ENABLE();__HAL_RCC_I2C1_CLK_ENABLE();
    g.Pin=GPIO_PIN_8|GPIO_PIN_9;g.Mode=GPIO_MODE_AF_OD;g.Pull=GPIO_NOPULL;
    g.Speed=GPIO_SPEED_FREQ_VERY_HIGH;g.Alternate=GPIO_AF4_I2C1;HAL_GPIO_Init(GPIOB,&g);
}
void HAL_I2C_MspDeInit(I2C_HandleTypeDef *h) {
    if(h->Instance!=I2C1)return;
    __HAL_RCC_I2C1_CLK_DISABLE();HAL_GPIO_DeInit(GPIOB,GPIO_PIN_8|GPIO_PIN_9);
}
