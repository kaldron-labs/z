//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZtcHub.hh>

namespace Ztc {
ZtEnumImplNS(HubState);
}

#include <zlib/ZmAssert.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZmRWLock.hh>
#include <zlib/ZmSingleton.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZmScratch.hh>

class ZtcHubMgr_ {
friend Ztc::HubMgr;

  using Key = ZuTuple<Ztc::LinkType::T, ZuID>;
  using Lock = ZmRWLock;
  using Guard = ZmGuard<Lock>;
  using ReadGuard = ZmReadGuard<Lock>;

  ZuDerive(Map,
    (ZmRBTreeKV<Key, Ztc::Hub *,
      ZmRBTreeUnique<true,
	ZmRBTreeLock<ZmNoLock,
	  ZmRBTreeHeapID<"Ztc.HubMgr">>>>));
  using Captures =
    ZtArray<Ztc::HubTelemetry,
      ZtArrayHeapID<"Ztc.HubMgr.Capture">>;

public:
  ~ZtcHubMgr_() {
    m_addFn = {};
    m_delFn = {};
    m_addLinkFn = {};
    m_delLinkFn = {};
    m_addPoolFn = {};
    m_delPoolFn = {};
    m_addQueueFn = {};
    m_delQueueFn = {};
  }

  static ZtcHubMgr_ *instance() {
    return
      ZmSingleton<ZtcHubMgr_,
	ZmSingletonCleanup<ZmCleanup::Library>>::instance();
  }

  void add(Ztc::Hub *hub) {
    Guard guard(m_watchLock);
    m_map.add(hub->telKey(), hub);
    if (m_addFn) m_addFn(hub);
  }
  void del(Ztc::Hub *hub) {
    Guard guard(m_watchLock);
    if (m_delFn) m_delFn(hub);
    m_map.del(hub->telKey());
  }

  void linkAdded(Ztc::Link *link) {
    Guard guard(m_watchLock);
    if (m_addLinkFn) m_addLinkFn(link);
    if (m_addQueueFn)
      link->allQueues(
	{[this](Ztc::Queue *queue) { m_addQueueFn(queue); }});
  }

  void linkDeleted(Ztc::Link *link) {
    Guard guard(m_watchLock);
    if (m_delQueueFn)
      link->allQueues(
	{[this](Ztc::Queue *queue) { m_delQueueFn(queue); }});
    if (m_delLinkFn) m_delLinkFn(link);
  }

  void poolAdded(Ztc::Pool *pool) {
    Guard guard(m_watchLock);
    if (m_addPoolFn) m_addPoolFn(pool);
    if (m_addQueueFn)
      pool->allQueues(
	{[this](Ztc::Queue *queue) { m_addQueueFn(queue); }});
  }

  void poolDeleted(Ztc::Pool *pool) {
    Guard guard(m_watchLock);
    if (m_delQueueFn)
      pool->allQueues(
	{[this](Ztc::Queue *queue) { m_delQueueFn(queue); }});
    if (m_delPoolFn) m_delPoolFn(pool);
  }

  unsigned all(Ztc::HubMgr::AllFn fn) const {
    ReadGuard guard(m_watchLock);
    unsigned n = 0;
    auto i = m_map.citer();
    while (auto hub = i.val()) {
      ++n;
      fn(hub);
    }
    return n;
  }

  void capture(Ztc::HubMgr::CaptureFn fn) const {
    ReadGuard guard(m_watchLock);
    unsigned count = m_map.count_();
    auto captures = ZmScratch(
      Ztc::HubTelemetry, count, Captures::VHeap);
    auto i = m_map.citer();
    while (auto hub = i.val()) {
      auto data = new (captures.push()) Ztc::HubTelemetry;
      hub->telemetry(*data);
    }
    guard.unlock();
    fn(captures.cspan());
  }

