//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>
#include <string>

#include <zlib/ZuBox.hh>

#include <zlib/ZmDemangle.hh>

#include <zlib/ZtJSON.hh>

#include <zlib/Zfb.hh>
#include <zlib/ZfbStruct.hh>

#include <flatbuffers/reflection.h>
#include <flatbuffers/reflection_generated.h>

#include "zfbtest3_fbs.h"

namespace zfbtest3 {

namespace Side {
  ZfbEnumValues(Side, Buy, Sell);
};

struct Order {
  ZuCArray<32>		symbol;
  uint64_t		orderID;
  ZuCArray<32>		link;
  ZuCArray<32>		clOrdID;
  uint64_t		seqNo;
  int			side;
  int			price;
  int			quantity;

  ZtBitmap		bitmap;
  ZuID			id;

  friend ZtStructPrint ZuPrintType(Order *);
};

ZfbStruct((Order, JSON),
  (((symbol), (Keys<0>, Ctor<0>)), (String)),
  (((orderID), (Keys<0>, Ctor<1>)), (UInt64)),
  (((link), ((Keys<1, 2>), Group<2>, Descend<2>, Ctor<2>)), (String)),
  (((clOrdID), (Keys<1>, Ctor<3>)), (String)),
  (((seqNo), (Keys<2>, Descend<2>, Ctor<4>)), (UInt64)),
  (((side), (Ctor<5>, Enum<Side::Map>)), (Int32)),
  (((price), (Ctor<6>)), (Int32)),
  (((quantity), (Ctor<7>)), (Int32)),

  (((bitmap), (Ctor<8>)), (UDT)),
  (((id), (Ctor<9>)), (UDT)));

} // zfbtest3

inline void out(const char *s) { std::cout << s << '\n'; }

#define CHECK(x) ((x) ? out("OK  " #x) : out("NOK " #x))

using IOBuilder = Zfb::IOBuilder;
using IOBuf = ZiIOBuf;

template <typename T>
ZtString<> json(const T &v) {
  ZtString<> s;
  ZtJSON::save<ZuFacet::JSON, ZtFieldFilter::All>(s, v);
  return s;
}

int main()
{
  using namespace zfbtest3;

  Order order{"IBM", 42, "FIX0", "order0", 0, Side::Buy, 100, 100, "1-3", "ID0"};

  {
    IOBuilder fbb(new ZiIOBufAlloc<>());
    //fbb.Finish(ZfbTransform::Object::save<ZuFacet::Core, ZtFieldFilter::Save>(fbb, order));
    fbb.Finish(ZfbStruct::save(fbb, order));
    auto buf = fbb.buf();
    auto fbo = ZfbStruct::root<Order>(buf->data());
    std::cout << "fbo: " << *fbo << '\n';
    std::cout << "JSON(fbo): " << json(*fbo) << '\n';
    auto order_ = ZfbStruct::ctor<Order>(fbo);
    std::cout << "order: " << order_ << '\n';
    std::cout << "JSON(order): " << json(order_) << '\n';
  }

  using Key = ZuStructKeyT<Order>;
  std::cout << "Key " << ZmDemangle(typeid(Key).name()) << '\n';
  using KeyFields = ZuFields<Key>;
  std::cout << "KeyFields " << ZmDemangle(typeid(KeyFields).name()) << '\n';
  ZuAssert((ZuIsSame<ZuUnder<Order>, Order>{}));
  ZuAssert((ZuIsSame<ZuUnder<fbs::Order>, Order>{}));
  ZuAssert((ZuIsSame<ZuUnder<Key>, Order>{}));
  auto key = ZuStructKey(order);
  std::cout << key << '\n' << json(key) << '\n';

  {
    IOBuilder fbb(new ZiIOBufAlloc<>());
    fbb.Finish(ZfbStruct::save(fbb, key));
    auto buf = fbb.buf();
    auto fbo = ZfbStruct::root<Order>(buf->data());
    std::cout << *fbo << '\n';
  }

  {
    ZuTuple<ZtString<>, uint64_t> k1{"IBM", 1};
    using Key = ZuStructKeyT<Order>;
    static auto fn = [](Key key) { std::cout << key << '\n'; };
    fn(k1);
  }
}
