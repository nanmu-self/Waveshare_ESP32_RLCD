# AGENTS.md — AI 协作指南

本文件面向在此仓库工作的 AI 编码代理（也适用于新接手的人类）。先读完再动手。

## 项目是什么

**希娜 Syna**：微雪 ESP32-S3-RLCD-4.2（400×300 黑白圆屏，16MB Flash / 8MB PSRAM）桌面助手，配电脑端 Reporter。板子首页显示日历，另有电脑性能、AI Agent（Codex）状态与额度、天气、Syna 对话/待办、电脑列表、关于页；电脑端通过局域网 UDP 上报数据。当前版本 1.0.0 候选版。

| 目录 | 内容 | 语言/技术栈 |
| --- | --- | --- |
| `xiaozhi/` | 固件：小智上游补丁（overlay）+ 板级源码 + 构建入口 | C/C++，ESP-IDF 6.0.2，LVGL |
| `reporter/` | 电脑端状态服务（性能采集、Codex 状态/额度）、PySide6 窗口 | Python 3.11 |
| `installer/` | Windows / macOS 一体安装器 | Python + PowerShell |
| `simulator/` | LVGL 桌面模拟器（不用硬件即可预览 UI） | C，CMake + SDL2 + LVGL 9.5.0 |
| `tests/` | 固件 UI 主机测试 | Python 驱动 + clang |
| `release/` | 发布签名工具、公钥、第三方许可证 | — |

权威构建文档：`docs/BUILD.md`；已知限制与验收状态：`docs/RELEASE_CHECKLIST.md`。

## 硬性约束（红线，违反即返工）

1. **Python 必须 3.11.x**。依赖按 3.11 锁定（PySide6 6.8.3、psutil 7.0.0），3.14 等新版本装不上。本机若只有新 Python，用 `py -3.11` 或静默装 3.11.9（3.11 系列最后一个带 Windows 安装器的版本）。
2. **固件构建路径必须是无空格纯 ASCII**（如 `C:/syna`），中文路径会破坏 ESP-IDF 工具链。
3. **上游版本锁死**：`xiaozhi/upstream.lock` 指定 78/xiaozhi-esp32 @ `5df5b7f`，`idf=6.0.2`。不得擅自升级上游——`apply_ui_overlay.py` 的补丁锚点会失效。
4. **板型唯一**：`waveshare/esp32-s3-rlcd-4.2`。不得将固件刷到其他型号。
5. **改 UI 要双写**：`xiaozhi/overlay/main/boards/waveshare/esp32-s3-rlcd-4.2/ui_*.c`（固件真身）和 `simulator/src/`（模拟器镜像）需人工保持同步，`simulator/src/assets/` 与 overlay 的 62 个 `ui_*.c` 一一对应（生成脚本只写模拟器一侧时，固件那份需手动同步）。
6. **不采集凭证**：项目哲学是只读接口，不读提示词/代码/文件内容/API Key，不抓登录态。新功能延续此原则。
7. **作者脚本的路径是示例**：`xiaozhi/build_windows.ps1`、`enter-idf.ps1`、`simulator/scripts/*.ps1` 内含作者本机布局（D:/syna-toolchains 等），复用前先改参数，通用环境优先用 `docs/BUILD.md` 手动命令。
8. **产物与备份不入库**：`.gitignore` 已覆盖 `dist/`、`release/`（reporter 的）、`*.venv`、`备份*/`、`full-flash.bin` 等。安装器备份含私人数据，绝不提交。

## 固件链路（xiaozhi/）

构建模型：本仓库**不含**上游源码。流程 = 检出上游 → 分两阶段打 overlay → 在上游目录用 ESP-IDF 编译。

```powershell
# 1. 检出上游（按 upstream.lock）
git clone https://github.com/78/xiaozhi-esp32.git; git checkout 5df5b7fb4da2b4d80e2b7f87285ec1f8a9ca565c
# 2. 打补丁（两个 phase 都要跑）
python <本项目>/xiaozhi/apply_ui_overlay.py <上游目录> --phase source
idf.py -DIDF_TARGET=esp32s3 '-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;syna.sdkconfig.defaults' -DBOARD_NAME=esp32-s3-rlcd-4.2 reconfigure
python <本项目>/xiaozhi/apply_ui_overlay.py <上游目录> --phase components
# 3. 编译（在上游目录）
python scripts/build.py waveshare/esp32-s3-rlcd-4.2 --name esp32-s3-rlcd-4.2 --language zh-CN
```

