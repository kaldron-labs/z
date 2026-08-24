//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmScheduler.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZiEventLoop.hh>

#include "ZiTestPorts.hh"

using namespace ZuTestUtil;

namespace {

constexpr unsigned TimeoutSeconds = 5;
constexpr char SocketOut[] = "PING";
constexpr char SocketIn[] = "PONG";

bool waitFor(ZmSemaphore &sem)
{
  return sem.timedwait(Zm::now(TimeoutSeconds)) == 0;
}

int sendSocketBytes(Zi::Socket socket, const char *buf, unsigned length)
{
#ifdef _WIN32
  return ::send(socket, buf, int(length), 0);
#else
  return int(::send(socket, buf, length, 0));
#endif
}

int recvSocketBytes(Zi::Socket socket, char *buf, unsigned length)
{
#ifdef _WIN32
  return ::recv(socket, buf, int(length), 0);
#else
  return int(::recv(socket, buf, length, 0));
#endif
}

void closeSocket_(Zi::Socket &socket)
{
  if (Zi::nullSocket(socket)) return;
  Zi::closeSocket(socket);
  socket = Zi::nullSocket();
}

#ifndef _WIN32
bool makeSocketPair(Zi::Socket &loopSocket, Zi::Socket &peerSocket)
{
  int sockets[2];
  if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets)) return false;
  loopSocket = sockets[0];
  peerSocket = sockets[1];
  return true;
}
#else
struct WSAInit {
  WSAInit()
  {
    WSADATA data;
    ok = !WSAStartup(MAKEWORD(2, 2), &data);
  }
  ~WSAInit()
  {
    if (ok) WSACleanup();
  }

  bool	ok = false;
};

bool makeSocketPair(Zi::Socket &loopSocket, Zi::Socket &peerSocket)
{
  loopSocket = Zi::nullSocket();
  peerSocket = Zi::nullSocket();

  SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listener == INVALID_SOCKET) return false;

  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(ZiTestPort::EventLoop);

  if (::bind(listener, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) ||
      ::listen(listener, 1)) {
    ::closesocket(listener);
    return false;
  }

  int addrLen = sizeof(addr);
  if (::getsockname(listener, reinterpret_cast<sockaddr *>(&addr), &addrLen)) {
    ::closesocket(listener);
    return false;
  }

  peerSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (peerSocket == INVALID_SOCKET) {
    ::closesocket(listener);
    return false;
  }

  if (::connect(peerSocket, reinterpret_cast<sockaddr *>(&addr), sizeof(addr))) {
    ::closesocket(listener);
    closeSocket_(peerSocket);
    return false;
  }

  loopSocket = ::accept(listener, nullptr, nullptr);
  ::closesocket(listener);

  if (loopSocket == INVALID_SOCKET) {
    closeSocket_(peerSocket);
    return false;
  }

  return true;
}
#endif

struct HandleSignal {
  Zi::Handle	loopHandle = Zi::nullHandle();
  Zi::Handle	triggerHandle = Zi::nullHandle();
};

#ifndef _WIN32
bool makeHandleSignal(HandleSignal &signal)
{
  int pipeFD[2];
  if (::pipe(pipeFD)) return false;
  signal.loopHandle = pipeFD[0];
  signal.triggerHandle = pipeFD[1];
  return true;
}

bool triggerHandleSignal(const HandleSignal &signal)
{
  static constexpr char byte = 'H';
  return ::write(signal.triggerHandle, &byte, 1) == 1;
}

void closeHandleSignal(HandleSignal &signal)
{
  if (!Zi::nullHandle(signal.loopHandle)) {
    ::close(signal.loopHandle);
    signal.loopHandle = Zi::nullHandle();
  }
  if (!Zi::nullHandle(signal.triggerHandle)) {
    ::close(signal.triggerHandle);
    signal.triggerHandle = Zi::nullHandle();
  }
}
#else
bool makeHandleSignal(HandleSignal &signal)
{
  HANDLE handle = CreateSemaphore(nullptr, 0, 0x7fffffff, nullptr);
  if (Zi::nullHandle(handle)) return false;
  signal.loopHandle = handle;
  signal.triggerHandle = handle;
  return true;
}

bool triggerHandleSignal(const HandleSignal &signal)
{
  return !!ReleaseSemaphore(signal.triggerHandle, 1, nullptr);
}

