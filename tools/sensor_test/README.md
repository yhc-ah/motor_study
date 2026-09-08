# Week 3 串口采集与独立验证

Python 3.10+；仅连接串口需要 `pyserial`。离线分析、CSV、JSON 和 SVG 生成只使用标准库。
在仓库根目录运行：

```powershell
python -m pip install -r tools/sensor_test/requirements.txt
python tools/sensor_test/sensor_test.py status --port COM5
python tools/sensor_test/sensor_test.py record --port COM5 --seconds 600 --outputdir artifacts/week3/static-600s
```

串口固定 **460800、8 数据位、无校验、1 停止位、无流控**。`--seconds` 默认 600，必须是有限正数。
每次使用新的输出目录；工具拒绝覆盖已有采集证据。只有一个程序可占用串口。
采集先停止旧流并排空，再读取初始状态、验证开始 ACK、采集、每约 5 秒读取状态。
正常结束、命令失败或 Ctrl+C 都会尝试停止、验证停止 ACK、排空尾部数据和读取最终状态。
串口断开时无法保证 MCU 已停止，此时 `summary.json` 会保留失败原因。

输出：

| 文件 | 内容 |
| --- | --- |
| `raw.bin` | 本次打开采集流程后收到的全部原始字节，包括状态、ACK、噪声、坏 CRC 和尾部碎片；不是仅保存合法帧 |
| `imu.csv` | 开始流请求至停止排空期间的 IMU 帧：MCU 序号、时间、七个有符号原始值、标志、主机接收时间、展开后的相对时间、顺序判定 |
| `th.csv` | 为未来温湿度帧预留的 CSV；当前固件没有温湿度驱动和发送路径，因此只有表头，不编造数值 |
| `status.jsonl` | 含主机相对时间和 MCU 32 字段状态的快照 |
| `summary.json` | 开始/停止确认、实际时长、有效帧数量、CRC/长度/噪声计数、序号缺口/重复/倒序/回绕、时间回绕/倒退、设备状态和错误 |

`session_result=COMPLETE` 仅表示采集流程完成且收到有效 IMU 数据；**不是硬件验收通过**。
所有报告保留 `hardware_verdict=NOT_ASSESSED`。采集计时使用主机单调时钟；MCU 样本时间用于频率与离线曲线。
驱动错误、有效性、INT 路由、示波器/逻辑分析仪测量等仍需结合原始证据判断。
约 5 秒一次的状态轮询只能观察快照；序号和 DRDY 计数器只反映固件可观测事件。
这些计数均为零缺口也不能证明物理采样、INT 边沿或 DMA 过程中绝对零丢失，仍须独立硬件测量。
`capability_flags` 表示固件构建配置：`int_route`、`th_configured`、`lcd_adapter`、`flash_range_configured`。
配置位存在不等于物理连接已经验证。型号未确认、配置位未开启或温湿度 CSV 为空均不能写成对应硬件功能通过。

## 校准与独立验证

把板子静止放置、固定朝向。分别采集校准文件与验证文件；验证文件必须来自另一次独立采集：

```powershell
python tools/sensor_test/sensor_test.py record --port COM5 --seconds 61 --outputdir artifacts/week3/calibration
python tools/sensor_test/sensor_test.py record --port COM5 --seconds 600 --outputdir artifacts/week3/validation
python tools/sensor_test/sensor_test.py analyze --calibration artifacts/week3/calibration/imu.csv --validation artifacts/week3/validation/imu.csv --outputdir artifacts/week3/analysis
```

也可以省略 `--calibration`，仅统计指定验证 CSV。
工具拒绝把同一路径同时作为校准与验证文件；不同路径仍须由操作者保证采集独立，复制文件不构成独立验证。
每轴输出均值、**样本标准差（n−1）**、最小值、最大值；只有一个有效样本时标准差为 `null`。
`valid=0`、重复、倒序和时间倒退的帧保留在采集 CSV 中，但不进入离线统计。
畸形列、非有限数和负时间会被拒绝；空数据不会得到通过结论。

从校准 CSV 开始的前 60 秒有效样本计算三个陀螺仪均值，固定减到独立验证数据上。
报告提供校准均值、验证原始均值、补偿后均值、两段均值之差以及标准差。
如果校准不足 60 秒，`first_60s_covered=false` 明确标识证据不足；工具仍输出实际样本统计供诊断。
该标志只检查被选入前 60 秒窗口的有效样本首尾跨度至少 59.9 秒；窗口外的晚到样本不能补足它。
首尾跨度覆盖并不证明期间无缺口或持续静止，需结合样本数、序号和原始采集记录判断。
静态偏置相减不改变样本标准差，不宣称其降低了随机噪声。

