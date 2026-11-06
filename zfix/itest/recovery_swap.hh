//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef recovery_swap_HH
#define recovery_swap_HH

// Included by recovery_io_test.cc: reuse its application store/queue and native
// parameters, while keeping the live replacement scenario independent of cold
// failover. Actual sockets carry every frame except the deliberately lost seq2.
struct ReplayState : TestState {
  const bool		logoutA;
  // Contended test synchronization; no live owner data is read through it.
  alignas(Zm::CacheLineSize)
  ZmSemaphore		initialReady, peerReady, resendPending, successorLogon,
    oldClose, oldRetired, selected, replayComplete, archived, serverStopped;
  ZmAtomic<unsigned>	accepts{0}, serverStops{0}, delivered{0}, replaced{0}, suppressed{0};
  ZmAtomic<unsigned>	aStops{0}, bStops{0};
  ZmAtomic<unsigned>	requests{0}, replayLoads{0};
  // I/O Rx publishes this handle via oldClose; main posts its consumption there.
  alignas(Zm::CacheLineSize)
  ZmFn<void()>		retire;
  // AppTx, inspected from main only after a semaphore or the final owner drain.
  alignas(Zm::CacheLineSize)
  uint64_t		ticket = 0; // AppTx, published by resendPending
  TxContext		request;
  TxBody::Ref		replacement;
  uint64_t		archiveBegin = 0, archiveEnd = 0; // AppTx-only acknowledged prefix
  uint64_t		probeTarget = 0;

  ReplayState(unsigned m, bool logout) : logoutA{logout} { mode = m; }
};

struct ReplayServer : Zfix::Server<ReplayServer, Dictionary, Network> {
  struct Link;
  struct Cxn;
  ReplayState		*state;
  ZmRef<Link>		session; // I/O Rx, application routing

  explicit ReplayServer(ReplayState *s) : state{s} { }

  ZiIP localIP() const { return ZiIP{"127.0.0.1"}; }
  unsigned localPort() const { return 0; }
  unsigned nAccepts() const { return 1; }

  void listening(const ZiListenInfo &i) { state->port = i.port; state->listening.post(); }
  void listenFailed(bool) { state->fail(); state->listening.post(); }
  ZiConnection *accepted(const ZiCxnInfo &);

  bool init(Network::ServerParams params);
};

struct ReplayClient : Zfix::Client<ReplayClient, Dictionary, Network> {
  struct Link;
  ReplayState	*state;

  explicit ReplayClient(ReplayState *s) : state{s} { }

  bool init(Network::ClientParams params);
};

struct ReplayServer::Link : ZmNHeap<Link>,
    Peer<Link, Zfix::SrvLink<ReplayServer, Link, Dictionary, Network>, false> {
friend inline ZuStringT<"Zfix.ReplaySwap.Server"> ZmNHeapID(Link *);
  using ZmNHeap<Link>::operator new;
  using ZmNHeap<Link>::operator delete;
  using Parent = Peer<Link, Zfix::SrvLink<ReplayServer, Link, Dictionary, Network>, false>;
  using Parent::Peer;

  void fixEventRx(SessionEvent e, ZmRef<Msg>) {
    if (e.actions & SessionAction::Failure) state()->fail();
    if (e.actions & SessionAction::Replay) {
      if (e.replayBegin == 2) ++state()->requests;
    }
    if ((e.actions & SessionAction::Ready) && readyConnection != e.connection) {
      readyConnection = e.connection; state()->peerReady.post();
    }
  }

  void fixControlRx(ZmRef<Msg> input) {
    const auto &control = input->data().control;
    if (!control.is<Session::ResendReq>()) return;
    // Later gaps can legitimately start at seq3 after seq2 has arrived.
    if (control.p<Session::ResendReq>().beginSeqNo == 2) ++state()->requests;
  }

  void fixPublishedTx(TxBody::Ref body, TxContext c, ZuDateTime original) {
    dropFrame = false;
    if (!c.replay) {
      auto record = store->find(c.epoch, c.seqNo);
      if (!record || record->body != body) { state()->fail(); return; }
      record->originalTime = original;
      if (body->msgType() == "D" && c.seqNo == 2) {
	dropFrame = true; state()->oldSeq = c.seqNo; state()->oldTime = original;
      }
      if (body->msgType() == "A" && c.connection == 2) state()->successorLogon.post();
      if (state()->logoutA && body->msgType() == "5" && c.connection == 1)
	dropFrame = true; // retain A in Logout until B displaces its socket
      if (body->msgType() == "1") {
	if (c.connection != 2 || state()->probeTarget) state()->fail();
	state()->probeTarget = c.seqNo + 1;
      }
    } else if (body->msgType() == "D") {
      if (c.connection != 2 || c.epoch != 1 || c.seqNo != 2 ||
	  body != state()->replacement || original != state()->oldTime) state()->fail();
      if (state()->replaced.xchAdd(1) == 1) state()->replayComplete.post();
    }
  }

  TxReload fixRetrieveTx(TxContext c, uint64_t, uint64_t ticket) {
    if (!app()->txInvoked() || !c.replay || c.epoch != 1) state()->fail();
    switch (c.seqNo) {
      case 2:
	++state()->replayLoads;
	if (!state()->ticket) {
	  state()->ticket = ticket; state()->request = c;
	  state()->resendPending.post(); return {{}, true};
	}
	// A repeated recovery request still consults application policy.
	return {TxHistory{state()->replacement, state()->oldTime, 1, 2}};
      case 3: ++state()->suppressed; return {};
      default: return {}; // suppress operational controls
    }
  }

  void fixArchiveTx(uint64_t epoch, uint64_t begin, uint64_t end) {
    auto s = state();
    if (!app()->txInvoked() || epoch != 1 || begin >= end ||
	(s->archiveEnd && s->archiveEnd != begin)) s->fail();
    if (!s->archiveBegin) s->archiveBegin = begin;
    s->archiveEnd = end;
    if (s->probeTarget && end >= s->probeTarget) {
      s->probeTarget = 0; s->archived.post();
    }
  }

  void fixStopTx(uint64_t) {
    store->tx = this->txProgress_();
    queue.clean();
  }

  void fixStoppedRx(bool) { ++state()->serverStops; state()->serverStopped.post(); }
};

