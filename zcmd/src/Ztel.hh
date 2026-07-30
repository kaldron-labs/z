//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef Ztel_HH
#define Ztel_HH

#ifndef ZcmdLib_HH
#include <zlib/ZcmdLib.hh>
#endif

#include <zlib/ZuID.hh>

#include <zlib/ZtcHash.hh>
#include <zlib/ZtcHeap.hh>
#include <zlib/ZtcMx.hh>
#include <zlib/ZtcThread.hh>

#include <zlib/Zfb.hh>
#include <zlib/ZfbStruct.hh>

#include <zlib/ZvThreadParams.hh>
#include <zlib/ZvEngine.hh>

#include <zlib/ZtcDB.hh>

#include <zlib/ztel_request_fbs.h>
#include <zlib/ztel_reqack_fbs.h>
#include <zlib/ztel_telemetry_fbs.h>
#include <zlib/ztel_thread_priority_fbs.h>
#include <zlib/ztel_engine_state_fbs.h>
#include <zlib/ztel_link_state_fbs.h>
#include <zlib/ztel_queue_type_fbs.h>

namespace Ztel {

ZfbEnumMatchNS(RAG, ZvRAG, Off, Red, Amber, Green)

ZfbEnumMatchNS(ThreadPriority, ZmThreadPriority,
    RealTime, High, Normal, Low)

namespace EngineState {
  using namespace ZvEngineState;

  int rag(int i) {
    using namespace RAG;
    if (i < 0 || i >= ZmEngineState::N) return Off;
    static const int values[ZmEngineState::N] =
      { Red, Amber, Green, Red, Amber, Red };
    return values[i];
  }
}
namespace LinkState {
  using namespace ZvLinkState;

  int rag(int i) {
    using namespace RAG;
    if (i < 0 || i >= ZvLinkState::N) return Off;
    static const int values[ZvLinkState::N] =
      { Red, Off, Off, Amber, Green, Amber, Amber, Red, Amber, Amber, Amber };
    return values[i];
  }
}

ZfbEnumMatchNS(SocketType, ZiCxnType, TCPIn, TCPOut, UDP)

ZfbEnumMatchNS(EngineState, ZmEngineState,
    Stopped, Starting, Running, Stopping, StartPending, StopPending)

ZfbEnumMatchNS(LinkState, ZvLinkState,
  Down,
  Disabled,
  Deleted,
  Connecting,
  Up,
  ReconnectPending,
  Reconnecting,
  Failed,
  Disconnecting,
  ConnectPending,
  DisconnectPending)

ZfbEnumMatchNS(QueueType, ZvQueueType, Thread, IPC, Rx, Tx)

namespace CacheMode {
  using namespace Ztc::DBCacheMode;
}

namespace DBHostState {
  using namespace Ztc::DBHostState;

  int rag(int i) {
    using namespace ZvRAG;
    enum { N = Ztc::DBHostState::N };
    if (i < 0 || i >= N) return Off;
    static const int values[N] = {
      Off, Amber, Amber, Green, Amber, Amber
    };
    return values[i];
  }
}

ZfbEnumNS(AppRole, Dev, Test, Prod)

ZfbEnumNS(Severity, Debug, Info, Warning, Error, Fatal)

using Heap_ = Ztc::HeapTelemetry;
struct Heap : public Heap_ {
  ZuDerive_(Heap, Heap_)

  uint64_t allocated() const { return (cacheAllocs + heapAllocs) - frees; }
  void allocated(uint64_t) { } // unused

  int8_t rag() const {
    if (!cacheSize) return RAG::Off;
    if (allocated() > cacheSize) return RAG::Red;
    if (heapAllocs) return RAG::Amber;
    return RAG::Green;
  }
  void rag(int8_t) { } // unused

  friend ZfStructPrint ZuPrintType(Heap *);
};
ZfbStruct(Heap,
    (((id),		(Ctor<0>, Keys<0>)),			(String)),
    (((size),		(Ctor<7>, Keys<0>)),			(UInt32)),
    (((alignment),	(Ctor<10>)),				(UInt8)),
    (((partition),	(Ctor<8>, Keys<0>)),			(UInt16)),
    (((sharded),	(Ctor<9>)),				(Bool)),
    (((cacheSize),	(Ctor<1>)),				(UInt64)),
    (((cpuset),		(Ctor<2>)),				(Bitmap)),
    (((cacheAllocs),	(Ctor<3>, Mutable, Series, Delta)),	(UInt64)),
    (((heapAllocs),	(Ctor<4>, Mutable, Series, Delta)),	(UInt64)),
    (((frees),		(Ctor<5>, Mutable, Series, Delta)),	(UInt64)),
    (((crossFrees),	(Ctor<6>, Mutable, Series, Delta)),	(UInt64)),
    (((allocated, RdFn), (Synthetic, Series)),			(UInt64)),
    (((rag, RdFn),	(Synthetic, Series, Enum<RAG::Map>)),	(Int8)));

using HashTbl_ = Ztc::HashTelemetry;
struct HashTbl : public HashTbl_ {
  ZuDerive_(HashTbl, HashTbl_)

