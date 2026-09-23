//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Linux AF_UNIX D-Bus connection transport

#ifndef ZdbusConnection_HH
#define ZdbusConnection_HH

#ifndef ZdbusLib_HH
#include <zlib/ZdbusLib.hh>
#endif

#include <zlib/ZmFn.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmScheduler.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/ZiEventLoop.hh>
#include <zlib/ZiIOBuf.hh>

#include <zlib/ZdbusAddress.hh>

#include <zlib/ZdbusAuth.hh>
#include <zlib/ZdbusEnvelope.hh>

namespace Zdbus_ {

ZtEnumNS(ZdbusAPI, CxnState, int8_t,
  Stopped, Connecting, Authenticating, Hello, Ready, Stopping);

namespace CxnError {
  enum { System, Auth, Frame, Resource, Stopped };
}

struct CxnFailure {
  unsigned	offset = 0;
  int		system = 0;
  int		code = CxnError::System;
};

namespace CxnDefault {
  enum {
    // Resource ceilings limit one untrusted frame and queued application data;
    // callers can tune them up to the protocol's 128-MiB maximum.
    FrameLimit = 16U << 20,
    QueueBytes = 16U << 20,
    QueueMsgs = 1024,
    // Per-turn budgets bound scheduler latency under a continuously ready FD.
    RxBytes = 64U << 10,
    TxBytes = 64U << 10,
    Msgs = 32
  };
}

struct CxnParams {
  // Runtime limits bound both a peer's frame claim and scheduler work/turn.
  unsigned	frameLimit = CxnDefault::FrameLimit;
  unsigned	queueBytes = CxnDefault::QueueBytes;
  unsigned	queueMsgs = CxnDefault::QueueMsgs;
  unsigned	rxBytes = CxnDefault::RxBytes;
  unsigned	txBytes = CxnDefault::TxBytes;
  unsigned	msgs = CxnDefault::Msgs;
};

using CxnReadyFn = ZmFn<void(), ZmFnHeapID<"Zdbus.CxnReadyFn">>;
using CxnFrameFn = ZmFn<void(ZmRef<ZiIOBuf>, FrameInfo),
  ZmFnHeapID<"Zdbus.CxnFrameFn">>;
using CxnFailFn = ZmFn<void(CxnFailure),
  ZmFnHeapID<"Zdbus.CxnFailFn">>;
using CxnSendFn = ZmFn<void(bool), ZmFnHeapID<"Zdbus.CxnSendFn">>;
using CxnStopFn = ZmFn<void(), ZmFnHeapID<"Zdbus.CxnStopFn">>;

// Internal receive-buffer split after a complete frame is parsed.
ZdbusExtern ZmRef<ZiIOBuf> splitFrame_(ZmRef<ZiIOBuf> &input,
  FrameInfo &info);

struct Out_ : public ZmObject {
  ZmRef<ZiIOBuf>	buf;
  CxnSendFn	fn;
  unsigned	offset = 0;
  unsigned	charge = 0;

  Out_() = default;
  Out_(ZmRef<ZiIOBuf> buf_, CxnSendFn fn_, unsigned charge_)
  : buf{ZuMv(buf_)}, fn{ZuMv(fn_)}, charge{charge_} { }
};
ZmListDerive(OutQueue, Out_,
  ZmListNode<Out_, ZmListHeapID<"Zdbus.Out">>);
using Out = OutQueue::Node;

struct CxnStopWait_ {
  CxnStopFn fn;

  explicit CxnStopWait_(CxnStopFn fn_) : fn{ZuMv(fn_)} { }
};
ZmListDerive(CxnStopQueue, CxnStopWait_,
  ZmListNode<CxnStopWait_, ZmListHeapID<"Zdbus.CxnStop">>);

class ZdbusAPI Connection {
public:
  Connection() = default;
  ~Connection();

  Connection(const Connection &) = delete;
  Connection &operator =(const Connection &) = delete;

  void init(ZmScheduler *, unsigned rxSid, unsigned txSid,
    Address, CxnParams, CxnReadyFn, CxnFrameFn, CxnFailFn);
  void final();
  void start();
  void ready();
  void send(ZmRef<ZiIOBuf>, CxnSendFn);
  void stop(CxnStopFn);

private:
  void txStart_();
  void connect_();
  void connected_();
  void rxRead_();
  void rxWrite_();
  void rxSend_(ZmRef<Out>);
  void txSend_(ZmRef<ZiIOBuf>, CxnSendFn);
  void txDone_(ZmRef<Out>, bool);
  void rxReady_();
  void rxFail_(CxnFailure);
  void rxFailDrain_();
  void rxStop_();
  void rxDrain_();
  void txStopped_();
  void close_();
  bool consume_(unsigned &);
  bool frame_();
  bool auth_();

private:
  ZmScheduler		*m_sched = nullptr;
  unsigned		m_rxSid = 0;
  unsigned		m_txSid = 0;
  Address		m_addr;
  CxnParams		m_params;
  CxnReadyFn		m_readyFn;
  CxnFrameFn		m_frameFn;
  CxnFailFn		m_failFn;
  ZmAtomic<unsigned>	m_startRequested = 0;

  alignas(Zm::CacheLineSize) unsigned m_txBytes = 0;
  unsigned		m_txMsgs = 0;
  bool			m_txStopping = false;
  bool			m_txStopped = false;
  bool			m_txActive = false;
  CxnStopQueue		m_stopWaits;

  alignas(Zm::CacheLineSize) ZiEventLoop m_loop;
  ZmRef<ZiIOBuf>	m_input;
  OutQueue		m_output;
  Auth			m_auth;
  int			m_socket = -1;
  CxnState::T		m_state = CxnState::Stopped;
  bool			m_loopStarted = false;
};

} // Zdbus_

using ZdbusConnection = Zdbus_::Connection;

#endif /* ZdbusConnection_HH */
