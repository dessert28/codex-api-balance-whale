<#
Regression harness for the desktop whale interactions. It drives the real
window (clicks, hotkey, tray menu) and measures the transparent bubble through
CAPTUREBLT screenshots, then checks each upstream-parity rule.

Usage (from the repository root, with the Release build already present):
  powershell -NoProfile -ExecutionPolicy Bypass -File qa\verify-parity.ps1

Artifacts land in qa-output\ (gitignored). The tray menu indices used by the
keyboard navigation match the item order in desktop_overlay.cpp; update them
when the menu changes.
#>
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class Win {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr GetDC(IntPtr h);
  [DllImport("user32.dll")] public static extern int ReleaseDC(IntPtr h, IntPtr dc);
  [DllImport("gdi32.dll")] public static extern bool BitBlt(IntPtr d, int x, int y, int w, int h, IntPtr s, int sx, int sy, int rop);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
  [DllImport("user32.dll")] public static extern int GetMenuItemCount(IntPtr menu);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetMenuString(IntPtr menu, uint item, StringBuilder text, int max, uint flags);
}
"@
# The harness must be per-monitor aware too, otherwise every window rectangle
# and capture comes back scaled down by 1.5 on this machine.
[void][Win]::SetProcessDpiAwarenessContext([IntPtr](-4))

$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'native\bin\Release\api-balance-whale.exe'
$log = Join-Path $PSScriptRoot 'overlay-debug.log'
# Screenshots and the fake session logs are scratch artifacts and stay out of
# the repository.
$artifacts = Join-Path $root 'qa-output'
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
$log = Join-Path $artifacts 'overlay-debug.log'
$failures = 0

function Check([string]$name, [bool]$ok, [string]$detail) {
  $state = if ($ok) { 'PASS' } else { 'FAIL' }
  if (-not $ok) { $script:failures++ }
  "{0} {1}: {2}" -f $state, $name, $detail
}

function Stop-Overlay {
  Get-Process api-balance-whale -ErrorAction SilentlyContinue | Stop-Process -Force
  Start-Sleep -Milliseconds 500
}

function Start-Overlay {
  if (Test-Path $log) { Remove-Item $log -Force }
  $env:WHALE_OVERLAY_DEBUG = $log
  Start-Process -FilePath $exe -ArgumentList '--overlay' -WorkingDirectory $root -WindowStyle Hidden | Out-Null
  for ($i = 0; $i -lt 40; $i++) {
    Start-Sleep -Milliseconds 250
    $p = Get-Process api-balance-whale -ErrorAction SilentlyContinue
    if ($p -and $p.MainWindowHandle -ne 0) { return $p.MainWindowHandle }
  }
  throw 'overlay window did not appear'
}

function Shot([IntPtr]$hwnd) {
  $r = New-Object Win+RECT
  [void][Win]::GetWindowRect($hwnd, [ref]$r)
  $w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
  $bmp = New-Object System.Drawing.Bitmap($w, $h)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $dst = $g.GetHdc(); $src = [Win]::GetDC([IntPtr]::Zero)
  # CAPTUREBLT: a per-pixel alpha layered window is skipped by a plain BitBlt.
  [void][Win]::BitBlt($dst, 0, 0, $w, $h, $src, $r.Left, $r.Top, 0x40CC0020)
  [Win]::ReleaseDC([IntPtr]::Zero, $src); $g.ReleaseHdc($dst); $g.Dispose()
  @{ Bitmap = $bmp; Rect = $r; Width = $w; Height = $h }
}

# Opaque bubble pixels; the card covers x 81..827 and y 15..479 of the view box.
function BubblePixels($shot) {
  $bmp = $shot.Bitmap; $u = $shot.Width / 1026.0
  $count = 0
  for ($y = [int](15 * $u); $y -lt [int](479 * $u); $y += 2) {
    for ($x = [int](81 * $u); $x -lt [int](827 * $u); $x += 2) {
      $p = $bmp.GetPixel($x, $y)
      if ($p.R -gt 235 -and $p.G -gt 235 -and $p.B -gt 235) { $count++ }
    }
  }
  $count
}

