# 交接清单（Codex 配额小鲸鱼 · 原生版）

> 更新时间：2026-09-21。给人看的接手文档：照着做就能在另一台 Windows 电脑上继续开发与安装。

## 1. 项目概况

| 项目 | 值 |
| --- | --- |
| 本地路径（这台机器） | `D:\project\githubclone\codex-marketplace\plugins\api-balance-whale` |
| 你的远端 | `git@github.com:dessert28/codex-api-balance-whale.git`（分支 `main`）。在 `D:\project\githubclone\codex-api-balance-whale` 这个 clone 里它的名字是 `origin` |
| 上游远端 | `https://github.com/MeteorNOX/DeepSeek-Balance-Whale-Widget.git`（分支 `For-Codex`）。新 clone 默认不配置它，需要对上游取经时自己 `git remote add upstream` 再加 |
| 版本 | `0.3.4+codex.<时间戳>`（`.codex-plugin/plugin.json`，每次重装前用脚本更新） |
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

如果这台机器装过 0.2.0 的 Electron 版，`.\status.ps1` 会报出旧版残留（计划任务仍指向 `WhaleLauncher-*.exe`）；先 `.\stop.ps1` 再 `.\install.ps1` 即可切到原生版，`%USERPROFILE%\.codex\whale-widget` 里的旧数据不会被动。

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
├─ legacy-install.ps1            旧版 Electron 安装残留的探测与停止（status / stop / install / uninstall 共用）
├─ 安装桌面组件.cmd / 启动桌面挂件.cmd / 停止挂件服务.cmd
└─ HANDOVER.md                   本文件
```

native 目录：

| 文件 | 作用 |
| --- | --- |
| `main.cpp` | 入口与五种模式分发（`--supervisor` / `--overlay` / `--settings` / `--quotes` / `--mcp`）、DPI 声明、MCP 工具实现 |
| `overlay\desktop_overlay.cpp` | 悬浮层：分层窗口、GDI+ 逐像素 Alpha、气泡绘制、拖拽吸附、托盘、音效、单实例 |
| `settings\settings_window.cpp` | 原生设置窗口（GDI+ 自绘，无 WinUI） |
| `settings\quote_editor.cpp` | 随机语句编辑器（`--quotes`）：加权语句集合的多行编辑、保存与热重载通知 |
| `core\usage_snapshot.cpp` | 只读 `%CODEX_HOME%\sessions` + `archived_sessions`，算配额与 token |
| `core\usage_monitor.cpp` | 每轮新增 token 的去重与提示 |
| `core\quotes.cpp` | 随机语句：`quotes` 的 JSON 读写、`权重\|文本` 解析、按权重抽取并避开上一条 |
| `core\task_end.cpp` | 每轮提示音：`taskEnd` 的 JSON 读写、上游 `preset:<组>:<按下\|松开>` 的解析与轮换 |
| `core\json_span.cpp` | 通用 JSON 取值区间查找与替换，给「多个写入方共用一个配置文件」兜底 |
| `core\audio.cpp` | MCI 播放 mp3（`D1/D2` 原版、`Ya1/Ya2` 小黄鸭）；`PlayPress` / `PlayRelease` 返回是否真的播上 |
| `core\autostart.cpp` | 开机自启：Run 键与计划任务互斥管理 |
| `core\bubble_policy.hpp` | 每类气泡的收起时长策略：配额 10 秒、普通气泡 `hideSeconds`、每轮提示 `turnSeconds`，`autoClose=0` 表示常驻 |
| `core\session_watcher.cpp` | `ReadDirectoryChangesW` 递归监听 `sessions` 目录（去抖 1.5 秒），日志一写完就让悬浮层重算，每轮提示因此能在约 2 秒内出现 |
| `core\supervisor.hpp` | supervisor 与悬浮层共用的“完全退出”事件名 |
| `native\tests\*.cpp` | 使用快照、轮次监控、气泡时长、目录监听与随机语句的单元测试（见第 6 节命令） |

## 4. 运行时数据与配置位置

| 内容 | 路径 / 名称 |
| --- | --- |
| 悬浮层与设置 | `%LOCALAPPDATA%\Codex\api-balance-whale\overlay.json`（`size` / `x` / `y` / `sound` / `soundSet` / `hideSeconds` / `turnSeconds` / `autoClose` / `turnNotice` / `taskEnd` / `quotes`） |
| 用量缓存 | `%LOCALAPPDATA%\Codex\api-balance-whale\usage-cache.json` |
| Codex 会话日志（只读） | `%CODEX_HOME%\sessions`、`%CODEX_HOME%\archived_sessions` |
| 登录自启（install.ps1） | 计划任务 `Codex API Balance Whale` |
| 登录自启（托盘/设置） | `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` → `ApiBalanceWhale` |
| 单实例互斥量 | `Local\ApiBalanceWhaleOverlayInstance`、`Local\ApiBalanceWhaleSupervisorInstance` |
| “完全退出”事件 | 命名事件 `Local\ApiBalanceWhaleSupervisorStop`（悬浮层置位，supervisor 收到后一起退出） |
| 旧版 Electron 残留 | `%USERPROFILE%\.codex\whale-widget`（`WhaleLauncher-*.exe` + `desktop-runtime` + `ledgers`）：0.2.0 时代那次安装留下的启动器与数据，原生版不使用；`status.ps1` 负责报出来，`stop.ps1` / `uninstall.ps1` 能结束它的进程 |

设置项含义：`size` 悬浮层边长（物理像素，200–900）、`sound` 点击音效开关、`soundSet` 0 原版 / 1 小黄鸭、`hideSeconds` 随机语句收起秒数（默认 5，范围 3–120）、`turnSeconds` 每轮提示收起秒数（默认 6，对齐上游 `ttlSec`）、`autoClose` 0 = 不自动收起、`turnNotice` 0 = 关闭每轮提示、`taskEnd` 是每轮提示音（`{"on":0|1,"sel":"preset:<duck|fx1>:<press|release>"}`，与上游 `usageSet.taskEnd` 同形，默认 `on:0`）、`quotes` 是加权随机语句数组（`[{"t":"文本","w":权重}]`，缺省或为空时回落到内置 10 条）。

## 5. 交互与行为（当前实现）

- 点鲸鱼：显示 5 小时 / 本周配额两行，10 秒后自动收起；再点鲸鱼只重新计时、不换内容（对齐上游 `showCodexQuotaOnClick`）。
- 点气泡：把配额卡片换成随机语句（随机语句按 `hideSeconds` 收起）；再点随机语句或右键即收起（对齐上游 `showRandomBubbleAfterQuota` / `bubbleNext`）。语句按 `w` 加权随机，并最多重试 6 次避开上一条（对齐上游 `bubblePickLine` 的 `avoidIdx`）。
- 随机语句可编辑：托盘「随机语句…」或 `--quotes` 打开编辑器，一行一条 `权重|文本`（省略权重即 1，范围 1–99，空行忽略，非数字前缀时 `|` 算正文）；保存只替换 `quotes` 字段，随后通知悬浮层热重载。
- 每轮 Codex 对话结束提示一次「模型 + 本轮 token + 两行配额」，按 `turnSeconds` 收起；`turnNotice=0` 时完全不弹。日志写入后约 1.5–2 秒弹出（目录监听 + 1.5 秒去抖），5 秒轮询只作兜底。
- 每轮提示音（对齐上游 `usageSet.taskEnd = {on, sel}`）：本轮对话一被监控到就先按 `sel` 播放，再决定要不要弹提示气泡；`sel` 是 `preset:<duck|fx1>:<press|release>`（小黄鸭 `duck` → `Ya1/Ya2`，音效1 `fx1` → `D1/D2`），空值或上游的 `grp:` / `frag:` 会回落到默认的 `preset:duck:press`；`taskEnd.on` 与全局“音效”任一关闭就不响。托盘“每轮提示音”是开关，“提示音：…”点一下换下一个预设；设置窗口里也能改这两项。
- 全局快捷键默认 `Ctrl+Alt+W` 显示/收起配额卡片；注册失败（被别的程序占用）自动改用 `Ctrl+Shift+W`，托盘首项显示实际绑定。
- 拖拽移动，靠近工作区边缘 24px 自动吸附；位置写入 `overlay.json`。
- 托盘：查看配额（含快捷键提示）、音效、音效组、大小预设（300/440/580）、开机自启、每轮提示、每轮提示音、提示音：…、气泡自动收起、设置、「随机语句…」、「本次退出挂件（下次启动 Codex 恢复）」、「完全退出」；双击托盘图标显示配额。
- 悬浮层只在 Codex 运行期间存在（由 `--supervisor` 托管）；「本次退出挂件」只关窗口，supervisor 继续待命，Codex 重启后自动恢复；「完全退出」会连 supervisor 一起结束。
- 透明区域点击穿透，只有鲸鱼与气泡区域可交互；窗口始终置顶覆盖普通应用。

## 6. 常用命令

```powershell
# 构建
& '<VS2022>\MSBuild\Current\Bin\MSBuild.exe' native\ApiBalanceWhale.vcxproj /t:Build /p:Configuration=Release /p:Platform=x64 /m
# 工程已带 /utf-8（源码是 UTF-8，代码页 936 的机器上不加 /utf-8 会把中文字面量编坏）

