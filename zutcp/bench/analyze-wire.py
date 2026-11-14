#!/usr/bin/env python3
# (c) Copyright 2026 Huw Rogers
# This code is licensed by the MIT license (see LICENSE for details).

"""Offline functional FIX/TCP evidence; never a throughput or physical-NIC claim."""
import argparse
import hashlib
import importlib.util
import json
import re
import sys
from pathlib import Path

sys.dont_write_bytecode = True
MASK = (1 << 32) - 1
PORT = 18187
STAMP = '20260101-00:00:00.000'


def check(value, why):
    if not value:
        raise AssertionError(why)


def fields(line):
    return dict(re.findall(r'(\w+)=([^\s]+)', line))


def expected(workload, response, sequence):
    seq = f'{sequence:010d}'
    result = {'49': 'PEER' if response else 'BENCH',
              '56': 'BENCH' if response else 'PEER', '34': seq, '52': STAMP}
    if workload == 'order':
        result.update({'35': '8' if response else 'D', '11': seq, '55': 'TEST',
                       '54': '1', '38': '100', '44': '123.45'})
        result.update({'17': seq, '37': seq, '150': '0', '39': '0', '151': '100',
                       '14': '0', '6': '0'} if response else {'60': STAMP, '40': '2'})
    elif workload == 'marketdata':
        result.update({'35': 'W' if response else 'V', '262': seq, '55': 'TEST'})
        result.update({'268': '1', '269': '0', '270': '123.45', '271': '100'}
                      if response else {'263': '0', '264': '1', '267': '1',
                                        '269': '0', '146': '1'})
    else:
        check(workload == 'updates', 'unknown workload')
        result.update({'35': 'X', '262': seq, '268': '1', '279': '0', '269': '0',
                       '278': seq, '55': 'TEST', '270': '123.45', '271': '100'})
    return result


def fix_stream(raw, workload, response, count):
    """Parse standard FIX lengths/checksums independently of the application codec."""
    records = []
    offset = 0
    for sequence in range(1, count + 1):
        match = re.match(rb'8=FIX\.4\.4\x019=([0-9]+)\x01', raw[offset:])
        check(match is not None, f'FIX header at sequence {sequence}')
        body_start = offset + match.end()
        body_end = body_start + int(match[1])
        end = body_end + 7
        check(end <= len(raw) and end - offset <= 256, 'FIX frame bounds')
        trailer = raw[body_end:end]
        check(re.fullmatch(rb'10=[0-9]{3}\x01', trailer) is not None, 'FIX trailer')
        check(sum(raw[offset:body_end]) % 256 == int(trailer[3:6]), 'FIX checksum')
        body = raw[body_start:body_end]
        check(body.endswith(b'\x01'), 'FIX BodyLength ends inside field')
        pairs = [part.split(b'=', 1) for part in body[:-1].split(b'\x01')]
        check(all(len(pair) == 2 and pair[0].isdigit() for pair in pairs), 'FIX fields')
        check(pairs[0][0] == b'35', 'MsgType must start body')
        decoded = {key.decode('ascii'): value.decode('ascii') for key, value in pairs}
        check(len(decoded) == len(pairs), 'unexpected duplicate FIX tag')
        check(decoded == expected(workload, response, sequence),
              f'FIX corpus/sequence mismatch {sequence}')
        records.append((offset, end))
        offset = end
    check(offset == len(raw), 'extra FIX bytes or records')
    return records


def stream(packets, base, bound):
    fins = [((p['seq'] - base) & MASK) + len(p['payload'])
            for p in packets if p['flags'] & 1]
    check(fins and len(set(fins)) == 1, 'missing or conflicting FIN frontier')
    end = fins[0]
    check(0 < end <= bound, 'stream size outside configured FIX bound')
    raw, seen = bytearray(end), bytearray(end)
    arrivals = []
    for p in packets:
        if p['flags'] & 2:
            check(not p['payload'], 'unexpected SYN application bytes')
            continue
        if not p['payload']:
            continue
        first = (p['seq'] - base) & MASK
        last = first + len(p['payload'])
        check(last <= end, 'payload beyond FIN')
        new = []
        for i, value in enumerate(p['payload'], first):
            check(not seen[i] or raw[i] == value, 'conflicting TCP retransmission')
            if not seen[i]:
                new.append(i)
                raw[i], seen[i] = value, 1
        if new:
            start = previous = new[0]
            for current in new[1:]:
                if current != previous + 1:
                    arrivals.append((start, previous + 1, p['time']))
                    start = current
                previous = current
            arrivals.append((start, previous + 1, p['time']))
    check(all(seen), 'TCP byte gap')
    return bytes(raw), arrivals, end


def record_times(records, arrivals):
    result, previous = [], 0.0
    for first, last in records:
        times = [time for left, right, time in arrivals if left < last and right > first]
        check(times, 'record without source bytes')
        previous = max(previous, max(times))
        result.append((min(times), previous))
    return result


