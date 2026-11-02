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
#include <zlib/ZiRx.hh>

#include "ZiTestPorts.hh"

using namespace ZuTestUtil;

namespace {

class LoopMx;
class UdpTOSMx;

class LoopCxn : public ZiConnection {
  static constexpr uint8_t Msg[4] = { 'P', 'I', 'N', 'G' };

public:
  LoopCxn(LoopMx *mx, const ZiCxnInfo &ci);
  ~LoopCxn() = default;

  void connected(ZiIOContext &io) override;
  void disconnected(bool) override;

private:
  bool sendClient(ZiIOContext &io);
  bool sendClientDone(ZiIOContext &io);
  bool recvClient(ZiIOContext &io);

  bool recvServer(ZiIOContext &io);
  bool sendServer(ZiIOContext &io);
  bool sendServerDone(ZiIOContext &io);

private:
  LoopMx		*m_owner = nullptr;
  bool		m_client = false;
  ZmRef<ZiIOBuf> txBuf();
};

class UdpTOSCxn : public ZiConnection {
  static constexpr uint8_t Msg[4] = { 'T', 'O', 'S', '!' };

public:
  UdpTOSCxn(UdpTOSMx *mx, const ZiCxnInfo &ci);
  ~UdpTOSCxn() = default;

  void connected(ZiIOContext &io) override;
  void disconnected(bool) override;

private:
  bool send_(ZiIOContext &io);
  bool sendDone_(ZiIOContext &io);
  bool recv_(ZiIOContext &io);

private:
  UdpTOSMx	*m_owner = nullptr;
  bool		m_client = false;
  ZmRef<ZiIOBuf> txBuf();
};

class LoopMx : public ZiMultiplex {
public:
  Zi::IOBufAllocator *allocator() const { return m_allocator.ptr(); }
  LoopMx(ZiIP loopIP, unsigned port, ZiMxParams params = {}) :
    ZiMultiplex{ZuMv(params)}, m_loopIP{loopIP}, m_listenPort{port} { }
  ~LoopMx() = default;

  ZiConnection *connected(const ZiCxnInfo &ci)
  {
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
    ZiMultiplex::listen(
      ZiListenFn{this, ZmFnPtr<&LoopMx::listening>{}},
      ZiFailFn{this, ZmFnPtr<&LoopMx::failed>{}},
      ZiConnectFn{this, ZmFnPtr<&LoopMx::connected>{}},
      m_loopIP, m_listenPort, 1,
      ZiCxnOptions());
  }

  bool waitDone(int seconds)
  {
    return m_done.timedwait(Zm::now(seconds)) == 0;
  }
  bool waitDisconnections(unsigned n, int seconds)
  {
    ZuTime timeout = Zm::now(seconds);
    while (m_disconnects.load_() < n)
      if (m_discDone.timedwait(timeout) != 0) return false;
    return true;
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
      ZiCxnOptions());
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
    m_discDone.post();
  }

private:
  ZmRef<Zi::IOBufAllocator> m_allocator = new ZiTxTCPBufAllocator<>;
	  ZmSemaphore		m_done;
	  ZmSemaphore		m_discDone;
	  ZmScheduler::Timer	m_retryTimer;
  ZiIP			m_loopIP;
	  unsigned		m_listenPort = 0;
  unsigned		m_retries = 0;
  ZmRef<LoopCxn>	m_clientCxn;
  ZmRef<LoopCxn>	m_serverCxn;
  ZmAtomic<unsigned>	m_failed = 0;
  ZmAtomic<unsigned>	m_failKind = 0;
  ZmAtomic<unsigned>	m_clientEcho = 0;
  ZmAtomic<unsigned>	m_clientDisc = 0;
  ZmAtomic<unsigned>	m_serverDisc = 0;
  ZmAtomic<unsigned>	m_disconnects = 0;
  ZmAtomic<unsigned>	m_peerDisconnects = 0;
  ZmAtomic<unsigned>	m_localDisconnects = 0;
};

class UdpTOSMx : public ZiMultiplex {
public:
  Zi::IOBufAllocator *allocator() const { return m_allocator.ptr(); }
  UdpTOSMx(ZiIP loopIP, unsigned port, ZiMxParams params = {}, unsigned size = 4) :
    ZiMultiplex{ZuMv(params)}, m_loopIP{loopIP}, m_serverPort{uint16_t(port)},
    m_size{size} { }
  ~UdpTOSMx() = default;

