# DeepSeek 鲸鱼娘（深蓝女仆）— 像素风格主力开发版（Version 1 · Pixel v1）

本目录存放第二角色 **DeepSeek 鲸鱼娘（深蓝女仆）** 的 **像素风格主力素材包（Version 1）**，严格对齐 `design/pixel-badge/` 的 `184×184` 圆形 AMOLED 硬件屏规格与 `138×132` 索引色图集标准，可直接用于下一阶段多角色引擎集成，且完全不改动现有第一角色（GPT 白龙娘）生产代码。

## 核心规格一览

- **单帧尺寸**：`138×132 px`（与 GPT 白龙娘图集 1:1 兼容；在 `184×184` 表盘内落点 `originX = 23`，`full` 模式 `originY = 18`，`close` 模式 `originY = 22`）
- **色彩编码**：**40 色「Ocean-Maid v2 深海女仆」索引调色板**（`PNG mode="P"`，`transparency=0`，提亮发尾海蓝→荧光青绿渐变与傲娇红腮，自动检测强制保证 **0 单像素针孔 + 0 水平裂缝**，适配 ESP32-S3）
- **零断层动画工艺**：全部 6 套动画序列均基于 **6 组专属透明抠图互动原画** + **连续 C¹ 平滑网格变形（Smooth Mesh Warp）** 制作，彻底杜绝静态切块白缝、原道具乱入及五官贴片污染
- **双视距裁切（基于头心对齐 `680×680` 母版）**：
  - `FULL_CROP = (-30.0, -24.0, 5.15)`：完整呈现左弯呆毛、蕾丝发箍、蓝蝴蝶结、鲸鱼鳍、女仆围裙小鲸鱼与分叉鲸尾，且顶不碰 `>_ DEEPSEEK`、底不碰状态栏
  - `CLOSE_CROP = (76.0, -6.0, 3.55)`：特写聚焦灵动海洋蓝眼瞳、呆毛、头鳍与领结
- **专属舞台背景**：**「汐光海港 (Tide Harbor)」四时段海洋场景**（晨雾 `dawn` / 蔚蓝晴海 `day` / 珊瑚灯塔 `dusk` / 荧光夜海 `night`），在中下段 `y=88..136` 特设发光海雾与水波亮带，彻底解决深蓝女仆裙在深色背景下的“暗撞暗”问题

## 文件清单

### 1. 静态 12 态图集（Indexed-P，40 色）
| 文件 | 尺寸 | 说明 |
| :--- | :--- | :--- |
| `deepseek-states-atlas.png` | `1104×528` | **主接入图集**（8 列双视距，前 2 行 `full` 12 态 + 后 2 行 `close` 12 态） |
| `deepseek-states-full.png` | `552×396` | 4×3 全身 12 态速览图集 |
| `deepseek-states-close.png` | `552×396` | 4×3 特写 12 态速览图集 |

### 2. 6 套多帧像素动画序列图（Indexed-P，均含 `full` + `close` 双视距）
| 文件 | 帧数 / FPS | 特色互动说明 |
| :--- | :--- | :--- |
| `deepseek-idle-anim.png` | 8 帧 · 6 FPS | 待机平滑呼吸律动 + 呆毛轻颤 + 鲸尾轻摆（0 裂缝平滑变形） |
| `deepseek-feed-rice.png` | 12 帧 · 7 FPS | **特色互动① 喂白米饭**：真实手拿筷子捧碗鼓腮咀嚼 → 双手抱出搞笑**巨型蓝边大白饭盆 + 星星眼** |
| `deepseek-sulk-weigh.png` | 10 帧 · 7 FPS | **特色互动② 称体重“才不胖!!”**：双手死死捂住红线复古体重秤 + 爆红脸颊 + 飞溅汗珠与 `💢` 跺脚傲娇 |
| `deepseek-ahoge-splash.png` | 8 帧 · 8 FPS | **特色互动③A 弹呆毛喷水花**：呆毛受压回弹 → 呆萌 `"o"` 嘴惊呆脸 + 冲天发光青蓝水柱四溅 |
| `deepseek-jellyfish-play.png` | 8 帧 · 7 FPS | **特色互动③B 逗荧光小水母**：双手抬起互动 + 发光半透明青色小水母上下律动与海洋微光 |
| `deepseek-deepsleep-nap.png` | 8 帧 · 4 FPS | **特色互动③C 抱浪花抱枕休眠（DeepSleep）**：闭眼怀抱浮世绘浪花抱枕轻柔呼吸 + 透明 `Zz` 气泡漂浮 |

### 3. 原生像素道具与 `16×16` UI 徽章图标（`props/`）
- `props/rice-bowl.png` (`28×24`)：冒热气白米饭碗 + 木筷
- `props/mega-rice-basin.png` (`46×36`)：带小鲸鱼纹样的巨型蓝边大白饭盆
- `props/weight-scale.png` (`30×22`)：指针顶格红线的复古薄荷绿体重秤
- `props/jellyfish-2f.png` (`48×26`)：2 帧触手摇曳发光小水母
- `props/wave-pillow.png` (`34×24`)：浮世绘浪花抱枕（DeepSleep 休眠道具）
- `props/icons-16x16.png` (`112×16`)：7 枚原生 `16×16` 像素徽章图标（`rice-bowl` / `scale-sulk` / `splash-drop` / `jellyfish` / `whale-tail` / `wave-sleep` / `star-think`），适配表盘左侧 `19×19` 发光徽章框
- `deepseek-props-sheet.png`：道具与图标 3× 放大检视总表

### 4. 元数据、复现脚本与交互式预览
- `deepseek-pixel-manifest.json`：完整 40 色表、裁切参数、帧率、状态顺序、道具及 SHA-256 校验清单
- `build_deepseek_assets.py`：一键确定性构建脚本（同步生成 Version 1 像素包与 Version 2 高清对齐包，并自动验证 `mode=P` / `transparency=0` / `pinholes=0`）
- `preview-badge-showcase.png` / `preview-4-skies.png`：圆形表盘实景展示图与四时段对比度证明图
- `review.html`：**交互式双版本对比预览页**（支持实时播放 5 套像素动画、切换 12 态与 `full/close`、切换 4 时段海洋背景，并与 Version 2 正常高清动画设定并排对比）

## 重新构建命令

```powershell
python esp32s3baji/assets/ai-girls/pixel/deepseek/v1/build_deepseek_assets.py
```

## 版权与署名说明

- 本像素资产包基于仓内 `assets/ai-girls/state-poses/deepseek/*.webp`（CrossPet）二次像素化重绘与动画扩展，遵循 **CC BY-NC-SA 4.0** 协议。
- 署名：**原型「溟月」上善无形 · 女仆设定 ZipZipPipe · 12 态基础立绘 CrossPet**。
