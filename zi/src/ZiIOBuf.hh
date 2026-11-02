//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// IO buffer (packet buffer)
// IOBufAlloc__ embeds ordinary payload storage in a ZmNHeap descriptor.
// IOBufURing references OS-mapped ring storage without a builtin payload.
// ZmVHeap supplies jumbo storage and unregistered URing allocation fallbacks.

#ifndef ZiIOBuf_HH
#define ZiIOBuf_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#ifndef _WIN32
#include <linux/io_uring.h>
#endif

#include <zlib/ZuPrint.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuSpan.hh>

#include <zlib/ZmPolymorph.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmRing.hh>
#include <zlib/ZmVHeap.hh>

#include <zlib/ZiPlatform.hh>

#include <zlib/ZtEnum.hh>

// TCP over Ethernet maximum payload is 1460 (without Jumbo frames)
// - ... but the vast majority of small messages are under 1k
//   and ZmVHeap is organized by powers of 2
#ifndef ZiIOBuf_DefltSize
#define ZiIOBuf_DefltSize 980
#endif

// Default ring slots/SQ capacity; bounds pinned storage and is build-overridable.
#ifndef ZiIOBuf_DefltRingSize
#define ZiIOBuf_DefltRingSize 64
#endif

// default hard-coded sane upper limit on an individual buffer size
#ifndef ZiIOBuf_DefltMaxSize
#define ZiIOBuf_DefltMaxSize (100<<20) // 100Mb
#endif

// 64-byte aligned for SSE/AVX encryption, e.g. Fusion AES-GCM
#ifndef ZiIOBuf_Align
#define ZiIOBuf_Align 64
#endif

struct ZiIOBuf_HeapID : public ZuStringT<"ZiIOBuf"> { };

namespace Zi {

ZuDerive(VHeap, (ZmVHeap_<ZiIOBuf_HeapID, ZmVHeap_DefltMin, ZmVHeap_DefltMax, ZiIOBuf_Align>));

struct IOBuf;
class IOBufRing;

ZtEnumNS(ZiAPI, IOBufType, int8_t, App, RxUDP, TxUDP, RxTCP, TxTCP, RxTLS, TxTLS);

struct IOBufAllocator : public ZmPolymorph {
  friend inline ZuStringT<"Zi.IOBufAllocator"> ZmNHeapID(IOBufAllocator *);

  virtual IOBufType::T type() = 0;
  virtual ZmRef<IOBuf> alloc(void *owner = nullptr, uint32_t length = 0) = 0;
  virtual IOBufRing *ring() const { return nullptr; }
};

#ifndef _WIN32
class URing;
// Views of the kernel-owned queues, bound once at initialization.
struct IOBufSQ {
  ZmAtomic<unsigned> *head;
  ZmAtomic<unsigned> *tail;
  unsigned	*array;
  io_uring_sqe	*sqes;
  unsigned	mask;
  ZmAtomic<unsigned> *flags;
};
struct IOBufCQ {
  ZmAtomic<unsigned> *head;
  ZmAtomic<unsigned> *tail;
  io_uring_cqe	*cqes;
  unsigned	mask;
};

// Ring mappings and registered payload storage; SQ/CQ are owned by the I/O shard.
class ZiAPI IOBufRing : public IOBufAllocator {
public:
  IOBufRing() = default;
  ~IOBufRing() override;
  IOBufRing(const IOBufRing &) = delete;
  IOBufRing &operator =(const IOBufRing &) = delete;

  bool init(unsigned ringSize);
  bool init(unsigned ringSize, unsigned bufSize);
  void final();

  // Ring-only instances have no buffer descriptor policy. Typed network
  // allocators override these; the probe and ordinary-send ring use no pool.
  IOBufType::T type() override { return IOBufType::App; }
  ZmRef<IOBuf> alloc(void *owner = nullptr, uint32_t length = 0) override;
  IOBufRing *ring() const override { return const_cast<IOBufRing *>(this); }

  uint8_t *allocSlot();
  void free(uint8_t *data);