  void watch(
      Ztc::HubMgr::AddFn addFn,
      Ztc::HubMgr::DelFn delFn,
      Ztc::HubMgr::AddLinkFn addLinkFn,
      Ztc::HubMgr::DelLinkFn delLinkFn,
      Ztc::HubMgr::AddPoolFn addPoolFn,
      Ztc::HubMgr::DelPoolFn delPoolFn,
      Ztc::HubMgr::AddQueueFn addQueueFn,
      Ztc::HubMgr::DelQueueFn delQueueFn) {
    Guard guard(m_watchLock);
    ZmAssert(
      !m_addFn && !m_delFn &&
      !m_addLinkFn && !m_delLinkFn &&
      !m_addPoolFn && !m_delPoolFn &&
      !m_addQueueFn && !m_delQueueFn, return);
    m_addFn = ZuMv(addFn);
    m_delFn = ZuMv(delFn);
    m_addLinkFn = ZuMv(addLinkFn);
    m_delLinkFn = ZuMv(delLinkFn);
    m_addPoolFn = ZuMv(addPoolFn);
    m_delPoolFn = ZuMv(delPoolFn);
    m_addQueueFn = ZuMv(addQueueFn);
    m_delQueueFn = ZuMv(delQueueFn);
  }

  void unwatch() {
    Guard guard(m_watchLock);
    m_addFn = {};
    m_delFn = {};
    m_addLinkFn = {};
    m_delLinkFn = {};
    m_addPoolFn = {};
    m_delPoolFn = {};
    m_addQueueFn = {};
    m_delQueueFn = {};
  }

private:
  Lock &watchLock() { return m_watchLock; }

  Map			m_map;
  mutable Lock		m_watchLock;
  Ztc::HubMgr::AddFn	m_addFn;
  Ztc::HubMgr::DelFn	m_delFn;
  Ztc::HubMgr::AddLinkFn m_addLinkFn;
  Ztc::HubMgr::DelLinkFn m_delLinkFn;
  Ztc::HubMgr::AddPoolFn m_addPoolFn;
  Ztc::HubMgr::DelPoolFn m_delPoolFn;
  Ztc::HubMgr::AddQueueFn m_addQueueFn;
  Ztc::HubMgr::DelQueueFn m_delQueueFn;
};

void Ztc::HubMgr::add(Hub *hub)
{
  ZtcHubMgr_::instance()->add(hub);
}

void Ztc::HubMgr::del(Hub *hub)
{
  ZtcHubMgr_::instance()->del(hub);
}

void Ztc::Hub::linkAdded_(Link *link)
{
  ++m_nLinks;
  linkDownInc_();
  HubMgr::linkAdded_(link);
}

void Ztc::Hub::linkDeleted_(Link *link)
{
  HubMgr::linkDeleted_(link);
  --m_nLinks;
}

void Ztc::Hub::poolAdded_(Pool *pool)
{
  HubMgr::poolAdded_(pool);
}

void Ztc::Hub::poolDeleted_(Pool *pool)
{
  HubMgr::poolDeleted_(pool);
}

void Ztc::HubMgr::linkAdded_(Link *link)
{
  ZtcHubMgr_::instance()->linkAdded(link);
}

void Ztc::HubMgr::linkDeleted_(Link *link)
{
  ZtcHubMgr_::instance()->linkDeleted(link);
}

void Ztc::HubMgr::poolAdded_(Pool *pool)
{
  ZtcHubMgr_::instance()->poolAdded(pool);
}

void Ztc::HubMgr::poolDeleted_(Pool *pool)
{
  ZtcHubMgr_::instance()->poolDeleted(pool);
}

unsigned Ztc::HubMgr::all(AllFn fn)
{
  return ZtcHubMgr_::instance()->all(ZuMv(fn));
}

ZmRWLock &Ztc::HubMgr::watchLock_()
{
  return ZtcHubMgr_::instance()->watchLock();
}

void Ztc::HubMgr::capture(CaptureFn fn)
{
  ZtcHubMgr_::instance()->capture(ZuMv(fn));
}

void Ztc::HubMgr::watch(
    AddFn addFn, DelFn delFn,
    AddLinkFn addLinkFn, DelLinkFn delLinkFn,
    AddPoolFn addPoolFn, DelPoolFn delPoolFn,
    AddQueueFn addQueueFn, DelQueueFn delQueueFn)
{
  ZtcHubMgr_::instance()->watch(
    ZuMv(addFn), ZuMv(delFn),
    ZuMv(addLinkFn), ZuMv(delLinkFn),
    ZuMv(addPoolFn), ZuMv(delPoolFn),
    ZuMv(addQueueFn), ZuMv(delQueueFn));
}

void Ztc::HubMgr::unwatch()
{
  ZtcHubMgr_::instance()->unwatch();
}
