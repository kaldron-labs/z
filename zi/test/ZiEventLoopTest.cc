//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmScheduler.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZiEventLoop.hh>
#include <zlib/ZiIOBuf.hh>

#include "ZiTestPorts.hh"

using namespace ZuTestUtil;

namespace ZiEventLoopTest_ {

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

int socketError_()
{
#ifdef _WIN32
  return WSAGetLastError();
#else
  return errno;
#endif
}

bool socketInterrupted_(int error)
{
#ifdef _WIN32
  return error == WSAEINTR;
#else
  return error == EINTR;
#endif
}

bool socketWouldBlock_(int error)
{
#ifdef _WIN32
  return error == WSAEWOULDBLOCK;
#else
  return error == EAGAIN || error == EWOULDBLOCK;
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
	loop.run([&loop, socket] { loop.delSocket(socket); });
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

constexpr auto BudgetPayload = "WXYZ"_Zu;
constexpr unsigned BudgetLength = BudgetPayload.length();

// One byte of logical output per turn, as a transport using a small Tx budget.
struct BudgetSender {
  ZiEventLoop	*loop;
  ZmSemaphore	*done;
  ZmAtomic<unsigned> *stage;
  Zi::Socket	socket;
  int		sent = 0;
  unsigned	continuations = 0;
  bool		posted = false;

  void send_() {
    if (sent < 0 || unsigned(sent) == BudgetLength || posted) return;
    char byte = BudgetPayload[unsigned(sent)];
    int n;
    int error;
    do {
      n = sendSocketBytes(socket, &byte, 1);
      error = n < 0 ? socketError_() : 0;
    } while (n < 0 && socketInterrupted_(error));
    if (n < 0 && socketWouldBlock_(error)) return;
    if (n != 1) {
      sent = -1;
      done->post();
      return;
    }
    if (++sent == int(BudgetLength)) {
      stage->store_(1);
      done->post();
      return;
    }
    posted = true;
    ++continuations;
    loop->run([this] { posted = false; send_(); });
  }
};

void testSocketBackpressure()
{
  ZuTestScope(testSocketBackpressure);

  Zi::Socket loopSocket = Zi::nullSocket();
  Zi::Socket peerSocket = Zi::nullSocket();
  ZuCHECK(makeSocketPair(loopSocket, peerSocket), "socketpair failed");
  if (Zi::nullSocket(loopSocket) || Zi::nullSocket(peerSocket)) return;
  if (!ZiEventLoop::unblock(loopSocket)) {
    ZuCHECK(false, "unblock socket failed");
    closeSocket_(loopSocket);
    closeSocket_(peerSocket);
    return;
  }

  // Request a small kernel send buffer so the bounded fill reaches would-block
  // even on hosts whose default socket buffers are tuned unusually large.
  int sendCapacity = ZiIOBuf_DefltSize;
  if (::setsockopt(loopSocket, SOL_SOCKET, SO_SNDBUF,
      reinterpret_cast<const char *>(&sendCapacity),
      sizeof(sendCapacity))) {
    ZuCHECK(false, "set socket send buffer failed");
    closeSocket_(loopSocket);
    closeSocket_(peerSocket);
    return;
  }

  // Limit the fixture's work even if the host has a large socket send buffer.
  constexpr unsigned FillLimit = 1U << 20;
  ZmRef<ZiIOBuf> fill = new ZiIOBufAlloc<>{};
  memset(fill->data(), 'F', fill->size);
  unsigned filled = 0;
  bool blocked = false;
  while (filled < FillLimit) {
    unsigned length = FillLimit - filled;
    if (length > fill->size) length = fill->size;
    int n = sendSocketBytes(loopSocket,
      reinterpret_cast<const char *>(fill->data()), length);
    if (n > 0) {
      filled += unsigned(n);
      continue;
    }
    int error = n < 0 ? socketError_() : 0;
    if (n < 0 && socketInterrupted_(error)) continue;
    blocked = n < 0 && socketWouldBlock_(error);
    break;
  }
  ZuCHECK(blocked && filled, "socket did not reach would-block");
  if (!blocked || !filled) {
    closeSocket_(loopSocket);
    closeSocket_(peerSocket);
    return;
  }

  ZmScheduler sched{ZmSchedParams().id("ZiEventLoopBackpressureTest")};
  ZiEventLoop loop;
  ZmSemaphore started, writable, closed, stopped;
  ZmAtomic<unsigned> failed = 0, loopStarted = 0, added = 0, stage = 0;
  BudgetSender sender{&loop, &writable, &stage, loopSocket};
  sched.start();
  loop.init(&sched, 1,
    [&failed, &started, &writable, &closed, &stopped](ZeException e) {
      log_("socket backpressure fail: ", e);
      failed.store_(1);
      started.post();
      writable.post();
      closed.post();
      stopped.post();
    });
  loop.start([&loop, loopSocket, &started, &closed, &failed,
      &loopStarted, &added, &sender, &stage](
      ZiEvent::StartResult result) {
    if (!result.is<ZiEvent::Exception>()) {
      loopStarted.store_(1);
      added.store_(loop.addSocket(loopSocket,
        [&sender](Zi::Socket) { sender.send_(); },
        [&loop, &closed, &failed, &stage](Zi::Socket socket) {
          char byte;
          if (recvSocketBytes(socket, &byte, 1) != 0) failed.store_(1);
          if (stage.load_() != 1) failed.store_(1);
          stage.store_(2);
          loop.run([&loop, socket] { loop.delSocket(socket); });
          closed.post();
        }, false));
    }
    started.post();
  });
  ZuCHECK(waitFor(started) && added.load_(),
    "backpressure socket registration failed");
  while (filled) {
    unsigned length = filled < fill->size ? filled : fill->size;
    int n = recvSocketBytes(peerSocket,
      reinterpret_cast<char *>(fill->data()), length);
    if (n > 0) {
      filled -= unsigned(n);
      continue;
    }
    if (n < 0 && socketInterrupted_(socketError_())) continue;
    failed.store_(1);
    break;
  }
  bool signalled = waitFor(writable);
  ZuCHECK(signalled && sender.sent == int(BudgetLength),
    "bounded writable continuation did not finish after would-block");
  if (signalled && sender.sent == int(BudgetLength)) {
    ZuArray<char, BudgetLength> received;
    unsigned n = 0;
    while (n < BudgetLength) {
      int r = recvSocketBytes(peerSocket, received.data() + n,
        BudgetLength - n);
      if (r <= 0) break;
      n += unsigned(r);
    }
    ZuCHECK(n == BudgetLength &&
      !memcmp(received.data(), BudgetPayload.data(), BudgetLength),
      "bounded writable payload mismatch");
    closeSocket_(peerSocket);
    ZuCHECK(waitFor(closed), "peer EOF was not dispatched");
  }
  if (loopStarted.load_()) {
    loop.stop([&stopped, &failed, &stage](ZiEvent::StopResult) {
      if (stage.load_() != 2) failed.store_(1);
      stage.store_(3);
      stopped.post();
    });
    ZuCHECK(waitFor(stopped), "backpressure loop stop timed out");
  }
  sched.stop();
  loop.final();
  closeSocket_(loopSocket);
  closeSocket_(peerSocket);
  ZuCHECK(sender.sent == int(BudgetLength) &&
    sender.continuations == BudgetLength - 1,
    "write budget did not post one continuation per remaining byte");
  ZuCHECK(stage.load_() == 3, "send, EOF, stop order was violated");
  ZuCHECK(!failed.load_(), "backpressure path failed");
}

void testSocketSendOnly()
{
  ZuTestScope(testSocketSendOnly);

  Zi::Socket loopSocket = Zi::nullSocket();
  Zi::Socket peerSocket = Zi::nullSocket();
  ZuCHECK(makeSocketPair(loopSocket, peerSocket), "socketpair failed");
  if (Zi::nullSocket(loopSocket) || Zi::nullSocket(peerSocket)) return;
  ZuCHECK(ZiEventLoop::unblock(loopSocket), "unblock socket failed");

  ZmScheduler sched{ZmSchedParams().id("ZiEventLoopSendOnlyTest")};
  ZiEventLoop loop;
  ZmSemaphore started, writable, closed, stopped;
  ZmAtomic<unsigned> failed = 0, loopStarted = 0, added = 0, events = 0;
  sched.start();
  loop.init(&sched, 1,
    [&failed, &started, &writable, &closed, &stopped](ZeException e) {
      log_("send-only socket fail: ", e);
      failed.store_(1);
      started.post();
      writable.post();
      closed.post();
      stopped.post();
    });
  loop.start([&loop, loopSocket, &started, &writable, &closed,
      &loopStarted, &added, &events](ZiEvent::StartResult result) {
    if (!result.is<ZiEvent::Exception>()) {
      loopStarted.store_(1);
      added.store_(loop.addSocket(loopSocket,
        [&loop, &writable, &closed, &events](Zi::Socket socket) {
          unsigned count = events.load_() + 1;
          events.store_(count);
          if (count == 1) {
            writable.post();
            return;
          }
          loop.run([&loop, socket] { loop.delSocket(socket); });
          closed.post();
        }, {}, false));
    }
    started.post();
  });

  ZuCHECK(waitFor(started) && added.load_(),
    "send-only socket registration failed");
  ZuCHECK(waitFor(writable) && events.load_() == 1,
    "initial send-only writable event missing");
  closeSocket_(peerSocket);
  ZuCHECK(waitFor(closed) && events.load_() == 2,
    "send-only hangup event missing");
  if (loopStarted.load_()) {
    loop.stop([&failed, &stopped](ZiEvent::StopResult result) {
      if (result.is<ZiEvent::Exception>()) failed.store_(1);
      stopped.post();
    });
    ZuCHECK(waitFor(stopped), "send-only stop timed out");
  }
  ZuCheck(!failed.load_());
  sched.stop();
  loop.final();
  closeSocket_(loopSocket);
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
	loop.run([&loop, handle] { loop.delHandle(handle); });
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

void testHandleWriteReady()
{
  ZuTestScope(testHandleWriteReady);

#ifdef _WIN32
  ZuCheck(true);
#else
  HandleSignal pipe;
  ZuCHECK(makeHandleSignal(pipe), "makeHandleSignal() failed");
  if (Zi::nullHandle(pipe.loopHandle)) return;

  int flags = ::fcntl(pipe.triggerHandle, F_GETFL, 0);
  bool unblocked = flags >= 0 &&
    !::fcntl(pipe.triggerHandle, F_SETFL, flags | O_NONBLOCK);
  ZuCHECK(unblocked, "unblock write handle failed");
  if (!unblocked) {
    closeHandleSignal(pipe);
    return;
  }

  ZmRef<ZiIOBuf> fill = new ZiIOBufAlloc<ZiIOBuf_DefltSize>{};
  memset(fill->data(), 'F', fill->size);
  unsigned filled = 0;
  for (;;) {
    int n = int(::write(pipe.triggerHandle, fill->data(), fill->size));
    if (n > 0) {
      filled += unsigned(n);
      continue;
    }
    if (n < 0 && errno == EINTR) continue;
    ZuCHECK(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK),
	"fill pipe failed");
    break;
  }

  ZmScheduler sched{ZmSchedParams().id("ZiEventLoopWriteTest")};
  ZiEventLoop loop;
  ZmSemaphore started, written, stopped;
  ZmAtomic<unsigned> failed = 0, loopStarted = 0;

  sched.start();
  loop.init(&sched, 1, [&failed, &started, &written, &stopped](ZeException e) {
    log_("handle write fail: ", e);
    failed.store_(1);
    started.post();
    written.post();
    stopped.post();
  });
  loop.start([&loop, &pipe, &started, &written, &failed, &loopStarted](
      ZiEvent::StartResult result) {
    if (result.is<ZiEvent::Exception>()) {
      failed.store_(1);
      started.post();
      return;
    }
    loopStarted.store_(1);
    bool added = loop.addHandle(pipe.triggerHandle,
      [&loop, &written, &failed](Zi::Handle handle) {
	char byte = 'W';
	int n = int(::write(handle, &byte, 1));
	if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
	if (n != 1) failed.store_(1);
	loop.run([&loop, handle] { loop.delHandle(handle); });
	written.post();
      }, {});
    if (!added) failed.store_(1);
    started.post();
  });

  ZuCHECK(waitFor(started), "handle write start timed out");
  while (filled) {
    unsigned length = filled < fill->size ? filled : fill->size;
    int n = int(::read(pipe.loopHandle, fill->data(), length));
    if (n > 0) {
      filled -= unsigned(n);
      continue;
    }
    if (n < 0 && errno == EINTR) continue;
    break;
  }
  ZuCHECK(waitFor(written), "handle write readiness timed out");
  ZuCHECK(!failed.load_(), "handle write path failed");

  if (loopStarted.load_()) {
    loop.stop([&failed, &stopped](ZiEvent::StopResult result) {
      if (result.is<ZiEvent::Exception>()) failed.store_(1);
      stopped.post();
    });
    ZuCHECK(waitFor(stopped), "handle write stop timed out");
  }
  sched.stop();
  loop.final();
  closeHandleSignal(pipe);
#endif
}

} // ZiEventLoopTest_

using namespace ZiEventLoopTest_;

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
  ZuTestCall(testSocketBackpressure);
  ZuTestCall(testSocketSendOnly);
  ZuTestCall(testHandleDispatch);
  ZuTestCall(testHandleWriteReady);
  return 0;
}
