# ESP32-S3 AI 电子吧唧

开发目录：`D:\esp32电子吧唧\esp32s3baji`。准备基线：2026-11-18。

本项目让圆屏角色跟随电脑上的 AI Agent 工作：**客户端决定主题，实际模型决定角色，生命周期事件决定姿态；额度、上下文与电量独立显示。**

项目包含可构建的 ESP-IDF 基础工程、带白龙娘的浏览器圆屏，以及能跟随 Codex CLI 事件或 BAJI 快照的本机预览桥。屏幕驱动、USB 固件联调和实机帧率验证仍待完成。

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
| [06 板卡与环境](docs/06-hardware-and-environment.md) | 本机 SDK、BSP 候选、中文路径构建、硬件核验 |
| [07 开发与验收](docs/07-roadmap-and-acceptance.md) | 工作包、顺序、指标、风险关闭 |
| [08 验证记录](docs/08-validation-log.md) | 已测试的结果与没有测试的部分 |
| [09 决策记录](docs/09-decisions.md) | 为什么采用这些技术选择 |

## 当前工程

```text
main/                       最小应用入口，不启动外设
components/baji_core/        C 状态投影、序号与 TTL、姿态选择
contracts/                  后续硬件/UI/资源/输入模块接口草案
host/baji_bridge/           Python 分帧、适配器类型与浏览器的只读状态投影
protocol/                   JSON Schema + 17 条人工场景消息
assets/                     4 角色 × 12 姿态、来源与许可、动效配方
design/                     可点击的本地圆屏原型和预览图
tools/                      环境诊断、构建、回放、准备检查
tests/                      可移植 C 状态机断言用例
docs/                       开发规格与验证记录
reference/original-template 原 HelloWorld / README / 测试备份
```

工程使用的 48 张美术源文件已包含在 `assets/ai-girls`，保留上游原始字节与逐文件 SHA-256。GPT 网页模型另外内嵌一张立绘用于离线绘制。新的 board / UI / transport / assets 驱动组件按 G1–G3 建立。

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

以下命令在本目录的 PowerShell 运行。IDF Python 已有需要的校验库，不必修改系统 Python。

```powershell
# 只读环境诊断，不打开 COM 口
.\tools\doctor.ps1

# 协议 Schema、消息编码、素材完整性
& 'C:/Espressif/tools/python/v6.1.1/venv/Scripts/python.exe' tools/check_preparation.py

# 串口分帧的离线单元测试
& 'C:/Espressif/tools/python/v6.1.1/venv/Scripts/python.exe' -m unittest discover -s host/tests -v

# 构建准备工程，不下载 BSP，不烧录
.\tools\build-preparation.ps1

# 生成演示帧文件，不向设备发送
& 'C:/Espressif/tools/python/v6.1.1/venv/Scripts/python.exe' tools/replay.py protocol/fixtures/codex-flow.jsonl --output .local/codex-demo.frames
```

打开 `design/badge-prototype.html` 即可预览，不需要 Web 服务。可附加 `?scene=claude`、`?scene=cross-provider`、`?scene=offline`、`?scene=unknown&page=1` 直接演示特定场景。

**本机中文路径有实际工具链兼容问题。** 构建脚本临时映射空闲英文盘符，直接执行 CMake/Ninja，再移除映射。源码和产物始终在原开发目录。本次生成在 `build-preparation-z/esp32s3baji.bin`，133,184 字节。不要将这份无屏幕功能的准备固件当成完整产品刷入设备。

原 `.vscode` 设置保留，选用 ESP-IDF 6.1.1。原 Docker/QEMU 配置仍是模板，不是本次验证基线。普通 `idf.py build` 和 IDE 默认构建按钮尚未应用本工程的中文路径处理方式，优先运行上面的脚本。

## 本轮验证与下一步

已通过：ESP32-S3 准备工程构建；11 项 Python 分帧与状态投影测试；7 项互动逻辑测试；17 条消息 Schema / 编解码；5 个非法 Schema 样例；48 张姿态 SHA-256 / 单帧 / 透明信息检查；浏览器场景与 430 px 布局检查。C 状态断言用例已提供，但尚未在宿主机或设备上执行。

下一步只推进 G1：确认板卡型号和端口，在本机 6.1.1 下验证候选 BSP，依次点亮屏幕、校验触摸、PSRAM、SD 和 PMIC；通过后再把这里的角色与协议接进去。

尚未完成：BSP/LVGL 实机集成、固件 JSON 解析与 USB 驱动、真实 Hook 安装、账号额度采集、SD 资源转换、续航与功耗实测。所有模拟值均明确为演示。

## 素材来源

角色姿态来源于 CrossPet 固定提交及其声明的上游设定，使用范围与署名见 [原始美术许可](assets/ai-girls/sources/crosspet/LICENSE.md)。当前方案定位个人非商业原型；代码与美术分别管理授权。仓库内的 [素材来源记录](assets/ai-girls/catalog.json) 保留本工程使用文件的来源、版本和校验信息。