  ZiConnection *connected(const ZiCxnInfo &ci)
  {
    auto cxn = new UdpTOSCxn(this, ci);
    if (!!ci.remoteIP)
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
    ZiCxnOptions options;
    options.udp(true);
    udp(
      ZiConnectFn{this, ZmFnPtr<&UdpTOSMx::connected>{}},
      ZiFailFn{this, ZmFnPtr<&UdpTOSMx::failed>{}},
      m_loopIP, m_serverPort, ZiIP{}, 0, options);
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

  void recvTOS(ZiTOS tos, bool payloadOK, unsigned flags, ZmRef<ZiIOBuf> buf)
  {
    m_retained = ZuMv(buf);
    m_flags = flags;
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
  bool haveTOS_() const { return m_haveTOS.load_(); }
  uint8_t tos_() const { return m_tos; }
  unsigned size() const { return m_size; }
  unsigned flags() const { return m_flags; }
  const ZiIOBuf *retained() const { return m_retained; }

private:
  void startClient()
  {
    ZiCxnOptions options;
    options.udp(true);
    udp(
      ZiConnectFn{this, ZmFnPtr<&UdpTOSMx::connected>{}},
      ZiFailFn{this, ZmFnPtr<&UdpTOSMx::failed>{}},
      m_loopIP, 0, m_loopIP, m_serverPort, options);
  }

private:
  ZmRef<Zi::IOBufAllocator> m_allocator = new ZiTxUDPBufAllocator<>;
  ZmRef<ZiIOBuf> m_retained;
  ZmSemaphore		m_done;
  ZiIP			m_loopIP;
  uint16_t		m_serverPort = 0;
  unsigned		m_size;
  unsigned		m_flags = 0;
  ZmRef<UdpTOSCxn>	m_serverCxn;
  ZmRef<UdpTOSCxn>	m_clientCxn;
  ZmAtomic<unsigned>	m_failed = 0;
  ZmAtomic<unsigned>	m_payloadOK = 0;
  ZmAtomic<unsigned>	m_haveTOS = 0;
  uint8_t		m_tos = 0;
};

ZmRef<ZiIOBuf> LoopCxn::txBuf() {
  auto buf = m_owner->allocator()->alloc();
  buf->append(Msg, sizeof(Msg));
  return buf;
}
ZmRef<ZiIOBuf> UdpTOSCxn::txBuf() {
  auto buf = m_owner->allocator()->alloc();
  unsigned n = m_owner->size();
  memset(buf->ensure(n), 'T', n);
  if (n >= sizeof(Msg)) memcpy(buf->data(), Msg, sizeof(Msg));
  buf->length = n;
  return buf;
}

LoopCxn::LoopCxn(LoopMx *mx, const ZiCxnInfo &ci) :
    ZiConnection(mx, ci),
    m_owner(mx),
    m_client(ci.type == ZiCxnType::TCPOut)
{
}

void LoopCxn::connected(ZiIOContext &io)
{
  if (m_client) {
    io.init(
      ZiIOFn{this, ZmFnPtr<&LoopCxn::recvClient>{}});
    send(ZiIOFn{this, ZmFnPtr<&LoopCxn::sendClient>{}});
  } else {
    io.init(
      ZiIOFn{this, ZmFnPtr<&LoopCxn::recvServer>{}});
  }
}

void LoopCxn::disconnected(bool peer)
{
  if (m_client)
    m_owner->markClientDisconnected(peer);
  else
    m_owner->markServerDisconnected(peer);
}

bool LoopCxn::sendClient(ZiIOContext &io)
{
  io.init(
    ZiIOFn{this, ZmFnPtr<&LoopCxn::sendClientDone>{}}, txBuf());
  return true;
}

bool LoopCxn::sendClientDone(ZiIOContext &io)
{
  if (!io.buf) { io.complete(); return true; }
  if (io.length < 0) {
    m_owner->markIOFailure();
    io.disconnect();
    return true;
  }
  if (io.buf->length < sizeof(Msg)) return true;
  io.complete();
  return true;
}

bool LoopCxn::recvClient(ZiIOContext &io)
{
  if (io.length < 0) {
    m_owner->markIOFailure();
    io.disconnect();
    return true;
  }
  if (io.buf->length < sizeof(Msg)) return true;

  m_owner->markClientEcho(!::memcmp(io.buf->data(), Msg, sizeof(Msg)));
  io.disconnect();
  return true;
}

bool LoopCxn::recvServer(ZiIOContext &io)
{
  if (io.length < 0) {
    m_owner->markIOFailure();
    io.disconnect();
    return true;
  }
  if (io.buf->length < sizeof(Msg)) return true;

  send(ZiIOFn{this, ZmFnPtr<&LoopCxn::sendServer>{}});
  io.complete();
  return true;
}

bool LoopCxn::sendServer(ZiIOContext &io)
{
  io.init(
    ZiIOFn{this, ZmFnPtr<&LoopCxn::sendServerDone>{}}, txBuf());
  return true;
}

bool LoopCxn::sendServerDone(ZiIOContext &io)
{
  if (!io.buf) { io.complete(); return true; }
  if (io.length < 0) {
    m_owner->markIOFailure();
    io.disconnect();
    return true;
  }
  if (io.buf->length < sizeof(Msg)) return true;

  io.disconnect();
  return true;
}

UdpTOSCxn::UdpTOSCxn(UdpTOSMx *mx, const ZiCxnInfo &ci) :
    ZiConnection(mx, ci),
    m_owner(mx),
    m_client(!!ci.remoteIP)
{
}

void UdpTOSCxn::connected(ZiIOContext &io)
{
  if (m_client) {
    send(ZiIOFn{this, ZmFnPtr<&UdpTOSCxn::send_>{}});
    io.complete();
  } else {
    io.init(
      ZiIOFn{this, ZmFnPtr<&UdpTOSCxn::recv_>{}});
    m_owner->serverReady(this);
  }
}

void UdpTOSCxn::disconnected(bool)
{
}

bool UdpTOSCxn::send_(ZiIOContext &io)
{
  io.init(
    ZiIOFn{this, ZmFnPtr<&UdpTOSCxn::sendDone_>{}}, txBuf());
  io.tos = uint8_t(0x2e);
  return true;
}

bool UdpTOSCxn::sendDone_(ZiIOContext &io)
{
  if (!io.buf) { io.complete(); return true; }
  if (io.length < 0) {
    m_owner->ioFailure();
    io.disconnect();
    return true;
  }
  io.disconnect();
  return true;
}

bool UdpTOSCxn::recv_(ZiIOContext &io)
{
  if (io.length < 0) {
    m_owner->ioFailure();
    io.disconnect();
    return true;
  }
  unsigned n = m_owner->size(), cap = io.buf->size - io.buf->skip;
  if (n > cap) n = cap;
  bool payloadOK = io.length == int(n) &&
    (!n || !::memcmp(io.buf->data(), Msg, sizeof(Msg)));
  m_owner->recvTOS(io.tos, payloadOK, io.flags, io.buf);
  io.disconnect();
  return true;
}

void testMulticastOptions()
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
  bool		emptyCxns = false;
  bool		done = false;
  bool		failed = false;
  unsigned	failKind = 0;
  bool		clientEcho = false;
  bool		clientDisc = false;
  bool		serverDisc = false;
  bool		waitDisc = false;
  unsigned	disconnects = 0;
  unsigned	peerDisconnects = 0;
  unsigned	localDisconnects = 0;
};

struct MxCheck {
  Ztc::Mx	*target;
  bool		found = false;
  unsigned	visited = 0;
};

LoopResult runTcpLoopbackAndTelemetry(
  ZiIP loopIP, unsigned port, ZiMxParams params = {})
{
  LoopResult result;
  LoopMx mx{loopIP, port, ZuMv(params)};
  unsigned cxns = 0;
  result.emptyCxns =
    !static_cast<const LoopMx &>(mx).allCxns(
      [&cxns](Ztc::Connection *) { ++cxns; }) && !cxns;
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

  // Some constrained sandboxes deny loopback socket setup; gate integration
  // assertions to keep the suite deterministic across environments.
  result.failed = mx.failed_();
  result.failKind = mx.failKind_();
  if (result.failed) { mx.stop(); return result; }

  result.clientEcho = mx.clientEcho_();
  result.clientDisc = mx.clientDisc_();
  result.serverDisc = mx.serverDisc_();
  unsigned disconnects = result.disconnects = mx.disconnects_();
  result.peerDisconnects = mx.peerDisconnects_();
  result.localDisconnects = mx.localDisconnects_();
  mx.repeatDisconnects();
  result.waitDisc = mx.waitDisconnections(disconnects + 4, 5);
  result.disconnects = mx.disconnects_();
  result.peerDisconnects = mx.peerDisconnects_();
  result.localDisconnects = mx.localDisconnects_();
  mx.stop();
  return result;
}

#define CheckTcpLoopback(result) do { \
  ZuCheck((result).started); \
  ZuCheck((result).telemetry); \
  ZuCheck((result).emptyCxns); \
  ZuCheck((result).done); \
  if ((result).failed) { \
    ZuCheck((result).failKind == 1); \
    return; \
  } \
  ZuCheck((result).clientEcho); \
  ZuCheck((result).clientDisc); \
  ZuCheck((result).serverDisc); \
  ZuCheck((result).waitDisc); \
  ZuCheck((result).disconnects >= 4); \
  ZuCheck((result).peerDisconnects == 0); \
  ZuCheck((result).localDisconnects >= 4); \
} while (0)

