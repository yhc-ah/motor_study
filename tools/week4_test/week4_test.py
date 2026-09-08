"""Week 4 acquisition. IRQ block timestamps are not individual conversion times."""
from __future__ import annotations
import argparse
import csv
import json
import math
from pathlib import Path
import statistics
import struct
import sys
import time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'sensor_test'))
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'uart_test'))
from sensor_test import CountedDecoder
from uart_test import Frame, encode_frame, open_port
FIELDS=('version uptime_ms run_id running samples_completed blocks_completed blocks_sent samples_sent overwritten_blocks copy_races adc_overruns dma_errors tx_dropped tx_errors max_consume_us ht_count tc_count tail_samples encoder_cnt encoder_position_low encoder_speed generator_steps generator_target generator_active pwm_hz pwm_arr pwm_ccr pwm_mode filter5_milli filter20_milli pause_events hardware_errors').split()
MASK=0xffffffff

def exact(fmt,p):
 if len(p)!=struct.calcsize(fmt):raise ValueError('malformed response length')
 return struct.unpack(fmt,p)

def decode_status(p):
 d=dict(zip(FIELDS,exact('<32I',p)))
 for key in ('encoder_position_low','encoder_speed'):
  if d[key]>=2**31:d[key]-=2**32
 return d

class Client:
 def __init__(self,port,on_frame=None,raw=None):
  self.port=port;self.on_frame=on_frame;self.raw=raw;self.sequence=0;self.decoder=CountedDecoder()
 def poll(self):
  b=self.port.read(min(self.port.in_waiting or 1,65536))
  if self.raw:self.raw.write(b)
  frames=self.decoder.feed(b)
  for f in frames:
   if self.on_frame:self.on_frame(f)
  return frames
 def command(self,cmd,payload=b'',timeout=2):
  self.sequence=(self.sequence+1)&MASK;seq=self.sequence;wire=encode_frame(seq,cmd,payload)
  if self.port.write(wire)!=len(wire):raise IOError('short serial write')
  end=time.monotonic()+timeout
  while time.monotonic()<end:
   for f in self.poll():
    if f.sequence!=seq or f.command!=cmd+0x88:continue
    if cmd==0x21:return decode_status(f.payload)
    if cmd==0x27:
     rc,raw=exact('<iI',f.payload)
     if rc:raise RuntimeError(f'poll result {rc}')
     if raw>4095:raise ValueError('ADC code out of range')
     return raw
    rc,=exact('<i',f.payload)
    if rc:raise RuntimeError(f'command {cmd:02x} result {rc}')
    return rc
  raise TimeoutError(f'ACK {cmd+0x88:02x} seq {seq} timeout')
 def status(self):return self.command(0x21)

class Tracker:
 def __init__(self,run):
  self.run=run;self.last_seq=None;self.next_sample=0;self.seen=set();self.rows=[];self.blocks=[]
  self.counts=dict(missing_blocks=0,missing_samples=0,duplicates=0,out_of_order=0,run_changes=0,malformed_blocks=0,sequence_wraps=0,closing_missing_samples=0,closing_missing_blocks=0,closing_count_mismatch=0,invalid_samples=0)
 def accept(self,f):
  try:
   if len(f.payload)<20:raise ValueError()
   run,seq,first,done,count,flags=struct.unpack_from('<IIIIHH',f.payload)
   if not 1<=count<=100 or len(f.payload)!=20+count*2 or seq!=f.sequence or flags&~3 or (count<100 and not flags&2):raise ValueError()
   samples=struct.unpack_from('<'+'H'*count,f.payload,20)
   if any(x>4095 for x in samples):raise ValueError()
  except (ValueError,struct.error):self.counts['malformed_blocks']+=1;return []
  if run!=self.run:self.counts['run_changes']+=1;return []
  if seq in self.seen:self.counts['duplicates']+=1;return []
  delta=seq+1 if self.last_seq is None else (seq-self.last_seq)&MASK
  if delta==0 or delta>=2**31:self.counts['out_of_order']+=1;return []
  samplegap=(first-self.next_sample)&MASK
  if samplegap>=2**31:self.counts['out_of_order']+=1;return []
  self.counts['missing_blocks']+=delta-1;self.counts['missing_samples']+=samplegap
  self.counts['sequence_wraps']+=int(self.last_seq is not None and seq<self.last_seq)
  self.seen.add(seq);self.last_seq=seq;self.next_sample=(first+count)&MASK
  rows=[dict(run_id=run,block_seq=seq,sample_seq=(first+i)&MASK,nominal_time_us=((first+i+1)&MASK)*1000,done_us=done,raw=x,valid=int(bool(flags&1)),flags=flags) for i,x in enumerate(samples)]
  self.counts['invalid_samples']+=sum(not r['valid'] for r in rows)
  self.rows.extend(rows);self.blocks.append(dict(first=first,done=done,count=count,valid=bool(flags&1),flags=flags));return rows
 def close(self,s):
  gap=(s['samples_completed']-self.next_sample)&MASK
  self.counts['closing_missing_samples']=gap if gap<2**31 else 0
  self.counts['closing_missing_blocks']=max(0,s['blocks_sent']-len(self.blocks))
  self.counts['closing_count_mismatch']=int(s['samples_sent']!=len(self.rows) or s['blocks_sent']!=len(self.blocks) or gap>=2**31)

