#!/usr/bin/env python3
# (c) Copyright 2026 Huw Rogers
# This code is licensed by the MIT license (see LICENSE for details).

"""Assert negotiated options on actual native request/echo traffic."""

import hashlib
import json
import os
import struct
import sys
from pathlib import Path

from zutcprecovery import MASK32, WireOrigins, abort_checksum, tcp_packet, pcap_frames, wire_tcp


def options(tcp):
    end, offset, found = (tcp[12] >> 4) * 4, 20, {}
    assert 20 <= end <= 60
    while offset < end:
        kind = tcp[offset]
        if kind == 0:
            break
        if kind == 1:
            offset += 1
            continue
        assert offset + 2 <= end
        size = tcp[offset + 1]
        assert size >= 2 and offset + size <= end
        value = tcp[offset + 2:offset + size]
        found.setdefault(kind, value)
        offset += size
    return found


def check(directory):
    out = Path(directory)
    profiles = {side: int(os.environ['ZUTCP_TEST_' + side.upper() + '_OPTIONS'])
                for side in ('client', 'server')}
    timestamps = bool(profiles['client'] & profiles['server'] & 4)
    scaling = bool(profiles['client'] & profiles['server'] & 2)
    streams = {}
    hashes = {}
    for source, target in (('client', 'server'), ('server', 'client')):
        path = out / ('to-' + target + '.pcap')
        footer = path.with_suffix('.capture').read_text().splitlines()
        assert '0 packets dropped by kernel' in footer, ('incomplete capture', path, footer)
        packets = []
        for when, frame in pcap_frames(path):
            p = wire_tcp(frame)
            if p is None or (p['dport'] == 18181) != (source == 'client'):
                continue
            p.update(time=when, options=options(p['tcp']))
            packets.append(p)
        assert packets, source
        syns = [p for p in packets if p['flags'] & 2]
        data = [p for p in packets if p['flags'] & 8 and not p['flags'] & 2]
        assert syns and data, source
        expected_ws = bool(profiles[source] & 2)
        expected_ts = bool(profiles[source] & 4)
        if source == 'server':
            expected_ws = scaling
            expected_ts = timestamps
        for p in syns:
            opts = p['options']
            assert (3 in opts) == expected_ws, (source, opts, profiles)
            assert (8 in opts) == expected_ts, (source, opts, profiles)
            assert (4 in opts) == bool(profiles[source] & 1), (source, opts, profiles)
            assert 2 in opts and len(opts[2]) == 2 and int.from_bytes(opts[2], 'big') > 0
            assert p['window'] <= 65535
            if 3 in opts:
                assert len(opts[3]) == 1 and 0 < opts[3][0] <= 14
        scale = syns[0]['options'].get(3, b'\0')[0] if scaling else 0
        for p in packets:
            if p['flags'] & (2 | 4):
                continue
            assert (8 in p['options']) == timestamps, (source, p, profiles)
        if scaling:
            assert any((p['window'] << scale) > 65535
                       for p in packets if not p['flags'] & 2), source
        streams[source] = packets
        hashes[path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
    if timestamps:
        for source, target in (('client', 'server'), ('server', 'client')):
            sent = {struct.unpack('!II', p['options'][8])[0]
                    for p in streams[target] if 8 in p['options']}
            echoes = [struct.unpack('!II', p['options'][8])[1]
                      for p in streams[source] if not p['flags'] & (2 | 4)]
            assert echoes and all(echo in sent for echo in echoes), (source, echoes, sent)
    for source, target in (('client', 'server'), ('server', 'client')):
        peer_syn = next(p for p in streams[target] if p['flags'] & 2)
        peer_mss = int.from_bytes(peer_syn['options'][2], 'big')
        for packet in streams[source]:
            if not packet['flags'] & 2:
                assert len(packet['tcp']) - 20 <= peer_mss, (source, peer_mss, packet)
    expected = (out / 'expected').read_bytes()
    assert (out / 'client.out').read_bytes() == expected
    assert expected.startswith(b'echo: ') and expected.endswith(b'\n')
    result = {'profiles': profiles, 'timestamps': timestamps, 'window_scaling': scaling,
              'request_bytes': len(expected) - len(b'echo: \n'),
              'exact_echo': True, 'pcap_sha256': hashes,
              'packets': {side: len(packets) for side, packets in streams.items()},
              'result': 'PASS'}
    (out / 'option-evidence.json').write_text(json.dumps(result, indent=2) + '\n')
    print('# native option matrix:', json.dumps(result, sort_keys=True))


def native_packet(frame, origins):
    origins.observe(frame)
    raw = wire_tcp(frame)
    if raw is None or 18181 not in (raw['sport'], raw['dport']) or raw['flags'] & 2:
        return None
    packet = origins.relative(frame)
    packet['options'] = options(packet['tcp'])
    payload = packet['body']
    packet['data'] = [(packet['seq'], len(payload), payload)] if payload else []
    packet['sack'] = []
    if 5 in packet['options']:
        value = packet['options'][5]
        assert value and len(value) % 8 == 0
        for i in range(0, len(value), 8):
            left, right = struct.unpack('!II', value[i:i + 8])
            packet['sack'].append(((left - packet['ack_base']) & MASK32,
                                   (right - packet['ack_base']) & MASK32))
    return packet


def with_sack(frame, ranges):
    """Replace only SACK TLVs; retain actual timestamp/echo and variable TCP header."""
    packet = wire_tcp(frame)
    tcp = packet['tcp']
    old_head = (tcp[12] >> 4) * 4
    kept = bytearray()
    offset = 20
    while offset < old_head:
        kind = tcp[offset]
        if kind == 0:
            break
        if kind == 1:
            offset += 1
            continue
        size = tcp[offset + 1]
        assert size >= 2 and offset + size <= old_head
        if kind != 5:
            kept.extend(tcp[offset:offset + size])
        offset += size
    if ranges:
        kept.extend(bytes((5, 2 + 8 * len(ranges))))
    for left, right in ranges:
        kept.extend(struct.pack('!II', left & MASK32, right & MASK32))
    while len(kept) % 4:
        kept.append(0)
    assert len(kept) <= 40
    tcp = bytearray(tcp[:20] + kept + tcp[old_head:])
    tcp[12] = ((20 + len(kept)) // 4) << 4 | (tcp[12] & 15)
    tcp[16:18] = b'\0\0'
    ip = bytearray(frame[14:])
    if frame[12:14] == b'\x08\x00':
        head = (ip[0] & 15) * 4
        ip = ip[:head]
        ip[2:4] = (head + len(tcp)).to_bytes(2, 'big')
        ip[10:12] = b'\0\0'
        ip[10:12] = abort_checksum(bytes(ip)).to_bytes(2, 'big')
        pseudo = bytes(ip[12:20]) + b'\0\x06' + len(tcp).to_bytes(2, 'big')
    else:
        ip = ip[:40]
        ip[4:6] = len(tcp).to_bytes(2, 'big')
        pseudo = bytes(ip[8:40]) + len(tcp).to_bytes(4, 'big') + b'\0\0\0\x06'
    tcp[16:18] = abort_checksum(pseudo + bytes(tcp)).to_bytes(2, 'big')
    return frame[:14] + bytes(ip + tcp)


def reneging_fault(directory, client_port, server_port):
    """Hold real feedback; replay allocated, never-delivered peer PNs causally."""
    import socket
    import subprocess
    import time
    assert os.environ.get('ZXDP_RUN_FIXTURE') == '1'
    assert os.readlink('/proc/self/ns/net') != os.readlink('/proc/1/ns/net')
    out = Path(directory)
    rx = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(3))
    tx = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(3))
    tx.bind((client_port, 0))
    injector_mark = 0x5a17
    tx.setsockopt(socket.SOL_SOCKET, socket.SO_MARK, injector_mark)
    origins = WireOrigins()
    originals = {}
    events = []
    evidence = {'mode': 'sack-reneging', 'controlled_feedback': events,
                'injector_mark': injector_mark}
    claimed = []
    first_hole = None
    primer = False
    fake_count = 0
    frontier_seen = None
    latest_ts = {}
    protocol = None
    deadline = time.monotonic() + 22

    def tc(*args):
        return subprocess.run(['tc', *args], check=True, capture_output=True, text=True).stdout

    def release(device, pref):
        evidence['qdisc_before_release_' + device + '_' + str(pref)] = tc(
            '-s', 'qdisc', 'show', 'dev', device)
        tc('filter', 'del', 'dev', device, 'parent', '1:', 'protocol', protocol,
           'pref', str(pref))

    def forward(frame, packet, ranges, name):
        # Forward only observed held receiver feedback; standard TCP has no packet ID.
        if name.startswith('reneged-sack'):
            assert packet['window'] == events[0]['window'] and packet['window']
        wire = tcp_packet(frame, packet['wire_seq'], b'', 0x10,
                            tsval=latest_ts.get('server'))
        base = packet['ack_base']
        blocks = [((base + ranges[0][0]) & MASK32,
                   (base + ranges[-1][1]) & MASK32)] if ranges else []
        wire = with_sack(wire, blocks)
        # Only this test socket carries the namespace-local classifier mark.
        filters = tc('-s', 'filter', 'show', 'dev', client_port, 'parent', '1:')
        (out / 'feedback-filter').write_text(filters)
        evidence['feedback_filter'] = filters
        path = out / (name + '.packet')
        path.write_bytes(wire)
        forwarded_at, forwarded_mono = time.time(), time.monotonic()
        assert tx.send(wire) == len(wire)
        events.append({'name': name, 'ordinal': len(events), 'ack': packet['ack'],
                       'actual_tsval': latest_ts.get('server'), 'time': forwarded_at,
                       'monotonic': forwarded_mono,
                       'source_packet_sha256': hashlib.sha256(frame).hexdigest(),
                       'source_flags': packet['flags'],
                       'window': packet['window']})
        (out / (name + '.source.packet')).write_bytes(frame)

    tc('filter', 'add', 'dev', client_port, 'parent', '1:', 'protocol', 'all',
       'pref', '1', 'handle', hex(injector_mark), 'fw', 'flowid', '1:1')
    (out / 'sack-monitor-ready').write_text('ready\n')
    try:
        while time.monotonic() < deadline:
            rx.settimeout(deadline - time.monotonic())
            frame, address = rx.recvfrom(65535)
            # Capture each real endpoint packet on ingress, before either bridge egress qdisc.
            if address[0] not in (client_port, server_port) or address[2] == socket.PACKET_OUTGOING:
                continue
            raw = wire_tcp(frame)
            if raw is None or (address[0] == client_port) != (raw['dport'] == 18181):
                continue
            packet = native_packet(frame, origins)
            if packet is None:
                continue
            now = time.monotonic()
            source = 'client' if packet['dport'] == 18181 else 'server'
            if 8 in packet['options']:
                latest_ts[source] = struct.unpack('!II', packet['options'][8])[0]
            protocol = 'ip' if frame[12:14] == b'\x08\x00' else 'ipv6'
            if source == 'client':
                for start, size, payload in packet['data']:
                    original = originals.setdefault(start, (size, payload, frame, packet, now))
                    if not primer or now == original[4]:
                        continue
                    if start == first_hole:
                        assert len(events) > 1, 'hole recovery preceded selective feedback'
                        evidence.setdefault('unclaimed_hole_retry', {
                            'offset': start, 'wire_seq': packet['wire_seq'],
                            'delay_from_original': now - originals[start][4],
                            'delay_from_first_false_sack': now - events[1]['monotonic']})
                    if claimed and start == claimed[0][0]:
                        assert fake_count == 3 and frontier_seen is not None
                        assert events[-1]['name'] == 'reneging-frontier'
                        assert now - events[-1]['monotonic'] >= 0.15
                        assert evidence['unclaimed_hole_retry']['delay_from_first_false_sack'] < 0.15
                        evidence['claimed_original_retry'] = {
                            'offset': start, 'length': size, 'wire_seq': packet['wire_seq'],
                            'delay_from_original': now - originals[start][4],
                            'delay_from_forwarded_frontier': now - events[-1]['monotonic'],
                            'cumulative_frontier_before_retry': frontier_seen}
                        release(server_port, 2)
                        release(client_port, 10)
                        break
                if 'claimed_original_retry' in evidence:
                    break
                continue
            # All these real packets are still held: only explicit forward() passes the bridge.
            dropped = sorted(k for k in originals if 4096 <= k <= 8191)
            if not primer and len(originals) >= 4 and len(dropped) >= 2 and \
                    packet['ack'] == dropped[0]:
                claimed = [(k, k + originals[k][0]) for k in dropped[1:]]
                assert all(a[1] == b[0] for a, b in zip(claimed, claimed[1:]))
                first_hole = dropped[0]
                iphead = 20 if protocol == 'ip' else 40
                proto_offset = 9 if protocol == 'ip' else 6
                for left, right in claimed:
                    tc('filter', 'add', 'dev', server_port, 'parent', '1:',
                       'protocol', protocol, 'pref', '2', 'u32',
                       'match', 'u8', '6', '0xff', 'at', str(proto_offset),
                       'match', 'u8', '0x08', '0x0e', 'at', str(iphead + 13),
                       'match', 'u32', hex((packet['ack_base'] + left) & MASK32),
                       '0xffffffff', 'at', str(iphead + 4), 'flowid', '1:3')
                release(server_port, 1)
                evidence.update({'fake_sack': claimed,
                                 'cumulative_ack_at_fake': packet['ack'],
                                 'originals_before_fake': len(originals)})
                forward(frame, packet, [], 'reneging-primer')
                primer = True
            elif primer and fake_count < 3 and packet['ack'] == first_hole:
                name = 'reneged-sack' if not fake_count else 'reneged-sack-' + str(fake_count)
                forward(frame, packet, claimed, name)
                if not fake_count:
                    evidence.update({'fake_ack': packet['ack'],
                                     'fake_sent_time': events[-1]['time'],
                                     'fake_actual_tsval': latest_ts.get('server')})
                fake_count += 1
            elif fake_count == 3 and packet['ack'] == claimed[0][0] and frontier_seen is None:
                forward(frame, packet, [], 'reneging-frontier')
                frontier_seen = packet['ack']
        else:
            raise RuntimeError('controlled SACK reneging did not reach retained-original recovery')
        evidence['result'] = 'fault evidence complete; exact application echo checked separately'
    finally:
        rx.close()
        tx.close()
        (out / 'sack-fault.json').write_text(json.dumps(evidence, indent=2) + '\n')


