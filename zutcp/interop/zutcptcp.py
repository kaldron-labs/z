#!/usr/bin/env python3
# (c) Copyright 2026 Huw Rogers
# This code is licensed by the MIT license (see LICENSE for details).

"""Standard Ethernet/IP/TCP evidence only; no intermediate protocol decoder."""

import hashlib
import ipaddress
import json
import re
import struct
import sys
from pathlib import Path

MASK32 = (1 << 32) - 1


def checksum(data):
    if len(data) & 1:
        data += b'\0'
    value = sum(int.from_bytes(data[i:i + 2], 'big') for i in range(0, len(data), 2))
    while value >> 16:
        value = (value & 65535) + (value >> 16)
    return (~value) & 65535


def capture(path):
    data = path.read_bytes()
    formats = {b'\xd4\xc3\xb2\xa1': ('<', 1000000), b'M<\xb2\xa1': ('<', 1000000000),
               b'\xa1\xb2\xc3\xd4': ('>', 1000000), b'\xa1\xb2<M': ('>', 1000000000)}
    endian, scale = formats[data[:4]]
    assert struct.unpack(endian + 'I', data[20:24])[0] == 1
    offset = 24
    while offset < len(data):
        sec, sub, size, wire = struct.unpack(endian + 'IIII', data[offset:offset + 16])
        offset += 16
        assert size == wire and offset + size <= len(data)
        yield sec + sub / scale, data[offset:offset + size]
        offset += size


def options(raw, syn):
    result = {}
    offset = 0
    while offset < len(raw):
        kind = raw[offset]
        offset += 1
        if not kind:
            break
        if kind == 1:
            continue
        assert offset < len(raw)
        length = raw[offset]
        offset += 1
        assert length >= 2 and offset + length - 2 <= len(raw)
        value = raw[offset:offset + length - 2]
        offset += length - 2
        if kind == 2:
            assert length == 4 and syn
            result['mss'] = int.from_bytes(value, 'big')
        elif kind == 3:
            assert length == 3 and syn
            result['scale'] = min(value[0], 14)
        elif kind == 4:
            assert length == 2 and syn
            result['sack_permitted'] = True
        elif kind == 5:
            assert length >= 10 and (length - 2) % 8 == 0 and not syn
            result['sack'] = [struct.unpack('!II', value[i:i + 8])
                              for i in range(0, len(value), 8)]
        elif kind == 8:
            assert length == 10
            result['ts'] = struct.unpack('!II', value)
        elif kind == 34 and syn:
            if length == 2 or 6 <= length <= 18 and length % 2 == 0:
                result['tfo'] = value.hex()
    return result


def tcp(frame, allow_reset=False):
    assert len(frame) >= 14
    ether = frame[12:14]
    ip = frame[14:]
    if ether == b'\x08\x00':
        assert len(ip) >= 20 and ip[0] >> 4 == 4
        head = (ip[0] & 15) * 4
        length = int.from_bytes(ip[2:4], 'big')
        assert head >= 20 and head <= length <= len(ip) and checksum(ip[:head]) == 0
        assert not int.from_bytes(ip[6:8], 'big') & 0x3fff
        if ip[9] != 6:
            return None
        source, target = ip[12:16], ip[16:20]
        segment = ip[head:length]
        pseudo = source + target + b'\0\6' + len(segment).to_bytes(2, 'big')
        family = 4
    elif ether == b'\x86\xdd':
        assert len(ip) >= 40 and ip[0] >> 4 == 6
        length = int.from_bytes(ip[4:6], 'big')
        assert 40 + length <= len(ip)
        if ip[6] != 6:
            return None
        source, target = ip[8:24], ip[24:40]
        segment = ip[40:40 + length]
        pseudo = source + target + len(segment).to_bytes(4, 'big') + b'\0\0\0\6'
        family = 6
    else:
        return None
    assert len(segment) >= 20 and checksum(pseudo + segment) == 0
    head = (segment[12] >> 4) * 4
    assert 20 <= head <= 60 and head <= len(segment)
    sport, dport, seq, ack = struct.unpack('!HHII', segment[:12])
    flags = segment[13]
    assert allow_reset or not flags & 4, (
        'unexpected reset', source.hex(), sport, target.hex(), dport)
    return {'family': family, 'source': str(ipaddress.ip_address(source)), 'sport': sport,
            'target': str(ipaddress.ip_address(target)), 'dport': dport, 'seq': seq, 'ack': ack,
            'flags': flags, 'window': int.from_bytes(segment[14:16], 'big'),
            'ecn': (ip[1] & 3) if family == 4 else (ip[1] >> 4) & 3,
            'options': options(segment[20:head], bool(flags & 2)), 'payload': segment[head:]}


