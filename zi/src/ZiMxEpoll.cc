//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#if defined(__linux__)

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>

#include "ZiMxEpoll.hh"

#include <zlib/ZmGuard.hh>

#include <zlib/ZtHexDump.hh>

#if !defined(IPV6_JOIN_GROUP) && defined(IPV6_ADD_MEMBERSHIP)
#define IPV6_JOIN_GROUP IPV6_ADD_MEMBERSHIP
#endif

static unsigned tcpMPS(Zi::Socket s)
{
  int mss = 0;
  socklen_t len = sizeof(mss);
  if (getsockopt(s, IPPROTO_TCP, TCP_MAXSEG, &mss, &len) < 0 || mss <= 0)
    return ~uint32_t(0);
  return unsigned(mss);
}

static unsigned udpMPS(Zi::Socket s, int ipType)
{
  int mtu = 0;
  socklen_t len = sizeof(mtu);
  bool v6 = ipType == ZiIPType::V6;
  if (getsockopt(s, v6 ? IPPROTO_IPV6 : IPPROTO_IP,
      v6 ? IPV6_MTU : IP_MTU, (char *)&mtu, &len)) return ~uint32_t(0);
  int overhead = v6 ? 48 : 28;
  return mtu > overhead ? unsigned(mtu - overhead) : ~uint32_t(0);
}

#define Log(severity, op, result, error) \
  ZiLOG(severity, "ZiMxEpoll", \
    ([result_ = result, error_ = error](auto &s) { \
    s << op << ' ' << Zi::ioResult(result_) << ' ' << error_; \
  }))
#define Error(op, result, error) Log(Error, op, result, error)
#define Warning(op, result, error) Log(Warning, op, result, error)

static int family(ZiIPType::T type)
{
  switch (type) {
    case ZiIPType::V6:
      return AF_INET6;
    default:
      return AF_INET;
  }
}

static ZiIPType::T txTOSType(const ZiSockAddr &addr, ZiIP local, ZiIP remote)
{
  ZiIP ip = addr ? addr.ip() : remote;
  if (!ip) ip = local;
  return ip.v4() || ip.mappedV4() ? ZiIPType::V4 : ZiIPType::V6;
}

#ifndef EPOLLRDHUP
#define EPOLLRDHUP 0
#endif

// 64-bit pointer-packing for epoll u64

static inline uint64_t u64_cxn(void *ptr) {
  return reinterpret_cast<uintptr_t>(ptr);
}
static inline bool u64_is_cxn(uint64_t v) { return !(v>>62); }
static inline uint64_t u64_listener(void *ptr) {
  return reinterpret_cast<uintptr_t>(ptr) | (uint64_t(1)<<62);
}
static inline bool u64_is_listener(uint64_t v) { return (v>>62) == 1; }
static inline uint64_t u64_connect(void *ptr) {
  return reinterpret_cast<uintptr_t>(ptr) | (uint64_t(2)<<62);
}
static inline bool u64_is_connect(uint64_t v) { return (v>>62) == 2; }
static constexpr uint64_t u64_event() { return uint64_t(3)<<62; }
static inline bool u64_is_event(uint64_t v) { return (v>>62) == 3; }
template <typename T>
inline T *u64_ptr(uint64_t v) {
  return reinterpret_cast<T *>(v & ~(uint64_t(3)<<62));
}

static bool recvTOS(Zi::Socket s, ZiIPType::T type)
{
  int b = 1;
  switch (type) {
    case ZiIPType::V6:
#ifdef IPV6_RECVTCLASS
      if (setsockopt(s, IPPROTO_IPV6, IPV6_RECVTCLASS, &b, sizeof(b)) < 0)
	return false;
#ifdef IP_RECVTOS
      // A dual-stack UDP socket receives IPv4-mapped packets with IP_TOS.
      (void)setsockopt(s, IPPROTO_IP, IP_RECVTOS, &b, sizeof(b));
#endif
      return true;
#else
      return false;
#endif
    default:
#ifdef IP_RECVTOS
      return setsockopt(s, IPPROTO_IP, IP_RECVTOS, &b, sizeof(b)) >= 0;
#else
      return false;
#endif
  }
}

static ZiTOS getTOS(const struct msghdr &msg)
{
  for (auto cmsg = CMSG_FIRSTHDR(&msg); cmsg;
      cmsg = CMSG_NXTHDR(const_cast<struct msghdr *>(&msg), cmsg)) {
    if (cmsg->cmsg_level == IPPROTO_IP && cmsg->cmsg_type == IP_TOS) {
      if (cmsg->cmsg_len >= CMSG_LEN(sizeof(int)))
	return ZiTOS(uint8_t(*reinterpret_cast<int *>(CMSG_DATA(cmsg))));
      if (cmsg->cmsg_len >= CMSG_LEN(sizeof(uint8_t)))
	return ZiTOS(*CMSG_DATA(cmsg));
    }
#ifdef IPV6_TCLASS
    if (cmsg->cmsg_level == IPPROTO_IPV6 &&
	cmsg->cmsg_type == IPV6_TCLASS &&
	cmsg->cmsg_len >= CMSG_LEN(sizeof(int)))
      return ZiTOS(uint8_t(*reinterpret_cast<int *>(CMSG_DATA(cmsg))));
#endif
  }
  return {};
}

