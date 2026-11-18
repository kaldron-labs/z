//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details).

// Ztcp application peer for independent kernel interoperability.
#include <stdlib.h>
#include <string.h>
#include <net/if.h>
#include <iostream>

#include <zlib/ZmBlock.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmQueue.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZiFile.hh>
#include <zlib/Ztcp.hh>
#include <zlib/Zutcp.hh>

ZuDerive(PeerBytes, (ZtString<ZtStringHeapID<"Zutcp.NativePeer.Bytes">>));
enum { MaxRequest = 1<<20, ReadSize = 4096, AppTx = 4, Sender = 5 };

class Result {
public:
  Result(ZiMultiplex *mx) : m_mx{mx} { }
  void fail(const char *message) {
    if (!m_failed.xch(1))
      ZiLOG(Error, "zutcppair", ([message = ZeString{message}](auto &out) {
	out << "error: " << message;
      }));
    m_event.post();
  }
  bool failed() const { return m_failed.load_(); }
  void closeTx(ZmRef<ZiConnection> cxn) { m_closeTx.push(ZuMv(cxn)); m_event.post(); }
  void done() { ++m_done; m_event.post(); }
  void wait(unsigned count) {
    while (m_done.load_() < count && !failed()) {
      m_event.wait();
      while (auto cxn = m_closeTx.shift()) {
	ZmBlock<>{}([this](auto wake) {
	  m_mx->run([wake = ZuMv(wake)]() mutable { wake(); }, AppTx);
	});
	cxn->sync();
	cxn->disconnect();
      }
    }
  }
private:
  // Existing frozen ZiConnection objects cannot embed application event links.
  using Closes = ZmQueue<ZmRef<ZiConnection>, ZmQueueLock<ZmLock,
    ZmQueueHeapID<"Zutcp.Peer.Closes">>>;
  ZiMultiplex *m_mx;
  Closes m_closeTx;
  ZmSemaphore m_event;
  ZmAtomic<unsigned> m_failed = 0, m_done = 0;
};

struct ClientApp : public Ztcp::Client<ClientApp> {
  class Cxn;
  using Link = Cxn;
  Result *result;
  const PeerBytes *request;
  ClientApp(Result *result_, const PeerBytes *request_) : result{result_}, request{request_} { }
};

class ClientApp::Cxn : public ZmNHeap<Cxn>, public Ztcp::CliLink<ClientApp, Cxn> {
friend inline ZuStringT<"Zutcp.KernelInterop.Client"> ZmNHeapID(Cxn *);
  using Base = Ztcp::CliLink<ClientApp, Cxn>;
public:
  using ZmNHeap<Cxn>::operator new;
  using ZmNHeap<Cxn>::operator delete;
  Cxn(ClientApp *app) : Base{app} { }
  void connected(Ztcp::Connected) {
    std::cerr << "backend=AF_XDP role=client connected=1\n" << std::flush;
    auto tx = txStream();
    tx << *app()->request;
    if (!tx.flush()) app()->result->fail("native client send failed");
    app()->result->closeTx(ZmRef<ZiConnection>{cxn()});
  }
  int process(Ztcp::RxStream &rx) {
    auto length = rx.length();
    while (rx) {
      auto part = rx.span();
      std::cout << ZuCSpan{part.data(), part.length()};
      m_received += part.length();
      rx.advance(part.length());
    }
    return length ? 1 : 0;
  }
  void connectFailed(bool) { app()->result->fail("native client connect failed"); }
  void disconnected(bool) {
    std::cout << std::flush;
    if (!std::cout || m_received != app()->request->length() + 6)
      app()->result->fail("native client did not complete exact EOF exchange");
    app()->result->done();
  }
private:
  uint64_t m_received = 0;
};

class ServerCxn;
class ServerApp : public Ztcp::Server<ServerApp> {
public:
  using Link = ServerCxn;
  ServerApp(Result *result, ZiIP address, uint16_t port, unsigned count) :
    m_result{result}, m_address{address}, m_port{port}, m_count{count} { }
  Result *result() const { return m_result; }
  ZiIP localIP() const { return m_address; }
  uint16_t localPort() const { return m_port; }
  unsigned nAccepts() const { return m_count; }
  ZiConnection *accepted(const ZiCxnInfo &);
  void listening(const ZiListenInfo &) {
    std::cerr << "backend=AF_XDP role=server ready=1\n" << std::flush;
  }
  void listenFailed(bool) { m_result->fail("native listen failed"); }
private:
  Result *m_result;
  ZiIP m_address;
  uint16_t m_port;
  unsigned m_count;
};

