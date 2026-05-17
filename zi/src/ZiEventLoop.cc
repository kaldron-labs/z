//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef _WIN32

#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <linux/unistd.h>

#ifndef EPOLLRDHUP
#define EPOLLRDHUP 0
#endif

#endif // !_WIN32

#include <zlib/ZiEventLoop.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiAssert.hh>

namespace ZiEvent {

// 64-bit pointer-packing

inline uint64_t u64_socket(void *ptr) {
  return reinterpret_cast<uintptr_t>(ptr);
}
inline bool u64_is_socket(uint64_t v) { return !(v>>62); }
inline uint64_t u64_handle(void *ptr) {
  return reinterpret_cast<uintptr_t>(ptr) | (uint64_t(1)<<62);
}
inline bool u64_is_handle(uint64_t v) { return (v>>62) == 1; }
constexpr const uint64_t u64_wake() { return uint64_t(2)<<62; }
inline bool u64_is_wake(uint64_t v) { return (v>>62) == 2; }
template <typename T>
inline T *u64_ptr(uint64_t v) {
  return reinterpret_cast<T *>(v & ~(uint64_t(3)<<62));
}

void Loop::init(ZmScheduler *sched, unsigned sid, FailFn failFn)
{
  m_sched = sched;
  m_sid = sid;
  m_failFn = ZuMv(failFn);
}

void Loop::final()
{
  m_sched = nullptr;
  m_sid = 0;
  m_failFn = FailFn{};
}

void Loop::start(StartFn fn)
{
  // ZiLOG(Debug, "ZiEventLoop", ([](auto &s) { }));

  m_sched->push(m_sid, [this, fn = ZuMv(fn)]() mutable {
    m_stopping = false;
    m_startFn = ZuMv(fn);
    m_stopFn = StopFn{};
    if (!start_()) {
      start_failed(ZeEXCEPT(Fatal, "ZiEventLoop", "start() failed"));
      return;
    }
    m_sched->wakeFn(m_sid, ZmFn<>{this, [](Loop *loop) { loop->wake(); }});
    run_();
  });
}

bool Loop::start_()
{
  // ZiLOG(Debug, "ZiEventLoop", ([](auto &s) { }));

#ifndef _WIN32

  // set up I/O multiplexer (epoll)
  if ((m_epollFD = epoll_create(2)) < 0) {
    failed(ZeEXCEPT(Fatal, "ZiEventLoop", ([e = ZeLastError](auto &s) {
      s << "epoll_create() failed: " << e;
    })));
    ::close(m_epollFD); m_epollFD = -1;
    return false;
  }
  if (pipe(&m_wakeFD) < 0) {
    failed(ZeEXCEPT(Fatal, "ZiEventLoop", ([e = errno](auto &s) {
      s << "pipe() failed: " << e;
    })));
    ::close(m_epollFD); m_epollFD = -1;
    return false;
  }
  if (fcntl(m_wakeFD, F_SETFL, O_NONBLOCK) < 0) {
    failed(ZeEXCEPT(Fatal, "ZiEventLoop", ([e = errno](auto &s) {
      s << "fcntl(F_SETFL, O_NONBLOCK) failed: " << e;
    })));
    ::close(m_epollFD); m_epollFD = -1;
    ::close(m_wakeFD); m_wakeFD = -1;
    ::close(m_wakeFD2); m_wakeFD2 = -1;
    return false;
  }
  {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(struct epoll_event));
    ev.events = EPOLLIN;
    ev.data.u64 = u64_wake();
    if (epoll_ctl(m_epollFD, EPOLL_CTL_ADD, m_wakeFD, &ev) < 0) {
      failed(ZeEXCEPT(Fatal, "ZiEventLoop", ([e = errno](auto &s) {
	s << "epoll_ctl(EPOLL_CTL_ADD) failed: " << e;
      })));
      ::close(m_epollFD); m_epollFD = -1;
      ::close(m_wakeFD); m_wakeFD = -1;
      ::close(m_wakeFD2); m_wakeFD2 = -1;
      return false;
    }
  }

#else /* !_WIN32 */