inline bool ReplayServer::init(Network::ServerParams params) {
  ZmRef<Link::RxAllocator> rx;
  ZmRef<Link::TxAllocator> tx;
  if (!params.rxAllocator()) {
    rx = new Link::RxAllocator{};
    params.rxAllocator(rx.ptr());
  }
  if (!params.txAllocator()) {
    tx = new Link::TxAllocator{};
    params.txAllocator(tx.ptr());
  }
  return Zfix::Server<ReplayServer, Dictionary, Network>::init(ZuMv(params));
}

struct ReplayServer::Cxn : ZmNHeap<Cxn>, Link::Cxn {
friend inline ZuStringT<"Zfix.ReplaySwap.Cxn"> ZmNHeapID(Cxn *);
  using ZmNHeap<Cxn>::operator new;
  using ZmNHeap<Cxn>::operator delete;
  using Base = Link::Cxn;
  ReplayState	*const state;
  ReplayServer	*const hub;
  const bool	first;
  bool		held = false; // native Rx

  Cxn(Link *link, const ZiCxnInfo &info, bool initial) : Base{ZmRef<Link>{link}, info},
    state{link->state()}, hub{link->app()}, first{initial} { }

  void disconnected(bool peer) override {
    if (held) return;
    if (!first) { Base::disconnected(peer); return; }
    held = true;
    state->retire = [self = ZmRef(this), peer] {
      self->Base::disconnected(peer);
      // Follow the native cleanup's real AppRx→AppTx→IOTx→AppRx FIFO chain.
      self->hub->rxRun([self] {
	self->hub->txRun([self] {
	  self->mx()->txRun([self] {
	    self->hub->rxRun([self] { self->state->oldRetired.post(); });
	  });
	});
      });
    };
    state->oldClose.post();
  }
};

ZiConnection *ReplayServer::accepted(const ZiCxnInfo &info)
{
  return new Cxn{session.ptr(), info, state->accepts.xchAdd(1) == 0};
}

