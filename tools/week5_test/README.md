# Week 5 streaming capture

F411 continuous-stream capture first synchronizes to a valid A5 status within
3 seconds. Pre-capture bytes are preserved in `sync.bin`, with their decoder
counters separately reported. Timed acceptance begins at a complete frame
boundary with fresh counters. At the end it waits at most1.5 seconds for a new
status (included in elapsed duration), then finishes the current frame within
100ms. No startup noise is silently removed from the timed evidence.

Python 3.10+ and `pip install -r tools/week5_test/requirements.txt`.
Run from the repository root. Close other programs using the port.
`record --out` must name a fresh directory: existing evidence is never overwritten.

```powershell
python tools/week5_test/week5_test.py --port COM7 record --out evidence/week5-stage2
python tools/week5_test/week5_test.py --port COM7 record --stage 1 --seconds 1800 --out evidence/week5-stage1
python tools/week5_test/week5_test.py --port COM7 status
```

Default baud is 460800 and default capture is stage 2 for 1800 seconds.
Every F407 stage starts ADC, 10 kHz PWM and a finite 100 Hz encoder stimulus
with `ceil((seconds+2)*100)` cycles. Each quadrature cycle produces four
encoder steps: 100 Hz therefore means 400 steps/s. Final counts must match
400 times capture seconds within 1% plus 40 boundary steps. Stage 1 disables IMU raw transmission;
stage 2 enables it; stage 3 adds actual LCD, stage 4 actual TH, stage 5 prepared
Flash. Firmware capabilities must confirm installed adapters; missing hardware
fails clearly. This tool cannot substitute simulated adapters for physical ones.
Baseline stage, ADC, PWM and encoder generator must be idle. The IMU must
become valid within three seconds. Times are bounded to 3600 seconds so the
finite encoder request remains within one million cycles.

## Artifacts and interpretation

- `raw.bin`: exact bytes received, including acknowledgments, periodic frames
  and malformed traffic. It is an unmodified wire capture, not a sample array.
- `imu.csv`: every accepted A0 sample, with raw integer axes, temperature,
  device start timestamp, data-ready counter, service time and validity.
- `adc.csv`: every accepted B0 ADC sample, including partial stop tails.
  `block_done_us` is the block IRQ timestamp, not each conversion's time.
- `stats.jsonl`: every B3 profile frame decoded into seven groups, streamed as
  its own event, plus approximately 1 Hz host timestamped counters and full F407
  B9/W4 status snapshots or F411 A5 snapshots.
- `result.json`: baselines, final actual counts, error deltas, wire and sequence
  errors, coverage, software integrity and separate `hardware_verdict`.

CSV and binary output stream directly to files; retained state is constant in
capture duration. No per-sample sets, row lists or full-run plots are retained.
The read batch is capped at 64 KiB. The long-run test exercises 900,000 IMU reads and
1,800,000 ADC samples, corresponding to 1800 seconds at nominal rates.
Older reordered/duplicate frames count as out-of-order; arbitrary historical
retransmissions are not stored to classify them further.

The result is **software integrity**, never proof of physical timing,
measurement precision, PWM shape or encoder wiring. Hardware verdict remains
`NOT_ASSESSED`. Check the raw physical interfaces with external equipment.
`imu_host_reads` counts received A0 frames; `imu_fresh_samples` is the driver's
cumulative new-data count and is not inferred from host reads. Device service
calls may exceed both. F407 final `imu_sent` must match received A0 count.

Checks include no missed IMU periods, maximum period error <200 us, ADC
maximum consume latency <80000 us, zero stream/wire/error deltas, final ADC
completed/sent/received counts, stopped generator and encoder position matching
actual generated steps. Raw stages require device timestamp span >=98% of the full capture minus
0.1 seconds and at least 490 host raw samples/s (10 boundary samples allowed).
F407 service calls must match 500 Hz within 1% (at least 10 boundary calls
allowed). ADC full-block timestamps and sample indices must measure
990..1010 Hz, and total samples must cover the capture at that rate with a
200-sample boundary allowance. Capture duration excludes cleanup. For captures
>=60 seconds, require >=98% approximately 1 Hz rows (one boundary row
allowance), and no observed status interval >2.5 seconds. Stage 1 intentionally
has no A0 raw coverage requirement and reports `NOT_ASSESSED_RAW_DISABLED`. F411 periodic status must be newly received
for each counted row. IMU timestamp span handles wrap incrementally.

Cleanup marks resources owned *before* sending start commands, so missing or
malformed ACKs still trigger cleanup. It stops IMU raw logging, stops ADC,
stops generator and turns PWM off, continues after individual cleanup errors,
drains queued data for 300 ms, then requests final status. A stop error is a
failure; no successful hardware shutdown is claimed when communication fails.
An ongoing journal operation gets up to two seconds to reach verified READY;
stage 5 requires final READY and approximately one verified record per ten
seconds, zero Flash missed periods, and no journal errors. LCD and TH update
counts must cover their requested 2 Hz and 1 Hz stage rates. The stage-0 IMU service continues, so cumulative driver reads may advance after
raw logging has stopped. Disk/port disconnects may prevent complete artifacts.

