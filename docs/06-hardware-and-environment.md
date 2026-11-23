# 1.75C 板卡驱动与 MotoNav 移植记录

目标板为 Waveshare ESP32-S3-Touch-AMOLED-1.75C。官方资料和 MotoNav 同型号设备记录一致，芯片为双核 ESP32-S3R8，外接 32 MiB Flash，封装内有 8 MiB PSRAM。C 型 BSP 标记 `BSP_CAPS_SDCARD=0`。角色资源应以现有板载 Flash 为基础规划；此版采用标准 factory 应用分区。带电池和不带电池是两种销售配置，电量由 PMIC 现场上报。[微雪板卡页](https://docs.waveshare.net/ESP32-S3-Touch-AMOLED-1.75C/)

## 参考工程读到的实际链路

本次对照本地 `motonav/moto_esp32` 的 0.3.18 源码、`dependencies.lock`、1.75C 原理图文件及同型号硬件测试记录。MotoNav 使用 ESP-IDF 5.5.4、1.75C BSP 3.0.0、CO5300 2.2.0、CST9217 2.0.0 和 LVGL 9.5.0。电子吧唧用 IDF 5.5.4 的公开驱动 API 实现下表中的端口，可以独立构建。

| MotoNav 来源 | 与电子吧唧有关的行为 | 本工程落点 |
| --- | --- | --- |
| `main/app/navigation_app.cpp`、`motonav_platform/platform.cpp` | 原应用依次初始化 NVS、导航服务、显示、LVGL 触摸与无线服务；设备名、重启策略和导航数据属于其应用生命周期。 | `main/app_main.c` 只负责启动、日志和退出清理。业务状态仍在可单独测试的 `baji_core`。 |
| `motonav_display/qspi_panel.cpp`、`strip_stream.h` | 40 MHz QSPI 的命令、地址、颜色数据在一次事务内发送。连续同列的条带用 RAMWRC 续写，最后一次 DMA 回收决定何时可重用缓冲区。 | `baji_display` 管面板时序、边界、窗口、亮度和 DMA；`baji_board` 管两块缓冲以及脏区扩边。 |
| `motonav_display/display.cpp`、`pixel_pack.cpp`、`pixel_pack_s3.S` | 参考工程还含 LVGL 调度、导航性能计数及可选 RGB332 压缩。地图跑分取决于传输色深、每帧覆盖面积和计算负载。 | 圆屏接口接收 RGB565 大端像素，应用可填充像素画和小范围变化，后续 UI 可通过相同接口提交。 |
| `motonav_touch/touch.cpp` 与锁定的触摸组件 | 双轴镜像，CST9217 的寄存器读取分发送地址、STOP 等待、接收数据三步。原应用随后注册 LVGL 输入设备。 | `baji_peripherals` 输出单指坐标或释放，支持 GPIO 中断等待。按住期间定时取样，坏帧保留上次有效样本。 |
| 1.75C BSP、`motonav_ble`、`motonav_graphics`、`motonav_ui` | BSP 提供音频接口；MotoNav 的蓝牙特征和重连状态服务于导航协议，图形库处理地图。参考启动链没有电池遥测。 | 本版新增 AXP2101 只读遥测。音频、IMU、RTC 和宿主通信属于后续独立功能；首版上电运行显示、触摸、电量诊断。 |

## 硬件连接

| 端口 | 板级配置 |
| --- | --- |
| CO5300 显示 | SPI2，CS 12，SCLK 38，D0–D3 为 4、5、6、7，面板复位 1，TE 扫描同步信号在 GPIO13。可见区域 466 × 466，显存列起点偏移 6。亮度经寄存器 `0x51` 设置。 |
| CST9217 触摸 | I²C1 共线，SDA 15、SCL 14、400 kHz，7 位地址 `0x5a`，触摸复位 2，低有效中断 11。识别芯片 ID 和分辨率后，两个方向按 `465 - 原始坐标` 变换。 |
| AXP2101 电源 | 同一总线，7 位地址 `0x34`。启动读取芯片 ID，采样电池连接、USB 有效供电、充电与剩余电量。无电池或无效百分比显示为 unknown，0% 是有效读数。寄存器位定义与 XPowers 对照。[AXP2101 状态实现](https://github.com/lewisxhe/XPowersLib/blob/master/src/XPowersAXP2101.hpp) |
| 存储与控制台 | QIO Flash、80 MHz 八线 PSRAM、USB Serial/JTAG。启动日志读取实际物理 Flash 容量和已初始化的 PSRAM 容量。 |

## 性能与所有权

全屏 RGB565 需要 434,312 字节。本版用两块内部 DMA 缓冲，每块 466 × 32 × 2 = 29,824 字节，两块共 59,648 字节。单次条带小于 32 KiB，完整矩形拆成 15 段，条带之间可交叠计算和传输。绘制回调直接填当前可写缓冲；提交下一段时回收上段，最终等待最后一次 DMA。超时保留尚在使用的缓冲。面板的偶数像素对齐由板级层处理，回调填满扩边后的矩形，避免相邻像素残留。[板级 API](../components/baji_board/include/baji_board.h)

窄区域按实际宽度分配条带高度。例如从第 1 行起刷新最右一列的 465 个像素，扩边后为 2 × 466，整列只提交一次 DMA。显示从初始化到释放由同一个任务拥有。触摸和电量使用同一总线实例与互斥锁，IRQ 只发信号，在任务上下文读取 I²C。诊断页在静止时停止显示刷新，首次全屏绘制并完成 DMA 后才提亮。触摸移动时就近合并标记的旧、新矩形，远距离移动只更新两块局部区域。

圆屏应用通过 `baji_board_paint_round()` 更新可见范围。上下边缘各分六段，覆盖中心在 (232.5, 232.5) 的圆面，并保留约 3 个像素的径向余量。一次整圆绘制发送 182,340 个 RGB565 像素，采用 13 个窗口和 17 笔 DMA。矩形 `baji_board_paint()` 仍可更新所有 217,156 个显存像素。两个或多个分离区域可用 `baji_board_paint_round_regions()` 按传入顺序绘制，整批共用末次 DMA 等待。每个回调按收到的矩形重建内容，包括偶数对齐的相邻像素。[板级 API](../components/baji_board/include/baji_board.h)

面板地址相同的轴可复用。驱动在需要重新定位时获取总线，连续条带沿用当前批次，公开的等待操作完成后释放总线。至多 1,024 字节的小块使用短时轮询回收 DMA，更大的传输由中断唤醒任务。直接使用异步显示 API 时，应在进入空闲前调用 `baji_display_wait()`。[显示 API](../components/baji_display/include/baji_display.h)

CO5300 的 QSPI 写时钟周期至少 20 ns。ESP32-S3 的 80 MHz APB 通过整数分频，在规格内可选的最高档为 40 MHz。四线净荷上限为 20 MB/s。完整矩形和本版圆屏窗口的纯数据下限分别为 21.716 ms、18.234 ms。面板扫描、绘制和 DMA 回收各自独立计时。[CO5300 数据手册 §6.4](https://files.waveshare.com/wiki/common/Co5300_Datasheet.pdf) · [乐鑫 SPI 主机驱动](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/api-reference/peripherals/spi_master.html) · [实板基准](../tests/device/display_bench/README.md)

## 复验

在项目根目录运行 `tools/build.ps1` 编译固件，运行 `python tests/display/run.py` 执行驱动回归。构建脚本使用临时 ASCII 盘符，固件、bootloader、分区表与烧录参数均位于脚本输出的 `build-firmware-*` 目录。每个构建目录独立生成 sdkconfig，配置源为仓库中的 `sdkconfig.defaults`。

上板后，观察启动日志中的内存容量、触摸/电源识别结果及首帧 CPU 绘制至最后一次 DMA 完成的微秒数。圆屏中央从左到右应为红、绿、蓝，上方有金色方向刻度。触摸时白色十字应跟随指尖、松手应擦除；再验证四个方向、连续拖动、USB 单独供电和装入电池后的读数。该页可独立验证驱动，角色动画及 Agent 传输在后续阶段接入。
