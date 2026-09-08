#include "bsp_uart_tx.h"
#include "usart.h"
static TxQueue queue;
static volatile uint8_t completed;
static uint8_t active,cooldown,fault;
static uint32_t started,errors,retry_at;
void BSP_UART_TxInit(void){TxQueue_Init(&queue);completed=0;active=0;errors=0;retry_at=0;cooldown=0;fault=0;}
HAL_StatusTypeDef BSP_UART_SendQueued(const uint8_t *p,uint16_t n){return TxQueue_Push(&queue,p,n)?HAL_OK:HAL_ERROR;}
const TxQueue *BSP_UART_TxStats(void){return &queue;}
uint32_t BSP_UART_TxErrors(void){return errors;}
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *h){if(h->Instance==USART1)completed=1;}
void BSP_UART_TxService(void) {
    uint16_t n;const uint8_t *p;HAL_StatusTypeDef rc;uint32_t now=HAL_GetTick();
    if(fault)return; /* A failed DMA stop pins its buffer until MCU reset. */
    if(active && completed){completed=0;active=0;TxQueue_Pop(&queue);}
    if(active && (uint32_t)(now-started)>100U){
        if(HAL_UART_AbortTransmit(&huart1)!=HAL_OK){errors++;fault=1;return;}
        active=0;completed=0;
        errors++;TxQueue_Pop(&queue);retry_at=now+10U;cooldown=1;
    }
    if(active)return;
    if(cooldown){if((int32_t)(now-retry_at)<0)return;cooldown=0;}
    p=TxQueue_Peek(&queue,&n);if(!p)return;
    completed=0;rc=HAL_UART_Transmit_DMA(&huart1,(uint8_t*)p,n);
    if(rc==HAL_OK){active=1;started=now;}
    else {errors++;TxQueue_Pop(&queue);retry_at=now+10U;cooldown=1;}
}
