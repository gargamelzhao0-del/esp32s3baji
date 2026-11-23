# DeepSeek 鲸鱼娘（深蓝女仆）— 正常动画高清备用版（Version 2 · Normal HD v1）

本目录存放第二角色 **DeepSeek 鲸鱼娘（深蓝女仆）** 的 **正常高清动画备用素材包（Version 2）**。当前阶段以像素版（`assets/ai-girls/pixel/deepseek/v1/`）为主力开发，本目录为后续大屏、Web 端、Live2D / Spine / Canvas-2D 矢量/骨骼高清动效预留标准化资产与绑定规范。

## 目录内容

| 文件 / 目录 | 说明 | 规格 |
| :--- | :--- | :--- |
| `poses/*.png` (12 张) | 12 种 Agent 状态的**头心对齐 + 脚底落线**标准高清关键帧 | `680×680` RGBA PNG（`face_x = 320`, `feet_y = 656`，`happy` 保留跳跃抬升 `-14px`），状态切换零水平抖动 |
| `signature-poses/*.png` (6 张) | **6 组精准去背 + 头心对齐的专属特色互动透明关键帧**（`feed_munch` / `feed_mega_basin` / `sulk_weigh` / `ahoge_spout` / `jellyfish_play` / `wave_pillow_nap`，含水母半透明荧光解预乘） | `680×680` RGBA PNG（可直接用于高清动画播放与骨骼叠层） |
| `normal-12-poses-aligned.png` | 12 态对齐总览校验表（带头心中轴线与脚底基线参考） | `1010×848` RGBA PNG |
| `deepseek-signature-interactions-sheet.png` | **6 大特色互动高清设定图**（吃白米饭、抱巨型大白饭盆、按体重秤红线傲娇“才不胖”、弹呆毛喷水花、逗荧光水母、抱浪花枕头摸鱼） | 高清 2×3 角色动作设定表 |
| `deepseek-normal-rig-turnaround.png` | **2 头身三视图 + 身体/鲸尾拆件 + 4 种特写表情 + 5 大道具高清表**（严格匹配现有 12 态 2 头身比例，可直接供 Live2D/Spine 拆层） | 高清设定图 |
| `deepseek-normal-fullbody-sheet.png` | **2.5 头身全身三视图备选方案**（适合后续高分辨率竖屏或桌面端展示） | 高清设定图 |
| `rig-spec.json` | 2D 骨骼绑定与图层 Z-Order 规范（17 层层级、8 处枢轴坐标、5 套特色动作配方、12 态锚点与 SHA-256） | JSON |

## 12 态关键帧与 Agent 状态映射

1. `idle` — 待机呼吸 / 欢迎
2. `thinking` — DeepThink 深度思考
3. `reading` — 读文件 / 查代码
4. `writing` — 写代码 / 编辑中
5. `running` — 执行命令 / 构建中（含坚定鼓劲神情）
6. `searching` — 搜索 / 检索中（手持黄铜放大镜）
7. `asking` — 需要确认 / 等待输入
8. `happy` — 本轮完成 / 喂饭满足
9. `oops` — 报错 / 网络波动（`@_@` 蚊香眼）
10. `tired` — 限流 / 饿了（手捧空白米饭碗）
11. `compact` — 上下文压缩（坐行李箱努力压紧）
12. `sleeping` — DeepSleep 休眠 / 抱浪花枕头

## 版权与署名说明

- 基础 12 态原画源自仓内 `assets/ai-girls/state-poses/deepseek/*.webp`（CrossPet commit `8954eb8f7f69fcfb8d35e9deceeb78e10c804402`）。
- 遵循 **CC BY-NC-SA 4.0（署名-非商业性使用-相同方式共享 4.0）** 协议。
- 署名：**原型「溟月」上善无形 · 女仆设定 ZipZipPipe · 12 态基础立绘 CrossPet**。
