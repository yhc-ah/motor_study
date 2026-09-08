import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
P=Path(__file__).resolve().parents[2]/'tools/week4_test/week4_test.py'

class Week4Tests(unittest.TestCase):
 def setUp(self):
  self.assertTrue(P.exists(), 'week4 CLI implementation missing')
  spec=importlib.util.spec_from_file_location('week4',P); self.w=importlib.util.module_from_spec(spec); spec.loader.exec_module(self.w)
 def block(self, seq=0, first=0, run=1, count=100, flags=1):
  payload=struct.pack('<IIIIHH',run,seq,first,(first+count)*1000,count,flags)+struct.pack('<'+'H'*count,*([1234]*count))
  return self.w.Frame(seq,0xB0,payload,b'')
 def test_fragment_crc_and_interleaving(self):
  w=self.w
  class Port:
   in_waiting=1
   def write(s,b):
    seq=struct.unpack_from('<I',b,4)[0]; bad=bytearray(w.encode_frame(seq,0xA8,struct.pack('<i',0))); bad[-1]^=1
    s.data=bytearray(bad+w.encode_frame(0,0xB0,self.block().payload)+w.encode_frame(seq,0xA8,struct.pack('<i',0))); return len(b)
   def read(s,n):
    b=bytes(s.data[:1]);del s.data[:1];return b
  seen=[]; c=w.Client(Port(),seen.append);c.command(0x20,b'\1');self.assertEqual(len([f for f in seen if f.command==0xB0]),1);self.assertEqual(c.decoder.counts['crc_errors'],1)
 def test_bad_ack_payload_and_sequence(self):
  w=self.w
  class Port:
   in_waiting=4096
   def write(s,b):s.data=w.encode_frame(99,0xA8,struct.pack('<i',0))+w.encode_frame(1,0xA8,b'');return len(b)
   def read(s,n):b=s.data;s.data=b'';return b
  with self.assertRaises(ValueError):w.Client(Port()).command(0x20,b'\1',timeout=.02)
 def test_gaps_duplicate_other_run_tail(self):
  t=self.w.Tracker(1);self.assertEqual(len(t.accept(self.block(1,100))),100);self.assertEqual(t.counts['missing_blocks'],1)
  self.assertEqual(t.accept(self.block(1,100)),[]);self.assertEqual(t.counts['duplicates'],1)
  self.assertEqual(t.accept(self.block(2,200,run=2)),[]);self.assertEqual(t.counts['run_changes'],1)
  t.accept(self.block(2,200,count=7,flags=3)); t.close({'samples_completed':210,'blocks_sent':3,'samples_sent':210});self.assertEqual(t.counts['closing_missing_samples'],3)
 def test_sequence_wrap(self):
  t=self.w.Tracker(1);t.last_seq=0xffffffff;t.next_sample=0;t.accept(self.block());self.assertEqual(t.counts['sequence_wraps'],1)
 def test_malformed(self):
  t=self.w.Tracker(1);f=self.block();self.assertEqual(t.accept(self.w.Frame(0,0xB0,f.payload[:-1],b'')),[]);self.assertEqual(t.counts['malformed_blocks'],1)
 def test_filter_and_step(self):
  r=self.w.step_test();self.assertTrue(r['passed']);self.assertAlmostEqual(r['tau5']['t63_ms'],5,delta=1);self.assertAlmostEqual(r['tau20']['t63_ms'],20,delta=1)
  rows=[dict(sample_seq=i,raw=100 if i<2 else 200,valid=1) for i in [0,1,4]];self.w.filter_rows(rows);self.assertEqual(rows[-1]['filter5'],200)
 def test_duration(self):
  for value in ['0','-1','nan','inf','90000']:
   with self.assertRaises(Exception):self.w.duration(value)
 def test_record_failure_stops_and_preserves(self):
  w=self.w
  class C:
   decoder=w.CountedDecoder()
   def __init__(s):s.commands=[]
   def status(s):return dict(run_id=1,running=0)
   def command(s,cmd,payload=b'',**kw):
    s.commands.append((cmd,payload))
    if cmd==0x20 and payload==b'\1':raise TimeoutError('lost ACK')
    if cmd==0x20 and payload==b'\0':raise TimeoutError('stop timeout')
   def poll(s):return []
  with tempfile.TemporaryDirectory() as d:
   c=C();r=w.record(c,Path(d),.01);self.assertIn((0x20,b'\0'),c.commands);self.assertTrue((Path(d)/'summary.json').exists());self.assertEqual(r['hardware_verdict'],'NOT_ASSESSED');self.assertTrue(r['errors'])


