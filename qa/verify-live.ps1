<#
Live smoke test against the real Codex home. It starts the overlay the way a
user would, checks that the idle refresh loop stays cheap with the session
watcher running, and asserts the window closes cleanly instead of hanging in
the watcher shutdown.

Usage (from the repository root, with the Release build already present):
  powershell -NoProfile -ExecutionPolicy Bypass -File qa\verify-live.ps1
#>
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'native\bin\Release\api-balance-whale.exe'
$artifacts = Join-Path $root 'qa-output'
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
$log = Join-Path $artifacts 'overlay-live.log'
$failures = 0

function Check([string]$name, [bool]$ok, [string]$detail) {
  $state = if ($ok) { 'PASS' } else { 'FAIL' }
  if (-not $ok) { $script:failures++ }
  "{0} {1}: {2}" -f $state, $name, $detail
}

Get-Process api-balance-whale -ErrorAction SilentlyContinue | Stop-Process -Force
if (Test-Path $log) { Remove-Item $log -Force }
$env:WHALE_OVERLAY_DEBUG = $log
$process = Start-Process -FilePath $exe -ArgumentList '--overlay' -WorkingDirectory $root -WindowStyle Hidden -PassThru
for ($i = 0; $i -lt 40; $i++) {
  Start-Sleep -Milliseconds 250
  $process.Refresh()
  if ($process.MainWindowHandle -ne 0) { break }
}
Check 'overlay starts against the real Codex home' ($process.MainWindowHandle -ne 0) ("hwnd=$($process.MainWindowHandle)")

# Two measured windows: the first one includes the cold snapshot scan.
Start-Sleep -Seconds 12
$process.Refresh(); $first = $process.TotalProcessorTime.TotalSeconds
Start-Sleep -Seconds 12
$process.Refresh(); $idle = [math]::Round($process.TotalProcessorTime.TotalSeconds - $first, 3)
Check 'idle refresh stays cheap with the watcher armed' ($idle -lt 1.5) ("cpu over 12s = ${idle}s")

$process.Refresh()
[void]$process.CloseMainWindow()
$closed = $process.WaitForExit(5000)
Check 'window closes without hanging' $closed ("exited=$closed code=$($process.ExitCode)")

$text = if (Test-Path $log) { Get-Content -Raw $log } else { '' }
Check 'tray and DIB are up' ($text -match 'tray icon added') ($text -replace "`r?`n", ' | ')

""
"failures = $failures"
if ($failures -gt 0) { exit 1 }
