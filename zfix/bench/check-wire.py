#!/usr/bin/env python3
# (c) Copyright 2026 Huw Rogers
# This code is licensed by the MIT license (see LICENSE for details).

"""Check captured managed FIX sessions and TCP byte identity on fixture veth runs."""

import argparse
import importlib.util
import json
import re
from pathlib import Path


def require(ok, reason):
    if not ok:
        raise AssertionError(reason)


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def frames(raw):
    offset = 0
    found = []
    while offset < len(raw):
        match = re.match(rb'8=FIX\.4\.4\x019=([0-9]+)\x01', raw[offset:])
        require(match is not None, 'FIX header')
        start = offset + match.end()
        end = start + int(match[1])
        trailer = raw[end:end + 7]
        require(re.fullmatch(rb'10=[0-9]{3}\x01', trailer) is not None, 'FIX trailer')
        require(sum(raw[offset:end]) % 256 == int(trailer[3:6]), 'FIX checksum')
        body = raw[start:end]
        require(body.endswith(b'\x01'), 'FIX body extent')
        parts = [part.split(b'=', 1) for part in body[:-1].split(b'\x01')]
        require(all(len(part) == 2 and part[0].isdigit() for part in parts), 'FIX field syntax')
        tags = {key.decode('ascii'): value.decode('ascii') for key, value in parts}
        require(len(tags) == len(parts) and parts[0][0] == b'35', 'FIX field uniqueness/order')
        tags['_order'] = tuple(key.decode('ascii') for key, _ in parts)
        tags['_size'] = end + 7 - offset
        found.append(tags)
        offset = end + 7
    require(offset == len(raw), 'extra TCP bytes')
    return found


def check_messages(messages, role, settings):
    total = int(settings['messages']) + int(settings['warmup'])
    workload = settings['workload']
    msg_type = {'order': ('D', '8'), 'marketdata': ('V', 'W'),
                'updates': ('X', 'X')}[workload][role == 'server']
    sender, target = (('BENCH', 'PEER') if role == 'client' else ('PEER', 'BENCH'))
    require(messages and messages[0].get('35') == 'A', 'managed Logon first')
    require(any(m.get('35') == '5' for m in messages), 'managed Logout missing')
    business = [m for m in messages if m.get('35') == msg_type]
    require(len(business) == total, 'business count')
    for serial, m in enumerate(business, 1):
        require(m.get('49') == sender and m.get('56') == target,
                'managed sender/target')
        number = f'{serial:010d}'
        if workload == 'order':
            fields = ([('11', number), ('55', 'TEST'), ('54', '1'),
                       ('60', '20260101-00:00:00.000'), ('38', '100'),
                       ('40', '2'), ('44', '123.45')]
                      if role == 'client' else
                      [('11', number), ('17', number), ('37', number),
                       ('150', '0'), ('39', '0'), ('55', 'TEST'), ('54', '1'),
                       ('38', '100'), ('44', '123.45'), ('151', '100'),
                       ('14', '0'), ('6', '0')])
        elif workload == 'marketdata':
            fields = ([('262', number), ('263', '0'), ('264', '1'),
                       ('267', '1'), ('269', '0'), ('146', '1'), ('55', 'TEST')]
                      if role == 'client' else
                      [('262', number), ('55', 'TEST'), ('268', '1'),
                       ('269', '0'), ('270', '123.45'), ('271', '100')])
        else:
            fields = [('262', number), ('268', '1'), ('279', '0'),
                      ('269', '0'), ('278', number), ('55', 'TEST'),
                      ('270', '123.45'), ('271', '100')]
        expected = tuple(tag for tag, _ in fields)
        require(m['_order'][-len(fields):] == expected,
                'business field order')
        require(set(m['_order']) == set(expected) | {'35', '49', '56', '34', '52'},
                'business field set')
        for tag, value in fields:
            require(m[tag] == value, 'business field ' + tag)
    seq = [int(m['34']) for m in messages]
    require(seq == list(range(1, len(seq) + 1)), 'FIX sequence continuity')
    return {'frames': len(messages), 'business': len(business),
            'controls': len(messages) - len(business),
            'business_bytes': sum(m['_size'] for m in business),
            'measured_business_bytes': sum(
                m['_size'] for m in business[int(settings['warmup']):]),
            'frame_sizes': sorted(set(m['_size'] for m in business))}


def check_run(directory, helper, direct):
    settings = dict(line.split('=', 1) for line in
                    (directory / 'settings').read_text().splitlines())
    require((directory / 'complete').is_file(), 'fixture not complete')
    require(settings['capture'] == '1', 'capture required')
    bridge = settings.get('capture_topology') == 'bridge'
    if not bridge:
        require('kernel' in (settings['client_backend'], settings['server_backend']),
                'native/native requires bridge capture')
    names = ['to-client.pcap', 'to-server.pcap'] if bridge else ['diagnostic.pcap']
    logs = ['capture-client.log', 'capture-server.log'] if bridge else ['capture.log']
    for name in logs:
        text = (directory / name).read_text()
        drops = re.findall(r'(\d+) packets dropped by kernel', text)
        require(drops == ['0'], 'capture drops or incomplete capture')
    packets = []
    for name in names:
        for when, raw in helper.capture(directory / name):
            packet = helper.tcp(raw)
            if packet is None:
                continue
            require(packet['family'] == int(settings['family']), 'IP family')
            packet['time'] = when
            packets.append(packet)
    syns = {(
        p['source'], p['sport'], p['target'], p['dport']): p
        for p in packets if p['dport'] == 18187 and p['flags'] & 2 and
        not p['flags'] & 16}
    require(len(syns) == int(settings['connections']), 'connection count')
    results = []
    for key, syn in syns.items():
        reverse = (key[2], key[3], key[0], key[1])
        flows = [[p for p in packets if
                  (p['source'], p['sport'], p['target'], p['dport']) == direction]
                 for direction in (key, reverse)]
        synacks = [p for p in flows[1] if p['flags'] & 2]
        require(synacks, 'SYNACK missing')
        bases = (syn['seq'] + 1, synacks[0]['seq'] + 1)
        report = {}
        for role, flow, base in zip(('client', 'server'), flows, bases):
            raw, _, _ = direct.stream(flow, base & 0xffffffff,
                                      (int(settings['messages']) +
                                       int(settings['warmup']) + 32) * 4096)
            report[role] = check_messages(frames(raw), role, settings)
            report[role]['bytes'] = len(raw)
        results.append(report)
    for role in ('client', 'server'):
        output = (directory / role / 'output').read_text()
        require(re.search(r'^result role=' + role + r' backend=\w+ exact=1 ',
                          output, re.M) is not None, 'application result')
    return {'directory': str(directory), 'status': 'pass', 'flows': results,
            'claim': 'functional managed FIX/TCP evidence only'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directories', nargs='+', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    helper = load(root / 'zutcp/interop/zutcptcp.py', 'tcp_evidence')
    direct = load(root / 'zutcp/bench/analyze-wire.py', 'direct_wire')
    reports, errors = [], []
    for directory in args.directories:
        try:
            reports.append(check_run(directory, helper, direct))
        except (AssertionError, OSError, KeyError, ValueError) as error:
            errors.append({'directory': str(directory), 'error': str(error)})
    print(json.dumps({'runs': reports, 'errors': errors}, indent=2))
    return bool(errors)


if __name__ == '__main__':
    raise SystemExit(main())
