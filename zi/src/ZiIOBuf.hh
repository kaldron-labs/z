//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// IO buffer (packet buffer)
//
//             .----------------.
//             | IOBufAllocator | ---> ZmVHeap
//             '----------------'
//                      ^ allocator
// .-----------.-------------.
// |  ZmNHeap  |   ZiIOBuf   |
// |-------------------------|
// |     Zi::IOBufAlloc      |
// '-------------------------'
//
// Zi::IOBufAlloc<Base, Size[, MaxSize]> uses ZmNHeap for built-in storage
// sized from Size. Its allocator supplies backing beyond that capacity,
// using ZmVHeap by default. Size may be zero for allocator-owned backing only.
// MaxSize is a hard-coded safe upper limit.

#ifndef ZiIOBuf_HH
#define ZiIOBuf_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <stdint.h>
#include <string.h>

#include <zlib/ZuPrint.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuSpan.hh>

#include <zlib/ZmPolymorph.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmAssert.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmVHeap.hh>

#include <zlib/ZtEnum.hh>

// TCP over Ethernet maximum payload is 1460 (without Jumbo frames)
// - ... but the vast majority of small messages are under 1k
//   and ZmVHeap is organized by powers of 2
#ifndef ZiIOBuf_DefltSize
#define ZiIOBuf_DefltSize 980
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

class ZiMultiplex;

namespace Zi {

ZuDerive(VHeap, (ZmVHeap_<ZiIOBuf_HeapID, ZmVHeap_DefltMin, ZmVHeap_DefltMax, ZiIOBuf_Align>));

ZtEnumNS(ZiAPI, IOBufType, int8_t, App, RxUDP, TxUDP, RxTCP, TxTCP, RxTLS, TxTLS);

struct IOBufAllocator_ : public ZmPolymorph { };
using IOBufAllocatorList = ZmList<IOBufAllocator_,
  ZmListNode<IOBufAllocator_, ZmListHeapID<"">>>;

class ZiAPI IOBufAllocator : private VHeap, public IOBufAllocatorList::Node {
friend ::ZiMultiplex;
friend inline ZuStringT<"Zi.IOBufAllocator"> ZmNHeapID(IOBufAllocator *);

public:
  IOBufAllocator(IOBufType::T type = IOBufType::App) : m_type{type} { }
  IOBufAllocator(const IOBufAllocator &) = delete;
  IOBufAllocator &operator =(const IOBufAllocator &) = delete;

  virtual uint8_t *alloc(unsigned size) {
    return static_cast<uint8_t *>(valloc(size));
  }
  virtual void free(uint8_t *data) { vfree(data); }

  ZiMultiplex *mx() const { return m_mx; }
  IOBufType::T type() const { return m_type; }
  bool finalized() const { return m_finalized; }

private:
  // Cold setup publishes the allocator owner before network use.
  bool bind(ZiMultiplex *mx) {
    if (!mx || m_finalized || (m_mx && m_mx != mx) ||
	m_type == IOBufType::App) return false;
    m_mx = mx;
    return true;
  }
  // Finalization does not turn a dedicated allocator into a reusable one.
  void finalMx() { m_mx = nullptr; m_finalized = true; }

private:
  ZiMultiplex	*m_mx = nullptr;
  IOBufType::T	m_type = IOBufType::App;
  bool		m_finalized = false;
};

struct IOBuf : public ZmPolymorph {
  uint32_t	size = 0;
  mutable void	*owner = nullptr;
  IOBufAllocator *allocator;
  uintptr_t	data__ = 0;
  uint32_t	length = 0;
  uint32_t	skip = 0;

  // 64bit pointer-packing - uses bits 63 and 62
  ZuAssert(sizeof(uintptr_t) == 8);
  static constexpr uintptr_t Builtin = (uintptr_t(1)<<63);
  static constexpr uintptr_t Failed = (uintptr_t(1)<<62);

protected:
  void free_() {
    if (data__ & Builtin) return;
    if (data_()) { ZmAssert(allocator, return); allocator->free(data_()); }
  }

