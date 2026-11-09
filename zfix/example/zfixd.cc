//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>
#include <zlib/ZuBox.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZePlatform.hh>
#include <zlib/ZfCLI.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZfixServer.hh>
#include "zfix.hh"

namespace Example {

  ZtEnumImplNS(Handling);
  ZtEnumImplNS(Handling, FIX);
  ZtEnumImplNS(Side);
  ZtEnumImplNS(Side, FIX);
  ZtEnumImplNS(OrdType);
  ZtEnumImplNS(OrdType, FIX);

  ZfStructImpl(Party, FIX);
  ZfStructImpl(Order, FIX);
  ZfStructImpl(Execution, FIX);
  ZfStructImpl(Event, FIX);
}

using namespace Example;

struct Options {
  ZuCSpan	bind{"127.0.0.1"}, cert, key;
  unsigned	port = 16404, timeout = 30;
  bool		tls = false, help = false;
};

ZfStruct(, (Options, CLI),
  (bind,	(CLI::Long<"bind">),				String),
  (port,	(CLI::Long<"port">),				UInt32),
  (timeout,	(CLI::Long<"timeout">),				UInt32),
  (cert,	(CLI::Long<"cert">),				String),
  (key,		(CLI::Long<"key">),				String),
  (tls,		(CLI::Long<"tls">),				Bool),
  (help,	(CLI::Flag<'h'>, CLI::Long<"help">),		Bool));
ZfStructImpl(Options, CLI);

static Zfix::SessionConfig config() {
  Zfix::SessionConfig c;
  c.identity.version = Zfix::Version::V4_4;
  c.identity.local = "EXAMPLE-S"; c.identity.peer = "EXAMPLE-C";
  return c;
}
template <typename Net> struct Server;
template <typename Net> class Link :
    public ZmNHeap<Link<Net>>, public Zfix::SrvLink<Server<Net>, Link<Net>, MTList, Net> {
friend inline ZuStringT<"Zfix.Example.SrvLink"> ZmNHeapID(Link *);
  using Base = Zfix::SrvLink<Server<Net>, Link<Net>, MTList, Net>;
public:
  using ZmNHeap<Link>::operator new;
  using ZmNHeap<Link>::operator delete;
  using Base::app;

  explicit Link(Server<Net> *app) : Base{app, config()} { }

  unsigned fixAssociateTx(Zfix::TxBody::Ref, Zfix::TxContext, uint64_t) {
    // Accept into the bounded operational queue; this app keeps no replay store.
    return Zfix::TxLoad::Accepted;
  }

  template <typename Response>
  Zfix::Fmt::Error fixReplyRx(NewOrder *, Response *, const Zfix::RequestContext &context) {
    const auto &session = this->session_();
    return context.context().session == session.instance_() &&
      context.context().epoch == session.epoch_() ? Zfix::Fmt::Error{} :
      Zfix::Fmt::Error{Zfix::Fmt::ErrorCode::Unresolved, 35};
  }

  unsigned fixAdmissionRx(Zfix::SessionEvent, ZmRef<Zfix::RxQueue::Node>,
      Zfix::AdmitRequest &) {
    // Optional FIX authentication belongs here. This example permits no FIX
    // credentials; TLS certificate policy is configured separately by the hub.
    return Zfix::Admission::Accepted;
  }

  Zfix::Fmt::Error fixReceiveRx(NewOrder *, const Order &order,
      const Zfix::RequestContext &context) {
    if (app()->orders || !order.key || !order.qty || order.price <= ZuDecimal{0} ||
	!order.parties.length())
      return {Zfix::Fmt::ErrorCode::Value, 11};
    ++app()->orders;
    std::cout << "received order=" << ZuBoxed(order.key) << " qty=" <<
      ZuBoxed(order.qty) << '\n';
    Execution report;
    report.key = order.key; report.order = 5001; report.exec = 1;
    report.kind = "0"; report.status = "0"; report.symbol = order.symbol;
    report.side = order.side; report.qty = report.leavesQty = order.qty;
    report.price = order.price; report.time = order.time;
    // Both replies encode synchronously while the retained request is alive.
    report.parties -= order.parties;
    Zfix::Fmt::Error error;
    if (!this->template reply<NewOrder, Ack>(context, report, report.exec, error)) {
      app()->fail("acknowledgement refused");
      return error ? error : Zfix::Fmt::Error{Zfix::Fmt::ErrorCode::Output};
    }
    report.exec = 2; report.kind = "F"; report.status = "2";
    report.leavesQty = 0; report.cumQty = report.lastQty = order.qty;
    report.avg = report.lastPx = order.price;
    if (!this->template reply<NewOrder, Fill>(context, report, report.exec, error)) {
      app()->fail("fill refused");
      return error ? error : Zfix::Fmt::Error{Zfix::Fmt::ErrorCode::Output};
    }
    if (!this->template send<Tick>(Event{order.symbol, order.price}, 9001, error)) {
      app()->fail("event refused"); return error;
    }
    return {};
  }

  void fixEventRx(Zfix::SessionEvent event, ZmRef<Zfix::RxQueue::Node>) {
    if (event.actions & Zfix::SessionAction::Failure) app()->fail("FIX session failed");
  }

  void fixSubmittedTx(Zfix::TxBody::Ref, unsigned result) {
    if (result != Zfix::TxSelect::Accepted && result != Zfix::TxSelect::Pending)
      app()->fail("reply admission failed");
  }

  void fixRejectedTx(Zfix::PeerReject reject) {
    ZiLOG(Error, "zfixd", ([epoch = reject.context.epoch, seq = reject.message.refSeqNo,
      tag = reject.message.refTagID](auto &s) {
      s << "peer reject epoch=" << epoch << " seq=" << seq << " tag=" << tag;
    }));
    app()->fail("peer rejected the reply");
  }

  Zfix::TxReload fixRetrieveTx(Zfix::TxContext context, uint64_t, uint64_t) {
    std::cerr << "history unavailable epoch=" << ZuBoxed(context.epoch) <<
      " seq=" << ZuBoxed(context.seqNo) << "; gap-fill\n";
    return {}; // application policy: keep no resend content; gap-fill every request
  }

  void fixStoppedRx(bool) { app()->retired(); }
};

