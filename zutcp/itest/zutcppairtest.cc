//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <iostream>
#include <net/if.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmBlock.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmPlatform.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZtString.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZiLog.hh>

#include "ZiTestResidue.hh"
#include "../test/ZutcpTestMxPlugin.hh"

using namespace ZuTestUtil;

// Private native probes preserve protocol controls removed from applications.

namespace ProtocolClient {

ZuDerive(AppRequest,
  (ZtString<ZtStringHeapID<"Zutcp.Example.Request">>));



class App : public ZmNHeap<App>, public ZmPolymorph {
friend inline ZuStringT<"Zutcp.Example.ClientApp"> ZmNHeapID(App *);
public:
  using ZmNHeap<App>::operator new;
  using ZmNHeap<App>::operator delete;
  class Cxn;
  bool tfo() const { return m_tfo; }
  bool earlyData(Cxn *);
  bool allowEarlyStream(Cxn *, uint64_t, bool);
  void earlyDataAccepted(Cxn *);
  void earlyDataRejected(Cxn *);
  App(Zutcp::MxPlugin *stack, ZuCSpan request) : m_stack{stack} { m_request << request; }
  Zutcp::MxPlugin *stack() const { return m_stack; }
  void final() { }
  ZuCSpan request() const { return m_request; }
  void done() { m_established.post(); m_done.post(); }
  void ready() { m_established.post(); }
  void waitReady() { m_established.wait(); }
  void wait() { m_done.wait(); }
  void setError(const char *msg) {
    if (!m_error.xch(1))
      ZiLOG(Error, "zutcp", ([msg = ZeString{msg}](auto &out) {
	out << "error: " << msg;
      }));
  }
  bool error() const { return m_error.load_(); }

private:
  Zutcp::MxPlugin *m_stack;
  bool			m_tfo = getenv("ZUTCP_TEST_TFO") != nullptr;
  bool			m_allowEarly = getenv("ZUTCP_TEST_TFO_SEND_DENIED") == nullptr;
  ZmSemaphore		m_done, m_established;
  AppRequest		m_request;
  ZmAtomic<unsigned>	m_error = 0;
};

class App::Cxn : public ZmNHeap<App::Cxn>,
  public Zutcp::CliCxn<Zutcp::MxPlugin, App::Cxn> {
friend inline ZuStringT<"Zutcp.Example.CliCxn"> ZmNHeapID(Cxn *);
  using Base = Zutcp::CliCxn<Zutcp::MxPlugin, App::Cxn>;
public:
  using ZmNHeap<Cxn>::operator new;
  using ZmNHeap<Cxn>::operator delete;
  Cxn(App *app, unsigned tfoID = 0) : Base{app->stack()}, m_probe{app}, m_tfoID{tfoID} {
    txErrorFn(ZiTxErrorFn{[](ZeException &e) {
      ZiLOG(Error, "zutcp", ([e](auto &out) { out << "transmit error: " << e; }));
      return false;
    }});
  }
  App *probe() const { return m_probe; }
  bool earlyData() { return probe()->earlyData(this); }
  bool allowEarlyStream(uint64_t id, bool local) {
    return probe()->allowEarlyStream(this, id, local);
  }
  void earlyDataAccepted() { probe()->earlyDataAccepted(this); }
  void earlyDataRejected() { probe()->earlyDataRejected(this); }
  bool sendTLPNew() {
    auto first = Base::txNext();
    bool sent = Base::sendTLPNew();
    if (getenv("ZUTCP_TEST_TLP_DELAY_US"))
      std::cerr << "tail probe=new accepted=" << unsigned(sent) << " first=" << first
        << " end=" << Base::txNext() << '\n' << std::flush;
    return sent;
  }
  bool sendTLPRetx(Zutcp::SentFrameRef &ref) {
    bool sent = Base::sendTLPRetx(ref);
    if (getenv("ZUTCP_TEST_TLP_DELAY_US"))
      std::cerr << "tail probe=retransmit accepted=" << unsigned(sent)
        << " first=" << ref.offset << " length=" << ref.length << '\n' << std::flush;
    return sent;
  }
  void connected(Zutcp::Connected) {
    std::cerr << "connection established\n" << std::flush;
    if (probe()->tfo()) {
      std::cerr << "tfo connected=" << m_tfoID << '\n' << std::flush;
      if (startRequest(false) && !send(ZuBSpan{}, true)) {
	probe()->setError("request FIN failed");
	probe()->done();
      }
    } else if (getenv("ZUTCP_WIRE_GATE_DIR")) {
      auto out = writer(); // Empty public Writer observes the published producer geometry.
      unsigned capacity = out.maxSize() - out.headRoom() - out.tailRoom();
      std::cerr << "wire producer=" << capacity << '\n' << std::flush;
      probe()->ready();
    } else startRequest();
  }
  unsigned tfoID() const { return m_tfoID; }
  bool startRequest(bool fin = true) {
    if (probe()->tfo()) {
      if (m_requestQueued) return true;
      m_requestQueued = true;
      std::cerr << "tfo queued=" << m_tfoID << '\n' << std::flush;
    }
    if (!send(probe()->request(), fin)) {
      probe()->setError("request send failed");
      probe()->done();
      return false;
    }
    return true;
  }
  int process(Zutcp::Reader &reader) {
    auto length = reader.length();
    if (length) {
      reader.each(length, [](auto part) -> int64_t {
	std::cout << ZuCSpan{part.data(), part.length()};
	return part.length();
      });
      reader.clean();
      if (!retireRx(length)) return -1;
    }
    if (finReceived() && rxComplete() && !m_responseSeen) {
      m_responseSeen = true;
      std::cout << '\n' << std::flush;
    }
    return length ? 1 : 0;
  }
  void disconnected(bool) {
    if (!m_responseSeen) probe()->setError("disconnected before response");
    probe()->done();
  }
  void connectFailed(bool) { probe()->setError("connect failed"); probe()->done(); }

private:
  App *m_probe;
  unsigned	m_tfoID = 0;
  bool		m_requestQueued = false;
  bool		m_responseSeen = false; // Rx
};

bool App::earlyData(Cxn *link)
{
  if (!m_tfo) return false;
  bool queued = link->startRequest(false);
  return queued && allowEarlyStream(link, 0, true);
}

bool App::allowEarlyStream(Cxn *link, uint64_t, bool)
{
  return m_tfo && (m_allowEarly || link->tfoID() == 1);
}

void App::earlyDataAccepted(Cxn *link)
{
  if (m_tfo) std::cerr << "tfo accepted=" << link->tfoID() << '\n' << std::flush;
}

void App::earlyDataRejected(Cxn *link)
{
  if (m_tfo) std::cerr << "tfo rejected=" << link->tfoID() << '\n' << std::flush;
}

static void usage()
{
  std::cerr << "Usage: zutcp INTERFACE SERVER PORT [REQUEST]\n"
    << std::flush;
  ::exit(1);
}

static int run(int argc, char **argv)
{
  if (argc < 4 || argc > 5) usage();

  unsigned interface = if_nametoindex(argv[1]);
  ZuCSpan server = argv[2];
  unsigned port = ZuBox<unsigned>(argv[3]);
  if (!interface || !port) usage();

  ZuCSpan request = argc == 5 ? argv[4] : "zutcp-example";

  unsigned count = 1;
  if (getenv("ZUTCP_TEST_TFO")) {
    const char *value = getenv("ZUTCP_TEST_TFO_COUNT");
    count = value ? unsigned(ZuBox<unsigned>(value)) : 2;
    if (!count) usage();
  }

  ZiLog::init("zutcp");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  ZmRef<Zpkt::Env> env = new Zpkt::Env;
  ZiMultiplex mx(
      ZiMxParams()
	.scheduler([](auto &s) {
	  s.nThreads(4)
	  .thread(1, [](auto &t) { t.isolated(1); })
	  .thread(2, [](auto &t) { t.isolated(1); })
	  .thread(4, [](auto &t) { t.isolated(true); }); })
	.rxThread(1).txThread(2), ZmRef{new Zutcp::MxPlugin});
  auto params = ZutcpTest::Params(&mx);
  if (const char *profile = getenv("ZUTCP_TEST_CLIENT_OPTIONS")) {
    unsigned options = ZuBox<unsigned>(profile);
    params.sackPermitted(options & 1).windowScale(options & 2).timestamps(options & 4)
      .ecn(options & 8);
    params.maxData(131072);
  }
  if (const char *delay = getenv("ZUTCP_TEST_TLP_DELAY_US"))
    params.tlpPeerAckDelay(Zutcp::timeUS(ZuBox<unsigned>(delay)));
  auto instance = ZutcpTest::initMxPlugin(env, &mx, interface, 4, params.config());
  auto stack = instance.ptr();
  ZmRef<App> appRef = new App{stack, request};
  auto &app = *appRef;
  if (!mx.start()) {
    ZiLOG(Error, "zutcp", ([](auto &out) { out << "ZiMultiplex start failed"; }));
    app.final();
    ZiLog::stop();
    return 1;
  }


  for (unsigned i = 0; i < count && !app.error(); ++i) {
    ZmRef<App::Cxn> link = new App::Cxn{&app, i + 1};
    link->connect(Zutcp::Host{server}, port);
    if (const char *directory = getenv("ZUTCP_WIRE_GATE_DIR")) {
      app.waitReady();
      if (!app.error()) {
	Zi::Path path;
	path << directory << "/wire-release";
	ZiFile file{path, ZiFile::ReadOnly | ZiFile::GC}; // Fixture main waits before output.
	char value;
	bool released = file && file.read(&value, 1) == 1 && value == 'R';
	if (released) link->startRequest();
	else { app.setError("wire fixture gate failed"); app.done(); }
      }
    }
    app.wait();
    ZmBlock<>{}([&link](auto wake) {
      link->disconnect([wake = ZuMv(wake)]() mutable { wake(); });
    });
    if (app.tfo() && !i && count > 1) {
      if (const char *directory = getenv("ZUTCP_TEST_TFO_GATE_DIR")) {
	link = nullptr;
	Zi::Path path;
	path << directory << "/tfo-first-complete";
	ZiFile marker{path, ZiFile::Write | ZiFile::GC};
	constexpr auto ready = "ready\n"_z;
	if (!marker || marker.write(ready.data(), ready.length()) != Zi::OK ||
	    marker.close() != Zi::OK) { app.setError("TFO fixture marker failed"); break; }
	Zi::Path next;
	next << directory << "/tfo-next";
	ZiFile gate{next, ZiFile::ReadOnly | ZiFile::GC}; // Fixture main waits for peer/fault.
	char value;
	bool released = gate && gate.read(&value, 1) == 1 && value == 'R';
	if (!released) { app.setError("TFO fixture gate failed"); break; }
      }
    }
  }

  app.final();
  mx.stop();
  ZiLog::stop();

  return app.error() ? 1 : 0;
}

} // namespace ProtocolClient

