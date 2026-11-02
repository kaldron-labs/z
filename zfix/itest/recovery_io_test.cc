//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmPlatform.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZiMultiplex.hh>
#include "ZiTestResidue.hh"
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
  ZuCSpan	id;
};

ZfStruct(, (Order), (id, (FIX::ID<11>), String));
using OrderFIXFields = ZuTypeConcat<Zfix::SessionFields<Order>, ZfFields_Order>;
OrderFIXFields ZuFields_(Order *, ZuFacet::FIX *);
ZfStructImpl(Order); ZfStructImpl(Order, FIX);
using OrderMsg = MT<Order, "D">;
using Dictionary = MTList<OrderMsg>;
template <typename T> using Items = ZtArray<T, ZtArrayHeapID<"Zfix.RecoveryIOTest">>;
// Shared pooled wire buffers and core bodies cannot carry fixture FIFO links.
using PendingWire = ZmList<ZmRef<ZiIOBuf>, ZmListHeapID<"Zfix.FailoverIO.Wire">>;
using AppQueue = ZmList<TxBody::Ref, ZmListHeapID<"Zfix.RecoveryIOTest.Queue">>;

// Application-owned test store survives replacement of all connection/session
// owners. Rx/Tx segments have separate owners; main inspects them only after
// both managed stopped barriers. The fixed test workload bounds this history.
struct Progress {
  uint64_t	epoch = 0;
  RxProgress	rx;
  TxProgress	tx;
};

struct Store : public ZmNHeap<Store>, public ZmObject {
friend inline ZuStringT<"Zfix.RecoveryIOTest.Store"> ZmNHeapID(Store *);
  RxProgress		rx; // Rx-owned progress
  TxProgress		tx; // Tx-owned progress
  Items<TxHistory>	history; // Tx-owned application mapping and original bytes
  uint64_t		lastLoad = 0;

  TxHistory *find(uint64_t epoch, uint64_t seq) {
    for (auto &h : history) if (h.epoch == epoch && h.seqNo == seq) return &h;
    return nullptr;
  }
};

// A fixture-local native binary file, not a Zfix storage format. All disk I/O
// runs on main after both owners stop. Scalars are written separately so no
// padding or process pointers enter the file; payload bytes remain length-aware.
template <typename ...T> static bool put(ZiFile &f, const T &...v) {
  return ((f.write(&v, sizeof(v)) == Zi::OK) && ...);
}
template <typename ...T> static bool get(ZiFile &f, T &...v) {
  return ((f.read(&v, sizeof(v)) == sizeof(v)) && ...);
}
static bool putText(ZiFile &f, ZuCSpan text) {
  uint32_t length = text.length();
  return put(f, length) && (!length || f.write(text.data(), length) == Zi::OK);
}
template <typename S> static bool getText(ZiFile &f, S &text) {
  uint32_t length;
  if (!get(f, length) || length > Fmt::Limits::FrameBytes) return false;
  text.length(length); return !length || f.read(text.data(), length) == length;
}
static bool putState(ZiFile &f, const Store &s) {
  const auto &r = s.rx; const auto &t = s.tx;
  return put(f, r.received, r.processed, r.gapBegin, r.gapEnd,
    t.loaded, t.sent, t.acknowledged, t.replayBegin, t.replayEnd);
}
static bool getState(ZiFile &f, Store &s) {
  auto &r = s.rx; auto &t = s.tx;
  return get(f, r.received, r.processed, r.gapBegin, r.gapEnd,
    t.loaded, t.sent, t.acknowledged, t.replayBegin, t.replayEnd);
}
static bool save(const ZiFile::Path &path, const Store &s) {
  ZiFile f{path, ZiFile::Write | ZiFile::GC, 0600};
  uint32_t version = 4, count = s.history.length();
  if (!f || !put(f, version, count, s.lastLoad) || !putState(f, s))
    return false;
  for (const auto &h : s.history) {
    uint64_t id = h.body->id(); bool admin = h.body->administrative();
    const auto &dt = h.originalTime;
    if (!put(f, h.epoch, h.seqNo, id, admin, dt.julian(), dt.sec(), dt.nsec()) ||
	!putText(f, h.body->msgType()) || !putText(f, h.body->body())) return false;
  }
  return f.sync() == Zi::OK;
}
static ZmRef<Store> load(const ZiFile::Path &path) {
  ZiFile f{path, ZiFile::ReadOnly | ZiFile::NoFollow | ZiFile::GC};
  uint32_t version, count; ZmRef<Store> s = new Store;
  if (!f || !get(f, version, count, s->lastLoad) || version != 4 ||
      count > s->lastLoad || !getState(f, *s)) return {};
  for (unsigned i = 0; i < count; ++i) {
    TxHistory h; uint64_t id; bool admin;
    SessionString type, bytes;
    if (!get(f, h.epoch, h.seqNo, id, admin, h.originalTime.julian(),
	h.originalTime.sec(), h.originalTime.nsec()) || !getText(f, type) || !getText(f, bytes))
      return {};
    Fmt::Error error;
    if (admin) h.body = TxBody::restoreControl(type, bytes, id, error);
    else if (type == OrderMsg::MsgType) h.body = TxBody::restore<OrderMsg>(bytes, id, error);
    else return {};
    if (error || !h.body || h.body->body() != bytes) return {};
    s->history.push(ZuMv(h));
  }
  if (f.offset() != f.size()) return {};
  return s;
}
struct TestState {
  ZmRef<Store>		client{new Store}, server{new Store};
  ZmSemaphore		listening, dropped, done, drained, restored, oldDelivered;
  ZmSemaphore		cliStopped, srvStopped, setupFailed, loadPending, wirePending;
  ZmFn<void()>		releaseLoad; // Tx publishes once through loadPending; main consumes
  uint64_t		oldLoad = 0, newLoad = 0, pendingSeq = 0, oldToken = 0, newToken = 0;
  unsigned		oldPart = 0;
  unsigned		responses = 0; // AppRx-owned response admission fault alternation
  ZmAtomic<unsigned>	reloadRefused{0}, reloadAdmitted{0};
  ZmAtomic<unsigned>	setupFailures{0}, newPublished{0}, newCompleted{0};
  ZmAtomic<unsigned>	errors{0}, closed{0}, ready{0}, oldReceived{0}, newReceived{0},
    replayed{0}, retrieved{0}, restoredCount{0}, logons{0};
  // Main hands immutable phase configuration to workers before each open.
  unsigned		mode = Transport::FIX4, version = Version::N, phase = 0, port = 0;
  bool			restart = false, failover = false;
  uint64_t		oldSeq = 0;
  ZuDateTime		oldTime;

