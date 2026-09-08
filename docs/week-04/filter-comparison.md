# 滤波与统计步骤

先将电位器放在约50%处稳定，记录10秒。对同一份raw计算y += alpha*(x-y)，Ts=1ms，tau5/20ms分别alpha0.181269247/0.0487705755。首个有效样本初始化；跨100点块保持状态，缺口/无效样本后重新初始化。

```powershell
python tools/week4_test/week4_test.py --port COM实际端口 record --seconds 10 --out results/noise
python tools/week4_test/week4_test.py analyze results/noise --settle-ms 200
python tools/week4_test/week4_test.py step-test --out results/step
```

分析默认在每个连续有效区间统一排除前200ms，三个序列使用同一统计窗口。输出均值、总体标准差、极值、峰峰值和noise.svg；`--settle-ms 0`可明确保留初始化段。SVG用名义时间轴，缺口不压缩、无效数据不连线。VREF只在明确传入 `--vref` 后用于code×VREF/4095换算，无隐含精度承诺。

阶跃为软件合成：第1000点由1000跳到3000，1ms步长。测试63.2%约2264以及10–90%的1200→2800。5ms滤波预期t63约5ms，上升约11ms；20ms滤波约20ms和44ms。合成计算不能替代电位器真实阶跃响应；再采集实际快速旋转过程并保留原始输入。

比较时分别说明滤波响应、约100ms分块等待及串口/显示延迟。滤波tau=5ms不代表系统端到端只有5ms。当前没有任何虚构实物噪声降低百分比。
