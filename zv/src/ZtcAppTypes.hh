//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// telemetry service application values

#ifndef ZtcAppTypes_HH
#define ZtcAppTypes_HH

#ifndef ZvLib_HH
#include <zlib/ZvLib.hh>
#endif

#include <zlib/ZuID.hh>
#include <zlib/ZuTime.hh>
#include <zlib/ZuTuple.hh>

#include <zlib/ZmEngine.hh>

#include <zlib/ZtString.hh>

#include <zlib/ZfStruct.hh>

#include <zlib/ZtcTypes.hh>

namespace Ztc {

struct AppTelemetry {
  ZuID			version;
  ZuID			role;
  int64_t		startTime = 0;
  ZmEngineState::T	state = ZmEngineState::Stopped;
  bool			degraded = false;
  RAG::T		rag = RAG::Off;

  friend ZfStructPrint ZuPrintType(AppTelemetry *);
};

using AlertMessage =
  ZtString<ZtStringHeapID<"Ztc.App.AlertMsg">>;

struct AlertTelemetry {
  AlertMessage	message;
  ZuTime	time;
  uint64_t	seqNo = 0;
  uint64_t	tid = 0;
  uint32_t	date = 0;
  int8_t	severity = 0;

  auto telKey() const { return ZuFwdTuple(date, seqNo); }

  friend ZfStructPrint ZuPrintType(AlertTelemetry *);
};

} // Ztc

#endif /* ZtcAppTypes_HH */
