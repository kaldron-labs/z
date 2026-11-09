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
#include <zlib/ZiResolver.hh>
#include <zlib/ZfixClient.hh>
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
  ZuCSpan	host{"localhost"}, symbol{"ZFIX"}, ca;
  unsigned	port = 16404, qty = 10, timeout = 10;
  ZuDecimal	price{12345, 2};
  bool		tls = false, help = false;
};

ZfStruct(, (Options, CLI),
  (host,	(CLI::Long<"host">),				String),
  (port,	(CLI::Long<"port">),				UInt32),
  (symbol,	(CLI::Long<"symbol">),				String),
  (qty,		(CLI::Long<"qty">),				UInt32),
  (price,	(CLI::Long<"price">),				Decimal),
  (timeout,	(CLI::Long<"timeout">),				UInt32),
  (ca,		(CLI::Long<"ca">),				String),
  (tls,		(CLI::Long<"tls">),				Bool),
  (help,	(CLI::Flag<'h'>, CLI::Long<"help">),		Bool));
ZfStructImpl(Options, CLI);

static Zfix::SessionConfig config() {
  Zfix::SessionConfig c;
  c.identity.version = Zfix::Version::V4_4;
  c.identity.local = "EXAMPLE-C"; c.identity.peer = "EXAMPLE-S";
  return c;
}
template <typename Net> struct Client;
template <typename Net> class Link :
    public ZmNHeap<Link<Net>>, public Zfix::CliLink<Client<Net>, Link<Net>, MTList, Net> {
friend inline ZuStringT<"Zfix.Example.CliLink"> ZmNHeapID(Link *);
  using Base = Zfix::CliLink<Client<Net>, Link<Net>, MTList, Net>;
public:
  using ZmNHeap<Link>::operator new;
  using ZmNHeap<Link>::operator delete;
  using Base::app;

  explicit Link(Client<Net> *app) : Base{app, config()} { }

  unsigned fixAssociateTx(Zfix::TxBody::Ref, Zfix::TxContext, uint64_t) {
    // Accept this one call into the core's bounded queue without an external
    // persistence barrier. Its sequence association lives until teardown.
    return Zfix::TxLoad::Accepted;
  }

  void fixEventRx(Zfix::SessionEvent event, ZmRef<Zfix::RxQueue::Node>) {
    if (event.actions & Zfix::SessionAction::Failure) app()->fail("FIX session failed");
    if (!(event.actions & Zfix::SessionAction::Ready) || m_started) return;
    m_started = true;
    // This application chooses its correlation key before sending. FIX wire
    // sequence allocation and business completion are separate concerns.
    Order order; order.key = Key; order.symbol = app()->options.symbol;
    order.qty = app()->options.qty; order.price = app()->options.price;
    order.time = ZuDateTime{Zm::now()};
    order.parties.push(Party{"client", "D", 3});
    order.parties.push(Party{"desk", "D", 11});
    Zfix::Fmt::Error error;
    if (!this->template send<NewOrder>(order, Key, error)) app()->fail("order refused");
  }

  template <typename Entry>
  Zfix::Fmt::Error fixReceiveRx(Entry *, const Execution &r, const Zfix::RequestContext &) {
    if (r.key != Key || !r.order || !r.exec ||
	(m_order && m_order != r.order) || r.qty != app()->options.qty ||
	r.price != app()->options.price || r.symbol != app()->options.symbol ||
	r.parties.length() != 2 || r.parties[0].id != "client" ||
	r.parties[0].source != "D" || r.parties[0].role != 3 ||
	r.parties[1].id != "desk" || r.parties[1].source != "D" || r.parties[1].role != 11)
      { app()->fail("unexpected execution report"); return {Zfix::Fmt::ErrorCode::Value, 11}; }
    if constexpr (ZuIsSame<Entry, Ack>{}) {
      if (r.status != "0" || r.leavesQty != r.qty || r.cumQty) {
	app()->fail("invalid acknowledgement");
	return {Zfix::Fmt::ErrorCode::Value, 39};
      }
    } else {
      if (r.status != "2" || r.leavesQty || r.cumQty != r.qty || r.lastQty != r.qty) {
	app()->fail("invalid fill");
	return {Zfix::Fmt::ErrorCode::Value, 39};
      }
    }
    m_order = r.order;
    auto &exec = ZuIsSame<Entry, Ack>{} ? m_ack : m_fill;
    if ((exec && exec != r.exec) || r.exec == (ZuIsSame<Entry, Ack>{} ? m_fill : m_ack)) {
      app()->fail("invalid execution ID");
      return {Zfix::Fmt::ErrorCode::Value, 17};
    }
    if (exec) return {}; // application policy for an identical duplicate report
    exec = r.exec;
    std::cout << (ZuIsSame<Entry, Ack>{} ? "ack" : "fill") << " order=" <<
      ZuBoxed(r.key) << " exec=" << ZuBoxed(r.exec) << '\n';
    // Both responses belong to the same call. A local send completion alone
    // cannot complete it; the application waits for acknowledgement AND fill.
    finished();
    return {};
  }

  Zfix::Fmt::Error fixReceiveRx(Tick *, const Event &event, const Zfix::RequestContext &) {
    if (event.symbol != app()->options.symbol || event.price != app()->options.price) {
      app()->fail("unexpected market event"); return {Zfix::Fmt::ErrorCode::Value, 55};
    }
    if (!m_tick) std::cout << "event symbol=" << event.symbol << '\n';
    m_tick = true; finished(); return {};
  }

  void fixSubmittedTx(Zfix::TxBody::Ref, unsigned result) {
    if (result != Zfix::TxSelect::Accepted && result != Zfix::TxSelect::Pending)
      app()->fail("order admission failed");
  }

  void fixRejectedTx(Zfix::PeerReject reject) {
    // RefSeqNum identifies the rejected outbound message in this epoch.
    ZiLOG(Error, "zfix", ([epoch = reject.context.epoch, seq = reject.message.refSeqNo,
      tag = reject.message.refTagID](auto &s) {
      s << "peer reject epoch=" << epoch << " seq=" << seq << " tag=" << tag;
    }));
    app()->fail("peer rejected the order");
  }

  Zfix::TxReload fixRetrieveTx(Zfix::TxContext context, uint64_t, uint64_t) {
    std::cerr << "history unavailable epoch=" << ZuBoxed(context.epoch) <<
      " seq=" << ZuBoxed(context.seqNo) << "; gap-fill\n";
    return {}; // application policy: keep no resend content; gap-fill every request
  }

  void fixStoppedRx(bool) {
    if (!m_ack || !m_fill) app()->fail("connection stopped before both reports");
    app()->stopped.post();
  }
private:
  void finished() {
    if (m_ack && m_fill && m_tick) app()->done.post();
  }

  enum { Key = 1001 };
  uint64_t	m_order = 0, m_ack = 0, m_fill = 0; // application Rx-owned call state
  bool		m_started = false, m_tick = false;
};

