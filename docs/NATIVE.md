# 原生实现说明

本插件 0.3.0 起是单一 C++ 程序 `native/bin/Release/api-balance-whale.exe`，运行时不依赖 Node、Electron 或 Windows App SDK。

## 进程模型

| 模式 | 作用 |
| --- | --- |
| `--supervisor` | 检测 Codex 生命周期：运行期间托管悬浮层；悬浮层被“本次退出挂件”关掉后保持待命，等 Codex 重启再拉起；托盘“完全退出”会让它一起结束 |
| `--overlay` | 桌面悬浮层本体；重复启动时会激活已有实例并显示配额 |
| `--settings` | 原生设置窗口（大小、音效、音效组、自动收起、开机自启） |
| `--quotes` | 随机语句编辑器：把 `overlay.json` 的 `quotes` 数组摊成「权重\|文本」多行文本，保存后通知悬浮层重载 |
| `--mcp` | stdio 上的 MCP 服务：`whale_codex_usage`、`whale_usage`、`whale_status`、`whale_open` |

## 悬浮层

- Win32 分层窗口 + GDI+ 逐像素 Alpha（`UpdateLayeredWindow`），背景完全透明，没有色键黑底。
- 进程声明 Per-Monitor V2 DPI 感知，画布与坐标都是物理像素。
- 气泡按上游 SVG 几何绘制：椭圆主体 + 两枚尾圆，描边 `#203170`，文字 `#536ba9` 加粗；字号按气泡内可用宽度自动收缩。
- 透明像素不参与命中测试，点击直接落到下面的窗口；鲸鱼与气泡区域可点击、拖拽，靠近工作区边缘自动吸附。
- 交互：点鲸鱼显示 5 小时/本周配额（固定 10 秒收起，再点只是重新计时）；点气泡把配额卡片换成随机语句；再点随机语句或右键即收起；每轮新 token 会按“提示时长”自动提示一次。
- 随机语句按权重抽取，并最多重试 6 次避开上一条（对齐上游 `bubblePickLine` 的 `avoidIdx`）；句子集合来自配置里的 `quotes`，不是写死的。
- 每轮提示音对齐上游 `usageSet.taskEnd = {on, sel}`：本轮对话出现时先按 `sel` 播放（`sel` 默认 `preset:duck:press`），再决定是否弹提示气泡。播放前要过两道闸——`taskEnd.on` 与全局“音效”开关，任一为假都不响；日志会记下 `sel`、`set`、`press` 和 `played`，`played=0` 表示音轨没打开。
- 每类气泡各有寿命：配额卡片沿用上游 10 秒，随机语句用 `hideSeconds`，每轮提示用 `turnSeconds`；关掉“自动收起”后气泡一直留着，直到下一次点击。
- 悬浮层用 `ReadDirectoryChangesW` 递归监听 `%CODEX_HOME%\sessions`，日志一有写入就（去抖 1.5 秒）立刻重算，5 秒轮询只作兜底，所以每轮提示在日志写完后约 2 秒内出现。
- 全局快捷键默认 `Ctrl+Alt+W`（显示/收起配额卡片）。热键是按进程全局注册的，若已被别的程序占用就自动回退到 `Ctrl+Shift+W`，生效的组合写在托盘首项里。

## 托盘

悬浮层进程同时提供托盘图标：查看配额（含快捷键提示）、音效开关、音效组、大小预设（300/440/580）、开机自启、每轮提示开关、每轮提示音开关、“提示音：<组>·<按下|松开>”（点一下换下一个预设）、气泡自动收起开关、设置、随机语句…、“本次退出挂件（下次启动 Codex 恢复）”、“完全退出”。

“随机语句…”用同一个 exe 的 `--quotes` 模式打开编辑窗口：一行一条，写成 `权重|文本`（省略权重即 1，范围 1–99，空行忽略，非数字前缀时 `|` 算正文）。保存时 `SetQuoteJson` 只替换 `quotes` 字段、其它设置原样保留，然后发 `ApiBalanceWhaleReloadOverlay` 让悬浮层立即换用新集合；“恢复默认”写回内置的 10 条句子。

## 配置

`%LOCALAPPDATA%\Codex\api-balance-whale\overlay.json`

```json
{"size":440,"x":2040,"y":1128,"sound":1,"soundSet":0,"hideSeconds":5,"turnSeconds":6,"autoClose":1,"turnNotice":1,"taskEnd":{"on":1,"sel":"preset:duck:release"},"quotes":[{"t":"今天也要好好休息呀～","w":1},{"t":"深呼吸，然后继续～","w":3}]}
```

- `x`/`y` 缺省时启动在右下角；设置窗口的“恢复默认位置”就是删掉这两个字段。
 - `hideSeconds` 是随机语句的收起秒数（3–120），`turnSeconds` 是每轮提示的收起秒数（3–120，默认 6，对齐上游 `ttlSec`）；`autoClose=0` 表示不自动收起，`turnNotice=0` 表示关闭每轮提示。
- 设置窗口保存后会给运行中的悬浮层发送 `ApiBalanceWhaleReloadOverlay` 注册消息，立即应用新配置。
- `quotes` 是加权随机语句集合，元素形如 `{"t":文本,"w":权重}`；缺省或为空时回落到内置的 10 条默认句子。设置窗口改别的选项时会原样保留这一段（`settings_window.cpp` 只在 `SaveState` 里逐个替换自己负责的字段）。
- `taskEnd` 是每轮提示音，形状与上游一致：`{"on":0|1,"sel":"preset:<duck|fx1>:<press|release>"}`，`sel` 还可以是上游的 `grp:` / `frag:` 形式（本移植没有相应素材，播放时按上游做法回落到默认预设）。可用的四个 preset 与上游音效选择器一致。
- 设置窗口、随机语句编辑器、悬浮层都会重写同一个文件，所以每个写入方只替换自己负责的字段：`quotes` 用 `FindQuoteSpan` 原样搬运，`taskEnd` 由设置窗口与悬浮层按 `{"on","sel"}` 序列化（`json_span.cpp` 提供通用的取值区间查找与替换）。

## 用量读取

- 只读 `%CODEX_HOME%\sessions` 与 `archived_sessions`，按每个 JSONL 的 `total_token_usage` 差值累计。
- 5 小时/周窗口取最新 `rate_limits.primary`/`secondary`；缓存写入同一目录下的 `usage-cache.json`，指纹（各文件大小 + 修改时间）没变就直接复用，一行都不用重新解析。
- 缓存里的“热窗口”只有 2 秒（`usage_snapshot.cpp` 的 `kHotCacheSeconds`）：必须短于 5 秒轮询，否则一轮对话结束后气泡要等缓存过期才弹。目录监听触发的刷新带 `forceProbe=true`，直接跳过这个热窗口按指纹重算。
- 不读取 `auth.json`，不联网，不监测 ChatGPT 网页端对话，也不查询 API 余额。

## 调试

- `WHALE_OVERLAY_DUMP=<png>`：只渲染画布并导出 PNG 后退出，用于检查气泡/鲸鱼绘制。
- `WHALE_OVERLAY_DEBUG=<log>`：记录托盘注册、窗口尺寸、`UpdateLayeredWindow` 结果等。
- 分层窗口需要带 `CAPTUREBLT` 的 `BitBlt` 才能被普通抓屏截到。