template <typename Net> struct Server : Zfix::Server<Server<Net>, MTList, Net> {
  using Link = ::Link<Net>;
  const Options		options;
  ZmSemaphore		listeningDone, done, stopped;
  ZmAtomic<unsigned>	errors{0};
  unsigned		port = 0, orders = 0;
  ZmRef<Link>		connection; // network Rx-owned persistent logical link

  explicit Server(const Options &o) : options{o} { }

  ZiIP localIP() const { return ZiIP{options.bind}; }
  unsigned localPort() const { return options.port; }
  unsigned nAccepts() const { return 1; }

  ZiConnection *accepted(const ZiCxnInfo &info) {
    if (!connection) connection = new Link{this};
    return new typename Link::Cxn{connection, info};
  }

  void listening(const ZiListenInfo &info) { port = info.port; listeningDone.post(); }

  void listenFailed(bool) { fail("listen failed"); listeningDone.post(); }

  void fail(const char *reason) {
    if (!errors.xchAdd(1)) ZiLOG(Error, "zfixd", reason);
    done.post();
  }

  void close() { this->mx()->rxRun([this] {
    if (connection) connection->disconnect(); else stopped.post();
  }); }

  void retired() { this->mx()->rxRun([this] {
    stopped.post(); done.post();
  }); }

  bool init(typename Net::ServerParams params) {
    Zi::IOBufAllocator *rx = nullptr;
    Zi::IOBufAllocator *tx = nullptr;
    if (!params.rxAllocator()) {
      rx = Link::rxAllocator(params.multiplex());
      params.rxAllocator(rx);
    }
    if (!params.txAllocator()) {
      tx = Link::txAllocator(params.multiplex());
      params.txAllocator(tx);
    }
    return Zfix::Server<Server<Net>, MTList, Net>::init(ZuMv(params));
  }
};

template <typename Net> static int run(const Options &options) {
  Server<Net> server{options};
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(4).thread(3, [](auto &t) { t.isolated(1); })
      .thread(4, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  typename Net::ServerParams params{&mx, "3", "4"};
  if constexpr (Net::Encrypted) params.certPath(options.cert).keyPath(options.key);
  bool initialized = server.init(ZuMv(params));
  if (!mx.start()) return 1;
  bool running = initialized && server.start();
  if (!running) server.fail("server initialization failed");
  else {
    server.listen();
    if (server.listeningDone.timedwait(Zm::now(options.timeout)))
      server.fail("listen deadline expired");
    if (!server.errors.load_()) {
      std::cout << "listening port=" << ZuBoxed(server.port) << " tls=" <<
	ZuBoxed(unsigned(Net::Encrypted)) << '\n' << std::flush;
      if (server.done.timedwait(Zm::now(options.timeout)))
	server.fail("session deadline expired");
    }
    if (server.errors.load_()) server.close();
    if (server.stopped.timedwait(Zm::now(options.timeout))) {
      server.fail("shutdown deadline expired"); server.close();
      server.stopped.wait(); // keep the hub alive through the actual stop callback
    }
  }
  if (running && !server.stop()) server.fail("server stop failed");
  server.final(); // also release any resources retained by a failed init()
  mx.stop();
  server.connection = nullptr;
  if (!server.orders) server.fail("no order received");
  if (!server.errors.load_()) std::cout << "served orders=1\n";
  return server.errors.load_() ? 1 : 0;
}
int main(int argc, char **argv) {
  Options options;
  try { argc = ZfCLI::load(options, argc, argv); }
  catch (const ZeException &) { argc = 0; }
  if (options.help || argc != 1 || !options.bind || options.port > 65535 ||
      !options.timeout || (options.tls && (!options.cert || !options.key))) {
    std::cerr << "usage: zfixd [--bind ADDRESS --port PORT] [--timeout SECONDS]\n"
      "  [--tls --cert PEM --key PEM]\n";
    return options.help ? 0 : 2;
  }
  ZiLog::init("zfixd");
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  int result = options.tls ? run<Zfix::TLS>(options) : run<Zfix::TCP>(options);
  ZiLog::stop();
  return result;
}