- 板级代码全在 `overlay/main/boards/waveshare/esp32-s3-rlcd-4.2/`：UI（`ui.c`、`ui_*.c` 位图页面 + LVGL）、服务（`reporter_service`、`api_balance_service`、`settings_portal_service`、`todo_service`、`environment_service`）、板文件 `waveshare-s3-rlcd-4.2.cc`。
- 语音行为：单轮对话补丁（`apply_ui_overlay.py` 的 `single_turn()`）——回答期间不接受语音打断，TTS 播完回待机。
- **刷机走安装器"保留配置升级"**（自动备份、只写应用分区、保留 Wi-Fi 与电脑绑定），不要裸 esptool 覆盖配置分区。

## 模拟器（simulator/）

- 依赖：CMake + SDL2 + LVGL 9.5.0 完整源码放 `simulator/vendor/lvgl-9.5.0`（不入库）。
- 运行：`simulator/scripts/run.ps1`（作者路径需先调整）；键盘 Space 切性能页、C 开电脑列表、R 循环 Agent 状态，映射板载 KEY 按键。
- 改 UI 先在这里验证（秒级、无硬件），再同步到 overlay。
- 图标/静态素材：改 `simulator/assets/source/` → 执行 `UI Assets: Regenerate`（`generate_assets.ps1`）重生成 1-bit C 数组。

## Reporter（reporter/）

```powershell
py -3.11 -m venv reporter\.venv
reporter\.venv\Scripts\python.exe -m pip install -r reporter\requirements-build.txt
# 测试（在仓库根目录；macOS 专用用例自动跳过）
reporter\.venv\Scripts\python.exe -m unittest discover -s reporter -p "test_*.py" -v
# 源码运行（窗口，关闭进托盘）
reporter\.venv\Scripts\python.exe reporter\reporter.py
# 一键打包：PyInstaller → 自检 → IExpress
powershell -ExecutionPolicy Bypass -File reporter\build_release.ps1 -Version 1.0.0
# 产物：reporter\dist\SynaReporter.exe（绿色版）+ reporter\release\SynaReporter-Setup-*.exe
```

- 入口 `reporter.py`（supervisor + worker 架构），UI 在 `windows_ui.py`，额度数据源目录在 `quota_providers.py`，Mac 专用 `macos_*.py`（勿在 Windows 环境动）。
- 运行数据：`%LOCALAPPDATA%\AIAgentPanel\`（reporter.json 身份/配对令牌、reporter.log）。
- 接口：HTTP 8765 仅本机诊断（`/api/v1/health|status|pairing`）；UDP 8766 局域网发现与数据。
- 首次构建前确保 `reporter/release/` 目录存在（IExpress 需要）。
- 打包机有代理时 pip 用 `--proxy http://127.0.0.1:7897`。

## 协议契约（改数据必读）

板子 ↔ Reporter 的 UDP JSON 字段名是**协议**，固件按字段名解析（`reporter_service.cc`）：

- 性能：`performance.gpu_percent` 等（null → 屏幕显示 `--`）
- Codex 额度：`codex_short_remaining` / `codex_week_remaining` / `codex_login_required` / `codex_quota_stale`——**板子只认字段名不关心数据源**，换数据源（如其他厂商额度）只需改 Reporter 并复用这些字段

- 音乐/媒体字段（`media_*`）已在两侧彻底删除；想要音乐功能请看 `music-feature` 分支。

改任何一侧字段名必须同步另一侧；新增字段先查 `reporter_service.cc` 的解析。额度数据源可切换/扩展：目录 `reporter/quota_providers.py` + `reporter.py` 的 `@register_quota_provider` 注册制，板子不感知数据源，切换与新增均不涉及固件。

## 平台坑位清单（Windows，血泪换来的）

