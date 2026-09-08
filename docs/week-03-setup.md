# 第三周固件与实验操作

本分支已合入 week2，提供 MPU6050、SPI NOR、UART TX DMA、PC 记录和统计工具。**第三周尚未全部完成硬件验收**：温湿度实物型号、LCD 控制器/接线、MPU INT 路由、Flash 可擦写区尚未确认；当前电脑未枚举到开发板串口。温湿度驱动和具体 LCD 驱动尚未实现，不能把空 CSV 或显示扩展接口算作该实验完成。

## 工程和构建

开发目录 `E:/motor_prj/stm32-realtime-bsp-week3`，分支 week3。原目录 `E:/motor_prj/stm32-realtime-bsp` 仍保留 week2 与其本地 Keil 修改。之后可继续在 week3 独立目录开发，也可保存原目录改动并退出此 worktree 后自行切换。

1. 打开 `MDK-ARM/stm32-realtime-bsp.uvprojx`，选择 `stm32-realtime-bsp`，ARMCC5.06u7。
2. Rebuild，检查错误和警告；烧录前确认下表接线。
3. 默认串口改为 **460800 8N1**。第二周 PC 工具使用 `--baud 460800`。
4. 新版 Python 工具命令见 [采集指南](../tools/sensor_test/README.md)。Python 离线分析无第三方依赖；连接串口需 pyserial。

```powershell
./tests/host/run_week3_tests.ps1
python -m pip install -r tools/sensor_test/requirements.txt
python tools/sensor_test/sensor_test.py status --port COM实际端口
python tools/sensor_test/sensor_test.py record --port COM实际端口 --seconds 600 --outputdir artifacts/week3/run01
```

`tools/configure_week3_project.py` 同步本分支源文件分组、默认 .ioc 引脚及频率。它不等同于执行 CubeMX，不能宣称已经验证 GUI 重新生成。手动修改 `week3_config.h` 的波特率/总线速度时也必须同步 .ioc，再重新编译。CubeMX 生成时保留用户区；RX 和 USART 的 BSP 包装器在 IRQ 用户区内，DMA7 处理函数在生成区域，不会在保留区复制第二个定义。

## 接线和实际配置

配置以野火教程连接为起点，仍须对照实际 V2 原理图、跳帽与外设占用：

| 功能 | 当前固件配置 |
|---|---|
| USART1 TX/RX | PA9 / PA10，经 J80 / J81；460800 8N1 |
| RX DMA | DMA2 Stream2 Channel4，Circular；保持 week2 |
| TX DMA | DMA2 Stream7 Channel4，Normal；16 个稳定帧缓冲，每帧最大251B |
| I2C1 SCL/SDA | PB8 / PB9，AF4 开漏，400kHz，外部上拉到正确电压 |
| SPI1 SCK/MISO/MOSI | PB3 / PB4 / PB5，AF5；模式0，84MHz/128=656.25kHz |
| Flash CS | PG6，初始高电平 |
| I2C 测量脚 | PC5，高电平包围寄存器事务；确认没有其他外设占用 |
| UART 回调测量脚 | PA1，沿用 week2；按键也用该探针，测量时区分事件 |
| 时间戳 | TIM2，1MHz，32位；约71.6分钟回绕，按无符号差值处理 |
| 心跳 | 蓝灯 PF8，每500ms翻转；LED_SET 蓝灯状态会被心跳刷新 |
| 可选 MPU INT | **默认未启用**；确认 PC4 空闲并把 INT 接到 PC4 后，修改 `WEEK3_MPU_PC4_INT=1` |

PC4 是本分支提供的可选外接方案，不是已经核实的板载 INT 原理图连接。默认代码通过 INT_STATUS 检查新数据，不需要猜测 INT 路由。保留 SWD（PA13/PA14），不启用占用 PB3/PB4 的 JTAG/Trace。当前为低压开发板实验，不连接电机动力级。

