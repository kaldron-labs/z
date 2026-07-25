//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// hash table

#include <zlib/ZmHashMgr.hh>

#include <zlib/ZuDerive.hh>
#include <zlib/ZuArray.hh>

#include <zlib/ZmAlloc.hh>
#include <zlib/ZmSingleton.hh>

class ZmHashMgr_ : public ZmObject {
friend ZmHashMgr;
friend Ztc::HashMgr;

  ZuDerive(ID2Params,
    (ZmRBTreeKV<ZuID, ZmHashParams,
      ZmRBTreeUnique<true,
	ZmRBTreeHeapID<"ZmHashMgr_",
	  ZmRBTreeLock<ZmNoLock>>>>));

public:
  ZmHashMgr_() { }
  ~ZmHashMgr_() {
    // ZmAssert(!m_tables.count_(), return);
    ZmGuard<ZmPLock> guard(m_lock);
    auto i = m_tables.iter();
    if (ZuLikely(!i.count())) return;
    ZuCArray<80> buf;
    buf << "\nleaked hash tables:\n";
    std::cerr << buf << std::flush;
    while (auto tbl = i()) {
      if (tbl->refCount()) {
	Ztc::HashTelemetry data;
	tbl->telemetry(data);
	buf.length(0);
	buf << ZuBoxPtr(tbl).hex() << ' ' << data.id << '\n';
	std::cerr << buf << std::flush;
      }
      i.del(tbl);
    }
  }

private:
  static ZmHashMgr_ *instance() {
    return
      ZmSingleton<ZmHashMgr_,
	ZmSingletonCleanup<ZmCleanup::Library>>::instance();
  }

  void init(ZuCSpan id, const ZmHashParams &params) {
    ZmAssert(id.length() + 1 < ZuIDSize);
    ZmGuard<ZmPLock> guard(m_lock);
    if (ID2Params::Node *node = m_params.find(id))
      node->val() = params;
    else
      m_params.add(id, params);
  }
  ZmHashParams &params(ZuCSpan id, ZmHashParams &in) {
    ZmAssert(id.length() + 1 < ZuIDSize);
    {
      ZmGuard<ZmPLock> guard(m_lock);
      if (ID2Params::Node *node = m_params.find(id))
	in = node->val();
    }
    return in;
  }

  void add(ZmAnyHash *tbl) {
    ZmGuard<ZmPLock> guard(m_lock);
    m_tables.addNode(tbl);
  }

  void del(ZmAnyHash *tbl) {
    ZmGuard<ZmPLock> guard(m_lock);
    m_tables.delNode(tbl);
  }

  using Tables = ZmHashMgr_Tables;

  void all(Ztc::HashMgr::AllFn fn) {
    ZmRef<ZmAnyHash> tbl;
    {
      ZmGuard<ZmPLock> guard(m_lock);
      auto next = m_tables.minimum();
      while (next && !next->refCount())
	next = m_tables.citer<ZmRBTreeGreater>(
	  ZmAnyHash_PtrAxor(*tbl))();
      tbl = next;
    }
    while (tbl) {
      fn(tbl.ptr());
      {
	ZmGuard<ZmPLock> guard(m_lock);
	auto next = tbl.ptr();
	do {
	  next = m_tables.citer<ZmRBTreeGreater>(
	    ZmAnyHash_PtrAxor(*next))();
	} while (next && !next->refCount());
	tbl = next;
      }
    }
  }

  ZmPLock	m_lock;
  ID2Params	m_params;
  Tables	m_tables;
};

void ZmHashMgr::init(ZuCSpan id, const ZmHashParams &params)
{
  ZmHashMgr_::instance()->init(id, params);
}

void Ztc::HashMgr::all(AllFn fn)
{
  ZmHashMgr_::instance()->all(ZuMv(fn));
}

ZmHashParams &ZmHashMgr::params(ZuCSpan id, ZmHashParams &in)
{
  return ZmHashMgr_::instance()->params(id, in);
}

void ZmHashMgr::add(ZmAnyHash *tbl)
{
  ZmHashMgr_::instance()->add(tbl);
}

void ZmHashMgr::del(ZmAnyHash *tbl)
{
  ZmHashMgr_::instance()->del(tbl);
}