class Week4IntegrationTests(unittest.TestCase):
 def test_record_interleaved_start_and_stop_tail(self):
  spec=importlib.util.spec_from_file_location('week4',P);w=importlib.util.module_from_spec(spec);spec.loader.exec_module(w)
  class Port:
   def __init__(s):s.data=bytearray();s.run=0;s.running=0
   @property
   def in_waiting(s):return len(s.data)
   def read(s,n):b=bytes(s.data[:min(n,7)]);del s.data[:len(b)];return b
   def write(s,wire):
    seq=struct.unpack_from('<I',wire,4)[0];cmd=wire[8];payload=wire[9:-2]
    if cmd==0x21:
     status=dict.fromkeys(w.FIELDS,0);status.update(version=1,run_id=s.run,running=s.running,samples_completed=107 if s.run and not s.running else 100 if s.run else 0,blocks_completed=1 if s.run else 0,blocks_sent=2 if s.run and not s.running else 1 if s.run else 0,samples_sent=107 if s.run and not s.running else 100 if s.run else 0)
     s.data.extend(w.encode_frame(seq,0xA9,struct.pack('<32I',*[status[k] for k in w.FIELDS])))
    elif cmd==0x20:
     s.running=payload[0]
     if s.running:s.run=1
     n=100 if s.running else 7;block=0 if s.running else 1;first=0 if s.running else 100
     s.data.extend(w.encode_frame(block,0xB0,struct.pack('<IIIIHH',1,block,first,(first+n)*1000,n,1 if s.running else 3)+struct.pack('<'+'H'*n,*([1234]*n))))
     s.data.extend(w.encode_frame(seq,0xA8,struct.pack('<i',0)))
    return len(wire)
  with tempfile.TemporaryDirectory() as d:
   r=w.record(w.Client(Port()),Path(d),.001)
   self.assertTrue(r['report_ok'],r['errors']);self.assertEqual(r['samples_received'],107);self.assertGreater((Path(d)/'raw.bin').stat().st_size,0)
   analysis=w.analyze(Path(d),settle_ms=0);self.assertEqual(analysis['filters']['raw']['n'],107)




class Week4ReviewRegressionTests(unittest.TestCase):
 def setUp(self):
  spec=importlib.util.spec_from_file_location('week4',P);self.w=importlib.util.module_from_spec(spec);spec.loader.exec_module(self.w)
 def test_busy_baseline_not_stopped(self):
  w=self.w
  class C:
   def __init__(s):s.decoder=w.CountedDecoder();s.commands=[]
   def status(s):return dict(run_id=1,running=1)
   def command(s,*args,**kwargs):s.commands.append(args)
   def poll(s):return []
  with tempfile.TemporaryDirectory() as d:
   c=C();r=w.record(c,Path(d),.001,joint=True);self.assertEqual(c.commands,[])
 def test_empty_and_short_coverage_are_not_success(self):
  w=self.w
  self.assertTrue(hasattr(w,'assess_record'),'record assessment missing')
  r=w.assess_record(w.Tracker(1),dict(samples_completed=0),60,0,False)
  self.assertFalse(r['acquisition_passed']);self.assertEqual(r['rate_verdict'],'NOT_ASSESSED')
 def test_fault_requires_injection_and_evidence(self):
  w=self.w;self.assertTrue(hasattr(w,'assess_record'),'record assessment missing')
  t=w.Tracker(1)
  r=w.assess_record(t,dict(overwritten_blocks=1,samples_completed=100),1,250,False)
  self.assertFalse(r['fault_detected'])
 def test_svg_keeps_gap_and_invalid_breaks(self):
  with tempfile.TemporaryDirectory() as d:
   rows=[dict(sample_seq=i,raw=100,filter5=100,filter20=100,valid=int(i!=101)) for i in [0,1,100,101,102,103]]
   p=Path(d)/'x.svg';self.w.svg(p,rows,'gap');s=p.read_text()
   self.assertGreaterEqual(s.count('<polyline'),9)
   self.assertIn('nominal',s.lower())
 def test_encoder_waits_and_saves_evidence(self):
  w=self.w;self.assertTrue(hasattr(w,'encoder_run'),'finite encoder runner missing')
  class C:
   def __init__(s):s.decoder=w.CountedDecoder();s.started=False;s.calls=0
   def command(s,cmd,p=b'',**kw):s.started=cmd==0x23
   def status(s):
    s.calls+=1
    return dict(generator_active=0,generator_steps=4000 if s.started else 0,encoder_position_low=-4000 if s.started else 0,encoder_cnt=61536 if s.started else 0,hardware_errors=0,tx_errors=0,tx_dropped=0)
   def poll(s):return []
  with tempfile.TemporaryDirectory() as d:
   c=C();r=w.encoder_run(c,Path(d),1000,1000,1,0)
   self.assertTrue(r['report_ok'],r);self.assertEqual(r['expected_magnitude'],4000);self.assertGreaterEqual(c.calls,3);self.assertTrue((Path(d)/'status.jsonl').exists())

