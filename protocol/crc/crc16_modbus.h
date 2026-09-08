#ifndef CRC16_MODBUS_H
#define CRC16_MODBUS_H

#include <stdint.h>

uint16_t CRC16_Modbus(const uint8_t *data, uint32_t length);
uint16_t CRC16_ModbusUpdate(uint16_t crc,
                            const uint8_t *data,
                            uint32_t length);

#endif