  int fd() const { return m_fd; }
  const IOBufSQ &sq() const { return m_sq; }
  const IOBufCQ &cq() const { return m_cq; }
  unsigned bufSize() const { return m_bufSize; }
  unsigned bufCount() const { return m_bufCount; }
  bool available() { return m_free.tryShift(); }
  int returnFD() const { return m_returnFD; }
  void wait();

  URing *io() const { return m_io; }
  void io(URing *p, void (*destroy)(URing *)) {
    m_io = p; m_destroyIO = destroy;
  }

protected:
  bool init_(unsigned ringSize, unsigned bufSize);
  unsigned index(const void *p) const {
    return (uintptr_t(p) - uintptr_t(m_bufs))/m_bufSize;
  }
  bool initReturn();

private:
  IOBufSQ			m_sq{};
  IOBufCQ			m_cq{};
  void				*m_map = nullptr;
  using Free = ZmRing<ZmRingT<uint8_t *, ZmRingMW<true>>>;
  Free				m_free;
  uint8_t			*m_bufs = nullptr;
  unsigned			m_bufSize = 0;
  unsigned			m_bufCount = 0;
  int				m_fd = -1;
  int				m_returnFD = -1;
  ZmAtomic<unsigned>		m_waiting{0};
  URing				*m_io = nullptr;
  void				(*m_destroyIO)(URing *) = nullptr;
};

class ZiAPI IOBufRxRing : public IOBufRing {
public:
  using IOBufRing::index;

  ~IOBufRxRing();
  bool init(unsigned ringSize, unsigned bufSize);
  void final();

  io_uring_buf_ring *bufRing() const { return m_bufRing; }
  unsigned ringSize() const { return m_ringSize; }

private:
  io_uring_buf_ring	*m_bufRing = nullptr;
  unsigned		m_ringSize = 0;
};
#endif

struct IOBuf : private VHeap, public ZmPolymorph {
  mutable void	*owner = nullptr;
  uintptr_t	data__ = 0;
  uint32_t	size = 0;
  uint32_t	length = 0;
  uint32_t	skip = 0;
  bool		failed_ = false;

  // 64bit pointer-packing - uses bits 63 and 62
  ZuAssert(sizeof(uintptr_t) == 8);
  static constexpr uintptr_t Jumbo = (uintptr_t(1)<<63);
  static constexpr uintptr_t URing = (uintptr_t(1)<<62);

private:
  using VHeap::valloc;
  using VHeap::vfree;

protected:
  virtual void releaseURing_() { }
  void release_() {
    if (data__ & URing) releaseURing_();
    else if (data__ & Jumbo) vfree(data_());
  }

  IOBuf(uint8_t *data_, uint32_t size_, void *owner_, uint32_t length_) :
    owner{owner_}, data__{reinterpret_cast<uintptr_t>(data_)},
    size{size_}, length{length_} { }

public:
  virtual ~IOBuf() { if (ZuUnlikely(data__ & Jumbo)) vfree(data_()); }
  virtual IOBufRing *ring() const { return nullptr; }

  IOBuf(const IOBuf &buf) = delete;
  IOBuf &operator =(const IOBuf &buf) = delete;
  IOBuf(IOBuf &&buf) = delete;
  IOBuf &operator =(IOBuf &&buf) = delete;

  uint8_t *data_() {
    return reinterpret_cast<uint8_t *>(data__ & ~(Jumbo | URing));
  }
  const uint8_t *data_() const {
    return const_cast<IOBuf *>(this)->data_();
  }

  bool jumbo() const { return data__ & Jumbo; }
  bool uring() const { return data__ & URing; }

  // Boolean conversion describes readable bytes; output status is separate.
  bool failed() const { return failed_; }
  bool operator !() const { return !length; }

  uint8_t *data() { return data_() + skip; }
  const uint8_t *data() const { return data_() + skip; }

  uint8_t *end() { return data_() + skip + length; }
  const uint8_t *end() const { return data_() + skip + length; }

  unsigned avail() const { return size - (skip + length); }

