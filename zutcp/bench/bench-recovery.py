#!/usr/bin/env python3
# (c) Copyright 2026 Huw Rogers
# This code is licensed by the MIT license (see LICENSE for details).
"""Opt-in FIX fixture faults: observe real endpoints, forward one unchanged SYNACK."""
import argparse
import hashlib
import importlib.util
import json
import socket
import struct
import sys
import time
from pathlib import Path

sys.dont_write_bytecode = True
PORT, MASK = 18187, (1 << 32) - 1


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def save(path, value):
    temporary = path.with_name(path.name + '.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(path)


def monitor(directory, mode, helper):
    import os
    require(os.readlink('/proc/self/ns/net') != os.readlink('/proc/1/ns/net'),
            'fault observer requires a private namespace')
    rx = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(3))
    reserve = 4 << 20
    rx.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, reserve)
    actual = rx.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF)
    if actual < reserve:
        rx.setsockopt(socket.SOL_SOCKET, getattr(socket, 'SO_RCVBUFFORCE', 33), reserve)
        actual = rx.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF)
    require(actual >= reserve, 'fault observer reserve unavailable')
    overflow = getattr(socket, 'SO_RXQ_OVFL', 40)
    rx.setsockopt(socket.SOL_SOCKET, overflow, 1)
    tx = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(3))
    tx.bind(('zbenchca', 0))
    evidence = {'mode': mode, 'receive_requested': reserve, 'receive_actual': actual,
                'overflow': 0, 'frames': 0}
    syn = held = original = higher = None
    (directory / 'fault-monitor-ready').write_text('ready\n')
    deadline = time.monotonic() + 30  # bounded fixture lifetime, not a recovery budget
    try:
        while time.monotonic() < deadline:
            rx.settimeout(deadline - time.monotonic())
            raw, ancillary, flags, address = rx.recvmsg(65535, socket.CMSG_SPACE(4))
            require(not flags & (socket.MSG_TRUNC | socket.MSG_CTRUNC), 'observer truncation')
            evidence['frames'] += 1
            for level, kind, value in ancillary:
                if level == socket.SOL_SOCKET and kind == overflow:
                    evidence['overflow'] = struct.unpack('=I', value)[0]
            require(not evidence['overflow'], 'fault observer dropped packets')
            device, outgoing = address[0], address[2] == socket.PACKET_OUTGOING
            if device not in ('zbenchca', 'zbenchsa'):
                continue
            p = helper.tcp(raw, True)
            if p is None or PORT not in (p['sport'], p['dport']):
                continue
            if syn is None:
                if device == 'zbenchca' and not outgoing and p['dport'] == PORT and \
                        p['flags'] & 0x17 == 2:
                    require(not p['payload'], 'unexpected early SYN data')
                    syn = p
                continue
            if held is None:
                if device != 'zbenchsa' or outgoing or not helper.same_flow(p, syn, True) or \
                        p['flags'] & 0x17 != 0x12:
                    continue
                require(not p['payload'] and p['ack'] == (syn['seq'] + 1) & MASK,
                        'actual SYNACK origin mismatch')
                held = p
                (directory / 'held-synack.packet').write_bytes(raw)
                evidence['held_synack_sha256'] = hashlib.sha256(raw).hexdigest()
                evidence['held_synack_observed'] = time.time()
                with (directory / 'fault-origins').open('w') as fifo:
                    fifo.write(f"{(syn['seq'] + 1) & MASK} {p['family']}\n")
                with (directory / 'fault-release').open('r') as fifo:
                    require(fifo.read() == 'R\n', 'missing control release')
                evidence['forwarded_synack_time'] = time.time()
                require(tx.send(raw) == len(raw), 'unchanged SYNACK forwarding failed')
                save(directory / 'fault-control.json', evidence)
                continue
            if not helper.same_flow(p, syn) or not p['payload']:
                continue
            base = (syn['seq'] + 1) & MASK
            if device == 'zbenchca' and not outgoing and original is None:
                require(p['seq'] == base, 'observer missed first request original')
                original = raw
                (directory / 'fault-original.packet').write_bytes(raw)
            elif device == 'zbenchsa' and outgoing and original is not None:
                if p['seq'] != base and 0 < (p['seq'] - base) & MASK < 0x80000000:
                    higher = True
                recovered = mode == 'loss' and p['seq'] == base
                reordered = mode == 'reorder' and raw == original and higher
                if recovered or reordered:
                    evidence['observed_destination_time'] = time.time()
                    evidence['original_sha256'] = hashlib.sha256(original).hexdigest()
                    save(directory / 'fault-observation.json', evidence)
                    with (directory / 'fault-done').open('w') as fifo:
                        fifo.write('R\n')
                    return
        raise RuntimeError('actual DATA fault/recovery witness missing')
    finally:
        save(directory / 'fault-observer.json', evidence)
        rx.close()
        tx.close()


