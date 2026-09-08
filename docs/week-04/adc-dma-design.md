# ADC、DMA及数据闭合

单通道1kHz，200个u16循环缓冲，HT/TC每100点交接。HT和TC各5Hz；回调只发布半区完成序号、时间及计数。主循环按NDTR、完成代次和100ms覆盖期限检查半区，复制后再次检查；无法证明稳定的块丢弃并计数。复制出的数据编码到UART队列自有内存，DMA源半区不会直接用作UART发送缓冲。

停止流程：关闭TIM3/TRGO→保留中断等待100μs让末次转换完成→屏蔽ADC/DMA的NVIC入口并处理挂起完成事件→在SysTick仍运行时HAL停止ADC/DMA→冻结NDTR并统计尾部。精确半区边界最多保留两个仍有效完整半区，其他位置保留最新完整半区及局部尾块。停止失败会锁定ADC故障、禁止数据读取和重启，需复位；不能重用可能还被DMA访问的内存。

正常采样：

```powershell
python tools/week4_test/week4_test.py --port COM实际端口 record --seconds 60 --out results/adc-clean
python tools/week4_test/week4_test.py analyze results/adc-clean
```

计数关系：samples_completed=100×blocks_completed+tail_samples；blocks_sent/samples_sent是成功排入UART队列的块/点，尾块也计入发送块。PC解析点数应与samples_sent一致，第一样本、块序号、尾端缺口和ADC/DMA/UART错误还需同时核对。串口发送队列成功不等于已经到达PC，故同时检查发送错误和最终PC闭合。

100点原始块载荷220字节，线上231字节；每秒10块为2310B/s。460800、8N1理论46080B/s，另留命令与状态余量。主机将原始块展开为CSV；逐点时间为逻辑序号推算的名义时间，不是假称逐点实测时间戳。

消费者故障：

```powershell
python tools/week4_test/week4_test.py --port COM实际端口 record --seconds 60 --pause-ms 250 --out results/adc-fault
```

只暂停前台消费，中断/DMA/串口继续。覆盖及样本缺口必须被检测，fault_detected才可为true。故障试验的acquisition_passed始终false，不能算正常无丢失。ADC overrun、DMA错误、CRC错误、UART错误或最终发送数量不符不属于预期覆盖，仍判异常。

DMA中断如果被屏蔽超过缓冲周期，同一硬件标志可能合并多个事件；错误/序号指标不可能证明这种情况下准确物理样本总数。必须结合PA6触发参考、PC2回调、实际数据率与独立仪器验收。250ms实验保持IRQ运行，不测试任意长全局关中断。