  uint8_t *alloc(unsigned newSize) {
    if (ZuLikely(newSize <= size)) return data();
    auto jumbo = static_cast<uint8_t *>(valloc(newSize));
    size = newSize;
    release_();
    data__ = reinterpret_cast<uintptr_t>(jumbo) | Jumbo;
    return jumbo;
  }

  void free(uint8_t *ptr) {
    if (ptr == data_()) {
      if (!(data__ & (Jumbo | URing))) return;
      release_();
      data__ = 0;
      length = size = 0;
      return;
    }
    vfree(ptr);
  }

  void clear() {
    length = 0;
    skip = 0;
    failed_ = false; // begin a new output operation
  }

  template <typename T>
  const T *ptr() const { return reinterpret_cast<const T *>(data()); }
  template <typename T>
  T *ptr() { return reinterpret_cast<T *>(data()); }

  template <typename T>
  const T &as() const { return *ptr<T>(); }
  template <typename T>
  T &as() { return *ptr<T>(); }

  // direct buffer access
  auto span() { return ZuSpan(data(), length); }
  auto cspan() const { return ZuSpan(data(), length); }

  auto span(unsigned offset) {
    if (ZuUnlikely(offset > length)) offset = length;
    return ZuSpan(data() + offset, length - offset);
  }
  auto cspan(unsigned offset) const {
    if (ZuUnlikely(offset > length)) offset = length;
    return ZuSpan(data() + offset, length - offset);
  }

  // advance/rewind buffer
  void advance(unsigned n) {
    if (ZuUnlikely(!n)) return;
    if (ZuUnlikely(n > length)) n = length;
    skip += n, length -= n;
  }
  void rewind(unsigned n) { // reverses advance - use prepend to grow buffer
    if (ZuUnlikely(!n)) return;
    if (ZuUnlikely(n > skip)) n = skip;
    skip -= n, length += n;
  }

  // reallocate (while building buffer), preserving head and tail bytes
  template <auto Grow = ZmGrow>
  uint8_t *realloc(
      unsigned oldSize, unsigned newSize,
      unsigned head, unsigned tail) {
    auto old = data();
    unsigned newSize_ = newSize + skip;
    if (ZuLikely(newSize_ <= size)) {
      if (tail) memmove(old + newSize - tail, old + oldSize - tail, tail);
      size = newSize_;
      return old;
    }
    newSize_ = Grow(size, newSize_);
    auto jumbo = reinterpret_cast<uint8_t *>(valloc(newSize_));
    if (head) memcpy(jumbo + skip, old, head);
    if (tail)
      memcpy(jumbo + newSize_ - tail, old + oldSize - tail, tail);
    size = newSize_;
    release_();
    data__ = reinterpret_cast<uintptr_t>(jumbo) | Jumbo;
    return jumbo + skip;
  }

  // ensure at least newSize bytes in buffer, preserving any existing data
  // - null means failed output status, size overflow, or insufficient Grow
  // - allocation failure is handled by ZmHeapFail
  template <auto Grow = ZmGrow>
  uint8_t *ensure(unsigned newSize) {
    if (ZuUnlikely(failed_ || newSize > UINT32_MAX - skip)) {
      failed_ = true;
      return nullptr;
    }
    newSize += skip;
    if (ZuLikely(newSize <= size)) return data();
    unsigned grown = Grow(size, newSize);
    if (ZuUnlikely(grown < newSize)) { failed_ = true; return nullptr; }
    newSize = grown;
    auto old = data_();
    auto jumbo = reinterpret_cast<uint8_t *>(valloc(newSize));
    if (length) memcpy(jumbo + skip, old + skip, length);
    size = newSize;
    release_();
    data__ = reinterpret_cast<uintptr_t>(jumbo) | Jumbo;
    return jumbo + skip;
  }