void testTcpLoopbackIPv4()
{
  ZuTestScope(testTcpLoopbackIPv4);

  auto result = runTcpLoopbackAndTelemetry(
    ZiIP{"127.0.0.1"}, ZiTestPort::MxLoopTCP4);
  CheckTcpLoopback(result);
}

void testTcpLoopbackIPv6()
{
  ZuTestScope(testTcpLoopbackIPv6);

  auto result = runTcpLoopbackAndTelemetry(
    ZiIP{"::1"}, ZiTestPort::MxLoopTCP6);
  CheckTcpLoopback(result);
}

void testUdpTOSIPv4()
{
  ZuTestScope(testUdpTOSIPv4);

  UdpTOSMx mx{ZiIP{"127.0.0.1"}, ZiTestPort::MxLoopUDP4};
  ZuCheck(mx.start());
  mx.startServer();
  ZuCheck(mx.waitDone(5));
  mx.stop();

  ZuCheck(!mx.failed_());
  ZuCheck(mx.payloadOK_());
  ZuCheck(mx.haveTOS_());
  ZuCheck(mx.tos_() == 0x2e);
}

#ifndef _WIN32
void testNativeOwners()
{
  ZuTestScopeRT(testNativeOwners);
  if (!Zi::uring()) { ZuCheckRT(!Zi::uring()); return; }
  for (bool shared : {false, true}) {
    for (auto host : {"127.0.0.1", "::1"}) {
      auto result = runTcpLoopbackAndTelemetry(ZiIP{host}, 0,
	ZiMxParams{}.backend(ZiMxBackend::URing).txThread(shared ? 1 : 2));
      ZuCheckRT(result.started && result.done && !result.failed);
      ZuCheckRT(result.clientEcho && result.clientDisc && result.serverDisc);
      ZuCheckRT(result.waitDisc && result.peerDisconnects == 0);
      UdpTOSMx mx{ZiIP{host}, 0,
	ZiMxParams{}.backend(ZiMxBackend::URing).txThread(shared ? 1 : 2)};
      ZuCheckRT(mx.start());
      ZuCheckRT(mx.backend() == ZiMxBackend::URing);
      mx.startServer();
      ZuCheckRT(mx.waitDone(5));
      ZuCheckRT(mx.stop());
      ZuCheckRT(!mx.failed_() && mx.payloadOK_());
      ZuCheckRT(mx.haveTOS_() && mx.tos_() == 0x2e);
      // A stopped multiplexer and the same application allocators are reusable.
      ZuCheckRT(mx.start());
      ZuCheckRT(mx.stop());
    }
  }
}

