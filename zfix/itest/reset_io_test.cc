//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZfixClient.hh>
#include <zlib/ZfixServer.hh>

using namespace ZuTestUtil;
using namespace Zfix;
#ifdef ZFIX_TEST_TLS
using Network = Zfix::TLS;
#else
using Network = Zfix::TCP;
#endif
struct Order {
  unsigned	phase;
};

ZfStruct(, (Order), (phase, (FIX::ID<7001>), UInt32));
using OrderFIXFields = ZuTypeConcat<Zfix::SessionFields<Order>, ZfFields_Order>;
OrderFIXFields ZuFields_(Order *, ZuFacet::FIX *);
ZfStructImpl(Order); ZfStructImpl(Order, FIX);
using Entry = MT<Order, "D">;
using Dictionary = MTList<Entry>;
// Shared core TxBody objects cannot carry this application FIFO's links.
using Pending = ZmList<TxBody::Ref, ZmListHeapID<"Zfix.ResetIOTest.Queue">>;
using History = ZtArray<TxHistory, ZtArrayHeapID<"Zfix.ResetIOTest.History">>;
namespace Fault {

  enum { None, PreparedClose, PreparedTimeout, StartedClose, StartedTimeout,
    RefusedLogon };
}

struct Side {
  ZmSemaphore		ready, drained, prepared, received, completed,
    stopped, result, resetSent, blocked;
  ZmAtomic<unsigned>	errors{0}, epochsRx{0}, epochsTx{0}, resetLogons{0};
  ZmAtomic<unsigned>	failures{0}, timeouts{0}, outputs{0}, readyCount{0};
  ZmAtomic<unsigned>	recoveryPauses{0}, recoveryReadies{0}, resendReqs{0};
  bool			observedReady = false; // Rx-owned independent notification edge
  bool			refusedLogon = false; // Tx-owned one-shot failure
  bool			accepted = false;
  // one outstanding command, handed to main through result
  uint64_t		txEpoch = 1, lastLoad = 0, businessEnd = 1; // Tx-owned
  Pending		queue;
  History		history;

  void fail(unsigned line = __builtin_LINE()) {
    log("# ZfixResetIOTest failure line=", line); ++errors;
  }
};

struct TestState {
  const unsigned	mode;
  unsigned		fault = Fault::None; // immutable after worker startup
  Side			cli, srv;
  ZmSemaphore		listening;
  unsigned		port = 0;
};

static bool wait(ZmSemaphore &s) { return s.timedwait(Zm::now(10)) == 0; }
static bool readyTransitions(const Side &side) {
  unsigned resumed = side.recoveryReadies.load_();
  bool ok = side.readyCount.load_() == 2 + resumed &&
    side.recoveryPauses.load_() == resumed && resumed <= side.resendReqs.load_();
  if (!ok) log("# reset Ready edges: ready=", side.readyCount.load_(),
    " pause=", side.recoveryPauses.load_(), " resumed=", resumed,
    " resend=", side.resendReqs.load_());
  return ok;
}
static SessionConfig config(bool cli, const TestState &test) {
  SessionConfig c; c.mode = test.mode; c.initiator = cli;
  c.identity.version = test.mode == Transport::FIXT11 ? Version::VT1_1 : Version::V4_4;
  c.identity.local = cli ? "CLIENT" : "SERVER"; c.identity.peer = cli ? "SERVER" : "CLIENT";
  c.appVerID = AppVersion::V5_0_SP2;
  if (test.fault) c.logonTimeout = ZuTime{1};
  return c;
}
template <typename Impl, typename Base, bool Client>
struct Peer : Base {
  using Msg = RxQueue::Node;
  using Base::app;

  template <typename Hub> explicit Peer(Hub *hub) : Base{hub, config(Client, *hub->test)} { }

  auto self() { return static_cast<Impl *>(this); }

  auto side() { return Client ? &app()->test->cli : &app()->test->srv; }

  void enqueue(TxBody::Ref body) {
    app()->txRun([this, body = ZuMv(body)]() mutable {
      side()->queue.push(ZuMv(body)); this->queued();
    });
  }

