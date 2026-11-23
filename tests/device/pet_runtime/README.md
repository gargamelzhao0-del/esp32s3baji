# 电子宠物生产绘制路径实板验收

ESP-IDF 5.5.4 的 ESP32-S3 开发终端中，从完整仓库的 ASCII 路径进入本目录。RAM 镜像使用主工程的角色播放器、像素合成、变化区域、板级显示和 GPIO13 扫描同步组件。构建时启用 `BAJI_PET_EXTERNAL_ASSET`，主机通过 USB Serial/JTAG 传入当前仓库的生产资源包，并在板上核对长度、CRC32 和解码结果。主机脚本依赖 pyserial。

```shell
idf.py -DIDF_TARGET=esp32s3 build
python -m esptool --chip esp32s3 --port COM21 --no-stub --after no_reset load_ram build/pet_runtime.bin
python upload.py --port COM21 --log runtime.log
```

程序先测同一合成器的 RGB565 整圆刷新，2 次预热、12 次计时。随后重新初始化角色状态，单独计量首次铺屏，再在 600 个连续的 TE 上升沿调用生产 `pet_player_tick`。输入依次为 180 个节拍静置、180 个节拍拖动与松手、240 个节拍内点按额头一次并播放完整蹭蹭动作。拖动轨迹和点按由基准注入，避免真人操作重复性影响对照。三个阶段之间连续计时，统计和串口输出均在最后一个节拍结束后进行。

| 输出 | 字段含义 |
| --- | --- |
| `PET_FULL`、`PET_FIRST` | 整圆稳定样本和首帧，从选帧/开始绘制到最后一笔 DMA 完成的微秒数 |
| `PET_MEMORY` | 板级初始化完成时，空闲 PSRAM、内部内存及最大的可用内部 DMA 缓冲区，单位为字节 |
| `PET_STAGE` | `sent/noop` 为有像素传输和无传输的节拍数；`pose_changes/gaze_changes` 记录实际播放器状态变化，`gaze_only_sent` 仅计独立视线变化且绘制成功的节拍 |
| `te_draw_*`、`te_gaze_*` | 实际绘制及其独立视线子集从 TE 到 DMA 完成的微秒数；`noop_*` 计量无绘制的播放器调用耗时 |
| `missed/late` | 两次调度间漏过的边沿，以及从当前边沿开始到提交结束时已经出现新边沿的次数 |

脚本核对三个阶段、动作起落、视线单独更新和无变化时的零传输。丢包、传输错误、超时、漏过边沿或越过当前周期均返回非零退出码。成功时收到 `PET_END ok=1` 和 `PET_LIVE`，设备释放基准的 TE 监听，复用同一份已解码素材和显示句柄进入主工程的 `pet_app_run_loaded`，此时可用真手指互动。复位回到 Flash 中的原固件。
