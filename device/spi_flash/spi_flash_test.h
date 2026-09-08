#ifndef SPI_FLASH_TEST_H
#define SPI_FLASH_TEST_H
#include "spi_flash.h"
#define SPI_FLASH_TEST_ROUNDS 100U
typedef struct {
    SpiFlash *flash;
    uint32_t sector, program_address, scan_offset, start_ms, elapsed_ms;
    uint32_t mismatches, first_mismatch_address;
    uint16_t rounds_completed, cross_page_rounds;
    uint8_t active, state;
    int result;
    uint8_t pattern[320];
} SpiFlashTest;
void SpiFlashTest_Init(SpiFlashTest *, SpiFlash *);
/* Explicit destructive start; requires previously reserved complete sector. */
int SpiFlashTest_Start(SpiFlashTest *, uint32_t sector, uint32_t now_ms);
/* At most one <=68-byte transaction and <=64-byte comparison per invocation.
 * Owns flash service while active. Leave ISR code entirely out of this path. */
int SpiFlashTest_Service(SpiFlashTest *, uint32_t now_ms);
#endif