def sack_fault(directory, mode, client_port, server_port):
    """Private bridge monitor; release only after actual selective feedback evidence."""
    import socket
    import subprocess
    import time
    assert os.environ.get('ZXDP_RUN_FIXTURE') == '1'
    assert os.readlink('/proc/self/ns/net') != os.readlink('/proc/1/ns/net')
    assert mode in ('sack-holes', 'sack-reneging')
    if mode == 'sack-reneging':
        return reneging_fault(directory, client_port, server_port)
    out = Path(directory)
    rx = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(3))
    tx = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(3))
    tx.bind((server_port, 0))
    feedback = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(3))
    feedback.bind((client_port, 0))
    origins = WireOrigins()
    originals = {}
    injected = None
    evidence = {'mode': mode}
    protocol = None
    claimed = []
    last_ts = {}
    first_hole = None
    frontier_seen = None
    deadline = time.monotonic() + 22
    (out / 'sack-monitor-ready').write_text('ready\n')

    def tc(*args):
        return subprocess.run(['tc', *args], check=True, capture_output=True, text=True).stdout

    def release(preference, device=server_port):
        evidence['qdisc_before_release_' + str(preference)] = tc('-s', 'qdisc', 'show',
                                                               'dev', device)
        tc('filter', 'del', 'dev', device, 'parent', '1:', 'protocol', protocol,
           'pref', str(preference))

    try:
        while time.monotonic() < deadline:
            rx.settimeout(deadline - time.monotonic())
            frame, address = rx.recvfrom(65535)
            # Observe endpoint ingress BEFORE the bridge egress loss classifiers.
            if address[0] not in (client_port, server_port) or \
                    address[2] == socket.PACKET_OUTGOING:
                continue
            raw = wire_tcp(frame)
            if raw is None or (address[0] == client_port) != (raw['dport'] == 18181):
                continue
            packet = native_packet(frame, origins)
            if packet is None:
                continue
            now = time.monotonic()
            source = 'client' if packet['dport'] == 18181 else 'server'
            if 8 in packet['options']:
                last_ts[source] = struct.unpack('!II', packet['options'][8])[0]
            protocol = 'ip' if frame[12:14] == b'\x08\x00' else 'ipv6'
            if source == 'client':
                for start, size, payload in packet['data']:
                    originals.setdefault(start, (size, payload, frame, packet, now))

                continue
            if mode == 'sack-holes':
                if injected is None and len(originals) >= 4 and len(packet['sack']) >= 2:
                    # Fast RACK may reduce cwnd before a second hole is formed.
                    # Let the initial flight form both holes without feedback, then
                    # release the real selective ACK before duplicate/recovery work.
                    (out / 'two-hole-feedback.packet').write_bytes(frame)
                    evidence['feedback_source_sha256'] = hashlib.sha256(frame).hexdigest()
                    evidence['feedback_release_time'] = time.time()
                    evidence['feedback_gate_filter'] = tc('-s', 'filter', 'show',
                        'dev', client_port, 'parent', '1:')
                    release(10, client_port)
                    assert feedback.send(frame) == len(frame)
                    start = next(k for k in sorted(originals) if 0 < k < 4096)
                    size, payload, template, original, unused = originals[start]
                    duplicate = tcp_packet(template, original['wire_seq'],
                                           original['body'], original['flags'],
                                             tsval=last_ts.get('client'))
                    (out / 'duplicate.packet').write_bytes(duplicate)
                    assert tx.send(duplicate) == len(duplicate)
                    injected = (start, start + size)
                    evidence.update({'originals_before_duplicate': len(originals),
                                     'two_hole_sack': packet['sack'],
                                     'duplicate_range': injected,
                                     'duplicate_actual_tsval': last_ts.get('client')})
                elif injected is not None and packet['sack'] and \
                        tuple(packet['sack'][0]) == injected:
                    first = packet['sack'][0]
                    dsack = first[1] <= packet['ack'] or any(
                        block[0] <= first[0] and first[1] <= block[1]
                        for block in packet['sack'][1:])
                    if dsack:
                        evidence['dsack'] = packet['sack']
                        evidence['dsack_ack'] = packet['ack']
                        release(1)
                        break
        else:
            raise RuntimeError('no actual SACK/DSACK or reneged-original recovery evidence')
    finally:
        rx.close()
        tx.close()
        feedback.close()
    evidence['result'] = 'fault evidence complete; exact application echo checked separately'
    (out / 'sack-fault.json').write_text(json.dumps(evidence, indent=2) + '\n')