  void fail(unsigned line = __builtin_LINE()) {
    log("# ZfixRecoveryIOTest failure line=", line, " mode=", mode, " phase=", phase);
    ++errors; dropped.post(); done.post();
  }

  void stopped(bool client) {
    auto old = closed.xchAdd(1);
    if (client) cliStopped.post(); else srvStopped.post();
    if (old & 1) drained.post();
  }
};

static SessionConfig config(bool client, unsigned mode, unsigned version) {
  SessionConfig c; c.mode = mode; c.initiator = client;
  c.identity.version = version < Version::N ? version :
    mode == Transport::FIXT11 ? Version::VT1_1 : Version::V4_4;
  c.identity.local = client ? "CLIENT" : "SERVER"; c.identity.peer = client ? "SERVER" : "CLIENT";
  c.appVerID = AppVersion::V5_0_SP2; return c;
}

template <typename Impl, typename Base, bool Client>
struct Peer : public Base {
  using Msg = RxQueue::Node;
  using Base::app;
  const ZmRef<Store>	store; // constructor-published handle; segments retain their owners
  uint64_t		readyConnection = 0; // Application Rx-owned
  // Application Tx owns pending publication, queueing and injected wire faults.
  alignas(Zm::CacheLineSize)
  AppQueue		queue;
  PendingWire		pendingWire; // already charged by WireTx
  uint64_t		pendingLoad = 0;
  bool			holdWire = false, holdQueue = false,
    blackholed = false, dropFrame = false;

  template <typename Hub>
  explicit Peer(Hub *hub) :
    Base{hub, config(Client, hub->state->mode, hub->state->version)},
    store{Client ? hub->state->client : hub->state->server} { }

  auto self() { return static_cast<Impl *>(this); }
  auto state() { return this->app()->state; }

  unsigned fixAssociateTx(TxBody::Ref b, TxContext c, uint64_t ticket) {
    if (!this->app()->txInvoked() || c.epoch != 1 || c.seqNo != store->lastLoad + 1)
      state()->fail();
    store->lastLoad = c.seqNo;
    bool business = b->msgType() == "D";
    store->history.push(TxHistory{ZuMv(b), {}, c.epoch, c.seqNo});
    if constexpr (Client) if (state()->failover && business) {
      if (!state()->phase) state()->oldLoad = ticket;
      else {
	pendingLoad = ticket; state()->newLoad = ticket; state()->pendingSeq = c.seqNo;
	state()->releaseLoad = ZmFn<void()>{[this, ticket] {
	  this->app()->txRun([this, ticket] {
	    pendingLoad = 0; this->loaded(ticket, true);
	  });
	}};
	state()->loadPending.post(); return TxLoad::Pending;
      }
    }
    return TxLoad::Accepted;
  }

