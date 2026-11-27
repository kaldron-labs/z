//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <string.h>

#ifdef linux
#include <errno.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>

#include "ZiTestPorts.hh"

using namespace ZuTestUtil;


class LoopMx;
class UdpTOSMx;

class LoopCxn : public ZmNHeap<LoopCxn>, public ZiConnection {
friend inline ZuStringT<"ZiMxLoopTest.LoopCxn"> ZmNHeapID(LoopCxn *);
  static constexpr uint8_t Msg[4] = { 'P', 'I', 'N', 'G' };

public:
  LoopCxn(LoopMx *mx, const ZiCxnInfo &ci);
  ~LoopCxn() = default;

  void connected(ZiRxContext &io) override;
  void disconnected(bool) override;

private:
  bool sendClient(ZiTxContext &io);
  bool sendClientDone(ZiTxContext &io);
  bool recvClient(ZiRxContext &io);

  bool recvServer(ZiRxContext &io);
  bool sendServer(ZiTxContext &io);
  bool sendServerDone(ZiTxContext &io);

private:
  LoopMx		*m_owner = nullptr;
  bool		m_client = false;
};

class UdpTOSCxn : public ZmNHeap<UdpTOSCxn>, public ZiConnection {
friend inline ZuStringT<"ZiMxLoopTest.UdpTOSCxn"> ZmNHeapID(UdpTOSCxn *);
  static constexpr uint8_t Msg[4] = { 'T', 'O', 'S', '!' };

public:
  UdpTOSCxn(UdpTOSMx *mx, const ZiCxnInfo &ci);
  ~UdpTOSCxn() = default;

  void connected(ZiRxContext &io) override;
  void disconnected(bool) override;

private:
  bool send_(ZiTxContext &io);
  bool sendDone_(ZiTxContext &io);
  bool recv_(ZiRxContext &io);

private:
  UdpTOSMx	*m_owner = nullptr;
  bool		m_client = false;
};

class LoopMx : public ZiMultiplex {
public:
  using RxAllocator = Zi::IOBufAllocator;
  using TxAllocator = Zi::IOBufAllocator;

  LoopMx(ZiIP loopIP, unsigned port) :
    m_loopIP{loopIP}, m_listenPort{port} {
    m_registered = state() == ZmEngineState::Stopped &&
      m_rx && m_rx->mx == this && m_tx && m_tx->mx == this;
  }
  ~LoopMx() = default;

  RxAllocator *rxAllocator() const { return m_rx; }
  TxAllocator *txAllocator() const { return m_tx; }

  ZiConnection *connected(const ZiCxnInfo &ci)
  {
    if (!ci.mps || ci.mps == ~uint32_t(0)) m_mpsOK = 0;
    auto cxn = new LoopCxn(this, ci);
    if (ci.type == ZiCxnType::TCPOut)
      m_clientCxn = cxn;
    else
      m_serverCxn = cxn;
    return cxn;
  }

  void listening(const ZiListenInfo &info)
  {
    m_listenPort = info.port;
    connectLoopback();
  }

  void failed(bool transient)
  {
	    if (transient && m_listenPort && m_retries < 3) {
	      ++m_retries;
	      add(&m_retryTimer, Zm::now(1), ZmScheduler::Update,
		  [this](auto &&arm) {
		    return arm([this]() { connectLoopback(); });
		  });
	      return;
	    }
    m_failKind = 1;
    m_failed = 1;
    m_done.post();
  }

  void startLoopback()
  {
    if (!m_registered.load_()) { markIOFailure(); return; }
    ZiMultiplex::listen(
      ZiListenFn{this, ZmFnPtr<&LoopMx::listening>{}},
      ZiFailFn{this, ZmFnPtr<&LoopMx::failed>{}},
      ZiConnectFn{this, ZmFnPtr<&LoopMx::connected>{}},
      m_loopIP, m_listenPort, 1,
      m_rx, ZiCxnOptions());
  }

  bool registered_() const { return m_registered.load_(); }
  bool singleRegistration_() const {
    return state() == ZmEngineState::Stopped &&
      m_rx && m_tx && m_rx->mx == this && m_tx->mx == this;
  }

  bool waitDone(int seconds)
  {
    return m_done.timedwait(Zm::now(seconds)) == 0;
  }
  void repeatDisconnects()
  {
    if (m_clientCxn) {
      m_clientCxn->disconnect();
      m_clientCxn->disconnect();
    }
    if (m_serverCxn) {
      m_serverCxn->disconnect();
      m_serverCxn->disconnect();
    }
  }

  void markClientEcho(bool ok)
  {
    if (!ok) {
      m_failKind = 3;
      m_failed = 1;
      m_done.post();
      return;
    }
    m_clientEcho = 1;
    maybeDone();
  }

  void markClientDisconnected(bool peer)
  {
    markDisconnected(peer);
    m_clientDisc = 1;
    maybeDone();
  }

  void markServerDisconnected(bool peer)
  {
    markDisconnected(peer);
    m_serverDisc = 1;
    maybeDone();
  }

  void markIOFailure()
  {
    m_failKind = 2;
    m_failed = 1;
    m_done.post();
  }

