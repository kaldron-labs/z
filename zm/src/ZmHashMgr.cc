//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// hash table

#include <zlib/ZmHashMgr.hh>

#include <zlib/ZuDerive.hh>
#include <zlib/ZuArray.hh>

#include <zlib/ZmAlloc.hh>
#include <zlib/ZmAssert.hh>
#include <zlib/ZmSingleton.hh>

class ZmHashMgr_ : public ZmObject {
friend ZmHashMgr;
friend Ztc::HashMgr;

  using Lock = ZmPLock;
  using Guard = ZmGuard<Lock>;

  ZuDerive(ID2Params,
    (ZmRBTreeKV<ZuID, ZmHashParams,
      ZmRBTreeUnique<true,
	ZmRBTreeHeapID<"ZmHashMgr_",
	  ZmRBTreeLock<ZmNoLock>>>>));

public:
  ZmHashMgr_() { }
  ~ZmHashMgr_() {
    // ZmAssert(!m_tables.count_(), return);
    m_addFn = {};
    m_delFn = {};
    Guard guard(m_lock);
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
    Guard guard(m_lock);
    if (ID2Params::Node *node = m_params.find(id))
      node->val() = params;
    else
      m_params.add(id, params);
  }
  ZmHashParams &params(ZuCSpan id, ZmHashParams &in) {
    ZmAssert(id.length() + 1 < ZuIDSize);
    {
      Guard guard(m_lock);
      if (ID2Params::Node *node = m_params.find(id))
	in = node->val();
    }
    return in;
  }

  void add(ZmAnyHash *tbl) {
    Guard guard(m_lock);
    m_tables.addNode(tbl);
    if (m_addFn) m_addFn(tbl);
  }

  void del(ZmAnyHash *tbl) {
    Guard guard(m_lock);
    if (m_delFn) m_delFn(tbl);
    m_tables.delNode(tbl);
  }

  using Tables = ZmHashMgr_Tables;

  unsigned all(Ztc::HashMgr::AllFn fn) {
    unsigned n = 0;
    ZmRef<ZmAnyHash> tbl;
    {
      Guard guard(m_lock);
      auto next = m_tables.minimum();
      while (next && !next->refCount())
	next = m_tables.citer<ZmRBTreeGreater>(
	  ZmAnyHash_PtrAxor(*next))();
      tbl = next;
    }
    while (tbl) {
      ++n;
      fn(tbl);
      {
	Guard guard(m_lock);
	auto next = tbl.ptr();
	do {
	  next = m_tables.citer<ZmRBTreeGreater>(
	    ZmAnyHash_PtrAxor(*next))();
	} while (next && !next->refCount());
	tbl = next;
      }
    }
    return n;
  }

  void capture(
      Ztc::HashMgr::MatchFn match, Ztc::HashMgr::CaptureFn fn) {
    Guard guard(m_lock);
    unsigned count = m_tables.count_();
    auto storage = ZmAlloc(Ztc::HashTelemetry, count);
    unsigned length = 0;
    auto i = m_tables.iter();
    while (auto table = i()) {
      if (match && !match(table)) continue;
      auto data = new (&storage[length++]) Ztc::HashTelemetry;
      table->telemetry(*data);
    }
    guard.unlock();
    fn(ZuSpan<const Ztc::HashTelemetry>{storage.ptr, length});
    for (unsigned i = 0; i < length; ++i)
      storage[i].~HashTelemetry();
  }

  void watch(Ztc::HashMgr::AddFn addFn, Ztc::HashMgr::DelFn delFn) {
    Guard guard(m_lock);
    ZmAssert(!m_addFn && !m_delFn, return);
    m_addFn = ZuMv(addFn);
    m_delFn = ZuMv(delFn);
  }

  void unwatch() {
    Guard guard(m_lock);
    m_addFn = {};
    m_delFn = {};
  }

  Lock &watchLock() { return m_lock; }

  Lock		m_lock;
  ID2Params	m_params;
  Tables	m_tables;
  Ztc::HashMgr::AddFn	m_addFn;
  Ztc::HashMgr::DelFn	m_delFn;
};

void ZmHashMgr::init(ZuCSpan id, const ZmHashParams &params)
{
  ZmHashMgr_::instance()->init(id, params);
}

unsigned Ztc::HashMgr::all(AllFn fn)
{
  return ZmHashMgr_::instance()->all(ZuMv(fn));
}

ZmPLock &Ztc::HashMgr::watchLock_()
{
  return ZmHashMgr_::instance()->watchLock();
}

void Ztc::HashMgr::capture(MatchFn match, CaptureFn fn)
{
  ZmHashMgr_::instance()->capture(ZuMv(match), ZuMv(fn));
}

void Ztc::HashMgr::watch(AddFn addFn, DelFn delFn)
{
  ZmHashMgr_::instance()->watch(ZuMv(addFn), ZuMv(delFn));
}

void Ztc::HashMgr::unwatch()
{
  ZmHashMgr_::instance()->unwatch();
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