static void putTOS(struct msghdr &msg, void *control, unsigned controlLen, ZiTOS tos,
    int level, int type)
{
  if (!tos.template is<uint8_t>()) return;
  msg.msg_control = control;
  msg.msg_controllen = controlLen;
  auto cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level = level;
  cmsg->cmsg_type = type;
  cmsg->cmsg_len = CMSG_LEN(sizeof(int));
  *reinterpret_cast<int *>(CMSG_DATA(cmsg)) = tos.template p<uint8_t>();
  msg.msg_controllen = cmsg->cmsg_len;
}
ZiMxEpoll::ZiMxEpoll(const ZiMxParams &params) :
  m_listeners(new ListenerHash(ZmHashParams().bits(4).loadFactor(1).cBits(4).
    init(params.listenerHash()))),
  m_cxns(new CxnHash(ZmHashParams().bits(8).loadFactor(1).cBits(4).
    init(params.cxnHash()))),
  m_epollMaxFDs(params.epollMaxFDs()),
  m_connects(new ConnectHash(ZmHashParams().bits(5).loadFactor(1).cBits(4).
    init(params.requestHash())))
{
}

static void blockSignals()
{
  sigset_t s;
  sigemptyset(&s);
  sigaddset(&s, SIGPIPE);
  sigaddset(&s, SIGURG);
  pthread_sigmask(SIG_BLOCK, &s, 0);
}

void ZiMxEpoll::mxBound()
{
  mx()->threadInit([] { blockSignals(); });
}

void ZiMxEpoll::start()
{
  blockSignals();
  if ((m_epollFD = epoll_create(m_epollMaxFDs)) < 0) {
    ZeError e{errno};
    mx()->pluginStarted(ZeEXCEPT(Error, "ZiMultiplex",
      ([e = e](auto &s) { s << "epoll_create" << ' ' << e; })));
    return;
  }
  // Preserve one wake per queued rx() callback without a pipe's byte limit.
  int wakeFD = eventfd(0, EFD_NONBLOCK | EFD_SEMAPHORE);
  if (wakeFD < 0) {
    ZeError e{errno};
    ::close(m_epollFD); m_epollFD = -1;
    mx()->pluginStarted(ZeEXCEPT(Error, "ZiMultiplex",
      ([e = e](auto &s) { s << "eventfd" << ' ' << e; })));
    return;
  }
  {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(struct epoll_event));
    ev.events = EPOLLIN;
    ev.data.u64 = u64_event();
    if (epoll_ctl(m_epollFD, EPOLL_CTL_ADD, wakeFD, &ev) < 0) {
      ZeError e{errno};
      ::close(m_epollFD); m_epollFD = -1;
      ::close(wakeFD);
      mx()->pluginStarted(ZeEXCEPT(Error, "ZiMultiplex",
        ([e = e](auto &s) { s << "epoll_ctl(EPOLL_CTL_ADD)" << ' ' << e; })));
      return;
    }
  }
  {
    ZmGuard guard{m_wakeLock};
    m_wakeFD = wakeFD;
  }
  mx()->pluginStarted({});
}

void ZiMxEpoll::stopped()
{
  ZmGuard guard{m_wakeLock};
  if (m_wakeFD >= 0) { ::close(m_wakeFD); m_wakeFD = -1; }
  if (m_epollFD >= 0) { ::close(m_epollFD); m_epollFD = -1; }
}

void ZiMxEpoll::stop()
{
  mx()->rxInvoke([this] {
    m_stopping = true;
    stopAccepting();
    if (!m_cxns->count_()) { finishStop(); return; }
    auto i = m_cxns->citer();
    while (ZmRef<ZiConnection> cxn = i.val()) cxn->disconnect();
  });
}

void ZiMxEpoll::stopAccepting()
{
  {
    ConnectHash::Iter i(*m_connects);
    while (ZmRef<Connect> connect = i()) {
      Socket s = connect->info().socket;
      epoll_ctl(m_epollFD, EPOLL_CTL_DEL, s, nullptr);
      i.del();
      ::close(s);
    }
  }

  {
    ListenerHash::Iter i(*m_listeners);
    while (ZmRef<Listener> listener = i()) {
      Socket s = listener->info().socket;
      epoll_ctl(m_epollFD, EPOLL_CTL_DEL, s, nullptr);
      i.del();
      listener->down();
      ::close(s);
    }
  }
}

void ZiMxEpoll::finishStop()
{
  m_stopping = false;
  mx()->pluginStopped({});
}