  bool failed_() const { return m_failed.load_(); }
  unsigned failKind_() const { return m_failKind.load_(); }
  bool clientEcho_() const { return m_clientEcho.load_(); }
  bool mpsOK_() const { return m_mpsOK.load_(); }
  bool clientDisc_() const { return m_clientDisc.load_(); }
  bool serverDisc_() const { return m_serverDisc.load_(); }
  unsigned disconnects_() const { return m_disconnects.load_(); }
  unsigned peerDisconnects_() const { return m_peerDisconnects.load_(); }
  unsigned localDisconnects_() const { return m_localDisconnects.load_(); }

private:
  void connectLoopback()
  {
    ZiMultiplex::connect(
      ZiConnectFn{this, ZmFnPtr<&LoopMx::connected>{}},
      ZiFailFn{this, ZmFnPtr<&LoopMx::failed>{}},
      m_loopIP, 0,
      m_loopIP, m_listenPort,
      m_rx, ZiCxnOptions());
  }

  void maybeDone()
  {
    if (m_failed.load_()) {
      m_done.post();
      return;
    }
    if (m_clientEcho.load_() && m_clientDisc.load_() && m_serverDisc.load_())
      m_done.post();
  }
  void markDisconnected(bool peer)
  {
    ++m_disconnects;
    if (peer)
      ++m_peerDisconnects;
    else
      ++m_localDisconnects;

  }

private:
  RxAllocator *m_rx = allocator<Zi::IOBufType::Rx, ZiIOBuf, 4>();
  TxAllocator *m_tx = allocator<Zi::IOBufType::TxTCP, ZiIOBuf, 4>();
  ZmAtomic<unsigned> m_registered = 0;
	  ZmSemaphore		m_done;
	  ZmScheduler::Timer	m_retryTimer;
  ZiIP			m_loopIP;
	  unsigned		m_listenPort = 0;
  unsigned		m_retries = 0;
  ZmRef<LoopCxn>	m_clientCxn;
  ZmRef<LoopCxn>	m_serverCxn;
  ZmAtomic<unsigned>	m_failed = 0;
  ZmAtomic<unsigned>	m_failKind = 0;
  ZmAtomic<unsigned>	m_clientEcho = 0;
  ZmAtomic<unsigned>	m_mpsOK = 1;
  ZmAtomic<unsigned>	m_clientDisc = 0;
  ZmAtomic<unsigned>	m_serverDisc = 0;
  ZmAtomic<unsigned>	m_disconnects = 0;
  ZmAtomic<unsigned>	m_peerDisconnects = 0;
  ZmAtomic<unsigned>	m_localDisconnects = 0;
};

class UdpTOSMx : public ZiMultiplex {
public:
  using RxAllocator = Zi::IOBufAllocator;
  using TxAllocator = Zi::IOBufAllocator;

  UdpTOSMx(ZiIP loopIP, unsigned port, ZiIP clientIP = {}) :
    m_loopIP{loopIP}, m_clientIP{!!clientIP ? clientIP : loopIP},
    m_serverPort{uint16_t(port)} {
    m_registered = state() == ZmEngineState::Stopped &&
      m_rx && m_rx->mx == this && m_tx && m_tx->mx == this;
  }
  ~UdpTOSMx() = default;

  RxAllocator *rxAllocator() const { return m_rx; }
  TxAllocator *txAllocator() const { return m_tx; }

  ZiConnection *connected(const ZiCxnInfo &ci)
  {
    if (!!ci.remote ? (!ci.mps || ci.mps == ~uint32_t(0)) : ci.mps != ~uint32_t(0))
      m_mpsOK = 0;
    auto cxn = new UdpTOSCxn(this, ci);
    if (!!ci.remote)
      m_clientCxn = cxn;
    else
      m_serverCxn = cxn;
    return cxn;
  }

  void failed(bool)
  {
    m_failed = 1;
    m_done.post();
  }

  void startServer()
  {
    if (!m_registered.load_()) { ioFailure(); return; }
    ZiCxnOptions options;
    options.udp(true);
    udp(
      ZiConnectFn{this, ZmFnPtr<&UdpTOSMx::connected>{}},
      ZiFailFn{this, ZmFnPtr<&UdpTOSMx::failed>{}},
      m_loopIP, m_serverPort, ZiIP{}, 0, m_rx, options);
  }

  bool registered_() const { return m_registered.load_(); }
  bool singleRegistration_() const {
    return state() == ZmEngineState::Stopped &&
      m_rx && m_tx && m_rx->mx == this && m_tx->mx == this;
  }

  void serverReady(ZiConnection *cxn)
  {
    ZiSockAddr addr;
    addr.init(m_loopIP.type());
    socklen_t len = addr.len();
    if (::getsockname(cxn->info().socket, addr.sa(), &len) < 0) {
      failed(false);
      return;
    }
    addr.sync();
    m_serverPort = addr.port();
    startClient();
  }

  void recvTOS(ZiTOS tos, bool payloadOK)
  {
    m_payloadOK = payloadOK;
    if (tos.template is<uint8_t>()) {
      m_haveTOS = 1;
      m_tos = tos.template p<uint8_t>();
    }
    m_done.post();
  }

  void ioFailure()
  {
    m_failed = 1;
    m_done.post();
  }

  bool waitDone(int seconds)
  {
    return m_done.timedwait(Zm::now(seconds)) == 0;
  }

