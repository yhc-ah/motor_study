"""Week 3 UART acquisition and offline analysis; Python stdlib + pyserial."""
from __future__ import annotations

import argparse
from contextlib import ExitStack
import csv
import json
import math
from pathlib import Path
import statistics
import struct
import sys
import time
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'uart_test'))
from uart_test import Frame, crc16_modbus, encode_frame, open_port

SOF = b'\xaa\x55'
AXES = ('ax', 'ay', 'az', 'gx', 'gy', 'gz')
STATUS_FIELDS = ('uptime_ms samples imu_errors imu_recoveries imu_valid imu_age_ms '
    'bus_errors bus_recoveries bus_recovery_failures max_bus_us max_response_us '
    'deadline_misses drdy_events drdy_missed tx_dropped tx_errors tx_queued '
    'tx_high_watermark flash_id flash_result flash_rounds flash_mismatches '
    'flash_active stream_enabled capabilities th_valid th_errors th_samples '
    'th_age_ms flash_cross_page_rounds flash_elapsed_ms flash_first_mismatch').split()
CAPABILITIES = ('int_route', 'th_configured', 'lcd_adapter', 'flash_range_configured')


class CountedDecoder:
    """Incremental CRC decoder, retaining a partial SOF and counting discarded data.

    A later complete CRC-valid frame can rescue a corrupt plausible length. As
    with any unescaped protocol this is ambiguous if a partial payload happens
    to contain an entire valid frame; retain raw.bin for forensic inspection.
    """
    def __init__(self):
        self.buffer = bytearray()
        self.counts = dict(valid_frames=0, crc_errors=0, length_errors=0,
                           noise_bytes=0, resync_events=0)

    def _discard(self, count):
        self.counts['noise_bytes'] += count
        del self.buffer[:count]

    def _later_valid(self):
        start = self.buffer.find(SOF, 2)
        while start >= 0:
            if len(self.buffer) >= start + 4:
                size = struct.unpack_from('<H', self.buffer, start + 2)[0]
                end = start + size + 6
                if 5 <= size <= 245 and end <= len(self.buffer):
                    if crc16_modbus(self.buffer[start + 2:end - 2]) == struct.unpack_from('<H', self.buffer, end - 2)[0]:
                        return start
            start = self.buffer.find(SOF, start + 2)
        return None

    def feed(self, data):
        self.buffer.extend(data)
        frames = []
        while self.buffer:
            start = self.buffer.find(SOF)
            if start < 0:
                self._discard(len(self.buffer) - (self.buffer[-1:] == b'\xaa'))
                break
            self._discard(start)
            if len(self.buffer) < 4:
                break
            size = struct.unpack_from('<H', self.buffer, 2)[0]
            if not 5 <= size <= 245:
                self.counts['length_errors'] += 1
                self._discard(1)
                continue
            end = size + 6
            if len(self.buffer) < end:
                later = self._later_valid()
                if later is None:
                    break
                self.counts['resync_events'] += 1
                self._discard(later)
                continue
            if crc16_modbus(self.buffer[2:end - 2]) != struct.unpack_from('<H', self.buffer, end - 2)[0]:
                self.counts['crc_errors'] += 1
                self._discard(1)
                continue
            raw = bytes(self.buffer[:end])
            frames.append(Frame(struct.unpack_from('<I', raw, 4)[0], raw[8], raw[9:-2], raw))
            del self.buffer[:end]
            self.counts['valid_frames'] += 1
        return frames


def unpack_exact(fmt, payload):
    if len(payload) != struct.calcsize(fmt):
        raise ValueError(f'payload length {len(payload)} != {struct.calcsize(fmt)}')
    return struct.unpack(fmt, payload)


def decode_imu(payload):
    keys = ('time_us drdy_count read_duration_us ax ay az temp gx gy gz flags reserved').split()
    row = dict(zip(keys, unpack_exact('<III7hHH', payload)))
    row.update(valid=int(bool(row['flags'] & 1)), int_timestamp=int(bool(row['flags'] & 2)),
               device_gap=int(bool(row['flags'] & 4)))
    return row


