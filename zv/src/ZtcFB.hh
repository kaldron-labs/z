//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// FlatBuffers metadata for telemetry service values

#ifndef ZtcFB_HH
#define ZtcFB_HH

#ifndef ZvLib_HH
#include <zlib/ZvLib.hh>
#endif

#include <zlib/ZfbStruct.hh>

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

#include <zlib/ztc_msg_fbs.h>

namespace ZmThreadPriority {
ZfbEnumMatch_(Ztc::fbs::ThreadPriority,
  Unset, RealTime, High, Normal, Low);
}

namespace Ztc {

namespace RAG {
ZfbEnumMatch_(fbs::RAG, Off, Red, Amber, Green);
}

ZfbEnumMatchNS(ZvAPI, EngineState, ZmEngineState,
  Stopped, Starting, Running, Stopping, StartPending, StopPending);
ZfbEnumMatchNS(ZvAPI, CxnType, ZiCxnType, TCPIn, TCPOut, UDP);

namespace QueueType {
ZfbEnumMatch_(fbs::QueueType, Thread, IPC, Rx, Tx);
}
namespace LinkType {
ZfbEnumMatch_(fbs::LinkType, TCP, TLS, QUIC, H1, H3, WS, FIX);
}
namespace LinkState {
ZfbEnumMatch_(fbs::LinkState,
  Down, Disabled, Deleted, Connecting, Up, ReconnectPending, Reconnecting,
  Failed, Disconnecting, ConnectPending, DisconnectPending);
}
namespace PoolState {
ZfbEnumMatch_(fbs::PoolState, Down, Up, Failed);
}

ZfbStruct(HeapTelemetry,
  (((id),		(Ctor<0>, Keys<0>)),			(String)),
  (((size),		(Ctor<7>, Keys<0>)),			(UInt32)),
  (((alignment),	(Ctor<10>, Keys<0>)),			(UInt8)),
  (((partition),	(Ctor<8>, Keys<0>)),			(UInt16)),
  (((sharded),		(Ctor<9>, Keys<0>)),			(UInt8)),
  (((cacheSize),	(Ctor<1>)),				(UInt64)),
  (((cpuset),		(Ctor<2>)),				(UDT)),
  (((cacheAllocs),	(Ctor<3>, Mutable, Series, Delta)),	(UInt64)),
  (((heapAllocs),	(Ctor<4>, Mutable, Series, Delta)),	(UInt64)),
  (((frees),		(Ctor<5>, Mutable, Series, Delta)),	(UInt64)),
  (((crossFrees),	(Ctor<6>, Mutable, Series, Delta)),	(UInt64)),
  (((allocated, RdFn),	(Synthetic, Series)),			(UInt64)),
  (((rag, RdFn),	(Synthetic, Series, Enum<RAG::Map>)),	(Int8)));

ZfbStruct(HashTelemetry,
  (((id),		(Ctor<0>, Keys<0>)),			(String)),
  (((addr),		(Ctor<1>, Keys<0>, Hex)),		(UInt64)),
  (((shadow),		(Ctor<10>)),				(UInt8)),
  (((linear),		(Ctor<9>)),				(UInt8)),
  (((bits),		(Ctor<7>)),				(UInt8)),
  (((cBits),		(Ctor<8>)),				(UInt8)),
  (((loadFactor),	(Ctor<2>)),				(Float)),
  (((nodeSize),		(Ctor<5>)),				(UInt32)),
  (((count),		(Ctor<4>, Mutable, Series)),		(UInt64)),
  (((effLoadFactor),	(Ctor<3>, Mutable, Series, NDP<2>)),	(Float)),
  (((resized),		(Ctor<6>)),				(UInt32)),
  (((rag, RdFn),	(Synthetic, Series, Enum<RAG::Map>)),	(Int8)));

ZfbStruct(ThreadTelemetry,
  (((name),		(Ctor<0>)),				(String)),
  (((tid),		(Ctor<1>, Keys<0>)),			(UInt64)),
  (((stackSize),	(Ctor<2>)),				(UInt64)),
  (((cpuset),		(Ctor<3>)),				(UDT)),
  (((cpuUsage),		(Ctor<4>, Mutable, Series, NDP<2>)),	(Float)),
  (((allocStack),	(Ctor<5>, Mutable, Series)),		(UInt64)),
  (((allocHeap),	(Ctor<6>, Mutable, Series)),		(UInt64)),
  (((sysPriority),	(Ctor<7>)),				(Int32)),
  (((sid),		(Ctor<8>)),				(UInt16)),
  (((partition),	(Ctor<9>)),				(UInt16)),
  (((priority),		(Ctor<10>, Enum<ZmThreadPriority::Map>)),	(Int8)),
  (((main),		(Ctor<11>)),				(Bool)),
  (((detached),		(Ctor<12>)),				(Bool)),
  (((rag, RdFn),	(Synthetic, Series, Enum<RAG::Map>)),	(Int8)));

ZfbStruct(CxnTelemetry,
  (((mxID),		(Ctor<0>, Keys<0>)),			(String)),
  (((remoteIP),		(Ctor<17>, Keys<0>)),			(UDT)),
  (((remotePort),	(Ctor<19>, Keys<0>)),			(UInt16)),
  (((localIP),		(Ctor<16>, Keys<0>)),			(UDT)),
  (((localPort),	(Ctor<18>, Keys<0>)),			(UInt16)),
  (((socket),		(Ctor<1>)),				(UInt64)),
  (((rxCalls),		(Ctor<2>, Mutable, Series, Delta)),	(UInt64)),
  (((rxBytes),		(Ctor<3>, Mutable, Series, Delta)),	(UInt64)),
  (((txCalls),		(Ctor<4>, Mutable, Series, Delta)),	(UInt64)),
  (((txBytes),		(Ctor<5>, Mutable, Series, Delta)),	(UInt64)),
  (((rxBufSize),	(Ctor<6>)),				(UInt32)),
  (((rxBufLen),		(Ctor<7>, Mutable, Series)),		(UInt32)),
  (((txBufSize),	(Ctor<8>)),				(UInt32)),
  (((txBufLen),		(Ctor<9>, Mutable, Series)),		(UInt32)),
  (((mreqAddr),		(Ctor<10>)),				(UDT)),
  (((mreqIf),		(Ctor<11>)),				(UDT)),
  (((mreqIfIndex),	(Ctor<12>)),				(UInt32)),
  (((mif),		(Ctor<13>)),				(UDT)),
  (((mifIndex),		(Ctor<14>)),				(UInt32)),
  (((ttl),		(Ctor<15>)),				(UInt32)),
  (((flags),		(Ctor<20>, Flags<ZiCxnFlags::Map>)),	(UInt8)),
  (((type),		(Ctor<21>, Enum<CxnType::Map>)),		(Int8)),
  (((rag, RdFn),	(Synthetic, Series, Enum<RAG::Map>)),	(Int8)));

ZfbStruct(MxTelemetry,
  (((id),		(Ctor<0>, Keys<0>)),			(String)),
  (((stackSize),	(Ctor<1>)),				(UInt32)),
  (((queueSize),	(Ctor<2>)),				(UInt32)),
  (((spin),		(Ctor<3>)),				(UInt32)),
  (((timeout),		(Ctor<4>)),				(UInt32)),
  (((rxBufSize),	(Ctor<5>)),				(UInt32)),
  (((txBufSize),	(Ctor<6>)),				(UInt32)),
  (((rxThread),		(Ctor<7>)),				(UInt16)),
  (((txThread),		(Ctor<8>)),				(UInt16)),
  (((partition),	(Ctor<9>)),				(UInt16)),
  (((state),		(Ctor<10>, Mutable, Enum<EngineState::Map>)), (Int8)),
  (((ll),		(Ctor<11>)),				(UInt8)),
  (((priority),		(Ctor<12>)),				(UInt8)),
  (((nThreads),		(Ctor<13>)),				(UInt8)),
  (((rag, RdFn),	(Synthetic, Series, Enum<RAG::Map>)),	(Int8)));

namespace QueueType {
  ZtEnumMap(ZvAPI, QueueType, Map, "Thread", "IPC", "Rx", "Tx");
}

ZfbStruct(QueueTelemetry,
  (((ownerID),		(Ctor<0>, Keys<0>)),			(String)),
  (((id),		(Ctor<1>, Keys<0>)),			(String)),
  (((type),		(Ctor<9>, Keys<0>, Enum<QueueType::Map>)), (Int8)),
  (((inBytes),		(Ctor<2>, Mutable, Series, Delta)),	(UInt64)),
  (((outBytes),		(Ctor<3>, Mutable, Series, Delta)),	(UInt64)),
  (((inCount),		(Ctor<4>, Mutable, Series, Delta)),	(UInt64)),
  (((outCount),		(Ctor<5>, Mutable, Series, Delta)),	(UInt64)),
  (((count),		(Ctor<6>, Mutable, Series)),		(UInt64)),
  (((size),		(Ctor<7>)),				(UInt32)),
  (((full),		(Ctor<8>, Mutable, Series, Delta)),	(UInt32)),
  (((rag, RdFn),	(Synthetic, Series, Enum<RAG::Map>)),	(Int8)));

ZfbStruct(HubTelemetry,
  (((id),		(Ctor<0>, Keys<0>)),			(String)),
  (((linkType),		(Ctor<11>, Keys<0>, Enum<LinkType::Map>)), (Int8)),
  (((mxID),		(Ctor<1>)),				(String)),
  (((down),		(Ctor<2>, Mutable, Series)),		(UInt16)),
  (((disabled),		(Ctor<3>, Mutable, Series)),		(UInt16)),
  (((transient),	(Ctor<4>, Mutable, Series)),		(UInt16)),
  (((up),		(Ctor<5>, Mutable, Series)),		(UInt16)),
  (((reconn),		(Ctor<6>, Mutable, Series)),		(UInt16)),
  (((failed),		(Ctor<7>, Mutable, Series)),		(UInt16)),
  (((nLinks),		(Ctor<8>)),				(UInt16)),
  (((rxThread),		(Ctor<9>)),				(UInt8)),
  (((txThread),		(Ctor<10>)),				(UInt8)),
  (((state),		(Ctor<12>, Mutable, Enum<EngineState::Map>)), (Int8)),
  (((rag, RdFn),	(Synthetic, Series, Enum<RAG::Map>)),	(Int8)));

ZfbStruct(LinkTelemetry,
  (((hubID),		(Ctor<0>, Keys<0>)),			(String)),
  (((id),		(Ctor<1>, Keys<0>)),			(String)),
  (((rxCalls),		(Ctor<2>, Mutable, Series, Delta)),	(UInt64)),
  (((txCalls),		(Ctor<3>, Mutable, Series, Delta)),	(UInt64)),
  (((rxBytes),		(Ctor<4>, Mutable, Series, Delta)),	(UInt64)),
  (((txBytes),		(Ctor<5>, Mutable, Series, Delta)),	(UInt64)),
  (((reconnects),	(Ctor<6>, Mutable, Series, Delta)),	(UInt32)),
  (((type),		(Ctor<7>, Enum<LinkType::Map>)),		(Int8)),
  (((state),		(Ctor<8>, Mutable, Enum<LinkState::Map>)),	(Int8)),
  (((rag, RdFn),	(Synthetic, Series, Enum<RAG::Map>)),	(Int8)));

ZfbStruct(PoolTelemetry,
  (((hubID),		(Ctor<0>, Keys<0>)),			(String)),
  (((id),		(Ctor<1>, Keys<0>)),			(String)),
  (((rxCalls),		(Ctor<2>, Mutable, Series, Delta)),	(UInt64)),
  (((rxBytes),		(Ctor<3>, Mutable, Series, Delta)),	(UInt64)),
  (((txCalls),		(Ctor<4>, Mutable, Series, Delta)),	(UInt64)),
  (((txBytes),		(Ctor<5>, Mutable, Series, Delta)),	(UInt64)),
  (((idle),		(Ctor<6>, Mutable, Series)),		(UInt16)),
  (((busy),		(Ctor<7>, Mutable, Series)),		(UInt16)),
  (((down),		(Ctor<8>, Mutable, Series)),		(UInt16)),
  (((type),		(Ctor<9>, Enum<LinkType::Map>)),		(Int8)),
  (((state),		(Ctor<10>, Mutable, Enum<PoolState::Map>)),	(Int8)),
  (((rag, RdFn),	(Synthetic, Series, Enum<RAG::Map>)),	(Int8)));

ZfbStruct(AppTelemetry,
  (((version),		(Ctor<0>)),			(String)),
  (((role),		(Ctor<1>)),			(String)),
  (((startTime),	(Ctor<2>)),			(Int64)),
  (((state),		(Ctor<3>, Mutable, Enum<EngineState::Map>)), (Int8)),
  (((degraded),		(Ctor<4>, Mutable)),		(Bool)),
  (((rag),		(Ctor<5>, Mutable, Enum<RAG::Map>)), (Int8)));

ZfbStruct(AlertTelemetry,
  (((date),		(Ctor<4>, Keys<0>)),		(UInt32)),
  (((seqNo),		(Ctor<2>, Keys<0>)),		(UInt64)),
  (((time),		(Ctor<1>)),			(Time)),
  (((tid),		(Ctor<3>)),			(UInt64)),
  (((severity),	(Ctor<5>)),			(Int8)),
  (((message),		(Ctor<0>)),			(String)));

} // Ztc

#endif /* ZtcFB_HH */
