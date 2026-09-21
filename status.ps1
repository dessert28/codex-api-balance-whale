$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'legacy-install.ps1')

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
  Write-Output ("overlay config: " + (Get-Content -LiteralPath $configPath -Raw -Encoding UTF8).Trim())
}

# 旧版 Electron 残留：不报出来的话，“桌面上的小鲸鱼到底是谁在画”根本看不出来。
$legacy = Get-LegacyWhaleInstall
if ($legacy.IsLegacyTask -or $legacy.Launchers.Count -gt 0) {
  Write-Output '--- 旧版 Electron 安装残留 ---'
  if ($legacy.IsLegacyTask) {
    Write-Output ("计划任务仍指向旧版启动器：{0}" -f $legacy.TaskAction)
  }
  if ($legacy.ScriptPath) {
    $state = '存在'
    if (-not $legacy.ScriptExists) { $state = '已不存在' }
    Write-Output ("旧版托管脚本：{0}（{1}）" -f $legacy.ScriptPath, $state)
  }
  if ($legacy.Launchers.Count -gt 0) {
    Write-Output ("旧版启动器进程：{0}" -f (($legacy.Launchers | ForEach-Object { "$($_.ProcessName)#$($_.Id)" }) -join ', '))
  }
  if ($legacy.Widgets.Count -gt 0) {
    Write-Output ("旧版 Electron 挂件进程：{0}" -f (($legacy.Widgets | ForEach-Object { "$($_.ProcessName)#$($_.Id)" }) -join ', '))
  }
  if ($legacy.DataDirExists) {
    Write-Output ("旧版数据目录仍在：{0}（原生版不用它，可保留或自行删除）" -f $legacy.DataDir)
  }
  Write-Output '桌面上的小鲸鱼是旧版、原生版没在跑时：先 .\stop.ps1 结束旧版，再 .\install.ps1 把自启切到原生 exe。'
}