def filter_rows(rows):
 prev=None;y5=y20=None
 for r in rows:
  seq=int(r['sample_seq']);x=float(r['raw']);valid=int(r['valid'])
  if not valid:r.update(filter5='',filter20='');prev=None;continue
  if prev is None or ((seq-prev)&MASK)!=1:y5=y20=x
  else:y5+=(1-math.exp(-.001/.005))*(x-y5);y20+=(1-math.exp(-.001/.020))*(x-y20)
  r.update(filter5=y5,filter20=y20);prev=seq
 return rows

def stats(values):
 return dict(n=len(values),mean=statistics.mean(values),stdev=statistics.pstdev(values),min=min(values),max=max(values),peak_to_peak=max(values)-min(values)) if values else dict(n=0)

def svg(path,rows,title):
 series=[('raw','#888'),('filter5','#1670ba'),('filter20','#bf4b22')];parts=[]
 def xpos(r):return float(r.get('nominal_time_us',(int(r['sample_seq'])+1)*1000))
 low=min((xpos(r) for r in rows),default=0);high=max((xpos(r) for r in rows),default=1)
 for key,color in series:
  segments=[];segment=[];previous=None
  for r in rows:
   seq=int(r['sample_seq'])
   if not int(r['valid']) or r.get(key,'')=='':
    if segment:segments.append(segment)
    segment=[];previous=None;continue
   if previous is not None and ((seq-previous)&MASK)!=1:
    if segment:segments.append(segment)
    segment=[]
   segment.append(r);previous=seq
  if segment:segments.append(segment)
  for segment in segments:
   # Retain both endpoints while thinning within each continuous segment.
   stride=max(1,math.ceil(len(segment)/4000));chosen=segment[::stride]
   if chosen[-1] is not segment[-1]:chosen.append(segment[-1])
   pts=[f'{50+900*(xpos(r)-low)/max(1,high-low):.2f},{340-300*float(r[key])/4095:.2f}' for r in chosen]
   parts.append(f'<polyline fill="none" stroke="{color}" stroke-width="1" points="{" ".join(pts)}"/>')
 path.write_text('<svg xmlns="http://www.w3.org/2000/svg" width="1000" height="400"><rect width="100%" height="100%" fill="white"/><text x="50" y="25">'+title+'; raw gray / tau5 blue / tau20 red; ADC code 0..4095</text>'+''.join(parts)+f'<text x="50" y="380">Nominal sample time {low/1000:g} to {high/1000:g} ms; gaps split; endpoints retained</text></svg>',encoding='utf-8')

def step_test(out=None):
 rows=filter_rows([dict(sample_seq=i,raw=1000 if i<1000 else 3000,valid=1) for i in range(2000)])
 result={'source':'synthetic 1000 -> 3000 at sample 1000; dt=1 ms','passed':True}
 for key,tau in [('filter5',5),('filter20',20)]:
  def crossing(frac):return next(i-999 for i,r in enumerate(rows[1000:],1000) if r[key]>=1000+2000*frac)
  t63=crossing(1-math.exp(-1));rise=crossing(.9)-crossing(.1)
  ok=abs(t63-tau)<=1 and abs(rise-math.log(9)*tau)<=2
  result['tau'+str(tau)]={'t63_ms':t63,'rise10_90_ms':rise,'passed':ok};result['passed'] &= ok
 if out:out.mkdir(parents=True,exist_ok=True);svg(out/'step.svg',rows,'Synthetic step');(out/'step.json').write_text(json.dumps(result,indent=2))
 return result

