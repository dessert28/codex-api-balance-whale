$ErrorActionPreference = 'Stop'
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
Write-Output "已卸载原生 Codex 小鲸鱼：$taskName（计划任务与当前用户启动项均已移除）"
