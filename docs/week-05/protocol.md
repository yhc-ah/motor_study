# Week5 wire contract

All integers little endian. Envelope and week4 commands unchanged; see ../week-04/protocol.md. Encoder cycles upper bound is now 1,000,000 in dispatcher, BSP and PC tool (4,000,000 transitions fit 32 bits). New recorder uses bounded memory for 30-minute runs.

| Command | Payload | Reply |
|---|---|---|
| 30 | u8 stage0..5,u8 IMUraw0/1 | B8 i32 rc; stage0 stops IMU raw; stage>0 requires valid IMU, resets timing/profile/log counters |
| 31 | empty | B9 48u32 status |
| 32 | ASCII W5OK,u32 confirmed_free_base,u32 size=20480 | BA i32 rc; only stage0; explicit destructive prepare, completion observed in B9 |
| 34 | u32 delay_us1..10000 | BC i32 rc; one deferred long LCD-profile span; fault test only |
| 35 | empty | BD i32 rc; erase+blank-verify final scratch sector, preserves journal records |

rc0=accepted, -1=invalid, -2=not ready/busy, -3=missing required physical capability; Flash can return its documented specific negative errors. Prepare/erase ACK is acceptance, not completion. Journal states:0off,1erase-start,2erase-wait,3blank-check,4ready,5program,6verify,7error.

A0 is the existing30B MPU raw payload: start_us,DRDYcount,duration_us,7i16,flags(u16),reserved(u16). It carries fresh data only; sequence is lifetime device sample count. B0 retains ADC run/block/sample/tail semantics. B9 automatic sequence0 every~1s while stage>0. Status request replies echo request sequence.

B9 indices:

```
0 version=1              1 uptime_ms          2 stage             3 raw_enabled
4 capabilities          5 imu_valid          6 valid_age_ms      7 service_calls
8 fresh_samples         9 imu_errors        10 recoveries       11 missed_slots
12 max_period_error_us 13 p99_error_upper_us 14 intervals        15 mean_period_us
16 start_us            17 done_us           18 last_valid_done  19 max_imu_span_us
20 raw_sent            21 first_raw_sequence 22 journal_state    23 journal_records
24 journal_errors      25 journal_result    26 tx_dropped       27 tx_errors
28 tx_high_watermark   29 period_violations 30 injected_long    31 invalidated_us
32 fresh_streak_cap10  33 streak10_at_us     34 th_updates       35 lcd_updates
36 extras_errors       37 flash_missed      38 occupancy_permille 39 last_HAL_I2C_error
40 journal_base        41 journal_size      42 imu_online       43 profile_over_budget
44 imu_last_error(i32)  45..47 reserved0
```

Capabilities bits0LCD/1TH/2journal-ready. Ready bit drops during actual Flash operations, so only startup uses it as an enable check. Valid age allones means no sample yet. Lifetime counters and per-stage counters are distinguished above; reset detection uses uptime and sequence regression.

B3 payload148B: u32version=1,u32uptime_ms, then7 groups of5u32: calls,total_us_low,total_us_high,max_us,over_budget. Group order IMU,ADC,UARTrx,UARTtx,Flash,TH,LCD. Counts are cumulative since stage start; save each packet, difference adjacent snapshots for1s windows. F411 A5 layout and independent auto-stream behavior are documented in ../../ports/f411/README.md.
