//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#if defined(_WIN32)

#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0a000003 // Windows 10 1703 (SIO_TCP_INFO)
#endif
#include "ZiMxIOCP.hh"

#include <mstcpip.h>

#include <zlib/ZmGuard.hh>

#include <zlib/ZtHexDump.hh>

#if !defined(IPV6_JOIN_GROUP) && defined(IPV6_ADD_MEMBERSHIP)
#define IPV6_JOIN_GROUP IPV6_ADD_MEMBERSHIP
#endif

static unsigned tcpMPS(Zi::Socket s)
{
  DWORD version = 0, len = 0;
  TCP_INFO_v0 info{};
  if (WSAIoctl(s, SIO_TCP_INFO, &version, sizeof(version),
      &info, sizeof(info), &len, nullptr, nullptr)) return ~uint32_t(0);
  return info.Mss;
}

static unsigned udpMPS(Zi::Socket s, int ipType)
{
  int mtu = 0;
  int len = sizeof(mtu);
  bool v6 = ipType == ZiIPType::V6;
  if (getsockopt(s, v6 ? IPPROTO_IPV6 : IPPROTO_IP,
      v6 ? IPV6_MTU : IP_MTU, (char *)&mtu, &len)) return ~uint32_t(0);
  int overhead = v6 ? 48 : 28;
  return mtu > overhead ? unsigned(mtu - overhead) : ~uint32_t(0);
}

#define Log(severity, op, result, error) \
  ZiLOG(severity, "ZiMxIOCP", \
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

extern "C" {
  typedef BOOL (PASCAL *PConnectEx)(
      SOCKET s, const struct sockaddr *sa, int salen, void *ptr, DWORD len,
      DWORD *count, OVERLAPPED *overlapped);
  typedef BOOL (PASCAL *PAcceptEx)(
      SOCKET l, SOCKET s, void *ptr, DWORD len, int localsalen, int remotesalen,
      DWORD *count, OVERLAPPED *overlapped);
  typedef void (PASCAL *PGetAcceptExSockaddrs)(
      void *buf, DWORD len, DWORD localaddrlen, DWORD remoteaddrlen,
      struct sockaddr **localsa, int *localsalen,
      struct sockaddr **remotesa, int *remotesalen);
  typedef BOOL (PASCAL *PDisconnectEx)(
      SOCKET s, OVERLAPPED *overlapped, DWORD flags, DWORD reserved);
  typedef INT (PASCAL *PWSARecvMsg)(
      SOCKET s, LPWSAMSG msg, LPDWORD count, LPWSAOVERLAPPED overlapped,
      LPWSAOVERLAPPED_COMPLETION_ROUTINE completion);
  typedef INT (PASCAL *PWSASendMsg)(
      SOCKET s, LPWSAMSG msg, DWORD flags, LPDWORD count,
      LPWSAOVERLAPPED overlapped,
      LPWSAOVERLAPPED_COMPLETION_ROUTINE completion);
}

#ifndef WSAID_CONNECTEX
#define WSAID_CONNECTEX \
  {0x25a207b9,0xddf3,0x4660,{0x8e,0xe9,0x76,0xe5,0x8c,0x74,0x06,0x3e}}
#endif
#ifndef WSAID_ACCEPTEX
#define WSAID_ACCEPTEX \
  {0xb5367df1,0xcbac,0x11cf,{0x95,0xca,0x00,0x80,0x5f,0x48,0xa1,0x92}}
#endif
#ifndef WSAID_GETACCEPTEXSOCKADDRS
#define WSAID_GETACCEPTEXSOCKADDRS \
  {0xb5367df2,0xcbac,0x11cf,{0x95,0xca,0x00,0x80,0x5f,0x48,0xa1,0x92}}
#endif
#ifndef WSAID_DISCONNECTEX
#define WSAID_DISCONNECTEX \
  {0x7fda2e11,0x8630,0x436f,{0xa0,0x31,0xf5,0x36,0xa6,0xee,0xc1,0x57}}
#endif

class ZiMxWSExt {
public:
  ZiMxWSExt();
  ~ZiMxWSExt();

  BOOL connectEx(
      SOCKET s, const struct sockaddr *sa, int salen, void *ptr, DWORD len,
      DWORD *count, OVERLAPPED *overlapped) {
    if (!m_connectEx) { WSASetLastError(WSASYSNOTREADY); return FALSE; }
    return (*m_connectEx)(s, sa, salen, ptr, len, count, overlapped);
  }
  BOOL acceptEx(
      SOCKET l, SOCKET s, void *ptr, DWORD len, int localsalen, int remotesalen,
      DWORD *count, OVERLAPPED *overlapped) {
    if (!m_acceptEx) { WSASetLastError(WSASYSNOTREADY); return FALSE; }
    return (*m_acceptEx)(l, s, ptr, len, localsalen, remotesalen,
			 count, overlapped);
  }
  void getAcceptExSockaddrs(
      void *buf, DWORD len, DWORD localaddrlen, DWORD remoteaddrlen,
      struct sockaddr **localsa, int *localsalen,
      struct sockaddr **remotesa, int *remotesalen) {
    if (!m_getAcceptExSockaddrs) { WSASetLastError(WSASYSNOTREADY); return; }
    (*m_getAcceptExSockaddrs)(buf, len, localaddrlen, remoteaddrlen,
			      localsa, localsalen, remotesa, remotesalen);
  }
  BOOL disconnectEx(
      SOCKET s, OVERLAPPED *overlapped, DWORD flags, DWORD reserved) {
    if (!m_disconnectEx) { WSASetLastError(WSASYSNOTREADY); return FALSE; }
    return (*m_disconnectEx)(s, overlapped, flags, reserved);
  }
  LPFN_TRANSMITPACKETS transmitPackets() const { return m_transmitPackets; }
  INT recvMsg(
      SOCKET s, LPWSAMSG msg, LPDWORD count, LPWSAOVERLAPPED overlapped,
      LPWSAOVERLAPPED_COMPLETION_ROUTINE completion) {
    if (!m_recvMsg) { WSASetLastError(WSASYSNOTREADY); return SOCKET_ERROR; }
    return (*m_recvMsg)(s, msg, count, overlapped, completion);
  }
  INT sendMsg(
      SOCKET s, LPWSAMSG msg, DWORD flags, LPDWORD count,
      LPWSAOVERLAPPED overlapped,
      LPWSAOVERLAPPED_COMPLETION_ROUTINE completion) {
    if (!m_sendMsg) { WSASetLastError(WSASYSNOTREADY); return SOCKET_ERROR; }
    return (*m_sendMsg)(s, msg, flags, count, overlapped, completion);
  }

  static ZiMxWSExt *instance();

private:
  PConnectEx		m_connectEx;
  PAcceptEx		m_acceptEx;
  PGetAcceptExSockaddrs	m_getAcceptExSockaddrs;
  PDisconnectEx		m_disconnectEx;
  LPFN_TRANSMITPACKETS	m_transmitPackets = nullptr;
  PWSARecvMsg		m_recvMsg;
  PWSASendMsg		m_sendMsg;
};

ZiMxWSExt *ZiMxWSExt::instance()
{
  return
    ZmSingleton<ZiMxWSExt,
      ZmSingletonCleanup<ZmCleanup::Platform>>::instance();
}

ZiMxWSExt::ZiMxWSExt()
{
  using Socket = Zi::Socket;
  Socket s = (Socket)::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (Zi::nullSocket(s)) goto error;
  {
    static GUID connectExGUID = WSAID_CONNECTEX;
    static GUID acceptExGUID = WSAID_ACCEPTEX;
    static GUID getAcceptExSockaddrsGUID = WSAID_GETACCEPTEXSOCKADDRS;
    static GUID disconnectExGUID = WSAID_DISCONNECTEX;
    static GUID transmitPacketsGUID = WSAID_TRANSMITPACKETS;
    static GUID recvMsgGUID = WSAID_WSARECVMSG;
    static GUID sendMsgGUID = WSAID_WSASENDMSG;

    DWORD n;
    if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER,
		 &connectExGUID, sizeof(GUID),
		 &m_connectEx, sizeof(PConnectEx), &n, 0, 0) ||
	n != sizeof(PConnectEx))
      goto error;

    if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER,
		 &acceptExGUID, sizeof(GUID),
		 &m_acceptEx, sizeof(PAcceptEx), &n, 0, 0) ||
	n != sizeof(PAcceptEx))
      goto error;

    if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER,
		 &getAcceptExSockaddrsGUID, sizeof(GUID),
		 &m_getAcceptExSockaddrs,
		 sizeof(PGetAcceptExSockaddrs), &n, 0, 0) ||
	n != sizeof(PGetAcceptExSockaddrs))
      goto error;

    if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER,
		 &disconnectExGUID, sizeof(GUID),
		 &m_disconnectEx,
		 sizeof(PDisconnectEx), &n, 0, 0) ||
	n != sizeof(PDisconnectEx))
      goto error;

    if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER,
		 &transmitPacketsGUID, sizeof(GUID),
		 &m_transmitPackets, sizeof(m_transmitPackets), &n, 0, 0) ||
	n != sizeof(m_transmitPackets))
      m_transmitPackets = nullptr;

    if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER,
		 &recvMsgGUID, sizeof(GUID),
		 &m_recvMsg, sizeof(PWSARecvMsg), &n, 0, 0) ||
	n != sizeof(PWSARecvMsg))
      m_recvMsg = nullptr;

    if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER,
		 &sendMsgGUID, sizeof(GUID),
		 &m_sendMsg, sizeof(PWSASendMsg), &n, 0, 0) ||
	n != sizeof(PWSASendMsg))
      m_sendMsg = nullptr;
  }
  ::closesocket(s);
  return;