namespace ProtocolServer {

class AppCxn;
class App : public ZmNHeap<App>, public ZutcpTest::Listener<App, AppCxn> {
friend inline ZuStringT<"Zutcp.Example.ServerApp"> ZmNHeapID(App *);
  using Base = ZutcpTest::Listener<App, AppCxn>;
public:
  using ZmNHeap<App>::operator new;
  using ZmNHeap<App>::operator delete;
  bool tfo() const { return m_tfo; }
  uint32_t maxEarlyData(AppCxn *) { return m_tfo && m_service ? 400 : 0; }
  bool allowEarlyStream(AppCxn *, uint64_t, bool) { return m_tfo && m_allowEarly; }
  void listening() { std::cerr << "listening\n" << std::flush; }
  App(Zutcp::MxPlugin *stack, ZiIP ip, unsigned port, unsigned target, bool waitEOF) :
    Base{stack, ip, uint16_t(port)}, m_localIP{ip}, m_localPort{port},
    m_target{target}, m_waitEOF{waitEOF} {
    if (m_tfo)
      if (const char *first = getenv("ZUTCP_TEST_TFO_SERVER_FIRST"))
	m_nextCxn = unsigned(ZuBox<unsigned>(first)) - 1;
  }
  ZmRef<AppCxn> accepted(const Zutcp::InitialInfo &);
  void disconnected(AppCxn *, bool);
  void endpointReady(Zutcp_::Endpoint_<App> *ep) { Base::endpointReady(ep); listening(); }
  ZiIP localIP() const { return m_localIP; }
  unsigned localPort() const { return m_localPort; }
  bool waitEOF() const { return m_waitEOF; }
  void listenFailed(bool) { setError("listen failed"); done(); }
  void done() { if (++m_doneCount >= m_target) m_done.post(); }
  void wait() { m_done.wait(); }
  void setError(const char *msg) {
    if (!m_error.xch(1))
      ZiLOG(Error, "zutcpd", ([msg = ZeString{msg}](auto &out) {
	out << "error: " << msg;
      }));
  }
  bool error() const { return m_error.load_(); }

private:
  bool			m_tfo = getenv("ZUTCP_TEST_TFO") != nullptr;
  bool			m_service = getenv("ZUTCP_TEST_TFO_SERVER_DISABLED") == nullptr;
  bool			m_allowEarly = getenv("ZUTCP_TEST_TFO_RECEIVE_DENIED") == nullptr;
  ZiIP			m_localIP;
  unsigned		m_localPort = 0;
  unsigned		m_target = 1;
  bool			m_waitEOF = false;
  alignas(Zm::CacheLineSize) ZmSemaphore m_done;
  ZmAtomic<unsigned>	m_doneCount = 0;
  ZmAtomic<unsigned>	m_error = 0;
  alignas(Zm::CacheLineSize) unsigned m_nextCxn = 0; // Rx
};

class AppCxn : public ZmNHeap<AppCxn>, public Zutcp::SrvCxn<Zutcp::MxPlugin, AppCxn> {
friend inline ZuStringT<"Zutcp.Example.SrvCxn"> ZmNHeapID(AppCxn *);
  using Base = Zutcp::SrvCxn<Zutcp::MxPlugin, AppCxn>;
public:
  using ZmNHeap<AppCxn>::operator new;
  using ZmNHeap<AppCxn>::operator delete;
  AppCxn(App *app, unsigned tfoID) : Base{app->stack()}, m_probe{app}, m_tfoID{tfoID} {
    txErrorFn(ZiTxErrorFn{[](ZeException &e) {
      ZiLOG(Error, "zutcpd", ([e](auto &out) { out << "transmit error: " << e; }));
      return false;
    }});
  }
  App *probe() const { return m_probe; }
  using Base::stageReuseISNRx;
  using Base::discardUnadoptedRx;
  using Base::releaseCxnRx;
  using Base::receivedRouted;
  void closedRx(bool peer) { probe()->disconnected(this, peer); }
  void retireLinkRoutes() { probe()->retireLinkRoutes(this); }
  void abortRx() { Base::Base::disconnect(false); }
  uint32_t maxEarlyData() { return probe()->maxEarlyData(this); }
  bool allowEarlyStream(uint64_t id, bool local) {
    return probe()->allowEarlyStream(this, id, local);
  }
  ZmRef<Zutcp::PacketBuf> allocTxPkt_(bool control = false) {
    return probe()->allocTxPkt_(control);
  }
  bool sendPktRaw(ZmRef<ZiIOBuf> packet, ZiSockAddr peer, Zutcp::EcnMark::T ecn,
      bool priority, const Zutcp::IPPath &path) {
    return probe()->sendPktRaw(ZuMv(packet), peer, ecn, priority, path);
  }
  void connected(Zutcp::Connected) {
    m_connected = true;
    if (probe()->tfo())
      std::cerr << "tfo connected=" << m_tfoID << '\n' << std::flush;
    std::cerr << "connection established\n" << std::flush;
  }
  int process(Zutcp::Reader &reader) {
    if (m_finQueued) return 0;
    auto length = reader.length();
    if (probe()->tfo() && length && !m_reported) {
      m_reported = true;
      std::cerr << "tfo process=" << m_tfoID << " early=" << unsigned(!m_connected)
	<< " bytes=" << length << '\n' << std::flush;
    }
    bool final = finReceived() && rxComplete();
    if (probe()->waitEOF() && !final) return 0;
    if (!length && !final) return 0;
    if (probe()->waitEOF() && !m_prefixSent) {
      std::cerr << "receive EOF; reply bytes=" << length << '\n' << std::flush;
    }
    if (!m_prefixSent) {
      if (!send("echo: ", false)) { complete(false); return -1; }
      m_prefixSent = true;
    }
    if (length) {
      auto n = reader.each(length, [this](auto part) -> int64_t {
	if (!send(ZuBSpan{part.data(), part.length()}, false)) return -1;
	return part.length();
      });
      if (n != int64_t(length)) { complete(false); return -1; }
      reader.clean();
      if (!retireRx(length)) { complete(false); return -1; }
    }
    if (final) {
      m_finQueued = true;
      if (!send(ZuBSpan{}, true)) { complete(false); return -1; }
    }
    return length ? 1 : 0;
  }
  void txComplete(bool ok) { complete(ok); }
  void disconnected(bool) { complete(false); }

private:
  void complete(bool ok) {
    if (m_completed.xch(1)) return;
    if (!ok) probe()->setError("echo did not complete");
    probe()->done();
  }
  App *m_probe;
  alignas(Zm::CacheLineSize) unsigned m_tfoID = 0;
  bool			m_connected = false, m_reported = false;
  bool			m_prefixSent = false; // Rx
  bool			m_finQueued = false; // Rx
  alignas(Zm::CacheLineSize) ZmAtomic<unsigned> m_completed = 0; // Rx/Tx publication
};

void App::disconnected(AppCxn *cxn, bool peer)
{
  ZmRef<AppCxn> held{cxn};
  Base::disconnected(cxn, peer);
  cxn->disconnected(peer);
}

ZmRef<AppCxn> App::accepted(const Zutcp::InitialInfo &)
{
  return new AppCxn{this, ++m_nextCxn};
}

static void usage()
{
  std::cerr << "Usage: zutcpd INTERFACE SERVER PORT [COUNT] [--wait-eof]\n"
    << std::flush;
  ::exit(1);
}

static int run(int argc, char **argv)
{
  bool waitEOF = argc > 4 && !strcmp(argv[argc - 1], "--wait-eof");
  unsigned positional = unsigned(argc) - unsigned(waitEOF);
  if (positional != 4 && positional != 5) usage();

  unsigned interface = if_nametoindex(argv[1]);
  ZiIP server = argv[2];
  unsigned port = ZuBox<unsigned>(argv[3]);
  unsigned count = 1;
  if (positional == 5) count = ZuBox<unsigned>(argv[4]);
  if (!interface || !port || !count) usage();

  ZiLog::init("zutcpd");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  ZmRef<Zpkt::Env> env = new Zpkt::Env;
  ZiMultiplex mx(
      ZiMxParams()
	.scheduler([](auto &s) {
	  s.nThreads(4)
	  .thread(1, [](auto &t) { t.isolated(1); })
	  .thread(2, [](auto &t) { t.isolated(1); })
	  .thread(4, [](auto &t) { t.isolated(true); }); })
	.rxThread(1).txThread(2), ZmRef{new Zutcp::MxPlugin});
  auto params = ZutcpTest::Params(&mx);
  if (const char *profile = getenv("ZUTCP_TEST_SERVER_OPTIONS")) {
    unsigned options = ZuBox<unsigned>(profile);
    params.sackPermitted(options & 1).windowScale(options & 2).timestamps(options & 4)
      .ecn(options & 8);
    params.maxData(131072);
  }
  if (const char *delay = getenv("ZUTCP_TEST_TLP_DELAY_US"))
    params.tlpPeerAckDelay(Zutcp::timeUS(ZuBox<unsigned>(delay)));
  auto instance = ZutcpTest::initMxPlugin(env, &mx, interface, 4, params.config());
  auto stack = instance.ptr();
  ZmRef<App> appRef = new App{stack, server, port, count, waitEOF};
  auto &app = *appRef;
  if (!mx.start()) {
    ZiLOG(Error, "zutcpd", ([](auto &out) { out << "ZiMultiplex start failed"; }));
    app.final();
    ZiLog::stop();
    return 1;
  }

  if (!app.start()) {
    ZiLOG(Error, "zutcpd", ([](auto &out) { out << "server listen failed"; }));
    app.final();
    mx.stop();
    ZiLog::stop();
    return 1;
  }

  app.wait();

  app.stop();
  app.final();
  mx.stop();
  ZiLog::stop();

  return app.error() ? 1 : 0;
}

} // namespace ProtocolServer

