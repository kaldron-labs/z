#!/usr/bin/env python3
# (c) Copyright 2026 Huw Rogers
# This code is licensed by the MIT license (see LICENSE for details).

"""Private fixture faults and assertions for standard TCP/IP carriage.
This helper runs separately from the library and is never a production I/O path.
"""

import hashlib
import json
import os
import socket
import struct
import sys
import time
from pathlib import Path

MASK32 = (1 << 32) - 1


def wire_tcp(frame):
    """Decode standard TCP/IP bytes; application payload has no inner framing."""
    if len(frame) < 14:
        return None
    ip = frame[14:]
    if frame[12:14] == b'\x08\x00' and len(ip) >= 20 and ip[9] == 6:
        head, length = (ip[0] & 15) << 2, int.from_bytes(ip[2:4], 'big')
        assert ip[0] >> 4 == 4 and 20 <= head <= length <= len(ip)
        assert not int.from_bytes(ip[6:8], 'big') & 0x3fff
        assert abort_checksum(ip[:head]) == 0
        tcp = ip[head:length]
        source, target = ip[12:16], ip[16:20]
        pseudo = source + target + b'\0\6' + len(tcp).to_bytes(2, 'big')
    elif frame[12:14] == b'\x86\xdd' and len(ip) >= 40 and ip[6] == 6:
        length = int.from_bytes(ip[4:6], 'big')
        assert ip[0] >> 4 == 6 and 40 + length <= len(ip)
        tcp = ip[40:40 + length]
        source, target = ip[8:24], ip[24:40]
        pseudo = source + target + len(tcp).to_bytes(4, 'big') + b'\0\0\0\6'
    else:
        return None
    assert len(tcp) >= 20 and abort_checksum(pseudo + tcp) == 0
    head = (tcp[12] >> 4) << 2
    assert 20 <= head <= min(60, len(tcp))
    sport, dport, seq, ack = struct.unpack('!HHII', tcp[:12])
    return {'key': (source.hex(), sport, target.hex(), dport),
            'tcp': tcp, 'body': tcp[head:], 'options': tcp[20:head].hex(),
            'sport': sport, 'dport': dport, 'wire_seq': seq, 'wire_ack': ack,
            'flags': tcp[13], 'window': int.from_bytes(tcp[14:16], 'big')}


def pcap_frames(path):
    raw = Path(path).read_bytes()
    formats = {b'\xd4\xc3\xb2\xa1': ('<', 1000000), b'M<\xb2\xa1': ('<', 1000000000),
               b'\xa1\xb2\xc3\xd4': ('>', 1000000), b'\xa1\xb2<M': ('>', 1000000000)}
    endian, scale = formats[raw[:4]]
    assert struct.unpack(endian + 'I', raw[20:24])[0] == 1
    pos = 24
    while pos < len(raw):
        sec, sub, cap, wire = struct.unpack(endian + 'IIII', raw[pos:pos + 16])
        pos += 16
        frame = raw[pos:pos + cap]
        pos += cap
        assert cap == wire and len(frame) == cap
        yield sec + sub / scale, frame


class WireOrigins:
    """Tuple and SYN sequence origins; no fabricated packet/CID identity."""
    def __init__(self):
        self.flows = {}

    @staticmethod
    def reverse(key):
        return key[2:] + key[:2]

    def observe(self, frame):
        packet = wire_tcp(frame)
        if packet is None or not packet['flags'] & 2:
            return
        key, flags = packet['key'], packet['flags']
        if flags & 0x17 == 2:
            assert packet['wire_ack'] == 0
            identity = (key, packet['wire_seq'])
            flow = self.flows.setdefault(identity, {
                'client': key, 'client_isn': packet['wire_seq'], 'syns': 0,
                'synacks': 0, 'final_acks': 0, 'early_bytes': len(packet['body'])})
            flow['syns'] += 1
            return
        assert flags & 0x17 == 0x12, packet
        candidates = [f for f in self.flows.values()
                      if f['client'] == self.reverse(key) and
                      (packet['wire_ack'] - f['client_isn'] - 1) & MASK32 <= f['early_bytes']]
        assert len(candidates) == 1, (packet, candidates)
        flow = candidates[0]
        if flow['synacks']:
            assert flow['server_isn'] == packet['wire_seq'], (flow, packet)
        flow['server_isn'] = packet['wire_seq']
        flow['synacks'] += 1

    def relative(self, frame):
        packet = wire_tcp(frame)
        if packet is None or packet['flags'] & 2:
            return None
        matches = []
        for flow in self.flows.values():
            if not flow['synacks']:
                base = (flow['client_isn'] + 1) & MASK32
                if packet['key'] == flow['client'] and packet['flags'] & 0x14 == 4:
                    seq = (packet['wire_seq'] - base) & MASK32
                    matches.append((seq, seq, 0, base, None))
                elif (packet['key'] == self.reverse(flow['client']) and
                      packet['flags'] & 0x14 == 0x14 and packet['wire_ack'] == base):
                    # SYN-SENT RST qualifies only by the known local SYN ACK.
                    # Its sender has not supplied a peer sequence origin.
                    matches.append((0, None, 0, None, base))
                continue
            server = packet['key'] == self.reverse(flow['client'])
            if not server and packet['key'] != flow['client']:
                continue
            seq_base = (flow['server_isn' if server else 'client_isn'] + 1) & MASK32
            ack_base = (flow['client_isn' if server else 'server_isn'] + 1) & MASK32
            seq, ack = (packet['wire_seq'] - seq_base) & MASK32, 0
            if packet['flags'] & 16:
                ack = (packet['wire_ack'] - ack_base) & MASK32
            # Current scripted streams fit below halfspace. Persist uses NXT-1.
            distance = min(seq, (seq + 1) & MASK32) + ack
            matches.append((distance, seq, ack, seq_base, ack_base))
        assert matches, ('TCP segment lacks captured SYN origin', packet)
        matches.sort(key=lambda row: row[0])
        if len(matches) > 1:
            assert matches[0][0] != matches[1][0], ('ambiguous incarnation', packet)
        _, seq, ack, seq_base, ack_base = matches[0]
        packet.update(seq=seq, ack=ack, seq_base=seq_base, ack_base=ack_base)
        return packet

    def final_ack(self, frame):
        packet = wire_tcp(frame)
        if packet is None or packet['flags'] & 0x17 != 0x10:
            return
        for flow in self.flows.values():
            if (flow['client'] == packet['key'] and flow['synacks'] and
                    packet['wire_seq'] == (flow['client_isn'] + 1) & MASK32 and
                    packet['wire_ack'] == (flow['server_isn'] + 1) & MASK32):
                flow['final_acks'] += 1

    def evidence(self):
        return list(self.flows.values())


def capture_origins(path):
    frames = [frame for capture in sorted(Path(path).parent.glob('*.pcap'))
              for _, frame in pcap_frames(capture)]
    origins = WireOrigins()
    # Direction-only captures may list SYNACK before the other file's SYN.
    for frame in frames:
        packet = wire_tcp(frame)
        if packet is not None and packet['flags'] & 0x17 == 0x02:
            origins.observe(frame)
    for frame in frames:
        packet = wire_tcp(frame)
        if packet is not None and packet['flags'] & 0x17 == 0x12:
            origins.observe(frame)
    for frame in frames:
        origins.final_ack(frame)
    assert origins.flows, ('no captured TCP SYN bootstrap', path)
    return origins


def wire_gate(directory, interface, port):
    out = Path(directory)
    rx = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(3))
    rx.bind((interface, 0))
    origins = WireOrigins()
    (out / 'wire-monitor-ready').write_text('ready\n')
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        rx.settimeout(deadline - time.monotonic())
        origins.observe(rx.recv(65535))
        ready = [flow for flow in origins.flows.values()
                 if flow['client'][3] == int(port) and flow['synacks']]
        if ready:
            assert len(ready) == 1, ready
            flow = ready[0]
            values = ((flow['client_isn'] + 1) & MASK32, (flow['server_isn'] + 1) & MASK32)
            (out / 'wire-origins.json').write_text(json.dumps(flow, indent=2) + '\n')
            with (out / 'wire-origins').open('w') as gate:
                gate.write('%d %d\n' % values)
            rx.close()
            return
    raise RuntimeError('no actual SYN/SYNACK for application fault gate')


def ack_bands(base, first, last):
    lo, hi = (int(base) + int(first)) & MASK32, (int(base) + int(last)) & MASK32
    intervals = ((lo, hi),) if lo <= hi else ((lo, MASK32), (0, hi))
    for lo, hi in intervals:
        while lo <= hi:
            size = lo & -lo if lo else 1 << 32
            while size > hi - lo + 1:
                size >>= 1
            print('0x%08x 0x%08x' % (lo, MASK32 ^ (size - 1)))
            lo += size


def syn_evidence(directory, retransmit=False):
    out = Path(directory)
    origins = capture_origins(out / 'bootstrap-client.pcap')
    paired = [flow for flow in origins.evidence() if flow['synacks']]
    assert paired and all(flow['final_acks'] for flow in paired), paired
    counts = {}
    for side, syn_flags in (('client', 2), ('server', 0x12)):
        capture = out / ('bootstrap-' + side + '.pcap')
        source = [wire_tcp(frame) for _, frame in pcap_frames(capture)]
        source = [p for p in source if p is not None and p['flags'] & 0x17 == syn_flags]
        assert source
        assert len({p['wire_seq'] for p in source}) == 1
        counts[side] = len(source)
    if retransmit:
        assert all(count >= 2 for count in counts.values()), counts
    (out / 'tcp-origins-evidence.json').write_text(json.dumps({
        'handshakes': paired, 'source_syn_submissions': counts,
        'retransmission_required': retransmit,
    }, indent=2) + '\n')
    print('# native SYN origins: anchored data bases, final ACK and stable retransmissions passed')


