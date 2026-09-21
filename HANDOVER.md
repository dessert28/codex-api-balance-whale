# 交接清单（Codex 配额小鲸鱼 · 原生版）

> 更新时间：2026-09-21。给人看的接手文档：照着做就能在另一台 Windows 电脑上继续开发与安装。

## 1. 项目概况

| 项目 | 值 |
| --- | --- |
| 本地路径（这台机器） | `D:\project\githubclone\codex-marketplace\plugins\api-balance-whale` |
| 你的远端 | `git@github.com:dessert28/codex-api-balance-whale.git`（分支 `main`）。在 `D:\project\githubclone\codex-api-balance-whale` 这个 clone 里它的名字是 `origin` |
| 上游远端 | `https://github.com/MeteorNOX/DeepSeek-Balance-Whale-Widget.git`（分支 `For-Codex`）。新 clone 默认不配置它，需要对上游取经时自己 `git remote add upstream` 再加 |
| 版本 | `0.3.0+codex.<时间戳>`（`.codex-plugin/plugin.json`，每次重装前用脚本更新） |
| 运行形态 | 单一 C++ 程序 `native\bin\Release\api-balance-whale.exe`，纯 Win32 + GDI+，无 Node / Electron / Windows App SDK |
| 本地 marketplace | `D:\project\githubclone\codex-marketplace`（名字 `personal`） |
| 插件安装缓存 | `C:\Users\<你>\.codex\plugins\cache\personal\api-balance-whale\<版本>` |

## 2. 另一台电脑的接手步骤

```powershell
# 1) 拉代码（你自己的仓库）
git clone git@github.com:dessert28/codex-api-balance-whale.git
cd codex-api-balance-whale

# 2) 编译（仓库里不含 exe，必须本机编译；需要 VS2022 + C++ 桌面工具 + Windows SDK 10.0.26100.0）
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' `
  native\ApiBalanceWhale.vcxproj /t:Build /p:Configuration=Release /p:Platform=x64 /m

# 3) 启动（会等待 Codex 运行后拉起悬浮层）
.\start.ps1
.\status.ps1

