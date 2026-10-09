# Syna Reporter（Windows / macOS）

macOS 基础版已适配：双击 `start.command` 启动，或使用
`dist-macos/SynaReporter.app`。功能范围、登录启动和测试见 [macOS 使用说明](MACOS.md)。
Windows 状态窗口已按 Mac 版布局移植，2026-09-14 已完成首轮 Windows 本机验证并构建新 `dist/SynaReporter.exe`；使用方法及剩余验证项见 [Windows 窗口说明](WINDOWS-UI.md)。
下面的网易云、NVIDIA 指标及安装方式适用于 Windows。

Windows 当前主入口为 `installer/windows` 的一体安装器，已在作者电脑完成独立版安装验收。源码整理目录不包含预编译 EXE；请按 [构建说明](../docs/BUILD.md) 生成。

当前本地新版直接运行：

```text
dist\SynaReporter.exe
```

下列路径属于保留的 Reporter 单独打包脚本输出，不是本次统一发布入口：

```text
release\SynaReporter-Setup-1.0.0.exe
```

安装后的单文件程序自带 Python 运行时和全部依赖，安装后自动登录启动；窗口管理的 worker 异常退出时自动恢复，显式命令行后台模式保留 supervisor。安装程序只添加 Private 网络 UDP 8766 入站规则。开发者从源码启动：

```powershell
powershell -ExecutionPolicy Bypass -File .\reporter\start.ps1
```

首次运行会在 `reporter/.venv` 创建独立 Python 环境并安装 `psutil` 与 Windows Runtime 媒体接口。Reporter 监听：

- HTTP `8765`：`/api/v1/health`、`/api/v1/status`
- UDP `8766`：响应 `AI_PANEL_DISCOVER_V1`，同时返回电脑身份、Agent 状态和性能快照

稳定的 `reporter_id` 与电脑名称保存在 `%LOCALAPPDATA%\AIAgentPanel\reporter.json`。

Reporter 首次启动还会生成随机配对令牌及指纹。新开发板会自动保存首次发现的稳定 Reporter ID 与指纹，无需用户复制令牌；手动令牌仅用于旧固件兼容或高级过滤。本机诊断接口 `http://127.0.0.1:8765/api/v1/pairing` 仍仅允许本机访问，不向局域网返回明文令牌。

当前版本只开放只读状态接口，不读取提示词、代码、文件内容或 API Key。首次局域网测试若 Windows 弹出防火墙提示，只允许“专用网络”。

开发板与电脑必须连接同一个允许设备互访的 Wi-Fi 或手机热点。启动成功后可在电脑浏览器检查：

```text
http://127.0.0.1:8765/api/v1/health
http://127.0.0.1:8765/api/v1/status
```

当前 MVP 的性能数据包括 CPU、Memory、GPU、Disk 使用率、GPU 温度和实时网络速度。Windows CPU 温度优先读 psutil，失败时用 PowerShell `Get-CimInstance` 查询 ACPI 温度区（旧版依赖的 `wmic` 在 Windows 11 24H2 已移除，不再使用）；部分主板不开放温度区，仍不可用时返回 `null`，屏幕显示 `--`。NVIDIA GPU 指标通过 `nvidia-smi` 获取；无 NVIDIA 时 GPU 使用率走 Windows PDH `GPU Engine` 计数器（核显同样适用，且用 `PdhAddEnglishCounter` 避免 Windows 语言差异）；仅核显的机器 GPU 温度用 CPU 温度近似（同一晶片），Intel Arc / AMD 独显等分立显卡不套用，显示 `--`；识别时忽略向日葵、RDP 等远程桌面虚拟显示适配器。

## Codex Agent 状态

Reporter 优先从 Codex 本地 `logs_2.sqlite` 提取固定格式的任务开始／结束标记，结合进程 PID 与创建时间汇总实时活动数；不读取或上报日志正文。该日志格式与客户端版本相关。无可用标记时，回退到只读查询 `thread_history_1.sqlite` 中的 `thread_turns` 状态，并从 `state_5.sqlite` 取得对应工作区名称。存在 `inProgress` turn 时显示“工作中”，最近一个 turn 完成后短暂显示“已完成”，Codex 进程存在但没有活动 turn 时显示“空闲”，进程退出时显示“离线”。多个 Codex 会话同时运行时，`active_count` 返回活动会话数量。

该方式不依赖 VS Code Hook，也不读取 `thread_items`、提示词、对话、代码、工具参数或终端输出。Reporter 通过 UDP 只向屏幕发送汇总状态、活动数量和工作区最后一级名称。

正式安装版不再使用 VBS。安装程序写入系统启动项，启动隐藏的 supervisor；运行日志位于 `%LOCALAPPDATA%\AIAgentPanel\reporter.log`。它与 VS Code 进程相互独立，执行 `Developer: Reload Window` 不再是状态同步的必要步骤。

## 额度来源（Codex / 火山方舟，可扩展）

额度卡片的数据源可切换，状态窗口“额度来源”按钮里选择；协议字段名固定为 `codex_short_remaining` / `codex_week_remaining` / `codex_month_remaining` / `codex_short_resets_at` / `codex_week_resets_at` / `codex_month_resets_at` / `quota_provider` / `quota_provider_title` / `codex_login_required` / `codex_quota_stale`（额度为剩余百分比，重置时间为 Unix 秒，-1 表示未知；Codex 源无月额度；provider 字段供开发板展示套餐来源），开发板不感知数据源，切换无需刷固件。