error:
  if (!Zi::nullSocket(s)) ::closesocket(s);
  m_connectEx = nullptr;
  m_acceptEx = nullptr;
  m_getAcceptExSockaddrs = nullptr;
  m_disconnectEx = nullptr;
  m_recvMsg = nullptr;
  m_sendMsg = nullptr;
}

ZiMxWSExt::~ZiMxWSExt()
{
}

#define ConnectEx(s, sa, salen, ptr, len, count, overlapped) \
  (ZiMxWSExt::instance()->connectEx(s, sa, salen, ptr, len, \
					    count, overlapped))
#define AcceptEx(l, s, ptr, len, localsalen, remotesalen, count, overlapped) \
  (ZiMxWSExt::instance()->acceptEx(l, s, ptr, len, localsalen, \
					   remotesalen, count, overlapped))
#define GetAcceptExSockaddrs(buf, len, localaddrlen, remoteaddrlen, \
			     localsa, localsalen, remotesa, remotesalen) \
  (ZiMxWSExt::instance()->getAcceptExSockaddrs(buf, len, \
						       localaddrlen, \
						       remoteaddrlen, \
						       localsa, localsalen, \
						       remotesa, remotesalen))
#define DisconnectEx(s, overlapped, flags, reserved) \
  (ZiMxWSExt::instance()->disconnectEx(s, overlapped, flags, reserved))
#define WSARecvMsg(s, msg, count, overlapped, completion) \
  (ZiMxWSExt::instance()->recvMsg( \
    s, msg, count, overlapped, completion))
#define WSASendMsg(s, msg, flags, count, overlapped, completion) \
  (ZiMxWSExt::instance()->sendMsg( \
    s, msg, flags, count, overlapped, completion))

static bool recvTOS(Zi::Socket s, ZiIPType::T type)
{
  DWORD b = TRUE;
  switch (type) {
    case ZiIPType::V6:
#ifdef IPV6_RECVTCLASS
      if (setsockopt(s,
	IPPROTO_IPV6, IPV6_RECVTCLASS, (const char *)&b, sizeof(b)))
	return false;
#ifdef IP_RECVTOS
      (void)setsockopt(s, IPPROTO_IP, IP_RECVTOS, (const char *)&b, sizeof(b));
#endif
      return true;
#else
      return false;
#endif
    default:
#ifdef IP_RECVTOS
      return !setsockopt(s, IPPROTO_IP, IP_RECVTOS, (const char *)&b, sizeof(b));
#else
      return false;
#endif
  }
}

static ZiTOS getTOS(WSAMSG &msg)
{
  for (auto cmsg = WSA_CMSG_FIRSTHDR(&msg); cmsg;
      cmsg = WSA_CMSG_NXTHDR(&msg, cmsg)) {
    if (cmsg->cmsg_level == IPPROTO_IP && cmsg->cmsg_type == IP_TOS &&
	cmsg->cmsg_len >= WSA_CMSG_LEN(sizeof(int)))
      return ZiTOS(uint8_t(*reinterpret_cast<int *>(WSA_CMSG_DATA(cmsg))));
#ifdef IPV6_TCLASS
    if (cmsg->cmsg_level == IPPROTO_IPV6 &&
	cmsg->cmsg_type == IPV6_TCLASS &&
	cmsg->cmsg_len >= WSA_CMSG_LEN(sizeof(int)))
      return ZiTOS(uint8_t(*reinterpret_cast<int *>(WSA_CMSG_DATA(cmsg))));
#endif
  }
  return {};
}