void ZiMxEpoll::udp(ZiConnectFn fn, ZiFailFn failFn,
  ZiIPType::T ipType, ZiIP localIP, uint16_t localPort,
  ZiIP remoteIP, uint16_t remotePort,
  Zi::IOBufAllocator *rxAllocator, ZiCxnOptions options)
{
  Socket s;

  s = (Socket)::socket(family(ipType), SOCK_DGRAM, IPPROTO_UDP);
  if (s < 0) {
    Error("socket", Zi::IOError, ZeLastSockError);
    failFn(false); // FD exhaustion is generally not transient
    return;
  }

  if (options.multicast()) {
    int b = 1;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
	  (const char *)&b, sizeof(int)) < 0) {
      ZeError e{errno};
      ::close(s);
      Error("setsockopt(SO_REUSEADDR)", Zi::IOError, e);
      failFn(false);
      return;
    }
  }

  if (!!localIP || localPort) {
    if (!localIP) localIP = ZiMultiplex::wildcard(ipType);
    ZiSockAddr local(localIP, localPort);
    if (::bind(s, local.sa(), local.len()) < 0) {
      ZeError e{errno};
      ::close(s);
      Warning("bind", Zi::IOError, e);
      failFn(true);
      return;
    }
  }

  if (!!remoteIP) {
    ZiSockAddr remote(remoteIP, remotePort);
    if (::connect(s, remote.sa(), remote.len()) < 0) {
      ZeError e{errno};
      ::close(s);
      // Warning("connect", Zi::IOError, e);
      failFn(true);
      return;
    }
  }

  if (!recvTOS(s, ipType))
    Warning("setsockopt(RECVTOS)", Zi::IOError, ZeLastSockError);

  if (options.multicast()) {
    switch (ipType) {
      case ZiIPType::V4:
	if (!!options.mif()) {
	  if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF,
		(const char *)&options.mif().inAddr(), sizeof(struct in_addr)) < 0) {
	    ZeError e{errno};
	    ::close(s);
	    Error("setsockopt(IP_MULTICAST_IF)", Zi::IOError, e);
	    failFn(false);
	    return;
	  }
	}
	if (options.ttl() > 0) {
	  int ttl = options.ttl();
	  if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL,
		(const char *)&ttl, sizeof(int)) < 0) {
	    ZeError e{errno};
	    ::close(s);
	    Error("setsockopt(IP_MULTICAST_TTL)", Zi::IOError, e);
	    failFn(false);
	    return;
	  }
	}
	{
	  int b = options.loopBack();
	  if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP,
		(const char *)&b, sizeof(int)) < 0) {
	    ZeError e{errno};
	    ::close(s);
	    Error("setsockopt(IP_MULTICAST_LOOP)", Zi::IOError, e);
	    failFn(false);
	    return;
	  }
	}
	if (unsigned n = options.mreqs().length()) {
	  for (unsigned i = 0; i < n; i++) {
	    if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP,
		  (const char *)&(options.mreqs())[i].ipMReq(),
		  sizeof(struct ip_mreq)) < 0) {
	      ZeError e{errno};
	      ::close(s);
	      Error("setsockopt(IP_ADD_MEMBERSHIP)", Zi::IOError, e);
	      failFn(false);
	      return;
	    }
	  }
	}
	break;
      case ZiIPType::V6:
	if (options.mifIndex()) {
	  int index = options.mifIndex();
	  if (setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_IF,
		(const char *)&index, sizeof(int)) < 0) {
	    ZeError e{errno};
	    ::close(s);
	    Error("setsockopt(IPV6_MULTICAST_IF)", Zi::IOError, e);
	    failFn(false);
	    return;
	  }
	}
	if (options.ttl() > 0) {
	  int hops = options.ttl();
	  if (setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
		(const char *)&hops, sizeof(int)) < 0) {
	    ZeError e{errno};
	    ::close(s);
	    Error("setsockopt(IPV6_MULTICAST_HOPS)", Zi::IOError, e);
	    failFn(false);
	    return;
	  }
	}
	{
	  int b = options.loopBack();
	  if (setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
		(const char *)&b, sizeof(int)) < 0) {
	    ZeError e{errno};
	    ::close(s);
	    Error("setsockopt(IPV6_MULTICAST_LOOP)", Zi::IOError, e);
	    failFn(false);
	    return;
	  }
	}
	if (unsigned n = options.mreqs().length()) {
	  for (unsigned i = 0; i < n; i++) {
	    if (setsockopt(s, IPPROTO_IPV6, IPV6_JOIN_GROUP,
		  (const char *)&(options.mreqs())[i].ip6MReq(),
		  sizeof(ZiIPv6MReq)) < 0) {
	      ZeError e{errno};
	      ::close(s);
	      Error("setsockopt(IPV6_JOIN_GROUP)", Zi::IOError, e);
	      failFn(false);
	      return;
	    }
	  }
	}
	break;
      default:
	::close(s);
	Error("multicast", Zi::IOError, ZeError(ZiEINVAL));
	failFn(false);
	return;
      }
  }

  if (fcntl(s, F_SETFL, O_NONBLOCK) < 0) {
    ZeError e{errno};
    ::close(s);
    Error("fcntl(F_SETFL, O_NONBLOCK)", Zi::IOError, e);
    failFn(false);
    return;
  }

  ZmRef<ZiConnection> cxn;

  cxn = fn(ZiCxnInfo {
      ZiCxnType::UDP, s, options,
      {localIP, localPort}, {remoteIP, remotePort},
      !!remoteIP ? udpMPS(s, ipType) : ~uint32_t(0), rxAllocator });

  if (!cxn) {
    Zi::closeSocket(s);
    return;
  }

  if (!cxnAdd(cxn, s)) return;

  connected(cxn);

  ZiDEBUG(mx(), ([socket = s, ip = remoteIP, port = remotePort](auto &s) {
    s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>()
      << " UDP CONNECTED to " << ip << ':' << port;
  }));
}

void ZiMxEpoll::connect(ZiConnectFn fn, ZiFailFn failFn,
  ZiIPType::T ipType, ZiIP localIP, uint16_t localPort,
  ZiIP remoteIP, uint16_t remotePort,
  Zi::IOBufAllocator *rxAllocator, ZiCxnOptions options)
{
  Socket s = ::socket(family(ipType), SOCK_STREAM, IPPROTO_TCP);
  if (Zi::nullSocket(s)) {
    ZeError e = ZeLastSockError;
    Error("socket", Zi::IOError, e);
    failFn(false);
    return;
  }

  if (!initSocket(s, options)) {
    Zi::closeSocket(s);
    failFn(false);
    return;
  }

  if (!!localIP || localPort) {
    if (!localIP) localIP = ZiMultiplex::wildcard(ipType);
    ZiSockAddr local(localIP, localPort);
    if (::bind(s, local.sa(), local.len()) < 0) {
      ZeError e{errno};
      ::close(s);
      Warning("bind", Zi::IOError, e);
      failFn(true);
      return;
    }
  }

  ZiDEBUG(mx(), ([socket = s, ip = remoteIP, port = remotePort](auto &s) {
    s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>()
      << " CONNECTING to " << ip << ':' << port;
  }));

  ZiSockAddr remote(remoteIP, remotePort);

  ZmRef<Connect> request = new Connect(fn, failFn,
      ZiCxnType::TCPOut, s, options,
      ZiSockAddr{localIP, localPort}, remote, ~uint32_t(0), rxAllocator);

  if (!connectAdd(request, s)) {
    Zi::closeSocket(s);
    request->fail(false);
    return;
  }

  connect(request);
}

