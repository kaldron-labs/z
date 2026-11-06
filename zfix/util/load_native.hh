//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZfixLoadNative_HH
#define ZfixLoadNative_HH

#include <zlib/ZuPtr.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmPlatform.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZfixClient.hh>
#include <zlib/ZfixServer.hh>
#include "load_run.hh"
#include "load_cache.hh"
#include "load_fix.hh"

namespace ZfixLoad {

using HistKey = ZuTuple<uint64_t, uint64_t>;
using NativeHistory = Cache<HistKey, Zfix::TxHistory>;
// Shared core TxBody cannot carry this application FIFO's links.
using NativeQueue = ZmList<Zfix::TxBody::Ref,
  ZmListHeapID<"Zfix.Load.Queue">>;
struct NativeStore {
  Zfix::RxProgress	rx;
  Zfix::TxProgress	tx;
  uint64_t		epoch = 0;
  NativeHistory		history;

  NativeStore(const Options &o, unsigned = 0) : history{o.retain, o.cacheBytes} { }
};

struct NativeCheckpoint {
  Zfix::RxProgress	rx;
  Zfix::TxProgress	tx;
  uint64_t		epoch = 0;
  Rows<Zfix::TxHistory>	history;
  uint64_t		businessKey = 0;
};

struct NativeState {
  const Options		&options;
  Ports			ports;
  Rows<Result>		results;
  Rows<Measurements>	measurements;
  Rows<ZmFn<void()>>	starters;
  ZmAtomic<unsigned>	errors{0};
  // Server network Rx counts a logical owner's first physical acceptance.
  // An unbound cold preparation has no logical-stop obligation yet.
  unsigned		created = 0;
  unsigned		ready = 0, finished = 0, closed = 0; // AppRx-owned
  const unsigned	expected;
  NativeCheckpoint	checkpoint;
  ZmSemaphore		listening, readySem, done, stopped;

  explicit NativeState(const Options &o, Ports p, bool server = false) :
      options{o}, ports{ZuMv(p)}, expected{o.sessions * (server ? o.connections : 1)} {
    results.length(o.sessions); measurements.length(o.sessions); starters.length(o.sessions);
    for (unsigned i = 0; i < o.sessions; ++i) results[i].session = i + 1;
  }

  void fail(const char *file = __builtin_FILE(), unsigned line = __builtin_LINE()) {
    ZiLOG(Error, "zfix", ([file, line](auto &s) {
      s << "zfix failure " << file << ':' << line;
    }));
    ++errors; readySem.post(); done.post();
  }

  void readyRx_(unsigned index, ZmFn<void()> start = {}) {
    if (errors.load_()) return;
    starters[index] = ZuMv(start);
    if (++ready != options.sessions) return;
    readySem.post();
    for (auto &fn : starters) { auto start = ZuMv(fn); if (start) start(); }
  }

  void doneRx_() { if (++finished == options.sessions) done.post(); }