def decode_th(payload):
    return dict(zip(('time_ms', 'temp_mC', 'rh_milli_percent', 'sample_count', 'valid'),
                    unpack_exact('<IiiII', payload)))


def decode_status(payload):
    result = dict(zip(STATUS_FIELDS, unpack_exact('<32I', payload)))
    if result['flash_result'] >= 0x80000000:
        result['flash_result'] -= 0x100000000
    result['capability_flags'] = {name: bool(result['capabilities'] & (1 << bit))
                                 for bit, name in enumerate(CAPABILITIES)}
    return result


class SampleTracker:
    """Modulo counters assume forward intervals smaller than 2**31 ticks.

    Reordered/duplicate samples do not advance the accepted reference. A reset
    is not distinguishable from a large counter reversal and is flagged.
    """
    def __init__(self):
        self.sequence = self.timestamp = None
        self.elapsed = 0
        self.counts = dict(sequence_gaps=0, duplicates=0, out_of_order=0,
                           sequence_wraps=0, time_wraps=0, time_regressions=0)

    def observe(self, sequence, timestamp):
        kind = 'first'
        eligible = True
        if self.sequence is not None:
            delta = (sequence - self.sequence) & 0xffffffff
            if delta == 0:
                self.counts['duplicates'] += 1
                kind, eligible = 'duplicate', False
            elif delta >= 0x80000000:
                self.counts['out_of_order'] += 1
                kind, eligible = 'out_of_order', False
            else:
                kind = 'gap' if delta > 1 else 'ok'
                dt = (timestamp - self.timestamp) & 0xffffffff
                if dt >= 0x80000000:
                    self.counts['time_regressions'] += 1
                    kind, eligible = 'time_regression', False
                else:
                    self.counts['sequence_gaps'] += delta - 1
                    self.counts['sequence_wraps'] += int(sequence < self.sequence)
                    self.counts['time_wraps'] += int(timestamp < self.timestamp)
                    self.elapsed += dt
        if eligible:
            self.sequence, self.timestamp = sequence, timestamp
        return dict(elapsed_us=self.elapsed, elapsed_s=self.elapsed / 1e6,
                    order=kind, eligible=int(eligible))


class Client:
    def __init__(self, port, on_frame=None, raw=None):
        self.port, self.on_frame, self.raw = port, on_frame, raw
        self.decoder = CountedDecoder()
        self.sequence = 0

    def poll(self):
        data = self.port.read(min(self.port.in_waiting or 1, 65536))
        if self.raw and data:
            self.raw.write(data)
        frames = self.decoder.feed(data)
        for frame in frames:
            if self.on_frame:
                self.on_frame(frame)
        return frames

    def command(self, command, payload, response, expected=None, timeout=2.0):
        self.sequence = (self.sequence + 1) & 0xffffffff
        wire = encode_frame(self.sequence, command, payload)
        if self.port.write(wire) != len(wire):
            raise OSError('short serial write')
        self.port.flush()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for frame in self.poll():
                if frame.sequence != self.sequence or frame.command not in (response, 0xff):
                    continue
                if frame.command == 0xff:
                    raise ValueError(f'device rejected command: {frame.payload.hex()}')
                if expected is not None and frame.payload != expected:
                    raise ValueError('ACK payload mismatch')
                return frame.payload
        raise TimeoutError(f'command 0x{command:02x} response timeout')

    def status(self):
        return decode_status(self.command(0x11, b'', 0x91))


def positive_seconds(value):
    result = float(value)
    if not math.isfinite(result) or result <= 0:
        raise ValueError('seconds must be finite and positive')
    return result


