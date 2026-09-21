$ErrorActionPreference = 'Stop'
Get-Process -Name 'api-balance-whale' -ErrorAction SilentlyContinue |
  Stop-Process -Force -ErrorAction SilentlyContinue