def duration(v):
 x=float(v)
 if not math.isfinite(x) or not 0<x<=86400:raise argparse.ArgumentTypeError('seconds must be finite, >0 and <=86400')
 return x

def analyze(directory,vref=None,settle_ms=200):
 if not math.isfinite(settle_ms) or settle_ms<0:raise ValueError('settle-ms must be finite and nonnegative')
 with (directory/'adc.csv').open(newline='') as f:rows=list(csv.DictReader(f))
 filter_rows(rows)
 eligible=[];previous=None;age=0
 for r in rows:
  seq=int(r['sample_seq'])
  if not int(r['valid']):previous=None;age=0;continue
  age=age+1 if previous is not None and ((seq-previous)&MASK)==1 else 0
  if age>=math.ceil(settle_ms):eligible.append(r)
  previous=seq
 if not eligible:raise ValueError('no valid samples remain after settling exclusion; use a longer capture or explicit --settle-ms 0')
 result={'hardware_verdict':'NOT_ASSESSED','source':'offline ADC codes','settle_ms_per_contiguous_segment':settle_ms,'filters':{k:stats([float(r[k]) for r in eligible]) for k in ('raw','filter5','filter20')}}
 if vref is not None:
  if not math.isfinite(vref) or vref<=0:raise ValueError('VREF must be positive and finite')
  result['user_vref_volts']=vref;result['voltage_note']='ideal code*VREF/4095 conversion; no calibration or precision claim'
  result['volts']=stats([float(r['raw'])*vref/4095 for r in eligible])
 svg(directory/'noise.svg',rows,'ADC noise');(directory/'analysis.json').write_text(json.dumps(result,indent=2),encoding='utf-8');return result

def assess_record(tracker,final,seconds,pause_ms,injected):
 blocks=tracker.blocks if tracker else [];rows=tracker.rows if tracker else []
 windows=[];segment=[]
 for b in blocks:
  if not b['valid'] or b['flags']&2:segment=[];continue
  if segment and b['first']!=segment[-1]['first']+segment[-1]['count']:segment=[]
  segment.append(b)
  if len(segment)>1:
   dt=(b['done']-segment[0]['done'])&MASK;dn=b['first']-segment[0]['first']
   if 1000000<=dt<2**31:windows.append((dn,dt))
 rate=windows[-1][0]*1e6/windows[-1][1] if windows else None
 coverage=(final or {}).get('samples_completed',0)/1000
 errors=[]
 if not rows:errors.append('no ADC samples received')
 if coverage<seconds-.002:errors.append('nominal sample coverage shorter than requested duration (2 ms boundary tolerance)')
 rate_verdict='NOT_ASSESSED' if rate is None else 'PASS' if 990<=rate<=1010 else 'FAIL'
 if rate_verdict=='FAIL':errors.append('IRQ block rate outside 990..1010 Hz')
 if seconds>=60 and rate is None:errors.append('insufficient contiguous valid timestamp window for 60 s acquisition')
 fault_detected=bool(pause_ms and injected and (final or {}).get('overwritten_blocks',0)>0 and tracker and (tracker.counts['missing_samples'] or tracker.counts['closing_missing_samples']))
 if pause_ms and not fault_detected:errors.append('requested pause fault not injected or no overwrite plus sample-gap evidence')
 integrity_ok=bool(tracker) and not any(v for k,v in tracker.counts.items() if k!='sequence_wraps')
 hardware_ok=not any((final or {}).get(k,0) for k in ('overwritten_blocks','copy_races','adc_overruns','dma_errors'))
 return dict(acquisition_passed=bool(rows) and not pause_ms and integrity_ok and hardware_ok and not errors and rate_verdict=='PASS',nominal_coverage_s=coverage,block_timestamp_rate_hz=rate,rate_verdict=rate_verdict,rate_note='IRQ completion interval estimate; nominal sample times are not measured conversion timestamps',fault_requested=bool(pause_ms),fault_injected=injected,fault_detected=fault_detected,assessment_errors=errors)


