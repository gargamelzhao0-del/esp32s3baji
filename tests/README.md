# 状态核心测试

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
