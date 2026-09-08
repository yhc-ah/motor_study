$ErrorActionPreference = "Stop"

$gcc = "gcc"

function Assert-NativeSuccess {
    param([string]$Step)
    if ($LASTEXITCODE -ne 0) {
        throw "$Step failed with exit code $LASTEXITCODE"
    }
}

& $gcc -std=c11 -Wall -Wextra -Werror -DBYTE_RING_HOST_TEST `
    -Iprotocol/ring tests/host/test_byte_ring.c `
    protocol/ring/byte_ring.c -o tests/host/test_byte_ring.exe
Assert-NativeSuccess "compile test_byte_ring"
& .\tests\host\test_byte_ring.exe
Assert-NativeSuccess "run test_byte_ring"

& $gcc -std=c11 -Wall -Wextra -Werror -Iprotocol/crc `
    tests/host/test_crc16.c protocol/crc/crc16_modbus.c `
    -o tests/host/test_crc16.exe
Assert-NativeSuccess "compile test_crc16"
& .\tests\host\test_crc16.exe
Assert-NativeSuccess "run test_crc16"

& $gcc -std=c11 -Wall -Wextra -Werror `
    -Iprotocol/crc -Iprotocol/frame -Iprotocol/parser `
    tests/host/test_frame_parser.c protocol/crc/crc16_modbus.c `
    protocol/frame/frame_codec.c protocol/parser/frame_parser.c `
    -o tests/host/test_frame_parser.exe
Assert-NativeSuccess "compile test_frame_parser"
& .\tests\host\test_frame_parser.exe
Assert-NativeSuccess "run test_frame_parser"

& $gcc -std=c11 -Wall -Wextra -Werror `
    -DCOMMAND_DISPATCH_HOST_TEST -DBYTE_RING_HOST_TEST `
    -Itests/host/stubs -Iapp -Iprotocol/ring -Iprotocol/crc `
    -Iprotocol/frame -Iprotocol/parser `
    tests/host/test_command_dispatch.c app/command_dispatch.c `
    protocol/ring/byte_ring.c protocol/crc/crc16_modbus.c `
    protocol/frame/frame_codec.c protocol/parser/frame_parser.c `
    -o tests/host/test_command_dispatch.exe
Assert-NativeSuccess "compile test_command_dispatch"
& .\tests\host\test_command_dispatch.exe
Assert-NativeSuccess "run test_command_dispatch"

python -m unittest tests.host.test_uart_tool -v
Assert-NativeSuccess "run Python unit tests"
python tools/uart_test/uart_test.py --self-test
Assert-NativeSuccess "run UART tool self-test"

Write-Output "All host tests passed."