namespace ProtocolPair {

ZuDerive(PeerBytes, (ZtString<ZtStringHeapID<"Zutcp.NativePeer.Bytes">>));
// The fixture's bounded request/prefix and cold file-read chunk.
enum { MaxRequest = 1<<20, PrefixBytes = 260, ReadSize = 4096 };
static constexpr uint64_t ResetError = 0x123456789abcdef0ULL;

static bool waitGate(const char *name)
{
  const char *path = getenv(name);
  if (!path) return true;
  ZiFile gate{path, ZiFile::ReadOnly | ZiFile::GC};
  if (!gate) {
    ZiLOG(Error, "zutcppair", ([error = gate.error()](auto &out) {
      out << "fixture gate: " << error;
    }));
    return false;
  }
  char value;
  return gate.read(&value, 1) == 1 && value == 'R';
}

class Result {
public:
  void fail(const char *message) {
    if (!m_failed.xch(1))
      ZiLOG(Error, "zutcppair", ([message = ZeString{message}](auto &out) {
	out << "error: " << message;
      }));
  }
  bool failed() const { return m_failed.load_(); }
  void done() { m_done.post(); }
  void wait() { m_done.wait(); }
  void connected() { m_connected.post(); }
  void waitConnected() { m_connected.wait(); }
private:
  using Flag = ZmAtomic<unsigned>;
  Flag		m_failed = 0;
  ZmSemaphore	m_done, m_connected;
};

class ClientApp : public ZmNHeap<ClientApp>, public ZmPolymorph {
friend inline ZuStringT<"Zutcp.NativePeer.ClientApp"> ZmNHeapID(ClientApp *);
public:
  using ZmNHeap<ClientApp>::operator new;
  using ZmNHeap<ClientApp>::operator delete;
  class Cxn;
  ClientApp(Zutcp::MxPlugin *stack, Result &result, const PeerBytes &request,
      unsigned windowChunk = 0) :
    m_stack{stack}, m_result{result}, m_request{request}, m_windowChunk{windowChunk} { }
  Zutcp::MxPlugin *stack() const { return m_stack; }
  void final() { }
  template <typename F> void rxRun(F &&fn) { stack()->rxRun(ZuFwd<F>(fn)); }
  Result &result() { return m_result; }
  ZuCSpan request() const { return {m_request.data(), m_request.length()}; }
  unsigned windowChunk() const { return m_windowChunk; }
  bool tfo() const { return m_tfo; }
  bool earlyData(Cxn *);
  bool allowEarlyStream(Cxn *, uint64_t, bool);
  void earlyDataAccepted(Cxn *);
  void earlyDataRejected(Cxn *);
private:
  Zutcp::MxPlugin *m_stack;
  Result		&m_result;
  const PeerBytes	&m_request;
  const unsigned	m_windowChunk;
  const bool	m_tfo = getenv("ZUTCP_KERNEL_TFO");
  const bool	m_denyEarly = getenv("ZUTCP_KERNEL_TFO_DENY");
};

class ClientApp::Cxn : public ZmNHeap<ClientApp::Cxn>,
  public Zutcp::CliCxn<Zutcp::MxPlugin, ClientApp::Cxn> {
friend inline ZuStringT<"Zutcp.KernelInterop.Client"> ZmNHeapID(Cxn *);
  using Base = Zutcp::CliCxn<Zutcp::MxPlugin, Cxn>;
public:
  using ZmNHeap<Cxn>::operator new;
  using ZmNHeap<Cxn>::operator delete;
  explicit Cxn(ClientApp *app, bool reset, unsigned id) :
    Base{app->stack()}, m_probe{app}, m_reset{reset}, m_id{id} { }
  ClientApp *probe() const { return m_probe; }
  bool earlyData() { return probe()->earlyData(this); }
  bool allowEarlyStream(uint64_t id, bool local) {
    return probe()->allowEarlyStream(this, id, local);
  }
  void earlyDataAccepted() { probe()->earlyDataAccepted(this); }
  void earlyDataRejected() { probe()->earlyDataRejected(this); }
  void connected(Zutcp::Connected) {
    std::cerr << "transport=AF_XDP role=client connected=1\n";
    std::cerr << std::flush;
    if (probe()->tfo()) std::cerr << "tfo native_client connected=" << m_id << "\n";
    if (m_reset) { startRequest(); probe()->result().connected(); }
    else if (getenv("ZUTCP_KERNEL_START_GATE")) probe()->result().connected();
    else startRequest();
  }
  void startRequest() {
    auto request = probe()->request();
    if (m_reset) {
      if (!send(ZuCSpan{request.data(), PrefixBytes}, false)) complete(false);
      return;
    }
    if (probe()->tfo()) {
      if (!queueEarly()) return;
      if (!send(ZuCSpan{request.data() + PrefixBytes, request.length() - PrefixBytes}, true))
	complete(false);
      return;
    }
    unsigned chunk = probe()->windowChunk();
    if (!chunk) { if (!send(request, true)) complete(false); return; }
    {
      auto tx = writer();
      std::cerr << "window writer max=" << tx.maxSize() << " head=" << tx.headRoom() << " tail="
	<< tx.tailRoom() << " capacity=" << tx.maxSize() - tx.headRoom() - tx.tailRoom()
	<< " chunk=" << chunk << "\n";
    }
    unsigned total = request.length();
    for (unsigned offset = 0; offset < total;) {
      unsigned length = total - offset;
      if (length > chunk) length = chunk;
      if (!send(ZuCSpan{request.data() + offset, length},
	  offset + length == total)) { complete(false); return; }
      offset += length;
    }
  }
  unsigned id() const { return m_id; }
  bool queueEarly() {
    if (m_earlyQueued) return true;
		m_earlyQueued = true;
    std::cerr << "tfo native_client queued=" <<	m_id << "\n";
    if (send(ZuCSpan{probe()->request().data(), PrefixBytes}, false)) return true;
    complete(false);
    return false;
  }
  void resetRequest() {
    if (!disconnect(ResetError)) complete(false);
  }
  int process(Zutcp::Reader &reader) {
    auto length = reader.length();
    if (length) {
      auto n = reader.each(length, [](auto part) -> int64_t {
	std::cout << ZuCSpan{part.data(), part.length()};
	return std::cout ? int64_t(part.length()) : 0;
      });
      if (n != int64_t(length)) { complete(false); return -1; }
      reader.clean();
      if (!retireRx(length)) { complete(false); return -1; }
    }
    if (finReceived() && rxComplete()) {
      std::cout << std::flush;
      complete(bool(std::cout));
    }
    return length ? 1 : 0;
  }
  void connectFailed(bool) { complete(false); }
  void disconnected(bool peer) {
    if (m_reset) {
      std::cerr << "reset native_client peer=" << unsigned(peer) << " local_error="
	<< closeError() << "\n";
      complete(!peer && closeError() == ResetError);
    } else if (!m_completed) complete(false);
  }
private:
  void complete(bool ok) {
    if (m_completed) return;
		m_completed = true;
    if (!ok) {
      probe()->result().fail("native client did not complete the exact EOF exchange");
      probe()->result().connected();
    }
    probe()->result().done();
  }
  ClientApp *m_probe;
  bool		m_completed = false; // Rx
  const bool	m_reset;
  const unsigned	m_id;
  bool		m_earlyQueued = false; // Rx
};

bool ClientApp::earlyData(Cxn *link)
{
  return m_tfo && link->queueEarly() && allowEarlyStream(link, 0, true);
}

bool ClientApp::allowEarlyStream(Cxn *link, uint64_t, bool)
{
  return m_tfo && (!m_denyEarly || link->id() == 1);
}

void ClientApp::earlyDataAccepted(Cxn *link)
{
  if (m_tfo) std::cerr << "tfo native_client accepted=" << link->id() << "\n";
}

void ClientApp::earlyDataRejected(Cxn *link)
{
  if (m_tfo) std::cerr << "tfo native_client rejected=" << link->id() << "\n";
}

class ServerCxn;
class ServerApp : public ZmNHeap<ServerApp>, public ZutcpTest::Listener<ServerApp, ServerCxn> {
friend inline ZuStringT<"Zutcp.NativePeer.ServerApp"> ZmNHeapID(ServerApp *);
  using Base = ZutcpTest::Listener<ServerApp, ServerCxn>;
public:
  using ZmNHeap<ServerApp>::operator new;
  using ZmNHeap<ServerApp>::operator delete;
  ServerApp(Zutcp::MxPlugin *stack, Result &result, ZiIP address, unsigned port, unsigned count) :
    Base{stack, address, uint16_t(port)}, m_result{result}, m_address{address},
    m_port{port}, m_target{count} { }
  ZiIP localIP() const { return m_address; }
  unsigned localPort() const { return m_port; }
  ZmRef<ServerCxn> accepted(const Zutcp::InitialInfo &);
  void disconnected(ServerCxn *, bool);
  bool tfo() const { return m_tfo; }
  uint32_t maxEarlyData(ServerCxn *) { return m_tfo ? 400 : 0; }
  bool allowEarlyStream(ServerCxn *, uint64_t, bool);
  void holdReader(ServerCxn *);
  void releaseReader();
  void listening() {
    auto stack = this->stack();
    std::cerr << "transport=AF_XDP role=server ready=" << unsigned(stack && stack->ready()) << "\n";
    std::cerr << std::flush;
  }
  void endpointReady(Zutcp_::Endpoint_<ServerApp> *ep) { Base::endpointReady(ep); listening(); }
  void listenFailed(bool) { m_result.fail("native listen failed"); m_result.done(); }
  void completed(bool ok) {
    if (!ok) m_result.fail("native server did not complete the exact EOF exchange");
    if (++m_completed >= m_target || !ok) m_result.done();
  }
private:
  using Flag = ZmAtomic<unsigned>;
  Result		&m_result;
  ZiIP		m_address;
  unsigned	m_port = 0, m_target = 1;
  const bool	m_tfo = getenv("ZUTCP_KERNEL_TFO");
  const bool	m_denyEarly = getenv("ZUTCP_KERNEL_TFO_DENY");
  alignas(Zm::CacheLineSize) Flag m_completed = 0;
  alignas(Zm::CacheLineSize) unsigned m_accepted = 0; // Rx accepted count
  ZmRef<ServerCxn>	m_held; // Rx, fixture hold through the existing Reader only
};

class ServerCxn : public ZmNHeap<ServerCxn>, public Zutcp::SrvCxn<Zutcp::MxPlugin, ServerCxn> {
friend inline ZuStringT<"Zutcp.KernelInterop.Server"> ZmNHeapID(ServerCxn *);
  using Base = Zutcp::SrvCxn<Zutcp::MxPlugin, ServerCxn>;
public:
  using ZmNHeap<ServerCxn>::operator new;
  using ZmNHeap<ServerCxn>::operator delete;
  explicit ServerCxn(ServerApp *app, bool reset, unsigned id) :
    Base{app->stack()}, m_probe{app}, m_reset{reset}, m_id{id} { }
  ServerApp *probe() const { return m_probe; }
  using Base::stageReuseISNRx;
  using Base::discardUnadoptedRx;
  using Base::releaseCxnRx;
  using Base::receivedRouted;
  void closedRx(bool peer) { probe()->disconnected(this, peer); }
  void retireLinkRoutes() { probe()->retireLinkRoutes(this); }
  void abortRx() { Base::Base::disconnect(false); }
  uint32_t maxEarlyData() { return probe()->maxEarlyData(this); }
  bool allowEarlyStream(uint64_t id, bool local) {
    return probe()->allowEarlyStream(this, id, local);
  }
  ZmRef<Zutcp::PacketBuf> allocTxPkt_(bool control = false) {
    return probe()->allocTxPkt_(control);
  }
  bool sendPktRaw(ZmRef<ZiIOBuf> packet, ZiSockAddr peer, Zutcp::EcnMark::T ecn,
      bool priority, const Zutcp::IPPath &path) {
    return probe()->sendPktRaw(ZuMv(packet), peer, ecn, priority, path);
  }
  void connected(Zutcp::Connected) {
		m_connected = true;
    if (probe()->tfo()) std::cerr << "tfo native_server connected=" << m_id << "\n";
    std::cerr << "transport=AF_XDP role=server connected=1\n";
    std::cerr << std::flush;
    if (m_reset) {
      auto stack = probe()->stack();
      auto record = Base::closedSnapshotRx(Base::closedKeyRx());
      if (!record.timestamps || !Base::admittedRx()) { complete(false); return; }
		m_fixtureHistory = stack->rememberClosed(record);
      Zutcp::ClosedTuple indexed;
      bool seeded = m_fixtureHistory.id && stack->closedTuple(record.key, indexed) &&
	indexed.id ==	m_fixtureHistory.id;
      std::cerr << "reset fixture_history_seed id="
	<< m_fixtureHistory.id << " timestamps="
	<< unsigned(record.timestamps) << " live_slot=" << unsigned(Base::admittedRx())
	<< "\n";
      if (!seeded) complete(false);
    }
  }
  unsigned id() const { return m_id; }
  int process(Zutcp::Reader &reader) {
    if (m_replied) return 0;
    auto length = reader.length();
    if (length && probe()->tfo())
      std::cerr << "tfo native_server process=" << m_id << " bytes=" << unsigned(length)
	<< " connected=" << unsigned(m_connected) << "\n";
    if (length && m_holdReader) { probe()->holdReader(this); return 0; }
    if (m_request.length() + length > MaxRequest) { complete(false); return -1; }
    if (length) {
      auto n = reader.each(length, [this](auto part) -> int64_t {
	m_request << ZuCSpan{part.data(), part.length()};
	return part.length();
      });
      if (n != int64_t(length)) { complete(false); return -1; }
      reader.clean();
      if (!retireRx(length)) { complete(false); return -1; }
    }
    if (finReceived() && rxComplete()) {
		m_replied = true;
      if (!send("echo: ", false) || !send(ZuCSpan{m_request.data(), m_request.length()}, true)) {
	complete(false);
	return -1;
      }
    }
    return length ? 1 : 0;
  }
  void releaseReader() {
		m_holdReader = false;
    std::cerr << "window reader=released\n";
    std::cerr << std::flush;
    Base::resumeApplicationRx(Base::dataGen());
  }
  void txComplete(bool ok) { if (!m_reset) complete(ok); }
  void disconnected(bool peer) {
    if (m_reset) {
      std::cerr << "reset native_server peer=" << unsigned(peer) << " bytes="
	<< unsigned(m_request.length()) << "\n";
      Zutcp::ClosedTuple current;
      bool found = probe()->stack()->closedTuple(m_fixtureHistory.key, current);
      bool slot = Base::admittedRx();
      std::cerr << "reset fixture_history_retired seed="
	<< m_fixtureHistory.id << " current="
	<< current.id << " fin=" << unsigned(current.fin)
	<< " slot=" << unsigned(slot) << "\n";
      bool retired = found && m_fixtureHistory.id && current.id != m_fixtureHistory.id &&
	!current.fin && current.timestamps && !slot;
      complete(peer && m_request.length() == PrefixBytes && !m_replied && retired);
    } else complete(false);
  }
private:
  void complete(bool ok) {
    if (m_completed.xch(1)) return;
    probe()->completed(ok);
  }
  ServerApp *m_probe;
  using Closed = Zutcp::ClosedTuple;
  using Flag = ZmAtomic<unsigned>;
  alignas(Zm::CacheLineSize) const bool m_reset;
  const unsigned	m_id;
  alignas(Zm::CacheLineSize) PeerBytes m_request; // Rx request bytes
  bool		m_connected = false; // Rx
  Closed		m_fixtureHistory; // Rx, explicitly seeded while this owner is live
  bool		m_replied = false; // Rx
  bool		m_holdReader = getenv("ZUTCP_KERNEL_READ_GATE"); // Rx
  alignas(Zm::CacheLineSize) Flag m_completed = 0; // Rx/Tx final publication
};

void ServerApp::disconnected(ServerCxn *link, bool peer)
{
  ZmRef<ServerCxn> keep{link};
  Base::disconnected(link, peer);
  keep->disconnected(peer);
}

void ServerApp::holdReader(ServerCxn *link)
{
  if (m_held) return;
  m_held = link;
  std::cerr << "window reader=held\n";
  std::cerr << std::flush;
}

void ServerApp::releaseReader()
{
  rxRun([this] {
    auto link = ZuMv(m_held);
    if (!link) {
		m_result.fail("fixture released a Reader that was never held");
		m_result.done();
    }
    else link->releaseReader();
  });
}

ZmRef<ServerCxn> ServerApp::accepted(const Zutcp::InitialInfo &)
{
  unsigned id = ++m_accepted;
  return new ServerCxn{this, id == 1 && getenv("ZUTCP_KERNEL_EXPECT_RESET"), id};
}

bool ServerApp::allowEarlyStream(ServerCxn *link, uint64_t, bool)
{
  return m_tfo && (!m_denyEarly || link->id() == 1);
}

static bool readFile(const char *name, PeerBytes &bytes)
{
  ZiFile file{name, ZiFile::ReadOnly | ZiFile::GC};
  if (!file) {
    ZiLOG(Error, "zutcppair", ([error = file.error()](auto &out) {
      out << "request file: " << error;
    }));
    return false;
  }
  char buf[ReadSize]; // Cold bounded utility input in4K chunks.
  while (true) {
    int n = file.read(buf, sizeof(buf), false);
    if (n == Zi::EndOfFile) return true;
    if (n < 0 || bytes.length() + unsigned(n) > MaxRequest) return false;
    bytes << ZuCSpan{buf, unsigned(n)};
  }
}

template <typename Params>
static void options(Params &params)
{
  if (const char *value = getenv("ZUTCP_PEER_OPTIONS")) {
    unsigned mask = unsigned(ZuBox<unsigned>(value));
    params.sackPermitted(mask & 1).windowScale(mask & 2).timestamps(mask & 4).ecn(mask & 8);
  }
}

static void usage()
{
  std::cerr << "Usage: zutcppair server INTERFACE ADDRESS PORT COUNT\n"
    "       zutcppair client INTERFACE ADDRESS PORT REQUEST_FILE [COUNT]\n";
  exit(2);
}

static int run(int argc, char **argv)
{
  if (argc != 6 && argc != 7) usage();
  bool server = !strcmp(argv[1], "server");
  if (!server && strcmp(argv[1], "client")) usage();
  if (server && argc != 6) usage();
  unsigned interface = if_nametoindex(argv[2]);
  ZiIP address{argv[3]};
  unsigned port = unsigned(ZuBox<unsigned>(argv[4]));
  unsigned count = server ? unsigned(ZuBox<unsigned>(argv[5])) :
    (argc == 7 ? unsigned(ZuBox<unsigned>(argv[6])) : 1);
  if (!interface || !port || port > 65535 || !count) usage();
  ZiLog::init("zutcppair");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZuGuard logStop{[] { ZiLog::stop(); }};
  PeerBytes request;
  if (!server && !readFile(argv[5], request)) return 1;
  if (!server && (getenv("ZUTCP_KERNEL_TFO") || getenv("ZUTCP_KERNEL_RESET_GATE")) &&
      request.length() < PrefixBytes) {
    ZiLOG(Error, "zutcppair", ([](auto &out) {
      out << "fixture request is shorter than its bounded prefix";
    }));
    return 1;
  }
  Result result;
  ZmRef<Zpkt::Env> env = new Zpkt::Env;
  ZiMultiplex mx{ZiMxParams().scheduler([](auto &s) {
    s.nThreads(4).thread(1, [](auto &t) { t.isolated(1); })
      .thread(2, [](auto &t) { t.isolated(1); })
      .thread(4, [](auto &t) { t.isolated(true); });
  }).rxThread(1).txThread(2), ZmRef{new Zutcp::MxPlugin}};
  auto xdp = ZutcpTest::openXdp(env, &mx, interface, 4);
  auto payload = Zutcp::tcpPayload(xdp->mtu(), address.type());
  auto params = ZutcpTest::Params{&mx};
  options(params);
  if (server && getenv("ZUTCP_KERNEL_READ_GATE")) params.maxData(4096);
  bool windowClient = !server && getenv("ZUTCP_KERNEL_WINDOW_CLIENT");
  if (windowClient) params.maxPayload(payload).sackPermitted(false).timestamps(true);
  auto instance = ZutcpTest::initMxPlugin(ZmRef{static_cast<Zutcp::MxPlugin *>(mx.plugin())},
    env, &mx, ZuMv(xdp), params.config());
  auto stack = instance.ptr();
  if (server) {
    ZmRef<ServerApp> appRef = new ServerApp{stack, result, address, port, count};
    auto &app = *appRef;
    bool listening = mx.start() && app.start();
    if (!listening) result.fail("native server start");
    else {
      if (getenv("ZUTCP_KERNEL_READ_GATE")) {
	if (!waitGate("ZUTCP_KERNEL_READ_GATE")) result.fail("native Reader release gate");
	else app.releaseReader();
      }
      if (!result.failed()) result.wait();
    }
    if (listening) app.stop();
    app.final();
    mx.stop();
  } else {
    unsigned windowChunk = windowClient ? (payload - 12)>>1 : 0;
    ZmRef<ClientApp> appRef = new ClientApp{stack, result, request, windowChunk};
    auto &app = *appRef;
    if (!mx.start()) result.fail("native client start");
    else {
      std::cerr << "transport=AF_XDP role=client ready=" << unsigned(stack->ready()) << "\n";
      for (unsigned i = 0; i < count && !result.failed(); ++i) {
	bool reset = !i && getenv("ZUTCP_KERNEL_RESET_GATE");
	ZmRef<ClientApp::Cxn> link = new ClientApp::Cxn{&app, reset, i + 1};
	link->connect(Zutcp::Host{argv[3]}, port);
	if (reset) {
	  result.waitConnected();
	  if (!result.failed()) {
	    if (!waitGate("ZUTCP_KERNEL_RESET_GATE")) result.fail("native reset gate");
	    else app.rxRun([link] { link->resetRequest(); });
	  }
	}
	if (getenv("ZUTCP_KERNEL_START_GATE")) {
	  result.waitConnected();
	  if (!result.failed()) {
	    if (!waitGate("ZUTCP_KERNEL_START_GATE")) result.fail("native request start gate");
	    else app.rxRun([link] { link->startRequest(); });
	  }
	}
	if (!result.failed()) result.wait();
	ZmBlock<>{}([&link](auto wake) {
	  link->disconnect([wake = ZuMv(wake)]() mutable { wake(); });
	});
	std::cerr << "native disconnect_complete=" << i + 1 << "\n";
      }
    }
    app.final();
    mx.stop();
  }
  auto admission = stack->cxnAdmission();
  auto tx = stack->txAdmission();
  bool drained = !stack->ready() && !admission.total && !admission.pending &&
    !tx.outstanding;
  std::cerr << "transport=AF_XDP stopped=" << unsigned(!stack->ready()) << " cxns="
    << admission.total << " pending=" << admission.pending << " tx=" << tx.outstanding
    << " control_count=" << tx.controlCount << "\n";
  if (!drained) result.fail("native owner/provider drain incomplete");
  return result.failed() ? 1 : 0;
}

} // namespace ProtocolPair

