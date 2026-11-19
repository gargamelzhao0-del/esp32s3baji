# USB 协议 v1

规范文件：`../protocol/baji-v1.schema.json`。实现参考：`../host/baji_bridge/protocol.py`。所有 fixture 均为人工演示数据，型号中的 demo 不是产品型号。

## 首版选择：完整快照

设备只需“当前应该显示什么”，不必重放全部工具事件。电脑在状态变化时合并发送，最短间隔 100 ms；平时每 2 s 发送一次完整快照兼作心跳。这样丢掉中间一帧后，下一帧仍足以恢复状态。

最大帧长 4096 字节，包括前缀、JSON、CRC 和 LF。字符串上限由 Schema 限字符数，最终还必须检查 UTF-8 字节总长。会话摘要最多 4 个、额度桶最多 4 个；更多内容在电脑端选择/分页。若过长，优先缩短摘要和可选标签，仍不合法则记录错误并保持上个快照，禁止截断 JSON。

## 字节帧

```text
@BAJI1:<紧凑 UTF-8 JSON>*<8 位 CRC32 十六进制>\n
```

CRC32 使用与 Python zlib.crc32 一致的 IEEE CRC-32，仅覆盖原始 JSON 字节，不包含前缀和星号。接收端按最后一个星号拆 CRC，字符串中的星号不影响解析。发送不使用 CRLF；日志行即使存在也不会通过前缀和 CRC 校验。

读到超过上限的行后丢弃到下一 LF，接收缓冲始终有界。拒绝 NaN、Infinity、重复键、不合法 UTF-8、未知消息类型与 Schema 不符内容；深度限制建议 8 层。CRC 是传输检错，不是身份认证。

设备业务串口启动后，应将常规调试日志转 UART / 禁用，减少干扰和带宽；前缀只是启动阶段日志共存的恢复机制。Flash/monitor 与 bridge 不同时占用同一串口，bridge 提供暂停与释放端口。

## 建连与来源

1. 设备每次启动生成 boot_id，USB 会话未绑定时每 1 s 发 hello。
2. bridge 收到 hello 后生成本连接 bridge_id，发送带对应 boot_id 的首次完整 snapshot。
3. 设备只在等待绑定阶段接受这个首次 snapshot，记录 bridge_id；随后只接受同一 boot_id / bridge_id。
4. 序号 seq 从 1 单调增加；重复或更小序号不刷新连接存活时间。允许跳号，因为每包都是完整状态。
5. 物理断开，或 8 s 无合法新快照后，清除绑定并回到 hello 阶段；UI 保留上次身份但标“连接已断开”。重新绑定后才重置核心序号状态。
6. bridge 重启必须重新等待 hello / 触发明确的重连流程，不能随意更换 ID 越过防旧包逻辑。seq 到上限前主动重连，不做回绕比较。

USB 单主机场景里的 ID 用于隔离启动与重连，不能证明发送者权限。Wi-Fi 版本必须在外层增加配对认证。

## 四类消息

| 类型 | 方向 | 用途 |
| --- | --- | --- |
| hello | 设备 → 电脑 | boot_id、固件版本、资源包、帧大小与实际能力 |
| snapshot | 电脑 → 设备 | 当前会话、选择依据、有限会话摘要、指标 |
| intent | 设备 → 电脑 | 切下一会话、固定/取消、定位电脑端会话 |
| ack | 电脑 → 设备 | 意图处理结果，按 request_id 对应 |

当前 Schema 是准备版合同。设备本地亮度、静音、动效设置不发给 Agent。电池来自设备本地，不由电脑 snapshot 虚构。

## Snapshot 字段

| 路径 | 含义 |
| --- | --- |
| session | 选中的会话对象；null 表示当前没有可靠会话 |
| session.client | 客户端主题标识 |
| session.turn_id | 当前回合的实际或适配器生成的局部标识；未知时null |
| session.provider / model_id | 实际来源；可 null |
| session.character_id / variant | 经过映射的角色和变体；设备本地白名单校验 |
| session.phase / activity / outcome | 会话阶段、活动细分、已证实的结果 |
| session.active_tools / active_subagents | 并行计数；0 不代表回合结束 |
| selected_by | pinned / foreground_session / recent_activity / none |
| metrics.quota[] | 每桶 used_pct、label、窗口秒数、重置剩余秒数 |
| metrics.context | 上下文占用，与额度严格区分 |
| metrics.tokens | input / output / cached；scope 明确 turn 或 session |

used_pct=null 时 quality 必须 unknown、age_ms=null；数字 0 是有效的已用 0%。已过期数字可以存在快照里作为历史，但 UI 主屏显示 —。设备计算新鲜度为 `age_ms + 距本帧接收的单调时间 < ttl_ms`，不能依赖设备 RTC 已联网校时。

reset_in_s 是发送时的剩余秒数；本地倒计时减去接收后时间，最低为 0。倒计时到零不自动“补满”额度，等待官方新数据。

## 意图的可靠性

request_id 唯一，带连接 ID 和目标 session_id。bridge 缓存近期 128 个结果并按 ID 去重，返回相同 ack。设备 500 ms 无 ack 可重试，最多 2 次；超时显示“电脑端未响应”。断线后不重放旧意图。

next_session 由电脑选择；pin_toggle 必须针对明确 session_id；open_host 仅调用已验证宿主的定位能力，不执行任意 URI 或 shell。首版没有 approve / run / send_prompt 等动作。电脑仍是高影响操作的确认位置。

## 负例与回放

检查脚本覆盖 Schema、CRC、中文、分片、粘包、超长行恢复、重复键、未知指标和多种角色组合。G3 还必须测试：ESP 重启时旧帧迟到、bridge 重启、日志夹杂、错误 CRC 连续灌入、串口被 monitor 占用、设备本地时钟持续运行超过回合长度、最新快照覆盖积压。

`tools/replay.py` 只把人工 fixture 编码成帧文件或 stdout，不打开 COM 口，也不模拟真实账号额度。
