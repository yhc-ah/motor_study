"""Idempotently synchronize week3 source groups / pins after CubeMX generation.
Does not invoke CubeMX or overwrite generated peripheral source files.
"""
from pathlib import Path
import re
ROOT = Path(__file__).resolve().parents[1]
project = ROOT / 'MDK-ARM/stm32-realtime-bsp.uvprojx'
text = project.read_text(encoding='utf-8')
paths = ['../bsp/sensors','../device/mpu6050','../device/spi_flash']
match = re.search(r'<IncludePath>([^<]*../Core/Inc[^<]*)</IncludePath>', text)
old=match.group(1)
includes=[p for p in old.split(';') if p != '../device/th_sensor']
includes.extend(p for p in paths if p not in includes)
text=text.replace(old,';'.join(includes),1)
files=['Core/Src/i2c.c','Core/Src/spi.c','Core/Src/tim.c',
       'Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_i2c.c',
       'Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_spi.c',
       'bsp/sensors/bsp_sensors.c','bsp/uart/tx_queue.c','bsp/uart/bsp_uart_tx.c',
       'device/mpu6050/mpu6050.c','device/spi_flash/spi_flash.c',
       'device/spi_flash/spi_flash_test.c','app/app_sensors.c']
entries=[]
for f in files:
    name=Path(f).name
    if f'<FileName>{name}</FileName>' not in text:
        entries.append(f'<File><FileName>{name}</FileName><FileType>1</FileType><FilePath>../{f}</FilePath></File>')
if entries:
    text=text.replace('</Groups>','<Group><GroupName>Week3</GroupName><Files>'+''.join(entries)+'</Files></Group>\n</Groups>')
project.write_text(text,encoding='utf-8')
ioc=ROOT/'stm32-realtime-bsp.ioc'
lines=ioc.read_text(encoding='utf-8').splitlines()
d=dict(line.split('=',1) for line in lines if '=' in line)
def add_list(prefix,values,countkey):
    keys=sorted((k for k in d if re.fullmatch(re.escape(prefix)+r'\d+',k)),
                key=lambda k:int(k[len(prefix):]))
    previous=[d[k] for k in keys]
    for k in list(d):
        if re.fullmatch(re.escape(prefix)+r'\d+',k):del d[k]
    for v in values:
        if v not in previous:previous.append(v)
    for n,v in enumerate(previous):d[prefix+str(n)]=v
    d[countkey]=str(len(previous))
add_list('Mcu.IP',['I2C1','SPI1','TIM2'],'Mcu.IPNb')
add_list('Mcu.Pin',['PB8','PB9','PB3','PB4','PB5','PG6','PC5','VP_TIM2_VS_ClockSourceINT'],'Mcu.PinsNb')
for pin,signal,mode in [('PB8','I2C1_SCL','I2C'),('PB9','I2C1_SDA','I2C'),('PB3','SPI1_SCK','Full_Duplex_Master'),('PB4','SPI1_MISO','Full_Duplex_Master'),('PB5','SPI1_MOSI','Full_Duplex_Master')]:
    d[pin+'.Signal']=signal;d[pin+'.Mode']=mode;d[pin+'.Locked']='true'
for pin,label,state in [('PG6','FLASH_CS','GPIO_PIN_SET'),('PC5','I2C_PROBE','GPIO_PIN_RESET')]:
    d[pin+'.Signal']='GPIO_Output';d[pin+'.GPIOParameters']='GPIO_Label,GPIO_Speed,PinState'
    d[pin+'.GPIO_Label']=label;d[pin+'.GPIO_Speed']='GPIO_SPEED_FREQ_VERY_HIGH';d[pin+'.PinState']=state
d.update({'I2C1.ClockSpeed':'400000','I2C1.IPParameters':'ClockSpeed',
          'SPI1.CalculateBaudRate':'656.25 KBits/s','SPI1.BaudRatePrescaler':'SPI_BAUDRATEPRESCALER_128',
          'SPI1.Direction':'SPI_DIRECTION_2LINES','SPI1.Mode':'SPI_MODE_MASTER',
          'SPI1.IPParameters':'Mode,Direction,BaudRatePrescaler,CalculateBaudRate',
          'TIM2.Prescaler':'83','TIM2.Period':'4294967295','TIM2.IPParameters':'Prescaler,Period',
          'VP_TIM2_VS_ClockSourceINT.Mode':'Internal','VP_TIM2_VS_ClockSourceINT.Signal':'TIM2_VS_ClockSourceINT',
          'USART1.BaudRate':'460800','USART1.IPParameters':'VirtualMode,BaudRate',
          'Dma.Request1':'USART1_TX','Dma.RequestsNb':'2'})
for k,v in list(d.items()):
    if k.startswith('Dma.USART1_RX.0.'):
        key=k.replace('USART1_RX','USART1_TX').replace('.0.','.1.')
        d[key]={'Instance':'DMA2_Stream7','Direction':'DMA_MEMORY_TO_PERIPH','Mode':'DMA_NORMAL','Priority':'DMA_PRIORITY_MEDIUM'}.get(k.rsplit('.',1)[1],v)
d['NVIC.DMA2_Stream7_IRQn']=r'true\:5\:0\:true\:false\:true\:true\:false\:true'
fn=d['ProjectManager.functionlistsort']
for n,name,ip in [(5,'MX_I2C1_Init','I2C1'),(6,'MX_SPI1_Init','SPI1'),(7,'MX_TIM2_Init','TIM2')]:
    if name not in fn:fn+=f',{n}-{name}-{ip}-false-HAL-true'
d['ProjectManager.functionlistsort']=fn
ioc.write_text('#MicroXplorer Configuration settings - do not modify\n'+'\n'.join(f'{k}={v}' for k,v in sorted(d.items()))+'\n',encoding='utf-8')
print('Week3 Keil groups and CubeMX pins synchronized.')
