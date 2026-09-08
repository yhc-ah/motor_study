$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$compiler = 'E:/Mingw/mingw64/bin/gcc.exe'
if (-not (Test-Path -LiteralPath $compiler)) { $compiler = 'gcc' }
$testOutput = Join-Path ([System.IO.Path]::GetTempPath()) 'week3_spi_flash_test.exe'
& $compiler -std=c99 -Wall -Wextra -Werror -pedantic -I (Join-Path $repoRoot 'device/spi_flash') (Join-Path $PSScriptRoot 'test_spi_flash.c') (Join-Path $repoRoot 'device/spi_flash/spi_flash.c') (Join-Path $repoRoot 'device/spi_flash/spi_flash_test.c') -o $testOutput
if ($LASTEXITCODE -ne 0) { throw 'SPI flash host compilation failed' }
& $testOutput
if ($LASTEXITCODE -ne 0) { throw 'SPI flash host tests failed' }
