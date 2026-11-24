#!/usr/bin/env python3
# (c) Copyright 2026 Huw Rogers
# This code is licensed by the MIT license (see LICENSE for details).

"""Independent native TFO option/bootstrap/data evidence for the private fixture."""

import hashlib
import json
import re
import sys
from pathlib import Path

from zutcprecovery import MASK32, pcap_frames, wire_tcp




def checksum(data):
    if len(data) & 1:
        data += b'\0'
    total = sum(int.from_bytes(data[i:i + 2], 'big') for i in range(0, len(data), 2))
    while total >> 16:
        total = (total & 65535) + (total >> 16)
    return (~total) & 65535


def checked_frame(frame, packet):
    source, unused, target, unused = packet['key']
    addresses = bytes.fromhex(source) + bytes.fromhex(target)
    tcp = packet['tcp']
    if len(addresses) == 8:
        ip = frame[14:]
        assert checksum(ip[:(ip[0] & 15) * 4]) == 0
        pseudo = addresses + b'\0\6' + len(tcp).to_bytes(2, 'big')
    else:
        pseudo = addresses + len(tcp).to_bytes(4, 'big') + b'\0\0\0\6'
    assert checksum(pseudo + tcp) == 0, ('TCP checksum', packet['key'])


def options(tcp):
    end = (tcp[12] >> 4) * 4
    offset = 20
    cookie = None
    while offset < end:
        kind = tcp[offset]
        offset += 1
        if kind == 0:
            break
        if kind == 1:
            continue
        assert offset < end
        length = tcp[offset]
        offset += 1
        assert length >= 2 and offset + length - 2 <= end
        if kind == 34:
            assert cookie is None and (length == 2 or
                                       6 <= length <= 18 and length % 2 == 0)
            cookie = tcp[offset:offset + length - 2]
        offset += length - 2
    return cookie






def packets(directory, source=None):
    result = []
    sides = ('server',) if source == 'client' else (
        ('client',) if source == 'server' else ('server', 'client'))
    for side in sides:
        name = 'tfo-' + source + '-source' if source else 'to-' + side
        capture = (directory / (name + '.capture')).read_text()
        assert '0 packets dropped by kernel' in capture
        for stamp, frame in pcap_frames(directory / (name + '.pcap')):
            packet = wire_tcp(frame)
            if packet is None:
                continue
            checked_frame(frame, packet)
            assert not packet['flags'] & 4, ('reset', side, packet)
            packet['time'] = stamp
            packet['side'] = side
            packet['cookie'] = options(packet['tcp'])
            result.append(packet)
    return sorted(result, key=lambda p: p['time'])


def reverse_key(key):
    return key[2:] + key[:2]


def check_synacks(flow, attempts, expected_ack, client_fin_time):
    for index, p in enumerate(attempts):
        assert p['wire_seq'] == flow['server_isn'] and not p['body']
        if p['wire_ack'] == expected_ack:
            continue
        # A late retry can also cover the FIN delivered after the SYN data.
        assert (index > 0 and client_fin_time is not None and
                client_fin_time <= p['time'] and
                p['wire_ack'] == (expected_ack + 1) & MASK32), (
                    'unexpected SYN-ACK acknowledgment', flow['key'], index,
                    p['wire_ack'], expected_ack, p['time'], client_fin_time)


