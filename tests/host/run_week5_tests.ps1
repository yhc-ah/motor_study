$ErrorActionPreference='Stop'
function Check([string]$step){if($LASTEXITCODE -ne 0){throw "$step failed: $LASTEXITCODE"}}
& ./tests/host/run_week4_tests.ps1
gcc -std=c99 -Wall -Wextra -Werror -Ialgorithm tests/host/test_week5_timing.c algorithm/week5_timing.c -o tests/host/test_week5_timing.exe
Check 'timing compile'
& ./tests/host/test_week5_timing.exe
Check 'timing tests'
gcc -std=c99 -Wall -Wextra -Werror -Iapp -Idevice/spi_flash -Iprotocol/crc tests/host/test_week5_journal.c app/week5_journal.c device/spi_flash/spi_flash.c protocol/crc/crc16_modbus.c -o tests/host/test_week5_journal.exe
Check 'journal compile'
& ./tests/host/test_week5_journal.exe
Check 'journal tests'
gcc -std=c99 -Wall -Wextra -Werror '-D__weak=__attribute__((weak))' -Itests/host/stubs -Iapp -Ialgorithm -Ibsp/sensors -Ibsp/uart -Idevice/mpu6050 -Idevice/spi_flash -Iprotocol/parser -Iprotocol/frame -Iprotocol/crc tests/host/test_app_week5.c app/app_week5.c algorithm/week5_timing.c app/week5_journal.c device/mpu6050/mpu6050.c device/spi_flash/spi_flash.c protocol/frame/frame_codec.c protocol/parser/frame_parser.c protocol/crc/crc16_modbus.c -o tests/host/test_app_week5.exe
Check 'supervisor compile'
& ./tests/host/test_app_week5.exe
Check 'supervisor tests'
gcc -std=c99 -Wall -Wextra -Werror -Iprotocol/parser -Iprotocol/frame -Iprotocol/crc tests/host/test_week5_uart_matrix.c protocol/parser/frame_parser.c protocol/frame/frame_codec.c protocol/crc/crc16_modbus.c -o tests/host/test_week5_uart_matrix.exe
Check 'UART matrix compile'
& ./tests/host/test_week5_uart_matrix.exe
Check 'UART matrix tests'
python -m unittest tests.host.test_week5_tool tests.host.test_week5_flood -v
Check 'week5 Python tests'
Write-Output 'Week5 host checks passed. Physical acceptance is a separate gate.'