  bool failed_() const { return m_failed.load_(); }
  bool payloadOK_() const { return m_payloadOK.load_(); }
  bool mpsOK_() const { return m_mpsOK.load_(); }
  bool haveTOS_() const { return m_haveTOS.load_(); }
  uint8_t tos_() const { return m_tos; }

private:
  void startClient()
  {
    ZiCxnOptions options;
    options.udp(true);
    udp(
      ZiConnectFn{this, ZmFnPtr<&UdpTOSMx::connected>{}},
      ZiFailFn{this, ZmFnPtr<&UdpTOSMx::failed>{}},
      m_clientIP, 0, m_clientIP, m_serverPort, m_rx, options);
  }

private:
  RxAllocator *m_rx = allocator<Zi::IOBufType::Rx, ZiIOBuf, 4>();
  TxAllocator *m_tx = allocator<Zi::IOBufType::TxUDP, ZiIOBuf, 4>();
  ZmAtomic<unsigned> m_registered = 0;
  ZmSemaphore		m_done;
  ZiIP			m_loopIP;
  ZiIP			m_clientIP;
  uint16_t		m_serverPort = 0;
  ZmRef<UdpTOSCxn>	m_serverCxn;
  ZmRef<UdpTOSCxn>	m_clientCxn;
  ZmAtomic<unsigned>	m_failed = 0;
  ZmAtomic<unsigned>	m_payloadOK = 0;
  ZmAtomic<unsigned>	m_mpsOK = 1;
  ZmAtomic<unsigned>	m_haveTOS = 0;
  uint8_t		m_tos = 0;
};

LoopCxn::LoopCxn(LoopMx *mx, const ZiCxnInfo &ci) :
    ZiConnection(mx, ci),
    m_owner(mx),
    m_client(ci.type == ZiCxnType::TCPOut)
{
}

void LoopCxn::connected(ZiRxContext &io)
{
  auto buf = m_owner->rxAllocator()->allocBuf(
    this, 0, info().mps);
  if (ZuUnlikely(!buf)) {
    m_owner->markIOFailure(); io.disconnect(); return;
  }
  if (m_client) {
    io.init(
      ZiRxFn{this, ZmFnPtr<&LoopCxn::recvClient>{}},
      ZuMv(buf));
    send(ZiTxFn{this, ZmFnPtr<&LoopCxn::sendClient>{}});
  } else {
    io.init(
      ZiRxFn{this, ZmFnPtr<&LoopCxn::recvServer>{}},
      ZuMv(buf));
  }
}

void LoopCxn::disconnected(bool peer)
{
  if (m_client)
    m_owner->markClientDisconnected(peer);
  else
    m_owner->markServerDisconnected(peer);
}

bool LoopCxn::sendClient(ZiTxContext &io)
{
  auto buf = m_owner->txAllocator()->allocBuf(
    this, 0, info().mps);
  if (ZuUnlikely(!buf)) {
    m_owner->markIOFailure(); io.close(); return true;
  }
  buf->append(ZuBSpan{Msg, sizeof(Msg)});
  io.init(
    ZiTxFn{this, ZmFnPtr<&LoopCxn::sendClientDone>{}},
    ZuMv(buf));
  return true;
}

bool LoopCxn::sendClientDone(ZiTxContext &io)
{
  if (io.length < 0) {
    m_owner->markIOFailure();
    io.close();
    return true;
  }
  if ((io.offset += io.length) < io.buf->length) return true;
  io.complete();
  return true;
}

bool LoopCxn::recvClient(ZiRxContext &io)
{
  if (io.buf->length < sizeof(Msg)) return true;

  m_owner->markClientEcho(!::memcmp(io.buf->data(), Msg, sizeof(Msg)));
  io.disconnect();
  return true;
}

bool LoopCxn::recvServer(ZiRxContext &io)
{
  if (io.buf->length < sizeof(Msg)) return true;

  if (::memcmp(io.buf->data(), Msg, sizeof(Msg))) {
    m_owner->markIOFailure();
    io.disconnect();
    return true;
  }
  send(ZiTxFn{this, ZmFnPtr<&LoopCxn::sendServer>{}});
  io.complete();
  return true;
}

bool LoopCxn::sendServer(ZiTxContext &io)
{
  auto buf = m_owner->txAllocator()->allocBuf(
    this, 0, info().mps);
  if (ZuUnlikely(!buf)) {
    m_owner->markIOFailure(); io.close(); return true;
  }
  buf->append(ZuBSpan{Msg, sizeof(Msg)});
  io.init(
    ZiTxFn{this, ZmFnPtr<&LoopCxn::sendServerDone>{}},
    ZuMv(buf));
  io.disconnect();
  return true;
}

bool LoopCxn::sendServerDone(ZiTxContext &io)
{
  if (io.length < 0) {
    m_owner->markIOFailure();
    io.close();
    return true;
  }
  if ((io.offset += io.length) < io.buf->length) return true;

  io.complete();
  return true;
}

UdpTOSCxn::UdpTOSCxn(UdpTOSMx *mx, const ZiCxnInfo &ci) :
    ZiConnection(mx, ci),
    m_owner(mx),
    m_client(!!ci.remote)
{
}

