//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// socket I/O multiplexing

#ifndef ZiMultiplex_HH
#define ZiMultiplex_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <stdint.h>
#include <string.h>

#include <zlib/ZuArray.hh>
#include <zlib/ZuAssert.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuFmt.hh>
#include <zlib/ZuHash.hh>
#include <zlib/ZuObject.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuUnion.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmAssert.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmScheduler.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmRWLock.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmPlatform.hh>
#include <zlib/ZmPolymorph.hh>
#include <zlib/ZmList.hh>

#include <zlib/ZtEnum.hh>
#include <zlib/ZtArray.hh>

#include <zlib/ZePlatform.hh>
#include <zlib/ZiLog.hh>

#include <zlib/ZiPlatform.hh>
#include <zlib/ZiIP.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiIOContext.hh>
#include <zlib/ZtcMx.hh>

#if defined(ZDEBUG) && !defined(ZiMultiplex_DEBUG)
#define ZiMultiplex_DEBUG	// enable testing / debugging
#if !defined(ZiMultiplex_FILTER)
#define ZiMultiplex_FILTER	// enable traffic filtering
#endif
#endif


#ifdef ZiMultiplex_DEBUG
#include <zlib/ZmBackTracer.hh>
#endif

#ifdef _WIN32
#define ZiMultiplex_IOCP	// Windows I/O completion ports
#endif

#ifdef linux
#define ZiMultiplex_EPoll	// Linux epoll
#endif

#ifdef ZiMultiplex_DEBUG
#define ZiDEBUG(mx, e) do { if ((mx)->debug()) ZiLOG(Debug, "ZiMultiplex", (e)); } while (0)
#else
#define ZiDEBUG(mx, e) (void())
#endif

#ifdef ZiMultiplex_IOCP
#define ZiMultiplex__AcceptHeap 1
#define ZiMultiplex__ConnectHash 0
#endif

#ifdef ZiMultiplex_EPoll
#define ZiMultiplex__AcceptHeap 0
#define ZiMultiplex__ConnectHash 1
#endif

class ZiConnection;
class ZiMultiplex;

#ifdef ZiMultiplex_FILTER
using FilterFn = ZmFn<bool(ZiConnection *, uint8_t *, unsigned),
  ZmFnHeapID<"ZiMultiplex.FilterFn">>;
#endif

class ZiCxnOptions;
struct ZiCxnInfo;

// transient
using ZiFailFn = ZmFn<void(bool), ZmFnHeapID<"ZiMultiplex.FailFn">>;

#ifdef _WIN32
using ZiIPv6MReq = IPV6_MREQ;
#else
using ZiIPv6MReq = struct ipv6_mreq;
#endif

// multicast subscription request (IGMP/MLD Report)
class ZiMReq {
public:
  using Req = ZuUnion<void, ip_mreq, ZiIPv6MReq>;
  enum {
    Null = Req::Index<void>{},
    V4 = Req::Index<ip_mreq>{},
    V6 = Req::Index<ZiIPv6MReq>{}
  };
  ZuAssert(Null == ZiIPType::Null);
  ZuAssert(V4 == ZiIPType::V4);
  ZuAssert(V6 == ZiIPType::V6);

  ZiMReq() = default;
  ZiMReq(const ZiIP &addr, const ZiIP &mif) {
    if (addr.type() != ZiIPType::V4 || mif.type() != ZiIPType::V4) {
      m_req.type_(Null);
      return;
    }
    m_req.p<V4>(ip_mreq{});
    m_req.p<V4>().imr_multiaddr = addr.inAddr();
    m_req.p<V4>().imr_interface = mif.inAddr();
  }
  ZiMReq(const ZiIP &addr, unsigned ifIndex) {
    if (addr.type() != ZiIPType::V6) {
      m_req.type_(Null);
      return;
    }
    m_req.p<V6>(ZiIPv6MReq{});
    m_req.p<V6>().ipv6mr_multiaddr = addr.in6Addr();
    m_req.p<V6>().ipv6mr_interface = ifIndex;
  }

  explicit ZiMReq(const struct ip_mreq &m) :
      m_req{m} { }
  ZiMReq &operator =(const struct ip_mreq &m) {
    m_req.p<V4>(m);
    return *this;
  }

  ZiIPType::T type() const { return m_req.type(); }

  bool equals(const ZiMReq &m) const {
    if (type() != m.type()) return false;
    switch (type()) {
      case ZiIPType::V4:
	return addr() == m.addr() && mif() == m.mif();
      case ZiIPType::V6:
	return addr() == m.addr() && ifIndex() == m.ifIndex();
      default:
	return true;
    }
  }
  int cmp(const ZiMReq &m) const {
    int r;
    if (r = ZuCompare(type(), m.type())) return r;
    switch (type()) {
      case ZiIPType::V4:
	if (r = addr().cmp(m.addr())) return r;
	return mif().cmp(m.mif());
      case ZiIPType::V6:
	if (r = addr().cmp(m.addr())) return r;
	return ZuCompare(ifIndex(), m.ifIndex());
      default:
	return 0;
    }
  }
  friend inline bool operator ==(const ZiMReq &l, const ZiMReq &r) {
    return l.equals(r);
  }
  friend inline int operator <=>(const ZiMReq &l, const ZiMReq &r) {
    return l.cmp(r);
  }

  bool operator !() const {
    switch (type()) {
      case ZiIPType::V4:
	return !addr() && !mif();
      case ZiIPType::V6:
	return !addr() && !ifIndex();
      default:
	return true;
    }
  }
  ZuOpBool

