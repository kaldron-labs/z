//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZfixLoadDiag_HH
#define ZfixLoadDiag_HH

#include <errno.h>
#include <limits.h>
#include <zlib/ZuPtr.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZfixNet.hh>
#include "load_run.hh"

#ifdef ZiMultiplex_BENCH
namespace ZfixLoad {

struct PoolDiag {
  uint64_t allocs = 0, destroyed = 0, registered = 0, ordinary = 0, depletion = 0;
  uint64_t slots = 0, peakSlots = 0, slotBytes = 0, peakSlotBytes = 0;
  uint64_t appSlots = 0, peakAppSlots = 0, appBytes = 0, peakAppBytes = 0;
  uint64_t earlyReturns = 0, preserveBytes = 0, moveBytes = 0, gatherBytes = 0;
  uint64_t slotReturns = 0;
};
ZfStruct(, (PoolDiag, JSON),
  (allocs, , UInt64), (destroyed, , UInt64), (registered, , UInt64),
  (ordinary, , UInt64), (depletion, , UInt64), (slots, , UInt64), (peakSlots, , UInt64),
  (slotBytes, , UInt64), (peakSlotBytes, , UInt64), (appSlots, , UInt64),
  (peakAppSlots, , UInt64), (appBytes, , UInt64), (peakAppBytes, , UInt64),
  (earlyReturns, , UInt64), (preserveBytes, , UInt64), (moveBytes, , UInt64),
  (gatherBytes, , UInt64), (slotReturns, , UInt64));
ZfStructImpl(PoolDiag); ZfStructImpl(PoolDiag, JSON);

#ifdef ZiMultiplex_DEBUG
struct PoolCounts {
  ZmAtomic<uint64_t> allocs{0}, destroyed{0}, registered{0}, ordinary{0}, depletion{0};
  ZmAtomic<uint64_t> slots{0}, peakSlots{0}, slotBytes{0}, peakSlotBytes{0};
  ZmAtomic<uint64_t> appSlots{0}, peakAppSlots{0}, appBytes{0}, peakAppBytes{0};
  ZmAtomic<uint64_t> earlyReturns{0}, preserveBytes{0}, moveBytes{0}, gatherBytes{0};
  ZmAtomic<uint64_t> slotReturns{0};

  static void peak(ZmAtomic<uint64_t> &counter, uint64_t value) {
    auto previous = counter.load_();
    while (value > previous) {
      auto seen = counter.cmpXch(value, previous);
      if (seen == previous) break;
      previous = seen;
    }
  }
  void take(unsigned bytes) {
    ++registered;
    peak(peakSlots, slots.xchAdd(1) + 1);
    peak(peakSlotBytes, slotBytes.xchAdd(bytes) + bytes);
  }
  void deliver(unsigned bytes) {
    peak(peakAppSlots, appSlots.xchAdd(1) + 1);
    peak(peakAppBytes, appBytes.xchAdd(bytes) + bytes);
  }
  PoolDiag snapshot() const {
    return {allocs.load_(), destroyed.load_(), registered.load_(), ordinary.load_(),
      depletion.load_(), slots.load_(), peakSlots.load_(), slotBytes.load_(),
      peakSlotBytes.load_(), appSlots.load_(), peakAppSlots.load_(), appBytes.load_(),
      peakAppBytes.load_(), earlyReturns.load_(), preserveBytes.load_(), moveBytes.load_(),
      gatherBytes.load_(), slotReturns.load_()};
  }
};

// Shared only for diagnostics. I/O payload owners never consult these counters
// to decide progress, and the main thread snapshots them after owner shutdown.
struct DiagMeta {
  PoolCounts *pool = nullptr;
  uint64_t receivedNS = 0;
  unsigned slotBytes = 0;
  bool slot = false, app = false;
};

template <typename Base>
struct DiagBuf : public Base, public DiagMeta {
  using Base::Base;