function Save-Shot($shot, [string]$name) {
  $shot.Bitmap.Save((Join-Path $artifacts $name), [System.Drawing.Imaging.ImageFormat]::Png)
}

$VISIBLE = 1500

function Press-Combo([string]$label) {
  $keys = switch ($label) {
    'Ctrl+Alt+W' { @(0x11, 0x12, 0x57) }
    'Ctrl+Shift+W' { @(0x11, 0x10, 0x57) }
    default { throw "unknown hotkey $label" }
  }
  foreach ($k in $keys) { [Win]::keybd_event([byte]$k, 0, 0, [UIntPtr]::Zero) }
  foreach ($k in $keys[($keys.Count - 1)..0]) { [Win]::keybd_event([byte]$k, 0, 2, [UIntPtr]::Zero) }
}

function Click-Client([IntPtr]$hwnd, [int]$x, [int]$y) {
  $lp = [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF))
  [void][Win]::PostMessage($hwnd, 0x0201, [IntPtr]1, $lp)
  Start-Sleep -Milliseconds 60
  [void][Win]::PostMessage($hwnd, 0x0202, [IntPtr]0, $lp)
}

function Write-Config([string]$dir, [string]$json) {
  $cfgDir = Join-Path $dir 'Codex\api-balance-whale'
  New-Item -ItemType Directory -Force -Path $cfgDir | Out-Null
  Set-Content -Path (Join-Path $cfgDir 'overlay.json') -Value $json -Encoding UTF8
}

# ---------------------------------------------------------------- stage 1
Stop-Overlay
$hwnd = Start-Overlay
$client = New-Object Win+RECT
[void][Win]::GetClientRect($hwnd, [ref]$client)
$size = $client.Right
Check 'dpi aware client size' ($size -eq 440) "client=$($client.Right)x$($client.Bottom)"

$whaleX = [int]($size * 0.70); $bubbleX = [int]($size * 0.4426); $bubbleY = [int]($size * 0.2408)

$shot = Shot $hwnd
$idle = BubblePixels $shot; Save-Shot $shot 'shot-idle.png'; $shot.Bitmap.Dispose()

$debug = Get-Content -LiteralPath $log -Raw
$hotkey = ([regex]'hotkey (\S+) registered').Match($debug)
Check 'global hotkey registered' $hotkey.Success ("log: " + ($debug -replace "`r?`n", ' | '))

if ($hotkey.Success) {
  Press-Combo $hotkey.Groups[1].Value
  Start-Sleep -Milliseconds 800
  $shot = Shot $hwnd
  $opened = BubblePixels $shot; Save-Shot $shot 'shot-hotkey-open.png'; $shot.Bitmap.Dispose()
  Start-Sleep -Milliseconds 200
  Press-Combo $hotkey.Groups[1].Value
  Start-Sleep -Milliseconds 800
  $shot = Shot $hwnd
  $reclosed = BubblePixels $shot; $shot.Bitmap.Dispose()
  Check 'hotkey toggles quota card' ($opened -gt $VISIBLE -and $reclosed -le $idle) "idle=$idle opened=$opened closed=$reclosed"
}

# A whale click shows the quota card and must keep it for the upstream 10 s.
$sw = [System.Diagnostics.Stopwatch]::StartNew()
Click-Client $hwnd $whaleX $whaleX
Start-Sleep -Milliseconds 900
$shot = Shot $hwnd
$quota = BubblePixels $shot; Save-Shot $shot 'shot-quota.png'; $shot.Bitmap.Dispose()
Check 'whale click shows quota card' ($quota -gt $VISIBLE) "pixels=$quota"