void ZiMxEpoll::listen(ZiListenFn listenFn, ZiFailFn failFn, ZiConnectFn acceptFn,
  ZiIP localIP, uint16_t localPort, unsigned nAccepts,
  Zi::IOBufAllocator *rxAllocator, ZiCxnOptions options)
{
  Socket lsocket;
  ZiIPType::T ipType = !!localIP ? localIP.type() : ZiIPType::V4;
  if (!localIP) localIP = ZiMultiplex::wildcard(ipType);

  lsocket = (Socket)::socket(family(ipType), SOCK_STREAM, IPPROTO_TCP);
  if (lsocket < 0) {
    Error("socket", Zi::IOError, ZeLastSockError);
    failFn(false);
    return;
  }

  {
    int b = 1;
    if (setsockopt(lsocket,
	  SOL_SOCKET, SO_REUSEADDR, (const char *)&b, sizeof(int)) < 0) {
      ZeError e{errno};
      ::close(lsocket);
      Error("setsockopt(SO_REUSEADDR)", Zi::IOError, e);
      failFn(false);
      return;
    }
  }

  if (ipType == ZiIPType::V6) {
    int b = 1;
    if (setsockopt(lsocket,
	  IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&b, sizeof(int)) < 0) {
      ZeError e{errno};
      ::close(lsocket);
      Error("setsockopt(IPV6_V6ONLY)", Zi::IOError, e);
      failFn(false);
      return;
    }
  }

  {
    ZiSockAddr local(localIP, localPort);
    if (::bind(lsocket, local.sa(), local.len()) < 0) {
      ZeError e{errno};
      ::close(lsocket);
      Warning("bind", Zi::IOError, e);
      failFn(true);
      return;
    }
  }

  if (fcntl(lsocket, F_SETFL, O_NONBLOCK) < 0) {
    ZeError e{errno};
    ::close(lsocket);
    Error("fcntl(F_SETFL, O_NONBLOCK)", Zi::IOError, e);
    failFn(false);
    return;
  }

  if (!localPort) {
    ZiSockAddr local;
    local.init(ipType);
    socklen_t len = local.len();
    if (getsockname(lsocket, local.sa(), &len) < 0) {
      ZeError e{errno};
      ::close(lsocket);
      Error("getsockname", Zi::IOError, e);
      failFn(false);
      return;
    }
    local.sync();
    localPort = local.port();
  }

  ZmRef<Listener> listener = new Listener(
      acceptFn, lsocket, nAccepts, localIP, localPort, options, rxAllocator);

  if (::listen(lsocket, nAccepts) < 0) {
    ZeError e{errno};
    ::close(lsocket);
    Error("listen", Zi::IOError, e);
    failFn(false);
    return;
  }

  if (!listenerAdd(listener, lsocket)) {
    Zi::closeSocket(lsocket);
    failFn(false);
    return;
  }

  listenFn(listener->info());
}

void ZiMxEpoll::stopListening(ZiIP localIP, uint16_t localPort)
{
  Socket lsocket;

  {
    ListenerHash::CIter i(*m_listeners);
    ZmRef<Listener> listener;

    while (listener = i()) {
      if (localIP == listener->info().ip &&
	  localPort == listener->info().port) {
	lsocket = listener->info().socket;
	goto found;
      }
    }
    return;
  }

found:
  listenerDel(lsocket);

  Zi::closeSocket(lsocket);
}

void ZiMxEpoll::completedConnect(Connect *request)
{
  ZiCxnInfo &ci = request->info();
  Socket s = ci.socket;

  int errNo = 0;
  socklen_t l = sizeof(int);
  if (getsockopt(s, SOL_SOCKET, SO_ERROR, &errNo, &l) < 0) {
    ZeError e{errno};
    connectDel(s);
    ::close(s);
    Error("getsockopt(SO_ERROR)", Zi::IOError, e);
    request->fail(false);
    return;
  }
  if (errNo) {
    connectDel(s);
    ::close(s);
    request->fail(true);
    return;
  }

  ci.local.init(ci.remote.type());
  socklen_t len = ci.local.len();
  if (getsockname(s, ci.local.sa(), &len) < 0) {
    ZeError e{errno};
    connectDel(s);
    ::close(s);
    Error("getsockname", Zi::IOError, e);
    request->fail(false);
    return;
  }
  ci.local.sync();

  ZiConnectFn fn = request->takeFn();
  connectDel(s);
  executedConnect(ZuMv(fn), ci);
}

// attempt completion of non-blocking connect
void ZiMxEpoll::connect(Connect *request)
{
  ZiCxnInfo &ci = request->info();
  Socket s = ci.socket;

  if (ZuUnlikely(ci.options.udp())) { // should never occur at this point
    connectDel(s);
    ::close(s);
    Error("connect", Zi::IOError, ZeError(ZiEINVAL));
    request->fail(false);
    return;
  }

retry:
  if (::connect(s, ci.remote.sa(), ci.remote.len()) < 0) {
    ZeError e{errno};
    if (e.errNo() == EAGAIN || e.errNo() == EINPROGRESS) return;
    if (e.errNo() == EINTR) goto retry;
    connectDel(s);
    ::close(s);
    // Warning("connect", Zi::IOError, e);
    request->fail(true);
    return;
  }

  completedConnect(request);
}