class Week4AssessmentTests(unittest.TestCase):
 def setUp(self):
  spec=importlib.util.spec_from_file_location('week4',P);self.w=importlib.util.module_from_spec(spec);spec.loader.exec_module(self.w)
 def tracker(self,interval=100000,blocks=601):
  t=self.w.Tracker(1)
  for i in range(blocks):
   p=struct.pack('<IIIIHH',1,i,i*100,(i+1)*interval,100,1)+struct.pack('<100H',*([1234]*100))
   t.accept(self.w.Frame(i,0xB0,p,b''))
  return t
 def test_long_capture_checks_rate_and_coverage(self):
  t=self.tracker();r=self.w.assess_record(t,dict(samples_completed=60100),60,0,False)
  self.assertTrue(r['acquisition_passed']);self.assertEqual(r['rate_verdict'],'PASS')
  t=self.tracker(interval=110000);r=self.w.assess_record(t,dict(samples_completed=60100),60,0,False)
  self.assertFalse(r['acquisition_passed']);self.assertEqual(r['rate_verdict'],'FAIL')
  t=self.tracker(blocks=11);r=self.w.assess_record(t,dict(samples_completed=1100),60,0,False)
  self.assertFalse(r['acquisition_passed'])
 def test_fault_requires_missing_samples_and_overwrite(self):
  t=self.tracker(blocks=1);t.counts['missing_samples']=100
  self.assertTrue(self.w.assess_record(t,dict(samples_completed=200,overwritten_blocks=1),1,250,True)['fault_detected'])
  self.assertFalse(self.w.assess_record(t,dict(samples_completed=200,overwritten_blocks=0),1,250,True)['fault_detected'])
 def test_short_requested_capture_and_fault_never_pass_normal_acquisition(self):
  t=self.tracker(blocks=20)
  self.assertFalse(self.w.assess_record(t,dict(samples_completed=2000),59,0,False)['acquisition_passed'])
  t.counts['missing_samples']=100
  self.assertFalse(self.w.assess_record(t,dict(samples_completed=2000,overwritten_blocks=1),1,250,True)['acquisition_passed'])
 def test_integrity_error_prevents_acquisition_pass(self):
  t=self.tracker(blocks=20);t.counts['duplicates']=1
  self.assertFalse(self.w.assess_record(t,dict(samples_completed=2000),1,0,False)['acquisition_passed'])
  t.counts['duplicates']=0
  self.assertFalse(self.w.assess_record(t,dict(samples_completed=2000,adc_overruns=1),1,0,False)['acquisition_passed'])
 def test_noise_statistics_exclude_same_settling_after_gap(self):
  with tempfile.TemporaryDirectory() as d:
   path=Path(d)/'adc.csv'
   path.write_text('sample_seq,raw,valid\n'+''.join(f'{i},2048,1\n' for i in list(range(300))+list(range(500,800))))
   r=self.w.analyze(Path(d),settle_ms=200)
   self.assertEqual(r['filters']['raw']['n'],200)
   self.assertEqual(r['filters']['filter5']['n'],200)

if __name__=='__main__':unittest.main()
