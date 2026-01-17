//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z database telemetry

#ifndef ZdbTelemetry_HH
#define ZdbTelemetry_HH

#ifndef ZdbLib_HH
#include <zlib/ZdbLib.hh>
#endif

#include <zlib/ZuArray.hh>

#include <zlib/ZtString.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtStruct.hh>

#include <zlib/ZfbStruct.hh>

#include <zlib/ZvCf.hh>

#include <zlib/ZdbTypes.hh>

#include <zlib/zdb_telemetry_fbs.h>

namespace Zdb_::Tel {

namespace DBCacheMode = Zdb_::CacheMode;
namespace DBHostState = Zdb_::HostState;

// display sequence: 
//   name, cacheMode, cacheSize, count,
//   cacheLoads, cacheMisses, cacheEvictions,
//   nShards, threads, rag
struct DBTable {
  using Name = ZuCArray<28>;

  Name			name;				// primary key
  uint32_t		nShards = 0;
  ZvCfStringVec		threads;
  uint64_t		count = 0;			// dynamic
  uint64_t		cacheLoads = 0;			// dynamic (*)
  uint64_t		cacheMisses = 0;		// dynamic (*)
  uint64_t		cacheEvictions = 0;		// dynamic (*)
  uint32_t		cacheSize = 0;
  int8_t		cacheMode = -1;			// CacheMode

  ZvRAG::T rag() const {
    unsigned total = cacheLoads + cacheMisses;
    if (!total) return ZvRAG::Off;
    if (cacheMisses * 10 > (total<<3)) return ZvRAG::Red;
    if ((cacheMisses<<1) > total) return ZvRAG::Amber;
    return ZvRAG::Green;
  }
  void rag(ZvRAG::T) { } // unused

  friend ZtStructPrint ZuPrintType(DBTable *);
};
ZfbStruct(DBTable,
    (((name),		(Ctor<0>, Keys<0>)),			(String)),
    (((cacheMode),	(Ctor<7>, Enum<DBCacheMode::Map>)),	(Int8)),
    (((cacheSize),	(Ctor<6>)),				(UInt32)),
    (((count),		(Ctor<3>, Mutable, Series, Delta)),	(UInt64)),
    (((cacheLoads),	(Ctor<4>, Mutable, Series, Delta)),	(UInt64)),
    (((cacheMisses),	(Ctor<5>, Mutable, Series, Delta)),	(UInt64)),
    (((cacheEvictions),	(Ctor<5>, Mutable, Series, Delta)),	(UInt64)),
    (((nShards),	(Ctor<1>)),				(UInt32)),
    (((threads),	(Ctor<2>)),				(StringVec)),
    (((rag, RdFn),	(Synthetic, Series, Enum<ZvRAG::Map>)),	(Int8)));

// display sequence:
//   id, priority, state, voted, ip, port
struct DBHost {
  ZiIP		ip;
  ZuID		id;
  uint32_t	priority = 0;
  uint16_t	port = 0;
  int8_t	state = 0;// RAG: Instantiated - Red; Active - Green; * - Amber
  uint8_t	voted = 0;

  ZvRAG::T rag() const { return DBHostState::rag(state); }
  void rag(ZvRAG::T) { } // unused

  friend ZtStructPrint ZuPrintType(DBHost *);
};
ZfbStruct(DBHost,
    (((ip),		(Ctor<0>)),				(UDT)),
    (((id),		(Ctor<1>, Keys<0>)),			(UDT)),
    (((priority),	(Ctor<2>)),				(UInt32)),
    (((state),		(Ctor<4>, Mutable, Enum<DBHostState::Map>)), (Int8)),
    (((voted),		(Ctor<5>, Mutable, Series)),		(Bool)),
    (((port),		(Ctor<3>)),				(UInt16)),
    (((rag, RdFn),	(Synthetic, Series, Enum<ZvRAG::Map>)),	(Int8)));

// display sequence: 
//   self, leader, prev, next, state, active, recovering, replicating,
//   nTables, nHosts, nPeers, nCxns,
//   thread,
//   heartbeatFreq, heartbeatTimeout, reconnectFreq, electionTimeout
struct DB {
  ZmThreadName	thread;
  ZuID		self;			// primary key - host ID 
  ZuID		leader;			// host ID
  ZuID		prev;			// ''
  ZuID		next;			// ''
  uint32_t	nCxns = 0;
  uint32_t	heartbeatFreq = 0;
  uint32_t	heartbeatTimeout = 0;
  uint32_t	reconnectFreq = 0;
  uint32_t	electionTimeout = 0;
  uint16_t	nTables = 0;
  uint8_t	nHosts = 0;
  uint8_t	nPeers = 0;
  int8_t	state = -1;		// same as hosts[hostID].state
  uint8_t	active = 0;
  uint8_t	recovering = 0;
  uint8_t	replicating = 0;

  ZvRAG::T rag() const { return DBHostState::rag(state); }
  void rag(ZvRAG::T) { } // unused

  friend ZtStructPrint ZuPrintType(DB *);
};
ZfbStruct(DB,
    (((self),		(Ctor<2>)),				(UDT)),
    (((leader),		(Ctor<3>, Mutable)),			(UDT)),
    (((prev),		(Ctor<4>, Mutable)),			(UDT)),
    (((next),		(Ctor<5>, Mutable)),			(UDT)),
    (((state),		(Ctor<14>, Mutable, Enum<DBHostState::Map>)), (Int8)),
    (((active),		(Ctor<15>, Mutable)),			(UInt8)),
    (((recovering),	(Ctor<16>, Mutable)),			(UInt8)),
    (((replicating),	(Ctor<17>, Mutable)),			(UInt8)),
    (((nTables),	(Ctor<11>)),				(UInt16)),
    (((nHosts),		(Ctor<12>)),				(UInt8)),
    (((nPeers),		(Ctor<13>)),				(UInt8)),
    (((nCxns),		(Ctor<6>, Mutable, Series)),		(UInt32)),
    (((thread),		(Ctor<0>)),				(String)),
    (((heartbeatFreq),	(Ctor<7>)),				(UInt32)),
    (((heartbeatTimeout), (Ctor<8>)),				(UInt32)),
    (((reconnectFreq),	(Ctor<9>)),				(UInt32)),
    (((electionTimeout), (Ctor<10>)),				(UInt32)),
    (((rag, RdFn),	(Synthetic, Series, Enum<RAG::Map>)),	(Int8)));

} // Zdb_::Tel

#endif /* ZdbTelemetry_HH */
