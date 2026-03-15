//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

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

namespace ZiEventLoop {

// monomorphic ZeEvent
using Exception = ZeException;

// start result
using StartResult = ZuUnion<void, Exception>;
// start callback
using StartFn = ZmFn<void(StartResult)>;

// stop result
using StopResult = ZuUnion<void, Exception>;
// stop callback
using StopFn = ZmFn<void(StopResult)>;

// failure notification
using FailFn = ZmFn<void(Exception)>;

// send/receive callbacks
using SocketSendFn = ZmFn<void(Zi::Socket)>;
using SocketRecvFn = ZmFn<void(Zi::Socket)>;
using HandleSendFn = ZmFn<void(Zi::Handle)>;
using HandleRecvFn = ZmFn<void(Zi::Handle)>;

// socket
template <typename Heap = ZuEmpty>
class Socket_ : public Heap, public ZuObject {
  Zi::Socket	socket = Zi::nullSocket();
  SocketSendFn	send;
  SocketRecvFn	recv;
#ifdef _WIN32
  int		index = -1;	// index into WFMO arrays
#endif

  static auto KeyAxor(const Socket_ &socket) { return socket.socket; }
};
ZuDerive(Socket, (Socket_<ZmHeap<"ZiEventLoop.Socket", Socket_<>>>));

// Socket hash table, keyed on socket
ZuDerive(Sockets,
  (ZmHash<Socket,
    ZmHashNode<Socket,
      ZmHashKey<Socket::KeyAxor,
	ZmHashHeapID<"ZiEventLoop.Socket">>>>));

// handle
template <typename Heap = ZuEmpty>
class Handle_ : public Heap, public ZuObject {
  Zi::Handle	handle = Zi::nullHandle();
  HandleSendFn	send;
  HandleRecvFn	recv;
#ifdef _WIN32
  int		index = -1;	// index into WFMO arrays
#endif

  static auto KeyAxor(const Handle_ &handle) { return handle.handle; }
};
ZuDerive(Handle, (Handle_<ZmHeap<"ZiEventLoop.Handle", Handle_<>>>));

// Handle hash table, keyed on handle
ZuDerive(Handles,
  (ZmHash<Handle,
    ZmHashNode<Handle,
      ZmHashKey<Handle::KeyAxor,
	ZmHashHeapID<"ZiEventLoop.Handle">>>>));

// event loop
class Loop {
public:
  void init(ZmScheduler *, unsigned sid, FailFn);
  void final();

  void start(StartFn);
  void stop(StopFn);

  bool stopping() const { return m_stopping; }

  template <typename ...Args> void run(Args &&...args) {
    m_mx->run(m_sid, ZuFwd<Args>(args)...);
  }
  template <typename ...Args> void invoke(Args &&...args) {
    m_mx->invoke(m_sid, ZuFwd<Args>(args)...);
  }

  void addSocket(Zi::Socket socket, SocketSendFn sendFn, SocketRecvFn recvFn);
  void delSocket(Zi::Socket socket);

  void disconnect(Zi::Socket socket); // simulate remote disconnect

  void addHandle(Zi::Handle handle, HandleSendFn sendFn, HandleRecvFn recvFn);
  void delHandle(Zi::Handle handle);

  void close(Zi::Handle handle); // simulate remote close

private:
  bool start_();
  void stop_();
  void stop_1();
  void stop_2();

  void wake();
  void wake_();
  void run_();

  void start_failed(ZeException);
  void started();

  void delSocket_(ZmRef<Socket>);
  void delHandle_(ZmRef<Handle>);
  void delIndex_(unsigned);

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

} // Zi

using ZiEventLoop = Zi::EventLoop;

#endif /* ZiEventLoop_HH */
