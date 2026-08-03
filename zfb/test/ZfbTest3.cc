//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>
#include <string>

#include <zlib/ZuBox.hh>
#include <zlib/ZuID.hh>
#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmDemangle.hh>

#include <zlib/ZfJSON.hh>

#include <zlib/Zfb.hh>
#include <zlib/ZfbStruct.hh>

#include <flatbuffers/reflection.h>
#include <flatbuffers/reflection_generated.h>

#include "ZfbTest3_fbs.h"

namespace zfbtest3 {

ZfbEnumNS(, Side, Buy, Sell)

ZtEnumImplNS(Side);

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
  ZiIP			ip;

  friend ZfStructPrint ZuPrintType(Order *);
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
  (((id), (Ctor<9>)), (String)),
  (((ip), (Ctor<10>)), (UDT)));

} // zfbtest3

using namespace ZuTestUtil;

#define CHECK(x) ZuCHECK((x), #x)

using IOBuilder = Zfb::IOBuilder;
using IOBuf = ZiIOBuf;

template <typename T>
ZtString<> json(const T &v) {
  ZtString<> s;
  ZfJSON::save<ZuFacet::JSON, ZfFieldFilter::All>(s, v);
  return s;
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  using namespace zfbtest3;

  Order order{
    "IBM", 42, "FIX0", "order0", 0, Side::Buy, 100, 100, "1-3",
    "abcdefghijklmnopqrstuvwxyz01",
    ZiIP{"2001:db8::1"}};

  {
    IOBuilder fbb(new ZiIOBufAlloc<>());
    //fbb.Finish(ZfbTransform::Object::save<ZuFacet::Core, ZfFieldFilter::Save>(fbb, order));
    fbb.Finish(ZfbStruct::save(fbb, order));
    auto buf = fbb.buf();
    auto fbo = ZfbStruct::root<Order>(buf->data());
    auto order_ = ZfbStruct::ctor<Order>(fbo);
    CHECK(order_.id == order.id);
    CHECK(order_.ip == order.ip);
  }

  using Key = ZuStructKeyT<Order>;
  using KeyFields = ZuFields<Key>;
  CHECK(KeyFields::N == 2);
  ZuAssert((ZuIsSame<ZuUnder<Order>, Order>{}));
  ZuAssert((ZuIsSame<ZuUnder<fbs::Order>, Order>{}));
  ZuAssert((ZuIsSame<ZuUnder<Key>, Order>{}));
  auto key = ZuStructKey(order);
  CHECK(key.p<0>() == "IBM");

  {
    IOBuilder fbb(new ZiIOBufAlloc<>());
    fbb.Finish(ZfbStruct::save(fbb, key));
    auto buf = fbb.buf();
    auto fbo = ZfbStruct::root<Order>(buf->data());
    CHECK(fbo != nullptr);
  }

  {
    ZuTuple<ZtString<>, uint64_t> k1{"IBM", 1};
    using Key = ZuStructKeyT<Order>;
    static auto fn = [](Key key) { return key.p<0>() == "IBM"; };
    CHECK(fn(k1));
  }
  return 0;
}
