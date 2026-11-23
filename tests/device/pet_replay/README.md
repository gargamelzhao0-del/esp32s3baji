# 实板角色局刷与 TE 节拍

在 ESP-IDF 5.5.4 的 ESP32-S3 开发终端中，进入 ASCII 路径的完整仓库下的 `tests/device/pet_replay`。IDF 的 Python 需要 Pillow。构建时自动从本地角色图集生成诊断数据，并打印源图 SHA-256。

```shell
idf.py -DIDF_TARGET=esp32s3 build
python -m esptool --chip esp32s3 --port COM21 --no-stub --after no_reset load_ram build/pet_replay.bin
```

无复位打开 USB Serial/JTAG 串口，收到 `REPLAY_END err=0` 即完成。核对两行 `REPLAY_TE` 的 `frames=300 edges=300 late=0 skipped=0` 可判定两个片段都赶上了节拍。复位会回到 Flash 中的原固件。

取远景“抱心”第 14–21 帧和近景“喷嚏”第 12–19 帧（零起点），和网页使用相同的八列图集及近景行偏移。在固定夜色背景下转为 RGB565，各自的八个 138 × 132 帧在启动时解码到 PSRAM。实际更新目标为 276 × 264。两类局刷均包含从末帧回到首帧的差异。

- `full` 提交整个角色视口。`box` 使用预先计算的单个差异包围框。`bands` 将差异划入宽或高 4 个原生像素的条带，用动态规划权衡覆盖面积和每个窗口的固定开销，再将选出的矩形一次提交给生产驱动。完整目标画面通过 RGB565 色值逐像素检查。
- `REPLAY_COST` 在 8 帧预热后测量 48 个样本。时间包括 PSRAM 读取、2 倍像素展开和最后一笔 DMA 回收。`bytes` 是这 48 帧实际提交的像素载荷总量。
- `REPLAY_TE` 逐片段测试 300 帧，每个新 GPIO13 TE 上升沿启动一帧变化。`te_dma_*` 从 ISR 记录边沿到末笔 DMA 回收。`late` 表示传输跨到了后一个边沿，`skipped` 表示两个处理周期之间漏过的边沿数。`wake_*` 反映中断唤醒绘制任务的时延。测试按面板节拍重复播放来施加负载；网页为两个片段设定的原始播放速度分别为 16 和 18 帧/秒。

本基准测量固定背景上的单角色图层，未执行五官、前景道具和 Agent 信息的合成。TE 节拍和 DMA 完成可判断更新是否赶上下一扫描周期；实际屏幕出光与撕裂需要另外观察。