  int8_t rag() const {
    if (resized) return RAG::Red;
    if (effLoadFactor >= loadFactor * 0.8) return RAG::Amber;
    return RAG::Green;
  }
  void rag(int8_t) { } // unused

  friend ZfStructPrint ZuPrintType(HashTbl *);
};
ZfbStruct(HashTbl,
    (((id),		(Ctor<0>, Keys<0>)),			(String)),
    (((addr),		(Ctor<1>, Keys<0>, Hex)),		(UInt64)),
    (((shadow),		(Ctor<10>)),				(Bool)),
    (((linear),		(Ctor<9>)),				(Bool)),
    (((bits),		(Ctor<7>)),				(UInt8)),
    (((cBits),		(Ctor<8>)),				(UInt8)),
    (((loadFactor),	(Ctor<2>)),				(Float)),
    (((nodeSize),	(Ctor<5>)),				(UInt32)),
    (((count),		(Ctor<4>, Mutable, Series)),		(UInt64)),
    (((effLoadFactor),	(Ctor<3>, Mutable, Series, NDP<2>)),	(Float)),
    (((resized),	(Ctor<6>)),				(UInt32)),
    (((rag, RdFn),	(Synthetic, Series, Enum<RAG::Map>)),	(Int8)));

using Thread_ = Ztc::ThreadTelemetry;
struct Thread : public Thread_ {
  ZuDerive_(Thread, Thread_)

  int8_t rag() const {
    if (cpuUsage >= 0.8) return RAG::Red;
    if (cpuUsage >= 0.5) return RAG::Amber;
    return RAG::Green;
  }
  void rag(int8_t) { } // unused

  friend ZfStructPrint ZuPrintType(Thread *);
};
// LATER - need to optionally enrich this with thread ring count and overCount
// (i.e. scheduler queue length and DLQ length)
ZfbStruct(Thread,
    (((name),		(Ctor<0>)),				(String)),
    (((sid),		(Ctor<8>)),				(UInt16)),
    (((tid),		(Ctor<1>, Keys<0>)),			(UInt64)),
    (((cpuUsage),	(Ctor<4>, Mutable, Series, NDP<2>)),	(Float)),
    (((allocStack),	(Ctor<5>, Mutable, Series)),		(UInt64)),
    (((allocHeap),	(Ctor<6>, Mutable, Series)),		(UInt64)),
    (((cpuset),		(Ctor<3>)),				(Bitmap)),
    (((priority),	(Ctor<10>, Enum<ThreadPriority::Map>)),	(Int8)),
    (((sysPriority),	(Ctor<7>)),				(Int32)),
    (((stackSize),	(Ctor<2>)),				(UInt64)),
    (((partition),	(Ctor<9>)),				(UInt16)),
    (((main),		(Ctor<11>)),				(Bool)),
    (((detached),	(Ctor<12>)),				(Bool)),
    (((rag, RdFn),	(Synthetic, Series, Enum<RAG::Map>)),	(Int8)));

using Mx_ = Ztc::MxTelemetry;
struct Mx : public Mx_ {
  ZuDerive_(Mx, Mx_)

  int8_t rag() const { return EngineState::rag(state); }
  void rag(int8_t) { } // unused

  friend ZfStructPrint ZuPrintType(Mx *);
};
ZfbStruct(Mx,
    (((id),		(Ctor<0>, Keys<0>)),			(String)),
    (((state),		(Ctor<10>, Mutable, Enum<EngineState::Map>)), (Int8)),
    (((nThreads),	(Ctor<13>)),				(UInt8)),
    (((rxThread),	(Ctor<7>)),				(UInt16)),
    (((txThread),	(Ctor<8>)),				(UInt16)),
    (((priority),	(Ctor<12>)),				(UInt8)),
    (((stackSize),	(Ctor<1>)),				(UInt32)),
    (((partition),	(Ctor<9>)),				(UInt16)),
    (((rxBufSize),	(Ctor<5>)),				(UInt32)),
    (((txBufSize),	(Ctor<6>)),				(UInt32)),
    (((queueSize),	(Ctor<2>)),				(UInt32)),
    (((ll),		(Ctor<11>)),				(Bool)),
    (((spin),		(Ctor<3>)),				(UInt32)),
    (((timeout),	(Ctor<4>)),				(UInt32)),
    (((rag, RdFn),	(Synthetic, Series, Enum<RAG::Map>)),	(Int8)));

using Socket_ = Ztc::CxnTelemetry;
struct Socket : public Socket_ {
  ZuDerive_(Socket, Socket_)