  HANDLE wakeSem = CreateSemaphore(nullptr, 0, 0x7fffffff, nullptr);
  if (wakeSem == NULL || wakeSem == INVALID_HANDLE_VALUE) {
    failed(ZeEXCEPT(Fatal, "ZiEventLoop", ([e = ZeLastError](auto &s) {
      s << "CreateEvent() failed: " << e;
    })));
    return false;
  }
  m_wakeSemIndex = m_wfmoHandles.length();
  m_wfmoHandles.push(wakeSem);
  m_wfmoData.push(u64_wake());

#endif /* !_WIN32 */

  return true;
}

void Loop::stop(StopFn fn)
{
  // ZiLOG(Debug, "ZiEventLoop", ([](auto &s) { }));

  m_stopFn = ZuMv(fn);
  m_stopping = true; // inhibits further application requests

  m_sched->wakeFn(m_sid, ZmFn<>{});
  m_sched->push(m_sid, [this]() mutable {
    stop_2();
    StopFn stopFn = ZuMv(m_stopFn);
    m_stopFn = StopFn{};
    if (stopFn) stopFn(StopResult{});
  });
  wake_();
}

void Loop::stop_2()
{
  // ZiLOG(Debug, "ZiEventLoop", ([](auto &s) { }));

  // remove sockets
  {
    auto i = m_sockets.iter();
    while (i()) delSocket_(i.del());
  }
  // remove handles
  {
    auto i = m_handles.iter();
    while (i()) delHandle_(i.del());
  }

#ifndef _WIN32

  // close I/O multiplexer
  if (m_epollFD >= 0) {
    if (m_wakeFD >= 0)
      epoll_ctl(m_epollFD, EPOLL_CTL_DEL, m_wakeFD, 0);
    ::close(m_epollFD);
    m_epollFD = -1;
  }
  if (m_wakeFD >= 0) { ::close(m_wakeFD); m_wakeFD = -1; }
  if (m_wakeFD2 >= 0) { ::close(m_wakeFD2); m_wakeFD2 = -1; }

#else /* !_WIN32 */

  // close wakeup event
  // - we previously removed all the Sockets, so any remaining
  //   element is the wakeup semaphore
  if (m_wfmoHandles.length()) {
    ZiAssert(m_wfmoHandles.length() == 1 && m_wakeSemIndex == 0,
      "ZiEventLoop", (), "internal error", return);
    CloseHandle(m_wfmoHandles[0]);
    m_wakeSemIndex = -1;
    m_wfmoHandles = {};
    m_wfmoData = {};
  }

#endif /* !_WIN32 */
}

void Loop::wake()
{
  // ZiLOG(Debug, "ZiEventLoop", ([](auto &s) { s << "pushing run_()"; }));

  m_sched->push(m_sid, [this]{ run_(); });
  wake_();
}

void Loop::wake_()
{
  // ZiLOG(Debug, "ZiEventLoop", ([](auto &s) { }));

#ifndef _WIN32

  char c = 0;
  while (::write(m_wakeFD2, &c, 1) < 0) {
    ZeError e{errno};
    if (e.errNo() != EINTR && e.errNo() != EAGAIN) {
      failed(ZeEXCEPT(Fatal, "ZiEventLoop", ([e](auto &s) {
	s << "write() failed: " << e;
      })));
      break;
    }
  }

#else /* !_WIN32 */

  if (!ReleaseSemaphore(m_wfmoHandles[m_wakeSemIndex], 1, 0)) {
    failed(ZeEXCEPT(Fatal, "ZiEventLoop", ([e = ZeLastError](auto &s) {
      s << "ReleaseSemaphore() failed: " << e;
    })));
  }

#endif /* !_WIN32 */
}

