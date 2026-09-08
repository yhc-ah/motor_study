# Week 4 PC 采集与离线分析

依赖 Python 3.10+ 和 `pyserial`（`python -m pip install pyserial`）。从仓库根目录执行；下面COM6仅为命令占位示例，必须改为实际开发板端口；本次电脑COM6是蓝牙端口，不是已确认开发板。默认 460800、8N1，无流控。脚本复用 week3 `CountedDecoder` 与 UART 公共帧编码/串口打开函数，不使用会遗漏命令等待期间数据的旧采集逻辑。

```powershell
python tools/week4_test/week4_test.py --port COM6 status
python tools/week4_test/week4_test.py --port COM6 pwm --hz 1000 --duty 500 --mode 1
python tools/week4_test/week4_test.py --port COM6 pwm --hz 10000 --duty 100 --center
python tools/week4_test/week4_test.py --port COM6 pwm --hz 20000 --mode 2
python tools/week4_test/week4_test.py --port COM6 encoder --hz 100 --cycles 1000 --direction 1 --initial 65530
python tools/week4_test/week4_test.py --port COM6 encoder-stop
python tools/week4_test/week4_test.py --port COM6 poll --count 1000 --cycles 84 --out results/poll84
python tools/week4_test/week4_test.py --port COM6 poll --count 1000 --cycles 480 --out results/poll480
python tools/week4_test/week4_test.py --port COM6 record --seconds 60 --out results/adc
python tools/week4_test/week4_test.py --port COM6 record --seconds 60 --pause-ms 250 --out results/overrun
python tools/week4_test/week4_test.py --port COM6 record --seconds 60 --joint --out results/joint
python tools/week4_test/week4_test.py analyze results/adc
python tools/week4_test/week4_test.py analyze results/adc --vref 3.298
python tools/week4_test/week4_test.py step-test --out results/step
python tests/host/test_week4_tool.py
```

`--port` 与 `--baud` 位于子命令之前。PWM mode 0 停止，1 固定占空比，2 每 10 ms 在 10/50/90% 间切换，3 每 100 ms 用 ADC 映射；duty 是千分比 0..1000。encoder hz 是 A 相频率 100/1000 Hz，cycles 1..100000，direction ±1，initial 0..65535。两种轮询必须在 ADC DMA 停止状态进行；记录真实 PC 发出/收到时间和往返延迟，不能当作 ADC 内部转换时间。

七项实验操作：

1. **PWM 频率/对齐/占空比**：分别用三档频率、`--center` 和 mode 1/2，示波器测频率、占空比、边沿；软件 status 只说明配置，不能证明引脚波形正确。
2. **编码器方向与回绕**：按接线文档连接发生器与编码器输入；initial 65530、正转跨越上边界，再 initial 5、负转跨越下边界。比较实际状态、目标步数和波形；运行后 `encoder-stop`。
3. **84/480 周期轮询对比**：各采集至少 1000 个点，比较 poll.csv 的 PC 往返时间统计，注明串口/操作系统开销，不能声称等于硬件转换延迟。
4. **60 秒 DMA 连续采集**：record 保存数据，核对丢块、错误计数及最终尾块闭合，再 analyze 查看噪声图。采样率估计只在至少 1 秒的连续有效完整块上用 IRQ 时间差计算。
5. **慢消费故障注入**：`--pause-ms 250` 在约 1 秒后暂停消费者。短记录在中点注入。预期应能观察并报告实际丢块/覆盖；必须成功注入，并同时观察覆盖计数增加与样本序号缺口，才标记 fault_detected。仅预期覆盖和缺口列入 expected_fault_errors；ADC overrun、DMA、TX、CRC、重复、乱序与最终发送计数不一致仍返回非零。原始文件仍保存，不能把故障实验写成无丢样通过。
6. **一阶滤波与阶跃**：step-test 生成合成输入（第 1000 点由 1000 变为 3000，dt=1 ms），验证 τ=5/20 ms 的 63.2% 和 10–90% 时间。真实 ADC 可用外部阶跃记录后查看曲线，合成测试通过不代表硬件阶跃验证通过。
7. **联合压力**：`--joint` 开 ADC、PWM ADC 映射和有限长度 100 Hz 编码器发生器，完成后停止三者。最长 990 秒以遵守发生器 cycles 上限；查看采集错误和状态，并用外部仪器核验波形。

输出：

