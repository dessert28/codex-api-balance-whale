$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'legacy-install.ps1')

$taskName = 'Codex API Balance Whale'
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$runValue = 'ApiBalanceWhale'

if (Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue) {
  Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
  Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
}
if (Get-ItemProperty -Path $runKey -Name $runValue -ErrorAction SilentlyContinue) {
  Remove-ItemProperty -Path $runKey -Name $runValue -Force
}
Get-Process -Name 'api-balance-whale' -ErrorAction SilentlyContinue |
  Stop-Process -Force -ErrorAction SilentlyContinue

# 计划任务被旧版 Electron 的 WhaleLauncher 占着时，上面那步只会移掉任务、不会停进程。
$legacy = @(Stop-LegacyWhaleInstall)
if ($legacy.Count -gt 0) {
  Write-Output ("同时结束了旧版 Electron 残留进程：{0}" -f ($legacy -join ', '))
}
Write-Output "已卸载原生 Codex 小鲸鱼：$taskName（计划任务与当前用户启动项均已移除）"