  // prepend to buffer (e.g. for a protocol header)
  template <auto Grow = ZmGrow>
  uint8_t *prepend(unsigned length_) {
    // Aligned arenas and sliced spans can leave unused trailing storage.
    ZmAssert(uint64_t(skip) + length <= size);
    if (ZuLikely(skip >= length_)) {
      skip -= length_;
      length += length_;
      return data();
    }
    auto old = data_();
    auto newSize = length + length_;
    if (ZuLikely(newSize <= size)) {
      auto newSkip = size - length;
      if (length) memmove(old + newSkip, old + skip, length);
      skip = newSkip - length_;
      length += length_;
      return data();
    }
    newSize = Grow(size, newSize);
    auto jumbo = reinterpret_cast<uint8_t *>(valloc(newSize));
    auto newSkip = newSize - length;
    if (length) memcpy(jumbo + newSkip, old + skip, length);
    size = newSize;
    skip = newSkip - length_;
    length += length_;
    release_();
    data__ = reinterpret_cast<uintptr_t>(jumbo) | Jumbo;
    return data();
  }

  bool append(const uint8_t *data, unsigned length_) {
    if (ZuUnlikely(failed_)) return false;
    if (!length_) return true;
    if (ZuUnlikely(length_ > UINT32_MAX - length)) {
      failed_ = true;
      return false;
    }
    unsigned total = length + length_;
    auto ptr = ensure(total);
    if (ZuUnlikely(!ptr)) return false;
    memcpy(ptr + length, data, length_);
    length = total;
    return true;
  }
  bool append(ZuBSpan data) { return append(data.data(), data.length()); }

private:
  template <typename U, typename R = void>
  using MatchPDelegate = ZuIfT<ZuPrint<U>::Delegate, R>;
  template <typename U, typename R = void>
  using MatchPBuffer = ZuIfT<ZuPrint<U>::Buffer, R>;

  template <typename P, typename = MatchPDelegate<P>>
  void append(P &&p) {
    if (ZuUnlikely(failed_)) return;
    ZuPrint<P>::print(*this, ZuFwd<P>(p));
  }
  template <typename P, typename = MatchPBuffer<P>>
  void append(const P &p) {
    if (ZuUnlikely(failed_)) return;
    unsigned length_ = ZuPrint<P>::length(p);
    if (!length_) return;
    if (ZuUnlikely(length_ > UINT32_MAX - length)) {
      failed_ = true;
      return;
    }
    auto ptr = ensure(length + length_);
    if (ZuUnlikely(!ptr)) return;
    length += ZuPrint<P>::print(
	reinterpret_cast<char *>(ptr + length), length_, p);
  }

  template <typename U, typename R = void>
  using MatchChar = ZuSame<U, char, R>;

public:
  // ZuBSpan will match any string/span type
  IOBuf &operator <<(ZuBSpan buf) {
    append(buf);
    return *this;
  }
  template <typename C, typename = MatchChar<C>>
  IOBuf & operator <<(C c) {
    return *this << ZuSpan{&c, 1};
  }

  template <typename R,
    typename = ZuIfT<
      (ZuTraits<R>::IsPrimitive && ZuTraits<R>::IsReal && !ZuIsSame<R, char>{}) ||
      (ZuPrint<R>::OK && !ZuPrint<R>::String)>>
  IOBuf & operator <<(const R &r) {
    if constexpr (ZuTraits<R>::IsPrimitive && ZuTraits<R>::IsReal && !ZuIsSame<R, char>{}) {
      append(ZuBoxed(r));
      return *this;
    } else {
      append(r);
      return *this;
    }
  }

