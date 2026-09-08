#ifndef SPI_FLASH_H
#define SPI_FLASH_H
#include <stdint.h>

#define SPI_FLASH_CAPACITY 0x01000000UL
#define SPI_FLASH_SECTOR_SIZE 4096U
#define SPI_FLASH_READ_MAX 64U
#define SPI_FLASH_PROGRAM_MAX 512U

enum { SPI_FLASH_OK = 0, SPI_FLASH_BUSY = 1,
       SPI_FLASH_E_ARGUMENT = -1, SPI_FLASH_E_IO = -2,
       SPI_FLASH_E_ID = -3, SPI_FLASH_E_RANGE = -4,
       SPI_FLASH_E_RESERVED = -5, SPI_FLASH_E_WEL = -6,
       SPI_FLASH_E_TIMEOUT = -7, SPI_FLASH_E_VERIFY = -8 };
typedef int (*SpiFlashTransfer)(void *, const uint8_t *, uint8_t *, uint16_t);
typedef void (*SpiFlashSelect)(void *, uint8_t);
typedef struct {
    SpiFlashTransfer transfer;
    SpiFlashSelect select;
    void *context;
    uint32_t jedec_id, reserved_base, reserved_size;
    uint32_t address, deadline_start, timeout_ms;
    uint16_t remaining, offset, chunk;
    uint8_t active, ready, state, erase;
    int result;
    uint8_t program_data[SPI_FLASH_PROGRAM_MAX];
} SpiFlash;

/* Foreground only. Transfer MUST itself have a finite timeout. CS is always
 * released after each call, including failures. No heap or IRQ bus access.
 * Whitelist: W25Q128 FV/JV EF4018 and JV DTR EF7018 in standard SPI mode. */
int SpiFlash_Init(SpiFlash *, SpiFlashTransfer, SpiFlashSelect, void *);
/* No default reserved range. Reserve rejects zero, misalignment, overflow.
 * Caller must ensure this range contains no firmware or persistent data. */
int SpiFlash_Reserve(SpiFlash *, uint32_t base, uint32_t size);
/* At most 64 payload bytes per read; unavailable while a mutation is active. */
int SpiFlash_Read(SpiFlash *, uint32_t address, uint8_t *, uint16_t length);
/* Copies up to 512 input bytes, safe to reuse caller's buffer after return. */
int SpiFlash_StartProgram(SpiFlash *, uint32_t, const uint8_t *, uint16_t, uint32_t now_ms);
int SpiFlash_StartErase(SpiFlash *, uint32_t sector_address, uint32_t now_ms);
/* At most one transport call (<=68 bytes) per service. BUSY until finished.
 * After an error, reinitialize before retrying; Init clears reserved range. */
int SpiFlash_Service(SpiFlash *, uint32_t now_ms);
#endif