  ~DiagBuf() { if (this->pool) ++this->pool->destroyed; }

  void preSlotRelease_() {
    if (!this->pool || !this->slot) return;
    ++this->pool->slotReturns;
    if (this->refCount()) ++this->pool->earlyReturns;
    this->pool->slots.xchSub(1);
    this->pool->slotBytes.xchSub(this->slotBytes);
    if (this->app) {
      this->pool->appSlots.xchSub(1);
      this->pool->appBytes.xchSub(this->slotBytes);
    }
    this->slot = this->app = false;
  }
  void mergeRx_(const ZiIOBuf &source, unsigned bytes) override {
    if (auto meta = dynamic_cast<const DiagMeta *>(&source)) {
      if (meta->receivedNS > this->receivedNS) this->receivedNS = meta->receivedNS;
    }
    if (this->pool) this->pool->gatherBytes.xchAdd(bytes);
  }
protected:
  void copied_(uint64_t bytes, bool inPlace) override {
    if (!this->pool) return;
    if (inPlace) this->pool->moveBytes.xchAdd(bytes);
    else this->pool->preserveBytes.xchAdd(bytes);
  }
};

#endif // ZiMultiplex_DEBUG

struct OpcodeDiag {
  unsigned opcode = 0;
  uint64_t prepared = 0, cqes = 0;
  static unsigned key(const OpcodeDiag &value) { return value.opcode; }
};
ZfStruct(, (OpcodeDiag, JSON), (opcode, , UInt32), (prepared, , UInt64), (cqes, , UInt64));
ZfStructImpl(OpcodeDiag); ZfStructImpl(OpcodeDiag, JSON);
ZfJSON::AsArray<ZfFieldTC::UDT> ZfJSON_Fmt(Rows<OpcodeDiag> *);

struct OwnerDiagRow {
  unsigned sid = 0;
  uint64_t enterCalls = 0, enterWaits = 0, enterErrors = 0, interrupted = 0;
  uint64_t acceptedSQEs = 0, preparedSQEs = 0, cqes = 0, dataCQEs = 0, notifCQEs = 0;
  uint64_t ordinarySends = 0, ordinaryBytes = 0, forwardedRx = 0;
  uint64_t rxBytes = 0, rxZero = 0, rxReady = 0, rxWait = 0;
  uint64_t unpairedNotifs = 0, pendingNotifs = 0, droppedSamples = 0;
  Quantiles notificationNs;
  Rows<OpcodeDiag> opcodes;
};
ZfStruct(, (OwnerDiagRow, JSON), (sid, , UInt32),
  (enterCalls, , UInt64), (enterWaits, , UInt64), (enterErrors, , UInt64),
  (interrupted, , UInt64), (acceptedSQEs, , UInt64), (preparedSQEs, , UInt64),
  (cqes, , UInt64), (dataCQEs, , UInt64), (notifCQEs, , UInt64),
  (ordinarySends, , UInt64), (ordinaryBytes, , UInt64), (forwardedRx, , UInt64),
  (rxBytes, , UInt64), (rxZero, , UInt64), (rxReady, , UInt64), (rxWait, , UInt64),
  (unpairedNotifs, , UInt64), (pendingNotifs, , UInt64), (droppedSamples, , UInt64),
  (notificationNs, , UDT), (opcodes, , UDT));
ZfStructImpl(OwnerDiagRow); ZfStructImpl(OwnerDiagRow, JSON);
ZfJSON::AsArray<ZfFieldTC::UDT> ZfJSON_Fmt(Rows<OwnerDiagRow> *);

struct OpTime {
  uintptr_t op;
  uint64_t ns;
  static uintptr_t key(const OpTime &value) { return value.op; }
};
struct OwnerDiag : public ZmNHeap<OwnerDiag> {
  friend inline ZuStringT<"Zfix.Load.IOBench"> ZmNHeapID(OwnerDiag *);
  using Pending = ZmHash<OpTime, ZmHashNode<OpTime,
    ZmHashKey<OpTime::key, ZmHashLock<ZmNoLock, ZmHashHeapID<"Zfix.Load.IONotif">>>>>;
  using Opcodes = ZmHash<OpcodeDiag, ZmHashNode<OpcodeDiag,
    ZmHashKey<OpcodeDiag::key, ZmHashLock<ZmNoLock, ZmHashHeapID<"Zfix.Load.IOOpcode">>>>>;
  OwnerDiagRow row;
  Samples delays;
  Pending pending;
  Opcodes opcodes;

