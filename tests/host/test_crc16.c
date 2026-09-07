#include "crc16_modbus.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    static const uint8_t vector[] = {
        '1', '2', '3', '4', '5', '6', '7', '8', '9'
    };

    assert(CRC16_Modbus(NULL, 0U) == 0xFFFFU);
    assert(CRC16_Modbus(NULL, 1U) == 0xFFFFU);
    assert(CRC16_Modbus(vector, sizeof(vector)) == 0x4B37U);
    puts("test_crc16: PASS");
    return 0;
}