# Second click three seconds later must re-arm the same card, not swap it for a
# random line: the card then lives ~10 s instead of the 5 s bubble delay.
Start-Sleep -Seconds 3
Click-Client $hwnd $whaleX $whaleX
Start-Sleep -Milliseconds 900
$shot = Shot $hwnd
$quota2 = BubblePixels $shot; $shot.Bitmap.Dispose()
$sw.Restart()
$elapsed = 'never'
while ($sw.Elapsed.TotalSeconds -lt 16) {
  Start-Sleep -Milliseconds 300
  $shot = Shot $hwnd
  $now = BubblePixels $shot; $shot.Bitmap.Dispose()
  if ($now -le ($idle + 200)) { $elapsed = [math]::Round($sw.Elapsed.TotalSeconds, 1); break }
}
Check 'second whale click keeps quota card' ($quota2 -gt $VISIBLE) "pixels=$quota2"
Check 'quota card auto close ~10s' ($elapsed -ne 'never' -and $elapsed -ge 8.5 -and $elapsed -le 12.5) "seconds=$elapsed"

# Clicking the card switches it to a random line, which uses the shorter bubble
# delay instead of the quota card's 10 s.
Click-Client $hwnd $whaleX $whaleX
Start-Sleep -Milliseconds 800
$shot = Shot $hwnd
$quota3 = BubblePixels $shot; $shot.Bitmap.Dispose()
Click-Client $hwnd $bubbleX $bubbleY
Start-Sleep -Milliseconds 800
$shot = Shot $hwnd
$random = BubblePixels $shot; Save-Shot $shot 'shot-random.png'; $shot.Bitmap.Dispose()
$sw.Restart()
$randomSeconds = 'never'
while ($sw.Elapsed.TotalSeconds -lt 12) {
  Start-Sleep -Milliseconds 250
  $shot = Shot $hwnd
  $now = BubblePixels $shot; $shot.Bitmap.Dispose()
  if ($now -le ($idle + 200)) { $randomSeconds = [math]::Round($sw.Elapsed.TotalSeconds, 1); break }
}
Check 'card click switches to a random line' ($quota3 -gt $VISIBLE -and $random -gt $VISIBLE -and $randomSeconds -ne 'never' -and $randomSeconds -le 7.5) "quota=$quota3 random=$random seconds=$randomSeconds"

# Clicking the line itself dismisses the card.
Click-Client $hwnd $whaleX $whaleX
Start-Sleep -Milliseconds 700
Click-Client $hwnd $bubbleX $bubbleY
Start-Sleep -Milliseconds 700
Click-Client $hwnd $bubbleX $bubbleY
Start-Sleep -Milliseconds 800
$shot = Shot $hwnd
$dismissed = BubblePixels $shot; $shot.Bitmap.Dispose()
Check 'line click dismisses the card' ($dismissed -le $idle) "pixels=$dismissed"

# A drag is also the write path for the tuning keys.
$lp = [IntPtr](($whaleX -shl 16) -bor $whaleX)
[void][Win]::PostMessage($hwnd, 0x0201, [IntPtr]1, $lp)
Start-Sleep -Milliseconds 80
$dragX = $whaleX - 40
[void][Win]::PostMessage($hwnd, 0x0200, [IntPtr]1, [IntPtr](($whaleX -shl 16) -bor ($dragX -band 0xFFFF)))
Start-Sleep -Milliseconds 80
[void][Win]::PostMessage($hwnd, 0x0202, [IntPtr]0, [IntPtr](($whaleX -shl 16) -bor ($dragX -band 0xFFFF)))
Start-Sleep -Milliseconds 500
$config = (Get-Content -LiteralPath (Join-Path $env:LOCALAPPDATA 'Codex\api-balance-whale\overlay.json') -Raw).Trim()
Check 'config keeps new tuning keys' ($config -match 'turnSeconds' -and $config -match 'autoClose' -and $config -match 'turnNotice') $config