  uint32_t hash() const {
    switch (type()) {
      case ZiIPType::V4:
	return ZuHash<uint8_t>::hash(type()) ^ addr().hash() ^ mif().hash();
      case ZiIPType::V6:
	return ZuHash<uint8_t>::hash(type()) ^
	  addr().hash() ^ ZuBoxed(ifIndex()).hash();
      default:
	return ZuHash<uint8_t>::hash(type());
    }
  }

  template <typename S> void print(S &s) const {
    switch (type()) {
      case ZiIPType::V4:
	s << addr() << "->" << mif();
	break;
      case ZiIPType::V6:
	s << addr() << "->" << ZuBoxed(ifIndex());
	break;
      default:
	s << "null";
	break;
    }
  }

  ZiIP addr() const {
    switch (type()) {
      case ZiIPType::V4:
	return ZiIP{m_req.p<V4>().imr_multiaddr};
      case ZiIPType::V6:
	return ZiIP{m_req.p<V6>().ipv6mr_multiaddr};
      default:
	return ZiIP{};
    }
  }
  ZiIP mif() const {
    return type() == ZiIPType::V4 ?
      ZiIP{m_req.p<V4>().imr_interface} : ZiIP{};
  }
  unsigned ifIndex() const {
    return type() == ZiIPType::V6 ? m_req.p<V6>().ipv6mr_interface : 0;
  }

  const ip_mreq &ipMReq() const { return m_req.p<V4>(); }
  ip_mreq &ipMReq() { return m_req.p<V4>(); }
  const ZiIPv6MReq &ip6MReq() const { return m_req.p<V6>(); }
  ZiIPv6MReq &ip6MReq() { return m_req.p<V6>(); }

  struct Traits : public ZuBaseTraits<ZiMReq> { enum { IsPOD = 1 }; };
  friend Traits ZuTraitsType(ZiMReq *);

  friend ZuPrintFn ZuPrintType(ZiMReq *);

private:
  Req			m_req;
};

// Inline multicast memberships per socket; override for applications joining
// multiple groups. The default keeps single-group socket options allocation-free.
#ifndef ZiCxnOptions_NMReq
#define ZiCxnOptions_NMReq 1
#endif

// protocol/socket options
namespace ZiCxnFlags {
  // U - create UDP socket (default TCP)
  // M - combine with U for multicast server socket
  // L - combine with M and U for multicast loopback
  // K - set SO_KEEPALIVE socket option
  // D - enable Nagle algorithm (no TCP_NODELAY)

  ZtFlags_(ZiCxnFlags, uint8_t,
    UDP, Multicast, LoopBack, KeepAlive, Nagle);
  ZtFlagsMap(ZiAPI, ZiCxnFlags, Map, "U", "M", "L", "K", "D");
}

class ZiCxnOptions {
  using MReqs = ZuArray<ZiMReq, ZiCxnOptions_NMReq>;

public:
  ZiCxnOptions() = default;
  ZiCxnOptions(const ZiCxnOptions &) = default;
  ZiCxnOptions &operator =(const ZiCxnOptions &) = default;
  ZiCxnOptions(ZiCxnOptions &&) = default;
  ZiCxnOptions &operator =(ZiCxnOptions &&) = default;

  uint32_t flags() const { return m_flags; }
  ZiCxnOptions &flags(uint32_t flags) {
    m_flags = flags;
    return *this;
  }
  bool udp() const {
    using namespace ZiCxnFlags;
    return m_flags & UDP();
  }
  ZiCxnOptions &udp(bool b) {
    using namespace ZiCxnFlags;
    b ? (m_flags |= UDP()) : (m_flags &= ~UDP());
    return *this;
  }
  bool multicast() const {
    using namespace ZiCxnFlags;
    return m_flags & Multicast();
  }
  ZiCxnOptions &multicast(bool b) {
    using namespace ZiCxnFlags;
    b ? (m_flags |= Multicast()) : (m_flags &= ~Multicast());
    return *this;
  }
  bool loopBack() const {
    using namespace ZiCxnFlags;
    return m_flags & LoopBack();
  }
  ZiCxnOptions &loopBack(bool b) {
    using namespace ZiCxnFlags;
    b ? (m_flags |= LoopBack()) : (m_flags &= ~LoopBack());
    return *this;
  }
  bool keepAlive() const {
    using namespace ZiCxnFlags;
    return m_flags & KeepAlive();
  }
  ZiCxnOptions &keepAlive(bool b) {
    using namespace ZiCxnFlags;
    b ? (m_flags |= KeepAlive()) : (m_flags &= ~KeepAlive());
    return *this;
  }
  const MReqs &mreqs() const {
    return m_mreqs;
  }
  void mreq(const ZiMReq &mreq) { m_mreqs.push(mreq); }
  const ZiIP &mif() const { return m_mif; }
  ZiCxnOptions &mif(ZiIP ip) {
    m_mif = ip;
    return *this;
  }
  unsigned mifIndex() const { return m_mifIndex; }
  ZiCxnOptions &mifIndex(unsigned i) {
    m_mifIndex = i;
    return *this;
  }
  const unsigned &ttl() const { return m_ttl; }
  ZiCxnOptions &ttl(unsigned i) {
    m_ttl = i;
    return *this;
  }
  bool multicastValid(ZiIPType::T type) const {
    if (type != ZiIPType::V4 && type != ZiIPType::V6) return false;
    if (type == ZiIPType::V4 && !!m_mif && m_mif.type() != ZiIPType::V4)
      return false;
    if (type == ZiIPType::V6 && !!m_mif) return false;
    for (unsigned i = 0, n = m_mreqs.length(); i < n; i++) {
      const auto &req = m_mreqs[i];
      if (req.type() != type || !req.addr().multicast()) return false;
    }
    return true;
  }
  bool nagle() const {
    using namespace ZiCxnFlags;
    return m_flags & Nagle();
  }
  ZiCxnOptions &nagle(bool b) {
    using namespace ZiCxnFlags;
    b ? (m_flags |= Nagle()) : (m_flags &= ~Nagle());
    return *this;
  }