def encoder_run(client,directory,hz,cycles,direction,initial):
 if hz not in (100,1000) or not 1<=cycles<=100000 or direction not in (-1,1) or not 0<=initial<=65535:raise ValueError('invalid encoder parameters')
 directory.mkdir(parents=True,exist_ok=True);statuses=[];r=dict(hardware_verdict='NOT_ASSESSED',errors=[],expected_magnitude=cycles*4,requested_direction=direction);attempted=False;baseline={};final={}
 def snapshot():
  status=client.status();statuses.append(dict(pc_monotonic_ns=time.monotonic_ns(),**status));return status
 with (directory/'raw.bin').open('wb') as raw:
  client.raw=raw
  try:
   baseline=snapshot()
   if baseline['generator_active']:raise RuntimeError('encoder generator already active')
   attempted=True;client.command(0x23,struct.pack('<IIiI',hz,cycles,direction,initial))
   deadline=time.monotonic()+cycles/hz+3
   while True:
    final=snapshot()
    if not final['generator_active']:break
    if time.monotonic()>=deadline:raise TimeoutError('encoder finite completion timeout')
    until=min(deadline,time.monotonic()+.05)
    while time.monotonic()<until:client.poll()
   # Let the 10 ms encoder extension task incorporate the final quadrature edge.
   until=time.monotonic()+.020
   while time.monotonic()<until:client.poll()
   final=snapshot()
   r['final']=final;r['observed_magnitude']=abs(final['encoder_position_low']);r['observed_direction']=0 if final['encoder_position_low']==0 else (1 if final['encoder_position_low']>0 else -1)
   if final['generator_active'] or final['generator_steps']!=cycles*4:r['errors'].append('generator completion/step count mismatch')
   if abs(final['encoder_position_low'])!=cycles*4:r['errors'].append('encoder position magnitude mismatch')
   if final['encoder_cnt']!=(initial+final['encoder_position_low'])&65535:r['errors'].append('final CNT does not match initial plus extended position')
   for key in ('tx_errors','tx_dropped','hardware_errors'):
    if (final.get(key,0)-baseline.get(key,0))&MASK:r['errors'].append(key+' increased')
  except Exception as e:r['errors'].append(str(e))
  finally:
   if attempted:
    try:client.command(0x24)
    except Exception as e:r['errors'].append('encoder stop: '+str(e))
   client.raw=None
 r['decoder']=dict(client.decoder.counts)
 if any(r['decoder'].get(k,0) for k in ('crc_errors','length_errors','resync_events','noise_bytes')) or client.decoder.buffer:r['errors'].append('serial decoding errors')
 r['report_ok']=not r['errors'];(directory/'status.jsonl').write_text(''.join(json.dumps(s)+'\n' for s in statuses),encoding='utf-8');(directory/'summary.json').write_text(json.dumps(r,indent=2),encoding='utf-8');return r

