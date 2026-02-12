//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuDerive.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuHash.hh>
#include <zlib/ZuObject.hh>

#include <zlib/ZmRef.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZuTime.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZmSingleton.hh>
#include <zlib/ZmSpecific.hh>

using namespace ZuTestUtil;

struct Order : public ZuObject {
  static unsigned IDAccessor(const Order *o) { return o->id; }
  Order(unsigned id_) : id(id_) { }
  unsigned id;
};

void dump(Order *o)
{
  log("order ID: ", o->id);}

ZuDerive(Orders,
  (ZmHash<ZmRef<Order>,
    ZmHashKey<Order::IDAccessor,
      ZmHashLock<ZmNoLock,
	ZmHashHeapID<"Orders">>>>));

int main(int argc, char **argv)
{
  parse(argc, argv);

  ZuTestMain();

  ZmHeapMgr::init("Orders", 0, ZmHeapConfig{100});
  ZmRef<Orders> orders = new Orders(ZmHashParams().bits(7).loadFactor(1.0));

  log("node size: ", sizeof(Orders::Node));
  for (unsigned i = 0; i < 100; i++) orders->add(new Order(i));
  ZuCheck(orders->count_() == 100);
  ZmRef<Order> o = orders->findVal(0);
  ZuCheck(o && o->id == 0);
  dump(o);
  ZuPtr<Orders::Node> n = orders->del(0);
  ZuCheck(n && n->val() && n->val()->id == 0);
  dump(n->val());
  n = nullptr;
  o = orders->delVal(1);
  ZuCheck(o && o->id == 1);
  dump(o);
  ZuCheck(orders->count_() == 98);

  // resize + deletion churn
  for (unsigned i = 0; i < 2048; i++)
    orders->add(new Order(1000 + i));
  ZuCheck(orders->count_() == 98 + 2048);

  bool churnOK = true;
  for (unsigned i = 0; i < 2048; i++) {
    auto d = orders->delVal(1000 + i);
    if (!(d && d->id == (1000 + i))) churnOK = false;
  }
  ZuCheck(churnOK);
  ZuCheck(orders->count_() == 98);
}