void testBackendSelection()
{
  ZuTestScopeRT(testBackendSelection);
  ZiMxParams params;
  params.tcpBufAllocator(new ZiAppBufAllocator<>);
  ZiMultiplex automatic{params};
  ZuCheckRT(automatic.start());
  ZuCheckRT(automatic.backend() == ZiMxBackend::EPoll);
  ZuCheckRT(automatic.stop());
  params.backend(ZiMxBackend::URing);
  ZiMultiplex required{ZuMv(params)};
  ZuCheckRT(!required.start());
  ZuCheckRT(required.state() == ZmEngineState::Stopped);
}

void testEpollOwners()
{
  ZuTestScopeRT(testEpollOwners);
  for (bool shared : {false, true}) {
    auto result = runTcpLoopbackAndTelemetry(ZiIP{"127.0.0.1"}, 0,
      ZiMxParams{}.backend(ZiMxBackend::EPoll).txThread(shared ? 1 : 2));
    ZuCheckRT(result.started && result.done && !result.failed);
    ZuCheckRT(result.clientEcho && result.clientDisc && result.serverDisc);
    ZuCheckRT(result.waitDisc && result.peerDisconnects == 0);
  }
}
#endif

#ifndef _WIN32
struct BurstBuf : public ZiRxBuf {
  using ZiRxBuf::ZiRxBuf;
  unsigned cookie = 0x1234;
};
class BurstMx;
class BurstCxn : public ZiConnection, public ZiRx<BurstCxn> {
public:
  BurstCxn(BurstMx *mx, const ZiCxnInfo &ci, Zi::IOBufAllocator *allocator);
  void connected(ZiIOContext &) override;
  void disconnected(bool) override { rxReset(); }
private:
  bool sent(ZiIOContext &);
  int header(const ZiIOContext &, Stream &) { return 64; }
  int body(const ZiIOContext &, Stream &, unsigned);
  BurstMx *m_owner;
  Zi::IOBufAllocator *m_allocator;
  unsigned m_next = 0;
  unsigned m_retired = 0;
  unsigned m_message = 0;
  unsigned m_offset = 0;
};