  void fixArchiveTx(uint64_t epoch, uint64_t begin, uint64_t end) {
    // Keep the fixture's app-owned records available after archive progress.
    // An archive notification carries no payload and releases no core history.
    if (!this->app()->txInvoked() || epoch != 1 || begin >= end) state()->fail();
  }

  TxReload fixRetrieveTx(TxContext context, uint64_t, uint64_t ticket) {
    auto record = store->find(context.epoch, context.seqNo);
    if (!record) return {};
    if (context.replay && record->body->administrative() && record->body->msgType() != "3")
      return {}; // this app suppresses old operational controls
    ++state()->retrieved;
    // Simulate deferred application storage on a distinct owner. Capture one
    // retained body plus fixed history metadata; never share a Tx queue node.
    auto history = *record;
    this->app()->mx()->run([this, ticket, history = ZuMv(history)]() mutable {
      // Storage owns this single outstanding response until AppRx can admit
      // it. On refusal, return the response on Tx without a library post.
      if (this->reloaded(ticket, history)) state()->fail(); // storage owner
      this->app()->rxRun([this, ticket, history = ZuMv(history)]() mutable {
	// Alternate real response delivery with a full hub budget. Existing
	// reservations stay intact; restore its limit before another Rx task.
	auto &budget = this->app()->fixAdminRx_(); auto limits = budget.limits_();
	bool force = !(state()->responses++ & 1);
	if (force && !budget.limits_({budget.state_().bytes})) state()->fail();
	bool accepted = this->reloaded(ticket, history);
	if (force) {
	  bool restored = budget.limits_(limits);
	  if (accepted || !restored) state()->fail();
	  ++state()->reloadRefused;
	} else if (accepted) { ++state()->reloadAdmitted; return; }
	this->app()->txRun([this, ticket, history = ZuMv(history)] {
	  if (!this->reloaded(ticket, history)) state()->fail();
	});
      });
    }, 5);
    return {{}, true};
  }

  void fixPublishedTx(TxBody::Ref body, TxContext c, ZuDateTime original) {
    dropFrame = false;
    if (!c.replay) {
      auto record = store->find(c.epoch, c.seqNo);
      if (!record || record->body != body) { state()->fail(); return; }
      record->originalTime = original;
    }
    if (body->msgType() == "D" && c.replay) {
      auto record = store->find(c.epoch, c.seqNo);
      if (!record || body != record->body || c.seqNo != state()->oldSeq ||
	  original != state()->oldTime || record->originalTime != original) state()->fail();
      ++state()->replayed;
    }
    if constexpr (Client) if (state()->failover && state()->phase &&
	body->msgType() == "D" && !c.replay) {
      holdWire = true; ++state()->newPublished;
    }
    if (Client && !state()->phase && body->msgType() == "D" && !blackholed) {
      blackholed = true; dropFrame = true;
      if (state()->failover) holdQueue = true;
    }
  }

  bool fixSendTx(ZmRef<ZiIOBuf> bytes) {
    if (dropFrame) {
      // Fault injection occurs after logical publication: model bytes accepted
      // locally and lost before peer processing when the connection dies.
      auto record = static_cast<WireBuf *>(bytes.ptr());
      state()->oldToken = record->token; state()->oldPart = record->part;
      this->transportSent(record, true);
      state()->dropped.post(); return true;
    }
    if (holdWire) {
      bool first = !pendingWire.count_();
      auto record = static_cast<WireBuf *>(bytes.ptr());
      if (first) state()->newToken = record->token;
      pendingWire.push(ZuMv(bytes));
      if (wireHeld_()) state()->wirePending.post();
      return true;
    }
    return Base::fixSendTx(ZuMv(bytes));
  }

  bool wireHeld_() const {
    return holdWire && pendingWire.count_() &&
      this->txStats_().wire.queuedCount == pendingWire.count_();
  }

  void releaseWire() {
    this->app()->txRun([this] {
      holdWire = false;
      while (auto node = pendingWire.shift())
	if (!Base::fixSendTx(ZuMv(node->data()))) state()->fail();
    });
  }

  void connectFailed(bool) {
    if (!Client || !state()->failover || !this->app()->rxInvoked()) state()->fail();
    ++state()->setupFailures; state()->setupFailed.post();
  }