def same_flow(p, first, reverse=False):
    source, sport, target, dport = (first['source'], first['sport'],
                                  first['target'], first['dport'])
    if reverse:
        source, sport, target, dport = target, dport, source, sport
    return (p['source'], p['sport'], p['target'], p['dport']) == (source, sport, target, dport)


def observe_reorder(directory, source_device):
    """Watch both bridge ports; never transmit or discard an endpoint packet."""
    import os
    import socket
    import time
    assert os.environ.get('ZXDP_RUN_FIXTURE') == '1'
    assert os.readlink('/proc/self/ns/net') != os.readlink('/proc/1/ns/net')
    rx = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(3))
    first = reply = original = higher = None
    (directory / 'observer-ready').write_text('ready\n')
    deadline = time.monotonic() + 25
    try:
        while time.monotonic() < deadline:
            rx.settimeout(deadline - time.monotonic())
            frame, address = rx.recvfrom(65535)
            device, outgoing = address[0], address[2] == socket.PACKET_OUTGOING
            if device not in (source_device, 'ztcpksa'):
                continue
            p = tcp(frame)
            if p is None:
                continue
            if first is None:
                if device == source_device and not outgoing and p['dport'] == 18181 and \
                        p['flags'] & 0x12 == 2:
                    first = p
                continue
            base = (first['seq'] + 1) & MASK32
            if reply is None:
                if device == 'ztcpksa' and not outgoing and same_flow(p, first, True) and \
                        p['flags'] & 0x12 == 0x12:
                    assert p['ack'] == base and not p['payload']
                    reply = p
                    with (directory / 'origins').open('w') as fifo:
                        fifo.write(str(base) + ' ' + str((p['seq'] + 1) & MASK32) + '\n')
                continue
            if not same_flow(p, first) or not p['payload']:
                continue
            row = {'time': time.time(), 'frame': frame.hex(), 'seq': p['seq']}
            if device == source_device and not outgoing and original is None:
                assert p['seq'] == base
                original = row
            elif device == 'ztcpksa' and outgoing and original is not None:
                if frame.hex() == original['frame']:
                    assert higher is not None
                    (directory / 'reorder-observation.json').write_text(json.dumps(
                        {'source_original': original, 'higher_arrival': higher,
                         'delayed_original': row}, indent=2) + '\n')
                    with (directory / 'reorder-ready').open('w') as fifo:
                        fifo.write('R\n')
                    return
                if 0 < (p['seq'] - base) & MASK32 < 0x80000000 and higher is None:
                    higher = row
        raise RuntimeError('exact delayed original after higher DATA was not observed')
    finally:
        rx.close()


