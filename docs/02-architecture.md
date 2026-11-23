# 代码架构、边界与实现次序

## 技术选择

固件基于 ESP-IDF 5.5.4。板级驱动使用 IDF 的 SPI、I2C、GPIO 与 FreeRTOS API。候选角色 UI 可在 G2 接入 LVGL 9。电脑端采用 Python，先实现命令行 bridge 与适配器，再加托盘。设置和映射使用 JSON，协议规范使用 JSON Schema 2020-12。

主固件由 `main` 打开板卡，启动像素角色应用，结束后回收板卡。G1 诊断保留独立入口。板级驱动可以单独接受实板与宿主测试，角色素材与变化区域算法可以脱离 SPI、GPIO、I²C 进行像素验证。[板级实现](06-hardware-and-environment.md) · [实板回归](../tests/device/pet_runtime/README.md)

## 接入一个应用画面

1. 在应用组件中持有场景状态、板卡句柄和已提交的画面状态，向 `baji_runtime_run` 提供绘制回调。回调在显示所有者任务上执行，接收单调微秒时间、最新触点及按下计数。短按即使在两帧之间结束，计数和按下位置也会保留。
2. 由应用选择变化区域。用 `baji_board_paint_round_regions` 一次提交该帧的全部区域，绘制函数须填满板级回调给出的实际矩形。最后一笔 DMA 成功后再提交逻辑状态并清空变化区域。发生部分传输错误时保留这些区域，下次使用最新场景覆盖。
3. 在 `next_us` 返回下一次变化的绝对时间。需要逐扫描跟随视线时返回当前时刻；纯静态信息页可返回 `INT64_MAX`；页面完成时返回 `BAJI_FRAME_DONE`。运行时会提早等待 TE，遇到信号中断时使用软件时钟，并在信号恢复后重新同步。
4. Agent 或其他后台任务可以在自己的锁内发布最新快照，然后给 `baji_runtime_config_t.changed` 指定的二值信号量。应用负责这个信号量的生命周期；多次通知会合并，绘制回调负责读取最新数据。该机制也能唤醒静态画面。账户数据采集与设备传输可以独立接入。[运行时接口](../components/baji_runtime/include/baji_runtime.h) · [实际应用](../components/baji_pet_app/pet_app.c)

## 当前与目标目录

| 路径 | 当前内容 / 后续职责 |
| --- | --- |
| main/app_main.c、main/board_check.c | 启动并清理板卡，绘制色条和触点，记录内存与电源读数 |
| components/baji_graphics、baji_scene_damage | 约定物理像素矩形、RGB565 回调和有界变化区域，接口独立于板卡 |
| components/baji_board、baji_display、baji_peripherals、baji_vsync | 板卡生命周期和条带缓冲、面板传输、触摸与电源、扫描同步 |
| components/baji_runtime | 管理显示调度与触摸读取任务，等待应用期限或外部事件，处理重试及退出 |
| components/baji_deepseek | 默认鲸鱼娘应用。将触屏操作映射为检视台命令，按动画和气泡的下一次变化调度 RGB565 局部绘制 |
| components/baji_pet、baji_pet_app | 载入并校验素材，绘制 RGB565 像素；上层播放器处理动作、触点与视线 |
| components/baji_core | 已实现纯 C 状态投影、序号去重、连接 TTL、指标 TTL、姿态选择 |
| contracts/baji_ports.h | 资源、输入、UI、传输接口草案。硬件接口使用 `baji_board.h` |
| protocol | 已有严格 Schema、完整快照与边界场景 |
| host/baji_bridge | 已有 USB 分帧与适配器类型接口；真实宿主适配器待实现 |
| host/config.example.json | 演示配置，真实适配器默认关闭，串口不猜测 |
| assets | 角色源清单和动效配方；当前设备用素材包在 `components/baji_pet/assets` |
| design | 可离线打开的圆屏交互原型 |
| tools / tests | 环境诊断、构建、协议检查、回放、状态测试 |
| reference/original-template | 原 HelloWorld 和原测试的备份 |
| docs | 工程规格、开发顺序、验收和事实记录 |

`baji_board` 聚合显示与外设驱动，硬件引脚集中在 `baji_board_profile`。角色渲染器使用 `baji_graphics` 的数据约定。运行时和播放器通过板级接口提交画面。`-BoardCheck` 构建仅选入板级诊断需要的项目组件；产品与实板角色基准链接同一个应用组件。

## 模块职责与失败路径

