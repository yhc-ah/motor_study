$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Push-Location $root
try {
    gcc -std=c99 -Wall -Wextra -Werror -Ibsp/week4 tests/host/test_week4_math.c bsp/week4/signal_math.c -lm -o tests/host/test_week4_math.exe
    if ($LASTEXITCODE -ne 0) { throw 'Week4 signal math compile failed' }
    & ./tests/host/test_week4_math.exe
    if ($LASTEXITCODE -ne 0) { throw 'Week4 signal math tests failed' }
} finally {
    Pop-Location
}
