//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZiLog.hh>

#include <zlib/Ztcp.hh>
#include <zlib/Zutcp.hh>

#include "ZiTestResidue.hh"
#include "ZpktTestCf.hh"

using namespace ZuTestUtil;

enum { Port = 18187, PayloadSize = 4096 };

struct Exchange {
  ZmSemaphore done;
  ZmAtomic<unsigned> errors{0}, connected{0}, received{0};
  unsigned bytes = 0;
  char payload[PayloadSize];

  explicit Exchange(char first) {
    for (unsigned i = 0; i < PayloadSize; ++i) payload[i] = char(first + i % 26);
  }

  bool consume(Ztcp::RxStream &rx, char first) {
    while (rx) {
      auto part = rx.span();
      for (auto byte : part) {
	if (bytes >= PayloadSize || byte != first + int(bytes % 26)) ++errors;
	++bytes;
      }
      rx.advance(part.length());
    }
    if (bytes != PayloadSize || received) return false;
    ++received;
    return true;
  }
};

template <typename Link> static void connected(Link *link)
{
  auto &exchange = *link->app()->exchange;
  auto cxn = link->cxn();
  if (!link->app()->rxInvoked() || !cxn || !cxn->up() || !cxn->plugin() ||
      !Zi::nullSocket(cxn->info().socket))
    ++exchange.errors;
  ++exchange.connected;
}

struct Client : public Ztcp::Client<Client> {
  struct Link;
  Exchange	*exchange;

  explicit Client(Exchange *exchange_) : exchange{exchange_} { }
  unsigned reconnFreq() const { return 0; }
};

struct Client::Link : public ZmNHeap<Link>, public Ztcp::CliLink<Client, Link> {
friend inline ZuStringT<"Zutcp.Afp.Client"> ZmNHeapID(Link *);
  using Base = Ztcp::CliLink<Client, Link>;
  using ZmNHeap<Link>::operator new;
  using ZmNHeap<Link>::operator delete;

  explicit Link(Client *app) : Base{app} { }

  void connected(Ztcp::Connected) {
    ::connected(this);
    auto tx = txStream();
    tx << ZuCSpan{app()->exchange->payload, PayloadSize};
    if (!tx.flush()) { ++app()->exchange->errors; disconnect(); }
  }
  int process(Ztcp::RxStream &rx) {
    unsigned n = rx.length();
    if (app()->exchange->consume(rx, 'A')) disconnect();
    return n ? 1 : 0;
  }
  void disconnected(bool) { app()->exchange->done.post(); }
  void connectFailed(bool) { ++app()->exchange->errors; app()->exchange->done.post(); }
};

struct Server : public Ztcp::Server<Server> {
  struct Link;
  Exchange	*exchange;
  ZiIP		ip;

  Server(Exchange *exchange_, ZiIP ip_) : exchange{exchange_}, ip{ip_} { }
  ZiIP localIP() const { return ip; }
  uint16_t localPort() const { return Port; }
  unsigned nAccepts() const { return 1; }
  ZiConnection *accepted(const ZiCxnInfo &);
  void listening(const ZiListenInfo &) { std::cerr << "READY\n" << std::flush; }
  void listenFailed(bool) { ++exchange->errors; exchange->done.post(); }
};

struct Server::Link : public ZmNHeap<Link>, public Ztcp::SrvLink<Server, Link> {
friend inline ZuStringT<"Zutcp.Afp.Server"> ZmNHeapID(Link *);
  using Base = Ztcp::SrvLink<Server, Link>;
  using ZmNHeap<Link>::operator new;
  using ZmNHeap<Link>::operator delete;

  explicit Link(Server *app) : Base{app} { }

  void connected(Ztcp::Connected) { ::connected(this); }
  int process(Ztcp::RxStream &rx) {
    unsigned n = rx.length();
    if (app()->exchange->consume(rx, 'a')) {
      auto tx = txStream();
      tx << ZuCSpan{app()->exchange->payload, PayloadSize};
      if (!tx.flush()) { ++app()->exchange->errors; disconnect(); }
    }
    return n ? 1 : 0;
  }
  void disconnected(bool) { app()->exchange->done.post(); }
};

ZiConnection *Server::accepted(const ZiCxnInfo &info)
{
  return new Link::Cxn{new Link{this}, info};
}

static bool serverRun(ZiMultiplex &mx, const char *address, Exchange &exchange)
{
  Server app{&exchange, ZiIP{address}};
  auto rx = Server::Link::rxAllocator(&mx);
  auto tx = Server::Link::txAllocator(&mx);
  if (!rx || !tx) return false;
  Ztcp::ServerParams params{&mx, "3", "4"};
  params.rxAllocator(rx).txAllocator(tx);
  if (!app.init(ZuMv(params)) || !mx.start() || !app.start()) return false;
  app.listen();
  bool done = !exchange.done.timedwait(Zm::now() + ZuTime{18});
  done = app.stop() && done;
  app.final();
  return mx.stop() && done;
}