  struct Traits : public ZuBaseTraits<IOBuf> {
    using Elem = char;
    enum { IsCString = 0, IsString = 1, IsWString = 0 };
    static char *data(IOBuf &buf) {
      return reinterpret_cast<char *>(buf.data());
    }
    static const char *data(const IOBuf &buf) {
      return reinterpret_cast<const char *>(buf.data());
    }
    static unsigned length(const IOBuf &buf) {
      return buf.length;
    }
  };
  friend Traits ZuTraitsType(IOBuf *);
};

// the Base parameter permits Ztls, Zdb, etc. to inject their own
// buffer type into the hierarchy

template <
  typename Base, unsigned Size_, unsigned MaxSize_, typename HeapID>
struct alignas(ZiIOBuf_Align) IOBufAlloc__ :
    public ZmNHeap<IOBufAlloc__<Base, Size_, MaxSize_, HeapID>>, public Base {
friend inline HeapID ZmNHeapID(IOBufAlloc__ *);

  using Base::data;

  enum { Size = Size_ };
  enum { MaxSize = MaxSize_ };

  alignas(ZiIOBuf_Align) uint8_t	data_[Size];

  IOBufAlloc__(void *owner, uint32_t length) :
    Base(&data_[0], Size, owner, length) { }

  ~IOBufAlloc__() = default;

  IOBufAlloc__(const IOBufAlloc__ &) = delete;
  IOBufAlloc__ &operator =(const IOBufAlloc__ &) = delete;
  IOBufAlloc__(IOBufAlloc__ &&) = delete;
  IOBufAlloc__ &operator =(IOBufAlloc__ &&) = delete;
};

template <
  typename Base,
  unsigned Size,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  typename HeapID = ZiIOBuf_HeapID>
using IOBufAlloc_ =
  IOBufAlloc__<Base, Size, MaxSize, HeapID>;

template <typename Base>
constexpr unsigned BuiltinSize(unsigned Size) {
  enum { CacheLineSize = Zm::CacheLineSize };
  // MinBufSz - minimum built-in buffer size
  enum { MinBufSz = sizeof(uintptr_t)<<1 };
  // IOBufOverhead - ZiIOBuf overhead
  enum { Overhead = sizeof(IOBufAlloc_<Base, MinBufSz>) - MinBufSz };
  // round up to cache line size, subtract overhead
  // and use that as the built-in buffer size
  return
    ((Size + Overhead + CacheLineSize - 1) & ~(CacheLineSize - 1)) - Overhead;
};

template <typename Base, unsigned Size, unsigned MaxSize, typename HeapID>
using IOBufAlloc = IOBufAlloc_<Base, BuiltinSize<Base>(Size), MaxSize, HeapID>;

// ensure cache line alignment
ZuAssert(!((sizeof(IOBufAlloc<IOBuf, 1, ZiIOBuf_DefltMaxSize, ZiIOBuf_HeapID>)) & (Zm::CacheLineSize - 1)));

#ifndef _WIN32
template <typename Base, typename HeapID>
struct IOBufURing :
    public ZmNHeap<IOBufURing<Base, HeapID>>, public Base {
friend inline HeapID ZmNHeapID(IOBufURing *);

  // The allocator is the ring and outlives all of its buffers and I/O.
  IOBufRing	*ring_;

  IOBufURing(
      IOBufRing *ring, uint8_t *data, unsigned size,
      void *owner, uint32_t length) :
    Base{data, data ? size : 0, owner, length},
    ring_{ring}
  {
    if (ring) this->data__ |= IOBuf::URing;
    else this->IOBuf::alloc(size);
  }
  ~IOBufURing() { if (this->data__ & IOBuf::URing) releaseURing_(); }

  IOBufRing *ring() const override { return ring_; }

protected:
  void releaseURing_() override {
    auto data = this->data_();
    auto ring = ring_;
    this->data__ = 0;
    ring_ = nullptr;
    ring->free(data);
  }
};

template <typename Base, unsigned Size, typename HeapID,
  bool Rx, bool UDP = false>
struct IOBufURingBase : public ZuIf<Rx, IOBufRxRing, IOBufRing> {
  using Ring = ZuIf<Rx, IOBufRxRing, IOBufRing>;
  using Ring::alloc;
  // Reserve recvmsg metadata without losing payload capacity or alignment.
  enum { Prefix = Rx && UDP ?
    (sizeof(io_uring_recvmsg_out) + sizeof(sockaddr_storage) +
      CMSG_SPACE(sizeof(int)) + ZiIOBuf_Align - 1) & ~(ZiIOBuf_Align - 1) : 0 };
  // Payload alignment is independent of descriptor storage.
  enum { BufSize = (Size + Prefix + ZiIOBuf_Align - 1) & ~(ZiIOBuf_Align - 1) };