| 模块 | 输入 → 输出 | 关键约束 / 失败处理 |
| --- | --- | --- |
| baji_board | 1.75C 显示、触摸和电源 → 应用板级接口 | 管理条带缓冲和外设生命周期，触摸与电量共享一条 I2C；业务通过接口获取能力和错误 |
| baji_protocol | 有界帧 → 完整快照 DTO | UTF-8 / JSON / CRC / 字段 / 深度 / 数量全部校验后再提交 |
| baji_transport | USB 字节 → 收包队列 | 不调用 LVGL；分片/粘包/噪声恢复；只保留最新合法快照 |
| baji_core | 快照 + 单调时钟 → view model | 纯逻辑，无 malloc、无硬件、无 LVGL、无账号读取 |
| baji_assets | 角色 / 姿态 ID → 图像描述与缓存 | 白名单路径；缺资源 → 本角色 idle → 中性内置图形 |
| baji_ui | view model → LVGL 对象更新 | 对象复用，比较前后差异；只在 UI 所有者上下文操作 |
| baji_input | 触点 → 单击/滑动/长按意图 | 明确互斥和防抖；不直接调用 Agent |
| baji_power | PMIC / 闲置 → 亮度与休眠建议 | 不伪造电量；不在 UI 线程访问慢速总线 |
| baji_settings | 设置 → NVS | 合并写入、2 s 防抖；启动配置版本迁移与默认恢复 |
| bridge/session_store | AgentEvent → 每会话运行状态 | 处理回合、并行工具、子 Agent、迟到事件和去重 |
| bridge/selector | 会话 + 前台 + 固定意图 → 选中会话 | 固定 > 明确前台 > 最近活动；保留选择依据 |
| bridge/usage | 官方指标 → metrics | 每桶单独 TTL；账号一致；失败退避，unknown 可恢复 |

板级运行接口返回 `esp_err_t`。`baji_board_open` 通过能力信息分别报告触摸和电源探测结果。后续资源、协议与 UI 层按各自接口区分未就绪、数据损坏、内存不足等情况，并统计 CRC 错误、非法帧、丢帧和资源失败。

## 任务与内存所有权

建议的任务划分是起点，栈大小和优先级须经实测调整：

| 任务 | 建议职责 | 起始约束 |
| --- | --- | --- |
| USB RX | 读取、拆帧、投递合法数据 | 高于资源加载，4 KiB 有界接收缓冲 |
| 状态/UI | 接收最新快照、投影、处理触摸意图 | 单一任务持有显示接口 |
| 资源加载 | 从 Flash 预取下一姿态 | 限制打开的资源数量，失败时使用保底图形 |
| 设备监测 | 电池、闲置等慢速采样 | 当前诊断应用每 5 秒读取电源状态，PMIC 与触摸共享 I2C 总线 |

当前驱动的显示句柄由创建它的任务持有，条带驻留内部 DMA 内存。G2 接入 UI 时由同一所有者提交绘制，图像解码和资源读取在绘制临界区外进行。

快照队列长度 1，覆盖旧快照可防止积压；触摸/命令队列长度 8，命令保留 ID；边界事件日志在电脑处理，不让设备重建整个 Agent 历史。

## 内存与刷新预算

这些是预算，不是实机成绩：

| 项目 | 数量级 |
| --- | --- |
| 全屏 RGB565 | 466 × 466 × 2 = 434,312 B，约 424 KiB |
| 两个全屏缓冲 | 约 848 KiB |
| 当前 466 × 32 行局部 RGB565 双缓冲 | 59,648 B，约 58.25 KiB |
| 320 × 320 RGB565 单帧 | 200 KiB |
| 320 × 320 RGB565 + A8 单帧 | 300 KiB；两帧约 600 KiB |
| 24 帧 RGB565 动画 | 约 4.69 MiB；带 A8 约 7.03 MiB |
| 目标 PSRAM 常驻用量 | 初期 < 4 MiB，至少留 2 MiB 余量 |
| 内部 DMA 缓冲 | 每块需连续 29,824 B，启动时分别分配并检查结果 |

不把全部角色和全部动画解码进 8 MB PSRAM。首版加载当前姿态与待切换姿态；帧动画阶段从板载 Flash 顺序读取，丢帧优先于阻塞触摸。中文字体做固定 UI 文案子集，并预留英数与未知字形；型号文本限长省略，详情可分页。

首版目标：角色变化 10 fps 即可；UI 动画最多约 30 fps；状态数值 1 Hz；快照常态 2 s 一次。状态变更目标 200 ms 内发出、端到端 P95 < 500 ms，均需 G3/G4 实测。

## 代码从哪里开始写

1. G1 板级固件已具备色条、触摸十字、PMIC 和内存诊断，下一步上板核对。
2. G2 实现 assets_open 和 UI 主屏，输入离线 fixture；确认固定锚点、裁切、字体和保底。
3. G3 把 Python framing 规格移植到 C，接入 USB 与 core；串口录制回放测乱序/超长/断连。
4. G4 在电脑端实现 session_store、selector，再分别接 Codex 和 Claude；最后才接账号额度。
5. G5 增加设置、电源、打包、自启动与恢复文档。

固件快照解析与 C model 的映射要集中在一个函数：百分比转换为千分位时先检查 finite 和范围，再四舍五入；unknown 设置 known=false。原始 JSON 不给 UI 保留指针，字符串复制进有界 DTO。当前 C 核心只投影主屏主额度桶；多桶详情、会话列表和身份字符串由协议 DTO 与 UI 层保存。

投影器比较 session.id + session.turn_id，发生变化时增加本地 identity_revision；相同回合的周期快照保持不变。这样刚切换到另一个已结束回合时可重新触发一次结束效果，又不会每2秒重复庆祝。不能用不断增加的seq代替这个身份版本。

## 版本与可回退

项目在仓库中维护 `sdkconfig.defaults`、驱动源码和板卡记录。当前固件只依赖锁定的 ESP-IDF 5.5.4。本地驱动移植依据及参考项目版本见[板卡记录](06-hardware-and-environment.md)。后续若引入受管组件，应提交对应的 `dependencies.lock`。原 HelloWorld 保存在 `reference/original-template`。