def observe(directory, device, mode):
    """Observe actual kernel/native packets; never generate a peer TCP segment."""
    import os
    import socket
    import time
    assert os.environ.get('ZXDP_RUN_FIXTURE') == '1'
    assert os.readlink('/proc/self/ns/net') != os.readlink('/proc/1/ns/net')
    if mode == 'reorder':
        return observe_reorder(directory, device)
    first = reply = zero = probe = None
    next_sent = 0
    request_size = len((directory / 'request').read_bytes())
    rx = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(3))
    rx.bind((device, 0))
    (directory / 'observer-ready').write_text('ready\n')
    deadline = time.monotonic() + 25
    try:
        while time.monotonic() < deadline:
            rx.settimeout(deadline - time.monotonic())
            frame = rx.recv(65535)
            p = tcp(frame, mode == 'reset')
            if p is None:
                continue
            now = time.time()
            if first is None:
                if p['dport'] == 18181 and p['flags'] & 0x12 == 2:
                    first = p
                continue
            client_base = (first['seq'] + 1) & MASK32
            if reply is None and same_flow(p, first, True) and p['flags'] & 0x12 == 0x12:
                assert p['ack'] == client_base and not p['payload']
                reply = p
                if mode == 'origins':
                    with (directory / 'origins').open('w') as fifo:
                        fifo.write(str(client_base) + ' ' + str((p['seq'] + 1) & MASK32) + '\n')
                    return
            if reply is None or p['flags'] & 2:
                continue
            if same_flow(p, first):
                offset = (p['seq'] - client_base) & MASK32
                if p['payload']:
                    next_sent = max(next_sent, offset + len(p['payload']))
                if zero and not p['flags'] & 7 and len(p['payload']) <= 1 and p['seq'] in (
                        (zero['ack'] - 1) & MASK32, (client_base + next_sent - 1) & MASK32):
                    probe = {'time': now, 'seq': p['seq'], 'frame': frame.hex()}
            elif same_flow(p, first, True) and p['flags'] & 16:
                frontier = (p['ack'] - client_base) & MASK32
                if mode == 'reset' and frontier == 260 and next_sent == 260:
                    (directory / 'reset-observation.json').write_text(json.dumps(
                        {'ack_time': now, 'ack_frame': frame.hex()}, indent=2) + '\n')
                    with (directory / 'reset-ready').open('w') as fifo:
                        fifo.write('R\n')
                    return
                if not p['window'] and 0 < frontier < request_size:
                    if zero is None:
                        zero = {'time': now, 'ack': p['ack'], 'frontier': frontier,
                                'frame': frame.hex()}
                    elif probe and p['ack'] == zero['ack']:
                        evidence = {'zero': zero, 'probe': probe,
                                    'reply': {'time': now, 'frame': frame.hex()}}
                        (directory / 'window-observation.json').write_text(
                            json.dumps(evidence, indent=2) + '\n')
                        with (directory / 'window-ready').open('w') as fifo:
                            fifo.write('R\n')
                        return
        raise RuntimeError('actual SYN origins/zero-window probe reply were not observed')
    finally:
        rx.close()