  void order(unsigned phase) {
    Fmt::Error error; auto body = TxBody::encode<Entry>(Order{phase}, phase, error);
    if (error) { side()->fail(); return; } enqueue(ZuMv(body));
  }

  TxBody::Ref fixDequeueTx() {
    auto node = side()->queue.shift(); return node ? ZuMv(node->data()) : TxBody::Ref{};
  }

  unsigned fixAssociateTx(TxBody::Ref body, TxContext c, uint64_t) {
    if (!app()->txInvoked() || c.epoch != side()->txEpoch || c.seqNo != ++side()->lastLoad)
      side()->fail();
    if (!body->administrative()) side()->businessEnd = c.seqNo + 1;
    return TxLoad::Accepted;
  }

  void fixPublishedTx(TxBody::Ref body, TxContext c, ZuDateTime original) {
    if (!c.replay)
      side()->history.push(TxHistory{ZuMv(body), original, c.epoch, c.seqNo});
  }

  TxReload fixRetrieveTx(TxContext c, uint64_t, uint64_t) {
    for (const auto &h : side()->history)
      if (h.epoch == c.epoch && h.seqNo == c.seqNo &&
	  (!h.body->administrative() || h.body->msgType() == "3")) return {h};
    return {};
  }

  void fixSelectedTx(TxBody::Ref, TxContext, unsigned result) {
    if (result != TxSelect::Accepted) side()->fail();
  }

  void fixCompleteTx(TxBody::Ref body, TxContext context, bool ok) {
    if (body->msgType() == "A" && context.epoch == 2 && context.connection == 1 && ok)
      side()->resetSent.post();
    if (body->msgType() == "D") {
      if (!ok) side()->fail(); side()->completed.post();
    }
  }

  TxBody::Ref fixLogonTx(TxBody::Ref body) {
    if (!app()->txInvoked()) side()->fail();
    if constexpr (Client) {
      if (app()->test->fault == Fault::RefusedLogon && side()->txEpoch == 2
	&& !side()->refusedLogon) {
	side()->refusedLogon = true; return {};
      }
    }
    return body;
  }

  void fixFaultTx(TxContext c, Fmt::Error e) {
    if (!app()->txInvoked() || app()->test->fault != Fault::RefusedLogon ||
	!Client || c.epoch != 2 || e.code != Fmt::ErrorCode::Output) {
      log("# ZfixResetIOTest unexpected Tx fault mode=", app()->test->mode, " fault=",
	app()->test->fault, " role=", Client ? "client" : "server", " epoch=", c.epoch,
	" connection=", c.connection, " code=", e.code, " tag=", e.tag);
      side()->fail();
    }
    ++side()->outputs;
  }

  void fixBlockedRx(TxContext) {
    if (!app()->rxInvoked()) side()->fail();
    side()->blocked.post();
  }

  void fixControlTx(TxBody::Ref body, TxContext c) {
    if (body->msgType() != "A") return;
    Session::Logon logon; Fmt::Error error;
    Fmt::read<Session::Logon, ZuFacet::FIX, ArchiveField>(logon, body->body(), error);
    if (error || c.epoch != side()->txEpoch) side()->fail();
    if (app()->test->fault && c.connection > 1 && logon.resetSeqNum) side()->fail();
    if (logon.resetSeqNum) {
      if (c.seqNo != 1) side()->fail(); ++side()->resetLogons;
    }
  }

  void fixEpochTx(TxContext old, uint64_t epoch, TxProgress p, uint64_t) {
    if (!app()->txInvoked() || old.epoch != side()->txEpoch || epoch != old.epoch + 1 ||
	p.acknowledged < side()->businessEnd || p.acknowledged > p.sent || p.sent != p.loaded)
      side()->fail();
    side()->txEpoch = epoch; side()->lastLoad = 0; side()->businessEnd = 1;
    side()->history.length(0); ++side()->epochsTx;
  }

  void fixEpochRx(uint64_t, uint64_t old, uint64_t epoch, RxProgress p) {
    if (!app()->rxInvoked() || epoch != old + 1 || p.received != p.processed || p.gapBegin)
      side()->fail();
    ++side()->epochsRx;
  }