void UdpTOSCxn::connected(ZiRxContext &io)
{
  if (m_client) {
    send(ZiTxFn{this, ZmFnPtr<&UdpTOSCxn::send_>{}});
    io.complete();
  } else {
    auto buf = m_owner->rxAllocator()->allocBuf(
      this, 0, info().mps);
    if (ZuUnlikely(!buf)) {
      m_owner->ioFailure(); io.disconnect(); return;
    }
    io.init(
      ZiRxFn{this, ZmFnPtr<&UdpTOSCxn::recv_>{}},
      ZuMv(buf));
    m_owner->serverReady(this);
  }
}

void UdpTOSCxn::disconnected(bool)
{
}

bool UdpTOSCxn::send_(ZiTxContext &io)
{
  auto buf = m_owner->txAllocator()->allocBuf(
    this, 0, info().mps);
  if (ZuUnlikely(!buf)) {
    m_owner->ioFailure(); io.close(); return true;
  }
  buf->append(ZuBSpan{Msg, sizeof(Msg)});
  io.init(
    ZiTxFn{this, ZmFnPtr<&UdpTOSCxn::sendDone_>{}},
    ZuMv(buf));
  io.tos = uint8_t(0x2e);
  return true;
}

bool UdpTOSCxn::sendDone_(ZiTxContext &io)
{
  if (io.length < 0) {
    m_owner->ioFailure();
    io.close();
    return true;
  }
  if ((io.offset += io.length) < io.buf->length) return true;
  io.disconnect();
  return true;
}

bool UdpTOSCxn::recv_(ZiRxContext &io)
{
  bool payloadOK = io.buf->length == sizeof(Msg) &&
    !::memcmp(io.buf->data(), Msg, sizeof(Msg));
  m_owner->recvTOS(io.tos, payloadOK);
  io.disconnect();
  return true;
}

static void testMulticastOptions()
{
  ZuTestScope(testMulticastOptions);

  ZiMReq v4{ZiIP{"224.0.0.1"}, ZiIP{"0.0.0.0"}};
  ZuCheck(v4.type() == ZiIPType::V4);
  ZuCheck(v4.addr() == ZiIP{"224.0.0.1"});
  ZuCheck(v4.mif() == ZiIP{"0.0.0.0"});
  ZuCheck(v4.ifIndex() == 0);
  ZuCheck(!!v4);

  ZiMReq v4b{ZiIP{"224.0.0.2"}, ZiIP{"0.0.0.0"}};
  ZuCheck(v4 != v4b);
  ZuCheck(v4.cmp(v4b) != 0);
  ZuCheck(v4.hash() != v4b.hash());
  ZtString<> s;
  s << v4;
  ZuCheck(s == "224.0.0.1->0.0.0.0");

  ZiMReq v6{ZiIP{"ff01::1"}, 3};
  ZuCheck(v6.type() == ZiIPType::V6);
  ZuCheck(v6.addr() == ZiIP{"ff01::1"});
  ZuCheck(!v6.mif());
  ZuCheck(v6.ifIndex() == 3);
  ZuCheck(!!v6);

  ZiMReq v6b{ZiIP{"ff01::2"}, 3};
  ZuCheck(v6 != v6b);
  ZuCheck(v6.cmp(v6b) != 0);
  ZuCheck(v6.hash() != v6b.hash());
  s.null();
  s << v6;
  ZuCheck(s == "ff01::1->3");

  ZiMReq mixed1{ZiIP{"ff01::1"}, ZiIP{"0.0.0.0"}};
  ZiMReq mixed2{ZiIP{"224.0.0.1"}, 3};
  ZuCheck(!mixed1);
  ZuCheck(!mixed2);

  ZiCxnOptions v4Opts;
  v4Opts.udp(true).multicast(true).loopBack(true).mif(ZiIP{"0.0.0.0"}).ttl(4);
  v4Opts.mreq(v4);
  ZuCheck(v4Opts.multicastValid(ZiIPType::V4));
  ZuCheck(!v4Opts.multicastValid(ZiIPType::V6));

  ZiCxnOptions v6Opts;
  v6Opts.udp(true).multicast(true).loopBack(true).mifIndex(3).ttl(4);
  v6Opts.mreq(v6);
  ZuCheck(v6Opts.multicastValid(ZiIPType::V6));
  ZuCheck(!v6Opts.multicastValid(ZiIPType::V4));
  ZuCheck(v4Opts != v6Opts);
  ZuCheck(v4Opts.cmp(v6Opts) != 0);
  ZuCheck(v4Opts.hash() != v6Opts.hash());
}

struct LoopResult {
  bool		started = false;
  bool		telemetry = false;
  bool		registered = false;
  bool		singleRegistration = false;
  bool		done = false;
  bool		failed = false;
  unsigned	failKind = 0;
  bool		clientEcho = false;
  bool		mpsOK = false;
  bool		clientDisc = false;
  bool		serverDisc = false;
  unsigned	disconnects = 0;
  unsigned	peerDisconnects = 0;
  unsigned	localDisconnects = 0;
};

struct MxCheck {
  Ztc::Mx	*target;
  bool		found = false;
  unsigned	visited = 0;
};