def sack_check(directory, mode):
    out = Path(directory)
    check(directory)
    assert all(int(os.environ['ZUTCP_TEST_' + side + '_OPTIONS']) & 1
               for side in ('CLIENT', 'SERVER'))
    fault = json.loads((out / 'sack-fault.json').read_text())
    assert fault['mode'] == mode
    seen = {'client': [], 'server': []}
    origins = WireOrigins()
    frames = sorted((when, frame) for target in ('client', 'server')
                    for when, frame in pcap_frames(out / ('to-' + target + '.pcap')))
    for when, frame in frames:
        packet = native_packet(frame, origins)
        if packet:
            packet['time'] = when
            seen['client' if packet['dport'] == 18181 else 'server'].append(packet)
    flow = next(iter(origins.flows.values()))
    bases = [(flow['client_isn'] + 1) & MASK32, (flow['server_isn'] + 1) & MASK32]
    assert [int(v) for v in (out / 'wire-bases').read_text().split()] == bases
    expected_bands = []
    lo, hi = (bases[0] + 4096) & MASK32, (bases[0] + 8191) & MASK32
    for lo, hi in (((lo, hi),) if lo <= hi else ((lo, MASK32), (0, hi))):
        while lo <= hi:
            size = lo & -lo if lo else 1 << 32
            while size > hi - lo + 1:
                size >>= 1
            expected_bands.append((lo, MASK32 ^ (size - 1)))
            lo += size
    if mode == 'sack-holes':
        expected_bands.append((bases[0], MASK32))
    actual_bands = [tuple(int(v, 16) for v in line.split())
                    for line in (out / 'sack-seq-bands').read_text().splitlines()]
    assert actual_bands == expected_bands
    import re
    drops = [int(v) for value in fault.values() if isinstance(value, str)
             for v in re.findall(r'dropped (\d+)', value)]
    assert drops and max(drops) > 0, fault
    expected = (out / 'expected').read_bytes()
    request = expected[len(b'echo: '):-1]
    covered = bytearray(len(request))
    for packet in seen['client']:
        for start, size, payload in packet['data']:
            assert payload == request[start:start + size]
            covered[start:start + size] = b'\1' * size
    assert covered and all(covered) and len({p['seq'] for p in seen['client'] if p['data']}) >= 4
    if mode == 'sack-holes':
        feedback = (out / 'two-hole-feedback.packet').read_bytes()
        assert hashlib.sha256(feedback).hexdigest() == fault['feedback_source_sha256']
        held = native_packet(feedback, origins)
        assert len(held['sack']) >= 2
        released = fault['feedback_release_time']
        assert not any(p['time'] < released for p in seen['server'])
        assert any(frame == feedback and when >= released for when, frame in frames)
        assert any(len(p['sack']) >= 2 for p in seen['server'])
        assert any(p['sack'] and list(p['sack'][0]) == fault['duplicate_range']
                   for p in seen['server'])
        injected = (out / 'duplicate.packet').read_bytes()
        assert any(frame == injected for unused, frame in frames)
    else:
        injected = (out / 'reneged-sack.packet').read_bytes()
        assert any(frame == injected for unused, frame in frames)
        retry = fault['claimed_original_retry']
        assert retry['offset'] == fault['fake_sack'][0][0]
        assert retry['cumulative_frontier_before_retry'] == retry['offset']
        assert retry['delay_from_forwarded_frontier'] >= 0.15
        assert fault['unclaimed_hole_retry']['delay_from_first_false_sack'] < 0.15
        feedback = fault['controlled_feedback']
        assert [event['name'] for event in feedback] == [
            'reneging-primer', 'reneged-sack', 'reneged-sack-1',
            'reneged-sack-2', 'reneging-frontier']
        assert [event['ordinal'] for event in feedback] == list(range(len(feedback)))
        for event in feedback:
            source = (out / (event['name'] + '.source.packet')).read_bytes()
            forwarded = (out / (event['name'] + '.packet')).read_bytes()
            a, b = native_packet(source, origins), native_packet(forwarded, origins)
            assert not any(frame == source and when < event['time']
                           for when, frame in frames)
            assert a['wire_seq'] == b['wire_seq'] and a['wire_ack'] == b['wire_ack']
            assert a['options'].get(8) == b['options'].get(8)
            assert b['flags'] == 0x10 and not b['data']
            assert any(frame == forwarded for unused, frame in frames)
        selective = (out / 'reneged-sack.packet').read_bytes()
        selective_time = next(t for t, frame in frames if frame == selective)
        first_hole = fault['cumulative_ack_at_fake']
        first_hole_time = min(p['time'] for p in seen['client']
                              if any(start == first_hole for start, size, data in p['data']))
        assert first_hole_time > selective_time
        claimed = {left for left, unused in fault['fake_sack']}
        assert not any(p['time'] < fault['fake_sent_time'] and
                       any(start in claimed for start, unused, payload in p['data'])
                       for p in seen['client']), 'claimed originals were never stored at receiver'
    fault.update({'exact_request_bytes': len(request), 'exact_echo': True,
                  'request_coverage': len(covered), 'result': 'PASS'})
    (out / 'sack-evidence.json').write_text(json.dumps(fault, indent=2) + '\n')
    print('# native selective recovery:', json.dumps(fault, sort_keys=True))


