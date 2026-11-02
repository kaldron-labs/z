//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZfixClient.hh>
#include <zlib/ZfixServer.hh>

using namespace ZuTestUtil;
using namespace Zfix;
#ifdef ZFIX_TEST_TLS
using Network = Zfix::TLS;
#else
using Network = Zfix::TCP;
#endif
struct Packet {
  unsigned	value;
};

ZfStruct(, (Packet), (value, (FIX::ID<7001>), UInt32));
using PacketFIXFields = ZuTypeConcat<Zfix::SessionFields<Packet>, ZfFields_Packet>;
PacketFIXFields ZuFields_(Packet *, ZuFacet::FIX *);
ZfStructImpl(Packet); ZfStructImpl(Packet, FIX);
using Entry = MT<Packet, "D">;
using Dictionary = MTList<Entry>;
struct Side {
  RxStats		rx;
  TxStats		tx;
  TraceRef		rxTrace, txTrace;
  TraceStats		rxTraceState, txTraceState;
  ZmSemaphore		ready, received, stopped, submitted;
  bool			pause = false;
  unsigned		expectedSubmit = RawSend::Accepted;
  ZmAtomic<unsigned>	errors{0}, receives{0}, stops{0}, faults{0}, published{0}, complete{0};

  void fail(unsigned line = __builtin_LINE()) {
    log("# ZfixIngressIOTest failure line=", line); ++errors;
  }
};

struct TestState {
  Side		cli1, cli2, srv1, srv2;
  ZmSemaphore	listening;
  unsigned	port = 0;
};

static bool wait(ZmSemaphore &s) { return s.timedwait(Zm::now(10)) == 0; }
static bool stopped(Side &s) { return s.stops.load_() || (wait(s.stopped) && s.stops.load_()); }
static SessionConfig config() { SessionConfig c; c.mode = Transport::Raw; return c; }
template <typename Impl, typename Base>
struct Peer : Base {
  using Base::app;
  Side	*state;

  template <typename Hub> Peer(Hub *hub, Side *s) :
    Base{hub, config()}, state{s} { }

  template <typename Hub> Peer(Hub *hub, Side *s, LinkLimits limits) :
    Base{hub, config(), limits}, state{s} { }

  void fixRawUpRx(uint64_t c) {
    if (!app()->rxInvoked() || c != 1) state->fail(); state->ready.post();
  }

  bool fixRawRx(ZmRef<RawInput> input, Fmt::Limits limits) {
    Fmt::MessageHandler<Packet> handler{input->frame, limits}; auto packet = handler.ctor();
    if (!app()->rxInvoked() || !handler.valid() || packet.value != 1) state->fail();
    ++state->receives; state->received.post(); return !state->pause;
  }

  void fixRawFaultRx(uint64_t, Fmt::Error e) {
    if (!app()->rxInvoked() || e.code != Fmt::ErrorCode::Size) state->fail(); ++state->faults;
  }

  void fixRawPublishedTx(const RawOutput &) {
    if (!app()->txInvoked()) state->fail(); ++state->published;
  }

  void fixRawSubmittedTx(RawOutput::Ref, unsigned result) {
    if (!app()->txInvoked() || result != state->expectedSubmit) state->fail();
    state->submitted.post();
  }

  void fixRawCompleteTx(TxBody::Ref, TxContext, bool) {
    if (!app()->txInvoked()) state->fail(); ++state->complete;
  }

  void fixRawStoppedRx(uint64_t, bool) {
    if (!app()->rxInvoked()) state->fail(); ++state->stops; state->stopped.post();
  }

  RawOutput::Ref output() {
    Fmt::Error error;
    auto body = TxBody::encodeRaw<Entry>(Packet{1}, 1, false, error);
    TxConfig config; config.beginString = "FIX.9.9";
    config.identity.local = "EXTERNAL-C"; config.identity.peer = "EXTERNAL-S";
    TxContext context{.connection = 1, .seqNo = 17, .sendingTime = ZuDateTime{Zm::now()}};
    auto output = RawOutput::encode(ZuMv(body), config, context, error);
    if (error || !output) state->fail();
    return output;
  }

  void fixTraceRx(TraceRef entry) {
    if (!app()->rxInvoked() || !entry || entry->data().context.epoch ||
	entry->data().context.seqNo) state->fail();
    state->rxTrace = ZuMv(entry);
  }

  void fixTraceTx(TraceRef entry) {
    if (!app()->txInvoked() || !entry || entry->data().context.epoch ||
	entry->data().context.seqNo != 17) state->fail();
    state->txTrace = ZuMv(entry);
  }