static LoopResult runTcpLoopbackAndTelemetry(ZiIP loopIP, unsigned port)
{
  LoopResult result;
  LoopMx mx{loopIP, port};
  result.started = mx.start();

  Ztc::MxTelemetry telemetry{};
  mx.telemetry(telemetry);
  MxCheck check{&mx};
  unsigned allMxs = Ztc::MxMgr::all(Ztc::MxMgr::AllFn{
    &check, [](MxCheck *check, Ztc::Mx *mx) {
      ++check->visited;
      if (mx == check->target) check->found = true;
    }});
  result.telemetry =
    telemetry.nThreads >= 1 && mx.telKey().p<0>() == telemetry.id && check.found &&
    allMxs == check.visited;

  mx.startLoopback();

  result.done = mx.waitDone(5);
  result.registered = mx.registered_();

  // Some constrained sandboxes deny loopback socket setup; gate integration
  // assertions to keep the suite deterministic across environments.
  result.failed = mx.failed_();
  result.failKind = mx.failKind_();
  if (result.failed) return result;

  result.clientEcho = mx.clientEcho_();
  result.mpsOK = mx.mpsOK_();
  result.clientDisc = mx.clientDisc_();
  result.serverDisc = mx.serverDisc_();
  mx.repeatDisconnects();
  mx.stop(); // drain both owners before counting notifications
  result.disconnects = mx.disconnects_();
  result.peerDisconnects = mx.peerDisconnects_();
  result.localDisconnects = mx.localDisconnects_();
  result.singleRegistration = mx.singleRegistration_();
  return result;
}

#define CheckTcpLoopback(result) do { \
  ZuCheck((result).started); \
  ZuCheck((result).telemetry); \
  ZuCheck((result).done); \
  ZuCheck((result).registered); \
  if ((result).failed) { \
    ZuCheck((result).failKind == 1); \
    return; \
  } \
  ZuCheck((result).singleRegistration); \
  ZuCheck((result).clientEcho); \
  ZuCheck((result).mpsOK); \
  ZuCheck((result).clientDisc); \
  ZuCheck((result).serverDisc); \
  ZuCheck((result).disconnects == 2); \
  ZuCheck((result).peerDisconnects == 0); \
  ZuCheck((result).localDisconnects == 2); \
} while (0)

static void testTcpLoopbackIPv4()
{
  ZuTestScope(testTcpLoopbackIPv4);

  auto result = runTcpLoopbackAndTelemetry(
    ZiIP{"127.0.0.1"}, ZiTestPort::MxLoopTCP4);
  CheckTcpLoopback(result);
}

static void testTcpLoopbackIPv6()
{
  ZuTestScope(testTcpLoopbackIPv6);

  auto result = runTcpLoopbackAndTelemetry(
    ZiIP{"::1"}, ZiTestPort::MxLoopTCP6);
  CheckTcpLoopback(result);
}

static void testUdpTOSIPv4()
{
  ZuTestScope(testUdpTOSIPv4);

  UdpTOSMx mx{ZiIP{"127.0.0.1"}, ZiTestPort::MxLoopUDP4};
  ZuCheck(mx.start());
  mx.startServer();
  ZuCheck(mx.waitDone(5));
  mx.stop();

  ZuCheck(mx.registered_());
  ZuCheck(mx.singleRegistration_());
  ZuCheck(!mx.failed_());
  ZuCheck(mx.payloadOK_());
  ZuCheck(mx.mpsOK_());
  ZuCheck(mx.haveTOS_());
  ZuCheck(mx.tos_() == 0x2e);
}

static void testUdpTOSMappedIPv4()
{
  ZuTestScope(testUdpTOSMappedIPv4);

  UdpTOSMx mx{ZiIP{"::"}, 0, ZiIP{"127.0.0.1"}};
  ZuCheck(mx.start());
  mx.startServer();
  ZuCheck(mx.waitDone(5));
  mx.stop();

  ZuCheck(!mx.failed_());
  ZuCheck(mx.payloadOK_());
  ZuCheck(mx.haveTOS_());
  ZuCheck(mx.tos_() == 0x2e);
}

static void testUdpMPSIPv6()
{
  ZuTestScope(testUdpMPSIPv6);

  UdpTOSMx mx{ZiIP{"::1"}, ZiTestPort::MxLoopUDP6};
  ZuCheck(mx.start());
  mx.startServer();
  ZuCheck(mx.waitDone(5));
  mx.stop();

  ZuCheck(!mx.failed_());
  ZuCheck(mx.payloadOK_());
  ZuCheck(mx.mpsOK_());
}

template <int Type> struct EndpointRxBuf : ZiIOBuf {
  using ZiIOBuf::ZiIOBuf;
};

struct EndpointRxCheck {
  ZiMultiplex	&mx;
  Zi::IOBufAllocator *rx[ZiCxnType::N];
  ZmSemaphore	done;
  bool		listenerOK = false;
  bool		connectedOK[ZiCxnType::N] = {};
  bool		failed = false;

  void fail(bool) {
    failed = true;
    done.post();
  }

  template <int Type>
  ZiConnection *connected(const ZiCxnInfo &info) {
    auto allocator = info.rxAllocator;
    if (info.type == Type && allocator == rx[Type]) {
      auto buf = allocator->allocBuf(this);
      connectedOK[Type] = buf && buf->allocator() == allocator && buf->owner == this &&
	dynamic_cast<EndpointRxBuf<Type> *>(buf.ptr()) && buf->builtin() && buf->data_();
    }
    done.post();
    return nullptr;
  }