class BurstMx : public ZiMultiplex {
public:
  enum { Clients = 20, Messages = 500, Hold = ZiIOBuf_DefltRingSize };
  explicit BurstMx(bool shared, bool many = false) : ZiMultiplex{
    ZiMxParams{}.backend(ZiMxBackend::URing).txThread(shared ? 1 : 2).txBufSize(2048)
      .tcpBufAllocator(new Zi::RxTCPBufAllocator<
	BurstBuf, 256, ZiIOBuf_DefltMaxSize, ZiIOBuf_HeapID>)} {
    if (many) {
      m_clients = ZiIOBuf_DefltRingSize + 8;
      for (unsigned i = 0; i < m_clients; ++i)
	m_allocators.push(new ZiTxTCPBufAllocator<64>);
    }
  }
  void startClients(const ZiListenInfo &info) {
    for (unsigned i = 0; i < m_clients; ++i) {
      auto allocator = m_allocators.length() ? m_allocators[i].ptr() :
	(i & 1 ? m_tls.ptr() : m_tcp.ptr());
      connect({[this, allocator](const ZiCxnInfo &ci) {
	return new BurstCxn{this, ci, allocator};
      }}, {[this](bool) { failed(); }},
	ZiIP{"127.0.0.1"}, 0, ZiIP{"127.0.0.1"}, info.port, {});
    }
  }
  void startServer() {
    listen({this, ZmFnPtr<&BurstMx::startClients>{}},
      {[this](bool) { failed(); }},
      {[this](const ZiCxnInfo &ci) { return new BurstCxn{this, ci, nullptr}; }},
      ZiIP{"127.0.0.1"}, 0, m_clients, {});
  }
  // A 256 KiB first write forces partial ordinary sends; later 16 KiB writes
  // use fixed storage alongside small packets and notification pressure.
  static unsigned size(unsigned i) { return !i ? 256<<10 : i % 31 ? 64 : 16<<10; }
  static unsigned bytes() {
    unsigned n = 0;
    for (unsigned i = 0; i < Messages; ++i) n += size(i);
    return Clients * n;
  }
  void sent(const ZiIOContext &io) {
    if (unsigned(io.length) < io.buf->length) ++m_partial;
    if (++m_sent == Clients * Messages) m_txDone.post();
  }
  bool ingress() { return !m_ingress.timedwait(Zm::now(15)); }
  void first() { if (++m_started == m_clients) m_bound.post(); }
  bool bound() { return !m_bound.timedwait(Zm::now(15)); }
  bool many() const { return m_clients != Clients; }
  void received(Zi::RxRange range) {
    auto buf = static_cast<BurstBuf *>(range.buf.ptr());
    m_original &= buf->uring() && buf->ring() && buf->cookie == 0x1234 &&
      invoked(rxThread());
    if (!m_bytes) m_ingress.post();
    m_bytes += range.length;
    if (!m_released && (!m_held.length() || m_held[m_held.length() - 1] != buf)) {
      m_held.push(ZuMv(range.buf));
      if (m_held.length() == Hold) {
	m_released = true;
	rxRun([this]() { m_held.null(); });
      }
    }
    if (m_bytes == bytes()) m_rxDone.post();
  }
  void failed() { m_failed = true; m_rxDone.post(); m_txDone.post(); }
  bool done() {
    auto deadline = Zm::now(15);
    return !m_rxDone.timedwait(deadline) && !m_txDone.timedwait(deadline);
  }
  bool ok() const {
    return !m_failed && m_original && m_released && m_partial &&
      m_sent == Clients * Messages && m_bytes == bytes();
  }
private:
  ZmRef<Zi::IOBufAllocator> m_tcp = new ZiTxTCPBufAllocator<32<<10>;
  ZmRef<Zi::IOBufAllocator> m_tls = new ZiTxTLSBufAllocator<32<<10>;
  ZtArray<ZmRef<Zi::IOBufAllocator>> m_allocators;
  ZtArray<ZmRef<ZiIOBuf>> m_held;
  ZmSemaphore m_rxDone, m_txDone, m_ingress, m_bound;
  unsigned m_clients = Clients;
  unsigned m_started = 0;
  ZmAtomic<unsigned> m_failed{0};
  unsigned m_sent = 0;
  unsigned m_partial = 0;
  unsigned m_bytes = 0;
  bool m_original = true;
  bool m_released = false;
};

