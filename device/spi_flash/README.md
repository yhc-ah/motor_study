# Cooperative SPI NOR subsystem

This is portable C99 with no allocation or STM32 dependency. The board adapter
provides a bounded foreground `transfer(context, tx, rx, length)` and
`select(context, active)` callback. The transfer clocks every supplied byte,
fills the receive buffer, returns zero on success, and must impose a finite
hardware timeout. The driver releases CS on success and failure. Each service
invocation issues at most one transfer of at most 68 bytes (64 payload bytes).

```c
static SpiFlash flash;
static SpiFlashTest flash_test;

/* Boot: identify only. This does not erase/program or assume any sector free. */
int result = SpiFlash_Init(&flash, board_spi_transfer, board_flash_cs, 0);
SpiFlashTest_Init(&flash_test, &flash);

/* Only after a user confirms an actual free, dedicated sector: */
result = SpiFlash_Reserve(&flash, explicitly_reserved_base, 4096);
/* Only on an explicit destructive-test start command: */
result = SpiFlashTest_Start(&flash_test, explicitly_reserved_base, now_ms);

/* Foreground main loop: give test exclusive ownership while it is active. */
if (flash_test.active) {
    result = SpiFlashTest_Service(&flash_test, now_ms);
} else {
    result = SpiFlash_Service(&flash, now_ms);
}
```

`SPI_FLASH_BUSY` means an accepted operation is still running, or a new request
was refused because an operation was already active. Check `active` before
dispatching a new request. `result` retains terminal success/error. Any transport,
WEL or deadline failure invalidates `ready`: diagnose the board first, then
explicitly initialize and reserve again. A timeout cannot cancel an erase already
accepted by the physical part. Initialization reads JEDEC once; normal reads,
programming and self-test never re-read it. Supported standard-SPI IDs are
`EF4018` and `EF7018`; other chips are rejected even if their capacity matches.

Reads accept 1–64 bytes within 16 MiB. Programs accept 1–512 bytes, copying the
caller data immediately. The service splits writes into at most 64-byte chunks
without crossing 256-byte boundaries and performs a separate WREN/WEL sequence
for every chunk. Erase accepts exactly one aligned 4096-byte sector. All mutations
must fit entirely in a validated explicit reserved window. This driver does not
erase implicitly before programming: the caller must respect NOR 1-to-0 rules.

The 100-round test is destructive and tests one reserved sector. Every round
erases, checks all 4096 bytes for `FF`, writes 320 bytes across page boundaries,
then compares the whole sector including untouched bytes. Five patterns cover
zeros, `AA`, `55`, incrementing data and a deterministic mixed pattern. Pattern
generation and comparisons are sliced into at most 64 bytes per invocation.
The copied 320-byte program request is one bounded operation. All 100 rounds
cross at least one page boundary. Failures stop the test after the current sector
comparison or immediately on a driver error. No final cleanup erase is performed.

Report `active`, `result`, `state`, `rounds_completed`, `cross_page_rounds`,
`mismatches`, `first_mismatch_address` (`0xFFFFFFFF` until a mismatch),
`program_address`, and `elapsed_ms`. State values: 0 idle, 1 start erase,
2 wait erase, 3 blank check, 4 build pattern, 5 start program, 6 wait program,
7 compare, 8 terminal. Elapsed time uses wrap-safe unsigned milliseconds.

Host verification: run `tests/host/run_flash_tests.ps1`. The protocol model checks
CS, write enable, busy polling, page-wrap avoidance, reserved-sector protection,
input copying, maximum transfer size, timeout across millisecond wrap, injected
transport faults, blank-check and post-program comparison failures, and all 100
rounds. These results validate software against a model, not physical SPI wiring,
flash identity or hardware timing. Hardware acceptance remains a separate run.
