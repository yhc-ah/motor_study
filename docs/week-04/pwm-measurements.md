# PWM 实验步骤

先完成pinmap接线，用实际串口替换COM实际端口。PC工具所有 `--port` 参数放在子命令之前。先启动1kHz50%，确认周期约1000μs、高电平约500μs。

```powershell
python tools/week4_test/week4_test.py --port COM实际端口 pwm --hz 1000 --duty 500
```

三档频率1000/10000/20000，分别使用duty100/500/900（千分比）采集九组波形。记录PSC、ARR、CCR、周期、高电平时间、仪器采样率。边沿配置PSC83，ARR分别999/99/49，CCR分别为周期计数×占空比并取整；20kHz25%对应CCR13，即26%，属于量化误差。

```powershell
python tools/week4_test/week4_test.py --port COM实际端口 pwm --hz 20000 --duty 250
python tools/week4_test/week4_test.py --port COM实际端口 pwm --hz 10000 --mode 2
```

mode2固定频率每10ms切换10/50/90%，保存至少100次切换波形。相同频率/对齐方式更新只写预装载CCR，不重启定时器。频率或对齐改变会重新配置周期，其过渡波形不属于固定周期CCR更新保证。

```powershell
python tools/week4_test/week4_test.py --port COM实际端口 pwm --hz 10000 --duty 0
python tools/week4_test/week4_test.py --port COM实际端口 pwm --hz 10000 --duty 1000
python tools/week4_test/week4_test.py --port COM实际端口 pwm --hz 10000 --mode 0
python tools/week4_test/week4_test.py --port COM实际端口 pwm --hz 10000 --duty 500 --center
```

停止模式关闭通道和计数器，并将PB6切回推挽低。重新启动时观察第一个脉冲。中心模式使用1MHz/(2×频率)得到ARR，10kHz时ARR50、50% CCR25。中心模式100%端点专门设CCR=ARR+1，保证比较阈值超过整个计数范围；不把数学占空比取整结果CCR=ARR当成端点电平保证。

实测门槛：10kHz误差<0.5%，高电平时间对量化后目标误差≤1μs（本配置一个计数）；100次切换无已检测异常脉冲；0/100%、停止/重启有波形证据。所有实测结果目前NOT RUN，status中寄存器配置不能替代测量。