- **wmic 在 Win11 24H2 已移除**：查系统信息用 PowerShell `Get-CimInstance`（见 `reporter.py` 的 `windows_thermal_numbers()`）。
- **ctypes 调 PDH 必须设 `restype = ctypes.c_uint32`**：PDH 状态码（如 `PDH_MORE_DATA = 0x800007D2`）按有符号解读会变负数，被误判为失败。
- **GPU 使用率**走 PDH `GPU Engine(*)\Utilization Percentage`，用 `PdhAddEnglishCounterW` 规避中文系统计数器本地化；聚合口径 = 按引擎类型分组求和取最大（任务管理器口径）。
- **核显温度无独立传感器**：纯核显机器用 CPU 温度近似（同晶片）；识别显卡时必须忽略远程桌面虚拟适配器（向日葵 OrayIddDriver、RDP、ToDesk 等，见 `_VIRTUAL_ADAPTER_MARKERS`），否则会否决真核显；Intel Arc / AMD RX/Pro 是独显，不套用 CPU 温度。
- 板载设置门户（"召唤Syna-sama"页，含 API 余额自定义 HTTP 查询）：**BOOT 长按** → 板子开热点 `Syna-0721` → 电脑连热点 → 浏览器访问 `http://192.168.4.1`。屏幕上**没有**任何 IP 显示；该简易客户端只支持简单鉴权，做不了 AK/SK HMAC 签名。

## 代码与提交约定

- 文档、注释、UI 文案一律**中文**；代码标识符英文。
- 版权头：原创文件 `Copyright (c) 2026 黑沐. MIT`；从上游改的文件保留上游声明。
- Python 风格：紧凑但清晰，类型注解（`float | None`），dataclass 做快照，线程用 `threading.Lock` 保护。
- 提交前跑：Reporter 全量测试 + 若动了 UI 则跑模拟器和 `tests/check_ui_branding.py`（需要 clang）。
- 固件主机测试需 C++ 编译器：设 `SYNA_VCVARS`（vcvars64.bat）与 `SYNA_FIRMWARE_SOURCE`（已打补丁的上游源码）。
- 版本号：根目录 `VERSION`，安装器用 `-Version` 参数；发布流程见 `docs/RELEASE_CHECKLIST.md`。

## 最近变更（流水账，非“未提交”；接手前先 `git log -5` 与 `git status` 确认）

- 首页**音乐卡片已替换为日历卡片**（本轮，已编译通过、待刷板）：删除 `ui_update_media` 及 UI 层全部 media 静态量，右卡改为当月网格（标题 `◀ 2026年 10月 ▶ 周六`、星期表头、6×7 日期、今天反色块）。底图里蚀刻的音乐元素由单块 `make_white_mask(screen, 184, 74, 204, 172)` 遮白，卡片圆角边框保留。日期用 `time()/localtime` + 纯算法（Sakamoto 星期、闰年表），**未校时（年 < 2020）时整块留空**；`ui_update_clock()` 每秒只做一次整数比较，跨零点才重画。翻月走 MCP 工具 `self.panel.calendar`（`next`/`prev`/`today` → `CustomLcdDisplay::StepPanelCalendar` → `ui_calendar_step`），返回主页时自动回到本月——板子两个按键都被占用，翻月只能靠语音。新增 `ui_font_11_cjk`（Noto 11px，仅 9 个字形）做小号星期表头；`generate_assets.ps1` 仍只写模拟器一侧（已知问题）。Reporter 侧 media 管道同轮删除：`media_monitor.py` / `macos_media.py` 及对应测试删除，`reporter.py` / `windows_ui.py` / `native_ui.swift` / `reporter_service.*` 的 media 字段全部移除，依赖减掉 `winrt-Windows.*` / `websocket-client` / `pycaw`，Windows 状态窗口的“网易云音乐”卡并入全宽 CODEX 卡。保留音乐的完整实现在分支 `music-feature`（指向 78f98ae）。**底座 `screen_base.png` 里仍蚀刻着音乐元素**（运行时被遮白块盖住），要彻底清理得改 `generate_assets.ps1` 重新生成底图，铺开面较大暂未做。
- `reporter/reporter.py`：CPU 温度改用 `Get-CimInstance`（wmic 失效修复）；GPU 走 PDH GPU Engine（核显可用）；纯核显机器 GPU 温度用 CPU 温度近似。配套测试已加（`test_reporter.py`）。
- `xiaozhi/.../ui.c` + `simulator/src/ui/ui.c`：关于页新增 STA IP 动态显示（`#ifdef ESP_PLATFORM`，模拟器占位）。
- 额度数据源可切换（本会话新增，已实测）：`reporter/quota_providers.py` 数据源目录 + `@register_quota_provider` 注册表；新增 `ArkQuotaCollector`（火山方舟 `GetCodingPlanUsage`，V4 签名纯标准库移植，Node 参考向量交叉验证，`session`→短周期、`weekly`→周额度，`monthly` 不展示；云端频控阈值未公开，实测 ~6 QPS 突发可用但长周期配额未知，轮询 300 秒 + 失败指数退避，陈旧缓存 600 秒防屏面跌--）；设置窗口新增“额度来源”对话框（目录驱动，AK/SK 存 `%LOCALAPPDATA%\AIAgentPanel\reporter.json`，保存后可一键重启 worker）；`status()` 新增 `quota_provider` 字段（仅本机 HTTP 诊断，UDP 协议不变）。真实 AK/SK 实测接口打通。未来加 OpenCode 等套餐 = 1 个收集器类 + 1 条目录描述，UI/工厂自动适配，未知 provider 回退 Codex。
- 额度重置倒计时 + 月额度上屏（同会话续）：UDP 新增 `codex_month_remaining`/`codex_month_resets_at`；dashboard 额度区改为整体自绘的紧凑网格（每行 = 标题(5h/周/月) | 40×5px 细进度条 | 右对齐百分比(14px) | 右对齐倒计时(14px)），倒计时极简格式 `31m`/`9h`/`2d9h`（<1分 `<1m`，≥10天退化为 `12d`），未知自动隐藏；agent 状态从黑色胶囊图改为标题行内联徽标 `● 工作中`（底图标题区遮白重绘，状态资产不再引用，DONE 闪烁保留）；`ui_update_codex_quota` 8 参；check_quota_stale 宿主测试同步（三种紧凑倒计时、填充宽度基准 38）；固件全量编译通过；模拟器截图验证。