  bool equals(const ZiCxnOptions &o) const {
    using namespace ZiCxnFlags;
    if (m_flags != o.m_flags) return false;
    if (!(m_flags & Multicast())) return true;
    return m_mreqs == o.m_mreqs &&
      m_mif == o.m_mif && m_mifIndex == o.m_mifIndex && m_ttl == o.m_ttl;
  }
  int cmp(const ZiCxnOptions &o) const {
    using namespace ZiCxnFlags;
    int i;
    if (i = ZuCompare(m_flags, o.m_flags)) return i;
    if (!(m_flags & Multicast())) return i;
    if (i = m_mreqs.cmp(o.m_mreqs)) return i;
    if (i = m_mif.cmp(o.m_mif)) return i;
    if (i = ZuCompare(m_mifIndex, o.m_mifIndex)) return i;
    return ZuCompare(m_ttl, o.m_ttl);
  }
  friend inline bool operator ==(const ZiCxnOptions &l, const ZiCxnOptions &r) {
    return l.equals(r);
  }
  friend inline int operator <=>(const ZiCxnOptions &l, const ZiCxnOptions &r) {
    return l.cmp(r);
  }

  uint32_t hash() const {
    using namespace ZiCxnFlags;
    uint32_t code = ZuHash<uint32_t>::hash(m_flags);
    if (!(m_flags & Multicast())) return code;
    return code ^ m_mreqs.hash() ^ m_mif.hash() ^
      ZuBoxed(m_mifIndex).hash() ^ ZuBoxed(m_ttl).hash();
  }

  template <typename S> void print(S &s) const {
    using namespace ZiCxnFlags;
    s << "flags=" << Map::Print{m_flags};
    if (m_flags & Multicast()) {
      s << " mreqs={";
      for (unsigned i = 0, n = m_mreqs.length(); i < n; i++) {
	if (i) s << ',';
	s << m_mreqs[i];
      }
      s << "} mif=" << m_mif <<
	" mifIndex=" << ZuBoxed(m_mifIndex) <<
	" TTL=" << ZuBoxed(m_ttl);
    }
  }

  friend ZuPrintFn ZuPrintType(ZiCxnOptions *);

private:
  MReqs			m_mreqs;
  ZiIP			m_mif;
  unsigned		m_mifIndex = 0;
  unsigned		m_ttl = 0;
  ZiCxnFlags::T		m_flags = 0;
};

// listener info (socket, accept queue size, local IP/port, options)
struct ZiListenInfo {
  Zi::Socket	socket;
  unsigned	nAccepts = 0;
  ZiIP		ip;
  uint16_t	port = 0;
  ZiCxnOptions	options;
  Zi::IOBufAllocator *rxAllocator = nullptr;

  template <typename S> void print(S &s) const {
    s << "socket=" << ZuBoxed(socket) <<
      " nAccepts=" << nAccepts <<
      " options={" << options <<
      "} localAddr=" << ip << ':' << port;
  }
  friend ZuPrintFn ZuPrintType(ZiListenInfo *);
};

// cxn information (direction, socket, local & remote endpoints, options)
ZtEnumNS(ZiAPI, ZiCxnType, int8_t, TCPIn, TCPOut, UDP);

struct ZiCxnInfo { // pure aggregate, no ctor
  int			type = -1;	// ZiCxnType
  Zi::Socket		socket;
  ZiCxnOptions 		options;
  ZiSockAddr		local;
  ZiSockAddr		remote;

  Zi::IOBufAllocator *rxAllocator = nullptr;

  bool operator !() const { return type == -1; }
  ZuOpBool

  template <typename S> void print(S &s) const {
    s << "type=" << ZiCxnType::name(type) <<
      " socket=" << ZuBoxed(socket) <<
      " options={" << options << "} ";
    s << "localAddr=" << local.ip() << ':' << local.port() <<
      " remoteAddr=" << remote.ip() << ':' << remote.port();
  }
  friend ZuPrintFn ZuPrintType(ZiCxnInfo *);
};

using ZiListenFn = ZmFn<void(const ZiListenInfo &),
  ZmFnHeapID<"ZiMultiplex.ListenFn">>;
using ZiConnectFn = ZmFn<ZiConnection *(const ZiCxnInfo &),
  ZmFnHeapID<"ZiMultiplex.ConnectFn">>;

#ifdef ZiMultiplex_IOCP
// overlapped I/O structure for a single request (Windows IOCP) - internal
class Zi_Overlapped {
public:
  using Executed = ZmFn<void(int, unsigned, ZeError),
    ZmFnHeapID<"ZiMultiplex.OverlappedFn">>;

  Zi_Overlapped() { }
  ~Zi_Overlapped() { }

  template <typename Executed>
  void init(Executed &&executed) {
    memset(&m_wsaOverlapped, 0, sizeof(WSAOVERLAPPED));
    m_executed = ZuFwd<Executed>(executed);
  }

