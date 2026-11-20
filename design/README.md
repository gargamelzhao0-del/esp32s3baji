# 圆屏交互原型

在 GPT 角色页选“工作中”，看白龙娘走过去把电脑桌拖回来。坐好以后点小显示器，她会指给你看。“口袋 → 小剧场”还可以邀请她欢呼、逗逗尾巴和递和好糖。[打开原型](badge-prototype.html) · [20 秒圆屏实录](gpt-pet/desk-story-preview.webp) · [角色动作方案](../docs/10-character-acting.md)

用浏览器直接打开本地 HTML 就能体验手动演示。读取 Agent 事件时，通过可选的本机 Python 桥进入联动页面。[打开圆屏](badge-prototype.html) · [Agent 联动说明](gpt-pet/README.md#agent-联动)

默认进入 GPT 白龙娘的角色主页。点她的手心可以邀她击掌，长按后拖动可以引来萤火虫。试着碰左翅、尾巴或连续碰两次鼻尖。玩过以后，等她安静几秒，会看到她再挥一挥刚才的手，或看看萤火虫停过的位置。圆屏下方的按钮也支持键盘操作。[白龙娘动作与实现](gpt-pet/README.md) · [观看动画](gpt-pet/gpt-pet-preview.webp)

圆屏左侧可以参与当前工作，右侧“口袋”收着工作玩法、陪玩项目和发饰。试试阅读时亲手翻页、写入时盖爪印、执行工具时同敲三拍。手动页面的指标为演示数据，联动页面使用来源提供的数值。Claude、DeepSeek、Gemini 使用已有的姿态图。[观看工作互动短片](gpt-pet/agent-pocket-preview.webp)

可选URL参数：
- `?scene=claude`：Claude Code + Opus + 等待确认。
- `?scene=cross-provider`：Claude Code主题 + DeepSeek角色。
- `?scene=offline`：断开连接。
- `?scene=unknown&page=1`：未知额度详情。
- `?page=3`：设置页面。

`prototype-desktop.png`、`prototype-claude.png`、`prototype-details.png`、`prototype-settings.png` 与 `prototype-mobile.png` 为本轮浏览器预览。屏幕元素的尺寸和固件实现以 [屏幕规格](../docs/04-screen-and-interaction.md) 为准。

素材署名与许可见 [CrossPet原始说明](../assets/ai-girls/sources/crosspet/LICENSE.md)。导出或分享原型时保留这一说明和所引用的原作者信息。
