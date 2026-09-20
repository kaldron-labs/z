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
#include <zlib/ZmPLock.hh>
#include <zlib/ZmSemaphore.hh>

using namespace ZuTestUtil;

struct Order : public ZuObject {
  static unsigned IDAccessor(const Order *o) { return o->id; }
  Order(unsigned id_) : id(id_) { }
  unsigned id;
};

void dump(Order *o)
{
  log("order ID: ", o->id);}

// This test covers hashes that retain shared reference-counted values.
ZmHashDerive(Orders, ZmRef<Order>,
  (ZmHashKey<Order::IDAccessor,
    ZmHashLock<ZmNoLock,
      ZmHashHeapID<"Orders">>>));

using OrdersDefault = ZmHash<ZmRef<Order>, Orders_NTP>;
using OrdersExplicit = ZmHash<ZmRef<Order>, Orders_NTP, Orders_Node, Orders>;
ZuAssert((ZuIsSame<Orders::Impl, Orders>{}));
ZuAssert((ZuIsSame<Orders::Node, Orders_Node>{}));
ZuAssert((ZuIsSame<OrdersExplicit::Node, Orders::Node>{}));
ZuAssert((ZuIsSame<OrdersExplicit::NodeRef, Orders::NodeRef>{}));
ZuAssert((ZuIsSame<OrdersExplicit::NodeMvRef, Orders::NodeMvRef>{}));
ZuAssert((sizeof(Orders::Node) == sizeof(OrdersDefault::Node)));
ZuAssert((alignof(Orders::Node) == alignof(OrdersDefault::Node)));

static void concurrentCount()
{
  ZuTestScope(concurrentCount);
  using Hash = ZmHash<unsigned, ZmHashLock<ZmPLock,
    ZmHashHeapID<"ZmHashTest.Count">>>;
  // Enough overlap to exercise independent lock stripes without resizing.
  enum { PerThread = 8192 };
  ZmRef<Hash> hash = new Hash{ZmHashParams{}.bits(15).cBits(2).loadFactor(1.0)};
  auto run = [hash](bool remove) -> int {
    ZmSemaphore ready, go;
    unsigned removed0 = 0, removed1 = 0;
    auto work = [hash, remove, &ready, &go](unsigned start, unsigned &removed) {
      ready.post();
      go.wait();
      for (unsigned i = 0; i < PerThread; ++i) {
	unsigned id = start + i;
	if (remove) { if (hash->del(id)) ++removed; }
	else hash->add(id);
      }
    };
    ZmThread first{[&work, &removed0]() { work(0, removed0); }};
    ZmThread second{[&work, &removed1]() { work(PerThread, removed1); }};
    bool started = bool(first) && bool(second);
    if (first) ready.wait();
    if (second) ready.wait();
    go.post();
    go.post();
    if (first) first.join();
    if (second) second.join();
    if (!started) return -1;
    return remove ? int(removed0 + removed1) : 0;
  };
  ZuCheck(run(false) == 0);
  ZuCheck(hash->count_() == 2 * PerThread);
  unsigned visited = 0;
  {
    auto i = hash->citer();
    while (i()) ++visited;
  }
  ZuCheck(visited == 2 * PerThread);
  ZuCheck(run(true) == 2 * PerThread);
  ZuCheck(!hash->count_());
  {
    auto i = hash->citer();
    ZuCheck(!i());
  }
}

int main(int argc, char **argv)
{
  parse(argc, argv);

  ZuTestMain();
  ZuTestCall(concurrentCount);

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

  bool found = false;
  unsigned visited = 0;
  unsigned allHashes = Ztc::HashMgr::all(Ztc::HashMgr::AllFn{
    [&found, &visited](Ztc::Hash *hash) {
      ++visited;
      Ztc::HashTelemetry data;
      hash->telemetry(data);
      if (data.id == "Orders") found = true;
    }});
  ZuCheck(allHashes == visited);
  ZuCheck(found);
  log(Ztc::hashCSV());
}