- 套餐来源上屏（同会话续）：UDP 新增 `quota_provider`（id）+ `quota_provider_title`（目录 board 字段，如"火山方舟"/"Codex"，未知 provider 回退 id）；dashboard 空白区新增"套餐 | 火山方舟"键值行（左标签 + 右对齐值，右缘与网格对齐，旧 reporter 不发时板端回退 "Codex"）；双写已同步；固件全量编译通过；UDP 探测脚本端到端验证载荷。剩余：刷板实测。

- OpenCode Go 套餐源（同会话续，未实测密钥）：`OpenCodeQuotaCollector`（GET opencode.ai/console/api/go/status，Bearer Key，仅标准库；接口返回各窗口 limit/used 微美分，剩余% = (limit-used)/limit 现场计算，`fiveHour`→短周期、`week`→周、`month`→月，`resetsAt` ISO 8601 转 Unix 秒，月档重置即套餐续费时间）；套餐元信息（到期/续订状态/取消标志/各窗口美元金额）经 `plan_info()` 暴露到本机诊断 `opencode_plan` 字段；目录新增 opencode 条目（字段 opencode_api_key），设置对话框/工厂/板端“套餐 | OpenCode”展示全部自动适配；401/403 → `opencode_auth_failed`；轮询 300 秒同方舟。固件零改动（板端来源名走 quota_provider_title）。注意：console 接口的鉴权方式未实测，若真实 Key 返回 401 需核对 Bearer 之外是否还需要别的凭证。
- 以上改动已在本机实测通过（Reporter 已打包验证：CPU 温度 27.9°C、核显使用率 ~10%、GPU 温度回退生效）。

### 天气页面（和风天气，本会话新增，待固件编译刷板）