  void complete(int status, unsigned len, ZeError e) {
    auto executed = ZuMv(m_executed);
    executed(status, len, e); // Note: may destroy this object
  }

  bool pending() const { return !!m_executed; }
  void clear() { m_executed = {}; }

private:
  WSAOVERLAPPED		m_wsaOverlapped;
  Executed		m_executed;
};
#endif

// connection class - must be derived from and instantiated by caller
// when listen() or connect() completion is called with an OK status;
// derived class must supply connected() and disconnected() functions
// (and probably a destructor)
class ZiAPI ZiConnection :
  public ZmPolymorph,
  public Ztc::Connection
{
  ZiConnection(const ZiConnection &) = delete;
  ZiConnection &operator =(const ZiConnection &) = delete;

friend ZiMultiplex;

public:
  using Socket = Zi::Socket;

  // index on socket
  static Zi::Socket SocketAxor(const ZiConnection *c) {
    return c->info().socket;
  }

protected:
  ZiConnection(ZiMultiplex *mx, const ZiCxnInfo &ci);

public:
  virtual ~ZiConnection();

  // recv
  void recv(ZiRxFn fn);
  void recv_(ZiRxFn fn);	// direct call from within rx thread

  // send
  void send(ZiTxFn fn);
  void send_(ZiTxFn fn);	// direct call from within tx thread

  // Control-thread blocking sync - drains Tx thread
  void sync();

  // graceful disconnect (socket shutdown); then socket close
  void disconnect();

  // close abruptly without socket shutdown
  void close();

  // low-frequency - vtbl dispatch overhead is fine
  virtual void connected(ZiRxContext &rxContext) = 0;
  virtual void disconnected(bool peer) = 0;

  bool up() const {
    return m_rxUp.load_() && m_txUp.load_();
  }

  ZiMultiplex *mx() const { return m_mx; }
  const ZiCxnInfo &info() const { return m_info; }
  // telemetry snapshots - intentionally unclean cross-thread reads
  uint64_t rxCalls() const { return m_rxCalls; }
  uint64_t rxBytes() const { return m_rxBytes; }
  uint64_t txCalls() const { return m_txCalls; }
  uint64_t txBytes() const { return m_txBytes; }

  Ztc::Connection::TelKey telKey() const override;
  void telemetry(Ztc::CxnTelemetry &data) const override;

  // for ZiMxPlugin use
  ZiRxContext &rxContext() { return m_rxContext; } // Rx thread exclusive
  ZiTxContext &txContext() { return m_txContext; } // Tx thread exclusive

private:
  void connected();

#ifdef ZiMultiplex_EPoll
  bool recv();
#else
  void recv();
#endif
#ifdef ZiMultiplex_IOCP
  void overlappedRecv(int status, unsigned n, ZeError e);
#endif
  void errorRecv(int status, ZeError e);
  void executedRecv(unsigned n);

  void send();
  bool allocatorOK();
  void errorSend(int status, ZeError e);
  void executedSend(unsigned n);

  void disconnect(bool peer);
  void disconnect_1(bool peer);
  void disconnect_2(bool peer);
  void close(bool peer);
  void close_1(bool peer);
  void close_2(bool peer);
#ifdef ZiMultiplex_IOCP
  void overlappedDisconnect(int status, unsigned n, ZeError e);
#endif
  void errorDisconnect(int status, ZeError e, bool peer);
  void executedDisconnect(bool peer);
  void notifyDisconnected(bool peer);

  // immutable
  ZiMultiplex		*m_mx;
  ZiCxnInfo		m_info;

  // mutable shared
  ZmAtomic<unsigned>	m_rxUp;
  ZmAtomic<unsigned>	m_txUp;

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  ZiRxContext		m_rxContext;
  uint64_t		m_rxCalls;
  uint64_t		m_rxBytes;
#ifdef ZiMultiplex_IOCP
  bool			m_discPending = false;
#endif
#ifdef ZiMultiplex_IOCP
  ZmRef<Zi::IOBuf>	m_rxPending; // actual outstanding kernel receive hold
  Zi_Overlapped		m_discOverlapped;
  Zi_Overlapped		m_rxOverlapped;
  WSABUF		m_rxWSABuf;
  WSAMSG		m_rxMsg;
  uint8_t		m_rxControl[WSA_CMSG_SPACE(sizeof(int))];
  DWORD			m_rxFlags;		// flags for WSARecv()
  bool			m_discPeer = false;
#endif

  // Tx thread exclusive
  alignas(Zm::CacheLineSize)
  ZiTxContext		m_txContext;
  uint64_t		m_txCalls;
  uint64_t		m_txBytes;
};

// named parameter list for configuring ZiMultiplex
class ZiMxParams {
public:
  enum { RxThread = 1, TxThread = 2 }; // defaults
  enum { RxQuantum = 8 };
  enum { EPollMaxFDs = 256 };

  ZiMxParams() :
    m_scheduler{ZmSchedParams{}
      .nThreads(3)
      .thread(ZiMxParams::RxThread, [](auto &t) { t.isolated(true); })
      .thread(ZiMxParams::TxThread, [](auto &t) { t.isolated(true); })} { }

  ZiMxParams(const ZiMxParams &) = default;
  ZiMxParams &operator =(const ZiMxParams &) = default;
  ZiMxParams(ZiMxParams &&) = default;
  ZiMxParams &operator =(ZiMxParams &&) = default;