  void listening(const ZiListenInfo &info) {
    listenerOK = info.rxAllocator == rx[ZiCxnType::TCPIn];
    done.post();
    mx.connect(
      {this, ZmFnPtr<&EndpointRxCheck::connected<ZiCxnType::TCPOut>>{}},
      {this, ZmFnPtr<&EndpointRxCheck::fail>{}},
      info.ip, 0, info.ip, info.port, rx[ZiCxnType::TCPOut]);
  }
};

static void testEndpointRxAllocators()
{
  ZuTestScope(testEndpointRxAllocators);
  using namespace ZiCxnType;
  using Zi::IOBufType::Rx;
  ZiMultiplex mx;
  EndpointRxCheck check{mx, {
    mx.allocator<Rx, EndpointRxBuf<TCPIn>, 32>(),
    mx.allocator<Rx, EndpointRxBuf<TCPOut>, 64>(),
    mx.allocator<Rx, EndpointRxBuf<UDP>, 128>()}};
  auto app = mx.allocator<>();
  auto rxAgain = mx.allocator<Rx, EndpointRxBuf<TCPIn>, 32>();
  ZuCheck(check.rx[TCPIn] && check.rx[TCPOut] && check.rx[UDP] && app && rxAgain);
  ZuCheck(check.rx[TCPIn] != check.rx[TCPOut] && check.rx[TCPIn] != check.rx[UDP] &&
    check.rx[TCPOut] != check.rx[UDP] && check.rx[TCPIn] != rxAgain);
  if (!check.rx[TCPIn] || !check.rx[TCPOut] || !check.rx[UDP] || !app) return;
  ZuCheck(!mx.alloc().is<ZeException>());
  ZuCheck((!mx.allocator<Rx, EndpointRxBuf<TCPIn>, 32>()));
  ZuCheck(!mx.allocator<>());
  bool started = mx.start();
  ZuCheck(started);
  if (!started) return;
  ZuCheck(!mx.allocator<Rx>());
  ZiIP loopback{"127.0.0.1"};
  mx.listen(
    {&check, ZmFnPtr<&EndpointRxCheck::listening>{}},
    {&check, ZmFnPtr<&EndpointRxCheck::fail>{}},
    {&check, ZmFnPtr<&EndpointRxCheck::connected<TCPIn>>{}},
    loopback, 0, 1, check.rx[TCPIn]);
  mx.udp(
    {&check, ZmFnPtr<&EndpointRxCheck::connected<UDP>>{}},
    {&check, ZmFnPtr<&EndpointRxCheck::fail>{}},
    loopback, 0, {}, 0, check.rx[UDP], ZiCxnOptions{}.udp(true));
  auto deadline = Zm::now(5);
  for (unsigned i = 0; i < N + 1; ++i) {
    bool done = check.done.timedwait(deadline) == 0;
    ZuCheck(done);
    if (!done) break;
  }
  ZuCheck(mx.stop());
  ZuCheck(!check.failed);
  ZuCheck(check.listenerOK);
  ZuCheck(check.connectedOK[TCPIn]);
  ZuCheck(check.connectedOK[TCPOut]);
  ZuCheck(check.connectedOK[UDP]);
  ZuCheck(!mx.allocator<>());
  auto buf = app->allocBuf();
  ZuCheck(buf && buf->builtin());
}

class StopCxn : public ZmNHeap<StopCxn>, public ZiConnection {
friend inline ZuStringT<"ZiMxLoopTest.StopCxn"> ZmNHeapID(StopCxn *);

public:
  StopCxn(ZiMultiplex *mx, const ZiCxnInfo &info, ZmSemaphore &ready) :
    ZiConnection{mx, info}, m_ready{ready} { }

  void connected(ZiRxContext &io) override {
    auto buf = info().rxAllocator->allocBuf(this);
    if (buf) io.init(ZiRxFn{[](ZiRxContext &) { return true; }}, ZuMv(buf));
    else io.disconnect();
    // Run after the backend has had a chance to submit the first receive.
    mx()->rxRun([this] { m_ready.post(); });
  }

  void disconnected(bool) override { ++m_disconnected; }

  unsigned disconnects() const { return m_disconnected; }

private:
  ZmSemaphore	&m_ready;
  unsigned	m_disconnected = 0;
};

static void testStopAndRestart()
{
  ZuTestScopeRT(testStopAndRestart);
  ZiMultiplex mx;
  auto plugin = mx.plugin();
  auto rx = mx.allocator<Zi::IOBufType::Rx>();
  ZuCheckRT(plugin && plugin->mx() == &mx && rx);
  if (!plugin || !rx) return;

  const ZiIP loopback{"127.0.0.1"};
  for (unsigned i = 0; i != 2; ++i) {
    ZmSemaphore ready;
    ZmRef<StopCxn> incoming, outgoing;
    auto fail = [&ready](bool) { ready.post(); };
    bool started = mx.start();
    ZuCheckRT(started);
    if (!started) return;
    mx.listen([&mx, &ready, &outgoing, fail, rx](const ZiListenInfo &info) {
      mx.connect([&mx, &ready, &outgoing](const ZiCxnInfo &ci) -> ZiConnection * {
	outgoing = new StopCxn{&mx, ci, ready};
	return outgoing;
      }, fail, info.ip, 0, info.ip, info.port, rx);
    }, fail, [&mx, &ready, &incoming](const ZiCxnInfo &ci) -> ZiConnection * {
      incoming = new StopCxn{&mx, ci, ready};
      return incoming;
    }, loopback, 0, 4, rx);
    auto deadline = Zm::now(5);
    bool connected = !ready.timedwait(deadline) && !ready.timedwait(deadline);
    ZuCheckRT(connected);
    ZuCheckRT(mx.stop());
    ZuCheckRT(mx.plugin() == plugin && plugin->mx() == &mx);
    ZuCheckRT(incoming && incoming->disconnects() == 1 && !incoming->up());
    ZuCheckRT(outgoing && outgoing->disconnects() == 1 && !outgoing->up());
  }
}