- 新增 `weather_service.cc/h`（板子直连和风 HTTPS API：实况 + 3 天预报，30 分钟刷新，失败 60 秒重试；X-QW-Api-Key 头认证）。
- 和风 API 无条件 gzip：vendor 了 zlib contrib 的 puff.cc/puff.h（zlib 许可）剥头解压，已验证 C++ 编译干净。
- 位置：默认自动 IP 定位（ip-api.com → 经纬度 → 和风 GeoAPI 换 LocationID，失败退 myip.ipip.net，再失败降级用旧缓存），LocationID 缓存 NVS 24 小时；设置门户可切手动填 LocationID。Key 走设置门户存 NVS（`wx-key`/`wx-mode`/`wx-loc`），不进仓库。
- UI：`PAGE_WEATHER` 插在 Syna 与 About 之间（KEY 短按循环五页），温度大字用新字体 `ui_font_40_regular`（Arimo 40px）；中文字库并入 GB2312 全集（12905 字符）保证任意城市名可显示；页面右上角按和风免费版条款标注"和风天气"署名。
- 双写已同步：overlay 与 simulator；模拟器 `AI_PANEL_START_PAGE=weather` 可预览，mock 数据在 main.c；截图延时新增 `AI_PANEL_SCREENSHOT_DELAY_MS` 环境变量（默认 200ms，签名页 2 秒需调大）。
- 验证状态：模拟器构建 + 截图通过、tests/check_ui_branding.py 通过（toggle 计数已改四次、clang 响应文件绕过 Windows 命令行长度限制、排除 LVGL SDL 驱动免链接 SDL2）、weather_service.cc g++ 桩语法检查 -Wall -Wextra 通过；**固件尚未刷板实测**（本机有 IDF 6.0.2：`C:/Espressif`，构建走 `C:/syna/build-session.ps1`）。
- 和风 Key 测试可用：7f0bda…4afa 是老版免费开发版（devapi.qweather.com 直连可用，无需专属 Host）；免费额度 1000 次/天，30 分钟刷新仅消耗 48 次/天。
- 视觉增强（第二轮）：14 个 1-bit 程序化图标（`generate_weather_icons.py` 生成，双写两端，const 数组存 Flash）；和风 icon code 映射天气图标（weather_icon_for）；参数图标（温度计/水滴/风向标/室内）；顶栏星期时钟（kWeekdayNames + ui_update_clock 分支）；城市旁 AQI（/v7/air/now 免费可用：aqi+category）；右侧第 4 行室内温湿度（板载 SHT 经 ui_update_environment 存档）；底栏 WiFi+电量标注+更新时间（署名移至关于页）；温度用新字体 ui_font_40_bold（Arimo Bold，fonts/Arimo-Bold.ttf sha256 d7a8b1…6ba）。
- 未采纳（1-bit 屏硬件限制）：灰度/彩色/淡色水印、28px 动态城市字库（~1.3MB）、降水概率（和风 daily 接口无 precipProb 字段，仅 hourly 有）。
- 已知坑：JSON→bash→Python 多层转义会把 `\0` 吃成 NUL 写进源码（g++ 报 "null character(s) preserved"），改文件一律写临时 .py 脚本执行；模拟器 ui_show_weather 曾因 clock_label 在 make_label 前赋 NULL 导致时钟不更新，已修。
- 视觉第三轮（本会话末）：14→35 个图标（16px 预报小图标、8 方位风向箭头、WiFi 两态、UV）；风向箭头按 wind360 指下风向（西南风→东北，weather_wind_icon_for）；预报行温度范围条（0-40°C 映射轨道，实心段=当日区间）；右卡第 4 行紫外线等级（服务端 UvLevelText 映射）；室内对比移入左卡；底栏 WiFi 小图标+图形电池（框+填充+正极）；城市旁 AQI 格式"广州 [优] 44"。
- 排版第四轮：预报行重排“今天｜晴｜25°/12°｜💧降水量｜农历”，删除温度条；新增 lunar.cc/h（solarlunar 表 1900-2100，锚点验证通过）；左下角 WiFi 图标改设备 IP（station_ip_text 复用，模拟器 AI_PANEL_MOCK_IP 可预览）；图形电池旁恢复 11px 百分比。降水用 daily 的 precip 降水量（mm）替代——和风 daily 接口无 precipProb 概率字段。

### 局域网管理页（本会话新增，固件已编译通过、待刷板实测）

