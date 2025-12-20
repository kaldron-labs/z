//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// shared memory inter-process ring buffer
// layered on ZmRing, but ZiRing is always multiple reader (MR)
// - single/multiple writers/producers - supports SWMR MWMR
// - fixed- and variable-sized messages (types)
// - broadcast - all readers receive all messages
// - for unicast, app should shard writes to multiple MWMR ring buffers
//   - most applications require sharding to ensure correct sequencing,
//     and sharding to multiple ring buffers is more performant than
//     multiple readers contending on a single ring buffer and
//     skipping past all the messages intended for other readers

// Linux - /dev/shm/*
// Windows - Local\*

#ifndef ZiRing_HH
#define ZiRing_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZmRing.hh>

#include <zlib/ZiPlatform.hh>
#include <zlib/ZiFile.hh>

namespace ZiRing_ {

using namespace ZmRing_;

// ring buffer parameters

struct ParamData : public ZmRing_::ParamData {
  Zi::Path	name;
  unsigned	killWait = 1;
  bool		coredump = false;

  inline const ParamData &data() { return *this; } // upcast

  using Base = ZmRing_::ParamData;

  ParamData() = default;
  ParamData(const ParamData &) = default;
  ParamData(ParamData &&) = default;
  template <
    typename Arg0, typename ...Args,
    typename = ZuIsNot<Arg0, ZmRing_::ParamData>,
    decltype(Base(ZuDeclVal<Args &&>()...), int()) = 0>
  ParamData(Arg0 &&arg0, Args &&...args) :
      Base(ZuFwd<Args>(args)...), name{ZuFwd<Arg0>(arg0)} { }
  ParamData &operator =(const ParamData &) = default;
  ParamData &operator =(ParamData &&) = default;
};

template <typename Derived, typename Data = ParamData>
class Params_ : public ZmRing_::Params_<Derived, Data> {
  using Base = ZmRing_::Params_<Derived, Data>;

  Derived &&derived() { return ZuMv(*static_cast<Derived *>(this)); }

public:
  Params_() = default;
  Params_(const Params_ &) = default;
  Params_(Params_ &&) = default;
  template <
    typename Arg0, typename ...Args,
    typename = ZuIsNot<Arg0, ZmRing_::ParamData>,
    decltype(Base(ZuDeclVal<Arg0 &&>(), ZuDeclVal<Args &&>()...), int()) = 0>
  Params_(Arg0 &&arg0, Args &&...args) :
      Base(ZuFwd<Arg0>(arg0), ZuFwd<Args>(args)...) { }
  Params_ &operator =(const Params_ &) = default;
  Params_ &operator =(Params_ &&) = default;

  Derived &&name(ZuCSpan s) { Data::name = s; return derived(); }
  Derived &&killWait(unsigned n) { Data::killWait = n; return derived(); }
  Derived &&coredump(bool b) { Data::coredump = b; return derived(); }
};

class Params : public Params_<Params> {
  using Base = Params_<Params>;

public:
  Params() = default;
  Params(const Params &) = default;
  Params(Params &&) = default;
  template <
    typename Arg0, typename ...Args,
    typename = ZuIsNot<Arg0, ZmRing_::ParamData>,
    decltype(Base(ZuDeclVal<Arg0 &&>(), ZuDeclVal<Args &&>()...), int()) = 0>
  Params(Arg0 &&arg0, Args &&...args) :
      Base(ZuFwd<Arg0>(arg0), ZuFwd<Args>(args)...) { }
  Params &operator =(const Params &) = default;
  Params &operator =(Params &&) = default;
};

class ZiAPI Blocker {
public:
  using Params = ParamData;

  Blocker();
  ~Blocker();

  Blocker(const Blocker &blocker);
  Blocker &operator =(const Blocker &blocker) {
    if (this != &blocker) {
      this->~Blocker();
      new (this) Blocker{blocker};
    }
    return *this;
  }

  bool open(bool head, const Params &);
  void close();

  // block until woken or timeout while addr == val
  int wait(
      ZmAtomic<uint32_t> &addr, uint32_t val,
      const Params &params);
  // wake up waiters on addr
  void wake(ZmAtomic<uint32_t> &addr);

protected:
#ifdef _WIN32
  HANDLE		m_sem;
#endif
};

class ZiAPI CtrlMem {
public:
  using Params = ParamData;

