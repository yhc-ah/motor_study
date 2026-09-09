"""Bounded-memory Week 5 wire recorder. No hardware performance verdict is inferred."""
from __future__ import annotations
import argparse
from contextlib import ExitStack
import csv
import json
import math
from pathlib import Path
import struct
import sys
import time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'week4_test'))
from week4_test import CountedDecoder, Frame, encode_frame, open_port, exact, decode_status
MASK=0xffffffff
FIELDS=('version uptime_ms stage raw_enabled capabilities imu_valid imu_age_ms imu_calls imu_fresh_samples imu_errors imu_recoveries missed_periods jitter_max_us jitter_p99_upper_us interval_count interval_mean_us start_us done_us last_valid_us imu_service_max_us imu_sent imu_first_sequence journal_state journal_records journal_errors journal_result tx_dropped tx_errors tx_high_watermark jitter_over_200 inject_count invalid_at_us valid_streak recovered_at_us th_updates lcd_updates extras_errors flash_missed occupancy_permille last_hal_error flash_base flash_size imu_online profile_over_budget imu_last_error reserved45 reserved46 reserved47').split()
F411_FIELDS=('version start_us done_us last_valid_start_us imu_fresh_samples imu_errors imu_recoveries missed_periods jitter_max_us jitter_over_200 imu_service_max_us tx_dropped imu_online imu_valid').split()

def versioned(fmt,p):
 v=exact(fmt,p)
 if v[0]!=1:raise ValueError('unsupported protocol version')
 return v

def status5(p):
 value=dict(zip(FIELDS,versioned('<48I',p)))
 if value['imu_last_error']>=2**31:value['imu_last_error']-=2**32
 return value

class Client:
 def __init__(self,port,on_frame=None,raw=None):self.port=port;self.on_frame=on_frame;self.raw=raw;self.sequence=0;self.decoder=CountedDecoder()
 def synchronize_f411(self,timeout=3):
  end=time.monotonic()+timeout
  while time.monotonic()<end:
   for f in self.poll():
    if f.command!=0xa5:continue
    status=dict(zip(F411_FIELDS,versioned('<14I',f.payload)))
    if status['imu_valid'] and self.finish_frame(min(.1,max(.001,end-time.monotonic()))):
     discarded=dict(self.decoder.counts);self.decoder=CountedDecoder()
     return status,discarded
  raise TimeoutError('F411 did not synchronize to a valid status within timeout')
 def poll(self):
  b=self.port.read(min(self.port.in_waiting or 1,65536))
  if self.raw:self.raw.write(b)
  frames=self.decoder.feed(b)
  for f in frames:
   if self.on_frame:self.on_frame(f)
  return frames
 def finish_frame(self,timeout=.1):
  end=time.monotonic()+timeout
  old_timeout=getattr(self.port,'timeout',None)
  if hasattr(self.port,'timeout'):self.port.timeout=min(old_timeout if old_timeout is not None else .01,.01)
  try:
   while self.decoder.buffer and time.monotonic()<end:
    b=self.port.read(1)
    if self.raw:self.raw.write(b)
    for f in self.decoder.feed(b):
     if self.on_frame:self.on_frame(f)
   return not self.decoder.buffer
  finally:
   if hasattr(self.port,'timeout'):self.port.timeout=old_timeout
 def command(self,cmd,payload=b'',timeout=2):
  self.sequence=(self.sequence+1)&MASK;seq=self.sequence;wire=encode_frame(seq,cmd,payload)
  if self.port.write(wire)!=len(wire):raise IOError('short serial write')
  end=time.monotonic()+timeout
  while time.monotonic()<end:
   for f in self.poll():
    if f.sequence!=seq or f.command!=cmd+0x88:continue
    if cmd==0x31:return status5(f.payload)
    if cmd==0x21:
     value=decode_status(f.payload)
     if value['version']!=1:raise ValueError('unsupported ADC status version')
     return value
    rc,=exact('<i',f.payload)
    if rc:raise RuntimeError(f'command {cmd:02x} result {rc}')
    return rc
  raise TimeoutError(f'ACK {cmd+0x88:02x} seq {seq} timeout')