# 启停与状态
.\start.ps1 ; .\status.ps1 ; .\install.ps1 ; .\uninstall.ps1
.\stop.ps1                     # 结束悬浮层与 supervisor
.\stop.ps1 -WhatIf             # 只列会被结束的进程（含旧版 Electron 残留），不动手

# 直接运行各模式
.\native\bin\Release\api-balance-whale.exe --overlay
.\native\bin\Release\api-balance-whale.exe --settings
.\native\bin\Release\api-balance-whale.exe --quotes
.\native\bin\Release\api-balance-whale.exe --supervisor

# MCP 冒烟（stdio，一行一个请求）
'{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"whale_codex_usage"}}' | .\native\bin\Release\api-balance-whale.exe --mcp

# 单元测试
cmd /d /c "call <VS2022>\VC\Auxiliary\Build\vcvars64.bat > nul && cl /nologo /std:c++17 /EHsc /utf-8 /I native\core native\tests\usage_snapshot_tests.cpp native\core\usage_snapshot.cpp /Fe:native\tests\usage_snapshot_tests.exe && native\tests\usage_snapshot_tests.exe"
cmd /d /c "call <VS2022>\VC\Auxiliary\Build\vcvars64.bat > nul && cl /nologo /std:c++17 /EHsc /utf-8 /I native\core native\tests\usage_monitor_tests.cpp native\core\usage_snapshot.cpp native\core\usage_monitor.cpp /Fe:native\tests\usage_monitor_tests.exe && native\tests\usage_monitor_tests.exe"
cmd /d /c "call <VS2022>\VC\Auxiliary\Build\vcvars64.bat > nul && cl /nologo /std:c++17 /EHsc /utf-8 /I native\core native\tests\bubble_policy_tests.cpp /Fe:native\tests\bubble_policy_tests.exe && native\tests\bubble_policy_tests.exe"
cmd /d /c "call <VS2022>\VC\Auxiliary\Build\vcvars64.bat > nul && cl /nologo /std:c++17 /EHsc /utf-8 /I native\core native\tests\session_watcher_tests.cpp native\core\session_watcher.cpp /Fe:native\tests\session_watcher_tests.exe && native\tests\session_watcher_tests.exe"
cmd /d /c "call <VS2022>\VC\Auxiliary\Build\vcvars64.bat > nul && cl /nologo /std:c++17 /EHsc /utf-8 /I native\core native\tests\quote_tests.cpp native\core\quotes.cpp /Fe:native\tests\quote_tests.exe && native\tests\quote_tests.exe"
cmd /d /c "call <VS2022>\VC\Auxiliary\Build\vcvars64.bat > nul && cl /nologo /std:c++17 /EHsc /utf-8 /I native\core native\tests\task_end_tests.cpp native\core\task_end.cpp native\core\json_span.cpp /Fe:native\tests\task_end_tests.exe && native\tests\task_end_tests.exe"