  void stoppedRx_(bool client) {
    if (client) ++closed;
    stopped.post(); if (closed == expected) done.post();
  }
};

static Zfix::HubLimits nativeLimits(const Options &o, unsigned sessions = 1) {
  Zfix::HubLimits limits;
  limits.link.tx.queuedOutputBytes = o.queueBytes; limits.link.wire.queuedBytes = o.queueBytes;
  limits.link.postedSubmitBytes = o.queueBytes; limits.submit.bytes = o.queueBytes * sessions;
  return limits;
}
static Zfix::SessionConfig nativeConfig(bool client, const Options &o, unsigned index) {
  Zfix::SessionConfig c; c.initiator = client;
  static_assert(Mode::FIX4 == 0 &&
    Zfix::Transport::FIX44A == Mode::FIX44A + Zfix::Transport::FIX4 &&
    Zfix::Transport::FIXT11 == Mode::FIXT11 + Zfix::Transport::FIX4 &&
    Zfix::Transport::N == Mode::N + Zfix::Transport::FIX4);
  static_assert(Version::V4_0 == Zfix::Version::V4_0 &&
    Version::V4_1 == Zfix::Version::V4_1 && Version::V4_2 == Zfix::Version::V4_2 &&
    Version::V4_3 == Zfix::Version::V4_3 && Version::V4_4 == Zfix::Version::V4_4 &&
    Version::VT1_1 == Zfix::Version::VT1_1 && Version::N == Zfix::Version::N);
  c.mode = o.mode + Zfix::Transport::FIX4;
  c.identity.version = o.version;
  c.appVerID = Zfix::AppVersion::V4_4; c.heartBtInt = o.heartbeat;
  Text sender; sender << o.sender << '-' << index + 1;
  c.identity.local = client ? ZuCSpan{sender} : ZuCSpan{o.target};
  c.identity.peer = client ? ZuCSpan{o.target} : ZuCSpan{sender};
  c.logonTimeout = ZuTime{o.deadline}; return c;
}
struct NoRun { NoRun(const Options &, unsigned) { } };

template <typename Net, bool Client> struct NativeHub;
template <typename Net, bool Client> class NativeLink;
template <typename Net> struct NativeHub<Net, true> : Zfix::Client<NativeHub<Net, true>,
  Dictionary, Net> {
  using Link = NativeLink<Net, true>;
  NativeState	*state;

  explicit NativeHub(NativeState *s) : Zfix::Client<NativeHub, Dictionary,
    Net>{nativeLimits(s->options, s->options.sessions)}, state{s} { }

  bool init(typename Net::ClientParams params) {
    ZmRef<typename Link::RxAllocator> rx;
    ZmRef<typename Link::TxAllocator> tx;
    if (!params.rxAllocator()) {
      rx = new typename Link::RxAllocator{};
      params.rxAllocator(rx.ptr());
    }
    if (!params.txAllocator()) {
      tx = new typename Link::TxAllocator{};
      params.txAllocator(tx.ptr());
    }
    return Zfix::Client<NativeHub, Dictionary, Net>::init(ZuMv(params));
  }
};

template <typename Net> struct NativeHub<Net, false> final :
    ZmNHeap<NativeHub<Net, false>>, Zfix::Server<NativeHub<Net, false>, Dictionary, Net> {
  using Link = NativeLink<Net, false>;
friend inline ZuStringT<"Zfix.Load.Hub"> ZmNHeapID(NativeHub *);
  using ZmNHeap<NativeHub>::operator new;
  using ZmNHeap<NativeHub>::operator delete;
  NativeState		*state;
  const unsigned	index;
  NativeStore		store;
  Broker		broker;
  ZmRef<Link>		connection; // network Rx-owned persistent logical link
  unsigned		acceptedCount = 0; // network Rx-owned
  uint64_t		nextEpoch = 1; // network Rx-owned, handed off after old owner drain
  bool			initialized = false, active = false; // main owner

  NativeHub(NativeState *s, unsigned i) :
    Zfix::Server<NativeHub, Dictionary, Net>{nativeLimits(s->options)}, state{s}, index{i},
    store{s->options}, broker{s->options, i} { }

  ZiIP localIP() const { return ZiIP{state->options.bind}; }
  unsigned localPort() const { return state->ports[index]; }
  unsigned nAccepts() const { return 1; }

  ZiConnection *accepted(const ZiCxnInfo &info);

  void listening(const ZiListenInfo &info) { state->ports[index] = info.port;
    state->listening.post(); }

  void listenFailed(bool) { state->fail(); state->listening.post(); }

  void close() { this->mx()->rxRun([this] { if (connection) connection->disconnect(); }); }

  void retired_(Link *link, uint64_t epoch, unsigned round,
      uint64_t generation) {
    this->mx()->rxRun([this, link = ZmRef<Link>{link}, epoch, round, generation] {
      if (connection != link) { retiredDone_(link, generation, false); return; }
      // Explicit reset/recovery profiles reconstruct a cold owner after its
      // complete drain. Ordinary reconnect retains the same sequencer/link.
      if (!(state->options.resetLogon || state->options.recoverRx) ||
	  acceptedCount == state->options.connections || state->errors.load_()) {
	retiredDone_(link, generation, true); return;
      }
      if (acceptedCount != round) { retiredDone_(link, generation, false); return; }
      nextEpoch = state->options.resetLogon ? epoch + 1 : epoch;
      ZmRef<Link> next = new Link{this, index, round + 1};
      auto next_ = next.ptr();
      next_->prepareServer([
	this, link = ZuMv(link), next = ZuMv(next), round, generation
      ](bool ok) mutable {
	this->mx()->rxRun([
	  this, link = ZuMv(link), next = ZuMv(next), round, generation, ok
	]() mutable {
	  // A newly accepted successor owns the current link even if this cold
	  // preparation was already queued. It cannot be replaced by its result.
	  if (connection != link || acceptedCount != round) {
	    retiredDone_(link, generation, false); return;
	  }
	  if (!ok) state->fail();
	  bool ready = ok && !state->errors.load_();
	  if (ready) connection = ZuMv(next);
	  retiredDone_(link, generation, ready);
	});
      });
    });
  }

  void retiredDone_(Link *link, uint64_t generation, bool ready) {
    if (ready && acceptedCount < state->options.connections && !state->errors.load_()) {
      ReconnectReady event; event.connection = acceptedCount + 1; event.ports = state->ports;
      output(event);
    }
    // Same-owner FIFO orders this return before a newer connection's final
    // retirement. Keep A alive without gating the successor's activation/stop.
    this->rxRun([link = ZmRef<Link>{link}, generation] {
      auto hub = link->app();
      if (link->session_().connection_() == generation) hub->state->stoppedRx_(false);
      else hub->state->stopped.post(); // A cannot finish a newer connection.
    });
  }

  bool init(typename Net::ServerParams params) {
    ZmRef<typename Link::RxAllocator> rx;
    ZmRef<typename Link::TxAllocator> tx;
    if (!params.rxAllocator()) {
      rx = new typename Link::RxAllocator{};
      params.rxAllocator(rx.ptr());
    }
    if (!params.txAllocator()) {
      tx = new typename Link::TxAllocator{};
      params.txAllocator(tx.ptr());
    }
    return Zfix::Server<NativeHub, Dictionary, Net>::init(ZuMv(params));
  }
};

template <typename Net, bool Client>
class NativeLink : public ZmNHeap<NativeLink<Net, Client>>, public ZuIf<Client,
    Zfix::CliLink<NativeHub<Net, Client>, NativeLink<Net, Client>, Dictionary, Net>,
    Zfix::SrvLink<NativeHub<Net, Client>, NativeLink<Net, Client>, Dictionary, Net>> {
friend inline ZuStringT<"Zfix.Load.Link"> ZmNHeapID(NativeLink *);
  using Base = ZuIf<Client,
    Zfix::CliLink<NativeHub<Net, Client>, NativeLink<Net, Client>, Dictionary, Net>,
    Zfix::SrvLink<NativeHub<Net, Client>, NativeLink<Net, Client>, Dictionary, Net>>;
public:
  using ZmNHeap<NativeLink>::operator new;
  using ZmNHeap<NativeLink>::operator delete;
  using Base::app;

  NativeLink(NativeHub<Net, Client> *hub, unsigned index) :
    NativeLink{hub, index, round_(hub)} { }

  NativeLink(NativeHub<Net, Client> *hub, unsigned index, unsigned round) :
    Base{hub, nativeConfig(Client, hub->state->options, index), epoch_(hub)},
    m_index{index}, m_round{round}, m_run{hub->state->options, index},
    m_store{hub->state->options, index} { }

  unsigned admissionRound() const { return m_round; }

  // Main waits on this cold-owner handoff before submitting a client connect.
  bool prepare() {
    return ZmBlock<bool>{}([this](auto wake) {
      app()->rxRun([this, wake = ZuMv(wake)]() mutable {
	auto &saved = state()->checkpoint;
	if ((saved.epoch && !this->restoreRx_(saved.epoch, saved.rx)) ||
	    (options().resetLogon && !resetOpen_())) { wake(false); return; }
	app()->txRun([this, wake = ZuMv(wake)]() mutable {
	  bool restored = true;
	  auto &saved = state()->checkpoint;
	  if (saved.epoch) restored = this->restoreTx_(saved.epoch, saved.tx) && restored;
	  for (const auto &h : saved.history)
	    restored = store_().history.put({h.epoch, h.seqNo}, h, h.body->memory()) && restored;
	  saved.history.length(0);
	  wake(restored);
	});
      });
    });
  }

  // Retired server owners prepare a successor before announcing reconnect.
  // The caller retains both owners until the final publication; this path has
  // no socket, active timers, or asynchronous application preparation hooks.
  void prepareServer(ZmFn<void(bool)> done) {
    app()->rxRun([this, done = ZuMv(done)]() mutable {
      bool reset = options().resetLogon && m_round > 1;
      auto epoch = store_().epoch;
      bool ok = reset ? resetOpen_() : !epoch || this->restoreRx_(epoch, store_().rx);
      if (!ok) { done(false); return; }
      app()->txRun([this, done = ZuMv(done), reset, epoch]() mutable {
	bool restored = reset || !epoch || this->restoreTx_(epoch, store_().tx);
	app()->rxRun([done = ZuMv(done), restored]() mutable { done(restored); });
      });
    });
  }

  void connected(typename Net::Connected event) {
    m_ready = false; Base::connected(event);
  }

  unsigned fixAssociateTx(Zfix::TxBody::Ref body, Zfix::TxContext, uint64_t) {
    // The operational assignment carries the association until publication;
    // Wire owns local completion. This app adds no external durability barrier.
    if (body->administrative()) return Zfix::TxLoad::Accepted;
    return body->id() && store_().history.fits(body->memory()) ?
      Zfix::TxLoad::Accepted : Zfix::TxLoad::Refused;
  }

  bool fixReleaseTx(Zfix::TxBody::Ref, Zfix::TxContext, uint64_t) { return true; }

  void fixPublishedTx(Zfix::TxBody::Ref body, Zfix::TxContext c, ZuDateTime time) {
    // This workload retains business output and Rejects, and suppresses stale
    // operational controls. That choice belongs to this application store.
    if (c.replay || (body->administrative() && body->msgType() != "3")) return;
    // Retention starts with published history, not preloaded candidates. The
    // hook runs before transport flush, so original time/ID/sequence are ready
    // before a peer can respond. Core holds only unpublished operational work.
    bool saved = store_().history.put({c.epoch, c.seqNo},
      Zfix::TxHistory{body, time, c.epoch, c.seqNo}, body->memory());
    if (!saved && !body->administrative()) state()->fail();
  }

  // The cache's count/byte policy retains these records beyond archive progress
  // so reconnect can recover them. Archive imposes no core release handshake.
  Zfix::TxReload fixRetrieveTx(Zfix::TxContext context, uint64_t, uint64_t) {
    auto record = store_().history.get({context.epoch, context.seqNo});
    if (record) return {*record};
    ++result().historyMiss; return {};
  }

  void fixSubmittedTx(Zfix::TxBody::Ref body, unsigned selected) {
    if (selected == Zfix::TxSelect::Accepted || selected == Zfix::TxSelect::Pending) return;
    if (selected != Zfix::TxSelect::Busy && selected != Zfix::TxSelect::Held) { state()->fail();
      return; }
    auto bytes = body->memory() + ZmHeapAllocSize<sizeof(typename NativeQueue::Node)>{};
    if (bytes > options().queueBytes || m_queueBytes > options().queueBytes - bytes) {
      state()->fail(); return;
    }
    m_queue.push(ZuMv(body)); m_queueBytes += bytes; this->queued();
  }

  Zfix::TxBody::Ref fixDequeueTx() {
    auto node = m_queue.shift(); if (!node) return {};
    auto body = ZuMv(node->data());
    m_queueBytes -= body->memory() + ZmHeapAllocSize<sizeof(NativeQueue::Node)>{};
    return body;
  }

  void fixSelectedTx(Zfix::TxBody::Ref, Zfix::TxContext, unsigned selected) {
    if (selected != Zfix::TxSelect::Accepted && !m_stopping) state()->fail();
  }

  Zfix::TxBody::Ref fixLogonTx(Zfix::TxBody::Ref body) {
    // A prepared reset owner has not touched the shared store. Clear obsolete
    // history only after this owner is actually bound, before its first Logon.
    if constexpr (!Client)
      if (options().resetLogon && m_round > 1 && this->txProgress_().loaded == 1)
	store_().history.clear();
    m_stopping = false; return body;
  }

  void fixRejectedTx(Zfix::PeerReject reject) {
    ZiLOG(Error, "zfix", ([seq = reject.message.refSeqNo, tag = reject.message.refTagID,
      reason = reject.message.reason](auto &s) {
      s << "session reject: refSeq=" << seq << " tag=" << tag << " reason=" << reason;
    }));
    state()->fail();
  }

  void fixEventRx(Zfix::SessionEvent event, ZmRef<Zfix::RxQueue::Node>) {
    if (event.actions & Zfix::SessionAction::Failure) {
      if (event.error == Zfix::SessionError::Timeout) ZiLOG(Error, "zfix", "FIX session timeout");
      else ZiLOG(Error, "zfix", ([error = event.error, tag = event.tag](auto &s) {
	s << "FIX session failure error=" << error << " tag=" << tag;
      }));
      state()->fail();
    }
    if (!(event.actions & Zfix::SessionAction::Ready)) return;
    if (m_ready) return;
    m_ready = true;
    ++result().connections;
    if constexpr (Client) state()->readyRx_(m_index, ZmFn<void()>{[this] { start_(); }});
    else state()->readyRx_(m_index);
  }

  Zfix::Fmt::Error fixReceiveRx(NewOrder *, const Order &o,
    const Zfix::RequestContext &context) {
    if constexpr (Client) return {Zfix::Fmt::ErrorCode::Unresolved, 35};
    else return serve_(o, context);
  }

  Zfix::Fmt::Error fixReceiveRx(CancelOrder *, const Cancel &o,
      const Zfix::RequestContext &context) {
    if constexpr (Client) return {Zfix::Fmt::ErrorCode::Unresolved, 35};
    else return serve_(o, context);
  }

  Zfix::Fmt::Error fixReceiveRx(ReplaceOrder *, const Replace &o,
      const Zfix::RequestContext &context) {
    if constexpr (Client) return {Zfix::Fmt::ErrorCode::Unresolved, 35};
    else return serve_(o, context);
  }

  template <typename Entry>
  Zfix::Fmt::Error fixReceiveRx(Entry *, const Report &r, const Zfix::RequestContext &) {
    if constexpr (!Client) return {Zfix::Fmt::ErrorCode::Unresolved, 35};
    else {
      if (!m_run.shape(r.text)) { state()->fail();
	return {Zfix::Fmt::ErrorCode::Value, 355}; }
      auto outcome = m_run.receive(r, monotonic(), [this]<typename O>(const O &o) {
	Zfix::Fmt::Error error;
	using E = ZuIf<ZuIsSame<O, Cancel>{}, CancelOrder, ReplaceOrder>;
	return this->template submit<E>(o, [&o] {
	  if constexpr (ZuIsSame<O, Cancel>{}) return o.key;
	  else return o.order.key;
	}(), error);
      });
      if (outcome == Outcome::Invalid) state()->fail();
      if (outcome == Outcome::Complete) pump_();
      return {};
    }
  }

  void fixStopRx(uint64_t, uint64_t) {
    m_ready = m_live = false; ++m_timerSerial; app()->mx()->del(&m_timer);
    auto &r = result();
    if constexpr (Client) m_run.finish(r, monotonic(), &state()->measurements[m_index]);
    else {
      r.progress = broker_().progress; r.cached = broker_().cached;
      r.replayRefused = broker_().replayRefused;
    }
    store_().rx = this->rxProgress_();
    store_().epoch = this->session_().epoch_();
    if constexpr (Client) if (options().stateOut.length()) {
      state()->checkpoint.rx = store_().rx; state()->checkpoint.epoch = store_().epoch;
    }
    r.nextTarget = store_().rx.processed;
    auto stats = this->rxStats_();
    if (m_round == 1) { r.rxFrames = stats.frames; r.rxBytes = stats.bytes; }
    else { r.rxFrames += stats.frames; r.rxBytes += stats.bytes; }
    state()->starters[m_index] = {};
  }

  void fixStopTx(uint64_t) {
    m_stopping = true; auto stats = this->txStats_(); auto &r = result();
    store_().tx = this->txProgress_();
    r.nextSender = store_().tx.loaded;
    if constexpr (Client) if (options().stateOut.length()) {
      auto &saved = state()->checkpoint; saved.tx = store_().tx; saved.history.length(0);
      store_().history.each([&saved](const HistKey &, const Zfix::TxHistory &h) {
	saved.history.push(h);
      });
    }
    if (m_round == 1) { r.txFrames = stats.wire.completed; r.txBytes = stats.wire.bytes; }
    else { r.txFrames += stats.wire.completed; r.txBytes += stats.wire.bytes; }
    r.retainedPeak = store_().history.peak(); m_queue.clean(); m_queueBytes = 0;
  }

  void fixStoppedRx(bool) {
    if constexpr (!Client) {
      // Model a lagging durable receive checkpoint after all old owner work
      // drains. Committed business knowledge and sender history remain intact.
      if (m_round == 1 && options().recoverRx) {
	auto &rx = store_().rx;
	if (options().recoverRx >= rx.processed) state()->fail();
	else {
	  rx.received = rx.processed = options().recoverRx;
	  rx.gapBegin = rx.gapEnd = 0;
	}
      }
      app()->retired_(this, this->session_().epoch_(), m_round,
	this->session_().connection_());
    } else state()->stoppedRx_(true);
  }
private:
  static unsigned round_(NativeHub<Net, Client> *hub) {
    if constexpr (Client) return 1;
    else return hub->acceptedCount;
  }

  static uint64_t epoch_(NativeHub<Net, Client> *hub) {
    if constexpr (Client) {
      if (hub->state->options.resetLogon) return 2;
      auto epoch = hub->state->checkpoint.epoch; return epoch ? epoch : 1;
    } else return hub->nextEpoch;
  }

  bool resetOpen_() {
    Zfix::ResetRequest request;
    request.op = Zfix::ResetOp::OnOpen; request.epoch = this->session_().epoch_();
    request.enabled = true; return this->reset_(request);
  }

  NativeStore &store_() {
    if constexpr (Client) return m_store;
    else return app()->store;
  }

  Broker &broker_() { return app()->broker; }
  NativeState *state() const { return app()->state; }
  const Options &options() const { return state()->options; }
  Result &result() { return state()->results[m_index]; }

  template <typename O> Zfix::Fmt::Error serve_(const O &o,
      const Zfix::RequestContext &context) {
    if (state()->errors.load_()) return {Zfix::Fmt::ErrorCode::Output};
    Zfix::Fmt::Error error;
    bool ok = broker_().receive(o, context.context().replay, [this, &error](const Report &r) {
      // Broker emits exactly these four one-byte report kinds.
      switch (r.kind[0]) {
	case '0': return submit<Ack>(r, r.exec, error);
	case 'F': return submit<Fill>(r, r.exec, error);
	case '4': return submit<Canceled>(r, r.exec, error);
	default: return submit<Replaced>(r, r.exec, error);
      }
    });
    return ok ? Zfix::Fmt::Error{} : error ? error :
      Zfix::Fmt::Error{Zfix::Fmt::ErrorCode::Value, 11};
  }

  template <typename Entry> bool submit(const typename Entry::Object &o, uint64_t id,
    Zfix::Fmt::Error &error) {
    auto body = mapping(o, [&o, id, &error]<typename Facet>(Facet) {
      using E = Zfix::MT<typename Entry::Object, Entry::MsgType, Facet,
	typename Entry::Discrim>;
      return Zfix::TxBody::encode<E>(o, id, error);
    });
    if (!body || error) return false;
    if (Base::submit(ZuMv(body))) return true;
    error = {Zfix::Fmt::ErrorCode::Output}; return false;
  }

  void start_() {
    if constexpr (Client) { if (!m_run.start(monotonic())) state()->fail(); else pump_(); }
  }

  void pump_() {
    if constexpr (Client) {
      if (!m_live || m_done || state()->errors.load_()) return;
      ++m_timerSerial; app()->mx()->del(&m_timer);
      m_run.pump(monotonic(), [this](const Order &o) {
	Zfix::Fmt::Error error; return submit<NewOrder>(o, o.key, error);
      });
      if (m_run.phase() == Phase::Failed) { state()->fail(); return; }
      if (m_run.phase() == Phase::Complete) { m_done = true; state()->doneRx_(); return; }
      auto now = monotonic(), at = m_run.nextDue(now); if (!at) return;
      auto delay = at > now ? at - now : 0;
      auto serial = m_timerSerial;
      app()->mx()->add(&m_timer, Zm::now() + ZuTime{ZuTime::Nano{delay}}, ZmScheduler::Update,
	[this, serial](auto &&arm) { return arm([this,
	  serial] { if (serial == m_timerSerial) pump_(); }); }, app()->rxThread());
    }
  }

  // Constructor-published identity/initial admission round. Ordinary reconnect
  // retains this owner; explicit cold profiles construct a later-round owner.
  const unsigned			m_index;
  const unsigned			m_round;

  // Application Rx owns workload progression and its startup/timer gates.
  ZuIf<Client, Run, NoRun>		m_run;
  bool					m_ready = false, m_live = true, m_done = false;
  ZmScheduler::Timer			m_timer;
  uint64_t				m_timerSerial = 0;

  // Tx owns history and pending output. Store's Rx snapshot is cold and
  // disjoint; restore/stop posts hand its DTOs across the ordered owner drain.
  alignas(Zm::CacheLineSize)
  ZuIf<Client, NativeStore, NoRun>	m_store;
  NativeQueue				m_queue;
  uint64_t				m_queueBytes = 0;
  bool					m_stopping = false;
};

// Physical closes include displaced predecessors. The ordinary managed stop
// callback remains the logical-owner drain barrier and alone can finish a run.
template <typename Net> class NativeCxn : public ZmNHeap<NativeCxn<Net>>,
  public NativeLink<Net, false>::Cxn {
friend inline ZuStringT<"Zfix.Load.Cxn"> ZmNHeapID(NativeCxn *);
  using Base = typename NativeLink<Net, false>::Cxn;
  using Hub = NativeHub<Net, false>;
public:
  using ZmNHeap<NativeCxn>::operator new;
  using ZmNHeap<NativeCxn>::operator delete;

  NativeCxn(Hub *hub, const ZiCxnInfo &info) : Base{hub->connection, info}, m_hub{hub} { }

  void disconnected(bool peer) override {
    // Zi may repeat this notification; consume the observer once. Enqueue its
    // count before native teardown can enqueue the final managed stop barrier.
    if (auto hub = ZuMvPtr(m_hub)) hub->rxRun([hub] { ++hub->state->closed; });
    Base::disconnected(peer);
  }
private:
  Hub	*m_hub; // native connection/owner drains precede hub destruction
};

template <typename Net> ZiConnection *NativeHub<Net, false>::accepted(const ZiCxnInfo &info) {
  if (acceptedCount == state->options.connections) return nullptr;
  ++acceptedCount;
  if (!connection) connection = new Link{this, index};
  if (connection->admissionRound() == acceptedCount) ++state->created;
  return new NativeCxn<Net>{this, info};
}
int native(bool server, int argc, char **argv);
} // ZfixLoad

#endif /* ZfixLoadNative_HH */