# ---------------------------------------------------------------- stage 2
[void][Win]::PostMessage($hwnd, 0x8000 + 2, [IntPtr]$hwnd, [IntPtr]0x0205)
Start-Sleep -Milliseconds 900
$menuWnd = [Win]::FindWindow('#32768', $null)
$items = @()
if ($menuWnd -ne [IntPtr]::Zero) {
  $menu = [Win]::SendMessage($menuWnd, 0x01E1, [IntPtr]::Zero, [IntPtr]::Zero)
  for ($i = 0; $i -lt [Win]::GetMenuItemCount($menu); $i++) {
    $sb = New-Object System.Text.StringBuilder 160
    [void][Win]::GetMenuString($menu, [uint32]$i, $sb, 160, 0x400)
    if ($sb.Length -gt 0) { $items += $sb.ToString() }
  }
}
[Win]::keybd_event(0x1B, 0, 0, [UIntPtr]::Zero)
[Win]::keybd_event(0x1B, 0, 2, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 300
[void][Win]::PostMessage($hwnd, 0x001F, [IntPtr]::Zero, [IntPtr]::Zero)
$joined = $items -join ' | '
Check 'tray has session exit' ($joined -match '本次退出挂件') $joined
Check 'tray has full exit' ($joined -match '完全退出') $joined
Check 'tray has turn notice toggle' ($joined -match '每轮提示') $joined
Check 'tray has auto close toggle' ($joined -match '气泡自动收起') $joined
Stop-Overlay

# ---------------------------------------------------------------- stage 3
function Invoke-TurnNotice([int]$turnNotice, [string]$tag) {
  $dir = Join-Path $artifacts $tag
  Remove-Item $dir -Recurse -Force -ErrorAction SilentlyContinue
  Write-Config $dir ("{`"size`":440,`"sound`":0,`"soundSet`":0,`"hideSeconds`":5,`"turnSeconds`":6,`"autoClose`":1,`"turnNotice`":$turnNotice}")
  $codexHome = Join-Path $dir 'codex'
  $sessionDir = Join-Path $codexHome 'sessions\2026\09\21'
  New-Item -ItemType Directory -Force -Path $sessionDir | Out-Null
  $jsonl = Join-Path $sessionDir 'rollout.jsonl'
  $quota = '{"primary":{"used_percent":13,"resets_at":1789707891},"secondary":{"used_percent":55,"resets_at":1789805325}}'
  $lines = @(
    '{"type":"session_meta","timestamp":"2026-09-21T01:00:00Z","payload":{"model":"gpt-5.6-terra"}}',
    ('{"type":"event_msg","timestamp":"2026-09-21T01:00:00Z","payload":{"type":"token_count","info":{"total_token_usage":{"input_tokens":100,"output_tokens":20}},"rate_limits":' + $quota + '}}')
  )
  Set-Content -Path $jsonl -Value $lines -Encoding ASCII
  $env:LOCALAPPDATA = $dir
  $env:CODEX_HOME = $codexHome
  $h = Start-Overlay
  Start-Sleep -Seconds 3
  $shot = Shot $h
  $baseline = BubblePixels $shot; $shot.Bitmap.Dispose()
  # A new turn appears in the log; the next 5 s scan must decide whether to show it.
  Add-Content -Path $jsonl -Value ('{"type":"event_msg","timestamp":"2026-09-21T01:10:00Z","payload":{"type":"token_count","info":{"total_token_usage":{"input_tokens":160,"output_tokens":30}},"rate_limits":' + $quota + '}}') -Encoding ASCII
  $peak = $baseline
  for ($i = 0; $i -lt 70; $i++) {
    Start-Sleep -Milliseconds 500
    $shot = Shot $h
    $now = BubblePixels $shot
    if ($now -gt $peak) { $peak = $now; Save-Shot $shot ("shot-" + $tag + ".png") }
    $shot.Bitmap.Dispose()
  }
  Stop-Overlay
  Remove-Item Env:\LOCALAPPDATA, Env:\CODEX_HOME -ErrorAction SilentlyContinue
  @{ Baseline = $baseline; Peak = $peak }
}

$on = Invoke-TurnNotice 1 'notice-on'
Check 'turn notice shown when enabled' ($on.Peak -gt $VISIBLE) ("baseline=$($on.Baseline) peak=$($on.Peak)")
$off = Invoke-TurnNotice 0 'notice-off'
Check 'turn notice suppressed when disabled' ($off.Peak -le ($off.Baseline + 200)) ("baseline=$($off.Baseline) peak=$($off.Peak)")

""
"failures = $failures"