def tail_fault(directory):
    import socket
    import time
    out = Path(directory)
    assert os.environ.get('ZXDP_RUN_FIXTURE') == '1'
    assert os.readlink('/proc/self/ns/net') != os.readlink('/proc/1/ns/net')
    rx = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(3))
    # Bounded fixture observation reserve: the request/echo burst also charges skb metadata.
    reserve = 4 * 1024 * 1024
    rx.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, reserve)
    forced = rx.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF) < reserve
    if forced:
        rx.setsockopt(socket.SOL_SOCKET, getattr(socket, 'SO_RCVBUFFORCE', 33), reserve)
    actual = rx.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF)
    assert actual >= reserve, ('tail observer receive reserve unavailable', reserve, actual)
    overflow_option = getattr(socket, 'SO_RXQ_OVFL', 40)
    rx.setsockopt(socket.SOL_SOCKET, overflow_option, 1)
    assert rx.getsockopt(socket.SOL_SOCKET, overflow_option) == 1
    observer = {'receive_requested': reserve, 'receive_actual': actual,
                'receive_force': forced, 'overflow_enabled': True,
                'overflow_reported': 0, 'overflow_messages': 0, 'frames': 0}
    (out / 'tail-observer.json').write_text(json.dumps(observer, indent=2) + '\n')
    rx.bind(('zutcpca', 0))
    tx = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(3))
    tx.setsockopt(socket.SOL_SOCKET, socket.SO_MARK, 0x5a18)
    tx.bind(('zutcpsa', 0))
    origins = WireOrigins()
    first = None
    probe = None
    deadline = time.monotonic() + 20
    (out / 'tail-monitor-ready').write_text('ready\n')
    try:
        while time.monotonic() < deadline:
            rx.settimeout(deadline - time.monotonic())
            frame, ancillary, flags, _ = rx.recvmsg(65535, socket.CMSG_SPACE(4))
            observer['frames'] += 1
            assert not flags & (socket.MSG_TRUNC | socket.MSG_CTRUNC), 'tail observer truncation'
            for level, kind, value in ancillary:
                if level == socket.SOL_SOCKET and kind == overflow_option:
                    assert len(value) == 4
                    observer['overflow_messages'] += 1
                    observer['overflow_reported'] = struct.unpack('=I', value)[0]
            assert observer['overflow_reported'] == 0, ('tail observer lost input', observer)
            packet = native_packet(frame, origins)
            if packet is None:
                continue
            now = time.monotonic()
            if packet['dport'] == 18181 and packet['data'] and \
                    packet['seq'] + len(packet['body']) == 81600:
                start, size, payload = packet['data'][-1]
                assert start + size == 81600
                if first is None:
                    first = (now, packet, payload, time.time())
                    (out / 'tail-original.packet').write_bytes(frame)
                elif probe is None:
                    assert payload == first[2] and packet['seq'] == first[1]['seq']
                    assert 0 < now - first[0] < .15, 'tail recovery waited for cumulative RTO'
                    # Forward exactly the real probe that the bridge loss rule withheld.
                    (out / 'tail-probe.packet').write_bytes(frame)
                    probe = (now, packet, time.time())
                    assert tx.send(frame) == len(frame)
            elif probe and packet['sport'] == 18181 and packet['ack'] >= 81601:
                assert now - first[0] < .15
                result = {'observer': observer.copy(), 'original_time': first[0], 'probe_time': probe[0],
                          'original_wire_time': first[3], 'probe_wire_time': probe[2],
                          'offset': first[1]['seq'], 'length': len(first[2]),
                          'probe_delay': probe[0] - first[0],
                          'ack_delay': now - first[0], 'cumulative_ack': packet['ack'],
                          'payload_sha256': hashlib.sha256(first[2]).hexdigest(),
                          'peer_ack_allowance_us': 0, 'result': 'probe acknowledged'}
                (out / 'tail-fault.json').write_text(json.dumps(result, indent=2) + '\n')
                return
        raise RuntimeError('no acknowledged early tail probe')
    finally:
        (out / 'tail-observer.json').write_text(json.dumps(observer, indent=2) + '\n')
        rx.close()
        tx.close()