- 需求：不再每次连热点才能配置；设备联网后浏览器直接打开 `http://板子IP:8080` 进入管理页，需密码登录。
- `settings_portal_service` 重写：`Start()` 注册 `IP_EVENT_STA_GOT_IP`，首个 STA IP 后拉起独立 httpd（端口 **8080**，ctrl_port 32769，避开热点门户的 80；stack 6144，max_open_sockets 4）。常驻运行，与热点门户（80 端口）并存不冲突。
- 管理密码：NVS `wifi-config` 存 `portal-salt`（16B 随机）+ `portal-hash`（PSA SHA-256(salt+密码)，恒时比较），永不回显。首次访问 LAN 页强制设密（4-64 位）；连续错 5 次锁 60 秒。会话为 RAM 内单令牌（esp_fill_random 32B，esp_timer 24h 过期，Cookie `syna_session` HttpOnly SameSite=Lax，重启失效）。热点门户保持免密（物理按键门槛 + 忘密恢复通道），但两端都可通过新增“管理密码”栏改密。
- LAN 专属路由：`/`（登录分流）、`/panel/login`（登录/首设）、`/panel/logout`、`/panel/wifi`（保存凭证→`SsidManager::AddSsid`→1.5s 后 `WifiManager::StopStation()+StartStation()` 热切换，无需重启）、`/scan`（STA 阻塞扫描，与后台定时扫描撞车时重试一次）。`/panel/config`、`/panel/save` 两端共用，LAN 侧需会话；config 响应新增 `portal`（ap/lan，驱动同一页面 JS 分支）与 `has_password`。设置页 HTML 单份共用，JS 按 portal 分支（AP 走 /submit//exit，LAN 走 /panel/wifi）。认证失败均返回 200/401 + JSON，前端 toast。
- UI：关于页 IP 行连接时显示“IP 地址：x.x.x.x（管理页 :8080）”，双写已同步（overlay + simulator）；中文字由 GB2312 全集字库覆盖。
- 编译链路修复：`apply_ui_overlay.py` 所有 read/write_text 补 `encoding='utf-8'`（GBK 代码页机器必炸）；本机构建需 `PYTHONUTF8=1` + 清 `MSYSTEM` + `IDF_PYTHON_ENV_PATH` 指向 py3.11 venv（C:/syna/build-session.ps1）。**本机有 IDF 6.0.2（C:/Espressif），AGENTS 此前“无环境”记录过时。**
- 验证：固件全量编译通过（xiaozhi.bin 3.8MB）；tests/check_panel_buttons、check_quota_stale、check_single_turn 全 PASS（SYNA_FIRMWARE_SOURCE=C:/syna/xiaozhi）；模拟器 ui.c 用 xtensa gcc -fsyntax-only 过；check_ui_branding 本机无 clang 跳过（UI 改动仅 About 一行文本）。**尚未刷板实测。**

### 关于页署名更新（本会话新增，固件已编译通过、待刷板）

- 仓库现由楠木（github.com/nanmu-self）维护，面向用户的署名已全部替换：作者楠木、QQ/微信同号 157884200、仓库链接 nanmu-self/Waveshare_ESP32_RLCD。
- 固件双写：关于页 10 行文案重排（删 B 站行，换 QQ/微信，新增“基于 xiaozhi-esp32 二次开发”，致谢加黑沐）；Syna 主页 28px 品牌大字“希娜 Syna · 楠木”（generate_ui_fonts.py 字符集同步，`py -3.11 simulator/assets/generate_ui_fonts.py` 重生成，需 Pillow+fonttools，锁 requirements.txt）。
- 同步改：reporter/windows_ui.py 关于对话框（作者/联系方式/仓库链接/REPOSITORY 常量）、installer/windows/installer_ui.py 作者行、README.md 作者与许可段、installer/macos/使用说明.md、tests/ui_branding.c 断言（黑沐→楠木）。
- **有意保留**：全部源文件版权头、THIRD_PARTY_NOTICES、installer Licenses 中的“黑沐”署名——MIT 义务（保留原作者版权声明），不应抹去；描述文字改为“基于黑沐的希娜 Syna 二次开发”。

## 文档索引

- 构建：`docs/BUILD.md`（唯一权威）
- 验收/已知限制：`docs/RELEASE_CHECKLIST.md`、`docs/BUILD_VALIDATION.md`
- Reporter 使用与 Codex 适配细节：`reporter/README.md`
- 模拟器：`simulator/README.md`
- Windows 安装器：`installer/windows/README.md`
- 第三方许可：`THIRD_PARTY_NOTICES.md`