bool Loop::addSocket(Zi::Socket socket_, SocketSendFn send, SocketRecvFn recv)
{
  ZmRef<Socket> socket = new Socket{socket_, ZuMv(send), ZuMv(recv)};

#ifndef _WIN32

  /* ZiLOG(Debug, "ZiEventLoop", ([this](auto &s) {
    s << "epoll_ctl(EPOLL_CTL_ADD) socket=" << socket;
  })); */

  {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(struct epoll_event));
    ev.events = EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR | EPOLLET;
    ev.data.u64 = u64_socket(socket.ptr());
    if (epoll_ctl(m_epollFD, EPOLL_CTL_ADD, socket_, &ev) < 0) {
      failed(ZeEXCEPT(Fatal, "ZiEventLoop", ([e = ZeLastError](auto &s) {
	s << "epoll_ctl(EPOLL_CTL_ADD) failed: " << e;
      })));
      return false;
    }
  }

#else /* !_WIN32 */

  HANDLE event = WSACreateEvent();
  if (event == NULL || event == INVALID_HANDLE_VALUE) {
    ZiLOG(Fatal, "ZiEventLoop", ([e = ZeLastError](auto &s) {
      s << "CreateEvent() failed: " << e;
    }));
    return false;
  }
  if (WSAEventSelect(socket, event,
      FD_READ | FD_WRITE | FD_OOB | FD_CLOSE)) {
    ZiLOG(Fatal, "ZiEventLoop", ([e = WSAGetLastError()](auto &s) {
      s << "WSAEventSelect() failed: " << e;
    }));
    return false;
  }
  socket->index = m_wfmoHandles.length();
  m_wfmoHandles.push(event);
  m_wfmoData.push(u64_socket(socket.ptr()));

#endif /* !_WIN32 */

  // "prime the pump" to ensure that read- and write-readiness is
  // correctly signalled via epoll / WFMO
  socket->send(socket_);
  socket->recv(socket_);

  m_sockets.addNode(ZuMv(socket));

  return true;
}

void Loop::delSocket(Zi::Socket socket_)
{
  if (Zi::nullSocket(socket_)) return;

  ZmRef<Socket> socket = m_sockets.del(socket_);

  if (!socket) return;

  delSocket_(ZuMv(socket));
}

bool Loop::unblock(Zi::Socket socket)
{
#ifndef _WIN32

  if (fcntl(socket, F_SETFL, O_NONBLOCK) < 0) {
    ZiLOG(Fatal, "ZiEventLoop", ([e = ZeLastError](auto &s) {
      s << "fcntl(O_NONBLOCK) failed: " << e;
    }));
    return false;
  }

#else /* !_WIN32 */

  u_long mode = 1;
  if (ioctlsocket(socket, FIONBIO, &mode) != 0) {
    ZiLOG(Fatal, "ZiEventLoop", ([e = ZeLastSockError](auto &s) {
      s << "ioctlsocket(FIONBIO, &1) failed: " << e;
    }));
    return false;
  }

#endif /* !_WIN32 */
  return true;
}

void Loop::delSocket_(ZmRef<Socket> socket)
{
#ifndef _WIN32

  if (m_epollFD >= 0)
    epoll_ctl(m_epollFD, EPOLL_CTL_DEL, socket->socket, 0);

#else /* !_WIN32 */

  // close connection event
  HANDLE handle = m_wfmoHandles[socket->index];
  CloseHandle(handle);
  delIndex_(socket->index);

#endif /* !_WIN32 */
}