  ZmRef<IOBuf> alloc(uint8_t *data, void *owner, uint32_t length) {
    return new IOBufURing<Base, HeapID>{
      data ? this : nullptr, data, BufSize, owner, length};
  }
};
#endif

template <typename Base, unsigned Size, unsigned MaxSize, typename HeapID>
struct AppBufAllocator :
  public ZmNHeap<AppBufAllocator<Base, Size, MaxSize, HeapID>>,
  public IOBufAllocator
{
  IOBufType::T type() override { return IOBufType::App; }
  ZmRef<IOBuf> alloc(void *owner, uint32_t length) override {
    return new IOBufAlloc<Base, Size, MaxSize, HeapID>(owner, length);
  }
};

template <typename Base, unsigned Size, unsigned MaxSize, typename HeapID>
struct RxUDPBufAllocator :
  public ZmNHeap<RxUDPBufAllocator<Base, Size, MaxSize, HeapID>>,
#ifndef _WIN32
  public IOBufURingBase<Base, Size, HeapID, true, true>
#else
  public IOBufAllocator
#endif
{
#ifndef _WIN32
  using URing = IOBufURingBase<Base, Size, HeapID, true, true>;
  RxUDPBufAllocator(unsigned ringSize = ZiIOBuf_DefltRingSize) {
    if (Zi::uring()) IOBufRxRing::init(ringSize, URing::BufSize);
  }
#endif

  IOBufType::T type() override { return IOBufType::RxUDP; }
  ZmRef<IOBuf> alloc(void *owner, uint32_t length) override {
#ifndef _WIN32
    if (Zi::uring()) {
      return URing::alloc(IOBufRxRing::allocSlot(), owner, length);
    } else {
      return new IOBufAlloc<Base, Size, MaxSize, HeapID>(owner, length);
    }
#else
    return new IOBufAlloc<Base, Size, MaxSize, HeapID>(owner, length);
#endif
  }
};

template <typename Base, unsigned Size, unsigned MaxSize, typename HeapID>
struct TxUDPBufAllocator :
  public ZmNHeap<TxUDPBufAllocator<Base, Size, MaxSize, HeapID>>,
#ifndef _WIN32
  public IOBufURingBase<Base, Size, HeapID, false, true>
#else
  public IOBufAllocator
#endif
{
#ifndef _WIN32
  using URing = IOBufURingBase<Base, Size, HeapID, false, true>;
  TxUDPBufAllocator(unsigned ringSize = ZiIOBuf_DefltRingSize) {
    if (Zi::uring()) IOBufRing::init(ringSize, URing::BufSize);
  }
#endif

  IOBufType::T type() override { return IOBufType::TxUDP; }
  ZmRef<IOBuf> alloc(void *owner, uint32_t length) override {
#ifndef _WIN32
    if (Zi::uring()) {
      return URing::alloc(IOBufRing::allocSlot(), owner, length);
    } else {
      return new IOBufAlloc<Base, Size, MaxSize, HeapID>(owner, length);
    }
#else
    return new IOBufAlloc<Base, Size, MaxSize, HeapID>(owner, length);
#endif
  }
};

template <typename Base, unsigned Size, unsigned MaxSize, typename HeapID>
struct RxTCPBufAllocator :
  public ZmNHeap<RxTCPBufAllocator<Base, Size, MaxSize, HeapID>>,
#ifndef _WIN32
  public IOBufURingBase<Base, Size, HeapID, true, false>
#else
  public IOBufAllocator
#endif
{
#ifndef _WIN32
  using URing = IOBufURingBase<Base, Size, HeapID, true, false>;
  RxTCPBufAllocator(unsigned ringSize = ZiIOBuf_DefltRingSize) {
    if (Zi::uring()) IOBufRxRing::init(ringSize, URing::BufSize);
  }
#endif

  IOBufType::T type() override { return IOBufType::RxTCP; }
  ZmRef<IOBuf> alloc(void *owner, uint32_t length) override {
#ifndef _WIN32
    if (Zi::uring()) {
      return URing::alloc(IOBufRxRing::allocSlot(), owner, length);
    } else {
      return new IOBufAlloc<Base, Size, MaxSize, HeapID>(owner, length);
    }
#else
    return new IOBufAlloc<Base, Size, MaxSize, HeapID>(owner, length);
#endif
  }
};

template <typename Base, unsigned Size, unsigned MaxSize, typename HeapID>
struct TxTCPBufAllocator :
  public ZmNHeap<TxTCPBufAllocator<Base, Size, MaxSize, HeapID>>,
#ifndef _WIN32
  public IOBufURingBase<Base, Size, HeapID, false, false>
#else
  public IOBufAllocator
#endif
{
#ifndef _WIN32
  using URing = IOBufURingBase<Base, Size, HeapID, false, false>;
  TxTCPBufAllocator(unsigned ringSize = ZiIOBuf_DefltRingSize) {
    if (Zi::uring()) IOBufRing::init(ringSize, URing::BufSize);
  }
#endif

  IOBufType::T type() override { return IOBufType::TxTCP; }
  ZmRef<IOBuf> alloc(void *owner, uint32_t length) override {
#ifndef _WIN32
    if (Zi::uring()) {
      return URing::alloc(IOBufRing::allocSlot(), owner, length);
    } else {
      return new IOBufAlloc<Base, Size, MaxSize, HeapID>(owner, length);
    }
#else
    return new IOBufAlloc<Base, Size, MaxSize, HeapID>(owner, length);
#endif
  }
};

template <typename Base, unsigned Size, unsigned MaxSize, typename HeapID>
struct RxTLSBufAllocator :
  public ZmNHeap<RxTLSBufAllocator<Base, Size, MaxSize, HeapID>>,
#ifndef _WIN32
  public IOBufURingBase<Base, Size, HeapID, true, false>
#else
  public IOBufAllocator
#endif
{
#ifndef _WIN32
  using URing = IOBufURingBase<Base, Size, HeapID, true, false>;
  RxTLSBufAllocator(unsigned ringSize = ZiIOBuf_DefltRingSize) {
    if (Zi::uring()) IOBufRxRing::init(ringSize, URing::BufSize);
  }
#endif

  IOBufType::T type() override { return IOBufType::RxTLS; }
  ZmRef<IOBuf> alloc(void *owner, uint32_t length) override {
#ifndef _WIN32
    if (Zi::uring()) {
      return URing::alloc(IOBufRxRing::allocSlot(), owner, length);
    } else {
      return new IOBufAlloc<Base, Size, MaxSize, HeapID>(owner, length);
    }
#else
    return new IOBufAlloc<Base, Size, MaxSize, HeapID>(owner, length);
#endif
  }
};

template <typename Base, unsigned Size, unsigned MaxSize, typename HeapID>
struct TxTLSBufAllocator :
  public ZmNHeap<TxTLSBufAllocator<Base, Size, MaxSize, HeapID>>,
#ifndef _WIN32
  public IOBufURingBase<Base, Size, HeapID, false, false>
#else
  public IOBufAllocator
#endif
{
#ifndef _WIN32
  using URing = IOBufURingBase<Base, Size, HeapID, false, false>;
  TxTLSBufAllocator(unsigned ringSize = ZiIOBuf_DefltRingSize) {
    if (Zi::uring()) IOBufRing::init(ringSize, URing::BufSize);
  }
#endif

  // Plaintext and final ciphertext share the same registered wire buffer.
  IOBufType::T type() override { return IOBufType::TxTLS; }
  ZmRef<IOBuf> alloc(void *owner, uint32_t length) override {
#ifndef _WIN32
    if (Zi::uring()) {
      return URing::alloc(IOBufRing::allocSlot(), owner, length);
    } else {
      return new IOBufAlloc<Base, Size, MaxSize, HeapID>(owner, length);
    }
#else
    return new IOBufAlloc<Base, Size, MaxSize, HeapID>(owner, length);
#endif
  }
};

} // Zi