# 4) 注册登录自启（可选）
.\install.ps1
```

把插件挂到本机 Codex 的方法（marketplace 根目录换成你 clone 下来的父目录）：

```powershell
codex plugin marketplace add <marketplace 根目录>
python <plugin-creator skill>\scripts\update_plugin_cachebuster.py <plugin 路径>   # 改版本号再重装
codex plugin add api-balance-whale@personal
```

注意：`native\bin\` 与 `native\obj\` 在 `.gitignore` 里，不会随仓库走；换机器后第一次编译会重新生成中间文件（较慢，属正常）。

## 3. 目录结构

```
api-balance-whale\
├─ .codex-plugin\plugin.json     插件清单（名称、版本、图标、MCP 声明）
├─ .mcp.json                     MCP 服务定义：native\bin\Release\api-balance-whale.exe --mcp
├─ assets\                       鲸鱼 PNG、音效 mp3、GIF、上游 whale-widget.js（仅参考，不加载）
├─ docs\NATIVE.md                原生实现说明；docs\UPSTREAM-README.md 上游原版说明
├─ native\                       全部 C++ 源码（见下表）
├─ skills\api-balance-whale\SKILL.md  给 Codex 用的技能说明（原生口径）
├─ qa-output\                    验证截图（不入库）
├─ install.ps1 / uninstall.ps1 / start.ps1 / stop.ps1 / status.ps1
├─ 安装桌面组件.cmd / 启动桌面挂件.cmd / 停止挂件服务.cmd
└─ HANDOVER.md                   本文件
```

native 目录：

| 文件 | 作用 |
| --- | --- |
| `main.cpp` | 入口与四种模式分发（`--supervisor` / `--overlay` / `--settings` / `--mcp`）、DPI 声明、MCP 工具实现 |
| `overlay\desktop_overlay.cpp` | 悬浮层：分层窗口、GDI+ 逐像素 Alpha、气泡绘制、拖拽吸附、托盘、音效、单实例 |
| `settings\settings_window.cpp` | 原生设置窗口（GDI+ 自绘，无 WinUI） |
| `core\usage_snapshot.cpp` | 只读 `%CODEX_HOME%\sessions` + `archived_sessions`，算配额与 token |
| `core\usage_monitor.cpp` | 每轮新增 token 的去重与提示 |
| `core\audio.cpp` | MCI 播放 mp3（`D1/D2` 原版、`Ya1/Ya2` 小黄鸭） |
| `core\autostart.cpp` | 开机自启：Run 键与计划任务互斥管理 |
| `core\bubble_policy.hpp` | 每类气泡的收起时长策略：配额 10 秒、普通气泡 `hideSeconds`、每轮提示 `turnSeconds`，`autoClose=0` 表示常驻 |
| `core\supervisor.hpp` | supervisor 与悬浮层共用的“完全退出”事件名 |
| `native\tests\*.cpp` | 使用快照、轮次监控与气泡时长的单元测试（见第 6 节命令） |

## 4. 运行时数据与配置位置

| 内容 | 路径 / 名称 |
| --- | --- |
| 悬浮层与设置 | `%LOCALAPPDATA%\Codex\api-balance-whale\overlay.json`（`size` / `x` / `y` / `sound` / `soundSet` / `hideSeconds` / `turnSeconds` / `autoClose` / `turnNotice`） |
| 用量缓存 | `%LOCALAPPDATA%\Codex\api-balance-whale\usage-cache.json` |
| Codex 会话日志（只读） | `%CODEX_HOME%\sessions`、`%CODEX_HOME%\archived_sessions` |
| 登录自启（install.ps1） | 计划任务 `Codex API Balance Whale` |
| 登录自启（托盘/设置） | `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` → `ApiBalanceWhale` |
| 单实例互斥量 | `Local\ApiBalanceWhaleOverlayInstance`、`Local\ApiBalanceWhaleSupervisorInstance` |
| “完全退出”事件 | 命名事件 `Local\ApiBalanceWhaleSupervisorStop`（悬浮层置位，supervisor 收到后一起退出） |

设置项含义：`size` 悬浮层边长（物理像素，200–900）、`sound` 点击音效开关、`soundSet` 0 原版 / 1 小黄鸭、`hideSeconds` 随机语句收起秒数（默认 5，范围 3–120）、`turnSeconds` 每轮提示收起秒数（默认 6，对齐上游 `ttlSec`）、`autoClose` 0 = 不自动收起、`turnNotice` 0 = 关闭每轮提示。

## 5. 交互与行为（当前实现）

- 点鲸鱼：显示 5 小时 / 本周配额两行，10 秒后自动收起；再点鲸鱼只重新计时、不换内容（对齐上游 `showCodexQuotaOnClick`）。
- 点气泡：把配额卡片换成随机语句（随机语句按 `hideSeconds` 收起）；再点随机语句或右键即收起（对齐上游 `showRandomBubbleAfterQuota` / `bubbleNext`）。
- 每轮 Codex 对话结束提示一次「模型 + 本轮 token + 两行配额」，按 `turnSeconds` 收起；`turnNotice=0` 时完全不弹。
- 全局快捷键默认 `Ctrl+Alt+W` 显示/收起配额卡片；注册失败（被别的程序占用）自动改用 `Ctrl+Shift+W`，托盘首项显示实际绑定。
- 拖拽移动，靠近工作区边缘 24px 自动吸附；位置写入 `overlay.json`。
- 托盘：查看配额（含快捷键提示）、音效、音效组、大小预设（300/440/580）、开机自启、每轮提示、气泡自动收起、设置、「本次退出挂件（下次启动 Codex 恢复）」、「完全退出」；双击托盘图标显示配额。
- 悬浮层只在 Codex 运行期间存在（由 `--supervisor` 托管）；「本次退出挂件」只关窗口，supervisor 继续待命，Codex 重启后自动恢复；「完全退出」会连 supervisor 一起结束。
- 透明区域点击穿透，只有鲸鱼与气泡区域可交互；窗口始终置顶覆盖普通应用。

## 6. 常用命令

```powershell
# 构建
& '<VS2022>\MSBuild\Current\Bin\MSBuild.exe' native\ApiBalanceWhale.vcxproj /t:Build /p:Configuration=Release /p:Platform=x64 /m
# 工程已带 /utf-8（源码是 UTF-8，代码页 936 的机器上不加 /utf-8 会把中文字面量编坏）

# 启停与状态
.\start.ps1 ; .\status.ps1 ; .\stop.ps1 ; .\install.ps1 ; .\uninstall.ps1

# 直接运行各模式
.\native\bin\Release\api-balance-whale.exe --overlay
.\native\bin\Release\api-balance-whale.exe --settings
.\native\bin\Release\api-balance-whale.exe --supervisor

# MCP 冒烟（stdio，一行一个请求）
'{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"whale_codex_usage"}}' | .\native\bin\Release\api-balance-whale.exe --mcp