## Explicit destructive Flash preparation

Choose the scratch range from the board's verified Flash allocation. The
following erases 20480 bytes at the provided base; never use a range containing
needed data. This is not run implicitly by `record`.

```powershell
python tools/week5_test/week5_test.py --port COM7 prepare-flash --base 0x100000 --confirm-erase
python tools/week5_test/week5_test.py --port COM7 status
python tools/week5_test/week5_test.py --port COM7 record --stage 5 --seconds 1800 --out evidence/week5-full
```

Wait until capabilities bit 2 is set. ACK means the asynchronous preparation
was accepted, not that erasure finished. Base must meet firmware alignment and
range checks. Stage 5 also requires LCD bit 0 and TH bit 1.

## F411 receive-only comparison

```powershell
python tools/week5_test/week5_test.py --port COM8 record --platform f411 --seconds 300 --out evidence/f411
```

This sends **no commands**, including cleanup. At the capture deadline, any
partial frame is completed bytewise for up to 100 ms, then reception stops at
the boundary. Missing bytes remain visible as an integrity failure. Run firmware streaming A0 and
periodic A5 beforehand. At least 300 seconds is required. No ADC/PWM/Flash
commands or fault injection are attempted. F411 cannot reconcile final
transmission totals because its A5 protocol does not include a sent counter;
sequence gaps, fresh periodic status, raw coverage and error deltas remain
available. Timing counters are cumulative since device initialization.

## Fault evidence and manual markers

```powershell
python tools/week5_test/week5_test.py --port COM7 record --seconds 90 --fault pause --out evidence/pause250
python tools/week5_test/week5_test.py --port COM7 record --seconds 90 --fault load --out evidence/load5000
python tools/week5_test/week5_test.py --port COM7 record --stage 5 --seconds 90 --fault erase --out evidence/erase
python tools/week5_test/week5_test.py marker --out evidence/load5000 --note "I2C disconnected physically"
python tools/week5_test/week5_test.py marker --out evidence/load5000 --note "I2C restored physically"
```

Faults inject once about two seconds after start: ADC consume pause 250 ms,
a 5000 us task, or background erase of the already prepared scratch area.
Markers append wall-clock nanoseconds to `markers.jsonl`; fault records also
carry wall-clock nanoseconds and elapsed time. Markers do not operate GPIO or
claim recovery occurred. Fault captures always report `FAULT_CAPTURE` or `FAULT_NOT_OBSERVED`, never
normal `PASS`. The result retains fault label, command acknowledgment, before
and immediate status, final evidence and expected/unexpected failure lists.
Pause observation requires both increased overwritten blocks and stream gaps;
load observation requires the injection counter and jitter violations to rise;
erase observation requires a busy state followed by verified READY without a
journal error. All errors remain recorded. A fault capture exits nonzero.
For UART 10,000-frame checks and 60-second flood use the existing
`tools/uart_test/uart_test.py` CLI and README separately; this recorder does not
silently start a concurrent writer on the same serial port.

## Protocol

All words are little endian. Commands use the standard framed CRC transport.
ACK sequence must match the request and payload length must be exact.
30: two bytes stage 0..5 and raw flag; B8 signed i32 result.
31: empty; B9 exactly 48 u32 fields.
32: `W5OK` + u32 base + u32 size 20480; BA signed i32.
34: u32 task delay 1..10000 us; BC signed i32.
35: empty scratch erase; BD signed i32.
W4 commands 20/21/22/23/24/25 and A8/A9/AA/AB/AC/AD remain unchanged.
B3 is exactly 37 u32 words: version, uptime then seven profiles each containing
calls, total low/high, maximum and over-budget count.

B9 names and ordering are defined by `FIELDS` in `week5_test.py`, directly
matching `app/app_week5.c::fields`. Word 44 is signed `imu_last_error`; reserved words 45..47 are preserved.
`jitter_p99_upper_us` is the firmware histogram upper bound, not a host-derived
exact percentile. `journal_result` is transported as a u32 bit pattern.
A5 is exactly 14 u32: version, start, done, last-valid-start, fresh samples,
errors, recoveries, missed, max period error, jitter violations, max service,
TX drops, online, valid. A6 is 10 u32: version, service calls, intervals,
mean period, maximum period error, P99 error upper bound, missed slots,
violations, interval sum low/high. Every A6 is validated and streamed to
`stats.jsonl` as an `f411_timing` event; the latest is also saved in the result. A0 is exactly `<III7hI`; validity 1 or 5 is accepted, with F411
fault bit 2 (value 4) counted separately as `imu_fault_samples`; B0 has the W4 header and
1..100 12-bit ADC codes. Malformed known telemetry and unsupported status/profile versions count
against integrity. Timestamp regressions of >=2^31 us are rejected and counted,
so corrupt timestamps cannot inflate duration coverage.

Run host tests:

```powershell
python -m unittest discover -s tests/host -p test_week5_tool.py
```
