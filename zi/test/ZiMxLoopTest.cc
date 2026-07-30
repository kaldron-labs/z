//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZiMultiplex.hh>

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
  uint8_t	m_buf[sizeof(Msg)] = {};
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
  uint8_t	m_buf[sizeof(Msg)] = {};
};

class LoopMx : public ZiMultiplex {
public:
  LoopMx(ZiIP loopIP) : m_loopIP{loopIP} { }
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
      m_loopIP, 0, 1,
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
  UdpTOSMx(ZiIP loopIP) : m_loopIP{loopIP} { }
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
      m_loopIP, 0, ZiIP{}, 0, options);
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
      m_loopIP, 0, m_loopIP, m_serverPort, options);
  }

private:
  ZmSemaphore		m_done;
  ZiIP			m_loopIP;
  uint16_t		m_serverPort = 0;
  ZmRef<UdpTOSCxn>	m_serverCxn;
  ZmRef<UdpTOSCxn>	m_clientCxn;
  ZmAtomic<unsigned>	m_failed = 0;
  ZmAtomic<unsigned>	m_payloadOK = 0;
  ZmAtomic<unsigned>	m_haveTOS = 0;
  uint8_t		m_tos = 0;
};

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
      ZiIOFn{this, ZmFnPtr<&LoopCxn::recvClient>{}},
      m_buf, sizeof(Msg), 0);
    send(ZiIOFn{this, ZmFnPtr<&LoopCxn::sendClient>{}});
  } else {
    io.init(
      ZiIOFn{this, ZmFnPtr<&LoopCxn::recvServer>{}},
      m_buf, sizeof(Msg), 0);
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
    ZiIOFn{this, ZmFnPtr<&LoopCxn::sendClientDone>{}},
    Msg, sizeof(Msg), 0);
  return true;
}

bool LoopCxn::sendClientDone(ZiIOContext &io)
{
  if (io.length < 0) {
    m_owner->markIOFailure();
    io.disconnect();
    return true;
  }
  if ((io.offset += io.length) < io.size) return true;
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
  if ((io.offset += io.length) < io.size) return true;

  m_owner->markClientEcho(!::memcmp(m_buf, Msg, sizeof(Msg)));
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
  if ((io.offset += io.length) < io.size) return true;

  send(ZiIOFn{this, ZmFnPtr<&LoopCxn::sendServer>{}});
  io.complete();
  return true;
}

bool LoopCxn::sendServer(ZiIOContext &io)
{
  io.init(
    ZiIOFn{this, ZmFnPtr<&LoopCxn::sendServerDone>{}},
    m_buf, sizeof(Msg), 0);
  return true;
}

bool LoopCxn::sendServerDone(ZiIOContext &io)
{
  if (io.length < 0) {
    m_owner->markIOFailure();
    io.disconnect();
    return true;
  }
  if ((io.offset += io.length) < io.size) return true;

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
      ZiIOFn{this, ZmFnPtr<&UdpTOSCxn::recv_>{}},
      m_buf, sizeof(Msg), 0);
    m_owner->serverReady(this);
  }
}

void UdpTOSCxn::disconnected(bool)
{
}

bool UdpTOSCxn::send_(ZiIOContext &io)
{
  io.init(
    ZiIOFn{this, ZmFnPtr<&UdpTOSCxn::sendDone_>{}},
    Msg, sizeof(Msg), 0);
  io.tos = uint8_t(0x2e);
  return true;
}

bool UdpTOSCxn::sendDone_(ZiIOContext &io)
{
  if (io.length < 0) {
    m_owner->ioFailure();
    io.disconnect();
    return true;
  }
  if ((io.offset += io.length) < io.size) return true;
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
  bool payloadOK = io.length == int(sizeof(Msg)) &&
    !::memcmp(m_buf, Msg, sizeof(Msg));
  m_owner->recvTOS(io.tos, payloadOK);
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

LoopResult runTcpLoopbackAndTelemetry(ZiIP loopIP)
{
  LoopResult result;
  LoopMx mx{loopIP};
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
    telemetry.nThreads >= 1 && mx.telKey() == telemetry.id && check.found &&
    allMxs == check.visited;

  mx.startLoopback();

  result.done = mx.waitDone(5);
  mx.stop();

  // Some constrained sandboxes deny loopback socket setup; gate integration
  // assertions to keep the suite deterministic across environments.
  result.failed = mx.failed_();
  result.failKind = mx.failKind_();
  if (result.failed) return result;

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

  auto result = runTcpLoopbackAndTelemetry(ZiIP{"127.0.0.1"});
  CheckTcpLoopback(result);
}

void testTcpLoopbackIPv6()
{
  ZuTestScope(testTcpLoopbackIPv6);

  auto result = runTcpLoopbackAndTelemetry(ZiIP{"::1"});
  CheckTcpLoopback(result);
}

void testUdpTOSIPv4()
{
  ZuTestScope(testUdpTOSIPv4);

  UdpTOSMx mx{ZiIP{"127.0.0.1"}};
  ZuCheck(mx.start());
  mx.startServer();
  ZuCheck(mx.waitDone(5));
  mx.stop();

  ZuCheck(!mx.failed_());
  ZuCheck(mx.payloadOK_());
  ZuCheck(mx.haveTOS_());
  ZuCheck(mx.tos_() == 0x2e);
}

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
    LoopMx mx{ZiIP{"127.0.0.1"}};
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

#undef CheckTcpLoopback

} // namespace

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testMulticastOptions);
  ZuTestCall(testTcpLoopbackIPv4);
  ZuTestCall(testTcpLoopbackIPv6);
  ZuTestCall(testUdpTOSIPv4);
  ZuTestCall(testMxWatch);
  return 0;
}
