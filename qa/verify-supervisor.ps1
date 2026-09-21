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
public static class K {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
}
"@
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

# Opens the tray menu on demand and picks an entry with the keyboard.
function Invoke-TrayItem([IntPtr]$hwnd, [int]$downs) {
  [void][K]::PostMessage($hwnd, 0x8000 + 2, [IntPtr]$hwnd, [IntPtr]0x0205)
  Start-Sleep -Milliseconds 900
  for ($i = 0; $i -lt $downs; $i++) {
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
  Invoke-TrayItem $hwnd 11
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
  Invoke-TrayItem $hwnd 10
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