**加速度原始均值含重力，不做零均值补偿。** 陀螺仪校准要求实际静止，工具不能从文件名判断是否静止。
固件当前 MPU6050 配置为 ±2g / ±250°/s：加速度除以 16384 得到 g，陀螺仪除以 131 得到 °/s。
`analysis.json` 同时保留原始 LSB 与物理单位；修改固件量程后必须同步换算。
原始温度寄存器值 `temp` 保存在 IMU CSV；它不是温湿度模块的温度读数。

输出 `analysis.json`、`statistics.csv`、`axes.svg`。SVG 可直接在浏览器打开，不依赖网络或绘图库。
蓝线为六轴原始数据，橙线仅显示陀螺仪补偿；长记录按分组极值保留尖峰，完整数据仍在 CSV。

## 显式 Flash 测试

仅在固件已配置**专用保留测试扇区**、确认该区域可擦写时执行：

```powershell
python tools/sensor_test/sensor_test.py flash-test --port COM5 --flash-test --outputdir artifacts/week3/flash
```

`flash-test` 子命令和显式 `--flash-test` 必须同时出现。普通 `record` 和 `status` 不发送 Flash 写入命令。
请求固定发送 `W3OK`；范围完全由固件保留区配置决定，PC 不接受任意地址。
ACK 是有符号 32 位整数：0 表示已接受，负值表示拒绝（如未配置范围返回 −3）。
接受后最多轮询约 1300 秒，保存原始字节、状态和结果；超时或中断不代表 MCU 已停止擦写。
`COMPLETED_OBSERVED` 只表示观察到 `flash_active=0`，须查看 `flash_result`、`flash_rounds`、
`flash_mismatches`、`flash_cross_page_rounds`、`flash_elapsed_ms`、`flash_first_mismatch` 等实测字段；工具不编造期望轮数或硬件通过结果。
当前固件 Flash 测试会暂停 IMU，测试后恢复；不要把 Flash 测试与连续 IMU 采集作为同一次无间断验收。

## 协议与计数限制

帧：`AA 55 | LEN(u16 LE) | SEQ(u32 LE) | CMD(u8) | PAYLOAD | CRC16(u16 LE)`。
LEN 包含 SEQ、CMD 和 PAYLOAD，范围 5..245；CRC16/MODBUS 覆盖 LEN 至 PAYLOAD。

| CMD → 响应 | 负载 |
| --- | --- |
| `0x10 → 0x90` | 1 字节 0/1 停止/开始，ACK 必须回显同值 |
| `0x11 → 0x91` | 请求空；响应 32 个 u32，`flash_result` 按 i32 解码 |
| `0x12 → 0x92` | 请求 ASCII `W3OK`；响应 i32 |
| `0xA0` | 主动 IMU：`<III7hHH`，30 字节；时间 us、DRDY 次数、读耗时、ax/ay/az/temp/gx/gy/gz、flags、reserved |
| `0xA1` | 保留的未来温湿度协议，仅 PC 解码器已实现：`<IiiII`；时间 ms、温度 m°C、湿度千分之一 %、样本数、valid。当前固件没有温湿度驱动或此帧发送路径 |

`0xA0` flags：bit0 有效、bit1 使用 INT 时间戳、bit2 设备检测缺口。
MCU 成功样本序号在关闭流时仍继续，因此第一帧以前的序号不算丢失；只统计本采集流内部的缺口。
500 Hz 的 IMU 预计 41 字节/帧，即 20500 字节/秒，是协议预算而非实测吞吐量。

32 位时间按模运算展开；需要相邻有效样本间隔小于 2³¹ us。MCU 复位和超大时间倒退不能可靠区分，记录为异常。
解析器支持碎片、粘包、坏 CRC、非法长度后的重新同步。疑似损坏但数值合法的长 LEN 后遇到完整 CRC 合法帧时，允许提前重新同步并计数。
未转义的协议若部分 payload 恰好内嵌完整合法帧存在固有歧义，因此保留 `raw.bin`，不把解析器推断当作完整链路证明。
`noise_bytes` 是所有丢弃字节（含坏帧残留），不是严格的外部电气噪声计数。

软件验证命令（不需要硬件）：

```powershell
python -m unittest discover -s tests/host -p test_sensor_tool.py -v
```

测试使用人工构造的协议与统计数据，不代表任何开发板实测结果。