  CtrlMem() = default;
  CtrlMem(const CtrlMem &mem) : m_file{mem.m_file} { }

  bool open(unsigned size, const Params &params);
  void close();

  ZuInline const void *addr() const { return m_file.addr(); }
  ZuInline void *addr() { return m_file.addr(); }

protected:
  ZiMMapFile	m_file;
};

template <bool MW> struct Ctrl;
template <>
struct Ctrl<true> : public ZmRing_::Ctrl<true> {
  ZmAtomic<uint32_t>		openSize; // opened size
  uint32_t			pad_6;
  uint32_t			rdrPID[MaxRdrs];
  ZuTime			rdrTime[MaxRdrs];
};
template <>
struct Ctrl<false> : public Ctrl<true> {
  ZmAtomic<uint32_t>		writerPID;
  ZuTime			writerTime;
};

template <typename CtrlMem_, typename Ctrl_, bool MW>
class CtrlMgr_ : public ZmRing_::CtrlMgr_<CtrlMem_, Ctrl_, true> {
protected:
  using CtrlMem = CtrlMem_;
  using Params = typename CtrlMem::Params;
  using Ctrl = Ctrl_;

private:
  using Base = ZmRing_::CtrlMgr_<CtrlMem, Ctrl, true>;

protected:
  using Base::ctrl;

  CtrlMgr_() = default;
  CtrlMgr_(const CtrlMgr_ &mgr) : Base{mgr} { }

  // PIDs may be re-used by the OS, so processes are ID'd by PID + start time

  ZuInline ZmAtomic<uint32_t> &openSize() { return ctrl()->openSize; }
  ZuInline const ZmAtomic<uint32_t> &openSize() const
    { return ctrl()->openSize; }
  ZuInline uint32_t *rdrPID() { return ctrl()->rdrPID; }
  ZuInline const uint32_t *rdrPID() const { return ctrl()->rdrPID; }
  ZuInline ZuTime *rdrTime() { return ctrl()->rdrTime; }
  ZuInline const ZuTime *rdrTime() const { return ctrl()->rdrTime; }

  ZuInline ZmAtomic<uint32_t> &writerPID();		// unused
  ZuInline const ZmAtomic<uint32_t> &writerPID() const;	// ''
  ZuInline ZuTime &writerTime();			// ''
  ZuInline const ZuTime &writerTime() const;		// ''
};
template <typename CtrlMem_, typename Ctrl_>
class CtrlMgr_<CtrlMem_, Ctrl_, false> :
    public CtrlMgr_<CtrlMem_, Ctrl_, true> {
protected:
  using CtrlMem = CtrlMem_;
  using Ctrl = Ctrl_;

private:
  using Base = CtrlMgr_<CtrlMem, Ctrl, true>;

public:
  using Base::ctrl;

  CtrlMgr_() = default;
  CtrlMgr_(const CtrlMgr_ &mgr) : Base{mgr} { }

protected:
  ZuInline ZmAtomic<uint32_t> &writerPID() { return ctrl()->writerPID; }
  ZuInline const ZmAtomic<uint32_t> &writerPID() const
    { return ctrl()->writerPID; }
  ZuInline ZuTime &writerTime() { return ctrl()->writerTime; }
  ZuInline const ZuTime &writerTime() const { return ctrl()->writerTime; }
};
template <typename CtrlMem, bool MW>
using CtrlMgr = CtrlMgr_<CtrlMem, Ctrl<MW>, MW>;

class ZiAPI DataMem {
public:
  using Params = ParamData;

  DataMem() = default;
  DataMem(const DataMem &mem) : m_file{mem.m_file} { }

  bool open(unsigned size, const Params &params);
  void close();

  ZuInline const void *addr() const { return m_file.addr(); }
  ZuInline void *addr() { return m_file.addr(); }

private:
  ZiMMapFile	m_file;
};

class ZiAPI MirrorMem {
public:
  using Params = ParamData;

