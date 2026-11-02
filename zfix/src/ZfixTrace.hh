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
  using Reader = unsigned (*)(ZuCSpan, Fmt::Token &, Fmt::Limits, Fmt::Error &);
  bool		known = false, secret = false, ambiguous = false;
  Reader	reader = nullptr;
  int32_t	valueTag = 0;

  constexpr void merge(TraceField f) {
    // Equivalent hidden template functions can have different addresses in
    // the application and shared library. Compare their wire interpretation.
    if (known && f.known && valueTag != f.valueTag) ambiguous = true;
    ambiguous |= f.ambiguous;
    if (f.known) { reader = f.reader; valueTag = f.valueTag; }
    known |= f.known;
    secret |= f.secret;
  }
};

// One primary-key lookup yields the reader and its interpretation. Conflicting
// group/candidate metadata is opaque, rather than a payload parsing guess.
template <typename Fields> struct TraceFields {
  template <typename F> using Key = ZuConstant<int32_t, int32_t(F::Tag)>;
  using Keys = ZuTypeUnique<ZuTypeMap<Key, Fields>>;
  enum { Bits = Keys::N > 1 ? ZuIntrin::log2(Keys::N) + 2 : 2 };

  template <typename K> struct Meta {
    inline static constexpr TraceField field = [] {
      TraceField out;
      ZuUnroll::all<Fields>([&out]<typename F>() {
	if constexpr (F::Tag == K{}) {
	  out.merge({.known = true,
	    .reader = F::Payload ? &F::read : nullptr, .valueTag = F::Payload});
	}
      });
      return out;
    }();
  };
  struct Index : ZmLHashKV<int32_t, const TraceField *,
    ZmLHashStatic<Bits, ZmLHashLocal<>>> {
    Index() {
      ZuUnroll::all<Keys>([this]<typename K>() {
	this->add(int32_t(K{}), &Meta<K>::field);
      });
    }
  };
  inline static const Index index{};
};

// Resolve known tags and secrets across the selected schema and group scopes.
template <typename O, typename Facet = ZuFacet::FIX>
TraceField traceField(int32_t tag) {
  using Fields = TraceFields<typename Fmt::WireFields<O, Facet>::T>;
  auto field = Fields::index.findVal(tag);
  return field ? *field : TraceField{};
}
ZfixExtern TraceField traceBuiltin(ZuCSpan msgType, int32_t tag);

template <typename MTList> TraceField traceField(ZuCSpan type, int32_t tag) {
  using Messages = typename MTList::Messages;
  if constexpr (!Messages::N) return {};
  else {
    int index = Dispatch_::find<Messages>(type);
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
  using Resolve = TraceField (*)(void *, ZuCSpan, int32_t);

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