def tail_check(directory):
    import re
    out = Path(directory)
    check(directory)
    result = json.loads((out / 'tail-fault.json').read_text())
    observer = json.loads((out / 'tail-observer.json').read_text())
    assert observer['overflow_enabled'] and observer['overflow_reported'] == 0
    assert observer['receive_actual'] >= observer['receive_requested'] == 4 * 1024 * 1024
    assert result['observer'] == observer
    probe = (out / 'tail-probe.packet').read_bytes()
    original = (out / 'tail-original.packet').read_bytes()
    delivered = list(pcap_frames(out / 'to-server.pcap'))
    producer, tail = [int(value) for value in (out / 'tail-producer').read_text().split()]
    assert result['offset'] == tail and tail == (81600 - 1) // producer * producer
    assert result['offset'] + result['length'] == 81600
    assert any(frame == probe for _, frame in delivered)
    assert not any(frame == original and when < result['probe_wire_time']
                   for when, frame in delivered), 'original tail was not lost'
    drops = [int(n) for n in re.findall(r'dropped (\d+)', (out / 'tail-loss').read_text())]
    assert drops and max(drops) >= 1
    assert result['original_time'] < result['probe_time']
    assert 0 < result['probe_delay'] < result['ack_delay'] < .15
    result.update(result='PASS', exact_request_bytes=81600, exact_echo=True)
    (out / 'tail-evidence.json').write_text(json.dumps(result, indent=2) + '\n')
    print('# native tail probe:', json.dumps(result, sort_keys=True))


