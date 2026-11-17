# run_tests.ps1 - build and run PowerToggle unit tests
$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$Src  = Join-Path $Root "src"
$Bin  = Join-Path $Root "build"
$Exe  = Join-Path $Bin "test_powertoggle.exe"

New-Item -ItemType Directory -Force -Path $Bin | Out-Null

if (-not (Get-Command g++ -ErrorAction SilentlyContinue)) {
    throw "g++ not found in PATH"
}

$Modules = @("util.cpp", "power.cpp", "config.cpp", "idle.cpp", "icon.cpp", "tray.cpp") |
           ForEach-Object { Join-Path $Src $_ }

$TArgs = @("-std=c++17", "-O1", "-g", "-municode", "-DUNICODE", "-D_UNICODE",
           "-Wall", "-Wextra", "-I", $Src, "-o", $Exe,
           (Join-Path $PSScriptRoot "test_main.cpp")) + $Modules + @(
           "-lpowrprof", "-lshell32", "-lole32", "-luser32", "-ladvapi32", "-lgdi32")

& g++ @TArgs
if ($LASTEXITCODE -ne 0) { throw "test build failed" }

& $Exe
$code = $LASTEXITCODE
Write-Host ""
if ($code -eq 0) { Write-Host "[tests] ALL PASSED" } else { Write-Host "[tests] FAILURES: $code" }
exit $code