template <typename Net> struct Client : Zfix::Client<Client<Net>, MTList, Net> {
  using Link = ::Link<Net>;
  const Options		options;
  ZmSemaphore		done, stopped;
  ZmAtomic<unsigned>	errors{0};

  explicit Client(const Options &o) : options{o} { }

  void fail(const char *reason) {
    if (!errors.xchAdd(1)) ZiLOG(Error, "zfix", reason);
    done.post();
  }

  bool init(typename Net::ClientParams params) {
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
    return Zfix::Client<Client<Net>, MTList, Net>::init(ZuMv(params));
  }
};

template <typename Net> static int run(const Options &options) {
  Client<Net> client{options};
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(4).thread(3, [](auto &t) { t.isolated(1); })
      .thread(4, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  typename Net::ClientParams params{&mx, "3", "4"};
  if constexpr (Net::Encrypted) params.caPath(options.ca);
  bool initialized = client.init(ZuMv(params));
  if (!mx.start()) return 1;
  bool running = initialized && client.start();
  ZmRef<Link<Net>> link;
  if (!running) client.fail("client initialization failed");
  else {
    link = new Link<Net>{&client}; link->connect(options.host, options.port);
    if (client.done.timedwait(Zm::now(options.timeout))) client.fail("order deadline expired");
    if (client.errors.load_()) link->disconnect(); else link->logout();
    if (client.stopped.timedwait(Zm::now(options.timeout))) {
      client.fail("shutdown deadline expired"); link->disconnect();
      client.stopped.wait(); // retain all owners until the actual stop callback
    }
  }
  if (running && !client.stop()) client.fail("client stop failed");
  client.final(); // also release any resources retained by a failed init()
  if (!ZiResolver::stop()) client.fail("resolver drain failed");
  ZiResolver::final();
  mx.stop(); link = nullptr;
  if (!client.errors.load_()) std::cout << "completed order=1001\n";
  return client.errors.load_() ? 1 : 0;
}
int main(int argc, char **argv) {
  Options options;
  try { argc = ZfCLI::load(options, argc, argv); }
  catch (const ZeException &) { argc = 0; }
  if (options.help || argc != 1 || !options.host || !options.symbol || !options.qty ||
      !options.port || options.port > 65535 || !options.timeout ||
      options.price <= ZuDecimal{0} || (options.tls && !options.ca)) {
    std::cerr <<
      "usage: zfix [--host HOST --port PORT --symbol SYMBOL --qty N --price DECIMAL]\n"
      "  [--tls --ca PEM] [--timeout SECONDS]\n";
    return options.help ? 0 : 2;
  }
  ZiLog::init("zfix");
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  int result = options.tls ? run<Zfix::TLS>(options) : run<Zfix::TCP>(options);
  ZiLog::stop();
  return result;
}
