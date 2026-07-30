//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmHash.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmThread.hh>

#include <zlib/ZtcQueue.hh>

using namespace ZuTestUtil;

static_assert(ZuIsSame<
  typename Ztc::HeapMgr::AllFn::HeapID,
  typename Ztc::AllFnHeapID::HeapID>{});
static_assert(ZuIsSame<
  typename Ztc::HashMgr::AllFn::HeapID,
  typename Ztc::AllFnHeapID::HeapID>{});
static_assert(ZuIsSame<
  typename Ztc::ThreadMgr::AllFn::HeapID,
  typename Ztc::AllFnHeapID::HeapID>{});
static_assert(ZuIsSame<
  typename Ztc::QueueMgr::AllFn::HeapID,
  typename Ztc::AllFnHeapID::HeapID>{});
static_assert(ZuIsSame<
  typename Ztc::HeapMgr::AddFn::HeapID,
  typename Ztc::WatchFnHeapID::HeapID>{});
static_assert(ZuIsSame<
  typename Ztc::HashMgr::DelFn::HeapID,
  typename Ztc::WatchFnHeapID::HeapID>{});
static_assert(ZuIsSame<
  typename Ztc::ThreadMgr::AddFn::HeapID,
  typename Ztc::WatchFnHeapID::HeapID>{});

template <typename Heap>
struct WatchAlloc_ : public Heap {
  uintptr_t value;
};
ZuDerive(WatchAlloc,
  (WatchAlloc_<ZmHeap<"Ztc.Watch.TestHeap", WatchAlloc_<ZuVoid>>>));

ZuDerive(WatchHash,
  (ZmHashKV<unsigned, unsigned,
    ZmHashHeapID<"Ztc.Watch.TestHash">>));
ZuDerive(WatchDrainHash,
  (ZmHashKV<unsigned, unsigned,
    ZmHashHeapID<"Ztc.Watch.Drain">>));

void managerGuards()
{
  ZuTestScope(managerGuards);
  unsigned guarded = 0;
  Ztc::HeapMgr::guard([&guarded]() { ++guarded; });
  Ztc::HashMgr::guard([&guarded]() { ++guarded; });
  Ztc::ThreadMgr::guard([&guarded]() { ++guarded; });
  ZuCheck(guarded == 3);
}

void heapWatch()
{
  ZuTestScope(heapWatch);
  unsigned adds = 0;
  Ztc::HeapMgr::watch(
    {[&adds](Ztc::Heap *heap) {
      Ztc::HeapTelemetry data;
      heap->telemetry(data);
      if (data.id == "Ztc.Watch.TestHeap") ++adds;
    }},
    {});
  auto alloc = new WatchAlloc{};
  delete alloc;
  Ztc::HeapMgr::unwatch();
  ZuCheck(adds == 1);
}

void hashWatch()
{
  ZuTestScope(hashWatch);
  unsigned adds = 0;
  unsigned dels = 0;
  Ztc::Hash *added = nullptr;
  Ztc::Hash *deleted = nullptr;
  Ztc::HashMgr::watch(
    {[&](Ztc::Hash *hash) {
      Ztc::HashTelemetry data;
      hash->telemetry(data);
      if (data.id == "Ztc.Watch.TestHash") {
	added = hash;
	++adds;
      }
    }},
    {[&](Ztc::Hash *hash) {
      Ztc::HashTelemetry data;
      hash->telemetry(data);
      if (data.id == "Ztc.Watch.TestHash") {
	deleted = hash;
	++dels;
      }
    }});
  {
    WatchHash hash{ZmHashParams{"Ztc.Watch.TestHash"}};
    ZuCheck(added == &hash);
    hash.add(1, 2);
  }
  Ztc::HashMgr::unwatch();
  ZuCheck(adds == 1);
  ZuCheck(dels == 1);
  ZuCheck(deleted == added);
}

void threadWatch()
{
  ZuTestScope(threadWatch);
  unsigned adds = 0;
  unsigned dels = 0;
  ZmSemaphore ran;
  Ztc::ThreadMgr::watch(
    {[&](Ztc::Thread *thread) {
      Ztc::ThreadTelemetry data;
      thread->telemetry(data);
      if (data.name == "ztcWatch") ++adds;
    }},
    {[&](Ztc::Thread *thread) {
      Ztc::ThreadTelemetry data;
      thread->telemetry(data);
      if (data.name == "ztcWatch") ++dels;
    }});
  ZmThread thread{
    [&ran]() { ran.post(); },
    ZmThreadParams{}.name("ztcWatch")};
  ran.wait();
  thread.join();
  Ztc::ThreadMgr::unwatch();
  ZuCheck(adds == 1);
  ZuCheck(dels == 1);
}

void unwatchDrain()
{
  ZuTestScope(unwatchDrain);
  ZmSemaphore entered;
  ZmSemaphore release;
  ZmSemaphore unwatchStarted;
  ZmSemaphore unwatchDone;
  Ztc::HashMgr::watch(
    {[&](Ztc::Hash *hash) {
      Ztc::HashTelemetry data;
      hash->telemetry(data);
      if (data.id != "Ztc.Watch.Drain") return;
      entered.post();
      release.wait();
    }},
    {});
  ZmThread producer{[&]() {
    WatchDrainHash hash{ZmHashParams{"Ztc.Watch.Drain"}};
  }};
  entered.wait();
  ZmThread drainer{[&]() {
    unwatchStarted.post();
    Ztc::HashMgr::unwatch();
    unwatchDone.post();
  }};
  unwatchStarted.wait();
  ZuCheck(unwatchDone.trywait() != 0);
  release.post();
  unwatchDone.wait();
  producer.join();
  drainer.join();
}

int main()
{
  ZuTestMain();
  ZuTestCall(managerGuards);
  ZuTestCall(heapWatch);
  ZuTestCall(hashWatch);
  ZuTestCall(threadWatch);
  ZuTestCall(unwatchDrain);
}