  void packet() {
    app()->rxRun([this, output = output()] {
      if (!this->sendRaw(output)) state->fail();
    });
  }
};

struct CliHub : RawClient<CliHub, Dictionary, Network> {
  using RawClient<CliHub, Dictionary, Network>::RawClient;
  struct Link;

  unsigned reconnFreq() const { return 0; }
};

struct CliHub::Link : public ZmNHeap<Link>, Peer<Link, RawCliLink<CliHub, Link, Dictionary,
  Network>> {
friend inline ZuStringT<"Zfix.ingress_io_test.Link"> ZmNHeapID(Link *);
  using ZmNHeap<Link>::operator new;
  using ZmNHeap<Link>::operator delete; using Peer::Peer; };

struct SrvHub : RawServer<SrvHub, Dictionary, Network> {
  using Base = RawServer<SrvHub, Dictionary, Network>;
  struct Link;
  TestState	*state;
  ZmRef<Link>	first, second;
  unsigned	next = 0; // network Rx-owned accept routing

  explicit SrvHub(TestState *s, HubLimits limits = {}) : Base{limits}, state{s} { }

  ZiIP localIP() const { return ZiIP{"127.0.0.1"}; }
  unsigned localPort() const { return 0; }
  unsigned nAccepts() const { return 1; }

  void listening(const ZiListenInfo &i) { state->port = i.port; state->listening.post(); }

  void listenFailed(bool) { state->srv1.fail(); state->listening.post(); }

  ZiConnection *accepted(const ZiCxnInfo &);
};

struct SrvHub::Link : public ZmNHeap<Link>, Peer<Link, RawSrvLink<SrvHub, Link, Dictionary,
  Network>> {
friend inline ZuStringT<"Zfix.ingress_io_test.Link"> ZmNHeapID(Link *);
  using ZmNHeap<Link>::operator new;
  using ZmNHeap<Link>::operator delete; using Peer::Peer; };

ZiConnection *SrvHub::accepted(const ZiCxnInfo &i) {
  auto link = next++ ? second : first;
  return link ? new Link::Cxn{link.ptr(), i} : nullptr;
}
struct Harness {
  TestState		state;
  CliHub		client;
  SrvHub		server;
  ZiMultiplex		mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(6)
      .thread(1, [](auto &t) { t.isolated(1); }).thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); }).thread(4, [](auto &t) { t.isolated(1); })
      .thread(5, [](auto &t) { t.isolated(1); }).thread(6, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  ZmRef<CliHub::Link>	first, second;

  explicit Harness(HubLimits cli = {}, HubLimits srv = {}) :
    client{cli}, server{&state, srv} { }

  bool start(bool submitZero = false) {
    if (!mx.start()) return false;
    Network::ClientParams cp{&mx, "5", "6"}; Network::ServerParams sp{&mx, "3", "4"};
#ifdef ZFIX_TEST_TLS
    auto cert = getenv("ZFIX_TEST_CERT"), key = getenv("ZFIX_TEST_KEY");
    if (!cert || !key) { mx.stop(); return false; }
    cp.caPath(cert); sp.certPath(cert).keyPath(key);
#endif
    bool ci = client.init(ZuMv(cp)), si = server.init(ZuMv(sp));
    if (!ci || !si) { client.final(); server.final(); mx.stop(); return false; }
    bool cs = client.start(), ss = server.start();
    if (!cs || !ss) {
      if (cs) client.stop(); if (ss) server.stop();
      client.final(); server.final(); mx.stop(); return false;
    }
    if (submitZero) {
      auto limits = client.fixLinkLimits(); limits.postedSubmitBytes = 0;
      first = new CliHub::Link{&client, &state.cli1, limits};
    } else first = new CliHub::Link{&client, &state.cli1};
    second = new CliHub::Link{&client, &state.cli2};
    server.first = new SrvHub::Link{&server, &state.srv1};
    server.second = new SrvHub::Link{&server, &state.srv2};
    server.listen();
    if (wait(state.listening) && state.port) return true;
    client.stop(); server.stop(); client.final(); server.final(); mx.stop();
    first = second = nullptr; server.first = server.second = nullptr;
    return false;
  }

  template <typename Link> void connect(Link *link) {
#ifdef ZFIX_TEST_TLS
    link->connect("localhost", state.port, ZiIP{"127.0.0.1"});
#else
    link->connect("127.0.0.1", state.port);
#endif
  }

  bool finish(bool two = true) {
    first->disconnect(); if (two) second->disconnect();
    bool done = stopped(state.cli1) && stopped(state.srv1);
    if (two) done = stopped(state.cli2) && stopped(state.srv2) && done;
    ZmBlock<>{}([this](auto wake) {
      client.rxRun([this, wake = ZuMv(wake)]() mutable {
	state.cli1.rx = first->rxStats_(); state.cli2.rx = second->rxStats_();
	state.cli1.rxTraceState = first->rxTraceStats_();
	  state.cli2.rxTraceState = second->rxTraceStats_(); wake();
      });
    });
    ZmBlock<>{}([this](auto wake) {
      client.txRun([this, wake = ZuMv(wake)]() mutable {
	state.cli1.tx = first->txStats_(); state.cli2.tx = second->txStats_();
	state.cli1.txTraceState = first->txTraceStats_();
	  state.cli2.txTraceState = second->txTraceStats_(); wake();
      });
    });
    ZmBlock<>{}([this](auto wake) {
      server.rxRun([this, wake = ZuMv(wake)]() mutable {
	state.srv1.rx = server.first->rxStats_(); state.srv2.rx = server.second->rxStats_();
	state.srv1.rxTraceState = server.first->rxTraceStats_();
	  state.srv2.rxTraceState = server.second->rxTraceStats_(); wake();
      });
    });
    ZmBlock<>{}([this](auto wake) {
      server.txRun([this, wake = ZuMv(wake)]() mutable {
	state.srv1.tx = server.first->txStats_(); state.srv2.tx = server.second->txStats_();
	state.srv1.txTraceState = server.first->txTraceStats_();
	  state.srv2.txTraceState = server.second->txTraceStats_(); wake();
      });
    });
    for (auto s : {&state.cli1, &state.cli2, &state.srv1, &state.srv2}) {
      if (s->tx.wire.published != s->published.load_() ||
	  s->tx.wire.completed + s->tx.wire.failed != s->complete.load_() ||
	  s->tx.wire.queuedCount || s->tx.wire.queuedBytes || s->tx.inFlight ||
	  s->rx.frames != s->receives.load_() || s->rx.pendingBytes) s->fail();
    }
    bool cs = client.stop(), ss = server.stop();
    client.final(); server.final(); mx.stop();
    first = second = nullptr; server.first = server.second = nullptr;
    return done && cs && ss;
  }

};

