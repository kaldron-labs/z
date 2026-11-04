//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// socket I/O multiplexing

#include <zlib/ZmAssert.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZmRWLock.hh>
#include <zlib/ZmSingleton.hh>
#include <zlib/ZmSpecific.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtScratch.hh>
#include <zlib/ZtHexDump.hh>
#include <zlib/ZmScratch.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiRxStream.hh>
#include <zlib/ZiAssert.hh>

ZtEnumImplNS(ZiCxnFlags);
ZtEnumImplNS(ZiCxnType);
ZtEnumImplNS(ZiMxBackend);
#ifdef ZiMultiplex_BENCH
namespace Zi {
  ZtEnumImplStruct(IOBenchKind);
  ZtEnumImplStruct(IOBenchFallback);
}
#endif

class ZiMxMgr_ {
friend Ztc::MxMgr;

  using Lock = ZmRWLock;
  using Guard = ZmGuard<Lock>;
  using ReadGuard = ZmReadGuard<Lock>;

  ZmRBTreeDerive(Map, ZiMultiplex *,
    ZmRBTreeUnique<true,
      ZmRBTreeLock<ZmNoLock,
	ZmRBTreeHeapID<"Ztc.MxMgr">>>);
  using Captures =
    ZtArray<Ztc::MxTelemetry,
      ZtArrayHeapID<"Ztc.MxMgr.Capture">>;

public:
  ~ZiMxMgr_() {
    m_addFn = {};
    m_delFn = {};
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

  void capture(Ztc::MxMgr::CaptureFn fn) const {
    ReadGuard guard(m_watchLock);
    unsigned count = m_map.count_();
    auto captures = ZmScratch(
      Ztc::MxTelemetry, count, Captures::VHeap);
    {
      auto i = m_map.citer();
      while (auto mx = i.key()) {
	auto data = new (captures.push()) Ztc::MxTelemetry;
	mx->telemetry(*data);
      }
    }
    guard.unlock();
    fn(captures.cspan());
  }

  void watch(Ztc::MxMgr::AddFn addFn, Ztc::MxMgr::DelFn delFn) {
    Guard guard(m_watchLock);
    ZmAssert(!m_addFn && !m_delFn, return);
    m_addFn = ZuMv(addFn);
    m_delFn = ZuMv(delFn);
  }

  void unwatch() {
    Guard guard(m_watchLock);
    m_addFn = {};
    m_delFn = {};
  }

private:
  Lock &watchLock() { return m_watchLock; }

  Map			m_map;
  mutable Lock		m_watchLock;
  Ztc::MxMgr::AddFn	m_addFn;
  Ztc::MxMgr::DelFn	m_delFn;
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

#ifdef ZiMultiplex_EPoll

#include <sys/resource.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <linux/unistd.h>
#include <linux/sockios.h>

#ifndef EPOLLRDHUP
#define EPOLLRDHUP 0
#endif

// 64-bit pointer-packing for epoll u64

static inline uint64_t u64_cxn(void *ptr) {
  return reinterpret_cast<uintptr_t>(ptr);
}
static inline bool u64_is_cxn(uint64_t v) { return !(v>>61); }
static inline uint64_t u64_listener(void *ptr) {
  return reinterpret_cast<uintptr_t>(ptr) | (uint64_t(1)<<61);
}
static inline bool u64_is_listener(uint64_t v) { return (v>>61) == 1; }
static inline uint64_t u64_connect(void *ptr) {
  return reinterpret_cast<uintptr_t>(ptr) | (uint64_t(2)<<61);
}
static inline bool u64_is_connect(uint64_t v) { return (v>>61) == 2; }
static constexpr uint64_t u64_event() { return uint64_t(3)<<61; }
static inline bool u64_is_event(uint64_t v) { return (v>>61) == 3; }
template <typename T>
inline T *u64_ptr(uint64_t v) {
  return reinterpret_cast<T *>(v & ~(uint64_t(7)<<61));
}

#endif /* ZiMultiplex_EPoll */

#define Log(severity, op, result, error) \
  ZiLOG(severity, "ZiMultiplex", \
    ([result_ = result, error_ = error](auto &s) { \
    s << op << ' ' << Zi::ioResult(result_) << ' ' << error_; \
  }))
#define Error(op, result, error) Log(Error, op, result, error)
#define Warning(op, result, error) Log(Warning, op, result, error)

static ZiIP wildcard(ZiIPType::T type)
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

static int family(ZiIPType::T type)
{
  switch (type) {
    case ZiIPType::V6:
      return AF_INET6;
    default:
      return AF_INET;
  }
}

static bool cxnType(ZiIP localIP, ZiIP remoteIP, ZiIPType::T &type)
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

static ZiTOS tosValue(unsigned v)
{
  return uint8_t(v);
}

#ifndef _WIN32
static bool recvTOS(Zi::Socket s, ZiIPType::T type)
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

static ZiTOS getTOS(const struct msghdr &msg)
{
  for (auto cmsg = CMSG_FIRSTHDR(&msg); cmsg;
      cmsg = CMSG_NXTHDR(const_cast<struct msghdr *>(&msg), cmsg)) {
    if (cmsg->cmsg_level == IPPROTO_IP && cmsg->cmsg_type == IP_TOS) {
      if (cmsg->cmsg_len >= CMSG_LEN(sizeof(int)))
	return tosValue(*reinterpret_cast<int *>(CMSG_DATA(cmsg)));
      if (cmsg->cmsg_len >= CMSG_LEN(sizeof(uint8_t)))
	return tosValue(*CMSG_DATA(cmsg));
    }
#ifdef IPV6_TCLASS
    if (cmsg->cmsg_level == IPPROTO_IPV6 &&
	cmsg->cmsg_type == IPV6_TCLASS &&
	cmsg->cmsg_len >= CMSG_LEN(sizeof(int)))
      return tosValue(*reinterpret_cast<int *>(CMSG_DATA(cmsg)));
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
#else
static bool recvTOS(Zi::Socket s, ZiIPType::T type)
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

static ZiTOS getTOS(WSAMSG &msg)
{
  for (auto cmsg = WSA_CMSG_FIRSTHDR(&msg); cmsg;
      cmsg = WSA_CMSG_NXTHDR(&msg, cmsg)) {
    if (cmsg->cmsg_level == IPPROTO_IP && cmsg->cmsg_type == IP_TOS &&
	cmsg->cmsg_len >= WSA_CMSG_LEN(sizeof(int)))
      return tosValue(*reinterpret_cast<int *>(WSA_CMSG_DATA(cmsg)));
#ifdef IPV6_TCLASS
    if (cmsg->cmsg_level == IPPROTO_IPV6 &&
	cmsg->cmsg_type == IPV6_TCLASS &&
	cmsg->cmsg_len >= WSA_CMSG_LEN(sizeof(int)))
      return tosValue(*reinterpret_cast<int *>(WSA_CMSG_DATA(cmsg)));
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
#endif

#ifdef ZiMultiplex_URing
namespace Zi {
struct URingAssoc_ { URing *io = nullptr; };
using URingAssocList = ZmList<URingAssoc_,
  ZmListNode<URingAssoc_, ZmListShadow<>>>;
using URingAssoc = URingAssocList::Node;

struct URingWork_ {
  URing *io = nullptr;
  bool queued = false;
};
using URingWorkList = ZmList<URingWork_,
  ZmListNode<URingWork_, ZmListShadow<>>>;
using URingWork = URingWorkList::Node;

// Persistent kernel requests retain their embedded controls until terminal CQEs.
struct URingCtl {
  enum { Watch, Return, Wake, EPoll, Cancel };
  int kind;
  URing *io = nullptr;
  io_uring_sqe sqe{};
  URingReq pending;
  uint64_t count = 0;
  bool active = false;
};

class URingLoop : public ZmNHeap<URingLoop> {
friend inline ZuStringT<"Zi.URingLoop"> ZmNHeapID(URingLoop *);
public:
  URingLoop(ZiMultiplex *mx, unsigned tid) : m_mx{mx}, m_tid{tid} { }
  ~URingLoop();
  bool init();
  void start(bool rx);
  void stop();
  void wake();
  void ready(URingWork *);
  void unready(URingWork *work) {
    if (!work->queued) return;
    m_ready.delNode(work);
    work->queued = false;
  }
  void submit(URingCtl &);
  void complete(URingCtl &, int, unsigned);
  IOBufRing *ring() { return &m_ring; }
  unsigned thread() const { return m_tid; }
  void attach(URingAssoc *assoc) { m_bound.pushNode(assoc); }
  void detach(URingAssoc *assoc) { m_bound.delNode(assoc); }
  void unsubmit(URingCtl &);
private:
  void start_(bool rx);
  void run();
  void read(URingCtl &, int);
  ZiMultiplex		*m_mx;
  unsigned		m_tid;
  int			m_wakeFD = -1;
  // Wake publications race only with idle entry; all CQ/SQ work is owner-local.
  alignas(Zm::CacheLineSize)
  ZmAtomic<unsigned>	m_work{0}; // bit 0: queued work, bit 1: entering idle wait
  alignas(Zm::CacheLineSize)
  IOBufRing		m_ring;
  URingWorkList		m_ready;
  URingCtl		m_wake{URingCtl::Wake};
  URingCtl		m_epoll{URingCtl::EPoll};
  URingCtl		m_cancel{URingCtl::Cancel};
  unsigned		m_controls = 0;
  URingAssocList	m_bound;
  bool			m_stopping = false;
};
} // Zi

// One service belongs to the allocator's kernel ring. Its owner batches SQEs
// and consumes the CQ directly; epoll never sees allocator ring descriptors.
class Zi::URing : public ZmNHeap<Zi::URing> {
friend inline ZuStringT<"Zi.URing"> ZmNHeapID(URing *);
friend ::ZiConnection;
friend URingLoop;
public:
  using Op = URingOp;

  URing(IOBufRing *ring, bool rx, bool udp) :
    m_ring{ring}, m_rx{rx}, m_udp{udp},
    m_limit{(ring->sq().mask + 1)>>1} {
    // Tx credits include retired data until its notification is safe. The
    // ordered callback retains the operation after returning that credit.
    // Half the SQ capacity leaves CQ space for data, notification and cancel.
    if (rx) { m_slots.length(ring->bufCount() + unsigned(udp)); replenish(); }
  }
  ~URing() {
    ZmAssert(!m_mx && !m_loop && !m_closing && !m_work.queued && !m_unbound);
    ZmAssert(!m_used && !m_controls && !m_pending.count_() && !m_waits.count_());
    ZmAssert(!m_watch.active && !m_return.active && !m_retire.active);
  }

  bool bind(ZiMultiplex *mx, URingLoop *loop) {
    if (m_closing) return false;
    if (m_mx) return m_mx == mx && m_loop == loop;
    ZmAssert(mx->invoked(loop->thread()));
    m_mx = mx;
    m_loop = loop;
    m_work.io = this;
    if (m_ring != loop->ring()) {
      m_assoc.io = this;
      loop->attach(&m_assoc);
      m_watch.io = this;
      m_watch.sqe.opcode = IORING_OP_POLL_ADD;
      m_watch.sqe.fd = m_ring->fd();
      m_watch.sqe.poll32_events = POLLIN;
      m_watch.sqe.len = IORING_POLL_ADD_MULTI;
      loop->submit(m_watch);
    }
    if (m_rx) {
      m_return.io = this;
      m_return.sqe.opcode = IORING_OP_READ;
      m_return.sqe.fd = m_ring->returnFD();
      m_return.sqe.addr = uintptr_t(&m_return.count);
      m_return.sqe.len = sizeof(m_return.count);
      loop->submit(m_return);
      replenish();
    }
    return true;
  }

  // The owner has quiesced producers and drained data/notification holds.
  // Completion runs on a later owner turn, after every embedded control retires.
  void unbind(ZmFn<> fn = {}) {
    if (!m_mx) { if (fn) fn(); return; }
    ZmAssert(!m_used && !m_controls && !m_pending.count_() && !m_waits.count_());
    ZmAssert(m_mx->invoked(m_loop->thread()));
    ZmAssert(!fn || !m_unbound);
    if (fn) m_unbound = ZuMv(fn);
    if (m_closing) return;
    m_closing = true;
    m_retire.io = this;
    schedule();
  }

  IOBufRing *ring() const { return m_ring; }
  bool closing() const { return m_closing; }
  ZmRef<IOBuf> rxBuf(unsigned id) {
    --m_provided;
    return ZuMv(m_slots[id]);
  }
  bool available() const { return m_rx ? m_provided != 0 : m_used < m_limit; }
  void queue(URingReq *req) { m_pending.pushNode(req); }
  void unqueue(URingReq *req) { m_pending.delNode(req); }
  bool pending() const { return m_pending.count_() != 0; }

  void submit(Op *op) {
    op->active(true);
    op->queued(true);
    op->cxn->uringHold();
    ++m_used;
    op->pending.op = op;
    m_pending.pushNode(&op->pending);
    schedule();
  }

  void cancel(Op *op) {
    if (op->queued()) { schedule(); return; }
    if (!op->active() || op->cancelling() || op->terminal()) return;
    op->cancelling(true);
    ++m_controls;
    op->cxn->uringHold();
    op->pending.op = op;
    m_pending.pushNode(&op->pending);
    schedule();
  }

  void wait(URingWait *op) {
    if (op->waiting) return;
    op->waiting = true;
    m_waits.pushNode(op);
    // Waiting for a slot/operation credit is part of the existing I/O drain.
    op->cxn->uringHold();
    if (m_rx) m_ring->wait();
  }

  void unwait(URingWait *op) {
    if (!op->waiting) return;
    m_waits.delNode(op);
    op->waiting = false;
    op->cxn->uringDone();
  }

  void release(Op *op) {
    op->active(false);
    --m_used;
    if (!m_servicing && !m_flushing && !m_closing) {
      resume();
      schedule();
    }
    if (op->rx()) op->cxn->uringDone();
  }

  void service(unsigned &quantum) {
    if (m_servicing || m_closing) return;
    m_servicing = true;
    auto &cq = m_ring->cq();
    unsigned head = cq.head->load_();
    while (!m_closing && quantum && head != unsigned(*cq.tail)) {
      --quantum;
      auto cqe = cq.cqes[head & cq.mask];
      *cq.head = ++head;
#ifdef ZiMultiplex_BENCH
      if (ZuUnlikely(m_mx && m_mx->benchFn())) observed(cqe);
#endif
      if (cqe.user_data & 2) {
	auto ctl = reinterpret_cast<URingCtl *>(cqe.user_data & ~uintptr_t(3));
	m_loop->complete(*ctl, cqe.res, cqe.flags);
	continue;
      }
      if (cqe.user_data & 1) {
	auto op = reinterpret_cast<Op *>(cqe.user_data & ~uintptr_t(1));
	auto cxn = op->cxn;
	ZmAssert(m_controls);
	--m_controls;
	op->cancelling(false);
	if (op->rx()) {
	  if (op->terminal()) release(op);
	} else cxn->uringTxDone();
	cxn->uringDone();
	continue;
      }
      auto op = reinterpret_cast<Op *>(cqe.user_data);
      if (op->rx()) op->cxn->uringReceived(op, cqe.res, cqe.flags);
      else op->cxn->uringSent(op, cqe.res, cqe.flags);
    }
    // Unbinding completes on a later owner turn, after this callback stack.
    // The allocator and its mappings remain live throughout service().
    if (!m_closing && m_rx) replenish();
    if (!m_closing) resume();
    if (!m_closing) flush();
    m_servicing = false;
    if (m_closing) return;
    if (head != unsigned(*cq.tail) || m_pending.count_()) schedule();
  }

private:
#ifdef ZiMultiplex_BENCH
  void observed(const io_uring_cqe &cqe) {
    // This is called only for actual kernel CQEs, never synthetic cancellation
    // callbacks for requests removed before submission. TxData/TxNotif plus
    // CQE (all non-Tx completions) form the total observed CQE count.
    if (cqe.user_data & 2) {
      auto ctl = reinterpret_cast<URingCtl *>(cqe.user_data & ~uintptr_t(3));
      m_mx->benchFn()(Zi::IOBenchEvent{.kind = IOBenchKind::CQE, .ring = m_ring,
	.op = cqe.user_data, .sid = m_loop->thread(), .result = cqe.res,
	.flags = cqe.flags, .opcode = ctl->sqe.opcode});
      return;
    }
    if (cqe.user_data & 1) {
      m_mx->benchFn()(Zi::IOBenchEvent{.kind = IOBenchKind::CQE, .ring = m_ring,
	.op = cqe.user_data, .sid = m_loop->thread(), .result = cqe.res,
	.flags = cqe.flags, .opcode = IORING_OP_ASYNC_CANCEL});
      return;
    }
    auto op = reinterpret_cast<Op *>(cqe.user_data);
    if (op->rx()) {
      m_mx->benchFn()(Zi::IOBenchEvent{.kind = IOBenchKind::CQE, .ring = m_ring,
#ifdef ZiMultiplex_DEBUG
	.cxn = op->cxn,
#endif
	.op = cqe.user_data, .sid = m_loop->thread(),
	.result = cqe.res, .flags = cqe.flags, .opcode = op->sqe.opcode});
      return;
    }
    auto buf = op->buf ? op->buf.ptr() : op->context.buf.ptr();
    bool fixed = (op->sqe.opcode == IORING_OP_SEND_ZC ||
      op->sqe.opcode == IORING_OP_SENDMSG_ZC) &&
	bool(op->sqe.ioprio & IORING_RECVSEND_FIXED_BUF);
    IOBenchFallback::T fallback = IOBenchFallback::None;
    if (op->sqe.opcode == IORING_OP_SEND || op->sqe.opcode == IORING_OP_SENDMSG) {
      if (!buf->uring()) fallback = IOBenchFallback::Unregistered;
      else if (buf->ring()->returnFD() >= 0) fallback = IOBenchFallback::RxForward;
      else fallback = IOBenchFallback::Empty;
    }
    m_mx->benchFn()(Zi::IOBenchEvent{
      .kind = IOBenchKind::T((cqe.flags & IORING_CQE_F_NOTIF) ?
	IOBenchKind::TxNotif : IOBenchKind::TxData),
#ifdef ZiMultiplex_DEBUG
      .buf = buf,
#endif
      .ring = m_ring,
#ifdef ZiMultiplex_DEBUG
      .cxn = op->cxn,
#endif
      .op = cqe.user_data,
      .sid = m_loop->thread(), .count = unsigned(op->vec.iov_len),
      .result = cqe.res, .flags = cqe.flags, .opcode = op->sqe.opcode,
      .fallback = fallback, .fixed = fixed});
  }
#endif

  bool put(const io_uring_sqe &sqe) {
    auto &sq = m_ring->sq();
    unsigned tail = sq.tail->load_();
    if (tail - unsigned(*sq.head) > sq.mask)
      return false;
    unsigned i = tail & sq.mask;
    sq.sqes[i] = sqe;
    sq.array[i] = i;
    *sq.tail = tail + 1;
#ifdef ZiMultiplex_BENCH
    if (ZuUnlikely(m_mx && m_mx->benchFn()))
      m_mx->benchFn()(Zi::IOBenchEvent{.kind = IOBenchKind::Prepared, .ring = m_ring,
	.op = sqe.user_data, .sid = m_loop->thread(), .count = 1,
	.flags = sqe.flags, .opcode = sqe.opcode,
	.fixed = (sqe.opcode == IORING_OP_SEND_ZC ||
	  sqe.opcode == IORING_OP_SENDMSG_ZC) &&
	    bool(sqe.ioprio & IORING_RECVSEND_FIXED_BUF)});
#endif
    return true;
  }

  void enter(bool wait = false) {
    auto &cq = m_ring->cq();
    if (wait && cq.head->load_() != unsigned(*cq.tail)) wait = false;
    auto &sq = m_ring->sq();
    unsigned n = sq.tail->load_() -
      unsigned(*sq.head);
    if (!wait && !n && !(unsigned(*sq.flags) &
	IORING_SQ_CQ_OVERFLOW)) return;
    int rc;
    do {
      rc = syscall(__NR_io_uring_enter, m_ring->fd(), n, unsigned(wait),
	IORING_ENTER_GETEVENTS, nullptr, size_t{0});
#ifdef ZiMultiplex_BENCH
      if (ZuUnlikely(m_mx && m_mx->benchFn())) {
	int error = rc < 0 ? errno : 0;
	m_mx->benchFn()(Zi::IOBenchEvent{.kind = IOBenchKind::Enter, .ring = m_ring,
	  .sid = m_loop->thread(), .count = n, .result = rc, .error = error,
	  .flags = IORING_ENTER_GETEVENTS, .wait = wait});
	if (rc < 0) errno = error;
      }
#endif
    } while (rc < 0 && errno == EINTR);
    if (rc < 0) {
      if (errno != EBUSY && errno != EAGAIN && errno != ENOMEM) {
	Error("io_uring_enter", Zi::IOError, ZeError{errno});
	// Invalid backend state cannot retire kernel-owned memory safely.
	ZmFail(io_uring_enter);
      }
      schedule();
      return;
    }
    if (unsigned(rc) < n) schedule();
  }

  void flush() {
    if (m_flushing || m_closing) return;
    m_flushing = true;
    auto &sq = m_ring->sq();
    unsigned quantum = m_mx->ioQuantum();
    while (!m_closing && quantum-- && m_pending.count_()) {
      auto p = m_pending.headPtr();
      if (p->sqe) {
	if (!put(*p->sqe)) break;
	m_pending.shift();
	// A non-null sentinel means this control has not reached the SQ yet.
	p->sqe = nullptr;
	continue;
      }
      auto op = static_cast<Op *>(p->op);
      // Leave one SQ entry for cancellation and other control operations.
      unsigned used = sq.tail->load_() -
	unsigned(*sq.head);
      if (used >= sq.mask + unsigned(!op->queued())) break;
      m_pending.shift();
      if (!op->queued()) {
	io_uring_sqe sqe{};
	sqe.opcode = IORING_OP_ASYNC_CANCEL;
	sqe.addr = uintptr_t(op);
	sqe.user_data = uintptr_t(op) | 1;
	put(sqe);
	continue;
      }
      if (op->cxn->ioUp(op->rx())) {
	put(op->sqe);
	op->queued(false);
      } else {
	op->queued(false);
	if (op->rx()) op->cxn->uringReceived(op, -ECANCELED, 0);
	else op->cxn->uringSent(op, -ECANCELED, 0);
      }
    }
    if (!m_closing && m_ring != m_loop->ring()) enter();
    m_flushing = false;
    if (m_closing) return;
    if (m_pending.count_() &&
	sq.tail->load_() -
	unsigned(*sq.head) < sq.mask) schedule();
  }

  void schedule() { m_loop->ready(&m_work); }

  void replenish() {
    auto ring = static_cast<IOBufRxRing *>(m_ring);
    unsigned mask = ring->ringSize() - 1;
    auto provided = ring->bufRing();
    unsigned tail = provided->tail;
    unsigned count = 0;
    while (m_provided + count < ring->ringSize() && m_ring->available()) {
      auto buf = m_ring->alloc();
      // Another allocating thread can claim the last slot after available().
      // Only mapped slots have an ID in this provided group.
      if (ZuUnlikely(!buf->uring())) break;
      unsigned id = ring->index(buf->data_());
      auto &entry = provided->bufs[(tail + count) & mask];
      entry.addr = uintptr_t(buf->data_());
      entry.len = buf->size;
      entry.bid = id;
      m_slots[id] = ZuMv(buf);
      ++count;
    }
    // Retained UDP packets may be waiting for a missing datagram. Keep one
    // receive available when they exhaust the mapped pool, using the
    // allocator's ordinary backing directly, without copying retained data.
    if (m_udp && !m_provided && !count) {
      auto buf = m_ring->alloc();
      unsigned id = ring->bufCount();
      auto &entry = provided->bufs[tail & mask];
      entry.addr = uintptr_t(buf->data_());
      entry.len = buf->size;
      entry.bid = id;
      m_slots[id] = ZuMv(buf);
      ++count;
    }
    if (count) {
      ZmAtomic_release();
      provided->tail = uint16_t(tail + count);
#ifdef ZiMultiplex_BENCH
      bool ready = !m_provided;
#endif
      m_provided += count;
#ifdef ZiMultiplex_BENCH
      if (ZuUnlikely(ready && m_mx && m_mx->benchFn()))
	m_mx->benchFn()(Zi::IOBenchEvent{.kind = IOBenchKind::RxReady, .ring = m_ring,
	  .sid = m_loop->thread(), .count = m_provided});
#endif
    }
  }

  void resume() {
    unsigned n = m_waits.count_(), quantum = m_mx->ioQuantum();
    if (n > quantum) n = quantum;
    while (!m_closing && n-- && (m_rx ? m_provided != 0 : m_used < m_limit)) {
      auto op = m_waits.shift();
      op->waiting = false;
      auto cxn = op->cxn;
      // The waiting hold protects the connection until the resumed request
      // takes over. Do not release it across a synchronous callback.
      if (m_rx) cxn->uringRecv();
      else cxn->send();
      cxn->uringDone();
    }
    if (m_closing || !m_waits.count_()) return;
    if (available()) schedule();
    else if (m_rx) m_ring->wait();
  }

  void detach_() {
    ZmAssert(!m_used && !m_controls && !m_pending.count_() && !m_waits.count_());
    ZmAssert(!m_watch.active && !m_return.active && !m_retire.active);
    m_loop->unready(&m_work);
    if (m_ring != m_loop->ring()) m_loop->detach(&m_assoc);
    m_mx = nullptr;
    m_loop = nullptr;
    m_closing = false;
  }

  void unbindStep() {
    // A completion may release the allocator, so leave its service/flush stack
    // before detaching and never touch the service after invoking completion.
    if (m_servicing || m_flushing) { schedule(); return; }
    for (auto ctl : {&m_watch, &m_return})
      if (ctl->active && ctl->pending.sqe) m_loop->unsubmit(*ctl);
    if (m_retire.active) return;
    auto target = m_watch.active ? &m_watch : m_return.active ? &m_return : nullptr;
    if (target) {
      m_retire.sqe = {};
      m_retire.sqe.opcode = IORING_OP_ASYNC_CANCEL;
      m_retire.sqe.addr = uintptr_t(target) | 2;
      m_loop->submit(m_retire);
      return;
    }
    auto fn = ZuMv(m_unbound);
    detach_();
    if (fn) fn();
  }

  using Slots = ZtArray<ZmRef<IOBuf>, ZtArrayHeapID<"Zi.URingSlots">>;

  IOBufRing		*m_ring;
  ZiMultiplex		*m_mx = nullptr;
  bool			m_rx;
  bool			m_udp;
  bool			m_servicing = false;
  bool			m_flushing = false;
  bool			m_closing = false;
  URingLoop		*m_loop = nullptr;
  URingWork		m_work;
  URingAssoc		m_assoc;
  ZmFn<>		m_unbound;
  URingCtl		m_watch{URingCtl::Watch};
  URingCtl		m_return{URingCtl::Return};
  URingCtl		m_retire{URingCtl::Cancel};
  unsigned		m_limit;
  unsigned		m_used = 0;
  unsigned		m_controls = 0;
  unsigned		m_provided = 0;
  Slots			m_slots;
  URingReqList		m_pending;
  URingWaitList		m_waits;
};

Zi::URingLoop::~URingLoop()
{
  ZmAssert(!m_bound.count_() && !m_ready.count_() && !m_controls);
  if (m_wakeFD >= 0) ::close(m_wakeFD);
}

bool Zi::URingLoop::init()
{
  unsigned size = m_tid == m_mx->rxThread() ? m_mx->rxRingSize() : 0;
  if (m_tid == m_mx->txThread() && size < m_mx->txRingSize())
    size = m_mx->txRingSize();
  if (!m_ring.init(size)) return false;
  m_wakeFD = eventfd(0, EFD_NONBLOCK);
  if (m_wakeFD < 0) return false;
  auto io = new URing{&m_ring, false, false};
  m_ring.io(io, [](URing *p) { delete p; });
  return true;
}

void Zi::URingLoop::read(URingCtl &ctl, int fd)
{
  ctl.sqe.opcode = IORING_OP_READ;
  ctl.sqe.fd = fd;
  ctl.sqe.addr = uintptr_t(&ctl.count);
  ctl.sqe.len = sizeof(ctl.count);
  submit(ctl);
}

void Zi::URingLoop::start(bool rx)
{
  m_mx->run([this, rx]() { start_(rx); }, m_tid);
}

void Zi::URingLoop::start_(bool rx)
{
  m_ring.io()->bind(m_mx, this);
  read(m_wake, m_wakeFD);
  if (rx) {
    m_epoll.sqe.opcode = IORING_OP_POLL_ADD;
    m_epoll.sqe.fd = m_mx->controlFD();
    m_epoll.sqe.poll32_events = POLLIN;
    m_epoll.sqe.len = IORING_POLL_ADD_MULTI;
    submit(m_epoll);
  }
  m_mx->wakeFn(m_tid, ZiMultiplex::WakeFn{this,
    [](URingLoop *loop) { loop->wake(); }});
  m_mx->push([this]() { run(); }, m_tid);
}

void Zi::URingLoop::wake()
{
  // Queue publication precedes this exchange. Only the idle transition needs
  // an eventfd write; same-owner posts and bursts share the pending-work bit.
  if (m_work.xchOr(1) != 2) return;
  uint64_t n = 1;
  while (::write(m_wakeFD, &n, sizeof(n)) < 0 && errno == EINTR);
}

void Zi::URingLoop::ready(URingWork *work)
{
  if (work->io->ring() == &m_ring || work->queued) return;
  work->queued = true;
  m_ready.pushNode(work);
}

void Zi::URingLoop::unsubmit(URingCtl &ctl)
{
  ZmAssert(ctl.active && ctl.pending.sqe);
  m_ring.io()->unqueue(&ctl.pending);
  ctl.pending.sqe = nullptr;
  ctl.active = false;
  --m_controls;
}

void Zi::URingLoop::submit(URingCtl &ctl)
{
  ctl.active = true;
  ++m_controls;
  ctl.sqe.user_data = uintptr_t(&ctl) | 2;
  ctl.pending.sqe = &ctl.sqe;
  m_ring.io()->queue(&ctl.pending);
}

void Zi::URingLoop::complete(URingCtl &ctl, int res, unsigned flags)
{
  bool more = flags & IORING_CQE_F_MORE;
  if (!more) { ctl.active = false; --m_controls; }
  if (ctl.io && ctl.io->closing()) {
    // Canceled control CQEs and already-queued successful/MORE CQEs all retain
    // the embedded ctl. Only the original terminal CQE clears its active bit.
    // Cancel ENOENT/EALREADY races are retried while that target remains active.
    if (ctl.kind == URingCtl::Cancel && res < 0 &&
	res != -ENOENT && res != -EALREADY && res != -ECANCELED) {
      Error("io_uring retire", Zi::IOError, ZeError{-res});
      ZmFail(io_uring_retire);
    }
    ctl.io->schedule();
    return;
  }
  if (m_stopping) return;
  if (res < 0) {
    Error("io_uring control", Zi::IOError, ZeError{-res});
    ZmFail(io_uring_control);
  }
  switch (ctl.kind) {
    case URingCtl::Watch:
    case URingCtl::Return:
      ctl.io->schedule();
      break;
    case URingCtl::EPoll:
      m_mx->rx(); // nonblocking connection control only
      break;
    case URingCtl::Wake:
    case URingCtl::Cancel:
      break;
  }
  if (!more) submit(ctl);
}

void Zi::URingLoop::stop()
{
  if (m_stopping) return;
  m_stopping = true;
  // Connection data and notification holds have already retired. Enumerate
  // idle bindings too; every association must detach before the loop stops.
  {
    auto i = m_bound.iter();
    while (auto assoc = i()) assoc->io->unbind();
  }
  // Cancel persistent watches/reads and consume their terminal CQEs here.
  m_cancel.sqe.opcode = IORING_OP_ASYNC_CANCEL;
  m_cancel.sqe.cancel_flags = IORING_ASYNC_CANCEL_ANY | IORING_ASYNC_CANCEL_ALL;
  submit(m_cancel);
}

void Zi::URingLoop::run()
{
  auto io = m_ring.io();
  unsigned quantum = m_mx->epollQuantum();
  io->service(quantum);
  unsigned n = m_ready.count_();
  while (n-- && quantum && m_ready.count_()) {
    auto work = m_ready.shift();
    work->queued = false;
    auto service = work->io;
    if (service->closing()) service->unbindStep();
    else service->service(quantum);
  }
  io->flush(); // includes requests produced by this turn's callbacks
  if (m_stopping && !m_controls && !m_ready.count_() && !m_bound.count_()) {
    io->detach_();
    m_mx->rxRun([mx = m_mx]() { mx->uringStopped(); });
    return;
  }
  // Stop cancels the wake read too; continue consuming cancellation CQEs.
  bool wait = !m_stopping && quantum && !m_ready.count_() && !io->pending() &&
    !m_work.xch(0) && m_work.cmpXch(2, 0) == 0;
  io->enter(wait);
  if (wait) m_work.cmpXch(0, 2);
  m_mx->push([this]() { run(); }, m_tid);
}

void ZiMultiplex::uringStopped()
{
  if (--m_uringStopping) {
    txRun([this]() { m_txLoop->stop(); });
    return;
  }
  m_stopping->post();
}

Zi::URing *ZiMultiplex::uring(Zi::IOBufRing *ring, bool rx, bool udp)
{
  if (!ring || ring->fd() < 0) return nullptr;
  auto io = ring->io();
  if (!io) {
    io = new Zi::URing{ring, rx, udp};
    ring->io(io, [](Zi::URing *p) { delete p; });
  }
  return io->bind(this, rx ? m_rxLoop : m_txLoop) ? io : nullptr;
}

Zi::IOBufRing *ZiMultiplex::txRing()
{
  return m_txLoop->ring();
}

void ZiConnection::uringDone(unsigned n)
{
  if (m_uringPending -= n) return;
  if (m_rxUp.load_() || m_txUp.load_()) return;
  m_mx->rxRun([cxn = ZmRef(this)]() {
    if (cxn->m_discPending && !unsigned(cxn->m_uringPending))
      cxn->notifyDisconnected(cxn->m_uringPeer);
  });
}

void ZiConnection::uringRecv()
{
  ZmAssert(m_mx->invoked(m_mx->rxThread()));
  auto &op = m_rxOp;
  if (!m_rxUp.load_() || m_rxContext.completed() || op.active() || op.waiter.waiting)
    return;
  bool udp = m_info.options.udp();
  auto allocator = m_mx->rxAllocator(udp);
  auto io = m_mx->uring(allocator->ring(), true, udp);
  if (!io) { errorRecv(Zi::IOError, ZeError{EINVAL}); return; }
  op.cxn = this;
  op.ring = io;
  op.rx(true);
  if (!io->available()) {
#ifdef ZiMultiplex_BENCH
    if (ZuUnlikely(m_mx->benchFn()))
      m_mx->benchFn()(Zi::IOBenchEvent{.kind = Zi::IOBenchKind::RxWait, .ring = allocator->ring(),
#ifdef ZiMultiplex_DEBUG
	.cxn = this,
#endif
	.op = uintptr_t(&op), .sid = m_mx->rxThread()});
#endif
    op.waiter.cxn = this;
    op.waiter.ring = io;
    io->wait(&op.waiter);
    return;
  }
  op.sqe = {};
  op.terminal(false);
  op.sqe.opcode = udp ? IORING_OP_RECVMSG : IORING_OP_RECV;
  op.sqe.fd = m_info.socket;
  op.sqe.flags = IOSQE_BUFFER_SELECT;
  op.sqe.ioprio = IORING_RECV_MULTISHOT;
  op.sqe.user_data = uintptr_t(&op);
  if (udp) {
    op.msg = {};
    op.msg.msg_namelen = Zi::URingOp::NameSize;
    op.msg.msg_controllen = Zi::URingOp::CtrlSize;
    op.sqe.addr = uintptr_t(&op.msg);
    op.sqe.len = 1;
  }
  io->submit(&op);
}

void ZiConnection::uringReceived(Zi::URingOp *op, int res, unsigned flags)
{
  ZmAssert(m_mx->invoked(m_mx->rxThread()));
  auto io = op->ring;
  bool udp = m_info.options.udp();
  bool more = flags & IORING_CQE_F_MORE;
  bool selected = flags & IORING_CQE_F_BUFFER;
  ZmRef<Zi::IOBuf> buf;
  if (selected) {
    unsigned id = flags>>IORING_CQE_BUFFER_SHIFT;
    buf = io->rxBuf(id);
#ifdef ZiMultiplex_BENCH
    if (ZuUnlikely(!io->available() && m_mx && m_mx->benchFn()))
      m_mx->benchFn()(Zi::IOBenchEvent{
	.kind = Zi::IOBenchKind::RxZero,
#ifdef ZiMultiplex_DEBUG
	.buf = buf.ptr(),
#endif
	.ring = io->ring(),
#ifdef ZiMultiplex_DEBUG
	.cxn = this,
#endif
	.op = uintptr_t(op), .sid = m_mx->rxThread(),
	.result = res, .flags = flags});
#endif
  }
  if (!res && !udp && m_rxUp.load_() &&
      !m_rxContext.completed()) {
    m_rxContext.complete();
    disconnect(true);
  }
  if (res >= 0 && selected && m_rxUp.load_() &&
      !m_rxContext.completed()) {
    auto &ctx = m_rxContext;
    ctx.buf = ZuMv(buf);
    if (udp) {
      auto out = ctx.buf->ptr<io_uring_recvmsg_out>();
      auto name = ctx.buf->data() + sizeof(*out);
      // Populate the zero-offset sockaddr storage before deriving its union tag.
      memcpy(static_cast<void *>(&ctx.addr), name, out->namelen);
      ctx.addr.sync();
      msghdr msg{};
      msg.msg_control = name + op->msg.msg_namelen;
      msg.msg_controllen = out->controllen < op->msg.msg_controllen ?
	out->controllen : op->msg.msg_controllen;
      ctx.tos = getTOS(msg);
      ctx.flags = out->flags;
      ctx.buf->skip = sizeof(*out) + op->msg.msg_namelen +
	op->msg.msg_controllen;
      res -= ctx.buf->skip;
    }
    ctx.buf->length = 0;
    if (res || udp) executedRecv(res);
    else disconnect(true);
    // A handler may move out the buffer. Drop any unclaimed reference before
    // replenishing; the multishot operation owns its callback independently.
    ctx.buf = nullptr;
    if (ctx.disconnected()) disconnect(false);
  } else if (res < 0 && res != -ECANCELED && res != -ENOBUFS &&
      !(udp && (res == -ZiECONNREFUSED || res == -ZiECONNRESET)) &&
      m_rxUp.load_()) {
    errorRecv(Zi::IOError, ZeError{-res});
  }
  if (more) {
    if (!m_rxUp.load_() || m_rxContext.completed()) io->cancel(op);
    return;
  }
  op->terminal(true);
  if (op->cancelling()) return;
  io->release(op);
  if (!io->closing() && m_rxUp.load_() && !m_rxContext.completed()) {
    io->replenish();
    if (!io->closing()) uringRecv();
  }
}

void ZiConnection::uringSend()
{
  ZmAssert(m_mx->invoked(m_mx->txThread()));
  if (m_txOp || m_txWait.waiting) return;
  auto &ctx = m_txContext;
  ctx.txSelect();
  auto ring = m_mx->txRing();
  bool fixed = false;
  if (ctx.buf->uring()) {
    auto backing = ctx.buf->ring();
    // Provided Rx storage forwarded to Tx is still the original allocation,
    // but it is not in a fixed-buffer table. Submit an ordinary send.
    if (backing->returnFD() < 0) {
      ring = backing;
      // A zero-length fixed import is rejected by the kernel. Empty UDP is
      // still a datagram: submit it normally on this same ring/backing.
      fixed = ctx.txRemaining;
    }
  }
  bool udp = m_info.options.udp();
  auto io = m_mx->uring(ring, false, udp);
  if (!io) { errorSend(Zi::IOError, ZeError{EINVAL}); return; }
  if (!io->available()) {
    m_txWait.cxn = this;
    m_txWait.ring = io;
    io->wait(&m_txWait);
    return;
  }
  auto op = new Zi::URingOp;
  op->cxn = this;
  op->ring = io;
  op->buf = ctx.buf;
  op->sqe.fd = m_info.socket;
  op->sqe.user_data = uintptr_t(op);
  op->sqe.msg_flags = MSG_NOSIGNAL;
  op->vec = {ctx.txData(), ctx.txRemaining};
  if (udp) {
    op->sqe.opcode = fixed ? IORING_OP_SENDMSG_ZC : IORING_OP_SENDMSG;
    op->addr = ctx.addr;
    if (!!op->addr) {
      op->msg.msg_name = op->addr.sa();
      op->msg.msg_namelen = op->addr.len();
    }
    op->msg.msg_iov = &op->vec;
    op->msg.msg_iovlen = 1;
    ZiIPType::T type;
    if (!cxnType(m_info.localIP, m_info.remoteIP, type)) type = ZiIPType::V4;
    putTOS(op->msg, op->control, sizeof(op->control), ctx.tos,
	 type == ZiIPType::V6 ? IPPROTO_IPV6 : IPPROTO_IP,
	 type == ZiIPType::V6 ? IPV6_TCLASS : IP_TOS);
    op->sqe.addr = uintptr_t(&op->msg);
    op->sqe.len = 1;
  } else {
    op->sqe.opcode = fixed ? IORING_OP_SEND_ZC : IORING_OP_SEND;
    op->sqe.addr = uintptr_t(ctx.txData());
    op->sqe.len = ctx.txRemaining;
  }
  if (fixed) {
    op->sqe.ioprio = IORING_RECVSEND_FIXED_BUF;
    op->sqe.buf_index = 0;
  }
#ifdef ZiMultiplex_FILTER
  if (ZuUnlikely(m_mx->filterTx(this,
	static_cast<uint8_t *>(op->vec.iov_base), op->vec.iov_len))) {
    op->filtered(true);
    op->sqe = {};
    op->sqe.opcode = IORING_OP_NOP;
    op->sqe.user_data = uintptr_t(op);
  }
#endif
  m_txOps.pushNode(op);
  m_txOp = op;
  io->submit(op);
}

void ZiConnection::uringSent(Zi::URingOp *op, int res, unsigned flags)
{
  ZmAssert(m_mx->invoked(m_mx->txThread()));
  auto io = op->ring;
  if (flags & IORING_CQE_F_NOTIF) {
    op->safe(true);
    uringTxDone();
    return;
  }
  if (res >= 0 && op->filtered()) res = op->vec.iov_len;
  op->dataDone(true);
  op->safe(!(flags & IORING_CQE_F_MORE));
  op->result = res;
  m_txOp = nullptr;
  auto &ctx = m_txContext;
  if (res >= 0) {
    ++m_txCalls;
    m_txBytes += res;
  }
  if (!m_txUp.load_() || ctx.completed()) {
    uringTxDone();
    return;
  }
  if (res >= 0) {
    ctx.txAdvance(unsigned(res));
    ctx.length = res;
    if (m_info.options.udp() && unsigned(res) != op->vec.iov_len)
      op->result = -EIO;
    op->last(!ctx.txRemaining || op->result < 0);
  } else op->last(true);
  if (op->last()) {
    // A null buffer requests the next pending send. The original buffer-bearing
    // callback is delivered once, below, after all preceding holds retire.
    op->context = ZuMv(ctx);
    op->buf = nullptr;
    ctx = {};
    ctx.cxn = this;
    ctx.fn = op->context.fn;
    bool udpError = m_info.options.udp() &&
      (op->result == -ZiECONNREFUSED || op->result == -ZiECONNRESET);
    if (op->result >= 0 || udpError) {
      op->context.flags |= ZiTxContext::TxDataDone;
      ctx.length = op->result < 0 ? -1 : op->result;
      while (!ctx());
    }
    else {
      ctx.complete();
      close_1(op->result == -ZiENOTCONN || op->result == -ZiECONNRESET);
    }
  }
  if (!io->closing() && m_txUp.load_() && !ctx.completed() && ctx.buf) send();
  uringTxDone();
}

void ZiConnection::uringTxDone()
{
  ZmAssert(m_mx->invoked(m_mx->txThread()));
  unsigned retired = 0;
  unsigned quantum = m_mx->ioQuantum();
  while (quantum--) {
    auto op = m_txOps.headPtr();
    if (!op) break;
    if (!op->dataDone() || !op->safe() || op->cancelling()) break;
    auto held = m_txOps.shift();
    // Kernel ownership is already safe. Return its allocator credit before the
    // terminal callback; retain the connection hold through that callback.
    op->ring->release(op);
    if (op->last()) {
      auto &ctx = op->context;
      ctx.length = op->result < 0 ? -1 : op->result;
      // The operation retains its terminal callback through safe retirement,
      // including after physical Tx closes.
      if (ctx.fn) while (!ctx());
      if (m_txUp.load_() && op->result < 0 && op->result != -ECANCELED &&
	  !(m_info.options.udp() && (op->result == -ZiECONNREFUSED ||
	    op->result == -ZiECONNRESET)))
	close_1(op->result == -ENOTCONN || op->result == -ECONNRESET);
      else if (ctx.disconnected()) disconnect(false);
    }
    ++retired;
  }
  if (auto op = m_txOps.headPtr())
    if (op->dataDone() && op->safe() && !op->cancelling())
      m_mx->txRun([cxn = ZmRef(this)]() { cxn->uringTxDone(); });
  synced();
  // Rx may remove the final connection reference as soon as this reaches zero.
  // Keep the actual completion holds through all local callbacks/queue access.
  if (retired) uringDone(retired);
}
#endif

void ZiMultiplex::unbind(Zi::IOBufAllocator *allocator, ZmFn<> fn)
{
  rxRun([
    this,
#ifdef ZiMultiplex_URing
    allocator,
#endif
    fn = ZuMv(fn)
  ]() mutable {
#ifdef ZiMultiplex_URing
    auto ring = allocator->ring();
    if (m_backend == ZiMxBackend::URing && ring && ring->returnFD() >= 0) {
      ZmFn<> done{[this, fn = ZuMv(fn)]() mutable {
	txRun([fn = ZuMv(fn)]() mutable { if (fn) fn(); });
      }};
      if (auto io = ring->io()) io->unbind(ZuMv(done));
      else done();
      return;
    }
#endif
    txRun([
#ifdef ZiMultiplex_URing
      allocator,
#endif
      fn = ZuMv(fn)
    ]() mutable {
#ifdef ZiMultiplex_URing
      if (auto ring = allocator->ring()) {
	if (auto io = ring->io()) { io->unbind(ZuMv(fn)); return; }
      }
#endif
      if (fn) fn();
    });
  });
}

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

void ZiConnection::closeSocket_()
{
  if (Zi::nullSocket(m_info.socket)) return;
  Zi::closeSocket(m_info.socket);
  m_info.socket = Zi::nullSocket();
}

ZiConnection::~ZiConnection()
{
  ZmAssert(Zi::nullSocket(m_info.socket));
  ZmAssert(!m_syncs.count_());
#ifdef ZiMultiplex_URing
  ZmAssert(!unsigned(m_uringPending) && !m_txOps.count_());
#endif
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

  rxInvoke([this, fn = ZuMv(fn), failFn = ZuMv(failFn),
      localIP, localPort, remoteIP, remotePort,
      options = ZuMv(options)]() mutable {
    udp_(ZuMv(fn), ZuMv(failFn),
	localIP, localPort, remoteIP, remotePort, ZuMv(options));
  });
}

void ZiMultiplex::udp_(ZiConnectFn fn, ZiFailFn failFn,
    ZiIP localIP, uint16_t localPort,
    ZiIP remoteIP, uint16_t remotePort,
    ZiCxnOptions options)
{
  ZiAssert(invoked(rxThread()), "ZiMultiplex", (), "Rx owner", return);
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

  Socket s;
  ZiIPType::T ipType;
  if (!cxnType(localIP, remoteIP, ipType)) {
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
    if (!localIP) localIP = wildcard(ipType);
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
    if (!localIP) localIP = wildcard(ipType);
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

  rxInvoke([this, fn = ZuMv(fn), failFn = ZuMv(failFn),
      localIP, localPort, remoteIP, remotePort,
      options = ZuMv(options)]() mutable {
    connect_(ZuMv(fn), ZuMv(failFn),
	localIP, localPort, remoteIP, remotePort, ZuMv(options));
  });
}

void ZiMultiplex::connect_(ZiConnectFn fn, ZiFailFn failFn,
    ZiIP localIP, uint16_t localPort,
    ZiIP remoteIP, uint16_t remotePort, ZiCxnOptions options)
{
  ZiAssert(invoked(rxThread()), "ZiMultiplex", (), "Rx owner", return);
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

  Socket s;
  ZiIPType::T ipType = ZiIPType::V4;

  if (!remoteIP || !cxnType(localIP, remoteIP, ipType)) {
    Error("connect", Zi::IOError, ZeError(ZiEINVAL));
    failFn(false);
    return;
  }
  s = ::socket(family(ipType), SOCK_STREAM, IPPROTO_TCP);
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
#ifndef _WIN32
    if (!localIP) localIP = wildcard(ipType);
    ZiSockAddr local(localIP, localPort);
    if (bind(s, local.sa(), local.len()) < 0) {
      ZeError e{errno};
      ::close(s);
      Warning("bind", Zi::IOError, e);
      failFn(true);
      return;
    }
#else
    if (!localIP) localIP = wildcard(ipType);
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

  if (!connectAdd(request, s)) {
    Zi::closeSocket(s);
    request->fail(false);
    return;
  }

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

  ZiConnectFn fn = request->takeFn();
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

  completedConnect(request);
}
#endif

void ZiMultiplex::executedConnect(ZiConnectFn fn, const ZiCxnInfo &ci)
{
  ZmRef<ZiConnection> cxn = fn(ci);

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

Ztc::Connection::TelKey ZiConnection::telKey() const
{
  return {
    m_mx->id(),
    m_info.remoteIP, m_info.remotePort,
    m_info.localIP, m_info.localPort
  };
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
  // Retain registry ownership while callbacks may release the multiplexer.
  ZmRef<CxnHash> cxns = m_cxns;
  if (!cxns) return 0;
  unsigned n = cxns->count_();
  using CxnRefs =
    ZtArray<ZmRef<ZiConnection>, ZtArrayHeapID<"Zi.CxnRefs">>;
  // Membership may grow between count_() and the slot-locked traversal.
  auto refs = ZtScratch(CxnRefs, n);
  {
    auto i = cxns->citer();
    while (ZmRef<ZiConnection> cxn = i.val())
      refs.push(ZuMv(cxn));
  }
  refs.all([&fn](ZmRef<ZiConnection> &cxn) { fn(cxn.ptr()); });
  return refs.length();
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

  rxInvoke([this, listenFn = ZuMv(listenFn),
      failFn = ZuMv(failFn), acceptFn = ZuMv(acceptFn),
      localIP, localPort, nAccepts, options = ZuMv(options)]() mutable {
    listen_(ZuMv(listenFn), ZuMv(failFn), ZuMv(acceptFn),
	localIP, localPort, nAccepts, ZuMv(options));
  });
}

void ZiMultiplex::listen_(
    ZiListenFn listenFn, ZiFailFn failFn, ZiConnectFn acceptFn,
    ZiIP localIP, uint16_t localPort, unsigned nAccepts, ZiCxnOptions options)
{
  ZiAssert(invoked(rxThread()), "ZiMultiplex", (), "Rx owner", return);
  if (ZuUnlikely(!running())) {
    Error("listen", Zi::NotReady, ZeOK);
    failFn(false);
    return;
  }

  Socket lsocket;
  ZiIPType::T ipType = !!localIP ? localIP.type() : ZiIPType::V4;
  if (!localIP) localIP = wildcard(ipType);

#ifndef _WIN32

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
    if (bind(lsocket, local.sa(), local.len())) {
      ZeError e(WSAGetLastError());
      ::closesocket(lsocket);
      Warning("bind", Zi::IOError, e);
      failFn(true);
      return;
    }
  }

#endif /* !_WIN32 */

  if (!localPort) {
    ZiSockAddr local;
    local.init(ipType);
#ifdef _WIN32
    int len = local.len();
    if (getsockname(lsocket, local.sa(), &len)) {
      ZeError e(WSAGetLastError());
      ::closesocket(lsocket);
#else
    socklen_t len = local.len();
    if (getsockname(lsocket, local.sa(), &len) < 0) {
      ZeError e{errno};
      ::close(lsocket);
#endif
      Error("getsockname", Zi::IOError, e);
      failFn(false);
      return;
    }
    local.sync();
    localPort = local.port();
  }

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
  ZmRef<Listener> hold{listener};
  unsigned quantum = ioQuantum();
  int s;
  ZeError e;
  ZiSockAddr remote;

retry:
  if (!listener->up()) return;
  if (!quantum--) {
    rxRun([this, listener = ZuMv(hold)]() { accept(listener); });
    return;
  }
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
    sockaddr *localAddr, *remoteAddr;
    int localLen, remoteLen;
    GetAcceptExSockaddrs(
	request->buf(), 0, ZiSockAddr::MaxLen + 16,
	ZiSockAddr::MaxLen + 16, &localAddr, &localLen, &remoteAddr, &remoteLen);
    ZiSockAddr local, remote;
    memcpy(static_cast<void *>(&local), localAddr, localLen);
    memcpy(static_cast<void *>(&remote), remoteAddr, remoteLen);
    local.sync();
    remote.sync();
    ci.localIP = local.ip();
    ci.localPort = local.port();
    ci.remoteIP = remote.ip();
    ci.remotePort = remote.port();
  }

  executedConnect(listener->acceptFn(), ci);
}
#endif

void ZiConnection::connected()
{
  connected(m_rxContext);

#ifdef ZiMultiplex_URing
  if (m_mx->backend() == ZiMxBackend::URing) {
    uringRecv();
    return;
  }
#endif

#ifdef ZiMultiplex_IOCP
  if (ZuLikely(!m_rxContext.completed()))
    recv();
#endif

#ifdef ZiMultiplex_EPoll
  if (ZuUnlikely(m_rxContext.completed()))
    m_mx->epollRecv(this, m_info.socket, 0);
#endif
}

void ZiConnection::recv(ZiRxFn fn)
{
  m_mx->rxInvoke(this, [this, fn = ZuMv(fn)]() mutable {
    recv_(ZuMv(fn));
    return this;
  });
}

void ZiConnection::recv_(ZiRxFn fn)
{
  ZiAssert(m_mx->invoked(m_mx->rxThread()), "ZiMultiplex", (), "Rx owner", return);
  m_rxContext.init(ZuMv(fn));

#ifdef ZiMultiplex_URing
  if (m_mx->backend() == ZiMxBackend::URing) {
    uringRecv();
    return;
  }
#endif

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
  if (m_rxPending) return;
#endif

  if (!m_rxContext.buf)
    m_rxContext.buf = m_mx->rxAllocator(m_info.options.udp())->alloc();

#ifdef ZiMultiplex_IOCP
  unsigned len = m_rxContext.buf->avail();

  WSABUF wsaBuf;
  wsaBuf.buf = m_rxContext.buf->ptr<char>() + m_rxContext.buf->length;
  wsaBuf.len = len;

  using Executed = Zi_Overlapped::Executed;
  Zi_Overlapped &overlapped = m_rxOverlapped;

#ifdef ZiMultiplex_DEBUG
  if (ZuUnlikely(!m_info.options.udp() && m_mx->frag())) {
    unsigned l = ((m_rxContext.buf->length + 8)>>1) + 1;
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
	size = m_rxContext.buf->size,
	offset = m_rxContext.buf->length,
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
  m_rxPending = ZuMv(m_rxContext.buf);
  ZeError e;
  DWORD n;
  if (m_info.options.udp()) {
    ZiIPType::T type;
    if (!cxnType(m_info.localIP, m_info.remoteIP, type))
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
  m_rxPending = nullptr;
  overlapped.clear();
  errorRecv(Zi::IOError, e);
  m_rxContext.complete();
  return;
#endif

#ifdef ZiMultiplex_EPoll
  if (ZuUnlikely(m_rxContext.completed())) {
    m_mx->epollRecv(this, m_info.socket, 0);
    return true;
  }

  unsigned quantum = m_mx->ioQuantum();
  unsigned len = m_rxContext.buf->avail();
  auto buf = m_rxContext.buf->end();

  ZeError e;
  int n;

#ifdef ZiMultiplex_DEBUG
  if (ZuUnlikely(!m_info.options.udp() && m_mx->frag())) {
    unsigned l = ((m_rxContext.buf->length + 8)>>1) + 1;
    if (len > l) len = l;
  }
  if (ZuUnlikely(m_mx->debug())) {
    ioctl(m_info.socket, FIONREAD, &n);
    ZiDEBUG(m_mx, ([
      socket = m_info.socket,
      len,
      size = m_rxContext.buf->size,
      offset = m_rxContext.buf->length,
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
    if (!cxnType(m_info.localIP, m_info.remoteIP, type))
      type = ZiIPType::V4;
    m_rxContext.addr.init(type);
    struct iovec iov;
    iov.iov_base = buf;
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
      m_rxContext.tos = getTOS(msg);
      m_rxContext.flags = msg.msg_flags;
    }
  } else {
    n = ::recv(
      m_info.socket, buf, len, 0);
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
    m_rxContext.complete();
    return false;
  }

  ZiDEBUG(m_mx, ([
    socket = m_info.socket,
    len,
    n,
    buf = ZmRef(new ZiDebugBuf(buf, n))
  ](auto &s) {
    s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>()
      << " recv(" << len << "): " << n << '\n'
      << ZtHexDump_(buf->cspan());
  }));

  if (ZuUnlikely(!n && !m_info.options.udp())) {
    m_rxContext.complete();
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

  if (!m_rxContext.buf)
    m_rxContext.buf = m_mx->rxAllocator(m_info.options.udp())->alloc();
  if (ZuUnlikely(!m_rxContext.buf->avail())) {
    m_rxContext.complete();
    m_mx->epollRecv(this, m_info.socket, 0);
    return true;
  }

  if (!--quantum) {
    m_mx->rxRun([cxn = ZmRef(this)]() { cxn->recv(); });
    return true;
  }

  len = m_rxContext.buf->avail();
  buf = m_rxContext.buf->end();
  goto retry;
#endif
}

#ifdef ZiMultiplex_IOCP
void ZiConnection::overlappedRecv(int status, unsigned n, ZeError e)
{
  auto buf = ZuMv(m_rxPending);
  if (!m_rxUp.load_() || m_rxContext.completed()) {
    if (m_discPending) notifyDisconnected(m_discPeer);
    return;
  }
  m_rxContext.buf = ZuMv(buf);
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
    buf = ZmRef(new ZiDebugBuf(
      m_rxContext.buf->end(), n))
  ](auto &s) {
    s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>()
      << " WSARecv(" << n << ")\n"
      << ZtHexDump_(buf->cspan());
  }));

  if (m_info.options.udp()) {
    m_rxContext.addr.sync();
    m_rxContext.tos = getTOS(m_rxMsg);
    m_rxContext.flags = m_rxMsg.dwFlags;
  }
  executedRecv(n);

  if (ZuUnlikely(m_rxContext.completed())) {
    if (m_rxContext.disconnected()) disconnect(false);
    return;
  }

  if (!m_rxContext.buf)
    m_rxContext.buf = m_mx->rxAllocator(m_info.options.udp())->alloc();
  if (ZuUnlikely(!m_rxContext.buf->avail())) {
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
  if (ZuUnlikely(m_mx->filterRx(
	  this, m_rxContext.buf->end(), n)))
    return;
#endif

#ifdef ZiMultiplex_BENCH
  if (ZuUnlikely(m_mx->benchFn()))
    m_mx->benchFn()(Zi::IOBenchEvent{.kind = Zi::IOBenchKind::Rx,
#ifdef ZiMultiplex_DEBUG
      .buf = m_rxContext.buf.ptr(),
#endif
      .ring = m_rxContext.buf->ring(),
#ifdef ZiMultiplex_DEBUG
      .cxn = this, .streamOffset = m_rxBytes,
#endif
      .sid = m_mx->rxThread(), .count = n, .result = int(n),
      .flags = m_rxContext.flags});
#endif
  m_rxCalls++, m_rxBytes += n;
  m_rxContext.buf->length += n;
  while (!m_rxContext());
}

void ZiConnection::send(ZiTxFn fn)
{
  m_mx->txInvoke([this, fn = ZuMv(fn)]() mutable {
    send_(ZuMv(fn));
    return this;
  });
}

void ZiConnection::send_(ZiTxFn fn)
{
  ZiAssert(m_mx->invoked(m_mx->txThread()), "ZiMultiplex", (), "Tx owner", return);
  if (fn(m_txContext)) send();
}

void ZiConnection::sync()
{
  ZiAssert(!m_mx->invoked(m_mx->rxThread()) &&
    !m_mx->invoked(m_mx->txThread()), "ZiMultiplex", (), "I/O shard sync", return);

  ZmBlock<>{}([this](auto wake) {
    m_mx->txInvoke([this, wake = ZuMv(wake)]() mutable {
      m_syncs.push(Sync_{ZmFn<void()>{ZuMv(wake)}});
      send();
      synced();
    });
  });
}

void ZiConnection::synced()
{
  if (!m_txContext.completed()) return;
#ifdef ZiMultiplex_URing
  if (m_txOps.count_() || m_txWait.waiting) return;
#endif
  while (auto node = m_syncs.shift()) node->wake();
}

void ZiConnection::sendNext()
{
  if (m_txContext.completed()) return;
  m_txContext.length = 0;
  while (!m_txContext());
}

void ZiConnection::send()
{
  if (m_sending) return;
  m_sending = true;
  send__();
  m_sending = false;
  synced();
}

void ZiConnection::send__()
{
#ifdef ZiMultiplex_DEBUG
  if (m_mx->trace()) m_mx->traceCapture();
#endif

  if (ZuUnlikely(!m_txUp.load_())) { m_txContext.complete(); return; }

  if (ZuLikely(m_txContext.completed())) return;

  if (!m_txContext.buf) {
    sendNext();
    if (m_txContext.completed()) return;
  }

  m_txContext.txSelect();

#ifdef ZiMultiplex_URing
  if (m_mx->backend() == ZiMxBackend::URing) {
    uringSend();
    return;
  }
#endif

  unsigned quantum = m_mx->ioQuantum();
  unsigned n;
#ifdef ZiMultiplex_IOCP
  WSABUF wsaBuf;
  wsaBuf.buf = reinterpret_cast<char *>(m_txContext.txData());
  wsaBuf.len = m_txContext.txRemaining;

  ZeError e;
  DWORD n_;
#endif

#ifdef ZiMultiplex_EPoll
  auto buf = m_txContext.txData();
  unsigned len = m_txContext.txRemaining;

  ZeError e;
  int n_;
#endif

#ifdef ZiMultiplex_FILTER
  if (ZuUnlikely(m_mx->filterTx(
	  this, m_txContext.txData(),
	  m_txContext.txRemaining))) {
    n = m_txContext.txRemaining;
    goto executed;
  }
#endif

#ifdef ZiMultiplex_IOCP
retry:
  ZiDEBUG(m_mx, ([
    socket = m_info.socket,
    size = m_txContext.txRemaining,
    offset = m_txContext.txOffset,
    buf = ZmRef(new ZiDebugBuf(wsaBuf.buf, wsaBuf.len))
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
    if (!cxnType(m_info.localIP, m_info.remoteIP, type))
      type = ZiIPType::V4;
    if (type == ZiIPType::V6) {
#ifdef IPV6_TCLASS
      putTOS(msg, control, m_txContext.tos, IPPROTO_IPV6, IPV6_TCLASS);
#endif
    } else
      putTOS(msg, control, m_txContext.tos, IPPROTO_IP, IP_TOS);
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
    size = m_txContext.txRemaining,
    offset = m_txContext.txOffset,
    buf = ZmRef(new ZiDebugBuf(buf, len))
  ](auto &s) {
    s << "FD: " << ZuBoxed(socket).fmt<ZuFmt::Right<3>>()
      << " send(" << buf->length << ") size: " << size
      << " offset: " << offset << '\n'
      << ZtHexDump_(buf->cspan());
  }));

  if (m_info.options.udp()) {
    struct iovec iov;
    iov.iov_base = buf;
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
    if (!cxnType(m_info.localIP, m_info.remoteIP, type))
      type = ZiIPType::V4;
    if (type == ZiIPType::V6) {
#ifdef IPV6_TCLASS
      putTOS(msg, control, sizeof(control), m_txContext.tos,
	IPPROTO_IPV6, IPV6_TCLASS);
#endif
    } else
      putTOS(msg, control, sizeof(control), m_txContext.tos,
	IPPROTO_IP, IP_TOS);
    n_ = ::sendmsg(m_info.socket, &msg, 0);
  }
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
  if (m_info.options.udp() &&
      n != m_txContext.txRemaining) {
    errorSend(Zi::IOError, ZeError{EIO});
    return;
  }
  executedSend(n);

  if (ZuLikely(m_txContext.completed())) {
    if (m_txContext.disconnected()) disconnect(false);
    return;
  }

  if (ZuLikely(!m_txContext.txRemaining)) {
    m_txContext.complete();
    return;
  }

  if (!--quantum) {
    m_mx->txRun([cxn = ZmRef(this)]() { cxn->send(); });
    return;
  }

#ifdef ZiMultiplex_IOCP
  wsaBuf.buf = reinterpret_cast<char *>(m_txContext.txData());
  wsaBuf.len = m_txContext.txRemaining;
#endif

#ifdef ZiMultiplex_EPoll
  buf = m_txContext.txData();
  len = m_txContext.txRemaining;
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

  m_txContext.txAdvance(n);
  m_txContext.length = n;
  if (m_txContext.txRemaining) return;
  // the completion may synchronously call send_() for the next message
  while (!m_txContext());
  if (!m_txContext.completed() && m_txContext.buf) m_txContext.txSelect();
  if (!m_txContext.completed() &&
      (!m_txContext.buf || !m_txContext.txRemaining)) {
    m_txContext.buf = nullptr;
    sendNext();
    if (!m_txContext.completed() && m_txContext.buf) m_txContext.txSelect();
  }
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
    if (!options.udp() && !options.nagle() &&
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
    if (!options.udp() && !options.nagle() &&
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
  if (m_backend != ZiMxBackend::URing) {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(struct epoll_event));
    ev.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET;
    ev.data.u64 = u64_cxn(cxn);
    if (epoll_ctl(m_epollFD, EPOLL_CTL_ADD, s, &ev) < 0) {
      ZeError e{errno};
      m_cxns->del(s);
      cxn->closeSocket_();
      Error("epoll_ctl(EPOLL_CTL_ADD)", Zi::IOError, e);
      return false;
    }
  }
#endif

  {
    ZmGuard<ZmRWLock> guard(m_cxnWatchLock);
    if (m_addCxnFn) m_addCxnFn(cxn);
  }
  return true;
}

void ZiMultiplex::cxnDel(Socket s)
{
#ifdef ZiMultiplex_EPoll
  if (m_backend != ZiMxBackend::URing)
    epoll_ctl(m_epollFD, EPOLL_CTL_DEL, s, 0);
#endif

  if (ZmRef<ZiConnection> cxn = m_cxns->delVal(s)) {
    ZmGuard<ZmRWLock> guard(m_cxnWatchLock);
    if (m_delCxnFn) m_delCxnFn(cxn);
  }
}

void ZiMultiplex::watch(
    Ztc::Mx::AddCxnFn addCxnFn, Ztc::Mx::DelCxnFn delCxnFn)
{
  ZmGuard<ZmRWLock> guard(m_cxnWatchLock);
  ZmAssert(!m_addCxnFn && !m_delCxnFn, return);
  m_addCxnFn = ZuMv(addCxnFn);
  m_delCxnFn = ZuMv(delCxnFn);
}

void ZiMultiplex::unwatch()
{
  ZmGuard<ZmRWLock> guard(m_cxnWatchLock);
  m_addCxnFn = {};
  m_delCxnFn = {};
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
    m_mx->rxRun([cxn = ZmRef(this), peer]() {
      cxn->notifyDisconnected(peer);
    });
    return;
  }
  
  m_txUp = false;

#ifdef ZiMultiplex_URing
  if (m_txWait.waiting) m_txWait.ring->unwait(&m_txWait);
  if (m_txOp) m_txOp->ring->cancel(m_txOp);
#endif
  m_txContext.complete();
  synced();

  m_mx->rxRun([cxn = ZmRef(this), peer]() { cxn->disconnect_2(peer); });
}

void ZiConnection::disconnect_2(bool peer)
{
  if (!m_rxUp.load_()) {
    notifyDisconnected(peer);
    return;
  }

  m_rxUp = 0;

  if (m_info.options.udp()) {
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
  overlapped.clear();
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
    return;
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
#ifdef ZiMultiplex_IOCP
  m_discPending = true;
  m_discPeer = peer;
  m_rxContext.complete();
  if (m_rxPending) {
    CancelIoEx(reinterpret_cast<HANDLE>(m_info.socket),
      reinterpret_cast<OVERLAPPED *>(&m_rxOverlapped));
    return;
  }
  if (m_discOverlapped.pending()) return;
  m_discPending = false;
#endif
#ifdef ZiMultiplex_URing
  if (m_mx->backend() == ZiMxBackend::URing) {
    m_discPending = true;
    m_uringPeer = peer;
    m_rxContext.complete();
    if (m_rxOp.waiter.waiting) m_rxOp.ring->unwait(&m_rxOp.waiter);
    if (m_rxOp.active()) m_rxOp.ring->cancel(&m_rxOp);
    if (unsigned(m_uringPending)) return;
    m_discPending = false;
  }
#endif
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
    m_mx->rxRun([cxn = ZmRef(this), peer]() {
      cxn->notifyDisconnected(peer);
    });
    return;
  }
  
  m_txUp = false;

#ifdef ZiMultiplex_URing
  if (m_txWait.waiting) m_txWait.ring->unwait(&m_txWait);
  if (m_txOp) m_txOp->ring->cancel(m_txOp);
#endif
  m_txContext.complete();
  synced();

  m_mx->rxRun([cxn = ZmRef(this), peer]() { cxn->close_2(peer); });
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
  m_rxRingSize(mxParams.rxRingSize()),
  m_txRingSize(mxParams.txRingSize()),
  m_rxBufSize(mxParams.rxBufSize()),
  m_txBufSize(mxParams.txBufSize())
  , m_backend(mxParams.backend()),
  m_tcpBufAllocator(mxParams.tcpBufAllocator()),
  m_udpBufAllocator(mxParams.udpBufAllocator())
#ifdef ZiMultiplex_BENCH
  , m_benchFn(mxParams.benchFn())
#endif
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
  if (!m_tcpBufAllocator)
    m_tcpBufAllocator = new Zi::RxTCPBufAllocator<ZiRxQueue::Node,
      ZiIOBuf_DefltSize, ZiIOBuf_DefltMaxSize, ZiIOBuf_HeapID>{m_rxRingSize};
  if (!m_udpBufAllocator)
    m_udpBufAllocator = new Zi::RxUDPBufAllocator<ZiRxQueue::Node,
      ZiIOBuf_DefltSize, ZiIOBuf_DefltMaxSize, ZiIOBuf_HeapID>{m_rxRingSize};
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
#ifdef ZiMultiplex_URing
  // Also release mappings from a failed start which never installed owner loops.
  if (m_txLoop != m_rxLoop) delete m_txLoop;
  delete m_rxLoop;
#endif
}

bool ZiMultiplex::start__()
{
  if (m_rxRingSize < ZiMxParams::MinRingSize || m_rxRingSize > ZiMxParams::MaxRingSize ||
      m_txRingSize < ZiMxParams::MinRingSize || m_txRingSize > ZiMxParams::MaxRingSize) {
    Error("ringSize", Zi::NotReady, ZeError{ZiEINVAL});
    return false;
  }
#ifdef ZiMultiplex_EPoll
  if (!m_epollQuantum || m_epollQuantum > unsigned(INT_MAX)) {
    Error("epollQuantum", Zi::NotReady, ZeError{ZiEINVAL});
    return false;
  }
#endif

#ifdef ZiMultiplex_URing
  // start__ is called only while stopped, including retries after failed start.
  if (m_txLoop != m_rxLoop) delete m_txLoop;
  delete m_rxLoop;
  m_rxLoop = m_txLoop = nullptr;
  if (m_backend != ZiMxBackend::EPoll) {
    bool available = Zi::uring();
    auto tcp = m_tcpBufAllocator->ring();
    auto udp = m_udpBufAllocator->ring();
    available = available && tcp && udp && tcp != udp &&
      tcp->fd() >= 0 && udp->fd() >= 0 && tcp->sq().mask && udp->sq().mask &&
      tcp->returnFD() >= 0 && udp->returnFD() >= 0;
    if (available) {
      m_rxLoop = new Zi::URingLoop{this, rxThread()};
      m_txLoop = rxThread() == txThread() ? m_rxLoop :
	new Zi::URingLoop{this, txThread()};
      available = m_rxLoop->init() &&
	(m_txLoop == m_rxLoop || m_txLoop->init());
    }
    if (!available) {
      if (m_txLoop != m_rxLoop) delete m_txLoop;
      delete m_rxLoop;
      m_rxLoop = m_txLoop = nullptr;
      if (m_backend == ZiMxBackend::URing) {
	Error("io_uring initialization", Zi::NotReady, ZeError{ENOSYS});
	return false;
      }
      m_backend = ZiMxBackend::EPoll;
    } else {
      m_backend = ZiMxBackend::URing;
      for (bool datagram : {false, true}) {
	auto allocator = rxAllocator(datagram);
	auto ring = allocator->ring();
	if (!ring->io()) ring->io(
	  new Zi::URing{ring, true, datagram},
	  [](Zi::URing *p) { delete p; });
      }
    }
  }
#else
  if (m_backend == ZiMxBackend::URing) return false;
  m_backend = ZiMxBackend::EPoll;
#endif
  if (!ZmScheduler::start__()) return false;

#ifdef ZiMultiplex_IOCP
  {
    WSADATA wd;
    DWORD errNo = WSAStartup(MAKEWORD(2, 2), &wd);
    if (errNo) {
      ZmScheduler::stop__();
      Error("WSAStartup", Zi::IOError, ZeError(errNo));
      return false;
    }
  }

  m_completionPort =
    CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, params().nThreads());
  if (!m_completionPort) {
    ZeError e(GetLastError());
    WSACleanup();
    ZmScheduler::stop__();
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
    ZeError e{errno};
    ZmScheduler::stop__();
    Error("epoll_create", Zi::IOError, e);
    return false;
  }
#ifdef ZiMultiplex_URing
  if (m_backend == ZiMxBackend::URing) {
    m_rxLoop->start(true);
    if (m_txLoop != m_rxLoop) m_txLoop->start(false);
    return true;
  }
#endif
  // Preserve one wake per queued rx() callback without a pipe's byte limit.
  if ((m_wakeFD = eventfd(0, EFD_NONBLOCK | EFD_SEMAPHORE)) < 0) {
    ZeError e{errno};
    ::close(m_epollFD); m_epollFD = -1;
    ZmScheduler::stop__();
    Error("eventfd", Zi::IOError, e);
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
      ZmScheduler::stop__();
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

  bool ok = ZmScheduler::stop__();

  stop_2();

  m_stopping = nullptr;

  return ok;
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

#ifdef ZiMultiplex_URing
  if (m_backend == ZiMxBackend::URing) {
    m_uringStopping = 1 + unsigned(m_rxLoop != m_txLoop);
    m_rxLoop->stop();
    return;
  }
#endif
  if (m_stopping) // paranoia
    m_stopping->post();
}

void ZiMultiplex::stop_2()
{
  // close down underlying I/O platform

  wakeFn(rxThread(), WakeFn{});
#ifdef ZiMultiplex_URing
  if (m_backend == ZiMxBackend::URing && txThread() != rxThread())
    wakeFn(txThread(), WakeFn{});
#endif

#ifdef ZiMultiplex_IOCP
  CloseHandle(m_completionPort);
  m_completionPort = Zi::nullHandle();

  WSACleanup();	// this is reference counted
#endif

#ifdef ZiMultiplex_EPoll
  if (m_wakeFD >= 0) { ::close(m_wakeFD); m_wakeFD = -1; }
  if (m_epollFD >= 0) { ::close(m_epollFD); m_epollFD = -1; }
#endif
#ifdef ZiMultiplex_URing
  if (m_txLoop != m_rxLoop) delete m_txLoop;
  delete m_rxLoop;
  m_rxLoop = m_txLoop = nullptr;
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
      nListeners = m_listeners->count_()
    ](auto &s) {
      s << "wait() nThreads: " << ZuBoxed(nThreads).fmt<ZuFmt::Right<2>>()
	<< " nConnections: " <<
	  ZuBoxed(nConnections).fmt<ZuFmt::Right<4>>()
	<< " epollFD: " << ZuBoxed(epollFD).fmt<ZuFmt::Right<3>>()
	<< " wakeFD: " << ZuBoxed(wakeFD).fmt<ZuFmt::Right<3>>()
	<< " nListeners: " << ZuBoxed(nListeners).fmt<ZuFmt::Right<3>>();
    }));

#if 0
#ifdef ZiMultiplex_DEBUG
    ZuTime now = Zm::now();
#endif
#endif

    r = epoll_wait(m_epollFD, ev, m_epollQuantum,
      m_backend == ZiMxBackend::URing ? 0 : -1);

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
	    txRun([cxn = ZmRef(cxn)]() { cxn->send(); });
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
    
    if (m_backend == ZiMxBackend::URing) {
      // Native multishot poll is edge-triggered. A full control batch may
      // leave connect/listener events ready without another edge.
      if (unsigned(r) == m_epollQuantum) rxRun([this]() { rx(); });
      break;
    }
    if (!wake) push([this]() { rx(); }, rxThread());
    break;
  }
#endif
}

void ZiMultiplex::wake()
{
#ifdef ZiMultiplex_URing
  if (m_backend == ZiMxBackend::URing) {
    if (m_rxLoop) m_rxLoop->wake();
    return;
  }
#endif
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

  uint64_t count;
  return ::read(m_wakeFD, &count, sizeof(count)) == sizeof(count);
}

void ZiMultiplex::writeWake()
{
  ZiDEBUG(this, ([wakeFD = m_wakeFD](auto &s) {
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

void Ztc::MxMgr::capture(CaptureFn fn)
{
  ZiMxMgr_::instance()->capture(ZuMv(fn));
}

void Ztc::MxMgr::watch(AddFn addFn, DelFn delFn)
{
  ZiMxMgr_::instance()->watch(ZuMv(addFn), ZuMv(delFn));
}

void Ztc::MxMgr::unwatch()
{
  ZiMxMgr_::instance()->unwatch();
}
