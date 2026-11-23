# 将 Agent 状态同步到圆屏

电脑负责解释 Agent 事件。吧唧接收当前选中会话的显示快照，在本地选择姿态并绘制。GPT 白龙娘和 DeepSeek 鲸鱼娘均支持此模式，角色仍在构建时选择。未连接时，设备可以独立播放和响应触摸。

## 构建与配对

在工程根目录打开 PowerShell，使用 Python 3.10+ 和工程支持的 ESP-IDF 5.5.4。

```powershell
.\tools\build.ps1 -Character GPT
# 或构建含像素和高清模式的鲸鱼娘
.\tools\build.ps1 -Character DeepSeek
python -m pip install -r host/requirements-wireless.txt
```

构建脚本打印产物目录。无线产品从 Flash 启动，使用对应目录中 `flash_args` 的完整镜像集合，鲸鱼娘还依赖 `hdmotion.hda` 分区。构建本身不操作设备。`-Offline` 可单独构建离线角色，`-BoardCheck` 构建板级诊断。

在已运行无线固件的设备上，关闭占用串口的监视器，通过板载 USB 配对。只有使用 `--show-ap` 才会把热点口令打印到当前终端。每块板的配对密钥和热点口令在首次启动时独立生成。

```powershell
python tools/agent_sync.py pair --port COM21 --show-ap
```

默认配置文件为当前用户的 `~/.baji/pairing.json`，含两项凭据。多块设备可分别指定 `--profile`，在 `pair` 和 `send` 中使用相同路径。

## 选择链路

| 链路 | 操作 |
| --- | --- |
| 设备 Wi-Fi 热点 | 在电脑系统设置中连接配对命令显示的 `BAJI-xxxxxx` 和口令。默认地址为 `http://192.168.4.1`。 |
| 同一路由器 | 通过 USB 执行 `python tools/agent_sync.py network --port COM21 --ssid "你的 2.4 GHz 网络"`，按提示输入密码，重启吧唧。使用串口启动日志中 `Agent endpoint at` 打印的地址。回到设备热点可执行 `python tools/agent_sync.py network --port COM21 --ap` 后重启。 |
| 蓝牙 BLE | 通过下方扫描命令查找 `BAJI-S3`，把扫描结果中的地址或设备 UUID 传给 `--ble`。多台设备同名时，使用目标设备的地址及其配对配置。 |

```powershell
python tools/agent_sync.py scan --seconds 5
python tools/agent_sync.py send --wifi http://192.168.4.1 --input host/examples/agent-state.jsonl --interval 1.5
# 将地址换成实际扫描结果
python tools/agent_sync.py send --ble AA:BB:CC:DD:EE:FF --input host/examples/agent-state.jsonl --interval 1.5
```

示例依次展示思考、阅读、等待和成功。屏幕顶部为模型名，底部的 `QUOTA LEFT` 为剩余额度，`CTX USED` 为已用上下文。示例首条上报额度已用 15%，圆屏显示剩余 85%。估算值带 `~`，数据缺失或超过有效期后显示 `--`。鲸鱼娘在像素和高清模式选择对应状态姿态，白龙娘用视线和状态图标反馈，屏上触摸继续生效。

## 接入自己的 Agent

每个桥进程读取一种输入格式。现有 BAJI v1 快照、Codex CLI 的 JSON 事件，以及下列通用格式都能通过文件或标准输入发送。集成自有 Agent 时，在生命周期回调中写一行 JSON 并立即刷新标准输出。

```json
{"type":"agent.state","session":"session-1","turn":"turn-7","model":"local-model","phase":"tool","activity":"reading","quota":{"used_pct":15,"ttl_ms":30000},"context":{"used_pct":41,"ttl_ms":12000,"age_ms":550,"quality":"estimated"}}
```

| 字段 | 约定 |
| --- | --- |
| `session`、`turn`、`model` | `session` 必填，`turn` 和 `model` 可省略。会话、轮次或模型变化时更新选中对象。发送 `{"type":"agent.state","session":null}` 可清除选择。模型标签按完整 UTF-8 字符截取至 32 字节。 |
| `phase` | 选中会话时必填。支持 `idle`、`working`、`tool`、`waiting`、`ended`、`error`、`limited`。 |
| `activity`、`outcome` | 活动为 `none/thinking/reading/writing/running/searching/compact`。结果为 `unknown/succeeded/failed/cancelled`。默认分别为 `none` 和 `unknown`。 |
| `quota`、`context` | 可选，均表示**已用百分比**，范围 0–100。提供数据时需同时提供 `ttl_ms`（1–3600000 毫秒）。`age_ms` 表示数据采集至当前事件的年龄，默认 0。`quality` 为 `reported` 或 `estimated`。 |

