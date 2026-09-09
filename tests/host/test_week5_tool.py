import io
import struct
import sys
import unittest
import tempfile
from types import SimpleNamespace
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools/week5_test'))
import week5_test as w

class Port:
 def __init__(self,reply):self.reply=reply;self.data=b''
 @property
 def in_waiting(self):return len(self.data)
 def write(self,b):
  f=w.CountedDecoder().feed(b)[0];self.data=self.reply(f);return len(b)
 def read(self,n):b,self.data=self.data[:n],self.data[n:];return b

class Tests(unittest.TestCase):
 def test_f411_sync_discards_only_pre_capture_noise(self):
  port=Port(None);words=[0]*14;words[0]=1;words[12]=words[13]=1
  port.data=b'partial-old-frame'+w.encode_frame(10,0xa5,struct.pack('<14I',*words))
  raw=io.BytesIO();c=w.Client(port,raw=raw)
  baseline,discarded=c.synchronize_f411(.1)
  self.assertEqual(baseline['imu_valid'],1)
  self.assertGreater(discarded['noise_bytes'],0)
  self.assertEqual(c.decoder.counts['noise_bytes'],0)
  self.assertTrue(raw.getvalue().startswith(b'partial-old-frame'))
  with self.assertRaises(TimeoutError):w.Client(Port(None)).synchronize_f411(.001)
 def test_ack_length(self):
  c=w.Client(Port(lambda f:w.encode_frame(f.sequence,0xb8,b'\0')))
  with self.assertRaises(ValueError):c.command(0x30,b'\2\1')
 def test_ack_sequence(self):
  c=w.Client(Port(lambda f:w.encode_frame(f.sequence+1,0xb8,bytes(4))))
  with self.assertRaises(TimeoutError):c.command(0x30,b'\2\1',timeout=.01)
 def test_cleanup_continues_bad_ack(self):
  class Bad:
   def __init__(self):self.calls=[]
   def command(self,c,p=b'',**k):self.calls.append(c);raise ValueError('bad ACK')
  c=Bad();errors=w.cleanup(c,True,True,True)
  self.assertEqual(c.calls,[0x30,0x20,0x24,0x22]);self.assertEqual(len(errors),4)
 def test_adc_gap_tail_and_bounded(self):
  s=w.Streams(io.StringIO(),io.StringIO());s.run=7
  def block(seq,first,n,flags=1):return w.Frame(seq,0xb0,struct.pack('<IIIIHH',7,seq,first,123,n,flags)+struct.pack('<'+'H'*n,*([100]*n)),b'')
  s.accept(block(0,0,100));s.accept(block(2,200,3,3))
  self.assertEqual(s.counts['adc_missing_samples'],100)
  self.assertEqual(s.counts['adc_samples'],103)
  s.close_adc(dict(samples_completed=203,samples_sent=103,blocks_sent=2))
  self.assertEqual(s.counts['adc_tail_missing'],0)
  # Simulate 1800 seconds at 500 Hz using sinks: retained state must stay fixed.
  class Sink:
   def write(self,x):return len(x)
  s=w.Streams(Sink(),Sink());before=set(s.__dict__)
  for i in range(900000):s.accept(w.Frame(i,0xa0,struct.pack('<III7hI',i*2000,0,20,*([1]*7),1),b''))
  self.assertEqual(s.counts['imu_host_reads'],900000)
  s.run=7
  for i in range(18000):s.accept(block(i,i*100,100))
  self.assertEqual(s.counts['adc_samples'],1800000)
  self.assertEqual(before,set(s.__dict__))
  self.assertFalse(any(isinstance(x,(list,set)) for x in s.__dict__.values()))
 def test_start_bad_ack_still_cleans_owned_resources(self):
  class Device:
   def __init__(self):self.calls=[];self.decoder=w.CountedDecoder();self.adc=False
   def poll(self):return []
   def command(self,cmd,p=b'',**kw):
    self.calls.append((cmd,p))
    if cmd==0x31:return dict.fromkeys(w.FIELDS,0)|dict(imu_valid=1)
    if cmd==0x21:
     from week4_test import FIELDS
     return dict.fromkeys(FIELDS,0)|dict(run_id=int(self.adc),running=int(self.adc))
    if cmd==0x20:self.adc=bool(p[0])
    if cmd==0x30 and p[0]:raise ValueError('bad start ACK')
    return 0
  c=Device()
  with tempfile.TemporaryDirectory() as d:
   r=w.record(c,SimpleNamespace(out=Path(d)/'capture',platform='f407',stage=2,seconds=.01,fault=None))
  self.assertIn((0x30,b'\0\0'),c.calls)
  self.assertIn((0x20,b'\0'),c.calls)
  self.assertIn((0x24,b''),c.calls)
  self.assertEqual(c.calls[-3][0],0x22)
  self.assertEqual(r['software_integrity'],'FAIL')
 def test_signed_rejection(self):
  c=w.Client(Port(lambda f:w.encode_frame(f.sequence,0xb8,struct.pack('<i',-3))))
  with self.assertRaisesRegex(RuntimeError,'-3'):c.command(0x30,b'\5\1')
 def test_status_exact_length(self):
  c=w.Client(Port(lambda f:w.encode_frame(f.sequence,0xb9,bytes(188))))
  with self.assertRaises(ValueError):c.command(0x31)
 def test_duplicate_and_missing_tail(self):
  s=w.Streams(io.StringIO(),io.StringIO());s.run=1
  f=w.Frame(0,0xb0,struct.pack('<IIIIHHH',1,0,0,20,1,3,123),b'')
  s.accept(f);s.accept(f)
  self.assertEqual(s.counts['adc_out_of_order'],1)
  s.close_adc(dict(samples_completed=4,samples_sent=4,blocks_sent=2))
  self.assertEqual(s.counts['adc_tail_missing'],3)
  self.assertEqual(s.counts['adc_final_mismatch'],1)
 def test_fault_valid_imu(self):
  s=w.Streams(io.StringIO(),io.StringIO())
  s.accept(w.Frame(1,0xa0,struct.pack('<III7hI',100,0,20,*([1]*7),5),b''))
  self.assertEqual(s.counts['imu_host_reads'],1)
  self.assertEqual(s.counts['imu_fault_samples'],1)
  self.assertEqual(s.counts['malformed_frames'],0)
 def test_timestamp_regression(self):
  s=w.Streams(io.StringIO(),io.StringIO())
  for seq,stamp in ((1,10000),(2,9000)):
   s.accept(w.Frame(seq,0xa0,struct.pack('<III7hI',stamp,0,20,*([1]*7),1),b''))
  self.assertEqual(s.imu_span_us,0)
  self.assertEqual(s.counts['imu_timestamp_regressions'],1)
 def test_profile_retained_to_sink(self):
  out=io.StringIO();s=w.Streams(io.StringIO(),io.StringIO(),out)
  s.accept(w.Frame(0,0xb3,struct.pack('<37I',1,123,*range(35)),b''))
  import json
  event=json.loads(out.getvalue())
  self.assertEqual(event['profiles']['imu']['calls'],0)
  self.assertEqual(event['profiles']['lcd']['max_us'],33)
  s.accept(w.Frame(0,0xb3,struct.pack('<37I',2,123,*range(35)),b''))
  self.assertEqual(s.counts['malformed_frames'],1)
 def test_finish_current_frame_only(self):
  wire=w.encode_frame(1,0xa0,struct.pack('<III7hI',100,0,20,*([1]*7),1))
  port=Port(None);port.data=wire[5:]+wire
  c=w.Client(port);c.decoder.feed(wire[:5]);c.raw=io.BytesIO()
  self.assertTrue(c.finish_frame())
  self.assertEqual(port.data,wire)
  self.assertEqual(c.raw.getvalue(),wire[5:])
 def test_adc_rate_detects_half_speed(self):
  s=w.Streams(io.StringIO(),io.StringIO());s.run=1
  for i in range(3):
   s.accept(w.Frame(i,0xb0,struct.pack('<IIIIHH',1,i,i*100,i*200000,100,1)+bytes(200),b''))
  self.assertEqual(s.adc_rate(),500)
 def test_raw_full_run_coverage(self):
  s=w.Streams(io.StringIO(),io.StringIO());s.imu_span_us=60_000_000;s.counts['imu_host_reads']=30000
  errors=w.coverage_errors(s,1800,True,False,None,None,2)
  self.assertIn('IMU raw does not cover full capture',errors)
 def test_fault_evidence_requires_observation(self):
  before=dict(status=dict(inject_count=1,jitter_over_200=0,journal_state=4,journal_errors=0),adc=dict(overwritten_blocks=0),counts=dict(adc_missing_samples=0))
  final=dict(inject_count=2,jitter_over_200=1,journal_state=4,journal_errors=0)
  self.assertTrue(w.observe_fault('load',True,before,final,dict(overwritten_blocks=0),dict(adc_missing_samples=0),False))
  self.assertFalse(w.observe_fault('erase',True,before,final,{}, {},False))
  self.assertTrue(w.observe_fault('erase',True,before,final,{}, {},True))
  self.assertFalse(w.observe_fault('pause',True,before,final,dict(overwritten_blocks=1),dict(adc_missing_samples=0),False))
  self.assertTrue(w.observe_fault('pause',True,before,final,dict(overwritten_blocks=1),dict(adc_missing_samples=100),False))
 def test_existing_capture_not_overwritten(self):
  with tempfile.TemporaryDirectory() as d:
   sentinel=Path(d)/'raw.bin';sentinel.write_bytes(b'evidence')
   with self.assertRaises(FileExistsError):w.record(None,SimpleNamespace(out=Path(d)))
   self.assertEqual(sentinel.read_bytes(),b'evidence')
 def test_cli_baud_and_duration(self):
  from unittest.mock import patch
  from contextlib import nullcontext
  with patch.object(sys,'argv',['tool','--port','COM_TEST','status']),patch.object(w,'open_port',return_value=nullcontext(None)) as op,patch.object(w.Client,'command',return_value={}),patch('builtins.print'):
   w.main();op.assert_called_once_with('COM_TEST',460800)
  with patch.object(sys,'argv',['tool','--port','COM_TEST','record','--seconds','3601','--out','unused']),patch('sys.stderr',io.StringIO()):
   with self.assertRaises(SystemExit):w.main()
 def test_encoder_four_edges_per_cycle(self):
  self.assertTrue(w.encoder_coverage_valid(720000,1800))
  self.assertFalse(w.encoder_coverage_valid(180000,1800))
  self.assertFalse(w.encoder_coverage_valid(360000,1800))
 def test_journal_wait_for_verified_ready(self):
  class Pending:
   def __init__(self):self.calls=0
   def command(self,*args,**kw):self.calls+=1;return dict(journal_state=4)
  c=Pending();self.assertEqual(w.close_journal(c,dict(journal_state=6))['journal_state'],4)
  self.assertEqual(c.calls,1)
 def test_f411_extended_timing_and_signed_error(self):
  out=io.StringIO();stream=w.Streams(io.StringIO(),io.StringIO(),out)
  stream.accept(w.Frame(0,0xa6,struct.pack('<10I',1,500,499,2000,9,10,0,0,998000,0),b''))
  import json
  event=json.loads(out.getvalue())
  self.assertEqual(event['type'],'f411_timing')
  self.assertEqual(event['p99_error_upper_us'],10)
  words=[0]*48;words[0]=1;words[44]=0xfffffffe
  self.assertEqual(w.status5(struct.pack('<48I',*words))['imu_last_error'],-2)
 def test_malformed_payload(self):
  s=w.Streams(io.StringIO(),io.StringIO());s.accept(w.Frame(0,0xa0,b'',b''))
  self.assertEqual(s.counts['malformed_frames'],1)

if __name__=='__main__':unittest.main()