def analyze_packets(packets, settings):
    total = int(settings['messages']) + int(settings['warmup'])
    syns = {}
    for p in packets:
        if p['dport'] == PORT and p['flags'] & 2 and not p['flags'] & 16:
            key = (p['source'], p['sport'], p['target'], p['dport'])
            check(key not in syns or syns[key]['seq'] == p['seq'], 'tuple incarnation ambiguity')
            syns.setdefault(key, p)
    check(len(syns) == int(settings['connections']), 'connection count differs from SYN flows')
    known = set(syns) | {(k[2], k[3], k[0], k[1]) for k in syns}
    check(all((p['source'], p['sport'], p['target'], p['dport']) in known for p in packets),
          'packet without captured initiating SYN')
    reports, intervals, totals = [], [], [0, 0]
    for key, syn in syns.items():
        direct = [p for p in packets if
                  (p['source'], p['sport'], p['target'], p['dport']) == key]
        reverse_key = (key[2], key[3], key[0], key[1])
        reverse = [p for p in packets if
                   (p['source'], p['sport'], p['target'], p['dport']) == reverse_key]
        replies = [p for p in reverse if p['flags'] & 2]
        check(replies and all(p['time'] >= syn['time'] and p['flags'] & 16
                             and p['ack'] == (syn['seq'] + 1) & MASK
                             and p['seq'] == replies[0]['seq'] for p in replies),
              'SYNACK origin/ACK mismatch')
        bases = ((syn['seq'] + 1) & MASK, (replies[0]['seq'] + 1) & MASK)
        flow, times = {'tuple': key, 'bases': bases}, []
        last_acks = []
        for direction, own in enumerate((direct, reverse)):
            raw, arrivals, end = stream(own, bases[direction], total * 256)
            records = fix_stream(raw, settings['workload'], bool(direction), total)
            delivery = record_times(records, arrivals)
            peer = reverse if not direction else direct
            terminal = (bases[direction] + end + 1) & MASK
            acknowledgments = [p for p in peer if p['flags'] & 16 and p['ack'] == terminal
                               and p['time'] >= min(q['time'] for q in own if q['flags'] & 1)]
            check(acknowledgments, 'missing final cumulative FIN ACK')
            for p in peer:
                if p['flags'] & 16:
                    check(((p['ack'] - bases[direction]) & MASK) <= end + 1,
                          'ACK beyond byte/FIN frontier')
            last_acks.append(min(p['time'] for p in acknowledgments))
            totals[direction] += len(raw)
            flow['response' if direction else 'request'] = {
                'bytes': len(raw), 'records': len(records),
                'frame_lengths': sorted({b - a for a, b in records}),
                'sha256': hashlib.sha256(raw).hexdigest(), 'fin_position': end}
            times.append(delivery)
        intervals.append((times[0][0][0], times[1][-1][1]))
        burst = int(settings['burst_size'])
        gaps = []
        if burst:
            warm = int(settings['warmup'])
            boundaries = list(range(burst, warm, burst)) + list(range(warm + burst, total, burst))
            gaps = [times[0][i][0] - times[0][i - 1][1] for i in boundaries]
        flow['request_burst_boundary_gaps_seconds'] = gaps
        flow['last_fin_ack_time'] = max(last_acks)
        reports.append(flow)
    active = peak = 0
    for _, delta in sorted([(a, 1) for a, _ in intervals] + [(b, -1) for _, b in intervals]):
        active += delta
        peak = max(peak, active)
    return {'flows': reports, 'request_bytes': totals[0], 'response_bytes': totals[1],
            'max_overlapping_data_flows': peak, 'concurrent_data_observed': peak >= 2}


def one_line(lines, prefix):
    matches = [fields(line) for line in lines if line.startswith(prefix)]
    check(len(matches) == 1, f'expected one {prefix} line')
    return matches[0]


