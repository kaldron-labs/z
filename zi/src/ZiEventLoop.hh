//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// generic event loop for use with external handles/sockets
// - intended for interop with sockets/handles from other libraries, e.g.
//   - postgres server connection (libpq / ZdbPQ)
//   - openssl async signing completion (libcrypto / Ztls)
// - uses epoll under Linux, WFMO under Windows
// - higher latency than ZiMultiplex under Windows
//   - ZiMultiplex is socket-only, uses Windows overlapped I/O for perf
//   - anonymous pipes (common on Windows) cannot use overlapped I/O

#ifndef ZiEventLoop_HH
#define ZiEventLoop_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZuDerive.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuUnion.hh>

#include <zlib/ZmFn.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmScheduler.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiPlatform.hh>
#include <zlib/ZiAssert.hh>

namespace ZiEvent {

// monomorphic ZeEvent
using Exception = ZeException;

// start result
using StartResult = ZuUnion<void, Exception>;
// start callback
using StartFn = ZmFn<void(StartResult),
  ZmFnHeapID<"ZiEventLoop.StartFn">>;

// stop result
using StopResult = ZuUnion<void, Exception>;
// stop callback
using StopFn = ZmFn<void(StopResult),
  ZmFnHeapID<"ZiEventLoop.StopFn">>;

// failure notification
using FailFn = ZmFn<void(Exception), ZmFnHeapID<"ZiEventLoop.FailFn">>;

// send/receive callbacks
using SocketSendFn = ZmFn<void(Zi::Socket),
  ZmFnHeapID<"ZiEventLoop.Socket.SendFn">>;
using SocketRecvFn = ZmFn<void(Zi::Socket),
  ZmFnHeapID<"ZiEventLoop.Socket.RecvFn">>;

using HandleWriteFn = ZmFn<void(Zi::Handle),
  ZmFnHeapID<"ZiEventLoop.Handle.WriteFn">>;
using HandleReadFn = ZmFn<void(Zi::Handle),
  ZmFnHeapID<"ZiEventLoop.Handle.ReadFn">>;

// socket
struct Socket__ {
  Zi::Socket	socket = Zi::nullSocket();
  SocketSendFn	send;
  SocketRecvFn	recv;
#ifdef _WIN32
  int		index = -1;	// index into WFMO arrays
#endif

  static auto KeyAxor(const Socket__ &socket) { return socket.socket; }
};
struct Socket_ : public ZuObject, public Socket__ { ZuDerive_(Socket_, Socket__); };

// Socket hash table, keyed on socket
ZmHashDerive(Sockets, Socket_,
  (ZmHashNode<Socket_,
    ZmHashKey<Socket_::KeyAxor,
	ZmHashHeapID<"ZiEventLoop.Socket">>>));

using Socket = Sockets::Node;

// handle
struct Handle__ {
  Zi::Handle	handle = Zi::nullHandle();
  HandleWriteFn	write;
  HandleReadFn	read;
#ifdef _WIN32
  int		index = -1;	// index into WFMO arrays
#endif

  static auto KeyAxor(const Handle__ &handle) { return handle.handle; }
};
struct Handle_ : public ZuObject, public Handle__ { ZuDerive_(Handle_, Handle__); };

// Handle hash table, keyed on handle
ZmHashDerive(Handles, Handle_,
  (ZmHashNode<Handle_,
    ZmHashKey<Handle_::KeyAxor,
	ZmHashHeapID<"ZiEventLoop.Handle">>>));

using Handle = Handles::Node;

// main event loop
class ZiAPI Loop {
public:
  void init(ZmScheduler *, unsigned sid, FailFn);
  void final();

  void start(StartFn);
  void stop(StopFn);

  bool stopping() const { return m_stopping; }
  bool invoked() const { return m_sched && m_sched->invoked(m_sid); }

  template <typename ...Args> void run(Args &&...args) {
    m_sched->run(ZuFwd<Args>(args)..., m_sid);
  }
  template <typename ...Args> void invoke(Args &&...args) {
    m_sched->invoke(ZuFwd<Args>(args)..., m_sid);
  }

  // use unblock() if sockets are not already set to non-blocking
  // by the originating library
  static bool unblock(Zi::Socket); // set non-blocking

  bool addSocket(Zi::Socket, SocketSendFn, SocketRecvFn, bool prime = true);
  void delSocket(Zi::Socket);

  void disconnect(Zi::Socket); // simulate remote disconnect

  bool addHandle(Zi::Handle, HandleWriteFn, HandleReadFn, bool prime = true);
  void delHandle(Zi::Handle);

  void close(Zi::Handle handle); // simulate remote close

private:
  bool start_();
  void stop_();
  void stop_0();

  void wake();
  void wake_();
  void run_();

  void start_failed(ZeException);
  void started();

  void delSocket_(ZmRef<Socket>);
  void delHandle_(ZmRef<Handle>);
  void delIndex_(unsigned);

  void failed(ZeException);

private:
  ZmScheduler		*m_sched = nullptr;
  unsigned		m_sid = 0;
  FailFn		m_failFn;

#ifndef _WIN32
  int			m_epollFD = -1;
  int			m_wakeFD = -1, m_wakeFD2 = -1;
#else
  int			m_wakeSemIndex = -1;
  ZtArray<HANDLE>	m_wfmoHandles;
  ZtArray<uintptr_t>	m_wfmoData;
#endif

  StartFn		m_startFn;
  StopFn		m_stopFn;
  bool			m_stopping = false;

  Sockets		m_sockets;
  Handles		m_handles;
};

} // ZiEvent

using ZiEventLoop = ZiEvent::Loop;

#endif /* ZiEventLoop_HH */
