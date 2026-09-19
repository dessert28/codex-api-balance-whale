param([string]$DataDir)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$args = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $root 'scripts\uninstall-follow.ps1'))
if ($DataDir) { $args += @('-DataDir', $DataDir) }
& powershell.exe @args
exit $LASTEXITCODE
