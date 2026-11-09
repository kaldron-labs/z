//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// IO buffer (packet buffer)

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
#ifndef ZiIOBuf_DefltAlign
#define ZiIOBuf_DefltAlign ZiIOBuf_Align
#endif

#ifndef ZiIOBuf_DefltCount
#define ZiIOBuf_DefltCount 1024
#endif

struct ZiIOBuf_HeapID : public ZuStringT<"ZiIOBuf"> { };

class ZiMultiplex;

namespace Zi {

ZtEnumNS(ZiAPI, IOBufType, int8_t, App, RxUDP, TxUDP, RxTCP, TxTCP, RxTLS, TxTLS);

class IOBufAllocator__ {
public:
  IOBufAllocator__(ZiMultiplex *mx, IOBufType::T type) : m_mx(mx), m_type(type) { }
  IOBufAllocator__(const IOBufAllocator__ &) = delete;
  IOBufAllocator__ &operator =(const IOBufAllocator__ &) = delete;

  virtual ~IOBufAllocator__() = default;

  virtual uint8_t *alloc(unsigned size, bool initial = false) const = 0;
  virtual void free(uint8_t *ptr, unsigned size, bool initial = false) const = 0;

  ZiMultiplex *mx() const { return m_mx; }
  IOBufType::T type() const { return m_type; }

private:
  ZiMultiplex	*m_mx = nullptr;
  IOBufType::T	m_type = IOBufType::App;
};
using IOBufAllocatorList = ZmList<IOBufAllocator__,
  ZmListNode<IOBufAllocator__, ZmListHeapID<"Zi.IOBufAllocator">>>;
using IOBufAllocator_ = IOBufAllocatorList::Node;

struct IOBuf : public ZmPolymorph {
  uint32_t		size = 0;
  mutable void		*owner = nullptr;
  IOBufAllocator_	*allocator;
  uintptr_t		data__ = 0;
  uint32_t		length = 0;
  uint32_t		skip = 0;

  // 64bit pointer-packing - uses bits 63 and 62
  ZuAssert(sizeof(uintptr_t) == 8);
  static constexpr uintptr_t Builtin = (uintptr_t(1)<<63);
  static constexpr uintptr_t Initial = (uintptr_t(1)<<62);
  static constexpr uintptr_t Failed = (uintptr_t(1)<<61);

protected:
  void free_() {
    if (builtin()) return;
    if (data_()) { allocator->free(data_(), size, initial()); }
  }

  IOBuf(uint8_t *data_, uint32_t size_,
      IOBufAllocator_ *allocator_,
      void *owner_ = nullptr, uint32_t length_ = 0, bool initial_ = false) :
    size{size_}, owner{owner_}, allocator{allocator_},
    data__{reinterpret_cast<uintptr_t>(data_) | (size_ && !initial_ ? Builtin : 0) | (initial_ ? Initial : 0)},
    length{length_} {
    if (length > size) alloc(length);
  }

public:
  virtual ~IOBuf() { free_(); }

  IOBuf(const IOBuf &buf) = delete;
  IOBuf &operator =(const IOBuf &buf) = delete;
  IOBuf(IOBuf &&buf) = delete;
  IOBuf &operator =(IOBuf &&buf) = delete;

  uint8_t *data_() {
    return reinterpret_cast<uint8_t *>(data__ & ~(Builtin | Initial | Failed));
  }
  const uint8_t *data_() const {
    return const_cast<IOBuf *>(this)->data_();
  }

  bool builtin() const { return data__ & Builtin; }
  bool initial() const { return data__ & Initial; }

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
    auto ptr = allocator->alloc(newSize, false);
    free_();
    size = newSize;
    data__ = reinterpret_cast<uintptr_t>(ptr) | (data__ & Failed); // clears Initial
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
    auto ptr = allocator->alloc(newSize);
    if (length) memcpy(ptr + skip, old + skip, length);
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

template <IOBufType::T Type, typename Base_, unsigned Size_,
  unsigned MaxSize_, unsigned Align, typename HeapID>
struct IOBufBuiltinAllocator;

template <typename Base, unsigned Size_, unsigned Align_, typename HeapID = ZiIOBuf_HeapID, unsigned MaxSize_ = ZiIOBuf_DefltMaxSize>
struct IOBufBuiltin_ : public ZmNHeap<IOBufBuiltin_<Base, Size_, Align_, HeapID, MaxSize_>>, public Base {
friend inline HeapID ZmNHeapID(IOBufBuiltin_ *);

  using Base::data;
  using Buf = Base;
  using Heap = HeapID;
  using Allocator = IOBufBuiltinAllocator<
    IOBufType::App, Base, Size_, MaxSize_, Align_, HeapID>;

  enum { Size = Size_, MaxSize = MaxSize_ };
  enum { Align = Align_ };

