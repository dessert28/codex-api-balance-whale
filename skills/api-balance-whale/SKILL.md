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
- `--quotes`：打开随机语句编辑器（编辑加权随机语句集合，支持 `{p5h}` 之类的占位符）
- `--supervisor`：检测 Codex 生命周期并托管悬浮层（计划任务用的就是它）

## 安装与启停

- 安装（注册当前用户计划任务并立即启动）：`.\install.ps1`
- 启动 `.\start.ps1`、停止 `.\stop.ps1`、卸载 `.\uninstall.ps1`、状态 `.\status.ps1`
- 首次使用需要先用 VS2022 构建 `native\ApiBalanceWhale.vcxproj`（Release x64）。找不到 exe 时先构建，不要退回旧脚本。
- 开机自启有两种等价方式，同一时间只会启用一种：计划任务 `Codex API Balance Whale`（`install.ps1`）或当前用户 Run 键 `ApiBalanceWhale`（设置窗口/托盘）。两者都关掉才算关闭自启。
- 计划任务以登录用户身份运行 `--supervisor`，可见窗口由它拉起；不要改用后台 PowerShell 包装进程。

## 交互与配置

- 点鲸鱼：显示 5 小时/本周配额两行，固定 10 秒收起；再点鲸鱼只是重新计时。没有配额数据时显示“暂无数据”，使用缓存时标注“上次数据”。
- 点气泡：把配额卡片换成随机语句（按权重抽取、尽量不重复上一条；按“气泡时长”收起，默认 5 秒）；再点随机语句或右键即收起。
- 全局快捷键：默认 `Ctrl+Alt+W` 显示/收起配额卡片；被占用时自动回退 `Ctrl+Shift+W`，以托盘首项为准。
- 托盘菜单：查看配额（含快捷键提示）、音效开关、音效组（原版 D1/D2 或小黄鸭 Ya1/Ya2）、大小预设（300/440/580）、开机自启、每轮提示、每轮提示音、提示音：…、气泡自动收起、设置、随机语句…、本次退出挂件（下次启动 Codex 恢复）、完全退出。
- 每轮提示音：本轮对话出现时播放提示音（对齐上游 `usageSet.taskEnd = {on, sel}`），托盘“每轮提示音”开关、“提示音：…”逐项切换四个预设（小黄鸭/音效1 的按下与松开）；同时受“音效”总开关约束，两者都开才会响。
- 随机语句可以改：托盘“随机语句…”或 `--quotes`，一行一条 `权重|文本`（省略权重即 1，范围 1–99），保存后立刻生效；`overlay.json` 里对应 `quotes` 数组，元素是 `{"t":文本,"w":权重}`，空数组会回落到内置 10 条。正文里可以写占位符（`{p5h}` `{week}` `{reset5h}` `{resetweek}` `{turn}` `{today}` `{tokens7d}` `{model}` `{date}` `{time}`），显示时替换成实时数值；不认识的占位符原样保留。
- 每轮提示在会话日志写完后约 1.5–2 秒弹出（悬浮层用 `ReadDirectoryChangesW` 监听 `sessions` 目录，5 秒轮询只作兜底）；与收起时长可在托盘或设置窗口调整：`turnNotice`、`turnSeconds`、`hideSeconds`、`autoClose` 都写在 `overlay.json`。
- 配置在 `%LOCALAPPDATA%\Codex\api-balance-whale\overlay.json`；设置窗口保存后会立刻刷新运行中的悬浮层。改配置请用设置窗口或托盘，不要手写计划任务或直接改 marketplace。

## 排障

- 悬浮层没出现：确认 Codex 正在运行、`.\status.ps1` 有进程、自启开关状态正常；必要时手动运行 `--overlay`。
- 一直显示“暂无数据”：本机会话日志里还没有 `rate_limits`（例如 API key 模式或日志被清理），这是预期结果，不要编造配额。
- 首次冷启动需要扫描近期归档日志，读数会慢一些；之后靠目录监听即时刷新，5 秒轮询只是兜底，待机 CPU 开销可以忽略。
- 需要截图验证时，普通 `BitBlt` 抓不到分层窗口，必须带 `CAPTUREBLT`。
- 排错日志：设置环境变量 `WHALE_OVERLAY_DEBUG=<日志路径>` 后重启悬浮层。

## 边界

- 只覆盖主显示器工作区，不覆盖全屏独占游戏与 UAC 安全桌面。
- 只监测本机 Codex 会话；ChatGPT 网页端对话没有本机 token 记录。
- 上游 Electron/Node 版本已经移除；`assets/whale-widget.js` 仅作为上游视觉参考保留，运行时不会加载。
