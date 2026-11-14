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
#ifndef ZiIOBuf_DefltSize
#define ZiIOBuf_DefltSize 980
#endif

// default upper limit on ZmVHeap arena-allocated buffer size
// - ZmVHeap is organized by powers of 2
#ifndef ZiIOBuf_DefltVHMaxSize
#define ZiIOBuf_DefltVHMaxSize (1<<20) // 1Mb
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

struct IOBufAllocator__ {
  ZiMultiplex	*mx = nullptr;
  IOBufType::T	type = IOBufType::App;
  uint32_t	size = 0;	// - overall capacity for each buffer
  uint32_t	headRoom = 0;	// - how much space is reserved for a frame header
  uint32_t	tailRoom = 0;	// - how much space is reserved for a frame trailer

  IOBufAllocator__(ZiMultiplex *mx_, IOBufType::T type_,
    uint32_t size_, uint32_t headRoom_ = 0, uint32_t tailRoom_ = 0)
  :
    mx(mx_), type(type_),
    size(size_),
    headRoom(headRoom_),
    tailRoom(tailRoom_) { }

  virtual ~IOBufAllocator__() = default;

  virtual uint8_t *alloc(unsigned size) const = 0;
  virtual void free(uint8_t *ptr, unsigned size, bool registered = false) const = 0;

  uint32_t capacity() const { return size - headRoom - tailRoom; }

  IOBufAllocator__(const IOBufAllocator__ &) = delete;
  IOBufAllocator__ &operator =(const IOBufAllocator__ &) = delete;
  IOBufAllocator__(IOBufAllocator__ &&) = delete;
  IOBufAllocator__ &operator =(IOBufAllocator__ &&) = delete;
};
using IOBufAllocatorList = ZmList<IOBufAllocator__,
  ZmListNode<IOBufAllocator__, ZmListHeapID<"Zi.IOBufAllocator">>>;
using IOBufAllocator_ = IOBufAllocatorList::Node;

struct IOBufAllocator;

struct IOBuf : public ZmPolymorph {
  uint32_t		size = 0;
  mutable void		*owner = nullptr;
  IOBufAllocator_	*allocator_;
  uintptr_t		data__ = 0;
  uint32_t		length = 0;
  uint32_t		skip = 0;

  // 64bit pointer-packing - uses bits 63 and 62
  ZuAssert(sizeof(uintptr_t) == 8);
  static constexpr uintptr_t Builtin = (uintptr_t(1)<<63);
  static constexpr uintptr_t Registered = (uintptr_t(1)<<62);
  static constexpr uintptr_t Failed = (uintptr_t(1)<<61);

protected:
  void free_() {
    if (builtin()) return;
    if (data_()) { allocator_->free(data_(), size, registered()); }
  }

public:
  IOBuf(uint8_t *data_, uint32_t size_,
    IOBufAllocator_ *allocator__, void *owner_ = nullptr,
    uint32_t length_ = 0, bool builtin_ = false,
    bool registered_ = false)
  :
    size{size_}, owner{owner_}, allocator_{allocator__},
    data__{reinterpret_cast<uintptr_t>(data_) |
      (builtin_ ? Builtin : 0) |
      (registered_ ? Registered : 0)},
    length{length_}
  {
    if (length > size) alloc(length);
  }

  virtual ~IOBuf() { free_(); }

  IOBuf(const IOBuf &buf) = delete;
  IOBuf &operator =(const IOBuf &buf) = delete;
  IOBuf(IOBuf &&buf) = delete;
  IOBuf &operator =(IOBuf &&buf) = delete;

  IOBufAllocator *allocator() const;

  uint8_t *data_() {
    return reinterpret_cast<uint8_t *>(data__ & ~(Builtin | Registered | Failed));
  }
  const uint8_t *data_() const {
    return const_cast<IOBuf *>(this)->data_();
  }

  bool builtin() const { return data__ & Builtin; }
  bool registered() const { return data__ & Registered; }

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
    auto ptr = allocator_->alloc(newSize);
    free_();
    size = newSize;
    data__ = reinterpret_cast<uintptr_t>(ptr) | (data__ & Failed); // clears Registered
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
    auto ptr = allocator_->alloc(newSize_);
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
  uint8_t *ensure(uint32_t newSize) {
    if (ZuUnlikely(failed() || newSize > UINT32_MAX - skip)) {
      failed(true);
      return nullptr;
    }
    newSize += skip;
    if (ZuLikely(newSize <= size)) return data();
    uint32_t grown = Grow(size, newSize);
    if (ZuUnlikely(grown < newSize)) { failed(true); return nullptr; }
    newSize = grown;
    auto old = data_();
    auto ptr = allocator_->alloc(newSize);
    if (length) memcpy(ptr + skip, old + skip, length);
    free_();
    size = newSize;
    data__ = reinterpret_cast<uintptr_t>(ptr) | (data__ & Failed);
    return ptr + skip;
  }