BurstCxn::BurstCxn(BurstMx *mx, const ZiCxnInfo &ci, Zi::IOBufAllocator *allocator) :
  ZiConnection{mx, ci}, m_owner{mx}, m_allocator{allocator} { }

void BurstCxn::connected(ZiIOContext &io)
{
  if (m_allocator) {
    io.complete();
    send(ZiIOFn{this, ZmFnPtr<&BurstCxn::sent>{}});
  } else ZiRx::recv<&BurstCxn::header, &BurstCxn::body>(io);
}

bool BurstCxn::sent(ZiIOContext &io)
{
  if (!m_owner->invoked(m_owner->txThread()) || io.length < 0) {
    m_owner->failed(); io.disconnect(); return true;
  }
  if (io.buf) {
    if (io.buf->data()[0] != uint8_t(++m_retired) || io.offset != io.buf->length)
      m_owner->failed();
    if (m_retired == 1) m_owner->first();
    m_owner->sent(io);
    io.complete();
    return true;
  }
  if (m_next == (m_owner->many() ? 1 : BurstMx::Messages)) {
    io.complete(); return true;
  }
  auto buf = m_allocator->alloc();
  unsigned n = m_owner->many() ? 64 : BurstMx::size(m_next);
  ++m_next;
  memset(buf->ensure(n), int(m_next), n);
  buf->length = n;
  io.init(ZiIOFn{this, ZmFnPtr<&BurstCxn::sent>{}}, ZuMv(buf));
  return true;
}

int BurstCxn::body(const ZiIOContext &, Stream &stream, unsigned n)
{
  stream.eachRange(n, [this](Zi::RxRange range) -> int64_t {
    auto length = range.length;
    for (auto c : range.cspan())
      if (c != uint8_t(m_message + 1)) { m_owner->failed(); return -1; }
    m_owner->received(ZuMv(range));
    return length;
  });
  m_offset += n;
  if (m_offset == BurstMx::size(m_message)) { ++m_message; m_offset = 0; }
  return n;
}

void testNativeBurst()
{
  ZuTestScopeRT(testNativeBurst);
  if (!Zi::uring()) { ZuCheckRT(!Zi::uring()); return; }
  for (bool shared : {false, true}) {
    BurstMx mx{shared};
    ZuCheckRT(mx.start());
    mx.startServer();
    ZuCheckRT(mx.done());
    ZuCheckRT(mx.stop());
    ZuCheckRT(mx.ok());
    BurstMx cancelled{shared};
    ZuCheckRT(cancelled.start());
    cancelled.startServer();
    ZuCheckRT(cancelled.ingress());
    ZuCheckRT(cancelled.stop()); // queued sends, fixed notifications, retained Rx
  }
}
#endif