# 端到端回归（需要先编译 Release；截图与临时日志落在 qa-output\，不入库）
powershell -NoProfile -ExecutionPolicy Bypass -File qa\verify-parity.ps1      # 33 项：交互、气泡时长、托盘、每轮提示延迟、每轮提示音、随机语句与编辑器
powershell -NoProfile -ExecutionPolicy Bypass -File qa\verify-supervisor.ps1  # 7 项：托管与两种退出
powershell -NoProfile -ExecutionPolicy Bypass -File qa\verify-live.ps1        # 4 项：真实会话目录下的启动、待机 CPU、干净退出

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

第三轮（本次改动，`qa\verify-parity.ps1` 18 项 + `qa\verify-supervisor.ps1` 7 项 + 新增 `qa\verify-live.ps1` 4 项，全部 PASS）：

- 每轮提示延迟：新增目录监听（`core\session_watcher.cpp`）+ 把 `usage-cache.json` 的热窗口从 15 秒压到 2 秒。脚本实测：向伪造日志追加一轮 token 后 **1.6 秒**弹出气泡（改前最长约 20 秒）。
- 单元测试新增缓存新鲜度用例：追加一轮后 6 秒必须看到新数据；热窗口内仍复用缓存；`forceProbe=true` 会跳过热窗口。
- 新增 `native\tests\session_watcher_tests.cpp`：嵌套目录写文件必须回调（实测 2 次通知），根目录不存在时不抛异常，`Stop()` 幂等且不挂。
- 实机（真实 `%CODEX_HOME%`）：悬浮层正常启动，待机 12 秒 CPU 时间约 0.016 秒，`WM_CLOSE` 后自行退出（code 0）。
- 4 个单元测试（usage_snapshot / usage_monitor / bubble_policy / session_watcher）全部通过；Release x64 编译无错误无警告。