static void putTOS(WSAMSG &msg, WSABUF &control, ZiTOS tos, int level, int type)
{
  if (!tos.template is<uint8_t>()) return;
  msg.Control = control;
  auto cmsg = WSA_CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level = level;
  cmsg->cmsg_type = type;
  cmsg->cmsg_len = WSA_CMSG_LEN(sizeof(int));
  *reinterpret_cast<int *>(WSA_CMSG_DATA(cmsg)) = tos.template p<uint8_t>();
  msg.Control.len = WSA_CMSG_SPACE(sizeof(int));
}
ZiMxIOCP::ZiMxIOCP(const ZiMxParams &params) :
  m_listeners(new ListenerHash(ZmHashParams().bits(4).loadFactor(1).cBits(4).
    init(params.listenerHash()))),
  m_cxns(new CxnHash(ZmHashParams().bits(8).loadFactor(1).cBits(4).
    init(params.cxnHash()))),
  m_connects(new ConnectHash(ZmHashParams().bits(5).loadFactor(1).cBits(4).
    init(params.requestHash())))
{
}

void ZiMxIOCP::start()
{
  {
    WSADATA wd;
    DWORD errNo = WSAStartup(MAKEWORD(2, 2), &wd);
    if (errNo) {
      mx()->pluginStarted(ZeEXCEPT(Error, "ZiMultiplex",
        ([e = ZeError(errNo)](auto &s) { s << "WSAStartup" << ' ' << e; })));
      return;
    }
  }

  HANDLE port =
    CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, mx()->params().nThreads());
  if (!port) {
    ZeError e(GetLastError());
    WSACleanup();
    mx()->pluginStarted(ZeEXCEPT(Error, "ZiMultiplex",
      ([e = e](auto &s) { s << "CreateIoCompletionPort" << ' ' << e; })));
    return;
  }
  {
    ZmGuard guard{m_wakeLock};
    m_completionPort = port;
  }
  mx()->pluginStarted({});
}

void ZiMxIOCP::stopped()
{
  ZmGuard guard{m_wakeLock};
  if (!m_completionPort) return;
  CloseHandle(m_completionPort);
  m_completionPort = nullptr;

  WSACleanup();	// this is reference counted
}

void ZiMxIOCP::stop()
{
  mx()->rxInvoke([this] {
    m_stopping = true;
    stopListeners();
    cancelConnects();
    if (!m_cxns->count_()) { finishStop(); return; }
    auto i = m_cxns->citer();
    while (ZmRef<ZiConnection> cxn = i.val()) cxn->disconnect();
  });
}

void ZiMxIOCP::cancelConnects()
{
  auto iter = m_connects->citer();
  while (auto request = iter()) Zi::closeSocket(request->info().socket);
}

void ZiMxIOCP::stopListeners()
{
  {
    ListenerHash::Iter i(*m_listeners);
    while (ZmRef<Listener> listener = i()) {
      i.del();
      listener->down();
      Zi::closeSocket(listener->info().socket);
    }
  }
}

void ZiMxIOCP::udp(ZiConnectFn fn, ZiFailFn failFn,
  ZiIPType::T ipType, ZiIP localIP, uint16_t localPort,
  ZiIP remoteIP, uint16_t remotePort,
  Zi::IOBufAllocator *rxAllocator, ZiCxnOptions options)
{
  Socket s;

  s = (Socket)::socket(family(ipType), SOCK_DGRAM, IPPROTO_UDP);
  if (Zi::nullSocket(s)) {
    Error("socket", Zi::IOError, ZeLastSockError);
    failFn(false);
    return;
  }

  if (options.multicast()) {
    BOOL b = TRUE;
    if (setsockopt(
	  s, SOL_SOCKET, SO_REUSEADDR, (const char *)&b, sizeof(BOOL)) < 0) {
      ZeError e(WSAGetLastError());
      ::closesocket(s);
      Error("setsockopt(SO_REUSEADDR)", Zi::IOError, e);
      failFn(false);
      return;
    }
  }

  {
    if (!localIP) localIP = ZiMultiplex::wildcard(ipType);
    ZiSockAddr local(localIP, localPort);
    if (::bind(s, local.sa(), local.len())) {
      ZeError e(WSAGetLastError());
      ::closesocket(s);
      Warning("bind", Zi::IOError, e);
      failFn(true);
      return;
    }
  }

  if (!!remoteIP) {
    ZiSockAddr remote(remoteIP, remotePort);
    if (::connect(s, remote.sa(), remote.len())) {
      ZeError e(WSAGetLastError());
      ::closesocket(s);
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
		(const char *)&options.mif().inAddr(), sizeof(struct in_addr))) {
	    ZeError e(WSAGetLastError());
	    ::closesocket(s);
	    Error("setsockopt(IP_MULTICAST_IF)", Zi::IOError, e);
	    failFn(false);
	    return;
	  }
	}
	if (options.ttl() > 0) {
	  DWORD ttl = options.ttl();
	  if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL,
		(const char *)&ttl, sizeof(DWORD))) {
	    ZeError e(WSAGetLastError());
	    ::closesocket(s);
	    Error("setsockopt(IP_MULTICAST_TTL)", Zi::IOError, e);
	    failFn(false);
	    return;
	  }
	}
	{
	  BOOL b = options.loopBack();
	  if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP,
		(const char *)&b, sizeof(BOOL))) {
	    ZeError e(WSAGetLastError());
	    ::closesocket(s);
	    Error("setsockopt(IP_MULTICAST_LOOP)", Zi::IOError, e);
	    failFn(false);
	    return;
	  }
	}
	if (int n = options.mreqs().length()) {
	  for (int i = 0; i < n; i++) {
	    if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP,
		  (const char *)&(options.mreqs())[i].ipMReq(),
		  sizeof(struct ip_mreq))) {
	      ZeError e(WSAGetLastError());
	      ::closesocket(s);
	      Error("setsockopt(IP_ADD_MEMBERSHIP)", Zi::IOError, e);
	      failFn(false);
	      return;
	    }
	  }
	}
	break;
      case ZiIPType::V6:
	if (options.mifIndex()) {
	  DWORD index = options.mifIndex();
	  if (setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_IF,
		(const char *)&index, sizeof(DWORD))) {
	    ZeError e(WSAGetLastError());
	    ::closesocket(s);
	    Error("setsockopt(IPV6_MULTICAST_IF)", Zi::IOError, e);
	    failFn(false);
	    return;
	  }
	}
	if (options.ttl() > 0) {
	  DWORD hops = options.ttl();
	  if (setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
		(const char *)&hops, sizeof(DWORD))) {
	    ZeError e(WSAGetLastError());
	    ::closesocket(s);
	    Error("setsockopt(IPV6_MULTICAST_HOPS)", Zi::IOError, e);
	    failFn(false);
	    return;
	  }
	}
	{
	  BOOL b = options.loopBack();
	  if (setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
		(const char *)&b, sizeof(BOOL))) {
	    ZeError e(WSAGetLastError());
	    ::closesocket(s);
	    Error("setsockopt(IPV6_MULTICAST_LOOP)", Zi::IOError, e);
	    failFn(false);
	    return;
	  }
	}
	if (int n = options.mreqs().length()) {
	  for (int i = 0; i < n; i++) {
	    if (setsockopt(s, IPPROTO_IPV6, IPV6_JOIN_GROUP,
		  (const char *)&(options.mreqs())[i].ip6MReq(),
		  sizeof(ZiIPv6MReq))) {
	      ZeError e(WSAGetLastError());
	      ::closesocket(s);
	      Error("setsockopt(IPV6_JOIN_GROUP)", Zi::IOError, e);
	      failFn(false);
	      return;
	    }
	  }
	}
	break;
      default:
	::closesocket(s);
	Error("multicast", Zi::IOError, ZeError(ZiEINVAL));
	failFn(false);
	return;
    }
  }

  if (!CreateIoCompletionPort((HANDLE)s, m_completionPort, 0, 0)) {
    ZeError e(GetLastError());
    ::closesocket(s);
    Error("CreateIoCompletionPort", Zi::IOError, e);
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

  cxnAdd(cxn);

  connected(cxn);

  ZiDEBUG(mx(), ([socket = s, ip = remoteIP, port = remotePort](auto &s) {
    s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>()
      << " UDP CONNECTED to " << ip << ':' << port;
  }));
}