void ZiMxEpoll::executedConnect(ZiConnectFn fn, ZiCxnInfo &ci)
{
  ci.mps = tcpMPS(ci.socket);
  ZmRef<ZiConnection> cxn = fn(ci);

  if (!cxn) {
    Zi::closeSocket(ci.socket);
    return;
  }

  if (!cxnAdd(cxn, ci.socket)) return;

  connected(cxn);

  ZiDEBUG(mx(), ([
    socket = ci.socket,
    ip = ci.remote.ip(),
    port = ci.remote.port()
  ](auto &s) {
    s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>()
      << " TCP CONNECTED to " << ip << ':' << port;
  }));
}

void ZiMxEpoll::accept(Listener *listener)
{
  ZmRef<Listener> hold{listener};
  int s;
  ZeError e;
  ZiSockAddr remote;

retry:
  if (!listener->up()) return;
  remote.init(listener->info().ip.type());
  socklen_t len = remote.len();
  s = ::accept(listener->info().socket, remote.sa(), &len);
  if (s < 0) {
    e = errno;
    if (e.errNo() == EAGAIN) return;
    if (e.errNo() == EINTR) goto retry;
    Error("accept", Zi::IOError, e);
    return;
  }

  if (!initSocket(s, listener->info().options)) {
    ::close(s);
    goto retry;
  }
  remote.sync();

  ZiDEBUG(mx(), ([
    socket = s,
    ip = remote.ip(),
    port = remote.port()
  ](auto &s) {
    s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>()
      << " ACCEPTING from " << ip << ':' << port;
  }));

  ZiCxnInfo ci{
      ZiCxnType::TCPIn, s, listener->info().options,
      {listener->info().ip, listener->info().port},
      remote, ~uint32_t(0), listener->info().rxAllocator };
  executedConnect(listener->acceptFn(), ci);

  goto retry;
}

bool ZiMxEpoll::initSocket(Socket s, const ZiCxnOptions &options)
{
  {
    int b = 1;
    static struct linger l = { 0, 0 };
    unsigned rxBufSize = mx()->rxBufSize();
    unsigned txBufSize = mx()->txBufSize();
    if (rxBufSize && setsockopt(s, SOL_SOCKET, SO_RCVBUF,
	  (const char *)&rxBufSize, sizeof(unsigned)) < 0) {
      Error("setsockopt(SO_RCVBUF)", Zi::IOError, ZeLastSockError);
      return false;
    }
    if (txBufSize && setsockopt(s, SOL_SOCKET, SO_SNDBUF,
	  (const char *)&txBufSize, sizeof(unsigned)) < 0) {
      Error("setsockopt(SO_SNDBUF)", Zi::IOError, ZeLastSockError);
      return false;
    }
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
	  (const char *)&b, sizeof(int)) < 0) {
      Error("setsockopt(SO_REUSEADDR)", Zi::IOError, ZeLastSockError);
      return false;
    }
    if (setsockopt(s, SOL_SOCKET, SO_LINGER,
	  (const char *)&l, sizeof(struct linger)) < 0) {
      Error("setsockopt(SO_LINGER)", Zi::IOError, ZeLastSockError);
      return false;
    }
    if (options.keepAlive() && setsockopt(s, SOL_SOCKET, SO_KEEPALIVE,
	  (const char *)&b, sizeof(int)) < 0) {
      Error("setsockopt(SO_KEEPALIVE)", Zi::IOError, ZeLastSockError);
      return false;
    }
    if (!options.udp() && !options.nagle() &&
	setsockopt(s, IPPROTO_TCP, TCP_NODELAY,
	  (const char *)&b, sizeof(int)) < 0) {
      Error("setsockopt(TCP_NODELAY)", Zi::IOError, ZeLastSockError);
      return false;
    }
  }

  if (fcntl(s, F_SETFL, O_NONBLOCK) < 0) {
    Error("fcntl(O_NONBLOCK)", Zi::IOError, ZeLastError);
    return false;
  }

  return true;
}

bool ZiMxEpoll::cxnAdd(ZiConnection *cxn, Socket s)
{
  m_cxns->add(cxn);

  {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(struct epoll_event));
    ev.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET;
    ev.data.u64 = u64_cxn(cxn);
    if (epoll_ctl(m_epollFD, EPOLL_CTL_ADD, s, &ev) < 0) {
      ZeError e{errno};
      // ZiConnection retains ownership of the socket.
      m_cxns->del(s);
      Error("epoll_ctl(EPOLL_CTL_ADD)", Zi::IOError, e);
      return false;
    }
  }

  return true;
}

bool ZiMxEpoll::listenerAdd(Listener *listener, Socket s)
{
  m_listeners->addNode(listener);

  {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(struct epoll_event));
    ev.events = EPOLLIN | EPOLLET;
    ev.data.u64 = u64_listener(listener);
    if (epoll_ctl(m_epollFD, EPOLL_CTL_ADD, s, &ev) < 0) {
      ZeError e{errno};
      m_listeners->del(s);
      Error("epoll_ctl(EPOLL_CTL_ADD)", Zi::IOError, e);
      return false;
    }
  }

  return true;
}

void ZiMxEpoll::listenerDel(Socket s)
{
  epoll_ctl(m_epollFD, EPOLL_CTL_DEL, s, 0);

  if (ZmRef<Listener> listener = m_listeners->del(s))
    listener->down();
}