  void fixEventRx(SessionEvent e, ZmRef<Msg>) {
    if (e.actions & SessionAction::Failure) {
      if (!app()->test->fault) side()->fail();
      ++side()->failures;
      if (e.error == SessionError::Timeout) ++side()->timeouts;
    }
    if (e.actions & SessionAction::Pause) {
      if (side()->observedReady && (e.actions & SessionAction::Recovery))
	++side()->recoveryPauses;
      side()->observedReady = false;
    }
    if (e.state != State::Established) side()->observedReady = false;
    if (e.actions & SessionAction::Ready) {
      if (side()->observedReady || (e.actions & SessionAction::Pause) ||
	  e.state != State::Established || !this->session_().ready_() ||
	  !this->session_().txDone_()) side()->fail();
      side()->observedReady = true;
      if (side()->recoveryPauses.load_() > side()->recoveryReadies.load_())
	++side()->recoveryReadies;
      ++side()->readyCount; side()->ready.post();
    }
    if (e.actions & SessionAction::Drained) side()->drained.post();
    if (e.actions & SessionAction::ResetReady) side()->prepared.post();
  }

  void fixControlRx(ZmRef<Msg> msg) {
    if (!app()->rxInvoked()) side()->fail();
    if (msg && msg->data().control.template is<Session::ResendReq>()) ++side()->resendReqs;
  }

  void fixResetRx(ResetRequest, bool accepted) {
    if (!app()->rxInvoked()) side()->fail(); side()->accepted = accepted; side()->result.post();
  }

  Fmt::Error fixBusinessRx(Msg *msg, Fmt::Limits limits) {
    Fmt::MessageHandler<Order> handler{msg->data().frame, limits}; auto o = handler.ctor();
    if (!handler.valid()) return handler.error();
    if (!app()->rxInvoked() || o.phase != this->session_().epoch_() ||
	msg->data().seqNo < 2) side()->fail();
    side()->received.post();
    if constexpr (!Client) order(o.phase);
    return {};
  }

  void fixStopRx(uint64_t, uint64_t ticket) { if (ticket) side()->fail(); }

  void fixStopTx(uint64_t) {
    if (side()->queue.count_() &&
	!(Client && app()->test->fault && side()->queue.count_() == 1)) side()->fail();
  }

  void fixStoppedRx(bool) { side()->stopped.post(); }
};

struct CliHub : Zfix::Client<CliHub, Dictionary, Network> {
  struct Link;
  TestState	*test;

  explicit CliHub(TestState *s) : test{s} { }

  unsigned reconnFreq() const { return 0; }

  bool init(Network::ClientParams params);
};

struct CliHub::Link : public ZmNHeap<Link>, Peer<Link, Zfix::CliLink<CliHub, Link, Dictionary,
  Network>, true> {
friend inline ZuStringT<"Zfix.reset_io_test.Link"> ZmNHeapID(Link *);
  using ZmNHeap<Link>::operator new;
  using ZmNHeap<Link>::operator delete;
  using Peer::Peer;
};

bool CliHub::init(Network::ClientParams params) {
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
  return Zfix::Client<CliHub, Dictionary, Network>::init(ZuMv(params));
}

struct SrvHub : Zfix::Server<SrvHub, Dictionary, Network> {
  struct Link;
  TestState	*test;
  ZmRef<Link>	link;

  explicit SrvHub(TestState *s) : test{s} { }

  ZiConnection *accepted(const ZiCxnInfo &);

  ZiIP localIP() const { return ZiIP{"127.0.0.1"}; }
  unsigned localPort() const { return 0; }
  unsigned nAccepts() const { return 1; }

  void listening(const ZiListenInfo &i) { test->port = i.port; test->listening.post(); }

  void listenFailed(bool) { test->srv.fail(); test->listening.post(); }

  bool init(Network::ServerParams params);
};

struct SrvHub::Link : public ZmNHeap<Link>, Peer<Link, Zfix::SrvLink<SrvHub, Link, Dictionary,
  Network>, false> {
friend inline ZuStringT<"Zfix.reset_io_test.Link"> ZmNHeapID(Link *);
  using ZmNHeap<Link>::operator new;
  using ZmNHeap<Link>::operator delete;
  using Peer::Peer;
};

