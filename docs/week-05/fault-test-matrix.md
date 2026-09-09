# 故障与恢复操作

所有故障输出使用新目录，不能混入正常30分钟记录。下表“主机验证”不代表已插拔实物。

| 实验 | 操作 | 判据 | 当前证据 |
|---|---|---|---|
| 固定种子1万例 | 下方 UART random；主机另运行 test_week5_uart_matrix | 错帧零执行、错误分类计数、分片/粘连/重同步 | C解析器1万例与Python坏帧测试；实物待测 |
| 连续60秒洪泛 | 下方 week5_uart_flood | 保存实际发送B/s、MCU溢出/丢弃、坏帧零执行、停止后有效PING恢复 | 工具与主机测试；实物待测 |
| ADC延迟250ms | record --fault pause | 覆盖/缺口被检测，DMA/中断持续工作，解除后继续获取有效块 | 继承DMA所有权测试，故障记录工具；实物待测 |
| 长操作5ms | record --fault load | inject_count增加、IMU最大周期误差/漏槽增加，能追溯LCDprofile超预算 | 调度跳槽与profile测试；实物待测 |
| Flash后台擦除 | 已明确准备空闲区域后 record --fault erase | 只擦末尾scratch扇区，原日志不变，未死等BUSY | 真协议模型隔离/读回测试；实物待测 |
| I2C断线3次 | 保持供电与地不动，只断开预留SDA连接，再恢复 | valid失效、有限错误；ADC/PWM持续；恢复后2s内连续10个新样本 | 设备错误/重新配置主机测试；真实插拔待测 |

```powershell
python tools/uart_test/uart_test.py --port COMx --baud 460800 --mode random --cases 10000 --seed 20260909
python tools/week5_uart_flood.py --port COMx --baud 460800 --seconds 60 --out results/week5-uart-flood
python tools/week5_test/week5_test.py --port COMx --baud 460800 record --stage 2 --seconds 10 --fault pause --out results/week5-adc-fault
python tools/week5_test/week5_test.py --port COMx --baud 460800 record --stage 2 --seconds 10 --fault load --out results/week5-long-fault
python tools/week5_test/week5_test.py --port COMx --baud 460800 record --stage 2 --seconds 10 --fault erase --out results/week5-erase-fault
```

UART random与flood各自占用串口，不与另一进程同时打开；洪泛测试可保留板上已启动的采集但由同一进程接收输出，最终只给故障结论。flood连续发送坏CRC的PING帧，前后PING执行计数必须不增加，然后发一个独立正确PING验证恢复。其吞吐是实际测得速率，不宣称PC/设备的理论极限。

I2C断线使用正常记录工具采集到独立fault目录，可在另一个终端仅写标记（不占串口）：

```powershell
python tools/week5_test/week5_test.py marker --out results/week5-i2c-fault --note "SDA disconnected, trial 1"
python tools/week5_test/week5_test.py marker --out results/week5-i2c-fault --note "SDA reconnected, trial 1"
```

手工标记存在操作延迟；严格2秒恢复判据需逻辑分析仪记录真实重连边沿，并与A0新数据及B9 invalidated_us/streak10_at_us对齐。B9只有每秒快照，单靠其时间间隔不能给出毫秒精度结论。每次记录：断线时刻、valid失效时刻、重连时刻、连续第10个新数据时刻、最大服务耗时、ADC错误与编码器计数。三次均通过才勾选。

LCD/TH真实适配器接入后，任何1000μs预算超限都应检查并拆分操作再重跑正常档位；不能通过把预算数字调大来掩盖2ms IMU失约。
