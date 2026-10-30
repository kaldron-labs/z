//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Application-owned selective session progress records

#ifndef ZfixCheckpoint_HH
#define ZfixCheckpoint_HH

#ifndef ZfixLib_HH
#include <zlib/ZfixLib.hh>
#endif

#include <zlib/ZuSpan.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/ZfixLinkState.hh>

namespace Zfix {

ZtFlagsNS(ZfixAPI, StateField, int32_t,
  RxReceived, RxProcessed, RxGap, TxLoaded, TxSent, TxAckd, TxReplay);
namespace StateMask {

  enum { Rx = StateField::RxReceived() | StateField::RxProcessed() | StateField::RxGap(),
    Tx = StateField::TxLoaded() | StateField::TxSent() | StateField::TxAckd() |
      StateField::TxReplay(), All = Rx | Tx };
}

struct RxProgress {
  uint64_t	received = 1;
  uint64_t	processed = 1;
  uint64_t	gapBegin = 0;
  uint64_t	gapEnd = 0;
};

struct TxProgress {
  uint64_t	loaded = 1;
  // ZmPQTx's send cursor: transport-admitted wire progress, not local completion.
  uint64_t	sent = 1;
  uint64_t	acknowledged = 1;
  uint64_t	replayBegin = 0;
  uint64_t	replayEnd = 0;
};

struct Checkpoint {
  SessionString	id;
  uint64_t	epoch = 0;
  unsigned	fields = 0;
  RxProgress	rx;
  TxProgress	tx;
};

// Cursors and range ends are exclusive. A zero range pair explicitly clears
// pending recovery; absent mask bits preserve current values. Identity/epoch are
// mandatory on restore and never inferred from another owner's live state.
// Identity is the application key; wire/header settings do not bind progress.
ZfixExtern bool stateMatches(const Checkpoint &, ZuCSpan, uint64_t epoch);
ZfixExtern bool stampState(Checkpoint &, ZuCSpan, uint64_t epoch);
ZfixExtern bool restoreRx(const Checkpoint &, const RxProgress &current,
  uint64_t sequenceLimit, RxProgress &out);
ZfixExtern bool restoreTx(const Checkpoint &, const TxProgress &current,
  uint64_t sequenceLimit, TxProgress &out);
} // Zfix

#endif /* ZfixCheckpoint_HH */
