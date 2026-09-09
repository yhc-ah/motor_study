# 第五周复现与操作

本说明针对工具链、接线已就绪后的五分钟启动演示。前四周按用户确认已完成，保留对应分支。本周软件测试与实物验收分开记录。

1. 使用 week5 分支。此次独立目录为 `E:\motor_prj\stm32-realtime-bsp-week5`；原工程目录与其本地修改保持不动。命令在仓库根目录执行。
2. 根据 [资源接线表](peripheral-resources.md) 接好 F407：MPU、串口、电位器、模拟编码器回接。保持动力电源、电机、驱动板未连接。
3. Keil5 打开 `MDK-ARM/stm32-realtime-bsp.uvprojx`，Build/Rebuild。使用当前实际 DAP 连接烧录；确认 USART1 460800、8N1。启动默认 ADC/PWM/发生器关闭，IMU 后台初始化。
4. Python 安装 `pyserial`（可在虚拟环境执行 `python -m pip install -r tools/week5_test/requirements.txt`）。`COMx` 必须替换为真实串口：

```powershell
python tools/week5_test/week5_test.py --port COMx --baud 460800 status
python tools/week5_test/week5_test.py --port COMx --baud 460800 record --stage 2 --seconds 62 --out results/week5-smoke
```

预期状态：version=1、imu_online=1、imu_valid=1、错误不增长；记录工具启动 ADC、10kHz PWM、100Hz 模拟编码器和 IMU 日志，结束时停止这些输出并核对尾部数据。结果在新建输出目录内；不要覆盖旧证据。首次62秒用于稳定取得完整60秒窗口，五分钟演示不替代长时验收。

5. 先完成每档约60秒负载对比，再做完整1800秒记录。当前仓库可执行 stage1/2；stage3/4/5 必须先补齐真实屏幕/温湿度适配。缺失时工具和固件都拒绝，不能用 stage2 冒充全模块通过。

```powershell
python tools/week5_test/week5_test.py --port COMx --baud 460800 record --stage 2 --seconds 1800 --out results/week5-core-30min
```

Flash 先由使用者核实一个连续20KiB、4KiB对齐、无其他数据的空闲范围。没有默认地址。以下 `0xYOUR_CONFIRMED_BASE` 仅为占位符，不能原样运行：

```powershell
python tools/week5_test/week5_test.py --port COMx --baud 460800 prepare-flash --base 0xYOUR_CONFIRMED_BASE --confirm-erase
python tools/week5_test/week5_test.py --port COMx --baud 460800 status
```

等待 journal_state=4、capabilities bit2=1，表示5个扇区全部擦除并逐字节检查为空白。此操作会擦除整个确认区域。装入真实适配器后用 `record --stage 5 --seconds 1800 --out results/week5-full-30min`；正常测试不要额外后台擦除或故障注入。

F411：执行 `powershell -ExecutionPolicy Bypass -File tools/build_f411.ps1` 得到 C/CPP 两个 HEX。使用实际 DAP 烧录 `ports/f411/build/c/f411.hex`，断电换接同一个MPU6050，按 [F411说明](../../ports/f411/README.md) 接串口和I2C。

```powershell
python tools/week5_test/week5_test.py --port COMx --baud 460800 record --platform f411 --seconds 300 --out results/week5-f411-5min
```

F411自动持续输出，记录工具不会发送F407命令或停止板上采集；退出记录后按需要断电。C++版本使用相同接线/命令复测。两版本构建成本见 [map比较](map-size-comparison.md)。

全量软件检查：`./tests/host/run_week5_tests.ps1`；F407 Keil实际构建；F411双目标脚本。原始波形、手动I2C恢复时间、Flash空闲区依据、真实TH/LCD状态仍需按 [故障矩阵](fault-test-matrix.md) 和 [验收记录](week-05-test.md) 填写。