struct ReplayClient::Link : ZmNHeap<Link>,
    Peer<Link, Zfix::CliLink<ReplayClient, Link, Dictionary, Network>, true> {
friend inline ZuStringT<"Zfix.ReplaySwap.Client"> ZmNHeapID(Link *);
  using ZmNHeap<Link>::operator new;
  using ZmNHeap<Link>::operator delete;
  using Parent = Peer<Link, Zfix::CliLink<ReplayClient, Link, Dictionary, Network>, true>;
  const bool	initial;

  Link(ReplayClient *hub, bool first) : Parent{hub}, initial{first} { }

  void fixEventRx(SessionEvent e, ZmRef<Msg>) {
    if ((e.actions & SessionAction::Failure) && !initial) state()->fail();
    if (!(e.actions & SessionAction::Ready) || readyConnection == e.connection) return;
    readyConnection = e.connection;
    (initial ? state()->initialReady : state()->done).post();
  }

  Fmt::Error fixReceiveRx(OrderMsg *, const Order &o, const RequestContext &context) {
    if (initial || o.id != "repriced" || context.context().epoch != 1 ||
	context.context().seqNo != 2 || !context.context().possDup ||
	context.origSendingTime() != state()->oldTime) state()->fail();
    ++state()->delivered; state()->selected.post(); return {};
  }

  void fixStoppedRx(bool) {
    ++(initial ? state()->aStops : state()->bStops);
    state()->stopped(true);
  }
};

inline bool ReplayClient::init(Network::ClientParams params) {
  ZmRef<Link::RxAllocator> rx;
  ZmRef<Link::TxAllocator> tx;
  if (!params.rxAllocator()) {
    rx = new Link::RxAllocator{};
    params.rxAllocator(rx.ptr());
  }
  if (!params.txAllocator()) {
    tx = new Link::TxAllocator{};
    params.txAllocator(tx.ptr());
  }
  return Zfix::Client<ReplayClient, Dictionary, Network>::init(ZuMv(params));
}

