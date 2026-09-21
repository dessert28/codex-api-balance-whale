# 结束原生悬浮层；顺带收掉旧版 Electron 安装的残留进程（如果有）。
# 只想看看会动到谁：.\stop.ps1 -WhatIf
[CmdletBinding(SupportsShouldProcess = $true)]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'legacy-install.ps1')

$native = Get-Process -Name 'api-balance-whale' -ErrorAction SilentlyContinue
if ($native) {
  foreach ($proc in $native) {
    if ($PSCmdlet.ShouldProcess("api-balance-whale PID $($proc.Id)", 'Stop-Process')) {
      Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    }
  }
} else {
  Write-Output 'api-balance-whale: stopped'
}

$legacy = @(Stop-LegacyWhaleInstall -DryRun:([bool]$WhatIfPreference))
if ($legacy.Count -eq 0) {
  Write-Output '旧版 Electron 残留：没有在跑的旧版进程'
} else {
  $verb = '已结束'
  if ($WhatIfPreference) { $verb = '将结束' }
  Write-Output ("旧版 Electron 残留：{0} {1} 个进程（{2}）" -f $verb, $legacy.Count, ($legacy -join ', '))
}