  void fixControlTx(TxBody::Ref b, TxContext c) {
    if (b->msgType() != "A") return;
    Session::Logon logon; Fmt::Error error;
    Fmt::read<Session::Logon, ZuFacet::FIX, ArchiveField>(logon, b->body(), error);
    if (error || logon.resetSeqNum || c.epoch != 1) state()->fail();
    if (state()->phase && c.seqNo != store->tx.loaded) state()->fail();
    ++state()->logons;
  }

  bool fixEnqueueTx(TxBody::Ref b) { queue.push(ZuMv(b)); return true; }

  TxBody::Ref fixDequeueTx() {
    if (holdQueue) return {};
    auto node = queue.shift(); return node ? ZuMv(node->data()) : TxBody::Ref{};
  }

  void fixSelectedTx(TxBody::Ref, TxContext, unsigned result) {
    if (result != TxSelect::Accepted) state()->fail();
  }

  void enqueue(TxBody::Ref b) {
    this->app()->txRun([this, b = ZuMv(b)]() mutable {
      queue.push(ZuMv(b)); this->queued();
    });
  }

  void fixCompleteTx(TxBody::Ref body, TxContext c, bool) {
    if (!this->app()->txInvoked()) state()->fail();
    if (Client && state()->failover && state()->phase && body->msgType() == "D" && !c.replay)
      ++state()->newCompleted;
    if (wireHeld_()) state()->wirePending.post();
  }

  void fixFaultTx(TxContext, Fmt::Error) { state()->fail(); }

  void fixEventRx(SessionEvent e, ZmRef<Msg>) {
    if (e.actions & SessionAction::Failure) {
      if (state()->phase || e.error != SessionError::Transport) state()->fail();
    }
    if (!(e.actions & SessionAction::Ready) || e.connection == readyConnection) return;
    readyConnection = e.connection;
    unsigned expected = state()->restart || (state()->failover
      && !Client) ? 1 : state()->phase + 1;
    if (e.epoch != 1 || e.connection != expected) state()->fail();
    ++state()->ready;
    if constexpr (Client) {
      if (state()->failover && state()->phase) {
	this->app()->txRun([this] { holdQueue = false; this->queued(); }); return;
      }
      Fmt::Error error;
      auto b = TxBody::encode<OrderMsg>(Order{state()->phase ? "new" : "old"},
	101 + state()->phase, error);
      if (error) state()->fail(); else enqueue(ZuMv(b));
    }
  }

  Fmt::Error fixReceiveRx(OrderMsg *, const Order &o, const RequestContext &context) {
    const auto &meta = context.context();
    auto version = state()->version;
    if (!this->app()->rxInvoked() || meta.session != this->session_().instance_() ||
	meta.epoch != 1 || meta.connection != this->session_().connection_()
	  || !meta.seqNo ||
	meta.version != context.frame().beginString || meta.replay != meta.possDup)
      state()->fail();
    if (version < Version::N && context.frame().beginString != Version::Wire::v2s(version)) state()->fail();
    if (!state()->phase || (o.id != "old" && o.id != "new")) state()->fail();
    if constexpr (!Client) {
      if (o.id == "old") {
	if (!meta.possDup || meta.seqNo != state()->oldSeq ||
	    context.origSendingTime() != state()->oldTime) state()->fail();
	++state()->oldReceived;
	state()->oldDelivered.post();
      } else ++state()->newReceived;
      Fmt::Error error; auto b = TxBody::encode<OrderMsg>(o, o.id == "old" ? 201 : 202, error);
      if (error) return error; enqueue(ZuMv(b));
    } else if (o.id == "new") state()->done.post();
    return {};
  }

  void fixStopRx(uint64_t, uint64_t ticket) {
    store->rx = this->rxProgress_();
    if (ticket) state()->fail();
  }

  void fixStopTx(uint64_t) {
    holdWire = false; pendingWire.clean();
    if (pendingLoad) {
      state()->fail(); this->loaded(pendingLoad, false); pendingLoad = 0;
    }
    bool retain = Client && state()->failover && !state()->newPublished.load_();
    store->tx = this->txProgress_();
    if (queue.count_() && (!retain || queue.count_() != 1)) state()->fail();
  }

  void fixStoppedRx(bool) { state()->stopped(Client); }

  void restore() {
    this->app()->rxRun([this] {
      if (!this->restoreRx_(1, store->rx)) state()->fail();
      this->app()->txRun([this] {
	if (!this->restoreTx_(1, store->tx)) state()->fail();
	++state()->restoredCount; state()->restored.post();
      });
    });
  }
};

struct CliHub : public Zfix::Client<CliHub, Dictionary, Network> {
  struct Link;
  TestState	*state;

  explicit CliHub(TestState *s) : state{s} { }