第四轮（本次改动，`qa\verify-parity.ps1` 26 项 + `qa\verify-supervisor.ps1` 7 项 + `qa\verify-live.ps1` 4 项 + 5 个单元测试，全部 PASS）：

- 随机语句改成可编辑的加权集合：新增 `core\quotes.cpp`（JSON 读写、`权重|文本` 解析、按权重抽取且最多重试 6 次避开上一条）与 `settings\quote_editor.cpp`（`--quotes` 编辑器窗口），托盘加「随机语句…」入口。脚本实测：配置 `{"t":"QA QUOTE ALPHA","w":1},{"t":"QA QUOTE BETA","w":9}` 时日志出现 `quote pick=1 of 2 weight=9 text=QA QUOTE BETA`；在编辑器里把文本改成 `5|QA EDITOR LINE` / `1|QA EDITOR SECOND`，保存后配置写成 `"w":5` / `"w":1`，悬浮层随即重载并显示新句子。
- 设置窗口不会抹掉 `quotes`：`SettingsState` 用 `FindQuoteSpan` 原样保留、原样写回；脚本触发一次拖拽回写后确认 `quotes` 仍在。
- 回归脚本加固：`verify-parity.ps1` 的 stage 1 改为跑在私有临时 `%LOCALAPPDATA%` / `%CODEX_HOME%` 上并附一条伪造会话——否则本机真实的 Codex 轮次会在 10 秒配额卡片测量的中间弹出提示气泡，把结果污染成“卡片从不收起”（本次实测复现过一次）；`verify-supervisor.ps1` 的托盘项改成按标签解析，不再用写死的下移次数。
- 新增 `native\tests\quote_tests.cpp`：解析/格式化往返、权重 clamp、JSON 往返（含引号、反斜杠、换行、非 ASCII）、`SetQuoteJson` 的追加与替换、加权分布、避重复、空集合。
- Release x64 编译无错误无警告。