def read_imu_csv(path):
    rows = []
    with Path(path).open(newline='', encoding='utf-8-sig') as f:
        reader = csv.DictReader(f)
        if not set(('elapsed_s', 'valid', *AXES)).issubset(reader.fieldnames or []):
            raise ValueError('CSV requires elapsed_s, valid and six raw axes')
        for line, raw in enumerate(reader, 2):
            try:
                row = {key: float(raw[key]) for key in ('elapsed_s', 'valid', *AXES)}
                if not all(math.isfinite(value) for value in row.values()) or row['elapsed_s'] < 0:
                    raise ValueError('nonfinite value or negative time')
                if row['valid'] not in (0, 1):
                    raise ValueError('valid must be 0 or 1')
                eligible = int(raw.get('eligible', '1'))
                if eligible not in (0, 1):
                    raise ValueError('eligible must be 0 or 1')
            except (ValueError, TypeError, KeyError) as error:
                raise ValueError(f'{path}:{line}: malformed sensor row') from error
            if row['valid'] and eligible:
                if rows and row['elapsed_s'] < rows[-1]['elapsed_s']:
                    raise ValueError(f'{path}:{line}: time moved backwards')
                rows.append(row)
    if not rows:
        raise ValueError('no valid eligible sensor rows')
    return rows


def describe(values):
    return dict(count=len(values), mean=statistics.mean(values),
                stdev=statistics.stdev(values) if len(values) > 1 else None,
                min=min(values), max=max(values))


def write_json(path, data):
    Path(path).write_text(json.dumps(data, ensure_ascii=False, indent=2, allow_nan=False) + '\n', encoding='utf-8')


def plot_svg(rows, biases, path):
    # Each bin retains extrema; downsampling does not erase isolated spikes.
    svg = ['<svg xmlns="http://www.w3.org/2000/svg" width="1100" height="1020" viewBox="0 0 1100 1020">',
           '<rect width="1100" height="1020" fill="white"/>',
           '<text x="60" y="26" font-family="sans-serif" font-size="18">Raw (blue) / gyro bias compensated (orange), raw LSB units</text>']
    t0, t1 = rows[0]['elapsed_s'], rows[-1]['elapsed_s']
    for index, axis in enumerate(AXES):
        top = 50 + index * 155
        raw = [row[axis] for row in rows]
        offset = biases.get(axis, 0)
        low, high = min(min(raw), min(raw) - offset), max(max(raw), max(raw) - offset)
        span = high - low or 1
        svg.append(f'<text x="10" y="{top + 16}" font-family="sans-serif">{axis}</text>')
        svg.append(f'<text x="60" y="{top + 14}" font-family="sans-serif" font-size="11">{high:.3f} LSB</text>')
        svg.append(f'<path d="M60 {top + 20}V{top + 125}H1060" stroke="#aaa" fill="none"/>')
        series = [(0, '#2675bd')]
        if axis in biases:
            series.append((offset, '#c75c10'))
        for shift, color in series:
            points = []
            step = max(1, math.ceil(len(rows) / 1500))
            for start in range(0, len(rows), step):
                indices = range(start, min(start + step, len(rows)))
                selected = sorted({min(indices, key=lambda i: raw[i]), max(indices, key=lambda i: raw[i])})
                for i in selected:
                    x = 60 + 1000 * (rows[i]['elapsed_s'] - t0) / (t1 - t0 or 1)
                    y = top + 125 - 100 * (raw[i] - shift - low) / span
                    points.append(f'{x:.2f},{y:.2f}')
            svg.append(f'<polyline points="{" ".join(points)}" stroke="{color}" stroke-width="1" fill="none"/>')
        svg.append(f'<text x="60" y="{top + 143}" font-family="sans-serif" font-size="11">{low:.3f} LSB; time {t0:.3f} to {t1:.3f} s</text>')
    svg.append('</svg>')
    Path(path).write_text('\n'.join(svg), encoding='utf-8')


