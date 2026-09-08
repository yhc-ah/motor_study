# 第四周：PWM、编码器与ADC实验

用户确认前三周已完成。本分支在week3 a168430基础上实现第四周软件；**软件通过不等于第四周实物实验全部通过**。本次没有识别到开发板串口/DAP，因此未烧录、未测波形/电压、未执行实物60秒及600秒验收。下面步骤是可执行的实验入口，报告据真实结果填写。

1. 阅读[引脚接线](pinmap.md)和[资源分配](timer-dma-resources.md)，检查实物占用，断电接外接电位器与A/B回环。
2. 打开 `MDK-ARM/stm32-realtime-bsp.uvprojx`，目标stm32-realtime-bsp，ARMCC5.06 update7，Rebuild；确认0错误/警告后用自己的DAP下载。
3. 保持460800、8N1，安装pyserial，串口型号以设备管理器实际枚举为准。
4. 使用下面的status命令确认protocol version=1；回归旧PING/GET_STATS及心跳。

```powershell
python -m pip install -r tools/week4_test/requirements.txt
python tools/week4_test/week4_test.py --port COM实际端口 status
./tests/host/run_week4_tests.ps1
```

| 实验 | 具体操作 | 必须保存的证据 |
|---|---|---|
| 0 基线/接线 | 本页及资源表 | 接线、时钟、编译/下载信息 |
| 1 PWM | [PWM步骤](pwm-measurements.md) | 九组静态、100次更新、端点/停止/中心对齐波形 |
| 2 编码器 | [编码器步骤](encoder-simulation.md) | 双频率双方向、回绕、CNT及位移、A/B波形 |
| 3 ADC电压 | [PC指南](../../tools/week4_test/README.md)中的三点六次采集 | 万用表与VREF实测、原始CSV、两档采样时间 |
| 4 ADC DMA | [DMA步骤](adc-dma-design.md) | ≥60秒正常数据、尾块闭合、250ms故障记录 |
| 5 滤波 | [滤波步骤](filter-comparison.md) | 同源原始/滤波CSV、统计、噪声和阶跃图 |
| 6 联合运行 | 下列600秒命令 | 状态日志、完整60秒窗口、联合波形与10分钟结果 |

```powershell
python tools/week4_test/week4_test.py --port COM实际端口 record --seconds 600 --joint --out results/week4-joint-600s
python tools/week4_test/week4_test.py analyze results/week4-joint-600s
```

联合配置：10kHz PWM每100ms由20ms低通后的ADC映射到10–90%；100Hz A相模拟编码器；ADC1kHz；按块上传，status约1Hz（PC可主动查询到2–5Hz）。执行600秒时缓慢旋转电位器；外部仪器检查PWM与A/B，比较单独和联合时最大消费延迟。工具结束时停止ADC/PWM/发生器，核对最终编码器计数与实发软件步数，不称为速度闭环。

本周使用文本状态，LCD不是隐藏的已实现项。可在已有具体屏幕驱动就绪后接入低频显示，但须重新核对引脚及消费延迟。第四周默认不运行第三周传感器，全部代码仍保存在其分支及本分支历史中。

数据目录每次使用新名称；PC输出raw.bin、adc.csv、status.jsonl、summary.json。缺口保留原序号，CRC/重复/乱序/错误ACK均被记录。`report_ok`只是工具检查结果；`acquisition_passed`是正常采集的软件检查，故障试验始终false；`hardware_verdict`始终NOT_ASSESSED，须结合[实测报告](week-04-test.md)与波形。

没有自动创建week-04-pass标签。全部实物验收通过并保存证据后再创建。