第五轮（本次改动，`qa\verify-parity.ps1` 33 项 + `qa\verify-supervisor.ps1` 7 项 + `qa\verify-live.ps1` 4 项 + 6 个单元测试，全部 PASS）：

- 每轮提示音落地：新增 `core\task_end.cpp`（`taskEnd` JSON、上游 `preset:<组>:<按下|松开>` 解析、托盘轮换）与 `core\json_span.cpp`（通用的取值区间查找/替换，`quotes` 也改用它）；托盘加“每轮提示音”“提示音：…”，设置窗口加同名的两行（窗口高度 640 → 744），音频层 `PlayPress` / `PlayRelease` 改为返回是否真的播上。
- 脚本实测：`{"on":1,"sel":"preset:duck:release"}` + 音效开 → 日志 `turn sound sel=preset:duck:release label=小黄鸭·松开 set=1 press=0 played=1`；音效关 → `turn sound skipped (sound off)`；`on:0` → `turn sound skipped (taskEnd off)`。`played=1` 说明 MCI 真的打开了音轨，不是只走了代码路径。
- 三个写入方共用 `overlay.json` 互不覆盖：拖拽回写、随机语句编辑器保存之后，`taskEnd` 与 `quotes` 都还在（脚本各查一遍）。
- 新增 `native\tests\task_end_tests.cpp`：预设表与顺序、标签、`duck`/`fx1` → `Ya*`/`D*` 的映射、未知 `sel` 回落、托盘轮换的回绕、`on:true` 与 `on:1` 两种写法、JSON 往返（含引号与反斜杠）、span 原样搬运。
- 顺手修掉：`qa\verify-parity.ps1` 读调试日志没带 `-Encoding UTF8`，中文标签在 PowerShell 5.1 下被按 GBK 解成乱码（日志文件本身是对的）。
- Release x64 编译无错误无警告。

第六轮（脚本健壮性，本机实测发现的两个真问题）：

- `install.ps1` / `status.ps1` / `uninstall.ps1` 之前是**无 BOM** 的 UTF-8：Windows PowerShell 5.1 按本机代码页 936 解码脚本，中文字面量变成乱码，末尾字节还会吃掉引号直接报 `Unexpected token`（实跑 `status.ps1` 复现）。三个脚本与新增的 `legacy-install.ps1` / `stop.ps1` 现在都是 UTF-8 with BOM，`Parser::ParseFile` 全部 0 错误。
- 新增 `legacy-install.ps1`：探测旧版 Electron 安装（计划任务动作是否指向 `WhaleLauncher-*.exe`、`launcher-state.json` 里的托管脚本是否还存在、启动器 / supervisor / Electron 进程、旧数据目录），并按进程树结束它。`status.ps1` 末尾会打印残留清单，`stop.ps1` 新增 `-WhatIf`（实测只列 8 个进程、不结束），`install.ps1` 切换任务动作时给出提示，`uninstall.ps1` 也会顺手收掉旧进程。
- `status.ps1` 读 `overlay.json` 补上 `-Encoding UTF8`（原先打印配置是乱码）。

## 8. 尚未实现 / 与上游原版差异

功能缺口（上游有、这里没有）：

1. API 余额与账本：`whale_balance` 工具、多服务商（账单 / New API / DeepSeek / 自定义 JSON）、币种与 Frankfurter 汇率、每轮费用估算、`turn-journal.json` 恢复。
2. 素材与角色：多角色图片、GIF 动图气泡、音频导入与裁剪、气泡样式编辑器、素材库管理（256MiB / 256 项等上限校验）。
3. 旧版数据迁移：Electron 时代的配置、素材、账本迁移到原生格式。
4. 横向翻转与自定义吸附区（上游「吸附与翻转」可拖拽设置，这里只有 24px 边缘吸附）。
5. 上游的配置 UI（素材库 / 角色 / 气泡样式编辑器 / 随机台词编辑）里只做了随机台词编辑（`--quotes`），其余没有；原生版的配置界面就是设置窗口加随机语句编辑器。

