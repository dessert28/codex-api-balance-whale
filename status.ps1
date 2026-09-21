$ErrorActionPreference = 'Stop'
$taskName = 'Codex API Balance Whale'
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$runValue = 'ApiBalanceWhale'
$configPath = Join-Path $env:LOCALAPPDATA 'Codex\api-balance-whale\overlay.json'

$processes = Get-Process -Name 'api-balance-whale' -ErrorAction SilentlyContinue
if ($processes) {
  $processes | Select-Object Id, MainWindowTitle, @{n='PrivateMB';e={[math]::Round($_.PrivateMemorySize64/1MB,1)}}, @{n='CpuSec';e={[math]::Round($_.TotalProcessorTime.TotalSeconds,1)}}
} else {
  Write-Output 'api-balance-whale: stopped'
}

$task = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
if ($task) {
  Write-Output ("scheduled task: {0}" -f $task.State)
} else {
  Write-Output 'scheduled task: not registered'
}

$runValueData = (Get-ItemProperty -Path $runKey -Name $runValue -ErrorAction SilentlyContinue).$runValue
if ($runValueData) {
  Write-Output "run key: $runValueData"
} else {
  Write-Output 'run key: not set'
}

if (Test-Path -LiteralPath $configPath) {
  Write-Output ("overlay config: " + (Get-Content -LiteralPath $configPath -Raw).Trim())
}
