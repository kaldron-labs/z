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

ZuAssert((ZuIsSame<
  typename Ztc::HeapMgr::AllFn::HeapID,
  typename Ztc::AllFnHeapID::HeapID>{}));
ZuAssert((ZuIsSame<
  typename Ztc::HashMgr::AllFn::HeapID,
  typename Ztc::AllFnHeapID::HeapID>{}));
ZuAssert((ZuIsSame<
  typename Ztc::ThreadMgr::AllFn::HeapID,
  typename Ztc::AllFnHeapID::HeapID>{}));
ZuAssert((ZuIsSame<
  typename Ztc::QueueMgr::AllFn::HeapID,
  typename Ztc::AllFnHeapID::HeapID>{}));
ZuAssert((ZuIsSame<
  typename Ztc::HeapMgr::AddFn::HeapID,
  typename Ztc::WatchFnHeapID::HeapID>{}));
ZuAssert((ZuIsSame<
  typename Ztc::HashMgr::DelFn::HeapID,
  typename Ztc::WatchFnHeapID::HeapID>{}));
ZuAssert((ZuIsSame<
  typename Ztc::ThreadMgr::AddFn::HeapID,
  typename Ztc::WatchFnHeapID::HeapID>{}));

template <typename Heap>
struct WatchAlloc_ : public Heap {
  uintptr_t value;
};
ZuDerive(WatchAlloc,
  (WatchAlloc_<ZmHeap<"Ztc.Watch.TestHeap", WatchAlloc_<ZuVoid>>>));

ZmHashKVDerive(WatchHash, unsigned, unsigned,
  (ZmHashHeapID<"Ztc.Watch.TestHash">));
ZmHashKVDerive(WatchDrainHash, unsigned, unsigned,
  (ZmHashHeapID<"Ztc.Watch.Drain">));

void managerGuards()
{
  ZuTestScope(managerGuards);
  unsigned guarded = 0;
  Ztc::HeapMgr::guard([&guarded]() { ++guarded; });
  Ztc::HashMgr::guard([&guarded]() { ++guarded; });
  Ztc::ThreadMgr::guard([&guarded]() { ++guarded; });
  ZuCheck(guarded == 3);
}

void mgrCapture()
{
  ZuTestScope(mgrCapture);

  auto alloc = new WatchAlloc{};
  Ztc::HeapMgr::capture(
    [](Ztc::Heap *heap) {
      return heap->telKey().p<0>() == "Ztc.Watch.TestHeap";
    },
    [](const auto &captures) {
      unsigned guarded = 0;
      Ztc::HeapMgr::guard([&guarded]() { ++guarded; });
      ZuCheck(guarded == 1);
      ZuCheck(captures.length() == 1);
      ZuCheck(captures[0].id == "Ztc.Watch.TestHeap");
    });
  delete alloc;

  WatchHash hash{ZmHashParams{"Ztc.Watch.TestHash"}};
  Ztc::HashMgr::capture(
    [](Ztc::Hash *hash_) {
      return hash_->telKey().p<0>() == "Ztc.Watch.TestHash";
    },
    [](const auto &captures) {
      unsigned guarded = 0;
      Ztc::HashMgr::guard([&guarded]() { ++guarded; });
      ZuCheck(guarded == 1);
      ZuCheck(captures.length() == 1);
      ZuCheck(captures[0].id == "Ztc.Watch.TestHash");
    });

  Ztc::ThreadMgr::capture({}, [](const auto &captures) {
    unsigned guarded = 0;
    Ztc::ThreadMgr::guard([&guarded]() { ++guarded; });
    ZuCheck(guarded == 1);
    ZuCheck(captures.length() >= 1);
  });
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
    {[&added, &adds](Ztc::Hash *hash) {
      Ztc::HashTelemetry data;
      hash->telemetry(data);
      if (data.id == "Ztc.Watch.TestHash") {
	added = hash;
	++adds;
      }
    }},
    {[&deleted, &dels](Ztc::Hash *hash) {
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
    {[&adds](Ztc::Thread *thread) {
      Ztc::ThreadTelemetry data;
      thread->telemetry(data);
      if (data.name == "ztcWatch") ++adds;
    }},
    {[&dels](Ztc::Thread *thread) {
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
    {[&entered, &release](Ztc::Hash *hash) {
      Ztc::HashTelemetry data;
      hash->telemetry(data);
      if (data.id != "Ztc.Watch.Drain") return;
      entered.post();
      release.wait();
    }},
    {});
  ZmThread producer{[]() {
    WatchDrainHash hash{ZmHashParams{"Ztc.Watch.Drain"}};
  }};
  entered.wait();
  ZmThread drainer{[&unwatchStarted, &unwatchDone]() {
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

void allRefs()
{
  ZuTestScope(allRefs);
  ZmRef<WatchHash> hash =
    new WatchHash{ZmHashParams{"Ztc.Watch.TestHash"}};
  bool seen = false;
  Ztc::HashMgr::all({[&hash, &seen](Ztc::Hash *table) {
    if (table->telKey().p<0>() != "Ztc.Watch.TestHash") return;
    ZuCheck(table == hash.ptr());
    ZuCheck(hash->refCount() >= 2);
    seen = true;
  }});
  ZuCheck(seen);
}

int main()
{
  ZuTestMain();
  ZuTestCall(managerGuards);
  ZuTestCall(heapWatch);
  ZuTestCall(mgrCapture);
  ZuTestCall(hashWatch);
  ZuTestCall(threadWatch);
  ZuTestCall(unwatchDrain);
  ZuTestCall(allRefs);
}