bool ZiMxEpoll::connectAdd(Connect *request, Socket s)
{
  m_connects->addNode(request);

  {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(struct epoll_event));
    ev.events = EPOLLOUT;
    ev.data.u64 = u64_connect(request);
    if (epoll_ctl(m_epollFD, EPOLL_CTL_ADD, s, &ev) < 0) {
      ZeError e{errno};
      m_connects->del(s);
      Error("epoll_ctl(EPOLL_CTL_ADD)", Zi::IOError, e);
      return false;
    }
  }

  return true;
}

void ZiMxEpoll::connectDel(Socket s)
{
  epoll_ctl(m_epollFD, EPOLL_CTL_DEL, s, 0);

  m_connects->del(s);
}

void ZiMxEpoll::connected(ZiConnection *cxn)
{
  cxn->connected(cxn->rxContext());
  if (cxn->rxContext().completed()) epollRecv(cxn, cxn->info().socket, 0);
}

void ZiMxEpoll::recv(ZiConnection *cxn)
{
  if (!epollRecv(cxn, cxn->info().socket, EPOLLIN | EPOLLRDHUP))
    cxn->rxContext().complete();
  else
    pumpRx(cxn); // strictly speaking this pump-priming should not be needed,
	    // but edge-triggered epoll does not behave identically
	    // under valgrind; while this call does potentially reduce
	    // latency, that benefit is marginal - in a latency-sensitive
	    // app, recv_() is invoked just once or twice per connection
	    // during initial setup - the receive context will remain
	    // open for the bulk of the communication
}

bool ZiMxEpoll::epollRecv(ZiConnection *cxn, int s, uint32_t events)
{
  struct epoll_event ev;
  memset(&ev, 0, sizeof(struct epoll_event));
  ev.events = events | EPOLLOUT | EPOLLET;
  ev.data.u64 = reinterpret_cast<uintptr_t>(cxn);
  if (epoll_ctl(m_epollFD, EPOLL_CTL_MOD, s, &ev) < 0) {
    Error("epoll_ctl(EPOLL_CTL_MOD)", Zi::IOError, ZeLastError);
    return false;
  }
  return true;
}