  ZiMxParams &&rxThread(unsigned tid)
    { m_rxThread = tid; return ZuMv(*this); }
  ZiMxParams &&rxThread(ZuCSpan id) {
    unsigned tid = m_scheduler.sid(id);
    return rxThread(tid && tid <= m_scheduler.nThreads() ? tid : 0);
  }
  ZiMxParams &&txThread(unsigned tid)
    { m_txThread = tid; return ZuMv(*this); }
  ZiMxParams &&txThread(ZuCSpan id) {
    unsigned tid = m_scheduler.sid(id);
    return txThread(tid && tid <= m_scheduler.nThreads() ? tid : 0);
  }
  ZiMxParams &&rxBufSize(unsigned v)
    { m_rxBufSize = v; return ZuMv(*this); }
  ZiMxParams &&txBufSize(unsigned v)
    { m_txBufSize = v; return ZuMv(*this); }
  ZiMxParams &&rxQuantum(unsigned n)
    { m_rxQuantum = n; return ZuMv(*this); }
#ifdef ZiMultiplex_EPoll
  ZiMxParams &&epollMaxFDs(unsigned n)
    { m_epollMaxFDs = n; return ZuMv(*this); }
#endif
  ZiMxParams &&listenerHash(const char *id)
    { m_listenerHash = id; return ZuMv(*this); }
  ZiMxParams &&requestHash(const char *id)
    { m_requestHash = id; return ZuMv(*this); }
  ZiMxParams &&cxnHash(const char *id)
    { m_cxnHash = id; return ZuMv(*this); }
#ifdef ZiMultiplex_DEBUG
  ZiMxParams &&trace(bool b) { m_trace = b; return ZuMv(*this); }
  ZiMxParams &&debug(bool b) { m_debug = b; return ZuMv(*this); }
  ZiMxParams &&frag(bool b) { m_frag = b; return ZuMv(*this); }
  ZiMxParams &&yield(bool b) { m_yield = b; return ZuMv(*this); }
#endif

  ZmSchedParams &scheduler() { return m_scheduler; }

  template <typename L>
  ZiMxParams &&scheduler(L &&l) {
    ZuFwd<L>(l)(m_scheduler);
    return ZuMv(*this);
  }

  unsigned rxThread() const { return m_rxThread; }
  unsigned txThread() const { return m_txThread; }
  ZuCSpan rxThreadID() const { return m_scheduler.thread(m_rxThread).name(); }
  ZuCSpan txThreadID() const { return m_scheduler.thread(m_txThread).name(); }
  unsigned rxBufSize() const { return m_rxBufSize; }
  unsigned txBufSize() const { return m_txBufSize; }
  unsigned rxQuantum() const { return m_rxQuantum; }
#ifdef ZiMultiplex_EPoll
  unsigned epollMaxFDs() const { return m_epollMaxFDs; }
#endif
  ZuCSpan listenerHash() const { return m_listenerHash; }
  ZuCSpan requestHash() const { return m_requestHash; }
  ZuCSpan cxnHash() const { return m_cxnHash; }
#ifdef ZiMultiplex_DEBUG
  bool trace() const { return m_trace; }
  bool debug() const { return m_debug; }
  bool frag() const { return m_frag; }
  bool yield() const { return m_yield; }
#endif

private:
  ZmSchedParams		m_scheduler;
  unsigned		m_rxThread = RxThread;
  unsigned		m_txThread = TxThread;
  unsigned		m_rxBufSize = 0;
  unsigned		m_txBufSize = 0;
  unsigned		m_rxQuantum = RxQuantum;
#ifdef ZiMultiplex_EPoll
  unsigned		m_epollMaxFDs = EPollMaxFDs;
#endif

  const char		*m_listenerHash = "ZiMultiplex.ListenerHash";
  const char		*m_requestHash = "ZiMultiplex.RequestHash";
  const char		*m_cxnHash = "ZiMultiplex.CxnHash";
#ifdef ZiMultiplex_DEBUG
  bool			m_trace = false;
  bool			m_debug = false;
  bool			m_frag = false;
  bool			m_yield = false;
#endif
};

// ZiMultiplex plugin for user-mode TCP/UDP
class ZiMxPlugin : public ZmPolymorph, public Ztc::Mx {
public:
  // start/stop result
  using StartResult = ZuUnion<void, ZeException>;
  using StopResult = ZuUnion<void, ZeException>;

  // the derived class ctor can inspect mx->rxQuantum()
  ZiMxPlugin(ZiMultiplex *mx) : m_mx(mx) { }

  ZiMultiplex *mx() const { return m_mx; }

  // type will be one of RxUDP, TxUDP, RxTCP, TxTCP, RxTLS, TxTLS
  virtual Zi::IOBufPlugin *allocator(Zi::IOBufType::T type,
    unsigned size, unsigned maxSize, unsigned align,
    unsigned count) = 0;

  // Rx thread idempotent start/stop

  virtual void start() = 0;		// callback: started
  virtual void stop() = 0;		// callback: stopped

  // Rx thread functions

  virtual void rx() = 0;		// handle I/O events until woken or rxQuantum processed

  // callbacks: listening, listenFailed, accepted
  virtual void listen(
      ZiListenFn listenFn, ZiFailFn failFn, ZiConnectFn acceptFn,
      ZiIP localIP, uint16_t localPort, unsigned nAccepts,
      Zi::IOBufAllocator *rxAllocator, ZiCxnOptions options) = 0;
  virtual void stopListening(ZiIP localIP, uint16_t localPort) = 0;