def reorder_host_resets(packets, source, ends, counts):
    """Only kernel-counted CLOSED replies to source-proven queued old DATA qualify."""
    fins = {}
    for sender in ('client', 'server'):
        fins[sender] = min(p['time'] for p in packets if p['sender'] == sender and
                           p['flags'] & 1 and
                           (p['wire_seq'] + len(p['body']) + 1) & MASK32 == ends[sender])
    closed = max(min(p['time'] for p in packets if p['sender'] == sender and
                     p['flags'] & 0x14 == 0x10 and p['wire_ack'] == ends[peer] and
                     p['time'] >= fins[peer])
                 for sender, peer in (('client', 'server'), ('server', 'client')))
    resets = sorted((p for p in packets if p['flags'] & 4), key=lambda p: p['time'])
    for sender in ('client', 'server'):
        actual = [p for p in source if p['sender'] == sender and p['flags'] & 4]
        delivered = [p for p in resets if p['sender'] == sender]
        before, after = counts[sender]
        assert before['time'] < min(p['time'] for p in source)
        assert after['time'] > max(p['time'] for p in packets + source)
        assert after['out_rsts'] - before['out_rsts'] == len(actual) == len(delivered), \
            ('kernel reset count or capture mismatch', sender, counts[sender], len(actual))
    assert not any(p['body'] and p['time'] > closed for p in source), \
        'new DATA source output after both FIN acknowledgments'
    used_source, used_trigger, used_reset, evidence = set(), set(), set(), []
    for reset in resets:
        assert reset['time'] > closed and reset['flags'] == 4
        assert not reset['wire_ack'] and not reset['window'] and not reset['options']
        assert not reset['body']
        assert len(bytes.fromhex(reset['frame'])) == (54 if reset['family'] == 4 else 74), \
            'RST has native padding or unexpected header geometry'
        copies = [(i, p) for i, p in enumerate(source) if i not in used_reset and
                  p['sender'] == reset['sender'] and p['frame'] == reset['frame'] and
                  closed < p['time'] <= reset['time']]
        assert copies, ('reset lacks its actual kernel-counted source copy', reset)
        index, emitted = copies[0]
        used_reset.add(index)
        triggers = [(i, p) for i, p in enumerate(packets) if i not in used_trigger and
                    p['sender'] != reset['sender'] and
                    p['key'] == WireOrigins.reverse(reset['key']) and
                    p['flags'] & 0x17 == 0x10 and p['body'] and
                    1024 <= p['ip_size'] < 2048 and
                    closed < p['time'] < emitted['time'] and
                    reset['wire_seq'] == p['wire_ack']]
        assert triggers, ('CLOSED reset lacks a new queued DATA trigger', reset)
        index, trigger = max(triggers, key=lambda item: item[1]['time'])
        used_trigger.add(index)
        copies = [(i, p) for i, p in enumerate(source) if i not in used_source and
                  p['sender'] == trigger['sender'] and p['frame'] == trigger['frame'] and
                  p['time'] < closed]
        assert copies, ('queued DATA lacks its actual pre-close source copy', trigger)
        index, sent = copies[0]
        used_source.add(index)
        evidence.append({'owner': 'kernel TCP CLOSED', 'sender': reset['sender'],
                         'both_fin_acks_time': closed, 'source_time': sent['time'],
                         'trigger_time': trigger['time'], 'reset_time': reset['time'],
                         'reset_source_time': emitted['time'],
                         'reset_wire_seq': reset['wire_seq'],
                         'trigger_wire_ack': trigger['wire_ack'],
                         'trigger_sha256': hashlib.sha256(
                             bytes.fromhex(trigger['frame'])).hexdigest()})
    return evidence


def reorder_host_evidence(out, request, response):
    origins = capture_origins(out / 'to-server.pcap').evidence()
    assert len(origins) == 1 and origins[0]['synacks']
    flow = origins[0]
    key = tuple(flow['client'])
    packets, source, counts = [], [], {}
    for sender, target in (('client', 'server'), ('server', 'client')):
        for name, destination in ((f'to-{target}', packets), (f'{sender}-source', source)):
            assert '0 packets dropped by kernel' in (out / f'{name}.capture').read_text()
            for stamp, frame in pcap_frames(out / f'{name}.pcap'):
                p = wire_tcp(frame)
                if p is None:
                    continue
                assert p['key'] == (key if sender == 'client' else WireOrigins.reverse(key))
                p.update(time=stamp, sender=sender, frame=frame.hex(),
                         family=4 if frame[12:14] == b'\x08\x00' else 6,
                         ip_size=int.from_bytes(frame[16:18] if frame[12:14] == b'\x08\x00'
                                                else frame[18:20], 'big'))
                destination.append(p)
        counts[sender] = [json.loads((out / f'{sender}-host-{phase}.json').read_text())
                          for phase in ('before', 'after')]
    ends = {'client': (flow['client_isn'] + 1 + len(request) + 1) & MASK32,
            'server': (flow['server_isn'] + 1 + len(response) + 1) & MASK32}
    evidence = reorder_host_resets(packets, source, ends, counts)
    (out / 'closed-host-reset-evidence.json').write_text(json.dumps(
        {'kernel_counts': counts, 'causal_chains': evidence}, indent=2) + '\n')
    return evidence


def pair(directory, reorder=False):
    out = Path(directory)
    expected = (out / 'expected').read_bytes()
    assert expected.startswith(b'echo: ') and expected.endswith(b'\n')
    response, request = expected[:-1], expected[6:-1]
    assert (out / 'client.out').read_bytes() == expected
    streams = {}
    for side, payload, peer in (('server', request, response), ('client', response, request)):
        assert '0 packets dropped by kernel' in (out / ('to-' + side + '.capture')).read_text()
        stream = decode(out / ('to-' + side + '.pcap'), payload)
        assert len({p['offset'] for p in stream['data']}) > 1
        assert any(p['type'] == 'fin' for p in stream['controls'])
        assert any(p['next'] == len(peer) + 1 for p in stream['acks'])
        headers = reset_packets(out / ('to-' + side + '.pcap'))
        if not reorder:
            assert not any(p['flags'] & 4 for p in headers)
        streams[side] = stream
    if reorder:
        streams['closed_host_resets'] = reorder_host_evidence(out, request, response)
    (out / 'pair-tcp-evidence.json').write_text(json.dumps(streams, indent=2) + '\n')
    print('# native standard TCP: exact payload, multi-packet stream, FIN and ACK passed')


def selector_evidence(directory, frontier=None):
    out = Path(directory)
    learned = json.loads((out / 'wire-origins.json').read_text())
    actual = [flow for flow in capture_origins(out / 'client-side.pcap').evidence()
              if flow['synacks'] and flow['client_isn'] == learned['client_isn'] and
              tuple(flow['client']) == tuple(learned['client'])]
    assert len(actual) == 1, (learned, actual)
    flow = actual[0]
    bases = [(flow['client_isn'] + 1) & MASK32, (flow['server_isn'] + 1) & MASK32]
    assert [int(value) for value in (out / 'wire-bases').read_text().split()] == bases
    if frontier is not None:
        rules = []
        for line in (out / 'wire-ack-selector').read_text().splitlines():
            device, wire, base, relative = line.split()
            wire, base, relative = int(wire), int(base), int(relative)
            assert base in bases and relative == frontier and wire == (base + relative) & MASK32
            rules.append({'device': device, 'wire_ack': wire, 'ack_base': base,
                          'relative_frontier': relative})
        assert rules
        return {'syn_bases': bases, 'exact_ack_rules': rules}
    expected = []
    lo, hi = (bases[0] + 1024) & MASK32, (bases[0] + 8191) & MASK32
    intervals = ((lo, hi),) if lo <= hi else ((lo, MASK32), (0, hi))
    for lo, hi in intervals:
        while lo <= hi:
            size = min(lo & -lo if lo else 1 << 32, 1 << ((hi - lo + 1).bit_length() - 1))
            expected.append((lo, MASK32 ^ (size - 1)))
            lo += size
    actual_bands = [tuple(int(value, 16) for value in line.split())
                    for line in (out / 'wire-ack-bands').read_text().splitlines()]
    assert actual_bands == expected and sum((MASK32 ^ mask) + 1 for _, mask in expected) == 7168
    return {'syn_bases': bases, 'relative_ack_interval': [1024, 8191],
            'wire_mask_bands': actual_bands}


