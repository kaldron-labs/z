//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// local memory intra-process ring buffer
// - single/multiple writers/producers and readers/consumers
//   - supports SWSR MWSR SWMR MWMR
// - fixed- and variable-sized messages (types)
// - MR is broadcast
//   - for unicast, shard writes to multiple MWSR ring buffers
//   - most applications require sharding to ensure correct sequencing,
//     and sharding to multiple ring buffers is more performant than
//     multiple readers contending on a single ring buffer and
//     skipping past all the messages intended for other readers

#ifndef ZmRing_HH
#define ZmRing_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZuIOResult.hh>

#include <zlib/ZmPlatform.hh>
#include <zlib/ZmAssert.hh>
#include <zlib/ZmBitmap.hh>
#include <zlib/ZmTopology.hh>
#include <zlib/ZmLock.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZmBackTrace.hh>

#ifdef ZmRing_FUNCTEST
#define ZmRing_bp(this_, name) this_->bp_##name.reached(#name)
#else
#ifdef ZmRing_STRESSTEST
#define ZmRing_bp(this_, name) ZmPlatform::yield()
#else
#define ZmRing_bp(this_, name) (void())
#endif
#endif

// ring buffer parameters

// uses NTP (named template parameters):
//
// ZmRingT<T>			// ring of fixed-size T

namespace ZmRing_ {

// NTP defaults
constexpr auto Defaults_SizeAxor() {
  return [](const void *) { return 0; };
}
struct Defaults {
  using T = void;	// variable-sized
  static constexpr auto SizeAxor = Defaults_SizeAxor();
  enum { MW = 0 };	// default to single-writer
  enum { MR = 0 };	// default to single-reader
};

} // ZmRing_

// fixed-size message type
template <typename T>
struct ZmRingT_SizeAxor {
  static constexpr auto Fn() {
    return [](const void *) { return sizeof(T); };
  }
};
template <typename T_, typename NTP = ZmRing_::Defaults>
struct ZmRingT : public NTP {
  using T = T_;
  static constexpr auto SizeAxor = ZmRingT_SizeAxor<T>::Fn();
};
template <typename NTP>
struct ZmRingT<void, NTP> : public NTP {
  using T = void;
  static constexpr auto SizeAxor = ZmRing_::Defaults_SizeAxor();
};

// SizeAxor is used for variable-sized messages
template <auto SizeAxor_, typename NTP = ZmRing_::Defaults>
struct ZmRingSizeAxor : public NTP {
  static constexpr auto SizeAxor = SizeAxor_;
};

// multiple writers (producers)
template <bool MW_, typename NTP = ZmRing_::Defaults>
struct ZmRingMW : public NTP {
  enum { MW = MW_ };
};

// multiple readers (consumers)
template <bool MR_, typename NTP = ZmRing_::Defaults>
struct ZmRingMR : public NTP {
  enum { MR = MR_ };
};

namespace ZmRing_ {

// ring buffer parameters

struct ParamData {
  unsigned	size = 0;
  bool		ll = false;
  ZmBitmap	cpuset;
  unsigned	spin = 1000;
  unsigned	timeout = 1;	// milliseconds

  inline const ParamData &data() { return *this; } // upcast
};

template <typename Derived, typename Data = ParamData>
class Params_ : public Data {
  using Base = Data;

  Derived &&derived() { return ZuMv(*static_cast<Derived *>(this)); }

public:
  Params_() = default;
  Params_(const Params_ &) = default;
  Params_(Params_ &&) = default;
  template <
    typename Arg0, typename ...Args,
    typename = ZuIsNot<Arg0, ParamData>,
    decltype(Base(ZuDeclVal<Arg0 &&>(), ZuDeclVal<Args &&>()...), int()) = 0>
  Params_(Arg0 &&arg0, Args &&...args) :
      Base(ZuFwd<Arg0>(arg0), ZuFwd<Args>(args)...) { }
  Params_ &operator =(const Params_ &) = default;
  Params_ &operator =(Params_ &&) = default;

  Derived &&size(unsigned n) { Data::size = n; return derived(); }
  Derived &&ll(bool b) { Data::ll = b; return derived(); }
  Derived &&cpuset(ZmBitmap b) { Data::cpuset = ZuMv(b); return derived(); }
  Derived &&spin(unsigned n) { Data::spin = n; return derived(); }
  Derived &&timeout(unsigned n) { Data::timeout = n; return derived(); }

  unsigned size() const { return Data::size; }
  bool ll() const { return Data::ll; }
  const ZmBitmap &cpuset() const { return Data::cpuset; }
  unsigned spin() const { return Data::spin; }
  unsigned timeout() const { return Data::timeout; }
};

class Params : public Params_<Params> {
  using Base = Params_<Params>;

public:
  Params() = default;
  Params(const Params &) = default;
  Params(Params &&) = default;
  template <
    typename Arg0, typename ...Args,
    typename = ZuIsNot<Arg0, ParamData>,
    decltype(Base(ZuDeclVal<Arg0 &&>(), ZuDeclVal<Args &&>()...), int()) = 0>
  Params(Arg0 &&arg0, Args &&...args) :
      Base(ZuFwd<Arg0>(arg0), ZuFwd<Args>(args)...) { }
  Params &operator =(const Params &) = default;
  Params &operator =(Params &&) = default;
};

class ZmAPI Blocker {
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
  HANDLE	m_sem = 0;
#endif
};

// use of bit flags in the various modes
//
//            head tail hdr
// Wrapped    *    *
// Locked     MW
// EndOfFile  *         !SWSR
// Waiting    SWSR *    !SWSR

// 64bit versions of flags
constexpr uint64_t EndOfFile() { return uint64_t(1)<<62; }
constexpr uint64_t Waiting()   { return uint64_t(2)<<62; }
constexpr uint64_t Mask()      { return EndOfFile() | Waiting(); }
constexpr uint64_t RdrMask() { return ~(uint64_t(3)<<62); }
enum { MaxRdrs = 62 };
#if Zu_BIGENDIAN
enum { Flags32Offset = 0 };	// 32bit offset of flags within 64bit header
#else
enum { Flags32Offset = 1 };
#endif
// 32bit versions of flags
constexpr uint32_t Wrapped32()   { return uint32_t(1)<<28; }
constexpr uint32_t Locked32()    { return uint32_t(2)<<28; }
constexpr uint32_t EndOfFile32() { return uint32_t(4)<<28; }
constexpr uint32_t Waiting32()   { return uint32_t(8)<<28; }
constexpr uint32_t Mask32() {
  return Locked32() | EndOfFile32() | Waiting32();
}

// control block
class ZmAPI CtrlMem {
public:
  using Params = ParamData;

  CtrlMem() = default;
  CtrlMem(const CtrlMem &mem) :
      m_addr{mem.m_addr}, m_size{mem.m_size}, m_shadow{true} { }
  ~CtrlMem() { close(); }

  bool open(unsigned size, const Params &params);
  void close();

