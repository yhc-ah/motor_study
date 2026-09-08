#include "bsp_uart_tx.h"
#include <assert.h>
#include <stdio.h>
UART_HandleTypeDef huart1={USART1};
static uint32_t tick,calls,aborts;
static uint8_t *dma_data;
static uint8_t abort_fail;
uint32_t HAL_GetTick(void){return tick;}
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *h,uint8_t *p,uint16_t n){
    assert(h==&huart1 && n==2);dma_data=p;calls++;return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *h){assert(h==&huart1);aborts++;return abort_fail?HAL_ERROR:HAL_OK;}
extern void HAL_UART_TxCpltCallback(UART_HandleTypeDef *);
int main(void){
    uint8_t data[2]={1,2};BSP_UART_TxInit();
    assert(BSP_UART_SendQueued(data,2)==HAL_OK);BSP_UART_TxService();
    assert(calls==1 && dma_data[0]==1);data[0]=9;
    assert(dma_data[0]==1);BSP_UART_TxService();assert(calls==1);
    HAL_UART_TxCpltCallback(&huart1);assert(BSP_UART_TxStats()->count==1);
    BSP_UART_TxService();assert(!BSP_UART_TxStats()->count);
    tick=0x80000010U;BSP_UART_SendQueued(data,2);BSP_UART_TxService();assert(calls==2);
    tick+=101;BSP_UART_TxService();assert(aborts==1 && BSP_UART_TxErrors()==1);
    tick+=11;BSP_UART_SendQueued(data,2);BSP_UART_TxService();assert(calls==3);
    HAL_UART_TxCpltCallback(&huart1);BSP_UART_TxService();
    tick=0xfffffff0U;BSP_UART_SendQueued(data,2);BSP_UART_TxService();assert(calls==4);
    tick=0x60;BSP_UART_TxService();assert(aborts==2);
    tick+=11;BSP_UART_SendQueued(data,2);BSP_UART_TxService();
    abort_fail=1;tick+=101;BSP_UART_TxService();
    assert(BSP_UART_TxStats()->count==1);tick+=101;BSP_UART_TxService();
    assert(BSP_UART_TxStats()->count==1 && aborts==3);
    puts("test_uart_tx: PASS");return 0;
}