bool Loop::addHandle(Zi::Handle handle_, HandleSendFn send, HandleRecvFn recv)
{
  ZmRef<Handle> handle = new Handle{handle_, ZuMv(send), ZuMv(recv)};

#ifndef _WIN32

  /* ZiLOG(Debug, "ZiEventLoop", ([this](auto &s) {
    s << "epoll_ctl(EPOLL_CTL_ADD) handle=" << handle;
  })); */

  {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(struct epoll_event));
    ev.events = EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR | EPOLLET;
    ev.data.u64 = u64_handle(handle.ptr());
    if (epoll_ctl(m_epollFD, EPOLL_CTL_ADD, handle_, &ev) < 0) {
      failed(ZeEXCEPT(Fatal, "ZiEventLoop", ([e = ZeLastError](auto &s) {
	s << "epoll_ctl(EPOLL_CTL_ADD) failed: " << e;
      })));
      return false;
    }
  }

#else /* !_WIN32 */

  handle->index = m_wfmoHandles.length();
  m_wfmoHandles.push(handle->handle);
  m_wfmoData.push(u64_handle(handle.ptr()));

#endif /* !_WIN32 */

  // "prime the pump" to ensure that read- and write-readiness is
  // correctly signalled via epoll / WFMO
  handle->send(handle_);
  handle->recv(handle_);

  m_handles.addNode(ZuMv(handle));

  return true;
}

void Loop::delHandle(Zi::Handle handle_)
{
  if (Zi::nullHandle(handle_)) return;

  ZmRef<Handle> handle = m_handles.del(handle_);

  if (!handle) return;

  delHandle_(ZuMv(handle));
}

void Loop::delHandle_(ZmRef<Handle> handle)
{
#ifndef _WIN32

  if (m_epollFD >= 0)
    epoll_ctl(m_epollFD, EPOLL_CTL_DEL, handle->handle, 0);

#else /* !_WIN32 */

  // close connection event
  CloseHandle(handle->handle);
  delIndex_(handle->index);

#endif /* !_WIN32 */
}

#ifdef _WIN32
void Loop::delIndex_(unsigned i)
{
  // splice out this object
  m_wfmoHandles.splice(i, 1);
  m_wfmoData.splice(i, 1);
  // adjust indices
  if (m_wakeSemIndex > i) --m_wakeSemIndex;
  for (unsigned n = m_wfmoData.length(); i < n; i++) {
    auto u64 = m_wfmoData[i];
    if (u64_is_socket(u64)) {
      auto socket = u64_ptr<Socket>(u64);
      --socket->index;
    } else if (u64_is_handle(u64)) {
      auto handle = u64_ptr<Handle>(u64);
      --handle->index;
    }
  }
}
#endif /* !_WIN32 */

// simulate connection failure, for testing purposes only
void Loop::disconnect(Zi::Socket socket)
{
  if (Zi::nullSocket(socket)) return;

#ifndef _WIN32

  ::close(socket);

#else /* !_WIN32 */

  ::closesocket(socket);

#endif /* !_WIN32 */
}

void Loop::close(Zi::Handle handle)
{
  if (Zi::nullHandle(handle)) return;

#ifndef _WIN32

  ::close(handle);

#else /* !_WIN32 */

  CloseHandle(handle);

#endif /* !_WIN32 */
}

