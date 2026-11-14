#!/usr/bin/env python3
# (c) Copyright 2026 Huw Rogers
# This code is licensed by the MIT license (see LICENSE for details).

"""Private fixture faults and assertions for the intermediate plaintext wire.
CIDs are the fixture's default eight bytes, validated from captured Initials.
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
    ip = frame[14:]
    if frame[12:14] == b'\x08\x00' and len(ip) >= 20 and ip[9] == 6:
        tcp = ip[(ip[0] & 15) * 4:int.from_bytes(ip[2:4], 'big')]
        source, target = ip[12:16], ip[16:20]
    elif frame[12:14] == b'\x86\xdd' and len(ip) >= 40 and ip[6] == 6:
        tcp = ip[40:40 + int.from_bytes(ip[4:6], 'big')]
        source, target = ip[8:24], ip[24:40]
    else:
        return None
    if len(tcp) < 20 or (tcp[12] >> 4) * 4 > len(tcp):
        return None
    sport, dport, seq, ack = struct.unpack('!HHII', tcp[:12])
    return {'key': (source.hex(), sport, target.hex(), dport),
            'tcp': tcp, 'body': tcp[(tcp[12] >> 4) * 4:],
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
    """Learn actual SYN incarnations; a denied new CID cannot replace an old flow."""
    def __init__(self):
        self.flows = {}
        self.shorts = {}

    @staticmethod
    def reverse(key):
        return key[2:] + key[:2]

    def observe(self, frame):
        packet = wire_tcp(frame)
        if packet is None:
            return
        body, flags = packet['body'], packet['flags']
        if len(body) < 7 or not body[0] & 128 or not flags & 2:
            return
        dl = body[5]
        assert dl <= 20 and len(body) >= 7 + dl
        sl = body[6 + dl]
        dcid, scid = body[6:6 + dl].hex(), body[7 + dl:7 + dl + sl].hex()
        assert sl <= 20 and len(scid) == sl * 2
        key = packet['key']
        offset = 7 + dl + sl
        assert offset < len(body)
        width = 1 << (body[offset] >> 6)
        offset += width
        pn_width = (body[0] & 3) + 1
        pn = int.from_bytes(body[offset:offset + pn_width], 'big')
        if flags & 0x17 == 0x02:
            assert packet['wire_ack'] == 0, packet
            identity = (key, dcid, scid)
            flow = self.flows.get(identity)
            if flow is None:
                flow = {'client': key, 'original_cid': dcid, 'client_cid': scid,
                        'client_isn': packet['wire_seq'], 'syns': 0, 'synacks': 0,
                        'final_acks': 0, 'client_syn_pns': [], 'server_syn_pns': []}
                self.flows[identity] = flow
            assert flow['client_isn'] == packet['wire_seq'], (flow, packet)
            flow['syns'] += 1
            if pn not in flow['client_syn_pns']:
                flow['client_syn_pns'].append(pn)
            self.shorts.setdefault((key, dcid), (flow, False))
        else:
            assert flags & 0x17 == 0x12, packet
            candidates = [flow for flow in self.flows.values()
                          if flow['client'] == self.reverse(key) and flow['client_cid'] == dcid
                          and (flow['client_isn'] + 1) & MASK32 == packet['wire_ack']]
            assert len(candidates) == 1, (packet, candidates)
            flow = candidates[0]
            if flow['synacks']:
                assert flow['server_isn'] == packet['wire_seq'] and flow['server_cid'] == scid
            flow['server_isn'], flow['server_cid'] = packet['wire_seq'], scid
            flow['synacks'] += 1
            if pn not in flow['server_syn_pns']:
                flow['server_syn_pns'].append(pn)
            self.shorts[(flow['client'], scid)] = (flow, False)
            self.shorts[(key, flow['client_cid'])] = (flow, True)

    def relative(self, frame):
        packet = wire_tcp(frame)
        if packet is None or not packet['body'] or packet['body'][0] & 128:
            return None
        cid = packet['body'][1:9].hex()
        match = self.shorts.get((packet['key'], cid))
        if packet['flags'] & 0x17 == 0x04:
            pending = [flow for flow in self.flows.values()
                       if flow['client'] == packet['key'] and flow['original_cid'] == cid
                       and not flow['synacks'] and packet['wire_ack'] == 0
                       and (flow['client_isn'] + 1) & MASK32 == packet['wire_seq']]
            if pending:
                assert len(pending) == 1, (packet, pending)
                match = pending[0], False
        assert match is not None, ('short packet lacks captured SYN origin', packet)
        flow, server = match
        seq_base = (flow['server_isn' if server else 'client_isn'] + 1) & MASK32
        peer = 'client_isn' if server else 'server_isn'
        ack_base = (flow[peer] + 1) & MASK32 if peer in flow else None
        if packet['flags'] & 16:
            assert ack_base is not None, packet
            ack = (packet['wire_ack'] - ack_base) & MASK32
        else:
            assert packet['wire_ack'] == 0 and packet['flags'] & 4, packet
            ack = 0
        packet.update(seq=(packet['wire_seq'] - seq_base) & MASK32, ack=ack,
                      seq_base=seq_base, ack_base=ack_base, cid=cid)
        return packet

    def final_ack(self, frame):
        packet = wire_tcp(frame)
        if packet is None or packet['flags'] & 0x17 != 0x10:
            return
        body = packet['body']
        if len(body) < 7 or not body[0] & 128 or not int.from_bytes(body[1:5], 'big'):
            return
        dl = body[5]
        dcid = body[6:6 + dl].hex()
        sl = body[6 + dl]
        scid = body[7 + dl:7 + dl + sl].hex()
        for flow in self.flows.values():
            if (flow['client'] == packet['key'] and flow['synacks'] and
                    flow['client_cid'] == scid and dcid in (flow['original_cid'], flow['server_cid'])):
                assert packet['wire_seq'] == (flow['client_isn'] + 1) & MASK32
                assert packet['wire_ack'] == (flow['server_isn'] + 1) & MASK32
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
    if retransmit:
        assert any(len(flow['client_syn_pns']) >= 2 and len(flow['server_syn_pns']) >= 2
                   for flow in paired), paired
    (out / 'tcp-origins-evidence.json').write_text(json.dumps({
        'handshakes': paired, 'retransmission_required': retransmit,
    }, indent=2) + '\n')
    print('# native SYN origins: anchored data bases, final ACK and stable retransmissions passed')


def selector_evidence(directory, frontier=None):
    out = Path(directory)
    learned = json.loads((out / 'wire-origins.json').read_text())
    actual = [flow for flow in capture_origins(out / 'client-side.pcap').evidence()
              if flow['synacks'] and flow['client_cid'] == learned['client_cid'] and
              flow['original_cid'] == learned['original_cid'] and
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
    """Inject an interior TCP ACK using a real dropped peer packet's PN/ranges.
    Frontier 536 crosses only this fixture's >=1024 ACK-loss selector. PING supplies
    an intermediate packet-receipt witness."""
    if (
        os.environ.get('ZXDP_RUN_FIXTURE') != '1'
        or os.readlink('/proc/self/ns/net') == os.readlink('/proc/1/ns/net')
    ):
        raise RuntimeError('requires the private native packet fixture')
    out = Path(filename)
    rx = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(3))
    rx.bind(('zutcpsa', 0))
    tx = socket.socket(socket.AF_PACKET, socket.SOCK_RAW)
    tx.bind(('zutcpca', 0))
    out.with_suffix('.ready').write_text('ready')

    def var(b, i):
        n = 1 << (b[i] >> 6)
        assert i + n <= len(b)
        return (int.from_bytes(b[i:i + n], 'big') & (1 << 8 * n - 2) - 1, i + n)

    def checksum(b):
        if len(b) & 1:
            b += b'\x00'
        total = sum(struct.unpack('!%dH' % (len(b) // 2), b))
        while total >> 16:
            total = (total & 65535) + (total >> 16)
        return ~total & 65535
    origins = WireOrigins()
    first = None
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        rx.settimeout(deadline - time.monotonic())
        wire, address = rx.recvfrom(65535)
        origins.observe(wire)
        eth = int.from_bytes(wire[12:14], 'big')
        if eth == 2048:
            ip = bytearray(wire[14:14 + int.from_bytes(wire[16:18], 'big')])
            if ip[9] != 6:
                continue
            iphead = (ip[0] & 15) * 4
        elif eth == 34525:
            ip = bytearray(wire[14:14 + 40 + int.from_bytes(wire[18:20], 'big')])
            if ip[6] != 6:
                continue
            iphead = 40
        else:
            continue
        tcp = ip[iphead:]
        head = (tcp[12] >> 4) * 4
        body = bytes(tcp[head:])
        if not body or body[0] & 128:
            continue
        sport, dport = struct.unpack('!HH', tcp[:4])
        if dport == 18181 and len(ip) >= 1024 and (first is None):
            first = (time.monotonic(), time.time())
        if sport != 18181 or first is None:
            continue
        sh = 1 + 8 + (body[0] & 3) + 1
        if sh >= len(body) or body[sh] not in (2, 3):
            continue
        relative = origins.relative(wire)
        frontier = relative['ack']
        if frontier < 1072:
            continue
        inner = sh
        t = body[sh]
        i = sh + 1
        largest, i = var(body, i)
        delay, i = var(body, i)
        nr, i = var(body, i)
        length, i = var(body, i)
        for n in range(nr):
            gap, i = var(body, i)
            length, i = var(body, i)
        if t == 3:
            for n in range(3):
                v, i = var(body, i)
        partial = 536
        payload = body[:sh] + body[inner:i] + b'\x01'
        tcp = tcp[:head] + payload
        tcp[8:12] = ((relative['ack_base'] + partial) & MASK32).to_bytes(4, 'big')
        tcp[16:18] = b'\x00\x00'
        if eth == 2048:
            pseudo = bytes(ip[12:20]) + b'\x00\x06' + len(tcp).to_bytes(2, 'big')
            header = ip[:iphead]
            header[2:4] = (iphead + len(tcp)).to_bytes(2, 'big')
            header[10:12] = b'\x00\x00'
            header[10:12] = checksum(bytes(header)).to_bytes(2, 'big')
        else:
            pseudo = bytes(ip[8:40]) + len(tcp).to_bytes(4, 'big') + b'\x00\x00\x00\x06'
            header = ip[:40]
            header[4:6] = len(tcp).to_bytes(2, 'big')
        tcp[16:18] = checksum(pseudo + bytes(tcp)).to_bytes(2, 'big')
        packet = wire[:14] + bytes(header) + bytes(tcp)
        wait = first[0] + 0.15 - time.monotonic()
        if wait > 0:
            time.sleep(wait)
        stamp = time.time()
        sent = tx.send(packet)
        assert sent == len(packet)
        out.write_text(json.dumps({
            'partial_frontier': partial,
            'original_frontier': frontier,
            'wire_original_frontier': relative['wire_ack'],
            'ack_base': relative['ack_base'],
            'wire_partial_frontier': (relative['ack_base'] + partial) & MASK32,
            'peer_pn': int.from_bytes(body[9:sh], 'big'),
            'ack_largest_client_pn': largest,
            'first_data_time': first[1],
            'injected_time': stamp,
            'delay_from_first_data': stamp - first[1],
            'packet_sha256': hashlib.sha256(packet).hexdigest(),
        }, indent=2) + '\n')
        rx.close()
        tx.close()
        break
    else:
        raise RuntimeError('no established DATA/ACK suitable for partial ACK injection')


def decode(path, expected, cid=None):
    origins = capture_origins(path)
    raw = path.read_bytes()
    formats = {
        b'\xd4\xc3\xb2\xa1': ('<', 1000000),
        b'M<\xb2\xa1': ('<', 1000000000),
        b'\xa1\xb2\xc3\xd4': ('>', 1000000),
        b'\xa1\xb2<M': ('>', 1000000000),
    }
    endian, scale = formats[raw[:4]]
    assert struct.unpack(endian + 'I', raw[20:24])[0] == 1
    pos = 24
    data = []
    acks = []
    controls = []
    coverage = bytearray(len(expected))
    cids = set()
    unknown = set()
    packets = 0
    while pos < len(raw):
        sec, sub, cap, wire = struct.unpack(endian + 'IIII', raw[pos:pos + 16])
        pos += 16
        frame = raw[pos:pos + cap]
        pos += cap
        packets += 1
        assert len(frame) == cap and cap == wire
        eth = int.from_bytes(frame[12:14], 'big')
        ip = frame[14:]
        if eth == 2048:
            if ip[9] != 6:
                continue
            tcp = ip[(ip[0] & 15) * 4:int.from_bytes(ip[2:4], 'big')]
        elif eth == 34525:
            if ip[6] != 6:
                continue
            tcp = ip[40:40 + int.from_bytes(ip[4:6], 'big')]
        else:
            continue
        p = tcp[(tcp[12] >> 4) * 4:]
        stamp = sec + sub / scale
        if not p:
            continue
        if p[0] & 128:
            dl = p[5]
            sl = p[6 + dl]
            cids.update([dl, sl])
            continue
        assert p[0] & 64
        destination = p[1:9].hex()
        if cid is not None and destination != cid:
            continue
        i = 1 + 8 + (p[0] & 3) + 1
        pn = int.from_bytes(p[9:i], 'big')
        relative = origins.relative(frame)
        seq, frontier = relative['seq'], relative['ack']
        width = int.from_bytes(tcp[14:16], 'big')
        ack = {'wire_seq': relative['wire_seq'], 'wire_ack': relative['wire_ack'],
               'seq_base': relative['seq_base'], 'ack_base': relative['ack_base'],
               'time': stamp, 'pn': pn, 'next': frontier, 'seq': seq,
               'window': width, 'ranges': [], 'cid': destination}
        if tcp[13] & 16:
            acks.append(ack)

        def var():
            nonlocal i
            n = 1 << (p[i] >> 6)
            assert i + n <= len(p)
            v = int.from_bytes(p[i:i + n], 'big') & (1 << n * 8 - 2) - 1
            i += n
            return v
        while i < len(p):
            t = p[i]
            i += 1
            if t in (0, 1, 30):
                if t == 1 and seq + 1 == len(expected) // 2:
                    controls.append({'time': stamp, 'pn': pn, 'type': 1,
                                     'value': seq + 1, 'cid': destination})
                continue
            if t in (2, 3):
                largest = var()
                delay = var()
                nr = var()
                first = var()
                low = largest - first
                ranges = [[low, largest]]
                for unused in range(nr):
                    gap = var()
                    length = var()
                    high = low - gap - 2
                    low = high - length
                    ranges.append([low, high])
                if t == 3:
                    var()
                    var()
                    var()
                ack['ranges'] = ranges
            elif 8 <= t <= 15:
                offset = var() if t & 4 else 0
                length = var() if t & 2 else len(p) - i
                body = p[i:i + length]
                assert len(body) == length
                i += length
                assert seq == offset & 0xffffffff, (path, seq, offset)
                assert body == expected[offset:offset + length], (path, offset, length)
                coverage[offset:offset + length] = b'\x01' * length
                if length:
                    data.append({
                        'time': stamp,
                        'pn': pn,
                        'offset': offset,
                        'length': length,
                        'fin': bool(t & 1),
                        'cid': destination,
                        'sha256': hashlib.sha256(body).hexdigest(),
                    })
            elif t == 4:
                var()
                var()
            elif t in (16, 20, 31):
                raise AssertionError('native legacy byte ACK/window carrier')
            elif t in (5, 25):
                var()
            elif t in (26, 27):
                i += 8
            elif t in (28, 29):
                break
            else:
                unknown.add(t)
                break
    assert cids == {8}, (path, cids)
    assert all(coverage), (path, sum(coverage), len(expected))
    unique = []
    seen = set()
    maximum = -1
    reordered = False
    for d in data:
        offset = d['offset']
        if offset in seen:
            continue
        seen.add(offset)
        unique.append(offset)
        if offset < maximum:
            reordered = True
        maximum = max(maximum, offset)
    return {
        'packets': packets,
        'data': data,
        'acks': acks,
        'controls': controls,
        'first_seen_offsets': unique,
        'reordered': reordered,
        'payload_bytes_covered': sum(coverage),
        'expected_bytes': len(expected),
        'unknown_control_types': sorted(unknown),
        'pcap_sha256': hashlib.sha256(raw).hexdigest(),
        'tcp_origins': origins.evidence(),
    }



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
        assert len({packet["pn"] for packet in copies}) == len(copies)
        gaps = [round(copies[i + 1]["time"] - copies[i]["time"], 6)
                for i in range(len(copies) - 1)]
        evidence = {"offset": 0, "length": first["length"], "gaps_seconds": gaps,
                    "packet_numbers": [packet["pn"] for packet in copies],
                    "payload_sha256": first["sha256"]}
        if mode == "ack-loss":
            assert gaps[0] > .15 and gaps[1] > gaps[0] * 1.5
        else:
            injection = json.loads((out / "injection.json").read_text())
            peer = injection["peer_pn"]
            partial = injection["partial_frontier"]
            assert 0 < partial < first["length"]
            receipts = [ack for ack in streams["client"]["acks"]
                        if ack["next"] == partial and ack["pn"] == peer]
            assert len(receipts) == 1
            stamp = receipts[0]["time"]
            delay = stamp - copies[0]["time"]
            restart = copies[1]["time"] - stamp
            assert .12 < delay < .19 and .15 < restart < .30 and gaps[0] > .30
            assert any(ack["time"] >= stamp and any(lo <= peer <= hi for lo, hi in ack["ranges"])
                       for ack in streams["server"]["acks"])
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
                cids = {p['cid'] for p in stream['data']
                        if p['offset'] == 0 and p['length'] == size}
                assert len(cids) == 1, (side, size, cids)
                streams[side] = decode(out / ('to-' + side + '.pcap'),
                                       message[:size] * 2, next(iter(cids)))
        for side, peer in (('client', 'server'), ('server', 'client')):
            stream = streams[side]
            other = streams[peer]
            capture = (out / ('to-' + side + '.capture')).read_text()
            assert '0 packets dropped by kernel' in capture
            assert not stream['unknown_control_types']
            first = next(p for p in stream['data'] if p['offset'] == 0)
            second = next(p for p in stream['data'] if p['offset'] == size)
            assert first['length'] == second['length'] == size
            assert second['time'] - first['time'] > 3.2
            probes = [p for p in stream['controls'] if p['type'] == 1 and p['value'] == size]
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


def retired_recovery(packets, active, data, first_pn=0, next_pn=None):
    """Match a retired ACK to repeated byte-bearing DATA or FIN, never an ACK."""
    peer = [p for p in packets if p['sport'] != active]
    peer_cid = data[0]['cid']
    active_cid = next(p['cid'] for p in packets if p['sport'] == active and p['flags'] & 8)
    payloads = {(p['cid'], p['pn']): p for p in data}
    requests = []
    for packet in peer:
        if packet['flags'] & 4 or not packet['flags'] & 9:
            continue
        payload = payloads.get((packet['cid'], packet['pn']))
        size = payload['length'] if payload else 0
        if packet['flags'] & 8:
            assert payload and size > 0 and packet['seq'] == payload['offset'], packet
        if packet['flags'] & 1:
            assert packet['seq'] + size == 19, packet
        assert packet['seq'] < 20 and packet['cid'] == peer_cid, packet
        requests.append(dict(packet, data_length=size))
    replies = [p for p in packets if p['sport'] == active and p['flags'] == 0x10
               and p['pn_length'] == 4 and p['body'] == '00'
               and first_pn <= p['pn'] and (next_pn is None or p['pn'] < next_pn)]
    assert replies and all(p['seq'] == 20 and p['ack'] == 20 and
                           p['cid'] == active_cid for p in replies), replies
    chains = []
    previous = float('-inf')
    for reply in replies:
        candidates = [p for p in requests if previous < p['time'] < reply['time']]
        assert candidates, ('retired ACK without a preceding DATA/FIN request', reply)
        request = candidates[-1]
        # A carrier PN changes on RTO; TCP bytes/FIN are the recovery identity.
        earlier = [p for p in requests if p['time'] < request['time'] and
                   p['cid'] == request['cid'] and p['seq'] == request['seq'] and
                   bool(p['flags'] & 8) == bool(request['flags'] & 8) and
                   p['data_length'] == request['data_length']]
        assert earlier, ('history response must follow retransmitted DATA/FIN', request)
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
    cid = next(p['cid'] for p in peer if p['flags'] & 8)
    stream = decode(out / 'peer-fin.pcap', b'native-mx-byte-path', cid)
    chains = retired_recovery(packets, active, stream['data'])
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
    eth = int.from_bytes(frame[12:14], 'big')
    ip = frame[14:]
    if eth == 2048 and ip[9] == 6:
        tcp = ip[(ip[0] & 15) * 4:int.from_bytes(ip[2:4], 'big')]
    elif eth == 34525 and ip[6] == 6:
        tcp = ip[40:40 + int.from_bytes(ip[4:6], 'big')]
    else:
        return None
    if len(tcp) < 20:
        return None
    body = tcp[(tcp[12] >> 4) * 4:]
    if not body or body[0] & 128:
        return None
    inner = 1 + 8 + (body[0] & 3) + 1
    relative = origins.relative(frame)
    return {
        'wire_seq': relative['wire_seq'], 'wire_ack': relative['wire_ack'],
        'seq_base': relative['seq_base'], 'ack_base': relative['ack_base'],
        'sport': int.from_bytes(tcp[:2], 'big'),
        'seq': relative['seq'],
        'ack': relative['ack'],
        'flags': tcp[13], 'window': int.from_bytes(tcp[14:16], 'big'),
        'ping': len(body) == inner + 1 and body[inner] == 1,
    }


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
            if packet['ping'] and packet['seq'] == 18 and zero is not None:
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
    probes = [p for p in packets if p['sport'] == initiator and p['ping']]
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
    raw = path.read_bytes()
    formats = {
        b'\xd4\xc3\xb2\xa1': ('<', 1000000),
        b'M<\xb2\xa1': ('<', 1000000000),
        b'\xa1\xb2\xc3\xd4': ('>', 1000000),
        b'\xa1\xb2<M': ('>', 1000000000),
    }
    endian, scale = formats[raw[:4]]
    assert struct.unpack(endian + 'I', raw[20:24])[0] == 1
    packets = []
    pos = 24
    while pos < len(raw):
        sec, sub, cap, wire = struct.unpack(endian + 'IIII', raw[pos:pos + 16])
        pos += 16
        frame = raw[pos:pos + cap]
        pos += cap
        assert cap == wire and len(frame) == cap
        packet = fin_window_packet(frame, origins)
        if packet is None:
            continue
        ip = frame[14:]
        tcp = ip[(ip[0] & 15) * 4:] if frame[12:14] == b'\x08\x00' else ip[40:]
        body = tcp[(tcp[12] >> 4) * 4:]
        head = 1 + 8 + (body[0] & 3) + 1
        packet.update({
            'time': sec + sub / scale,
            'dport': int.from_bytes(tcp[2:4], 'big'),
            'pn': int.from_bytes(body[9:head], 'big'),
            'cid': body[1:9].hex(),
            'pn_length': (body[0] & 3) + 1,
            'body': body[head:].hex(),
        })
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
        assert reset['pn_length'] == 4 and reset['body'] == '00', reset
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
    data_length = next((p['length'] for p in streams['server']['data']
                        if p['pn'] == first_fin['pn']), 0)
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
    packet = fin_window_packet(frame, origins)
    if packet is None:
        return None
    ip = frame[14:]
    tcp = ip[(ip[0] & 15) * 4:] if frame[12:14] == b'\x08\x00' else ip[40:]
    body = tcp[(tcp[12] >> 4) * 4:]
    head = 1 + 8 + (body[0] & 3) + 1
    packet.update({'dport': int.from_bytes(tcp[2:4], 'big'),
                   'pn': int.from_bytes(body[9:head], 'big')})
    return packet, tcp, body


def abort_checksum(data):
    if len(data) & 1:
        data += b'\x00'
    total = sum(struct.unpack('!%dH' % (len(data) // 2), data))
    while total >> 16:
        total = (total & 65535) + (total >> 16)
    return ~total & 65535


def abort_packet(template, seq, pn, frames, flags):
    ip = bytearray(template[14:])
    v4 = template[12:14] == b'\x08\x00'
    iphead = (ip[0] & 15) * 4 if v4 else 40
    tcp = bytearray(ip[iphead:iphead + 20])
    assert tcp[12] >> 4 == 5
    original = bytes(ip[iphead + 20:])
    assert original[0] & 64 and not original[0] & 128
    payload = bytes([(original[0] & 0xfc) | 3]) + original[1:9] + pn.to_bytes(4, 'big') + frames
    tcp[4:8] = seq.to_bytes(4, 'big')
    tcp[13] = flags
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


def abort_ranges(body):
    i = 1 + 8 + (body[0] & 3) + 1
    ranges = []

    def var():
        nonlocal i
        n = 1 << (body[i] >> 6)
        assert i + n <= len(body)
        value = int.from_bytes(body[i:i + n], 'big') & ((1 << (8 * n - 2)) - 1)
        i += n
        return value

    while i < len(body):
        kind = body[i]
        i += 1
        if kind in (0, 1, 30):
            continue
        if kind in (2, 3):
            largest, delay, count, first = var(), var(), var(), var()
            low = largest - first
            ranges.append([low, largest])
            for unused in range(count):
                gap, length = var(), var()
                high = low - gap - 2
                low = high - length
                ranges.append([low, high])
            if kind == 3:
                var()
                var()
                var()
        elif 8 <= kind <= 15:
            if kind & 4:
                var()
            size = var() if kind & 2 else len(body) - i
            i += size
        else:
            break
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
    original = abort_tcp(template, origins)[0]
    pn = original['pn'] + 100
    future = abort_packet(template, (original['seq_base'] + 32) & MASK32, pn, b'\x0e\x20\x01?', 0x18)
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
        ranges = abort_ranges(body)
        if any(low <= pn <= high for low, high in ranges):
            assert packet['ack'] == 19, packet
            witness = {'header': packet, 'packet_ranges': ranges}
            break
    assert witness is not None, 'future DATA was not acknowledged as retained'
    # The FIN's byte position is in-window, but conflicts with that retained byte.
    final = abort_packet(template, (original['seq_base'] + 19) & MASK32, pn + 1, b'\x0f\x13\x00', 0x11)
    (out / 'conflicting-fin.packet').write_bytes(final)
    assert tx.send(final) == len(final)
    (out / 'fatal-injection.json').write_text(json.dumps({
        'template_pn': original['pn'], 'future_pn': pn, 'fin_pn': pn + 1,
        'future_sequence': 32, 'fin_sequence': 19,
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
        assert reset['body'] == '00' and reset['pn_length'] == 4, reset
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
        assert any(low <= injection['future_pn'] <= high for low, high in
                   injection['retained_future_ack']['packet_ranges'])
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
    assert before[0] > 0 and before[0] == after[0] and after[1] > before[1]
    assert before[2:] == after[2:], (before, after)
    assert (out / 'post-eof').is_file() and (out / 'held-reader').is_file()
    captures = {side: reset_packets(out / (side + '-side.pcap'))
                for side in ('client', 'server')}
    old_headers = [p for p in captures['client'] if 18192 in (p['sport'], p['dport'])]
    active_header = next(p for p in old_headers if p['sport'] == active)
    offsets = [(before[2] - active_header['seq_base']) & MASK32,
               (before[3] - active_header['ack_base']) & MASK32]
    assert offsets == [20, 20], (before, active_header)
    evidence = {'normalized_closed_frontiers': offsets}
    for side, packets in captures.items():
        assert not any(p['flags'] & 4 for p in packets), packets
        old = [p for p in packets if 18192 in (p['sport'], p['dport'])]
        fresh = [p for p in packets if 18193 in (p['sport'], p['dport'])]
        assert old and fresh
        streams = {}
        for port in (18191, 18192):
            cid = next(p['cid'] for p in old if p['sport'] == port)
            streams[port] = decode(out / (side + '-side.pcap'), b'native-mx-byte-path', cid)
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
    chains = retired_recovery(old, active, data, before[1], after[1])
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
    raw = path.read_bytes()
    formats = {b'\xd4\xc3\xb2\xa1': ('<', 1000000), b'M<\xb2\xa1': ('<', 1000000000),
               b'\xa1\xb2\xc3\xd4': ('>', 1000000), b'\xa1\xb2<M': ('>', 1000000000)}
    endian, scale = formats[raw[:4]]
    assert struct.unpack(endian + 'I', raw[20:24])[0] == 1
    packets = []
    pos = 24
    while pos < len(raw):
        sec, sub, cap, wire = struct.unpack(endian + 'IIII', raw[pos:pos + 16])
        pos += 16
        frame = raw[pos:pos + cap]
        pos += cap
        assert cap == wire and len(frame) == cap
        ip = frame[14:]
        if frame[12:14] == b'\x08\x00' and ip[9] == 6:
            tcp = ip[(ip[0] & 15) * 4:int.from_bytes(ip[2:4], 'big')]
        elif frame[12:14] == b'\x86\xdd' and ip[6] == 6:
            tcp = ip[40:40 + int.from_bytes(ip[4:6], 'big')]
        else:
            continue
        body = tcp[(tcp[12] >> 4) * 4:]
        if not body:
            continue
        packets.append({'sport': int.from_bytes(tcp[:2], 'big'),
                        'dport': int.from_bytes(tcp[2:4], 'big'),
                        'flags': tcp[13], 'long': bool(body[0] & 128),
                        'cid': (body[6:6 + body[5]] if body[0] & 128 else body[1:9]).hex(),
                        'version': int.from_bytes(body[1:5], 'big') if body[0] & 128 else None,
                        'time': sec + sub / scale})
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
                cid = next(p['cid'] for p in flow if p['sport'] == source)
                streams[source] = decode(path, b'native-mx-byte-path', cid)
            flows[port] = streams
        if passive:
            denied = [p for p in all_packets if p['sport'] == 18193 and p['dport'] == 18195]
            assert denied and any(p['long'] for p in denied), denied
            assert not any(p['sport'] == 18195 and p['dport'] == 18193 for p in all_packets)
            evidence[side] = {'flows': flows, 'denied_initials': denied}
        else:
            assert not any(18194 in (p['sport'], p['dport']) or
                           18198 in (p['sport'], p['dport']) for p in all_packets)
            pending = [p for p in all_packets if p['sport'] == 18197 and p['dport'] == 18199]
            assert pending and any(p['long'] for p in pending), pending
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
                cid = next(p['cid'] for p in flow if p['sport'] == source)
                streams[source] = decode(path, b'native-mx-byte-path', cid)
            flows[port] = streams
        first_fin = min(p['time'] for p in packets if
                        49152 in (p['sport'], p['dport']) and p['flags'] & 1)
        late_initials = [p for p in envelopes if p['sport'] == 49152 and
                         p['long'] and p['time'] > first_fin]
        assert not late_initials, late_initials
        new_initials = [p for p in envelopes if p['sport'] == 49153 and p['long']]
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
                if packet['ping'] and packet['seq'] == 999 and zero is not None:
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
        stream = decode(path, expected)
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
            probe = next(p for p in packets if p['sport'] == 18192 and p['ping'] and
                         p['seq'] == 999 and p['time'] > zero['time'])
            opening = next(p for p in packets if p['sport'] == 18191 and
                           p['ack'] == frontier and p['window'] == 400 and
                           p['time'] > probe['time'])
            assert not any(zero['time'] < p['time'] < opening['time'] for p in data), phases
            phases.append({'zero': zero, 'probe': probe, 'opening': opening})
        assert any(p['sport'] == 18191 and p['flags'] & 1 for p in packets), packets
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
            assert reset['body'] == '00' and reset['pn_length'] == 4, reset
            assert not any(p['sport'] == receiver and p['time'] > reset['time'] for p in old)
            assert not any(p['flags'] & 1 for p in old), old
            fresh = [p for p in packets if 18193 in (p['sport'], p['dport'])]
            assert fresh and not any(p['flags'] & 4 for p in fresh)
            assert sum(bool(p['flags'] & 1) for p in fresh) >= 2
            streams = {}
            for port in (18193, 18195):
                cid = next(p['cid'] for p in fresh if p['sport'] == port)
                streams[port] = decode(path, b'native-mx-byte-path', cid)
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
                cid = next(p['cid'] for p in old if p['sport'] == port)
                streams[port] = decode(path, b'native-mx-byte-path', cid)
            local_fin = min(p['time'] for p in old if p['sport'] == 18192 and p['flags'] & 1)
            reverse = min(p['time'] for p in streams[18191]['data'])
            assert reverse > local_fin, (local_fin, reverse)
            evidence[side] = {'streams': streams, 'client_fin': local_fin,
                              'first_positive_reverse_data': reverse}
    if stop:
        evidence['actual_data_loss'] = (out / 'hub-stop.loss').read_text()
    (out / 'native-lifecycle-evidence.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print('# native public lifecycle: zero/default FIN or numeric/Hub RST with listener reuse passed')


def passive_tuple(directory):
    out = Path(directory)
    controls = json.loads((out / 'passive-controls.json').read_text())
    assert controls == {'guarded_total': 1, 'guarded_pending': 0, 'race_pending': 1,
                        'duplicate_pending': 1, 'released_total': 0,
                        'factory_calls': 3, 'stop_callbacks': 1}, controls
    evidence = {'controlled_server_paths': controls}
    for side in ('client', 'server'):
        path = out / (side + '-side.pcap')
        packets = reset_packets(path)
        envelopes = admission_packets(path)
        assert not any(p['flags'] & 4 for p in envelopes), envelopes
        requests = [p for p in envelopes if p['sport'] == 49152 and p['dport'] == 18191
                    and p['long'] and p['cid'] == b'pass-old'.hex()]
        assert len(requests) == 1, requests
        request = requests[0]
        assert not any(p['sport'] == 18191 and p['dport'] == 49152 and
                       p['time'] > request['time'] for p in envelopes), envelopes
        flows = {}
        for port, server_port in ((49152, 18191), (49153, 18191), (49154, 18195)):
            flow = [p for p in packets if port in (p['sport'], p['dport'])]
            assert flow and sum(bool(p['flags'] & 1) for p in flow) >= 2, flow
            streams = {}
            for source in (port, server_port):
                cid = next(p['cid'] for p in flow if p['sport'] == source)
                streams[source] = decode(path, b'native-mx-byte-path', cid)
            flows[port] = streams
        first_fin = min(p['time'] for p in packets if
                        49152 in (p['sport'], p['dport']) and p['flags'] & 1)
        marker = min(p['time'] for p in flows[49153][49153]['data'])
        assert first_fin < request['time'] < marker, (first_fin, request, marker)
        negotiation = [p for p in envelopes if p['sport'] == 18191 and p['dport'] == 19190
                       and p['long'] and p['version'] == 0]
        assert negotiation, envelopes
        evidence[side] = {'refused_fresh_cid_request': request, 'flows': flows,
                          'ordered_fresh_data_marker': marker,
                          'preserved_version_negotiation': negotiation}
    (out / 'passive-tuple-evidence.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print('# passive tuple: wire refusal, real pending-route race, FIFO eviction and reuse passed')


if __name__ == "__main__":
    if len(sys.argv) == 5 and sys.argv[1] == "wire-gate":
        wire_gate(sys.argv[2], sys.argv[3], sys.argv[4])
    elif len(sys.argv) == 5 and sys.argv[1] == "ack-bands":
        ack_bands(sys.argv[2], sys.argv[3], sys.argv[4])
    elif len(sys.argv) == 4 and sys.argv[1] == "syn-evidence":
        syn_evidence(sys.argv[2], sys.argv[3] == "retransmit")
    elif len(sys.argv) == 4 and sys.argv[1] == "positive-window-probe":
        positive_window_probe(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 3 and sys.argv[1] == "positive-window":
        positive_window(sys.argv[2])
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