struct Gate {
  ZmSemaphore	entered, release;

  void block() { entered.post(); release.wait(); } };

static void submitBudget(bool local) {
  ZuTestScope(submitBudget);
  Harness h; bool up = h.start(local); ZuCheck(up); if (!up) return;
  h.connect(h.first.ptr()); ZuCheck(wait(h.state.cli1.ready)); ZuCheck(wait(h.state.srv1.ready));
  h.connect(h.second.ptr()); ZuCheck(wait(h.state.cli2.ready)); ZuCheck(wait(h.state.srv2.ready));
  auto output = h.first->output(); ZuCheck(output);
  // A caller outside either application owner cannot queue a retained handle.
  ZuCheck(!h.first->sendRaw(output));
  Gate tx; h.client.txRun([&tx] { tx.block(); }); ZuCheck(wait(tx.entered));
  bool first = false, second = false, tuned = false;
  PostedState charged, link;
  const uint64_t bytes = sizeof(RawOutput) + output->body()->memory() +
    sizeof(ZiIOBuf) + uint64_t(output->frame()->size);
  ZmBlock<>{}([&h, &output, &first, &second, &tuned, &charged, &link, bytes](auto wake) {
    h.client.rxRun([&h, &output, &first, &second, &tuned, &charged, &link,
	bytes, wake = ZuMv(wake)]() mutable {
      auto &budget = h.client.fixSubmitRx_(); tuned = budget.limits_({bytes});
      first = h.first->sendRaw(output); second = h.second->sendRaw(output);
      charged = budget.state_(); link = h.first->txPosted_(); wake();
    });
  });
  ZuCheck(tuned && first == !local && second == local);
  ZuCheck(charged.count == 1 && charged.bytes == bytes && charged.refused == 1);
  ZuCheck(link.count == unsigned(!local) && link.refused == unsigned(local));
  ZuCheck(!h.state.cli1.published.load_() && !h.state.cli2.published.load_());
  tx.release.post(); ZuCheck(wait(local ? h.state.srv2.received : h.state.srv1.received));
  PostedState drained;
  ZmBlock<>{}([&h, &drained](auto wake) {
    h.client.txRun([&h, &drained, wake = ZuMv(wake)]() mutable {
      h.client.rxRun([&h, &drained, wake = ZuMv(wake)]() mutable {
	drained = h.client.fixSubmitRx_().state_(); wake();
      });
    });
  });
  ZuCheck(!drained.bytes && !drained.count);
  // Reuse the released domain capacity and then close with a submission pending.
  Gate last; h.client.txRun([&last] { last.block(); }); ZuCheck(wait(last.entered));
  bool reused = false;
  ZmBlock<>{}([&h, &output, &reused](auto wake) {
    h.client.rxRun([&h, &output, &reused, wake = ZuMv(wake)]() mutable {
      reused = h.second->sendRaw(output); h.second->disconnect_(); wake();
    });
  });
  ZuCheck(reused && !h.state.cli2.stops.load_()); last.release.post();
  ZuCheck(h.finish());
  ZuCheck(h.state.cli1.published.load_() == unsigned(!local));
  ZuCheck(h.state.cli2.published.load_() == unsigned(local) + 1);
  ZuCheck(h.state.cli1.complete.load_() == h.state.cli1.published.load_());
  ZuCheck(h.state.cli2.complete.load_() == h.state.cli2.published.load_());
  ZuCheck(!h.state.cli1.errors.load_() && !h.state.cli2.errors.load_());
  ZuCheck(!h.state.srv1.errors.load_() && !h.state.srv2.errors.load_());
}
static void hubLimits(unsigned kind) {
  ZuTestScope(hubLimits);
  HubLimits cli, srv;
  switch (kind) {
    case 0: srv.link.rx.codec.frameBytes = 1; break;
    case 1: cli.link.tx.codec.frameBytes = 1; break;
    case 2: cli.link.wire.queuedBytes = cli.link.wire.adminBytes = 0; break;
  }
  Harness h{cli, srv}; bool up = h.start(); ZuCheck(up); if (!up) return;
  h.state.cli1.expectedSubmit = kind ? RawSend::Overload : RawSend::Accepted;
  h.connect(h.first.ptr()); ZuCheck(wait(h.state.cli1.ready)); ZuCheck(wait(h.state.srv1.ready));
  h.first->packet(); ZuCheck(wait(h.state.cli1.submitted));
  if (!kind) {
    ZuCheck(stopped(h.state.srv1) && stopped(h.state.cli1));
    ZuCheck(h.state.srv1.faults.load_() == 1 && !h.state.srv1.receives.load_());
  } else {
    ZuCheck(!h.state.cli1.published.load_() && !h.state.cli1.complete.load_());
    ZuCheck(!h.state.cli1.stops.load_() && !h.state.srv1.stops.load_());
  }
  ZuCheck(h.finish(false));
  ZuCheck(!h.state.cli1.errors.load_() && !h.state.srv1.errors.load_());
}
static void tracing() {
  ZuTestScope(tracing);
  HubLimits limits;
  limits.link.traceRx.recordBytes = limits.link.traceTx.recordBytes = ZiIOBuf_DefltSize;
  limits.link.traceRx.bytes = limits.link.traceTx.bytes = 2 * ZiIOBuf_DefltSize;
  Harness h{limits, limits}; bool up = h.start(); ZuCheck(up); if (!up) return;
  h.connect(h.first.ptr()); ZuCheck(wait(h.state.cli1.ready)); ZuCheck(wait(h.state.srv1.ready));
  h.first->packet(); ZuCheck(wait(h.state.srv1.received));
  h.first->packet(); ZuCheck(wait(h.state.srv1.received));
  ZuCheck(h.finish(false));
  const auto &rx = h.state.srv1.rxTraceState, &tx = h.state.cli1.txTraceState;
  ZuCheck(rx.records == 2 && tx.records == 2 && rx.rotated == 1 && tx.rotated == 1);
  ZuCheck(rx.count == 1 && tx.count == 1 && rx.bytes <= limits.link.traceRx.bytes);
  ZuCheck(tx.bytes <= limits.link.traceTx.bytes);
  // Observer-owned records outlive links, hub finalization and queue eviction.
  auto input = h.state.srv1.rxTrace, output = h.state.cli1.txTrace;
  ZuCheck(input && output && input->data().text() == output->data().text());
  ZuCheck(input->data().context.connection == 1 && output->data().context.connection == 1);
  ZuCheck(!h.state.cli1.errors.load_() && !h.state.srv1.errors.load_());
}
int main(int argc, char **argv) {
  parse(argc, argv); ZuTestMain();
  ZuTestCall(submitBudget, false);
  ZuTestCall(submitBudget, true);
  ZuTestCall(hubLimits, 0);
  ZuTestCall(hubLimits, 1);
  ZuTestCall(hubLimits, 2);
  ZuTestCall(tracing);
  return 0;
}
