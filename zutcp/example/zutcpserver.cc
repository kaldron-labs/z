//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <stdlib.h>
#include <string.h>
#include <net/if.h>

#include <iostream>

#include <zlib/ZmHeap.hh>
#include <zlib/ZtString.hh>
#include <zlib/Zutcp.hh>

class AppCxn;
class App : public Zutcp::Server<App, AppCxn> {
public:
  bool tfo() const { return m_tfo; }
  uint32_t maxEarlyData(AppCxn *) { return m_tfo && m_service ? 400 : 0; }
  bool allowEarlyStream(AppCxn *, uint64_t, bool) { return m_tfo && m_allowEarly; }
  App(ZiIP ip, unsigned port, unsigned target, bool waitEOF) :
    m_localIP{ip}, m_localPort{port}, m_target{target}, m_waitEOF{waitEOF} {
    if (m_tfo)
      if (const char *first = getenv("ZUTCP_TEST_TFO_SERVER_FIRST"))
	m_nextCxn = unsigned(ZuBox<unsigned>(first)) - 1;
  }
  ZmRef<AppCxn> accepted(const Zutcp::InitialInfo &);
  ZiIP localIP() const { return m_localIP; }
  unsigned localPort() const { return m_localPort; }
  bool waitEOF() const { return m_waitEOF; }
  void listenFailed(bool) { setError("listen failed"); done(); }
  void done() { if (++m_doneCount >= m_target) m_done.post(); }
  void wait() { m_done.wait(); }
  void setError(const char *msg) {
    if (!m_error.xch(1))
      ZiLOG(Error, "zutcpserver", ([msg = ZeString{msg}](auto &out) {
	out << "error: " << msg;
      }));
  }
  bool error() const { return m_error.load_(); }

private:
  bool			m_tfo = getenv("ZUTCP_TEST_TFO") != nullptr;
  bool			m_service = getenv("ZUTCP_TEST_TFO_SERVER_DISABLED") == nullptr;
  bool			m_allowEarly = getenv("ZUTCP_TEST_TFO_RECEIVE_DENIED") == nullptr;
  unsigned		m_nextCxn = 0;
  ZmSemaphore		m_done;
  ZiIP			m_localIP;
  unsigned		m_localPort = 0;
  unsigned		m_target = 1;
  bool			m_waitEOF = false;
  ZmAtomic<unsigned>	m_doneCount = 0;
  ZmAtomic<unsigned>	m_error = 0;
};

class AppCxn : public ZmNHeap<AppCxn>, public Zutcp::SrvCxn<App, AppCxn> {
friend inline ZuStringT<"Zutcp.Example.SrvCxn"> ZmNHeapID(AppCxn *);
  using Base = Zutcp::SrvCxn<App, AppCxn>;
public:
  using ZmNHeap<AppCxn>::operator new;
  using ZmNHeap<AppCxn>::operator delete;
  AppCxn(App *app, unsigned tfoID) : Base{app}, m_tfoID{tfoID} {
    txErrorFn(ZiTxErrorFn{[](ZeException &e) {
      ZiLOG(Error, "zutcpserver", ([e](auto &out) { out << "transmit error: " << e; }));
      return false;
    }});
  }
  void connected(Zutcp::Connected) {
    m_connected = true;
    if (app()->tfo())
      std::cerr << "tfo connected=" << m_tfoID << '\n' << std::flush;
    std::cerr << "connection established\n" << std::flush;
  }
  int process(Zutcp::Reader &reader) {
    if (m_finQueued) return 0;
    auto length = reader.length();
    if (app()->tfo() && length && !m_reported) {
      m_reported = true;
      std::cerr << "tfo process=" << m_tfoID << " early=" << unsigned(!m_connected)
	<< " bytes=" << length << '\n' << std::flush;
    }
    bool final = finReceived() && rxComplete();
    if (app()->waitEOF() && !final) return 0;
    if (!length && !final) return 0;
    if (app()->waitEOF() && !m_prefixSent) {
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
    if (!ok) app()->setError("echo did not complete");
    app()->done();
  }
  unsigned		m_tfoID = 0;
  bool			m_connected = false, m_reported = false;
  bool			m_prefixSent = false; // Rx
  bool			m_finQueued = false; // Rx
  ZmAtomic<unsigned>	m_completed = 0; // Rx/Tx completion publication
};

ZmRef<AppCxn> App::accepted(const Zutcp::InitialInfo &)
{
  return new AppCxn{this, ++m_nextCxn};
}

static void usage()
{
  std::cerr << "Usage: zutcpserver INTERFACE SERVER PORT [COUNT] [--wait-eof]\n"
    << std::flush;
  ::exit(1);
}

int main(int argc, char **argv)
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

  ZiLog::init("zutcpserver");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  ZiMultiplex mx(
      ZiMxParams()
	.scheduler([](auto &s) {
	  s.nThreads(3)
	  .thread(1, [](auto &t) { t.isolated(1); })
	  .thread(2, [](auto &t) { t.isolated(1); }); })
	.rxThread(1).txThread(2));
  mx.plugin(new Zutcp::Stack{&mx, {.interface = interface}});


  App app{server, port, count, waitEOF};

  auto params = Zutcp::ServerParams(&mx);
  if (const char *profile = getenv("ZUTCP_TEST_SERVER_OPTIONS")) {
    unsigned options = ZuBox<unsigned>(profile);
    params.sackPermitted(options & 1).windowScale(options & 2).timestamps(options & 4)
      .ecn(options & 8);
    params.maxData(131072);
  }
  if (const char *delay = getenv("ZUTCP_TEST_TLP_DELAY_US"))
    params.tlpPeerAckDelay(Zutcp::timeUS(ZuBox<unsigned>(delay)));
  if (!app.init(params)) {
    ZiLOG(Error, "zutcpserver", ([](auto &out) { out << "server initialization failed"; }));
    mx.stop();
    ZiLog::stop();
    return 1;
  }

  if (!mx.start()) {
    ZiLOG(Error, "zutcpserver", ([](auto &out) { out << "ZiMultiplex start failed"; }));
    app.final();
    ZiLog::stop();
    return 1;
  }

  if (!app.start()) {
    ZiLOG(Error, "zutcpserver", ([](auto &out) { out << "server listen failed"; }));
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
