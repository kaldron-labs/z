//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZfixCheckpoint.hh>

namespace Zfix {

ZtEnumImplNS(StateField);
bool stateMatches(const Checkpoint &s, ZuCSpan id, uint64_t epoch)
{
  return epoch && s.epoch == epoch && id.length() && s.id == id &&
    !(s.fields & ~StateMask::All);
}
bool stampState(Checkpoint &s, ZuCSpan id, uint64_t epoch)
{
  if (!epoch || !id.length() || (s.epoch && s.epoch != epoch) ||
      (s.fields & ~StateMask::All) || (s.fields && (!s.epoch || !s.id.length()))) return false;
  if (s.id.length()) {
    if (s.id != id) return false;
  } else s.id = id;
  s.epoch = epoch;
  return true;
}
static bool range(uint64_t begin, uint64_t end, uint64_t lo, uint64_t hi)
{
  return (!begin && !end) || (begin >= lo && begin < end && end <= hi);
}
bool restoreRx(const Checkpoint &s, const RxProgress &current,
    uint64_t limit, RxProgress &out)
{
  if (s.fields & ~StateMask::All) return false;
  out = current;
  const auto &p = s.rx;
  if (s.fields & StateField::RxReceived()) out.received = p.received;
  if (s.fields & StateField::RxProcessed()) out.processed = p.processed;
  if (s.fields & StateField::RxGap()) { out.gapBegin = p.gapBegin; out.gapEnd = p.gapEnd; }
  return out.processed && out.processed <= out.received && out.received - 1 <= limit &&
    range(out.gapBegin, out.gapEnd, out.processed, out.received);
}
bool restoreTx(const Checkpoint &s, const TxProgress &current,
    uint64_t limit, TxProgress &out)
{
  if (s.fields & ~StateMask::All) return false;
  out = current;
  const auto &p = s.tx;
  if (s.fields & StateField::TxLoaded()) out.loaded = p.loaded;
  if (s.fields & StateField::TxSent()) out.sent = p.sent;
  if (s.fields & StateField::TxAckd()) out.acknowledged = p.acknowledged;
  if (s.fields & StateField::TxReplay()) {
    out.replayBegin = p.replayBegin; out.replayEnd = p.replayEnd;
  }
  return out.acknowledged && out.acknowledged <= out.sent && out.sent <= out.loaded &&
    out.loaded && out.loaded - 1 <= limit &&
    range(out.replayBegin, out.replayEnd, 1, out.loaded);
}
} // Zfix