  // callbacks: connected, connectFailed
  virtual void connect(
      ZiConnectFn fn, ZiFailFn failFn,
      ZiIPType::T ipType,
      ZiIP localIP, uint16_t localPort,
      ZiIP remoteIP, uint16_t remotePort,
      Zi::IOBufAllocator *rxAllocator, ZiCxnOptions options) = 0;

  // callbacks: udp, udpFailed
  virtual void udp(
      ZiConnectFn fn, ZiFailFn failFn,
      ZiIPType::T ipType,
      ZiIP localIP, uint16_t localPort,
      ZiIP remoteIP, uint16_t remotePort,
      Zi::IOBufAllocator *rxAllocator, ZiCxnOptions options) = 0;

  // Tx thread

  virtual void send(ZiConnection *) = 0;	// equivalent of ZiConnection::send()

  virtual void disconnect(ZiConnection *, bool peer) = 0;	// graceful - drains
  virtual void close(ZiConnection *, bool peer) = 0;		// abrupt

  // non-Rx thread functions

  virtual void wake() = 0;		// wake up rx(), cause it to return

private:
  ZiMultiplex	*m_mx = nullptr;
};

class ZiAPI ZiMultiplex : public ZmScheduler, public Ztc::Mx {
friend ZiConnection;

  class Listener_;
  class Accept;
  class Connect_;

  class Listener_ : public ZuObject {
  friend ZiMultiplex;
  friend Accept;

    using Socket = Zi::Socket;

  public:
    static Zi::Socket SocketAxor(const Listener_ &l) {
      return l.info().socket;
    }

  protected:
    template <typename ...Args>
    Listener_(ZiMultiplex *mx, ZiConnectFn acceptFn, Args &&...args) :
	m_mx(mx), m_acceptFn(ZuMv(acceptFn)),
	m_info{ZuFwd<Args>(args)...}, m_up(1) { }

  private:
    const ZiConnectFn &acceptFn() const { return m_acceptFn; }
    bool up() const { return m_up; }
    void down() { m_up = 0; }
    const ZiListenInfo &info() const { return m_info; }

    ZiMultiplex		*m_mx;
    ZiConnectFn		m_acceptFn;
    ZiListenInfo	m_info;
    bool		m_up;
  };
  ZmHashDerive(ListenerHash, Listener_,
    (ZmHashNode<Listener_,
      ZmHashKey<Listener_::SocketAxor,
	ZmHashHeapID<"ZiMultiplex.Listener",
	  ZmHashSharded<true>>>>));
  using Listener = ListenerHash::Node;

#if ZiMultiplex__AcceptHeap
  // heap-allocated asynchronous accept, exclusively used by IOCP
  class Accept;
friend Accept;
  class Accept : public ZmNHeap<Accept> {
  friend inline ZuStringT<"ZiMultiplex.Accept"> ZmNHeapID(Accept *);

  friend ZiMultiplex;

    Accept(Listener *listener) :
      m_listener(listener), m_info{
	ZiCxnType::TCPIn, Zi::nullSocket(), listener->m_info.options}
    {
      m_overlapped.init(
	Zi_Overlapped::Executed::Member<&Accept::executed>::fn(this));
    }

    ZmRef<Listener> listener() const { return m_listener; }
    ZiCxnInfo &info() { return m_info; }
    Zi_Overlapped &overlapped() { return m_overlapped; }
    void *buf() { return (void *)&m_buf[0]; }

    void executed(int status, unsigned n, ZeError e) {
      m_listener->m_mx->overlappedAccept(this, status, n, e);
      delete this;
    }

    ZmRef<Listener>	m_listener;
    ZiCxnInfo		m_info;
    Zi_Overlapped	m_overlapped;

    alignas(16)
    char		m_buf[(ZiSockAddr::MaxLen + 16) * 2];
  };

#endif

  // heap-allocated non-blocking / asynchronous connect
#if ZiMultiplex__ConnectHash
  class Connect_ : public ZuObject
#else
  class Connect_;
friend Connect_;
  class Connect_ : public ZmNHeap<Connect_>, public ZuObject
#endif
  {
  friend inline ZuStringT<"ZiMultiplex.Connect"> ZmNHeapID(Connect_ *);

  friend ZiMultiplex;

    using Socket = Zi::Socket;

#ifdef ZiMultiplex__ConnectHash
  public:
    static Zi::Socket SocketAxor(const Connect_ &c) {
      return c.info().socket;
    }
#endif

  protected:
    template <typename ...Args> Connect_(
	ZiMultiplex *mx, ZiConnectFn fn, ZiFailFn failFn, Args &&...args) :
      m_mx(mx), m_fn(fn), m_failFn(failFn), m_info{ZuFwd<Args>(args)...} {
#ifdef ZiMultiplex_IOCP
      m_overlapped.init(
	  Zi_Overlapped::Executed::Member<&Connect_::executed>::fn(this));
#endif
    }

  private:
    void fail(bool transient) { m_failFn(transient); }

    const ZiConnectFn &fn() const { return m_fn; }
#ifdef ZiMultiplex_EPoll
    ZiConnectFn takeFn() { return ZuMv(m_fn); }
#endif
    const ZiCxnInfo &info() const { return m_info; }
    ZiCxnInfo &info() { return m_info; }

#ifdef ZiMultiplex_IOCP
    Zi_Overlapped &overlapped() { return m_overlapped; }

    void executed(int status, unsigned n, ZeError e) {
      m_mx->overlappedConnect(this, status, n, e);
      delete this;
    }
#endif

    ZiMultiplex		*m_mx;
    ZiConnectFn		m_fn;
    ZiFailFn		m_failFn;
    ZiCxnInfo		m_info;
#ifdef ZiMultiplex_IOCP
    Zi_Overlapped	m_overlapped;
#endif
  };
#if ZiMultiplex__ConnectHash
  ZmHashDerive(ConnectHash, Connect_,
    (ZmHashNode<Connect_,
      ZmHashKey<Connect_::SocketAxor,
	ZmHashHeapID<"ZiMultiplex.Connect">>>));
  using Connect = ConnectHash::Node;
#else

