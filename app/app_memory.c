#include "app_memory.h"
#include "stm32f4xx_hal.h"
#include <stdint.h>

const uint8_t flash_table[64] = {1, 2, 3};

uint8_t init_data[128] = {1, 2, 3};

static uint8_t zero_buffer[256];

volatile uint32_t memory_probe_sink;

void APP_MemoryProbe(void)
{
    uint32_t tick = HAL_GetTick();

    /*
     * volatile 指针用于防止 Release 优化时，
     * 编译器把整个实验变量删除或常量折叠。
     */
    volatile const uint8_t *flash_ptr = flash_table;
    volatile uint8_t *data_ptr = init_data;
    volatile uint8_t *zero_ptr = zero_buffer;

    uint32_t flash_index = tick & 63U;
    uint32_t data_index  = tick & 127U;
    uint32_t zero_index  = tick & 255U;

    zero_ptr[zero_index] = (uint8_t)tick;

    memory_probe_sink =
        flash_ptr[flash_index] +
        data_ptr[data_index] +
        zero_ptr[zero_index];
}