def record(client,directory,seconds,pause_ms=0,joint=False):
 seconds=duration(seconds)
 if pause_ms and not 1<=pause_ms<=1000:raise ValueError('pause-ms 1..1000')
 if joint and seconds>990:raise ValueError('joint duration <=990 s (finite generator limit)')
 directory.mkdir(parents=True,exist_ok=True)
 result={'hardware_verdict':'NOT_ASSESSED','errors':[]};tracker=None;pending=[];statuses=[];baseline={};final=None;start_attempted=False;joint_attempted=False;paused=False
 raw=(directory/'raw.bin').open('wb');client.raw=raw
 def receive(f):
  if f.command==0xB0:
   if tracker:tracker.accept(f)
   else:pending.append(f)
 client.on_frame=receive
 def snapshot():
  s=client.status();statuses.append(dict(pc_monotonic_ns=time.monotonic_ns(),**s));return s
 try:
  baseline=snapshot()
  if baseline.get('running'):raise RuntimeError('ADC already running')
  start_attempted=True
  client.command(0x20,b'\1')
  started=snapshot()
  if started['run_id']==baseline['run_id'] or not started['running']:raise RuntimeError('start status did not confirm a new active run')
  tracker=Tracker(started['run_id'])
  for f in pending:tracker.accept(f)
  if joint:
   joint_attempted=True
   client.command(0x22,struct.pack('<IHBB',10000,500,0,3));client.command(0x23,struct.pack('<IIiI',100,math.ceil((seconds+2)*100),1,0))
  begin=time.monotonic();nextstatus=begin+1;paused=False
  while time.monotonic()-begin<seconds:
   client.poll();now=time.monotonic()
   if pause_ms and not paused and now-begin>=min(1,seconds/2):client.command(0x25,struct.pack('<I',pause_ms));paused=True
   if now>=nextstatus:snapshot();nextstatus=now+1
 except Exception as e:result['errors'].append(str(e))
 finally:
  # START may have reached hardware even if its ACK was lost.
  if start_attempted:
   try:client.command(0x20,b'\0',timeout=3)
   except Exception as e:result['errors'].append('stop: '+str(e))
  if joint_attempted:
   for cmd,p in [(0x24,b''),(0x22,struct.pack('<IHBB',10000,0,0,0))]:
    try:client.command(cmd,p)
    except Exception as e:result['errors'].append('cleanup: '+str(e))
  try:
   until=time.monotonic()+.15
   while time.monotonic()<until:client.poll()
   final=snapshot()
   if final.get('running'):result['errors'].append('final ADC still running')
   if tracker:
    if final['run_id']!=tracker.run:result['errors'].append('final run changed')
    tracker.close(final)
  except Exception as e:result['errors'].append('final status: '+str(e))
  raw.close();client.raw=None
  rows=filter_rows(tracker.rows if tracker else [])
  with (directory/'adc.csv').open('w',newline='') as f:
   w=csv.DictWriter(f,fieldnames=['run_id','block_seq','sample_seq','nominal_time_us','done_us','raw','valid','flags','filter5','filter20']);w.writeheader();w.writerows(rows)
  (directory/'status.jsonl').write_text(''.join(json.dumps(s)+'\n' for s in statuses),encoding='utf-8')
  result['decoder']=dict(client.decoder.counts);result['trailing_bytes']=len(client.decoder.buffer);result['tracker']=tracker.counts if tracker else {};result['samples_received']=len(rows)
  if final:
   result['final']=final;result['cumulative_deltas']={k:(final.get(k,0)-baseline.get(k,0))&MASK for k in ('tx_dropped','tx_errors','hardware_errors')}
   if joint_attempted:
    if final.get('generator_active') or not final.get('generator_steps',0) or abs(final.get('encoder_position_low',0))!=final.get('generator_steps',0):result['errors'].append('joint encoder stopped count does not match generated steps')
   for k in ('overwritten_blocks','copy_races','adc_overruns','dma_errors'):
    if final.get(k,0):result['errors'].append(k+' nonzero')
   if any(result['cumulative_deltas'].values()):result['errors'].append('TX/hardware error delta nonzero')
  if tracker:
   bad=[k for k,v in tracker.counts.items() if v and k!='sequence_wraps']
   if bad:result['errors'].append('data integrity: '+', '.join(bad))
  assessment=assess_record(tracker,final,seconds,pause_ms,paused);result.update(assessment);result['errors'].extend(assessment['assessment_errors'])
  result['expected_fault_errors']=[]
  if assessment['fault_detected']:
   retained=[]
   allowed={'missing_blocks','missing_samples','closing_missing_samples'}
   for error in result['errors']:
    expected=error=='overwritten_blocks nonzero'
    if error.startswith('data integrity: '):expected=set(error.removeprefix('data integrity: ').split(', '))<=allowed
    if expected:result['expected_fault_errors'].append(error)
    else:retained.append(error)
   result['errors']=retained
  if any(result['decoder'].get(k,0) for k in ('crc_errors','length_errors','resync_events','noise_bytes')) or result['trailing_bytes']:result['errors'].append('serial decoder corruption/truncated frame')
  result['report_ok']=not result['errors']
  result['acquisition_passed']=bool(result.get('acquisition_passed') and result['report_ok'] and not pause_ms)
  (directory/'summary.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
 return result

def main(argv=None):
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--port');p.add_argument('--baud',type=int,default=460800);sub=p.add_subparsers(dest='action',required=True)
 sub.add_parser('status');sub.add_parser('encoder-stop')
 q=sub.add_parser('pwm');q.add_argument('--hz',type=int,choices=[1000,10000,20000],default=10000);q.add_argument('--duty',type=int,default=500);q.add_argument('--center',action='store_true');q.add_argument('--mode',type=int,choices=range(4),default=1)
 q=sub.add_parser('encoder');q.add_argument('--hz',type=int,choices=[100,1000],default=100);q.add_argument('--cycles',type=int,default=1000);q.add_argument('--direction',type=int,choices=[-1,1],default=1);q.add_argument('--initial',type=int,default=0);q.add_argument('--out',type=Path,default=Path('week4-encoder'))
 q=sub.add_parser('poll');q.add_argument('--count',type=int,default=1000);q.add_argument('--cycles',type=int,choices=[84,480],default=84);q.add_argument('--out',type=Path,default=Path('week4-poll'))
 q=sub.add_parser('record');q.add_argument('--seconds',type=duration,default=60);q.add_argument('--pause-ms',type=int,default=0);q.add_argument('--joint',action='store_true');q.add_argument('--out',type=Path,default=Path('week4-record'))
 q=sub.add_parser('analyze');q.add_argument('directory',type=Path);q.add_argument('--vref',type=float);q.add_argument('--settle-ms',type=float,default=200)
 q=sub.add_parser('step-test');q.add_argument('--out',type=Path,default=Path('week4-step'))
 a=p.parse_args(argv)
 try:
  if a.action=='analyze':r=analyze(a.directory,a.vref,a.settle_ms)
  elif a.action=='step-test':r=step_test(a.out)
  else:
   if not a.port:p.error('--port required for hardware commands')
   with open_port(a.port,a.baud) as port:
    c=Client(port)
    if a.action=='status':r=c.status()
    elif a.action=='encoder-stop':r=c.command(0x24)
    elif a.action=='pwm':
     if not 0<=a.duty<=1000:raise ValueError('duty 0..1000')
     r=c.command(0x22,struct.pack('<IHBB',a.hz,a.duty,int(a.center),a.mode))
    elif a.action=='encoder':
     if not 1<=a.cycles<=100000 or not 0<=a.initial<=65535:raise ValueError('cycles 1..100000, initial 0..65535')
     r=encoder_run(c,a.out,a.hz,a.cycles,a.direction,a.initial)
    elif a.action=='record':r=record(c,a.out,a.seconds,a.pause_ms,a.joint)
    else:
     if not 1000<=a.count<=1000000:raise ValueError('poll count 1000..1000000')
     a.out.mkdir(parents=True,exist_ok=True);rows=[];r={'hardware_verdict':'NOT_ASSESSED','errors':[]}
     with (a.out/'raw.bin').open('wb') as raw:
      c.raw=raw
      try:
       if c.status()['running']:raise RuntimeError('poll requires idle ADC')
       c.command(0x26,struct.pack('<I',a.cycles))
       for i in range(a.count):
        before=time.monotonic_ns();value=c.command(0x27);after=time.monotonic_ns();rows.append(dict(index=i,pc_before_ns=before,pc_after_ns=after,roundtrip_ns=after-before,raw=value))
      except Exception as e:r['errors'].append(str(e))
     with (a.out/'poll.csv').open('w',newline='') as f:
      w=csv.DictWriter(f,fieldnames=['index','pc_before_ns','pc_after_ns','roundtrip_ns','raw']);w.writeheader();w.writerows(rows)
     r.update(count=len(rows),roundtrip_ns=stats([x['roundtrip_ns'] for x in rows]),adc_codes=stats([x['raw'] for x in rows]),sample_cycles=a.cycles)
     (a.out/'summary.json').write_text(json.dumps(r,indent=2))
  print(json.dumps(r,indent=2));return int(isinstance(r,dict) and (bool(r.get('errors')) or r.get('passed') is False))
 except (ValueError,RuntimeError,OSError,TimeoutError) as e:print(str(e),file=sys.stderr);return 1
if __name__=='__main__':sys.exit(main())