# 单元测试
cmd /d /c "call <VS2022>\VC\Auxiliary\Build\vcvars64.bat > nul && cl /nologo /std:c++17 /EHsc /utf-8 /I native\core native\tests\usage_snapshot_tests.cpp native\core\usage_snapshot.cpp /Fe:native\tests\usage_snapshot_tests.exe && native\tests\usage_snapshot_tests.exe"
cmd /d /c "call <VS2022>\VC\Auxiliary\Build\vcvars64.bat > nul && cl /nologo /std:c++17 /EHsc /utf-8 /I native\core native\tests\usage_monitor_tests.cpp native\core\usage_snapshot.cpp native\core\usage_monitor.cpp /Fe:native\tests\usage_monitor_tests.exe && native\tests\usage_monitor_tests.exe"
cmd /d /c "call <VS2022>\VC\Auxiliary\Build\vcvars64.bat > nul && cl /nologo /std:c++17 /EHsc /utf-8 /I native\core native\tests\bubble_policy_tests.cpp /Fe:native\tests\bubble_policy_tests.exe && native\tests\bubble_policy_tests.exe"

# 插件清单校验
python <plugin-creator skill>\scripts\validate_plugin.py .
```

调试开关：

- `WHALE_OVERLAY_DUMP=<png 路径>`：只渲染画布并导出 PNG 后退出（不开窗口），用来检查气泡与鲸鱼绘制。
- `WHALE_OVERLAY_DEBUG=<日志路径>`：记录托盘注册、窗口尺寸、`UpdateLayeredWindow` 结果等。

## 7. 已验证内容（2026-09-21）

第一轮（0.3.0 初版）：

- 编译：Release x64 通过，生成 `api-balance-whale.exe`（约 0.25MB）。
- 单元测试：`usage_snapshot_tests` 与 `usage_monitor_tests` 均通过。
- 插件清单校验：通过。
- MCP：`whale_codex_usage` / `whale_status` 返回真实数据（5 小时 13%、本周 55%），源码目录与安装缓存副本都验证过。
- 悬浮层：截图确认背景完全透明、可覆盖 Codex 窗口、气泡几何与上游一致。
- 交互：点鲸鱼 → 配额气泡；点气泡 → 随机语句；音效路径不崩溃；拖拽 + 吸附 + 位置记忆生效。
- 设置窗口：改「大小」→ 保存后悬浮层立即从 440 变为 480（端到端验证通过）。
- 托盘：`Shell_NotifyIcon` 注册成功（调试日志 `tray icon added`）。
- 系统集成：GUI 子系统，实测不再产生控制台窗口；开关自启会在 Run 键与计划任务之间互斥。
- 资源占用：supervisor 私有约 1.4MB、悬浮层约 5.2MB；5 秒轮询期间 CPU 约 2.6%。

第二轮（本次改动，脚本 `qa\verify-parity.ps1` + `qa\verify-supervisor.ps1`，15 + 7 项全部 PASS；两个脚本已入库，跑之前先编译 Release）：

- 气泡点击语义：点鲸鱼 → 配额卡片（实测 9.5 秒收起）；再点鲸鱼 → 仍是配额卡片、只是重新计时；点气泡 → 换成随机语句（实测 4.6 秒收起）；再点语句 → 收起。
- 快捷键：`Ctrl+Alt+W` 在本机被别的程序占用（`RegisterHotKey` 返回 1409），程序自动回退 `Ctrl+Shift+W`，实测可开/关配额卡片。
- 托盘菜单（脚本枚举真实 HMENU）：查看配额 | 快捷键：Ctrl+Shift+W | 音效 | 音效组：小黄鸭 | 大小：100% | 开机自启 | 每轮提示 | 气泡自动收起 | 设置… | 本次退出挂件（下次启动 Codex 恢复） | 完全退出。
- 配置回写：拖拽后 `overlay.json` = `{"size":440,...,"hideSeconds":5,"turnSeconds":6,"autoClose":1,"turnNotice":1}`。
- 每轮提示开关：向伪造会话日志追加一轮 token，`turnNotice=1` 时弹出提示（气泡像素 9887），`turnNotice=0` 时不弹（0）。
- supervisor：「完全退出」连 supervisor 一起结束；「本次退出挂件」只关悬浮层，supervisor 保持待命（连续 6 秒未重新拉起）。
- MCP：`whale_codex_usage` 返回真实读数（5 小时 85%、本周 13%、`deepseek-flash`），`whale_status` 返回 `codexRunning:true`。
- 设置窗口：截图确认新增「气泡时长 / 每轮提示 / 提示时长 / 自动收起」四行无重叠，窗口 542×696。
- 截图存于 `qa-output\shot-*.png`（不入库）。

## 8. 尚未实现 / 与上游原版差异

功能缺口（上游有、这里没有）：

1. API 余额与账本：`whale_balance` 工具、多服务商（账单 / New API / DeepSeek / 自定义 JSON）、币种与 Frankfurter 汇率、每轮费用估算、`turn-journal.json` 恢复。
2. 素材与角色：多角色图片、GIF 动图气泡、音频导入与裁剪、气泡样式编辑器、素材库管理（256MiB / 256 项等上限校验）。
3. 旧版数据迁移：Electron 时代的配置、素材、账本迁移到原生格式。
4. 横向翻转与自定义吸附区（上游「吸附与翻转」可拖拽设置，这里只有 24px 边缘吸附）。
5. 上游的「素材库 / 角色 / 气泡样式编辑器 / 随机台词编辑」这些配置 UI 全都没有，原生版只有设置窗口那几项。

行为差异（当前实现与上游不同）：

1. 每轮提示内容：上游显示本轮 API 扣费金额（含 pending/unknown/失败中性文案），这里显示模型 + 本轮 token。
2. 气泡内容：上游气泡可放任意模块（文字大小/颜色/图片/GIF 混排），这里只有「两行配额」或「一行随机语句」。
3. 随机语句：上游是可编辑的加权随机集合（含峰谷提示、今日已用、GIF、卖萌吐槽），这里是 10 条内置句子。
4. 气泡内文字大小：上游用固定字号（`dshwv-label` 66 单位等），这里按气泡内可用宽度自动收缩。
5. 每轮提示延迟：上游 5 秒内出现，这里最长约 20 秒（5 秒轮询 + `usage-cache.json` 的 15 秒 TTL，见第 9 节）。
6. 手感：上游气泡是 DOM 弹性动画（`cubic-bezier` 缩放淡入），这里分层窗口直接贴位，没有入场动画。

有意保留的边界：只监测本机 Codex 会话（不监测 ChatGPT 网页端）；只覆盖主显示器工作区，不覆盖全屏独占游戏与 UAC 安全桌面；首次冷启动需要扫描近期归档日志。

## 9. 已知坑

1. **抓屏**：悬浮层是 `UpdateLayeredWindow` 逐像素 Alpha，普通 `BitBlt` 抓不到，必须带 `CAPTUREBLT`（`0x40000000`）。
2. **DPI**：程序是 Per-Monitor V2；这台机器是 150% 缩放，物理分辨率 2520×1680，逻辑 1680×1120。用 DPI 不感知的 PowerShell 读窗口坐标会得到 1/1.5 的值，先调用 `SetProcessDpiAwarenessContext(-4)` 再量。
3. **定位窗口**：在 PowerShell 里 `FindWindow('ApiBalanceWhaleOverlayWindow', null)` 有时返回 0（原因未查明），用 `(Get-Process api-balance-whale).MainWindowHandle` 更可靠；程序内部用 `FindWindow` 通知重载是正常的。
4. **重装插件前**：先 `.\stop.ps1` 或结束进程，否则 exe 被占用会导致链接失败（`LNK1104`）。
5. **别用 WinUI 画悬浮层/设置页**：WinUI 3 的组合层做不出真正的逐像素透明，而且这台机器上 `XamlControlsResources` 会因 `AcrylicBackgroundFillColorDefaultBrush` 缺失直接崩溃（已因此改为纯 GDI+）。
6. **首次编译**：删掉 `native\obj` 后第一次构建会重新生成全部中间文件，耗时会明显变长。
7. `native\obj` 和 `native\bin` 不入库，换机器必须重新编译，然后重新 `codex plugin add` 才会用到新的 exe。
8. **每轮提示最晚会晚约 20 秒**：`usage-cache.json` 带 15 秒 TTL（上一版为省 CPU 加的），加上 5 秒轮询，一轮对话结束后最长约 20 秒才弹；要更快就改小 `usage_snapshot.cpp` 里 `generatedAt + 15 < nowSeconds` 的 15。
9. **全局热键可能被占用**：`Ctrl+Alt+W` 经常被别的软件抢走（本机实测 `RegisterHotKey` 返回 1409）。程序会自动回退 `Ctrl+Shift+W`；两个都失败时托盘首项显示“不可用（已被占用）”。
10. **回调里的鼠标坐标是屏幕坐标**：`HandleClick` 只接受客户区坐标，必须先 `ScreenToClient`。0.3.0 初版漏了这一步，导致点气泡实际走成点鲸鱼（本次已修）。

## 10. 建议的下一步（按性价比）

1. 把每轮提示延迟从 ~20 秒压到 ~5 秒：让缓存 TTL 小于轮询间隔，或只对「配额/总量」用 TTL、「最新一轮」每次校验文件 `last_write_time`。
2. 快捷键做成可配置（并同步显示在设置窗口里，现在设置窗口只写了默认值）。
3. 随机语句改为可编辑的加权集合（`overlay.json` 加 `quotes` + 权重），向第 8 节第 3 条靠。
4. 余额/账本（`whale_balance`、多服务商、汇率、每轮费用）——工作量大，先定存储格式。
5. 素材库与多角色、气泡样式编辑器。