  ZuInline const void *addr() const { return m_addr; }
  ZuInline void *addr() { return m_addr; }

protected:
  void		*m_addr = nullptr;
  unsigned	m_size = 0;
  bool		m_shadow = false;
};

template <bool MR> struct Ctrl {
  alignas(Zm::CacheLineSize)
  ZmAtomic<uint32_t>		head;
  ZmAtomic<uint64_t>		inCount;
  ZmAtomic<uint64_t>		inBytes;

  alignas(Zm::CacheLineSize)
  ZmAtomic<uint32_t>		tail;
  ZmAtomic<uint64_t>		outCount;
  ZmAtomic<uint64_t>		outBytes;
};

template <> struct Ctrl<true> : public Ctrl<false> {
  alignas(Zm::CacheLineSize)
  ZmAtomic<uint32_t>		rdrCount; // reader count
  ZmAtomic<uint64_t>		rdrMask;  // active readers
  ZmAtomic<uint64_t>		attMask;  // readers pending attach
  ZmAtomic<uint64_t>		attSeqNo; // attach/detach seqNo
};

template <typename CtrlMem_, typename Ctrl_, bool MR>
class CtrlMgr_ {
protected:
  using CtrlMem = CtrlMem_;
  using Params = typename CtrlMem::Params;
  using Ctrl = Ctrl_;

  CtrlMgr_() = default;
  CtrlMgr_(const CtrlMgr_ &mgr) : m_ctrl{mgr.m_ctrl} { }

  bool openCtrl(const Params &params) {
    return m_ctrl.open(sizeof(Ctrl), params);
  }
  void closeCtrl() {
    m_ctrl.close();
  }

public:
  constexpr unsigned ctrlSize() const { return sizeof(Ctrl); }

  ZuInline const Ctrl *ctrl() const {
    return static_cast<const Ctrl *>(m_ctrl.addr());
  }
  ZuInline Ctrl *ctrl() {
    return static_cast<Ctrl *>(m_ctrl.addr());
  }

protected:
  ZuInline const ZmAtomic<uint32_t> &head() const { return ctrl()->head; }
  ZuInline ZmAtomic<uint32_t> &head() { return ctrl()->head; }

  ZuInline const ZmAtomic<uint32_t> &tail() const { return ctrl()->tail; }
  ZuInline ZmAtomic<uint32_t> &tail() { return ctrl()->tail; }

  ZuInline const ZmAtomic<uint64_t> &inCount() const { return ctrl()->inCount; }
  ZuInline ZmAtomic<uint64_t> &inCount() { return ctrl()->inCount; }
  ZuInline const ZmAtomic<uint64_t> &inBytes() const { return ctrl()->inBytes; }
  ZuInline ZmAtomic<uint64_t> &inBytes() { return ctrl()->inBytes; }
  ZuInline const ZmAtomic<uint64_t> &outCount() const
    { return ctrl()->outCount; }
  ZuInline ZmAtomic<uint64_t> &outCount() { return ctrl()->outCount; }
  ZuInline const ZmAtomic<uint64_t> &outBytes() const
    { return ctrl()->outBytes; }
  ZuInline ZmAtomic<uint64_t> &outBytes() { return ctrl()->outBytes; }

  ZmAtomic<uint32_t> &rdrCount();		// unused
  const ZmAtomic<uint32_t> &rdrCount() const;	// ''
  ZmAtomic<uint64_t> &rdrMask();		// ''
  const ZmAtomic<uint64_t> &rdrMask() const;	// ''
  ZmAtomic<uint64_t> &attMask();		// ''
  const ZmAtomic<uint64_t> &attMask() const;	// ''
  ZmAtomic<uint64_t> &attSeqNo();		// ''
  const ZmAtomic<uint64_t> &attSeqNo() const;	// ''

private:
  CtrlMem	m_ctrl;
};
template <typename CtrlMem_, typename Ctrl_>
class CtrlMgr_<CtrlMem_, Ctrl_, true> :
    public CtrlMgr_<CtrlMem_, Ctrl_, false> {
protected:
  using CtrlMem = CtrlMem_;
  using Ctrl = Ctrl_;

private:
  using Base = CtrlMgr_<CtrlMem, Ctrl, false>;

public:
  using Base::ctrl;

  CtrlMgr_() = default;
  CtrlMgr_(const CtrlMgr_ &mgr) : Base{mgr} { }

protected:
  ZuInline ZmAtomic<uint32_t> &rdrCount() { return ctrl()->rdrCount; }
  ZuInline const ZmAtomic<uint32_t> &rdrCount() const
    { return ctrl()->rdrCount; }
  ZuInline ZmAtomic<uint64_t> &rdrMask() { return ctrl()->rdrMask; }
  ZuInline const ZmAtomic<uint64_t> &rdrMask() const
    { return ctrl()->rdrMask; }
  ZuInline ZmAtomic<uint64_t> &attMask() { return ctrl()->attMask; }
  ZuInline const ZmAtomic<uint64_t> &attMask() const
    { return ctrl()->attMask; }
  ZuInline ZmAtomic<uint64_t> &attSeqNo() { return ctrl()->attSeqNo; }
  ZuInline const ZmAtomic<uint64_t> &attSeqNo() const
    { return ctrl()->attSeqNo; }
};
template <typename CtrlMem, bool MR>
using CtrlMgr = CtrlMgr_<CtrlMem, Ctrl<MR>, MR>;

template <bool MW, bool MR> struct AlignFn {
  static constexpr unsigned align(unsigned n) {
    return (((n) + 8 + 15) & ~15);
  }
};
template <> struct AlignFn<false, false> {
  static constexpr unsigned align(unsigned n) {
    return (((n) + 15) & ~15);
  }
};

// data block
class ZmAPI DataMem {
public:
  using Params = ParamData;

  DataMem() = default;
  DataMem(const DataMem &mem) :
      m_addr{mem.m_addr}, m_size{mem.m_size}, m_shadow{true} { }
  ~DataMem() { close(); }

  bool open(unsigned size, const Params &params);
  void close();

  ZuInline const void *addr() const { return m_addr; }
  ZuInline void *addr() { return m_addr; }

private:
  void		*m_addr = nullptr;
  unsigned	m_size = 0;
  bool		m_shadow = false;
};

class ZmAPI MirrorMem {
public:
  using Params = ParamData;

  MirrorMem() = default;
  MirrorMem(const MirrorMem &mem) :
      m_handle{nullHandle()},
      m_addr{mem.m_addr},
      m_size{mem.m_size} { }
  ~MirrorMem() { close(); }

  static unsigned alignSize(unsigned size);

  bool open(unsigned size, const Params &params);
  void close();

  ZuInline const void *addr() const { return m_addr; }
  ZuInline void *addr() { return m_addr; }

private:
#ifndef _WIN32
  using Handle = int;
  static constexpr Handle nullHandle() { return -1; }
  bool nullHandle(Handle i) { return i < 0; }
#else
  using Handle = HANDLE;
  static Handle nullHandle() { return INVALID_HANDLE_VALUE; }
  bool nullHandle(Handle i) { return !i || i == INVALID_HANDLE_VALUE; }
#endif

