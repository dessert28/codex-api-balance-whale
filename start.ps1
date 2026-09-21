$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $root 'native\bin\Release\api-balance-whale.exe'
if (-not (Test-Path -LiteralPath $exe)) {
  throw "Native build not found: $exe. Build native\ApiBalanceWhale.vcxproj with VS2022 first."
}
Start-Process -FilePath $exe -ArgumentList '--supervisor' -WorkingDirectory $root -WindowStyle Hidden | Out-Null