void ZiMxIOCP::connect(ZiConnectFn fn, ZiFailFn failFn,
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

  { // ConnectEx requires a bound socket, including for an ephemeral port
    if (!localIP) localIP = ZiMultiplex::wildcard(ipType);
    ZiSockAddr local(localIP, localPort);
    if (::bind(s, local.sa(), local.len())) {
      ZeError e(WSAGetLastError());
      ::closesocket(s);
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

  ZmRef<Connect> request = new Connect(
      this, fn, failFn,
      ZiCxnType::TCPOut, s, options,
      ZiSockAddr{localIP, localPort}, remote, ~uint32_t(0), rxAllocator);
  m_connects->addNode(request);
  Overlapped &overlapped = request->overlapped();
  ZeError e;
  if (ZuLikely(ConnectEx(s, remote.sa(), remote.len(), 0, 0, 0,
	  &overlapped) ||
	(e = WSAGetLastError()).errNo() == WSA_IO_PENDING))
    return;
  m_connects->del(s);
  ::closesocket(s);
  Error("ConnectEx", Zi::IOError, e);
  failFn(false);
  return;
}

void ZiMxIOCP::listen(ZiListenFn listenFn, ZiFailFn failFn, ZiConnectFn acceptFn,
  ZiIP localIP, uint16_t localPort, unsigned nAccepts,
  Zi::IOBufAllocator *rxAllocator, ZiCxnOptions options)
{
  Socket lsocket;
  ZiIPType::T ipType = !!localIP ? localIP.type() : ZiIPType::V4;
  if (!localIP) localIP = ZiMultiplex::wildcard(ipType);

  lsocket = (Socket)::socket(family(ipType), SOCK_STREAM, IPPROTO_TCP);
  if (Zi::nullSocket(lsocket)) {
    Error("socket", Zi::IOError, ZeLastSockError);
    failFn(false);
    return;
  }

  {
    BOOL b = TRUE;
    if (setsockopt(lsocket,
	  SOL_SOCKET, SO_REUSEADDR, (const char *)&b, sizeof(BOOL))) {
      ZeError e(WSAGetLastError());
      ::closesocket(lsocket);
      Error("setsockopt(SO_REUSEADDR)", Zi::IOError, e);
      failFn(false);
      return;
    }
  }

  if (ipType == ZiIPType::V6) {
    BOOL b = TRUE;
    if (setsockopt(lsocket,
	  IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&b, sizeof(BOOL))) {
      ZeError e(WSAGetLastError());
      ::closesocket(lsocket);
      Error("setsockopt(IPV6_V6ONLY)", Zi::IOError, e);
      failFn(false);
      return;
    }
  }

  {
    ZiSockAddr local(localIP, localPort);
    if (::bind(lsocket, local.sa(), local.len())) {
      ZeError e(WSAGetLastError());
      ::closesocket(lsocket);
      Warning("bind", Zi::IOError, e);
      failFn(true);
      return;
    }
  }

  if (!localPort) {
    ZiSockAddr local;
    local.init(ipType);
    int len = local.len();
    if (getsockname(lsocket, local.sa(), &len)) {
      ZeError e(WSAGetLastError());
      ::closesocket(lsocket);
      Error("getsockname", Zi::IOError, e);
      failFn(false);
      return;
    }
    local.sync();
    localPort = local.port();
  }

  ZmRef<Listener> listener = new Listener(
      this, acceptFn, lsocket, nAccepts, localIP, localPort, options, rxAllocator);

  m_listeners->addNode(listener);

  if (!CreateIoCompletionPort((HANDLE)lsocket, m_completionPort, 0, 0)) {
    ZeError e(GetLastError());
    listenerDel(lsocket);
    ::closesocket(lsocket);
    Error("CreateIoCompletionPort", Zi::IOError, e);
    failFn(false);
    return;
  }

  if (::listen(lsocket, nAccepts)) {
    ZeError e(WSAGetLastError());
    listenerDel(lsocket);
    ::closesocket(lsocket);
    Error("listen", Zi::IOError, e);
    failFn(false);
    return;
  }

  listenFn(listener->info());

  for (unsigned i = 0; i < nAccepts; i++) accept(listener);
}

void ZiMxIOCP::stopListening(ZiIP localIP, uint16_t localPort)
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

  // Closing the listener cancels its pending accepts.
  Zi::closeSocket(lsocket);
}

void ZiMxIOCP::overlappedConnect(Connect_ *request,
    int status, unsigned, ZeError e)
{
  ZiCxnInfo &ci = request->info();
  auto hold = m_connects->del(ci.socket);
  if (m_stopping) return;

  if (ZuUnlikely(status != Zi::OK)) {
    ::closesocket(ci.socket);
    Error("ConnectEx", status, e);
    request->fail(true);
    return;
  }

  if (setsockopt(ci.socket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0)) {
    ZeError e(WSAGetLastError());
    ::closesocket(ci.socket);
    Error("setsockopt(SO_UPDATE_CONNECT_CONTEXT)", Zi::IOError, e);
    request->fail(false);
    return;
  }

  {
    ci.local.init(ci.remote.type());
    int len = ci.local.len();
    if (getsockname(ci.socket, ci.local.sa(), &len)) {
      ZeError e(WSAGetLastError());
      ::closesocket(ci.socket);
      Error("getsockname", Zi::IOError, e);
      request->fail(false);
      return;
    }
    ci.local.sync();
  }

  executedConnect(request->fn(), ci);
}

void ZiMxIOCP::executedConnect(ZiConnectFn fn, ZiCxnInfo &ci)
{
  ci.mps = tcpMPS(ci.socket);
  ZmRef<ZiConnection> cxn = fn(ci);

  if (!cxn) {
    Zi::closeSocket(ci.socket);
    return;
  }

  cxnAdd(cxn);

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

void ZiMxIOCP::accept(Listener *listener)
{
  if (!listener->up()) return;
  Socket s = (Socket)::socket(
    family(listener->info().ip.type()), SOCK_STREAM, IPPROTO_TCP);
  if (Zi::nullSocket(s)) {
    Error("socket", Zi::IOError, ZeLastSockError);
    return;
  }

  if (!initSocket(s, listener->info().options)) {
    ::closesocket(s);
    return;
  }

  Accept *request = new Accept(listener);
  request->info().socket = s;
  Overlapped &overlapped = request->overlapped();
  ZeError e;
  if (ZuLikely(AcceptEx(listener->info().socket, s, request->buf(), 0,
	  ZiSockAddr::MaxLen + 16,
	  ZiSockAddr::MaxLen + 16,
	  0, &overlapped) ||
	(e = WSAGetLastError()).errNo() == WSA_IO_PENDING)) {
    ++m_pendingAccepts;
    return;
  }
  delete request;
  ::closesocket(s);
  Error("AcceptEx", Zi::IOError, e);
}

void ZiMxIOCP::overlappedAccept(Accept *request,
    int status, unsigned, ZeError e)
{
  --m_pendingAccepts;
  ZmRef<Listener> listener = request->listener();

  if (ZuUnlikely(!listener->up())) {
    ::closesocket(request->info().socket);
    return;
  }

  accept(listener); // re-invoke

  ZiCxnInfo &ci = request->info();

  if (ZuUnlikely(status != Zi::OK)) {
    ::closesocket(ci.socket);
    Error("AcceptEx", status, e);
    return;
  }

  if (setsockopt(ci.socket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
	(const char *)&listener->info().socket, sizeof(SOCKET))) {
    e = WSAGetLastError();
    ::closesocket(ci.socket);
    Error("setsockopt", Zi::IOError, e);
    return;
  }

  {
    sockaddr *localAddr, *remoteAddr;
    int localLen, remoteLen;
    GetAcceptExSockaddrs(
	request->buf(), 0, ZiSockAddr::MaxLen + 16,
	ZiSockAddr::MaxLen + 16, &localAddr, &localLen, &remoteAddr, &remoteLen);
    memcpy(static_cast<void *>(&ci.local), localAddr, localLen);
    memcpy(static_cast<void *>(&ci.remote), remoteAddr, remoteLen);
    ci.local.sync();
    ci.remote.sync();
  }

  ci.rxAllocator = listener->info().rxAllocator;
  executedConnect(listener->acceptFn(), ci);
}

bool ZiMxIOCP::initSocket(Socket s, const ZiCxnOptions &options)
{
  {
    BOOL b = TRUE;
    unsigned rxBufSize = mx()->rxBufSize();
    unsigned txBufSize = mx()->txBufSize();
    if (rxBufSize && setsockopt(s, SOL_SOCKET, SO_RCVBUF,
	  (const char *)&rxBufSize, sizeof(unsigned))) {
      Error("setsockopt(SO_RCVBUF)", Zi::IOError, ZeLastSockError);
      return false;
    }
    if (txBufSize && setsockopt(s, SOL_SOCKET, SO_SNDBUF,
	  (const char *)&txBufSize, sizeof(unsigned))) {
      Error("setsockopt(SO_SNDBUF)", Zi::IOError, ZeLastSockError);
      return false;
    }
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
	  (const char *)&b, sizeof(BOOL))) {
      Error("setsockopt(SO_REUSEADDR)", Zi::IOError, ZeLastSockError);
      return false;
    }
    if (setsockopt(s, SOL_SOCKET, SO_DONTLINGER,
	  (const char *)&b, sizeof(BOOL))) {
      Error("setsockopt(SO_DONTLINGER)", Zi::IOError, ZeLastSockError);
      return false;
    }
    if (options.keepAlive() && setsockopt(s, SOL_SOCKET, SO_KEEPALIVE,
	  (const char *)&b, sizeof(BOOL))) {
      Error("setsockopt(SO_KEEPALIVE)", Zi::IOError, ZeLastSockError);
      return false;
    }
    if (!options.udp() && !options.nagle() &&
	setsockopt(s, IPPROTO_TCP, TCP_NODELAY,
	  (const char *)&b, sizeof(BOOL))) {
      Error("setsockopt(TCP_NODELAY)", Zi::IOError, ZeLastSockError);
      return false;
    }
  }

  if (!CreateIoCompletionPort((HANDLE)s, m_completionPort, 0, 0)) {
    Error("CreateIoCompletionPort", Zi::IOError, ZeLastError);
    return false;
  }

  return true;
}