def inject(filename):
    """Inject an interior standard TCP ACK from a real receiver header."""
    if (os.environ.get('ZXDP_RUN_FIXTURE') != '1' or
            os.readlink('/proc/self/ns/net') == os.readlink('/proc/1/ns/net')):
        raise RuntimeError('requires the private native packet fixture')
    out = Path(filename)
    rx = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(3))
    rx.bind(('zutcpsa', 0))
    tx = socket.socket(socket.AF_PACKET, socket.SOCK_RAW)
    tx.bind(('zutcpca', 0))
    out.with_suffix('.ready').write_text('ready')
    origins = WireOrigins()
    first = None
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        rx.settimeout(deadline - time.monotonic())
        wire = rx.recv(65535)
        origins.observe(wire)
        raw = wire_tcp(wire)
        packet = origins.relative(wire) if raw is not None and not raw['flags'] & 2 else None
        if packet is None:
            continue
        if packet['dport'] == 18181 and len(packet['body']) >= 1024 and first is None:
            first = (time.monotonic(), time.time(), len(packet['body']))
        if (packet['sport'] != 18181 or first is None or not packet['flags'] & 16 or
                packet['ack'] < 1072):
            continue
        partial = 536
        assert partial < first[2]
        tcp = bytearray(packet['tcp'][:(packet['tcp'][12] >> 4) << 2])
        tcp[13] = 16
        tcp[8:12] = ((packet['ack_base'] + partial) & MASK32).to_bytes(4, 'big')
        # Retain actual TS echo; SACK blocks would make an unrelated selective claim.
        index = 20
        while index < len(tcp):
            kind = tcp[index]
            if not kind:
                break
            if kind == 1:
                index += 1
                continue
            length = tcp[index + 1]
            assert length >= 2 and index + length <= len(tcp)
            if kind == 5:
                tcp[index:index + length] = b'\1' * length
            index += length
        ip = wire[14:]
        if wire[12:14] == b'\x08\x00':
            head = bytearray(ip[:(ip[0] & 15) << 2])
            head[2:4] = (len(head) + len(tcp)).to_bytes(2, 'big')
            head[10:12] = b'\0\0'
            head[10:12] = abort_checksum(bytes(head)).to_bytes(2, 'big')
            pseudo = bytes(head[12:20]) + b'\0\6' + len(tcp).to_bytes(2, 'big')
        else:
            head = bytearray(ip[:40])
            head[4:6] = len(tcp).to_bytes(2, 'big')
            pseudo = bytes(head[8:40]) + len(tcp).to_bytes(4, 'big') + b'\0\0\0\6'
        tcp[16:18] = b'\0\0'
        tcp[16:18] = abort_checksum(pseudo + bytes(tcp)).to_bytes(2, 'big')
        injected = wire[:14] + bytes(head) + bytes(tcp)
        wait = first[0] + .15 - time.monotonic()
        if wait > 0:
            time.sleep(wait)
        stamp = time.time()
        assert tx.send(injected) == len(injected)
        out.write_text(json.dumps({
            'partial_frontier': partial, 'original_frontier': packet['ack'],
            'wire_original_frontier': packet['wire_ack'], 'ack_base': packet['ack_base'],
            'wire_partial_frontier': (packet['ack_base'] + partial) & MASK32,
            'first_data_time': first[1], 'injected_time': stamp,
            'delay_from_first_data': stamp - first[1],
            'packet_sha256': hashlib.sha256(injected).hexdigest(),
        }, indent=2) + '\n')
        rx.close()
        tx.close()
        return
    raise RuntimeError('no established DATA/ACK suitable for partial ACK injection')


def decode(path, expected, key=None, base=None):
    origins = capture_origins(path)
    data, acks, controls = [], [], []
    coverage = bytearray(len(expected))
    packets = 0
    for stamp, frame in pcap_frames(path):
        packet = origins.relative(frame)
        if packet is None:
            continue
        if key is not None and packet['key'] != tuple(key):
            continue
        if base is not None and packet['seq_base'] != base:
            continue
        packets += 1
        seq, body = packet['seq'], packet['body']
        if packet['flags'] & 16:
            acks.append({'time': stamp, 'next': packet['ack'], 'seq': seq,
                         'window': packet['window'], 'wire_seq': packet['wire_seq'],
                         'wire_ack': packet['wire_ack'], 'seq_base': packet['seq_base'],
                         'ack_base': packet['ack_base'], 'options': packet['options']})
        if body:
            assert seq + len(body) <= len(expected), (path, seq, len(body))
            assert body == expected[seq:seq + len(body)], (path, seq, len(body))
            coverage[seq:seq + len(body)] = b'\1' * len(body)
            data.append({'time': stamp, 'offset': seq, 'length': len(body),
                         'fin': bool(packet['flags'] & 1), 'key': packet['key'],
                         'seq_base': packet['seq_base'],
                         'sha256': hashlib.sha256(body).hexdigest()})
        if packet['flags'] & 1:
            assert seq + len(body) == len(expected), (path, seq, len(body), len(expected))
            controls.append({'time': stamp, 'type': 'fin', 'value': seq + len(body)})
        elif not body and packet['flags'] & 0x1f == 0x10:
            frontier = max((v['offset'] + v['length'] for v in data), default=0)
            if (seq + 1) & MASK32 == frontier:
                controls.append({'time': stamp, 'type': 'persist', 'value': frontier})
    assert all(coverage), (path, sum(coverage), len(expected))
    unique, seen, maximum, reordered = [], set(), -1, False
    for packet in data:
        offset = packet['offset']
        if offset in seen:
            continue
        seen.add(offset)
        unique.append(offset)
        reordered |= offset < maximum
        maximum = max(maximum, offset)
    return {'packets': packets, 'data': data, 'acks': acks, 'controls': controls,
            'first_seen_offsets': unique, 'reordered': reordered,
            'payload_bytes_covered': sum(coverage), 'expected_bytes': len(expected),
            'pcap_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
            'tcp_origins': origins.evidence()}



def check(directory, mode):
    out = Path(directory)
    assert mode in ("reorder", "ack-loss", "partial-ack")
    expected = (out / "expected").read_bytes()
    assert expected.startswith(b"echo: ") and expected.endswith(b"\n")
    echo = expected[:-1]
    request = echo[len(b"echo: "):]
    streams = {direction: decode(out / ("to-" + direction + ".pcap"),
                                request if direction == "server" else echo)
               for direction in ("server", "client")}
    assert (out / "client.out").read_bytes() == expected
    for direction in ("server", "client"):
        assert (
            "0 packets dropped by kernel" in (out / ("to-" + direction + ".capture")).read_text()
        )
    if mode == "reorder":
        assert all(stream["reordered"] for stream in streams.values())
    else:
        first = next(packet for packet in streams["server"]["data"] if packet["offset"] == 0)
        copies = [packet for packet in streams["server"]["data"]
                  if packet["offset"] == 0 and packet["length"] == first["length"]]
        assert len(copies) >= 3
        assert len({packet["sha256"] for packet in copies}) == 1
        gaps = [round(copies[i + 1]["time"] - copies[i]["time"], 6)
                for i in range(len(copies) - 1)]
        evidence = {"offset": 0, "length": first["length"], "gaps_seconds": gaps,
                    "payload_sha256": first["sha256"]}
        if mode == "ack-loss":
            assert gaps[0] > .15 and gaps[1] > gaps[0] * 1.5
        else:
            injection = json.loads((out / "injection.json").read_text())
            partial = injection["partial_frontier"]
            assert 0 < partial < first["length"]
            receipts = [ack for ack in streams["client"]["acks"]
                        if ack["next"] == partial]
            assert len(receipts) == 1
            stamp = receipts[0]["time"]
            delay = stamp - copies[0]["time"]
            restart = copies[1]["time"] - stamp
            assert .12 < delay < .19 and .15 < restart < .30 and gaps[0] > .30
            evidence.update(partial_ack_frontier=partial,
                            injection_delay_seconds=round(delay, 6),
                            retry_since_partial_ack_seconds=round(restart, 6))
        assert any(ack["next"] is not None and ack["next"] >= len(request)
                   for ack in streams["client"]["acks"])
        streams["rto_evidence"] = evidence
    if mode in ("ack-loss", "partial-ack"):
        streams["wire_fault_selector"] = selector_evidence(out)
    (out / "regression-evidence.json").write_text(json.dumps(streams, indent=2) + "\n")
    print("# recovery " + mode + ": exact DATA coverage and wire assertions passed")


def window(directory, restart=False, lost=False):
    out = Path(directory)
    message = bytes((i * 13 + 7) & 255 for i in range(1024))
    decoded = {side: decode(out / ('to-' + side + '.pcap'), message * 2)
               for side in ('client', 'server')}
    rounds = []
    if restart:
        handshakes = [flow for flow in capture_origins(out / 'to-client.pcap').evidence()
                      if flow['synacks']]
        assert {flow['client'][1] for flow in handshakes} == {18192, 18193}, handshakes
        assert all(flow['final_acks'] for flow in handshakes), handshakes
    for size in ((1024, 512) if restart else (1024,)):
        streams = decoded
        if restart:
            streams = {}
            for side, stream in decoded.items():
                keys = {p['key'] for p in stream['data']
                        if p['offset'] == 0 and p['length'] == size}
                assert len(keys) == 1, (side, size, keys)
                streams[side] = decode(out / ('to-' + side + '.pcap'),
                                       message[:size] * 2, next(iter(keys)))
        for side, peer in (('client', 'server'), ('server', 'client')):
            stream = streams[side]
            other = streams[peer]
            capture = (out / ('to-' + side + '.capture')).read_text()
            assert '0 packets dropped by kernel' in capture
            first = next(p for p in stream['data'] if p['offset'] == 0)
            second = next(p for p in stream['data'] if p['offset'] == size)
            assert first['length'] == second['length'] == size
            assert second['time'] - first['time'] > 3.2
            probes = [p for p in stream['controls'] if p['type'] == 'persist' and
                      p['value'] == size]
            assert len(probes) >= 2, (side, probes)
            gap = probes[1]['time'] - probes[0]['time']
            assert .9 < probes[0]['time'] - first['time'] < 1.5
            assert 1.8 < gap < 2.5, (side, gap)
            for probe in probes[:2]:
                assert any(c['next'] == size and c['window'] == 0 and
                           0 <= c['time'] - probe['time'] < .5 for c in other['acks'])
            opening = next(c for c in other['acks']
                           if c['next'] == size and c['window'] == size and c['seq'] == size)
            assert -.01 <= second['time'] - opening['time'] < .5
            if lost:
                assert len(probes) >= 3
                later_gap = probes[2]['time'] - probes[1]['time']
                assert 3.8 < later_gap < 4.5
                assert 0 <= opening['time'] - probes[2]['time'] < .5
                assert second['time'] >= probes[2]['time']
                port = 'zutcpmlpa' if side == 'client' else 'zutcpmlpb'
                assert int((out / (port + '.opening-drops')).read_text()) > 0
            stream['persist_evidence'] = {
                'closed_edge': size,
                'probe_times': [p['time'] for p in probes],
                'probe_gap_seconds': gap,
                'opening_edge': size * 2,
                'resume_seconds': second['time'] - opening['time'],
            }
        rounds.append({'window': size, 'streams': streams})
    evidence = {'rounds': rounds} if restart else rounds[0]['streams']
    if lost:
        evidence['wire_fault_selector'] = selector_evidence(out, 1024)
    name = ('opening-loss-evidence.json' if lost else
            'restart-evidence.json' if restart else 'window-evidence.json')
    (out / name).write_text(json.dumps(evidence, indent=2) + '\n')
    print('# mapped ' + ('lost window: persist restores credit, ' if lost else
             'restart: changed settings, ' if restart else 'zero window: ')
          + 'exact bytes, probe/backoff/reply and immediate reopening passed')