  Handle	m_handle = nullHandle();
  void		*m_addr = nullptr;
  unsigned	m_size = 0;
};

template <typename DataMem_> class DataMgr_ {
protected:
  using DataMem = DataMem_;
  using Params = typename DataMem::Params;

  DataMgr_() = default;
  DataMgr_(const DataMgr_ &ring) : m_data{ring.m_data} { }

  bool openData(unsigned size, const Params &params) {
    return m_data.open(size, params);
  }
  void closeData() {
    m_data.close();
  }

public:
  ZuInline const uint8_t *data() const {
    return reinterpret_cast<const uint8_t *>(m_data.addr());
  }
  ZuInline uint8_t *data() {
    return reinterpret_cast<uint8_t *>(m_data.addr());
  }

private:
  DataMem		m_data;
};

template <
  typename DataMem_, typename MirrorMem_,
  typename T, bool MW, bool MR>
class DataMgr : public DataMgr_<DataMem_> {
protected:
  using DataMem = DataMem_;
  using Params = typename DataMem::Params;

private:
  using Base = DataMgr_<DataMem>;

public:
  enum { MsgSize = AlignFn<MW, MR>::align(sizeof(T)) };

protected:
  DataMgr() = default;
  DataMgr(const DataMgr &ring) : Base{ring} { }

  static constexpr unsigned alignSize(unsigned n) {
    return ((n + (MsgSize<<1) - 1) / MsgSize) * MsgSize;
  }
};
template <typename DataMem_, typename MirrorMem_, bool MW, bool MR>
class DataMgr<DataMem_, MirrorMem_, void, MW, MR> :
    public DataMgr_<MirrorMem_> {
protected:
  using DataMem = MirrorMem_;

private:
  using Base = DataMgr_<DataMem>;

protected:
  enum { MsgSize = 0 };

  DataMgr() = default;
  DataMgr(const DataMgr &ring) : Base{ring} { }

  static unsigned alignSize(unsigned n) {
    return DataMem::alignSize(n);
  }
};

// CRTP - ring extensions template for multiple readers, shared memory, etc.
template <typename Ring, bool MW, bool MR>
class RingExt {
  Ring *ring() { return static_cast<Ring *>(this); }
  const Ring *ring() const { return static_cast<const Ring *>(this); }

public:
  using Friend = RingExt;

  RingExt() = default;

  RingExt(const RingExt &) = default;
  RingExt &operator =(const RingExt &ring) = default;

  RingExt(RingExt &&) = delete;
  RingExt &operator =(RingExt &&) = delete;

  int attach();				// unused
  void detach();			// ''

  int rdrID() const { return 0; }	// ''

protected:
  uint32_t openSize_(uint32_t size) { return size; } // ''

  bool open_() { return true; }		// ''
  void close_() { }			// ''

  static constexpr unsigned gc() { return 0; } // ''

  void rdrID(int);			// ''

  uint32_t rdrTail() const;		// ''
  void rdrTail(uint32_t);		// ''

  constexpr void attached(unsigned) { }	// ''
  constexpr void detached(unsigned) { }	// ''
};
// CRTP - multiple reader ring extensions
template <typename Ring, bool MW>
class RingExt<Ring, MW, true> {
  Ring *ring() { return static_cast<Ring *>(this); }
  const Ring *ring() const { return static_cast<const Ring *>(this); }

public:
  using Friend = RingExt;

  RingExt() = default;

  RingExt(const RingExt &) = default;
  RingExt &operator =(const RingExt &ring) = default;

  RingExt(RingExt &&) = delete;
  RingExt &operator =(RingExt &&) = delete;

  int attach();
  void detach();

  int rdrID() const { return m_rdrID; }

protected:
  uint32_t openSize_(uint32_t size) { return size; }

  bool open_();
  void close_();

  static constexpr unsigned gc() { return 0; } // unused

  void rdrID(int v) { m_rdrID = v; }

  uint32_t rdrTail() const { return m_rdrTail; }
  void rdrTail(uint32_t v) { m_rdrTail = v; }