void closeHandleSignal(HandleSignal &signal)
{
  if (!Zi::nullHandle(signal.loopHandle)) {
    CloseHandle(signal.loopHandle);
    signal.loopHandle = Zi::nullHandle();
    signal.triggerHandle = Zi::nullHandle();
  }
}
#endif

struct SocketState {
  ZmSemaphore		started;
  ZmSemaphore		sent;
  ZmSemaphore		recvd;
  ZmSemaphore		stopped;
  ZmAtomic<unsigned>	failed = 0;
  ZmAtomic<unsigned>	loopStarted = 0;
  ZmAtomic<unsigned>	startedOK = 0;
  ZmAtomic<unsigned>	sendSeen = 0;
  ZmAtomic<unsigned>	expectRecv = 0;
  ZmAtomic<unsigned>	recvOK = 0;
};

struct HandleState {
  ZmSemaphore		started;
  ZmSemaphore		read;
  ZmSemaphore		stopped;
  ZmAtomic<unsigned>	failed = 0;
  ZmAtomic<unsigned>	loopStarted = 0;
  ZmAtomic<unsigned>	startedOK = 0;
  ZmAtomic<unsigned>	expectRead = 0;
  ZmAtomic<unsigned>	readOK = 0;
};

void testSocketSendRecv()
{
  ZuTestScope(testSocketSendRecv);

  Zi::Socket loopSocket = Zi::nullSocket();
  Zi::Socket peerSocket = Zi::nullSocket();

  ZuCHECK(makeSocketPair(loopSocket, peerSocket), "makeSocketPair() failed");
  if (Zi::nullSocket(loopSocket) || Zi::nullSocket(peerSocket)) return;

  bool unblocked = ZiEventLoop::unblock(loopSocket);
  ZuCHECK(unblocked, "ZiEventLoop::unblock() failed");
  if (!unblocked) {
    closeSocket_(loopSocket);
    closeSocket_(peerSocket);
    return;
  }

  ZmScheduler sched{ZmSchedParams().id("ZiEventLoopSockTest")};
  ZiEventLoop loop;
  SocketState state;

  sched.start();
  loop.init(&sched, 1, [&state](ZeException e) {
    log_("socket fail: ", e);
    state.failed.store_(1);
    state.started.post();
    state.sent.post();
    state.recvd.post();
    state.stopped.post();
  });

  loop.start([&loop, loopSocket, &state](ZiEvent::StartResult result) {
    if (result.is<ZiEvent::Exception>()) {
      log_("socket start failed: ", ZuMv(result).p<ZiEvent::Exception>());
      state.failed.store_(1);
      state.started.post();
      return;
    }

    state.loopStarted.store_(1);

    bool added = loop.addSocket(
      loopSocket,
      [&state](Zi::Socket socket) {
	if (state.sendSeen.cmpXch(1, 0)) return;
	if (sendSocketBytes(socket, SocketOut, sizeof(SocketOut) - 1) !=
	    int(sizeof(SocketOut) - 1))
	  state.failed.store_(1);
	state.sent.post();
      },
      [&loop, &state](Zi::Socket socket) {
	if (!state.expectRecv.load_()) return;

	char buf[sizeof(SocketIn)] = {};
	int n = recvSocketBytes(socket, buf, sizeof(SocketIn) - 1);
	state.recvOK.store_(
	  n == int(sizeof(SocketIn) - 1) &&
	  !::memcmp(buf, SocketIn, sizeof(SocketIn) - 1) ? 1U : 0U);
	loop.delSocket(socket);
	state.recvd.post();
      });

    state.startedOK.store_(added ? 1U : 0U);
    if (!added) state.failed.store_(1);
    state.started.post();
  });

  ZuCHECK(waitFor(state.started), "socket start timed out");
  ZuCHECK(state.startedOK.load_(), "loop.addSocket() failed");
  ZuCHECK(!state.failed.load_(), "unexpected socket failure callback");

  bool ready = state.startedOK.load_() && !state.failed.load_();

  if (ready) ZuCHECK(waitFor(state.sent), "socket send timed out");
  if (ready && !state.failed.load_()) {
    char buf[sizeof(SocketOut)] = {};
    int n = recvSocketBytes(peerSocket, buf, sizeof(SocketOut) - 1);
    ZuCHECK(n == int(sizeof(SocketOut) - 1), "peer recv length=", n);
    if (n == int(sizeof(SocketOut) - 1))
      ZuCHECK(!::memcmp(buf, SocketOut, sizeof(SocketOut) - 1));
  }

  if (ready) {
    state.expectRecv.store_(1);
    int sent = sendSocketBytes(peerSocket, SocketIn, sizeof(SocketIn) - 1);
    ZuCHECK(sent == int(sizeof(SocketIn) - 1), "peer send length=", sent);
    ZuCHECK(waitFor(state.recvd), "socket recv timed out");
    ZuCHECK(state.recvOK.load_(), "socket recv payload mismatch");
  }

  if (state.loopStarted.load_()) {
    loop.stop([&state](ZiEvent::StopResult result) {
      if (result.is<ZiEvent::Exception>()) state.failed.store_(1);
      state.stopped.post();
    });

    ZuCHECK(waitFor(state.stopped), "socket stop timed out");
  }

  sched.stop();
  loop.final();

  closeSocket_(loopSocket);
  closeSocket_(peerSocket);

  ZuCHECK(state.sendSeen.load_() == 1);
  ZuCHECK(!state.failed.load_(), "socket path hit fail callback");
}