def check_flow(directory, direction, family, profile, packets, request, tfo=False):
    response = b'echo: ' + request
    syns = [p for p in packets if p['side'] == 'server' and p['flags'] & 0x12 == 2]
    replies = [p for p in packets if p['side'] == 'client' and p['flags'] & 0x12 == 0x12]
    assert syns and replies
    first = min(syns, key=lambda p: p['time'])
    reply = min(replies, key=lambda p: p['time'])
    assert first['dport'] == 18181 and reply['sport'] == first['dport']
    assert reply['dport'] == first['sport']
    assert reply['source'] == first['target'] and reply['target'] == first['source']
    client_base, server_base = (first['seq'] + 1) & MASK32, (reply['seq'] + 1) & MASK32
    assert all(p['seq'] == first['seq'] and (tfo or not p['payload']) for p in syns)
    early = len(first['payload']) if tfo and reply['ack'] == (
        client_base + len(first['payload'])) & MASK32 else 0
    assert all(p['seq'] == reply['seq'] and
               p['ack'] == (client_base + early) & MASK32 and not p['payload'] for p in replies)
    assert profile in ('baseline', 'absent', 'loss', 'window', 'reorder')
    if profile == 'absent':
        assert all(not any(name in p['options'] for name in ('scale', 'ts', 'sack_permitted'))
                   and not p['flags'] & 0xc0 for p in syns + replies)
        assert all(not any(name in p['options'] for name in ('ts', 'sack')) and
                   not p['flags'] & 0xc0 and not p['ecn'] for p in packets)
    else:
        assert 'mss' in first['options'] and 'mss' in reply['options']
    streams = {}
    for side, expected, base, peer, peer_bytes in (
            ('server', request, client_base, server_base, len(response)),
            ('client', response, server_base, client_base, len(request))):
        seen = bytearray(len(expected))
        data_packets = 0
        fin = []
        ack = 0
        for p in packets:
            if p['side'] != side:
                continue
            if p['flags'] & 2:
                if not tfo or side != 'server' or not early:
                    continue
                assert p['payload'] == request[:early]
                offset = 0
            else:
                offset = (p['seq'] - base) & MASK32
            payload = p['payload']
            if payload:
                assert offset + len(payload) <= len(expected)
                assert payload == expected[offset:offset + len(payload)], (
                    'non-application bytes, gap or corruption', side, offset, len(payload))
                seen[offset:offset + len(payload)] = b'\1' * len(payload)
                data_packets += 1
            if p['flags'] & 1:
                assert offset + len(payload) == len(expected)
                fin.append(p['time'])
            if p['flags'] & 16 and not p['flags'] & 2:
                frontier = (p['ack'] - peer) & MASK32
                assert frontier <= peer_bytes + 1
                ack = max(ack, frontier)
        assert all(seen) and data_packets > 1 and fin and ack == peer_bytes + 1
        streams[side] = {'bytes': len(expected), 'data_packets': data_packets,
                         'first_fin': min(fin), 'peer_acked': ack}
    first_response = min(p['time'] for p in packets if p['side'] == 'client' and p['payload'])
    assert first_response >= streams['server']['first_fin']
    native_side = 'client' if direction == 'native-client' else 'server'
    kernel_side = 'server' if direction == 'native-client' else 'client'
    native = (directory / (native_side + '.err')).read_text()
    kernel = (directory / (kernel_side + '.err')).read_text()
    assert 'backend=AF_XDP role=' + native_side + ' ready=1' in native
    drained = re.search(
        r'backend=AF_XDP stopped=1 cxns=0 pending=0 tx=0 control=(\d+)/(\d+)', native)
    assert drained and drained[1] == drained[2]
    assert 'backend=kernel-tcp role=' + kernel_side in kernel
    selected = {}
    if profile == 'loss':
        source = []
        capture_log = (directory / 'fault-source.capture').read_text()
        assert '0 packets dropped by kernel' in capture_log
        for stamp, raw in capture(directory / 'fault-source.pcap'):
            p = tcp(raw)
            if p and same_flow(p, first) and p['payload']:
                p.update(time=stamp, frame=raw.hex())
                source.append(p)
        lost = min((p for p in source if p['seq'] == client_base), key=lambda p: p['time'])
        delivered = sorted((p for p in packets if p['side'] == 'server' and p['payload']),
                           key=lambda p: p['time'])
        recovered = min((p for p in delivered if p['seq'] == client_base),
                        key=lambda p: p['time'])
        retry = min((p for p in source if p['seq'] == client_base and
                     p['time'] > lost['time']), key=lambda p: p['time'])
        assert lost['time'] < retry['time'] <= recovered['time']
        if direction == 'native-client':
            assert lost['payload'] == recovered['payload']
        else:
            assert lost['payload'].startswith(recovered['payload'])
        assert delivered[0]['seq'] != client_base, 'loss must actually expose a receive gap'
        assert not any(p['frame'] == lost['frame'] and p['time'] < recovered['time']
                       for p in delivered)
        drops = [int(n) for n in re.findall(r'dropped (\d+)',
                                          (directory / 'fault-loss').read_text())]
        assert drops and max(drops) >= 1
        selected = {'lost_first_data_bytes': len(lost['payload']),
                    'source_first_time': lost['time'], 'source_retry_time': retry['time'],
                    'recovered_time': recovered['time'],
                    'out_of_order_first_seq': delivered[0]['seq'], 'actual_drops': max(drops)}
    if profile == 'reorder':
        source = []
        assert '0 packets dropped by kernel' in (directory / 'fault-source.capture').read_text()
        for stamp, raw in capture(directory / 'fault-source.pcap'):
            p = tcp(raw)
            if p and same_flow(p, first) and p['payload']:
                p.update(time=stamp, frame=raw.hex())
                source.append(p)
        original = min(source, key=lambda p: p['time'])
        assert original['seq'] == client_base
        raw_original = bytes.fromhex(original['frame'])
        field = raw_original[16:18] if family == 4 else raw_original[18:20]
        assert 1024 <= int.from_bytes(field, 'big') < 2048
        delivered = sorted((p for p in packets if p['side'] == 'server' and p['payload']),
                           key=lambda p: p['time'])
        assert delivered[0]['seq'] != client_base
        delayed = min((p for p in delivered if p['frame'] == original['frame']),
                      key=lambda p: p['time'])
        assert original['time'] < delivered[0]['time'] < delayed['time']
        assert delayed['payload'] == original['payload']
        observed = json.loads((directory / 'reorder-observation.json').read_text())
        assert observed['source_original']['frame'] == original['frame']
        assert observed['delayed_original']['frame'] == delayed['frame']
        assert any(p['frame'] == observed['higher_arrival']['frame'] and
                   p['time'] < delayed['time'] for p in delivered)
        stats = (directory / 'fault-reorder').read_text()
        drops = [int(n) for n in re.findall(r'dropped (\d+)', stats)]
        backlog = [int(n) for n in re.findall(r'backlog \S+ (\d+)p', stats)]
        assert re.search(r'qdisc netem[^\n]*\bdelay\s+20(?:\.0+)?ms\b', stats)
        assert drops and not any(drops) and backlog and not any(backlog)
        netem = stats[stats.index('qdisc netem'):].split('\nqdisc ', 1)[0]
        served = re.search(r'Sent (\d+) bytes (\d+) pkt', netem)
        assert served and int(served[1]) > 0 and int(served[2]) > 0
        selected = {'source_first_time': original['time'],
                    'higher_first_time': delivered[0]['time'],
                    'delayed_original_time': delayed['time'],
                    'original_sha256': hashlib.sha256(raw_original).hexdigest(),
                    'delayed_packets_served': int(served[2]),
                    'actual_drops': 0, 'drained_backlog_packets': 0}
    if profile == 'window':
        observation = json.loads((directory / 'window-observation.json').read_text())
        release = float((directory / 'window-release-time').read_text())
        zero = observation['zero']
        zero_wire = [p for p in packets if p['frame'] == zero['frame']]
        probe_wire = [p for p in packets if p['frame'] == observation['probe']['frame']]
        reply_wire = [p for p in packets if p['frame'] == observation['reply']['frame']]
        assert zero_wire and probe_wire and reply_wire
        assert zero_wire[0]['side'] == 'client' and not zero_wire[0]['window']
        assert probe_wire[0]['side'] == 'server' and len(probe_wire[0]['payload']) <= 1
        probe_time = min(p['time'] for p in probe_wire if p['time'] >= zero_wire[0]['time'])
        reply_time = min(p['time'] for p in reply_wire if p['time'] > probe_time)
        assert zero_wire[0]['time'] < probe_time < reply_time < release
        opened = min((p for p in packets if p['side'] == 'client' and p['flags'] & 16 and
                      p['window'] and p['time'] >= release and p['ack'] == zero['ack']),
                     key=lambda p: p['time'])
        assert any(p['side'] == 'server' and p['payload'] and p['time'] > opened['time'] and
                   ((p['seq'] - client_base) & MASK32) >= zero['frontier'] for p in packets)
        server_log = (directory / 'server.err').read_text()
        assert server_log.count('window reader=held\n') == 1
        assert server_log.count('window reader=released\n') == 1
        if direction == 'native-client':
            rcvbuf = re.search(r'window kernel_rcvbuf=(\d+)', server_log)
            assert rcvbuf and int(rcvbuf[1]) >= 4096
        selected = {'zero_frontier': zero['frontier'], 'probe_time': probe_time,
                    'zero_reply_time': reply_time, 'release_time': release,
                    'reopened_time': opened['time']}
    report = {'mode': 'kernel-interop', 'profile': profile,
              'direction': direction, 'family': family, 'selected': selected,
              'request_sha256': hashlib.sha256(request).hexdigest(),
              'client_syn_options': first['options'], 'server_syn_options': reply['options'],
              'streams': streams, 'native_backend': 'AF_XDP', 'peer_backend': 'kernel-tcp'}
    return report