  alignas(Align) uint8_t	data_[Size];

  IOBufBuiltin_(IOBufAllocator_ *allocator, void *owner = nullptr, uint32_t length = 0) :
    Base(Size ? data_ : nullptr, Size, allocator, owner, length) { }

  ~IOBufBuiltin_() = default;

  IOBufBuiltin_(const IOBufBuiltin_ &) = delete;
  IOBufBuiltin_ &operator =(const IOBufBuiltin_ &) = delete;
  IOBufBuiltin_(IOBufBuiltin_ &&) = delete;
  IOBufBuiltin_ &operator =(IOBufBuiltin_ &&) = delete;
};

template <typename Base>
constexpr unsigned BuiltinSize(unsigned Size) {
  if (!Size) return 0;
  enum { CacheLineSize = Zm::CacheLineSize };
  // MinBufSz - minimum built-in buffer size
  enum { MinBufSz = sizeof(uintptr_t)<<1 };
  // IOBufOverhead - ZiIOBuf overhead
  enum { Overhead = sizeof(IOBufBuiltin_<Base, MinBufSz, ZiIOBuf_DefltAlign>) - MinBufSz };
  // round up to cache line size, subtract overhead
  // and use that as the built-in buffer size
  return
    ((Size + Overhead + CacheLineSize - 1) & ~(CacheLineSize - 1)) - Overhead;
};

template <typename Base, unsigned Size, unsigned Align, typename HeapID,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize>
using IOBufBuiltin = IOBufBuiltin_<Base, BuiltinSize<Base>(Size), Align, HeapID, MaxSize>;

// ensure cache line alignment
ZuAssert(!((sizeof(IOBufBuiltin<IOBuf, 1, ZiIOBuf_DefltAlign, ZiIOBuf_HeapID>)) & (Zm::CacheLineSize - 1)));

struct IOBufAllocator : public IOBufAllocator_ {
  using IOBufAllocator_::IOBufAllocator_;
  virtual ZmRef<IOBuf> allocBuf(void *owner = 0, uint32_t length = 0) = 0;
};

template <
  IOBufType::T Type = IOBufType::App,
  typename Base_ = Zi::IOBuf,
  unsigned Size_ = ZiIOBuf_DefltSize,
  unsigned MaxSize_ = ZiIOBuf_DefltMaxSize,
  unsigned Align = ZiIOBuf_DefltAlign,
  typename HeapID = ZiIOBuf_HeapID>
struct IOBufBuiltinAllocator : public IOBufAllocator {
  enum { BufType = Type, Size = Size_, MaxSize = MaxSize_ };
  using Heap = HeapID;

  IOBufBuiltinAllocator(ZiMultiplex *mx = nullptr) : IOBufAllocator(mx, Type) { }

  using Buf = IOBufBuiltin<Base_, Size, Align, HeapID, MaxSize>;

  ZmRef<IOBuf> allocBuf(void *owner = nullptr, uint32_t length = 0) override {
    return new Buf(this, owner, length);
  }

  using VHeap = ZmVHeap_<HeapID, Size, MaxSize, Align>;

  uint8_t *alloc(unsigned size, bool) const override {
    return static_cast<uint8_t *>(VHeap::alloc(size));
  }
  void free(uint8_t *ptr, unsigned size, bool) const override  {
    VHeap::free(ptr, size);
  }
};

struct IOBufPlugin {
  virtual uint8_t *alloc(uint32_t, bool) = 0;
  virtual void free(uint8_t *, uint32_t, bool) = 0;
};

// plugin allocators use plugin backing storage, including growth
template <IOBufType::T Type, unsigned Size, typename Buf_ = IOBuf>
struct IOBufPluginAllocator : public IOBufAllocator {
  IOBufPlugin	*plugin = nullptr;

  struct Buf : public Buf_ {
    Buf(uint8_t *data, IOBufAllocator_ *allocator, void *owner, uint32_t length) :
      Buf_(data, Size, allocator, owner, length, true) { }
  };

  IOBufPluginAllocator(ZiMultiplex *mx, IOBufPlugin *plugin_) :
    IOBufAllocator(mx, Type), plugin(plugin_) { }

  ZmRef<IOBuf> allocBuf(void *owner = nullptr, uint32_t length = 0) override {
    auto storage = plugin->alloc(Size, true);
    if (ZuUnlikely(!storage)) return nullptr;
    return new Buf(storage, this, owner, length);
  }
  uint8_t *alloc(uint32_t size, bool initial = false) const override {
    return plugin->alloc(size, initial);
  }
  void free(uint8_t *ptr, uint32_t size, bool initial) const override {
    plugin->free(ptr, size, initial);
  }
};

} // Zi

using ZiIOBuf = Zi::IOBuf;

#endif /* ZiIOBuf_HH */