  int8_t rag() const {
    if (rxBufLen * 10 >= (rxBufSize<<3) ||
	txBufLen * 10 >= (txBufSize<<3)) return RAG::Red;
    if ((rxBufLen<<1) >= rxBufSize ||
	(txBufLen<<1) >= txBufSize) return RAG::Amber;
    return RAG::Green;
  }
  void rag(int8_t) { } // unused

  friend ZfStructPrint ZuPrintType(Socket *);
};
ZfbStruct(Socket,
    (((mxID),		(Ctor<0>)),				(String)),
    (((type),		(Ctor<17>, Enum<SocketType::Map>)),	(Int8)),
    (((remoteIP),	(Ctor<13>, Keys<0>)),			(IP)),
    (((remotePort),	(Ctor<15>, Keys<0>)),			(UInt16)),
    (((localIP),	(Ctor<12>, Keys<0>)),			(IP)),
    (((localPort),	(Ctor<14>, Keys<0>)),			(UInt16)),
    (((socket),		(Ctor<1>)),				(UInt64)),
    (((flags),		(Ctor<16>, Flags<ZiCxnFlags::Map>)),	(UInt8)),
    (((mreqAddr),	(Ctor<6>)),				(IP)),
    (((mreqIf),		(Ctor<7>)),				(IP)),
    (((mreqIfIndex),	(Ctor<8>)),				(UInt32)),
    (((mif),		(Ctor<9>)),				(IP)),
    (((mifIndex),	(Ctor<10>)),				(UInt32)),
    (((ttl),		(Ctor<11>)),				(UInt32)),
    (((rxBufSize),	(Ctor<2>)),				(UInt32)),
    (((rxBufLen),	(Ctor<3>, Mutable, Series)),		(UInt32)),
    (((txBufSize),	(Ctor<4>)),				(UInt32)),
    (((txBufLen),	(Ctor<5>, Mutable, Series)),		(UInt32)),
    (((rag, RdFn),	(Synthetic, Series, Enum<RAG::Map>)),	(Int8)));

// display sequence:
//   id, type, size, full, count, seqNo,
//   inCount, inBytes, outCount, outBytes
using Queue_ = ZvQueueTelemetry;
struct Queue : public Queue_ {
  ZuDerive_(Queue, Queue_)

  // RAG for queues - count > 50% size - amber; 80% - red
  int8_t rag() const {
    if (!size) return RAG::Off;
    if (count * 10 >= (size<<3)) return RAG::Red;
    if ((count<<1) >= size) return RAG::Amber;
    return RAG::Green;
  }
  void rag(int8_t) { } // unused

  friend ZfStructPrint ZuPrintType(Queue *);
};
ZfbStruct(Queue,
    (((id),		(Keys<0>, Ctor<0>)),			(String)),
    (((type),		(Keys<0>, Ctor<9>, Enum<ZvQueueType::Map>)), (Int8)),
    (((size),		(Ctor<7>)),				(UInt32)),
    (((full),		(Ctor<8>, Mutable, Series, Delta)),	(UInt32)),
    (((count),		(Ctor<2>, Mutable, Series)),		(UInt64)),
    (((seqNo),		(Ctor<1>)),				(UInt64)),
    (((inCount),	(Ctor<3>, Mutable, Series, Delta)),	(UInt64)),
    (((inBytes),	(Ctor<4>, Mutable, Series, Delta)),	(UInt64)),
    (((outCount),	(Ctor<5>, Mutable, Series, Delta)),	(UInt64)),
    (((outBytes),	(Ctor<6>, Mutable, Series, Delta)),	(UInt64)),
    (((rag, RdFn),	(Synthetic, Series, Enum<RAG::Map>)),	(Int8)));

// display sequence:
//   id, state, reconnects, rxSeqNo, txSeqNo
using Link_ = ZvAnyLink::Telemetry;
struct Link : public Link_ {
  ZuDerive_(Link, Link_)

  int8_t rag() const { return LinkState::rag(state); }
  void rag(int8_t) { } // unused

