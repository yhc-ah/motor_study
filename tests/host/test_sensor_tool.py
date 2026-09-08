import csv
import contextlib
import io
import importlib.util
import json
import math
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / 'tools/sensor_test/sensor_test.py'


class SensorToolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.mod = None
        if TOOL.exists():
            spec = importlib.util.spec_from_file_location('sensor_tool_under_test', TOOL)
            cls.mod = importlib.util.module_from_spec(spec)
            sys.modules[spec.name] = cls.mod
            spec.loader.exec_module(cls.mod)

    def tool(self):
        self.assertIsNotNone(self.mod, 'week 3 sensor CLI is not implemented')
        return self.mod

    def test_fragmentation_crc_noise_illegal_lengths_and_recovery(self):
        m = self.tool()
        d = m.CountedDecoder()
        good = m.encode_frame(0xffffffff, 0xa0, b'abc\xaa\x55xyz')
        bad = bytearray(good)
        bad[-1] ^= 1
        wire = b'noise' + b'\xaa\x55\xff\xff' + b'\xaa\x55\x00\x00' + bytes(bad) + good
        frames = []
        for b in wire:
            frames.extend(d.feed(bytes([b])))
        self.assertEqual([(f.sequence, f.payload) for f in frames], [(0xffffffff, b'abc\xaa\x55xyz')])
        self.assertGreaterEqual(d.counts['crc_errors'], 1)
        self.assertGreaterEqual(d.counts['length_errors'], 2)
        self.assertGreaterEqual(d.counts['noise_bytes'], 5)
        self.assertEqual(d.counts['valid_frames'], 1)

    def test_plausible_corrupt_length_does_not_hold_next_valid_frame(self):
        m = self.tool()
        d = m.CountedDecoder()
        good = m.encode_frame(9, 0x90, b'\x01')
        frames = d.feed(b'\xaa\x55\xf5\x00garbage' + good)
        self.assertEqual([f.sequence for f in frames], [9])
        self.assertEqual(d.counts['resync_events'], 1)

    def test_signed_sensor_fields_status_and_strict_payload_sizes(self):
        m = self.tool()
        p = struct.pack('<III7hHH', 123, 2, 41, -1, -32768, 32767, -3, -4, 5, -6, 3, 0)
        row = m.decode_imu(p)
        self.assertEqual((row['ax'], row['ay'], row['gz']), (-1, -32768, -6))
        self.assertEqual(row['valid'], 1)
        th = m.decode_th(struct.pack('<IiiII', 1, -1000, 55000, 3, 1))
        self.assertEqual(th['temp_mC'], -1000)
        fields = [0] * 32
        fields[19] = 0xfffffffe
        fields[24] = 5
        status = m.decode_status(struct.pack('<32I', *fields))
        self.assertEqual(status['flash_result'], -2)
        self.assertTrue(status['capability_flags']['int_route'])
        self.assertFalse(status['capability_flags']['th_configured'])
        for fn, size in ((m.decode_imu, 30), (m.decode_th, 20), (m.decode_status, 128)):
            for n in (0, size - 1, size + 1):
                with self.assertRaises(ValueError):
                    fn(bytes(n))

    def test_sequence_wrap_gaps_duplicate_out_of_order_and_time_wrap(self):
        m = self.tool()
        tracker = m.SampleTracker()
        tracker.observe(0xfffffffe, 0xfffffff0)
        tracker.observe(0xffffffff, 0xfffffff8)
        result = tracker.observe(0, 8)
        self.assertEqual(result['elapsed_us'], 24)
        tracker.observe(0, 8)
        tracker.observe(0xffffffff, 0xfffffff8)
        tracker.observe(3, 32)
        self.assertEqual(tracker.counts, {'sequence_gaps': 2, 'duplicates': 1,
            'out_of_order': 1, 'sequence_wraps': 1, 'time_wraps': 1, 'time_regressions': 0})

    def test_independent_validation_statistics_and_sixty_second_calibration(self):
        m = self.tool()
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            def write(name, values):
                path = base / name
                with path.open('w', newline='') as f:
                    w = csv.DictWriter(f, fieldnames=['elapsed_s', 'valid', *m.AXES])
                    w.writeheader()
                    for t, value in values:
                        w.writerow(dict(elapsed_s=t, valid=1, **{a: value for a in m.AXES}))
                return path
            cal = write('cal.csv', [(0, 1), (1, 3), (60, 1000)])
            val = write('val.csv', [(0, 4), (1, 6), (2, 8)])
            result = m.analyze_csv(val, base / 'report', cal)
            self.assertEqual(result['calibration']['samples'], 2)
            self.assertFalse(result['calibration']['first_60s_covered'])
            ax = result['axes']['ax']
            self.assertEqual(ax['raw']['mean'], 6)
            self.assertEqual(ax['raw']['stdev'], 2)
            self.assertEqual(ax['raw']['min'], 4)
            self.assertEqual(ax['raw']['max'], 8)
            self.assertNotIn('compensated', ax)
            self.assertEqual(ax['physical_raw']['mean'], 6 / 16384)
            gx = result['axes']['gx']
            self.assertEqual(gx['compensated']['mean'], 4)
            self.assertEqual(gx['compensated']['stdev'], 2)
            self.assertTrue((base / 'report' / 'axes.svg').exists())
            with self.assertRaises(ValueError):
                m.analyze_csv(cal, base / 'same', cal)

    def test_rejected_time_regression_does_not_double_count_sequence_gaps(self):
        m = self.tool()
        tracker = m.SampleTracker()
        tracker.observe(10, 100)
        rejected = tracker.observe(12, 90)
        self.assertEqual(rejected['eligible'], 0)
        self.assertEqual(tracker.counts['sequence_gaps'], 0)
        accepted = tracker.observe(12, 120)
        self.assertEqual(accepted['eligible'], 1)
        self.assertEqual(tracker.counts['sequence_gaps'], 1)
        self.assertEqual(tracker.counts['time_regressions'], 1)
        self.assertEqual(accepted['elapsed_us'], 20)

    def test_rejected_time_regression_does_not_double_count_sequence_wrap(self):
        m = self.tool()
        tracker = m.SampleTracker()
        tracker.observe(0xffffffff, 100)
        tracker.observe(0, 90)
        self.assertEqual(tracker.counts['sequence_wraps'], 0)
        tracker.observe(0, 120)
        self.assertEqual(tracker.counts['sequence_wraps'], 1)
        self.assertEqual(tracker.counts['time_regressions'], 1)

    def test_malformed_csv_and_invalid_cli_durations_are_rejected(self):
        m = self.tool()
        for value in ('-1', '0', 'nan', 'inf'):
            with self.assertRaises(ValueError):
                m.positive_seconds(value)
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / 'bad.csv'
            p.write_text('elapsed_s,valid,ax,ay,az,gx,gy,gz\n0,1,nan,1,2,3,4,5\n')
            with self.assertRaises(ValueError):
                m.read_imu_csv(p)

    def test_command_requires_matching_sequence_and_ack_payload(self):
        m = self.tool()
        class Port:
            def __init__(self): self.data = b''
            @property
            def in_waiting(self): return len(self.data)
            def write(self, data):
                seq = struct.unpack_from('<I', data, 4)[0]
                self.data += m.encode_frame(seq + 1, 0x90, b'\x01')
                self.data += m.encode_frame(seq, 0x90, b'\x00')
                return len(data)
            def flush(self): pass
            def read(self, n):
                result, self.data = self.data[:n], self.data[n:]
                return result
        client = m.Client(Port())
        with self.assertRaises(ValueError):
            client.command(0x10, b'\x01', 0x90, expected=b'\x01', timeout=0.01)
        class Silent(Port):
            def write(self, data): return len(data)
        with self.assertRaises(TimeoutError):
            m.Client(Silent()).command(0x11, b'', 0x91, timeout=0.005)

    def test_flash_requires_explicit_flag(self):
        m = self.tool()
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            m.parse_args(['flash-test', '--port', 'COM1', '--outputdir', 'out'])

    def device(self, m, flash_result=0, reject_start=False):
        class Device:
            def __init__(self):
                self.data, self.commands, self.streaming, self.sample = b'', [], 0, 0
                self.fields = [0] * 32
                self.fields[24] = 15
            @property
            def in_waiting(self): return len(self.data)
            def write(self, wire):
                seq = struct.unpack_from('<I', wire, 4)[0]
                cmd, payload = wire[8], wire[9:-2]
                self.commands.append((cmd, payload))
                if cmd == 0x10:
                    self.streaming = payload[0]
                    self.data += m.encode_frame(seq, 0x90, b'\x00' if reject_start else payload)
                    if payload[0] == 0 and self.sample:
                        self.sample += 1
                        p = struct.pack('<III7hHH', self.sample * 2000, self.sample, 30, *([1] * 7), 3, 0)
                        self.data += m.encode_frame(self.sample, 0xa0, p)
                elif cmd == 0x11:
                    self.fields[23] = self.streaming
                    self.fields[1] = self.sample
                    self.data += m.encode_frame(seq, 0x91, struct.pack('<32I', *self.fields))
                elif cmd == 0x12:
                    self.data += m.encode_frame(seq, 0x92, struct.pack('<i', flash_result))
                return len(wire)
            def flush(self): pass
            def read(self, n):
                if not self.data and self.streaming:
                    self.sample += 1
                    p = struct.pack('<III7hHH', self.sample * 2000, self.sample, 30, *([1] * 7), 3, 0)
                    self.data = m.encode_frame(self.sample, 0xa0, p)
                result, self.data = self.data[:n], self.data[n:]
                return result
        return Device()

    def test_record_creates_evidence_verifies_stop_drains_and_never_starts_flash(self):
        m = self.tool()
        self.assertTrue(hasattr(m, 'record_session'), 'record_session is not implemented')
        port = self.device(m)
        with tempfile.TemporaryDirectory() as tmp:
            result = m.record_session(port, .02, Path(tmp))
            self.assertTrue(result['start_ack_verified'])
            self.assertTrue(result['stop_ack_verified'])
            self.assertEqual(result['hardware_verdict'], 'NOT_ASSESSED')
            self.assertEqual(result['session_result'], 'COMPLETE')
            self.assertNotIn(0x12, [cmd for cmd, _ in port.commands])
            self.assertFalse(port.streaming)
            with (Path(tmp) / 'imu.csv').open() as f:
                rows = list(csv.DictReader(f))
            self.assertEqual(int(rows[-1]['sequence']), port.sample)
            for name in ('raw.bin', 'imu.csv', 'th.csv', 'status.jsonl', 'summary.json'):
                self.assertTrue((Path(tmp) / name).exists())
            snapshots = [json.loads(line) for line in (Path(tmp) / 'status.jsonl').read_text().splitlines()]
            self.assertGreaterEqual(len(snapshots), 2)
            self.assertEqual(snapshots[-1]['status']['stream_enabled'], 0)

    def test_failed_start_preserves_evidence_and_stops_device(self):
        m = self.tool()
        self.assertTrue(hasattr(m, 'record_session'), 'record_session is not implemented')
        port = self.device(m, reject_start=True)
        with tempfile.TemporaryDirectory() as tmp:
            result = m.record_session(port, .01, Path(tmp))
            self.assertEqual(result['session_result'], 'INCOMPLETE')
            self.assertFalse(port.streaming)
            self.assertTrue(result['errors'])
            self.assertTrue((Path(tmp) / 'summary.json').exists())

    def test_record_rate_does_not_count_duplicate_frames(self):
        m = self.tool()
        port = self.device(m)
        original_read = port.read
        def duplicate_read(n):
            data = original_read(n)
            # Force one full frame per read from this fake device.
            if data and port.streaming and data.startswith(b'\xaa\x55') and len(data) == 41:
                return data + data
            return data
        # Real serial may return partial frames; this fake returns a full pending frame.
        def full_read(n):
            return duplicate_read(65536)
        port.read = full_read
        with tempfile.TemporaryDirectory() as tmp:
            result = m.record_session(port, .02, Path(tmp))
            self.assertGreater(result['sample_counters']['duplicates'], 0)
            self.assertAlmostEqual(result['observed_rate_hz'], 500)

    def test_flash_reports_negative_ack_and_finite_polling(self):
        m = self.tool()
        self.assertTrue(hasattr(m, 'flash_session'), 'flash_session is not implemented')
        with tempfile.TemporaryDirectory() as tmp:
            result = m.flash_session(self.device(m, flash_result=-4), Path(tmp), timeout=.01)
            self.assertEqual(result['accepted_result'], -4)
            self.assertEqual(result['session_result'], 'REJECTED')
            self.assertEqual(result['hardware_verdict'], 'NOT_ASSESSED')
        port = self.device(m)
        port.fields[22] = 1
        with tempfile.TemporaryDirectory() as tmp:
            result = m.flash_session(port, Path(tmp), timeout=.01)
            self.assertEqual(result['session_result'], 'TIMEOUT')


if __name__ == '__main__':
    unittest.main()
