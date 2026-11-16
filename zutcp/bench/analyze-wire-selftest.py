#!/usr/bin/env python3
# (c) Copyright 2026 Huw Rogers
# This code is licensed by the MIT license (see LICENSE for details).

import copy
import importlib.util
import sys
from pathlib import Path
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location(
    'analyzer', Path(__file__).resolve().with_name('analyze-wire.py'))
a = importlib.util.module_from_spec(spec)
spec.loader.exec_module(a)


def message(workload, response, sequence):
    values = a.expected(workload, response, sequence)
    body = ('35=' + values.pop('35') + '\x01' +
            ''.join(k + '=' + v + '\x01' for k, v in values.items())).encode()
    raw = b'8=FIX.4.4\x019=' + str(len(body)).encode() + b'\x01' + body
    return raw + b'10=' + f'{sum(raw) % 256:03d}'.encode() + b'\x01'


def packets(workload, port=40000, shift=0.0):
    request = b''.join(message(workload, False, n) for n in (1, 2, 3))
    response = b''.join(message(workload, True, n) for n in (1, 2, 3))
    cb, sb = 0xffffff80, 0xfffffe00
    result = []
    def packet(reverse, seq, ack, flags, payload=b'', time=0):
        result.append(dict(family=4, source='198.18.87.2' if reverse else '198.18.87.1',
                           target='198.18.87.1' if reverse else '198.18.87.2',
                           sport=a.PORT if reverse else port,
                           dport=port if reverse else a.PORT,
                           seq=seq & a.MASK, ack=ack & a.MASK, flags=flags,
                           payload=payload, time=time + shift))
    packet(False, cb - 1, 0, 2, time=1)
    packet(True, sb - 1, cb, 18, time=2)
    packet(False, cb, sb, 16, time=3)
    # Out-of-order original, then missing prefix and byte-identical retransmission.
    packet(False, cb + 100, sb, 24, request[100:], time=4)
    packet(False, cb, sb, 24, request[:100], time=5)
    packet(False, cb, sb, 24, request[:100], time=6)
    packet(True, sb, cb + len(request), 25, response, time=7)
    packet(False, cb + len(request), sb + len(response) + 1, 17, time=8)
    packet(True, sb + len(response) + 1, cb + len(request) + 1, 16, time=9)
    return result


def rejects(fn):
    try:
        fn()
    except AssertionError:
        return
    raise AssertionError('negative unexpectedly accepted')


positive = negative = 0
for workload in ('order', 'marketdata', 'updates'):
    for response in (False, True):
        raw = b''.join(message(workload, response, n) for n in (1, 2, 3))
        a.fix_stream(raw, workload, response, 3)
        positive += 1
        for bad in (raw[:-1], raw + b'x', raw.replace(b'9=', b'9=9', 1),
                    raw.replace(b'10=', b'10=0', 1)):
            rejects(lambda: a.fix_stream(bad, workload, response, 3))
            negative += 1
        rejects(lambda: a.fix_stream(raw, workload, not response, 3))
        rejects(lambda: a.fix_stream(raw, workload, response, 2))
        negative += 2
    settings = dict(messages='2', warmup='1', connections='1', workload=workload, burst_size='1')
    original = packets(workload)
    result = a.analyze_packets(original, settings)
    assert result['flows'][0]['request']['records'] == 3
    positive += 1
    for kind in ('gap', 'conflict', 'no_final_ack', 'wrong_synack', 'bad_fin'):
        bad = copy.deepcopy(original)
        if kind == 'gap':
            bad = [p for p in bad if p['time'] not in (5, 6)]
        elif kind == 'conflict':
            bad[5]['payload'] = b'x' + bad[5]['payload'][1:]
        elif kind == 'no_final_ack':
            bad.pop()
        elif kind == 'wrong_synack':
            bad[1]['ack'] += 1
        else:
            bad[-2]['seq'] += 1
        rejects(lambda: a.analyze_packets(bad, settings))
        negative += 1
    combined = original + packets(workload, 40001, 0.5)
    settings['connections'] = '2'
    assert a.analyze_packets(combined, settings)['max_overlapping_data_flows'] == 2
    positive += 1
print(f'PASS {positive} positives, {negative} negatives')