  friend ZfStructPrint ZuPrintType(Link *);
};
ZfbStruct(Link,
    (((id),		(Ctor<0>, Keys<0>)),			(String)),
    (((engineID),	(Ctor<1>)),				(String)),
    (((state),		(Ctor<5>, Mutable, Enum<LinkState::Map>)), (Int8)),
    (((reconnects),	(Ctor<4>, Mutable, Series, Delta)),	(UInt32)),
    (((rxSeqNo),	(Ctor<2>, Mutable, Series, Delta)),	(UInt64)),
    (((txSeqNo),	(Ctor<3>, Mutable, Series, Delta)),	(UInt64)),
    (((rag, RdFn),	(Synthetic, Series, Enum<RAG::Map>)),	(Int8)));

// display sequence:
//   id, state, nLinks, up, down, disabled, transient, reconn, failed,
//   mxID, rxThread, txThread
using Engine_ = ZvEngine::Telemetry;
struct Engine : public Engine_ {
  ZuDerive_(Engine, Engine_)

  int8_t rag() const { return EngineState::rag(state); }
  void rag(int8_t) { } // unused

  friend ZfStructPrint ZuPrintType(Engine *);
};
ZfbStruct(Engine,
    (((id),		(Keys<0>, Ctor<0>)),			(String)),
    (((type),		(Ctor<1>)),				(String)),
    (((state),		(Ctor<12>, Mutable, Enum<EngineState::Map>)), (Int8)),
    (((nLinks),		(Ctor<9>)),				(UInt16)),
    (((up),		(Ctor<6>, Mutable, Series)),		(UInt16)),
    (((down),		(Ctor<3>, Mutable, Series)),		(UInt16)),
    (((disabled),	(Ctor<4>, Mutable, Series)),		(UInt16)),
    (((transient),	(Ctor<5>, Mutable, Series)),		(UInt16)),
    (((reconn),		(Ctor<7>, Mutable, Series)),		(UInt16)),
    (((failed),		(Ctor<8>, Mutable, Series)),		(UInt16)),
    (((mxID),		(Ctor<2>)),				(String)),
    (((rxThread),	(Ctor<10>)),				(UInt16)),
    (((txThread),	(Ctor<11>)),				(UInt16)),
    (((rag, RdFn),	(Synthetic, Series, Enum<RAG::Map>)),	(Int8)));

using DBTable = Ztc::DBTableTelemetry;
using DBHost = Ztc::DBHostTelemetry;
using DB = Ztc::DBTelemetry;

// display sequence:
//   id, role, RAG, uptime, version
struct App {
  ZuID		id;
  ZuID		version;
  ZuDateTime	uptime;
  // LATER - need instanceID (i.e. hostID) for clustered apps
  int8_t	role = -1;
  int8_t	rag = -1;

  friend ZfStructPrint ZuPrintType(App *);
};
ZfbStruct(App,
    (((id),		(Keys<0>, Ctor<0>)),			(String)),
    (((version),	(Ctor<1>)),				(String)),
    (((uptime),		(Ctor<2>, Mutable)),			(DateTime)),
    (((role),		(Ctor<3>, Enum<AppRole::Map>)),		(Int8)),
    (((rag),		(Ctor<4>, Mutable, Enum<RAG::Map>)),	(Int8)));

// display sequence:
//   time, severity, tid, message
struct Alert {
  ZuDateTime	time;
  uint64_t	seqNo = 0;
  uint64_t	tid = 0;
  int8_t	severity = -1;
  ZeString	message;

  friend ZfStructPrint ZuPrintType(Alert *);
};
ZfbStruct(Alert,
    (((time),		(Ctor<0>)),				(DateTime)),
    (((seqNo),		(Ctor<1>)),				(UInt64)),
    (((tid),		(Ctor<2>)),				(UInt64)),
    (((severity),	(Ctor<3>, Enum<Severity::Map>)),	(Int8)),
    (((message),	(Ctor<4>)),				(String)));

ZfbEnumNS(ReqType, Heap, HashTbl, Thread, Mx, Queue, Engine, DB, App, Alert)

ZfbEnumUnionNS(TelData,
    Heap, HashTbl, Thread, Mx, Socket, Queue, Engine, Link,
    DBTable, DBHost, DB, App, Alert);

using TypeList = ZuTypeList<
  Heap, HashTbl, Thread, Mx, Socket, Queue, Engine, Link,
  DBTable, DBHost, DB, App, Alert>;

} // Ztel

#endif /* Ztel_HH */
