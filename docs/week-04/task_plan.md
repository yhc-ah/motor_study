# Week 04 implementation plan

Goal: implement the user's approved seven experiments on week4, preserve weeks 1–3, verify software and push week4 without fabricating physical acceptance.

Architecture: foreground command/processing tasks, hardware PWM/encoder/ADC trigger, bounded DMA half-buffer handover and owned UART frames. TIM2 remains the timebase. Week4 defaults to an idle laboratory mode; week3 sensor polling is disabled in this build profile and remains selectable at compile time. No motor connection.

Tech stack: STM32F407ZG, HAL, ARMCC5, GCC host tests, Python standard library plus pyserial.

- [x] Inspect remote/local branches, preserve original local edits, create sibling week4 worktree and fast-forward week3; baseline tests.
- [x] Pure C signal math: PWM edge/center parameters and quantization, 16-bit signed encoder delta/ambiguity, continuous low-pass state. Write failing host assertions, implement, rerun.
- [x] Board BSP: TIM4 PB6 PWM, TIM8 PC6/PC7 encoder, TIM6 PG2/PG3 quadrature generator, ADC1 PC0 with TIM3 TRGO 1kHz and DMA2 Stream0, PC2 callback probe, PA6 TIM3_CH1 trigger reference. These are explicit external wiring defaults, not a claim of onboard routing.
- [x] ADC ownership: ISR completion sequence/timestamp; foreground validates DMA half, generation and deadline before/after copy; count overwritten blocks. Stop freezes DMA, accounts complete blocks and partial tail. Inject 250ms consumer pause without masking interrupts.
- [x] Foreground app and protocol: idle defaults, ADC start/stop/status, PWM static/cycle/stop and center alignment, encoder finite cycles/direction/reset, ADC polling/configuration, controlled fault injection, joint mode. Reuse owned TX queue.
- [x] Python client: status, PWM, encoder, poll, record, joint run, fault capture; matching ACKs, raw evidence, exact ADC/block/end counts, CSV and independent filter/step analysis SVG. Unit tests with fragmented/corrupt input and command failure/stop cleanup.
- [x] Integrate Keil sources and CubeMX profile; test all old/new host suites and ARMCC build; independent spec and quality review, fix findings.
- [x] Chinese wiring/operations/protocol/report; physical results remain NOT RUN until observed. Prepare source delivery; push/remote SHA result is reported after delivery.

Protocol v1 (little endian; existing AA55/length/u32 seq/u8 cmd/payload/CRC16 envelope, payload <=240):

- 0x20 ADC run: payload u8 0 stop/1 start. Reply 0xA8 signed i32 result. New start resets acquisition counters/filter and returns BUSY if running. Stop is idempotent, sends all remaining valid block/tail frames before ACK.
- 0x21 status: empty, reply 0xA9, 32 u32 fields defined in protocol.md.
- 0x22 PWM: payload `<I H B B>` Hz (1000/10000/20000), duty permille 0..1000, center 0/1, mode 0 stop/1 fixed/2 automatic 10/50/90 every10ms/3 ADC mapping every100ms. Reply 0xAA i32.
- 0x23 encoder: payload `<I I i I>` A Hz (100/1000), cycles (1..100000), direction (-1/+1), initial CNT (0..65535). Starts finite test only when idle. Reply 0xAB i32. 0x24 empty stops generator, reply 0xAC i32.
- 0x25 fault: u32 consumer pause milliseconds 1..1000, requires running; reply 0xAD i32.
- 0x26 ADC sampling cycles: u32 84 or480, idle only; reply 0xAE i32.
- 0x27 ADC single poll: empty, idle only; reply 0xAF `<i I>` result/raw (bounded polling, explicitly distinct from DMA record).
- 0xB0 ADC block: `<IIIIHH>` run_id, block_seq (0-based), first_sample_seq (0-based), block_done_us, count1..100, flags (bit0 valid, bit1 terminal partial), followed count u16 raw. Outer seq=block_seq. Nominal sample period1000us. Fault gaps advance logical sample index. No reused DMA memory reaches UART.
- status fields in order: version,uptime_ms,run_id,running,samples_completed,blocks_completed,blocks_sent,samples_sent,overwritten_blocks,copy_races,adc_overruns,dma_errors,tx_dropped,tx_errors,max_consume_us,ht_count,tc_count,tail_samples,encoder_cnt,encoder_position_low(i32),encoder_speed(i32),generator_steps,generator_target,generator_active,pwm_hz,pwm_arr,pwm_ccr,pwm_mode,filter5_milli,filter20_milli,pause_events,hardware_errors. ADC counters are current run; UART and hardware_errors are cumulative since boot; encoder/generator/PWM state is independent. Samples_completed includes lost full blocks plus terminal tail, not proof of exact conversion count if DMA IRQ missed. ADC error stops acquisition and invalidates partial data.

Host assertion examples: edge84MHz/10kHz -> PSC83 ARR99 CCR50; center -> ARR50 CCR25; wrap65530->4=10; ambiguous32768 rejected; lowpass step63.2% within one sample. Integration should reject malformed commands without hardware mutation and preserve raw bytes on recording failure.

Physical prerequisites: board/DAP currently not enumerated (Bluetooth COM3–6 only), actual header/load conflicts require user check. User reports weeks1–3 complete; no reclassification of those results.
