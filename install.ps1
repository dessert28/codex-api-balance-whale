param([switch]$NoStart)
$ErrorActionPreference = 'Stop'

if ($PSVersionTable.PSEdition -ne 'Desktop') {
  $forward = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $PSCommandPath)
  if ($NoStart) { $forward += '-NoStart' }
  & "$env:WINDIR\System32\WindowsPowerShell\v1.0\powershell.exe" @forward
  exit $LASTEXITCODE
}

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $root 'native\bin\Release\api-balance-whale.exe'
$taskName = 'Codex API Balance Whale'
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$runValue = 'ApiBalanceWhale'

if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
  throw "找不到原生程序：$exe。请先用 VS2022 构建 native\ApiBalanceWhale.vcxproj。"
}

# 只保留一种自启方式：安装脚本用计划任务，托盘/设置里的“开机自启”用 Run 键。
if (Get-ItemProperty -Path $runKey -Name $runValue -ErrorAction SilentlyContinue) {
  Remove-ItemProperty -Path $runKey -Name $runValue -Force
}

$action = New-ScheduledTaskAction -Execute $exe -Argument '--supervisor' -WorkingDirectory $root
$principal = New-ScheduledTaskPrincipal -UserId ([Security.Principal.WindowsIdentity]::GetCurrent().Name) -LogonType Interactive -RunLevel Limited
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit ([TimeSpan]::Zero) -MultipleInstances IgnoreNew -StartWhenAvailable
$trigger = New-ScheduledTaskTrigger -AtLogOn -User ([Security.Principal.WindowsIdentity]::GetCurrent().Name)
$task = New-ScheduledTask -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Description 'Per-user Codex native whale supervisor.'
Register-ScheduledTask -TaskName $taskName -InputObject $task -Force | Out-Null
$registered = Get-ScheduledTask -TaskName $taskName -ErrorAction Stop
if ($registered.Actions.Count -ne 1 -or $registered.Actions[0].Execute -ine $exe -or $registered.Actions[0].Arguments -cne '--supervisor') {
  throw '计划任务创建后验证失败：动作不是原生 supervisor。'
}

if (-not $NoStart) {
  Start-ScheduledTask -TaskName $taskName
}
Write-Output "原生 Codex 小鲸鱼已安装：$taskName（登录自启，仅当前用户）"