def analyze_csv(validation, outputdir, calibration=None):
    validation, outputdir = Path(validation), Path(outputdir)
    if calibration and Path(calibration).resolve() == validation.resolve():
        raise ValueError('calibration and validation must be different CSV files')
    rows = read_imu_csv(validation)
    biases, cal_report = {}, None
    if calibration:
        all_cal = read_imu_csv(calibration)
        cal = [row for row in all_cal if row['elapsed_s'] < all_cal[0]['elapsed_s'] + 60]
        biases = {axis: statistics.mean(row[axis] for row in cal) for axis in AXES[3:]}
        cal_report = dict(path=str(Path(calibration).resolve()), samples=len(cal), window_seconds=60,
                          observed_span_seconds=cal[-1]['elapsed_s'] - cal[0]['elapsed_s'],
                          first_60s_covered=cal[-1]['elapsed_s'] - cal[0]['elapsed_s'] >= 59.9,
                          offsets_lsb=biases)
    result = dict(validation=str(validation.resolve()), samples=len(rows), calibration=cal_report,
                  hardware_verdict='NOT_ASSESSED', units='raw LSB', axes={},
                  sensor_profile='MPU6050 +/-2g, +/-250 deg/s: 16384 LSB/g, 131 LSB/(deg/s)',
                  interpretation='Accelerometer raw means include gravity and are not compensated. Only gyro calibration means are subtracted; this requires a stationary calibration. Constant offsets do not reduce sample standard deviation.')
    for axis in AXES:
        raw = [row[axis] for row in rows]
        scale = 16384 if axis.startswith('a') else 131
        item = dict(raw=describe(raw), physical_unit='g' if axis.startswith('a') else 'deg/s',
                    physical_raw=describe([x / scale for x in raw]))
        if axis in biases:
            item.update(calibration_mean=biases[axis], compensated=describe([x - biases[axis] for x in raw]),
                        physical_compensated=describe([(x - biases[axis]) / scale for x in raw]),
                        validation_mean_minus_calibration_mean=statistics.mean(raw) - biases[axis])
        result['axes'][axis] = item
    outputdir.mkdir(parents=True, exist_ok=True)
    write_json(outputdir / 'analysis.json', result)
    with (outputdir / 'statistics.csv').open('w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        writer.writerow(['axis', 'series', 'count', 'mean_lsb', 'sample_stdev_lsb', 'min_lsb', 'max_lsb'])
        for axis, item in result['axes'].items():
            for series in ('raw', 'compensated'):
                if series in item:
                    s = item[series]
                    writer.writerow([axis, series, s['count'], s['mean'], s['stdev'], s['min'], s['max']])
    plot_svg(rows, biases, outputdir / 'axes.svg')
    return result


def fresh_output(outputdir, names):
    outputdir = Path(outputdir)
    outputdir.mkdir(parents=True, exist_ok=True)
    if any((outputdir / name).exists() for name in names):
        raise FileExistsError('evidence files already exist; choose a fresh output directory')
    return outputdir


def drain(client, quiet_seconds=.1, maximum_seconds=2.0):
    deadline = time.monotonic() + maximum_seconds
    quiet = time.monotonic() + quiet_seconds
    while time.monotonic() < min(deadline, quiet):
        waiting = client.port.in_waiting
        frames = client.poll()
        if waiting or frames:
            quiet = time.monotonic() + quiet_seconds
    return client.port.in_waiting == 0 and not client.decoder.buffer


def record_session(port, seconds, outputdir):
    seconds = positive_seconds(seconds)
    outputdir = fresh_output(outputdir, ('raw.bin', 'imu.csv', 'th.csv', 'status.jsonl', 'summary.json'))
    result = dict(requested_seconds=seconds, baud=460800, framing='8N1',
                  hardware_verdict='NOT_ASSESSED', session_result='INCOMPLETE',
                  start_ack_verified=False, stop_ack_verified=False, drain_complete=False,
                  imu_frames=0, imu_valid_frames=0, imu_eligible_frames=0, th_frames=0, malformed_payloads=0,
                  device_gap_flags=0, int_timestamp_frames=0, errors=[],
                  evidence_note='Acquisition completion is not a hardware acceptance verdict. Check full duration, capabilities, wiring and external timing measurements.')
    tracker = SampleTracker()
    started = time.monotonic()
    capture = False
    snapshots = []
    with ExitStack() as stack:
        raw = stack.enter_context((outputdir / 'raw.bin').open('xb'))
        imu_file = stack.enter_context((outputdir / 'imu.csv').open('x', newline='', encoding='utf-8'))
        th_file = stack.enter_context((outputdir / 'th.csv').open('x', newline='', encoding='utf-8'))
        status_file = stack.enter_context((outputdir / 'status.jsonl').open('x', encoding='utf-8'))
        imu_keys = ['host_elapsed_s', 'sequence', *decode_imu(bytes(30)).keys(),
                    'elapsed_us', 'elapsed_s', 'order', 'eligible']
        imu_writer = csv.DictWriter(imu_file, fieldnames=imu_keys)
        th_writer = csv.DictWriter(th_file, fieldnames=['host_elapsed_s', 'sequence', *decode_th(bytes(20)).keys()])
        imu_writer.writeheader()
        th_writer.writeheader()

        def receive(frame):
            try:
                if frame.command == 0x91:
                    status = decode_status(frame.payload)
                    snapshots.append(status)
                    status_file.write(json.dumps(dict(host_elapsed_s=time.monotonic() - started,
                                                      sequence=frame.sequence, status=status)) + '\n')
                    status_file.flush()
                elif capture and frame.command == 0xa0:
                    row = decode_imu(frame.payload)
                    row.update(tracker.observe(frame.sequence, row['time_us']))
                    row.update(sequence=frame.sequence, host_elapsed_s=time.monotonic() - started)
                    imu_writer.writerow(row)
                    result['imu_frames'] += 1
                    result['imu_valid_frames'] += row['valid']
                    result['imu_eligible_frames'] += row['eligible']
                    result['device_gap_flags'] += row['device_gap']
                    result['int_timestamp_frames'] += row['int_timestamp']
                elif capture and frame.command == 0xa1:
                    row = decode_th(frame.payload)
                    row.update(sequence=frame.sequence, host_elapsed_s=time.monotonic() - started)
                    th_writer.writerow(row)
                    result['th_frames'] += 1
            except (ValueError, struct.error):
                result['malformed_payloads'] += 1

        client = Client(port, receive, raw)
        acquisition_started = None
        try:
            client.command(0x10, b'\x00', 0x90, expected=b'\x00')
            drain(client)
            result['initial_status'] = client.status()
            capture = True
            client.command(0x10, b'\x01', 0x90, expected=b'\x01')
            result['start_ack_verified'] = True
            acquisition_started = time.monotonic()
            deadline, next_status = acquisition_started + seconds, acquisition_started + 5
            while time.monotonic() < deadline:
                client.poll()
                if time.monotonic() >= next_status:
                    client.status()
                    next_status = time.monotonic() + 5
                    raw.flush()
                    imu_file.flush()
                    th_file.flush()
            result['duration_completed'] = True
        except (Exception, KeyboardInterrupt) as error:
            result['errors'].append(f'{type(error).__name__}: {error}')
        finally:
            result['acquisition_seconds'] = (time.monotonic() - acquisition_started) if acquisition_started is not None else 0
            try:
                client.command(0x10, b'\x00', 0x90, expected=b'\x00')
                result['stop_ack_verified'] = True
                result['drain_complete'] = drain(client)
                result['final_status'] = client.status()
                result['drain_complete'] = drain(client) and result['drain_complete']
                if result['final_status']['stream_enabled']:
                    raise ValueError('final status still reports streaming enabled')
            except (Exception, KeyboardInterrupt) as error:
                result['errors'].append(f'stop/final status: {type(error).__name__}: {error}')
            result['decoder'] = dict(client.decoder.counts, trailing_bytes=len(client.decoder.buffer))
            result['sample_counters'] = tracker.counts
            result['device_span_seconds'] = tracker.elapsed / 1e6
            result['status_snapshots'] = len(snapshots)
            result['missing_capabilities'] = [name for name, value in result.get('final_status', {}).get('capability_flags', {}).items() if not value]
            result['observed_rate_hz'] = ((result['imu_eligible_frames'] - 1) / (tracker.elapsed / 1e6)) if tracker.elapsed else None
            if (result.get('duration_completed') and result['start_ack_verified'] and result['stop_ack_verified']
                    and result['drain_complete'] and not result['errors'] and result['imu_valid_frames'] > 0
                    and result['status_snapshots'] >= 2):
                result['session_result'] = 'COMPLETE'
            write_json(outputdir / 'summary.json', result)
    return result


def flash_session(port, outputdir, timeout=1300.0):
    timeout = positive_seconds(timeout)
    outputdir = fresh_output(outputdir, ('raw.bin', 'status.jsonl', 'summary.json'))
    result = dict(session_result='INCOMPLETE', hardware_verdict='NOT_ASSESSED',
                  accepted_result=None, timeout_seconds=timeout, errors=[],
                  evidence_note='Only the configured reserved sector is authorized. Device status is recorded as reported; no expected flash result is invented.')
    started = time.monotonic()
    with (outputdir / 'raw.bin').open('xb') as raw, (outputdir / 'status.jsonl').open('x', encoding='utf-8') as log:
        def receive(frame):
            if frame.command == 0x91:
                status = decode_status(frame.payload)
                log.write(json.dumps(dict(host_elapsed_s=time.monotonic() - started, status=status)) + '\n')
                log.flush()
        client = Client(port, receive, raw)
        try:
            result['initial_status'] = client.status()
            payload = client.command(0x12, b'W3OK', 0x92)
            result['accepted_result'] = unpack_exact('<i', payload)[0]
            if result['accepted_result'] != 0:
                result['session_result'] = 'REJECTED'
            else:
                deadline = time.monotonic() + timeout
                while time.monotonic() < deadline:
                    status = client.status()
                    result['final_status'] = status
                    if not status['flash_active']:
                        result['session_result'] = 'COMPLETED_OBSERVED'
                        break
                    until = min(deadline, time.monotonic() + 1)
                    while time.monotonic() < until:
                        client.poll()
                else:
                    result['session_result'] = 'TIMEOUT'
        except (Exception, KeyboardInterrupt) as error:
            result['errors'].append(f'{type(error).__name__}: {error}')
        finally:
            result['elapsed_seconds'] = time.monotonic() - started
            result['decoder'] = dict(client.decoder.counts, trailing_bytes=len(client.decoder.buffer))
            write_json(outputdir / 'summary.json', result)
    return result


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    record = sub.add_parser('record', help='record IMU/TH and status (never starts flash test)')
    record.add_argument('--port', required=True)
    record.add_argument('--seconds', type=positive_seconds, default=600.0)
    record.add_argument('--outputdir', type=Path, required=True)
    status = sub.add_parser('status', help='read current device status')
    status.add_argument('--port', required=True)
    flash = sub.add_parser('flash-test', help='erase/write configured reserved flash test sector')
    flash.add_argument('--port', required=True)
    flash.add_argument('--flash-test', action='store_true', required=True,
                       help='explicitly authorize erasing/writing configured reserved sector')
    flash.add_argument('--outputdir', type=Path, required=True)
    analyze = sub.add_parser('analyze', help='six-axis statistics and standalone SVG')
    analyze.add_argument('--validation', type=Path, required=True)
    analyze.add_argument('--calibration', type=Path)
    analyze.add_argument('--outputdir', type=Path, required=True)
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    try:
        if args.action == 'analyze':
            result = analyze_csv(args.validation, args.outputdir, args.calibration)
        else:
            port = open_port(args.port, 460800)
            try:
                if args.action == 'status':
                    result = Client(port).status()
                elif args.action == 'record':
                    result = record_session(port, args.seconds, args.outputdir)
                else:
                    result = flash_session(port, args.outputdir)
            finally:
                port.close()
        print(json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False))
        return 0 if result.get('session_result', 'COMPLETE') in ('COMPLETE', 'COMPLETED_OBSERVED') else 1
    except (OSError, ValueError, TimeoutError, RuntimeError) as error:
        print(f'{type(error).__name__}: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
