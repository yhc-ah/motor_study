# Week 5 implementation plan

Goal: integrate the F407 experiments, reuse the MPU6050 device on F411, and provide reproducible timing/fault/long-run evidence tools.

Architecture: retain tested week4 ADC/PWM/encoder ownership; a week5 foreground supervisor schedules IMU and bounded low-priority steps. Portable scheduling/statistics and journal logic have host tests. F411 has an independent target referencing the same device source.

- [x] Preserve original checkout; create week5 worktree and fast-forward week4 baseline b548b0d.
- [x] Baseline regression and build size.
- [x] Portable absolute scheduler and timing statistics, wrap/overload tests.
- [x] F407 supervisor, UART time budget, explicit capabilities, timestamp semantics.
- [x] Guarded asynchronous Flash journal, blank verification and readback.
- [x] F411 C/C++ targets, actual build and same-source verification.
- [x] Streaming PC recorder, fault tools, long encoder run bounds and tests.
- [x] Review, clean builds, tests, documentation and prepare verified week5 commit; remote SHA verification recorded at handoff.

Hardware dependencies: agent requested actual TH/LCD model and driver location, F411 board/pins, and verified free Flash range. Unavailable hardware evidence must remain NOT_RUN; never substitute synthetic data or create a pass tag.
