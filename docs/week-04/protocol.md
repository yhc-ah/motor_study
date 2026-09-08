# Week4 protocol v1

Little endian. Existing wire envelope: AA55, u16 body length, u32 sequence, u8 command, payload, CRC16 Modbus. Body length=5+payload; maximum payload240, wire251. Invalid length/CRC is handled by the inherited parser without executing a command. Requests use distinct sequence numbers; responses echo them. No physical result is encoded as an automatic hardware PASS.

| Request | Payload | Response |
|---|---|---|
| 0x20 ADC run | u8 0 stop /1 start | 0xA8 i32 rc |
| 0x21 status | empty | 0xA9, 32 u32 below |
| 0x22 PWM | u32 Hz, u16 duty permille, u8 center, u8 mode | 0xAA i32 rc |
| 0x23 encoder finite run | u32 A Hz, u32 cycles, i32 direction, u32 initial CNT | 0xAB i32 rc |
| 0x24 generator stop | empty | 0xAC i32 rc |
| 0x25 consumer pause | u32 ms | 0xAD i32 rc |
| 0x26 ADC sampling time | u32 cycles 84/480, idle only | 0xAE i32 rc |
| 0x27 ADC poll | empty, idle only | 0xAF i32 rc +u32 raw |

rc=0 success, -1 invalid argument, -2 busy/incompatible state, -3 hardware fault. Malformed status/poll requests return the corresponding response command with only i32 error; strict clients reject that short response rather than interpret it as data. Unknown commands retain the old dispatcher error response. PWM Hz1k/10k/20k,duty0..1000,center0/1,mode0stop1fixed2cycle3ADCmapping. EncoderHz100/1000,cycles1..100000,direction+/-1,initial0..65535. Pause1..1000ms requires active ADC. Start while running is rejected; stop is idempotent and queues remaining valid complete/partial blocks before ACK. Mode3 uses latest valid filtered ADC sample; without a valid sample it holds its initial duty.

ADC data command0xB0: `<IIIIHH>` run_id, block_seq, first_sample_seq, block_done_us, count, flags, then count u16 raw. Header20bytes; count1..100; flags bit0valid, bit1terminal partial. Outer frame sequence=block_seq. New start increments run_id and resets block/sample sequence to0. Full blocks have100samples; terminal partial1..99. Missing blocks leave sequence gaps. block_done_us is an ISR completion timestamp (terminal uses stop timestamp), not individual conversion timing. Nominal sample time=(sample_seq+1)*1000us; TIM2 timestamp wraps at2^32us.

Status indices:

| Index | Field | Meaning |
|---:|---|---|
|0|version|1|
|1|uptime_ms|HAL uptime, u32 wrap|
|2|run_id|increments successful ADC start|
|3|running|ADC acquisition state|
|4|samples_completed|100*full completed blocks + frozen terminal tail|
|5|blocks_completed|ISR-counted full half-buffers|
|6|blocks_sent|blocks successfully enqueued, including terminal partial|
|7|samples_sent|points successfully enqueued|
|8|overwritten_blocks|unavailable/invalidated full blocks|
|9|copy_races|copy failed post-validation; also counted in field8|
|10|adc_overruns|ADC OVR callbacks|
|11|dma_errors|DMA errors + unexpected HT/TC order|
|12|tx_dropped|cumulative UART queue drops|
|13|tx_errors|cumulative UART DMA errors|
|14|max_consume_us|max validated completion-to-copy interval while running|
|15|ht_count|completed first halves|
|16|tc_count|completed second halves|
|17|tail_samples|frozen terminal1..99 or0|
|18|encoder_cnt|raw16-bit counter|
|19|encoder_position_low|i32 displacement since last encoder start|
|20|encoder_speed|i32 count/s from actual time difference|
|21|generator_steps|software-emitted state transitions|
|22|generator_target|4*requested cycles|
|23|generator_active|0 only after final foreground count update|
|24|pwm_hz|last configured PWM frequency|
|25|pwm_arr|actual ARR|
|26|pwm_ccr|commanded preload CCR, may await next update|
|27|pwm_mode|0stop,1fixed,2cycle,3mapping|
|28|filter5_milli|latest filter code*1000, 0 if not initialized|
|29|filter20_milli|latest filter code*1000|
|30|pause_events|accepted consumer pause commands in this run|
|31|hardware_errors|cumulative since boot: init/timeout/encoder gap failures|

ADC run fields reset on successful start; UART/hardware errors are cumulative and must be compared with the baseline. PWM/encoder state is independent of ADC run. samples_completed is an observed DMA-event accounting quantity, not a promise of exact conversion count after a hardware overrun or prolonged IRQ masking. Fault reports must preserve this distinction.
