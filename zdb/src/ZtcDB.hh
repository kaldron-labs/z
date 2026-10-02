//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// generic database telemetry

#ifndef ZtcDB_HH
#define ZtcDB_HH

#ifndef ZdbLib_HH
#include <zlib/ZdbLib.hh>
#endif

#include <zlib/ZuDerive.hh>
#include <zlib/ZuID.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuTuple.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZmFn_.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmRWLock.hh>

#include <zlib/ZfStruct.hh>

#include <zlib/ZfbStruct.hh>

#include <zlib/ZdbTypes.hh>

#include <zlib/ZtcDBTable.hh>
#include <zlib/ZtcDBHost.hh>
#include <zlib/ZtcRAGMap.hh>
#include <zlib/ZtcTypes.hh>

#include <zlib/ztc_db_fbs.h>

namespace Ztc {

using DBString = Zdb_::String;
using DBThreads = Zdb_::Threads;
using DBSIDs = Zdb_::SIDs;

struct DBMgr;

struct DBTelemetry {
  ZuID		thread;
  DBThreads	threads;
  uint32_t	nShards = 0;
  ZuID		self;
  ZuID		leader;
  ZuID		prev;
  ZuID		next;
  uint32_t	nCxns = 0;
  uint32_t	heartbeatFreq = 0;
  uint32_t	heartbeatTimeout = 0;
  uint32_t	reconnectFreq = 0;
  uint32_t	electionTimeout = 0;
  uint16_t	nTables = 0;
  uint8_t	nHosts = 0;
  uint8_t	nPeers = 0;
  DBHostState::T state = -1;
  uint8_t	active = 0;
  uint8_t	recovering = 0;
  uint8_t	replicating = 0;

  RAG::T rag() const {
    switch (state) {
      case DBHostState::Instantiated: return RAG::Off;
      case DBHostState::Active: return RAG::Green;
      case DBHostState::Initialized:
      case DBHostState::Electing:
      case DBHostState::Inactive:
      case DBHostState::Stopping: return RAG::Amber;
      default: return RAG::Off;
    }
  }
  void rag(RAG::T) { } // unused

  friend ZfStructPrint ZuPrintType(DBTelemetry *);
};
ZfbStruct(ZdbAPI, DBTelemetry,
    (((thread),		(Ctor<0>)),					String),
    (((threads),	(Ctor<1>)),					StringVec),
    (((nShards),	(Ctor<2>)),					UInt32),
    (((self),		(Ctor<3>, Keys<0>)),				String),
    (((leader),		(Ctor<4>, Mutable)),				String),
    (((prev),		(Ctor<5>, Mutable)),				String),
    (((next),		(Ctor<6>, Mutable)),				String),
    (((nCxns),		(Ctor<7>, Mutable, Series)),			UInt32),
    (((heartbeatFreq),	(Ctor<8>)),					UInt32),
    (((heartbeatTimeout), (Ctor<9>)),					UInt32),
    (((reconnectFreq),	(Ctor<10>)),					UInt32),
    (((electionTimeout), (Ctor<11>)),					UInt32),
    (((nTables),	(Ctor<12>)),					UInt16),
    (((nHosts),		(Ctor<13>)),					UInt8),
    (((nPeers),		(Ctor<14>)),					UInt8),
    (((state),		(Ctor<15>, Mutable, Enum<DBHostState::Map>)),	Int8),
    (((active),		(Ctor<16>, Mutable)),				UInt8),
    (((recovering),	(Ctor<17>, Mutable)),				UInt8),
    (((replicating),	(Ctor<18>, Mutable)),				UInt8),
    (((rag, RdFn),	(Synthetic, Series, Enum<RAG::Map>)),		Int8));

struct ZdbAPI DB {
  using AllDBTablesFn = ZmFn<void(DBTable *), AllFnHeapID>;
  using AllDBHostsFn = ZmFn<void(DBHost *), AllFnHeapID>;

  ZuDerive(TelKey, (ZuTuple<ZuCSpan>));
  virtual TelKey telKey() const = 0;
  virtual void telemetry(DBTelemetry &data) const = 0;
  virtual bool start() = 0;
  virtual bool stop() = 0;
  virtual unsigned allDBTables(AllDBTablesFn fn) const = 0;
  virtual unsigned allDBHosts(AllDBHostsFn fn) const = 0;

protected:
  static void hostAdded_(DBHost *);
  static void hostDeleted_(DBHost *);
  static void tableAdded_(DBTable *);
  static void tableDeleted_(DBTable *);
};

struct ZdbAPI DBMgr {
private:
  using WatchLock = ZmRWLock;
  using WatchGuard = ZmGuard<WatchLock>;

  static WatchLock &watchLock_();

public:
  using AllFn = ZmFn<void(DB *), AllFnHeapID>;
  using CaptureFn =
    ZmFn<void(ZuSpan<const DBTelemetry>), AllFnHeapID>;
  using AddFn = ZmFn<void(DB *), WatchFnHeapID>;
  using DelFn = ZmFn<void(DB *), WatchFnHeapID>;
  using AddHostFn = ZmFn<void(DBHost *), WatchFnHeapID>;
  using DelHostFn = ZmFn<void(DBHost *), WatchFnHeapID>;
  using AddTableFn = ZmFn<void(DBTable *), WatchFnHeapID>;
  using DelTableFn = ZmFn<void(DBTable *), WatchFnHeapID>;

  static void add(DB *);
  static void del(DB *);
  static unsigned all(AllFn);
  static void capture(CaptureFn);
  template <typename L> static void guard(L &&l) {
    WatchGuard guard(watchLock_());
    ZuFwd<L>(l)();
  }
  static void watch(
    AddFn, DelFn,
    AddHostFn, DelHostFn,
    AddTableFn, DelTableFn);
  static void unwatch();

private:
  friend struct DB;
  static void hostAdded_(DBHost *);
  static void hostDeleted_(DBHost *);
  static void tableAdded_(DBTable *);
  static void tableDeleted_(DBTable *);
};

} // Ztc

#endif /* ZtcDB_HH */
