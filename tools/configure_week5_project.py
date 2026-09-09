"""Register hand-owned week5 foreground source; preserve generated peripheral owners."""
from pathlib import Path
import re
ROOT = Path(__file__).resolve().parents[1]
p = ROOT / 'MDK-ARM/stm32-realtime-bsp.uvprojx'
s = p.read_text(encoding='utf-8')
m = re.search(r'<IncludePath>([^<]*../Core/Inc[^<]*)</IncludePath>', s)
old = m.group(1)
if '../algorithm' not in old:
    s = s.replace(old, old + ';../algorithm', 1)
sources = ['app/app_week5.c', 'app/week5_journal.c', 'algorithm/week5_timing.c']
entries = []
for source in sources:
    name = Path(source).name
    if f'<FileName>{name}</FileName>' not in s:
        entries.append(f'<File><FileName>{name}</FileName><FileType>1</FileType><FilePath>../{source}</FilePath></File>')
if entries:
    s = s.replace('</Groups>', '<Group><GroupName>Week5</GroupName><Files>' + ''.join(entries) + '</Files></Group>\n</Groups>')
p.write_text(s, encoding='utf-8')
print('Week5 source registration complete.')
