param([string]$ArmBin='E:/ProgramFileForSTM32/keil5/core/ARM/ARMCC/bin')
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
Push-Location $repo
try {
 $sources=@('ports/f411/main.c','device/mpu6050/mpu6050.c','protocol/frame/frame_codec.c','protocol/crc/crc16_modbus.c','Drivers/CMSIS/Device/ST/STM32F4xx/Source/Templates/system_stm32f4xx.c')
 foreach($name in @('hal','hal_cortex','hal_gpio','hal_rcc','hal_rcc_ex','hal_flash','hal_flash_ex','hal_pwr','hal_pwr_ex','hal_i2c','hal_dma')){$sources+="Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_$name.c"}
 $sources+='algorithm/week5_timing.c'
 $inc=@('algorithm','ports/f411','device/mpu6050','protocol/frame','protocol/crc','Drivers/CMSIS/Include','Drivers/CMSIS/Device/ST/STM32F4xx/Include','Drivers/STM32F4xx_HAL_Driver/Inc')
 $flags=@('--cpu=Cortex-M4.fp','--apcs=interwork','-O2','--split_sections','-DUSE_HAL_DRIVER','-DSTM32F411xE')
 foreach($dir in $inc){$flags+=@("-I$dir")}
 foreach($mode in @('c','cpp')) {
  $out="ports/f411/build/$mode";New-Item -ItemType Directory -Force $out | Out-Null
  $objs=@();$log=@()
  foreach($src in ($sources+$(if($mode -eq 'c'){'ports/f411/imu_c.c'}else{'ports/f411/imu_cpp.cpp'}))) {
   $obj="$out/$([IO.Path]::GetFileNameWithoutExtension($src)).o";$objs+=$obj
   [string[]]$language=if($src.EndsWith('.cpp')){@('--cpp','--no_exceptions','--no_rtti')}else{@('--c99')}
   $result=& "$ArmBin/armcc.exe" @flags @language -c $src -o $obj 2>&1
   $log+=$result;$result | Write-Output
   if($LASTEXITCODE -ne 0){throw "Compile failed: $src"}
  }
  $obj="$out/startup.o";$objs+=$obj
  & "$ArmBin/armasm.exe" --cpu=Cortex-M4.fp --apcs=interwork 'Drivers/CMSIS/Device/ST/STM32F4xx/Source/Templates/arm/startup_stm32f411xe.s' -o $obj
  if($LASTEXITCODE -ne 0){throw 'Assembly failed'}
  $result=& "$ArmBin/armlink.exe" @objs --scatter ports/f411/f411.sct --entry Reset_Handler --map --symbols --info sizes,totals --list "$out/f411.map" -o "$out/f411.axf" 2>&1
  $log+=$result;$result | Write-Output
  if($LASTEXITCODE -ne 0){throw 'Link failed'}
  & "$ArmBin/fromelf.exe" --i32combined --output "$out/f411.hex" "$out/f411.axf"
  if($LASTEXITCODE -ne 0){throw 'HEX conversion failed'}
  $log | Set-Content "$out/build.log"
  Write-Output "PASS F411 $mode : $out/f411.axf"
 }
} finally {Pop-Location}
