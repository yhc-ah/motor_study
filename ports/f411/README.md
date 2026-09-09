# STM32F411CEU6 port (week 5)

This standalone target compiles the **same** `../../device/mpu6050/mpu6050.c` and frame/CRC implementation as the F407 project. No F407 project changes are needed. Hardware acquisition and five-minute endurance results are **NOT RUN**; compile success is not a board test.

## Build and flash

Run `powershell -ExecutionPolicy Bypass -File tools/build_f411.ps1` from the repository. Override `-ArmBin` for a different ARMCC 5 installation. The script stops on errors and builds both `ports/f411/build/c/f411.{axf,hex,map}` and `build/cpp/f411.{axf,hex,map}` using ARMCC 5 Cortex-M4 hardware floating point, `-O2`, split sections. Both compile the common driver as C. The C++ adapter alone uses `--cpp --no_exceptions --no_rtti`; it adds no virtual methods, allocation, or hardware constructor. `PortImu_Init` is explicit after hardware initialization.

`f411.uvprojx` additionally provides `F411_C` and `F411_CPP` uVision targets. Install the Keil STM32F4 device pack named in the project when using the IDE. Select the connected CMSIS-DAP or ST-Link probe and the STM32F4 512 KB flash algorithm for the actual F411CE device. Linker layout is explicit in `f411.sct`: flash `0x08000000..0x0807FFFF`, SRAM `0x20000000..0x2001FFFF`. The startup/vector and system sources are ST's F411xE files from the repository, not F407 startup. The vendor startup reserves 1 KB stack and 512 bytes heap; the program never allocates from that heap.

## Wiring and clocks

| Function | F411 pin | Connection |
|---|---|---|
| USART1 TX | PA9 | USB serial RX (3.3 V TTL) |
| USART1 RX | PA10 | USB serial TX; no commands currently processed |
| I2C1 SCL | PB8 | MPU6050 SCL |
| I2C1 SDA | PB9 | MPU6050 SDA |
| Supply | 3.3 V, GND | MPU6050 supply and common ground |

Use external SDA/SCL pull-ups appropriate to 400 kHz (typically 2.2–4.7 kohm for short wiring); internal pull-ups alone are insufficient. AD0 may select 0x68 or 0x69. The driver probes both. No INT wire is required. Do not connect 5 V UART signals.

HSI nominal 16 MHz feeds PLL M=16, N=192, P=2, Q=4. SYSCLK/HCLK=96 MHz, PCLK1=48 MHz, PCLK2=96 MHz, TIM2 clock=96 MHz. TIM2 PSC=95 gives nominal 1 MHz; ARR=0xffffffff gives wrap every ~71.6 minutes. No external crystal is assumed. UART is 460800 8N1; BRR=208 produces nominal 461538 baud (+0.16% before HSI tolerance). I2C is nominal 400 kHz with duty cycle 2. Actual rates depend on HSI accuracy and require hardware measurement.

## Timing and recovery

A foreground absolute deadline advances by 2000 us. When late by full periods, missed slots are counted and skipped; the loop never runs a catch-up burst. All wrapping comparisons use unsigned subtraction or signed deadline difference. `jitter_max_us` is the maximum absolute consecutive service-start period error abs(dt - 2000 us), including skipped periods; `jitter_count` counts errors >=200 us. The tested portable week5 scheduler is shared with F407. `missed_slots` separately captures whole lost periods. Initialization and offline periods also contribute to these scheduling counters.

`time_start` is captured immediately before the service call; `time_done` immediately after it, excluding bus recovery, frame encoding and queueing. `last_valid_start` changes only on a fresh complete sample. `service_max_us` includes initialization, errors and no-data calls, not just valid samples. A0 is emitted only for fresh samples. Online and valid are separate: after initialization online can be 1 before the first valid sample. No synthetic stale samples are transmitted.

The HAL memory transfers use a finite 2 ms tick timeout (tick granularity can add ~1 ms). A BUSY precheck fails immediately; a race after that check is still bounded by the vendor HAL's 25 ms BUSY timeout. Interrupts remain enabled so SysTick advances. Errors initiate at most nine 5 us low/high recovery pulses and a STOP, then peripheral reset/reinitialization. A physically held line does not trap the recovery loop. The shared driver retries initialization after 500 ms and counts successful reinitialization in `recoveries`. Faults may miss 2 ms deadlines; the scheduler records the missed slots instead of concealing them.

UART TX is an interrupt-driven 1024-byte queue. Frames are admitted whole or dropped whole, incrementing `tx_dropped`. There is no blocking UART wait. A0 at 500 Hz uses 20500 bytes/s, below the nominal 46080 bytes/s serial budget.

## Wire format

Common frame: `AA 55 | u16 LE body_length | u32 LE sequence | u8 command | payload | u16 LE CRC16-Modbus`. Body length includes sequence and command. CRC covers length through payload, excluding sync and CRC. Output begins automatically; no stream-enable command is required.

A0 is the existing 30-byte raw layout: `u32 time_start_us`, `u32 drdy_count=0`, `u32 service_duration_us`, seven `i16` values (ax, ay, az, temperature, gx, gy, gz), `u16 flags`, `u16 reserved=0`. Flags bit 0=fresh valid; bit 2=slot skipped or service duration >=2000 us; bit 1 remains 0 (no hardware DRDY timestamp). Frame sequence is shared-driver sample count. Timestamp is a host-read start, not a claimed sensor conversion timestamp. Temperature conversion is raw/340+36.53 C; accelerometer full scale is +/-2g and gyro +/-250 deg/s.

A5 is emitted about once per second, 56-byte payload of 14 little-endian u32 fields, in order:

`version(1), time_start, time_done, last_valid_start, samples, errors, recoveries, missed_slots, jitter_max_us, jitter_count, service_max_us, tx_dropped, online, valid`.

A5 sequence is the current sample count and may repeat when offline. It is a periodic snapshot, not a sample; do not include A5 in raw sample sequence-gap calculations.

A6 accompanies A5 once per second: 10 little-endian u32 values:
`version(1), service_calls, intervals, mean_period_us, max_period_error_us, p99_error_upper_us, missed_slots, violations, sum_us_low, sum_us_high`.
P99 is exact for histogram ranks below200us; overflow ranks return the observed maximum as a conservative upper bound. Timing sum is64-bit. This provides timing sample counts and means without inferring them from successful raw samples.

## Evidence and remaining bench work

On 2026-09-09, ARMCC 5 CLI compilation/link/HEX conversion passed for both adapters without compiler diagnostics. Initial comparison at identical O2 flags: C and C++ each Code=7486, RO=482, RW=44, ZI=3644 bytes; ROM=8012 and RAM=3688 bytes. This is whole-program size including vendor HAL/startup. It shows no image-size increase for this thin wrapper, not a general claim that C++ is always zero cost. Rebuild after changes and use the map files as the authoritative size evidence. CPU-cycle/runtime overhead comparison is **NOT RUN**.

Bench acceptance still required: confirm boot and WHO_AM_I; measure SCL and UART rates; capture at least five minutes of raw A0 and A5; check CRC and sample-sequence gaps; compute start-time intervals and service durations; inspect missed_slots/jitter/tx_dropped; disconnect/reconnect MPU6050 and hold SDA low to verify bounded failure and recovery; retain the capture, plots and board/wiring details. Do not report any physical pass before those measurements exist.
