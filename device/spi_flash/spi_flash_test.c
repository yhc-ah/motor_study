#include "spi_flash_test.h"
#include <string.h>
enum { TEST_IDLE, TEST_ERASE, TEST_WAIT_ERASE, TEST_BLANK,
       TEST_PATTERN, TEST_PROGRAM, TEST_WAIT_PROGRAM, TEST_COMPARE, TEST_DONE };
static int test_finish(SpiFlashTest *t, int result)
{ t->active = 0; t->state = TEST_DONE; t->result = result; return result; }
void SpiFlashTest_Init(SpiFlashTest *t, SpiFlash *f)
{
    if (!t) return;
    memset(t, 0, sizeof(*t)); t->flash = f;
    t->first_mismatch_address = 0xFFFFFFFFUL;
}
int SpiFlashTest_Start(SpiFlashTest *t, uint32_t sector, uint32_t now_ms)
{
    SpiFlash *f;
    if (!t || !t->flash || !t->flash->ready) return SPI_FLASH_E_ARGUMENT;
    f = t->flash;
    if (t->active || f->active) return SPI_FLASH_BUSY;
    if (sector % SPI_FLASH_SECTOR_SIZE || sector >= SPI_FLASH_CAPACITY) return SPI_FLASH_E_RANGE;
    if (f->reserved_size < SPI_FLASH_SECTOR_SIZE || sector < f->reserved_base ||
        sector - f->reserved_base > f->reserved_size - SPI_FLASH_SECTOR_SIZE) return SPI_FLASH_E_RESERVED;
    SpiFlashTest_Init(t, f); t->sector = sector; t->start_ms = now_ms;
    t->active = 1; t->state = TEST_ERASE; t->result = SPI_FLASH_BUSY;
    return SPI_FLASH_BUSY;
}
static uint8_t pattern_byte(uint16_t round, uint32_t offset)
{
    switch (round % 5U) {
    case 0: return 0x00;
    case 1: return 0xAA;
    case 2: return 0x55;
    case 3: return (uint8_t)(offset + round);
    default: return (uint8_t)((offset * 73U + round * 29U) ^ (offset >> 2));
    }
}
static uint8_t expected_byte(SpiFlashTest *t, uint32_t address)
{
    if (address >= t->program_address && address - t->program_address < sizeof(t->pattern))
        return t->pattern[address - t->program_address];
    return 0xFF;
}
int SpiFlashTest_Service(SpiFlashTest *t, uint32_t now_ms)
{
    uint8_t readback[SPI_FLASH_READ_MAX]; uint32_t i, count, address; int result;
    if (!t || !t->flash) return SPI_FLASH_E_ARGUMENT;
    if (!t->active) return t->result;
    t->elapsed_ms = now_ms - t->start_ms;
    switch (t->state) {
    case TEST_ERASE:
        result = SpiFlash_StartErase(t->flash, t->sector, now_ms);
        if (result != SPI_FLASH_BUSY) return test_finish(t, result);
        t->state = TEST_WAIT_ERASE; break;
    case TEST_WAIT_ERASE:
    case TEST_WAIT_PROGRAM:
        result = SpiFlash_Service(t->flash, now_ms);
        if (result < 0) return test_finish(t, result);
        if (result == SPI_FLASH_OK) {
            t->scan_offset = 0;
            t->state = t->state == TEST_WAIT_ERASE ? TEST_BLANK : TEST_COMPARE;
        }
        break;
    case TEST_BLANK:
    case TEST_COMPARE:
        address = t->sector + t->scan_offset;
        result = SpiFlash_Read(t->flash, address, readback, SPI_FLASH_READ_MAX);
        if (result != SPI_FLASH_OK) return test_finish(t, result);
        for (i = 0; i < SPI_FLASH_READ_MAX; ++i) {
            uint8_t expected = t->state == TEST_BLANK ? 0xFF : expected_byte(t, address + i);
            if (readback[i] != expected) {
                if (!t->mismatches) t->first_mismatch_address = address + i;
                ++t->mismatches;
            }
        }
        t->scan_offset += SPI_FLASH_READ_MAX;
        if (t->scan_offset == SPI_FLASH_SECTOR_SIZE) {
            if (t->mismatches) return test_finish(t, SPI_FLASH_E_VERIFY);
            if (t->state == TEST_BLANK) {
                t->scan_offset = 0; t->state = TEST_PATTERN;
                t->program_address = t->sector + 253U + ((uint32_t)t->rounds_completed * 37U % 3000U);
            } else {
                ++t->rounds_completed; ++t->cross_page_rounds;
                if (t->rounds_completed == SPI_FLASH_TEST_ROUNDS) return test_finish(t, SPI_FLASH_OK);
                t->state = TEST_ERASE;
            }
        }
        break;
    case TEST_PATTERN:
        count = sizeof(t->pattern) - t->scan_offset;
        if (count > SPI_FLASH_READ_MAX) count = SPI_FLASH_READ_MAX;
        for (i = 0; i < count; ++i)
            t->pattern[t->scan_offset + i] = pattern_byte(t->rounds_completed, t->scan_offset + i);
        t->scan_offset += count;
        if (t->scan_offset == sizeof(t->pattern)) t->state = TEST_PROGRAM;
        break;
    case TEST_PROGRAM:
        result = SpiFlash_StartProgram(t->flash, t->program_address, t->pattern, sizeof(t->pattern), now_ms);
        if (result != SPI_FLASH_BUSY) return test_finish(t, result);
        t->state = TEST_WAIT_PROGRAM; break;
    default: return test_finish(t, SPI_FLASH_E_ARGUMENT);
    }
    return SPI_FLASH_BUSY;
}