  OwnerDiag(unsigned sid, unsigned limit) : delays{limit} { row.sid = sid; }

  void observe(const Zi::IOBenchEvent &event) {
    switch (event.kind) {
      case Zi::IOBenchKind::Prepared: {
	++row.preparedSQEs;
	++opcodes.findAdd(OpcodeDiag{event.opcode})->data().prepared;
      } break;
      case Zi::IOBenchKind::Enter:
	++row.enterCalls;
	if (event.wait) ++row.enterWaits;
	if (event.result < 0) {
	  ++row.enterErrors; if (event.error == EINTR) ++row.interrupted;
	} else row.acceptedSQEs += unsigned(event.result);
	break;
      case Zi::IOBenchKind::TxData: {
	++row.cqes; ++row.dataCQEs;
	if (event.fallback != Zi::IOBenchFallback::None) {
	  ++row.ordinarySends;
	  if (event.result > 0) row.ordinaryBytes += unsigned(event.result);
	  if (event.fallback == Zi::IOBenchFallback::RxForward) ++row.forwardedRx;
	}
	++opcodes.findAdd(OpcodeDiag{event.opcode})->data().cqes;
#if Zi_URing
	if (event.flags & IORING_CQE_F_MORE) pending.add(OpTime{event.op, monotonic()});
#endif
      } break;
      case Zi::IOBenchKind::TxNotif: {
	++row.cqes; ++row.notifCQEs;
	++opcodes.findAdd(OpcodeDiag{event.opcode})->data().cqes;
	if (auto previous = pending.del(event.op)) {
	  if (!delays.add(monotonic() - previous->data().ns)) ++row.droppedSamples;
	} else ++row.unpairedNotifs;
      } break;
      case Zi::IOBenchKind::CQE:
	++row.cqes; ++opcodes.findAdd(OpcodeDiag{event.opcode})->data().cqes; break;
      case Zi::IOBenchKind::Rx: row.rxBytes += event.count; break;
      case Zi::IOBenchKind::RxZero: ++row.rxZero; break;
      case Zi::IOBenchKind::RxReady: row.rxReady += event.count; break;
      case Zi::IOBenchKind::RxWait: ++row.rxWait; break;
    }
  }
  OwnerDiagRow finish() {
    row.pendingNotifs = pending.count_();
    row.notificationNs = delays.finish();
    auto i = opcodes.iter();
    while (auto entry = i()) row.opcodes.push(entry->data());
    return ZuMv(row);
  }
};

struct FrameDiagRow {
  unsigned session = 0;
  uint64_t missingOrigin = 0, droppedSamples = 0;
  Quantiles completionToFramerNs;
};
ZfStruct(, (FrameDiagRow, JSON), (session, , UInt32), (missingOrigin, , UInt64),
  (droppedSamples, , UInt64), (completionToFramerNs, , UDT));
ZfStructImpl(FrameDiagRow); ZfStructImpl(FrameDiagRow, JSON);
ZfJSON::AsArray<ZfFieldTC::UDT> ZfJSON_Fmt(Rows<FrameDiagRow> *);
#ifdef ZiMultiplex_DEBUG
struct FrameDiag {
  Samples samples;
  uint64_t seen = 0, missing = 0, dropped = 0;
};

#endif // ZiMultiplex_DEBUG

struct IODiagnostics {
  ZuCSpan event{"io_diag"}, engine{"zfix"}, backend;
  ZuCSpan clock{"CLOCK_MONOTONIC"};
  ZuCSpan snapshot{"after mx.stop, before mux/allocator-cache destruction"};
#ifdef ZiMultiplex_DEBUG
  bool provenance = true;
  ZuCSpan rxOrigin{"userspace receive completion"};
  ZuCSpan copyCoverage{"IOBuf preservation/move and RxStream gather; excludes crypto-internal copies"};
#else
  bool provenance = false;
  ZuCSpan rxOrigin, copyCoverage;
#endif
  PoolDiag rxPool, txPool;
  Rows<OwnerDiagRow> owners;
  Rows<FrameDiagRow> frames;
};
ZfStruct(, (IODiagnostics, JSON), (event, , String), (engine, , String), (backend, , String),
  (clock, , String), (snapshot, , String), (provenance, , Bool),
  (rxOrigin, , String), (copyCoverage, , String),
  (rxPool, , UDT), (txPool, , UDT), (owners, , UDT), (frames, , UDT));
ZfStructImpl(IODiagnostics); ZfStructImpl(IODiagnostics, JSON);

struct NativeDiag : public ZmNHeap<NativeDiag> {
  friend inline ZuStringT<"Zfix.Load.NativeDiag"> ZmNHeapID(NativeDiag *);
  inline static NativeDiag *active = nullptr;
  const Options &options;
#ifdef ZiMultiplex_DEBUG
  PoolCounts rxPool, txPool;
  Rows<FrameDiag> frames;
#endif
  Rows<ZuPtr<OwnerDiag>> owners;
  unsigned limit;