def check(directory):
    profile_path = directory / 'tfo-profile'
    profile = profile_path.read_text().strip() if profile_path.exists() else 'authorized'
    assert profile in ('authorized', 'sender-denied', 'receiver-denied', 'stale',
                       'setup-loss', 'setup-duplicate', 'disabled-service', 'response-loss')
    count = 3 if profile == 'stale' else 2
    request = (directory / 'request').read_bytes()
    assert 0 < len(request) <= 400
    captured = packets(directory)
    flows = []
    for p in captured:
        if p['side'] != 'server' or p['flags'] & 0x17 != 2:
            continue
        identity = p['key'], p['wire_seq']
        flow = next((f for f in flows if f['identity'] == identity), None)
        if flow is None:
            flow = {'identity': identity, 'key': p['key'], 'syn': [], 'synack': [],
                    'client_isn': p['wire_seq']}
            flows.append(flow)
        assert p['wire_seq'] == flow['client_isn']
        flow['syn'].append(p)
    assert len(flows) == count, ('actual connection count', len(flows), profile)
    for flow in flows[1:]:
        assert flows[0]['key'][0] == flow['key'][0]
        assert flows[0]['key'][2:] == flow['key'][2:]
    for index, flow in enumerate(flows):
        reverse = reverse_key(flow['key'])
        for p in captured:
            if p['side'] == 'client' and p['key'] == reverse and p['flags'] & 0x17 == 0x12:
                flow['synack'].append(p)
        assert flow['synack']
        flow['server_isn'] = flow['synack'][0]['wire_seq']
        for p in flow['synack']:
            if profile == 'response-loss' and index == 1:
                assert p['cookie'] in (None, flows[0]['synack'][0]['cookie'])
            elif profile == 'disabled-service' or (index == 1 and profile in (
                    'sender-denied', 'setup-loss')):
                assert p['cookie'] is None
            else:
                assert p['cookie'] is not None and len(p['cookie']) == 16
    if profile == 'stale':
        assert flows[0]['synack'][0]['cookie'] != flows[1]['synack'][0]['cookie']
        assert flows[1]['synack'][0]['cookie'] == flows[2]['synack'][0]['cookie']
    evidence = []
    for index, flow in enumerate(flows):
        early_sent = profile not in ('sender-denied', 'disabled-service') and index != 0
        fallback_syn = profile == 'setup-loss' and index == 1
        early = early_sent and profile != 'receiver-denied' and not fallback_syn and not (
            profile == 'stale' and index == 1)
        expected_ack = (flow['client_isn'] + 1 + (len(request) if early else 0)) & MASK32
        cookie = flows[index - 1]['synack'][0]['cookie'] if early_sent else b''
        for attempt, p in enumerate(flow['syn']):
            ordinary_retry = profile == 'response-loss' and index == 1 and attempt != 0
            if fallback_syn or ordinary_retry or (profile == 'sender-denied' and index == 1):
                assert p['cookie'] is None
            else:
                assert p['cookie'] == cookie
            syn_data = early_sent and not fallback_syn and not ordinary_retry
            assert p['body'] == (request if syn_data else b'')
            assert not p['flags'] & 1
        client_fin_time = None
        for side, expected, key, base in (
                ('server', request, flow['key'], flow['client_isn'] + 1),
                ('client', b'echo: ' + request, reverse_key(flow['key']),
                 flow['server_isn'] + 1)):
            seen = bytearray(len(expected))
            fin = False
            acknowledged = False
            for p in captured:
                if p['side'] != side or p['key'] != key or p['flags'] & 2:
                    continue
                first = (p['wire_seq'] - base) & MASK32
                data = p['body']
                if data:
                    assert first + len(data) <= len(expected)
                    assert data == expected[first:first + len(data)]
                    seen[first:first + len(data)] = b'\1' * len(data)
                if p['flags'] & 1:
                    assert first + len(data) == len(expected)
                    if (side == 'server' and profile == 'response-loss' and index == 1
                            and client_fin_time is None):
                        assert p['flags'] & 16
                        client_fin_time = p['time']
                    fin = True
                peer_base = flow['server_isn'] + 1 if side == 'server' else (
                    flow['client_isn'] + 1)
                peer_size = len(request) + (6 if side == 'server' else 0)
                if p['flags'] & 16 and p['wire_ack'] == (peer_base + peer_size + 1) & MASK32:
                    acknowledged = True
            if early and side == 'server':
                # The preceding SYN-data equality and full SYNACK byte ACK prove coverage.
                seen[:] = b'\1' * len(expected)
            assert all(seen) and fin and acknowledged, (
                'stream coverage/FIN/terminalACK', index, side, profile)
        check_synacks(flow, flow['synack'], expected_ack, client_fin_time)
        evidence.append({'connection': index + 1, 'early_sent': early_sent, 'early': early,
                         'fallback_syn': fallback_syn,
                         'syns': len(flow['syn']), 'synacks': len(flow['synack']),
                         'cookie': (flow['syn'][0]['cookie'] or b'').hex(),
                         'bytes': len(request)})
        if profile == 'response-loss' and index == 1:
            source = [p for p in packets(directory, source='server')
                      if p['flags'] & 0x17 == 0x12 and p['key'] == reverse_key(flow['key'])]
            assert len(source) >= 2
            check_synacks(flow, source, expected_ack, client_fin_time)
            assert source[0]['cookie'] == cookie
            assert all(p['cookie'] in (None, cookie) for p in source[1:])
            # Identical retries are possible. Both server attempts must precede
            # the first delivery, and the fault filter must have recorded a drop.
            response_time = flow['synack'][0]['time']
            assert source[0]['time'] < source[1]['time'] <= response_time
            assert re.search(r'dropped [1-9][0-9]*', (directory / 'tfo-fault').read_text())
            retries = flow['syn'][1:]
            winner = 'client-retry' if retries and retries[0]['time'] < response_time else (
                'server-response')
            evidence[index].update(response_source_synacks=len(source),
                                   dropped_synack_time=source[0]['time'],
                                   recovered_synack_time=response_time,
                                   ordinary_syn_retries=len(retries), recovery_first=winner)
    if profile == 'setup-loss':
        source = [p for p in packets(directory, source='client')
                  if p['flags'] & 0x17 == 2 and p['key'] == flows[1]['key'] and
                  p['wire_seq'] == flows[1]['client_isn']]
        assert len(source) >= 2 and source[0]['time'] < flows[1]['syn'][0]['time']
        assert source[0]['cookie'] == flows[0]['synack'][0]['cookie']
        assert source[0]['body'] == request
        for p in source:
            assert p['wire_seq'] == flows[1]['client_isn']
        for p in source[1:]:
            assert p['cookie'] is None and not p['body']
        fault = (directory / 'tfo-fault').read_text()
        assert re.search(r'dropped [1-9][0-9]*', fault)
        evidence[1]['source_syns'] = len(source)
    if profile == 'setup-duplicate':
        assert len(flows[1]['syn']) > len({p['tcp'] for p in flows[1]['syn']})
        assert 'duplicate 100%' in (directory / 'tfo-fault').read_text()
    client = (directory / 'client.err').read_text()
    server = (directory / 'server.err').read_text()
    for index, flow in enumerate(flows):
        number = str(index + 1)
        assert len(re.findall(r'^tfo queued=' + number + r'$', client, re.M)) == 1
        assert len(re.findall(r'^tfo connected=' + number + r'$', client, re.M)) == 1
        assert client.index('tfo queued=' + number + '\n') < client.index(
            'tfo connected=' + number + '\n')
        early = evidence[index]['early']
        row = ('tfo process=' + number + ' early=' + str(int(early)) +
               ' bytes=' + str(len(request)))
        assert len(re.findall('^' + re.escape(row) + '$', server, re.M)) == 1
        connected = server.index('tfo connected=' + number + '\n')
        processed = server.index(row + '\n')
        assert (processed < connected) == early
        accepted = len(re.findall(r'^tfo accepted=' + number + r'$', client, re.M))
        assert accepted == int(early)
        if evidence[index]['early_sent'] and not early:
            assert len(re.findall(r'^tfo rejected=' + number + r'$', client, re.M)) == 1
        if profile == 'response-loss' and index == 1:
            assert not re.findall(r'^tfo rejected=' + number + r'$', client, re.M)
    assert (directory / 'client.out').read_bytes() == (b'echo: ' + request + b'\n') * count
    report = {'mode': 'tfo', 'profile': profile, 'same_process_connections': count,
              'connections': evidence, 'request_sha256': hashlib.sha256(request).hexdigest()}
    (directory / 'tfo-evidence.json').write_text(json.dumps(report, indent=2) + '\n')
    print('# TFO ' + profile + ': exact retained bytes, callbacks, ordinary FIN and echo')


if __name__ == '__main__':
    check(Path(sys.argv[1]))