bool ZiMxEpoll::pumpRx(ZiConnection *cxn)
{
#ifdef ZiMultiplex_DEBUG
  if (mx()->trace()) mx()->traceCapture();
#endif

  if (ZuUnlikely(!cxn->rxUp().load_())) {
    cxn->rxContext().complete();
    return false;
  }

  if (ZuUnlikely(cxn->rxContext().completed()))
    return true;

  ZmAssert(cxn->rxContext().buf);

  unsigned len = cxn->rxContext().buf->avail();
  auto buf = cxn->rxContext().buf->end();

  ZeError e;
  int n;

#ifdef ZiMultiplex_DEBUG
  if (ZuUnlikely(!cxn->info().options.udp() && mx()->frag())) {
    unsigned l = ((cxn->rxContext().buf->length + 8)>>1) + 1;
    if (len > l) len = l;
  }
  if (ZuUnlikely(mx()->debug())) {
    ioctl(cxn->info().socket, FIONREAD, &n);
    ZiDEBUG(mx(), ([
      socket = cxn->info().socket,
      len,
      size = cxn->rxContext().buf->size,
      offset = cxn->rxContext().buf->length,
      buffered = n
    ](auto &s) {
      s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>()
	<< " recv(" << len << ") size: " << size
	<< " offset: " << offset << " buffered: " << buffered;
    }));
  }
#endif

retry:
  if (cxn->info().options.udp()) {
    ZiIPType::T type;
    if (!ZiMultiplex::cxnType(cxn->info().local.ip(), cxn->info().remote.ip(), type))
      type = ZiIPType::V4;
    cxn->rxContext().addr.init(type);
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = len;
    alignas(struct cmsghdr) uint8_t control[CMSG_SPACE(sizeof(int))];
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_name = cxn->rxContext().addr.sa();
    msg.msg_namelen = cxn->rxContext().addr.len();
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);
    n = ::recvmsg(cxn->info().socket, &msg, 0);
    if (n >= 0) {
      cxn->rxContext().addr.sync();
      cxn->rxContext().tos = getTOS(msg);
      cxn->rxContext().flags = msg.msg_flags;
    }
  } else {
    n = ::recv(
      cxn->info().socket, buf, len, 0);
  }
  if (ZuUnlikely(n < 0)) e = errno;

  ZiDEBUG(mx(), ([
    socket = cxn->info().socket,
    len,
    n,
    errNo = int(e.errNo())
  ](auto &s) {
    s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>()
      << " recv(" << len << "): " << n << " errno: " << errNo
      << " (EAGAIN=" << EAGAIN << " EINTR=" << EINTR << ')';
  }));

  if (ZuUnlikely(n < 0)) {
    if (e.errNo() == EAGAIN) {
#ifdef ZiMultiplex_DEBUG
      if (mx()->yield()) Zm::yield();
#endif
      return true;
    }
    if (e.errNo() == EINTR) goto retry;
    if (cxn->info().options.udp() &&
	(e.errNo() == ZiECONNREFUSED || e.errNo() == ZiECONNRESET))
      return true;
    cxn->errorRecv("recv", Zi::IOError, e);
    cxn->rxContext().complete();
    return false;
  }

  ZiDEBUG(mx(), ([
    socket = cxn->info().socket,
    len,
    n,
    buf = ZiMultiplex::debugBuf(buf, n)
  ](auto &s) {
    s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>()
      << " recv(" << len << "): " << n << '\n'
      << ZtHexDump_(buf.cspan());
  }));

  if (ZuUnlikely(!n && !cxn->info().options.udp())) {
    cxn->rxContext().complete();
    cxn->disconnect(true);
    return false;
  }

  cxn->executedRecv(n);

  if (ZuUnlikely(cxn->rxContext().completed())) {
    if (cxn->rxContext().disconnected()) {
      cxn->disconnect(false);
      return false;
    } else {
      epollRecv(cxn, cxn->info().socket, 0);
      return true;
    }
  }

  ZmAssert(cxn->rxContext().buf);
  if (ZuUnlikely(!cxn->rxContext().buf->avail())) {
    cxn->rxContext().complete();
    epollRecv(cxn, cxn->info().socket, 0);
    return true;
  }

  len = cxn->rxContext().buf->avail();
  buf = cxn->rxContext().buf->end();
  goto retry;
}
void ZiMxEpoll::send(ZiConnection *cxn)
{
  unsigned n;

  auto buf = cxn->txContext().buf->data() + cxn->txContext().offset;
  unsigned len = cxn->txContext().buf->length - cxn->txContext().offset;
  ZeError e;

  int n_;

#ifdef ZiMultiplex_FILTER
  if (ZuUnlikely(mx()->filterTx(cxn, buf, len))) {
    n = len;
    goto executed;
  }
#endif

retry:
  ZiDEBUG(mx(), ([
    socket = cxn->info().socket,
    size = cxn->txContext().buf->length,
    offset = cxn->txContext().offset,
    buf = ZiMultiplex::debugBuf(buf, len)
  ](auto &s) {
    s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>()
      << " send(" << buf.length() << ") size: " << size
      << " offset: " << offset << '\n'
      << ZtHexDump_(buf.cspan());
  }));

  if (cxn->info().options.udp()) {
    struct iovec iov;
    iov.iov_base = reinterpret_cast<char *>(buf);
    iov.iov_len = len;
    alignas(struct cmsghdr) uint8_t control[CMSG_SPACE(sizeof(int))];
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    if (!!cxn->txContext().addr) {
      msg.msg_name = cxn->txContext().addr.sa();
      msg.msg_namelen = cxn->txContext().addr.len();
    }
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    ZiIPType::T type = txTOSType(
      cxn->txContext().addr, cxn->info().local.ip(), cxn->info().remote.ip());
    if (type == ZiIPType::V6) {
#ifdef IPV6_TCLASS
      putTOS(msg, control, sizeof(control), cxn->txContext().tos,
	IPPROTO_IPV6, IPV6_TCLASS);
#endif
    } else
      putTOS(msg, control, sizeof(control), cxn->txContext().tos,
	IPPROTO_IP, IP_TOS);
    n_ = ::sendmsg(cxn->info().socket, &msg, 0);
  } else if (ZuUnlikely(cxn->txContext().disconnected())) {
    iovec iov{buf, len};
    msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    n_ = ::sendmsg(cxn->info().socket, &msg, MSG_MORE);
  } else
    n_ = ::send(cxn->info().socket, buf, len, 0);
  if (ZuUnlikely(n_ < 0)) e = errno;

  ZiDEBUG(mx(), ([
    socket = cxn->info().socket,
    len,
    n = n_,
    e
  ](auto &s) {
    s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>()
      << " send(" << len << "): " << n << " errno: " << e
      << " (EAGAIN=" << EAGAIN << " EINTR=" << EINTR << ')';
  }));

  if (ZuUnlikely(n_ < 0)) {
    if (e.errNo() == EAGAIN) {
#ifdef ZiMultiplex_DEBUG
      if (mx()->yield()) Zm::yield();
#endif
      return;
    }
    if (e.errNo() == EINTR) goto retry;
    cxn->errorSend("send", Zi::IOError, e);
    return;
  }
  n = n_;

  ZiDEBUG(mx(), ([
    socket = cxn->info().socket,
    len,
    n
  ](auto &s) {
    s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>()
      << " send(" << len << "): " << n;
  }));

#ifdef ZiMultiplex_FILTER
executed:
#endif
  if (!cxn->executedSend(n)) return;

  buf = cxn->txContext().buf->data() + cxn->txContext().offset;
  len = cxn->txContext().buf->length - cxn->txContext().offset;

  goto retry;
}

void ZiMxEpoll::disconnect(ZiConnection *cxn, bool peer)
{
  // A peer FIN closes its sending half. Our marked suffix can still drain.
  if (cxn->txContext().disconnected() && !cxn->txContext().completed()) {
    if (peer) cxn->send(); // EOF and writability can share one epoll event
    return;
  }
  if (!cxn->txUp().load_()) return;
  cxn->txUp() = false;
  cxn->txContext().complete();
  mx()->rxRun([this, cxn = ZmRef(cxn), peer] { disconnectRx(cxn, peer); });
}

void ZiMxEpoll::disconnectRx(ZiConnection *cxn, bool peer)
{
  if (!cxn->rxUp().load_()) {
    retired(cxn, peer);
    return;
  }

  cxn->rxContext().complete();
  cxn->rxUp() = 0;

  if (cxn->info().options.udp()) {
    retired(cxn, peer);
    return;
  }

  ZiDEBUG(mx(), ([socket = cxn->info().socket](auto &s) {
    s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>() <<
      " disconnect()";
  }));

  ZeError e;
  int i;

retry:
  i = ::shutdown(cxn->info().socket, SHUT_RDWR);
  if (i < 0) {
    e = errno;
    if (e.errNo() == EINTR) goto retry;
    retired(cxn, peer);
    if (e.errNo() != ZiENOTCONN) Error("shutdown", Zi::IOError, e);
    return;
  }
  retired(cxn, peer);
}
void ZiMxEpoll::close(ZiConnection *cxn, bool peer)
{
  if (!cxn->txUp().load_()) return;
  cxn->txUp() = false;

  cxn->txContext().complete();

  mx()->rxRun([this, cxn = ZmRef(cxn), peer] { closeRx(cxn, peer); });
}

