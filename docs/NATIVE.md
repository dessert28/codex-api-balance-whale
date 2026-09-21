# 原生实现说明

本插件 0.3.0 起是单一 C++ 程序 `native/bin/Release/api-balance-whale.exe`，运行时不依赖 Node、Electron 或 Windows App SDK。

## 进程模型

| 模式 | 作用 |
| --- | --- |
| `--supervisor` | 检测 Codex 生命周期：运行期间托管悬浮层；悬浮层被“本次退出挂件”关掉后保持待命，等 Codex 重启再拉起；托盘“完全退出”会让它一起结束 |
| `--overlay` | 桌面悬浮层本体；重复启动时会激活已有实例并显示配额 |
| `--settings` | 原生设置窗口（大小、音效、音效组、自动收起、开机自启） |
| `--mcp` | stdio 上的 MCP 服务：`whale_codex_usage`、`whale_usage`、`whale_status`、`whale_open` |

## 悬浮层

- Win32 分层窗口 + GDI+ 逐像素 Alpha（`UpdateLayeredWindow`），背景完全透明，没有色键黑底。
- 进程声明 Per-Monitor V2 DPI 感知，画布与坐标都是物理像素。
- 气泡按上游 SVG 几何绘制：椭圆主体 + 两枚尾圆，描边 `#203170`，文字 `#536ba9` 加粗；字号按气泡内可用宽度自动收缩。
- 透明像素不参与命中测试，点击直接落到下面的窗口；鲸鱼与气泡区域可点击、拖拽，靠近工作区边缘自动吸附。
- 交互：点鲸鱼显示 5 小时/本周配额（固定 10 秒收起，再点只是重新计时）；点气泡把配额卡片换成随机语句；再点随机语句或右键即收起；每轮新 token 会按“提示时长”自动提示一次。
- 每类气泡各有寿命：配额卡片沿用上游 10 秒，随机语句用 `hideSeconds`，每轮提示用 `turnSeconds`；关掉“自动收起”后气泡一直留着，直到下一次点击。
- 全局快捷键默认 `Ctrl+Alt+W`（显示/收起配额卡片）。热键是按进程全局注册的，若已被别的程序占用就自动回退到 `Ctrl+Shift+W`，生效的组合写在托盘首项里。

## 托盘

悬浮层进程同时提供托盘图标：查看配额（含快捷键提示）、音效开关、音效组、大小预设（300/440/580）、开机自启、每轮提示开关、气泡自动收起开关、设置、“本次退出挂件（下次启动 Codex 恢复）”、“完全退出”。

## 配置

`%LOCALAPPDATA%\Codex\api-balance-whale\overlay.json`

```json
{"size":440,"x":2040,"y":1128,"sound":1,"soundSet":0,"hideSeconds":5,"turnSeconds":6,"autoClose":1,"turnNotice":1}
```

- `x`/`y` 缺省时启动在右下角；设置窗口的“恢复默认位置”就是删掉这两个字段。
 - `hideSeconds` 是随机语句的收起秒数（3–120），`turnSeconds` 是每轮提示的收起秒数（3–120，默认 6，对齐上游 `ttlSec`）；`autoClose=0` 表示不自动收起，`turnNotice=0` 表示关闭每轮提示。
- 设置窗口保存后会给运行中的悬浮层发送 `ApiBalanceWhaleReloadOverlay` 注册消息，立即应用新配置。

## 用量读取

- 只读 `%CODEX_HOME%\sessions` 与 `archived_sessions`，按每个 JSONL 的 `total_token_usage` 差值累计。
- 5 小时/周窗口取最新 `rate_limits.primary`/`secondary`；缓存写入同一目录下的 `usage-cache.json`。
- 不读取 `auth.json`，不联网，不监测 ChatGPT 网页端对话，也不查询 API 余额。

## 调试

- `WHALE_OVERLAY_DUMP=<png>`：只渲染画布并导出 PNG 后退出，用于检查气泡/鲸鱼绘制。
- `WHALE_OVERLAY_DEBUG=<log>`：记录托盘注册、窗口尺寸、`UpdateLayeredWindow` 结果等。
- 分层窗口需要带 `CAPTUREBLT` 的 `BitBlt` 才能被普通抓屏截到。
