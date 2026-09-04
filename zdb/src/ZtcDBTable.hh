//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// generic database table telemetry

#ifndef ZtcDBTable_HH
#define ZtcDBTable_HH

#ifndef ZdbLib_HH
#include <zlib/ZdbLib.hh>
#endif

#include <zlib/ZuID.hh>
#include <zlib/ZuTuple.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZfStruct.hh>

#include <zlib/ZfbStruct.hh>

#include <zlib/ZtcTypes.hh>
#include <zlib/ZtcRAGMap.hh>

#include <zlib/ztc_db_fbs.h>

namespace Ztc {

ZfbEnumNS(ZdbAPI, DBCacheMode, Normal, All);

using DBTableID =
  ZtString<ZtStringHeapID<"Ztc.DBTableID">>;
using DBTableKey = ZuTuple<const ZuID &, DBTableID>;

struct DBTableTelemetry {
  ZuID			dbID;		// primary key
  DBTableID		id;		// primary key
  ZtArray<ZtString<>>	threads;
  uint64_t		count = 0;
  uint64_t		cacheLoads = 0;
  uint64_t		cacheMisses = 0;
  uint64_t		cacheEvictions = 0;
  uint32_t		nShards = 0;
  uint32_t		cacheSize = 0;
  DBCacheMode::T	cacheMode = -1;

  RAG::T rag() const {
    uint64_t total = cacheLoads + cacheMisses;
    if (!total) return RAG::Off;
    if (cacheMisses * 10 > (total<<3)) return RAG::Red;
    if ((cacheMisses<<1) > total) return RAG::Amber;
    return RAG::Green;
  }
  void rag(RAG::T) { } // unused

  friend ZfStructPrint ZuPrintType(DBTableTelemetry *);
};
ZfbStruct(DBTableTelemetry,
    (((dbID),		(Ctor<0>, Keys<0>)),			(String)),
    (((id),		(Ctor<1>, Keys<0>)),			(String)),
    (((cacheMode),	(Ctor<9>, Enum<DBCacheMode::Map>)),	(Int8)),
    (((cacheSize),	(Ctor<8>)),				(UInt32)),
    (((count),		(Ctor<3>, Mutable, Series, Delta)),	(UInt64)),
    (((cacheLoads),	(Ctor<4>, Mutable, Series, Delta)),	(UInt64)),
    (((cacheMisses),	(Ctor<5>, Mutable, Series, Delta)),	(UInt64)),
    (((cacheEvictions),	(Ctor<6>, Mutable, Series, Delta)),	(UInt64)),
    (((nShards),	(Ctor<7>)),				(UInt32)),
    (((threads),	(Ctor<2>)),				(StringVec)),
    (((rag, RdFn),	(Synthetic, Series, Enum<RAG::Map>)),	(Int8)));

struct DBTable {
  virtual DBTableKey telKey() const = 0;
  virtual void telemetry(DBTableTelemetry &) const = 0;
};

} // Ztc

#endif /* ZtcDBTable_HH */