class ZiMxIOCP::Cxn : public ZmPolymorph, public ZmNHeap<Cxn> {
friend ZiMxIOCP;
  friend inline ZuStringT<"ZiMxIOCP.Cxn"> ZmNHeapID(Cxn *);

  Cxn(ZiMxIOCP *backend, ZiConnection *cxn) : m_backend{backend}, m_cxn{cxn} { }

  void recv();
  void send();
  void disconnect(bool);
  void disconnect_2(bool);
  void close_1(bool);
  void close_2(bool);
  void notifyDisconnected(bool);
  void overlappedRecv(int, unsigned, ZeError);
  void overlappedSend(int, unsigned, ZeError);
  void overlappedDisconnect(int, unsigned, ZeError);
  void errorDisconnect(int, ZeError, bool);

private:
  ZiMxIOCP		*m_backend;
  ZiConnection		*m_cxn;

  // Rx owner, including receive cancellation and disconnect completions.
  alignas(Zm::CacheLineSize)
  bool			m_discPending = false;
  ZmRef<Zi::IOBuf>	m_rxPending; // actual outstanding kernel receive hold
  Overlapped		m_discOverlapped;
  Overlapped		m_rxOverlapped;
  WSABUF		m_rxWSABuf;
  WSAMSG		m_rxMsg;
  alignas(WSACMSGHDR)
  uint8_t		m_rxControl[WSA_CMSG_SPACE(sizeof(int))];
  DWORD			m_rxFlags;		// flags for WSARecv()
  bool			m_discPeer = false;

