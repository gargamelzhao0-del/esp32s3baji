# ESP32-S3 AI 电子吧唧

目标板为 Waveshare ESP32-S3-Touch-AMOLED-1.75C。本机工程目录为 `C:\Users\Administrator\Documents\work\esp32电子吧唧\esp32s3baji`。

本项目让圆屏角色跟随电脑上的 AI Agent 工作：**客户端决定主题，实际模型决定角色，生命周期事件决定姿态；额度、上下文与电量独立显示。**

ESP32-S3 固件默认启动近景像素白龙娘。手指在圆屏上移动时她会看向触点，点按额头会播放一次摸头表演。固件保留 G1 色条、触摸十字和电源诊断入口。浏览器原型可通过本机预览桥跟随 Codex CLI 事件或 BAJI 快照。[板级接口与上板检查](docs/06-hardware-and-environment.md)

## 先看这三个入口

1. [产品与范围](docs/00-product-and-scope.md)：首版做什么，每条需求如何认定完成。
2. 选“工作中”看白龙娘搬桌开机，坐定后点她的小屏幕。在“口袋 → 小剧场”可以一起欢呼、逗尾巴和喂糖和好。[打开圆屏交互原型](design/badge-prototype.html) · [20 秒动作预览](design/gpt-pet/desk-story-preview.webp) · [动作设计与技术调研](docs/10-character-acting.md) · [接入 Agent 事件](design/gpt-pet/README.md#agent-联动)
3. [开发顺序与验收](docs/07-roadmap-and-acceptance.md)：G0–G5，下一步从板卡独立诊断开始。

[Codex 画面预览](design/prototype-desktop.png) · [Claude 画面预览](design/prototype-claude.png) · [用量详情](design/prototype-details.png) · [手机宽度预览](design/prototype-mobile.png)

## 完整规格

| 文档 | 解决的问题 |
| --- | --- |
| [00 产品与范围](docs/00-product-and-scope.md) | 产品定义、需求、首版边界、信息真实性 |
| [01 系统与数据](docs/01-system-and-data.md) | Codex / Claude 接入、状态机、多会话、实际模型与额度 |
| [02 代码架构](docs/02-architecture.md) | 模块、任务、接口、内存、失败路径、代码实现顺序 |
| [03 通信协议](docs/03-protocol.md) | JSON、CRC、握手、序号、完整快照、重连和互动意图 |
| [04 屏幕与互动](docs/04-screen-and-interaction.md) | 像素布局、信息层级、主题、触摸与休眠 |
| [05 角色与动效](docs/05-art-and-motion.md) | 静态姿态、动效分期、动画制作、资源格式、授权 |
| [06 板卡与环境](docs/06-hardware-and-environment.md) | 1.75C 硬件、MotoNav 驱动移植、构建与实机核验 |
| [07 开发与验收](docs/07-roadmap-and-acceptance.md) | 工作包、顺序、指标、风险关闭 |
| [08 验证记录](docs/08-validation-log.md) | 已测试的结果与没有测试的部分 |
| [09 决策记录](docs/09-decisions.md) | 为什么采用这些技术选择 |

## 当前工程

```text
main/                       宠物播放、触摸输入和可选 G1 板级诊断
components/baji_pet/         网页原画生成的 RGB565 动作与视线素材及局部绘制
components/baji_scene_damage/ 记录、合并待更新的物理像素区域
components/baji_vsync/       面板扫描同步信号及时间戳
components/baji_board/       应用侧板级接口和条带双缓冲
components/baji_display/     CO5300 QSPI 与 DMA 传输
components/baji_peripherals/ 共享 I2C、CST9217、AXP2101
components/baji_board_profile/ 固定板型的引脚和尺寸
components/baji_core/        C 状态投影、序号与 TTL、姿态选择
contracts/                  后续 UI、资源、输入、传输的接口草案
host/baji_bridge/           Python 分帧、适配器类型与浏览器的只读状态投影
protocol/                   JSON Schema + 17 条人工场景消息
assets/                     4 角色 × 12 姿态、来源与许可、动效配方
design/                     可点击的本地圆屏原型和预览图
tools/                      环境诊断、构建、回放、准备检查
tests/                      可移植 C 状态机断言用例
docs/                       开发规格与验证记录
reference/original-template 原 HelloWorld / README / 测试备份
```

工程使用的 48 张美术源文件已包含在 `assets/ai-girls`，保留上游原始字节与逐文件 SHA-256。GPT 网页模型另外内嵌一张立绘用于离线绘制。设备角色 UI、资源包和 Agent 传输按 G2–G3 接入。

## 克隆后预览

```powershell
git clone https://github.com/gargamelzhao0-del/esp32s3baji.git
cd esp32s3baji
Start-Process .\design\badge-prototype.html

# 可选：使用 Python 3.10+ 回放 Agent 状态，打开终端打印的地址
python tools/agent_preview.py --replay protocol/fixtures/codex-flow.jsonl
```

浏览器演示无需安装依赖；回放桥仅使用 Python 标准库。协议和素材校验需要 `python -m pip install -r host/requirements-dev.txt`，随后运行 `python tools/check_preparation.py`。Python 测试运行 `python -m unittest discover -s host/tests -v`；互动逻辑测试运行 `node --test tests/gpt-pet-play.test.cjs tests/gpt-pet-work.test.cjs`。

## 在本机运行

以下命令在工程根目录的 PowerShell 运行。主机协议校验需要 `host/requirements-dev.txt` 中的依赖，固件构建使用单独的 IDF Python 环境。

```powershell
# 只读环境诊断，不打开 COM 口
.\tools\doctor.ps1

# 协议 Schema、消息编码、素材完整性
python -m pip install -r host/requirements-dev.txt
python tools/check_preparation.py

# 串口分帧的离线单元测试
python -m unittest discover -s host/tests -v

# 构建 1.75C 电子宠物固件
.\tools\build.ps1

# 生成演示帧文件，不向设备发送
python tools/replay.py protocol/fixtures/codex-flow.jsonl --output .local/codex-demo.frames
```

打开 `design/badge-prototype.html` 即可预览，不需要 Web 服务。可附加 `?scene=claude`、`?scene=cross-provider`、`?scene=offline`、`?scene=unknown&page=1` 直接演示特定场景。

构建脚本为中文目录临时映射英文盘符并调用 CMake/Ninja。固件和烧录参数位于命令打印的 `build-firmware-*` 目录，构建结束后脚本释放映射。每个构建目录独立生成 `sdkconfig`。

VS Code 已选用 ESP-IDF 5.5.4。普通 `idf.py build` 和 IDE 的构建按钮没有脚本的中文路径处理，建议使用上述 PowerShell 入口。Docker/QEMU 配置仍为模板。

## 本轮验证与下一步

协议、资源和浏览器原型的既有离线检查，以及本轮固件构建与板级检查结果，见[验证记录](docs/08-validation-log.md)。

用 `.\tools\build.ps1 -BoardCheck` 可以单独构建色条、触摸十字和电源诊断固件。使用 IDF 开发终端时也可在 `menuconfig → Electronic badge` 中启用 G1。角色节拍和局部刷新可通过 RAM 基准独立复测。[上板检查步骤](docs/06-hardware-and-environment.md) · [角色互动基准](tests/device/pet_runtime/README.md)

设备内置待机、视线与摸头画面。Agent 实时额度和上下文、更多动作、续航和整机功耗仍在后续阶段。

## 素材来源

角色姿态来源于 CrossPet 固定提交及其声明的上游设定，使用范围与署名见 [原始美术许可](assets/ai-girls/sources/crosspet/LICENSE.md)。当前方案定位个人非商业原型；代码与美术分别管理授权。仓库内的 [素材来源记录](assets/ai-girls/catalog.json) 保留本工程使用文件的来源、版本和校验信息。