def check_reset(directory, direction, packets, request):
    syns = [p for p in packets if p['side'] == 'server' and p['flags'] & 0x12 == 2]
    replies = [p for p in packets if p['side'] == 'client' and p['flags'] & 0x12 == 0x12]
    assert syns and replies
    first = min(syns, key=lambda p: p['time'])
    reply = min(replies, key=lambda p: p['time'])
    base, peer = (first['seq'] + 1) & MASK32, (reply['seq'] + 1) & MASK32
    assert all(p['seq'] == first['seq'] and not p['payload'] for p in syns)
    assert all(p['seq'] == reply['seq'] and p['ack'] == base and not p['payload']
               for p in replies)
    seen = bytearray(260)
    for p in packets:
        assert not p['flags'] & 1, 'reset connection must not send FIN'
        if p['payload']:
            assert p['side'] == 'server'
            offset = (p['seq'] - base) & MASK32
            assert offset + len(p['payload']) <= 260
            assert p['payload'] == request[offset:offset + len(p['payload'])]
            seen[offset:offset + len(p['payload'])] = b'\1' * len(p['payload'])
    assert all(seen)
    resets = [p for p in packets if p['flags'] & 4]
    assert len(resets) == 1
    reset = resets[0]
    assert reset['side'] == 'server' and reset['flags'] == 0x14 and not reset['payload']
    assert reset['seq'] == (base + 260) & MASK32 and reset['ack'] == peer
    ack = min((p for p in packets if p['side'] == 'client' and p['flags'] & 16 and
               p['ack'] == (base + 260) & MASK32 and p['time'] < reset['time']),
              key=lambda p: p['time'])
    assert not any(p['side'] == 'client' and p['time'] > reset['time'] for p in packets)
    observed = json.loads((directory / 'reset-observation.json').read_text())
    assert any(p['frame'] == observed['ack_frame'] for p in packets)
    native_side = 'client' if direction == 'native-client' else 'server'
    native = (directory / (native_side + '.err')).read_text()
    kernel = (directory / ('server.err' if native_side == 'client' else 'client.err')).read_text()
    if direction == 'native-client':
        assert native.count('reset native_client peer=0 local_error=1311768467463790320\n') == 1
        assert kernel.count('reset kernel_server errno=104 bytes=260\n') == 1
    else:
        assert native.count('reset native_server peer=1 bytes=260\n') == 1
        assert kernel.count('reset kernel_client linger=0\n') == 1
        assert 'ts' in first['options'] and 'ts' in reply['options']
        seeded = re.findall(r'reset fixture_history_seed id=(\d+) timestamps=1 live_slot=1',
                            native)
        retired = re.findall(
            r'reset fixture_history_retired seed=(\d+) current=(\d+) fin=0 slot=0', native)
        assert len(seeded) == len(retired) == 1
        assert int(seeded[0]) > 0 and retired[0][0] == seeded[0]
        assert int(retired[0][1]) != int(seeded[0]) and int(retired[0][1]) > 0
    return {'prefix_bytes': 260, 'ack_time': ack['time'], 'reset_time': reset['time'],
            'local_error': 1311768467463790320 if direction == 'native-client' else None,
            'fixture_seeded_history': direction == 'kernel-client',
            'actual_reset_options': reset['options'],
            'missing_ts_live_reset': direction == 'kernel-client' and
                'ts' not in reset['options']}