def ecn_check(directory):
    out = Path(directory)
    check(directory)
    profiles = [int(os.environ['ZUTCP_TEST_' + side + '_OPTIONS'])
                for side in ('CLIENT', 'SERVER')]
    negotiated = bool(profiles[0] & profiles[1] & 8)
    origins = WireOrigins()
    packets = {'client': [], 'server': []}
    frames = sorted((when, frame) for side in ('client', 'server')
                    for when, frame in pcap_frames(out / ('to-' + side + '.pcap')))
    originals = {'client': set(), 'server': set()}
    for when, frame in frames:
        raw = wire_tcp(frame)
        if raw is None:
            continue
        source = 'client' if raw['dport'] == 18181 else 'server'
        mark = frame[15] & 3 if frame[12:14] == b'\x08\x00' else (frame[15] >> 4) & 3
        packet = native_packet(frame, origins)
        raw.update(time=when, ecn=mark)
        if raw['flags'] & 2:
            offered = bool(profiles[0] & 8) if source == 'client' else negotiated
            expected = 0xc0 if source == 'client' else 0x40
            assert raw['flags'] & 0xc0 == (expected if offered else 0)
            assert not mark
        elif packet:
            raw.update(packet)
            data = packet['data']
            new = bool(data) and all(start not in originals[source] for start, _, _ in data)
            if negotiated:
                assert mark in (2, 3) if new else mark == 0
            else:
                assert not mark and not raw['flags'] & 0xc0
            if raw['flags'] & 0x80:
                assert new, 'CWR belongs to accepted new DATA'
            for start, _, _ in data:
                originals[source].add(start)
        packets[source].append(raw)
    ce = [p for p in packets['client'] if p['ecn'] == 3]
    ece = [p for p in packets['server'] if p['flags'] & 0x40 and not p['flags'] & 2]
    cwr = [p for p in packets['client'] if p['flags'] & 0x80 and not p['flags'] & 2]
    if negotiated:
        assert len(ce) == 1 and ece and cwr
        assert ce[0]['seq'] == 0 and ce[0]['time'] < ece[0]['time'] < cwr[0]['time']
        end = max(start + size for start, size, _ in cwr[0]['data'])
        cleared = [p for p in packets['server'] if p['time'] > cwr[0]['time']
                   and p.get('ack', 0) >= end and not p['flags'] & 0x40]
        assert cleared, 'unmarked CWR must clear the peer ECE latch'
    else:
        assert not ce and not ece and not cwr
    result = {'profiles': profiles, 'negotiated': negotiated, 'ce_packets': len(ce),
              'ece_packets': len(ece), 'cwr_packets': len(cwr),
              'exact_request_bytes': 81600, 'exact_echo': True, 'result': 'PASS'}
    (out / 'ecn-evidence.json').write_text(json.dumps(result, indent=2) + '\n')
    print('# native classic ECN:', json.dumps(result, sort_keys=True))


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == 'tail-fault':
        tail_fault(sys.argv[2])
        sys.exit(0)
    if len(sys.argv) > 1 and sys.argv[1] == 'tail-check':
        tail_check(sys.argv[2])
        sys.exit(0)
    if len(sys.argv) > 1 and sys.argv[1] == 'ecn':
        ecn_check(sys.argv[2])
        sys.exit(0)
    if sys.argv[1] == 'fault':
        sack_fault(*sys.argv[2:])
    elif sys.argv[1] == 'selective':
        sack_check(*sys.argv[2:])
    else:
        check(sys.argv[1])
