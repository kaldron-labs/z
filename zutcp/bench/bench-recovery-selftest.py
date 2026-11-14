#!/usr/bin/env python3
# (c) Copyright 2026 Huw Rogers
# This code is licensed by the MIT license (see LICENSE for details).
"""Pure fault-evidence vectors; complete FIX framing is tested by analyze-wire-selftest."""
import copy
import importlib.util
import json
import sys
import tempfile
from pathlib import Path
from types import SimpleNamespace
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location(
    'fault', Path(__file__).resolve().with_name('bench-recovery.py'))
f = importlib.util.module_from_spec(spec)
spec.loader.exec_module(f)
positive = negative = 0


def rejects(fn):
    global negative
    try:
        fn()
    except (AssertionError, StopIteration):
        negative += 1
        return
    raise AssertionError('negative evidence unexpectedly accepted')


def frame(number, length=60):
    raw = bytearray(length)
    raw[16:18] = (length - 14).to_bytes(2, 'big')
    raw[-1] = number
    return raw.hex()


def packet(time, number, side, seq, ack, flags, payload=b''):
    reverse = side == 'client'
    return dict(time=time, frame=frame(number, 200 if payload else 60), side=side,
                family=4, source='s' if reverse else 'c', target='c' if reverse else 's',
                sport=18187 if reverse else 40000, dport=40000 if reverse else 18187,
                seq=seq, ack=ack, flags=flags, payload=payload, window=4000, options=[])


class Frame(dict):
    def hex(self):
        return self['frame']


def same_flow(p, q, reverse=False):
    keys = ('source', 'target', 'sport', 'dport')
    expected = ('target', 'source', 'dport', 'sport') if reverse else keys
    return all(p[k] == q[v] for k, v in zip(keys, expected))