  // Tx owner, except that Rx dispatches the final send completion to Tx.
  alignas(Zm::CacheLineSize)
  ZmRef<Zi::IOBuf>	m_txPending; // kernel backing until final-send retirement
  Overlapped		m_txOverlapped;
  TRANSMIT_PACKETS_ELEMENT m_txElement;
  bool			m_txNativeDisconnected = false;
  bool			m_txClosePeer = false;
};

void ZiMxIOCP::cxnAdd(ZiConnection *cxn)
{
  cxn->backend(new Cxn{this, cxn});
  m_cxns->add(cxn);
}

void ZiMxIOCP::listenerDel(Socket s)
{
  if (ZmRef<Listener> listener = m_listeners->del(s))
    listener->down();
}

void ZiMxIOCP::connected(ZiConnection *cxn)
{
  cxn->connected(cxn->rxContext());
  if (!cxn->rxContext().completed()) recv(cxn);
}

void ZiMxIOCP::recv(ZiConnection *cxn)
{
  static_cast<Cxn *>(cxn->backend())->recv();
}

void ZiMxIOCP::send(ZiConnection *cxn)
{
  static_cast<Cxn *>(cxn->backend())->send();
}

void ZiMxIOCP::disconnect(ZiConnection *cxn, bool peer)
{
  static_cast<Cxn *>(cxn->backend())->disconnect(peer);
}

void ZiMxIOCP::close(ZiConnection *cxn, bool peer)
{
  static_cast<Cxn *>(cxn->backend())->close_1(peer);
}

void ZiMxIOCP::Cxn::recv()
{
#ifdef ZiMultiplex_DEBUG
  if (m_cxn->mx()->trace()) m_cxn->mx()->traceCapture();
#endif

  if (ZuUnlikely(!m_cxn->rxUp().load_())) {
    m_cxn->rxContext().complete();
    return;
  }

  if (ZuUnlikely(m_cxn->rxContext().completed()))
    return;

  if (m_rxPending) return;

  ZmAssert(m_cxn->rxContext().buf);

  unsigned len = m_cxn->rxContext().buf->avail();

  WSABUF wsaBuf;
  wsaBuf.buf = m_cxn->rxContext().buf->ptr<char>() + m_cxn->rxContext().buf->length;
  wsaBuf.len = len;

  using Executed = Overlapped::Executed;
  Overlapped &overlapped = m_rxOverlapped;

#ifdef ZiMultiplex_DEBUG
  if (ZuUnlikely(!m_cxn->info().options.udp() && m_cxn->mx()->frag())) {
    unsigned l = ((m_cxn->rxContext().buf->length + 8)>>1) + 1;
    if (len > l) len = l;
  }
  if (ZuUnlikely(m_cxn->mx()->debug())) {
    unsigned long n;
    DWORD o;
    if (!WSAIoctl(
	m_cxn->info().socket, FIONREAD, 0, 0, &n, sizeof(unsigned long), &o, 0, 0)) {
      ZiDEBUG(m_cxn->mx(), ([
	socket = m_cxn->info().socket,
	len,
	size = m_cxn->rxContext().buf->size,
	offset = m_cxn->rxContext().buf->length,
	buffered = n,
	overlapped = &overlapped
      ](auto &s) {
	s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>()
	  << " WSARecv(" << len << ") size: " << size
	  << " offset: " << offset << " buffered: " << buffered
	  << " overlapped: " << ZuBoxPtr(overlapped).hex<false, ZuFmt::Alt<>>();
      }));
    }
  }
#endif

  m_rxFlags = 0;
  overlapped.init(Executed{this, ZmFnPtr<&ZiMxIOCP::Cxn::overlappedRecv>{}});
  m_rxPending = ZuMv(m_cxn->rxContext().buf);
  ZeError e;
  DWORD n;
  if (m_cxn->info().options.udp()) {
    ZiIPType::T type;
    if (!ZiMultiplex::cxnType(m_cxn->info().local.ip(), m_cxn->info().remote.ip(), type))
      type = ZiIPType::V4;
    m_cxn->rxContext().addr.init(type);
    memset(&m_rxMsg, 0, sizeof(m_rxMsg));
    m_rxWSABuf = wsaBuf;
    m_rxMsg.name = m_cxn->rxContext().addr.sa();
    m_rxMsg.namelen = m_cxn->rxContext().addr.len();
    m_rxMsg.lpBuffers = &m_rxWSABuf;
    m_rxMsg.dwBufferCount = 1;
    m_rxMsg.Control.buf = reinterpret_cast<char *>(m_rxControl);
    m_rxMsg.Control.len = sizeof(m_rxControl);
    m_rxMsg.dwFlags = 0;
    if (ZuLikely(WSARecvMsg(m_cxn->info().socket, &m_rxMsg, &n,
	    &overlapped, 0) != SOCKET_ERROR ||
	  (e = WSAGetLastError()).errNo() == WSA_IO_PENDING)) {
#ifdef ZiMultiplex_DEBUG
      if (m_cxn->mx()->yield()) Zm::yield();
#endif
      return;
    }
  } else {
    if (ZuLikely(WSARecv(m_cxn->info().socket, &wsaBuf, 1, &n, &m_rxFlags,
	    &overlapped, 0) != SOCKET_ERROR ||
	  (e = WSAGetLastError()).errNo() == WSA_IO_PENDING)) {
#ifdef ZiMultiplex_DEBUG
      if (m_cxn->mx()->yield()) Zm::yield();
#endif
      return;
    }
  }
  m_rxPending = nullptr;
  overlapped.clear();
  m_cxn->errorRecv("WSARecv", Zi::IOError, e);
  m_cxn->rxContext().complete();
  return;
}

