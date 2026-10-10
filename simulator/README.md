# LVGL 桌面可视化模拟器

该目录用于在 Windows 和 VS Code 中预览 ESP32-S3-RLCD-4.2 的 400×300 黑白界面，不需要连接或烧录开发板。

## 当前页面

- 桌面状态首页：时间、温湿度、AI Agent 状态、额度网格（5h/周/月 + 倒计时 + 套餐来源）和右侧**日历卡片**（月份标题、上下月箭头、星期表头、当月网格，今天用反色块突出）。
- 电脑性能页：CPU/GPU/内存/磁盘占用、温度、实时网络流量曲线。
- Syna 对话页：语音对话气泡、本地待办清单、API 余额。
- 天气页：和风天气实况、3 天预报、农历、空气质量、紫外线、风向标和室内温湿度。
- 电脑列表：模拟发现三台电脑、选择当前电脑以及在线/离线状态；选中项使用加粗圆角边框。
- 关于页：版本、作者、仓库、设备 IP（联网时附带管理页地址）。

## 运行

在 VS Code 中执行任务 `UI Simulator: Run`，或者在终端运行：

```powershell
& .\simulator\scripts\run.ps1
```

`scripts/*.ps1` 内含作者本机路径（MSYS2 / `C:\ai-panel-build`），换机器先按 `docs/BUILD.md` 直接调用 CMake。

模拟器以 2 倍比例显示，逻辑分辨率始终保持 400×300。

## 键盘操作

| 按键 | 功能 |
| --- | --- |
| `Space` | 按首页 → 性能 → Syna → 天气 → 关于循环 |
| `P` / `S` / `W` | 直接切到性能页 / Syna 页 / 天气页 |
| `C` | 打开电脑列表 |
| `↑` / `↓` | 选择电脑 |
| `Enter` | 确认当前电脑并返回首页 |
| `Esc` | 返回首页 |
| `R` | 循环模拟 Working/Waiting/Done 状态 |

电脑列表也支持鼠标单击进行高亮选择。硬件移植时，这些操作映射到开发板的 KEY 按键（短按翻页、长按打开电脑列表）。

## 环境变量

| 变量 | 作用 |
| --- | --- |
| `AI_PANEL_START_PAGE` | 启动页：`computers` / `performance` / `syna` / `weather`（缺省首页） |
| `AI_PANEL_SCREENSHOT_PATH` | 到点后保存一张 BMP 截图 |
| `AI_PANEL_SCREENSHOT_DELAY_MS` | 截图延时，默认 200；**要跳过 2 秒开屏署名必须设 ≥2200** |
| `AI_PANEL_AUTOCLOSE_MS` | 到点自动退出，便于脚本化截图 |
| `AI_PANEL_MOCK_IP` | 关于页/天气页底栏显示用的假 IP |

## 工程边界

- `src/ui/`：可移植的 LVGL UI 代码，与 ESP32 固件的 `ui.c` 逐段对应，改一边必须同步另一边。
- `src/main.c`：仅用于 Windows 的 SDL2 启动、mock 数据与键盘模拟。
- `src/assets/`：生成的 1-bit 资源与位图字体，与固件 overlay 同名文件保持一致。
- `vendor/lvgl-9.5.0/`：锁定的 LVGL 依赖，不直接修改。
- 构建产物输出到 `C:\ai-panel-build\simulator`，避免 Windows 工具链受到中文构建路径影响。

动态数字使用 LVGL 内置英文字体；中文为 1-bit 嵌入式位图，不依赖运行电脑的系统字体。

## 单色素材与字体

| 脚本 | 内容 | 输出 |
| --- | --- | --- |
| `assets/generate_assets.ps1` | 底图、状态胶囊、设备/电池图标（由 `assets/source/` 图片合成） | 仅 `src/assets/` |
| `assets/generate_weather_icons.py` | 程序化绘制的天气/风向/UV/WiFi 图标 | `src/assets/` + 固件 overlay |
| `assets/generate_ui_fonts.py` | `ui_font_*.c` 位图字体（Arimo / Unifont / Noto 子集） | `src/assets/` + 固件 overlay |

改完图标或字体后重跑对应脚本；`generate_assets.ps1` 目前**只写模拟器一侧**，固件 overlay 需要手动同步同名 `ui_*.c`（这是已知问题，见仓库 issue 记录）。

字体生成依赖已锁定版本：`assets/fonts/requirements.txt`（Pillow 11.3.0、fonttools 4.60.2）。生成结果确定，重跑不会产生无意义 diff。

日历卡片的星期表头使用 `ui_font_11_cjk`（Noto 11px，仅「日一二三四五六」九个字形）；其余中文走 `ui_font_14_cjk`（Unifont 16px 点阵，GB2312 全集）。1-bit 屏没有灰色，层级只能靠字号和 1px 细线区分。
