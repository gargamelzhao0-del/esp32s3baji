# 状态核心测试

浏览器逐帧检查使用 Python Playwright 和 Chromium。在项目根目录执行 `python tools/audit_pet_motion.py --out motion-review --check`，也可以用 `--browser` 指定已安装的 Chrome，用 `--case cart` 单独回放搬桌。每段使用固定随机种子，以每秒 30 步推进实际渲染器。输出包含关键帧 PNG 和每帧一行的 CSV。检查模式会报告脸部像素消失、身体网格翻面、握紧后手掌脱离把手或笔杆的具体帧。[逐帧检查入口](../tools/audit_pet_motion.py)

CSV 的 `max_stretch` 是原画上不透明三角形局部变换的最大奇异值，已扣除镜头与舞台的统一缩放。`flipped` 是翻面的三角形数。`opaque_pixels`、`isolated_holes` 和 `changed_rgba_pixels` 从最终 612 × 540 像素计算，分别记录透明度至少为 128 的像素数、四邻域均不透明的单像素透明孔，以及相邻两帧的 RGBA 变化数。遮挡的语义需结合关键帧检查。

角色动画的回归可在开发目录运行 `node --test tests/gpt-pet-play.test.cjs tests/gpt-pet-work.test.cjs tests/gpt-pet-stage.test.cjs tests/gpt-pet-motion.test.cjs`。用例覆盖脚步接触、连续工具事件、开工中断、指屏、击掌庆祝、闹别扭、减少动效、抚摸与抛接。[角色表演测试](gpt-pet-stage.test.cjs) · [触摸与抛接测试](gpt-pet-motion.test.cjs)

像素吧唧的动作导演与互动回归在 `design/pixel-badge` 运行 `node --test authoring/motion.test.cjs authoring/gestures.test.cjs authoring/forehead-invite.test.cjs authoring/star-game.test.cjs`。覆盖待机微动作调度、亲密度、状态气泡、戳脸/挠痒、额头邀请和星萤。Chrome 冒烟与 466 实屏另测。[动作导演](../design/pixel-badge/MOTION-V2.md)

`core_state_test.c` 是不依赖 FreeRTOS / LVGL 的 C 断言用例，覆盖重复帧不延长在线状态、跳号快照恢复、指标过期、零与未知、非法状态、本轮结束不自动成功、成功动效不被周期快照重复触发及重新建连。

需要 **宿主机 C 编译器**，不能使用默认生成 Xtensa / RISC-V ELF 的交叉编译器生成 .exe 后在 Windows 直接执行。本轮已进行核心的 ESP32-S3 工程编译，未执行此断言程序。

在具备本机 cc 的环境中，可按以下命令编译运行（输出目录先创建）：

```sh
cc -std=c11 -Wall -Wextra -Werror \
  -I components/baji_core/include \
  components/baji_core/baji_state.c tests/core_state_test.c \
  -o .local/core-state-test
.local/core-state-test
```

Windows 可使用配置好的 MSVC / LLVM 宿主工具链执行等价命令。后续应纳入CI的原生平台任务；真实硬件的驱动测试另放G1/G2，不与纯逻辑断言混淆。
