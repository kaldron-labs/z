//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Fixed owner-shard protocol/queue snapshots

#ifndef ZfixStats_HH
#define ZfixStats_HH

#ifndef ZfixLib_HH
#include <zlib/ZfixLib.hh>
#endif
#include <stdint.h>

namespace Zfix {

// Cumulative counters span connection/epoch changes. Queue fields are current
// owner snapshots. Copy snapshots to reporting owners; never share live state.
struct RxCounters {
  uint64_t	frames = 0, bytes = 0, framingErrors = 0;
  uint64_t	rejectsIn = 0, rejectsOut = 0, gaps = 0, replays = 0;
  uint64_t	duplicates = 0, ignored = 0, decodeErrors = 0, hookFailures = 0;
  uint64_t	faults = 0, overloads = 0;
};

struct RxStats : RxCounters {
  uint64_t	queuedCount = 0, queuedBytes = 0, pendingBytes = 0;
};

struct WireCounters {
  uint64_t	admitted = 0, published = 0, bytes = 0, replays = 0, rejects = 0;
  uint64_t	completed = 0, failed = 0, refused = 0;
  uint64_t	peakBytes = 0;
};

struct WireStats : WireCounters {
  uint64_t	queuedCount = 0, queuedBytes = 0;
};

struct TxCounters {
  uint64_t	loadRefused = 0, encodeErrors = 0, outputRefused = 0;
};

struct TxStats : TxCounters {
  WireStats	wire;
  uint64_t	enqueueRefused = 0, faults = 0;
  uint64_t	operationalCount = 0, operationalBytes = 0, controlBytes = 0, inFlight = 0;
};

} // Zfix

#endif /* ZfixStats_HH */