  unsigned reconnFreq() const { return 0; }
};

struct CliHub::Link : public ZmNHeap<Link>, public Peer<Link, Zfix::CliLink<CliHub, Link,
  Dictionary, Network>, true> {
friend inline ZuStringT<"Zfix.recovery_io_test.Link"> ZmNHeapID(Link *);
  using ZmNHeap<Link>::operator new;
  using ZmNHeap<Link>::operator delete;
  using Peer::Peer;
};

struct SrvHub : public Zfix::Server<SrvHub, Dictionary, Network> {
  using Base = Zfix::Server<SrvHub, Dictionary, Network>;
  struct Link;
  TestState	*state;
  ZmRef<Link>	session; // network Rx routing; immutable while an accepted link is live
  unsigned	listenPort;

  explicit SrvHub(TestState *s) : state{s}, listenPort{s->port} { }


  ZiConnection *accepted(const ZiCxnInfo &);

  ZiIP localIP() const { return ZiIP{"127.0.0.1"}; }
  unsigned localPort() const { return listenPort; }
  unsigned nAccepts() const { return 1; }

  void listening(const ZiListenInfo &i) { state->port = i.port; state->listening.post(); }

  void listenFailed(bool) { state->fail(); state->listening.post(); }
};

struct SrvHub::Link : public ZmNHeap<Link>, public Peer<Link, Zfix::SrvLink<SrvHub, Link,
  Dictionary, Network>, false> {
friend inline ZuStringT<"Zfix.recovery_io_test.Link"> ZmNHeapID(Link *);
  using ZmNHeap<Link>::operator new;
  using ZmNHeap<Link>::operator delete;
  using Peer::Peer;
};

ZiConnection *SrvHub::accepted(
  const ZiCxnInfo &ci) { return session ? new Link::Cxn{session.ptr(), ci} : nullptr; }