void Loop::run_()
{
  // ZiLOG(Debug, "ZiEventLoop", ([](auto &s) { }));

  started();

  for (;;) {

#ifndef _WIN32

    epoll_event ev[8];

    // ZiLOG(Debug, "ZiEventLoop", ([](auto &s) { s << "epoll_wait()..."; }));

again:
    int r = epoll_wait(m_epollFD, ev, 8, -1); // max events is 8

    // ZiLOG(Debug, "ZiEventLoop", ([r](auto &s) { s << "epoll_wait(): " << r; }));

    if (r < 0) {
      auto e = errno;
      if (e == EINTR || e == EAGAIN) goto again;
      failed(ZeEXCEPT(Fatal, "ZiEventLoop", ([e](auto &s) {
	s << "epoll_wait() failed: " << e;
      })));
      return;
    }
    for (unsigned i = 0; i < unsigned(r); i++) {
      uint32_t events = ev[i].events;
      auto u64 = ev[i].data.u64;

      /* ZiLOG(Debug, "ZiEventLoop", ([events, v](auto &s) {
	s << "epoll_wait() events=" << events << " v=" << v
	  << " u64=" << ZuBoxPtr(u64).hex()
	  << " EPOLLIN=" << ZuBoxed(EPOLLIN).hex()
	  << " EPOLLOUT=" << ZuBoxed(EPOLLOUT).hex();
      })); */

      if (u64_is_socket(u64)) {
	auto socket = u64_ptr<Socket>(u64);

	if (events & EPOLLOUT)
	  socket->send(socket->socket);
	if (events & (EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR))
	  socket->recv(socket->socket);
      } else if (u64_is_handle(u64)) {
	auto handle = u64_ptr<Handle>(u64);

	if (events & EPOLLOUT)
	  handle->send(handle->handle);
	if (events & (EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR))
	  handle->recv(handle->handle);
      } else { // u64_is_wake(u64)
	char c;
	int r = ::read(m_wakeFD, &c, 1);
	if (r >= 1) return;
	if (r < 0) {
	  ZeError e{errno};
	  if (e.errNo() != EINTR && e.errNo() != EAGAIN) return;
	}
      }
    }

#else /* !_WIN32 */

    auto n = m_wfmoHandles.length();
    DWORD event = WaitForMultipleObjectsEx(
	n, &m_wfmoHandles[0], false, INFINITE, false);
    if (event == WAIT_FAILED) {
      failed(ZeEXCEPT(Fatal, "ZiEventLoop", ([e = ZeLastError](auto &s) {
	s << "WaitForMultipleObjectsEx() failed: " << e;
      })));
      return;
    }
    if (event >= WAIT_OBJECT_0 && event < WAIT_OBJECT_0 + n) {
      unsigned i = event - WAIT_OBJECT_0;
      auto u64 = m_wfmoData[i];
      if (u64_is_socket(u64)) {
	auto socket = u64_ptr<Socket>(u64);

	WSANETWORKEVENTS events;
	auto rc = WSAEnumNetworkEvents(
	  socket->socket, m_wfmoHandles[socket->index], &events);
	if (rc != 0) {
	  failed(ZeEXCEPT(Fatal, "ZiEventLoop", ([e = WSAGetLastError()](auto &s) {
	    s << "WSAEnumNetworkEvents() failed: " << e;
	  })));
	  return;
	}
	if ((events.lNetworkEvents & (FD_WRITE|FD_CLOSE)) == FD_WRITE)
	  socket->send(socket->socket);
	if (events.lNetworkEvents & (FD_READ|FD_OOB|FD_CLOSE))
	  socket->recv(socket->socket);
      } else if (u64_is_handle(u64)) {
	auto handle = u64_ptr<Handle>(u64);
	if (handle->send) handle->send(handle->handle);
	if (handle->recv) handle->recv(handle->handle);
      } else { // u64_is_wake(u64)
	// LATER WFMO should have decremented the semaphore, but test this,
	// we may need to:
	// switch (WaitForSingleObject(m_wakeSem, 0)) {
	//   case WAIT_OBJECT_0: return;
	//   case WAIT_TIMEOUT:  break;
	// }
	return;
      }
    }

#endif /* !_WIN32 */

  }
}

void Loop::started()
{
  // ZiLOG(Debug, "ZiEventLoop", ([](auto &s) { }));

  auto startFn = ZuMv(m_startFn);

  m_startFn = StartFn{};

  if (startFn) startFn(StartResult{});
}

void Loop::start_failed(ZeException e)
{
  // ZiLOG(Debug, "ZiEventLoop", ([](auto &s) { }));

  stop_2();

  auto startFn = ZuMv(m_startFn);

  m_startFn = StartFn{};

  if (startFn) startFn(StartResult{ZuMv(e)});
}

void Loop::failed(Exception e)
{
  if (m_failFn)
    m_failFn(ZuMv(e));
  else
    ZiLog::log(ZuMv(e));
}

} // ZiEvent
