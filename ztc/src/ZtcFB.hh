//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// FlatBuffers metadata for telemetry service values

#ifndef ZtcFB_HH
#define ZtcFB_HH

#ifndef ZtcLib_HH
#include <zlib/ZtcLib.hh>
#endif

#include <zlib/ZuAssert.hh>
#include <zlib/ZuStruct.hh>

#include <zlib/Zfb.hh>

#include <zlib/ZiMultiplex.hh>

#include <zlib/ZtcHeap.hh>
#include <zlib/ZtcHash.hh>
#include <zlib/ZtcThread.hh>
#include <zlib/ZtcQueue.hh>
#include <zlib/ZtcMx.hh>
#include <zlib/ZtcHub.hh>
#include <zlib/ZtcAppTypes.hh>
#include <zlib/ZtcRAGMap.hh>

#include <zlib/ZvThreadParams.hh>

// SemVer retains the packed uint32 wire representation.
namespace Ztc {
  struct SemVerMap {
    using T = uint32_t;
    enum { Code = ZfFieldTC::UInt32 };
    static constexpr uint32_t save(ZuSemVer v) { return v.value(); }
    static constexpr ZuSemVer load(uint32_t v) { return ZuSemVer{v}; }
  };
}
Ztc::SemVerMap ZfbTransform(ZuSemVer *);
inline ZuCSpan ZfVFieldTypeID(ZuSemVer *) { return "SemVer"; }

namespace ZmThreadPriority {
  Zfb::AsEnum<"Ztc.fbs.ThreadPriority"> Zfb_Fmt(Map *);
}
namespace ZiCxnFlags {
  Zfb::AsEnum<"Ztc.fbs.CxnFlags"> Zfb_Fmt(Map *);
}