static void run(TestState &state, unsigned begin, unsigned end) {
  ZuTestScope(run);
  SrvHub server{&state}; CliHub client{&state};
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(5)
      .thread(1, [](auto &t) { t.isolated(1); })
      .thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); })
      .thread(4, [](auto &t) { t.isolated(1); })
      .thread(5, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  bool started = mx.start(); ZuCheck(started); if (!started) return;
  typename Network::ServerParams sp{&mx, "3", "4"};
  typename Network::ClientParams cp{&mx, "3", "4"};
#ifdef ZFIX_TEST_TLS
  const char *cert = getenv("ZFIX_TEST_CERT"), *key = getenv("ZFIX_TEST_KEY");
  if (!cert || !key) { ZuCheck(cert && key); mx.stop(); return; }
  sp.certPath(cert).keyPath(key); cp.caPath(cert);
#endif
  bool si = server.init(ZuMv(sp)), ci = client.init(ZuMv(cp)); ZuCheck(si && ci);
  bool ss = si && server.start(), cs = ci && client.start(); ZuCheck(ss && cs);
  ZmRef<CliHub::Link> link = new CliHub::Link{&client};
  server.session = new SrvHub::Link{&server};
  if (begin) {
    link->restore(); server.session->restore();
    ZuCheck(state.restored.timedwait(Zm::now(5)) == 0);
    ZuCheck(state.restored.timedwait(Zm::now(5)) == 0);
  }
  server.listen(); ZuCheck(state.listening.timedwait(Zm::now(5)) == 0);
  for (unsigned phase = begin; phase < end; ++phase) {
    state.phase = phase;
#ifdef ZFIX_TEST_TLS
    link->connect("localhost", state.port, ZiIP{"127.0.0.1"});
#else
    link->connect("127.0.0.1", state.port);
#endif
    if (!phase) {
      ZuCheck(state.dropped.timedwait(Zm::now(5)) == 0); link->disconnect();
    } else {
      ZuCheck(state.done.timedwait(Zm::now(5)) == 0); link->logout();
    }
    ZuCheck(state.drained.timedwait(Zm::now(5)) == 0);
    if (!phase) {
      ZuCheck(!state.errors.load_() && !state.oldReceived.load_());
      for (const auto &h : state.client->history) {
	if (h.body->msgType() == "D") { state.oldSeq = h.seqNo; state.oldTime = h.originalTime; }
      }
      ZuCheck(state.oldSeq && *state.oldTime && state.client->tx.sent > state.oldSeq);
      ZuCheck(state.server->rx.processed <= state.oldSeq);
    }
  }
  bool cstop = client.stop(), sstop = server.stop(); ZuCheck(cstop && sstop);
  client.final(); server.final(); mx.stop(); link = nullptr; server.session = nullptr;
}
static void recovery(unsigned mode, bool restart) {
  ZuTestScope(recovery);
  TestState state; state.mode = mode; state.restart = restart;
  if (restart && mode == Transport::FIX4) state.version = Version::V4_2;
  if (!restart) ZuTestCall(run, state, 0, 2);
  else {
    ZuTestCall(run, state, 0, 1);
    // Destroy the original stores and all payload references after writing.
    // Fresh links recover only from files read into new application stores.
    auto cliPath = ZiTestResidue::file(ZiTestResidue::uniqueName("client"));
    auto srvPath = ZiTestResidue::file(ZiTestResidue::uniqueName("server"));
    bool saved = save(cliPath, *state.client) && save(srvPath, *state.server);
    ZuCheck(saved); if (!saved) return;
    state.client = nullptr; state.server = nullptr;
    state.client = load(cliPath); state.server = load(srvPath);
    ZuCheck(state.client && state.server); if (!state.client || !state.server) return;
    // The selected links retain their sequence epoch across a coordinated wire change.
    if (mode == Transport::FIX4) state.version = Version::V4_3;
    // A fresh listening endpoint/port, hubs, mux and both session owners.
    state.port = 0;
    ZuTestCall(run, state, 1, 2);
  }
  ZuCheck(!state.errors.load_() && state.closed.load_() == 4);
  ZuCheck(state.ready.load_() == 4 && state.logons.load_() == 4);
  ZuCheck(state.oldReceived.load_() == 1 && state.newReceived.load_() == 1);
  ZuCheck(state.replayed.load_() && (!restart || state.retrieved.load_()));
  ZuCheck(state.restoredCount.load_() == (restart ? 2 : 0));
  if (state.retrieved.load_()) {
    ZuCheck(state.reloadRefused.load_());
    ZuCheck(state.responses == state.reloadRefused.load_() + state.reloadAdmitted.load_());
  }
}
static bool wait(ZmSemaphore &s) { return s.timedwait(Zm::now(10)) == 0; }
static void connect(CliHub::Link *link, unsigned port) {
#ifdef ZFIX_TEST_TLS
  link->connect("localhost", port, ZiIP{"127.0.0.1"});
#else
  link->connect("127.0.0.1", port);
#endif
}
static void failover(unsigned mode, bool cold) {
  ZuTestScope(failover);
  TestState state; state.mode = mode; state.failover = true;
  SrvHub primary{&state}, backup{&state}; CliHub client{&state};
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(5)
      .thread(1, [](auto &t) { t.isolated(1); }).thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); }).thread(4, [](auto &t) { t.isolated(1); })
      .thread(5, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  bool started = mx.start(); ZuCheck(started); if (!started) return;
#ifdef ZFIX_TEST_TLS
  if (!getenv("ZFIX_TEST_CERT") || !getenv("ZFIX_TEST_KEY")) {
    ZuCheck(false); mx.stop(); return;
  }
#endif
  auto serverParams = [&mx] {
    typename Network::ServerParams p{&mx, "3", "4"};
#ifdef ZFIX_TEST_TLS
    p.certPath(getenv("ZFIX_TEST_CERT")).keyPath(getenv("ZFIX_TEST_KEY"));
#endif
    return p;
  };
  typename Network::ClientParams cp{&mx, "3", "4"};
#ifdef ZFIX_TEST_TLS
  cp.caPath(getenv("ZFIX_TEST_CERT"));
#endif
  bool pi = primary.init(serverParams()), bi = backup.init(serverParams()),
    ci = client.init(ZuMv(cp));
  ZuCheck(pi && bi && ci);
  bool ps = pi && primary.start(), bs = bi && backup.start(), cs = ci && client.start();
  auto final = [&primary, &backup, &client, &mx, &ps, &bs, &cs] {
    bool ok = true;
    if (cs) { ok &= client.stop(); cs = false; }
    if (ps) { ok &= primary.stop(); ps = false; }
    if (bs) { ok &= backup.stop(); bs = false; }
    client.final(); primary.final(); backup.final(); mx.stop(); return ok;
  };
  ZuCheck(ps && bs && cs); if (!ps || !bs || !cs) { final(); return; }
  ZmRef<CliHub::Link> link = new CliHub::Link{&client};
  primary.session = new SrvHub::Link{&primary};
  auto snapshot = [&client, &link](RxProgress &rx, TxProgress &tx, uint64_t &connection) {
    ZmBlock<>{}([&client, &link, &rx, &tx, &connection](auto wake) {
      client.rxRun([&client, &link, &rx, &tx, &connection, wake = ZuMv(wake)]() mutable {
	rx = link->rxProgress_(); connection = link->session_().connection_();
	client.txRun([&link, &tx, wake = ZuMv(wake)]() mutable { tx = link->txProgress_();
	  wake(); });
      });
    });
  };
  primary.listen(); ZuCheck(wait(state.listening)); unsigned primaryPort = state.port;
  backup.listen(); ZuCheck(wait(state.listening)); unsigned backupPort = state.port;
  ZuCheck(primaryPort && backupPort && primaryPort != backupPort);
  connect(link.ptr(), primaryPort); ZuCheck(wait(state.dropped));
  Fmt::Error error;
  auto queued = TxBody::encode<OrderMsg>(Order{"new"}, 102, error);
  ZuCheck(queued && !error); link->enqueue(ZuMv(queued));
  bool retained = false;
  ZmBlock<>{}([&client, &link, &state, &retained](auto wake) {
    client.txRun([&link, &state, &retained, wake = ZuMv(wake)]() mutable {
      retained = link->queue.count_() == 1 && link->queue.headNode()->data()->id() == 102 &&
	!state.newLoad && link->txProgress_().loaded == state.client->lastLoad + 1;
      wake();
    });
  });
  ZuCheck(retained);
  link->disconnect(); ZuCheck(wait(state.cliStopped) && wait(state.srvStopped));
  ZuCheck(!state.errors.load_());
  for (const auto &h : state.client->history)
    if (h.body->msgType() == "D") { state.oldSeq = h.seqNo; state.oldTime = h.originalTime; }
  ZuCheck(state.oldSeq && *state.oldTime && state.oldLoad && state.oldToken && state.oldPart);
  ZuCheck(!state.oldReceived.load_() && state.client->tx.sent > state.oldSeq);
  // The backup is already listening when the primary becomes unavailable.
  bool primaryStopped = primary.stop(); ZuCheck(primaryStopped);
  if (primaryStopped) ps = false;
  auto refused = ::socket(AF_INET, SOCK_STREAM, 0);
  int reuse = 1;
  if (!Zi::nullSocket(refused))
    ::setsockopt(refused, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&reuse),
      sizeof(reuse));
  sockaddr_in address{}; address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = htons(primaryPort);
  bool bound = !Zi::nullSocket(refused) &&
    !::bind(refused, reinterpret_cast<sockaddr *>(&address), sizeof(address));
  ZuCheck(bound);
  if (!bound) { if (!Zi::nullSocket(refused)) Zi::closeSocket(refused); final(); return; }
  RxProgress rxBefore, rxAfter; TxProgress txBefore, txAfter; uint64_t before, after;
  snapshot(rxBefore, txBefore, before);
  state.phase = 1;
  connect(link.ptr(), primaryPort);
  ZuCheck(wait(state.setupFailed) && wait(state.cliStopped));
  snapshot(rxAfter, txAfter, after);
  ZuCheck(before == 1 && after == before && state.setupFailures.load_() == 1);
  ZuCheck(rxBefore.received == rxAfter.received && rxBefore.processed == rxAfter.processed &&
    rxBefore.gapBegin == rxAfter.gapBegin && rxBefore.gapEnd == rxAfter.gapEnd);
  ZuCheck(txBefore.loaded == txAfter.loaded && txBefore.sent == txAfter.sent &&
    txBefore.acknowledged == txAfter.acknowledged &&
    txBefore.replayBegin == txAfter.replayBegin && txBefore.replayEnd == txAfter.replayEnd);
  Zi::closeSocket(refused);
  if (cold) {
    auto path = ZiTestResidue::file(ZiTestResidue::uniqueName("failover-server"));
    auto previous = state.server;
    bool saved = save(path, *previous); ZuCheck(saved);
    if (!saved) { final(); return; }
    state.server = load(path); ZuCheck(state.server && state.server != previous);
    if (!state.server) { final(); return; }
    ZuCheck(state.server->lastLoad == previous->lastLoad
      && state.server->history.length() == previous->history.length());
    for (unsigned i = 0, n = state.server->history.length(); i < n; ++i) {
      const auto &restored = state.server->history[i], &old = previous->history[i];
      ZuCheck(restored.body != old.body && restored.body->body() == old.body->body());
      ZuCheck(restored.epoch == old.epoch && restored.seqNo == old.seqNo
	&& restored.originalTime == old.originalTime);
    }
  }
  ZmRef<SrvHub::Link> standby = new SrvHub::Link{&backup};
  // Restore both application owners before exposing the session through the
  // already-live listener's network-Rx routing.
  standby->restore(); ZuCheck(wait(state.restored));
  if (state.errors.load_()) { final(); return; }
  ZmBlock<>{}([&mx, &backup, &standby](auto wake) {
    mx.rxRun([&backup, standby, wake = ZuMv(wake)]() mutable { backup.session = standby; wake();
      });
  });
  connect(link.ptr(), backupPort); ZuCheck(wait(state.loadPending));
  ZuCheck(state.newLoad > state.oldLoad && !state.newPublished.load_());
  link->loaded(state.oldLoad, false); // Late first-connection association reply.
  TxStats stale; TxProgress pending;
  ZmBlock<>{}([&client, &link, &stale, &pending](auto wake) {
    client.txRun([&link, &stale, &pending, wake = ZuMv(wake)]() mutable {
      stale = link->txStats_(); pending = link->txProgress_(); wake();
    });
  });
  ZuCheck(!stale.loadRefused && pending.loaded == state.pendingSeq
    && !state.newPublished.load_());
  // Publishing new work holds all subsequent Wire output, including replay.
  // Confirm old replay remotely before releasing the new association.
  ZuCheck(wait(state.oldDelivered));
  auto release = ZuMv(state.releaseLoad); ZuCheck(release); if (release) release();
  ZuCheck(wait(state.wirePending));
  bool held = ZmBlock<bool>{}([&client, &link](auto wake) {
    client.txRun([&link, wake = ZuMv(wake)]() mutable { wake(link->wireHeld_()); });
  });
  ZuCheck(held && state.newToken > state.oldToken && state.newPublished.load_() == 1
    && !state.newCompleted.load_());
  ZmBlock<>{}([&client, &link, &state, &stale](auto wake) {
    client.txRun([&client, &link, &state, &stale, wake = ZuMv(wake)]() mutable {
      ZmRef<WireBuf> old = new CliHub::Link::TxBufAlloc{link.ptr()};
      old->token = state.oldToken; old->part = state.oldPart;
      link->transportSent(old.ptr(), false); // Current wire head is held and live.
      client.txRun([&link, &stale, wake = ZuMv(wake)]() mutable { stale = link->txStats_();
	wake(); });
    });
  });
  ZuCheck(!stale.wire.failed && !stale.loadRefused && !state.newCompleted.load_());
  ZuCheck(state.oldReceived.load_() == 1 && !state.newReceived.load_());
  link->releaseWire(); ZuCheck(wait(state.done)); link->logout();
  ZuCheck(wait(state.cliStopped) && wait(state.srvStopped));
  ZuCheck(!state.errors.load_() && state.closed.load_() == 5 && state.ready.load_() == 4);
  ZuCheck(state.logons.load_() == 4 && state.restoredCount.load_() == 1
    && state.replayed.load_());
  ZuCheck(state.oldReceived.load_() == 1 && state.newReceived.load_() == 1
    && state.newCompleted.load_() == 1);
  ZuCheck(final()); link = nullptr; primary.session = backup.session = nullptr; standby = nullptr;
}
#include "recovery_swap.hh"
#include "managed_partial_test.hh"