  IOBuf(uint8_t *data_, uint32_t size_,
      IOBufAllocator *allocator_,
      void *owner_ = nullptr, uint32_t length_ = 0) :
    size{size_}, owner{owner_}, allocator{allocator_},
    data__{reinterpret_cast<uintptr_t>(data_) | (size_ ? Builtin : 0)},
    length{length_} {
    ZmAssert(allocator);
    if (length > size) alloc(length);
  }

public:
  virtual ~IOBuf() { free_(); }

  IOBuf(const IOBuf &buf) = delete;
  IOBuf &operator =(const IOBuf &buf) = delete;
  IOBuf(IOBuf &&buf) = delete;
  IOBuf &operator =(IOBuf &&buf) = delete;

  uint8_t *data_() {
    return reinterpret_cast<uint8_t *>(data__ & ~(Builtin | Failed));
  }
  const uint8_t *data_() const {
    return const_cast<IOBuf *>(this)->data_();
  }

  bool builtin() const { return data__ & Builtin; }

  // Boolean conversion describes readable bytes; output status is separate.
  bool failed() const { return data__ & Failed; }
  void failed(bool value) {
    if (value) data__ |= Failed;
    else data__ &= ~Failed;
  }
  bool operator !() const { return !length; }

  uint8_t *data() { return data_() + skip; }
  const uint8_t *data() const { return data_() + skip; }

  uint8_t *end() { return data_() + skip + length; }
  const uint8_t *end() const { return data_() + skip + length; }

  unsigned avail() const { return size - (skip + length); }

  uint8_t *alloc(unsigned newSize) {
    if (ZuLikely(newSize <= size)) return data();
    ZmAssert(allocator, return nullptr);
    auto ptr = allocator->alloc(newSize);
    free_();
    size = newSize;
    data__ = reinterpret_cast<uintptr_t>(ptr) | (data__ & Failed);
    return ptr;
  }

  void clear() {
    length = 0;
    skip = 0;
    failed(false); // begin a new output operation
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
      if (tail && newSize != oldSize) {
	memmove(old + newSize - tail, old + oldSize - tail, tail);
      }
      size = newSize_;
      return old;
    }
    newSize_ = Grow(size, newSize_);
    ZmAssert(allocator, return nullptr);
    auto ptr = allocator->alloc(newSize_);
    if (head) memcpy(ptr + skip, old, head);
    if (tail)
      memcpy(ptr + newSize_ - tail, old + oldSize - tail, tail);
    free_();
    size = newSize_;
    data__ = reinterpret_cast<uintptr_t>(ptr) | (data__ & Failed);
    return ptr + skip;
  }

  // ensure at least newSize bytes in buffer, preserving any existing data
  // - null means failed output status, size overflow, or insufficient Grow
  // - allocation failure is handled by ZmHeapFail
  template <auto Grow = ZmGrow>
  uint8_t *ensure(unsigned newSize) {
    if (ZuUnlikely(failed() || newSize > UINT32_MAX - skip)) {
      failed(true);
      return nullptr;
    }
    newSize += skip;
    if (ZuLikely(newSize <= size)) return data();
    unsigned grown = Grow(size, newSize);
    if (ZuUnlikely(grown < newSize)) { failed(true); return nullptr; }
    newSize = grown;
    auto old = data_();
    ZmAssert(allocator, return nullptr);
    auto ptr = allocator->alloc(newSize);
    if (length) {
      memcpy(ptr + skip, old + skip, length);
    }
    free_();
    size = newSize;
    data__ = reinterpret_cast<uintptr_t>(ptr) | (data__ & Failed);
    return ptr + skip;
  }