void ZiMxIOCP::Cxn::overlappedRecv(int status, unsigned n, ZeError e)
{
  auto buf = ZuMv(m_rxPending);
  if (!m_cxn->rxUp().load_() || m_cxn->rxContext().completed()) {
    if (m_discPending) notifyDisconnected(m_discPeer);
    return;
  }
  m_cxn->rxContext().buf = ZuMv(buf);
  if (ZuUnlikely(status != Zi::OK)) {
    if (m_cxn->rxUp().load_()) m_cxn->errorRecv("WSARecv", status, e);
    return;
  }
  if (ZuUnlikely(!n && !m_cxn->info().options.udp())) {
    if (m_cxn->rxUp().load_()) m_cxn->disconnect(true);
    return;
  }

  ZiDEBUG(m_cxn->mx(), ([
    socket = m_cxn->info().socket,
    n,
    buf = ZiMultiplex::debugBuf(m_cxn->rxContext().buf->end(), n)
  ](auto &s) {
    s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>()
      << " WSARecv(" << n << ")\n"
      << ZtHexDump_(buf.cspan());
  }));

  if (m_cxn->info().options.udp()) {
    m_cxn->rxContext().addr.sync();
    m_cxn->rxContext().tos = getTOS(m_rxMsg);
    m_cxn->rxContext().flags = m_rxMsg.dwFlags;
  }
  m_cxn->executedRecv(n);

  if (ZuUnlikely(m_cxn->rxContext().completed())) {
    if (m_cxn->rxContext().disconnected()) m_cxn->disconnect(false);
    return;
  }

  ZmAssert(m_cxn->rxContext().buf);
  if (ZuUnlikely(!m_cxn->rxContext().buf->avail())) {
    m_cxn->rxContext().complete();
    return;
  }

  recv();
}
void ZiMxIOCP::Cxn::send()
{
  if (m_txPending) return;

  unsigned n;

  auto buf = m_cxn->txContext().buf->data() + m_cxn->txContext().offset;
  unsigned len = m_cxn->txContext().buf->length - m_cxn->txContext().offset;
  ZeError e;

  WSABUF wsaBuf;
  wsaBuf.buf = reinterpret_cast<char *>(buf);
  wsaBuf.len = len;

  DWORD n_;

#ifdef ZiMultiplex_FILTER
  if (ZuUnlikely(m_cxn->mx()->filterTx(m_cxn, buf, len))) {
    n = len;
    goto executed;
  }
#endif

retry:
  ZiDEBUG(m_cxn->mx(), ([
    socket = m_cxn->info().socket,
    size = m_cxn->txContext().buf->length,
    offset = m_cxn->txContext().offset,
    buf = ZiMultiplex::debugBuf(wsaBuf.buf, wsaBuf.len)
  ](auto &s) {
    s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>()
      << " WSASend(" << buf.length() << ") size: " << size
      << " offset: " << offset << '\n'
      << ZtHexDump_(buf.cspan());
  }));

  if (m_cxn->info().options.udp()) {
    uint8_t controlBuf[WSA_CMSG_SPACE(sizeof(int))];
    WSABUF control;
    control.buf = reinterpret_cast<char *>(controlBuf);
    control.len = sizeof(controlBuf);
    WSAMSG msg;
    memset(&msg, 0, sizeof(msg));
    if (!!m_cxn->txContext().addr) {
      msg.name = m_cxn->txContext().addr.sa();
      msg.namelen = m_cxn->txContext().addr.len();
    }
    msg.lpBuffers = &wsaBuf;
    msg.dwBufferCount = 1;
    msg.dwFlags = 0;
    ZiIPType::T type = txTOSType(
      m_cxn->txContext().addr, m_cxn->info().local.ip(), m_cxn->info().remote.ip());
    if (type == ZiIPType::V6) {
#ifdef IPV6_TCLASS
      putTOS(msg, control, m_cxn->txContext().tos, IPPROTO_IPV6, IPV6_TCLASS);
#endif
    } else
      putTOS(msg, control, m_cxn->txContext().tos, IPPROTO_IP, IP_TOS);
    if (ZuUnlikely(WSASendMsg(m_cxn->info().socket, &msg, 0, &n_, 0, 0) ==
	  SOCKET_ERROR)) {
      e = WSAGetLastError();
      m_cxn->errorSend("WSASend", Zi::IOError, e);
      return;
    }
    n = n_;
    ZiDEBUG(m_cxn->mx(), ([
      socket = m_cxn->info().socket,
      len = wsaBuf.len,
      n
    ](auto &s) {
      s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>()
	<< " WSASendMsg(" << len << "): " << n;
    }));
  } else {
    if (m_cxn->txContext().disconnected() && len) {
      // TransmitPackets accepts at most INT_MAX - 1 bytes per operation.
      // Send any larger prefix normally so TF_DISCONNECT goes on the tail.
      constexpr unsigned maxBytes = 0x7ffffffe;
      if (len > maxBytes)
	wsaBuf.len = maxBytes;
      else if (auto transmit = ZiMxWSExt::instance()->transmitPackets()) {
	m_txElement.dwElFlags = TP_ELEMENT_MEMORY;
	m_txElement.cLength = len;
	m_txElement.pBuffer = buf;
	m_txPending = m_cxn->txContext().buf;
	using Executed = Overlapped::Executed;
	m_txOverlapped.init(Executed{this, ZmFnPtr<&ZiMxIOCP::Cxn::overlappedSend>{}});
	if (transmit(m_cxn->info().socket, &m_txElement, 1, 0,
	      &m_txOverlapped, TF_DISCONNECT) ||
	    (e = WSAGetLastError()).errNo() == WSA_IO_PENDING)
	  return; // immediate success also delivers an IOCP completion
	m_txOverlapped.clear();
	m_txPending = nullptr;
	if (e.errNo() != WSAEOPNOTSUPP) {
	  m_cxn->errorSend("WSASend", Zi::IOError, e);
	  return;
	}
      }
    }
    if (ZuUnlikely(WSASend(m_cxn->info().socket, &wsaBuf, 1, &n_, 0,
	    0, 0) == SOCKET_ERROR)) {
      e = WSAGetLastError();
      m_cxn->errorSend("WSASend", Zi::IOError, e);
      return;
    }
    n = n_;
    ZiDEBUG(m_cxn->mx(), ([
      socket = m_cxn->info().socket,
      len = wsaBuf.len,
      n
    ](auto &s) {
      s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>()
	<< " WSASend(" << len << "): " << n;
    }));
  }

#ifdef ZiMultiplex_FILTER
executed:
#endif
  if (!m_cxn->executedSend(n)) return;

  buf = m_cxn->txContext().buf->data() + m_cxn->txContext().offset;
  len = m_cxn->txContext().buf->length - m_cxn->txContext().offset;

  wsaBuf.buf = reinterpret_cast<char *>(buf);
  wsaBuf.len = len;

  goto retry;
}

void ZiMxIOCP::Cxn::overlappedSend(int status, unsigned n, ZeError e)
{
  m_cxn->mx()->txRun([this, cxn = ZmRef(m_cxn), status, n, e]() {
    m_txPending = nullptr;
    if (!cxn->txUp().load_()) {
      // close_1 disarmed the callback. Rx can now retire the socket.
      m_cxn->mx()->rxRun([this, cxn, peer = m_txClosePeer]() {
	close_2(peer);
      });
      return;
    }
    if (status != Zi::OK) {
      m_cxn->errorSend("WSASend", status, e);
      return;
    }
    m_txNativeDisconnected = true;
    if (!cxn->executedSend(n)) return;
    // Winsock has already disconnected. An unaccepted suffix cannot be retried.
    if (n < m_txElement.cLength)
      m_cxn->errorSend("WSASend", Zi::IOError, ZeError{WSAECONNABORTED});
    else
      close_1(false);
  });
}

