# Codex 配额小鲸鱼（原生桌面版）

本项目基于 [MeteorNOX/DeepSeek-Balance-Whale-Widget 的 For-Codex 分支](https://github.com/MeteorNOX/DeepSeek-Balance-Whale-Widget/tree/For-Codex) 改造，保留上游 MIT 许可证、鲸鱼素材与来源说明。

运行核心是单一 C++ 程序 `native\bin\Release\api-balance-whale.exe`：Win32 分层窗口 + GDI+ 逐像素 Alpha 的桌面悬浮层，背景完全透明；进程声明 Per-Monitor V2 DPI 感知。不再依赖 Node、Electron 或 Windows App SDK。

## 功能

- 点击鲸鱼：显示 `5 小时` 与 `本周` 使用率及重置倒计时；没有配额数据时显示“暂无数据”，使用缓存时标注“上次数据”。
- 点击气泡：显示随机语句；右键收起；默认 5 秒自动收起。
- 点击音效：原版（`D1/D2.mp3`）或小黄鸭（`Ya1/Ya2.mp3`），可开关。
- 托盘图标：查看配额、音效、音效组、大小预设（300/440/580）、开机自启、设置、退出。
- 设置窗口（`--settings`）：大小、点击音效、音效组、自动收起秒数、开机自启、恢复默认位置；保存后立即刷新悬浮层。
- 拖拽移动并在工作区边缘吸附；位置与设置记忆在 `%LOCALAPPDATA%\Codex\api-balance-whale\overlay.json`。
- 每轮 Codex 对话结束后提示一次模型与本轮 token。
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

生成文件：`native\bin\Release\api-balance-whale.exe`，构建后会自动把鲸鱼与音效素材复制到 `native\bin\Release\assets`。

## 启动与安装

```powershell
.\start.ps1       # 启动 supervisor；它等待 Codex 并在其运行期间托管悬浮层
.\status.ps1      # 进程、计划任务、自启状态、当前配置
.\stop.ps1        # 结束悬浮层与 supervisor
.\install.ps1     # 注册当前用户登录计划任务并立即启动
.\uninstall.ps1   # 移除计划任务与当前用户启动项，并结束进程
```

也可以直接运行：

```powershell
.\native\bin\Release\api-balance-whale.exe --overlay
.\native\bin\Release\api-balance-whale.exe --supervisor
.\native\bin\Release\api-balance-whale.exe --settings
.\native\bin\Release\api-balance-whale.exe --mcp
```

开机自启只有一种生效方式：`install.ps1` 写计划任务 `Codex API Balance Whale`，托盘/设置里的“开机自启”写当前用户 Run 键 `ApiBalanceWhale`；开启其中一种会自动移除另一种，supervisor 也有单实例保护，不会出现两只鲸鱼。

## 数据口径与限制

token 按每个 JSONL 的 `total_token_usage` 差值累计；配额取最新可用的 `rate_limits.primary` 与 `rate_limits.secondary`。限制：只监测本机 Codex 会话（ChatGPT 网页端对话没有本机 token 记录）；只覆盖主显示器工作区，全屏独占游戏与 UAC 安全桌面不覆盖；首次冷启动需要扫描近期归档日志，5 秒轮询期间 CPU 约 2.6%；原 Electron/Node 实现已移除，`assets/whale-widget.js` 仅作为上游视觉参考保留。

## 测试

```powershell
cmd /d /c "call D:\software\VisualStudio\vs2022\VC\Auxiliary\Build\vcvars64.bat > nul && cl /nologo /std:c++17 /EHsc /I native\core native\tests\usage_snapshot_tests.cpp native\core\usage_snapshot.cpp /Fe:native\tests\usage_snapshot_tests.exe && native\tests\usage_snapshot_tests.exe"
cmd /d /c "call D:\software\VisualStudio\vs2022\VC\Auxiliary\Build\vcvars64.bat > nul && cl /nologo /std:c++17 /EHsc /I native\core native\tests\usage_monitor_tests.cpp native\core\usage_snapshot.cpp native\core\usage_monitor.cpp /Fe:native\tests\usage_monitor_tests.exe && native\tests\usage_monitor_tests.exe"
```

## 调试

- `WHALE_OVERLAY_DUMP=<png 路径>`：只渲染画布并导出 PNG（不显示窗口），用于检查气泡与鲸鱼绘制。
- `WHALE_OVERLAY_DEBUG=<日志路径>`：记录托盘注册、窗口尺寸、`UpdateLayeredWindow` 结果。
- 分层窗口用 `UpdateLayeredWindow` 逐像素 Alpha，普通 `BitBlt` 抓屏需要带 `CAPTUREBLT` 才能截到鲸鱼。