  // prepend to buffer (e.g. for a protocol header)
  template <auto Grow = ZmGrow>
  uint8_t *prepend(unsigned length_) {
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
      if (length && newSkip != skip) {
	memmove(old + newSkip, old + skip, length);
      }
      skip = newSkip - length_;
      length += length_;
      return data();
    }
    newSize = Grow(size, newSize);
    ZmAssert(allocator, return nullptr);
    auto ptr = allocator->alloc(newSize);
    auto newSkip = newSize - length;
    if (length) {
      memcpy(ptr + newSkip, old + skip, length);
    }
    free_();
    size = newSize;
    skip = newSkip - length_;
    length += length_;
    data__ = reinterpret_cast<uintptr_t>(ptr) | (data__ & Failed);
    return data();
  }

  bool append(const uint8_t *data, unsigned length_) {
    if (ZuUnlikely(failed())) return false;
    if (!length_) return true;
    if (ZuUnlikely(length_ > UINT32_MAX - length)) {
      failed(true);
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
    if (ZuUnlikely(failed())) return;
    ZuPrint<P>::print(*this, ZuFwd<P>(p));
  }
  template <typename P, typename = MatchPBuffer<P>>
  void append(const P &p) {
    if (ZuUnlikely(failed())) return;
    unsigned length_ = ZuPrint<P>::length(p);
    if (!length_) return;
    if (ZuUnlikely(length_ > UINT32_MAX - length)) {
      failed(true);
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
  using Buf = Base;
  using Heap = HeapID;

  enum { Size = Size_ };
  enum { MaxSize = MaxSize_ };

  alignas(ZiIOBuf_Align) uint8_t	data_[Size];

  template <typename ...Args,
    decltype(Base(
	ZuDeclVal<uint8_t *>(), unsigned(), ZuDeclVal<IOBufAllocator *>(),
	ZuDeclVal<Args &&>()...), int()) = 0>
  IOBufAlloc__(IOBufAllocator *allocator, Args &&...args) :
    Base(Size ? data_ : nullptr, Size, allocator, ZuFwd<Args>(args)...) { }

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
  if (!Size) return 0;
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

template <typename Base, unsigned Size = ZiIOBuf_DefltSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize, typename HeapID = ZiIOBuf_HeapID>
using IOBufAlloc = IOBufAlloc_<Base, BuiltinSize<Base>(Size), MaxSize, HeapID>;

// ensure cache line alignment
ZuAssert(!((sizeof(IOBufAlloc<IOBuf, 1, ZiIOBuf_DefltMaxSize, ZiIOBuf_HeapID>)) & (Zm::CacheLineSize - 1)));

template <typename Base, unsigned Size, unsigned MaxSize = ZiIOBuf_DefltMaxSize>
struct AppBufAllocator :
  public ZmNHeap<AppBufAllocator<Base, Size, MaxSize>>,
  public IOBufAllocator
{
  AppBufAllocator() : IOBufAllocator{IOBufType::App} { }
  ZmRef<Base> allocBuf(void *owner = nullptr, uint32_t length = 0) {
    return new IOBufAlloc<Base, Size, MaxSize>{this, owner, length};
  }
};

template <typename Base, unsigned Size, unsigned MaxSize = ZiIOBuf_DefltMaxSize>
struct RxUDPBufAllocator :
  public ZmNHeap<RxUDPBufAllocator<Base, Size, MaxSize>>,
  public IOBufAllocator
{
  RxUDPBufAllocator() : IOBufAllocator{IOBufType::RxUDP} { }
  ZmRef<Base> allocBuf(void *owner = nullptr, uint32_t length = 0) {
    return new IOBufAlloc<Base, Size, MaxSize>{this, owner, length};
  }
};

template <typename Base, unsigned Size, unsigned MaxSize = ZiIOBuf_DefltMaxSize>
struct TxUDPBufAllocator :
  public ZmNHeap<TxUDPBufAllocator<Base, Size, MaxSize>>,
  public IOBufAllocator
{
  TxUDPBufAllocator() : IOBufAllocator{IOBufType::TxUDP} { }
  ZmRef<Base> allocBuf(void *owner = nullptr, uint32_t length = 0) {
    return new IOBufAlloc<Base, Size, MaxSize>{this, owner, length};
  }
};

template <typename Base, unsigned Size, unsigned MaxSize = ZiIOBuf_DefltMaxSize>
struct RxTCPBufAllocator :
  public ZmNHeap<RxTCPBufAllocator<Base, Size, MaxSize>>,
  public IOBufAllocator
{
  RxTCPBufAllocator() : IOBufAllocator{IOBufType::RxTCP} { }
  ZmRef<Base> allocBuf(void *owner = nullptr, uint32_t length = 0) {
    return new IOBufAlloc<Base, Size, MaxSize>{this, owner, length};
  }
};

template <typename Base, unsigned Size, unsigned MaxSize = ZiIOBuf_DefltMaxSize>
struct TxTCPBufAllocator :
  public ZmNHeap<TxTCPBufAllocator<Base, Size, MaxSize>>,
  public IOBufAllocator
{
  TxTCPBufAllocator() : IOBufAllocator{IOBufType::TxTCP} { }
  ZmRef<Base> allocBuf(void *owner = nullptr, uint32_t length = 0) {
    return new IOBufAlloc<Base, Size, MaxSize>{this, owner, length};
  }
};

template <typename Base, unsigned Size, unsigned MaxSize = ZiIOBuf_DefltMaxSize>
struct RxTLSBufAllocator :
  public ZmNHeap<RxTLSBufAllocator<Base, Size, MaxSize>>,
  public IOBufAllocator
{
  RxTLSBufAllocator() : IOBufAllocator{IOBufType::RxTLS} { }
  ZmRef<Base> allocBuf(void *owner = nullptr, uint32_t length = 0) {
    return new IOBufAlloc<Base, Size, MaxSize>{this, owner, length};
  }
};

template <typename Base, unsigned Size, unsigned MaxSize = ZiIOBuf_DefltMaxSize>
struct TxTLSBufAllocator :
  public ZmNHeap<TxTLSBufAllocator<Base, Size, MaxSize>>,
  public IOBufAllocator
{
  TxTLSBufAllocator() : IOBufAllocator{IOBufType::TxTLS} { }
  ZmRef<Base> allocBuf(void *owner = nullptr, uint32_t length = 0) {
    return new IOBufAlloc<Base, Size, MaxSize>{this, owner, length};
  }
};

} // Zi

using ZiIOBuf = Zi::IOBuf;

template <typename Base, unsigned Size, unsigned MaxSize = ZiIOBuf_DefltMaxSize>
ZuDerive(ZiAppBufAllocator, (Zi::AppBufAllocator<Base, Size, MaxSize>));

template <typename Base, unsigned Size, unsigned MaxSize = ZiIOBuf_DefltMaxSize>
ZuDerive(ZiRxUDPBufAllocator, (Zi::RxUDPBufAllocator<Base, Size, MaxSize>));

template <typename Base, unsigned Size, unsigned MaxSize = ZiIOBuf_DefltMaxSize>
ZuDerive(ZiTxUDPBufAllocator, (Zi::TxUDPBufAllocator<Base, Size, MaxSize>));

template <typename Base, unsigned Size, unsigned MaxSize = ZiIOBuf_DefltMaxSize>
ZuDerive(ZiRxTCPBufAllocator, (Zi::RxTCPBufAllocator<Base, Size, MaxSize>));

template <typename Base, unsigned Size, unsigned MaxSize = ZiIOBuf_DefltMaxSize>
ZuDerive(ZiTxTCPBufAllocator, (Zi::TxTCPBufAllocator<Base, Size, MaxSize>));

template <typename Base, unsigned Size, unsigned MaxSize = ZiIOBuf_DefltMaxSize>
ZuDerive(ZiRxTLSBufAllocator, (Zi::RxTLSBufAllocator<Base, Size, MaxSize>));

template <typename Base, unsigned Size, unsigned MaxSize = ZiIOBuf_DefltMaxSize>
ZuDerive(ZiTxTLSBufAllocator, (Zi::TxTLSBufAllocator<Base, Size, MaxSize>));

#endif /* ZiIOBuf_HH */