参考：[野火 MPU6050 原始数据](https://doc.embedfire.com/mcu/stm32/f407batianhu/hal/zh/latest/doc/chapter44/chapter44.html)、[野火 SPI Flash](https://doc.embedfire.com/mcu/stm32/f407batianhu/hal/zh/latest/doc/chapter24/chapter24.html)、[MPU 寄存器手册](https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Register-Map1.pdf)。

## 实验0：基线

Rebuild/烧录后用 week2 工具验证 PING/GET_STATS/LED_SET（460800）。坏CRC不执行命令。蓝灯心跳继续，按键行为保留。波形回归时不要同时按键影响PA1测量。记录编译器、提交号、COM口、时钟树和接线照片。代码的PLLQ沿用week2；此分支不使用USB外设的48MHz时钟。

## 实验1：MPU6050

启动时只探测0x68/0x69，WHO_AM_I必须0x68。复位等待100ms、唤醒等待100ms均由主循环状态机处理。配置DLPF=3、DIV=1、±2g、±250度/秒，回读所有设置；关闭FIFO和内部I2C主机。一次从0x3A连续读15字节（状态+七组数据），只有DATA_RDY置位且整个读取成功才增加样本计数、更新最后成功时间与有效标记。

1. 先查状态，确认IMU有效、没有持续错误。
2. 静止、翻转、绕三轴正反转，核对原始符号。AX/AY/AZ除16384得到g，GX/GY/GZ除131得到度/秒；芯片温度raw/340+36.53不是外部温湿度数据。
3. 逻辑分析仪观察PB8/PB9/PC5；配置的周期为2ms，但实际频率与延迟必须测量。
4. 默认1ms轮询一次状态；可选EXTI开启后事件可提前触发读取，IRQ只保存计数和时间。传感器配置依然500Hz。
5. `deadline_misses` 是总响应>=2ms或相邻成功读取开始间隔>3ms的事件数量，**不是精确丢失样本数**。状态轮询不能计出被覆盖的全部样本。`drdy_missed=0` 在未接INT模式下不构成零漏样证据；软件成功样本序号主要发现上传丢帧。
6. INT模式也受IRQ屏蔽/事件合并限制，需用独立波形或进一步FIFO采集证明严格无漏样；不能仅靠软件计数承诺。

上传默认关闭，避免污染第二周工具。`record` 自动开启并在结束时关闭。IMU帧41字节×500=20500B/s，460800下理论占载荷44.5%，仍需验证总开销。

## 实验2与LCD：明确未完成部分

原计划“AHT32”没有匹配的已确认实物手册，仓库也没有对应驱动。请提供实际模块型号、手册/购买资料；依据准确命令、忙位、CRC、转换时间实现非阻塞状态机后，才可接入共享总线。当前TH状态为未配置、无有效数据，PC的0xA1解析是预留能力，固件不会发送温湿度帧。

LCD没有控制器型号与初始化例程，当前仅提供 `APP_DisplayAvailable` / `APP_DisplayStatus` 弱扩展接口，默认不访问屏幕。接口不代表LCD实验已完成。需要实际屏幕驱动、数据视图与分段刷新预算，随后验证2–5Hz显示不干扰采样。

## 实验3：Flash

仅支持当前白名单W25Q128对应JEDEC ID；其他ID不会进行擦写。开机只读ID，**不自动擦除、不猜测最后扇区空闲**。修改 `app/week3_config.h` 的 `WEEK3_FLASH_TEST_BASE` 和 `WEEK3_FLASH_TEST_SIZE` 前，确认完整保留区不包含字库/图片/参数，基址与长度都按4096B对齐。默认size=0，任何测试启动被拒绝。

确认保留区并重新编译后，先关闭采样，再执行：

```powershell
python tools/sensor_test/sensor_test.py flash-test --port COM实际端口 --flash-test --outputdir artifacts/week3/flash01
```

每轮擦除一个保留扇区、全扇区检查FF、生成320B模式、跨页编程、全扇区回读含未写保护区，共100轮，全部跨页。每次服务最多68个SPI字节或64字节比较；每个程序分段重新WREN并确认WEL。NOR错误会锁住ready，需要重新初始化后重新保留区；当前应用可重启后重新测试，不能把未完成轮次计为成功。无整片擦除命令。

Flash测试期间暂停IMU采样并标无效，拒绝开启采样流；完成后恢复。联合实时擦写属于后续扩展，不属于本次无漏样验收。

## 实验4：原始数据与统计

采集两次独立静止记录，每次至少60秒。PC保留全部收到的原始字节、IMU原始CSV和状态；重复/倒序/无效记录保留但不加入正常统计。离线分析输出六轴均值、样本标准差、极值与SVG。仅减去第一段陀螺仪均值，应用于第二段；加速度包含重力，不减成三轴零。参见PC指南的 `analyze` 命令。

## 实验5：断线恢复

在外接模块SDA设置可控断点，保持VCC/GND稳定；不用带电乱拔整排线。分别单IMU、共享总线、加屏幕时断开2–5秒，接回后检查首个有效新样本。后两种场景须等对应模块驱动完成。

单次总线调用有限超时；错误使数据无效，保留历史成功时间。离线按500ms退避重试；需要时最多9次开漏SCL脉冲、释放允许后STOP、重新初始化I2C，再重新核对MPU身份与配置。SCL持续卡低、物理断线等无法软件保证修复；心跳与主循环保持运行。恢复规范参见[NXP UM10204](https://www.nxp.com/docs/en/user-guide/UM10204.pdf)。

UART TX如果超时会先中止DMA；中止失败时保留DMA缓冲并锁定TX故障，避免重用仍被读取的内存。该硬件故障需复位处理，不承诺任何DMA故障都可无复位恢复。

## 实验6与验收边界

最终要求500Hz IMU、温湿度、串口和LCD共同运行600秒，检查读取失败、已检测漏样、TX丢弃、PC帧序号与CRC。目前可以运行IMU+串口600秒诊断，但**不能据此宣称完成缺少温湿度/LCD的联合验收**。故障注入记录与正常连续采样分别保存。

报告填写真实波形/计数/CSV结果，详见 [测试报告](week-03-test.md)。全部物理项目通过后才创建week-03-pass；本次不创建该标签。