- `raw.bin`：本次采集实际读到的全部串口字节，包括 CRC 错帧和 ACK。
- `adc.csv`：run_id、block_seq、sample_seq、nominal_time_us、done_us、raw、valid、flags、filter5、filter20。重复块/乱序块/其他 run 不进入统计，计入 summary；缺失样本保留序号空洞，不重新编号。
- `status.jsonl`：基线、新 run、定期、结束状态，附 PC 单调时钟时间。
- `summary.json`：异常、解码计数、尾部闭合、最终状态和相对基线的累计 TX/硬件错误。即使 START/STOP 超时也保存结果并尝试清理。`report_ok` 只表示协议和软件一致性检查，`hardware_verdict` 始终 `NOT_ASSESSED`，硬件验收须外部仪器及实验记录。
- `analysis.json` / `noise.svg`：同一批连续有效 raw 分别作 5/20 ms 一阶滤波，统计 n、均值、总体标准差、极值、峰峰值。缺口和无效点重置滤波，禁止跨缺口伪造连续响应。
- `step.json` / `step.svg`：明确标记 synthetic 的滤波器测试。

滤波采用 `y += (1-exp(-dt/tau))*(x-y)`。done_us 为 DMA IRQ 完成块时间，**不是逐点时间戳**；nominal_time_us 为 `(sample_seq+1)*1000`，只是名义时间。不得由此宣称精确物理采样率。VREF 仅在用户明确提供时用于 `code*VREF/4095` 理想电压换算，无默认 3.3 V，无固定精度或校准保证。

协议摘要：AA55 + little-endian u16 body length + u32 sequence + u8 command + payload + CRC16 Modbus，payload≤240。命令 20/21/22/23/24/25/26/27 分别对应 A8/A9/AA/AB/AC/AD/AE/AF；ACK 必须精确匹配 sequence、command 和长度，除 status/poll 外返回 i32 result=0。ADC B0 为 `<IIIIHH>`（run、block、first sample、IRQ us、count、flags）+ count 个 u16；flags bit0 有效，bit1 停止尾块。每块最多 100 点，尾块 count 可小于 100。完整 DMA 半块数与发送块数含义不同，结束时以最终 samples_completed/blocks_sent/samples_sent 核对缺失与尾块。

记录目录会覆盖同名文件，请每次用新的 `--out` 目录保存实验。所有软件测试仅为主机模拟数据；本仓库不附任何虚构硬件测量。

追加验收说明：

- record 发现基线 ADC 已在运行时拒绝采集，不发送 STOP，也不改变 PWM/发生器。只有本次尝试 START 后才尝试停止，START ACK 丢失仍执行清理。
- 无 ADC 数据不能通过。60 秒及以上记录检查名义样本覆盖时长（2 ms 边界容差）以及至少 1 秒的连续有效 IRQ 块窗口，估计速率须在 990..1010 Hz；不足窗口时 rate_verdict=NOT_ASSESSED，acquisition_passed=false。软件通过仍不代表物理硬件验收。
- encoder 默认输出 week4-encoder，可用 --out 指定新目录。命令等待 cycles/hz+3 秒内有限发生器结束，再等待至少 20 ms 读取新状态，保存 raw.bin、status.jsonl 和 summary.json。核对 generator_steps=4*cycles、编码器位移绝对值=4*cycles（1000 周期即 4000）以及 CNT 与 initial+位移相符；记录观察到的正负号，不预先假定接线方向。失败也尝试 encoder-stop。
- SVG 横轴使用真实保留的名义采样时间范围，遇到无效样本或序号缺口分段。每段抽稀时保留首尾，避免压缩时间空洞。

三点电压与采样时间的六次采集：先稳定在约10%量程，万用表测中点和参考电压，分别用 `poll --count 1000 --cycles 84 --out results/adc10-84` 与 `--cycles 480 --out results/adc10-480`。重复约50%位置（adc50-84、adc50-480）和约90%位置（adc90-84、adc90-480）。每次输出目录另外记录仪器型号、实测中点/VREF、电位器位置、采样周期和接线照片。比较原始均值/标准差及换算电压偏差；PC往返时间只描述整条命令链，不能代替ADC转换时间。

`analyze` 默认每个连续有效片段排除前200ms初始化段，对raw及两条滤波结果使用同一统计窗口；可以用 `--settle-ms 0` 明确保留全部数据。`acquisition_passed` 必须同时满足时长、速率、完整性及错误检查；任何故障注入试验均为false。时长覆盖检查适用于所有请求时长，2ms只是采集边界容差；短记录缺少足够速率窗口时仍为未验收。