int main(int argc, char **argv) {
  parse(argc, argv);
#ifdef ZFIX_TEST_TLS
  ZiTestResidue::init("recovery_tls_test");
#else
  ZiTestResidue::init("recovery_tcp_test");
#endif
  ZuTestMain();
  ZuTestCall(recovery, Transport::FIX4, false);
  ZuTestCall(recovery, Transport::FIX44A, false);
  ZuTestCall(recovery, Transport::FIXT11, false);
  ZuTestCall(recovery, Transport::FIX4, true);
  ZuTestCall(recovery, Transport::FIX44A, true);
  ZuTestCall(recovery, Transport::FIXT11, true);
  ZuTestCall(failover, Transport::FIX4, false);
  ZuTestCall(failover, Transport::FIX44A, false);
  ZuTestCall(failover, Transport::FIXT11, false);
  ZuTestCall(failover, Transport::FIX4, true);
  ZuTestCall(failover, Transport::FIX44A, true);
  ZuTestCall(failover, Transport::FIXT11, true);
  ZuTestCall(replayReplacement, Transport::FIX4, false);
  ZuTestCall(replayReplacement, Transport::FIX44A, false);
  ZuTestCall(replayReplacement, Transport::FIXT11, false);
  ZuTestCall(replayReplacement, Transport::FIX4, true);
  ZuTestCall(replayReplacement, Transport::FIX44A, true);
  ZuTestCall(replayReplacement, Transport::FIXT11, true);
#if defined(__linux__) && !defined(ZFIX_TEST_TLS)
  ZuTestCall(managedPartial);
#endif
  return 0;
}