bool SrvHub::init(Network::ServerParams params) {
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
  return Zfix::Server<SrvHub, Dictionary, Network>::init(ZuMv(params));
}

ZiConnection *SrvHub::accepted(const ZiCxnInfo &i) { return new Link::Cxn{link.ptr(), i}; }
struct Progress {
  uint64_t	epoch = 0;
  RxProgress	rx;
  TxProgress	tx;
};

struct Snapshot {
  Progress	state;
  uint64_t	connection = 0;
  unsigned	phase = 0;
  bool		drained = false, timers = false;
  RxStats	rx;
  TxStats	tx;
  PostedState	admin;
  unsigned	queued = 0;

  ResetRequest request(unsigned op, uint64_t next = 0) const {
    return {.connection = connection, .epoch = state.epoch, .nextEpoch = next, .op = op};
  }
};

template <typename Link> static bool inspect(Link *link, Snapshot &out) {
  out = {};
  return ZmBlock<bool>{}([link, &out](auto wake) {
    link->app()->rxRun([link, &out, wake = ZuMv(wake)]() mutable {
      const auto &s = link->session_();
      out.connection = s.connection_(); out.phase = s.state_(); out.drained = s.drained_();
      out.rx = link->rxStats(); out.admin = link->adminPosted();
      for (unsigned kind = SessionTimer::Logon; kind <= SessionTimer::Drain; ++kind)
	out.timers |= *s.deadline_(kind);
      out.state.epoch = link->session_().epoch_(); out.state.rx = link->rxProgress();
      link->app()->txRun([link, &out, wake = ZuMv(wake)]() mutable {
	out.tx = link->txStats(); out.queued = link->side()->queue.count_();
	out.state.tx = link->txProgress(); wake(true);
      });
    });
  });
}
template <typename Link> static bool command(Link *link, Side &side, ResetRequest request) {
  link->app()->mx()->run([link = ZmRef(link), request] { link->reset(request); }, 5);
  return wait(side.result) && side.accepted;
}
static void resetLoop(unsigned mode, unsigned path, bool clientDriver = true,
  bool initial = false, unsigned fault = Fault::None) {
  ZuTestScope(resetLoop);
  TestState state{mode}; state.fault = fault; CliHub client{&state}; SrvHub server{&state};
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(5)
      .thread(1, [](auto &t) { t.isolated(1); }).thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); }).thread(4, [](auto &t) { t.isolated(1); })
      .thread(5, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  Network::ClientParams cp{&mx, "3", "4"}; Network::ServerParams sp{&mx, "3", "4"};
#ifdef ZFIX_TEST_TLS
  const char *cert = getenv("ZFIX_TEST_CERT"), *key = getenv("ZFIX_TEST_KEY");
  ZuCheck(cert && key); if (!cert || !key) { mx.stop(); return; }
  cp.caPath(cert); sp.certPath(cert).keyPath(key);
#endif
  bool ci = client.init(ZuMv(cp)), si = server.init(ZuMv(sp)); ZuCheck(ci && si);
  if (!ci || !si) { client.final(); server.final(); mx.stop(); return; }
  bool started = mx.start(); ZuCheck(started); if (!started) return;
  bool cs = client.start(), ss = server.start(); ZuCheck(cs && ss);
  if (!cs || !ss) {
    if (cs) client.stop(); if (ss) server.stop();
    client.final(); server.final(); mx.stop(); return;
  }
  ZmRef<CliHub::Link> link = new CliHub::Link{&client}; server.link = new SrvHub::Link{&server};
  Snapshot c, s;
  if (initial) {
    ZuCheck(!command(link.ptr(), state.cli, {.connection = 1, .epoch = 1,
      .op = ResetOp::OnOpen}));
    ZuCheck(!command(link.ptr(), state.cli, {.epoch = 0, .op = ResetOp::OnOpen}));
    ZuCheck(command(link.ptr(), state.cli, {.epoch = 1, .op = ResetOp::OnOpen}));
    ZuCheck(command(server.link.ptr(), state.srv, {.epoch = 1, .op = ResetOp::OnOpen}));
  }
  server.listen(); ZuCheck(wait(state.listening) && state.port);
  auto connect = [&link, &state] {
#ifdef ZFIX_TEST_TLS
    link->connect("localhost", state.port, ZiIP{"127.0.0.1"});
#else
    link->connect("127.0.0.1", state.port);
#endif
  };
  connect(); ZuCheck(wait(state.cli.ready)); ZuCheck(wait(state.srv.ready));
  link->order(1);
  ZuCheck(wait(state.cli.received)); ZuCheck(wait(state.srv.received));
  ZuCheck(wait(state.cli.completed)); ZuCheck(wait(state.srv.completed));
  ZuCheck(inspect(link.ptr(), c)); ZuCheck(inspect(server.link.ptr(), s));
  ZuCheck(!command(link.ptr(), state.cli, c.request(ResetOp::Prepare, 2)));
  ZuCheck(command(link.ptr(), state.cli, c.request(ResetOp::Drain)));
  ZuCheck(command(server.link.ptr(), state.srv, s.request(ResetOp::Drain)));
  ZuCheck(wait(state.cli.drained)); ZuCheck(wait(state.srv.drained));
  ZuCheck(inspect(link.ptr(), c)); ZuCheck(inspect(server.link.ptr(), s));
  ZuCheck(c.drained && s.drained && c.state.epoch == 1 && s.state.epoch == 1);
  // Retain unsequenced work through live turnover; reconnect uses an empty queue.
  if (path == ResetPath::Live) link->order(2);
  auto invalid = c.request(ResetOp::Prepare, 2); invalid.path = ResetPath::Reconnect + 1;
  ZuCheck(!command(link.ptr(), state.cli, invalid));
  invalid = c.request(ResetOp::Prepare, 1); ZuCheck(!command(link.ptr(), state.cli, invalid));
  auto cr = c.request(ResetOp::Prepare, 2), sr = s.request(ResetOp::Prepare, 2);
  cr.initiator = clientDriver; sr.initiator = !clientDriver; cr.path = sr.path = path;
  ZuCheck(command(link.ptr(), state.cli, cr)); ZuCheck(command(server.link.ptr(), state.srv, sr));
  ZuCheck(wait(state.cli.prepared)); ZuCheck(wait(state.srv.prepared));
  ZuCheck(inspect(link.ptr(), c)); ZuCheck(inspect(server.link.ptr(), s));
  ZuCheck(c.state.epoch == 2 && s.state.epoch == 2 && c.state.tx.loaded == 1
    && s.state.tx.loaded == 1);
  ZuCheck(c.state.rx.processed == 1 && s.state.rx.processed == 1);
  ZuCheck(!command(link.ptr(), state.cli, cr)); // stale old-epoch prepare
  if (fault) {
    bool startedLogon = fault == Fault::StartedClose || fault == Fault::StartedTimeout;
    if (startedLogon || fault == Fault::RefusedLogon)
      ZuCheck(command(link.ptr(), state.cli, c.request(ResetOp::Start)));
    if (startedLogon) {
      // The reset Session::Logon was locally written and is now a retained complete
      // frame on the peer, whose reset coordinator has not started Session::Logon.
      ZuCheck(wait(state.cli.resetSent)); ZuCheck(wait(state.srv.blocked));
    }
    if (fault == Fault::PreparedClose || fault == Fault::StartedClose) link->disconnect();
    // Other cases close through the actual owner deadline or preparation fault.
    bool cliStopped = wait(state.cli.stopped), srvStopped = wait(state.srv.stopped);
    ZuCheck(cliStopped && srvStopped);
    if (!cliStopped || !srvStopped) {
      // A missing deadline must fail without leaving this fixture connected.
      link->disconnect();
      if (!cliStopped) ZuCheck(wait(state.cli.stopped));
      if (!srvStopped) ZuCheck(wait(state.srv.stopped));
    }
    ZuCheck(inspect(link.ptr(), c)); ZuCheck(inspect(server.link.ptr(), s));
    ZuCheck(c.phase == State::Disconnected && s.phase == State::Disconnected);
    ZuCheck(c.state.epoch == 2 && s.state.epoch == 2 && c.connection == 1 && s.connection == 1);
    ZuCheck(c.state.tx.loaded == unsigned(startedLogon ? 2 : 1) && s.state.tx.loaded == 1);
    ZuCheck(c.state.tx.sent == c.state.tx.loaded && s.state.tx.sent == 1);
    ZuCheck(c.state.rx.processed == 1 && s.state.rx.processed == 1 && c.queued == 1 && !s.queued);
    ZuCheck(!c.timers && !s.timers && !c.admin.count && !s.admin.count);
    ZuCheck(!c.rx.pendingBytes && !s.rx.pendingBytes);
    ZuCheck(!c.tx.wire.queuedCount && !s.tx.wire.queuedCount && !c.tx.inFlight && !s.tx.inFlight);
    ZuCheck(state.cli.readyCount.load_() == 1 && state.srv.readyCount.load_() == 1);
    ZuCheck(state.cli.failures.load_() && state.srv.failures.load_());
    if (fault == Fault::PreparedTimeout || fault == Fault::StartedTimeout)
      ZuCheck(state.cli.timeouts.load_() + state.srv.timeouts.load_() > 0);
    if (fault == Fault::RefusedLogon) ZuCheck(state.cli.outputs.load_() == 1);
    auto previous = c;
    ZuCheck(!command(link.ptr(), state.cli, c.request(ResetOp::Start)));
    connect();
    // Continuing epoch 2 retains every assigned reset-Session::Logon sequence. A
    // missing first Session::Logon is recovered; reopening never arms another reset.
    ZuCheck(wait(state.cli.ready)); ZuCheck(wait(state.srv.ready));
    ZuCheck(inspect(link.ptr(), c)); ZuCheck(inspect(server.link.ptr(), s));
    ZuCheck(c.state.epoch == 2 && s.state.epoch == 2 && c.connection == 2 && s.connection == 2);
    ZuCheck(c.state.tx.loaded > previous.state.tx.loaded);
    ZuCheck(!command(link.ptr(), state.cli, previous.request(ResetOp::Start)));
  } else if (path == ResetPath::Reconnect) {
    bool ca = false, sa = false; ZmSemaphore issued;
    // Both local owners agreed before either side releases physical transport.
    client.rxRun([link, peer = server.link, cr = c.request(ResetOp::Disconnect),
	sr = s.request(ResetOp::Disconnect), &ca, &sa, &issued] {
      ca = link->reset_(cr); sa = peer->reset_(sr); issued.post();
    });
    ZuCheck(wait(issued) && ca && sa);
    ZuCheck(wait(state.cli.stopped)); ZuCheck(wait(state.srv.stopped));
    connect();
  } else if (clientDriver) {
    ZuCheck(command(link.ptr(), state.cli, c.request(ResetOp::Start)));
    ZuCheck(command(server.link.ptr(), state.srv, s.request(ResetOp::Start)));
  } else {
    ZuCheck(command(server.link.ptr(), state.srv, s.request(ResetOp::Start)));
    ZuCheck(command(link.ptr(), state.cli, c.request(ResetOp::Start)));
  }
  if (!fault) { ZuCheck(wait(state.cli.ready)); ZuCheck(wait(state.srv.ready)); }
  if (path == ResetPath::Reconnect) link->order(2);
  ZuCheck(wait(state.cli.received)); ZuCheck(wait(state.srv.received));
  ZuCheck(wait(state.cli.completed)); ZuCheck(wait(state.srv.completed));
  ZuCheck(inspect(link.ptr(), c)); ZuCheck(inspect(server.link.ptr(), s));
  ZuCheck(c.state.epoch == 2 && s.state.epoch == 2 && c.phase == State::Established
    && s.phase == State::Established);
  ZuCheck(c.connection == (fault || path == ResetPath::Reconnect ? 2 : 1)
    && s.connection == c.connection);
  ZuCheck(state.cli.epochsRx.load_() == 1 && state.srv.epochsRx.load_() == 1);
  ZuCheck(state.cli.epochsTx.load_() == 1 && state.srv.epochsTx.load_() == 1);
  unsigned cliLogons = fault ? unsigned(fault == Fault::StartedClose
    || fault == Fault::StartedTimeout) : (initial ? 2 : 1);
  unsigned srvLogons = fault ? 0 : (initial ? 2 : 1);
  ZuCheck(state.cli.resetLogons.load_() == cliLogons
    && state.srv.resetLogons.load_() == srvLogons);
  ZuCheck(!c.queued && !s.queued);
  // A later peer ResendReq may legitimately pause/reopen readiness after Logon.
  ZuCheck(readyTransitions(state.cli) && readyTransitions(state.srv));
  auto stale = c.request(ResetOp::Drain); stale.connection = 0;
  ZuCheck(!command(link.ptr(), state.cli, stale));
  link->logout(); ZuCheck(wait(state.cli.stopped)); ZuCheck(wait(state.srv.stopped));
  bool stoppedC = client.stop(), stoppedS = server.stop(); ZuCheck(stoppedC && stoppedS);
  client.final(); server.final(); mx.stop(); link = nullptr; server.link = nullptr;
  ZuCheck(!state.cli.errors.load_() && !state.srv.errors.load_());
}
int main(int argc, char **argv) {
  parse(argc, argv); ZuTestMain();
  ZuTestCall(resetLoop, Transport::FIX4, ResetPath::Live);
  ZuTestCall(resetLoop, Transport::FIX44A, ResetPath::Live);
  ZuTestCall(resetLoop, Transport::FIXT11, ResetPath::Live);
  ZuTestCall(resetLoop, Transport::FIX4, ResetPath::Live, false);
  ZuTestCall(resetLoop, Transport::FIX44A, ResetPath::Live, false);
  ZuTestCall(resetLoop, Transport::FIXT11, ResetPath::Live, false);
  ZuTestCall(resetLoop, Transport::FIX4, ResetPath::Reconnect);
  ZuTestCall(resetLoop, Transport::FIX44A, ResetPath::Reconnect);
  ZuTestCall(resetLoop, Transport::FIXT11, ResetPath::Reconnect);
  ZuTestCall(resetLoop, Transport::FIX4, ResetPath::Live, true, true);
  ZuTestCall(resetLoop, Transport::FIX44A, ResetPath::Live, true, true);
  ZuTestCall(resetLoop, Transport::FIXT11, ResetPath::Live, true, true);
  ZuTestCall(resetLoop, Transport::FIX4, ResetPath::Live, true, false, Fault::PreparedClose);
  ZuTestCall(resetLoop, Transport::FIX4, ResetPath::Live, true, false, Fault::PreparedTimeout);
  ZuTestCall(resetLoop, Transport::FIX4, ResetPath::Live, true, false, Fault::StartedClose);
  ZuTestCall(resetLoop, Transport::FIX4, ResetPath::Live, true, false, Fault::StartedTimeout);
  ZuTestCall(resetLoop, Transport::FIX4, ResetPath::Live, true, false, Fault::RefusedLogon);
  ZuTestCall(resetLoop, Transport::FIX44A, ResetPath::Live, true, false, Fault::PreparedClose);
  ZuTestCall(resetLoop, Transport::FIX44A, ResetPath::Live, true, false, Fault::PreparedTimeout);
  ZuTestCall(resetLoop, Transport::FIX44A, ResetPath::Live, true, false, Fault::StartedClose);
  ZuTestCall(resetLoop, Transport::FIX44A, ResetPath::Live, true, false, Fault::StartedTimeout);
  ZuTestCall(resetLoop, Transport::FIX44A, ResetPath::Live, true, false, Fault::RefusedLogon);
  ZuTestCall(resetLoop, Transport::FIXT11, ResetPath::Live, true, false, Fault::PreparedClose);
  ZuTestCall(resetLoop, Transport::FIXT11, ResetPath::Live, true, false, Fault::PreparedTimeout);
  ZuTestCall(resetLoop, Transport::FIXT11, ResetPath::Live, true, false, Fault::StartedClose);
  ZuTestCall(resetLoop, Transport::FIXT11, ResetPath::Live, true, false, Fault::StartedTimeout);
  ZuTestCall(resetLoop, Transport::FIXT11, ResetPath::Live, true, false, Fault::RefusedLogon);
  return 0;
}
