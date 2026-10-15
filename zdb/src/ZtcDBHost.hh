//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// generic database host telemetry

#ifndef ZtcDBHost_HH
#define ZtcDBHost_HH

#ifndef ZdbLib_HH
#include <zlib/ZdbLib.hh>
#endif

#include <zlib/ZuDerive.hh>
#include <zlib/ZuID.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuTuple.hh>

#include <zlib/ZtEnum.hh>

#include <zlib/ZiIP.hh>

#include <zlib/ZfStruct.hh>

#include <zlib/Zfb.hh>

#include <zlib/ZtcTypes.hh>
#include <zlib/ZtcRAGMap.hh>


namespace Ztc {

namespace RAG { Zfb::AsEnum<"Ztc.fbs.RAG"> Zfb_Fmt(Map *); }

ZtEnumNS(ZdbAPI, DBHostState, int8_t,
  Instantiated,
  Initialized,
  Electing,
  Active,
  Inactive,
  Stopping);
namespace DBHostState { Zfb::AsEnum<"Ztc.fbs.DBHostState"> Zfb_Fmt(Map *); }

struct DBHostTelemetry {
  ZiIP		ip;
  ZuID		dbID;		// primary key
  ZuID		id;		// primary key
  uint32_t	priority = 0;
  uint16_t	port = 0;
  DBHostState::T state = DBHostState::Instantiated;
  uint8_t	voted = 0;

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

  friend ZfStructPrint ZuPrintType(DBHostTelemetry *);
};
ZfStruct(ZdbAPI, (DBHostTelemetry, FB),
  (ip,		(Ctor<0>),					UDT),
  (dbID,	(Ctor<1>, Keys<0>),				String),
  (id,		(Ctor<2>, Keys<0>),				String),
  (priority,	(Ctor<3>),					UInt32),
  (state,	(Ctor<5>, Mutable, Enum<DBHostState::Map>),	Int8),
  (voted,	(Ctor<6>, Mutable, Series),			Bool),
  (port,	(Ctor<4>),					UInt16),
  ((rag, RdFn),	(Synthetic, Series, Enum<RAG::Map>),		Int8));

Zfb::AsTable<"Ztc.fbs.DBHostTelemetry"> Zfb_Fmt(DBHostTelemetry *);

struct DBHost {
  ZuDerive(TelKey, (ZuTuple<ZuCSpan, ZuCSpan>));
  virtual TelKey telKey() const = 0;
  virtual void telemetry(DBHostTelemetry &) const = 0;
};


} // Ztc

#endif /* ZtcDBHost_HH */
