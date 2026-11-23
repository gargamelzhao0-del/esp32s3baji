# 1.75C 显示驱动实板基准

使用 ESP-IDF 5.5.4 的 ESP32-S3 开发终端，在 ASCII 路径的完整仓库下进入 `tests/device/display_bench`，运行以下命令。此项目用当前仓库的显示驱动、板级绘制和 G1 画笔生成 RAM 镜像。

```shell
idf.py -DIDF_TARGET=esp32s3 build
python -m esptool --chip esp32s3 --port COM21 --no-stub load_ram build/display_bench.bin
```

串口为 115200 波特，无复位连接 USB Serial/JTAG，读取至 `DISPLAY_BENCH_DONE err=0`。计时完成后显示生产 G1 诊断图，复位可以回到原 Flash 固件。

每组预热 6 次，记录其后 48 次的微秒耗时。P50 取中间两值的均数，P95 取升序第 46 个值。测量区间从开始绘制或提交到末笔传输回收。完整矩形见 `g1_full`，按圆形可见范围处理的绘制见 `round_full`。小窗口包含固定地址、交替地址、一次完成两个远端矩形以及整屏高度的窄列。重复地址和交替地址用于区分寻址命令缓存的影响。

`raw_full_sync` 和 `raw_full_stream` 使用一块已生成的只读 DMA 条带；前者每条带显式等待，后者在整帧末端显式等待。GPIO13 的 TE 上升沿单独采样一秒，在绘制测速前移除中断。对照不同版本时使用同一块板、相同配置，以及相同的基准代码。