def retired_recovery(packets, active, data, after=0):
    """Cold reply follows a repeated byte-bearing request after owner/drop gates."""
    requests = [p for p in packets if p['sport'] != active and not p['flags'] & 4 and
                (p['data_length'] or p['flags'] & 1)]
    for packet in requests:
        assert packet['seq'] < 20
        if packet['data_length']:
            assert packet['seq'] == 0 and packet['data_length'] == 19
        if packet['flags'] & 1:
            assert packet['seq'] + packet['data_length'] == 19
    replies = [p for p in packets if p['sport'] == active and p['flags'] == 0x10 and
               not p['data_length'] and not p['window'] and
               p['time'] > after and p['ack'] == 20]
    assert replies and all(p['seq'] == 20 for p in replies), replies
    chains, previous = [], after
    for reply in replies:
        candidates = [p for p in requests if previous < p['time'] < reply['time']]
        assert candidates, ('retired ACK without a new DATA/FIN request', reply)
        request = candidates[-1]
        assert any(p['time'] < request['time'] and p['seq'] == request['seq'] and
                   p['data_length'] == request['data_length'] and
                   bool(p['flags'] & 1) == bool(request['flags'] & 1) for p in requests), request
        chains.append({'request': request, 'reply': reply})
        previous = reply['time']
    return chains


def final_ack(directory):
    out = Path(directory)
    peer = reset_packets(out / 'peer-fin.pcap')
    packets = sorted(peer + reset_packets(out / 'final-ack.pcap'), key=lambda p: p['time'])
    assert not any(p['flags'] & 4 for p in packets), 'graceful final-ACK recovery emitted RST'
    passive = peer[0]['sport']
    active = 18191 if passive == 18192 else 18192
    key = next(p['key'] for p in peer if p['data_length'])
    stream = decode(out / 'peer-fin.pcap', b'native-mx-byte-path', key)
    cut = max((out / 'retired').stat().st_mtime, (out / 'final-ack.loss').stat().st_mtime)
    chains = retired_recovery(packets, active, stream['data'], cut)
    assert any(p['flags'] & 1 for p in peer), peer
    assert '0 packets dropped by kernel' in (out / 'peer-fin.capture').read_text()
    assert '0 packets dropped by kernel' in (out / 'final-ack.capture').read_text()
    (out / 'final-ack-evidence.json').write_text(json.dumps({
        'retired_recovery_chains': chains, 'peer_stream': stream,
        'peer_fin_sequences': [p['seq'] for p in peer if p['flags'] & 1],
        'wire_peer_fins': [p for p in peer if p['flags'] & 1],
        'selector': selector_evidence(out, 20),
        'owner_retired_before_loss_release': (out / 'retired').is_file(),
        'drops': (out / 'final-ack.loss').read_text(),
    }, indent=2) + '\n')
    print('# final ACK loss: retransmitted DATA/FIN and retired-owner response passed')



def fin_window_packet(frame, origins):
    origins.observe(frame)
    raw = wire_tcp(frame)
    if raw is None or raw['flags'] & 2:
        return None
    relative = origins.relative(frame)
    return {name: relative[name] for name in (
        'wire_seq', 'wire_ack', 'seq_base', 'ack_base', 'sport', 'dport', 'seq', 'ack',
        'flags', 'window')} | {
            'data_length': len(relative['body']),
            'persist': not relative['body'] and relative['flags'] & 0x1f == 0x10}


def fin_window_probe(directory, interface):
    out = Path(directory)
    assert os.environ.get('ZXDP_RUN_FIXTURE') == '1'
    assert os.readlink('/proc/self/ns/net') != os.readlink('/proc/1/ns/net')
    initiator = int((out / 'fin-window-role').read_text())
    origins = WireOrigins()
    zero = None
    rx = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(3))
    rx.bind((interface, 0))
    (out / 'monitor-ready').write_text('ready\n')
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        rx.settimeout(deadline - time.monotonic())
        packet = fin_window_packet(rx.recv(65535), origins)
        if packet is None:
            continue
        if packet['sport'] == initiator:
            assert not packet['flags'] & 1, packet
            if packet['persist'] and packet['seq'] == 18 and zero is not None:
                (out / 'persist-observed.json').write_text(json.dumps({
                    'zero_window': zero, 'persist': packet,
                }, indent=2) + '\n')
                # The main test caller blocks on this FIFO before reopening Rx.
                # Release it only after wire evidence of the queued-FIN probe.
                with (out / 'reopen').open('w') as gate:
                    gate.write('R')
                rx.close()
                return
        elif packet['ack'] == 19 and not packet['window']:
            zero = packet
    raise RuntimeError('no queued-FIN persist after zero-window acknowledgment')


def fin_window(directory):
    out = Path(directory)
    origins = capture_origins(out / 'client-side.pcap')
    initiator = int((out / 'fin-window-role').read_text())
    packets = []
    for direction in ('client', 'server'):
        raw = (out / ('to-' + direction + '.pcap')).read_bytes()
        formats = {
            b'\xd4\xc3\xb2\xa1': ('<', 1000000),
            b'M<\xb2\xa1': ('<', 1000000000),
            b'\xa1\xb2\xc3\xd4': ('>', 1000000),
            b'\xa1\xb2<M': ('>', 1000000000),
        }
        endian, scale = formats[raw[:4]]
        assert struct.unpack(endian + 'I', raw[20:24])[0] == 1
        pos = 24
        while pos < len(raw):
            sec, sub, cap, wire = struct.unpack(endian + 'IIII', raw[pos:pos + 16])
            pos += 16
            frame = raw[pos:pos + cap]
            pos += cap
            assert cap == wire and len(frame) == cap
            packet = fin_window_packet(frame, origins)
            if packet is not None:
                packet['time'] = sec + sub / scale
                packets.append(packet)
    assert not any(p['flags'] & 4 for p in packets), 'graceful FIN window emitted RST'
    packets.sort(key=lambda packet: packet['time'])
    data = [p for p in packets if p['sport'] == initiator and p['flags'] & 8]
    fins = [p for p in packets if p['sport'] == initiator and p['flags'] & 1]
    probes = [p for p in packets if p['sport'] == initiator and p['persist']]
    zero = [p for p in packets if p['sport'] != initiator and p['ack'] == 19
            and not p['window']]
    assert data and data[0]['seq'] == 0 and not data[0]['flags'] & 1, data
    assert zero and probes and fins, (zero, probes, fins)
    probe = next(p for p in probes if p['seq'] == 18 and p['time'] >= zero[0]['time'])
    opening = next(p for p in packets if p['sport'] != initiator and p['ack'] == 19
                   and p['window'] > 0 and p['time'] > probe['time'])
    assert fins[0]['seq'] == 19 and fins[0]['time'] > opening['time'], fins
    assert not fins[0]['flags'] & 8, fins[0]
    assert (out / 'persist-observed.json').is_file()
    (out / 'fin-window-evidence.json').write_text(json.dumps({
        'first_data': data[0], 'zero_window': zero[0], 'persist': probe,
        'reopening': opening, 'standalone_fin': fins[0],
    }, indent=2) + '\n')
    print('# FIN window: exact DATA, persist, reopening and standalone FIN passed')


def reset_packets(path):
    origins = capture_origins(path)
    packets = []
    for stamp, frame in pcap_frames(path):
        packet = origins.relative(frame)
        if packet is None:
            continue
        packet.pop('tcp')
        packet['data_length'] = len(packet.pop('body'))
        packet['persist'] = not packet['data_length'] and packet['flags'] & 0x1f == 0x10
        packet.update(time=stamp)
        packets.append(packet)
    return packets