static void exchange(bool v6, const char *mode)
{
  ZuTestScopeRT(exchange);
  Zi::Name name;
  name << (v6 ? "v6-" : "v4-") << mode;
  auto directory = ZiTestResidue::dir(name);
  auto child = fork();
  if (!child) {
    execl("/bin/bash", "bash", "./zutcppairfixture.sh", v6 ? "6" : "4", mode,
      directory.data(), static_cast<char *>(nullptr));
    _exit(127);
  }
  int status = -1;
  if (child > 0) {
    pid_t result;
    do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
    if (result != child) status = -1;
  }
  ZuCheckRT(!status, log_("native IPv", v6 ? 6 : 4, ' ', mode,
    " exact multi-packet exchange/drain; logs ", directory));
}

static void testV4() { exchange(false, "plain"); }
static void testV6() { exchange(true, "plain"); }
static void testHalfCloseV4() { exchange(false, "wait-eof"); }
static void testHalfCloseV6() { exchange(true, "wait-eof"); }
static void testLossV4() { exchange(false, "loss"); }
static void testLossV6() { exchange(true, "loss"); }
static void testDataLossV4() { exchange(false, "data-loss"); }
static void testDataLossV6() { exchange(true, "data-loss"); }
static void testDupV4() { exchange(false, "duplicate"); }
static void testDupV6() { exchange(true, "duplicate"); }
static void testReorderV4() { exchange(false, "reorder"); }
static void testReorderV6() { exchange(true, "reorder"); }
static void testAckLossV4() { exchange(false, "ack-loss"); }
static void testAckLossV6() { exchange(true, "ack-loss"); }
static void testPartialAckV4() { exchange(false, "partial-ack"); }
static void testPartialAckV6() { exchange(true, "partial-ack"); }
static void testSACKHolesV4() { exchange(false, "sack-holes"); }
static void testSACKHolesV6() { exchange(true, "sack-holes"); }
static void testRenegingV4() { exchange(false, "sack-reneging"); }
static void testRenegingV6() { exchange(true, "sack-reneging"); }
static void testTLPV4() { exchange(false, "tail-matrix"); }
static void testTLPV6() { exchange(true, "tail-matrix"); }
static void testECNV4() { exchange(false, "ecn-matrix"); }
static void testECNV6() { exchange(true, "ecn-matrix"); }
static void testTFOV4() { exchange(false, "tfo-matrix"); }
static void testTFOV6() { exchange(true, "tfo-matrix"); }
static void testOptionsV4() { exchange(false, "option-matrix"); }
static void testOptionsV6() { exchange(true, "option-matrix"); }