  constexpr void attached(unsigned) { }	// ''
  constexpr void detached(unsigned) { }	// ''

private:
  int		m_rdrID = -1;
  uint32_t	m_rdrTail = 0;
};

template <
  typename NTP = Defaults,
  typename ParamData_ = ParamData,
  typename Blocker_ = Blocker,
  typename CtrlMgr_ = CtrlMgr<CtrlMem, NTP::MR>,
  typename DataMgr_ =
    DataMgr<DataMem, MirrorMem, typename NTP::T, NTP::MW, NTP::MR>,
  template <typename, bool, bool> typename RingExt_ = RingExt>
class Ring :
  public AlignFn<NTP::MW, NTP::MR>,
  public CtrlMgr_,
  public DataMgr_,
  public RingExt_<
    Ring<NTP, ParamData_, Blocker_, CtrlMgr_, DataMgr_, RingExt_>,
    NTP::MW, NTP::MR> {
public:
  using T = typename NTP::T;
  enum { MW = NTP::MW };
  enum { MR = NTP::MR };

protected:
  using ParamData = ParamData_;
  using Blocker = Blocker_;
  using CtrlMgr = CtrlMgr_;
  using DataMgr = DataMgr_;
  using RingExt = RingExt_<Ring, MW, MR>;
friend RingExt;
friend typename RingExt::Friend;

private:
  using AlignFn = ZmRing_::AlignFn<MW, MR>;

public:
  static constexpr auto SizeAxor = NTP::SizeAxor;
  enum { V = ZuIsSame<T, void>{} };
  enum { MsgSize = DataMgr_::MsgSize };

  // MR requires a non-default SizeAxor
  ZuAssert((!MR ||
      !ZuIsSame<decltype(Defaults::SizeAxor), decltype(SizeAxor)>{}));

  enum { // open() flags
    Read	= 0x00000001,
    Write	= 0x00000002,
    Shadow	= 0x00000004
  };

  Ring() = default;

  template <typename Params, typename ...Args, typename = ZuIsNot<Params, Ring>>
  Ring(Params params, Args &&...args) :
      m_params{ZuMv(params)} { }

  Ring(const Ring &ring) :
      CtrlMgr{ring}, DataMgr{ring}, RingExt{ring},
      m_params{ring.m_params},
      m_headBlocker{ring.m_headBlocker},
      m_tailBlocker{ring.m_tailBlocker},
      m_flags{Shadow}, m_size{ring.m_size} { }
  Ring &operator =(const Ring &ring) {
    if (this != &ring) {
      this->~Ring();
      new (this) Ring{ring};
    }
    return *this;
  }

  Ring(Ring &&) = delete;
  Ring &operator =(Ring &&) = delete;

  ~Ring() { close(); }

  template <typename Params>
  void init(Params params) { m_params = ZuMv(params); }

  auto &params() { return m_params; }
  const auto &params() const { return m_params; }

  auto &headBlocker() { return m_headBlocker; }
  const auto &headBlocker() const { return m_headBlocker; }
  auto &tailBlocker() { return m_tailBlocker; }
  const auto &tailBlocker() const { return m_tailBlocker; }

  uint32_t flags() const { return m_flags; }

  unsigned size() const { return m_size; }
  static constexpr unsigned ctrlSize() { return sizeof(Ctrl); }

  // returns how many times push() was delayed by this ring buffer being full
  unsigned full() const { return m_full; }

private:
  using AlignFn::align;

  using Ctrl = typename CtrlMgr_::Ctrl;
private:
  using CtrlMgr::openCtrl;
  using CtrlMgr::closeCtrl;
public:
  using CtrlMgr::ctrl;
private:
  using CtrlMgr::head;
  using CtrlMgr::tail;
  using CtrlMgr::inCount;
  using CtrlMgr::inBytes;
  using CtrlMgr::outCount;
  using CtrlMgr::outBytes;
  using CtrlMgr::rdrCount;
  using CtrlMgr::rdrMask;
  using CtrlMgr::attMask;
  using CtrlMgr::attSeqNo;

private:
  using DataMgr::openData;
  using DataMgr::closeData;
  using DataMgr::alignSize;
public:
  using DataMgr::data;

  using RingExt::attach;
  using RingExt::detach;
  using RingExt::rdrID;
private:
  using RingExt::rdrTail;
  using RingExt::openSize_;
  using RingExt::open_;
  using RingExt::close_;
  using RingExt::gc;

public:
  bool closed() const { return !m_size; }

  int open(unsigned flags) {
    flags &= (Read | Write);
    if (m_flags &Shadow) {
      if (m_flags & (Read | Write)) {
	if ((m_flags & (Read | Write)) == flags) return Zu::OK;
	return Zu::IOError;
      }
    } else {
      if (ctrl()) return Zu::OK;
      if (!m_headBlocker.open(true, m_params))
	return Zu::IOError;
      if (!m_tailBlocker.open(false, m_params)) {
	m_headBlocker.close();
	return Zu::IOError;
      }
      m_flags = flags;
      if (!openCtrl(params())) {
	m_headBlocker.close();
	m_tailBlocker.close();
	m_flags = 0;
	return Zu::IOError;
      }
      m_size = openSize_(params().size ? alignSize(params().size) : 0);
      if (!m_size) {
	closeCtrl();
	m_headBlocker.close();
	m_tailBlocker.close();
	m_flags = 0;
	return Zu::IOError;
      }
      if (!openData(m_size, params())) {
	closeCtrl();
	m_headBlocker.close();
	m_tailBlocker.close();
	m_flags = 0;
	m_size = 0;
	return Zu::IOError;
      }
    }
    m_flags |= flags;
    if (!open_()) {
      m_flags &= ~(Read | Write);
      if (!(m_flags &Shadow)) {
	closeCtrl();
	closeData();
	m_headBlocker.close();
	m_tailBlocker.close();
      }
      m_flags = 0;
      m_size = 0;
      return Zu::IOError;
    }
    if (flags &Write) {
      eof(false);
      gc();
    }
    return Zu::OK;
  }

  void close() {
    if (closed()) return;
    m_size = 0;
    close_();
    if (!(m_flags &Shadow)) {
      closeCtrl();
      closeData();
      m_headBlocker.close();
      m_tailBlocker.close();
    }
    m_flags = 0;
  }

  int reset() {
    if (ZuUnlikely(closed() || (m_flags &Shadow))) return Zu::IOError;
    auto flags = (m_flags & (Read | Write));
    close_();
    m_flags &= ~(Read | Write);
    ZuGuard guard([this, flags]() { m_flags |= flags; open_(); });
    if constexpr (MR) {
      if (rdrMask()) return Zu::NotReady;
    }
    memset(static_cast<void *>(ctrl()), 0, sizeof(Ctrl));
    memset(data(), 0, m_size);
    m_full = 0;
    openSize_(m_size); // restore shared capacity cleared with the control block
    return Zu::OK;
  }

  unsigned length() {
    uint32_t head = this->head().load_() & ~Mask32();
    uint32_t tail = this->tail().load_() & ~Mask32();
    if (head == tail) return 0;
    if ((head ^ tail) == Wrapped32()) return size();
    head &= ~Wrapped32();
    tail &= ~Wrapped32();
    if (head > tail) return head - tail;
    return size() - (tail - head);
  }

  // inspection accessors

  uint32_t head_() const { return this->head().load_(); }
  uint32_t tail_() const {
    if constexpr (MR)
      return rdrTail();
    else
      return this->tail().load_();
  }

  // writer

  template <bool V_ = V, typename = ZuIfT<!V_>>
  ZuInline void *push() {
    return push_<1>();
  }
  template <bool V_ = V, typename = ZuIfT<V_>>
  ZuInline void *push(unsigned size) {
    return push_<1>(size);
  }
  template <bool V_ = V, typename = ZuIfT<!V_>>
  ZuInline void *tryPush() {
    return push_<0>();
  }
  template <bool V_ = V, typename = ZuIfT<V_>>
  ZuInline void *tryPush(unsigned size) {
    return push_<0>(size);
  }

private:
#define ZmRing_move_head(msgSize) \
    head += msgSize; \
    if (ZuUnlikely((head & ~(Wrapped32() | Mask32())) >= this->size())) \
      head = (head ^ Wrapped32()) - this->size()

  // ring buffers rely on clear-ahead if readers wait on the header
  // rather than on the control block, i.e. (MW || MR); in these cases
  // the head must be prevented from hitting the tail, maintaining
  // enough space in between for a blank 64bit header where the next
  // message will be pushed
  template <bool V_ = V, typename = ZuIfT<!V_>>
  bool pushFull(uint32_t head, uint32_t tail) {
    head &= ~Mask32();
    tail &= ~Mask32();
    if constexpr (MW || MR) { ZmRing_move_head(MsgSize); }
    return ZuUnlikely((head ^ tail) == Wrapped32());
  }
  template <bool V_ = V, typename = ZuIfT<V_>>
  bool pushFull(uint32_t head, uint32_t tail, unsigned size) {
    head &= ~Mask32();
    tail &= ~Mask32();
    if (ZuUnlikely(head == tail)) return false; // empty
    bool wrapped = (head ^ tail) &Wrapped32();
    head &= ~Wrapped32();
    tail &= ~Wrapped32();
    if (wrapped) head += this->size();
    if constexpr (MW || MR) size += 8;
    head += size;
    return (head - tail) >= this->size();
  }

  void writeAssert() {
    ZmAssert(ctrl());
    ZmAssert(m_flags &Write);
  }

  unsigned alignAssert(unsigned size) {
    size = align(size);
    ZmAssert(size <= this->size());
    return size;
  }

#define ZmRing_push_get_head_tail_swsr() \
    uint32_t head = this->head().load_(); \
    if (ZuUnlikely(head &EndOfFile32())) return nullptr; \
    uint32_t tail = this->tail() /* acquire */
#define ZmRing_push_get_head_tail_swmr() ZmRing_push_get_head_tail_swsr()
#define ZmRing_push_get_head_tail_mwsr() \
    uint32_t head = this->head().load_(); \
    if (ZuUnlikely(head &Locked32())) goto retry; \
    if (ZuUnlikely(head &EndOfFile32())) return nullptr; \
    uint32_t tail = this->tail() /* acquire */
#define ZmRing_push_get_head_tail_mwmr() ZmRing_push_get_head_tail_mwsr()

#define ZmRing_push_check_rdrMask() \
    if (!this->rdrMask().load_()) return nullptr /* no readers */

#define ZmRing_push_retry() \
    do { \
      ++m_full; \
      if (gc() > 0) goto retry; \
      if constexpr (!Wait) return nullptr; \
      if (ZuUnlikely(!params().ll)) { \
	if (this->tail().cmpXch(tail | Waiting32(), tail) != tail) \
          goto retry; \
	tail |= Waiting32(); \
	if (m_tailBlocker.wait( \
	      this->tail(), tail, params()) != Zu::OK) return nullptr; \
      } \
      goto retry; \
    } while (0)

// SWSR - push2() advances head, wakeReaders() updates head
#define ZmRing_move_head_swsr(msgSize) \
    ZmRing_move_head(msgSize);
// SWMR - push2() advances head, wakeReaders() does not update head
#define ZmRing_move_head_swmr(msgSize) \
    auto head_ = head; \
    ZmRing_move_head(msgSize); \
    *reinterpret_cast<uint64_t *>(&(data())[ \
	head & ~(Wrapped32() | Mask32())]) = 0; /* clear-ahead */ \
    this->head() = head /* release */
// MWSR | MWMR - push() advances head, updating it atomically
#define ZmRing_move_head_mwsr(msgSize) \
    auto head_ = head; \
    ZmRing_move_head(msgSize); \
    if (ZuUnlikely(this->head().cmpXch(head | Locked32(), head_) != head_)) \
      goto retry; \
    *reinterpret_cast<uint64_t *>(&(data())[ \
	head & ~(Wrapped32() | Mask32())]) = 0; /* clear-ahead */ \
    this->head() = head /* release */
#define ZmRing_move_head_mwmr(msgSize) ZmRing_move_head_mwsr(msgSize)

#define ZmRing_push_return_swsr() \
    return &(data())[head & ~(Wrapped32() | Mask32())]
#define ZmRing_push_return_swmr() \
    auto ptr = reinterpret_cast<uint64_t *>( \
	&(data())[head & ~(Wrapped32() | Mask32())]); \
    return &ptr[1]
#define ZmRing_push_return_mwsr() \
    auto ptr = reinterpret_cast<uint64_t *>( \
	&(data())[head_ & ~(Wrapped32() | Mask32())]); \
    return &ptr[1]
#define ZmRing_push_return_mwmr() ZmRing_push_return_mwsr()

#define ZmRing_push2_get_head() \
    uint32_t head = this->head().load_();
#define ZmRing_push2_ptr2head() \
    uint32_t head = uint32_t((reinterpret_cast<uint8_t *>(ptr) - 8) - data())

#define ZmRing_push2_update_stats(msgSize) \
    inCount().store_(inCount().load_() + 1); \
    inBytes().store_(inBytes().load_() + msgSize)

  // SWSR

  // !SWSR
  template <uint64_t Flags = 0, bool MW_ = MW, bool MR_ = MR,
    typename = void>
  void wakeReaders(uint32_t head) {
    if constexpr (!MW_ && !MR_) {
      head = (head & ~Waiting32()) | uint32_t(Flags>>32);
      if (ZuUnlikely(this->head().xch(head) &Waiting32()))
	m_headBlocker.wake(this->head());
    } else {
      wakeReaders_<Flags>(
	  reinterpret_cast<ZmAtomic<uint64_t> *>(
	      &(data())[head & ~(Wrapped32() | Mask32())]));
    }
  }
  // SWMR | MWMR
  template <uint64_t Flags = 0, bool MR_ = MR, typename = ZuIfT<MR_>>
  void wakeReaders_(ZmAtomic<uint64_t> *hdrPtr) {
    uint64_t rdrMask;
    if constexpr (Flags &EndOfFile())
      rdrMask = 0;
    else
      rdrMask = this->rdrMask().load_();
    if (ZuUnlikely((hdrPtr->xch(Flags | rdrMask)) &Waiting())) {
      auto &hdrPtr32 =
	reinterpret_cast<ZmAtomic<uint32_t> *>(hdrPtr)[Flags32Offset];
      m_headBlocker.wake(hdrPtr32);
    }
  }
  // MWSR
  template <uint64_t Flags = 0, bool MR_ = MR,
    uint64_t RdrMask_ = !(Flags &EndOfFile()),
    typename = ZuIfT<!MR_>>
  void wakeReaders_(ZmAtomic<uint64_t> *hdrPtr) {
    if (ZuUnlikely(hdrPtr->xch(Flags | RdrMask_) &Waiting())) {
      auto &hdrPtr32 =
	reinterpret_cast<ZmAtomic<uint32_t> *>(hdrPtr)[Flags32Offset];
      m_headBlocker.wake(hdrPtr32);
    }
  }

  // fixed-size SWSR

public:
  template <bool MW_ = MW, bool MR_ = MR, bool V_ = V,
    typename = ZuIfT<!MW_ && !V_>>
  void push2() {
    if constexpr (!MW_ && !MR_ && !V_) {
      writeAssert();
      ZmRing_push2_get_head();
      ZmRing_move_head_swsr(MsgSize);
      wakeReaders(head);
      ZmRing_push2_update_stats(MsgSize);
    } else {
      writeAssert();
      ZmRing_push2_get_head();
      ZmRing_move_head_swmr(MsgSize);
      wakeReaders(head_);
      ZmRing_push2_update_stats(MsgSize);
    }
  }
public:
  template <bool MW_ = MW, bool MR_ = MR, bool V_ = V,
    typename = ZuIfT<V_ && !MW_>>
  void push2(unsigned size) {
    if constexpr (!MW_ && !MR_ && V_) {
      writeAssert();
      size = alignAssert(size);
      ZmRing_push2_get_head();
      ZmRing_move_head_swsr(size);
      wakeReaders(head);
      ZmRing_push2_update_stats(size);
    } else {
      writeAssert();
      size = alignAssert(size);
      ZmRing_push2_get_head();
      ZmRing_move_head_swmr(size);
      wakeReaders(head_);
      ZmRing_push2_update_stats(size);
    }
  }
private:
  // fixed-size MWMR
  template <bool Wait, bool MW_ = MW, bool MR_ = MR, bool V_ = V,
    typename = ZuIfT<!V_>>
  void *push_() {
    writeAssert();
  retry:
    if constexpr (!MW_ && !MR_ && !V_) {
      ZmRing_push_get_head_tail_swsr();
      if (pushFull(head, tail)) ZmRing_push_retry();
      ZmRing_push_return_swsr();
    } else if constexpr (!MW_ && MR_ && !V_) {
      ZmRing_push_check_rdrMask();
      ZmRing_push_get_head_tail_swmr();
      if (pushFull(head, tail)) ZmRing_push_retry();
      ZmRing_push_return_swmr();
    } else if constexpr (MW_ && !MR_ && !V_) {
      ZmRing_push_get_head_tail_mwsr();
      if (pushFull(head, tail)) ZmRing_push_retry();
      ZmRing_move_head_mwsr(MsgSize);
      ZmRing_push_return_mwsr();
    } else {
      ZmRing_push_check_rdrMask();
      ZmRing_push_get_head_tail_mwmr();
      if (pushFull(head, tail)) ZmRing_push_retry();
      ZmRing_move_head_mwmr(MsgSize);
      ZmRing_push_return_mwmr();
    }
  }
public:
  template <bool MW_ = MW, bool MR_ = MR, bool V_ = V,
    typename = ZuIfT<MW_ && !V_>>
  void push2(void *ptr) {
    writeAssert();
    ZmRing_push2_ptr2head();
    wakeReaders(head);
    ZmRing_push2_update_stats(MsgSize);
  }
private:
  // variable-size MWMR - push2() is same as fixed-size
  template <bool Wait, bool MW_ = MW, bool MR_ = MR, bool V_ = V,
    typename = ZuIfT<V_>>
  void *push_(unsigned size) {
    writeAssert();
    size = align(size);
    if (size > this->size()) return nullptr;
  retry:
    if constexpr (!MW_ && !MR_ && V_) {
      ZmRing_push_get_head_tail_swsr();
      if (pushFull(head, tail, size)) ZmRing_push_retry();
      ZmRing_push_return_swsr();
    } else if constexpr (!MW_ && MR_ && V_) {
      ZmRing_push_check_rdrMask();
      ZmRing_push_get_head_tail_swmr();
      if (pushFull(head, tail, size)) ZmRing_push_retry();
      ZmRing_push_return_swmr();
    } else if constexpr (MW_ && !MR_ && V_) {
      ZmRing_push_get_head_tail_mwsr();
      if (pushFull(head, tail, size)) ZmRing_push_retry();
      ZmRing_move_head_mwsr(size);
      ZmRing_push_return_mwsr();
    } else {
      ZmRing_push_check_rdrMask();
      ZmRing_push_get_head_tail_mwmr();
      if (pushFull(head, tail, size)) ZmRing_push_retry();
      ZmRing_move_head_mwmr(size);
      ZmRing_push_return_mwmr();
    }
  }
public:
  template <bool MW_ = MW, bool MR_ = MR, bool V_ = V,
    typename = ZuIfT<MW_ && V_>>
  void push2(void *ptr, unsigned size) {
    writeAssert();
    size = alignAssert(size);
    ZmRing_push2_ptr2head();
    wakeReaders(head);
    ZmRing_push2_update_stats(size);
  }

  // EOF signalling is complex:
  // - for SWSR, readers wait on the head, which is signalled with 32bit flags
  // - in all other cases, readers wait on the header
  // -  wakeReaders() updates either the head, or the header, accordingly
  //    ... however readStatus() only examines the head, not the header, and
  //    needs to determine EOF, so eof() needs to ensure that both head _and_
  //    header are updated in non-SWSR cases

  template <bool MW_ = MW, bool MR_ = MR,
    typename = void>
  void eof(bool eof = true) {
    if constexpr (!MW_ && !MR_) {
	  // SWSR
      writeAssert();
      uint32_t head = this->head().load_();
      if (eof)
	wakeReaders<EndOfFile()>(head); // updates head
      else
	this->head() = head & ~EndOfFile32();
    } else if constexpr (MW_) {
		  // MWSR | MWMR
      writeAssert();
  retry:
      uint32_t head = this->head().load_();
      if (head &Locked32()) goto retry;
      if (eof) {
	if (this->head().cmpXch(
	      head | Locked32() | EndOfFile32(), head) != head)
	  goto retry;
	wakeReaders<EndOfFile()>(head); // updates hdr
	this->head() = head | EndOfFile32();
      } else {
	if (this->head().cmpXch(
	      (head | Locked32()) & ~EndOfFile32(), head) != head)
	  goto retry;
	*reinterpret_cast<uint64_t *>(&(data())[
	    head & ~(Wrapped32() | Mask32())]) &= ~EndOfFile();
	this->head() = head & ~EndOfFile32();
      }
    } else {
	  // SWMR
      writeAssert();
      uint32_t head = this->head().load_();
      if (eof) {
	this->head() = (head |= EndOfFile32());
	wakeReaders<EndOfFile()>(head); // updates hdr
      } else {
	this->head() = head & ~EndOfFile32();
	*reinterpret_cast<uint64_t *>(&(data())[
	    head & ~(Wrapped32() | Mask32())]) &= ~EndOfFile();
      }
    }
  }

private:
  int writeStatus_() const {
    uint32_t head = this->head().load_();
    if (ZuUnlikely(head &EndOfFile32())) return Zu::EndOfFile;
    head &= ~(Wrapped32() | Mask32());
    uint32_t tail = this->tail() & ~(Wrapped32() | Mask32());
    if (head < tail) return tail - head;
    return size() - (head - tail);
  }
public:
  // can be called by writers after push() returns 0
  // - returns Error (not open), NotReady (no readers), EndOfFile,
  //   or amount of space remaining in ring buffer (>= 0)
  // SR

  // MR
  template <bool MR_ = MR, typename = void>
  int writeStatus() const {
    if constexpr (!MR_) {
      ZmAssert(m_flags &Write);
      if (ZuUnlikely(closed())) return Zu::IOError;
      return writeStatus_();
    } else {
      ZmAssert(m_flags &Write);
      if (ZuUnlikely(closed())) return Zu::IOError;
      if (ZuUnlikely(!rdrMask())) return Zu::NotReady;
      return writeStatus_();
    }
  }

  // reader

public:
  ZuInline T *shift() { return shift_<1>(); }
  ZuInline T *tryShift() { return shift_<0>(); }

private:

  template <bool MR_ = MR, typename = void>
  void readAssert() {
    if constexpr (!MR_) {
      ZmAssert(ctrl());
      ZmAssert(m_flags &Read);
    } else {
      readAssert<false>();
      ZmAssert(rdrID() >= 0);
    }
  }

#define ZmRing_shift_get_tail_() \
    this->tail().load_() & ~Mask32()

#define ZmRing_shift_get_tail() \
    uint32_t tail = ZmRing_shift_get_tail_()
#define ZmRing_shift_get_tail_mr() \
    uint32_t tail = rdrTail()

#define ZmRing_shift_get_head() \
    uint32_t head = this->head(); /* acquire */ \
    if constexpr (MW) if (head &Locked32()) goto retry; \
    /**/ZmRing_bp(this, shift1)

#define ZmRing_shift_get_hdr() \
    auto hdrPtr = reinterpret_cast<ZmAtomic<uint64_t> *>( \
	  &(data())[tail & ~Wrapped32()]); \
    uint64_t hdr = *hdrPtr; /* acquire */ \
    /**/ZmRing_bp(this, shift1)

#define ZmRing_shift_empty_swsr() (tail == (head & ~Mask32()))
#define ZmRing_shift_empty_swmr() (!(hdr & ~Mask()))
#define ZmRing_shift_empty_mwsr() ZmRing_shift_empty_swmr()
#define ZmRing_shift_empty_mwmr() ZmRing_shift_empty_swmr()

#define ZmRing_shift_retry_swsr() \
    do { \
      if (ZuUnlikely(head &EndOfFile32())) return nullptr; \
      if constexpr (!Wait) return nullptr; \
      if (ZuUnlikely(!params().ll)) { \
	if (this->head().cmpXch(head | Waiting32(), head) != head) \
          goto retry; \
	head |= Waiting32(); \
	if (m_headBlocker.wait( \
	      this->head(), head, params()) != Zu::OK) return nullptr; \
      } \
      goto retry; \
    } while (0)
#define ZmRing_shift_retry_swmr() \
    do { \
      if (ZuUnlikely(hdr &EndOfFile())) return nullptr; \
      if constexpr (!Wait) return nullptr; \
      if (ZuUnlikely(!params().ll)) { \
	if (hdrPtr->cmpXch(hdr | Waiting(), hdr) != hdr) goto retry; \
	hdr |= Waiting(); \
	auto &hdr32 = \
	  reinterpret_cast<ZmAtomic<uint32_t> *>(hdrPtr)[Flags32Offset]; \
	if (m_headBlocker.wait( \
	      hdr32, hdr>>32, params()) != Zu::OK) return nullptr; \
      } \
      goto retry; \
    } while (0)
#define ZmRing_shift_retry_mwsr() ZmRing_shift_retry_swmr()
#define ZmRing_shift_retry_mwmr() ZmRing_shift_retry_swmr()

#define ZmRing_move_tail_(msgSize) \
    tail += msgSize; \
    if (ZuUnlikely((tail & ~Wrapped32()) >= this->size())) \
      tail = (tail ^ Wrapped32()) - this->size()

#define ZmRing_shift_return_swsr() \
    return reinterpret_cast<T *>(&(data())[tail & ~Wrapped32()])
#define ZmRing_shift_return_mwsr() \
    return reinterpret_cast<T *>(&hdrPtr[1])
#define ZmRing_shift_return_swmr() \
    ZmRing_shift_return_mwsr();
#define ZmRing_shift_return_mwmr() ZmRing_shift_return_swmr()

#define ZmRing_move_tail_swsr(msgSize) \
    ZmRing_move_tail_(msgSize)
#define ZmRing_move_tail_swmr(msgSize) \
    auto tail_ = tail; \
    ZmRing_move_tail_(msgSize); \
    rdrTail(tail); \
    if ((*reinterpret_cast<ZmAtomic<uint64_t> *>( \
	&(data())[tail_ & ~Wrapped32()]) &= \
	  ~(uint64_t(1)<<rdrID())) &RdrMask()) \
      return
#define ZmRing_move_tail_mwsr(msgSize) \
    *reinterpret_cast<ZmAtomic<uint64_t> *>( \
	&(data())[tail & ~Wrapped32()]) = 0; \
    ZmRing_move_tail_(msgSize)
#define ZmRing_move_tail_mwmr(msgSize) ZmRing_move_tail_swmr(msgSize)

#define ZmRing_shift2_update_stats(msgSize) \
    this->outCount().store_(this->outCount().load_() + 1); \
    this->outBytes().store_(this->outBytes().load_() + msgSize)

  void wakeWriters(uint32_t tail) {
    tail &= ~Waiting32();
    if (ZuUnlikely(this->tail().xch(tail) &Waiting32()))
      m_tailBlocker.wake(this->tail());
  }

  // SWSR

private:
  // MWMR
  template <bool Wait, bool MW_ = MW, bool MR_ = MR,
    typename = void>
  T *shift_() {
    readAssert();
    uint32_t tail;
    if constexpr (MR_) tail = rdrTail();
    else tail = ZmRing_shift_get_tail_();
  retry:
    if constexpr (!MW_ && !MR_) {
      ZmRing_shift_get_head();
      if (ZmRing_shift_empty_swsr()) ZmRing_shift_retry_swsr();
      ZmRing_shift_return_swsr();
    } else if constexpr (!MW_ && MR_) {
      ZmRing_shift_get_hdr();
      if (ZmRing_shift_empty_swmr()) ZmRing_shift_retry_swmr();
      ZmRing_shift_return_swmr();
    } else if constexpr (MW_ && !MR_) {
      ZmRing_shift_get_hdr();
      if (ZmRing_shift_empty_mwsr()) ZmRing_shift_retry_mwsr();
      ZmRing_shift_return_mwsr();
    } else {
      ZmRing_shift_get_hdr();
      if (ZmRing_shift_empty_mwmr()) ZmRing_shift_retry_mwmr();
      ZmRing_shift_return_mwmr();
    }
  }
public:
  // fixed-size MWMR
  template <bool MW_ = MW, bool MR_ = MR, bool V_ = V,
    typename = ZuIfT<!V_>>
  void shift2() {
    if constexpr (!MW_ && !MR_ && !V_) {
      readAssert();
      ZmRing_shift_get_tail();
      ZmRing_move_tail_swsr(MsgSize);
      wakeWriters(tail);
      ZmRing_shift2_update_stats(MsgSize);
    } else if constexpr (!MW_ && MR_ && !V_) {
      readAssert();
      ZmRing_shift_get_tail_mr();
      ZmRing_move_tail_swmr(MsgSize);
      wakeWriters(tail);
      ZmRing_shift2_update_stats(MsgSize);
    } else if constexpr (MW_ && !MR_ && !V_) {
      readAssert();
      ZmRing_shift_get_tail();
      ZmRing_move_tail_mwsr(MsgSize);
      wakeWriters(tail);
      ZmRing_shift2_update_stats(MsgSize);
    } else {
      readAssert();
      ZmRing_shift_get_tail_mr();
      ZmRing_move_tail_mwmr(MsgSize);
      wakeWriters(tail);
      ZmRing_shift2_update_stats(MsgSize);
    }
  }
  // variable-size MWMR
  template <bool MW_ = MW, bool MR_ = MR, bool V_ = V,
    typename = ZuIfT<V_>>
  void shift2(unsigned size) {
    if constexpr (!MW_ && !MR_ && V_) {
      readAssert();
      size = alignAssert(size);
      ZmRing_shift_get_tail();
      ZmRing_move_tail_swsr(size);
      wakeWriters(tail);
      ZmRing_shift2_update_stats(size);
    } else if constexpr (!MW_ && MR_ && V_) {
      readAssert();
      size = alignAssert(size);
      ZmRing_shift_get_tail_mr();
      ZmRing_move_tail_swmr(size);
      wakeWriters(tail);
      ZmRing_shift2_update_stats(size);
    } else if constexpr (MW_ && !MR_ && V_) {
      readAssert();
      size = alignAssert(size);
      ZmRing_shift_get_tail();
      ZmRing_move_tail_mwsr(size);
      wakeWriters(tail);
      ZmRing_shift2_update_stats(size);
    } else {
      readAssert();
      size = alignAssert(size);
      ZmRing_shift_get_tail_mr();
      ZmRing_move_tail_mwmr(size);
      wakeWriters(tail);
      ZmRing_shift2_update_stats(size);
    }
  }

private:
  int readStatus_(uint32_t tail) const {
    uint32_t head = this->head(); /* acquire */
    bool eof = head &EndOfFile32();
    head &= ~Mask32();
    if ((head ^ tail) == Wrapped32()) return size();
    head &= ~Wrapped32();
    tail &= ~Wrapped32();
    if (head >= tail) {
      if (head > tail) return head - tail;
      if (ZuUnlikely(eof)) return Zu::EndOfFile;
      return 0;
    }
    return size() - (tail - head);
  }
public:
  // can be called by a reader after shift() returns 0; returns
  // EndOfFile, or amount of data remaining in ring buffer (>= 0)
  // SR

  // MR
  template <bool MR_ = MR, typename = void>
  int readStatus() const {
    if constexpr (!MR_) {
      ZmAssert(m_flags &Read);
      if (ZuUnlikely(closed())) return Zu::IOError;
      return readStatus_(this->tail().load_() & ~Mask32());
    } else {
      ZmAssert(m_flags &Read);
      if (ZuUnlikely(closed())) return Zu::IOError;
      return readStatus_(rdrTail());
    }
  }

  unsigned count_() const {
    int i = readStatus();
    if (i < 0) return 0;
    if constexpr (!MsgSize)
      return i;
    else
      return i / MsgSize;
  }

  void stats(
      uint64_t &inCount, uint64_t &inBytes,
      uint64_t &outCount, uint64_t &outBytes) const {
    ZmAssert(ctrl());

    inCount = this->inCount().load_();
    inBytes = this->inBytes().load_();
    outCount = this->outCount().load_();
    outBytes = this->outBytes().load_();
  }

private:
  ParamData		m_params;
  Blocker		m_headBlocker, m_tailBlocker;
  uint32_t		m_flags = 0;
  uint32_t		m_size = 0;
  uint32_t		m_full = 0;

#ifdef ZmRing_FUNCTEST
public:
  ZmRing_Breakpoint	bp_attach1;
  ZmRing_Breakpoint	bp_attach2;
  ZmRing_Breakpoint	bp_attach3;
  ZmRing_Breakpoint	bp_attach4;
  ZmRing_Breakpoint	bp_detach1;
  ZmRing_Breakpoint	bp_detach2;
  ZmRing_Breakpoint	bp_detach3;
  ZmRing_Breakpoint	bp_shift1;
#endif
};

template <typename Ring, bool MW>
inline bool RingExt<Ring, MW, true>::open_()
{
  if (ring()->flags() & Ring::Read) {
    uint32_t rdrCount;
    do {
      rdrCount = ring()->rdrCount();
      if (rdrCount >= MaxRdrs) return false;
    } while (ring()->rdrCount().cmpXch(rdrCount + 1, rdrCount) != rdrCount);
  }
  return true;
}

template <typename Ring, bool MW>
inline void RingExt<Ring, MW, true>::close_()
{
  if (ring()->flags() & Ring::Read) {
    if (rdrID() >= 0) detach();
    --ring()->rdrCount();
  }
}

template <typename Ring, bool MW, bool MR>
inline int RingExt<Ring, MW, MR>::attach()	// unused
{
  /**/ZmRing_bp(ring(), attach1);
  /**/ZmRing_bp(ring(), attach2);
  /**/ZmRing_bp(ring(), attach3);
  /**/ZmRing_bp(ring(), attach4);
  return Zu::OK;
}

template <typename Ring, bool MW>
inline int RingExt<Ring, MW, true>::attach()	// MR attach
{
  enum { Read = Ring::Read };

  ZmAssert(ring()->ctrl());
  ZmAssert(ring()->flags() &Read);

  if (rdrID() >= 0) return Zu::OK;

  // allocate an ID for this reader
  {
    uint64_t attMask;
    unsigned id;
    do {
      attMask = ring()->attMask().load_();
      for (id = 0; id < MaxRdrs; id++)
	if (!(attMask & (uint64_t(1)<<id))) break;
      if (id == MaxRdrs) return Zu::IOError;
    } while (
      ring()->attMask().cmpXch(
	attMask | (uint64_t(1)<<id), attMask) != attMask);
    rdrID(id);
  }

  ++(ring()->attSeqNo());

  ring()->attached(rdrID());

  /**/ZmRing_bp(ring(), attach1);

  auto data = ring()->data();
  auto size = ring()->size();

  // skip any trailing messages not intended for us, since other readers
  // may be concurrently advancing the ring's tail; this must be
  // re-attempted as long as the head keeps moving and the writer remains
  // unaware of our attach
  uint32_t tail = ring()->tail().load_() & ~Mask32();
  uint32_t head = ring()->head() & ~Mask32(); // acquire
  uint32_t head_;
  /**/ZmRing_bp(ring(), attach2);
  // notifies the writer about an attach
  ring()->rdrMask() |= (uint64_t(1)<<rdrID());
  /**/ZmRing_bp(ring(), attach3);

  do {
    while (tail != head) {
      auto hdrPtr = reinterpret_cast<ZmAtomic<uint64_t> *>(
	  &data[tail & ~Wrapped32()]);
      if (*hdrPtr & (uint64_t(1)<<rdrID())) goto done; // writer aware
      tail += ring()->align(Ring::SizeAxor(&hdrPtr[1]));
      if ((tail & ~Wrapped32()) >= size) tail = (tail ^ Wrapped32()) - size;
    }
    head_ = head;
    head = ring()->head() & ~Mask32(); // acquire
  } while (head != head_);
done:
  /**/ZmRing_bp(ring(), attach4);

  rdrTail(tail);

  ++(ring()->attSeqNo());

  return Zu::OK;
}

template <typename Ring, bool MW, bool MR>
inline void RingExt<Ring, MW, MR>::detach()	// unused
{
  /**/ZmRing_bp(ring(), detach1);
  /**/ZmRing_bp(ring(), detach2);
  /**/ZmRing_bp(ring(), detach3);
}

template <typename Ring, bool MW>
inline void RingExt<Ring, MW, true>::detach()	// MR detach
{
  enum { Read = Ring::Read };

  ZmAssert(ring()->ctrl());
  ZmAssert(ring()->flags() &Read);

  if (rdrID() < 0) return;

  ++(ring()->attSeqNo());

  // notifies the writer about a detach
  ring()->rdrMask() &= ~(uint64_t(1)<<rdrID());
  /**/ZmRing_bp(ring(), detach1);

  auto data = ring()->data();
  auto size = ring()->size();

  // drain any trailing messages that are waiting to be read by us,
  // advancing ring's tail as needed; this must be
  // re-attempted as long as the head keeps moving and the writer remains
  // unaware of our detach
  uint32_t tail = rdrTail();
  /**/ZmRing_bp(ring(), detach2);
  uint32_t head = ring()->head() & ~Mask32(); // acquire
  uint32_t head_;

  do {
    while (tail != head) {
      auto hdrPtr = reinterpret_cast<ZmAtomic<uint64_t> *>(
	  &data[tail & ~Wrapped32()]);
      if (!(*hdrPtr & (uint64_t(1)<<rdrID()))) goto done; // writer aware
      tail += ring()->align(Ring::SizeAxor(&hdrPtr[1]));
      if ((tail & ~Wrapped32()) >= size) tail = (tail ^ Wrapped32()) - size;
      if (*hdrPtr &= ~(uint64_t(1)<<rdrID())) continue;
      ring()->wakeWriters(tail);
    }
    head_ = head;
    head = ring()->head() & ~Mask32(); // acquire
  } while (head != head_);
done:
  /**/ZmRing_bp(ring(), detach3);

  rdrTail(tail);

  // release ID for re-use by future attach

  ring()->detached(rdrID());

  ++(ring()->attSeqNo());

  ring()->attMask() &= ~(uint64_t(1)<<rdrID());
  rdrID(-1);
}

} // ZmRing_

using ZmRingParams = ZmRing_::Params;

template <typename NTP = ZmRing_::Defaults>
using ZmRing = ZmRing_::Ring<NTP>;

#endif /* ZmRing_HH */