- **Codex（默认）**：每 30 秒通过本机 Codex App Server 的 `account/rateLimits/read` 读取账户限额，把 `usedPercent` 换算为剩余百分比，并按 `windowDurationMins` 区分短周期额度和周额度。
- **火山方舟 Coding Plan**：每 5 分钟调用 `GetCodingPlanUsage`（火山引擎 V4 签名，仅标准库实现；云端接口频控阈值未公开，实测短时突发可用但长周期配额未知，故采用保守轮询并指数退避），`session` 档映射短周期、`weekly` 档映射周额度，`monthly` 暂不展示。需要在本机保存访问密钥（火山引擎控制台 → 访问控制 IAM → API 访问密钥），AK/SK 只写入 `%LOCALAPPDATA%\AIAgentPanel\reporter.json`，仅用于只读查询额度；未配置时屏幕显示 `--`，密钥无效时状态窗口提示“密钥无效”。

- **OpenCode Go 套餐**：每 5 分钟调用 `https://opencode.ai/zen/go/v1/usage`（Bearer Key 认证，仅标准库），`rolling` 档映射短周期、`weekly` 档映射周额度、`monthly` 档映射月额度，`resetsAt`（ISO 8601）换算为 Unix 秒；`percent` 按已用口径换算为剩余百分比（若实机验证发现口径相反，改 `_parse_snapshot` 一处即可）。Key 存本机 reporter.json；未配置/Key 无效时屏幕显示 `--` 并在状态窗口提示。

数据源目录在 `quota_providers.py`（纯数据：展示名、说明、需填写的字段），收集器在 `reporter.py` 内实现并挂 `@register_quota_provider` 注册；新增套餐（如 OpenCode）只需新增一个收集器类 + 一条目录描述，UI 与工厂全部自动适配，未知 `quota_provider` 值回退到 Codex 并记录告警。

这一过程只调用只读限额接口，不读取认证令牌、提示词、对话或代码。Codex 客户端升级后若本地 App Server 协议变化，需要同步更新 Reporter 适配器。

注意：Agent 卡的任务/工作区状态始终来自本机 Codex，与额度来源无关；切换到方舟后若本机未登录 Codex，额度照常显示，Agent 卡仍会提示登录 Codex（任务状态功能需要）。

Codex 未登录时 Reporter 会立刻清除旧额度，上报登录所需状态；屏幕显示“请登录”，两项额度显示 `--`。登录成功后最迟在下一次 30 秒轮询恢复。

## 网易云音乐与歌词

Reporter 优先从仅监听 `127.0.0.1:9223` 的本地 CDP 通道直接读取网易云的歌曲名、歌手、播放状态和真实进度；旧版客户端仍可用 Windows `GlobalSystemMediaTransportControlsSessionManager`。网易云 3.1.39 等新版禁用 Windows 媒体会话时，Reporter 会从窗口标题和本地 `playingList` 只读获取当前歌曲、歌手、歌曲 ID 与时长，因此普通快捷方式启动也不会整块丢失媒体信息。需要精确进度时执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\reporter\start_netease_precise.ps1 -Restart
```

脚本会先完整退出现有网易云进程，再以精确模式启动，并验证端口确实可用；验证失败会明确报错，不再出现“看似启动成功、实际没有媒体信息”的情况。以后手动退出网易云后也应使用这个脚本启动。

歌曲变化时，Reporter 优先从网易云本地缓存文件名解析候选歌曲 ID，再用歌曲名和歌手严格校验；找不到缓存匹配项时才调用网易云搜索接口。歌词按 LRC 时间戳匹配当前播放位置，网络不可用、歌曲无歌词或无法可靠匹配时显示“暂无歌词”，不会套用同名翻唱歌曲的歌词。歌曲 ID 和歌词会缓存到下一次切歌，正常播放时不会反复请求网络。

首页字体覆盖常用中文、日文平假名、片假名、半角片假名和 JIS 汉字，可动态显示中日文歌曲、歌手和单句歌词。当前只适配网易云音乐桌面客户端。如果网易云未运行、刚重启后尚未加载歌曲或未播放，屏幕显示“未播放”。

ESP32 的实时数据链路使用 UDP 请求/响应，因此不需要为 HTTP `8765` 创建 Windows TCP 入站规则；HTTP 接口只是本机诊断入口。不要开放公用网络防火墙权限，也不要在路由器上做端口映射。

开发板支持发现最多 8 台 Reporter、列表翻页、选择当前电脑并按稳定 Reporter ID 和记忆的指纹锁定当前选择；板卡自身以 Wi-Fi STA MAC 作为稳定 ID。该机制用于避免多电脑/多板误串，不等同于抵御恶意局域网攻击；带随机 nonce 的双向消息认证和撤销列表仍待后续安全加固。Agent 状态与额度现已接入 Codex，媒体与歌词当前仅支持网易云音乐桌面客户端。

构建发布包：

```powershell
powershell -ExecutionPolicy Bypass -File .\reporter\build_release.ps1 -Version 1.0.0
```
