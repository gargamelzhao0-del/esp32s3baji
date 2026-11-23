# 固件像素角色

此组件将近景白龙娘的 RGB565 图像直接绘制到板级条带。内置黄昏背景、像素网格和离线仪表。待机采用原画的全部 43 帧，以 12 帧/秒播放；摸头采用全部 30 帧，以 16 帧/秒播放。每个姿势有五个水平、三个垂直视线位置，摸头闭眼期间沿用原画。额头热区随姿势的面部锚点移动。

封装内含 8 × 8 像素块的编号、共享像素行、各个姿势的局部眼部替换，以及相邻姿势、进入摸头和返回待机的变化区域。当前 BPT2 素材为 1,201,523 字节，解码后占用 2,984,716 字节 PSRAM，当前姿势的索引另占约 7 KiB 内部内存。每 32,768 个像素块使用一组 16 位行索引，两组通过相邻的地址范围共享 23,608 条像素行。相同画面比原 BPT1 省 951,996 字节常驻 PSRAM。解码器也支持 BPT1 的 32 位行索引。

角色绘制器依赖公共像素矩形和 RGB565 条带约定。由一个显示任务依次选择姿势、渲染条带，将已经提交的状态保留到整次绘制成功。角色公共头可由 C 或 C++ 应用直接引用。[角色接口](include/baji_pet.h) · [像素矩形与条带约定](../baji_graphics/include/baji_graphics.h)

## 构建与复核

正常固件直接嵌入已生成的 `assets/scene.bpz`，编译仅需 ESP-IDF。将 CMake 变量 `BAJI_PET_EXTERNAL_ASSET` 设为 `ON` 时，调用方可通过 `baji_pet_open_data` 提供同一份压缩资源，函数返回后调用方即可释放输入。

如需重新生成素材，安装 Python、Pillow、NumPy、Playwright 以及 Chromium，在仓库根目录运行：

```sh
python components/baji_pet/authoring/bake.py
python components/baji_pet/tests/run.py
```

导出器默认为 `authoring/reference.zip` 中锁定的两段动作。归档保留网页绘制脚本、近景图块、待机与摸头的锚点以及原作者声明。不参与这两个动作的全身取景在图集中为透明。参考输出使用 Chrome 157.0.8103.39、Playwright 1.65.2、Pillow 12.2.0 和 NumPy 2.4.4。可以用 `--browser` 指定 Chromium 路径，用 `--output` 和 `--golden` 将输出写到临时目录。

`--web design/pixel-badge` 改用当前工作区的网页与角色图集。对于原始网页渲染器，导出器在图集写入画布后插入跟随面部锚点的凝视层；带有 FaceFX 的版本调用网页自有的眼部绘制。更新锁定参考时显式加上 `--freeze-reference`。脚本检查每个睁眼姿势的目光确实变化、闭眼期间没有新增瞳孔。

`tests/web.crc` 包含直接取自浏览器最终 466 × 466 画面的 1,095 个 RGB565 校验值。宿主测试使用 GCC 或 Clang 编译生产绘制器，分别以整屏和局部窗口组成同一画面，覆盖循环、跳帧、触摸中断及单像素裁剪。角色与字体署名应随固件和素材分发。[素材与授权](assets/CREDITS.txt)
