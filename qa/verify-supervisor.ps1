<#
Checks the supervisor contract: "完全退出" ends the supervisor with the overlay,
while "本次退出挂件" only closes the overlay and leaves the supervisor dormant
until Codex starts again.

Usage (from the repository root, with the Release build already present):
  powershell -NoProfile -ExecutionPolicy Bypass -File qa\verify-supervisor.ps1
#>
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class K {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
  [DllImport("user32.dll")] public static extern int GetMenuItemCount(IntPtr menu);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetMenuString(IntPtr menu, uint item, StringBuilder text, int max, uint flags);
  [DllImport("user32.dll")] public static extern bool GetMenuItemRect(IntPtr hwnd, IntPtr menu, uint item, out RECT rect);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out RECT point);
  [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
}
"@
# The menu rectangles are physical pixels on a per-monitor aware process; without
# this the click would land at 1/1.5 of the intended position.
[void][K]::SetProcessDpiAwarenessContext([IntPtr](-4))
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'native\bin\Release\api-balance-whale.exe'
$failures = 0

function Check([string]$name, [bool]$ok, [string]$detail) {
  $state = if ($ok) { 'PASS' } else { 'FAIL' }
  if (-not $ok) { $script:failures++ }
  "{0} {1}: {2}" -f $state, $name, $detail
}

function Get-Whale([string]$mode) {
  Get-CimInstance Win32_Process -Filter "Name = 'api-balance-whale.exe'" -ErrorAction SilentlyContinue |
    Where-Object { $_.CommandLine -like "*$mode*" }
}

function Stop-Whales {
  Get-Whale '' | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
  Start-Sleep -Milliseconds 600
}

function Start-Supervisor {
  Start-Process -FilePath $exe -ArgumentList '--supervisor' -WorkingDirectory $root -WindowStyle Hidden | Out-Null
}

function Wait-Overlay {
  for ($i = 0; $i -lt 60; $i++) {
    Start-Sleep -Milliseconds 500
    $overlay = Get-Whale '--overlay'
    if ($overlay) {
      $p = Get-Process -Id $overlay.ProcessId -ErrorAction SilentlyContinue
      if ($p -and $p.MainWindowHandle -ne 0) { return $p.MainWindowHandle }
    }
  }
  return [IntPtr]::Zero
}

# Opens the tray menu on demand and picks an entry by label. The entry is clicked
# at its own menu rectangle: the keyboard route needs one Down press per *visible*
# item while the menu index space also holds separators, so any hardcoded or
# counted offset breaks as soon as the menu in desktop_overlay.cpp changes.
function Invoke-TrayLabel([IntPtr]$hwnd, [string]$label) {
  [void][K]::PostMessage($hwnd, 0x8000 + 2, [IntPtr]$hwnd, [IntPtr]0x0205)
  Start-Sleep -Milliseconds 900
  $menuWnd = [K]::FindWindow('#32768', $null)
  if ($menuWnd -eq [IntPtr]::Zero) { throw 'tray menu did not open' }
  $menu = [K]::SendMessage($menuWnd, 0x01E1, [IntPtr]::Zero, [IntPtr]::Zero)
  $labels = @()
  $target = -1
  for ($i = 0; $i -lt [K]::GetMenuItemCount($menu); $i++) {
    $sb = New-Object System.Text.StringBuilder 160
    [void][K]::GetMenuString($menu, [uint32]$i, $sb, 160, 0x400)
    $text = $sb.ToString()
    # Separators have no text.
    if ($text.Length -eq 0) { continue }
    $labels += $text
    if ($target -lt 0 -and $text -like "$label*") { $target = $i }
  }
  if ($target -lt 0) { throw "tray item missing: $label (have: $($labels -join ' | '))" }
  $rect = New-Object K+RECT
  if ([K]::GetMenuItemRect($menuWnd, $menu, [uint32]$target, [ref]$rect)) {
    $x = [int](($rect.Left + $rect.Right) / 2)
    $y = [int](($rect.Top + $rect.Bottom) / 2)
    # Clicking needs the real pointer, so put it back where the user left it.
    $before = New-Object K+RECT
    [void][K]::GetCursorPos([ref]$before)
    [void][K]::SetCursorPos($x, $y)
    Start-Sleep -Milliseconds 150
    [K]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 80
    [K]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 1200
    [void][K]::SetCursorPos($before.Left, $before.Top)
    return
  }
  # Fallback: walk the visible items with the keyboard.
  $visible = 0
  foreach ($text in $labels) {
    ++$visible
    if ($text -like "$label*") { break }
  }
  for ($i = 0; $i -lt $visible; $i++) {
    [K]::keybd_event(0x28, 0, 0, [UIntPtr]::Zero)
    [K]::keybd_event(0x28, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 120
  }
  [K]::keybd_event(0x0D, 0, 0, [UIntPtr]::Zero)
  [K]::keybd_event(0x0D, 0, 2, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 1200
}

# --- scenario A: 完全退出 stops the supervisor too
Stop-Whales
Start-Supervisor
$hwnd = Wait-Overlay
Check 'supervisor launches overlay while Codex runs' ($hwnd -ne [IntPtr]::Zero) "hwnd=$hwnd"
if ($hwnd -ne [IntPtr]::Zero) {
  Invoke-TrayLabel $hwnd '完全退出'
  $overlay = Get-Whale '--overlay'
  $supervisor = Get-Whale '--supervisor'
  Check 'full exit closes the overlay' (-not $overlay) ("overlay=" + ($overlay | Measure-Object).Count)
  Check 'full exit stops the supervisor' (-not $supervisor) ("supervisor=" + ($supervisor | Measure-Object).Count)
}

# --- scenario B: 本次退出挂件 leaves the supervisor dormant
Stop-Whales
Start-Supervisor
$hwnd = Wait-Overlay
Check 'overlay returns for the second scenario' ($hwnd -ne [IntPtr]::Zero) "hwnd=$hwnd"
if ($hwnd -ne [IntPtr]::Zero) {
  Invoke-TrayLabel $hwnd '本次退出挂件'
  $overlay = Get-Whale '--overlay'
  $supervisor = Get-Whale '--supervisor'
  Check 'session exit closes the overlay' (-not $overlay) ("overlay=" + ($overlay | Measure-Object).Count)
  Check 'session exit keeps the supervisor alive' ([bool]$supervisor) ("supervisor=" + ($supervisor | Measure-Object).Count)
  Start-Sleep -Seconds 6
  $overlay = Get-Whale '--overlay'
  $supervisor = Get-Whale '--supervisor'
  Check 'supervisor stays dormant without relaunching' ((-not $overlay) -and [bool]$supervisor) ("overlay=" + ($overlay | Measure-Object).Count + " supervisor=" + ($supervisor | Measure-Object).Count)
}

Stop-Whales
""
"failures = $failures"
