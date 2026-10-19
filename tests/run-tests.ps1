param(
  [string]$BackendRoot = 'D:\code_capstone\smart-locking-be',
  [string]$Compiler = 'C:\msys64\ucrt64\bin\g++.exe'
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskOutput = Join-Path ([System.IO.Path]::GetTempPath()) ('locker-firmware-check-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskOutput | Out-Null
$taskCore = Join-Path $taskRoot 'firmware'
$taskBinary = Join-Path $taskOutput 'core-tests.exe'
& $Compiler -std=c++17 -O1 -Wall -Wextra -Werror -I (Join-Path $PSScriptRoot 'support') -I $taskCore (Join-Path $taskCore 'Protocol.cpp') (Join-Path $taskCore 'LockerCore.cpp') (Join-Path $taskCore 'StateStore.cpp') (Join-Path $PSScriptRoot 'core_tests.cpp') -o $taskBinary
if ($LASTEXITCODE -ne 0) { throw 'Native build failed.' }
$taskHardware = Join-Path $taskOutput 'hardware-tests.exe'
& $Compiler -std=c++17 -O1 -Wall -Wextra -Werror -I (Join-Path $PSScriptRoot 'support') -I $taskCore (Join-Path $taskCore 'Hardware.cpp') (Join-Path $PSScriptRoot 'hardware_tests.cpp') -o $taskHardware
if ($LASTEXITCODE -ne 0) { throw 'Hardware adapter test build failed.' }
& $taskHardware
if ($LASTEXITCODE -ne 0) { throw 'Hardware adapter verification failed.' }
$taskContract = Join-Path $PSScriptRoot 'ContractCheck/ContractCheck.csproj'
dotnet run --project $taskContract -p:BackendRoot=$BackendRoot -- generate $taskOutput
if ($LASTEXITCODE -ne 0) { throw 'BE codec command generation failed.' }
& $taskBinary $taskOutput
if ($LASTEXITCODE -ne 0) { throw 'Core verification failed.' }
dotnet run --project $taskContract -p:BackendRoot=$BackendRoot -- validate $taskOutput
if ($LASTEXITCODE -ne 0) { throw 'BE codec verification failed.' }
Write-Output "Verification artifacts: $taskOutput"
