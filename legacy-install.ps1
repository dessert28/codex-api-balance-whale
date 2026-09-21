# 旧版 Electron 安装的探测与停止。
#
# 0.2.0 时代的挂件是 Electron 版：`WhaleLauncher-*.exe` 托管 `desktop\supervisor.ps1`，
# 数据目录是 `%USERPROFILE%\.codex\whale-widget`。原生版重写时删掉了 `desktop\`，
# 但计划任务 `Codex API Balance Whale` 与已经跑起来的进程可能还指着旧路径，于是就会
# 出现「桌面上的小鲸鱼还是旧版、原生版根本没被拉起来」这种看不懂的状态。
# 由 status.ps1 / stop.ps1 / uninstall.ps1 点源加载；本文件本身不做任何事。

$script:LegacyDataDir = Join-Path $HOME '.codex\whale-widget'
$script:LegacyTaskName = 'Codex API Balance Whale'

function Get-LegacyWhaleInstall {
  $install = [pscustomobject]@{
    TaskAction    = ''
    IsLegacyTask  = $false
    DataDir       = $script:LegacyDataDir
    DataDirExists = Test-Path -LiteralPath $script:LegacyDataDir
    ScriptPath    = ''
    ScriptExists  = $false
    Launchers     = @()
    Supervisors   = @()
    Widgets       = @()
  }

  $task = Get-ScheduledTask -TaskName $script:LegacyTaskName -ErrorAction SilentlyContinue
  if ($task -and $task.Actions.Count -gt 0) {
    $install.TaskAction = [string]$task.Actions[0].Execute
    if ($install.TaskAction -like '*WhaleLauncher*') { $install.IsLegacyTask = $true }
  }

  $stateFile = Join-Path $script:LegacyDataDir 'launcher-state.json'
  if (Test-Path -LiteralPath $stateFile) {
    try {
      $state = Get-Content -LiteralPath $stateFile -Raw -Encoding UTF8 | ConvertFrom-Json
      if ($state.script) {
        $install.ScriptPath = [string]$state.script
        $install.ScriptExists = Test-Path -LiteralPath $install.ScriptPath
      }
    } catch {
      Write-Output ("旧版 launcher-state.json 读不出来：{0}" -f $_.Exception.Message)
    }
  }

  $install.Launchers = @(Get-Process -Name 'WhaleLauncher*' -ErrorAction SilentlyContinue)

  $procs = Get-CimInstance Win32_Process -ErrorAction SilentlyContinue
  $install.Supervisors = @($procs |
    Where-Object { $_.Name -eq 'powershell.exe' -and $_.CommandLine -and $_.CommandLine -like '*supervisor.ps1*' -and $_.CommandLine -like '*whale-widget*' } |
    ForEach-Object { Get-Process -Id $_.ProcessId -ErrorAction SilentlyContinue })
  $install.Widgets = @($procs |
    Where-Object { $_.Name -eq 'electron.exe' -and $_.CommandLine -and $_.CommandLine -like '*whale-widget*' } |
    ForEach-Object { Get-Process -Id $_.ProcessId -ErrorAction SilentlyContinue })

  return $install
}

function Get-LegacyProcessTree {
  param([int[]]$RootId)

  $byParent = @{}
  foreach ($proc in (Get-CimInstance Win32_Process -ErrorAction SilentlyContinue)) {
    $parent = [int]$proc.ParentProcessId
    if (-not $byParent.ContainsKey($parent)) { $byParent[$parent] = New-Object System.Collections.ArrayList }
    [void]$byParent[$parent].Add($proc)
  }

  $seen = New-Object System.Collections.ArrayList
  $queue = New-Object System.Collections.Queue
  foreach ($root in $RootId) { $queue.Enqueue([int]$root) }
  while ($queue.Count -gt 0) {
    $current = [int]$queue.Dequeue()
    if ($seen -contains $current) { continue }
    [void]$seen.Add($current)
    if ($byParent.ContainsKey($current)) {
      foreach ($child in $byParent[$current]) { $queue.Enqueue([int]$child.ProcessId) }
    }
  }
  return $seen
}

# 结束旧版启动器及其全部子进程（supervisor、Electron 挂件、渲染进程）。
# 没有启动器在跑时只报告，不碰别的进程；-DryRun 只列清单。
function Stop-LegacyWhaleInstall {
  param([switch]$DryRun)

  $install = Get-LegacyWhaleInstall
  $roots = @($install.Launchers | ForEach-Object { [int]$_.Id } | Sort-Object -Unique)
  if ($roots.Count -eq 0) { return @() }

  $actions = @()
  foreach ($procId in (Get-LegacyProcessTree -RootId $roots)) {
    $proc = Get-Process -Id $procId -ErrorAction SilentlyContinue
    if (-not $proc) { continue }
    $actions += ("{0}#{1}" -f $proc.ProcessName, $proc.Id)
    if (-not $DryRun) { Stop-Process -Id $procId -Force -ErrorAction SilentlyContinue }
  }
  return $actions
}
