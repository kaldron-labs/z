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

#include <zlib/ZmPolyHash.hh>

#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiTx.hh>

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
using ZdbBuf_Size = typename ZdbBuf_Size_<ZuDecay<U>>::T;

// type-specific buffer cache ID, e.g.
// struct Foo {
//   ...
//   friend ZuStringT<"Foo"> ZdbBufHeapID(Foo *);
// };
ZuStringT<"Zdb.Buf"> ZdbBufHeapID(...); // default
template <typename U>
struct ZdbBuf_HeapID_ { using T = decltype(ZdbBufHeapID(ZuDeclVal<U *>())); };
template <typename U>
using ZdbBuf_HeapID = typename ZdbBuf_HeapID_<ZuDecay<U>>::T;

namespace Zdb_ {

class AnyTable;
class DB;

// --- I/O buffer

struct IOBuf_ : public ZiTxBuf {
  mutable void	*rep = nullptr;	// typed replication buffer RepBuf<T>

  using ZiTxBuf::ZiTxBuf;

  auto hdr() const { return ptr<Hdr>(); }
  auto hdr() { return ptr<Hdr>(); }

  template <typename T>
  const ZfbType<T> *fbo() const {
    auto record = Zdb_::record(msg(hdr()));
    if (ZuUnlikely(!record)) return nullptr;
    auto data = Zfb::Load::bytes(record->data());
    return ZfbStruct::verify<T>(data);
  }
  // trusted buffer (written locally)
  template <typename T>
  const ZfbType<T> *fbo_() const {
    auto record = record_(msg_(hdr()));
    auto data = Zfb::Load::bytes(record->data());
    return ZfbStruct::root<T>(&data[0]);
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
  return record_(msg_(buf.hdr()))->un();
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

// buffer allocation

template <typename T = void>
using IOBufAlloc =
  Zi::IOBufAlloc<IOBuf, ZdbBuf_Size<T>{}, MaxBufSize, ZdbBuf_HeapID<T>>;

typedef ZmRef<IOBuf> (*IOBufAllocFn)();

using RxBufAlloc = IOBufAlloc<>;

} // Zdb_

#endif /* ZdbBuf_HH */
