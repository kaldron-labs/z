//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// mock data store for Zdb automated testing
// - based on in-memory data store
// - optionally defers work with a work queue
// - optionally defers callbacks with a callback queue

#ifndef ZdbMockStore_HH
#define ZdbMockStore_HH

#include <zlib/ZdbMemStore.hh>

namespace zdbtest {

using namespace Zdb_;

class Store;

class StoreTbl : public ZdbMem::StoreTbl {
public:
  StoreTbl(
    Store *store, IDString id, unsigned nShards,
    ZtVFieldArray fields, ZtVKeyFieldArray keyFields,
    const reflection::Schema *schema, IOBufAllocFn bufAllocFn
  ) : ZdbMem::StoreTbl{
    store, ZuMv(id), nShards,
    ZuMv(fields), ZuMv(keyFields), schema, ZuMv(bufAllocFn)
  } { }

  zdbtest::Store *store() const;
  auto count() { return ZdbMem::StoreTbl::count(); }

  void count(KeyID keyID, ZmRef<IOBuf>, CountFn);

  void select(
    bool selectRow, bool selectNext, bool inclusive,
    KeyID keyID, ZmRef<IOBuf>,
    unsigned limit, TupleFn);

  void find(KeyID keyID, ZmRef<IOBuf>, RowFn);

  void recover(Shard shard, UN, RowFn);

  void write(ZmRef<IOBuf>, CommitFn);
};

// --- mock data store

class Store : public ZdbMem::Store_<StoreTbl> {
  using Base = ZdbMem::Store_<StoreTbl>;

public:
  using Base::Base;

  void sync() {
    ZmBlock<>{}([this](auto wake) {
      run([wake = ZuMv(wake)]() mutable { wake(); });
    });
  }

  bool deferWork() const { return m_deferWork; }
  void deferWork(bool v) { m_deferWork = v; }
  void addWork(ZmFn<> fn) {
    if (m_deferWork)
      m_work.push(ZuMv(fn));
    else
      fn();
  }
  void performWork() {
    /* ZeLOG(Debug, ([n = m_work.count_()](auto &s) {
      s << "performWork() count=" << n;
    })); */
    while (auto fn = m_work.shift()) fn();
    sync();
  }

  bool deferCallbacks() const { return m_deferCallbacks; }
  void deferCallbacks(bool v) { m_deferCallbacks = v; }
  void addCallback(ZmFn<> fn) {
    if (m_deferCallbacks)
       m_callbacks.push(ZuMv(fn));
    else
      fn();
  }
  void performCallbacks() {
    /* ZeLOG(Debug, ([n = m_callbacks.count_()](auto &s) {
      s << "performCallbacks() count=" << n;
    })); */
    while (auto fn = m_callbacks.shift()) fn();
    sync();
  }

private:
  ZuDerive(Queue, (ZmQueue<ZmFn<>, ZmQueueLock<ZmPLock>>));

  bool		m_deferWork = false;
  bool		m_deferCallbacks = false;
  Queue		m_work;
  Queue		m_callbacks;
};

inline zdbtest::Store *StoreTbl::store() const
{
  return static_cast<zdbtest::Store *>(ZdbMem::StoreTbl::store());
}

inline void StoreTbl::count(
  KeyID keyID, ZmRef<IOBuf> buf, CountFn countFn)
{
  auto work_ = [
    this, keyID, buf = ZuMv(buf), countFn = ZuMv(countFn)
  ]() mutable {
    ZdbMem::StoreTbl::count(keyID, ZuMv(buf), ZuMv(countFn));
  };
  store()->addWork(ZuMv(work_));
}

inline void StoreTbl::select(
  bool selectRow, bool selectNext, bool inclusive,
  KeyID keyID, ZmRef<IOBuf> buf,
  unsigned limit, TupleFn tupleFn)
{
  // ZeLOG(Debug, "select() work enqueue");
  auto work_ = [
    this, selectRow, selectNext, inclusive,
    keyID, buf = ZuMv(buf), limit, tupleFn = ZuMv(tupleFn)
  ]() mutable {
    // ZeLOG(Debug, "select() work dequeue");
    ZdbMem::StoreTbl::select(
      selectRow, selectNext, inclusive,
      keyID, ZuMv(buf), limit, [
	this, tupleFn = ZuMv(tupleFn)
      ](TupleResult result) mutable {
	// ZeLOG(Debug, "select() callback enqueue");
	auto callback = [
	  tupleFn, result = ZuMv(result) // tupleFn is called repeatedly
	]() mutable {
	  // ZeLOG(Debug, "select() callback dequeue");
	  tupleFn(ZuMv(result));
	};
	store()->addCallback(ZuMv(callback));
      });
  };
  store()->addWork(ZuMv(work_));
}

inline void StoreTbl::find(
  KeyID keyID, ZmRef<IOBuf> buf, RowFn rowFn)
{
  // ZeLOG(Debug, "find() work enqueue");
  auto work_ = [
    this, keyID, buf = ZuMv(buf), rowFn = ZuMv(rowFn)
  ]() mutable {
    // ZeLOG(Debug, "find() work dequeue");
    ZdbMem::StoreTbl::find(keyID, ZuMv(buf), [
      this, rowFn = ZuMv(rowFn)
    ](RowResult result) mutable {
      // ZeLOG(Debug, "find() callback enqueue");
      auto callback = [
	rowFn = ZuMv(rowFn), result = ZuMv(result)
      ]() mutable {
	// ZeLOG(Debug, "find() callback dequeue");
	rowFn(ZuMv(result));
      };
      store()->addCallback(ZuMv(callback));
    });
  };
  store()->addWork(ZuMv(work_));
}

inline void StoreTbl::recover(Shard shard, UN un, RowFn rowFn) {
  // ZeLOG(Debug, "recover() work enqueue");
  auto work_ = [this, shard, un, rowFn = ZuMv(rowFn)]() mutable {
    // ZeLOG(Debug, "recover() work dequeue");
    ZdbMem::StoreTbl::recover(shard, un, [
      this, rowFn = ZuMv(rowFn)
    ](RowResult result) mutable {
      // ZeLOG(Debug, "recover() callback enqueue");
      auto callback = [
	rowFn = ZuMv(rowFn), result = ZuMv(result)
      ]() mutable {
	// ZeLOG(Debug, "recover() callback dequeue");
	rowFn(ZuMv(result));
      };
      store()->addCallback(ZuMv(callback));
    });
  };
  store()->addWork(ZuMv(work_));
}

inline void StoreTbl::write(ZmRef<IOBuf> buf, CommitFn commitFn) {
  // ZeLOG(Debug, "write() work enqueue");
  auto work_ = [
    this, buf = ZuMv(buf), commitFn = ZuMv(commitFn)
  ]() mutable {
    // ZeLOG(Debug, "write() work dequeue");
    ZdbMem::StoreTbl::write(ZuMv(buf), [
      this, commitFn = ZuMv(commitFn)
    ](ZmRef<IOBuf> buf, CommitResult result) mutable {
      // ZeLOG(Debug, "write() callback enqueue");
      auto callback = [
	commitFn = ZuMv(commitFn), buf = ZuMv(buf), result = ZuMv(result)
      ]() mutable {
	// ZeLOG(Debug, "write() callback dequeue");
	commitFn(ZuMv(buf), ZuMv(result));
      };
      store()->addCallback(ZuMv(callback));
    });
  };
  store()->addWork(ZuMv(work_));
}

} // zdbtest

#endif /* ZdbMockStore_HH */