def read_packets(directory, helper):
    packets = []
    for side in ('client', 'server'):
        for stamp, frame in helper.capture(directory / ('to-' + side + '.pcap')):
            p = helper.tcp(frame, True)
            if p:
                p.update(time=stamp, frame=frame.hex(), side=side)
                packets.append(p)
    return sorted(packets, key=lambda p: p['time'])


def source_packets(directory, helper, name='fault-source.pcap', allow_reset=False):
    packets = []
    for stamp, frame in helper.capture(directory / name):
        p = helper.tcp(frame, allow_reset)  # client reflection remains forbidden
        if p:
            p.update(time=stamp, frame=frame.hex())
            packets.append(p)
    return sorted(packets, key=lambda p: p['time'])


def host_count(path):
    for line in path.read_text().splitlines():
        words = line.split()
        if words and words[0] == 'TcpOutRsts':
            return int(words[1])
    raise AssertionError('missing isolated kernel TcpOutRsts counter')


def closed_resets(directory, packets, settings, wire, helper):
    resets = [p for p in packets if p['flags'] & 4]
    source = source_packets(directory, helper)
    reverse_source = source_packets(directory, helper, 'fault-control-source.pcap', True)
    emitted = [p for p in reverse_source if p['flags'] & 4]
    before = float((directory / 'counter-snapshot.before').read_text())
    after = float((directory / 'counter-snapshot.after').read_text())
    require(before < min(p['time'] for p in source + reverse_source) and
            after > max(p['time'] for p in packets + source + reverse_source),
            'kernel counters do not bracket actual source/destination evidence')
    for side, captured in (('client', 0), ('server', len(emitted))):
        delta = host_count(directory / side / 'netstat.after') - \
            host_count(directory / side / 'netstat.before')
        require(delta == captured, 'kernel/source reset count mismatch')
    require(len(emitted) == len(resets), 'kernel reset source/destination mismatch')
    if not resets:
        return []
    require(settings['recovery'] == 'reorder', 'reset outside reorder profile')
    require(len(wire['flows']) == 1, 'fault scope requires one flow')
    flow = wire['flows'][0]
    closed = flow['last_fin_ack_time']
    require(not any(p['payload'] and p['time'] > closed for p in source + reverse_source),
            'new source DATA after both FIN acknowledgments')
    first = next(p for p in packets if p['dport'] == PORT and p['flags'] & 0x17 == 2)
    base = flow['bases'][0]
    used, delayed = set(), []
    for p in packets:
        if not helper.same_flow(p, first) or not p['payload'] or p['seq'] != base:
            continue
        matches = [(i, q) for i, q in enumerate(source) if i not in used and
                   q['frame'] == p['frame'] and q['time'] < p['time']]
        require(matches, 'destination first-range copy lacks actual source copy')
        index, q = matches[0]
        used.add(index)
        raw = bytes.fromhex(p['frame'])
        size = int.from_bytes(raw[16:18] if p['family'] == 4 else raw[18:20], 'big')
        if q['time'] < closed < p['time'] and size >= 128 and p['flags'] & 0x17 == 0x10:
            delayed.append(p)
    evidence, used_reset = [], set()
    for reset in resets:
        require(helper.same_flow(reset, first, True),
                'wrong closed reset tuple/direction')
        require(reset['time'] > closed and reset['flags'] == 4 and not reset['ack'] and
                not reset['window'] and not reset['options'] and not reset['payload'],
                'not the exact post-close bare RST')
        require(len(bytes.fromhex(reset['frame'])) == (54 if reset['family'] == 4 else 74),
                'not the unpadded kernel reset geometry')
        copies = [(i, p) for i, p in enumerate(emitted) if i not in used_reset and
                  p['frame'] == reset['frame'] and closed < p['time'] <= reset['time']]
        require(copies, 'reset has no identical kernel-counted source emission')
        index, actual = copies[0]
        used_reset.add(index)
        matches = [(i, p) for i, p in enumerate(delayed) if p['time'] < actual['time'] and
                   p['ack'] == reset['seq']]
        require(matches, 'reset lacks a unique queued old DATA trigger')
        index, trigger = matches[0]
        delayed.pop(index)
        require(not any(p['time'] > reset['time'] for p in source),
                'source output/reflection after closed reset')
        evidence.append({'sender': 'receiving kernel TCP after close',
                         'closed': closed, 'trigger': trigger['time'], 'time': reset['time'],
                         'trigger_sha256': hashlib.sha256(
                             bytes.fromhex(trigger['frame'])).hexdigest()})
    return evidence


