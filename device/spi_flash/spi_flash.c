#include "spi_flash.h"
#include <string.h>

enum { WAIT_READY, WRITE_ENABLE, CHECK_WEL, ISSUE_COMMAND, WAIT_COMPLETE };

static int exchange(SpiFlash *f, const uint8_t *tx, uint8_t *rx, uint16_t n)
{
    int result;
    f->select(f->context, 1);
    result = f->transfer(f->context, tx, rx, n);
    f->select(f->context, 0);
    return result == 0 ? SPI_FLASH_OK : SPI_FLASH_E_IO;
}
static int finish(SpiFlash *f, int result)
{
    f->active = 0;
    f->result = result;
    /* The device may still be busy after a transport/deadline error. Require
     * a fresh explicit initialization + reservation before any further work. */
    if (result < 0) f->ready = 0;
    return result;
}
static int physical_range(uint32_t address, uint32_t n)
{ return n != 0 && address < SPI_FLASH_CAPACITY && n <= SPI_FLASH_CAPACITY - address; }
static int mutation_range(SpiFlash *f, uint32_t address, uint32_t n)
{
    if (!f || !f->ready) return SPI_FLASH_E_ARGUMENT;
    if (f->active) return SPI_FLASH_BUSY;
    if (!physical_range(address, n)) return SPI_FLASH_E_RANGE;
    if (!f->reserved_size || address < f->reserved_base ||
        n > f->reserved_size || address - f->reserved_base > f->reserved_size - n)
        return SPI_FLASH_E_RESERVED;
    return SPI_FLASH_OK;
}
static void set_address(uint8_t *tx, uint32_t address)
{
    tx[1] = (uint8_t)(address >> 16);
    tx[2] = (uint8_t)(address >> 8);
    tx[3] = (uint8_t)address;
}
int SpiFlash_Init(SpiFlash *f, SpiFlashTransfer transfer, SpiFlashSelect select, void *context)
{
    uint8_t tx[4] = { 0x9F, 0, 0, 0 }, rx[4];
    int result;
    if (!f || !transfer || !select) return SPI_FLASH_E_ARGUMENT;
    memset(f, 0, sizeof(*f));
    f->transfer = transfer; f->select = select; f->context = context;
    result = exchange(f, tx, rx, 4);
    if (result != SPI_FLASH_OK) return finish(f, result);
    f->jedec_id = ((uint32_t)rx[1] << 16) | ((uint32_t)rx[2] << 8) | rx[3];
    if (f->jedec_id != 0xEF4018UL && f->jedec_id != 0xEF7018UL)
        return finish(f, SPI_FLASH_E_ID);
    f->ready = 1;
    return SPI_FLASH_OK;
}
int SpiFlash_Reserve(SpiFlash *f, uint32_t base, uint32_t size)
{
    if (!f || !f->ready) return SPI_FLASH_E_ARGUMENT;
    if (f->active) return SPI_FLASH_BUSY;
    if ((base % SPI_FLASH_SECTOR_SIZE) || (size % SPI_FLASH_SECTOR_SIZE) || !physical_range(base, size))
        return SPI_FLASH_E_RANGE;
    f->reserved_base = base; f->reserved_size = size;
    return SPI_FLASH_OK;
}
int SpiFlash_Read(SpiFlash *f, uint32_t address, uint8_t *out, uint16_t length)
{
    uint8_t tx[68] = {0}, rx[68]; int result;
    if (!f || !f->ready || !out || !length || length > SPI_FLASH_READ_MAX)
        return SPI_FLASH_E_ARGUMENT;
    if (f->active) return SPI_FLASH_BUSY;
    if (!physical_range(address, length)) return SPI_FLASH_E_RANGE;
    tx[0] = 0x03; set_address(tx, address);
    result = exchange(f, tx, rx, (uint16_t)(length + 4));
    if (result != SPI_FLASH_OK) return finish(f, result);
    memcpy(out, rx + 4, length);
    return SPI_FLASH_OK;
}
static int start(SpiFlash *f, uint32_t address, uint32_t now_ms, uint8_t erase)
{
    f->address = address; f->offset = 0; f->erase = erase;
    f->active = 1; f->state = WAIT_READY;
    f->deadline_start = now_ms; f->timeout_ms = 1000;
    f->result = SPI_FLASH_BUSY;
    return SPI_FLASH_BUSY;
}
int SpiFlash_StartProgram(SpiFlash *f, uint32_t address, const uint8_t *data, uint16_t length, uint32_t now_ms)
{
    int result;
    if (!data || !length || length > SPI_FLASH_PROGRAM_MAX) return SPI_FLASH_E_ARGUMENT;
    result = mutation_range(f, address, length);
    if (result != SPI_FLASH_OK) return result;
    memcpy(f->program_data, data, length); f->remaining = length;
    return start(f, address, now_ms, 0);
}
int SpiFlash_StartErase(SpiFlash *f, uint32_t address, uint32_t now_ms)
{
    int result;
    if (address % SPI_FLASH_SECTOR_SIZE) return SPI_FLASH_E_RANGE;
    result = mutation_range(f, address, SPI_FLASH_SECTOR_SIZE);
    if (result != SPI_FLASH_OK) return result;
    f->remaining = 0;
    return start(f, address, now_ms, 1);
}
int SpiFlash_Service(SpiFlash *f, uint32_t now_ms)
{
    uint8_t tx[68] = {0}, rx[68]; uint16_t length = 2; int result;
    if (!f) return SPI_FLASH_E_ARGUMENT;
    if (!f->active) return f->result;
    if ((uint32_t)(now_ms - f->deadline_start) >= f->timeout_ms)
        return finish(f, SPI_FLASH_E_TIMEOUT);
    if (f->state == WRITE_ENABLE) { tx[0] = 0x06; length = 1; }
    else if (f->state == ISSUE_COMMAND) {
        tx[0] = f->erase ? 0x20 : 0x02; set_address(tx, f->address); length = 4;
        if (!f->erase) {
            f->chunk = (uint16_t)(256U - (f->address & 255U));
            if (f->chunk > SPI_FLASH_READ_MAX) f->chunk = SPI_FLASH_READ_MAX;
            if (f->chunk > f->remaining) f->chunk = f->remaining;
            memcpy(tx + 4, f->program_data + f->offset, f->chunk);
            length = (uint16_t)(length + f->chunk);
        }
    } else tx[0] = 0x05;
    result = exchange(f, tx, rx, length);
    if (result != SPI_FLASH_OK) return finish(f, result);
    switch (f->state) {
    case WAIT_READY:
        if (!(rx[1] & 1U)) f->state = WRITE_ENABLE;
        break;
    case WRITE_ENABLE: f->state = CHECK_WEL; break;
    case CHECK_WEL:
        if (rx[1] & 1U) return finish(f, SPI_FLASH_E_WEL);
        if (!(rx[1] & 2U)) return finish(f, SPI_FLASH_E_WEL);
        f->state = ISSUE_COMMAND; break;
    case ISSUE_COMMAND:
        f->deadline_start = now_ms; f->timeout_ms = f->erase ? 1000U : 20U;
        f->state = WAIT_COMPLETE; break;
    case WAIT_COMPLETE:
        if (rx[1] & 1U) break;
        if (f->erase) return finish(f, SPI_FLASH_OK);
        f->remaining = (uint16_t)(f->remaining - f->chunk);
        f->offset = (uint16_t)(f->offset + f->chunk); f->address += f->chunk;
        if (!f->remaining) return finish(f, SPI_FLASH_OK);
        f->state = WRITE_ENABLE; f->deadline_start = now_ms; f->timeout_ms = 1000;
        break;
    default: return finish(f, SPI_FLASH_E_ARGUMENT);
    }
    return SPI_FLASH_BUSY;
}
