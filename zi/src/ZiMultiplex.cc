//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// socket I/O multiplexing

#include <zlib/ZiMultiplex.hh>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZmRWLock.hh>
#include <zlib/ZmSingleton.hh>

#include <zlib/ZtHexDump.hh>
#include <zlib/ZtLocalArray.hh>

#include <zlib/ZiLog.hh>

class ZiMxMgr_ {
friend Ztc::MxMgr;

  using Lock = ZmRWLock;
  using Guard = ZmGuard<Lock>;
  using ReadGuard = ZmReadGuard<Lock>;

  ZuDerive(Map,
    (ZmRBTree<ZiMultiplex *,
      ZmRBTreeUnique<true,
	ZmRBTreeLock<ZmNoLock,
	  ZmRBTreeHeapID<"Ztc.MxMgr">>>>));

public:
  ~ZiMxMgr_() {
    m_addFn = {};
    m_delFn = {};
    m_addCxnFn = {};
    m_delCxnFn = {};
    m_addQueueFn = {};
    m_delQueueFn = {};
  }

  static ZiMxMgr_ *instance() {
    return
      ZmSingleton<ZiMxMgr_,
	ZmSingletonCleanup<ZmCleanup::Library>>::instance();
  }

  void add(ZiMultiplex *mx) {
    Guard guard(m_watchLock);
    m_map.add(mx);
    if (m_addFn) m_addFn(mx);
  }
  void del(ZiMultiplex *mx) {
    Guard guard(m_watchLock);
    if (m_delFn) m_delFn(mx);
    m_map.del(mx);
  }

  unsigned all(Ztc::MxMgr::AllFn fn) const {
    ReadGuard guard(m_watchLock);
    unsigned n = 0;
    auto i = m_map.citer();
    while (auto mx = i.key()) {
      ++n;
      fn(mx);
    }
    return n;
  }

  template <typename L> void guard(L &&l) {
    Guard guard(m_watchLock);
    ZuFwd<L>(l)();
  }

  void watch(Ztc::MxMgr::AddFn addFn, Ztc::MxMgr::DelFn delFn) {
    Guard guard(m_watchLock);
    m_addFn = ZuMv(addFn);
    m_delFn = ZuMv(delFn);
  }

  void unwatch() {
    Guard guard(m_watchLock);
    m_addFn = {};
    m_delFn = {};
  }

  void watch(
      Ztc::Mx::AddCxnFn addCxnFn, Ztc::Mx::DelCxnFn delCxnFn,
      Ztc::Mx::AddQueueFn addQueueFn, Ztc::Mx::DelQueueFn delQueueFn) {
    Guard guard(m_watchLock);
    m_addCxnFn = ZuMv(addCxnFn);
    m_delCxnFn = ZuMv(delCxnFn);
    m_addQueueFn = ZuMv(addQueueFn);
    m_delQueueFn = ZuMv(delQueueFn);
  }

  void unwatchMx() {
    Guard guard(m_watchLock);
    m_addCxnFn = {};
    m_delCxnFn = {};
    m_addQueueFn = {};
    m_delQueueFn = {};
  }

  void cxnAdded_(ZiConnection *cxn) {
    Guard guard(m_watchLock);
    if (m_addCxnFn) m_addCxnFn(cxn);
  }

  void cxnDeleted_(ZiConnection *cxn) {
    Guard guard(m_watchLock);
    if (m_delCxnFn) m_delCxnFn(cxn);
  }

private:
  Lock &watchLock() { return m_watchLock; }

  Map				m_map;
  mutable Lock			m_watchLock;
  Ztc::MxMgr::AddFn		m_addFn;
  Ztc::MxMgr::DelFn		m_delFn;
  Ztc::Mx::AddCxnFn		m_addCxnFn;
  Ztc::Mx::DelCxnFn		m_delCxnFn;
  Ztc::Mx::AddQueueFn		m_addQueueFn;
  Ztc::Mx::DelQueueFn		m_delQueueFn;
};

#ifndef _WIN32
#include <alloca.h>
#endif

#if !defined(IPV6_JOIN_GROUP) && defined(IPV6_ADD_MEMBERSHIP)
#define IPV6_JOIN_GROUP IPV6_ADD_MEMBERSHIP
#endif

#ifdef ZiMultiplex_IOCP

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

class ZiMultiplex_WSExt {
public:
  ZiMultiplex_WSExt();
  ~ZiMultiplex_WSExt();

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

  static ZiMultiplex_WSExt *instance();

private:
  PConnectEx		m_connectEx;
  PAcceptEx		m_acceptEx;
  PGetAcceptExSockaddrs	m_getAcceptExSockaddrs;
  PDisconnectEx		m_disconnectEx;
  PWSARecvMsg		m_recvMsg;
  PWSASendMsg		m_sendMsg;
};

ZiMultiplex_WSExt *ZiMultiplex_WSExt::instance()
{
  return
    ZmSingleton<ZiMultiplex_WSExt,
      ZmSingletonCleanup<ZmCleanup::Platform>>::instance();
}

