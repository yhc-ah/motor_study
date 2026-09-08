# Week 03 implementation plan

Goal: extend the accepted week2 firmware on week3 with sensor acquisition, bounded bus recovery, safe NOR tests, binary logging and reproducible PC validation.

Architecture: cooperative foreground tasks own buses and queues. IRQs only publish completion or data-ready events. Device logic uses testable transport callbacks. All results distinguish host validation from physical acceptance.

The user authorized implementing the previously presented experiments and pushing week3. Original week2 worktree has local Keil session changes and is preserved. Week3 is developed in E:/motor_prj/stm32-realtime-bsp-week3.

- [x] Merge week2 into week3; preserve week1 experiment text; run baseline host suite.
- [x] Test-first NOR driver: ID, WEL/BUSY deadlines, restricted sector erasure, page splitting, comparison, 100-round foreground test.
- [x] Test-first MPU data parsing, initialization, optional DRDY acquisition and recovery.
- [x] Board I2C1 PB8/PB9, SPI1 PB3/PB4/PB5 + PG6, microsecond timer, optional PC4 INT, UART TX DMA queue; preserve CubeMX user hooks. Actual wiring and GUI regeneration remain unverified.
- [x] Integrate nominal 500 Hz foreground acquisition, stream/status/control frames and nonblocking command responses.
- [x] PC recorder, sequence/CRC checks, CSV statistics and independent bias validation; host tests.
- [x] Keil compile, existing/new host tests, independent specification and code review; fix findings.
- [x] Document wiring, configuration, capture/recovery procedure and actual verification evidence.
- [ ] Commit and push HEAD to week3, verify remote SHA (delivery result reported after push).
- [ ] Temperature/humidity driver for verified actual device and concrete LCD driver: blocked on missing hardware profile.
- [ ] Physical acceptance, including 600-second joint run: not run; requires connected board and completed module support.

Physical prerequisites still to resolve from user/hardware: actual temperature/humidity model, IMU INT route, display controller/driver, reserved Flash test sector, connected board. Do not guess those values or label hardware experiments passed. NOR mutation requires an explicit configured reserved range and start command; never erase automatically at boot.
