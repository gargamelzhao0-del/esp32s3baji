# 板级驱动来源

CO5300 的寄存器初始化、QSPI 写入约定及连续条带策略参考 MotoNav 0.3.18 的 `motonav_display/qspi_panel.cpp`、`strip_stream.h`，以及该工程锁定的 Waveshare ESP32-S3-Touch-AMOLED-1.75C BSP 3.0.0 和 Espressif CO5300 2.2.0。面板原驱动著作权归 2023 Espressif Systems (Shanghai) CO LTD。

CST9217 的寄存器寻址、读取时序与数据格式参考 Waveshare CST9217 2.0.0。原驱动著作权归 2015–2024 Espressif Systems (Shanghai) CO LTD 和 2025 Waveshare。本工程以 C 重新组织显示传输和共享总线，调整了缓冲区交接、单指镜像和中断唤醒接口。以上来源采用 [Apache License 2.0](Apache-2.0.txt)。