class ServerCxn : public ZmNHeap<ServerCxn>, public Ztcp::SrvLink<ServerApp, ServerCxn> {
friend inline ZuStringT<"Zutcp.KernelInterop.Server"> ZmNHeapID(ServerCxn *);
  using Base = Ztcp::SrvLink<ServerApp, ServerCxn>;
public:
  using ZmNHeap<ServerCxn>::operator new;
  using ZmNHeap<ServerCxn>::operator delete;
  ServerCxn(ServerApp *app) : Base{app} { }
  class Wire : public ZmNHeap<Wire>, public Ztcp::SrvCxn<ServerCxn> {
  friend inline ZuStringT<"Zutcp.Peer.Wire"> ZmNHeapID(Wire *);
    using Base = Ztcp::SrvCxn<ServerCxn>;
  public:
    using ZmNHeap<Wire>::operator new;
    using ZmNHeap<Wire>::operator delete;
    Wire(ServerCxn *link, const ZiCxnInfo &info) : Base{link, info}, m_link{link} { }
    void connected(ZiRxContext &io) override {
      Base::connected(io);
      io.fn = ZiRxFn{[link = m_link, data = ZuMv(io.fn)](ZiRxContext &io) mutable {
	if (io.buf && !io.buf->length) {
	  auto owner = ZmRef<ServerCxn>{link};
	  io.complete();
	  owner->app()->rxRun([owner = ZuMv(owner)] { owner->eof(); });
	  return true;
	}
	return data.as<ZiRxFn>()(io);
      }};
    }
  private:
    ServerCxn *m_link;
  };
  void connected(Ztcp::Connected) {
    std::cerr << "backend=AF_XDP role=server connected=1\n" << std::flush;
  }
  int process(Ztcp::RxStream &rx) {
    auto length = rx.length();
    if (length > MaxRequest - m_request.length()) {
      app()->result()->fail("native request too long");
      return -1;
    }
    while (rx) {
      auto part = rx.span();
      m_request << ZuCSpan{part.data(), part.length()};
      rx.advance(part.length());
    }
    return length ? 1 : 0;
  }
  void eof() {
    m_eof = true;
    auto tx = txStream();
    tx << "echo: " << m_request;
    if (!tx.flush()) app()->result()->fail("native server send failed");
    app()->result()->closeTx(ZmRef<ZiConnection>{cxn()});
  }
  void disconnected(bool) {
    if (!m_eof) app()->result()->fail("native server disconnected before request EOF");
    app()->result()->done();
  }
private:
  PeerBytes m_request;
  bool m_eof = false;
};

ZiConnection *ServerApp::accepted(const ZiCxnInfo &info)
{
  return new ServerCxn::Wire{new ServerCxn{this}, info};
}

static bool readFile(const char *name, PeerBytes &bytes)
{
  ZiFile file{name, ZiFile::ReadOnly | ZiFile::GC};
  if (!file) return false;
  char buf[ReadSize];
  while (true) {
    int n = file.read(buf, sizeof(buf), false);
    if (n == Zi::EndOfFile) return true;
    if (n < 0 || bytes.length() + unsigned(n) > MaxRequest) return false;
    bytes << ZuCSpan{buf, unsigned(n)};
  }
}

static void usage()
{
  std::cerr << "Usage: zutcppair server INTERFACE ADDRESS PORT COUNT\n"
    "       zutcppair client INTERFACE ADDRESS PORT REQUEST_FILE [COUNT]\n";
  exit(2);
}

int main(int argc, char **argv)
{
  if (argc != 6 && argc != 7) usage();
  bool server = !strcmp(argv[1], "server");
  if ((!server && strcmp(argv[1], "client")) || (server && argc != 6)) usage();
  unsigned interface = if_nametoindex(argv[2]);
  ZiIP address{argv[3]};
  unsigned port = ZuBox<unsigned>(argv[4]);
  unsigned count = server ? unsigned(ZuBox<unsigned>(argv[5])) :
    (argc == 7 ? unsigned(ZuBox<unsigned>(argv[6])) : 1);
  if (!interface || !port || port > 65535 || !count) usage();
  PeerBytes request;
  if (!server && !readFile(argv[5], request)) return 1;
  ZiLog::init("zutcppair");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZiMultiplex mx{ZiMxParams{}.rxThread(1).txThread(2).scheduler([](auto &s) {
    s.nThreads(Sender);
    for (unsigned i = 1; i <= Sender; ++i) s.thread(i, [](auto &t) { t.isolated(true); });
  })};
  ZmRef<Zutcp::Instance> instance = new Zutcp::Instance{&mx};
  auto initializedBackend = instance->init({.interface = interface, .txSid = Sender});
  if (initializedBackend.is<ZeException>()) {
    ZiLOG(Error, "zutcppair", ([error = ZuMv(initializedBackend.p<ZeException>())](auto &out) {
      out << "backend initialization: " << error;
    }));
    ZiLog::stop();
    return 1;
  }
  mx.plugin(instance);
  Result result{&mx};
  if (server) {
    ServerApp app{&result, address, uint16_t(port), count};
    Ztcp::ServerParams params{&mx, "3", "4"};
    params.rxAllocator(ServerCxn::rxAllocator(&mx)).txAllocator(ServerCxn::txAllocator(&mx));
    bool initialized = app.init(ZuMv(params));
    bool started = initialized && mx.start() && app.start();
    if (!started) result.fail("native server start");
    else { app.listen(); result.wait(count); }
    if (started) app.stop();
    if (initialized) app.final();
    mx.stop();
  } else {
    ClientApp app{&result, &request};
    Ztcp::ClientParams params{&mx, "3", "4"};
    params.rxAllocator(ClientApp::Cxn::rxAllocator(&mx))
      .txAllocator(ClientApp::Cxn::txAllocator(&mx));
    bool initialized = app.init(ZuMv(params));
    if (!initialized || !mx.start()) result.fail("native client start");
    else {
      std::cerr << "backend=AF_XDP role=client ready=1\n" << std::flush;
      for (unsigned i = 0; i < count && !result.failed(); ++i) {
	ZmRef<ClientApp::Cxn> link = new ClientApp::Cxn{&app};
	link->connect(Ztcp::Host{argv[3]}, uint16_t(port));
	result.wait(i + 1);
	std::cerr << "native disconnect_complete=" << i + 1 << '\n';
      }
    }
    if (initialized) app.final();
    mx.stop();
  }
  std::cerr << "backend=AF_XDP stopped=1\n" << std::flush;
  ZiLog::stop();
  return result.failed() ? 1 : 0;
}
