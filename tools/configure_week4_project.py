"""Add hand-owned week4 BSP to Keil; reserve its pins in CubeMX.

Week4 timers/ADC/DMA are owned by bsp_week4.c, not generated MX_* functions.
The IOC deliberately reserves GPIO pins without generating duplicate peripherals
or IRQ handlers. Consult docs/week-04 for full register/resource configuration.
"""
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[1]
path=ROOT/'MDK-ARM/stm32-realtime-bsp.uvprojx'
text=path.read_text(encoding='utf-8')
match=re.search(r'<IncludePath>([^<]*../Core/Inc[^<]*)</IncludePath>',text)
old=match.group(1)
if '../bsp/week4' not in old:text=text.replace(old,old+';../bsp/week4',1)
files=['bsp/week4/signal_math.c','bsp/week4/adc_handover.c','bsp/week4/bsp_week4.c','app/app_week4.c',
       'Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_adc.c','Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_adc_ex.c']
entries=[]
for f in files:
    if f'<FileName>{Path(f).name}</FileName>' not in text:
        entries.append(f'<File><FileName>{Path(f).name}</FileName><FileType>1</FileType><FilePath>../{f}</FilePath></File>')
if entries:text=text.replace('</Groups>','<Group><GroupName>Week4</GroupName><Files>'+''.join(entries)+'</Files></Group>\n</Groups>')
path.write_text(text,encoding='utf-8')
path=ROOT/'stm32-realtime-bsp.ioc'
d=dict(line.split('=',1) for line in path.read_text(encoding='utf-8').splitlines() if '=' in line)
keys=sorted((k for k in d if re.fullmatch(r'Mcu.Pin\d+',k)),key=lambda k:int(k[7:]))
pins=[d.pop(k) for k in keys]
for pin,label in [('PB6','W4_PWM'),('PC6','W4_ENC_A'),('PC7','W4_ENC_B'),('PG2','W4_SIM_A'),('PG3','W4_SIM_B'),('PC0','W4_ADC'),('PC2','W4_DMA_PROBE'),('PA6','W4_TRIGGER')]:
    if pin not in pins:pins.append(pin)
    # Reserved as analog until the hand-owned BSP initializes them safely.
    d[pin+'.Signal']='GPIO_Analog';d[pin+'.Locked']='true'
    d[pin+'.GPIOParameters']='GPIO_Label';d[pin+'.GPIO_Label']=label
for i,pin in enumerate(pins):d['Mcu.Pin'+str(i)]=pin
d['Mcu.PinsNb']=str(len(pins))
path.write_text('#MicroXplorer Configuration settings - do not modify\n'+'\n'.join(f'{k}={v}' for k,v in sorted(d.items()))+'\n',encoding='utf-8')
print('Week4 source groups and reserved pins synchronized (BSP owns peripherals).')
