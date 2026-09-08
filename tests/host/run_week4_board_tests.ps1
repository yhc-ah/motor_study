$ErrorActionPreference='Stop'
gcc -std=c99 -Wall -Wextra -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -Wno-misleading-indentation -DSTM32F407xx -DUSE_HAL_DRIVER `
 -include tests/host/week4_board_stubs.h -Itests/host -ICore/Inc -Iapp -Ibsp/week4 -Ibsp/sensors -Ibsp/led `
 -isystem Drivers/STM32F4xx_HAL_Driver/Inc -isystem Drivers/CMSIS/Device/ST/STM32F4xx/Include -isystem Drivers/CMSIS/Include `
 tests/host/test_week4_board.c bsp/week4/bsp_week4.c bsp/week4/adc_handover.c bsp/week4/signal_math.c -o tests/host/test_week4_board.exe
if($LASTEXITCODE -ne 0){throw 'BSP host compile failed'}
& ./tests/host/test_week4_board.exe
if($LASTEXITCODE -ne 0){throw 'BSP host tests failed'}