  NativeDiag(const Options &o) : options{o}, limit{o.diagSamples} {
    if (!limit) {
      uint64_t count = uint64_t(o.count) + o.warmup + Zfix::TxLimits{}.controlHeadroom;
      count *= 2;
      limit = count > UINT_MAX ? UINT_MAX : unsigned(count);
    }
#ifdef ZiMultiplex_DEBUG
    frames.length(o.sessions);
    for (auto &frame : frames) frame.samples = Samples{limit};
#endif
    active = this;
  }
  ~NativeDiag() { active = nullptr; }
  void prepare(unsigned threads) {
    owners.length(threads + 1);
    uint64_t samples = uint64_t(limit) * options.sessions;
    unsigned cap = samples > UINT_MAX ? UINT_MAX : unsigned(samples);
    for (unsigned sid = 0; sid <= threads; ++sid)
      owners[sid] = new OwnerDiag{sid, cap};
  }
  Zi::IOBenchFn observer() {
    return Zi::IOBenchFn{[this](const Zi::IOBenchEvent &event) {
      if (event.sid < owners.length()) owners[event.sid]->observe(event);
#ifdef ZiMultiplex_DEBUG
      if (event.kind != Zi::IOBenchKind::Rx || !event.buf) return;
      if (auto meta = dynamic_cast<DiagMeta *>(event.buf)) {
	meta->receivedNS = monotonic();
	if (meta->slot && !meta->app) {
	  meta->app = true; meta->pool->deliver(meta->slotBytes);
	}
      }
#endif
    }};
  }
#ifdef ZiMultiplex_DEBUG
  void framed(unsigned index, bool client, const Zi::RxRange &source,
      const Zfix::Fmt::FrameView &frame) {
    if (frame.msgType != (client ? "8" : "D")) return;
    auto &record = frames[index];
    if (++record.seen <= uint64_t(options.warmup) * (client ? 2 : 1)) return;
    auto meta = dynamic_cast<const DiagMeta *>(source.buf.ptr());
    if (!meta || !meta->receivedNS) { ++record.missing; return; }
    auto now = monotonic();
    if (now < meta->receivedNS || !record.samples.add(now - meta->receivedNS))
      ++record.dropped;
  }
#endif
  void report(ZuCSpan backend) {
    IODiagnostics result;
    result.backend = backend;
    for (auto &owner : owners) result.owners.push(owner->finish());
#ifdef ZiMultiplex_DEBUG
    result.rxPool = rxPool.snapshot(); result.txPool = txPool.snapshot();
    for (unsigned i = 0; i < frames.length(); ++i) {
      auto &frame = frames[i];
      result.frames.push(FrameDiagRow{i + 1, frame.missing, frame.dropped, frame.samples.finish()});
    }
#endif
    output(result);
  }
};

#ifdef ZiMultiplex_DEBUG
// Used only by opt-in debug peer instantiations; benchmark-only peers retain their
// original allocation/node types. The application owns NativeDiag through stop
// and the final release of these allocator-owned descriptors.
template <typename Policy, typename Buf, bool Rx>
struct ObservedAllocator : public ZmNHeap<ObservedAllocator<Policy, Buf, Rx>>, public Policy {
  using Heap = ZmNHeap<ObservedAllocator<Policy, Buf, Rx>>;
  using Heap::operator new;
  using Heap::operator delete;
  friend inline ZuStringT<"Zfix.Load.IOAllocator"> ZmNHeapID(ObservedAllocator *);
  NativeDiag *diag = NativeDiag::active;
  bool poolUsable = false;

