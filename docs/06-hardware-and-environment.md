# 板卡、环境与依赖基线

## 已核实与仍需上板核实

Waveshare 官方资料说明此板为 ESP32-S3、466 × 466 AMOLED、CO5300 QSPI、CST9217 触摸，具备 PSRAM、SD、PMIC 与 IMU，适合本地角色显示。AMOLED 亮度应由屏幕控制命令管理，不能套普通 LCD 背光 PWM。I2C 共享、USB 引脚占用和 SD 1-bit 接法以该板官方原理图与 BSP 为准。[板卡文档](https://docs.waveshare.net/ESP32-S3-Touch-AMOLED-1.75/)、[硬件参考](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/blob/main/HARDWARE_REFERENCE.md)

| 项目 | 基线 / 待确认 |
| --- | --- |
| SoC / 内存 | ESP32-S3R8，标称 8 MB PSRAM + 16 MB Flash；G1 读芯片实际值 |
| 屏幕 | 466 × 466，QSPI；旋转方向、颜色顺序、可用 DMA 模式上板验证 |
| 触摸 | CST9217，共用 I2C；多点能力存在，首版只使用单指 |
| SD | 1-bit SDMMC，首版 FAT32；卡容量/品牌/读取时延实测 |
| 电源 | AXP2101；是否装电池、容量、保护与极性仍需实物确认 |
| 版本 | 标准 / B 外壳 / G GPS；首版不启用 GPS，不能凭网页猜所购 SKU |
| USB | 原生 USB；业务先用 USB Serial/JTAG，不同时用 OTG 功能占用同一控制资源 |
| 按键 | PWR 保留电源职责；BOOT 为下载绑定位，不随意复用 |
| 音频 / IMU | 首版不启用；减少总线和功耗变量 |

固件不在核心层硬编码引脚。G1 只从同一版本 BSP 导入 board definitions，核对原理图后再配置。屏幕 reset 与触摸 reset 不可混用；SD 的 D3/CS 不可当作机械插卡检测脚。

## 本机环境事实

2026-11-18 只读检查：

- VS Code 的 `idf.currentSetup` 指向 `D:\.espressif\v6.1.1\esp-idf`，`IDF_TARGET=esp32s3`。
- 本地 SDK 的 `tools/cmake/version.cmake` 为 6.1.1。
- 工具在 `C:\Espressif\tools`；IDF Python 位于 `python\v6.1.1\venv\Scripts\python.exe`，已有 idf_component_manager、pyserial、jsonschema。
- 系统 Python 3.12 是另一个环境，未安装上面这些库。不要把“命令找不到”误认为 SDK 没有安装。
- 当前 PowerShell 未激活 IDF；工程内构建脚本临时设置所需环境，退出时恢复，不修改机器全局 PATH。
- Windows 中文工程路径遇到了实际 GCC specs 文件路径错误；仅设置 PYTHONUTF8=1 不能解决。脚本保留原目录，临时映射空闲英文盘符，以 CMake / Ninja 直接构建，结束后校验目标并清理映射。
- 可枚举的 COM 口不能直接确认设备身份；当前没有打开串口或进行烧录。

原 `.vscode` 配置保留。`.devcontainer` 仍是模板，使用浮动 latest 镜像和 QEMU，暂不作为本项目可靠构建基线；没有测试 Docker USB 转发。后续启用时应锁定镜像 digest 并移除“QEMU 能验证真实屏幕”的误解。

## BSP 候选锁定

本次查阅的 registry 版本为 `waveshare/esp32_s3_touch_amoled_1_75 3.0.1`。依赖声明包括 IDF ≥5.5、LVGL ≥8,<10 与 esp_lvgl_adapter ~0.6。这个范围允许尝试本机 6.1.1，但不等于该组合已经验证。候选清单保存在 `../config/idf_component.display.yml.example`，尚未加入默认构建。[BSP 版本](https://components.espressif.com/components/waveshare/esp32_s3_touch_amoled_1_75/versions/3.0.1)、[依赖](https://components.espressif.com/components/waveshare/esp32_s3_touch_amoled_1_75/versions/3.0.1/dependencies)

registry 给出的代码链接固定到 `bfeb6e6d5737178cdde78b630c8118074da0a657`。G1 使用这一来源核对 API，再生成真正的 dependencies.lock；禁止混用官方整机应用里的私有 fork 与组件仓库的另一套头文件。[对应 BSP 源码](https://github.com/waveshareteam/Waveshare-ESP32-components/tree/bfeb6e6d5737178cdde78b630c8118074da0a657/bsp/esp32_s3_touch_amoled_1_75)

方案优先保留用户 6.1.1。若 G1 出现明确不兼容，再开独立构建目录验证官方示例标注的版本，记录差异，不静默替换用户 SDK。完整 package 下载/依赖解析、BSP 编译和实机驱动验证尚未完成。

## 构建与上板分开

当前默认应用只验证核心可以进入 ESP-IDF 工程；不启动屏幕、PSRAM、SD 和真实串口业务。使用 `tools/build-preparation.ps1` 构建；它禁用组件管理器，因为当前没有外部依赖，避免无意联网更新。接入 BSP 时另用正常 IDF 环境启用组件管理器，不沿用这个开关。

本机已完成准备工程编译，产物在 `build-preparation-z`。普通的 `idf.py build` 在此中文路径下仍会失败：idf.py 会把临时盘符 realpath 回中文路径。因此工程脚本先检查 IDF Python 依赖，再执行等价的 CMake 配置与 Ninja 构建步骤；源文件没有迁移，也没有修改 SDK。之后开发完整固件应沿用路径处理方式，启用所需的组件管理器。

如果换电脑，修改脚本参数与工具版本目录。脚本选择 Z: 至 R: 中空闲盘符，按盘符隔离构建目录；不占用已有盘符。脚本异常结束时先检查其明确打印的映射，只清理属于本工程的映射。常规构建成功后已验证没有遗留 Z:。

G1 在明确设备端口后顺序验证：芯片/Flash → PSRAM → PMIC → 屏幕色条 → 触摸定位 → SD → UI。先保存设备原有固件的恢复方式及厂商下载地址。不要把多个外设同时失败的问题混在一起定位。

Flash 分区先用 IDF 的准备构建默认布局。产品布局候选为双 3 MiB OTA app + NVS/OTA data + 内置保底资源，其余空间按实测固件尺寸规划；素材主要放 SD。当前没有提交或烧写自定义分区表，避免过早改变设备数据布局。

## 电池与结构准备

USB 桌面阶段无需假设电池已安装。佩戴阶段确认真实电池容量、接头极性、固定方式与充电参数；背壳避免压屏和遮挡 USB/按键。续航以实测电流计算：可用容量 ÷ 平均电流，再留转换损耗与余量。未测电流前不承诺续航小时数。

需要准备一条数据 USB 线、可用的 FAT32 microSD、板卡购买型号/版本信息。温升、低电和插拔必须在外壳装好后复测，不能只在裸板上验收。