using ZiIOBuf = Zi::IOBuf;

template <
  unsigned Size = ZiIOBuf_DefltSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  typename HeapID = ZiIOBuf_HeapID>
ZuDerive(ZiAppBufAllocator_,
  (Zi::AppBufAllocator<ZiIOBuf, Size, MaxSize, HeapID>));
template <
  unsigned Size = ZiIOBuf_DefltSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  ZuString HeapID = "ZiIOBuf">
ZuDerive(ZiAppBufAllocator,
  (Zi::AppBufAllocator<ZiIOBuf, Size, MaxSize, ZuStringT<HeapID>>));

template <
  unsigned Size = ZiIOBuf_DefltSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  typename HeapID = ZiIOBuf_HeapID>
ZuDerive(ZiRxUDPBufAllocator_,
  (Zi::RxUDPBufAllocator<ZiIOBuf, Size, MaxSize, HeapID>));
template <
  unsigned Size = ZiIOBuf_DefltSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  ZuString HeapID = "ZiIOBuf">
ZuDerive(ZiRxUDPBufAllocator,
  (Zi::RxUDPBufAllocator<ZiIOBuf, Size, MaxSize, ZuStringT<HeapID>>));

template <
  unsigned Size = ZiIOBuf_DefltSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  typename HeapID = ZiIOBuf_HeapID>