class Streams:
 """Only counters and latest sequence/status retained, independent of duration."""
 def __init__(self,imu,adc,stats=None):
  self.stats=stats
  self.imu=csv.writer(imu);self.adc=csv.writer(adc)
  self.imu.writerow(('sequence','start_us','drdy','service_us','ax','ay','az','temperature','gx','gy','gz','valid'))
  self.adc.writerow(('run_id','block_sequence','sample_sequence','block_done_us','raw','valid','flags'))
  self.run=None;self.imu_seq=None;self.adc_seq=None;self.adc_next=0;self.latest=None;self.latest411=None;self.latest411_timing=None;self.status411_count=0
  self.adc_first_full=None;self.adc_last_full=None;self.adc_last_done=None;self.adc_full_span_us=0
  self.first_imu_us=None;self.last_imu_us=None;self.imu_span_us=0
  self.counts=dict(imu_host_reads=0,imu_fault_samples=0,imu_timestamp_regressions=0,imu_missing=0,imu_out_of_order=0,adc_samples=0,adc_blocks=0,adc_missing_samples=0,adc_missing_blocks=0,adc_timestamp_regressions=0,adc_out_of_order=0,adc_wrong_run=0,invalid_samples=0,malformed_frames=0,adc_tail_missing=0,adc_final_mismatch=0)
 def accept(self,f):
  try:
   if f.command==0xa0:
    v=exact('<III7hI',f.payload)
    if v[-1] not in (1,5):raise ValueError('invalid IMU')
    self.counts['imu_fault_samples']+=int(bool(v[-1]&4))
    if self.imu_seq is not None:
     delta=(f.sequence-self.imu_seq)&MASK
     if delta==0 or delta>=2**31:self.counts['imu_out_of_order']+=1;return
     self.counts['imu_missing']+=delta-1
    if self.last_imu_us is not None and ((v[0]-self.last_imu_us)&MASK)>=2**31:
     self.counts['imu_timestamp_regressions']+=1;return
    self.imu_seq=f.sequence;self.counts['imu_host_reads']+=1
    if self.last_imu_us is not None:self.imu_span_us+=(v[0]-self.last_imu_us)&MASK
    else:self.first_imu_us=v[0]
    self.last_imu_us=v[0];self.imu.writerow((f.sequence,*v))
   elif f.command==0xb0:
    if len(f.payload)<20:raise ValueError('short block')
    run,seq,first,done,n,flags=struct.unpack_from('<IIIIHH',f.payload)
    if not 1<=n<=100 or seq!=f.sequence or flags&~3 or (n<100 and not flags&2):raise ValueError('block header')
    values=exact('<'+'H'*n,f.payload[20:])
    if any(v>4095 for v in values):raise ValueError('ADC code')
    if run!=self.run:self.counts['adc_wrong_run']+=1;return
    delta=seq+1 if self.adc_seq is None else (seq-self.adc_seq)&MASK;gap=(first-self.adc_next)&MASK
    if not 0<delta<2**31 or gap>=2**31:self.counts['adc_out_of_order']+=1;return
    self.counts['adc_missing_blocks']+=delta-1;self.counts['adc_missing_samples']+=gap
    self.adc_seq=seq;self.adc_next=(first+n)&MASK;self.counts['adc_samples']+=n;self.counts['adc_blocks']+=1
    self.counts['invalid_samples']+=n if not flags&1 else 0
    if n==100 and flags==1:
     dt=0 if self.adc_last_done is None else (done-self.adc_last_done)&MASK
     if dt>=2**31:self.counts['adc_timestamp_regressions']+=1
     else:
      if self.adc_first_full is None:self.adc_first_full=first
      self.adc_last_full=first;self.adc_full_span_us+=dt;self.adc_last_done=done
    for i,v in enumerate(values):self.adc.writerow((run,seq,(first+i)&MASK,done,v,int(bool(flags&1)),flags))
   elif f.command==0xa9:
    if decode_status(f.payload)['version']!=1:raise ValueError('unsupported ADC status version')
   elif f.command==0xb9:self.latest=status5(f.payload)
   elif f.command==0xa5:self.latest411=dict(zip(F411_FIELDS,versioned('<14I',f.payload)));self.status411_count+=1
   elif f.command==0xa6:
    v=versioned('<10I',f.payload)
    self.latest411_timing=dict(zip(('version','service_calls','intervals','mean_period_us','max_period_error_us','p99_error_upper_us','missed_slots','violations','sum_us_low','sum_us_high'),v))
    if self.stats:self.stats.write(json.dumps(dict(type='f411_timing',pc_unix_ns=time.time_ns(),**self.latest411_timing))+'\n')
   elif f.command==0xb3:
    v=versioned('<37I',f.payload)
    profiles={}
    for i,name in enumerate(('imu','adc','uart','tx','flash','th','lcd')):
     calls,lo,hi,maximum,over=v[2+5*i:7+5*i]
     profiles[name]=dict(calls=calls,total_us=lo+(hi<<32),max_us=maximum,over_budget=over)
    if self.stats:self.stats.write(json.dumps(dict(type='profiles',pc_unix_ns=time.time_ns(),uptime_ms=v[1],version=v[0],profiles=profiles))+'\n')
  except (ValueError,struct.error):self.counts['malformed_frames']+=1
 def adc_rate(self):
  if not self.adc_full_span_us or self.adc_first_full is None:return None
  return ((self.adc_last_full-self.adc_first_full)&MASK)*1e6/self.adc_full_span_us
 def close_adc(self,s):
  gap=(s['samples_completed']-self.adc_next)&MASK
  self.counts['adc_tail_missing']=gap
  self.counts['adc_final_mismatch']=int(s['samples_sent']!=self.counts['adc_samples'] or s['blocks_sent']!=self.counts['adc_blocks'])

