//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZtcHub.hh>

#include <zlib/ZmRBTree.hh>
#include <zlib/ZmSingleton.hh>

class ZtcHubMgr_ {
  using Key = ZuTuple<Ztc::LinkType::T, ZuID>;

  ZuDerive(Map,
    (ZmRBTreeKV<Key, Ztc::Hub *,
      ZmRBTreeUnique<true,
	ZmRBTreeLock<ZmPLock,
	  ZmRBTreeHeapID<"Ztc.HubMgr">>>>));

public:
  static ZtcHubMgr_ *instance() {
    return
      ZmSingleton<ZtcHubMgr_,
	ZmSingletonCleanup<ZmCleanup::Library>>::instance();
  }

  void add(Ztc::Hub *hub) { m_map.add(hub->telKey(), hub); }
  void del(Ztc::Hub *hub) { m_map.del(hub->telKey()); }

  void all(Ztc::HubMgr::AllFn fn) const {
    auto i = m_map.citer();
    while (auto hub = i.val()) fn(hub);
  }

private:
  Map	m_map;
};

void Ztc::HubMgr::add(Hub *hub)
{
  ZtcHubMgr_::instance()->add(hub);
}

void Ztc::HubMgr::del(Hub *hub)
{
  ZtcHubMgr_::instance()->del(hub);
}

void Ztc::HubMgr::all(AllFn fn)
{
  ZtcHubMgr_::instance()->all(ZuMv(fn));
}
