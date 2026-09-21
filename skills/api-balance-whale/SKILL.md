---
name: api-balance-whale
description: 控制本机 Codex 配额小鲸鱼（原生 C++ 桌面悬浮窗）：查看 5 小时/周配额、今日与近 7 天 token、启停悬浮层、查询状态、调整大小/音效/开机自启。适用于用户提到小鲸鱼、挂件、配额悬浮窗或 Codex 用量读数时；普通编码任务不需要此技能。
---

小鲸鱼只监测本机 Codex 会话日志与配额窗口：不查询 API 余额、不读取 `auth.json`、不联网，也不监测 ChatGPT 网页端对话。用户问“余额”时说明本插件读取的是 Codex 配额（5 小时/周）与本机 token 用量；确实需要 API 余额时按用户使用的服务商单独处理，不要编造数字。

## 工具

插件根目录是本文件上两级。优先使用 MCP 工具：

- `whale_codex_usage` 与 `whale_usage`：返回 `todayTokens`、`last7dTokens`、`currentModel`、`windows.fiveHour` / `windows.weekly`（`usedPercent`、`resetsAt`）、`lastTurn`、`stale`、`error`。
- `whale_status`：`codexRunning`、`native`、`mode`。
- `whale_open`：显示或唤出桌面小鲸鱼。

没有加载 MCP 工具时直接运行 `native\bin\Release\api-balance-whale.exe`：

- `--mcp`：stdio MCP 服务
- `--overlay`：显示悬浮层；已有实例会被唤醒并展示配额
- `--settings`：打开原生设置窗口
- `--supervisor`：检测 Codex 生命周期并托管悬浮层（计划任务用的就是它）

## 安装与启停

- 安装（注册当前用户计划任务并立即启动）：`.\install.ps1`
- 启动 `.\start.ps1`、停止 `.\stop.ps1`、卸载 `.\uninstall.ps1`、状态 `.\status.ps1`
- 首次使用需要先用 VS2022 构建 `native\ApiBalanceWhale.vcxproj`（Release x64）。找不到 exe 时先构建，不要退回旧脚本。
- 开机自启有两种等价方式，同一时间只会启用一种：计划任务 `Codex API Balance Whale`（`install.ps1`）或当前用户 Run 键 `ApiBalanceWhale`（设置窗口/托盘）。两者都关掉才算关闭自启。
- 计划任务以登录用户身份运行 `--supervisor`，可见窗口由它拉起；不要改用后台 PowerShell 包装进程。

## 交互与配置

- 点鲸鱼：显示 5 小时/本周配额两行；没有配额数据时显示“暂无数据”，使用缓存时标注“上次数据”。
- 点气泡：随机语句；右键收起；默认 5 秒自动收起。
- 托盘菜单：查看配额、音效开关、音效组（原版 D1/D2 或小黄鸭 Ya1/Ya2）、大小预设（300/440/580）、开机自启、设置、退出。
- 配置在 `%LOCALAPPDATA%\Codex\api-balance-whale\overlay.json`；设置窗口保存后会立刻刷新运行中的悬浮层。改配置请用设置窗口或托盘，不要手写计划任务或直接改 marketplace。

## 排障

- 悬浮层没出现：确认 Codex 正在运行、`.\status.ps1` 有进程、自启开关状态正常；必要时手动运行 `--overlay`。
- 一直显示“暂无数据”：本机会话日志里还没有 `rate_limits`（例如 API key 模式或日志被清理），这是预期结果，不要编造配额。
- 首次冷启动需要扫描近期归档日志，读数会慢一些；5 秒轮询属于正常开销。
- 需要截图验证时，普通 `BitBlt` 抓不到分层窗口，必须带 `CAPTUREBLT`。
- 排错日志：设置环境变量 `WHALE_OVERLAY_DEBUG=<日志路径>` 后重启悬浮层。

## 边界

- 只覆盖主显示器工作区，不覆盖全屏独占游戏与 UAC 安全桌面。
- 只监测本机 Codex 会话；ChatGPT 网页端对话没有本机 token 记录。
- 上游 Electron/Node 版本已经移除；`assets/whale-widget.js` 仅作为上游视觉参考保留，运行时不会加载。