行为差异（当前实现与上游不同）：

1. 每轮提示内容：上游显示本轮 API 扣费金额（含 pending/unknown/失败中性文案），这里显示模型 + 本轮 token。
2. 气泡内容：上游气泡可放任意模块（文字大小/颜色/图片/GIF 混排），这里只有「两行配额」或「一行随机语句」。
3. 随机语句：这里已经是可编辑的加权集合（权重与热重载都对上了），但只有纯文本；上游的集合里还有峰谷提示、今日已用、GIF、卖萌吐槽这些动态条目。
4. 气泡内文字大小：上游用固定字号（`dshwv-label` 66 单位等），这里按气泡内可用宽度自动收缩。
5. 每轮提示延迟：上游用 `fs.watch` 秒级反应；这里改成 `ReadDirectoryChangesW` 监听 + 1.5 秒去抖，实测约 1.6 秒，已对齐。
6. 手感：上游气泡是 DOM 弹性动画（`cubic-bezier` 缩放淡入），这里分层窗口直接贴位，没有入场动画。
7. 每轮提示音触发条件：上游只在本轮账本判定为成功（`notice.completionKind === 'success'`）时播；这里没有账本，监控到“新的一轮”就播，分不出失败轮次。音源也只有 `preset:` 那四种，`grp:` / `frag:` 会回落到默认预设。

有意保留的边界：只监测本机 Codex 会话（不监测 ChatGPT 网页端）；只覆盖主显示器工作区，不覆盖全屏独占游戏与 UAC 安全桌面；首次冷启动需要扫描近期归档日志。

## 9. 已知坑

1. **抓屏**：悬浮层是 `UpdateLayeredWindow` 逐像素 Alpha，普通 `BitBlt` 抓不到，必须带 `CAPTUREBLT`（`0x40000000`）。
2. **DPI**：程序是 Per-Monitor V2；这台机器是 150% 缩放，物理分辨率 2520×1680，逻辑 1680×1120。用 DPI 不感知的 PowerShell 读窗口坐标会得到 1/1.5 的值，先调用 `SetProcessDpiAwarenessContext(-4)` 再量。
3. **定位窗口**：在 PowerShell 里 `FindWindow('ApiBalanceWhaleOverlayWindow', null)` 有时返回 0（原因未查明），用 `(Get-Process api-balance-whale).MainWindowHandle` 更可靠；程序内部用 `FindWindow` 通知重载是正常的。
4. **重装插件前**：先 `.\stop.ps1` 或结束进程，否则 exe 被占用会导致链接失败（`LNK1104`）。
5. **别用 WinUI 画悬浮层/设置页**：WinUI 3 的组合层做不出真正的逐像素透明，而且这台机器上 `XamlControlsResources` 会因 `AcrylicBackgroundFillColorDefaultBrush` 缺失直接崩溃（已因此改为纯 GDI+）。
6. **首次编译**：删掉 `native\obj` 后第一次构建会重新生成全部中间文件，耗时会明显变长。
7. `native\obj` 和 `native\bin` 不入库，换机器必须重新编译，然后重新 `codex plugin add` 才会用到新的 exe。
8. **缓存热窗口必须短于轮询间隔**：`usage-cache.json` 的“热窗口”现在是 2 秒（`usage_snapshot.cpp` 里的 `kHotCacheSeconds`），超时后按文件指纹（大小 + 修改时间）重新校验，所以一轮对话结束后最多再等一个 5 秒轮询。把它调大就会让每轮提示变慢——15 秒那版最长要等 20 秒。目录监听触发的刷新带 `forceProbe=true`，会直接跳过热窗口。
9. **全局热键可能被占用**：`Ctrl+Alt+W` 经常被别的软件抢走（本机实测 `RegisterHotKey` 返回 1409）。程序会自动回退 `Ctrl+Shift+W`；两个都失败时托盘首项显示“不可用（已被占用）”。
10. **回调里的鼠标坐标是屏幕坐标**：`HandleClick` 只接受客户区坐标，必须先 `ScreenToClient`。0.3.0 初版漏了这一步，导致点气泡实际走成点鲸鱼（本次已修）。
11. **`ReadDirectoryChangesW` 的目录句柄必须带 `FILE_FLAG_OVERLAPPED`**：否则完成事件永远不置位，`GetOverlappedResult(..., TRUE)` 会永久阻塞（本次踩过一次：测试进程 0 CPU 卡死）。现在 `session_watcher.cpp` 用 `FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED` 打开，等待上限 500 毫秒 + `CancelIoEx`，保证 `Stop()` 立刻返回。
12. **`Start-Process -WindowStyle Hidden` 会吃掉窗口的第一次 `ShowWindow`**：`STARTUPINFO.wShowWindow = SW_HIDE` 一旦生效，程序之后自己调 `ShowWindow(SW_SHOW)` 也救不回来，窗口永远不可见、`MainWindowHandle` 一直是 0。测 `--quotes` 编辑器时踩到过（`verify-parity.ps1` 里那行已经去掉 `-WindowStyle Hidden`）；悬浮层不受影响，因为它用 `UpdateLayeredWindow` + `WS_VISIBLE` 建窗口。
13. **跨进程 `SendMessage` 必须钉住 Unicode 入口**：`[DllImport("user32.dll")]` 不带 `CharSet` 会解析到 `SendMessageA`，它跨进程时按 ANSI 解释字符串指针，UTF-16 载荷在第一个 NUL 处被截断——`5|QA EDITOR LINE` 落到编辑器里只剩 `5`，保存出来就是一条 `{"t":"5","w":1}`。P/Invoke 要写 `CharSet=CharSet.Unicode, EntryPoint="SendMessageW"`。另外 `WM_GETTEXT` / `GetWindowTextLength` 跨进程读不到别的进程 Edit 的内容，别拿它做断言。
14. **Windows PowerShell 读日志要带 `-Encoding UTF8`**：调试日志是 UTF-8（`std::ofstream` + `WideToUtf8`），但 `Get-Content` 在 PowerShell 5.1 里默认按 ANSI（本机 936）解码，中文会显示成 `灏忛粍楦锋澗寮€` 这种乱码——文件没错，是读的人错了。`qa\verify-parity.ps1` 里读日志的地方都已补上 `-Encoding UTF8`。

