#include "spi_flash.h"
#include "spi_flash_test.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t memory[8192], cs, wel, stuck, deny_wel, busy;
    uint32_t id, calls, writes, erases, reads, wrens, max_len;
    int fail_next, corrupt_read;
} Model;
static Model model;
static SpiFlash flash;
static void select_flash(void *ctx, uint8_t active)
{ Model *m = ctx; assert(active != m->cs); m->cs = active; }
static int transfer(void *ctx, const uint8_t *tx, uint8_t *rx, uint16_t len)
{
    Model *m = ctx; uint32_t a; uint16_t i;
    assert(m->cs && len <= 68); ++m->calls;
    if (len > m->max_len) m->max_len = len;
    if (m->fail_next) { m->fail_next = 0; return -1; }
    memset(rx, 0, len);
    switch (tx[0]) {
    case 0x9F: assert(len == 4); rx[1] = (uint8_t)(m->id >> 16); rx[2] = (uint8_t)(m->id >> 8); rx[3] = (uint8_t)m->id; break;
    case 0x05: assert(len == 2); rx[1] = (uint8_t)((m->wel ? 2 : 0) | ((m->stuck || m->busy) ? 1 : 0)); if (m->busy) --m->busy; break;
    case 0x06: assert(len == 1); ++m->wrens; if (!m->deny_wel) m->wel = 1; break;
    case 0x03:
        a = ((uint32_t)tx[1] << 16) | ((uint32_t)tx[2] << 8) | tx[3];
        assert(a + len - 4 <= sizeof(m->memory)); ++m->reads;
        memcpy(rx + 4, m->memory + a, len - 4);
        if (m->corrupt_read) rx[4] ^= 1;
        break;
    case 0x02:
        a = ((uint32_t)tx[1] << 16) | ((uint32_t)tx[2] << 8) | tx[3];
        assert(m->wel && !m->busy && !m->stuck); assert((a & 255U) + len - 4 <= 256U);
        assert(a >= 4096 && a + len - 4 <= 8192); ++m->writes;
        for (i = 4; i < len; ++i) m->memory[a + i - 4] &= tx[i];
        m->wel = 0; m->busy = 2; break;
    case 0x20:
        a = ((uint32_t)tx[1] << 16) | ((uint32_t)tx[2] << 8) | tx[3];
        assert(m->wel && !m->busy && !m->stuck && a == 4096 && len == 4);
        memset(m->memory + a, 255, 4096); ++m->erases; m->wel = 0; m->busy = 3; break;
    default: assert(0);
    }
    return 0;
}
static void setup(void)
{
    memset(&model, 0, sizeof(model)); model.id = 0xEF4018;
    memset(model.memory, 0x5A, sizeof(model.memory));
    assert(SpiFlash_Init(&flash, transfer, select_flash, &model) == SPI_FLASH_OK);
    assert(!model.cs && model.calls == 1 && flash.jedec_id == model.id);
}
static int drain(uint32_t start)
{
    uint32_t n, before;
    for (n = 0; n < 3000 && flash.active; ++n) {
        before = model.calls; SpiFlash_Service(&flash, start + n);
        assert(model.calls - before <= 1 && !model.cs);
    }
    assert(!flash.active); return flash.result;
}
static void test_id_bounds(void)
{
    uint8_t data[65] = {0}; setup();
    assert(SpiFlash_StartErase(&flash, 4096, 0) == SPI_FLASH_E_RESERVED);
    assert(SpiFlash_Reserve(&flash, 1, 4096) == SPI_FLASH_E_RANGE);
    assert(SpiFlash_Reserve(&flash, 4096, 0) == SPI_FLASH_E_RANGE);
    assert(SpiFlash_Reserve(&flash, 0xFFFFF000U, 8192) == SPI_FLASH_E_RANGE);
    assert(SpiFlash_Reserve(&flash, SPI_FLASH_CAPACITY - 4096, 8192) == SPI_FLASH_E_RANGE);
    assert(SpiFlash_Reserve(&flash, 4096, 4096) == 0);
    assert(SpiFlash_StartErase(&flash, 4097, 0) == SPI_FLASH_E_RANGE);
    assert(SpiFlash_StartErase(&flash, 0, 0) == SPI_FLASH_E_RESERVED);
    assert(SpiFlash_StartProgram(&flash, 8191, data, 2, 0) == SPI_FLASH_E_RESERVED);
    assert(SpiFlash_StartProgram(&flash, 0xFFFFFFFFU, data, 2, 0) == SPI_FLASH_E_RANGE);
    assert(SpiFlash_Read(&flash, 4096, data, 65) == SPI_FLASH_E_ARGUMENT);
    assert(SpiFlash_Read(&flash, SPI_FLASH_CAPACITY, data, 1) == SPI_FLASH_E_RANGE);
    model.id = 0xEF4017;
    assert(SpiFlash_Init(&flash, transfer, select_flash, &model) == SPI_FLASH_E_ID);
    assert(SpiFlash_StartErase(&flash, 4096, 0) < 0);
    model.id = 0xEF7018; assert(SpiFlash_Init(&flash, transfer, select_flash, &model) == 0);
    model.fail_next = 1; assert(SpiFlash_Init(&flash, transfer, select_flash, &model) == SPI_FLASH_E_IO); assert(!model.cs);
}
static void test_program_erase(void)
{
    uint8_t data[320], saved[320], out[64]; unsigned i; setup();
    assert(SpiFlash_Reserve(&flash, 4096, 4096) == 0);
    assert(SpiFlash_StartErase(&flash, 4096, 0) == SPI_FLASH_BUSY);
    assert(SpiFlash_Read(&flash, 4096, out, 64) == SPI_FLASH_BUSY);
    assert(SpiFlash_Reserve(&flash, 0, 4096) == SPI_FLASH_BUSY);
    assert(drain(0) == 0 && model.erases == 1);
    for (i = 0; i < 320; ++i) saved[i] = data[i] = (uint8_t)(i * 17);
    assert(SpiFlash_StartProgram(&flash, 4096 + 253, data, 320, 20) == SPI_FLASH_BUSY);
    memset(data, 0, sizeof(data)); assert(drain(20) == 0);
    assert(memcmp(model.memory + 4096 + 253, saved, 320) == 0);
    assert(model.wrens == model.writes + model.erases);
    assert(SpiFlash_Read(&flash, 4096 + 253, out, 64) == 0 && memcmp(out, saved, 64) == 0);
    for (i = 0; i < 4096; ++i) assert(model.memory[i] == 0x5A);
}
static void test_faults(void)
{
    unsigned fail_step; uint8_t data[64] = {0};
    for (fail_step = 0; fail_step < 5; ++fail_step) {
        unsigned n; setup(); assert(SpiFlash_Reserve(&flash, 4096, 4096) == 0);
        assert(SpiFlash_StartErase(&flash, 4096, 0) == SPI_FLASH_BUSY);
        for (n = 0; n < fail_step; ++n) SpiFlash_Service(&flash, n);
        model.fail_next = 1; assert(drain(fail_step) == SPI_FLASH_E_IO && !model.cs);
        assert(!flash.ready);
    }
    setup(); SpiFlash_Reserve(&flash, 4096, 4096); model.deny_wel = 1;
    SpiFlash_StartErase(&flash, 4096, 0); assert(drain(0) == SPI_FLASH_E_WEL && !model.erases);
    setup(); SpiFlash_Reserve(&flash, 4096, 4096); model.stuck = 1;
    SpiFlash_StartErase(&flash, 4096, 0xFFFFFFF0U);
    assert(drain(0xFFFFFFF0U) == SPI_FLASH_E_TIMEOUT && !model.erases);
    setup(); SpiFlash_Reserve(&flash, 4096, 4096); SpiFlash_StartErase(&flash, 4096, 0);
    while (!model.erases) SpiFlash_Service(&flash, 0);
    model.stuck = 1; assert(drain(0) == SPI_FLASH_E_TIMEOUT);
    setup(); SpiFlash_Reserve(&flash, 4096, 4096);
    SpiFlash_StartProgram(&flash, 4096, data, sizeof(data), 0);
    while (!model.writes) SpiFlash_Service(&flash, 0);
    model.stuck = 1; assert(drain(0) == SPI_FLASH_E_TIMEOUT);
    assert(!flash.ready && model.writes == 1);
    setup(); model.fail_next = 1;
    assert(SpiFlash_Read(&flash, 4096, data, sizeof(data)) == SPI_FLASH_E_IO);
    assert(!model.cs && !flash.ready);
}
static void test_hundred_rounds(void)
{
    SpiFlashTest test; uint32_t n, before; setup(); SpiFlashTest_Init(&test, &flash);
    assert(SpiFlashTest_Start(&test, 4096, 0) == SPI_FLASH_E_RESERVED);
    SpiFlash_Reserve(&flash, 4096, 4096);
    assert(SpiFlashTest_Start(&test, 4096, 100) == SPI_FLASH_BUSY);
    for (n = 100; n < 100000 && test.active; ++n) {
        before = model.calls; SpiFlashTest_Service(&test, n);
        assert(model.calls - before <= 1 && !model.cs);
    }
    assert(!test.active && test.result == 0 && test.rounds_completed == 100);
    assert(test.cross_page_rounds >= 20 && test.mismatches == 0 && test.elapsed_ms > 0);
    assert(model.erases == 100 && model.max_len <= 68);
    for (n = 0; n < 4096; ++n) assert(model.memory[n] == 0x5A);
    setup(); SpiFlash_Reserve(&flash, 4096, 4096); SpiFlashTest_Init(&test, &flash);
    SpiFlashTest_Start(&test, 4096, 0); model.corrupt_read = 1;
    for (n = 0; n < 2000 && test.active; ++n) SpiFlashTest_Service(&test, n);
    assert(!test.active && test.result == SPI_FLASH_E_VERIFY && test.mismatches > 0);
    assert(test.first_mismatch_address == 4096);
    setup(); SpiFlash_Reserve(&flash, 4096, 4096); SpiFlashTest_Init(&test, &flash);
    SpiFlashTest_Start(&test, 4096, 0);
    for (n = 0; n < 2000 && test.active; ++n) {
        SpiFlashTest_Service(&test, n);
        if (model.writes) model.memory[8000] = 0;
    }
    assert(!test.active && test.result == SPI_FLASH_E_VERIFY);
    assert(test.rounds_completed == 0 && test.first_mismatch_address == 8000);
}
int main(void)
{ test_id_bounds(); test_program_erase(); test_faults(); test_hundred_rounds(); puts("PASS: SPI NOR protocol, bounds, injected faults, 100 cooperative rounds (host model only)"); return 0; }
