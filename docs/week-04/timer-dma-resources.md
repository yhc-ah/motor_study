# 定时器、DMA及工程所有权

当前168MHz SYSCLK/HCLK，PCLK1=42MHz，PCLK2=84MHz。APB1定时器TIM2/3/4/6输入84MHz；TIM8在APB2，编码器外部计数模式无需拿168MHz当编码器信号频率。不要修改前三周时钟后继续照搬参数。

| 模块 | 配置 | IRQ/内存 |
|---|---|---|
| TIM2 | 1MHz，32位自由运行，保留微秒时间戳 | 无更新IRQ，约71.6分钟回绕 |
| TIM3 | PSC83，ARR999，TRGO Update；CH1参考PWM CCR500 | 1kHz硬件触发ADC，无更新IRQ |
| TIM4 | PSC83；边沿ARR999/99/49，中心ARR500/50/25 | 硬件PWM，CCR和ARR预装载 |
| TIM8 | TI12编码器，PSC0，ARR65535，滤波0，输入分频1 | 无逐边沿IRQ；主循环10ms扩展计数 |
| TIM6 | PSC83，ARR2499/249 | A相100/1000Hz，状态变化400/4000次每秒；IRQ优先级6 |
| ADC1 | PC0 IN10，12位右对齐，PCLK2/4=21MHz，84/480采样周期 | 外部触发、非连续转换；ADC IRQ优先级4 |
| ADC DMA | DMA2 Stream0 Channel0，Circular，Halfword，Memory increment | HT/TC/错误，优先级4；200个u16，普通SRAM |
| USART1 RX | DMA2 Stream2 Channel4，Circular | 原week2配置 |
| USART1 TX | DMA2 Stream7 Channel4，Normal | 原week3独立帧缓冲队列 |

第四周外设由 `bsp/week4/bsp_week4.c` 手工BSP拥有；并没有假装调用CubeMX生成相同外设。TIM3初始化UG发生时TRGO关闭，ADC启动准备好后才启用TRGO与计数器。

`tools/configure_week4_project.py` 维护Keil源文件/包含路径，并在 `.ioc` 将第四周引脚预留为模拟态。BSP初始化随后设置实际AF、输出和ADC功能。**IOC中的预留模拟态不是运行配置；运行配置以上表及BSP为准。** 不要在CubeMX同时启用这套TIM/ADC/DMA，否则会产生重复所有权和IRQ定义。当前验证的是提交的Keil工程和同步脚本，未执行CubeMX GUI重新生成验收。

构建入口 `MDK-ARM/stm32-realtime-bsp.uvprojx`，ARMCC5.06 update7。`app/week4_config.h` 默认APP_WEEK4=1；默认运行第四周命令任务，保留week1/2串口和按键功能。旧传感器任务与其命令在第四周模式不运行；前三周分支未被改变。