def coverage_errors(stream,elapsed,raw_expected,adc,final,baseline,stage):
 errors=[]
 if raw_expected and (stream.imu_span_us/1e6<max(0,elapsed*.98-.1) or stream.counts['imu_host_reads']<max(1,490*elapsed-10)):
  errors.append('IMU raw does not cover full capture')
 if adc:
  rate=stream.adc_rate()
  if rate is None or not 990<=rate<=1010:errors.append('ADC measured rate outside 990..1010 Hz')
  if not max(1,990*elapsed-200)<=stream.counts['adc_samples']<=1010*elapsed+200:errors.append('ADC samples do not cover full capture')
 if final and 'imu_calls' in final:
  if abs(final['imu_calls']-500*elapsed)>max(10,5*elapsed):errors.append('IMU service calls do not cover full capture at 500 Hz')
  if baseline:
   for key,required in (('lcd_updates',max(0,math.floor(2*elapsed)-2) if stage>=3 else 0),('th_updates',max(0,math.floor(elapsed)-2) if stage>=4 else 0)):
    if ((final[key]-baseline[key])&MASK)<required:errors.append(key+' below full capture requirement')
   if stage>=5:
    records=(final['journal_records']-baseline['journal_records'])&MASK
    if not max(0,math.floor((elapsed-1)/10))<=records<=math.ceil(elapsed/10)+1:errors.append('journal records do not cover full capture')
    if (final['flash_missed']-baseline['flash_missed'])&MASK:errors.append('Flash logging periods missed')
 return errors

def encoder_coverage_valid(steps,elapsed):
 return abs(steps-400*elapsed)<=40+4*elapsed

def close_journal(c,status,timeout=2):
 end=time.monotonic()+timeout
 while status['journal_state'] not in (4,7) and time.monotonic()<end:
  status=c.command(0x31,timeout=min(.2,max(.001,end-time.monotonic())))
 return status

def observe_fault(label,acknowledged,before,final,adc,counts,busy_seen):
 if not acknowledged or not before or not final:return False
 delta=lambda after,prior,key:(after.get(key,0)-prior.get(key,0))&MASK
 if label=='pause':return bool(delta(adc or {},before['adc'],'overwritten_blocks') and delta(counts,before['counts'],'adc_missing_samples'))
 if label=='load':return bool(delta(final,before['status'],'inject_count') and delta(final,before['status'],'jitter_over_200'))
 if label=='erase':return bool(busy_seen and before['status']['journal_state']==4 and final['journal_state']==4 and not delta(final,before['status'],'journal_errors'))
 return False

def cleanup(c,imu,adc,joint):
 errors=[]
 commands=[]
 if imu:commands.append((0x30,b'\0\0'))
 if adc:commands.append((0x20,b'\0'))
 if joint:commands.extend(((0x24,b''),(0x22,struct.pack('<IHBB',10000,0,0,0))))
 for cmd,p in commands:
  try:c.command(cmd,p,timeout=3)
  except Exception as e:errors.append(f'cleanup {cmd:02x}: {e}')
 return errors