static bool clientRun(ZiMultiplex &mx, const char *address, Exchange &exchange)
{
  Client app{&exchange};
  auto rx = Client::Link::rxAllocator(&mx);
  auto tx = Client::Link::txAllocator(&mx);
  if (!rx || !tx) return false;
  Ztcp::ClientParams params{&mx, "3", "4"};
  params.rxAllocator(rx).txAllocator(tx);
  if (!app.init(ZuMv(params)) || !mx.start()) return false;
  ZmRef<Client::Link> link = new Client::Link{&app};
  link->connect(Ztcp::Host{address}, Port);
  bool done = !exchange.done.timedwait(Zm::now() + ZuTime{18});
  app.final();
  return mx.stop() && done;
}

static bool run(bool server, const char *interface, const char *address)
{
  ZmRef<Zpkt::Env> env = new Zpkt::Env;
  ZmRef<Zutcp::MxPlugin> stack = new Zutcp::MxPlugin;
  ZiMultiplex mx{ZiMxParams{}.rxBufSize(128).scheduler([](auto &s) {
    s.nThreads(4);
    for (unsigned i = 1; i <= 4; ++i) s.thread(i, [](auto &t) { t.isolated(true); });
  }), stack};

  auto cf = ZpktTest::object();
  ZpktTest::value(cf.ptr(), "module", ZUTCP_AFP_MODULE);
  auto entry = ZpktTest::object(ZpktTest::object(cf.ptr(), "interfaces"), interface);
  ZpktTest::value(entry, "rxSpin", "0");
  try { env->init(cf.ptr()); }
  catch (const ZeException &err) { ZiLogEvent(err); return false; }
  cf = nullptr;
  if (env->loopback()) return false;
  auto opened = env->nif(&mx, interface);
  if (opened.is<ZeException>()) { ZiLogEvent(opened.p<ZeException>()); return false; }
  ZmRef<Zpkt::Nif> nif = opened.p<Zpkt::Nif *>();
  if (nif->zeroCopy()) { nif->final(); return false; }
  try { stack->init(&mx, env, ZuMv(nif), {.rxRetainFramesPerCxn = 64}); }
  catch (const ZeException &err) {
    if (nif) nif->final();
    ZiLogEvent(err);
    return false;
  }

  Exchange exchange{server ? 'A' : 'a'};
  bool done = server ? serverRun(mx, address, exchange) : clientRun(mx, address, exchange);
  std::cerr << "exchange done=" << done << " connections=" << unsigned(exchange.connected)
    << " receive_bytes=" << exchange.bytes << " errors=" << unsigned(exchange.errors) << '\n';
  return done && !exchange.errors && exchange.connected == 1 &&
    exchange.received == 1 && exchange.bytes == PayloadSize;
}

static void testExchange(bool ipv6)
{
  ZuTestScopeRT(testExchange);
  auto directory = ZiTestResidue::dir(ipv6 ? "ipv6" : "ipv4");
  auto pid = fork();
  if (!pid) {
    Zi::Path output;
    output << directory << "/fixture.log";
    int fd = open(output, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0 || dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0) _exit(126);
    close(fd);
    execlp("unshare", "unshare", "--mount", "--net", "--propagation", "private",
      "/bin/bash", ZUTCP_AFP_FIXTURE, ipv6 ? "6" : "4", directory.data(),
      ZUTCP_AFP_PROGRAM, static_cast<char *>(nullptr));
    _exit(127);
  }
  int status = -1;
  if (pid > 0) {
    pid_t result;
    do { result = waitpid(pid, &status, 0); } while (result < 0 && errno == EINTR);
    if (result != pid) status = -1;
  }
  bool verified = !status;
  if (verified) {
    Zi::Path summaryPath;
    summaryPath << directory << "/wire-summary";
    auto summary = fopen(summaryPath, "r");
    char line[256];
    verified = summary && fgets(line, sizeof(line), summary);
    if (summary) fclose(summary);
    if (verified) {
      ZuTestMgr::indent();
      std::cout << "# IPv" << (ipv6 ? 6 : 4) << ' ' << line << std::flush;
    }
  }
  ZuCheckRT(verified, log_("AFP IPv", ipv6 ? 6 : 4,
    " exact duplex exchange, valid checksums, zero TCP resets and graceful close. Logs ", directory));
}

int main(int argc, char **argv)
{
  if (argc == 4 && (!strcmp(argv[1], "server") || !strcmp(argv[1], "client"))) {
    ZiLog::init("zutcpafptest");
    ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
    ZiLog::start();
    bool ok = run(!strcmp(argv[1], "server"), argv[2], argv[3]);
    ZiLog::stop();
    return !ok;
  }
  parse(argc, argv);
  if (geteuid() || !getenv("ZPKT_RUN_FIXTURE")) {
    std::cout << "1..0 # SKIP requires root and ZPKT_RUN_FIXTURE=1 for private namespaces\n";
    return 0;
  }
  ZiTestResidue::init("zutcpafptest");
  ZuTestMain();
  ZuTestCall(testExchange, false);
  ZuTestCall(testExchange, true);
}