每条通用事件完整替换上一条。若要沿用仍然有效的指标，要带上更新过的年龄或剩余有效期。原始提示词、工具参数和对话正文均不会进入无线快照。对缺少配额接口的 Agent，可以直接省略指标。

```powershell
codex exec --json "概述当前工程" | python tools/agent_sync.py send --wifi http://192.168.4.1
python tools/agent_sync.py send --wifi http://192.168.4.1 --input protocol/fixtures/codex-flow.jsonl --interval 1
```

Codex 事件若省略模型名，可在 `send` 增加 `--model` 并填入该次实际运行的模型名。桥在输入打开期间约每 2 秒发送心跳，突发事件按最新状态合并。文件回放用 `--interval` 逐项观察。标准输入结束后，最后状态获确认，桥即退出。设备连续 8 秒未收到有效快照会显示离线。

## 模块责任

| 模块 | 责任与依赖 |
| --- | --- |
| `host/baji_bridge` | 解析提供方事件，合并状态，扣除指标年龄，签名，重连并发送最新快照。新增 Agent 的适配器可放在主机端，固件继续共用同一协议。 |
| `baji_wifi`、`baji_ble` | 分别管理 ESP Wi-Fi、HTTP 和 NimBLE GATT 外设，只依赖 `baji_link_endpoint_t` 的握手与收包接口。 |
| `baji_agent`、`baji_core` | 校验消息、管理发送者绑定与顺序、原子更新快照，按本机单调时钟计算指标有效期和工作姿态。 |
| `baji_agent_port`、`baji_agent_ui` | 向显示任务提供只读快照和变化通知，复用模型标签、状态图标及两项指标。显示内容发生变化时标记局部区域。 |
| `baji_pet_app`、`baji_deepseek` | 各自定义状态到角色表演的映射，由原来的显示任务处理触摸、面板时序、合成和 DMA 提交。 |
| `baji_connectivity`、`main` | 管理组件生命周期、USB 配对、NVS 和降级。板卡先申请显示 DMA 缓冲。无线构建把显示主任务固定在 CPU1，Wi-Fi 驱动、NimBLE 主机和 HTTP 收包在 CPU0 运行。 |

Wi-Fi 提供 `GET /v1/hello` 与 `POST /v1/state`，成功提交返回 HTTP 204。BLE 服务为 `95a10000-79bb-4bc9-97b7-b1468fc445a7`，第一段为 `95a10001` 的特征用于读取握手值，`95a10002` 用于带响应写入。默认 ATT MTU 下每次写入 2 字节分片头和至多 18 字节数据。最后一片通过整帧验证后才确认。

两种链路共享同一会话绑定。每个 BAS1 快照含设备的 16 字节挑战值、8 字节发送流标识、递增序号、状态和 HMAC-SHA256。超时换发挑战值，避免旧快照在重连后重新生效。同一时刻只有一个活跃发送流，从另一个进程接管需等待旧流超时。HMAC 验证来源与完整性。Wi-Fi HTTP 和普通 GATT 承载的显示字段本身没有端到端加密，配对密钥通过物理 USB 取回。

## 验证入口

```powershell
python -m unittest discover -s host/tests -v
python tests/wireless/run.py
python components/baji_wifi/tests/run.py
python components/baji_deepseek/tests/run.py
python tests/display/run.py
```

前两条覆盖主机发包和生产 C 接收代码的跨语言往返。验证实际射频链路时，同一台设备分别用热点、路由器和 BLE 回放上面的文件，观察模型、额度及触摸，停桥 8 秒再从另一条链路接管。无线开启后的实机帧率需用对应角色与显示模式独立测量。

驱动参考 [Espressif Wi-Fi API](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/api-reference/network/esp_wifi.html)、[Espressif NimBLE](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/api-reference/bluetooth/nimble/index.html) 和 [Bleak GATT 客户端](https://bleak.readthedocs.io/en/stable/api/client.html)。