struct MxWatchState {
  ZiMultiplex	*mx = nullptr;
  unsigned	rootAdds = 0;
  unsigned	rootDels = 0;
};

static void testMxWatch()
{
  ZuTestScope(testMxWatch);
  MxWatchState state;
  Ztc::MxMgr::watch(
    {[&state](Ztc::Mx *mx_) {
      state.mx = static_cast<ZiMultiplex *>(mx_);
      ++state.rootAdds;
    }},
    {[&state](Ztc::Mx *mx_) {
      ZuCheck(mx_ == state.mx);
      ++state.rootDels;
    }});
  {
    LoopMx mx{ZiIP{"127.0.0.1"}, ZiTestPort::MxLoopTCP4};
    ZuCheck(state.mx == &mx);
    ZuCheck(mx.start());
    ZuCheck(!mx.allocator<>());
    ZuCheck(state.rootAdds == 1);
    ZuCheck(mx.stop());
  }
  Ztc::MxMgr::unwatch();
  ZuCheck(state.rootDels == 1);
}

#ifdef linux
// Exercise the native nonblocking send with the receiver held idle. A Tx
// fence after setup observes the cursor once sendmsg has returned EAGAIN.
struct DrainCxn : public ZiConnection {
  DrainCxn(ZiMultiplex *mx, const ZiCxnInfo &info) : ZiConnection{mx, info} { }
  ZmSemaphore up, down;
  unsigned accepted = 0, callbacks = 0, errors = 0, teardowns = 0;
  bool ordered = true, replaced = false;
  ZmRef<ZiIOBuf> extra;
  void connected(ZiRxContext &io) override {
    io.init(ZiRxFn{[](ZiRxContext &) { return true; }}, info().rxAllocator->allocBuf());
    up.post();
  }
  void disconnected(bool) override { ++teardowns; down.post(); }

  bool sent(ZiTxContext &io) {
    ++callbacks;
    if (io.length < 0) { ++errors; io.close(); return true; }
    ordered &= io.offset == accepted && io.disconnected();
    io.offset += unsigned(io.length);
    accepted = io.offset;
    io.init(ZiTxFn{[this](ZiTxContext &) { replaced = true; return true; }}, io.buf);
    send_(ZiTxFn{[this](ZiTxContext &) { replaced = true; return true; }});
    // A callback which directly promotes another buffer cannot extend the
    // final send or replace its suffix on the next writable notification.
    io.buf = extra;
    io.offset = 0;
    io.fn = ZiTxFn{[this](ZiTxContext &) { replaced = true; return true; }};
    io.complete();
    return true;
  }
};

static void testNativeDrain()
{
  ZuTestScopeRT(testNativeDrain);
  enum { Length = 1 << 20 };
  enum { Drain, PeerFinish, Close, Error, IdleFinish, IdleClose };
  for (unsigned mode = Drain; mode <= IdleClose; ++mode) {
    ZiMultiplex mx{ZiMxParams{}.txBufSize(4096)};
    auto rx = mx.allocator<Zi::IOBufType::Rx>();
    auto tx = mx.allocator<Zi::IOBufType::TxTCP>();
    auto buf = tx->allocBuf(nullptr, Length);
    for (unsigned i = 0; i < Length; ++i) buf->data()[i] = uint8_t(i);
    bool started = mx.start();
    ZuCheckRT(started);
    if (!started) return;
    ZmSemaphore listening, connected;
    ZmRef<DrainCxn> cxn;
    uint16_t port = 0;
    mx.listen([&port, &listening](const ZiListenInfo &info) {
      port = info.port; listening.post();
    }, [&listening](bool) { listening.post(); }, [&mx, &cxn, &connected](
	const ZiCxnInfo &info) -> ZiConnection * {
      cxn = new DrainCxn{&mx, info};
      connected.post();
      return cxn;
    }, ZiIP{"127.0.0.1"}, 0, 1, rx);
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    timeval timeout{5, 0};
    if (fd >= 0) setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    bool ready = !listening.timedwait(Zm::now(5)) && port && fd >= 0;
    ZiSockAddr addr{ZiIP{"127.0.0.1"}, port};
    ready = ready && !::connect(fd, addr.sa(), addr.len()) &&
      !connected.timedwait(Zm::now(5)) && !cxn->up.timedwait(Zm::now(5));
    ZuCheckRT(ready);
    if (!ready) { if (fd >= 0) ::close(fd); mx.stop(); return; }

    if (mode == IdleFinish || mode == IdleClose) {
      cxn->send(ZiTxFn{[mode](ZiTxContext &io) {
	io.disconnect();
	if (mode == IdleClose) io.close();
	return false;
      }});
      uint8_t byte;
      ZuCheckRT(!::recv(fd, &byte, 1, 0));
      ZuCheckRT(!cxn->down.timedwait(Zm::now(5)) && cxn->teardowns == 1);
      ::close(fd);
      mx.stop();
      continue;
    }
    cxn->extra = tx->allocBuf(nullptr, 1);
    ZmSemaphore stalled;
    bool pending = false;
    mx.txRun([&cxn, &buf, &pending, &stalled] {
      cxn->send_(ZiTxFn{[&cxn, &buf](ZiTxContext &io) {
	io.init(ZiTxFn{cxn.ptr(), ZmFnPtr<&DrainCxn::sent>{}}, buf);
	io.disconnect();
	return false; // final setup must drive the send
      }});
      auto &io = cxn->txContext();
      pending = io.disconnected() && io.buf == buf && io.offset && io.offset < Length;
      stalled.post();
    });
    ZuCheckRT(!stalled.timedwait(Zm::now(5)) && pending);
    if (mode == PeerFinish) ::shutdown(fd, SHUT_WR);
    if (mode == Close) cxn->close();
    if (mode == Error) {
      // Force the retry on this same socket to fail with EPIPE.
      mx.txRun([&cxn] { ::shutdown(cxn->info().socket, SHUT_WR); });
    }
    ZmThread waiter{[&cxn] { cxn->sync(); }};
    unsigned received = 0;
    bool sequence = true;
    uint8_t block[8192];
    ssize_t n;
    while ((n = ::recv(fd, block, sizeof(block), 0)) > 0) {
      for (ssize_t i = 0; i < n; ++i)
	if (block[i] != uint8_t(received + i)) sequence = false;
      received += unsigned(n);
    }
    bool done = !cxn->down.timedwait(Zm::now(5));
    if (!done) cxn->close();
    waiter.join();
    ::close(fd);
    mx.stop();
    ZuCheckRT(done && cxn->teardowns == 1 && cxn->callbacks && cxn->ordered);
    ZuCheckRT(!cxn->replaced && cxn->txContext().completed() && !cxn->txContext().buf);
    ZuCheckRT(sequence && received == cxn->accepted && !n);
    ZuCheckRT(mode <= PeerFinish ? received == Length : received && received < Length);
    ZuCheckRT(cxn->errors == unsigned(mode == Error));
  }
}