15. **Windows PowerShell 5.1 按 ANSI 解码没有 BOM 的 `.ps1`**：本机代码页 936，脚本里的中文字面量会被解成乱码，字面量末尾的字节还可能把引号一起吃掉、直接变成语法错误（本轮实跑 `status.ps1` 报 `Unexpected token`）。带中文的 `.ps1` 必须存成 UTF-8 with BOM（前三个字节 `EF BB BF`）；`qa\verify-*.ps1` 一直有 BOM 所以没事，`install.ps1` / `status.ps1` / `uninstall.ps1` 是本轮补上的。所以 `.cmd` 包装器一律用 `powershell.exe -File` 跑这些脚本，别用 `Get-Content` 拼字符串再 `Invoke-Expression`。
16. **旧版 Electron 安装与原生版共用计划任务名**：`Codex API Balance Whale` 可能仍指向 `WhaleLauncher-*.exe` 与早已删掉的 `desktop\supervisor.ps1`，于是「任务在跑、`api-balance-whale` 却没进程、桌面上却有鲸鱼」。`install.ps1` 只会替换任务动作、不会停旧进程，正确顺序是 `.\stop.ps1` → `.\install.ps1`；`status.ps1` 会把这种残留报出来。

## 10. 建议的下一步（按性价比）

1. 快捷键做成可配置（并同步显示在设置窗口里，现在设置窗口只写了默认值）。
2. 余额/账本（`whale_balance`、多服务商、汇率、每轮费用）——工作量大，先定存储格式。
3. 素材库与多角色、气泡样式编辑器。
4. 随机语句支持动态占位符（上游 `bubbleContentTokenMap` 的 `{balance_api}` 之类），现在只有纯文本。