ZuDerive(ZiTxUDPBufAllocator_,
  (Zi::TxUDPBufAllocator<ZiIOBuf, Size, MaxSize, HeapID>));
template <
  unsigned Size = ZiIOBuf_DefltSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  ZuString HeapID = "ZiIOBuf">
ZuDerive(ZiTxUDPBufAllocator,
  (Zi::TxUDPBufAllocator<ZiIOBuf, Size, MaxSize, ZuStringT<HeapID>>));

template <
  unsigned Size = ZiIOBuf_DefltSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  typename HeapID = ZiIOBuf_HeapID>
ZuDerive(ZiRxTCPBufAllocator_,
  (Zi::RxTCPBufAllocator<ZiIOBuf, Size, MaxSize, HeapID>));
template <
  unsigned Size = ZiIOBuf_DefltSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  ZuString HeapID = "ZiIOBuf">
ZuDerive(ZiRxTCPBufAllocator,
  (Zi::RxTCPBufAllocator<ZiIOBuf, Size, MaxSize, ZuStringT<HeapID>>));

template <
  unsigned Size = ZiIOBuf_DefltSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  typename HeapID = ZiIOBuf_HeapID>
ZuDerive(ZiTxTCPBufAllocator_,
  (Zi::TxTCPBufAllocator<ZiIOBuf, Size, MaxSize, HeapID>));
template <
  unsigned Size = ZiIOBuf_DefltSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  ZuString HeapID = "ZiIOBuf">
ZuDerive(ZiTxTCPBufAllocator,
  (Zi::TxTCPBufAllocator<ZiIOBuf, Size, MaxSize, ZuStringT<HeapID>>));

template <
  unsigned Size = ZiIOBuf_DefltSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  typename HeapID = ZiIOBuf_HeapID>
ZuDerive(ZiRxTLSBufAllocator_,
  (Zi::RxTLSBufAllocator<ZiIOBuf, Size, MaxSize, HeapID>));
template <
  unsigned Size = ZiIOBuf_DefltSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  ZuString HeapID = "ZiIOBuf">
ZuDerive(ZiRxTLSBufAllocator,
  (Zi::RxTLSBufAllocator<ZiIOBuf, Size, MaxSize, ZuStringT<HeapID>>));

template <
  unsigned Size = ZiIOBuf_DefltSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  typename HeapID = ZiIOBuf_HeapID>
ZuDerive(ZiTxTLSBufAllocator_,
  (Zi::TxTLSBufAllocator<ZiIOBuf, Size, MaxSize, HeapID>));
template <
  unsigned Size = ZiIOBuf_DefltSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  ZuString HeapID = "ZiIOBuf">
ZuDerive(ZiTxTLSBufAllocator,
  (Zi::TxTLSBufAllocator<ZiIOBuf, Size, MaxSize, ZuStringT<HeapID>>));

#endif /* ZiIOBuf_HH */
