import importlib.util
from pathlib import Path
import unittest

P=Path(__file__).resolve().parents[2]/'tools/week5_uart_flood.py'
spec=importlib.util.spec_from_file_location('flood',P)
f=importlib.util.module_from_spec(spec);spec.loader.exec_module(f)

class FloodTests(unittest.TestCase):
    def test_bad_frames_never_decode(self):
        d=f.FrameDecoder()
        self.assertEqual(d.feed(f.bad_wire()*10000),[])
        good=f.encode_frame(42,1,b'recovered')
        self.assertEqual(d.feed(good)[0].payload,b'recovered')
    def test_assessment_rejects_bad_execution(self):
        before=dict(ping_count=3,crc_errors=0)
        after=dict(ping_count=4,crc_errors=100)
        self.assertFalse(f.assess(before,after,True)['bad_frames_zero_execution'])
        after['ping_count']=3
        self.assertTrue(f.assess(before,after,True)['bad_frames_zero_execution'])
    def test_wrap_counters(self):
        self.assertEqual(f.delta(1,0xffffffff),2)

if __name__=='__main__':unittest.main()