def record(c,a):
 a.out.mkdir(parents=True,exist_ok=False)
 result=dict(hardware_verdict='NOT_ASSESSED',software_integrity='FAIL',platform=a.platform,requested_seconds=a.seconds,stage=a.stage,errors=[])
 imu=adc=joint=False;baseline=baseadc=final=finaladc=None;begin=None;coverage=0;last_tick=None;last411=0;max_stats_gap=0;first411=None;capture_elapsed=0;capture_status=None;fault_before=None;fault_after=None;fault_busy=False;fault=False
 with ExitStack() as files:
  raw=files.enter_context((a.out/'raw.bin').open('wb'))
  stream=Streams(files.enter_context((a.out/'imu.csv').open('w',newline='')),files.enter_context((a.out/'adc.csv').open('w',newline='')))
  stats=files.enter_context((a.out/'stats.jsonl').open('w',encoding='utf-8'))
  stream.stats=stats;c.raw=raw;c.on_frame=stream.accept
  try:
   if a.platform=='f407':
    baseline=c.command(0x31);baseadc=c.command(0x21)
    if baseline['stage'] or baseadc['running'] or baseadc['generator_active'] or baseadc['pwm_mode']:raise RuntimeError('baseline must have idle stage, ADC, generator and PWM')
    end=time.monotonic()+3
    while not baseline['imu_valid'] and time.monotonic()<end:baseline=c.command(0x31)
    if not baseline['imu_valid']:raise RuntimeError('IMU did not become valid within 3 seconds')
    required=(1 if a.stage>=3 else 0)|(2 if a.stage>=4 else 0)|(4 if a.stage>=5 else 0)
    if baseline['capabilities']&required!=required:raise RuntimeError('stage requires actual LCD/TH/Flash capabilities; prepare Flash explicitly first')
    if a.stage>=1:
     adc=True;stream.run=(baseadc['run_id']+1)&MASK;c.command(0x20,b'\1');started=c.command(0x21)
     if not started['running'] or started['run_id']!=stream.run:raise RuntimeError('ADC start not confirmed')
    if a.stage>=1:
     joint=True;c.command(0x22,struct.pack('<IHBB',10000,500,0,3));c.command(0x23,struct.pack('<IIiI',100,math.ceil((a.seconds+2)*100),1,0))
    imu=True;c.command(0x30,bytes((a.stage,int(a.stage>=2))))
   else:
    # Opening a continuous transmitter can start in the middle of a frame.
    # Keep those exact bytes separately; timed acceptance starts at a boundary.
    c.on_frame=None
    try:
     with (a.out/'sync.bin').open('wb') as sync:
      c.raw=sync;first411,result['pre_capture_sync_counts']=c.synchronize_f411()
    finally:c.raw=raw;c.on_frame=stream.accept
    result['f411_baseline']=first411
   begin=time.monotonic();tick=begin+1;fault=False
   while time.monotonic()-begin<a.seconds:
    c.poll();now=time.monotonic()
    if now>=tick:
     if a.platform=='f407':snap=c.command(0x31);adcstatus=c.command(0x21)
     else:
      snap=stream.latest411 if stream.status411_count>last411 else None;adcstatus=None;last411=stream.status411_count
      if first411 is None and snap is not None:first411=dict(snap)
     stats.write(json.dumps(dict(elapsed_seconds=now-begin,status=snap,adc=adcstatus,counts=stream.counts))+'\n');stats.flush();raw.flush()
     if snap is not None:
      coverage+=1;max_stats_gap=max(max_stats_gap,now-(last_tick if last_tick is not None else begin));last_tick=now
     tick=now+1
    if a.fault and not fault and now-begin>=min(2,a.seconds/2):
     cmd,p={'pause':(0x25,struct.pack('<I',250)),'load':(0x34,struct.pack('<I',5000)),'erase':(0x35,b'')}[a.fault]
     fault_before=dict(status=c.command(0x31),adc=c.command(0x21),counts=dict(stream.counts))
     stats.write(json.dumps(dict(marker=a.fault,pc_unix_ns=time.time_ns(),elapsed_seconds=now-begin))+'\n');c.command(cmd,p);fault=True
     fault_after=c.command(0x31);fault_busy=fault_after['journal_state'] in (1,2,3)
  except Exception as e:result['errors'].append(str(e))
  finally:
   if a.platform=='f411' and begin is not None:
    # Include a newly received terminal status in the measured interval so
    # the final subsecond of faults is not hidden behind an older snapshot.
    try:
     status_count=stream.status411_count;until=time.monotonic()+1.5
     while stream.status411_count==status_count and time.monotonic()<until:c.poll()
     if stream.status411_count==status_count:result['errors'].append('F411 terminal status timeout')
    except Exception as e:result['errors'].append('F411 terminal status: '+str(e))
   capture_elapsed=0 if begin is None else time.monotonic()-begin
   if a.platform=='f407':
    if begin is not None:
     try:capture_status=c.command(0x31)
     except Exception as e:result['errors'].append('capture end status: '+str(e))
    result['errors'].extend(cleanup(c,imu,adc,joint))
    try:
     until=time.monotonic()+.3
     while time.monotonic()<until:c.poll()
     final=c.command(0x31)
     if a.stage>=5 or a.fault=='erase' or final['journal_state'] in (5,6):
      final=close_journal(c,final)
      if final['journal_state']!=4:result['errors'].append('journal did not reach verified READY within 2 seconds')
     finaladc=c.command(0x21)
     if adc:stream.close_adc(finaladc)
    except Exception as e:result['errors'].append('final status: '+str(e))
   else:
    try:
     if not c.finish_frame():result['errors'].append('F411 frame completion timeout')
    except Exception as e:result['errors'].append('F411 frame completion: '+str(e))
    final=stream.latest411;capture_status=final
   c.raw=None;c.on_frame=None
  result.update(elapsed_seconds=capture_elapsed,capture_end_status=capture_status,f411_timing=stream.latest411_timing,adc_measured_hz=stream.adc_rate(),counts=stream.counts,decoder=c.decoder.counts,trailing_bytes=len(c.decoder.buffer),status_1hz_rows=coverage,raw_imu_span_seconds=stream.imu_span_us/1e6,final=final,final_adc=finaladc,baseline=baseline,baseline_adc=baseadc)
  ignored={'imu_host_reads','adc_samples','adc_blocks'}
  for k,v in stream.counts.items():
   if k not in ignored and v:result['errors'].append('stream '+k+' nonzero')
  if any(c.decoder.counts[k] for k in ('crc_errors','length_errors','noise_bytes','resync_events')) or c.decoder.buffer:result['errors'].append('wire corruption or incomplete trailing frame')
  raw_expected=a.platform=='f411' or a.stage>=2
  result['imu_raw_integrity']='ASSESSED' if raw_expected else 'NOT_ASSESSED_RAW_DISABLED'
  if raw_expected and not stream.counts['imu_host_reads']:result['errors'].append('no IMU raw data')
  if a.seconds>=60 and (coverage<math.floor(a.seconds*.98)-1 or max_stats_gap>2.5):result['errors'].append('insufficient raw duration or 1 Hz coverage')
  result['max_stats_gap_seconds']=max_stats_gap
  if begin is not None:
   if capture_elapsed<a.seconds:result['errors'].append('capture ended before requested duration')
   coverage_status=dict(capture_status) if capture_status else None
   if coverage_status is not None and final is not None and a.platform=='f407':
    for key in ('journal_records','journal_state','journal_errors','flash_missed'):coverage_status[key]=final[key]
   result['errors'].extend(coverage_errors(stream,capture_elapsed,raw_expected,adc,coverage_status,baseline,a.stage))
  if final:
   if final['missed_periods'] or final['jitter_max_us']>=200:result['errors'].append('IMU timing threshold exceeded')
   if not final['imu_valid']:result['errors'].append('final IMU invalid')
   if baseline:
    deltas={k:(final[k]-baseline[k])&MASK for k in ('imu_errors','imu_recoveries','journal_errors','tx_dropped','tx_errors','extras_errors')};result['error_deltas']=deltas
    if any(deltas.values()):result['errors'].append('device error deltas nonzero')
    if final['imu_sent']!=stream.counts['imu_host_reads']:result['errors'].append('IMU sent/host count mismatch')
   else:
    if first411:
     result['error_deltas']={k:(final[k]-first411[k])&MASK for k in ('imu_errors','imu_recoveries','tx_dropped')}
     if any(result['error_deltas'].values()):result['errors'].append('F411 error deltas nonzero')
  else:result['errors'].append('no final device status')
  if adc and not stream.counts['adc_samples']:result['errors'].append('no ADC samples')
  if adc and finaladc:
   if finaladc['running']:result['errors'].append('ADC not stopped')
   if finaladc['max_consume_us']>=80000:result['errors'].append('ADC max consume threshold exceeded')
   for k in ('overwritten_blocks','copy_races','adc_overruns','dma_errors'):
    if finaladc[k]:result['errors'].append('ADC '+k+' nonzero')
   result['adc_error_deltas']={k:(finaladc[k]-baseadc[k])&MASK for k in ('hardware_errors','tx_dropped','tx_errors')}
   if any(result['adc_error_deltas'].values()):result['errors'].append('ADC hardware/TX error delta nonzero')
   if joint and not encoder_coverage_valid(finaladc['generator_steps'],capture_elapsed):result['errors'].append('encoder steps do not cover capture at 100 Hz quadrature (400 edges/s)')
   if joint and finaladc['pwm_mode']:result['errors'].append('PWM not stopped')
   if joint and (finaladc['generator_active'] or abs(finaladc['encoder_position_low'])!=finaladc['generator_steps'] or not finaladc['generator_steps']):result['errors'].append('actual encoder/generated counts disagree')
  result['software_integrity']='PASS' if not result['errors'] else 'FAIL'
  result['fault_label']=a.fault
  if a.fault:
   observed=observe_fault(a.fault,fault,fault_before,final,finaladc,stream.counts,fault_busy)
   result.update(fault_observed=observed,fault_acknowledged=fault,fault_before=fault_before,fault_immediate_status=fault_after,fault_erase_busy_seen=fault_busy)
   # Preserve every failure. Only precisely attributable checks are labelled expected.
   allowed={'load':{'IMU timing threshold exceeded'},'pause':{'ADC samples do not cover full capture','ADC overwritten_blocks nonzero','ADC max consume threshold exceeded','stream adc_missing_samples nonzero','stream adc_missing_blocks nonzero'},'erase':set()}[a.fault]
   result['expected_failures']=[e for e in result['errors'] if observed and e in allowed]
   result['unexpected_errors']=[e for e in result['errors'] if e not in result['expected_failures']]
   result['expected_failure_evidence']=({k:v for k,v in stream.counts.items() if k in ('adc_missing_samples','adc_missing_blocks')} if a.fault=='pause' and observed else {})
   result['software_integrity']='FAULT_CAPTURE' if observed else 'FAULT_NOT_OBSERVED'
   if not observed:result['errors'].append('requested fault not observed in device and stream evidence')
 (a.out/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8');return result

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--port');p.add_argument('--baud',type=int,default=460800)
 sub=p.add_subparsers(dest='action',required=True)
 r=sub.add_parser('record');r.add_argument('--platform',choices=('f407','f411'),default='f407');r.add_argument('--stage',type=int,choices=range(1,6),default=2);r.add_argument('--seconds',type=float,default=1800);r.add_argument('--out',type=Path,required=True);r.add_argument('--fault',choices=('pause','load','erase'))
 f=sub.add_parser('prepare-flash');f.add_argument('--base',type=lambda x:int(x,0),required=True);f.add_argument('--confirm-erase',action='store_true')
 sub.add_parser('status');m=sub.add_parser('marker');m.add_argument('--out',type=Path,required=True);m.add_argument('--note',required=True)
 a=p.parse_args()
 if a.action=='marker':
  a.out.mkdir(parents=True,exist_ok=True)
  with (a.out/'markers.jsonl').open('a',encoding='utf-8') as f:f.write(json.dumps(dict(pc_unix_ns=time.time_ns(),note=a.note))+'\n')
  return
 if not a.port:p.error('--port required')
 if a.action=='prepare-flash' and not a.confirm_erase:p.error('Flash erase requires --confirm-erase')
 if a.action=='record':
  if not math.isfinite(a.seconds) or not 0<a.seconds<=3600:p.error('seconds must be finite, >0 and <=3600')
  if a.platform=='f411' and (a.seconds<300 or a.fault):p.error('F411 receive-only requires >=300 seconds and no command faults')
 with open_port(a.port,a.baud) as port:
  c=Client(port)
  if a.action=='record':result=record(c,a);print(json.dumps(result,indent=2));return 0 if result['software_integrity']=='PASS' else 1
  if a.action=='status':print(json.dumps(c.command(0x31),indent=2))
  else:c.command(0x32,b'W5OK'+struct.pack('<II',a.base,20480));print('Prepare accepted; poll status until capabilities bit 2 is set before stage 5.')
 return 0
if __name__=='__main__':sys.exit(main())
