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

  ZuDerive(ID2Params,
    (ZmRBTreeKV<ZmIDString, ZmHashParams,
      ZmRBTreeUnique<true,
	ZmRBTreeHeapID<"ZmHashMgr_",
	  ZmRBTreeLock<ZmNoLock>>>>));

public:
  ZmHashMgr_() { }
  ~ZmHashMgr_() {
    for (;;) {
      unsigned n = m_tables.count_();
      if (ZuLikely(!n)) return;
      auto buf = ZmAlloc(ZmAnyHash *, n);
      if (!buf) return;
      unsigned j = 0;
      bool overflow = false;
      {
	ZmGuard<ZmPLock> guard(m_lock);
	auto i = m_tables.iter();
	while (ZmAnyHash *tbl = i()) {
	  if (ZuUnlikely(j >= n)) {
	    overflow = true;
	    break;
	  }
	  tbl->ref2_();
	  buf[j++] = tbl;
	  i.del(tbl);
	}
      }
      for (unsigned k = 0; k < j; ++k) buf[k]->deref_();
      if (!overflow) return;
    }
  }

private:
  static ZmHashMgr_ *instance() {
    return
      ZmSingleton<ZmHashMgr_,
	ZmSingletonCleanup<ZmCleanup::Library>>::instance();
  }

  void init(ZuCSpan id, const ZmHashParams &params) {
    ZmAssert(id.length() + 1 < ZmIDStrSize);
    ZmGuard<ZmPLock> guard(m_lock);
    if (ID2Params::Node *node = m_params.find(id))
      node->val() = params;
    else
      m_params.add(id, params);
  }
  ZmHashParams &params(ZuCSpan id, ZmHashParams &in) {
    ZmAssert(id.length() + 1 < ZmIDStrSize);
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
    // deref, otherwise m_tables.add() prevents dtor from ever being called
    tbl->deref_();
  }

  void del(ZmAnyHash *tbl) {
    ZmGuard<ZmPLock> guard(m_lock);
    // double ref prevents m_tables.del() from recursing into dtor
    tbl->ref2_();
    if (!m_tables.del(ZmAnyHash_PtrAxor(*tbl))) tbl->deref_();
    tbl->deref_();
  }

  using Tables = ZmHashMgr_Tables;

  void all(ZmFn<void(ZmAnyHash *)> fn) {
    ZmRef<ZmAnyHash> tbl;
    {
      ZmGuard<ZmPLock> guard(m_lock);
      tbl = m_tables.minimum();
    }
    while (tbl) {
      fn(tbl);
      {
	ZmGuard<ZmPLock> guard(m_lock);
	tbl = m_tables.citer<ZmRBTreeGreater>(
	  ZmAnyHash_PtrAxor(*tbl))();
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

void ZmHashMgr::all(ZmFn<void(ZmAnyHash *)> fn)
{
  ZmHashMgr_::instance()->all(fn);
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
