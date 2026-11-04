"""Require actual paired measured-work process counters. Never use wait4 fallback."""

def validate(raw, case, sessions, count):
    assert set(raw) == {'client', 'server'}
    complete = sum(row['progress']['complete'] for row in case['clients'])
    assert complete == sessions * count > 0
    assert len(case['clients']) == sessions
    assert all(row['progress']['complete'] == count for row in case['clients'])
    client, server = raw['client'], raw['server']
    assert client['token'] == server['token'] and client['token'] > 0
    assert client['pid'] > 0 and server['pid'] > 0 and client['pid'] != server['pid']
    first = min(row['startNs'] for row in case['clients'])
    last = max(row['endNs'] for row in case['clients'])
    assert first < last
    # The exact counters have real, explicit local cut points. Both local
    # intervals contain the entire shared measured client work; never infer
    # server CPU from client timestamps or sum overlapping session CPU deltas.
    assert server['start']['wallAfterNs'] <= client['start']['wallBeforeNs']
    assert client['start']['wallAfterNs'] <= first
    assert last <= client['end']['wallBeforeNs']
    assert client['end']['wallAfterNs'] <= server['end']['wallBeforeNs']
    result = {'clock': 'CLOCK_PROCESS_CPUTIME_ID', 'counterScope': 'all threads in each endpoint process',
              'phaseScope': 'paired control-bracketed shared measured workload',
              'complete': complete, 'firstMeasuredOfferNs': first, 'lastMeasuredCompleteNs': last,
              'overlappingWarmupSessions': client['overlappingWarmupSessions'],
              'raw': raw, 'peers': {}}
    assert 0 <= client['overlappingWarmupSessions'] < sessions
    for role, record in raw.items():
        assert all(type(record[k]) is int for k in ['pid', 'token', 'error', 'sessions', 'overlappingWarmupSessions', 'controlSends', 'controlReceives', 'controlPolls'])
        assert record['event'] == 'process_window' and record['role'] == role
        assert record['sessions'] == sessions and record['error'] == 0
        rows = case['clients' if role == 'client' else 'servers']
        assert rows and all(row['engine'] == record['engine'] for row in rows)
        assert record['controlSends'] >= 2 and record['controlReceives'] >= 2 and record['controlPolls'] >= 2
        start, end = record['start'], record['end']
        assert type(start['valid']) is bool and type(end['valid']) is bool
        assert start['valid'] and end['valid']
        assert 0 < start['wallBeforeNs'] <= start['wallAfterNs'] <= end['wallBeforeNs'] <= end['wallAfterNs']
        keys = ['cpuNs', 'userNs', 'systemNs', 'voluntary', 'involuntary', 'helperCpuNs']
        for key in keys:
            assert type(start[key]) is int and type(end[key]) is int and 0 <= start[key] <= end[key]
        delta = {key: end[key] - start[key] for key in keys}
        assert delta['cpuNs'] > 0
        bracket = (start['wallAfterNs'] - start['wallBeforeNs'] +
                   end['wallAfterNs'] - end['wallBeforeNs'])
        adjusted = delta['cpuNs'] - delta['helperCpuNs']
        assert adjusted + bracket >= 0
        result['peers'][role] = {
            'pid': record['pid'], 'launcherPid': case['resources'][role]['pid'],
            'counterDelta': delta,
            'phaseProcessCpuNsPerMeasuredOrder': delta['cpuNs'] / complete,
            'phaseUserNsPerMeasuredOrder': delta['userNs'] / complete,
            'phaseSystemNsPerMeasuredOrder': delta['systemNs'] / complete,
            'boundarySampleBracketNs': bracket,
            # Do not pretend asynchronous CPU clock reads are one atomic cut.
            # Helper-adjusted CPU is bounded separately; raw process delta is
            # the primary actual phase metric and includes this known observer.
            'helperAdjustedCpuNsBounds': [max(0, adjusted - bracket), adjusted + bracket],
            'helperAdjustmentErrorBoundNs': bracket,
            'controlCalls': {key: record[key] for key in ['controlSends', 'controlReceives', 'controlPolls']},
        }
    result['boundaryGuardNs'] = {
        'serverBeforeClientMin': client['start']['wallBeforeNs'] - server['start']['wallAfterNs'],
        'serverBeforeClientMax': client['start']['wallAfterNs'] - server['start']['wallBeforeNs'],
        'serverAfterClientMin': server['end']['wallBeforeNs'] - client['end']['wallAfterNs'],
        'serverAfterClientMax': server['end']['wallAfterNs'] - client['end']['wallBeforeNs'],
        'clientBeforeFirstOfferMin': first - client['start']['wallAfterNs'],
        'clientAfterLastCompleteMin': client['end']['wallBeforeNs'] - last,
    }
    return result
