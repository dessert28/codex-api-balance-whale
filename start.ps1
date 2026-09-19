$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
& node (Join-Path $root 'scripts\control.mjs') open
exit $LASTEXITCODE
