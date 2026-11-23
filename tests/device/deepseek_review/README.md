# DeepSeek V1 圆屏实板验收

本工程通过 ROM 的 RAM 下载入口临时运行生产版鲸娘应用。设备从 USB Serial/JTAG 接收生产资源包和浏览器参考 CRC，依次核对传输、触控初始化、实际像素与屏幕 DMA，随后进入可用手指操作的应用。重启会恢复 Flash 中的固件。

在 ESP-IDF 5.5.4 的开发终端中，将完整仓库放在 ASCII 路径，从本目录执行。上传脚本需要 pyserial。

```shell
idf.py -DIDF_TARGET=esp32s3 build
python -m esptool --chip esp32s3 --port COM21 --baud 460800 --no-stub --after no_reset load_ram build/deepseek_review.bin
python upload.py --port COM21 --hd ../../../components/baji_deepseek_hd/assets/scene-ram.hds --motion ../../../components/baji_deepseek_hd/assets/motion.hda --timeout 600 --log com21.log
```

像素参考表来自网页在 Chromium 中的逐帧截图，默认路径为 `components/baji_deepseek/authoring/web.crc`。默认测试遍历全部 66 个动作帧或状态、两种视距、四个时段，并在晨雾全身视距上测试每一条目的全部气泡位置，共 636 幅。加上 `--every-bubble` 可在每个组合都测量全部七个气泡位置，共 3,696 幅。独立参考和生产包分别使用 `--reference` 与 `--asset` 指定。

- `DSK_PIXELS` 核对板端生产合成器输出的 466 × 466 扫描线，按屏幕线序、每像素 RGB565 高字节在前计算 CRC32。四个不可见的矩形角也参与比较。
- `DSK_PANEL` 针对 6 个动画和 12 个状态各提交一次整圆绘制，计时到末笔 DMA 完成，单位为微秒。这个数字描述整圆传输，日常播放使用局部刷新。
- `DSK_HD_PIXELS` 对比主机与设备运行高清生产合成器的 612 个呼吸/辅助线帧、16 种图集视图。主机另对 36 张静止舞台使用 Chromium 参考进行校验。两个资源包同时常驻 PSRAM。
- `DSK_SWITCH` 通过两次注入按住和松手的时间、坐标，驱动产品 `deepseek_app_tick` 并校验进入高清、返回像素两个实际状态。省略 `--hd` 可单独验收像素版。
- `DSK_MOTION` 通过 USB 分页读取正式版的 HDA2 屏幕字节序呼吸包（兼容旧 HDA1），复用生产选择、解压、画笔接口，在芯片上对照 Chromium 的 340 × 340 舞台截图。272 个呼吸页各核对参考线开启/关闭两种画面，并对每页提交一次屏幕 DMA。省略 `--motion` 可单测双包常驻模式。验收后 RAM 继续运行同驻双包，正式固件从 Flash 读取完整 HDA2。select 与 DMA 的微秒数取自已接收的内存页，实际 Flash 分区读取耗时需另行计入。
- `DSK_LIVE_OK` 核对进入真实触屏应用后一分钟的绘制结果。链接器仅在此验收程序中观测生产显示和选帧接口的返回，记录成功提交的次数、局部刷新的次数和最大传输时间，以及循环播放经过的姿势、跳过的姿势和片段切换数。采样时间可由 CMake 变量 `DSK_LIVE_CHECK_MS` 设置，验收后串口释放，应用继续播放。

原尺寸高清图集可单独使用一次 RAM 下载。重新执行上面的 `load_ram` 后，运行 `python upload.py --port COM21 --native-gallery ../../../components/baji_deepseek_hd/assets/scene.hds`。此模式只载入正式版 HDS，将四张图集各按适配、居中放大、放大后两个对角平移绘到屏幕，16 幅均与 `sheets-native.crc` 比较。输出 `DSK_NATIVE_OK` 后释放设备，可再下载双模式运行。

要测试实际呼吸节奏，使用 `--native-gallery ../../../components/baji_deepseek_hd/assets/scene-ram.hds --motion ../../../components/baji_deepseek_hd/assets/motion.hda`。程序在 USB 全帧比对时保留喂饭姿势的 16 个压缩页，在闭合 USB 驱动后按 GPIO13 的实际扫描节拍播放三个连续 2.8 秒周期。`DSK_BREATH` 的 `steps` 记录已提交的呼吸档位变化，`skipped` 记录相邻绘制跨过的档位，`past_next` 记录本次传输完成时已越过下个动画变化时刻的次数。完整双模式验收后，也可重新 `load_ram`，仅传 `--hd ../../../components/baji_deepseek_hd/assets/scene-ram.hds --smoke` 以少量场景验证并恢复可交互演示。
