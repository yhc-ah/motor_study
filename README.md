实验 0：工具链与工程创建
任务：
1. 安装 STM32CubeMX。
2. 安装 STM32CubeIDE，或使用已有 Keil 工程。
3. 确认 ARM GNU 工具链可用。
4. 使用 CubeMX 选择正确芯片型号。
5. 配置系统时钟、LED、按键。
6. 生成工程。
7. 编译、烧录、运行。
8. 初始化 Git。
优先安装：
- STM32CubeMX 官方页面
- STM32CubeIDE 官方页面
- STM32CubeIDE 用户手册 UM2609


实验 1：非阻塞 LED 闪烁
要求：
- 不允许使用 HAL_Delay() 实现主循环任务。
- 使用 HAL_GetTick() 判断时间。
- LED 闪烁逻辑放在主循环或 app 层。
- GPIO 操作放在 bsp 层。
推荐结构：
bsp_led.c       // 只负责 LED 硬件操作
app_task.c      // 负责周期调度
main.c          // 初始化和主循环
测试频率：
- 10 Hz
- 50 Hz
- 100 Hz
LED 肉眼无法准确判断 50/100 Hz，因此需要使用独立测试 GPIO 输出，并接逻辑分析仪。


实验 2：按键 EXTI 外部中断
要求：
- 按键配置为 EXTI。
- EXTI 回调只设置事件或递增计数。
- 按键消抖在主循环处理。
- 按键事件触发 LED 状态切换或计数器加一。
- 不能在中断中打印和延时。
逻辑结构：
按键动作
  ↓
EXTI
  ↓
NVIC
  ↓
HAL_GPIO_EXTI_Callback()
  ↓
只设置 key_event
  ↓
主循环读取事件
  ↓
消抖、执行动作、记录日志
建议使用：
volatile uint32_t key_irq_count;
在中断中只执行：
key_irq_count++;
主循环再判断计数变化并进行消抖。


实验 3：中断执行时间测量
找一个空闲 GPIO 作为测试脚：
进入 EXTI 回调：测试脚拉高
退出 EXTI 回调：测试脚拉低
使用逻辑分析仪测量高电平持续时间，即中断处理时间。
中断中只能做：
- 设置标志
- 递增计数
- 测试脚翻转
不要在中断中：
- 处理业务逻辑
- 访问 LCD
- 发送串口
- 调用延时
- 做浮点运算


实验 4：map/size 内存分析
在工程中添加几类变量：
const uint8_t flash_table[64] = {1, 2, 3};
uint8_t init_data[128] = {1, 2, 3};
static uint8_t zero_buffer[256];
编译后观察：
- flash_table 位于哪里？
- init_data 位于哪里？
- zero_buffer 位于哪里？
- 哪个函数占用 Flash 最大？
- 哪个变量占用 RAM 最大？
使用：
arm-none-eabi-size -A Debug\你的工程.elf
也可以使用：
arm-none-eabi-nm -S --size-sort --print-size Debug\你的工程.elf
需要分别编译：
- Debug
- Release
比较：
- .text
- .rodata
- .data
- .bss
- 总 Flash
- 总 RAM
- 最大函数
- 最大变量

验收标准：
空工程可以独立编译。
程序可以通过 DAP 烧录。
LED 可以非阻塞闪烁。
按键可以通过 EXTI 触发事件。
按键 100 次操作无重复触发。
10 Hz、50 Hz、100 Hz 周期误差小于 1%。
EXTI ISR 执行时间小于 50 μs。
ISR 中没有延时、打印和复杂业务逻辑。
能解释复位向量到 main() 的过程。
能解释 .text/.rodata/.data/.bss。
能从 map 文件找到中断向量、一个函数、一个全局变量和一个静态变量。
能列出 Flash/RAM 占用最大的 3 个对象。
完成 Debug/Release size 对比。
