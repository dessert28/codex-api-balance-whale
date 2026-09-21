# Codex 配额小鲸鱼（原生桌面版）

本项目基于 [MeteorNOX/DeepSeek-Balance-Whale-Widget 的 For-Codex 分支](https://github.com/MeteorNOX/DeepSeek-Balance-Whale-Widget/tree/For-Codex) 改造，保留上游 MIT 许可证、鲸鱼素材与来源说明。

运行核心是单一 C++ 程序 `native\bin\Release\api-balance-whale.exe`：Win32 分层窗口 + GDI+ 逐像素 Alpha 的桌面悬浮层，背景完全透明；进程声明 Per-Monitor V2 DPI 感知。不再依赖 Node、Electron 或 Windows App SDK。

## 功能

- 点击鲸鱼：显示 `5 小时` 与 `本周` 使用率及重置倒计时，固定 10 秒后自动收起；没有配额数据时显示“暂无数据”，使用缓存时标注“上次数据”。再点鲸鱼只会重新计时，不会切换内容。
- 点击气泡：配额卡片换成随机语句（按权重抽取、尽量不重复上一条；按“气泡时长”收起，默认 5 秒）；再点随机语句、或右键，收起气泡。
- 随机语句可编辑：托盘“随机语句…”或 `--quotes` 打开编辑器，每行写成 `权重|文本`（省略权重即 1，范围 1–99），保存后写回 `overlay.json` 并让悬浮层立即重载；“恢复默认”可取回内置的 10 条句子。
- 语句支持占位符（对齐上游 `bubbleContentTokenMap` 的写法）：`{p5h}` / `{week}` 是 5 小时与本周已用百分比，`{reset5h}` / `{resetweek}` 是重置倒计时，`{turn}` 是上一轮 token，`{today}` / `{tokens7d}` 是今日与近 7 天 token，另有 `{model}`、`{date}`、`{time}`。气泡显示时才替换成实时数值，读不到的显示 `--`，不认识的占位符（例如上游的 `{balance_api}`）原样保留；这份清单也印在语句编辑器里。
- 点击音效：原版（`D1/D2.mp3`）或小黄鸭（`Ya1/Ya2.mp3`），可开关。
- 每轮提示音：对齐上游 `usageSet.taskEnd = {on, sel}`——本轮对话结束后播放提示音，可在托盘/设置里开关，并在 `preset:<组>:<按下|松开>` 里选（小黄鸭 `duck` 用 `Ya1/Ya2`，音效1 `fx1` 用 `D1/D2`）；受全局“音效”开关约束，`sel` 为空或无法识别时回落到上游默认的 `preset:duck:press`。
- 全局快捷键：`Ctrl+Alt+W` 显示/收起配额卡片；若已被其它程序占用会自动改用 `Ctrl+Shift+W`，托盘首项显示实际生效的组合。
- 托盘图标：查看配额（含快捷键提示）、音效、音效组、大小预设（300/440/580）、开机自启、每轮提示、每轮提示音、提示音：…、气泡自动收起、设置、随机语句…、本次退出挂件（下次启动 Codex 恢复）、完全退出。
- 设置窗口（`--settings`）：大小、点击音效、音效组、气泡时长、每轮提示开关、提示时长、自动收起开关、开机自启、每轮提示音开关、提示音选择、恢复默认位置；保存后立即刷新悬浮层。
- 拖拽移动并在工作区边缘吸附；位置与设置记忆在 `%LOCALAPPDATA%\Codex\api-balance-whale\overlay.json`。
- 每轮 Codex 对话结束后提示一次模型与本轮 token（默认 6 秒收起，可在托盘/设置里关闭）；悬浮层监听会话目录，日志写完后约 1.5–2 秒弹出，不再等下一个轮询周期。
- MCP 提供 `whale_codex_usage`、`whale_usage`、`whale_status`、`whale_open`。
- 只读 `%CODEX_HOME%\sessions` 与 `archived_sessions`，缓存写在同一数据目录；不读取 `auth.json`，不联网。

架构细节见 [docs/NATIVE.md](docs/NATIVE.md)。

## 构建

要求：VS2022、C++ 桌面工具、Windows 10/11 SDK 10.0.26100.0。不需要 Windows App Runtime。

```powershell
cd D:\project\githubclone\codex-marketplace\plugins\api-balance-whale
& 'D:\software\VisualStudio\vs2022\MSBuild\Current\Bin\MSBuild.exe' `
  native\ApiBalanceWhale.vcxproj /t:Build /p:Configuration=Release /p:Platform=x64 /m
```

生成文件：`native\bin\Release\api-balance-whale.exe`，构建后会自动把鲸鱼与音效素材复制到 `native\bin\Release\assets`。上面的路径按自己机器替换；源码是 UTF-8，工程已带 `/utf-8`，不需要额外设置代码页。

## 启动与安装

```powershell
.\start.ps1       # 启动 supervisor；它等待 Codex 并在其运行期间托管悬浮层
.\status.ps1      # 进程、计划任务、自启状态、当前配置
.\stop.ps1 -WhatIf # 只列会被结束的进程（含旧版 Electron 残留）
.\stop.ps1        # 结束悬浮层、supervisor 与旧版 Electron 残留
.\install.ps1     # 注册当前用户登录计划任务并立即启动
.\uninstall.ps1   # 移除计划任务与当前用户启动项，并结束进程
```

也可以直接运行：

```powershell
.\native\bin\Release\api-balance-whale.exe --overlay
.\native\bin\Release\api-balance-whale.exe --supervisor
.\native\bin\Release\api-balance-whale.exe --settings
.\native\bin\Release\api-balance-whale.exe --mcp
.\native\bin\Release\api-balance-whale.exe --quotes
```

开机自启只有一种生效方式：`install.ps1` 写计划任务 `Codex API Balance Whale`，托盘/设置里的“开机自启”写当前用户 Run 键 `ApiBalanceWhale`；开启其中一种会自动移除另一种，supervisor 也有单实例保护，不会出现两只鲸鱼。

这台机器上如果还留着 0.2.0 时代的 Electron 安装（`WhaleLauncher-*.exe` + `desktop\supervisor.ps1`，数据目录 `%USERPROFILE%\.codex\whale-widget`），它会占着同名的计划任务，桌面上画的就还是旧版。`.\status.ps1` 会把这种残留单独报出来，`.\stop.ps1` 结束旧进程（含它的 supervisor 与 Electron 子进程），再 `.\install.ps1` 把任务改指原生 exe 就切换完成；旧数据目录不会被删。

## 数据口径与限制

token 按每个 JSONL 的 `total_token_usage` 差值累计；配额取最新可用的 `rate_limits.primary` 与 `rate_limits.secondary`。悬浮层用 `ReadDirectoryChangesW` 监听 `%CODEX_HOME%\sessions`，日志一变就（去抖 1.5 秒）重算一次，5 秒轮询只作兜底；实测待机 12 秒的 CPU 时间约 0.02 秒。限制：只监测本机 Codex 会话（ChatGPT 网页端对话没有本机 token 记录）；只覆盖主显示器工作区，全屏独占游戏与 UAC 安全桌面不覆盖；首次冷启动需要扫描近期归档日志；原 Electron/Node 实现已移除，`assets/whale-widget.js` 仅作为上游视觉参考保留。

## 测试

```powershell
cmd /d /c "call D:\software\VisualStudio\vs2022\VC\Auxiliary\Build\vcvars64.bat > nul && cl /nologo /std:c++17 /EHsc /utf-8 /I native\core native\tests\usage_snapshot_tests.cpp native\core\usage_snapshot.cpp /Fe:native\tests\usage_snapshot_tests.exe && native\tests\usage_snapshot_tests.exe"
cmd /d /c "call D:\software\VisualStudio\vs2022\VC\Auxiliary\Build\vcvars64.bat > nul && cl /nologo /std:c++17 /EHsc /utf-8 /I native\core native\tests\usage_monitor_tests.cpp native\core\usage_snapshot.cpp native\core\usage_monitor.cpp /Fe:native\tests\usage_monitor_tests.exe && native\tests\usage_monitor_tests.exe"
cmd /d /c "call D:\software\VisualStudio\vs2022\VC\Auxiliary\Build\vcvars64.bat > nul && cl /nologo /std:c++17 /EHsc /utf-8 /I native\core native\tests\bubble_policy_tests.cpp /Fe:native\tests\bubble_policy_tests.exe && native\tests\bubble_policy_tests.exe"
cmd /d /c "call D:\software\VisualStudio\vs2022\VC\Auxiliary\Build\vcvars64.bat > nul && cl /nologo /std:c++17 /EHsc /utf-8 /I native\core native\tests\session_watcher_tests.cpp native\core\session_watcher.cpp /Fe:native\tests\session_watcher_tests.exe && native\tests\session_watcher_tests.exe"
cmd /d /c "call D:\software\VisualStudio\vs2022\VC\Auxiliary\Build\vcvars64.bat > nul && cl /nologo /std:c++17 /EHsc /utf-8 /I native\core native\tests\quote_tests.cpp native\core\quotes.cpp native\core\json_span.cpp /Fe:native\tests\quote_tests.exe && native\tests\quote_tests.exe"
cmd /d /c "call D:\software\VisualStudio\vs2022\VC\Auxiliary\Build\vcvars64.bat > nul && cl /nologo /std:c++17 /EHsc /utf-8 /I native\core native\tests\task_end_tests.cpp native\core\task_end.cpp native\core\json_span.cpp /Fe:native\tests\task_end_tests.exe && native\tests\task_end_tests.exe"
cmd /d /c "call D:\software\VisualStudio\vs2022\VC\Auxiliary\Build\vcvars64.bat > nul && cl /nologo /std:c++17 /EHsc /utf-8 /I native\core native\tests\bubble_token_tests.cpp native\core\bubble_tokens.cpp /Fe:native\tests\bubble_token_tests.exe && native\tests\bubble_token_tests.exe"

powershell -NoProfile -ExecutionPolicy Bypass -File qa\verify-parity.ps1      # 36 项：交互、气泡时长、托盘、每轮提示延迟、每轮提示音、随机语句（含占位符）与编辑器
powershell -NoProfile -ExecutionPolicy Bypass -File qa\verify-supervisor.ps1  # 7 项：托管与两种退出
powershell -NoProfile -ExecutionPolicy Bypass -File qa\verify-live.ps1        # 4 项：真实会话目录下的启动、待机 CPU、干净退出
```

## 调试

- `WHALE_OVERLAY_DUMP=<png 路径>`：只渲染画布并导出 PNG（不显示窗口），用于检查气泡与鲸鱼绘制。
- `WHALE_OVERLAY_DEBUG=<日志路径>`：记录托盘注册、窗口尺寸、`UpdateLayeredWindow` 结果。
- 分层窗口用 `UpdateLayeredWindow` 逐像素 Alpha，普通 `BitBlt` 抓屏需要带 `CAPTUREBLT` 才能截到鲸鱼。
