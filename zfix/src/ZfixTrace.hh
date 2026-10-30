//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Bounded owner-shard wire diagnostics

#ifndef ZfixTrace_HH
#define ZfixTrace_HH

#ifndef ZfixLib_HH
#include <zlib/ZfixLib.hh>
#endif

#include <zlib/ZuSwitch.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuUnroll.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZfixDispatch.hh>
#include <zlib/ZfixNotice.hh>

namespace Zfix {

struct TraceLimits {
  uint64_t	bytes = 0; // zero disables capture, including parsing/allocation
  unsigned	recordBytes = 4096; // initial escaped diagnostic excerpt budget
};

struct TraceCounters {
  uint64_t	records = 0, dropped = 0, rotated = 0, truncated = 0;
  uint64_t	bytes = 0;
};

struct TraceStats : TraceCounters {
  uint64_t	count = 0;
};

struct TraceField {
  unsigned	dataTag = 0; // nonzero denotes a binary length tag
  bool		known = false, binary = false, secret = false, ambiguous = false;

  void merge(TraceField f) {
    if (ambiguous || f.ambiguous || (known && f.known &&
	(dataTag != f.dataTag || binary != f.binary))) {
      *this = {}; ambiguous = true; return;
    }
    bool redact = secret || f.secret;
    if (f.known) *this = f;
    secret = redact;
  }
};

// Tag metadata only. Conflicting binary semantics across group scopes make the
// remaining trace opaque; capture never guesses a binary boundary. Runtime union
// discrimination remains application policy, as in the codec.
template <typename O, typename Facet = ZuFacet::FIX, unsigned Depth = 0>
TraceField traceField(unsigned tag) {
  using S = Fmt::Schema<O, Facet>;
  TraceField out;
  int index = S::index().find(tag);
  if constexpr (S::Leaves::N) if (index >= 0) {
    ZuSwitch::dispatch<S::Leaves::N>(unsigned(index) >> 1, [&out, index]<typename I>(I) {
      using F = typename ZuType<I{}, typename S::Leaves>::Field;
      out.known = true;
      if (index & 1) out.dataTag = Fmt::tag<F>();
      else out.binary = bool(Fmt::lenTag<F>());
    });
  }
  if constexpr (Depth < Fmt::Limits::GroupDepth) {
    ZuUnroll::all<typename S::Leaves>([&out, tag]<typename L>() {
      using F = typename L::Field;
      if constexpr (Fmt::GroupField<F, Facet>{})
	out.merge(traceField<Fmt::GroupElem<typename F::T>, Facet, Depth + 1>(tag));
    });
  }
  return out;
}
ZfixExtern TraceField traceBuiltin(ZuCSpan msgType, unsigned tag);

template <typename MTList> TraceField traceField(ZuCSpan type, unsigned tag) {
  using Messages = typename MTList::Messages;
  if constexpr (!Messages::N) return {};
  else {
    int index = Dispatch_::index<Messages>().find(type);
    if (index < 0) return {};
    return ZuSwitch::dispatch<Messages::N>(index, [tag]<typename I>(I) {
      using E = ZuType<I{}, Messages>;
      using Entries = typename Dispatch_::Candidates<E, Messages>::T;
      using Sources = ZuTypeUnique<ZuTypeMap<Dispatch_::EntrySource, Entries>>;
      TraceField out;
      ZuUnroll::all<Sources>([&out, tag]<typename S>() {
	out.merge(traceField<typename S::Object, typename S::Mapping>(tag));
      });
      return out;
    });
  }
}

// The record, queue links and escaped backing share one pooled allocation.
// Original wire backing is never retained by a trace record.
struct TraceRecord : public ZiIOBuf {
  TxContext	context;
  uint64_t	charge = 0;
  unsigned	wireBytes = 0;
  bool		truncated = false;

  TraceRecord(uint8_t *data, unsigned size) : ZiIOBuf{data, size} { }

  ZuCSpan text() const { return cspan(); }
};

using TraceQueue = ZmList<TraceRecord, ZmListNode<ZiIOBuf, ZmListHeapID<"">>>;
using TraceRef = ZmRef<const TraceQueue::Node>;
class ZfixAPI WireTrace {
public:
  using Resolve = TraceField (*)(void *, ZuCSpan, unsigned);

  explicit WireTrace(TraceLimits limits = {}) : m_limits{limits} { }

  WireTrace(const WireTrace &) = delete;
  WireTrace &operator =(const WireTrace &) = delete;

  bool enabled_() const { return m_limits.bytes && m_limits.recordBytes; }

  TraceStats stats_() const {
    TraceStats s{m_stats}; s.count = m_queue.count_(); return s;
  }

  TraceRef capture_(ZuCSpan, TxContext, Resolve = nullptr, void * = nullptr);
  // Reuse a successful framing result; never checksum an admitted frame again.
  TraceRef capture_(const Fmt::FrameView &, TxContext, Resolve = nullptr, void * = nullptr);

  TraceRef head_() const { return m_queue.headNode(); }

  TraceRef pop_();
  void clear_();
private:
  TraceRef capture_(ZuCSpan, const Fmt::FrameView *, TxContext, Resolve, void *);

  const TraceLimits	m_limits;
  TraceCounters		m_stats;
  TraceQueue		m_queue;
};

} // Zfix

#endif /* ZfixTrace_HH */