static void replayReplacement(unsigned mode, bool logoutA)
{
  ZuTestScope(replayReplacement);
  ReplayState state{mode, logoutA};
  ReplayServer server{&state}; ReplayClient client{&state};
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(5)
      .thread(1, [](auto &t) { t.isolated(1); }).thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); }).thread(4, [](auto &t) { t.isolated(1); })
      .thread(5, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  bool started = mx.start(); ZuCheck(started); if (!started) return;
  Network::ServerParams sp{&mx, "3", "4"}; Network::ClientParams cp{&mx, "3", "4"};
#ifdef ZFIX_TEST_TLS
  auto cert = getenv("ZFIX_TEST_CERT"), key = getenv("ZFIX_TEST_KEY");
  if (!cert || !key) { ZuCheck(false); mx.stop(); return; }
  sp.certPath(cert).keyPath(key); cp.caPath(cert);
#endif
  bool si = server.init(ZuMv(sp)), ci = client.init(ZuMv(cp)); ZuCheck(si && ci);
  bool ss = si && server.start(), cs = ci && client.start(); ZuCheck(ss && cs);
  ZmRef<ReplayClient::Link> a, b;
  bool attemptedA = false, attemptedB = false, success = false;
  auto connectPeer = [&state](ReplayClient::Link *link) {
#ifdef ZFIX_TEST_TLS
    link->connect("localhost", state.port, ZiIP{"127.0.0.1"});
#else
    link->connect("127.0.0.1", state.port);
#endif
  };
  if (ss && cs) do {
    server.session = new ReplayServer::Link{&server};
    a = new ReplayClient::Link{&client, true};
    server.listen(); bool listening = wait(state.listening); ZuCheck(listening);
    if (!listening) break;
    attemptedA = true; connectPeer(a.ptr());
    bool ready = wait(state.initialReady) && wait(state.peerReady); ZuCheck(ready);
    if (!ready) break;
    Progress checkpoint;
    bool snapshot = ZmBlock<bool>{}([&client, &a, &checkpoint](auto wake) {
      client.rxRun([&a, &checkpoint, wake = ZuMv(wake)]() mutable {
	checkpoint.epoch = a->session_().epoch_(); checkpoint.rx = a->rxProgress_(); wake(true);
      });
    });
    ZuCheck(snapshot && checkpoint.rx.processed == 2);
    server.txRun([link = server.session] {
      Fmt::Error error;
      auto old = TxBody::encode<OrderMsg>(Order{"old"}, 301, error);
      auto stale = TxBody::encode<OrderMsg>(Order{"stale"}, 302, error);
      if (error || !old || !stale) { link->state()->fail(); return; }
      link->enqueue(ZuMv(old)); link->enqueue(ZuMv(stale));
    });
    bool pending = wait(state.resendPending); ZuCheck(pending);
    if (!pending) break;
    snapshot = ZmBlock<bool>{}([&client, &a, &checkpoint](auto wake) {
      client.txRun([&a, &checkpoint, wake = ZuMv(wake)]() mutable {
	checkpoint.tx = a->txProgress_(); wake(true);
      });
    });
    ZuCheck(snapshot && checkpoint.tx.loaded > 2 && state.request.connection == 1 &&
      state.request.seqNo == 2 && state.ticket);
    state.client = new Store;
    b = new ReplayClient::Link{&client, false};
    bool restored = ZmBlock<bool>{}([&client, &b, &checkpoint](auto wake) {
      client.rxRun([&client, &b, &checkpoint, wake = ZuMv(wake)]() mutable {
	if (!b->restoreRx_(checkpoint.epoch, checkpoint.rx)) { wake(false); return; }
	client.txRun([&b, &checkpoint, wake = ZuMv(wake)]() mutable {
	  if (!b->restoreTx_(checkpoint.epoch, checkpoint.tx)) { wake(false); return; }
	  b->store->lastLoad = checkpoint.tx.loaded - 1; wake(true);
	});
      });
    });
    ZuCheck(restored); if (!restored) break;
    if (logoutA) {
      server.session->logout();
      bool loggingOut = ZmBlock<bool>{}([&server](auto wake) {
	server.rxRun([link = server.session, wake = ZuMv(wake)]() mutable {
	  wake(link->session_().state_() == State::Logout && link->cxn());
	});
      });
      ZuCheck(loggingOut); if (!loggingOut) break;
    }
    attemptedB = true; connectPeer(b.ptr());
    bool bound = wait(state.successorLogon) && wait(state.oldClose); ZuCheck(bound);
    if (!bound) break;
    server.txRun([link = server.session] {
      Fmt::Error error;
      auto s = link->state();
      s->replacement = TxBody::encode<OrderMsg>(Order{"repriced"}, 401, error);
      if (error || !s->replacement ||
	  !link->reloaded(s->ticket, TxHistory{s->replacement, s->oldTime, 1, 2})) s->fail();
    });
    bool recovered = wait(state.selected) && wait(state.done) && wait(state.peerReady) &&
      wait(state.replayComplete);
    ZuCheck(recovered); if (!recovered) break;
    ZuCheck(state.delivered.load_() == 1);
    // A's pending request and B's recovery request each select app content.
    // Repeated requests legitimately retransmit; the receiver applies seq2 once.
    ZuCheck(state.requests.load_() >= 2 && state.replayLoads.load_() >= 2 &&
      state.replaced.load_() == state.replayLoads.load_());
    ZuCheck(state.suppressed.load_());
    ZuCheck(!state.serverStops.load_() && state.retire);
    server.session->probe();
    bool archived = wait(state.archived); ZuCheck(archived);
    mx.rxRun([&state] { auto retire = ZuMv(state.retire); if (retire) retire(); });
    bool retired = wait(state.oldRetired); ZuCheck(retired);
    // Prove B can exchange a fresh TestRequest/Heartbeat after every A cleanup stage.
    server.session->probe();
    bool usable = wait(state.archived); ZuCheck(usable);
    success = archived && retired && usable;
  } while (false);
  // Always settle a held logical request before waiting for terminal owner drain.
  if (server.session) server.txRun([link = server.session] {
    auto s = link->state();
    if (s->ticket) (void)link->reloaded(s->ticket, {});
  });
  mx.rxRun([&state] { auto retire = ZuMv(state.retire); if (retire) retire(); });
  if (attemptedB) { if (success) b->logout(); else b->disconnect(); }
  else if (attemptedA) a->disconnect();
  bool stoppedA = !attemptedA || wait(state.cliStopped);
  bool stoppedB = !attemptedB || wait(state.cliStopped);
  bool stoppedServer = !state.accepts.load_() || wait(state.serverStopped);
  ZuCheck(stoppedA && stoppedB && stoppedServer);
  if (!stoppedA || !stoppedB || !stoppedServer) abort();
  bool cstop = !cs || client.stop(), sstop = !ss || server.stop(); ZuCheck(cstop && sstop);
  if (ci) client.final();
  if (si) server.final();
  mx.stop(); a = b = nullptr; server.session = nullptr;
  ZuCheck(success && !state.errors.load_() && state.serverStops.load_() == 1 &&
    state.accepts.load_() == 2 && state.aStops.load_() == 1 && state.bStops.load_() == 1 &&
    !state.retire);
  ZuCheck(state.archiveBegin == 1 && state.archiveEnd == state.server->tx.acknowledged &&
    state.archiveEnd > 3 && !state.probeTarget && state.delivered.load_() == 1 &&
    state.replaced.load_() == state.replayLoads.load_());
}

#endif /* recovery_swap_HH */