  ZuDerive(Connect, (Connect_));
#endif

  ZmHashDerive(CxnHash, ZmRef<ZiConnection>,
    (ZmHashKey<ZiConnection::SocketAxor,
	ZmHashHeapID<"ZiMultiplex.Connection">>));

public:
  using Socket = Zi::Socket;

  ZiMultiplex(ZiMxParams mxParams = ZiMxParams{});
  ~ZiMultiplex();

  ZiMultiplex(const ZiMultiplex &) = delete;
  ZiMultiplex &operator =(const ZiMultiplex &) = delete;

  // main API
  void listen(
      ZiListenFn listenFn, ZiFailFn failFn, ZiConnectFn acceptFn,
      ZiIP localIP, uint16_t localPort, unsigned nAccepts,
      Zi::IOBufAllocator *rxAllocator, ZiCxnOptions options = ZiCxnOptions());
  void listen_(							// Rx thread
      ZiListenFn listenFn, ZiFailFn failFn, ZiConnectFn acceptFn,
      ZiIP localIP, uint16_t localPort, unsigned nAccepts,
      Zi::IOBufAllocator *rxAllocator, ZiCxnOptions options = ZiCxnOptions());
  void stopListening(ZiIP localIP, uint16_t localPort);
  void stopListening_(ZiIP localIP, uint16_t localPort);	// Rx thread

  void connect(
      ZiConnectFn fn, ZiFailFn failFn,
      ZiIP localIP, uint16_t localPort,
      ZiIP remoteIP, uint16_t remotePort,
      Zi::IOBufAllocator *rxAllocator, ZiCxnOptions options = ZiCxnOptions());
  void connect_(						// Rx thread
      ZiConnectFn fn, ZiFailFn failFn,
      ZiIP localIP, uint16_t localPort,
      ZiIP remoteIP, uint16_t remotePort,
      Zi::IOBufAllocator *rxAllocator, ZiCxnOptions options = ZiCxnOptions());

  void udp(
      ZiConnectFn fn, ZiFailFn failFn,
      ZiIP localIP, uint16_t localPort,
      ZiIP remoteIP, uint16_t remotePort,
      Zi::IOBufAllocator *rxAllocator, ZiCxnOptions options = ZiCxnOptions());
  void udp_(							// Rx thread
      ZiConnectFn fn, ZiFailFn failFn,
      ZiIP localIP, uint16_t localPort,
      ZiIP remoteIP, uint16_t remotePort,
      Zi::IOBufAllocator *rxAllocator, ZiCxnOptions options = ZiCxnOptions());

  bool stop();

  // register allocators and any plugin while stopped, before start() and I/O use
  template <
    Zi::IOBufType::T Type = Zi::IOBufType::App,
    typename Base = Zi::IOBuf,
    unsigned Size = ZiIOBuf_DefltSize,
    unsigned MaxSize = ZiIOBuf_DefltMaxSize,
    unsigned Align = ZiIOBuf_DefltAlign,
    typename HeapID = ZiIOBuf_HeapID>
  Zi::IOBufAllocator *allocator(unsigned count = ZiIOBuf_DefltCount) {
    ZmAssert(state() == ZmEngineState::Stopped);
    Zi::IOBufAllocator *a;
    if (m_plugin)
      a = new Zi::IOBufPluginAllocator<Type, Size, Base>(
	this, m_plugin->allocator(Type, Size, MaxSize, Align, count));
    else
      a = new Zi::IOBufBuiltinAllocator<Type, Base, Size, MaxSize, Align, HeapID>(this);
    m_allocators.pushNode(a);
    return a;
  }

  void plugin(ZmRef<ZiMxPlugin> plugin);
  ZiMxPlugin *plugin() const { return m_plugin; }

  // threads
  unsigned rxThread() const { return m_rxThread; }
  unsigned txThread() const { return m_txThread; }

  template <typename ...Args> void rxRun(Args &&...args) {
    run(ZuFwd<Args>(args)..., m_rxThread);
  }
  template <typename ...Args> void rxInvoke(Args &&...args) {
    invoke(ZuFwd<Args>(args)..., m_rxThread);
  }
  template <typename ...Args> void txRun(Args &&...args) {
    run(ZuFwd<Args>(args)..., m_txThread);
  }
  template <typename ...Args> void txInvoke(Args &&...args) {
    invoke(ZuFwd<Args>(args)..., m_txThread);
  }

  // plugin callbacks
  void pluginStarted(ZiMxPlugin::StartResult);
  void pluginStopped(ZiMxPlugin::StopResult);

