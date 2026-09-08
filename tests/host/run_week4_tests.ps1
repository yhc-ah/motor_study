$ErrorActionPreference='Stop'
function Check([string]$step) { if ($LASTEXITCODE -ne 0) { throw "$step failed: $LASTEXITCODE" } }
& ./tests/host/run_week3_tests.ps1
& ./tests/host/run_week4_math_tests.ps1
& ./tests/host/run_week4_board_tests.ps1
gcc -std=c99 -Wall -Wextra -Werror -Ibsp/week4 tests/host/test_week4_handover.c bsp/week4/adc_handover.c -o tests/host/test_week4_handover.exe
Check 'handover compile'
& ./tests/host/test_week4_handover.exe
Check 'handover tests'
gcc -std=c99 -Wall -Wextra -Werror -Itests/host/stubs -Iapp -Ibsp/week4 -Ibsp/uart -Iprotocol/parser -Iprotocol/frame -Iprotocol/crc tests/host/test_app_week4.c app/app_week4.c bsp/week4/signal_math.c protocol/frame/frame_codec.c protocol/crc/crc16_modbus.c -o tests/host/test_app_week4.exe
Check 'app compile'
& ./tests/host/test_app_week4.exe
Check 'app tests'
python -m unittest tests.host.test_week4_tool -v
Check 'PC tests'
Write-Output 'Week4 host validation passed; physical acceptance requires measurements.'