static void testNativeStartFailure()
{
  ZuTestScopeRT(testNativeStartFailure);
  // Keep descriptor limits and failure-time cleanup isolated from the harness.
  pid_t child = ::fork();
  ZuCheckRT(child >= 0);
  if (child < 0) return;
  if (!child) {
    ::alarm(20);
    // Error reporting must already own its ring: the injection targets native
    // multiplexer setup, not lazy logger initialization under descriptor limits.
    ZiLog::init("ZiMxLoopTest");
    ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
    ZiLog::start();
    {
      ZmAtomic<unsigned> retired = 0;
      ZiMultiplex mx;
      mx.threadFinal([&retired]() { ++retired; });
      struct rlimit saved;
      if (::getrlimit(RLIMIT_NOFILE, &saved)) _exit(1);
      struct rlimit limited = saved;
      limited.rlim_cur = 0;
      if (::setrlimit(RLIMIT_NOFILE, &limited)) _exit(2);
      bool started = mx.start();
      if (::setrlimit(RLIMIT_NOFILE, &saved)) _exit(3);
      if (started || mx.state() != ZmEngineState::Stopped) _exit(4);
      if (mx.allocator<>()) _exit(8);
      // Failure is reported only after every worker has exited. In particular,
      // destructor cleanup must not be what retires this failed start's workers.
      unsigned n = mx.params().nThreads();
      if (retired.load_() != n) _exit(5);
      if (!mx.start() || !mx.stop()) _exit(6);
      if (retired.load_() != n + n) _exit(7);
    }
    ZiLog::stop();
    _exit(0);
  }
  int status = 0;
  pid_t result;
  do { result = ::waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
  ZuCheckRT(result == child);
  ZuCheckRT(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
#endif

static void testWakeBurst()
{
  ZuTestScope(testWakeBurst);
  ZiMultiplex mx;
  ZmSemaphore done;
  unsigned completed = 0;
  // Exceed the usual 64 KiB wake pipe while its Rx reader is posting work.
  constexpr unsigned count = 128 * 1024;
  ZuCheck(mx.start());
  mx.rxRun([&mx, &completed, &done]() {
    for (unsigned i = 0; i < count; ++i)
      mx.rxRun([&completed, &done]() { if (++completed == count) done.post(); });
  });
  ZuCheck(done.timedwait(Zm::now(30)) == 0);
  ZuCheck(mx.stop());
  ZuCheck(completed == count);
}

#undef CheckTcpLoopback


int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
#ifdef linux
  ZuTestCall(testNativeStartFailure);
  ZuTestCall(testNativeDrain);
#endif
  ZuTestCall(testMulticastOptions);
  ZuTestCall(testTcpLoopbackIPv4);
  ZuTestCall(testTcpLoopbackIPv6);
  ZuTestCall(testUdpTOSIPv4);
  ZuTestCall(testUdpTOSMappedIPv4);
  ZuTestCall(testUdpMPSIPv6);
  ZuTestCall(testEndpointRxAllocators);
  ZuTestCall(testStopAndRestart);
  ZuTestCall(testMxWatch);
  ZuTestCall(testWakeBurst);
  return 0;
}