def check_tfo(directory, direction, profile, connections, request):
    cookie = None
    evidence = []
    for index, flow in enumerate(connections):
        syns = sorted((p for p in flow if p['side'] == 'server' and p['flags'] & 2),
                      key=lambda p: p['time'])
        replies = sorted((p for p in flow if p['side'] == 'client' and p['flags'] & 2),
                         key=lambda p: p['time'])
        first, reply = syns[0], replies[0]
        base = (first['seq'] + 1) & MASK32
        if not index:
            assert first['options'].get('tfo') == '' and not first['payload']
            cookie = reply['options']['tfo']
            assert 8 <= len(cookie) <= 32 and len(cookie) % 4 == 0
            assert reply['ack'] == base
        elif profile == 'tfo-fallback' and direction == 'native-client':
            assert 'tfo' not in first['options'] and not first['payload']
            assert reply['ack'] == base
        else:
            assert first['options'].get('tfo') == cookie
            assert first['payload'] == request[:260]
            accepted = profile == 'tfo'
            assert reply['ack'] == (base + (260 if accepted else 0)) & MASK32
            if not accepted:
                assert any(p['side'] == 'server' and not p['flags'] & 2 and
                           p['seq'] == base and p['payload'].startswith(request[:260])
                           and p['time'] > reply['time'] for p in flow)
        evidence.append({'cookie': first['options'].get('tfo'),
                         'syn_bytes': len(first['payload']),
                         'synack_acked_bytes': (reply['ack'] - base) & MASK32})
    native_side = 'client' if direction == 'native-client' else 'server'
    native = (directory / (native_side + '.err')).read_text()
    kernel = (directory / ('server.err' if native_side == 'client' else 'client.err')).read_text()
    for index in (1, 2):
        if native_side == 'client':
            queued = 'tfo native_client queued=' + str(index) + '\n'
            connected = 'tfo native_client connected=' + str(index) + '\n'
            assert native.count(queued) == native.count(connected) == 1
            assert native.index(queued) < native.index(connected)
            assert native.count('tfo native_client accepted=' + str(index) + '\n') == int(
                profile == 'tfo' and index == 2)
            assert 'tfo native_client rejected=' not in native
        else:
            connected = 'tfo native_server connected=' + str(index) + '\n'
            assert native.count(connected) == 1
            rows = re.findall(r'tfo native_server process=' + str(index) +
                              r' bytes=(\d+) connected=([01])', native)
            assert rows and sum(int(n) for n, unused in rows) == len(request)
            early = [(int(n), flag) for n, flag in rows if flag == '0']
            assert early == ([(260, '0')] if profile == 'tfo' and index == 2 else [])
    if native_side == 'client':
        assert 'tfo kernel_server supported=1\n' in kernel
    else:
        assert len(re.findall(r'connected=1 tfo_sent=\d+', kernel)) == 2
    return evidence


