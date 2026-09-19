param([switch]$NoStart, [switch]$NoStartup, [string]$DataDir)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $root
try {
  node (Join-Path $root 'scripts\install-desktop.mjs')
  $args = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $root 'scripts\install-follow.ps1'))
  if ($NoStart) { $args += '-NoStart' }
  if ($NoStartup) { $args += '-NoStartup' }
  if ($DataDir) { $args += @('-DataDir', $DataDir) }
  & powershell.exe @args
  if ($LASTEXITCODE -ne 0) { throw "Scheduled task installation failed with exit code $LASTEXITCODE." }
} finally { Pop-Location }
