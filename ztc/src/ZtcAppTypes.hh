//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// telemetry service application values

#ifndef ZtcAppTypes_HH
#define ZtcAppTypes_HH

#ifndef ZtcLib_HH
#include <zlib/ZtcLib.hh>
#endif

#include <zlib/ZuDerive.hh>
#include <zlib/ZuID.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuTime.hh>
#include <zlib/ZuTuple.hh>

#include <zlib/ZmEngine.hh>

#include <zlib/ZtString.hh>

#include <zlib/ZfStruct.hh>

#include <zlib/ZtcTypes.hh>

namespace Ztc {

using RequestFilter =
  ZtString<ZtStringHeapID<"Ztc.Request.Filter">>;

struct Request {
  RequestFilter	filter;
  ZuID		id;
  uint64_t	seqNo = ZuCmp<uint64_t>::null();
  uint64_t	alertSeqNo = 0;
  uint32_t	interval = 0;
  uint32_t	alertDate = 0;
  uint8_t	group = 0;
  bool		subscribe = false;

  friend ZfStructPrint ZuPrintType(Request *);
};

struct Ack {
  ZuID id;
  uint64_t seqNo = 0;
  uint32_t interval = 0;
  uint8_t status = 0;
};

struct EOS {
  ZuID id;
  uint64_t seqNo = 0;
};

using ErrorMessage =
  ZtString<ZtStringHeapID<"Ztc.Error.Message">>;

struct Error {
  ErrorMessage	message;
  ZuID		id;
  uint64_t	seqNo = ZuCmp<uint64_t>::null();
  int32_t	code = 0;

  friend ZfStructPrint ZuPrintType(Error *);
};

struct AppTelemetry {
  ZuID			version;
  ZuID			role;
  int64_t		startTime = 0;
  uint32_t		ztcver = Z_VERSION;
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

  ZuDerive(TelKey, (ZuTuple<uint32_t, uint64_t>));
  TelKey telKey() const { return {date, seqNo}; }

  friend ZfStructPrint ZuPrintType(AlertTelemetry *);
};

} // Ztc

#endif /* ZtcAppTypes_HH */