void testHandleDispatch()
{
  ZuTestScope(testHandleDispatch);

  HandleSignal signal;

  ZuCHECK(makeHandleSignal(signal), "makeHandleSignal() failed");
  if (Zi::nullHandle(signal.loopHandle)) return;

  ZmScheduler sched{ZmSchedParams().id("ZiEventLoopHandleTest")};
  ZiEventLoop loop;
  HandleState state;

  sched.start();
  loop.init(&sched, 1, [&state](ZeException e) {
    log_("handle fail: ", e);
    state.failed.store_(1);
    state.started.post();
    state.read.post();
    state.stopped.post();
  });

  loop.start([&loop, &signal, &state](ZiEvent::StartResult result) {
    if (result.is<ZiEvent::Exception>()) {
      log_("handle start failed: ", ZuMv(result).p<ZiEvent::Exception>());
      state.failed.store_(1);
      state.started.post();
      return;
    }

    state.loopStarted.store_(1);

    bool added = loop.addHandle(
      signal.loopHandle,
      [](Zi::Handle) { },
      [&loop, &state](Zi::Handle handle) {
	if (!state.expectRead.load_()) return;
#ifndef _WIN32
	char byte = 0;
	int n = int(::read(handle, &byte, 1));
	state.readOK.store_(n == 1 && byte == 'H' ? 1U : 0U);
#else
	state.readOK.store_(1);
#endif
	loop.delHandle(handle);
	state.read.post();
      });

    state.startedOK.store_(added ? 1U : 0U);
    if (!added) state.failed.store_(1);
    state.started.post();
  });

  ZuCHECK(waitFor(state.started), "handle start timed out");
  ZuCHECK(state.startedOK.load_(), "loop.addHandle() failed");
  ZuCHECK(!state.failed.load_(), "unexpected handle failure callback");

  bool ready = state.startedOK.load_() && !state.failed.load_();

  if (ready) {
    state.expectRead.store_(1);
    ZuCHECK(triggerHandleSignal(signal), "triggerHandleSignal() failed");
    ZuCHECK(waitFor(state.read), "handle read timed out");
    ZuCHECK(state.readOK.load_(), "handle read payload mismatch");
  }

  if (state.loopStarted.load_()) {
    loop.stop([&state](ZiEvent::StopResult result) {
      if (result.is<ZiEvent::Exception>()) state.failed.store_(1);
      state.stopped.post();
    });

    ZuCHECK(waitFor(state.stopped), "handle stop timed out");
  }

  sched.stop();
  loop.final();

  closeHandleSignal(signal);

  ZuCHECK(!state.failed.load_(), "handle path hit fail callback");
}

} // namespace

int main(int argc, char **argv)
{
  parse(argc, argv);

#ifdef _WIN32
  WSAInit wsa;
  if (!wsa.ok) {
    log_("WSAStartup() failed");
    return 1;
  }
#endif

  ZuTestMain();
  ZuTestCall(testSocketSendRecv);
  ZuTestCall(testHandleDispatch);
  return 0;
}
