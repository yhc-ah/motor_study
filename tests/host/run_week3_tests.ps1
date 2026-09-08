$ErrorActionPreference = 'Stop'
function Check([string]$step) { if ($LASTEXITCODE -ne 0) { throw "$step failed: $LASTEXITCODE" } }
& ./tests/host/run_tests.ps1
& ./tests/host/run_flash_tests.ps1
gcc -std=c99 -Wall -Wextra -Werror -Idevice/mpu6050 tests/host/test_mpu6050.c device/mpu6050/mpu6050.c -o tests/host/test_mpu6050.exe
Check 'compile MPU'
& ./tests/host/test_mpu6050.exe
Check 'MPU tests'
gcc -std=c99 -Wall -Wextra -Werror -Ibsp/uart tests/host/test_tx_queue.c bsp/uart/tx_queue.c -o tests/host/test_tx_queue.exe
Check 'compile TX queue'
& ./tests/host/test_tx_queue.exe
Check 'TX queue tests'
gcc -std=c99 -Wall -Wextra -Werror -Itests/host/tx_stubs -Ibsp/uart tests/host/test_uart_tx.c bsp/uart/bsp_uart_tx.c bsp/uart/tx_queue.c -o tests/host/test_uart_tx.exe
Check 'compile UART TX'
& ./tests/host/test_uart_tx.exe
Check 'UART TX tests'
gcc -std=c99 -Wall -Wextra -Werror '-D__weak=__attribute__((weak))' -DWEEK3_FLASH_TEST_SIZE=4096 `
    -Itests/host/stubs -Iapp -Ibsp/sensors -Ibsp/uart -Idevice/mpu6050 -Idevice/spi_flash `
    -Iprotocol/parser -Iprotocol/frame -Iprotocol/crc tests/host/test_app_sensors.c `
    app/app_sensors.c device/mpu6050/mpu6050.c device/spi_flash/spi_flash.c `
    device/spi_flash/spi_flash_test.c protocol/frame/frame_codec.c protocol/parser/frame_parser.c `
    protocol/crc/crc16_modbus.c -o tests/host/test_app_sensors.exe
Check 'compile app integration'
& ./tests/host/test_app_sensors.exe
Check 'app integration tests'
python -m unittest tests.host.test_sensor_tool -v
Check 'sensor PC tests'
Write-Output 'Week3 host validation passed; hardware acceptance is separate.'