def reorder_closed_replies(directory, packets, report, request):
    resets = sorted((p for p in packets if p['flags'] & 4), key=lambda p: p['time'])
    if not resets:
        return []
    first = min((p for p in packets if p['side'] == 'server' and p['flags'] & 0x12 == 2),
                key=lambda p: p['time'])
    reply = min((p for p in packets if p['side'] == 'client' and p['flags'] & 0x12 == 0x12),
                key=lambda p: p['time'])
    base, peer = (first['seq'] + 1) & MASK32, (reply['seq'] + 1) & MASK32
    end = (base + report['streams']['server']['bytes'] + 1) & MASK32
    peer_end = (peer + report['streams']['client']['bytes'] + 1) & MASK32
    closed = max(min(p['time'] for p in packets if p['side'] != side and
                     p['flags'] & 16 and p['ack'] == frontier and
                     p['time'] >= report['streams'][side]['first_fin'])
                 for side, frontier in (('server', end), ('client', peer_end)))
    source = []
    for stamp, raw in capture(directory / 'fault-source.pcap'):
        p = tcp(raw)  # A source reset/reflection remains forbidden.
        if p and same_flow(p, first):
            p.update(time=stamp, frame=raw.hex())
            source.append(p)
    source.sort(key=lambda p: p['time'])
    used_source, delayed = set(), []
    for p in sorted(packets, key=lambda p: p['time']):
        if p['side'] != 'server' or not same_flow(p, first) or p['seq'] != base or \
                not p['payload']:
            continue
        matches = [(i, q) for i, q in enumerate(source) if i not in used_source and
                   q['frame'] == p['frame'] and q['time'] < p['time']]
        assert matches, ('delayed DATA lacks its actual source copy', p)
        index, sent = matches[0]
        used_source.add(index)
        raw = bytes.fromhex(p['frame'])
        size = int.from_bytes(raw[16:18] if p['family'] == 4 else raw[18:20], 'big')
        if sent['time'] < closed < p['time'] and 1024 <= size < 2048 and \
                p['flags'] & 0x17 == 0x10:
            assert p['payload'] == request[:len(p['payload'])]
            delayed.append((p, sent))
    evidence = []
    for reset in resets:
        assert reset['side'] == 'client' and same_flow(reset, first, True)
        assert reset['time'] > closed and reset['flags'] == 0x04
        assert not reset['ack'] and not reset['window'] and not reset['options']
        assert not reset['payload']
        matches = [(i, p, sent) for i, (p, sent) in enumerate(delayed) if
                   p['time'] < reset['time'] and reset['seq'] == p['ack']]
        assert matches, ('closed reset lacks an unmatched queued old DATA trigger', reset)
        index, trigger, sent = matches[0]
        delayed.pop(index)
        # Destination copies may still be draining, but no new source output may
        # reflect the closed reply or resurrect data after the completed flow.
        assert not any(p['time'] > reset['time'] for p in source)
        evidence.append({'sender': 'receiving host after TCP close',
                         'both_fin_acks_time': closed, 'source_time': sent['time'],
                         'trigger_time': trigger['time'], 'reset_time': reset['time'],
                         'trigger_sha256': hashlib.sha256(
                             bytes.fromhex(trigger['frame'])).hexdigest(),
                         'reset_wire_seq': reset['seq'], 'trigger_wire_ack': trigger['ack']})
    return evidence