with tempfile.TemporaryDirectory(prefix='zutcp-fix-fault-selftest-') as temporary:
    root = Path(temporary)
    for side in ('client', 'server'):
        (root / side).mkdir()
        for phase in ('before', 'after'):
            (root / side / ('netstat.' + phase)).write_text('TcpOutRsts 0\n')
    for log in ('fault-source.log', 'fault-control-source.log'):
        (root / log).write_text('2 packets captured\n0 packets dropped by kernel\n')
    (root / 'counter-snapshot.before').write_text('0')
    (root / 'counter-snapshot.after').write_text('100')
    syn = packet(1, 1, 'server', 99, 0, 2)
    reply = packet(2, 2, 'client', 199, 100, 18)
    original = packet(4, 3, 'server', 100, 200, 24, b'a' * 146)
    higher = packet(5, 4, 'server', 246, 200, 24, b'b' * 146)
    retry = packet(7, 5, 'server', 100, 200, 24, b'a' * 146)
    wire = dict(flows=[dict(bases=[100, 200], last_fin_ack_time=9)])
    data = {}
    helper = SimpleNamespace(
        capture=lambda p: [(x['time'], Frame(x)) for x in data[p.name]],
        tcp=lambda p, allow_reset=False: dict(p), same_flow=same_flow)

    def analyze(directory, helper, reset_validator):
        packets = f.read_packets(directory, helper)
        evidence = reset_validator(directory, packets, {'recovery': mode}, wire)
        assert len(evidence) == sum(bool(p['flags'] & 4) for p in packets)
        return dict(status='pass', **copy.deepcopy(wire))

    analyzer = SimpleNamespace(analyze=analyze)
    for mode in ('loss', 'reorder'):
        (root / 'settings').write_text(
            f'recovery={mode}\ntopology=bridge\nconnections=1\nwarmup=0\n')
        delivered = copy.deepcopy(retry if mode == 'loss' else original)
        delivered['time'] = 8
        data = {'to-server.pcap': [syn, higher, delivered], 'to-client.pcap': [reply],
                'fault-source.pcap': [syn, original, higher, retry],
                'fault-control-source.pcap': [reply]}
        held = bytes.fromhex(reply['frame'])
        (root / 'held-synack.packet').write_bytes(held)
        f.save(root / 'fault-control.json', dict(
            held_synack_sha256=f.hashlib.sha256(held).hexdigest(), forwarded_synack_time=1.9))
        f.save(root / 'fault-observer.json', dict(overflow=0, receive_actual=8 << 20))
        (root / 'fault-control-open-time').write_text('1.8')
        source_reply = copy.deepcopy(reply)
        source_reply['time'] = 1.5
        data['fault-control-source.pcap'] = [source_reply]
        (root / 'fault-control-qdisc').write_text('dropped 1')
        (root / 'fault-filter-release-time').write_text('6')
        stats = 'dropped 1 backlog 0b 0p' if mode == 'loss' else \
            'delay 20ms dropped 0 backlog 0b 0p'
        (root / 'fault-data-qdisc').write_text(stats)
        f.check_fault(root, helper, analyzer)
        positive += 1
        for bad in ('capture-drop', 'observer-drop', 'wrong-held', 'gate-escaped',
                    'no-control-drop', 'backlog', 'no-fault', 'missing-source'):
            saved = {p.name: p.read_bytes() for p in root.iterdir() if p.is_file()}
            captured = copy.deepcopy(data)
            if bad == 'capture-drop':
                (root / 'fault-source.log').write_text('1 packets dropped by kernel\n')
            elif bad == 'observer-drop':
                f.save(root / 'fault-observer.json', dict(overflow=1, receive_actual=8 << 20))
            elif bad == 'wrong-held':
                (root / 'held-synack.packet').write_bytes(b'wrong')
            elif bad == 'gate-escaped':
                data['to-client.pcap'][0]['time'] = 1.7
            elif bad == 'no-control-drop':
                (root / 'fault-control-qdisc').write_text('dropped 0')
            elif bad == 'backlog':
                (root / 'fault-data-qdisc').write_text(stats.replace('0p', '1p'))
            elif bad == 'no-fault':
                (root / 'fault-data-qdisc').write_text('dropped 0 backlog 0b 0p')
            else:
                data['fault-source.pcap'] = [syn]
            rejects(lambda: f.check_fault(root, helper, analyzer))
            data = captured
            for name, contents in saved.items():
                (root / name).write_bytes(contents)

    # Whole-flow CLOSED-reset qualification is independently opt-in and counter-owned.
    reset = packet(12, 8, 'client', 200, 0, 4)
    reset.update(frame=frame(8, 54), window=0)
    late = copy.deepcopy(original)
    late.update(time=10, ack=200)
    emitted = copy.deepcopy(reset)
    emitted['time'] = 11
    data['fault-control-source.pcap'] = [source_reply, emitted]
    (root / 'server/netstat.after').write_text('TcpOutRsts 1\n')
    packets = copy.deepcopy([syn, reply, late, reset])
    for p in packets:
        p.pop('side')  # installed analyzer hook carries TCP fields/time/frame only
    settings = dict(recovery='reorder')
    assert len(f.closed_resets(root, packets, settings, wire, helper)) == 1
    positive += 1
    for bad in ('preclose', 'wrong-seq', 'options', 'native-padding', 'wrong-count',
                'reflection', 'source-after-close', 'missing-original'):
        candidate = copy.deepcopy(packets)
        saved = copy.deepcopy(data)
        if bad == 'preclose':
            candidate[-1]['time'] = 8
        elif bad == 'wrong-seq':
            candidate[-1]['seq'] += 1
        elif bad == 'options':
            candidate[-1]['options'] = [1]
        elif bad == 'native-padding':
            candidate[-1]['frame'] = frame(8, 60)
        elif bad == 'wrong-count':
            (root / 'server/netstat.after').write_text('TcpOutRsts 0\n')
        elif bad == 'reflection':
            data['fault-source.pcap'].append(packet(13, 9, 'server', 100, 0, 16))
        elif bad == 'source-after-close':
            data['fault-source.pcap'][1]['time'] = 9.5
        else:
            data['fault-source.pcap'] = [syn]
        rejects(lambda: f.closed_resets(root, candidate, settings, wire, helper))
        data = saved
        (root / 'server/netstat.after').write_text('TcpOutRsts 1\n')
print(f'PASS {positive} positive / {negative} negative fault-evidence vectors; no sockets opened')
