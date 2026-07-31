//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZtcDB.hh>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZmRWLock.hh>
#include <zlib/ZmSingleton.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtScratch.hh>

class ZtcDBMgr_ {
friend Ztc::DBMgr;

  using Lock = ZmRWLock;
  using Guard = ZmGuard<Lock>;
  using ReadGuard = ZmReadGuard<Lock>;

  ZuDerive(Map,
    (ZmRBTree<Ztc::DB *,
      ZmRBTreeUnique<true,
	ZmRBTreeLock<ZmNoLock,
	  ZmRBTreeHeapID<"Ztc.DBMgr">>>>));
  using Captures =
    ZtArray<Ztc::DBTelemetry,
      ZtArrayHeapID<"Ztc.DBMgr.Capture">>;

public:
  ~ZtcDBMgr_() {
    m_addFn = {};
    m_delFn = {};
    m_addHostFn = {};
    m_delHostFn = {};
    m_addTableFn = {};
    m_delTableFn = {};
  }

  static ZtcDBMgr_ *instance() {
    return
      ZmSingleton<ZtcDBMgr_,
	ZmSingletonCleanup<ZmCleanup::Library>>::instance();
  }

  void add(Ztc::DB *db) {
    Guard guard(m_watchLock);
    m_map.add(db);
    if (m_addFn) m_addFn(db);
  }
  void del(Ztc::DB *db) {
    Guard guard(m_watchLock);
    if (m_delFn) m_delFn(db);
    m_map.del(db);
  }

  unsigned all(Ztc::DBMgr::AllFn fn) const {
    ReadGuard guard(m_watchLock);
    unsigned n = 0;
    auto i = m_map.citer();
    while (auto db = i.key()) {
      ++n;
      fn(db);
    }
    return n;
  }

  void capture(Ztc::DBMgr::CaptureFn fn) const {
    ReadGuard guard(m_watchLock);
    unsigned count = m_map.count_();
    auto captures = ZtScratch(Captures, count);
    auto i = m_map.citer();
    while (auto db = i.key()) {
      auto data = new (captures.push()) Ztc::DBTelemetry;
      db->telemetry(*data);
    }
    guard.unlock();
    fn(captures.cspan());
  }

  void watch(Ztc::DBMgr::AddFn addFn, Ztc::DBMgr::DelFn delFn) {
    Guard guard(m_watchLock);
    ZmAssert(!m_addFn && !m_delFn, return);
    m_addFn = ZuMv(addFn);
    m_delFn = ZuMv(delFn);
  }

  void unwatch() {
    Guard guard(m_watchLock);
    m_addFn = {};
    m_delFn = {};
    m_addHostFn = {};
    m_delHostFn = {};
    m_addTableFn = {};
    m_delTableFn = {};
  }

  void watch(
      Ztc::DBMgr::AddHostFn addHostFn,
      Ztc::DBMgr::DelHostFn delHostFn,
      Ztc::DBMgr::AddTableFn addTableFn,
      Ztc::DBMgr::DelTableFn delTableFn) {
    Guard guard(m_watchLock);
    ZmAssert(
      !m_addHostFn && !m_delHostFn &&
      !m_addTableFn && !m_delTableFn, return);
    m_addHostFn = ZuMv(addHostFn);
    m_delHostFn = ZuMv(delHostFn);
    m_addTableFn = ZuMv(addTableFn);
    m_delTableFn = ZuMv(delTableFn);
  }

  void hostAdded(Ztc::DBHost *host) {
    Guard guard(m_watchLock);
    if (m_addHostFn) m_addHostFn(host);
  }
  void hostDeleted(Ztc::DBHost *host) {
    Guard guard(m_watchLock);
    if (m_delHostFn) m_delHostFn(host);
  }
  void tableAdded(Ztc::DBTable *table) {
    Guard guard(m_watchLock);
    if (m_addTableFn) m_addTableFn(table);
  }
  void tableDeleted(Ztc::DBTable *table) {
    Guard guard(m_watchLock);
    if (m_delTableFn) m_delTableFn(table);
  }

private:
  Lock &watchLock() { return m_watchLock; }

  mutable Lock		m_watchLock;
  Map			m_map;
  Ztc::DBMgr::AddFn	m_addFn;
  Ztc::DBMgr::DelFn	m_delFn;
  Ztc::DBMgr::AddHostFn	m_addHostFn;
  Ztc::DBMgr::DelHostFn	m_delHostFn;
  Ztc::DBMgr::AddTableFn m_addTableFn;
  Ztc::DBMgr::DelTableFn m_delTableFn;
};

void Ztc::DB::hostAdded_(DBHost *host)
{
  DBMgr::hostAdded_(host);
}

void Ztc::DB::hostDeleted_(DBHost *host)
{
  DBMgr::hostDeleted_(host);
}

void Ztc::DB::tableAdded_(DBTable *table)
{
  DBMgr::tableAdded_(table);
}

void Ztc::DB::tableDeleted_(DBTable *table)
{
  DBMgr::tableDeleted_(table);
}

void Ztc::DBMgr::add(DB *db)
{
  ZtcDBMgr_::instance()->add(db);
}

void Ztc::DBMgr::del(DB *db)
{
  ZtcDBMgr_::instance()->del(db);
}

unsigned Ztc::DBMgr::all(AllFn fn)
{
  return ZtcDBMgr_::instance()->all(ZuMv(fn));
}

ZmRWLock &Ztc::DBMgr::watchLock_()
{
  return ZtcDBMgr_::instance()->watchLock();
}

void Ztc::DBMgr::capture(CaptureFn fn)
{
  ZtcDBMgr_::instance()->capture(ZuMv(fn));
}

void Ztc::DBMgr::watch(AddFn addFn, DelFn delFn)
{
  ZtcDBMgr_::instance()->watch(ZuMv(addFn), ZuMv(delFn));
}

void Ztc::DBMgr::watch(
    AddHostFn addHostFn, DelHostFn delHostFn,
    AddTableFn addTableFn, DelTableFn delTableFn)
{
  ZtcDBMgr_::instance()->watch(
    ZuMv(addHostFn), ZuMv(delHostFn),
    ZuMv(addTableFn), ZuMv(delTableFn));
}

void Ztc::DBMgr::unwatch()
{
  ZtcDBMgr_::instance()->unwatch();
}

void Ztc::DBMgr::hostAdded_(DBHost *host)
{
  ZtcDBMgr_::instance()->hostAdded(host);
}

void Ztc::DBMgr::hostDeleted_(DBHost *host)
{
  ZtcDBMgr_::instance()->hostDeleted(host);
}

void Ztc::DBMgr::tableAdded_(DBTable *table)
{
  ZtcDBMgr_::instance()->tableAdded(table);
}

void Ztc::DBMgr::tableDeleted_(DBTable *table)
{
  ZtcDBMgr_::instance()->tableDeleted(table);
}
