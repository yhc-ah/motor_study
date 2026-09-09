"""Measured-duration malformed UART flood, independent from normal acceptance."""
import argparse
import json
import math
from pathlib import Path
import sys
import time
sys.path.insert(0,str(Path(__file__).parent/'uart_test'))
from uart_test import encode_frame, decode_stats, open_port
sys.path.insert(0,str(Path(__file__).parent/'sensor_test'))
from sensor_test import CountedDecoder as FrameDecoder

def delta(after,before):return (after-before)&0xffffffff

def bad_wire():
    wire=bytearray(encode_frame(0x11223344,1,b'bad-only'))
    wire[-1]^=1
    return bytes(wire)

def assess(before,after,recovered):
    d={key:delta(after[key],before[key]) for key in before if key in after}
    zero=d.get('ping_count')==0
    return dict(bad_frames_zero_execution=zero,recovered=recovered,deltas=d,
                flood_check_passed=zero and recovered and d.get('crc_errors',0)>0,
                hardware_verdict='NOT_ASSESSED',
                note='Fault-window overload drops are counted; no normal timing claim.')

class Connection:
    def __init__(self,port,raw):self.port=port;self.raw=raw;self.decoder=FrameDecoder();self.seq=0
    def read(self):
        data=self.port.read(min(self.port.in_waiting or 1,65536));self.raw.write(data)
        return self.decoder.feed(data)
    def request(self,cmd,payload=b''):
        self.seq+=1;wire=encode_frame(self.seq,cmd,payload)
        if self.port.write(wire)!=len(wire):raise IOError('short write')
        until=time.monotonic()+3
        while time.monotonic()<until:
            for f in self.read():
                if f.sequence==self.seq and f.command==cmd+0x80:
                    if cmd==2:return decode_stats(f.payload)
                    if f.payload!=payload:raise ValueError('incorrect recovery echo')
                    return f.payload
        raise TimeoutError('response timeout')

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port',required=True);ap.add_argument('--baud',type=int,default=460800)
    ap.add_argument('--seconds',type=float,default=60);ap.add_argument('--out',type=Path,required=True)
    a=ap.parse_args()
    if not math.isfinite(a.seconds) or not 1<=a.seconds<=600:ap.error('seconds must be 1..600')
    a.out.mkdir(parents=True,exist_ok=False)
    result=dict(completed=False,hardware_verdict='NOT_ASSESSED',requested_seconds=a.seconds)
    port=None
    try:
        port=open_port(a.port,a.baud)
        with (a.out/'rx.bin').open('wb') as raw:
            c=Connection(port,raw);before=c.request(2);wire=bad_wire()*16
            start=time.monotonic();sent=unexpected=0
            while time.monotonic()-start<a.seconds:
                count=port.write(wire)
                if count!=len(wire):raise IOError('short flood write')
                sent+=count
                if port.in_waiting:
                    unexpected+=sum(f.command==0x81 and f.sequence==0x11223344 for f in c.read())
            # Include drain time in observed end-to-end rate; finite write timeout
            # bounds submission. Never call an unbounded serial flush.
            end=time.monotonic()+0.3
            while time.monotonic()<end:c.read()
            elapsed=time.monotonic()-start;after=c.request(2)
            c.request(1,b'week5-recovered')
            result.update(assess(before,after,True))
            result.update(completed=True,unexpected_bad_responses=unexpected,sent_bytes=sent,
                          elapsed_seconds=elapsed,observed_bytes_per_second=sent/elapsed,
                          baud=a.baud,before=before,after=after)
            if unexpected:result['flood_check_passed']=False
    except Exception as exc:result['error']=str(exc)
    finally:
        if port is not None:port.close()
        (a.out/'report.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result,indent=2))
    return 0 if result.get('flood_check_passed') else 1

if __name__=='__main__':raise SystemExit(main())