# Exercise run metadata and teardown checks without opening any live capture.
import tempfile
from pathlib import Path
from types import SimpleNamespace
with tempfile.TemporaryDirectory(prefix='zutcp-bench-analyzer-selftest-') as temporary:
    directory = Path(temporary)
    settings = dict(backend='native-client', family='4', capture='1',
                    capture_endpoint='server', client_backend='native', server_backend='kernel',
                    messages='2', warmup='1', pipeline='1', connections='1', workload='order',
                    burst_size='1', burst_interval_us='1000')
    (directory / 'settings').write_text(''.join(k + '=' + v + '\n' for k, v in settings.items()))
    (directory / 'complete').touch()
    (directory / 'capture.log').write_text('12 packets captured\n0 packets dropped by kernel\n')
    request = sum(len(message('order', False, n)) for n in (1, 2, 3))
    response = sum(len(message('order', True, n)) for n in (1, 2, 3))
    for role, native, size in (('client', True, response), ('server', False, request)):
        (directory / role).mkdir()
        (directory / role / 'output').write_text(
            f'connection id=0 plugin_actual={int(native)} kernel_rcvbuf_actual=4096 '
            'kernel_sndbuf_actual=4096 pipeline_peak=1 application_bursts=3 '
            'late_bursts=0 received=3 accepted=3\n'
            f'result role={role} backend={"native" if native else "kernel"} exact=1 '
            'connections=1 pipeline=1 warmup=1 measured_per_connection=2 workload=order '
            f'burst_size=1 burst_us=1000 received_fix_bytes={size}\n' +
            ('native_stopped ready=0 cxns=0 pending=0 tx_outstanding=0 '
             'control_free=64 control_count=64\nnative_rx retained=0\n' if native else '') +
            (f'application_bytes request_frame={request // 3} response_frame={response // 3} '
             f'measured_request={request // 3 * 2} measured_response={response // 3 * 2}\n'
             if role == 'client' else ''))
    def capture(_):
        return [(p['time'], p) for p in packets('order')]
    helper = SimpleNamespace(capture=capture, tcp=lambda p: dict(p))
    assert a.analyze(directory, helper)['status'] == 'pass'
    (directory / 'capture.log').write_text('listening on zbenchs\n')
    rejects(lambda: a.analyze(directory, helper))
    (directory / 'capture.log').write_text('1 packets dropped by kernel\n')
    rejects(lambda: a.analyze(directory, helper))
    (directory / 'capture.log').write_text('0 packets dropped by kernel\n')
    path = directory / 'client' / 'output'
    valid = path.read_text()
    for bad in (valid.replace('tx_outstanding=0', 'tx_outstanding=1'),
                valid.replace('plugin_actual=1', 'plugin_actual=0'),
                valid.replace('received_fix_bytes=', 'received_fix_bytes=1'),
                valid.replace('application_bursts=3', 'application_bursts=2')):
        path.write_text(bad)
        rejects(lambda: a.analyze(directory, helper))
    path.write_text(valid)
    settings['capture_topology'] = 'bridge'
    (directory / 'settings').write_text(''.join(k + '=' + v + '\n' for k, v in settings.items()))
    for side in ('client', 'server'):
        (directory / ('capture-' + side + '.log')).write_text('0 packets dropped by kernel\n')
    def bridge_capture(path):
        reverse = path.name == 'to-client.pcap'
        return [(p['time'], p) for p in packets('order') if (p['sport'] == a.PORT) == reverse]
    helper.capture = bridge_capture
    assert a.analyze(directory, helper)['capture_topology'] == 'bridge'
    helper.capture = capture
    rejects(lambda: a.analyze(directory, helper))
    reset = dict(packets('order')[-1], flags=4, time=10)
    def with_reset(path):
        source = bridge_capture(path)
        if path.name == 'to-client.pcap':
            source.append((reset['time'], reset))
        return source
    class FakeFrame(dict):
        def hex(self):
            return 'selftest'
    original_with_reset = with_reset
    def with_reset(path):
        return [(time, FakeFrame(packet)) for time, packet in original_with_reset(path)]
    def decode(packet, allow_reset=False):
        a.check(allow_reset or not packet['flags'] & 4, 'strict default reset rejection')
        return dict(packet)
    helper.capture, helper.tcp = with_reset, decode
    rejects(lambda: a.analyze(directory, helper))
    rejects(lambda: a.analyze(directory, helper, lambda *args: []))
    def validator(path, all_packets, configured, wire):
        assert path == directory and configured == settings and len(wire['flows']) == 1
        assert len([p for p in all_packets if p['flags'] & 4]) == 1
        # This checks hook accounting only, not reset validity; recovery owns that proof.
        return [{'selftest_only': True}]
    assert a.analyze(directory, helper, validator)['wire']['qualified_resets'] == [
        {'selftest_only': True}]
    def refusal(*args):
        raise AssertionError('unqualified reset')
    rejects(lambda: a.analyze(directory, helper, refusal))
print('PASS run orchestration/hook: 3 positives, 10 negatives')

raw = b'abcdefghijklmnopqrstuvwxyz1234'
islands = [dict(seq=110, flags=16, payload=raw[10:20], time=1),
           dict(seq=100, flags=17, payload=raw, time=2)]
assembled, arrivals, _ = a.stream(islands, 100, 100)
assert assembled == raw and arrivals == [(10, 20, 1), (0, 10, 2), (20, 30, 2)]
print('PASS disjoint first-arrival byte runs')