  // prepend to buffer (e.g. for a protocol header)
  template <auto Grow = ZmGrow>
  uint8_t *prepend(uint32_t length_) {
    ZmAssert(skip + length <= size);
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
    uint32_t grown = Grow(size, newSize);
    if (ZuUnlikely(grown < newSize)) { failed(true); return nullptr; }
    newSize = grown;
    auto ptr = allocator_->alloc(newSize);
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

// the Buf parameter permits Ztls, Zdb, etc. to inject their own
// buffer type into the hierarchy

template <
  typename Buf_,
  typename HeapID = ZiIOBuf_HeapID>
struct IOBufStub :
  public ZmNHeap<IOBufStub<Buf_, HeapID>>,
  public Buf_ {
friend inline HeapID ZmNHeapID(IOBufStub *);

  using Buf = Buf_;
  using Buf::Buf;
  ~IOBufStub() = default;
};

template <
  typename Buf_,
  unsigned Size_,
  unsigned Align_,
  typename HeapID = ZiIOBuf_HeapID>
struct IOBufBuiltin :
  public ZmNHeap<IOBufBuiltin<Buf_, Size_, Align_, HeapID>>,
  public Buf_ {
friend inline HeapID ZmNHeapID(IOBufBuiltin *);

  using Buf = Buf_;
  using Buf::data;

  enum { Size = Size_ };
  enum { Align = Align_ };

  alignas(Align) uint8_t	data_[Size];

  IOBufBuiltin(
    IOBufAllocator_ *allocator,
    void *owner = nullptr,
    uint32_t size = Size,
    uint32_t length = 0)
  :
    Buf(Size ? data_ : nullptr, size, allocator, owner, length, Size) { }

  ~IOBufBuiltin() = default;
};

template <typename Buf, unsigned Align>
consteval unsigned BuiltinSize(unsigned Size) {
  if (!Size) return 0;
  constexpr unsigned CacheLineSize = Zm::CacheLineSize;
  // MinBufSz - minimum built-in buffer size
  constexpr unsigned MinBufSz = sizeof(uintptr_t)<<1;
  // IOBufOverhead - ZiIOBuf overhead
  constexpr unsigned Overhead = sizeof(IOBufBuiltin<Buf, MinBufSz, Align>) - MinBufSz;
  // round up to cache line size, subtract overhead
  // and use that as the built-in buffer size
  return ((Size + Overhead + CacheLineSize - 1) & ~(CacheLineSize - 1)) - Overhead;
};

// ensure cache line alignment
ZuAssert(!((sizeof(IOBufBuiltin<
	IOBuf,
	BuiltinSize<IOBuf, ZiIOBuf_DefltAlign>(1),
	ZiIOBuf_DefltAlign,
	ZiIOBuf_HeapID>)) & (Zm::CacheLineSize - 1)));

struct IOBufPlugin;

struct IOBufAllocator : public IOBufAllocator_ {
  using IOBufAllocator_::IOBufAllocator_;
  virtual IOBufPlugin *provider() const { return nullptr; }
  virtual ZmRef<IOBuf> allocBuf(
    void *owner = nullptr, uint32_t length = 0, uint32_t mps = ~uint32_t(0)) = 0;
};

inline IOBufAllocator *IOBuf::allocator() const {
  return static_cast<IOBufAllocator *>(allocator_);
}

template <
  IOBufType::T Type = IOBufType::App,
  typename Buf_ = IOBuf,
  unsigned Size_ = ZiIOBuf_DefltSize,
  unsigned Align = ZiIOBuf_DefltAlign,
  typename HeapID = ZiIOBuf_HeapID,
  unsigned VHMaxSize_ = ZiIOBuf_DefltVHMaxSize,
  auto Grow = ZmGrow>
struct IOBufBuiltinAllocator :
  public ZmNHeap<IOBufBuiltinAllocator<Type, Buf_, Size_, Align, HeapID, VHMaxSize_, Grow>>,
  public IOBufAllocator {
friend inline ZuStringT<"Zi.IOBufBuiltinAllocator"> ZmNHeapID(IOBufBuiltinAllocator *);
  using Heap = ZmNHeap<IOBufBuiltinAllocator<Type, Buf_, Size_, Align, HeapID, VHMaxSize_, Grow>>;
  using Heap::operator new;
  using Heap::operator delete;
  enum { BufType = Type };
  using Buf = Buf_;
  enum { Size = BuiltinSize<Buf, Align>(Size_) };
  enum { VHMaxSize = BuiltinSize<Buf, Align>(VHMaxSize_) };
  using VHeap = ZmVHeap_<HeapID, Size, VHMaxSize, Align>;

  IOBufBuiltinAllocator(ZiMultiplex *mx = nullptr) :
    IOBufAllocator(mx, Type, Size) { }

  ZmRef<IOBuf> allocBuf(
    void *owner = nullptr, uint32_t length = 0, uint32_t mps = ~uint32_t(0)) override
  {
    ZmAssert(size == Size && !headRoom && !tailRoom);
    uint32_t ps = Size;
    if (ps > mps) ps = mps;
    if (ZuLikely(length <= ps))
      return new IOBufBuiltin<Buf, Size, Align, HeapID>(
	this, owner, ps, length);
    uint32_t size_ = length <= Size ? length : Grow(Size, length);
    if (ZuUnlikely(mps < size_)) return nullptr;
    auto data = static_cast<uint8_t *>(VHeap::alloc(size_));
    return new IOBufStub<Buf, HeapID>(
      data, size_, this, owner, length, false, false);
  }

  uint8_t *alloc(unsigned size) const override {
    return static_cast<uint8_t *>(VHeap::alloc(size));
  }
  void free(uint8_t *ptr, unsigned size, bool) const override  {
    VHeap::free(ptr, size);
  }
};

struct IOBufPlugin : public ZmPolymorph {
  uint32_t	size = 0;	// - overall size of each buffer
  uint32_t	headRoom = 0;	// - how much space is reserved for a frame header
  uint32_t	tailRoom = 0;	// - how much space is reserved for a frame trailer

  virtual uint8_t *alloc() = 0;
  virtual void free(uint8_t *) = 0;
};

// plugin allocators can use registered plugin backing storage
template <
  IOBufType::T Type,
  typename Buf_ = IOBuf,
  unsigned Size_ = ZiIOBuf_DefltSize,
  unsigned Align = ZiIOBuf_DefltAlign,
  typename HeapID = ZiIOBuf_HeapID,
  unsigned VHMaxSize_ = ZiIOBuf_DefltVHMaxSize,
  auto Grow = ZmGrow>
struct IOBufPluginAllocator :
  public ZmNHeap<IOBufPluginAllocator<Type, Buf_, Size_, Align, HeapID, VHMaxSize_, Grow>>,
  public IOBufAllocator {
friend inline ZuStringT<"Zi.IOBufPluginAllocator"> ZmNHeapID(IOBufPluginAllocator *);
  using Heap = ZmNHeap<IOBufPluginAllocator<Type, Buf_, Size_, Align, HeapID, VHMaxSize_, Grow>>;
  ZuAssert(
    Type == IOBufType::RxUDP || 
    Type == IOBufType::TxUDP || 
    Type == IOBufType::RxTCP || 
    Type == IOBufType::TxTCP);

  using Buf = Buf_;
  enum { Size = BuiltinSize<Buf, Align>(Size_) };
  enum { VHMaxSize = BuiltinSize<Buf, Align>(VHMaxSize_) };
  using VHeap = ZmVHeap_<HeapID, Size, VHMaxSize, Align>;

  ZmRef<IOBufPlugin>	plugin;

  using Heap::operator new;
  using Heap::operator delete;

  IOBufPluginAllocator(ZiMultiplex *mx, IOBufPlugin *plugin_) :
    IOBufAllocator(mx, Type,
      plugin_->size, plugin_->headRoom, plugin_->tailRoom),
    plugin(plugin_) { }

  IOBufPlugin *provider() const override { return plugin; }

  ZmRef<IOBuf> allocBuf(
    void *owner = nullptr, uint32_t length = 0, uint32_t mps = ~uint32_t(0)) override
  {
    if constexpr (
	Type == IOBufType::TxUDP ||
	Type == IOBufType::TxTCP) {
      // registered tx buffer
      uint32_t ps = capacity();
      if (ps > mps) ps = mps;
      if (length > ps) return nullptr;
      uint32_t size_ = ps + headRoom + tailRoom; // clamped size
      auto data = plugin->alloc();
      if (ZuUnlikely(!data)) return nullptr;
      ZmRef<IOBuf> buf = new IOBufStub<Buf, HeapID>(
	data, size_, this, owner, length, false, true);
      buf->skip = headRoom;
      return buf;
    } else {
      // registered rx buffer - stub only
      if (length) return nullptr;
      ZmRef<IOBuf> buf = new IOBufStub<Buf, HeapID>(
	nullptr, 0, this, owner, 0, false, true);
      buf->skip = headRoom;
      return buf;
    }
  }

  uint8_t *alloc(uint32_t size) const override {
    return static_cast<uint8_t *>(VHeap::alloc(size));
  }
  void free(uint8_t *ptr, uint32_t size, bool registered) const override {
    if (registered)
      return plugin->free(ptr);
    else
      VHeap::free(ptr, size);
  }
};

} // Zi

using ZiIOBuf = Zi::IOBuf;

#endif /* ZiIOBuf_HH */
