# Week 03 verification record

Date: 2026-09-08. Source foundation: week2 13989cb, merged into week3 at84b0fcc. User states week1/week2 experiments complete. This report only records newly observed software evidence and explicitly separates physical acceptance.

## Software validation

- Baseline week2: four C executables, four Python UART tests and UART codec self-test passed.
- New MPU tests: signed extremes, register setup, only DATA_RDY yields sample, failure invalidates data, success timestamp preserved through reinitialization, wrong identity, millisecond wrap and long uptime.
- New TX tests: bounded queue, copy/lifetime, full/drop counters, wrap, completion before reuse, timeout, failed DMA stop retains buffer, long uptime.
- Flash host model: reserved boundaries, page splitting, copied input, SPI failures, WEL failure, stuckBUSY deadlines and wrap;100 cooperative rounds with full-sector blank/readback and guard corruption detection.
- Application host model: CRC-valid41-byte sample frames, status layout, stream ACK, exclusive Flash/stream state, no booterase.
- PC tests: raw framing/noise/CRC, signedpayload/status, sequence/time anomalies, ACKtimeouts, stop/drain, independentgyro bias and sample statistics.
- Run `./tests/host/run_week3_tests.ps1` for exact current combined results. Compiler/source checks and final counts recorded in completion entry below.

## Final software evidence (2026-09-08)

- `./tests/host/run_week3_tests.ps1`: exit 0; all 9 C test executables passed (4 inherited, 5 new), all 18 Python tests passed (4 inherited, 14 new), and the UART codec self-test passed.
- Keil ARMCC 5.06 update 7 build 960: **0 errors, 0 warnings**. Code 18,324 bytes; RO-data 540 bytes; RW-data 252 bytes; ZI-data 9,028 bytes. Build elapsed 45 seconds. The IDE generated the AXF and HEX successfully; no download was performed.
- Project XML parsed successfully; all 48 referenced source paths exist and none are duplicated. Two consecutive project synchronization runs produced identical SHA-256 hashes for the project and `.ioc`. The numeric ordering defect discovered during this check was fixed.
- Independent Flash specification/quality, firmware integration and PC recorder reviews completed; reported issues were fixed and regression-tested. These reviews and host models do not replace physical measurements.
- Generated Keil session/build files are excluded from the source change. The original week2 directory's three pre-existing modifications are preserved.

## Physical acceptance: NOT RUN

| Required item | Status | Missing evidence/prerequisite |
|---|---|---|
| Board rebuild/download regression | Build tested; download not run | Connected board/debugger |
| I2C waveform and identity | NOT RUN | Actual wiring and waveform |
| 500Hz output/response budget | NOT RUN | DRDY/PC5/I2C capture and timestamps |
| Temperature/humidity60 samples | NOT IMPLEMENTED for actual hardware | Exact module/handbook required |
| Shared bus without sampling loss | NOT RUN | TH profile and measurement |
| Physical Flash100 rounds | NOT RUN | Exact chip + explicitly reserved test sector |
| Two independent60-second static records | NOT RUN | Real raw CSV, temperature and orientation |
| Three disconnect recoveries | NOT RUN | Board and controllable SDA breakpoint |
| Actual LCD display | NOT IMPLEMENTED for actual hardware | Controller/interface/driver required |
| Complete600-second joint run | NOT RUN | All above hardware modules and evidence |

Only Bluetooth serial devices were found in current enumeration; no board serial port identified. No measurements, photographs, waveforms or calibration results have been fabricated. Mock SPI RAM is not physical Flash endurance evidence. Pollingstatus counters cannot prove exact physical zero-loss. Do not createweek-03-pass from this report.