def analyze(directory, helper, reset_validator=None):
    settings = dict(line.split('=', 1) for line in
                    (directory / 'settings').read_text().splitlines())
    check((directory / 'complete').is_file(), 'run not complete')
    check(settings['capture'] == '1', 'capture not enabled')
    report = {'directory': str(directory), 'settings': settings,
              'claim': 'functional TCP/FIX evidence only; no performance or physical-NIC claim'}
    outputs = {}
    for role in ('client', 'server'):
        lines = (directory / role / 'output').read_text().splitlines()
        result = one_line(lines, 'result ')
        check(result['role'] == role and result['backend'] == settings[role + '_backend']
              and result['exact'] == '1', 'application result/backend mismatch')
        for field, setting in [('connections', 'connections'), ('pipeline', 'pipeline'),
                               ('warmup', 'warmup'), ('measured_per_connection', 'messages'),
                               ('workload', 'workload'), ('burst_size', 'burst_size'),
                               ('burst_us', 'burst_interval_us')]:
            check(result[field] == settings[setting], 'result/settings mismatch: ' + field)
        connections = [fields(line) for line in lines if line.startswith('connection ')]
        check(sorted(int(c['id']) for c in connections) ==
              list(range(int(settings['connections']))),
              'application connection IDs/count')
        for c in connections:
            check(int(c['plugin_actual']) == (result['backend'] == 'native'), 'actual backend')
            check(int(c['received']) == int(c['accepted']) ==
                  int(settings['messages']) + int(settings['warmup']), 'application counts')
            if role == 'client':
                check(0 < int(c['pipeline_peak']) <= int(settings['pipeline']),
                      'bounded pipeline')
                burst = int(settings['burst_size'])
                wanted = sum((int(settings[k]) + burst - 1) // burst
                             for k in ('warmup', 'messages')) if burst else 0
                check(int(c['application_bursts']) == wanted, 'application burst count')
            if result['backend'] == 'kernel':
                check(int(c['kernel_rcvbuf_actual']) > 0 and int(c['kernel_sndbuf_actual']) > 0,
                      'kernel socket readback')
        if result['backend'] == 'native':
            stopped = one_line(lines, 'native_stopped ')
            check(all(int(stopped[k]) == 0 for k in
                      ('ready', 'cxns', 'pending', 'tx_outstanding')) and
                  stopped['control_free'] == stopped['control_count'], 'native owner/pool drain')
            check(one_line(lines, 'native_rx ')['retained'] == '0', 'native Rx leases remain')
        outputs[role] = {'result': result, 'connections': connections}
    report['application'] = outputs
    bridge = settings.get('capture_topology') == 'bridge'
    logs = ['capture-client.log', 'capture-server.log'] if bridge else ['capture.log']
    for log in logs:
        footer = (directory / log).read_text()
        drops = re.findall(r'(\d+) packets dropped by kernel', footer)
        check(drops == ['0'], 'capture not closed or kernel capture drops: ' + log)
        interface_drops = re.findall(r'(\d+) packets dropped by interface', footer)
        check(all(int(value) == 0 for value in interface_drops), 'interface capture drops')
    if settings['client_backend'] == settings['server_backend'] == 'native' and not bridge:
        report.update(status='excluded_native_native_capture', independent_kernel=False,
                      reason='native/native endpoint capture may omit AF_XDP packets')
        return report
    if not bridge:
        side = settings['capture_endpoint']
        check(settings[side + '_backend'] == 'kernel',
              'complete capture must be at kernel endpoint')
    captures = [('to-client.pcap', 'client'), ('to-server.pcap', 'server')] if bridge else [
        ('diagnostic.pcap', None)]
    packets = []
    for name, side in captures:
        for time, frame in helper.capture(directory / name):
            packet = (helper.tcp(frame, allow_reset=True) if reset_validator is not None
                      else helper.tcp(frame))
            if packet is None:
                continue
            check(PORT in (packet['sport'], packet['dport']), 'unexpected capture TCP port')
            if side:
                check(packet['sport' if side == 'client' else 'dport'] == PORT,
                      'middle-bridge capture includes wrong egress direction')
            check(packet['family'] == int(settings['family']), 'capture family mismatch')
            packet['time'] = time
            if reset_validator is not None:
                packet['frame'] = frame.hex()
            packets.append(packet)
    packets.sort(key=lambda packet: packet['time'])
    # Complete ordinary TCP/FIX proof precedes any optional post-close exception.
    ordinary = [packet for packet in packets if not packet['flags'] & 4]
    wire = analyze_packets(ordinary, settings)
    if reset_validator is not None:
        evidence = reset_validator(directory, packets, settings, wire)
        check(isinstance(evidence, list) and len(evidence) ==
              sum(bool(packet['flags'] & 4) for packet in packets),
              'reset validator must account for every captured RST')
        wire['qualified_resets'] = evidence
    check(int(outputs['client']['result']['received_fix_bytes']) == wire['response_bytes'] and
          int(outputs['server']['result']['received_fix_bytes']) == wire['request_bytes'],
          'wire/application byte totals')
    lines = (directory / 'client' / 'output').read_text().splitlines()
    measured = one_line(lines, 'application_bytes ')
    for name in ('request', 'response'):
        sizes = {size for flow in wire['flows'] for size in flow[name]['frame_lengths']}
        check(sizes == {int(measured[name + '_frame'])}, 'application frame size readback')
        check(int(measured['measured_' + name]) == int(settings['connections']) *
              int(settings['messages']) * int(measured[name + '_frame']),
              'measured application byte count')
    report.update(status='pass', independent_kernel='kernel' in
                  (settings['client_backend'], settings['server_backend']),
                  capture_topology='bridge' if bridge else 'endpoint',
                  mixed_backend=settings['client_backend'] != settings['server_backend'],
                  wire=wire)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directories', nargs='+', type=Path)
    parser.add_argument('--tcp-helper', type=Path,
                        default=Path(__file__).resolve().parents[1] / 'interop' / 'zutcptcp.py')
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('tcp_evidence', args.tcp_helper)
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    reports, errors = [], []
    for directory in args.directories:
        try:
            reports.append(analyze(directory, helper))
        except (AssertionError, OSError, KeyError, ValueError) as error:
            errors.append({'directory': str(directory), 'error': str(error)})
    print(json.dumps({'runs': reports, 'errors': errors}, indent=2))
    return bool(errors)


if __name__ == '__main__':
    sys.exit(main())