def check(directory, direction, family, profile='baseline'):
    request = (directory / 'request').read_bytes()
    count = 2 if profile in ('repeat', 'tfo', 'tfo-fallback') else 1
    assert (directory / 'client.out').read_bytes() == (b'echo: ' + request) * count
    packets = []
    for side in ('server', 'client'):
        capture_log = (directory / ('to-' + side + '.capture')).read_text()
        assert '0 packets dropped by kernel' in capture_log
        for stamp, frame in capture(directory / ('to-' + side + '.pcap')):
            packet = tcp(frame, profile in ('reset', 'reorder'))
            if packet is None:
                continue
            assert packet['family'] == family
            packet.update(time=stamp, side=side, frame=frame.hex())
            packets.append(packet)
    if profile not in ('repeat', 'reset', 'tfo', 'tfo-fallback'):
        stream_packets = [p for p in packets if not p['flags'] & 4]
        report = check_flow(directory, direction, family, profile, stream_packets, request)
        if profile == 'reorder':
            report['selected']['post_close_host_resets'] = reorder_closed_replies(
                directory, packets, report, request)
    else:
        starts = {}
        for p in sorted(packets, key=lambda p: p['time']):
            if p['side'] == 'server' and p['flags'] & 0x12 == 2:
                key = (p['source'], p['target'], p['sport'], p['dport'])
                starts.setdefault(key, p)
        assert len(starts) == 2, 'two actual independent TCP connections are required'
        reports, connections = [], []
        prior_done = None
        for index, first in enumerate(starts.values()):
            flow = [p for p in packets if same_flow(p, first) or same_flow(p, first, True)]
            connections.append(flow)
            if prior_done is not None:
                assert first['time'] > prior_done, (
                    'next connection started before the prior application close boundary')
            if profile == 'reset' and not index:
                report = check_reset(directory, direction, flow, request)
                prior_done = report['reset_time']
                reports.append(report)
                continue
            report = check_flow(directory, direction, family, 'baseline', flow, request,
                                tfo=profile in ('tfo', 'tfo-fallback'))
            peer_syn = min((p for p in flow if p['side'] == 'client' and
                            p['flags'] & 0x12 == 0x12), key=lambda p: p['time'])
            ends = {'server': ((first['seq'] + len(request) + 2) & MASK32),
                    'client': ((peer_syn['seq'] + len(request) + 8) & MASK32)}
            completed = []
            for side, frontier in ends.items():
                fin_time = report['streams'][side]['first_fin']
                completed.append(min(p['time'] for p in flow if p['side'] != side and
                                     p['flags'] & 16 and p['ack'] == frontier and
                                     p['time'] >= fin_time))
            final_ack = max(completed)
            # Native main explicitly waits the public disconnect callback. Kernel
            # close may leave its own FIN awaiting ACK after response EOF returned.
            prior_done = final_ack if direction == 'native-client' else (
                report['streams']['client']['first_fin'])
            report['client_port'] = first['sport']
            report['final_ack_time'] = final_ack
            report['next_open_barrier_time'] = prior_done
            report['next_open_barrier'] = (
                'disconnect-callback' if direction == 'native-client' else 'response-eof')
            reports.append(report)
        native_side = 'client' if direction == 'native-client' else 'server'
        native = (directory / (native_side + '.err')).read_text()
        assert native.count('backend=AF_XDP role=' + native_side + ' connected=1\n') == 2
        if direction == 'native-client':
            assert native.count('native disconnect_complete=1\n') == 1
            assert native.count('native disconnect_complete=2\n') == 1
        report = {'mode': 'kernel-interop', 'profile': profile, 'direction': direction,
                  'family': family, 'connections': reports, 'same_stack': True}
        if profile in ('tfo', 'tfo-fallback'):
            report['tfo'] = check_tfo(directory, direction, profile, connections, request)
    (directory / 'tcp-evidence.json').write_text(json.dumps(report, indent=2) + '\n')
    print('# ordinary kernel TCP/' + direction + ': exact binary bytes, standard SYN/ACK/FIN')


if __name__ == '__main__':
    if sys.argv[1] == 'observe':
        observe(Path(sys.argv[2]), sys.argv[3], sys.argv[4])
    else:
        check(Path(sys.argv[1]), sys.argv[2], int(sys.argv[3]),
              sys.argv[4] if len(sys.argv) > 4 else 'baseline')