ZiMultiplex_WSExt::ZiMultiplex_WSExt()
{
  using Socket = Zi::Socket;
  Socket s = (Socket)::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (Zi::nullSocket(s)) goto error;
  {
    static GUID connectExGUID = WSAID_CONNECTEX;
    static GUID acceptExGUID = WSAID_ACCEPTEX;
    static GUID getAcceptExSockaddrsGUID = WSAID_GETACCEPTEXSOCKADDRS;
    static GUID disconnectExGUID = WSAID_DISCONNECTEX;
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

ZiMultiplex_WSExt::~ZiMultiplex_WSExt()
{
}

#define ConnectEx(s, sa, salen, ptr, len, count, overlapped) \
  (ZiMultiplex_WSExt::instance()->connectEx(s, sa, salen, ptr, len, \
					    count, overlapped))
#define AcceptEx(l, s, ptr, len, localsalen, remotesalen, count, overlapped) \
  (ZiMultiplex_WSExt::instance()->acceptEx(l, s, ptr, len, localsalen, \
					   remotesalen, count, overlapped))
#define GetAcceptExSockaddrs(buf, len, localaddrlen, remoteaddrlen, \
			     localsa, localsalen, remotesa, remotesalen) \
  (ZiMultiplex_WSExt::instance()->getAcceptExSockaddrs(buf, len, \
						       localaddrlen, \
						       remoteaddrlen, \
						       localsa, localsalen, \
						       remotesa, remotesalen))
#define DisconnectEx(s, overlapped, flags, reserved) \
  (ZiMultiplex_WSExt::instance()->disconnectEx(s, overlapped, flags, reserved))
#define WSARecvMsg(s, msg, count, overlapped, completion) \
  (ZiMultiplex_WSExt::instance()->recvMsg( \
    s, msg, count, overlapped, completion))
#define WSASendMsg(s, msg, flags, count, overlapped, completion) \
  (ZiMultiplex_WSExt::instance()->sendMsg( \
    s, msg, flags, count, overlapped, completion))

#endif /* ZiMultiplex_IOCP */

#if !defined(_WIN32) && defined(ZiMultiplex_Netlink)
#include <zlib/ZiNetlink.hh>  // support netlink for Unix only
#endif

#ifdef ZiMultiplex_EPoll

#include <sys/resource.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <linux/unistd.h>
#include <linux/sockios.h>

#ifndef EPOLLRDHUP
#define EPOLLRDHUP 0
#endif

#if 0
#ifndef __NR_eventfd
#if defined(__x86_64__)
#define __NR_eventfd 284
#elif defined(__i386__)
#define __NR_eventfd 323
#else
#error Unsupported architecture!
#endif
#endif /* !__NR_eventfd */
#endif

// 64-bit pointer-packing for epoll u64

inline uint64_t u64_cxn(void *ptr) {
  return reinterpret_cast<uintptr_t>(ptr);
}
inline bool u64_is_cxn(uint64_t v) { return !(v>>62); }
inline uint64_t u64_listener(void *ptr) {
  return reinterpret_cast<uintptr_t>(ptr) | (uint64_t(1)<<62);
}
inline bool u64_is_listener(uint64_t v) { return (v>>62) == 1; }
inline uint64_t u64_connect(void *ptr) {
  return reinterpret_cast<uintptr_t>(ptr) | (uint64_t(2)<<62);
}
inline bool u64_is_connect(uint64_t v) { return (v>>62) == 2; }
constexpr const uint64_t u64_event() { return uint64_t(3)<<62; }
inline bool u64_is_event(uint64_t v) { return (v>>62) == 3; }
template <typename T>
inline T *u64_ptr(uint64_t v) {
  return reinterpret_cast<T *>(v & ~(uint64_t(3)<<62));
}

#endif /* ZiMultiplex_EPoll */

#define Log(severity, op, result, error) \
  ZiLOG(severity, "ZiMultiplex", \
    ([op_ = op, result_ = result, error_ = error](auto &s) { \
    s << op_ << ' ' << Zi::ioResult(result_) << ' ' << error_; \
  }))
#define Error(op, result, error) Log(Error, op, result, error)
#define Warning(op, result, error) Log(Warning, op, result, error)

namespace {

ZiIP ZiIP_wildcard(ZiIPType::T type)
{
  switch (type) {
    case ZiIPType::V6:
      return ZiIP{in6addr_any};
    default: {
      in_addr addr;
      addr.s_addr = 0;
      return ZiIP{addr};
    }
  }
}

int ZiIP_family(ZiIPType::T type)
{
  switch (type) {
    case ZiIPType::V6:
      return AF_INET6;
    default:
      return AF_INET;
  }
}

bool ZiIP_cxnType(ZiIP localIP, ZiIP remoteIP, ZiIPType::T &type)
{
  if (!!localIP && !!remoteIP && localIP.type() != remoteIP.type())
    return false;
  if (!!localIP)
    type = localIP.type();
  else if (!!remoteIP)
    type = remoteIP.type();
  else
    type = ZiIPType::V4;
  return true;
}

ZiTOS ZiTOS_(unsigned v)
{
  return uint8_t(v);
}

#ifndef _WIN32
bool ZiSetRecvTOS(Zi::Socket s, ZiIPType::T type)
{
  int b = 1;
  switch (type) {
    case ZiIPType::V6:
#ifdef IPV6_RECVTCLASS
      return setsockopt(s, IPPROTO_IPV6, IPV6_RECVTCLASS, &b, sizeof(b)) >= 0;
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

ZiTOS ZiGetTOS(const struct msghdr &msg)
{
  for (auto cmsg = CMSG_FIRSTHDR(&msg); cmsg;
      cmsg = CMSG_NXTHDR(const_cast<struct msghdr *>(&msg), cmsg)) {
    if (cmsg->cmsg_level == IPPROTO_IP && cmsg->cmsg_type == IP_TOS) {
      if (cmsg->cmsg_len >= CMSG_LEN(sizeof(int)))
	return ZiTOS_(*reinterpret_cast<int *>(CMSG_DATA(cmsg)));
      if (cmsg->cmsg_len >= CMSG_LEN(sizeof(uint8_t)))
	return ZiTOS_(*static_cast<uint8_t *>(CMSG_DATA(cmsg)));
    }
#ifdef IPV6_TCLASS
    if (cmsg->cmsg_level == IPPROTO_IPV6 &&
	cmsg->cmsg_type == IPV6_TCLASS &&
	cmsg->cmsg_len >= CMSG_LEN(sizeof(int)))
      return ZiTOS_(*reinterpret_cast<int *>(CMSG_DATA(cmsg)));
#endif
  }
  return {};
}

void ZiPutTOS(struct msghdr &msg, void *control, unsigned controlLen, ZiTOS tos,
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
  msg.msg_controllen = CMSG_SPACE(sizeof(int));
}
#else
bool ZiSetRecvTOS(Zi::Socket s, ZiIPType::T type)
{
  DWORD b = TRUE;
  switch (type) {
    case ZiIPType::V6:
#ifdef IPV6_RECVTCLASS
      return !setsockopt(s,
	IPPROTO_IPV6, IPV6_RECVTCLASS, (const char *)&b, sizeof(b));
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

ZiTOS ZiGetTOS(WSAMSG &msg)
{
  for (auto cmsg = WSA_CMSG_FIRSTHDR(&msg); cmsg;
      cmsg = WSA_CMSG_NXTHDR(&msg, cmsg)) {
    if (cmsg->cmsg_level == IPPROTO_IP && cmsg->cmsg_type == IP_TOS &&
	cmsg->cmsg_len >= WSA_CMSG_LEN(sizeof(int)))
      return ZiTOS_(*reinterpret_cast<int *>(WSA_CMSG_DATA(cmsg)));
#ifdef IPV6_TCLASS
    if (cmsg->cmsg_level == IPPROTO_IPV6 &&
	cmsg->cmsg_type == IPV6_TCLASS &&
	cmsg->cmsg_len >= WSA_CMSG_LEN(sizeof(int)))
      return ZiTOS_(*reinterpret_cast<int *>(WSA_CMSG_DATA(cmsg)));
#endif
  }
  return {};
}

void ZiPutTOS(WSAMSG &msg, WSABUF &control, ZiTOS tos, int level, int type)
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
#endif

} // namespace

ZiConnection::ZiConnection(ZiMultiplex *mx, const ZiCxnInfo &info) :
  m_mx(mx), m_info(info), m_rxUp(1),
  m_txUp(1), m_rxCalls(0), m_rxBytes(0),
#ifdef ZiMultiplex_IOCP
  m_rxFlags(0),
#endif
  m_txCalls(0), m_txBytes(0)
{
  m_rxContext.cxn = m_txContext.cxn = this;
}

ZiConnection::~ZiConnection()
{
  // precautionary code to ensure no leaking of sockets

  if (!Zi::nullSocket(m_info.socket)) {
    ZiLOG(Warning, "ZiMultiplex", "~ZiConnection() called with socket still open");
    Zi::closeSocket(m_info.socket);
  }
}

void ZiMultiplex::udp(ZiConnectFn fn, ZiFailFn failFn,
    ZiIP localIP, uint16_t localPort,
    ZiIP remoteIP, uint16_t remotePort,
    ZiCxnOptions options)
{
  if (ZuUnlikely(!running())) {
    Error("udp", Zi::NotReady, ZeOK);
    failFn(false);
    return;
  }

  if (ZuUnlikely(!options.udp())) {
    Error("udp", Zi::IOError, ZeError(ZiEINVAL));
    failFn(false);
    return;
  }

  rxInvoke([this, fn = ZuMv(fn), failFn = ZuMv(failFn),
      localIP, localPort, remoteIP, remotePort,
      options = ZuMv(options)]() mutable {
    udp_(ZuMv(fn), ZuMv(failFn),
	localIP, localPort, remoteIP, remotePort, options);
  });
}

void ZiMultiplex::udp_(ZiConnectFn fn, ZiFailFn failFn,
    ZiIP localIP, uint16_t localPort,
    ZiIP remoteIP, uint16_t remotePort,
    ZiCxnOptions options)
{
  Socket s;
  ZiIPType::T ipType;
  if (!ZiIP_cxnType(localIP, remoteIP, ipType)) {
    Error("udp", Zi::IOError, ZeError(ZiEINVAL));
    failFn(false);
    return;
  }
  if (options.multicast() && !options.multicastValid(ipType)) {
    Error("udp", Zi::IOError, ZeError(ZiEINVAL));
    failFn(false);
    return;
  }

#ifndef _WIN32

  s = (Socket)::socket(ZiIP_family(ipType), SOCK_DGRAM, IPPROTO_UDP);
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
    if (!localIP) localIP = ZiIP_wildcard(ipType);
    ZiSockAddr local(localIP, localPort);
    if (bind(s, local.sa(), local.len()) < 0) {
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

  if (!ZiSetRecvTOS(s, ipType))
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

#ifdef ZiMultiplex_EPoll
  if (fcntl(s, F_SETFL, O_NONBLOCK) < 0) {
    ZeError e{errno};
    ::close(s);
    Error("fcntl(F_SETFL, O_NONBLOCK)", Zi::IOError, e);
    failFn(false);
    return;
  }
#endif

#else /* !_WIN32 */

  s = (Socket)::socket(ZiIP_family(ipType), SOCK_DGRAM, IPPROTO_UDP);
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
    if (!localIP) localIP = ZiIP_wildcard(ipType);
    ZiSockAddr local(localIP, localPort);
    if (bind(s, local.sa(), local.len())) {
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

  if (!ZiSetRecvTOS(s, ipType))
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

#ifdef ZiMultiplex_IOCP
  if (!CreateIoCompletionPort((HANDLE)s, m_completionPort, 0, 0)) {
    ZeError e(GetLastError());
    ::closesocket(s);
    Error("CreateIoCompletionPort", Zi::IOError, e);
    failFn(false);
    return;
  }
#endif

#endif /* !_WIN32 */

  ZmRef<ZiConnection> cxn;

  cxn = fn(ZiCxnInfo {
      ZiCxnType::UDP, s, options, localIP, localPort, remoteIP, remotePort });

  if (!cxn) {
    Zi::closeSocket(s);
    return;
  }

  if (!cxnAdd(cxn, s)) return;

  cxn->connected();

  ZiDEBUG(this, ([socket = s, ip = remoteIP, port = remotePort](auto &s) {
    s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>()
      << " UDP CONNECTED to " << ip << ':' << port;
  }));

#ifdef ZiMultiplex_IOCP
  cxn->recv();
#endif
}

void ZiMultiplex::connect(
    ZiConnectFn fn, ZiFailFn failFn, ZiIP localIP, uint16_t localPort,
    ZiIP remoteIP, uint16_t remotePort, ZiCxnOptions options)
{
  if (ZuUnlikely(!running())) {
    Error("connect", Zi::NotReady, ZeOK);
    failFn(false);
    return;
  }

  if (ZuUnlikely(options.udp())) {
    Error("connect", Zi::IOError, ZeError(ZiEINVAL));
    failFn(false);
    return;
  }

  rxInvoke([this, fn = ZuMv(fn), failFn = ZuMv(failFn),
      localIP, localPort, remoteIP, remotePort,
      options = ZuMv(options)]() mutable {
    connect_(ZuMv(fn), ZuMv(failFn),
	localIP, localPort, remoteIP, remotePort, options);
  });
}

void ZiMultiplex::connect_(ZiConnectFn fn, ZiFailFn failFn,
    ZiIP localIP, uint16_t localPort,
    ZiIP remoteIP, uint16_t remotePort, ZiCxnOptions options)
{
  Socket s;
  ZiIPType::T ipType = ZiIPType::V4;

#if !defined(_WIN32) && defined(ZiMultiplex_Netlink)
  if (options.netlink())
    s = (Socket)::socket(AF_NETLINK, SOCK_DGRAM, NETLINK_GENERIC);
  else
#endif
  {
    if (!remoteIP || !ZiIP_cxnType(localIP, remoteIP, ipType)) {
      Error("connect", Zi::IOError, ZeError(ZiEINVAL));
      failFn(false);
      return;
    }
    s = (Socket)::socket(ZiIP_family(ipType), SOCK_STREAM, IPPROTO_TCP);
  }
  if (Zi::nullSocket(s)) {
    ZeError e = ZeLastSockError;
    Zi::closeSocket(s);
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
#ifndef _WIN32
#ifdef ZiMultiplex_Netlink
    if (options.netlink()) {
      ZiNetlinkSockAddr local;
      if (bind(s, local.sa(), local.len()) < 0) {
	ZeError e{errno};
	::close(s);
	Warning("bind", Zi::IOError, e);
	failFn(true);
	return;
      }
    } else
#endif
    {
      if (!localIP) localIP = ZiIP_wildcard(ipType);
      ZiSockAddr local(localIP, localPort);
      if (bind(s, local.sa(), local.len()) < 0) {
	ZeError e{errno};
	::close(s);
	Warning("bind", Zi::IOError, e);
	failFn(true);
	return;
      }
    }
#else
    if (!localIP) localIP = ZiIP_wildcard(ipType);
    ZiSockAddr local(localIP, localPort);
    if (bind(s, local.sa(), local.len())) {
      ZeError e(WSAGetLastError());
      ::closesocket(s);
      Warning("bind", Zi::IOError, e);
      failFn(true);
      return;
    }
#endif
  }

  ZiDEBUG(this, ([socket = s, ip = remoteIP, port = remotePort](auto &s) {
    s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>()
      << " CONNECTING to " << ip << ':' << port;
  }));

  ZiSockAddr remote(remoteIP, remotePort);

#ifdef ZiMultiplex_IOCP
  Connect *request = new Connect(
      this, fn, failFn,
      ZiCxnType::TCPOut, s, options, localIP, localPort, remoteIP, remotePort);
  using Executed = Zi_Overlapped::Executed;
  Zi_Overlapped &overlapped = request->overlapped();
  overlapped.init(Executed::Member<&Connect::executed>::fn(request));
  ZeError e;
  if (ZuLikely(ConnectEx(s, remote.sa(), remote.len(), 0, 0, 0,
	  (OVERLAPPED *)&overlapped) ||
	(e = WSAGetLastError()).errNo() == WSA_IO_PENDING))
    return;
  delete request;
  Error("ConnectEx", Zi::IOError, e);
  failFn(false);
  return;
#endif

#ifdef ZiMultiplex_EPoll
  ZmRef<Connect> request = new Connect(this, fn, failFn,
      ZiCxnType::TCPOut, s, options, localIP, localPort, remoteIP, remotePort);

  if (!connectAdd(request, s)) return;

  connect(request);
#endif
}

#ifdef ZiMultiplex_IOCP
void ZiMultiplex::overlappedConnect(Connect *request,
    int status, unsigned, ZeError e)
{
  ZiCxnInfo &ci = request->info();

  if (ZuUnlikely(status != Zi::OK)) {
    ::closesocket(ci.socket);
    if (!m_stopping) Error("ConnectEx", status, e);
    request->fail(true);
    return;
  }

  {
    ZiSockAddr localSockAddr;
    localSockAddr.init(ci.remoteIP.type());
    int localSockAddrLen = localSockAddr.len();
    if (getsockname(ci.socket, localSockAddr.sa(), &localSockAddrLen)) {
      ZeError e(WSAGetLastError());
      ::closesocket(ci.socket);
      Error("getsockname", Zi::IOError, e);
      request->fail(false);
      return;
    }
    localSockAddr.sync();
    ci.localIP = localSockAddr.ip();
    ci.localPort = localSockAddr.port();
  }

  executedConnect(request->fn(), ci);
}
#endif

#ifdef ZiMultiplex_EPoll
void ZiMultiplex::completedConnect(Connect *request)
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

#ifdef ZiMultiplex_Netlink
  if (!ci.options.netlink())
#endif
  {
    ZiSockAddr local;
    local.init(ci.remoteIP.type());
    socklen_t len = local.len();
    if (getsockname(s, local.sa(), &len) < 0) {
      ZeError e{errno};
      connectDel(s);
      ::close(s);
      Error("getsockname", Zi::IOError, e);
      request->fail(false);
      return;
    }
    local.sync();
    ci.localIP = local.ip();
    ci.localPort = local.port();
  }

  ZiConnectFn fn = ZuMv(request->fn());
  connectDel(s);
  executedConnect(ZuMv(fn), ci);
}

// attempt completion of non-blocking connect
void ZiMultiplex::connect(Connect *request)
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
#ifdef ZiMultiplex_Netlink
  if (ci.options.netlink()) {
    ZeError e;
    if ((e = ZiNetlink::connect(
	s, ci.options.familyName(), ci.familyID, ci.portID)) != ZeOK) {
      connectDel(s);
      ::close(s);
      // Warning("connect", Zi::IOError, e);
      request->fail(true);
      return;
    }
  } else
#endif
  {
    ZiSockAddr remote(ci.remoteIP, ci.remotePort);
    if (::connect(s, remote.sa(), remote.len()) < 0) {
      ZeError e{errno};
      if (e.errNo() == EAGAIN || e.errNo() == EINPROGRESS) return;
      if (e.errNo() == EINTR) goto retry;
      connectDel(s);
      ::close(s);
      // Warning("connect", Zi::IOError, e);
      request->fail(true);
      return;
    }
  }

  completedConnect(request);
}
#endif

void ZiMultiplex::executedConnect(ZiConnectFn fn, const ZiCxnInfo &ci)
{
  ZmRef<ZiConnection> cxn = (ZiConnection *)fn(ci);

  if (!cxn) {
    Zi::closeSocket(ci.socket);
    return;
  }

  if (!cxnAdd(cxn, ci.socket)) return;

  cxn->connected();

  ZiDEBUG(this, ([
    socket = ci.socket,
    ip = ci.remoteIP,
    port = ci.remotePort
  ](auto &s) {
    s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>()
      << " TCP CONNECTED to " << ip << ':' << port;
  }));
}

void ZiConnection::telemetry(Ztc::CxnTelemetry &data) const
{
  unsigned rxBufSize = 0;
  unsigned txBufSize = 0;
  socklen_t l = sizeof(unsigned);
  getsockopt(
    m_info.socket, SOL_SOCKET, SO_RCVBUF,
    reinterpret_cast<char *>(&rxBufSize), &l);
  getsockopt(
    m_info.socket, SOL_SOCKET, SO_SNDBUF,
    reinterpret_cast<char *>(&txBufSize), &l);
#ifdef ZiMultiplex_EPoll
  int rxBufLen = 0;
  int txBufLen = 0;
  ioctl(m_info.socket, SIOCINQ, &rxBufLen);
  ioctl(m_info.socket, SIOCOUTQ, &txBufLen);
#endif
#ifdef ZiMultiplex_IOCP
  u_long rxBufLen = 0;
  u_long txBufLen = 0; // WinDoze - txBufLen is unavailable
  ioctlsocket(m_info.socket, FIONREAD, &rxBufLen);
#endif
  ZiIP mreqAddr, mreqIf;
  uint32_t mreqIfIndex = 0;
  const auto &mreqs = m_info.options.mreqs();
  if (mreqs.length()) {
    mreqAddr = mreqs[0].addr();
    switch (mreqs[0].type()) {
      case ZiIPType::V4:
	mreqIf = mreqs[0].mif();
	break;
      case ZiIPType::V6:
	mreqIfIndex = mreqs[0].ifIndex();
	break;
    }
  }
  data.mxID = m_mx->id();
  data.socket = m_info.socket;
  data.rxCalls = m_rxCalls;
  data.rxBytes = m_rxBytes;
  data.txCalls = m_txCalls;
  data.txBytes = m_txBytes;
  data.rxBufSize = rxBufSize;
  data.rxBufLen = rxBufLen;
  data.txBufSize = txBufSize;
  data.txBufLen = txBufLen;
  data.mreqAddr = mreqAddr;
  data.mreqIf = mreqIf;
  data.mreqIfIndex = mreqIfIndex;
  data.mif = m_info.options.mif();
  data.mifIndex = m_info.options.mifIndex();
  data.ttl = m_info.options.ttl();
  data.localIP = m_info.localIP;
  data.remoteIP = m_info.remoteIP;
  data.localPort = m_info.localPort;
  data.remotePort = m_info.remotePort;
  data.flags = m_info.options.flags();
  data.type = m_info.type;
}

unsigned ZiMultiplex::allCxns(Ztc::Mx::AllCxnsFn fn) const
{
  ZmRef<CxnHash> cxns = m_cxns;
  if (!cxns) return 0;
  unsigned n = cxns->count_();
  if (!n) return 0;
  using CxnRefs =
    ZtArray<ZmRef<ZiConnection>, ZtArrayHeapID<"Zi.CxnRefs">>;
  auto refs = ZtLocalArray(CxnRefs, n);
  {
    auto i = cxns->citer();
    while (ZmRef<ZiConnection> cxn = i.val())
      refs.push(ZuMv(cxn));
  }
  refs.all([&fn](ZmRef<ZiConnection> &cxn) { fn(cxn.ptr()); });
  return refs.length();
}

unsigned ZiMultiplex::allCxns_(Ztc::Mx::AllCxnsFn fn) const
{
  return allCxns(ZuMv(fn));
}

void ZiMultiplex::listen(
    ZiListenFn listenFn, ZiFailFn failFn, ZiConnectFn acceptFn,
    ZiIP localIP, uint16_t localPort, unsigned nAccepts, ZiCxnOptions options)
{
  if (ZuUnlikely(!running())) {
    Error("listen", Zi::NotReady, ZeOK);
    failFn(false);
    return;
  }

#ifdef ZiMultiplex_Netlink
  if (options.netlink()) {
    Error("listen", Zi::IOError, ZeError(ZiEINVAL));
    failFn(false);
    return;
  }
#endif

  rxInvoke([this, listenFn = ZuMv(listenFn),
      failFn = ZuMv(failFn), acceptFn = ZuMv(acceptFn),
      localIP, localPort, nAccepts, options = ZuMv(options)]() mutable {
    listen_(ZuMv(listenFn), ZuMv(failFn), ZuMv(acceptFn),
	localIP, localPort, nAccepts, options);
  });
}

void ZiMultiplex::listen_(
    ZiListenFn listenFn, ZiFailFn failFn, ZiConnectFn acceptFn,
    ZiIP localIP, uint16_t localPort, unsigned nAccepts, ZiCxnOptions options)
{
  Socket lsocket;
  ZiIPType::T ipType = !!localIP ? localIP.type() : ZiIPType::V4;
  if (!localIP) localIP = ZiIP_wildcard(ipType);

#ifndef _WIN32

  lsocket = (Socket)::socket(ZiIP_family(ipType), SOCK_STREAM, IPPROTO_TCP);
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
    if (bind(lsocket, local.sa(), local.len()) < 0) {
      ZeError e{errno};
      ::close(lsocket);
      Warning("bind", Zi::IOError, e);
      failFn(true);
      return;
    }
  }

#ifdef ZiMultiplex_EPoll
  if (fcntl(lsocket, F_SETFL, O_NONBLOCK) < 0) {
    ZeError e{errno};
    ::close(lsocket);
    Error("fcntl(F_SETFL, O_NONBLOCK)", Zi::IOError, e);
    failFn(false);
    return;
  }
#endif

#else /* !_WIN32 */

  lsocket = (Socket)::socket(ZiIP_family(ipType), SOCK_STREAM, IPPROTO_TCP);
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
    if (bind(lsocket, local.sa(), local.len())) {
      ZeError e(WSAGetLastError());
      ::closesocket(lsocket);
      Warning("bind", Zi::IOError, e);
      failFn(true);
      return;
    }
  }

#endif /* !_WIN32 */

  ZmRef<Listener> listener = new Listener(
      this, acceptFn, lsocket, nAccepts, localIP, localPort, options);

#ifdef ZiMultiplex_EPoll
  if (::listen(lsocket, nAccepts) < 0) {
    ZeError e{errno};
    ::close(lsocket);
    Error("listen", Zi::IOError, e);
    failFn(false);
    return;
  }
#endif

  if (!listenerAdd(listener, lsocket)) {
    Zi::closeSocket(lsocket);
    failFn(false);
    return;
  }

#ifdef ZiMultiplex_IOCP
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
#endif

  m_nAccepts += nAccepts;

  listenFn(listener->info());

#ifdef ZiMultiplex_IOCP
  for (unsigned i = 0; i < nAccepts; i++) accept(listener);
#endif
}

void ZiMultiplex::stopListening(ZiIP localIP, uint16_t localPort)
{
  if (ZuUnlikely(!running())) return;

  rxInvoke([this, localIP, localPort]() {
    stopListening_(localIP, localPort);
  });
}

void ZiMultiplex::stopListening_(ZiIP localIP, uint16_t localPort)
{
  Socket lsocket;
  unsigned nAccepts;

  {
    ListenerHash::CIter i(*m_listeners);
    ZmRef<Listener> listener;

    while (listener = i()) {
      if (localIP == listener->info().ip &&
	  localPort == listener->info().port) {
	lsocket = listener->info().socket;
	nAccepts = listener->info().nAccepts;
	goto found;
      }
    }
    return;
  }

found:
  listenerDel(lsocket);

  // under IOCP this will cause all pending accepts to complete with an error
  Zi::closeSocket(lsocket);

  m_nAccepts -= nAccepts;
}

void ZiMultiplex::accept(Listener *listener)
{
#ifdef ZiMultiplex_IOCP
  Socket s = (Socket)::socket(
    ZiIP_family(listener->info().ip.type()), SOCK_STREAM, IPPROTO_TCP);
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
  Zi_Overlapped &overlapped = request->overlapped();
  ZeError e;
  if (ZuLikely(AcceptEx(listener->info().socket, s, request->buf(), 0,
	  ZiSockAddr::MaxLen + 16,
	  ZiSockAddr::MaxLen + 16,
	  0, (OVERLAPPED *)&overlapped) ||
	(e = WSAGetLastError()).errNo() == WSA_IO_PENDING))
    return;
  delete request;
  ::closesocket(s);
  Error("AcceptEx", Zi::IOError, e);
#endif

#ifdef ZiMultiplex_EPoll
  int s;
  ZeError e;
  ZiSockAddr remote;

retry:
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

  ZiDEBUG(this, ([
    socket = s,
    ip = remote.ip(),
    port = remote.port()
  ](auto &s) {
    s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>()
      << " ACCEPTING from " << ip << ':' << port;
  }));

  executedConnect(listener->acceptFn(), ZiCxnInfo {
      ZiCxnType::TCPIn, s, listener->info().options,
      listener->info().ip, listener->info().port,
      remote.ip(), remote.port() });

  goto retry;
#endif
}

#ifdef ZiMultiplex_IOCP
void ZiMultiplex::overlappedAccept(Accept *request,
    int status, unsigned, ZeError e)
{
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
    ZiSockAddr *localSockAddr, *remoteSockAddr;
    int localSockAddrLen, remoteSockAddrLen;
    GetAcceptExSockaddrs(
	request->buf(), 0, ZiSockAddr::MaxLen + 16,
	ZiSockAddr::MaxLen + 16, (struct sockaddr **)&localSockAddr,
	&localSockAddrLen, (struct sockaddr **)&remoteSockAddr,
	&remoteSockAddrLen);
    ci.localIP = localSockAddr->ip();
    ci.localPort = localSockAddr->port();
    ci.remoteIP = remoteSockAddr->ip();
    ci.remotePort = remoteSockAddr->port();
  }

  executedConnect(listener->acceptFn(), ci);
}
#endif

void ZiConnection::connected()
{
  connected(m_rxContext);

#ifdef ZiMultiplex_IOCP
  if (ZuLikely(!m_rxContext.completed()))
    recv();
#endif

#ifdef ZiMultiplex_EPoll
  if (ZuUnlikely(m_rxContext.completed()))
    m_mx->epollRecv(this, m_info.socket, 0);
#endif
}

void ZiConnection::recv(ZiIOFn fn)
{
  m_mx->rxInvoke(this, [this, fn = ZuMv(fn)]() mutable {
    recv_(ZuMv(fn));
    return this;
  });
}

void ZiConnection::recv_(ZiIOFn fn)
{
  m_rxContext.init_(ZuMv(fn));

#ifdef ZiMultiplex_IOCP
  if (ZuLikely(!m_rxContext.completed())) recv();
#endif

#ifdef ZiMultiplex_EPoll
  if (ZuLikely(!m_rxContext.completed())) {
    if (!m_mx->epollRecv(this, m_info.socket, EPOLLIN | EPOLLRDHUP))
      m_rxContext.complete();
    else
      recv(); // strictly speaking this pump-priming should not be needed,
	      // but edge-triggered epoll does not behave identically
	      // under valgrind; while this call does potentially reduce
	      // latency, that benefit is marginal - in a latency-sensitive
	      // app, recv_() is invoked just once or twice per connection
	      // during initial setup - the receive context will remain
	      // open for the bulk of the communication
  }
#endif
}

#ifdef ZiMultiplex_EPoll
bool ZiMultiplex::epollRecv(ZiConnection *cxn, int s, uint32_t events)
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
#endif

#ifdef ZiMultiplex_EPoll
bool ZiConnection::recv()
#else
void ZiConnection::recv()
#endif
{
#ifdef ZiMultiplex_DEBUG
  if (m_mx->trace()) m_mx->traceCapture();
#endif

  if (ZuUnlikely(!m_rxUp.load_())) {
    m_rxContext.complete();
#ifdef ZiMultiplex_EPoll
    return false;
#else
    return;
#endif
  }

  if (ZuUnlikely(m_rxContext.completed()))
#ifdef ZiMultiplex_EPoll
    return true;
#else
    return;
#endif

#ifdef ZiMultiplex_IOCP
  unsigned len = m_rxContext.size - m_rxContext.offset;

  WSABUF wsaBuf;
  wsaBuf.buf = reinterpret_cast<char *>(m_rxContext.ptr + m_rxContext.offset);
  wsaBuf.len = len;

  using Executed = Zi_Overlapped::Executed;
  Zi_Overlapped &overlapped = m_rxOverlapped;

#ifdef ZiMultiplex_DEBUG
  if (ZuUnlikely(!m_info.options.udp() && m_mx->frag())) {
    unsigned l = ((m_rxContext.offset + 8)>>1) + 1;
    if (len > l) len = l;
  }
  if (ZuUnlikely(m_mx->debug())) {
    unsigned long n;
    DWORD o;
    if (!WSAIoctl(
	m_info.socket, FIONREAD, 0, 0, &n, sizeof(unsigned long), &o, 0, 0)) {
      ZiDEBUG(m_mx, ([
	socket = m_info.socket,
	len,
	size = m_rxContext.size,
	offset = m_rxContext.offset,
	buffered = n,
	overlapped,
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
  overlapped.init(Executed::Member<&ZiConnection::overlappedRecv>::fn(this));
  ZeError e;
  DWORD n;
  if (m_info.options.udp()) {
    ZiIPType::T type;
    if (!ZiIP_cxnType(m_info.localIP, m_info.remoteIP, type))
      type = ZiIPType::V4;
    m_rxContext.addr.init(type);
    memset(&m_rxMsg, 0, sizeof(m_rxMsg));
    m_rxWSABuf = wsaBuf;
    m_rxMsg.name = m_rxContext.addr.sa();
    m_rxMsg.namelen = m_rxContext.addr.len();
    m_rxMsg.lpBuffers = &m_rxWSABuf;
    m_rxMsg.dwBufferCount = 1;
    m_rxMsg.Control.buf = reinterpret_cast<char *>(m_rxControl);
    m_rxMsg.Control.len = sizeof(m_rxControl);
    m_rxMsg.dwFlags = 0;
    if (ZuLikely(WSARecvMsg(m_info.socket, &m_rxMsg, &n,
	    (WSAOVERLAPPED *)&overlapped, 0) != SOCKET_ERROR ||
	  (e = WSAGetLastError()).errNo() == WSA_IO_PENDING)) {
#ifdef ZiMultiplex_DEBUG
      if (m_mx->yield()) Zm::yield();
#endif
      return;
    }
  } else {
    if (ZuLikely(WSARecv(m_info.socket, &wsaBuf, 1, &n, &m_rxFlags,
	    (WSAOVERLAPPED *)&overlapped, 0) != SOCKET_ERROR ||
	  (e = WSAGetLastError()).errNo() == WSA_IO_PENDING)) {
#ifdef ZiMultiplex_DEBUG
      if (m_mx->yield()) Zm::yield();
#endif
      return;
    }
  }
  errorRecv(Zi::IOError, e);
  m_rxContext.completed();
  return;
#endif

#ifdef ZiMultiplex_EPoll
  if (ZuUnlikely(m_rxContext.completed())) {
    m_mx->epollRecv(this, m_info.socket, 0);
    return true;
  }

  unsigned len = m_rxContext.size - m_rxContext.offset;
  auto buf = m_rxContext.ptr + m_rxContext.offset;

  ZeError e;
  int n;

#ifdef ZiMultiplex_DEBUG
  if (ZuUnlikely(!m_info.options.udp() && m_mx->frag())) {
    unsigned l = ((m_rxContext.offset + 8)>>1) + 1;
    if (len > l) len = l;
  }
  if (ZuUnlikely(m_mx->debug())) {
    ioctl(m_info.socket, FIONREAD, &n);
    ZiDEBUG(m_mx, ([
      socket = m_info.socket,
      len,
      size = m_rxContext.size,
      offset = m_rxContext.offset,
      buffered = n
    ](auto &s) {
      s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>()
	<< " recv(" << len << ") size: " << size
	<< " offset: " << offset << " buffered: " << buffered;
    }));
  }
#endif

retry:
  if (m_info.options.udp()) {
    ZiIPType::T type;
    if (!ZiIP_cxnType(m_info.localIP, m_info.remoteIP, type))
      type = ZiIPType::V4;
    m_rxContext.addr.init(type);
    struct iovec iov;
    iov.iov_base = reinterpret_cast<char *>(buf);
    iov.iov_len = len;
    alignas(struct cmsghdr) uint8_t control[CMSG_SPACE(sizeof(int))];
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_name = m_rxContext.addr.sa();
    msg.msg_namelen = m_rxContext.addr.len();
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);
    n = ::recvmsg(m_info.socket, &msg, 0);
    if (n >= 0) {
      m_rxContext.addr.sync();
      m_rxContext.tos = ZiGetTOS(msg);
    }
  } else {
    n = ::recv(
      m_info.socket, reinterpret_cast<char *>(buf), len, 0);
  }
  if (ZuUnlikely(n < 0)) e = errno;

  ZiDEBUG(m_mx, ([
    socket = m_info.socket,
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
      if (m_mx->yield()) Zm::yield();
#endif
      return true;
    }
    if (e.errNo() == EINTR) goto retry;
    if (m_info.options.udp() &&
	(e.errNo() == ZiECONNREFUSED || e.errNo() == ZiECONNRESET))
      return true;
    errorRecv(Zi::IOError, e);
    m_rxContext.completed();
    return false;
  }

  ZiDEBUG(m_mx, ([
    socket = m_info.socket,
    len,
    n,
    buf = ZmMkRef(new ZiDebugBuf(buf, n))
  ](auto &s) {
    s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>()
      << " recv(" << len << "): " << n << '\n'
      << ZtHexDump_(buf->cspan());
  }));

  if (ZuUnlikely(!n)) {
    if (m_info.options.udp()) return true;
    m_rxContext.completed();
    disconnect(true);
    return false;
  }

  executedRecv(n);

  if (ZuUnlikely(m_rxContext.completed())) {
    if (m_rxContext.disconnected()) {
      disconnect(false);
      return false;
    } else {
      m_mx->epollRecv(this, m_info.socket, 0);
      return true;
    }
  }

  if (ZuUnlikely(m_rxContext.offset >= m_rxContext.size)) {
    m_rxContext.complete();
    m_mx->epollRecv(this, m_info.socket, 0);
    return true;
  }

  len = m_rxContext.size - m_rxContext.offset;
  buf = m_rxContext.ptr + m_rxContext.offset;
  goto retry;
#endif
}

#ifdef ZiMultiplex_IOCP
void ZiConnection::overlappedRecv(int status, unsigned n, ZeError e)
{
  if (ZuUnlikely(status != Zi::OK)) {
    if (m_rxUp.load_()) errorRecv(status, e);
    return;
  }
  if (ZuUnlikely(!n && !m_info.options.udp())) {
    if (m_rxUp.load_()) disconnect(true);
    return;
  }

  ZiDEBUG(m_mx, ([
    socket = m_info.socket,
    n,
    buf = ZmMkRef(new ZiDebugBuf(
      m_rxContext.ptr + m_rxContext.offset, n))
  ](auto &s) {
    s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>()
      << " WSARecv(" << n << ")\n"
      << ZtHexDump_(buf->cspan());
  }));

  if (m_info.options.udp()) {
    m_rxContext.addr.sync();
    m_rxContext.tos = ZiGetTOS(m_rxMsg);
  }
  executedRecv(n);

  if (ZuUnlikely(m_rxContext.completed())) {
    if (m_rxContext.disconnected()) disconnect(false);
    return;
  }

  if (ZuUnlikely(m_rxContext.offset >= m_rxContext.size)) {
    m_rxContext.complete();
    return;
  }

  recv();
}
#endif

void ZiConnection::errorRecv(int status, ZeError e)
{
  bool peer = status == Zi::IOError &&
    (e.errNo() == ZiENOTCONN || e.errNo() == ZiECONNRESET);
  if (m_rxUp.load_()) close(peer);
  if (status == Zi::IOError &&
      (e.errNo() == ZiENOTCONN || e.errNo() == ZiECONNRESET)) return;
  Error(
#ifndef _WIN32
      "recv",
#else
      "WSARecv",
#endif
      Zi::IOError, e);
}

void ZiConnection::executedRecv(unsigned n)
{
  ZiDEBUG(m_mx, "executedRecv()");

  ZmAssert(!m_rxContext.completed());

#ifdef ZiMultiplex_FILTER
  if (ZuUnlikely(m_mx->m_rxFilter(
	  this, m_rxContext.ptr + m_rxContext.offset, n)))
    return;
#endif

  m_rxCalls++, m_rxBytes += n;
  m_rxContext.length = n;
  while (!m_rxContext());
}

void ZiConnection::send(ZiIOFn fn)
{
  m_mx->txInvoke([this, fn = ZuMv(fn)]() mutable {
    send_(ZuMv(fn));
    return this;
  });
}

void ZiConnection::send_(ZiIOFn fn)
{
  if (fn(m_txContext)) send();
}

void ZiConnection::sync()
{
  ZmBlock<>{}([this](auto wake) {
    m_mx->txInvoke([this, wake = ZuMv(wake)]() mutable {
      while (!m_txContext.completed()) send();
      wake();
    });
  });
}

void ZiConnection::send()
{
#ifdef ZiMultiplex_DEBUG
  if (m_mx->trace()) m_mx->traceCapture();
#endif

  if (ZuUnlikely(!m_txUp.load_())) { m_txContext.complete(); return; }

  if (ZuLikely(m_txContext.completed())) return;

  unsigned n;
#ifdef ZiMultiplex_IOCP
  WSABUF wsaBuf;
  wsaBuf.buf = reinterpret_cast<char *>(m_txContext.ptr + m_txContext.offset);
  wsaBuf.len = m_txContext.size - m_txContext.offset;

  ZeError e;
  DWORD n_;
#endif

#ifdef ZiMultiplex_EPoll
  auto buf = m_txContext.ptr + m_txContext.offset;
  unsigned len = m_txContext.size - m_txContext.offset;

  ZeError e;
  int n_;
#endif

#ifdef ZiMultiplex_FILTER
  if (ZuUnlikely(m_mx->m_txFilter(
	  this, m_txContext.ptr + m_txContext.offset,
	  m_txContext.size - m_txContext.offset))) {
    n = m_txContext.size - m_txContext.offset;
    goto executed;
  }
#endif

#ifdef ZiMultiplex_IOCP
retry:
  ZiDEBUG(m_mx, ([
    socket = m_info.socket,
    size = m_txContext.size,
    offset = m_txContext.offset,
    buf = ZmMkRef(new ZiDebugBuf(wsaBuf.buf, wsaBuf.len))
  ](auto &s) {
    s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>()
      << " WSASend(" << buf->length << ") size: " << size
      << " offset: " << offset << '\n'
      << ZtHexDump_(buf->cspan());
  }));

  if (m_info.options.udp()) {
    uint8_t controlBuf[WSA_CMSG_SPACE(sizeof(int))];
    WSABUF control;
    control.buf = reinterpret_cast<char *>(controlBuf);
    control.len = sizeof(controlBuf);
    WSAMSG msg;
    memset(&msg, 0, sizeof(msg));
    if (!!m_txContext.addr) {
      msg.name = m_txContext.addr.sa();
      msg.namelen = m_txContext.addr.len();
    }
    msg.lpBuffers = &wsaBuf;
    msg.dwBufferCount = 1;
    msg.dwFlags = 0;
    ZiIPType::T type;
    if (!ZiIP_cxnType(m_info.localIP, m_info.remoteIP, type))
      type = ZiIPType::V4;
    if (type == ZiIPType::V6) {
#ifdef IPV6_TCLASS
      ZiPutTOS(msg, control, m_txContext.tos, IPPROTO_IPV6, IPV6_TCLASS);
#endif
    } else
      ZiPutTOS(msg, control, m_txContext.tos, IPPROTO_IP, IP_TOS);
    if (ZuUnlikely(WSASendMsg(m_info.socket, &msg, 0, &n_, 0, 0) ==
	  SOCKET_ERROR)) {
      e = WSAGetLastError();
      errorSend(Zi::IOError, e);
      return;
    }
    n = n_;
    ZiDEBUG(m_mx, ([
      socket = m_info.socket,
      len = wsaBuf.len,
      n
    ](auto &s) {
      s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>()
	<< " WSASendMsg(" << len << "): " << n;
    }));
  } else {
    if (ZuUnlikely(WSASend(m_info.socket, &wsaBuf, 1, &n_, 0,
	    0, 0) == SOCKET_ERROR)) {
      e = WSAGetLastError();
      errorSend(Zi::IOError, e);
      return;
    }
    n = n_;
    ZiDEBUG(m_mx, ([
      socket = m_info.socket,
      len = wsaBuf.len,
      n
    ](auto &s) {
      s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>()
	<< " WSASend(" << len << "): " << n;
    }));
  }
#endif

#ifdef ZiMultiplex_EPoll
retry:
  ZiDEBUG(m_mx, ([
    socket = m_info.socket,
    size = m_txContext.size,
    offset = m_txContext.offset,
    buf = ZmMkRef(new ZiDebugBuf(buf, len))
  ](auto &s) {
    s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>()
      << " send(" << buf->length << ") size: " << size
      << " offset: " << offset << '\n'
      << ZtHexDump_(buf->cspan());
  }));

  if (m_info.options.udp()) {
    struct iovec iov;
    iov.iov_base = reinterpret_cast<char *>(buf);
    iov.iov_len = len;
    alignas(struct cmsghdr) uint8_t control[CMSG_SPACE(sizeof(int))];
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    if (!!m_txContext.addr) {
      msg.msg_name = m_txContext.addr.sa();
      msg.msg_namelen = m_txContext.addr.len();
    }
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    ZiIPType::T type;
    if (!ZiIP_cxnType(m_info.localIP, m_info.remoteIP, type))
      type = ZiIPType::V4;
    if (type == ZiIPType::V6) {
#ifdef IPV6_TCLASS
      ZiPutTOS(msg, control, sizeof(control), m_txContext.tos,
	IPPROTO_IPV6, IPV6_TCLASS);
#endif
    } else
      ZiPutTOS(msg, control, sizeof(control), m_txContext.tos,
	IPPROTO_IP, IP_TOS);
    n_ = ::sendmsg(m_info.socket, &msg, 0);
  }
#ifdef ZiMultiplex_Netlink
  else if (m_info.options.netlink())
    n_ = ZiNetlink::send(m_info.socket, m_ci.familyID, m_ci.portID, buf, len);
#endif
  else
    n_ = ::send(m_info.socket, buf, len, 0);
  if (ZuUnlikely(n_ < 0)) e = errno;

  ZiDEBUG(m_mx, ([
    socket = m_info.socket,
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
      if (m_mx->yield()) Zm::yield();
#endif
      return;
    }
    if (e.errNo() == EINTR) goto retry;
    errorSend(Zi::IOError, e);
    return;
  }
  n = n_;

  ZiDEBUG(m_mx, ([
    socket = m_info.socket,
    len,
    n
  ](auto &s) {
    s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>()
      << " send(" << len << "): " << n;
  }));
#endif

#ifdef ZiMultiplex_FILTER
executed:
#endif
  executedSend(n);

  if (ZuLikely(m_txContext.completed())) {
    if (m_txContext.disconnected()) disconnect(false);
    return;
  }

  if (ZuLikely(m_txContext.offset >= m_txContext.size)) {
    m_txContext.complete();
    return;
  }

#ifdef ZiMultiplex_IOCP
  wsaBuf.buf = reinterpret_cast<char *>(m_txContext.ptr + m_txContext.offset);
  wsaBuf.len = m_txContext.size - m_txContext.offset;
#endif

#ifdef ZiMultiplex_EPoll
  buf = m_txContext.ptr + m_txContext.offset;
  len = m_txContext.size - m_txContext.offset;
#endif

  goto retry;
}

void ZiConnection::errorSend(int status, ZeError e)
{
  m_txContext.length = -1;
  m_txContext();
  if (m_info.options.udp() && status == Zi::IOError &&
      (e.errNo() == ZiECONNREFUSED || e.errNo() == ZiECONNRESET))
    return;
  bool peer = status == Zi::IOError &&
    (e.errNo() == ZiENOTCONN || e.errNo() == ZiECONNRESET);
  close_1(peer);
  if (status == Zi::IOError &&
      (e.errNo() == ZiENOTCONN || e.errNo() == ZiECONNRESET)) return;
  Error(
#ifndef _WIN32
      "send",
#else
      "WSASend",
#endif
      status, e);
}

void ZiConnection::executedSend(unsigned n)
{
  ZiDEBUG(m_mx, "executedSend()");

#ifdef ZiMultiplex_IOCP
  ZiDEBUG(m_mx, ([
    socket = m_info.socket,
    n
  ](auto &s) {
    s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>()
      << " WSASend(" << n << ')';
  }));
#endif

  m_txCalls++, m_txBytes += n;

  m_txContext.length = n;
  // the completion may synchronously call send_() for the next message
  while (!m_txContext());
}

bool ZiMultiplex::initSocket(Socket s, const ZiCxnOptions &options)
{
#ifndef _WIN32
  {
    int b = 1;
    static struct linger l = { 0, 0 };
    if (m_rxBufSize && setsockopt(s, SOL_SOCKET, SO_RCVBUF,
	  (const char *)&m_rxBufSize, sizeof(unsigned)) < 0) {
      Error("setsockopt(SO_RCVBUF)", Zi::IOError, ZeLastSockError);
      return false;
    }
    if (m_txBufSize && setsockopt(s, SOL_SOCKET, SO_SNDBUF,
	  (const char *)&m_txBufSize, sizeof(unsigned)) < 0) {
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
    if (!options.udp() && !options.netlink() && !options.nagle() &&
	setsockopt(s, IPPROTO_TCP, TCP_NODELAY,
	  (const char *)&b, sizeof(int)) < 0) {
      Error("setsockopt(TCP_NODELAY)", Zi::IOError, ZeLastSockError);
      return false;
    }
  }

#ifdef ZiMultiplex_EPoll
  if (fcntl(s, F_SETFL, O_NONBLOCK) < 0) {
    Error("fcntl(O_NONBLOCK)", Zi::IOError, ZeLastError);
    return false;
  }
#endif

#else /* !_WIN32 */

  {
    BOOL b = TRUE;
    if (m_rxBufSize && setsockopt(s, SOL_SOCKET, SO_RCVBUF,
	  (const char *)&m_rxBufSize, sizeof(unsigned))) {
      Error("setsockopt(SO_RCVBUF)", Zi::IOError, ZeLastSockError);
      return false;
    }
    if (m_txBufSize && setsockopt(s, SOL_SOCKET, SO_SNDBUF,
	  (const char *)&m_txBufSize, sizeof(unsigned))) {
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
    if (!options.udp() && !options.netlink() && !options.nagle() &&
	setsockopt(s, IPPROTO_TCP, TCP_NODELAY,
	  (const char *)&b, sizeof(BOOL))) {
      Error("setsockopt(TCP_NODELAY)", Zi::IOError, ZeLastSockError);
      return false;
    }
  }

#ifdef ZiMultiplex_IOCP
  if (!CreateIoCompletionPort((HANDLE)s, m_completionPort, 0, 0)) {
    Error("CreateIoCompletionPort", Zi::IOError, ZeLastError);
    return false;
  }
#endif

#endif /* !_WIN32 */

  return true;
}

bool ZiMultiplex::cxnAdd(ZiConnection *cxn, Socket s)
{
  m_cxns->add(cxn);

#ifdef ZiMultiplex_EPoll
  {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(struct epoll_event));
    ev.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET;
    ev.data.u64 = u64_cxn(cxn);
    if (epoll_ctl(m_epollFD, EPOLL_CTL_ADD, s, &ev) < 0) {
      ZeError e{errno};
      m_cxns->del(s);
      Zi::closeSocket(s);
      Error("epoll_ctl(EPOLL_CTL_ADD)", Zi::IOError, e);
      return false;
    }
  }
#endif

  ZiMxMgr_::instance()->cxnAdded_(cxn);
  return true;
}

void ZiMultiplex::cxnDel(Socket s)
{
#ifdef ZiMultiplex_EPoll
  epoll_ctl(m_epollFD, EPOLL_CTL_DEL, s, 0);
#endif

  if (ZmRef<ZiConnection> cxn = m_cxns->valMv(m_cxns->del(s)))
    ZiMxMgr_::instance()->cxnDeleted_(cxn);
}

void ZiMultiplex::watch(
    Ztc::Mx::AddCxnFn addCxnFn, Ztc::Mx::DelCxnFn delCxnFn,
    Ztc::Mx::AddQueueFn addQueueFn, Ztc::Mx::DelQueueFn delQueueFn)
{
  ZiMxMgr_::instance()->watch(
    ZuMv(addCxnFn), ZuMv(delCxnFn),
    ZuMv(addQueueFn), ZuMv(delQueueFn));
}

void ZiMultiplex::unwatch()
{
  ZiMxMgr_::instance()->unwatchMx();
}

bool ZiMultiplex::listenerAdd(Listener *listener, Socket s)
{
  m_listeners->addNode(listener);

#ifdef ZiMultiplex_EPoll
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
#endif

  return true;
}

void ZiMultiplex::listenerDel(Socket s)
{
#ifdef ZiMultiplex_EPoll
  epoll_ctl(m_epollFD, EPOLL_CTL_DEL, s, 0);
#endif

  if (ZmRef<Listener> listener = m_listeners->del(s))
    listener->down();
}

#ifdef ZiMultiplex_EPoll
bool ZiMultiplex::connectAdd(Connect *request, Socket s)
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

#endif

void ZiMultiplex::connectDel(Socket s)
{
#ifdef ZiMultiplex_EPoll
  epoll_ctl(m_epollFD, EPOLL_CTL_DEL, s, 0);
#endif

#if ZiMultiplex__ConnectHash
  m_connects->del(s);
#endif
}

void ZiConnection::disconnect()
{
  disconnect(false);
}

void ZiConnection::disconnect(bool peer)
{
  m_mx->txInvoke(this, [this, peer]() { disconnect_1(peer); return this; });
}

void ZiConnection::disconnect_1(bool peer)
{
  if (!m_txUp.load_()) {
    m_mx->rxRun([cxn = ZmMkRef(this), peer]() {
      cxn->notifyDisconnected(peer);
    });
    return;
  }
  
  m_txUp = false;

  m_mx->rxRun([cxn = ZmMkRef(this), peer]() { cxn->disconnect_2(peer); });
}

void ZiConnection::disconnect_2(bool peer)
{
  if (!m_rxUp.load_()) {
    notifyDisconnected(peer);
    return;
  }

  m_rxUp = 0;

  if (m_info.options.udp()
#ifdef ZiMultiplex_NetLink
      || m_info.options.netlink()
#endif
      ) {
    executedDisconnect(peer);
    return;
  }

  ZiDEBUG(m_mx, ([socket = m_info.socket](auto &s) {
    s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>() <<
      " disconnect()";
  }));
  
#ifdef ZiMultiplex_IOCP
  using Executed = Zi_Overlapped::Executed;
  Zi_Overlapped &overlapped = m_discOverlapped;
  ZeError e;
  m_discPeer = peer;
  overlapped.init(
      Executed::Member<&ZiConnection::overlappedDisconnect>::fn(this));
  if (ZuLikely(DisconnectEx(m_info.socket, (OVERLAPPED *)&overlapped, 0, 0) ||
	(e = WSAGetLastError()).errNo() == WSA_IO_PENDING))
    return;
  errorDisconnect(Zi::IOError, e, peer);
#endif

#ifdef ZiMultiplex_EPoll
  ZeError e;
  int i;

retry:
  i = ::shutdown(m_info.socket, SHUT_RDWR);
  if (i < 0) {
    e = errno;
    if (e.errNo() == EAGAIN) return;
    if (e.errNo() == EINTR) goto retry;
    errorDisconnect(Zi::IOError, e, peer);
  }
  executedDisconnect(peer);
#endif
}

#ifdef ZiMultiplex_IOCP
void ZiConnection::overlappedDisconnect(int status, unsigned n, ZeError e)
{
  if (ZuUnlikely(status != Zi::OK)) {
    errorDisconnect(status, e, m_discPeer);
    return;
  }
  executedDisconnect(m_discPeer);
}
#endif

void ZiConnection::errorDisconnect(int status, ZeError e, bool peer)
{
  notifyDisconnected(peer);
  if (e.errNo() != ZiENOTCONN)
    Error(
#ifndef _WIN32
      "shutdown",
#else
      "DisconnectEx",
#endif
      status, e);
}

void ZiConnection::executedDisconnect(bool peer)
{
  notifyDisconnected(peer);
}

void ZiConnection::notifyDisconnected(bool peer)
{
  ZmRef<ZiConnection> this_{this}; // maintain +ve ref count in scope
  if (!Zi::nullSocket(m_info.socket)) {
    m_mx->disconnected(this);
    Zi::closeSocket(m_info.socket);
    m_info.socket = Zi::nullSocket();
  }
  disconnected(peer);
}

void ZiMultiplex::disconnected(ZiConnection *cxn)
{
  Socket s = cxn->info().socket;

  ZiDEBUG(this, ([socket = s](auto &s) {
    s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>() <<
      " disconnected()";
  }));

  cxnDel(s);
  
  if (ZuUnlikely(m_stopping && !m_cxns->count_())) stop_1();
}

void ZiConnection::close()
{
  close(false);
}

void ZiConnection::close(bool peer)
{
  m_mx->txInvoke(this, [this, peer]() { close_1(peer); return this; });
}

void ZiConnection::close_1(bool peer)
{
  if (!m_txUp.load_()) {
    m_mx->rxRun([cxn = ZmMkRef(this), peer]() {
      cxn->notifyDisconnected(peer);
    });
    return;
  }
  
  m_txUp = false;

  m_mx->rxRun([cxn = ZmMkRef(this), peer]() { cxn->close_2(peer); });
}

void ZiConnection::close_2(bool peer)
{
  if (!m_rxUp.load_()) {
    notifyDisconnected(peer);
    return;
  }
  
  m_rxUp = 0;

  ZiDEBUG(m_mx, ([socket = m_info.socket](auto &s) {
    s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>() << " close()";
  }));

  executedDisconnect(peer);
}

ZiMultiplex::ZiMultiplex(ZiMxParams mxParams) :
  ZmScheduler(ZuMv(mxParams.scheduler())),
  m_rxThread(mxParams.rxThread()),
  m_txThread(mxParams.txThread()),
  m_rxBufSize(mxParams.rxBufSize()),
  m_txBufSize(mxParams.txBufSize())
#ifdef ZiMultiplex_EPoll
  , m_epollMaxFDs(mxParams.epollMaxFDs()),
  m_epollQuantum(mxParams.epollQuantum())
#endif
#ifdef ZiMultiplex_DEBUG
  , m_trace(mxParams.trace()),
  m_debug(mxParams.debug()),
  m_frag(mxParams.frag()),
  m_yield(mxParams.yield())
#endif
{
  m_listeners =
    new ListenerHash(ZmHashParams().bits(4).loadFactor(1).cBits(4).
	init(mxParams.listenerHash()));
#if ZiMultiplex__ConnectHash
  m_connects =
    new ConnectHash(ZmHashParams().bits(5).loadFactor(1).cBits(4).
	init(mxParams.requestHash()));
#endif
  m_cxns = new CxnHash(ZmHashParams().bits(8).loadFactor(1).cBits(4).
      init(mxParams.cxnHash()));
  if (!params().thread(m_rxThread).name())
    params_().thread(m_rxThread).name("ioRx");
  if (!params().thread(m_txThread).name())
    params_().thread(m_txThread).name("ioTx");

#ifdef ZiMultiplex_EPoll
  ZmScheduler::threadInit([]() {
    sigset_t s;
    sigemptyset(&s);
    sigaddset(&s, SIGPIPE);
    sigaddset(&s, SIGURG);
    pthread_sigmask(SIG_BLOCK, &s, 0);
  });
#endif
  ZiMxMgr_::instance()->add(this);
}

ZiMultiplex::~ZiMultiplex()
{
  ZiMxMgr_::instance()->del(this);
}

bool ZiMultiplex::start__()
{
  if (!ZmScheduler::start__()) return false;

#ifdef ZiMultiplex_IOCP
  {
    WSADATA wd;
    DWORD errNo = WSAStartup(MAKEWORD(2, 2), &wd);
    if (errNo) {
      Error("WSAStartup", Zi::IOError, ZeError(errNo));
      return false;
    }
  }

  m_completionPort =
    CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, params().nThreads());
  if (!m_completionPort) {
    ZeError e(GetLastError());
    WSACleanup();
    Error("CreateIoCompletionPort", Zi::IOError, e);
    return false;
  }
#endif

#ifdef ZiMultiplex_EPoll
  {
    sigset_t s;
    sigemptyset(&s);
    sigaddset(&s, SIGPIPE);
    sigaddset(&s, SIGURG);
    pthread_sigmask(SIG_BLOCK, &s, 0);
  }
  if ((m_epollFD = epoll_create(m_epollMaxFDs)) < 0) {
    Error("epoll_create", Zi::IOError, ZeLastError);
    return false;
  }
  if (pipe(&m_wakeFD) < 0) {
    ZeError e{errno};
    ::close(m_epollFD); m_epollFD = -1;
    Error("pipe", Zi::IOError, e);
    return false;
  }
  if (fcntl(m_wakeFD, F_SETFL, O_NONBLOCK) < 0) {
    ZeError e{errno};
    ::close(m_epollFD); m_epollFD = -1;
    ::close(m_wakeFD); m_wakeFD = -1;
    ::close(m_wakeFD2); m_wakeFD2 = -1;
    Error("fcntl(F_SETFL, O_NONBLOCK)", Zi::IOError, e);
    return false;
  }
  {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(struct epoll_event));
    ev.events = EPOLLIN;
    ev.data.u64 = u64_event();
    if (epoll_ctl(m_epollFD, EPOLL_CTL_ADD, m_wakeFD, &ev) < 0) {
      ZeError e{errno};
      ::close(m_epollFD); m_epollFD = -1;
      ::close(m_wakeFD); m_wakeFD = -1;
      ::close(m_wakeFD2); m_wakeFD2 = -1;
      Error("epoll_ctl(EPOLL_CTL_ADD)", Zi::IOError, e);
      return false;
    }
  }
#endif

  wakeFn(rxThread(),
    WakeFn{this, [](ZiMultiplex *mx) { mx->wakeRx(); }});
  push([this]() { rx(); }, rxThread());
  return true;
}

bool ZiMultiplex::stop()
{
  wake();
  return ZmScheduler::stop();
}

// the scheduler's control thread synchronously blocks on shutdown
bool ZiMultiplex::stop__()
{
  auto &stopping = ZmTLS<ZmSemaphore, &ZiMultiplex::stop__>();

  m_stopping = &stopping;

  rxInvoke([this]() { stop_0(); });

  stopping.wait();

  wake();

  stop_2();

  m_stopping = nullptr;

  return ZmScheduler::stop__();
}

void ZiMultiplex::stop_0()
{
  if (!m_cxns->count_()) { stop_1(); return; }

  auto i = m_cxns->citer();
  while (ZmRef<ZiConnection> cxn = i.val())
    cxn->disconnect();
}

void ZiMultiplex::stop_1()
{
#ifdef ZiMultiplex_EPoll
  {
    ConnectHash::Iter i(*m_connects);
    while (ZmRef<Connect> connect = i()) {
      i.del();
      ::close(connect->info().socket);
    }
  }
#endif

  {
    ListenerHash::Iter i(*m_listeners);
    while (ZmRef<Listener> listener = i()) {
      i.del();
      listener->down();
      Zi::closeSocket(listener->info().socket);
    }
  }

  // drain any I/O completions
#ifdef ZiMultiplex_IOCP
  {
    DWORD len;
    ULONG_PTR key;
    Zi_Overlapped *overlapped;
    while (GetQueuedCompletionStatus(
	  m_completionPort, &len, &key, (OVERLAPPED **)&overlapped, 0)) {
      if (overlapped) overlapped->complete(Zi::OK, len, ZeOK);
    }
  }
#endif

  if (m_stopping) // paranoia
    m_stopping->post();
}

void ZiMultiplex::stop_2()
{
  // close down underlying I/O platform

  wakeFn(rxThread(), WakeFn{});

#ifdef ZiMultiplex_IOCP
  CloseHandle(m_completionPort);
  m_completionPort = Zi::nullHandle();

  WSACleanup();	// this is reference counted
#endif

#ifdef ZiMultiplex_EPoll
  if (m_wakeFD >= 0) { ::close(m_wakeFD); m_wakeFD = -1; }
  if (m_wakeFD2 >= 0) { ::close(m_wakeFD2); m_wakeFD2 = -1; }
  if (m_epollFD >= 0) { ::close(m_epollFD); m_epollFD = -1; }
#endif
}

void ZiMultiplex::rx()
{
#ifdef ZiMultiplex_IOCP
  DWORD len;
  ULONG_PTR key;
  Zi_Overlapped *overlapped;
  ZeError e;

  for (;;) {
    ZiDEBUG(this, ([
      nThreads = params().nThreads(),
      nConnections = m_cxns->count_(),
      nListeners = m_listeners->count_()
    ](auto &s) {
      s << "wait() nThreads: " << ZuBoxed(nThreads).fmt<ZuFmt::Right<2>>()
	<< " nConnections: " <<
	  ZuBoxed(nConnections).fmt<ZuFmt::Right<4>>()
	<< " nListeners: " << ZuBoxed(nListeners).fmt<ZuFmt::Right<3>>();
    }));
    if (!GetQueuedCompletionStatus(
	  m_completionPort, &len, &key, (OVERLAPPED **)&overlapped, INFINITE)) {
      e = GetLastError();
      ZiDEBUG(this, ([
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
      overlapped->complete(Zi::IOError, 0, e);
    } else {
      ZiDEBUG(this, ([overlapped](auto &s) {
	s << "wait() overlapped: " <<
	  ZuBoxPtr(overlapped).hex<false, ZuFmt::Alt<>>();
      }));
      if (!overlapped) { // PostQueuedCompletionStatus() called
	ZiDEBUG(this, "wait() woken by PostQueuedCompletionStatus()");
	return;
      }
      overlapped->complete(Zi::OK, len, ZeOK);
    }
  }
#endif

#ifdef ZiMultiplex_EPoll
  bool wake = false;

  int r;
  ZeError e;
  auto ev = static_cast<epoll_event *>(
    ZuAlloca(m_epollQuantum * sizeof(epoll_event), alignof(epoll_event)));
  if (ZuUnlikely(!ev)) return;

  for (;;) {
    ZiDEBUG(this, ([
      nThreads = params().nThreads(),
      nConnections = m_cxns->count_(),
      epollFD = m_epollFD,
      wakeFD = m_wakeFD,
      wakeFD2 = m_wakeFD2,
      nListeners = m_listeners->count_()
    ](auto &s) {
      s << "wait() nThreads: " << ZuBoxed(nThreads).fmt<ZuFmt::Right<2>>()
	<< " nConnections: " <<
	  ZuBoxed(nConnections).fmt<ZuFmt::Right<4>>()
	<< " epollFD: " << ZuBoxed(epollFD).fmt<ZuFmt::Right<3>>()
	<< " wakeFD: " << ZuBoxed(wakeFD).fmt<ZuFmt::Right<3>>()
	<< " wakeFD2: " << ZuBoxed(wakeFD2).fmt<ZuFmt::Right<3>>()
	<< " nListeners: " << ZuBoxed(nListeners).fmt<ZuFmt::Right<3>>();
    }));

#if 0
#ifdef ZiMultiplex_DEBUG
    ZuTime now = Zm::now();
#endif
#endif

    r = epoll_wait(m_epollFD, ev, m_epollQuantum, -1);

#if 0
#ifdef ZiMultiplex_DEBUG
    now = Zm::now() - now;
    if (now.microsecs() > 1000) {
      ZiLOG(Info, "ZiMultiplex", ([usecs = now.microsecs()](auto &s) {
	s << "slow epoll_wait(): " << usecs << " us";
      }));
    }
#endif
#endif

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
	    if (ZuUnlikely(!cxn->recv())) continue;
	  if (events & EPOLLOUT)
	    txRun([cxn = ZmMkRef(cxn)]() { cxn->send(); });
	  continue;
	}

	if (ZuLikely(u64_is_event(v))) { wake = readWake(); continue; }

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
#endif
}

void ZiMultiplex::wake()
{
  ZiDEBUG(this, "wake");

#ifdef ZiMultiplex_IOCP
  PostQueuedCompletionStatus(m_completionPort, 0, 0, 0);
#endif

#ifdef ZiMultiplex_EPoll
  writeWake();
#endif
}

void ZiMultiplex::wakeRx()
{
  if (running()) push([this]() { rx(); }, rxThread());
  wake();
}

#ifdef ZiMultiplex_EPoll
bool ZiMultiplex::readWake()
{
  ZiDEBUG(this, ([wakeFD = m_wakeFD](auto &s) {
    s << "FD: " << ZuBoxed(wakeFD).fmt<ZuFmt::Right<3>>() << " readWake";
  }));

  char c;
  return ::read(m_wakeFD, &c, 1) >= 1;
}

void ZiMultiplex::writeWake()
{
  ZiDEBUG(this, ([wakeFD = m_wakeFD2](auto &s) {
    s << "FD: " << ZuBoxed(wakeFD).fmt<ZuFmt::Right<3>>() << " writeWake";
  }));

  char c = 0;
  while (::write(m_wakeFD2, &c, 1) < 0) {
    ZeError e{errno};
    if (e.errNo() != EINTR && e.errNo() != EAGAIN) {
      Error("write", Zi::IOError, e);
      break;
    }
  }
}
#endif

void ZiMultiplex::telemetry(Ztc::MxTelemetry &data) const
{
  data.id = params().id();
  data.stackSize = params().stackSize();
  data.queueSize = params().queueSize();
  data.spin = params().spin();
  data.timeout = params().timeout();
  data.rxBufSize = rxBufSize();
  data.txBufSize = txBufSize(),
  data.rxThread = rxThread();
  data.txThread = txThread();
  data.partition = params().partition();
  data.state = state();
  data.ll = params().ll();
  data.priority = params().priority();
  data.nThreads = params().nThreads();
}

unsigned Ztc::MxMgr::all(AllFn fn)
{
  return ZiMxMgr_::instance()->all(ZuMv(fn));
}

ZmRWLock &Ztc::MxMgr::watchLock_()
{
  return ZiMxMgr_::instance()->watchLock();
}

void Ztc::MxMgr::watch(AddFn addFn, DelFn delFn)
{
  ZiMxMgr_::instance()->watch(ZuMv(addFn), ZuMv(delFn));
}

void Ztc::MxMgr::unwatch()
{
  ZiMxMgr_::instance()->unwatch();
}
