//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z Database buffer

#ifndef ZdbBuf_HH
#define ZdbBuf_HH

#ifndef ZdbLib_HH
#include <zlib/ZdbLib.hh>
#endif

#include <zlib/ZuDerive.hh>

#include <zlib/ZmFn.hh>
#include <zlib/ZmPolyHash.hh>

#include <zlib/Zfb.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiTx.hh>
#include <zlib/ZiRxStream.hh>

#include <zlib/ZdbMsg.hh>

namespace Zdb_ {

// --- I/O Buffer Sizes

enum { DefltBufSize = 192 };		// default row buffer size
enum { HBBufSize = 128 };		// heartbeat buffer size
enum { MaxBufSize = 100<<20 };		// 100Mb hard-coded safe upper limit

} // Zdb_

// type-specific buffer size, e.g.
// struct Foo {
//   ...
//   friend ZuUnsigned<512> ZdbBufSize(Foo *);
// };
ZuUnsigned<Zdb_::DefltBufSize> ZdbBufSize(...); // default
template <typename U>
struct ZdbBuf_Size_ { using T = decltype(ZdbBufSize(ZuDeclVal<U *>())); };
template <typename U>
using ZdbBuf_Size = ZdbBuf_Size_<ZuDecay<U>>::T;

// type-specific buffer cache ID, e.g.
// struct Foo {
//   ...
//   friend ZuStringT<"Foo"> ZdbBufHeapID(Foo *);
// };
ZuStringT<"Zdb.Buf"> ZdbBufHeapID(...); // default
template <typename U>
struct ZdbBuf_HeapID_ { using T = decltype(ZdbBufHeapID(ZuDeclVal<U *>())); };
template <typename U>
using ZdbBuf_HeapID = ZdbBuf_HeapID_<ZuDecay<U>>::T;

namespace Zdb_ {

class AnyTable;
class DB;

// --- I/O buffer

struct IOBuf_ : public ZiTxBuf {
  mutable void	*rep = nullptr;	// typed replication buffer RepBuf<T>

  using ZiTxBuf::ZiTxBuf;

  auto hdr() const { return reinterpret_cast<const Hdr *>(ZiIOBuf::data()); }
  auto hdr() { return reinterpret_cast<Hdr *>(ZiIOBuf::data()); }
  auto frameSpan() const { return msgData(hdr()); }

  template <typename T>
  Zfb::View<T> fbo() const {
    return Zdb_::data<T>(Zdb_::record(msg(hdr())));
  }
  // trusted buffer (written locally or already verified at ingress)
  template <typename T>
  Zfb::View<T> fbo_() const {
    return Zdb_::data_<T>(record_(msg_(hdr())));
  }

  struct Print {
    const IOBuf_ *buf = nullptr;
    const AnyTable *table = nullptr;
    template <typename S> void print(S &s) const;
    friend ZuPrintFn ZuPrintType(Print *);
  };
  Print print(AnyTable *table = nullptr) { return Print{this, table}; }
};

inline UN IOBuf_UNAxor(const IOBuf_ &buf) {
  return record_(msg_(buf.hdr())).get<Msg::ZfField(Record, un)>();
}

constexpr const auto &BufCache_ID() { return "Zdb.BufCache"; }

ZmHashDerive(BufCacheUN, IOBuf_,
  (ZmHashNode<IOBuf_,
    ZmHashKey<IOBuf_UNAxor,
      ZmHashLock<ZmPLock,
	ZmHashShadow<>>>>));

struct IOBuf : public BufCacheUN::Node {
  ZuDerive_(IOBuf, BufCacheUN::Node)
  using ZiIOBuf::data;
};

// A frame view owns only metadata; the original receive payload stays in place.
struct RxBuf : public ZmNHeap<RxBuf>, public IOBuf {
  ZmRef<ZiIOBuf> backing;

  friend inline ZuStringT<"Zdb.RxBuf"> ZmNHeapID(RxBuf *);
  RxBuf(ZmRef<ZiIOBuf> buf) :
    IOBuf{buf->data_(), buf->size, buf->allocator(), nullptr, buf->length, true},
    backing{ZuMv(buf)}
  {
    skip = backing->skip;
  }
};

// Each connection queues its own descriptor while retaining the source frame.
struct TxBuf : public ZmNHeap<TxBuf>, public ZiTxBuf {
  using ZiIOBuf::data;
  ZmRef<Zdb_::IOBuf> backing;

  friend inline ZuStringT<"Zdb.TxBuf"> ZmNHeapID(TxBuf *);
  TxBuf(Zi::IOBufAllocator *allocator, ZmRef<Zdb_::IOBuf> buf) :
    ZiTxBuf{nullptr, 0, allocator}, backing{ZuMv(buf)}
  {
    auto span = backing->frameSpan();
    data__ = reinterpret_cast<uintptr_t>(span.data()) | Builtin;
    size = length = span.length();
  }
};

// buffer allocation

template <typename T = void>
auto localBufAllocator() {
  static ZiMultiplex mx;
  static auto allocator = mx.allocator<Zi::IOBufType::App, IOBuf,
    ZdbBuf_Size<T>{}, ZiIOBuf_Align, ZdbBuf_HeapID<T>, MaxBufSize>();
  return allocator;
}

template <typename T>
class RowBuilder : public Zfb::IOBuilder<Zfb::BufferAlign<T>::N> {
  using Base = Zfb::IOBuilder<Zfb::BufferAlign<T>::N>;
public:
  RowBuilder() : Base{Zdb_::localBufAllocator<T>()->allocBuf(), Base::Align, UINT32_MAX,
    0, Zfb::Wire::MaxAlign} { }
};

template <typename T>
using TableBufAllocator = Zi::IOBufAllocator;

// The callback retains its allocator; the handle registers transport ownership.
class IOBufAllocFn :
  public ZmFn<ZmRef<IOBuf>(), ZmFnHeapID<"Zdb.BufAlloc">> {
  using Base = ZmFn<ZmRef<IOBuf>(), ZmFnHeapID<"Zdb.BufAlloc">>;
public:
  using Base::Base;

  Zi::IOBufAllocator *allocator() const { return m_allocator; }
  void allocator(Zi::IOBufAllocator *value) { m_allocator = value; }

private:
  Zi::IOBufAllocator *m_allocator = nullptr;
};
using IOBufAllocFactory = ZmFn<IOBufAllocFn(ZiMultiplex *)>;

template <typename T>
IOBufAllocFn tableBufAllocFn(ZiMultiplex *mx) {
  auto allocator = mx->allocator<Zi::IOBufType::App, IOBuf,
    ZdbBuf_Size<T>{}, ZiIOBuf_Align, ZdbBuf_HeapID<T>, MaxBufSize>();
  IOBufAllocFn fn{[allocator]() {
    ZmRef<IOBuf> buf = allocator->allocBuf();
    // Reserve the envelope before the backwards-growing row arena starts.
    buf->skip = Zfb::Wire::MaxAlign;
    return buf;
  }};
  fn.allocator(allocator);
  return fn;
}

template <typename T>
IOBufAllocFn localBufAllocFn() {
  return IOBufAllocFn{[]() {
    auto buf = ZmRef<IOBuf>{Zdb_::localBufAllocator<T>()->allocBuf()};
    buf->skip = Zfb::Wire::MaxAlign;
    return buf;
  }};
}


} // Zdb_

#endif /* ZdbBuf_HH */
