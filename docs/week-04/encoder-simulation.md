# 编码器实验步骤

PG2→PC6为A相，PG3→PC7为B相；逻辑分析仪并联观察。两相状态为00→01→11→10→00或反向序列。每次只改变一位，每四个状态变化为一个A相完整周期。初始相位重置时先停止接收计数，然后设CNT与累计位移起点，避免启动沿混入结果。

```powershell
python tools/week4_test/week4_test.py --port COM实际端口 encoder --hz 100 --cycles 1000 --direction 1 --initial 0 --out results/encoder-100-forward
python tools/week4_test/week4_test.py --port COM实际端口 encoder --hz 100 --cycles 1000 --direction -1 --initial 0 --out results/encoder-100-reverse
python tools/week4_test/week4_test.py --port COM实际端口 encoder --hz 1000 --cycles 1000 --direction 1 --initial 0 --out results/encoder-1000-forward
python tools/week4_test/week4_test.py --port COM实际端口 encoder --hz 1000 --cycles 1000 --direction -1 --initial 0 --out results/encoder-1000-reverse
```

每组工具等待发生器完成，保存原始串口、状态及summary。比较实际发生器步数4000、编码器位移绝对值4000；两方向应反号，符号以实测接线约定登记。100Hz约10秒，1kHz约1秒。工具内部计数一致不证明GPIO实际发出所有边沿，仍须检查波形。

回绕用initial65530或5，cycles10等少量周期分别测试两方向。16位模差分：65530→4=+10、4→65530=-10；恰好32768的差分拒绝。每10ms用实际TIM2时间差计算count/s，读取长时间停顿会记hardware_errors，不能还原任意多次回绕。

发生器最后一次边沿后等待至少2μs，并在短临界区完成最终计数快照后才报告generator_active=0。encoder_position_low是从本次命令起点开始的有符号累计位移，CNT可以从任意initial开始。停止输入后速度应归零。没有真实编码器每转计数，不输出rpm。