#ifndef _WIN32
void testRingPressure()
{
  ZuTestScopeRT(testRingPressure);
  if (!Zi::uring()) { log("native ring pressure unavailable"); return; }
  BurstMx mx{true, true};
  ZuCheckRT(mx.start());
  mx.startServer();
  ZuCheckRT(mx.bound()); // more allocator watches than service-ring SQ entries
  ZuCheckRT(mx.stop());
}

void testUdpGeometry()
{
  ZuTestScopeRT(testUdpGeometry);
  for (auto backend : {ZiMxBackend::EPoll, ZiMxBackend::URing}) {
    if (backend == ZiMxBackend::URing && !Zi::uring()) continue;
    for (unsigned size : {0U, 8192U}) {
      UdpTOSMx mx{ZiIP{"127.0.0.1"}, 0, ZiMxParams{}.backend(backend), size};
      ZuCheckRT(mx.start());
      mx.startServer();
      ZuCheckRT(mx.waitDone(5));
      ZuCheckRT(mx.stop());
      ZuCheckRT(!mx.failed_() && mx.payloadOK_());
      ZuCheckRT(mx.haveTOS_() && mx.tos_() == 0x2e);
      ZuCheckRT(bool(mx.flags() & MSG_TRUNC) == bool(size));
      auto buf = mx.retained();
      ZuCheckRT(buf && buf->uring() == Zi::uring());
      ZuCheckRT(buf && (size ? buf->length > 0 : buf->length == 0));
    }
  }
}
#endif

struct MxWatchState {
  ZiMultiplex	*mx = nullptr;
  unsigned	rootAdds = 0;
  unsigned	rootDels = 0;
  unsigned	queueSeeds = 0;
};

void testMxWatch()
{
  ZuTestScope(testMxWatch);
  MxWatchState state;
  Ztc::MxMgr::watch(
    {[&state](Ztc::Mx *mx_) {
      state.mx = static_cast<ZiMultiplex *>(mx_);
      ++state.rootAdds;
      state.queueSeeds = mx_->allQueues(
	{[](Ztc::Queue *) { }});
    }},
    {[&state](Ztc::Mx *mx_) {
      ZuCheck(mx_ == state.mx);
      ++state.rootDels;
    }});
  {
    LoopMx mx{ZiIP{"127.0.0.1"}, ZiTestPort::MxLoopTCP4};
    ZuCheck(state.mx == &mx);
    ZuCheck(mx.start());
    ZuCheck(state.rootAdds == 1);
    ZuCheck(state.queueSeeds == mx.params().nThreads());
    ZuCheck(mx.stop());
    mx.unwatch();
  }
  Ztc::MxMgr::unwatch();
  ZuCheck(state.rootDels == 1);
}

#ifdef linux
void testNativeStartFailure()
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

void testWakeBurst()
{
  ZuTestScope(testWakeBurst);
  ZiMultiplex mx;
  ZmSemaphore done;
  unsigned completed = 0;
  // Exceed the usual 64 KiB wake pipe while its Rx reader is posting work.
  constexpr unsigned count = 128 * 1024;
  ZuCheck(mx.start());
  mx.rxRun([&]() {
    for (unsigned i = 0; i < count; ++i)
      mx.rxRun([&]() { if (++completed == count) done.post(); });
  });
  ZuCheck(done.timedwait(Zm::now(30)) == 0);
  ZuCheck(mx.stop());
  ZuCheck(completed == count);
}

#undef CheckTcpLoopback

} // namespace

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
#ifdef linux
  ZuTestCall(testNativeStartFailure);
#endif
  ZuTestCall(testMulticastOptions);
  ZuTestCall(testTcpLoopbackIPv4);
  ZuTestCall(testTcpLoopbackIPv6);
  ZuTestCall(testUdpTOSIPv4);
  ZuTestCall(testMxWatch);
  ZuTestCall(testWakeBurst);
#ifndef _WIN32
  ZuTestCall(testNativeOwners);
  ZuTestCall(testNativeBurst);
  ZuTestCall(testUdpGeometry);
  ZuTestCall(testRingPressure);
  ZuTestCall(testEpollOwners);
  ZuTestCall(testBackendSelection);
#endif
  return 0;
}