  MirrorMem() = default;
  MirrorMem(const MirrorMem &mem) : m_file{mem.m_file} { }

  bool open(unsigned size, const Params &params);
  void close();

  ZuInline const void *addr() const { return m_file.addr(); }
  ZuInline void *addr() { return m_file.addr(); }

  static unsigned alignSize(unsigned size) {
    return ZmRing_::MirrorMem::alignSize(size);
  }

private:
  ZiMMapFile	m_file;
};

// ring extensions base class for shared memory IPC
class ZiAPI RingExt_ {
public:
  static void getpinfo(uint32_t &pid, ZuTime &start);
  static bool alive(uint32_t pid, ZuTime start);
  static bool kill(uint32_t pid, bool coredump);
};

// generic ring extensions for shared memory IPC - always multi-reader
template <typename Ring, bool MW, bool>
class RingExt :
    public RingExt_,
    public ZmRing_::RingExt<Ring, MW, true> {
  using Base = ZmRing_::RingExt<Ring, MW, true>;

  Ring *ring() { return static_cast<Ring *>(this); }
  const Ring *ring() const { return static_cast<const Ring *>(this); }

public:
  using Friend = Base; // extend ZmRing friendship to ZmRing_::RingExt

  RingExt() = default;

  RingExt(const RingExt &ring) : Base{ring} { }
  RingExt &operator =(const RingExt &ring) {
    if (this != &ring) {
      this->~RingExt();
      new (this) RingExt{ring};
    }
    return *this;
  }

  RingExt(RingExt &&) = delete;
  RingExt &operator =(RingExt &&) = delete;

  // can be called by writer if ring is full to garbage collect
  // dead readers and any lingering messages intended exclusively for them;
  // returns space freed
  unsigned gc();

  // kills all stalled readers (following a timeout), sleeps, then runs gc()
  unsigned kill();

protected:
  uint32_t openSize_(uint32_t size);

  bool open_();
  void close_();

  void attached(unsigned id);
  void detached(unsigned id);
};

template <typename Ring, bool MW, bool MR>
inline uint32_t RingExt<Ring, MW, MR>::openSize_(uint32_t reqSize)
{
  if (reqSize) {
    // check that requested sizes and latency are consistent
    if (uint32_t openSize = ring()->openSize().cmpXch(reqSize, 0))
      if (openSize != reqSize) return 0;
  } else {
    uint32_t openSize = ring()->openSize();
    if (!openSize) return 0;
    reqSize = openSize;
  }
  return reqSize;
}

template <typename Ring, bool MW, bool MR>
inline bool RingExt<Ring, MW, MR>::open_()
{
  if (!Base::open_()) return false;

  if constexpr (!MW)
    if (ring()->flags() & Ring::Write) {
      uint32_t pid;
      ZuTime start;
      getpinfo(pid, start);
      uint32_t oldPID = ring()->writerPID().load_();
      if (alive(oldPID, ring()->writerTime()) ||
	  ring()->writerPID().cmpXch(pid, oldPID) != oldPID) {
	Base::close_();
	return false;
      }
      ring()->writerTime() = start;
    }

  return true;
}

template <typename Ring, bool MW, bool MR>
inline void RingExt<Ring, MW, MR>::close_()
{
  if constexpr (!MW)
    if (ring()->flags() & Ring::Write) {
      ring()->writerTime() = ZuTime{}; // subsequent writerPID store releases
      ring()->writerPID() = 0;
    }

  Base::close_();
}

template <typename Ring, bool MW, bool MR>
inline unsigned RingExt<Ring, MW, MR>::gc()
{
  ZmAssert(ring()->ctrl());
  ZmAssert(ring()->flags() & Ring::Write);

  const auto &params = ring()->params();

  auto rdrPID = ring()->rdrPID();
  auto rdrTime = ring()->rdrTime();

  // GC dead readers

  unsigned freed = 0;
  uint64_t dead;
  unsigned rdrCount;

  // below loop is a probe - as long as any concurrent attach() or
  // detach() overlap with our discovery of dead readers, the results
  // are unreliable and the probe must be re-attempted - after N attempts
  // give up and return 0
  for (unsigned i = 0;; ) {
    uint64_t attSeqNo = ring()->attSeqNo().load_();
    dead = ring()->rdrMask(); // assume all dead
    rdrCount = 0;
    if (dead) {
      for (unsigned id = 0; id < MaxRdrs; id++) {
	if (!(dead & (1ULL<<id))) continue;
	if (alive(rdrPID[id], rdrTime[id])) {
	  dead &= ~(1ULL<<id);
	  ++rdrCount;
	}
      }
    }
    if (attSeqNo == ring()->attSeqNo()) break;
    Zm::yield();
    if (++i == params.spin) return 0;
  }

  // remove dead readers from rdrMask
  for (unsigned id = 0; id < MaxRdrs; id++)
    if (dead & (1ULL<<id))
      if (rdrPID[id])
	ring()->rdrMask() &= ~(1ULL<<id);

  // clear messages intended for dead readers

  auto data = ring()->data();
  auto size = ring()->size();

  uint32_t tail_ = ring()->tail(); // acquire
  uint32_t tail = tail_ & ~Mask32();
  uint32_t head;
  if constexpr (MW)
    do { head = ring()->head().load_(); } while (head & Locked32());
  else
    head = ring()->head().load_();
  head &= ~Mask32();

  while (tail != head) {
    auto hdrPtr = reinterpret_cast<ZmAtomic<uint64_t> *>(
	&data[tail & ~Wrapped32()]);
    auto msgSize = ring()->align(Ring::SizeAxor(&hdrPtr[1]));
    tail += msgSize;
    if ((tail & ~Wrapped32()) >= size) tail = (tail ^ Wrapped32()) - size;
    uint64_t mask = hdrPtr->xchAnd(~dead);
    if (mask && !(mask & (~dead & RdrMask()))) {
      freed += msgSize;
      ring()->wakeWriters(tail);
    }

    if constexpr (MW)
      do { head = ring()->head().load_(); } while (head & Locked32());
    else
      head = ring()->head().load_();
    head &= ~Mask32();
  }

  // detach dead readers
  for (unsigned id = 0; id < MaxRdrs; id++)
    if (dead & (1ULL<<id))
      if (rdrPID[id]) {
	detached(id);
	++(ring()->attSeqNo());
	ring()->attMask() &= ~(1ULL<<id);
      }
  ring()->rdrCount() = rdrCount;

  return freed;
}

template <typename Ring, bool MW, bool MR>
inline unsigned RingExt<Ring, MW, MR>::kill()
{
  const auto &params = ring()->params();

  auto data = ring()->data();
  auto rdrPID = ring()->rdrPID();

  uint64_t hdr;
  {
    uint32_t tail = ring()->tail() & ~(Wrapped32() | Mask32());
    auto hdrPtr = reinterpret_cast<ZmAtomic<uint64_t> *>(&data[tail]);
    hdr = *hdrPtr;
  }
  for (unsigned id = 0; id < MaxRdrs; id++)
    if (hdr & (1ULL<<id))
      kill(rdrPID[id], params.coredump);
  Zm::sleep(ZuTime{time_t(params.killWait)});
  return gc();
}

template <typename Ring, bool MW, bool MR>
inline void RingExt<Ring, MW, MR>::attached(unsigned id)
{
  getpinfo((ring()->rdrPID())[id], (ring()->rdrTime())[id]);
}

template <typename Ring, bool MW, bool MR>
inline void RingExt<Ring, MW, MR>::detached(unsigned id)
{
  (ring()->rdrPID())[id] = 0;
  (ring()->rdrTime())[id] = ZuTime{};
}

} // ZiRing_

using ZiRingParams = ZiRing_::Params;

template <typename NTP = ZiRing_::Defaults>
using ZiRing = ZmRing_::Ring<
  ZmRingMR<true, NTP>,
  ZiRing_::ParamData,
  ZiRing_::Blocker,
  ZiRing_::CtrlMgr<ZiRing_::CtrlMem, NTP::MW>,
  ZmRing_::DataMgr<
    ZiRing_::DataMem, ZiRing_::MirrorMem, typename NTP::T, NTP::MW, true>,
  ZiRing_::RingExt>;

#endif /* ZiRing_HH */