void ZiMxEpoll::closeRx(ZiConnection *cxn, bool peer)
{
  if (!cxn->rxUp().load_()) {
    retired(cxn, peer);
    return;
  }

  cxn->rxContext().complete();
  cxn->rxUp() = 0;

  ZiDEBUG(mx(), ([socket = cxn->info().socket](auto &s) {
    s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>() << " close()";
  }));

  retired(cxn, peer);
}

void ZiMxEpoll::retired(ZiConnection *cxn, bool peer)
{
  Socket s = cxn->info().socket;
  if (Zi::nullSocket(s)) return;
  ZmRef<ZiConnection> hold{cxn};
  epoll_ctl(m_epollFD, EPOLL_CTL_DEL, s, nullptr);
  m_cxns->del(s);
  cxn->notifyDisconnected(peer);
  if (m_stopping && !m_cxns->count_()) finishStop();
}

void ZiMxEpoll::rx()
{
  bool wake = false;

  int r;
  ZeError e;
  auto ev = static_cast<epoll_event *>(
    ZuAlloca(mx()->rxQuantum() * sizeof(epoll_event), alignof(epoll_event)));
  if (ZuUnlikely(!ev)) return;

  for (;;) {
    ZiDEBUG(mx(), ([
      nThreads = mx()->params().nThreads(),
      nConnections = m_cxns->count_(),
      epollFD = m_epollFD,
      wakeFD = m_wakeFD,
      nListeners = m_listeners->count_()
    ](auto &s) {
      s << "wait() nThreads: " << ZuBoxed(nThreads).fmt<ZuFmt::Right<2>>()
	<< " nConnections: " <<
	  ZuBoxed(nConnections).fmt<ZuFmt::Right<4>>()
	<< " epollFD: " << ZuBoxed(epollFD).fmt<ZuFmt::Right<3>>()
	<< " wakeFD: " << ZuBoxed(wakeFD).fmt<ZuFmt::Right<3>>()
	<< " nListeners: " << ZuBoxed(nListeners).fmt<ZuFmt::Right<3>>();
    }));


    r = epoll_wait(m_epollFD, ev, mx()->rxQuantum(), -1);


    if (r < 0) {
      e = errno;
      if (e.errNo() == EINTR || e.errNo() == EAGAIN) continue;
      Error("epoll_wait", Zi::IOError, e);
      break;
    }

    if (ZuLikely(r)) {
      for (unsigned i = 0; i < unsigned(r); i++) {
	uint32_t events = ev[i].events;
	uintptr_t v = ev[i].data.u64;

	if (ZuLikely(u64_is_cxn(v))) {
	  ZiConnection *cxn = u64_ptr<ZiConnection>(v);
	  if (events & (EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR))
	    if (ZuUnlikely(!pumpRx(cxn))) continue;
	  if (events & EPOLLOUT)
	    mx()->txRun([cxn = ZmRef(cxn)]() { cxn->send(); });
	  continue;
	}

	if (ZuLikely(u64_is_event(v))) { wake |= readWake(); continue; }

	if (ZuLikely(u64_is_listener(v))) {
	  Listener *listener = u64_ptr<Listener>(v);
	  if (ZuLikely(!(events & EPOLLERR))) {
	    accept(listener);
	    continue;
	  }
	  int n;
	  int s = listener->info().socket;
	  if (ioctl(s, FIONREAD, &n) < 0) {
	    e = errno;
	    listenerDel(s);
	    ::close(s);
	    Error("listen", Zi::IOError, e);
	  }
	  continue;
	}

	if (ZuLikely(u64_is_connect(v))) {
	  ZmRef<Connect> request = u64_ptr<Connect>(v);
	  if (ZuLikely(!(events & EPOLLERR))) {
	    completedConnect(request);
	    continue;
	  }
	  int n;
	  int s = request->info().socket;
	  if (ioctl(s, FIONREAD, &n) < 0)
	    e = errno;
	  else {
	    int errNo = EIO;
	    socklen_t l = sizeof(int);
	    if (getsockopt(s, SOL_SOCKET, SO_ERROR, &errNo, &l) < 0)
	      errNo = EIO;
	    e = errNo;
	  }
	  connectDel(s);
	  ::close(s);
	  // Warning("connect", Zi::IOError, e);
	  request->fail(true);
	  continue;
	}
      }
    }

    if (wake) break;
  }
}

bool ZiMxEpoll::readWake()
{
  ZiDEBUG(mx(), ([wakeFD = m_wakeFD](auto &s) {
    s << "FD: " << ZuBoxed(wakeFD).fmt<ZuFmt::Right<3>>() << " readWake";
  }));

  uint64_t count;
  return ::read(m_wakeFD, &count, sizeof(count)) == sizeof(count);
}

void ZiMxEpoll::wake()
{
  ZmGuard guard{m_wakeLock};
  if (m_wakeFD < 0) return;
  ZiDEBUG(mx(), ([wakeFD = m_wakeFD](auto &s) {
    s << "FD: " << ZuBoxed(wakeFD).fmt<ZuFmt::Right<3>>() << " writeWake";
  }));

  uint64_t count = 1;
  while (::write(m_wakeFD, &count, sizeof(count)) < 0) {
    ZeError e{errno};
    if (e.errNo() != EINTR) {
      Error("write", Zi::IOError, e);
      break;
    }
  }
}

#endif