int main(int argc, char **argv)
{
  if (argc > 1 && !strcmp(argv[1], "--protocol-client"))
    return ProtocolClient::run(argc - 1, argv + 1);
  if (argc > 1 && !strcmp(argv[1], "--protocol-server"))
    return ProtocolServer::run(argc - 1, argv + 1);
  if (argc > 1 && !strcmp(argv[1], "--protocol-pair"))
    return ProtocolPair::run(argc - 1, argv + 1);
  parse(argc, argv);
  if (geteuid() || !getenv("ZPKT_RUN_FIXTURE")) {
    std::cout << "1..0 # SKIP requires privileged private namespace fixture\n";
    return 0;
  }
  ZiTestResidue::init("zutcppairtest");
  ZuTestMain();
  ZuTestCall(testV4);
  ZuTestCall(testV6);
  ZuTestCall(testHalfCloseV4);
  ZuTestCall(testHalfCloseV6);
  ZuTestCall(testLossV4);
  ZuTestCall(testLossV6);
  ZuTestCall(testDataLossV4);
  ZuTestCall(testDataLossV6);
  ZuTestCall(testDupV4);
  ZuTestCall(testDupV6);
  ZuTestCall(testReorderV4);
  ZuTestCall(testReorderV6);
  ZuTestCall(testAckLossV4);
  ZuTestCall(testAckLossV6);
  ZuTestCall(testPartialAckV4);
  ZuTestCall(testPartialAckV6);
  ZuTestCall(testSACKHolesV4);
  ZuTestCall(testSACKHolesV6);
  ZuTestCall(testRenegingV4);
  ZuTestCall(testRenegingV6);
  ZuTestCall(testOptionsV4);
  ZuTestCall(testOptionsV6);
  ZuTestCall(testECNV4);
  ZuTestCall(testECNV6);
  ZuTestCall(testTLPV4);
  ZuTestCall(testTLPV6);
  ZuTestCall(testTFOV4);
  ZuTestCall(testTFOV6);
}