void ZiMxIOCP::Cxn::disconnect(bool peer)
{
  // A peer FIN closes its sending half. Our marked suffix can still drain.
  if (m_cxn->txContext().disconnected() && !m_cxn->txContext().completed()) {
    if (peer) m_cxn->send();
    return;
  }
  if (m_txPending) return; // TF_DISCONNECT is already in flight
  if (m_txNativeDisconnected) { close_1(peer); return; }
  if (!m_cxn->txUp().load_()) return;
  m_cxn->txUp() = false;
  m_cxn->txContext().complete();
  m_cxn->mx()->rxRun([this, cxn = ZmRef(m_cxn), peer]() { disconnect_2(peer); });
}

void ZiMxIOCP::Cxn::disconnect_2(bool peer)
{
  if (!m_cxn->rxUp().load_()) {
    notifyDisconnected(peer);
    return;
  }

  m_cxn->rxContext().complete();
  m_cxn->rxUp() = 0;

  if (m_cxn->info().options.udp()) {
    notifyDisconnected(peer);
    return;
  }

  ZiDEBUG(m_cxn->mx(), ([socket = m_cxn->info().socket](auto &s) {
    s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>() <<
      " disconnect()";
  }));

  using Executed = Overlapped::Executed;
  Overlapped &overlapped = m_discOverlapped;
  ZeError e;
  m_discPeer = peer;
  overlapped.init(Executed{this, ZmFnPtr<&ZiMxIOCP::Cxn::overlappedDisconnect>{}});
  if (ZuLikely(DisconnectEx(m_cxn->info().socket, &overlapped, 0, 0) ||
	(e = WSAGetLastError()).errNo() == WSA_IO_PENDING)) {
    notifyDisconnected(peer); // Cancel receive and wait for both completions.
    return;
  }
  overlapped.clear();
  errorDisconnect(Zi::IOError, e, peer);
}

void ZiMxIOCP::Cxn::overlappedDisconnect(int status, unsigned, ZeError e)
{
  if (ZuUnlikely(status != Zi::OK)) {
    errorDisconnect(status, e, m_discPeer);
    return;
  }
  notifyDisconnected(m_discPeer);
}

void ZiMxIOCP::Cxn::errorDisconnect(int status, ZeError e, bool peer)
{
  notifyDisconnected(peer);
  if (e.errNo() != ZiENOTCONN)
    Error(
      "DisconnectEx",
      status, e);
}

void ZiMxIOCP::Cxn::notifyDisconnected(bool peer)
{
  m_discPending = true;
  m_discPeer = peer;
  m_cxn->rxContext().complete();
  if (m_rxPending) {
    CancelIoEx(reinterpret_cast<HANDLE>(m_cxn->info().socket),
      &m_rxOverlapped);
    return;
  }
  if (m_discOverlapped.pending()) return;
  m_discPending = false;
  m_backend->retired(m_cxn, peer);
}

void ZiMxIOCP::Cxn::close_1(bool peer)
{
  if (m_txPending) {
    if (!m_cxn->txUp().load_()) return;
    m_cxn->txUp() = false;
    m_txClosePeer = peer;
    m_cxn->txContext().complete();
    CancelIoEx(reinterpret_cast<HANDLE>(m_cxn->info().socket),
      &m_txOverlapped);
    return; // the completion is also delivered if cancellation lost the race
  }
  if (!m_cxn->txUp().load_()) return;
  m_cxn->txUp() = false;

  m_cxn->txContext().complete();

  m_cxn->mx()->rxRun([this, cxn = ZmRef(m_cxn), peer]() { close_2(peer); });
}

void ZiMxIOCP::Cxn::close_2(bool peer)
{
  if (!m_cxn->rxUp().load_()) {
    notifyDisconnected(peer);
    return;
  }

  m_cxn->rxContext().complete();
  m_cxn->rxUp() = 0;

  ZiDEBUG(m_cxn->mx(), ([socket = m_cxn->info().socket](auto &s) {
    s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>() << " close()";
  }));

  notifyDisconnected(peer);
}

void ZiMxIOCP::retired(ZiConnection *cxn, bool peer)
{
  Socket s = cxn->info().socket;
  if (Zi::nullSocket(s)) return;
  ZmRef<ZiConnection> hold{cxn};
  m_cxns->del(s);
  cxn->notifyDisconnected(peer);
  finishStop();
}

void ZiMxIOCP::rx()
{
  DWORD len;
  ULONG_PTR key;
  OVERLAPPED *native;
  ZeError e;

  for (;;) {
    ZiDEBUG(mx(), ([
      nThreads = mx()->params().nThreads(),
      nConnections = m_cxns->count_(),
      nListeners = m_listeners->count_()
    ](auto &s) {
      s << "wait() nThreads: " << ZuBoxed(nThreads).fmt<ZuFmt::Right<2>>()
	<< " nConnections: " <<
	  ZuBoxed(nConnections).fmt<ZuFmt::Right<4>>()
	<< " nListeners: " << ZuBoxed(nListeners).fmt<ZuFmt::Right<3>>();
    }));
    native = nullptr;
    BOOL ok = GetQueuedCompletionStatus(m_completionPort, &len, &key, &native, INFINITE);
    auto overlapped = static_cast<Overlapped *>(native);
    if (!ok) {
      e = GetLastError();
      ZiDEBUG(mx(), ([
	overlapped,
	errNo = int(e.errNo())
      ](auto &s) {
	s << "wait() overlapped: " <<
	  ZuBoxPtr(overlapped).hex<false, ZuFmt::Alt<>>() <<
	  " errNo: " << errNo;
      }));
      if (!overlapped) {
	Error("GetQueuedCompletionStatus", Zi::IOError, e);
	return;
      }
      overlapped->complete(Zi::IOError, len, e);
    } else {
      ZiDEBUG(mx(), ([overlapped](auto &s) {
	s << "wait() overlapped: " <<
	  ZuBoxPtr(overlapped).hex<false, ZuFmt::Alt<>>();
      }));
      if (!overlapped) { // PostQueuedCompletionStatus() called
	ZiDEBUG(mx(), "wait() woken by PostQueuedCompletionStatus()");
	return;
      }
      overlapped->complete(Zi::OK, len, ZeOK);
    }
    finishStop();
  }
}

void ZiMxIOCP::wake()
{
  ZmGuard guard{m_wakeLock};
  if (m_completionPort) PostQueuedCompletionStatus(m_completionPort, 0, 0, nullptr);
}

void ZiMxIOCP::finishStop()
{
  if (!m_stopping || m_cxns->count_() || m_connects->count_() || m_pendingAccepts) return;
  m_stopping = false;
  mx()->pluginStopped({});
}

#endif