def check_fault(directory, helper, analyzer):
    settings = dict(line.split('=', 1) for line in
                    (directory / 'settings').read_text().splitlines())
    require(settings['recovery'] in ('loss', 'reorder') and settings['topology'] == 'bridge',
            'wrong recovery topology/profile')
    require(settings['connections'] == '1' and settings['warmup'] == '0',
            'fault profile must be a measured single-flow workload')
    report = analyzer.analyze(directory, helper,
        reset_validator=lambda d, p, s, w: closed_resets(d, p, s, w, helper))
    require(report['status'] == 'pass', 'missing complete FIX wire proof')
    import re
    for log in ('fault-source.log', 'fault-control-source.log'):
        footer = (directory / log).read_text()
        require(re.findall(r'(\d+) packets dropped by kernel', footer) == ['0'],
                'source capture incomplete')
    packets = read_packets(directory, helper)
    source = source_packets(directory, helper)
    first = next(p for p in packets if p['dport'] == PORT and p['flags'] & 0x17 == 2)
    direct = [p for p in source if helper.same_flow(p, first) and p['payload']]
    require(direct, 'no actual source original')
    original = direct[0]
    raw = bytes.fromhex(original['frame'])
    size = int.from_bytes(raw[16:18] if original['family'] == 4 else raw[18:20], 'big')
    require(size >= 128 and original['flags'] & 0x17 == 0x10,
            'source first original is outside the DATA classifier')
    require(original['seq'] == (first['seq'] + 1) & MASK, 'not the first original')
    delivered = [p for p in packets if p['side'] == 'server' and p['payload']]
    control = json.loads((directory / 'fault-control.json').read_text())
    observed = json.loads((directory / 'fault-observer.json').read_text())
    require(not observed['overflow'] and observed['receive_actual'] >= 4 << 20,
            'observer evidence incomplete')
    held = (directory / 'held-synack.packet').read_bytes()
    require(hashlib.sha256(held).hexdigest() == control['held_synack_sha256'],
            'control source identity changed')
    require(any(p['frame'] == held.hex() and p['time'] >= control['forwarded_synack_time']
                for p in packets), 'actual held SYNACK was not forwarded')
    opened = float((directory / 'fault-control-open-time').read_text())
    control_source = source_packets(directory, helper, 'fault-control-source.pcap', True)
    require(any(p['frame'] == held.hex() and p['time'] < opened for p in control_source),
            'held SYNACK lacks actual source-capture provenance')
    require(not any(p['side'] == 'client' and p['flags'] & 2 and
                    p['time'] < opened for p in packets),
            'handshake escaped its setup gate')
    gate = (directory / 'fault-control-qdisc').read_text()
    require(any(int(n) > 0 for n in re.findall(r'dropped (\d+)', gate)), 'no held SYNACK')
    stats = (directory / 'fault-data-qdisc').read_text()
    drops = [int(n) for n in re.findall(r'dropped (\d+)', stats)]
    backlog = [int(n) for n in re.findall(r'backlog \S+ (\d+)p', stats)]
    require(backlog and not any(backlog), 'fault queue was flushed before draining')
    if settings['recovery'] == 'loss':
        release = float((directory / 'fault-filter-release-time').read_text())
        require(drops and max(drops) > 0, 'no actual DATA loss')
        retries = [p for p in direct[1:] if p['seq'] == original['seq']]
        require(retries, 'missing real first-range retry')
        repaired = [p for p in delivered if p['seq'] == original['seq']]
        require(repaired and min(p['time'] for p in repaired) >= release,
                'initial DATA escaped the loss gate')
        require(any(q['time'] < p['time'] and q['frame'] == p['frame']
                    for q in retries for p in repaired), 'retry did not reach receiver')
        selected = {'original_source': original['time'], 'recovered': repaired[0]['time'],
                    'actual_data_drops': max(drops)}
    else:
        require(drops and not any(drops), 'reorder lost DATA')
        delayed = next(p for p in delivered if p['frame'] == original['frame'])
        higher = [p for p in delivered if 0 < (p['seq'] - original['seq']) & MASK <
                  0x80000000 and p['time'] < delayed['time']]
        require(higher and original['time'] < higher[0]['time'] < delayed['time'],
                'no actual first-original order inversion')
        require('delay 20ms' in stats or 'delay 20.0ms' in stats, 'wrong reorder fault')
        selected = {'original_source': original['time'], 'higher': higher[0]['time'],
                    'delayed_original': delayed['time'], 'actual_data_drops': 0}
    report['recovery'] = dict(selected, profile=settings['recovery'],
                              unchanged_synack_gate=True, observer=observed)
    save(directory / 'recovery-evidence.json', report)
    print('# FIX recovery/control evidence PASS:', settings['recovery'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('monitor', 'check'))
    parser.add_argument('directory', type=Path)
    parser.add_argument('mode', nargs='?', choices=('loss', 'reorder'))
    parser.add_argument('--tcp-helper', type=Path,
                        default=Path(__file__).resolve().parents[1] / 'interop/zutcptcp.py')
    parser.add_argument('--fix-analyzer', type=Path,
                        default=Path(__file__).with_name('analyze-wire.py'))
    args = parser.parse_args()
    helper = load(args.tcp_helper, 'recovery_tcp')
    if args.command == 'monitor':
        require(args.mode is not None, 'missing fault mode')
        monitor(args.directory, args.mode, helper)
    else:
        check_fault(args.directory, helper, load(args.fix_analyzer, 'recovery_fix'))


if __name__ == '__main__':
    main()