  explicit ObservedAllocator(unsigned ringSize =
      Rx ? ZiIOBuf_DefltRxRingSize : ZiIOBuf_DefltTxRingSize) :
    Policy{ringSize} {
#if Zi_URing
    auto ring = this->ring();
    poolUsable = ring && ring->fd() >= 0;
#endif
  }
  ZmRef<ZiIOBuf> alloc(void *owner = nullptr, uint32_t length = 0) override {
    auto result = Policy::alloc(owner, length);
    auto meta = static_cast<Buf *>(result.ptr());
    meta->pool = Rx ? &diag->rxPool : &diag->txPool;
    ++meta->pool->allocs;
    meta->slot = result->uring(); meta->slotBytes = result->size;
    if (meta->slot) meta->pool->take(meta->slotBytes);
    else {
      ++meta->pool->ordinary;
      if (poolUsable) ++meta->pool->depletion;
    }
    return result;
  }
};

template <typename Net>
struct DiagnosticNet : Net {
  template <unsigned Size = (Net::Encrypted ?
      Ztls_::TxRecordCapacity + Zfix::wirePrefixGap() : ZiIOBuf_DefltSize),
    unsigned Max = ZiIOBuf_DefltMaxSize, ZuString Heap = "Zfix.Load.Observed.Tx">
  using TxAllocator = ObservedAllocator<ZuIf<Net::Encrypted,
    Zi::TxTLSBufAllocator<DiagBuf<Zfix::WireBuf>, Size, Max, ZuStringT<Heap>>,
    Zi::TxTCPBufAllocator<DiagBuf<Zfix::WireBuf>, Size, Max, ZuStringT<Heap>>>,
    DiagBuf<Zfix::WireBuf>, false>;
  template <unsigned Size = (Net::Encrypted ? Ztls_::TxRecordCapacity : ZiIOBuf_DefltSize),
    unsigned Max = ZiIOBuf_DefltMaxSize, ZuString Heap = "Zfix.Load.Observed.Rx">
  using RxAllocator = ObservedAllocator<ZuIf<Net::Encrypted,
    Zi::RxTLSBufAllocator<DiagBuf<ZiRxBuf>, Size, Max, ZuStringT<Heap>>,
    Zi::RxTCPBufAllocator<DiagBuf<ZiRxBuf>, Size, Max, ZuStringT<Heap>>>,
    DiagBuf<ZiRxBuf>, true>;
};

#else
template <typename Net> using DiagnosticNet = Net;
#endif // ZiMultiplex_DEBUG

} // ZfixLoad
#endif

#endif /* ZfixLoadDiag_HH */