  // telemetry
  unsigned allCxns(Ztc::Mx::AllCxnsFn fn) const override;
  unsigned allQueues(Ztc::QueueMgr::AllFn fn) const override;
  void watch(Ztc::Mx::AddCxnFn, Ztc::Mx::DelCxnFn) override;
  void unwatch() override;

private:
  unsigned allCxns_(Ztc::Mx::AllCxnsFn fn) const;

public:
#ifdef ZiMultiplex_DEBUG
  template <typename S> void traceDump(S &s) { m_tracer.dump(s); }
  bool trace() const { return m_trace; }
  void trace(bool b) { m_trace = b; }
  bool debug() const { return m_debug; }
  void debug(bool b) { m_debug = b; }
  bool frag() const { return m_frag; }
  void frag(bool b) { m_frag = b; }
  bool yield() const { return m_yield; }
  void yield(bool b) { m_yield = b; }
#endif
#ifdef ZiMultiplex_FILTER
  void rxFilter(FilterFn fn) { m_rxFilter = ZuMv(fn); }
  void txFilter(FilterFn fn) { m_txFilter = ZuMv(fn); }
#endif

  unsigned rxBufSize() const { return m_rxBufSize; }
  unsigned txBufSize() const { return m_txBufSize; }
  unsigned rxQuantum() const { return m_rxQuantum; }
#ifdef ZiMultiplex_EPoll
  unsigned epollMaxFDs() const { return m_epollMaxFDs; }
#endif

  TelKey telKey() const override { return {id()}; }
  void telemetry(Ztc::MxTelemetry &data) const override;

private:

#ifdef ZiMultiplex_FILTER
  bool filterRx(ZiConnection *cxn, uint8_t *buf, unsigned n) {
    return m_rxFilter(cxn, buf, n);
  }
  bool filterTx(ZiConnection *cxn, uint8_t *buf, unsigned n) {
    return m_txFilter(cxn, buf, n);
  }
#endif

  bool start__() override;
  bool stop__() override;

  void stop_0();	// Rx thread - disconnect all connections
  void stop_1();	// Rx thread - stop connecting / listening / accepting
  void stop_2();	// App thread - clean up

  void busy() { ZmScheduler::busy(); }
  void idle() { ZmScheduler::idle(); }

  void rx();			// handle I/O completions (IOCP) or
				// readiness notifications (epoll, ports, etc.)
  void wake();			// wake up rx(), cause it to return
  void wakeRx();		// re-run rx() after wake()


#ifdef ZiMultiplex_EPoll
  void connect(Connect *);
#endif
#ifdef ZiMultiplex_IOCP
  void overlappedConnect(Connect *, int status, unsigned, ZeError e);
#endif
  void executedConnect(ZiConnectFn, const ZiCxnInfo &);

  void accept(Listener *);
#ifdef ZiMultiplex_IOCP
  void overlappedAccept(Accept *, int status, unsigned n, ZeError e);
#endif

  void disconnected(ZiConnection *cxn);

#ifdef ZiMultiplex_EPoll
  bool epollRecv(ZiConnection *, int s, uint32_t events);
#endif

  bool initSocket(Socket, const ZiCxnOptions &);

  bool cxnAdd(ZiConnection *, Socket);
  void cxnDel(Socket);

  bool listenerAdd(Listener *, Socket);
  void listenerDel(Socket);

  bool connectAdd(Connect *, Socket);
#ifdef ZiMultiplex_EPoll
  void completedConnect(Connect *);
#endif
  void connectDel(Socket);

#ifdef ZiMultiplex_EPoll
  bool readWake();
  void writeWake();
#endif

#ifdef ZiMultiplex_DEBUG
  void traceCapture() { m_tracer.capture(1); }
#endif

  // immutable
  ZmRef<ZiMxPlugin>	m_plugin = 0;
  unsigned		m_rxThread = 0;
  unsigned		m_txThread = 0;
  unsigned		m_rxBufSize = 0; // setsockopt SO_RCVBUF option
  unsigned		m_txBufSize = 0; // setsockopt SO_SNDBUF option
  unsigned		m_rxQuantum = ZiMxParams::RxQuantum;
#ifdef ZiMultiplex_EPoll
  unsigned		m_epollMaxFDs = 0;
#endif

  // mutable shared
  ZmSemaphore		*m_starting = nullptr;
  ZmSemaphore		*m_stopping = nullptr;
  ZmRWLock		m_cxnWatchLock;
  Ztc::Mx::AddCxnFn	m_addCxnFn;
  Ztc::Mx::DelCxnFn	m_delCxnFn;
  bool			m_pluginFailed = false;

#ifdef ZiMultiplex_DEBUG
  bool			m_trace = false;
  bool			m_debug = false;
  bool			m_frag = false;
  bool			m_yield = false;
  ZmBackTracer<64>	m_tracer;
#endif

  // Rx thread exclusive
#ifdef ZiMultiplex_IOCP
  alignas(Zm::CacheLineSize)
  HANDLE		m_completionPort = INVALID_HANDLE_VALUE;
#endif
#ifdef ZiMultiplex_EPoll
  alignas(Zm::CacheLineSize)
  int			m_epollFD = -1;
  int			m_wakeFD = -1;
#endif
  // Registration is stopped-only; the app serializes it with lifecycle changes.
  Zi::IOBufAllocatorList m_allocators;
  ZmRef<ListenerHash>	m_listeners;
#if ZiMultiplex__ConnectHash
  ZmRef<ConnectHash>	m_connects;
#endif
  ZmRef<CxnHash>	m_cxns;	// connections
  unsigned		m_nAccepts = 0; // total #accepts for all listeners
#ifdef ZiMultiplex_FILTER
  FilterFn		m_rxFilter;
#endif

  // Tx thread exclusive
#ifdef ZiMultiplex_FILTER
  alignas(Zm::CacheLineSize)
  FilterFn		m_txFilter;
#endif
};

#endif /* ZiMultiplex_HH */