def reset_pair(directory):
    out = Path(directory)
    initiator = int((out / 'rst-role').read_text())
    evidence = {}
    for side in ('client', 'server'):
        packets = reset_packets(out / (side + '-side.pcap'))
        old = [p for p in packets if 18192 in (p['sport'], p['dport'])]
        resets = [p for p in old if p['flags'] & 4]
        assert len(resets) == 1, resets
        reset = resets[0]
        assert reset['sport'] == initiator and reset['flags'] == 0x14, reset
        assert reset['seq'] == 19 and reset['ack'] == 0, reset
        assert not reset['data_length'], reset
        # No peer DATA was delivered on this tuple. Once RST arrives, there
        # must be neither an ACK of it nor a reflected reset on the same tuple.
        later_peer = [p for p in old if p['sport'] != initiator and
                      p['time'] > reset['time']]
        assert not later_peer, later_peer
        fresh = [p for p in packets if 18193 in (p['sport'], p['dport'])]
        assert fresh and not any(p['flags'] & 4 for p in fresh), fresh
        assert any(p['flags'] & 1 for p in fresh), fresh
        assert any(p['flags'] & 8 and p['sport'] == 18193 for p in fresh), fresh
        assert any(p['flags'] & 8 and p['sport'] == 18191 for p in fresh), fresh
        evidence[side] = {'reset': reset, 'peer_packets_after_reset': len(later_peer),
                          'fresh_packets': len(fresh)}
    evidence['data_drop'] = (out / 'rst-data.loss').read_text()
    (out / 'rst-evidence.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print('# RST: wire abort without ACK/reflection, backing drain and fresh listener passed')


def half_close(directory):
    out = Path(directory)
    expected = (out / 'expected').read_bytes()
    assert expected.startswith(b'echo: ') and expected.endswith(b'\n')
    echo, request = expected[:-1], expected[6:-1]
    assert 0 < len(request) < 65535
    assert (out / 'client.out').read_bytes() == expected
    streams = {side: decode(out / ('to-' + side + '.pcap'), payload)
               for side, payload in (('server', request), ('client', echo))}
    headers = {side: reset_packets(out / ('to-' + side + '.pcap'))
               for side in ('server', 'client')}
    for side in ('server', 'client'):
        assert '0 packets dropped by kernel' in (out / ('to-' + side + '.capture')).read_text()
        assert not any(p['flags'] & 4 for p in headers[side])
    fins = [p for p in headers['server'] if p['flags'] & 1]
    assert fins, headers['server']
    first_fin = min(fins, key=lambda p: p['time'])
    data_length = first_fin['data_length']
    assert first_fin['seq'] + data_length == len(request), first_fin
    first_reply = min(streams['client']['data'], key=lambda p: p['time'])
    assert first_reply['time'] > first_fin['time'], (first_fin, first_reply)
    assert any(p['ack'] == len(request) + 1 for p in headers['client'])
    # The callback guard before creating any positive output establishes causal
    # EOF-before-production; capture order alone is not the application proof.
    marker = 'receive EOF; reply bytes=' + str(len(request))
    lines = (out / 'server.err').read_text().splitlines()
    assert lines.count(marker) == 1, lines
    (out / 'half-close-evidence.json').write_text(json.dumps({
        'request_bytes': len(request), 'reply_bytes': len(echo),
        'peer_fin': first_fin, 'first_positive_reply': first_reply,
        'application_eof_before_enqueue': marker,
        'streams': streams,
    }, indent=2) + '\n')
    print('# native half-close: new exact echo DATA generated after peer EOF passed')


def abort_tcp(frame, origins):
    origins.observe(frame)
    raw = wire_tcp(frame)
    if raw is None or raw['flags'] & 2:
        return None
    packet = origins.relative(frame)
    tcp, body = packet.pop('tcp'), packet.pop('body')
    return packet, tcp, body


def abort_checksum(data):
    if len(data) & 1:
        data += b'\x00'
    total = sum(struct.unpack('!%dH' % (len(data) // 2), data))
    while total >> 16:
        total = (total & 65535) + (total >> 16)
    return ~total & 65535


def timestamp_offset(tcp):
    head = (tcp[12] >> 4) * 4
    assert 20 <= head <= min(60, len(tcp))
    offset = 20
    while offset < head:
        kind = tcp[offset]
        if kind == 0:
            return None
        if kind == 1:
            offset += 1
            continue
        assert offset + 2 <= head
        length = tcp[offset + 1]
        assert 2 <= length <= head - offset
        if kind == 8:
            assert length == 10
            return offset + 2
        offset += length
    return None


def tcp_packet(template, seq, payload, flags, tsval=None):
    ip = bytearray(template[14:])
    v4 = template[12:14] == b'\x08\x00'
    iphead = (ip[0] & 15) * 4 if v4 else 40
    tcphead = (ip[iphead + 12] >> 4) * 4
    assert 20 <= tcphead <= 60 and iphead + tcphead <= len(ip)
    tcp = bytearray(ip[iphead:iphead + tcphead])
    tcp[4:8] = seq.to_bytes(4, 'big')
    tcp[13] = flags
    if tsval is not None:
        offset = timestamp_offset(tcp)
        if offset is not None:
            tcp[offset:offset + 4] = (tsval & MASK32).to_bytes(4, 'big')
    tcp[16:18] = b'\x00\x00'
    tcp += payload
    if v4:
        ip = ip[:iphead]
        ip[2:4] = (iphead + len(tcp)).to_bytes(2, 'big')
        ip[10:12] = b'\x00\x00'
        pseudo = bytes(ip[12:20]) + b'\x00\x06' + len(tcp).to_bytes(2, 'big')
        ip[10:12] = abort_checksum(bytes(ip)).to_bytes(2, 'big')
    else:
        ip = ip[:40]
        ip[4:6] = len(tcp).to_bytes(2, 'big')
        pseudo = bytes(ip[8:40]) + len(tcp).to_bytes(4, 'big') + b'\x00\x00\x00\x06'
    tcp[16:18] = abort_checksum(pseudo + bytes(tcp)).to_bytes(2, 'big')
    return template[:14] + bytes(ip + tcp)


def tcp_sacks(tcp, base):
    index, end, ranges = 20, (tcp[12] >> 4) << 2, []
    while index < end:
        kind = tcp[index]
        if not kind:
            break
        if kind == 1:
            index += 1
            continue
        length = tcp[index + 1]
        assert length >= 2 and index + length <= end
        if kind == 5:
            assert length >= 10 and (length - 2) % 8 == 0
            for offset in range(index + 2, index + length, 8):
                left, right = struct.unpack('!II', tcp[offset:offset + 8])
                ranges.append([(left - base) & MASK32, (right - base) & MASK32])
        index += length
    return ranges


def abort_inject(directory, interface):
    out = Path(directory)
    assert os.environ.get('ZXDP_RUN_FIXTURE') == '1'
    assert os.readlink('/proc/self/ns/net') != os.readlink('/proc/1/ns/net')
    victim = int((out / 'abort-victim').read_text())
    rx = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(3))
    rx.bind((interface, 0))
    tx = socket.socket(socket.AF_PACKET, socket.SOCK_RAW)
    tx.bind((interface, 0))
    (out / 'monitor-ready').write_text('ready\n')
    # The callback has consumed the ordinary first DATA before this gate opens.
    with (out / 'ready').open('r') as gate:
        assert gate.read(1) == 'R'
    deadline = time.monotonic() + 15
    origins = WireOrigins()
    template = None
    while time.monotonic() < deadline:
        rx.settimeout(deadline - time.monotonic())
        frame = rx.recv(65535)
        parsed = abort_tcp(frame, origins)
        if parsed is None:
            continue
        packet, tcp, body = parsed
        if packet['dport'] == victim and packet['seq'] == 0 and packet['flags'] & 8:
            if body.endswith(b'native-mx-byte-path'):
                template = frame
                break
    assert template is not None, 'no actual established DATA suitable for fatal-input injection'
    original, template_tcp, unused = abort_tcp(template, origins)
    offset = timestamp_offset(template_tcp)
    # This probe targets final-size handling, so an old copied timestamp must
    # not turn it into a PAWS test. The out-of-order probe cannot update Recent.
    fresh_ts = None if offset is None else (
        int.from_bytes(template_tcp[offset:offset + 4], 'big') + 1000) & MASK32
    future = tcp_packet(template, (original['seq_base'] + 32) & MASK32,
                        b'?', 0x18, fresh_ts)
    (out / 'future-data.packet').write_bytes(future)
    assert tx.send(future) == len(future)
    witness = None
    while time.monotonic() < deadline:
        rx.settimeout(deadline - time.monotonic())
        parsed = abort_tcp(rx.recv(65535), origins)
        if parsed is None:
            continue
        packet, tcp, body = parsed
        if packet['sport'] != victim or packet['dport'] != original['sport']:
            continue
        ranges = tcp_sacks(tcp, packet['ack_base'])
        if [32, 33] in ranges:
            assert packet['ack'] == 19, packet
            witness = {'header': packet, 'sack_blocks': ranges}
            break
    assert witness is not None, 'future DATA was not acknowledged as retained'
    # The FIN's byte position is in-window, but conflicts with that retained byte.
    final = tcp_packet(template, (original['seq_base'] + 19) & MASK32,
                       b'', 0x11, fresh_ts)
    (out / 'conflicting-fin.packet').write_bytes(final)
    assert tx.send(final) == len(final)
    (out / 'fatal-injection.json').write_text(json.dumps({
        'future_sequence': 32, 'fin_sequence': 19,
        'probe_timestamp': fresh_ts,
        'retained_future_ack': witness,
        'future_sha256': hashlib.sha256(future).hexdigest(),
        'fin_sha256': hashlib.sha256(final).hexdigest(),
    }, indent=2) + '\n')
    with (out / 'injected').open('w') as gate:
        gate.write('R')
    rx.close()
    tx.close()


def abort_pair(directory):
    out = Path(directory)
    victim = int((out / 'abort-victim').read_text())
    fatal = (out / 'abort-mode').read_text().strip() != 'abort-reject'
    evidence = {}
    for side in ('client', 'server'):
        packets = reset_packets(out / (side + '-side.pcap'))
        old = [p for p in packets if 18192 in (p['sport'], p['dport'])]
        resets = [p for p in old if p['flags'] & 4]
        assert len(resets) == 1, resets
        reset = resets[0]
        assert reset['sport'] == victim and reset['flags'] == 0x14, reset
        assert reset['seq'] == 0 and reset['ack'] == (19 if fatal else 0), reset
        assert not reset['data_length'], reset
        replies = [p for p in old if p['sport'] != victim and p['time'] > reset['time']
                   and (p['flags'] & 4 or p['flags'] & 0x1d == 0x10)]
        assert not replies, replies
        fresh = [p for p in packets if 18193 in (p['sport'], p['dport'])]
        assert fresh and not any(p['flags'] & 4 for p in fresh), fresh
        assert any(p['flags'] & 1 for p in fresh), fresh
        for port in (18191, 18193):
            assert any(p['flags'] & 8 and p['sport'] == port for p in fresh), fresh
        evidence[side] = {'reset': reset, 'responses_after_reset': len(replies),
                          'fresh_packets': len(fresh)}
    if fatal:
        injection = json.loads((out / 'fatal-injection.json').read_text())
        assert injection['retained_future_ack']['header']['ack'] == 19
        assert [32, 33] in injection['retained_future_ack']['sack_blocks']
        evidence['injection'] = injection
        evidence['suppressed_injected_ack'] = (out / 'injected-ack.loss').read_text()
        evidence['wire_fault_selector'] = selector_evidence(out, 19)
    (out / 'abort-evidence.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print('# meaningful abort: rejection/fatal input, accepted RST and fresh listener passed')


def native_history(directory):
    out = Path(directory)
    active = int((out / 'native-active').read_text())
    passive = 18191 if active == 18192 else 18192
    before = [int(value) for value in (out / 'retired').read_text().split()]
    after = [int(value) for value in (out / 'recovered').read_text().split()]
    assert before[0] > 0 and before == after and len(before) == 3
    assert (out / 'post-eof').is_file() and (out / 'held-reader').is_file()
    captures = {side: reset_packets(out / (side + '-side.pcap'))
                for side in ('client', 'server')}
    old_headers = [p for p in captures['client'] if 18192 in (p['sport'], p['dport'])]
    active_header = next(p for p in old_headers if p['sport'] == active)
    offsets = [(before[1] - active_header['seq_base']) & MASK32,
               (before[2] - active_header['ack_base']) & MASK32]
    assert offsets == [20, 20], (before, active_header)
    evidence = {'normalized_closed_frontiers': offsets}
    for side, packets in captures.items():
        assert not any(p['flags'] & 4 for p in packets), packets
        old = [p for p in packets if 18192 in (p['sport'], p['dport'])]
        fresh = [p for p in packets if 18193 in (p['sport'], p['dport'])]
        assert old and fresh
        streams = {}
        for port in (18191, 18192):
            key = next(p['key'] for p in old if p['sport'] == port)
            streams[port] = decode(out / (side + '-side.pcap'), b'native-mx-byte-path', key)
        active_fin = next(p for p in old if p['sport'] == active and p['flags'] & 1)
        positive_reply = min(streams[passive]['data'], key=lambda p: p['time'])
        assert positive_reply['time'] > active_fin['time'], (active_fin, positive_reply)
        for port in (18191, 18193):
            assert any(p['sport'] == port and p['flags'] & 8 for p in fresh), fresh
        assert sum(bool(p['flags'] & 1) for p in fresh) >= 2
        evidence[side] = {'first_streams': streams, 'fresh_packets': len(fresh),
                          'first_peer_fin': active_fin,
                          'first_post_eof_positive_data': positive_reply}
    active_side = 'client' if active == 18192 else 'server'
    old = [p for p in captures[active_side] if 18192 in (p['sport'], p['dport'])]
    fins = [p for p in old if p['sport'] == passive and p['flags'] & 1]
    assert fins, old
    data = evidence[active_side]['first_streams'][passive]['data']
    cut = max((out / 'retired').stat().st_mtime,
              (out / 'native-final-ack.loss').stat().st_mtime)
    chains = retired_recovery(old, active, data, cut)
    replies = [chain['reply'] for chain in chains]
    evidence['retired_recovery_chains'] = chains
    evidence['closed_record_before'] = before
    evidence['closed_record_after'] = after
    evidence['retired_history_replies'] = replies
    evidence['repeated_peer_fins'] = fins
    evidence['drops'] = (out / 'native-final-ack.loss').read_text()
    evidence['wire_fault_selector'] = selector_evidence(out, 20)
    evidence['retained_reader_release'] = (out / 'held-reader').read_text()
    (out / 'native-history-evidence.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print('# direct native: retired DATA/FIN recovery, successive owners and held reader passed')


def admission_packets(path):
    packets = []
    for stamp, frame in pcap_frames(path):
        packet = wire_tcp(frame)
        if packet is None:
            continue
        packet.pop('tcp')
        packet['data_length'] = len(packet.pop('body'))
        packet['time'] = stamp
        packets.append(packet)
    return packets


def shared_admission(directory):
    out = Path(directory)
    passive = (out / 'shared-mode').read_text().strip() == 'shared-passive'
    evidence = {}
    for side in ('client', 'server'):
        path = out / (side + '-side.pcap')
        packets = reset_packets(path)
        all_packets = admission_packets(path)
        fresh_ports = (18192, 18196) if passive else (18192, 18193, 18196)
        flows = {}
        for port in fresh_ports:
            flow = [p for p in packets if port in (p['sport'], p['dport'])]
            assert flow and not any(p['flags'] & 4 for p in flow), flow
            assert sum(bool(p['flags'] & 1) for p in flow) >= 2, flow
            streams = {}
            for source in (port, 18195 if port == 18196 else 18191):
                key = next(p['key'] for p in flow if p['sport'] == source)
                streams[source] = decode(path, b'native-mx-byte-path', key)
            flows[port] = streams
        if passive:
            denied = [p for p in all_packets if p['sport'] == 18193 and p['dport'] == 18195]
            assert denied and any(bool(p['flags'] & 2) for p in denied), denied
            requests = [p for p in denied if p['flags'] & 0x17 == 2]
            refusals = [p for p in all_packets if p['sport'] == 18195 and p['dport'] == 18193]
            assert refusals and all(p['flags'] == 0x14 and not p['data_length'] and
                not p['wire_seq'] and not p['window'] and not p['options'] and
                any(q['time'] < p['time'] and
                    p['wire_ack'] == (q['wire_seq'] + 1) & MASK32 for q in requests)
                for p in refusals)
            evidence[side] = {'flows': flows, 'denied_initials': denied,
                              'refusal_resets': refusals}
        else:
            assert not any(18194 in (p['sport'], p['dport']) or
                           18198 in (p['sport'], p['dport']) for p in all_packets)
            pending = [p for p in all_packets if p['sport'] == 18197 and p['dport'] == 18199]
            assert pending and any(bool(p['flags'] & 2) for p in pending), pending
            evidence[side] = {'flows': flows, 'pending_initials': pending,
                              'mapped_denial_packets': 0}
    (out / 'shared-admission-evidence.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print('# shared Stack admission: native Hubs/listeners, mapped denial and slot reuse passed')


def conservative_tuple(directory):
    out = Path(directory)
    evidence = {}
    for side in ('client', 'server'):
        path = out / (side + '-side.pcap')
        packets = reset_packets(path)
        envelopes = admission_packets(path)
        assert not any(p['flags'] & 4 for p in envelopes), envelopes
        flows = {}
        for port in (49152, 49153):
            flow = [p for p in packets if port in (p['sport'], p['dport'])]
            assert flow and sum(bool(p['flags'] & 1) for p in flow) >= 2, flow
            streams = {}
            for source in (port, 18191):
                key = next(p['key'] for p in flow if p['sport'] == source)
                streams[source] = decode(path, b'native-mx-byte-path', key)
            flows[port] = streams
        first_fin = min(p['time'] for p in packets if
                        49152 in (p['sport'], p['dport']) and p['flags'] & 1)
        late_initials = [p for p in envelopes if p['sport'] == 49152 and
                         bool(p['flags'] & 2) and p['time'] > first_fin]
        assert not late_initials, late_initials
        new_initials = [p for p in envelopes if p['sport'] == 49153 and bool(p['flags'] & 2)]
        assert new_initials and min(p['time'] for p in new_initials) > first_fin
        evidence[side] = {'flows': flows, 'rejected_reuse_initials': late_initials,
                          'automatic_port_initials': new_initials}
    (out / 'conservative-tuple-evidence.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print('# conservative tuple: fixed reuse refused without wire output; automatic 49153 passed')


def positive_window_probe(directory, interface):
    out = Path(directory)
    assert os.environ.get('ZXDP_RUN_FIXTURE') == '1'
    assert os.readlink('/proc/self/ns/net') != os.readlink('/proc/1/ns/net')
    rx = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(3))
    rx.bind((interface, 0))
    (out / 'monitor-ready').write_text('ready\n')
    origins = WireOrigins()
    evidence = []
    deadline = time.monotonic() + 20
    for frontier, name in ((400, 'reopen'), (800, 'reopen-second')):
        zero = None
        while time.monotonic() < deadline:
            rx.settimeout(deadline - time.monotonic())
            packet = fin_window_packet(rx.recv(65535), origins)
            if packet is None:
                continue
            if packet['sport'] == 18191 and packet['ack'] == frontier and not packet['window']:
                zero = packet
            elif packet['sport'] == 18192:
                assert not packet['flags'] & 1, packet
                if packet['persist'] and packet['seq'] == 999 and zero is not None:
                    evidence.append({'zero_window': zero, 'persist': packet})
                    with (out / name).open('w') as gate:
                        gate.write('R')
                    break
        else:
            raise RuntimeError('no persist after strict receiver window ' + str(frontier))
    rx.close()
    (out / 'positive-window-probes.json').write_text(json.dumps(evidence, indent=2) + '\n')


def positive_window(directory):
    out = Path(directory)
    expected = bytes((i * 13 + 7) & 255 for i in range(1000))
    evidence = {}
    for side in ('client', 'server'):
        path = out / (side + '-side.pcap')
        packets = reset_packets(path)
        assert packets and not any(p['flags'] & 4 for p in packets), packets
        key = next(p['key'] for p in packets if p['sport'] == 18192 and p['data_length'])
        stream = decode(path, expected, key)
        data = stream['data']
        assert len(data) >= 3 and stream['payload_bytes_covered'] == 1000, stream
        assert all(p['offset'] == 0 and p['length'] == 1000 and not p['fin'] for p in data), data
        client_fin = next(p for p in packets if p['sport'] == 18192 and p['flags'] & 1)
        assert client_fin['seq'] == 1000 and not client_fin['flags'] & 8, client_fin
        eligible = next(p for p in packets if p['sport'] == 18191 and
                        p['ack'] <= 1000 < p['ack'] + p['window'] and
                        p['time'] < client_fin['time'])
        final = next(p for p in packets if p['sport'] == 18191 and p['ack'] == 1001 and
                     p['time'] > max(d['time'] for d in data))
        phases = []
        for frontier in (400, 800):
            zero = next(p for p in packets if p['sport'] == 18191 and
                        p['ack'] == frontier and not p['window'])
            probe = next(p for p in packets if p['sport'] == 18192 and p['persist'] and
                         p['seq'] == 999 and p['time'] > zero['time'])
            opening = next(p for p in packets if p['sport'] == 18191 and
                           p['ack'] == frontier and p['window'] == 400 and
                           p['time'] > probe['time'])
            assert not any(zero['time'] < p['time'] < opening['time'] for p in data), phases
            phases.append({'zero': zero, 'probe': probe, 'opening': opening})
        reverse_fin = [p for p in packets if p['sport'] == 18191 and p['flags'] & 1]
        assert reverse_fin and all(not p['data_length'] and not p['seq'] for p in reverse_fin)
        evidence[side] = {'original_data': data, 'zero_reopen': phases,
                          'fin_credit': eligible, 'final_ack': final, 'standalone_fin': client_fin}
    evidence['observed_probes'] = json.loads((out / 'positive-window-probes.json').read_text())
    (out / 'positive-window-evidence.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print('# positive window: original1000, strict400/400/200, persist/recovery and FIN passed')


def native_lifecycle(directory):
    out = Path(directory)
    mode = (out / 'lifecycle-mode').read_text().strip()
    stop = mode != 'native-zero-close'
    initiator = 18192 if mode == 'native-error-client' else 18191
    receiver = 18191 if initiator == 18192 else 18192
    evidence = {}
    for side in ('client', 'server'):
        path = out / (side + '-side.pcap')
        packets = reset_packets(path)
        old = [p for p in packets if 18192 in (p['sport'], p['dport'])]
        if stop:
            resets = [p for p in old if p['flags'] & 4]
            assert len(resets) == 1, resets
            reset = resets[0]
            assert reset['sport'] == initiator and reset['flags'] == 0x14, reset
            assert reset['seq'] == 19 and reset['ack'] == 0, reset
            assert not reset['data_length'], reset
            assert not any(p['sport'] == receiver and p['time'] > reset['time'] for p in old)
            assert not any(p['flags'] & 1 for p in old), old
            fresh = [p for p in packets if 18193 in (p['sport'], p['dport'])]
            assert fresh and not any(p['flags'] & 4 for p in fresh)
            assert sum(bool(p['flags'] & 1) for p in fresh) >= 2
            streams = {}
            for port in (18193, 18195):
                key = next(p['key'] for p in fresh if p['sport'] == port)
                streams[port] = decode(path, b'native-mx-byte-path', key)
            evidence[side] = {'reset': reset, 'surviving_listener_streams': streams}
            if side == ('client' if initiator == 18192 else 'server'):
                data = [p for p in old if p['sport'] == initiator and p['flags'] & 8]
                assert data and min(p['time'] for p in data) < reset['time'], old
                evidence[side]['outstanding_data'] = data
        else:
            assert old and not any(p['flags'] & 4 for p in packets)
            assert sum(bool(p['flags'] & 1) for p in old) >= 2
            streams = {}
            for port in (18191, 18192):
                key = next(p['key'] for p in old if p['sport'] == port)
                streams[port] = decode(path, b'native-mx-byte-path', key)
            local_fin = min(p['time'] for p in old if p['sport'] == 18192 and p['flags'] & 1)
            reverse = min(p['time'] for p in streams[18191]['data'])
            assert reverse > local_fin, (local_fin, reverse)
            evidence[side] = {'streams': streams, 'client_fin': local_fin,
                              'first_positive_reverse_data': reverse}
    if stop:
        evidence['actual_data_loss'] = (out / 'hub-stop.loss').read_text()
    (out / 'native-lifecycle-evidence.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print('# native public lifecycle: zero/default FIN or numeric/Hub RST with listener reuse passed')


def paws_wrap(directory):
    """Only the independent raw peer chooses its own wire timestamp values."""
    out = Path(directory)
    accepted, rejected = b'reuse-mx-byte-path!', b'native-mx-byte-path'
    evidence = {}
    for side in ('client', 'server'):
        path = out / (side + '-side.pcap')
        origins = capture_origins(path)
        initial_flows = [f for f in origins.flows.values() if f['client'][1] == 49152]
        reset_flows = [f for f in origins.flows.values() if f['client'][1] == 49153]
        assert len(initial_flows) == len(reset_flows) == 1
        flow, reset_flow = initial_flows[0], reset_flows[0]
        assert reset_flow['synacks'] and reset_flow['final_acks']
        assert flow['synacks'] and flow['final_acks']
        records = []
        for stamp, frame in pcap_frames(path):
            packet = wire_tcp(frame)
            if packet is None or {packet['sport'], packet['dport']} != {49152, 18191}:
                continue
            assert not packet['flags'] & 4
            tcp = packet['tcp']
            offset = timestamp_offset(tcp)
            assert offset is not None
            tsval, tsecr = struct.unpack('!II', tcp[offset:offset + 8])
            row = {'time': stamp, 'sport': packet['sport'], 'flags': packet['flags'],
                   'tsval': tsval, 'tsecr': tsecr, 'wire_seq': packet['wire_seq'],
                   'wire_ack': packet['wire_ack'], 'syn': bool(packet['flags'] & 2)}
            if not row['syn']:
                relative = origins.relative(frame)
                row.update(seq=relative['seq'], ack=relative['ack'], data=[])
                if packet['body'] or packet['flags'] & 1:
                    row['data'].append({'offset': row['seq'], 'length': len(packet['body']),
                                        'bytes': packet['body'].hex(),
                                        'fin': bool(packet['flags'] & 1)})
            else:
                assert not packet['body'], 'ordinary SYN must have no intermediate envelope'
            records.append(row)
        syn = next(p for p in records if p['sport'] == 49152 and p['flags'] == 2)
        synack = next(p for p in records if p['sport'] == 18191 and p['flags'] == 0x12)
        setup_ack = next(p for p in records if p['sport'] == 49152 and p['flags'] == 0x10)
        assert syn['tsval'] == 0xfffffff0 and not syn['tsecr']
        assert synack['tsecr'] == syn['tsval']
        assert setup_ack['seq'] == setup_ack['ack'] == 0
        assert setup_ack['tsval'] == 0xfffffff1 and setup_ack['tsecr'] == synack['tsval']
        sent = [p for p in records if p['sport'] == 49152 and not p['syn'] and
                p is not setup_ack]
        assert len(sent) == 5, sent
        old, wrapped, old_tail, fin, final_ack = sent
        assert [p['seq'] for p in sent] == [0, 0, 19, 19, 20]
        assert [p['tsval'] for p in sent] == [0xfffffff0, 0x10, 0xfffffff0, 0x11, 0x12]
        for packet, payload, offset in ((old, rejected, 0), (wrapped, accepted, 0),
                                        (old_tail, rejected, 19)):
            assert packet['flags'] == 0x18 and packet['data'] == [
                {'offset': offset, 'length': 19, 'bytes': payload.hex(), 'fin': False}]
        assert fin['flags'] == 0x11 and fin['data'] == [
            {'offset': 19, 'length': 0, 'bytes': '', 'fin': True}]
        assert final_ack['flags'] == 0x10 and not final_ack['data'] and final_ack['ack'] == 20
        replies = [p for p in records if p['sport'] == 18191 and not p['syn']]
        def rejection_reply(request, following, ack, echo):
            matches = [p for p in replies if request['time'] < p['time'] < following['time']
                       and p['ack'] == ack and p['tsecr'] == echo and not p['data']]
            assert matches, (request, following, replies)
            return matches[0]
        first_reply = rejection_reply(old, wrapped, 0, 0xfffffff1)
        tail_reply = rejection_reply(old_tail, fin, 19, 0x10)
        assert 0 < ((wrapped['tsval'] - setup_ack['tsval']) & MASK32) < 0x80000000
        data = [p for p in replies if any(d['length'] for d in p['data'])]
        assert data and all(p['tsecr'] == 0x11 and all(
            d['offset'] == 0 and d['length'] == 19 and d['bytes'] == rejected.hex()
            for d in p['data'] if d['length']) for p in data)
        fins = [p for p in replies if p['flags'] & 1]
        assert fins and all(p['ack'] in (19, 20) and p['tsecr'] == 0x11 and
                            p['seq'] + sum(d['length'] for d in p['data']) == 19 and
                            [d['offset'] + d['length'] for d in p['data'] if d['fin']] == [19]
                            for p in fins)
        # The application may queue its FIN before the Rx FIN-retirement ACK
        # snapshot reaches Tx. Require that final cumulative ACK on the wire,
        # whether carried by FIN itself or the following ACK-only segment.
        assert all(p['time'] > fin['time'] for p in fins)
        assert any(p['ack'] == 20 and p['time'] <= final_ack['time'] for p in replies)
        assert final_ack['time'] > fins[0]['time'] and final_ack['tsecr'] == fins[0]['tsval']
        live_reset = []
        for stamp, frame in pcap_frames(path):
            raw = wire_tcp(frame)
            if raw is None or {raw['sport'], raw['dport']} != {49153, 18191}:
                continue
            if raw['flags'] & 2:
                assert timestamp_offset(raw['tcp']) is not None
                continue
            packet = origins.relative(frame)
            packet.pop('tcp')
            packet['data_length'] = len(packet.pop('body'))
            packet['time'] = stamp
            live_reset.append(packet)
        resets = [p for p in live_reset if p['flags'] & 4]
        assert len(resets) == 1
        reset = resets[0]
        assert reset['sport'] == 49153 and reset['flags'] == 0x14 and not reset['options']
        assert reset['seq'] == reset['ack'] == reset['data_length'] == 0
        assert not any(p['flags'] & 1 or p['data_length'] for p in live_reset)
        assert not any(p['sport'] == 18191 and p['time'] > reset['time'] for p in live_reset)
        evidence[side] = {'syn': syn, 'synack': synack, 'setup_ack': setup_ack,
                          'old_data': old, 'old_data_reply': first_reply,
                          'wrapped_data': wrapped, 'old_tail': old_tail,
                          'old_tail_reply': tail_reply, 'valid_fin': fin,
                          'reverse_data': data, 'reverse_fins': fins, 'final_ack': final_ack,
                          'origins': origins.evidence(),
                          'live_reset_without_ts': reset,
                          'live_reset_no_reflection': True}
    (out / 'paws-wrap-evidence.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print('# actual native PAWS: rejected old bytes, accepted wrapped TS and exact FIN passed')


def passive_tuple(directory):
    out = Path(directory)
    controls = json.loads((out / 'passive-controls.json').read_text())
    assert controls == {'guarded_total': 1, 'guarded_pending': 0, 'race_pending': 1,
                        'duplicate_pending': 1, 'released_total': 0,
                        'factory_calls': 3, 'stop_callbacks': 1}
    evidence = {'controlled_server_paths': controls}
    for side in ('client', 'server'):
        path = out / (side + '-side.pcap')
        packets, headers = reset_packets(path), admission_packets(path)
        assert not any(p['flags'] & 4 for p in headers)
        origins = capture_origins(path)
        paired = [f for f in origins.flows.values() if f['client'][1] == 49152 and
                  f['client'][3] == 18191 and f['synacks']]
        assert len(paired) == 2
        def first_syn(flow):
            return min(p['time'] for p in headers if p['flags'] & 0x17 == 2 and
                       p['key'] == flow['client'] and p['wire_seq'] == flow['client_isn'])
        prior, current = sorted(paired, key=first_syn)
        assert current['client'] == prior['client']
        def after(value, previous):
            return 0 < (value - previous) & MASK32 < 0x80000000
        assert after(current['client_isn'], (prior['client_isn'] + 20) & MASK32)
        assert after(current['server_isn'], (prior['server_isn'] + 20) & MASK32)
        assert current['final_acks'] and prior['final_acks']
        flows = {}
        for port, server_port in ((49152, 18191), (49153, 18191), (49154, 18195)):
            candidates = [f for f in origins.flows.values() if f['client'][1] == port and
                          f['client'][3] == server_port and f['synacks']]
            flow = prior if port == 49152 else candidates[0]
            streams = {}
            for source, key, base in (
                    (port, flow['client'], (flow['client_isn'] + 1) & MASK32),
                    (server_port, WireOrigins.reverse(flow['client']),
                     (flow['server_isn'] + 1) & MASK32)):
                streams[source] = decode(path, b'native-mx-byte-path', key, base)
            flows[port] = streams
        reuse_streams = {
            49152: decode(path, b'reuse-mx-byte-path!', current['client'],
                          (current['client_isn'] + 1) & MASK32),
            18191: decode(path, b'reuse-mx-byte-path!', WireOrigins.reverse(current['client']),
                          (current['server_isn'] + 1) & MASK32)}
        new_start = first_syn(current)
        old_requests = [p for p in packets if p['key'] == prior['client'] and
                        p['seq_base'] == (prior['client_isn'] + 1) & MASK32 and
                        p['time'] > new_start and (p['data_length'] or p['flags'] & 1)]
        assert len(old_requests) == 2 and [p['flags'] for p in old_requests] == [0x18, 0x11]
        assert [p['seq'] for p in old_requests] == [0, 19]
        old_replies = [p for p in packets if p['key'] == WireOrigins.reverse(prior['client']) and
                       p['seq_base'] == (prior['server_isn'] + 1) & MASK32 and
                       p['time'] > new_start]
        assert len(old_replies) == 2 and all(p['seq'] == p['ack'] == 20 and
               p['flags'] == 0x10 and not p['data_length'] for p in old_replies)
        new_data = min(p['time'] for p in reuse_streams[49152]['data'])
        assert all(p['time'] < new_data for p in old_requests)
        assert all(a['time'] < b['time'] for a, b in zip(old_requests, old_replies))
        refused = [p for p in headers if p['key'] == prior['client'] and
                   p['flags'] & 0x17 == 2 and
                   p['wire_seq'] == (prior['client_isn'] + 20) & MASK32]
        assert len(refused) == 1
        marker = min(p['time'] for p in flows[49153][49153]['data'])
        first_fin = min(p['time'] for p in packets if p['key'] == prior['client'] and
                        p['flags'] & 1 and p['time'] < new_start)
        assert first_fin < refused[0]['time'] < marker
        assert not any(p['key'] == WireOrigins.reverse(prior['client']) and
                       refused[0]['time'] < p['time'] < marker for p in headers)
        evidence[side] = {'refused_syn': refused[0], 'flows': flows,
                          'ordered_fresh_data_marker': marker, 'prior_origins': prior,
                          'reused_origins': current, 'reused_streams': reuse_streams,
                          'old_requests_with_new_owner': old_requests,
                          'old_history_replies_with_new_owner': old_replies}
    (out / 'passive-tuple-evidence.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print('# passive tuple: conservative SYN refusal, pending race, FIFO and eligible reuse '
          'passed')


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "pair-reorder":
        pair(sys.argv[2], True)
        raise SystemExit(0)
    if len(sys.argv) == 3 and sys.argv[1] == "pair":
        pair(sys.argv[2])
    elif len(sys.argv) == 5 and sys.argv[1] == "wire-gate":
        wire_gate(sys.argv[2], sys.argv[3], sys.argv[4])
    elif len(sys.argv) == 5 and sys.argv[1] == "ack-bands":
        ack_bands(sys.argv[2], sys.argv[3], sys.argv[4])
    elif len(sys.argv) == 4 and sys.argv[1] == "syn-evidence":
        syn_evidence(sys.argv[2], sys.argv[3] == "retransmit")
    elif len(sys.argv) == 4 and sys.argv[1] == "positive-window-probe":
        positive_window_probe(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 3 and sys.argv[1] == "positive-window":
        positive_window(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "paws-wrap":
        paws_wrap(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "passive-tuple":
        passive_tuple(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "native-lifecycle":
        native_lifecycle(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "conservative-tuple":
        conservative_tuple(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "shared-admission":
        shared_admission(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "inject":
        inject(sys.argv[2])
    elif len(sys.argv) == 4 and sys.argv[1] == "fin-window-probe":
        fin_window_probe(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 3 and sys.argv[1] == "fin-window":
        fin_window(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "native-history":
        native_history(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "half-close":
        half_close(sys.argv[2])
    elif len(sys.argv) == 4 and sys.argv[1] == "abort-inject":
        abort_inject(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 3 and sys.argv[1] == "abort":
        abort_pair(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "rst":
        reset_pair(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "final-ack":
        final_ack(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] in ("window", "restart", "lost-window"):
        window(sys.argv[2], sys.argv[1] == "restart", sys.argv[1] == "lost-window")
    elif len(sys.argv) == 4 and sys.argv[1] == "check":
        check(sys.argv[2], sys.argv[3])
    else:
        raise SystemExit("usage: zutcprecovery.py inject output.json | "
                         "check directory mode | window/restart/lost-window directory")