namespace Ztc {

namespace RAG {
  Zfb::AsEnum<"Ztc.fbs.RAG"> Zfb_Fmt(Map *);
}

ZtEnumNS(ZtcAPI, EngineState, int8_t,
  Stopped, Starting, Running, Stopping, StartPending, StopPending);
ZtEnumNS(ZtcAPI, CxnType, int8_t, TCPIn, TCPOut, UDP);

ZuAssert(EngineState::Stopped == ZmEngineState::Stopped);
ZuAssert(EngineState::Starting == ZmEngineState::Starting);
ZuAssert(EngineState::Running == ZmEngineState::Running);
ZuAssert(EngineState::Stopping == ZmEngineState::Stopping);
ZuAssert(EngineState::StartPending == ZmEngineState::StartPending);
ZuAssert(EngineState::StopPending == ZmEngineState::StopPending);
ZuAssert(CxnType::TCPIn == ZiCxnType::TCPIn);
ZuAssert(CxnType::TCPOut == ZiCxnType::TCPOut);
ZuAssert(CxnType::UDP == ZiCxnType::UDP);

namespace EngineState { Zfb::AsEnum<"Ztc.fbs.EngineState"> Zfb_Fmt(Map *); }
namespace CxnType { Zfb::AsEnum<"Ztc.fbs.CxnType"> Zfb_Fmt(Map *); }

namespace LinkType {
  Zfb::AsEnum<"Ztc.fbs.LinkType"> Zfb_Fmt(Map *);
}
namespace LinkState {
  Zfb::AsEnum<"Ztc.fbs.LinkState"> Zfb_Fmt(Map *);
}
namespace PoolState {
  Zfb::AsEnum<"Ztc.fbs.PoolState"> Zfb_Fmt(Map *);
}

ZtEnumNS(ZtcAPI, Group, uint8_t, Heap, Hash, Thread, Mx, Queue, Hub, DB, App, Alert);
namespace Group { Zfb::AsEnum<"Ztc.fbs.Group"> Zfb_Fmt(Map *); }
ZtEnumNS(ZtcAPI, AckStatus, int8_t, OK, Invalid, Unsupported, Failed);
namespace AckStatus { Zfb::AsEnum<"Ztc.fbs.AckStatus"> Zfb_Fmt(Map *); }

ZfStruct(ZtcAPI, (Request, FB),
  (seqNo,	(Ctor<2>),			UInt64),
  (group,	(Ctor<6>, Enum<Group::Map>),	UInt8),
  (filter,	(Ctor<0>),			String),
  (interval,	(Ctor<4>),			UInt32),
  (subscribe,	(Ctor<7>),			Bool),
  (alertDate,	(Ctor<5>),			UInt32),
  (alertSeqNo,	(Ctor<3>),			UInt64),
  (id,		(Ctor<1>),			String));
Zfb::AsTable<"Ztc.fbs.Request"> Zfb_Fmt(Request *);

ZfStruct(ZtcAPI, (Ack, FB),
  (id,		(Ctor<0>),				String),
  (seqNo,	(Ctor<1>),				UInt64),
  (status,	(Ctor<3>, Enum<AckStatus::Map>),	UInt8),
  (interval,	(Ctor<2>),				UInt32));
Zfb::AsTable<"Ztc.fbs.Ack"> Zfb_Fmt(Ack *);

ZfStruct(ZtcAPI, (EOS, FB),
  (id,		(Ctor<0>),	String),
  (seqNo,	(Ctor<1>),	UInt64));
Zfb::AsTable<"Ztc.fbs.EOS"> Zfb_Fmt(EOS *);

ZfStruct(ZtcAPI, (Error, FB),
  (id,		(Ctor<1>),	String),
  (seqNo,	(Ctor<2>),	UInt64),
  (code,	(Ctor<3>),	Int32),
  (message,	(Ctor<0>),	String));
Zfb::AsTable<"Ztc.fbs.Error"> Zfb_Fmt(Error *);

// Compare ordered PK types; FB field adapters expose strings as spans.
ZfStruct(ZtcAPI, (HeapTelemetry, FB),
  (id,			(Ctor<0>, Keys<0>),			String),
  (partition,		(Ctor<12>, Keys<0>),			UInt16),
  (vshift,		(Ctor<14>),				UInt8),
  (size,		(Ctor<11>, Keys<0>),			UInt32),
  (alignment,		(Ctor<13>, Keys<0>),			UInt16),
  (sharded,		(Ctor<15>, Keys<0>),			Bool),
  (cacheSize,		(Ctor<1>),				UInt64),
  (cpuset,		(Ctor<2>),				UDT),
  (cacheAllocs,		(Ctor<3>, Mutable, Series, Delta),	UInt64),
  (cacheFrees,		(Ctor<4>, Mutable, Series, Delta),	UInt64),
  (crossFrees,		(Ctor<5>, Mutable, Series, Delta),	UInt64),
  (heapAllocs,		(Ctor<6>, Mutable, Series, Delta),	UInt64),
  (heapFrees,		(Ctor<7>, Mutable, Series, Delta),	UInt64),
  (globalHeapAllocs,	(Ctor<8>, Mutable, Series, Delta),	UInt64),
  (globalHeapFrees,	(Ctor<9>, Mutable, Series, Delta),	UInt64),
  (globalHeapMax,	(Ctor<10>, Mutable, Series),		UInt64),
  ((allocated, RdFn),	(Synthetic, Series),			UInt64),
  ((rag, RdFn),		(Synthetic, Series, Enum<RAG::Map>),	Int8));
Zfb::AsTable<"Ztc.fbs.HeapTelemetry"> Zfb_Fmt(HeapTelemetry *);

ZuAssert((ZuIsSame<
  ZuStructKeyT<Zfb::View<HeapTelemetry>>::Types,
  decltype(ZuDeclVal<const Heap &>().telKey())::Types>{}));

ZfStruct(ZtcAPI, (HashTelemetry, FB),
  (id,			(Ctor<0>, Keys<0>),			String),
  (addr,		(Ctor<1>, Keys<0>, Hex),		UInt64),
  (shadow,		(Ctor<11>),				UInt8),
  (linear,		(Ctor<10>),				UInt8),
  (bits,		(Ctor<8>),				UInt8),
  (cBits,		(Ctor<9>),				UInt8),
  (loadFactor,		(Ctor<2>),				Float),
  (nodeSize,		(Ctor<6>),				UInt32),
  (count,		(Ctor<4>, Mutable, Series),		UInt64),
  (maxCount,		(Ctor<5>, Mutable, Series),		UInt64),
  (effLoadFactor,	(Ctor<3>, Mutable, Series, NDP<2>),	Float),
  (resized,		(Ctor<7>),				UInt32),
  ((rag, RdFn),		(Synthetic, Series, Enum<RAG::Map>),	Int8));
Zfb::AsTable<"Ztc.fbs.HashTelemetry"> Zfb_Fmt(HashTelemetry *);

ZuAssert((ZuIsSame<
  ZuStructKeyT<Zfb::View<HashTelemetry>>::Types,
  decltype(ZuDeclVal<const Hash &>().telKey())::Types>{}));

ZfStruct(ZtcAPI, (ThreadTelemetry, FB),
  (name,	(Ctor<0>),					String),
  (tid,		(Ctor<1>, Keys<0>),				UInt64),
  (stackSize,	(Ctor<2>),					UInt64),
  (cpuset,	(Ctor<3>),					UDT),
  (cpuUsage,	(Ctor<4>, Mutable, Series, NDP<2>),		Float),
  (allocStack,	(Ctor<5>, Mutable, Series),			UInt64),
  (allocHeap,	(Ctor<6>, Mutable, Series),			UInt64),
  (sysPriority,	(Ctor<7>),					Int32),
  (sid,		(Ctor<8>),					UInt16),
  (partition,	(Ctor<9>),					UInt16),
  (priority,	(Ctor<10>, Enum<ZmThreadPriority::Map>),	Int8),
  (main,	(Ctor<11>),					Bool),
  (detached,	(Ctor<12>),					Bool),
  ((rag, RdFn),	(Synthetic, Series, Enum<RAG::Map>),		Int8));
Zfb::AsTable<"Ztc.fbs.ThreadTelemetry"> Zfb_Fmt(ThreadTelemetry *);

ZuAssert((ZuIsSame<
  ZuStructKeyT<Zfb::View<ThreadTelemetry>>::Types,
  decltype(ZuDeclVal<const Thread &>().telKey())::Types>{}));

ZfStruct(ZtcAPI, (CxnTelemetry, FB),
  (mxID,	(Ctor<0>, Keys<0>),			String),
  (remoteIP,	(Ctor<17>, Keys<0>),			UDT),
  (remotePort,	(Ctor<19>, Keys<0>),			UInt16),
  (localIP,	(Ctor<16>, Keys<0>),			UDT),
  (localPort,	(Ctor<18>, Keys<0>),			UInt16),
  (socket,	(Ctor<1>),				UInt64),
  (rxCalls,	(Ctor<2>, Mutable, Series, Delta),	UInt64),
  (rxBytes,	(Ctor<3>, Mutable, Series, Delta),	UInt64),
  (txCalls,	(Ctor<4>, Mutable, Series, Delta),	UInt64),
  (txBytes,	(Ctor<5>, Mutable, Series, Delta),	UInt64),
  (rxBufSize,	(Ctor<6>),				UInt32),
  (rxBufLen,	(Ctor<7>, Mutable, Series),		UInt32),
  (txBufSize,	(Ctor<8>),				UInt32),
  (txBufLen,	(Ctor<9>, Mutable, Series),		UInt32),
  (mreqAddr,	(Ctor<10>),				UDT),
  (mreqIf,	(Ctor<11>),				UDT),
  (mreqIfIndex,	(Ctor<12>),				UInt32),
  (mif,		(Ctor<13>),				UDT),
  (mifIndex,	(Ctor<14>),				UInt32),
  (ttl,		(Ctor<15>),				UInt32),
  (flags,	(Ctor<20>, Flags<ZiCxnFlags::Map>),	UInt8),
  (type,	(Ctor<21>, Enum<CxnType::Map>),		Int8),
  ((rag, RdFn),	(Synthetic, Series, Enum<RAG::Map>),	Int8));
Zfb::AsTable<"Ztc.fbs.CxnTelemetry"> Zfb_Fmt(CxnTelemetry *);

ZuAssert((ZuIsSame<
  ZuStructKeyT<Zfb::View<CxnTelemetry>>::Types,
  decltype(ZuDeclVal<const Connection &>().telKey())::Types>{}));

ZfStruct(ZtcAPI, (MxTelemetry, FB),
  (id,		(Ctor<0>, Keys<0>),				String),
  (stackSize,	(Ctor<1>),					UInt32),
  (queueSize,	(Ctor<2>),					UInt32),
  (spin,	(Ctor<3>),					UInt32),
  (timeout,	(Ctor<4>),					UInt32),
  (rxBufSize,	(Ctor<5>),					UInt32),
  (txBufSize,	(Ctor<6>),					UInt32),
  (rxThread,	(Ctor<7>),					UInt16),
  (txThread,	(Ctor<8>),					UInt16),
  (partition,	(Ctor<9>),					UInt16),
  (state,	(Ctor<10>, Mutable, Enum<EngineState::Map>),	Int8),
  (ll,		(Ctor<11>),					UInt8),
  (priority,	(Ctor<12>),					UInt8),
  (nThreads,	(Ctor<13>),					UInt8),
  ((rag, RdFn),	(Synthetic, Series, Enum<RAG::Map>),		Int8));
Zfb::AsTable<"Ztc.fbs.MxTelemetry"> Zfb_Fmt(MxTelemetry *);

ZuAssert((ZuIsSame<
  ZuStructKeyT<Zfb::View<MxTelemetry>>::Types,
  decltype(ZuDeclVal<const Mx &>().telKey())::Types>{}));

namespace QueueType {
  ZtEnumMap(ZtcAPI, QueueType, Map, "Thread", "IPC", "Rx", "Tx");
  Zfb::AsEnum<"Ztc.fbs.QueueType"> Zfb_Fmt(Map *);
}

ZfStruct(ZtcAPI, (QueueTelemetry, FB),
  (ownerID,	(Ctor<0>, Keys<0>),				String),
  (id,		(Ctor<1>, Keys<0>),				String),
  (type,	(Ctor<9>, Keys<0>, Enum<QueueType::Map>),	Int8),
  (inBytes,	(Ctor<2>, Mutable, Series, Delta),		UInt64),
  (outBytes,	(Ctor<3>, Mutable, Series, Delta),		UInt64),
  (inCount,	(Ctor<4>, Mutable, Series, Delta),		UInt64),
  (outCount,	(Ctor<5>, Mutable, Series, Delta),		UInt64),
  (count,	(Ctor<6>, Mutable, Series),			UInt64),
  (size,	(Ctor<7>),					UInt32),
  (full,	(Ctor<8>, Mutable, Series, Delta),		UInt32),
  ((rag, RdFn),	(Synthetic, Series, Enum<RAG::Map>),		Int8));
Zfb::AsTable<"Ztc.fbs.QueueTelemetry"> Zfb_Fmt(QueueTelemetry *);

ZuAssert((ZuIsSame<
  ZuStructKeyT<Zfb::View<QueueTelemetry>>::Types,
  decltype(ZuDeclVal<const Queue &>().telKey())::Types>{}));

ZfStruct(ZtcAPI, (HubTelemetry, FB),
  (linkType,	(Ctor<11>, Keys<0>, Enum<LinkType::Map>),	Int8),
  (id,		(Ctor<0>, Keys<0>),				String),
  (mxID,	(Ctor<1>),					String),
  (down,	(Ctor<2>, Mutable, Series),			UInt16),
  (disabled,	(Ctor<3>, Mutable, Series),			UInt16),
  (transient,	(Ctor<4>, Mutable, Series),			UInt16),
  (up,		(Ctor<5>, Mutable, Series),			UInt16),
  (reconn,	(Ctor<6>, Mutable, Series),			UInt16),
  (failed,	(Ctor<7>, Mutable, Series),			UInt16),
  (nLinks,	(Ctor<8>),					UInt16),
  (rxThread,	(Ctor<9>),					UInt8),
  (txThread,	(Ctor<10>),					UInt8),
  (state,	(Ctor<12>, Mutable, Enum<EngineState::Map>),	Int8),
  ((rag, RdFn),	(Synthetic, Series, Enum<RAG::Map>),		Int8));
Zfb::AsTable<"Ztc.fbs.HubTelemetry"> Zfb_Fmt(HubTelemetry *);

ZuAssert((ZuIsSame<
  ZuStructKeyT<Zfb::View<HubTelemetry>>::Types,
  decltype(ZuDeclVal<const Hub &>().telKey())::Types>{}));

ZfStruct(ZtcAPI, (LinkTelemetry, FB),
  (hubID,	(Ctor<0>, Keys<0>),				String),
  (id,		(Ctor<1>, Keys<0>),				String),
  (rxCalls,	(Ctor<2>, Mutable, Series, Delta),		UInt64),
  (txCalls,	(Ctor<3>, Mutable, Series, Delta),		UInt64),
  (rxBytes,	(Ctor<4>, Mutable, Series, Delta),		UInt64),
  (txBytes,	(Ctor<5>, Mutable, Series, Delta),		UInt64),
  (reconnects,	(Ctor<6>, Mutable, Series, Delta),		UInt32),
  (type,	(Ctor<7>, Enum<LinkType::Map>),			Int8),
  (state,	(Ctor<8>, Mutable, Enum<LinkState::Map>),	Int8),
  ((rag, RdFn),	(Synthetic, Series, Enum<RAG::Map>),		Int8));
Zfb::AsTable<"Ztc.fbs.LinkTelemetry"> Zfb_Fmt(LinkTelemetry *);

ZuAssert((ZuIsSame<
  ZuStructKeyT<Zfb::View<LinkTelemetry>>::Types,
  decltype(ZuDeclVal<const Link &>().telKey())::Types>{}));

ZfStruct(ZtcAPI, (PoolTelemetry, FB),
  (hubID,	(Ctor<0>, Keys<0>),				String),
  (id,		(Ctor<1>, Keys<0>),				String),
  (rxCalls,	(Ctor<2>, Mutable, Series, Delta),		UInt64),
  (rxBytes,	(Ctor<3>, Mutable, Series, Delta),		UInt64),
  (txCalls,	(Ctor<4>, Mutable, Series, Delta),		UInt64),
  (txBytes,	(Ctor<5>, Mutable, Series, Delta),		UInt64),
  (idle,	(Ctor<6>, Mutable, Series),			UInt16),
  (busy,	(Ctor<7>, Mutable, Series),			UInt16),
  (down,	(Ctor<8>, Mutable, Series),			UInt16),
  (type,	(Ctor<9>, Enum<LinkType::Map>),			Int8),
  (state,	(Ctor<10>, Mutable, Enum<PoolState::Map>),	Int8),
  ((rag, RdFn),	(Synthetic, Series, Enum<RAG::Map>),		Int8));
Zfb::AsTable<"Ztc.fbs.PoolTelemetry"> Zfb_Fmt(PoolTelemetry *);

ZuAssert((ZuIsSame<
  ZuStructKeyT<Zfb::View<PoolTelemetry>>::Types,
  decltype(ZuDeclVal<const Pool &>().telKey())::Types>{}));

ZfStruct(ZtcAPI, (AppTelemetry, FB),
  (version,	(Ctor<0>),					String),
  (role,	(Ctor<1>),					String),
  (startTime,	(Ctor<2>),					DateTime),
  (state,	(Ctor<4>, Mutable, Enum<EngineState::Map>),	Int8),
  (degraded,	(Ctor<5>, Mutable),				Bool),
  (rag,		(Ctor<6>, Mutable, Enum<RAG::Map>),		Int8),
  (ztcver,	(Ctor<3>),					UDT));
Zfb::AsTable<"Ztc.fbs.AppTelemetry"> Zfb_Fmt(AppTelemetry *);

ZfStruct(ZtcAPI, (AlertTelemetry, FB),
  (date,	(Ctor<4>, Keys<0>),	UInt32),
  (seqNo,	(Ctor<2>, Keys<0>),	UInt64),
  (time,	(Ctor<1>),		Time),
  (tid,		(Ctor<3>),		UInt64),
  (severity,	(Ctor<5>),		Int8),
  (message,	(Ctor<0>),		String));
Zfb::AsTable<"Ztc.fbs.AlertTelemetry"> Zfb_Fmt(AlertTelemetry *);

ZuAssert((ZuIsSame<
  ZuStructKeyT<Zfb::View<AlertTelemetry>>::Types,
  decltype(ZuDeclVal<const AlertTelemetry &>().telKey())::Types>{}));

} // Ztc

#endif /* ZtcFB_HH */
