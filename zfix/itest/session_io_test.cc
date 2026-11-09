//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>
#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmPlatform.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmScheduler.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZiMultiplex.hh>
#ifdef ZFIX_TEST_TLS
#include <zlib/Ztls.hh>
namespace Net = Ztls;
#else
#include <zlib/Ztcp.hh>
namespace Net = Ztcp;
#endif
#include <zlib/ZtlsHMAC.hh>
#include <zlib/ZfixClient.hh>
#include <zlib/ZfixRx.hh>
#include <zlib/ZfixSeq.hh>
#include <zlib/ZfixServer.hh>

using namespace ZuTestUtil;
using namespace Zfix;

struct Progress {
  uint64_t	epoch = 0;
  RxProgress	rx;
  TxProgress	tx;
};

struct Order {
  ZuCSpan	id;
  unsigned	lane = 0;
  ZuBSpan	data, signature;
  uint64_t	seqNo = 0;
  bool		possDup = false;
};

ZfStruct(, (Order), (id, (FIX::ID<11>), String),
  (lane, (FIX::ID<7001>), UInt32),
  (data, (FIX::ID<95>, FIX::Binary<96>, FIX::Opt), Bytes),
  (signature, ((FIX::Sig<93, 89>)), Bytes));
using OrderFIXFields = ZuTypeConcat<SessionFields<Order>, ZfFields_Order>;
OrderFIXFields ZuFields_(Order *, ZuFacet::FIX *);
ZfStructImpl(Order); ZfStructImpl(Order, FIX);
using OrderMsg = MT<Order, "D">;
using Reply0 = MT<Order, "8", ZuFacet::FIX, Discrim<7001, "0">>;
using Reply1 = MT<Order, "8", ZuFacet::FIX, Discrim<7001, "1">>;
using Reply2 = MT<Order, "8", ZuFacet::FIX, Discrim<7001, "2">>;
using Reply3 = MT<Order, "8", ZuFacet::FIX, Discrim<7001, "3">>;
using Lane0 = MT<Order, "D", ZuFacet::FIX, Discrim<7001, "0">, ZuTypeList<Reply0>>;
using Lane1 = MT<Order, "D", ZuFacet::FIX, Discrim<7001, "1">, ZuTypeList<Reply1>>;
using Lane2 = MT<Order, "D", ZuFacet::FIX, Discrim<7001, "2">, ZuTypeList<Reply2>>;
using Lane3 = MT<Order, "D", ZuFacet::FIX, Discrim<7001, "3">, ZuTypeList<Reply3>>;
using Dictionary = MTList<Lane0, Lane1, Lane2, Lane3, Reply0, Reply1, Reply2, Reply3>;
#ifdef ZFIX_TEST_TLS
using Network = Zfix::TLS;
#else
using Network = Zfix::TCP;
#endif
static constexpr auto SignedPayload =
  "8=FIX.4.4\1" "9=13\1" "35=D\1" "58=ABCD\1" "10=155\1" "\0"
  "8=FIX.4.4\1" "9=13\1" "35=8\1" "58=ABCD\1" "10=143\1"_z;

template <typename O>
static bool verifySignature(const Fmt::FrameView &frame, ZuBSpan signature) {
  if (signature.length() != Ztls::HMAC<>::Size) return false;
  Fmt::FieldPos length, payload;
  if (Fmt::inspect<O>(frame.content, 93, length) ||
      Fmt::inspect<O>(frame.content, 89, payload) ||
      length.offset == UINT_MAX || payload.offset == UINT_MAX ||
      payload.offset + payload.length + 1 != frame.content.length()) return false;
  if (signature.data() != reinterpret_cast<const uint8_t *>(
      frame.content.data() + payload.offset)) return false;
  auto first = frame.msgType.data() - 3; // the profile covers the complete 35= field
  auto last = frame.content.data() + length.offset - 3; // immediately before 93=
  Ztls::HMAC<> mac;
  mac.start(ZuBSpan{"session fixture"});
  mac.update(ZuBSpan{ZuCSpan{first, uint64_t(last - first)}});
  uint8_t digest[Ztls::HMAC<>::Size]; mac.finish(digest);
  return signature == ZuBSpan{digest};
}

template <typename T> using Items = ZtArray<T, ZtArrayHeapID<"Zfix.SessionIOTest">>;
// Shared core TxBody objects cannot carry this application FIFO's links.
using PendingQueue = ZmList<TxBody::Ref, ZmListHeapID<"Zfix.SessionIOTest.Queue">>;
namespace Scenario {

  enum { Normal, Refused, PendingClose, LoadClose, PrepareRefused,
    DrainHold, PostedOverflow };
}

struct ServerLink;
struct TestState {
  RequestContext	retained;
  ZuCSpan		retainedID;
  ZuBSpan		retainedData, retainedSignature;
  const char		*retainedWire = nullptr;
  bool			signing = false; // immutable before workers start
  ZmAtomic<unsigned>	signedClient{0}, signedServer{0}, verifiedData{0}, verifiedAdmin{0};
  ZmSemaphore		contextReady;
  unsigned		workCase = 0; // immutable before workers start
  ZmSemaphore		workReady, workStop, workSent, workStarted, workDrained;
  ZmFn<void()>		workRelease, workDrain;
  ZmFn<bool()>		workIsDrained;
  bool			reverse = false; // immutable before workers start
  bool			keepContext = false; // immutable before workers start
  ZmSemaphore		listening, done, drained, timerDone, pending, stopPending, stoppedOne,
    socketSubmitted, socketInstalled;
  ZmFn<RxStats()>	rxSample; // invoked only on application Rx
  bool			adminTest = false, cutReplay = false;
  PostedState		replayCut;
  ZmFn<PostedState()>	adminSample; // application Rx-only snapshots
  ZmFn<TxStats()>	adminTx; // application Tx-only snapshots
  ZmFn<TxProgress()>	adminProgress;
  ZmFn<void()>		release; // published by Tx through stopPending, consumed once by main
  ZmAtomic<unsigned>	errors{0}, closed{0}, ready{0}, received{0}, businessSent{0},
    admission{0}, blocked{0}, periodic{0}, cancelled{0}, failures{0}, snapshots{0};
  unsigned		port = 0, listenPort = 0, mode = Transport::FIX4, heartbeat = 30,
    scenario = Scenario::Normal;
  ZmAtomic<unsigned>	listenFailures{0}, setupFailures{0}, overloadEvents{0};
  bool			expectListenFailure = false;
  unsigned		lane = 0, reconn = 0;
  SessionString		sub;
  ZmRef<ServerLink>	serverLink; // network Rx-owned; released after owner drains
  bool			asyncAdmission = false, forceBusy = false, trace = false;
  bool			asymmetric = false;
  ZmAtomic<unsigned>	traceRx{0}, traceTx{0}, traceLogons{0};
  ZmAtomic<unsigned>	busy{0}, downEvents{0};
  bool			setup = false, socketGate = false;

  void fail(const char *file = __builtin_FILE(), unsigned line = __builtin_LINE()) {
    log("# ZfixLinkStateIOTest failure ", file, ":", line, " mode=", mode, " lane=", lane);
    ++errors; done.post();
  }

  void stopped() { if (closed.xchAdd(1) == 1) drained.post(); stoppedOne.post(); }
};

static SessionConfig config(bool client, const TestState *s) {
  auto mode = s->mode; auto heartbeat = s->heartbeat;
  SessionConfig c; c.mode = mode; c.initiator = client; c.heartBtInt = heartbeat;
  c.identity.version = mode == Transport::FIXT11 ? Version::VT1_1 : Version::V4_4;
  c.appVerID = AppVersion::V5_0_SP2; c.identity.local = client ? "CLIENT" : "SERVER";
  c.identity.peer = client ? "SERVER" : "CLIENT";
  (client ? c.identity.localSub : c.identity.peerSub) = s->sub; return c;
}
static bool contains(ZuCSpan text, ZuCSpan term) {
  return text.find(term) >= 0;
}
template <typename Impl> struct Peer {
  using Msg = RxQueue::Node;
  using W = Zfix::Work<RequestContext, Impl>;
  const bool		client; // constructor-published role, stable on every shard

  // Application Rx owns dispatch/admission and its retained diagnostics/work.
  unsigned		replyCalls = 0, stopReports = 0;
  bool			refuseReply = false;
  TraceRef		rxTrace;
  AdmitFn		admission;
  typename W::Ref	pendingWork;

  // Tx owns sequence history and unsequenced work awaiting native admission.
  alignas(Zm::CacheLineSize)
  Items<TxHistory>	history;
  PendingQueue		queue;
  uint64_t		lastLoad = 0, pendingLoad = 0;
  bool			submitted = false;
  TraceRef		txTrace;
  struct DropWork {
    typename W::Ref	work;

    void operator ()() { work = nullptr; }
  };

  struct DropDispatch {
    Impl	*link;

    void operator ()() { link->app()->mx()->run(DropWork{ZuMv(link->pendingWork)}, 5); }
  };

  template <typename Entry> struct WorkReply {
    Peer		*peer;
    typename W::Ref	work;
    Order		order;

    void operator ()() {
      using Response = ZuType<0, typename Entry::Responses>;
      Fmt::Error error;
      if (!peer->self()->template reply<Entry, Response>(work->input(), Order{"res",
	order.lane}, 201, error))
	peer->state()->fail();
    }
  };

  template <typename Entry> struct WorkRun {
    Peer		*peer;
    typename W::Ref	work;
    Order		order;

    void operator ()() {
      if (peer->self()->app()->rxInvoked() || peer->self()->app()->txInvoked() ||
	order.id != "req" || !work->input().template is<Entry>()) peer->state()->fail();
      peer->self()->app()->rxRun(WorkReply<Entry>{peer, work, order});
    }
  };

  template <typename Entry>
  auto admitWork(const RequestContext &context, uint64_t extra) {
    Fmt::Error error;
    auto work = self()->template work<Entry>(context, error, extra);
    if (!work || error) { state()->fail(); return work; }
    auto &local = self()->workBudget(); auto &hub = self()->app()->fixWorkRx();
    if (local.limits_().count != 1 || hub.limits_().count != 1) state()->fail();
    auto failed = self()->template work<Entry>(context, error, UINT64_MAX);
    if (failed || error.code != Fmt::ErrorCode::Size) state()->fail();
    failed = self()->template work<Entry>(context, error, extra);
    if (failed || error.code != Fmt::ErrorCode::Size) state()->fail();
    if (!local.limits_({WorkLimits{}.bytes, 2})) state()->fail();
    failed = self()->template work<Entry>(context, error, extra);
    if (failed || error.code != Fmt::ErrorCode::Size) state()->fail();
    auto bytes = work->bytes();
    if (local.limits_({bytes - 1, 2}) || hub.limits_({bytes - 1, 2})) state()->fail();
    if (!local.limits_({2 * bytes - 1, 2}) || !hub.limits_({2 * bytes, 2})) state()->fail();
    failed = self()->template work<Entry>(context, error, extra);
    if (failed || error.code != Fmt::ErrorCode::Size) state()->fail();
    if (!local.limits_({2 * bytes, 2})) state()->fail();
    auto second = self()->template work<Entry>(context, error, extra);
    if (!second || error || second->bytes() != bytes || local.state_().count != 2 ||
	hub.state_().count != 2 || local.state_().bytes != 2 * bytes) state()->fail();
    second = nullptr; // retirement stays charged until the next Rx owner turn
    if (local.state_().count != 2 || !local.limits_({}) || !hub.limits_({})) state()->fail();
    return work;
  }

  explicit Peer(bool client_) : client{client_} { }

  auto self() { return static_cast<Impl *>(this); }
  auto state() { return self()->test; }

  enum { SignMsg = 1 };
  template <typename S>
  void signMsg(S &out, const TxBody *, TxContext, ZuCSpan message) const {
    auto link = static_cast<const Impl *>(this);
    auto s = link->test;
    if (!s->signing) return;
    if (!link->app()->txInvoked()) { s->fail(); out.fail(Fmt::ErrorCode::Output); return; }
    Ztls::HMAC<> mac;
    mac.start(ZuBSpan{"session fixture"}); mac.update(ZuBSpan{message});
    uint8_t digest[Ztls::HMAC<>::Size]; mac.finish(digest);
    Fmt::saveBinary<ZfField(Order, signature)>(out, digest);
    if (client) ++s->signedClient; else ++s->signedServer;
  }

  unsigned fixAssociateTx(TxBody::Ref b, TxContext c, uint64_t ticket) {
    if (!self()->app()->txInvoked() || c.epoch != 1 || c.seqNo != ++lastLoad) state()->fail();
    if (!b->administrative() &&
	((client && state()->scenario == Scenario::LoadClose) ||
	 (!client && state()->scenario == Scenario::DrainHold))) {
      pendingLoad = ticket; state()->pending.post(); return TxLoad::Pending;
    }
    return TxLoad::Accepted;
  }

  void fixStopTx(uint64_t) {
    queue.clean();
    if (!self()->txProgress().loaded) state()->fail();
    ++state()->snapshots;
    if (pendingLoad) {
      if (state()->scenario == Scenario::DrainHold) {
	state()->release = ZmFn<void()>{self(), [](Impl *link) {
	  link->app()->txRun([link] {
	    auto ticket = link->pendingLoad; link->pendingLoad = 0;
	    ++link->state()->cancelled; link->loaded(ticket, false);
	  });
	}};
	state()->stopPending.post(); return;
      }
      auto ticket = pendingLoad; pendingLoad = 0;
      ++state()->cancelled; self()->loaded(ticket, false);
    }
  }

  void fixStopRx(uint64_t, uint64_t) {
    if (!client && state()->workCase == 2)
      self()->app()->rxRun([this] { state()->workStop.post(); });
    if (!self()->rxProgress().processed) state()->fail();
    ++state()->snapshots;
    if (admission) {
      ++state()->cancelled;
      auto reply = ZuMv(admission);
      reply(false);
    }
  }

  void fixPublishedTx(TxBody::Ref body, TxContext c, ZuDateTime original) {
    if (!c.replay)
      history.push(TxHistory{ZuMv(body), original, c.epoch, c.seqNo});
  }

  TxReload fixRetrieveTx(TxContext c, uint64_t, uint64_t) {
    for (const auto &h : history)
      if (h.epoch == c.epoch && h.seqNo == c.seqNo &&
	  (!h.body->administrative() || h.body->msgType() == "3")) return {h};
    return {};
  }

  TxBody::Ref fixLogonTx(TxBody::Ref body) {
    if (!self()->app()->txInvoked()) state()->fail();
    if (client && state()->scenario == Scenario::PrepareRefused) return {};
    if (!state()->trace && !state()->signing) return body;
    SessionString auth;
    auth << body->body() << "95=" << ZuBoxed(SignedPayload.length()) << '\1' <<
      "96=" << SignedPayload << '\1';
    if (state()->trace)
      auth << "553=trace-user\1" "554=trace-secret\1" "10001=auth-private\1";
    Fmt::Error error;
    auto out = TxBody::restoreControl("A", auth, body->id(), error);
    if (error) state()->fail(); return out;
  }

  void fixCompleteTx(TxBody::Ref b, TxContext, bool ok) {
    if (!self()->app()->txInvoked() || (!ok
      && state()->scenario == Scenario::Normal)) state()->fail();
    if (b->msgType() == "D" || b->msgType() == "8") ++state()->businessSent;
    if (state()->workCase >= 2 && ok && !b->administrative()) state()->workSent.post();
  }

  void fixAckdTx(TxContext, uint64_t previous, uint64_t next) {
    if (!state()->trace) return;
    if (!self()->app()->txInvoked() || next <= previous) state()->fail();
    state()->done.post();
  }

  void traceCheck(TraceRef entry) {
    if (!state()->trace || !entry) { state()->fail(); return; }
    auto text = entry->data().text();
    if (contains(text, "trace-user") || contains(text, "trace-secret") || contains(text,
      "auth-private")) state()->fail();
    if (contains(text, "35=A")) {
      if (!contains(text, "<redacted>")) state()->fail(); ++state()->traceLogons;
    }
    for (auto c : text) if (uint8_t(c) < 32 || uint8_t(c) >= 127) state()->fail();
  }

  void fixTraceRx(TraceRef entry) {
    if (!self()->app()->rxInvoked()) state()->fail();
    traceCheck(entry); rxTrace = ZuMv(entry); ++state()->traceRx;
  }

  void fixTraceTx(TraceRef entry) {
    if (!self()->app()->txInvoked()) state()->fail();
    traceCheck(entry); txTrace = ZuMv(entry); ++state()->traceTx;
  }

  void fixFaultTx(TxContext, Fmt::Error) {
    if (state()->scenario == Scenario::Normal && state()->workCase != 2) state()->fail();
  }

  unsigned fixAdmissionRx(SessionEvent, ZmRef<Msg> msg, AdmitRequest &request) {
    if (!self()->app()->rxInvoked() || !msg || !msg->data().control.template is<Session::Logon>())
      state()->fail();
    if (state()->asymmetric && msg && msg->data().control.template is<Session::Logon>()) {
      // Peer receive capacity differs from both our receive and its Tx limit.
      unsigned expected = client ? 8192 : 4096;
      if (msg->data().control.template p<Session::Logon>().maxMessageSize != expected)
	state()->fail();
    }
    if (state()->signing || state()->trace) {
      const auto &auth = msg->data().control.template p<Session::Logon>().rawAuth;
      Fmt::FieldPos pos;
      if (Fmt::inspect<Session::Logon>(msg->data().frame.content, 96, pos) ||
	  pos.offset == UINT_MAX || auth != ZuBSpan{SignedPayload} ||
	  auth.data() != reinterpret_cast<const uint8_t *>(
	    msg->data().frame.content.data() + pos.offset)) state()->fail();
    }
    if (state()->signing) {
      const auto &logon = msg->data().control.template p<Session::Logon>();
      if (!verifySignature<Session::Logon>(msg->data().frame, logon.signature)) state()->fail();
      ++state()->verifiedAdmin;
    }
    ++state()->admission;
    if (!client && state()->scenario == Scenario::Refused) return Admission::Refused;
    if (!state()->asyncAdmission || !client) return Admission::Accepted;
    admission = request.defer();
    return Admission::Pending;
  }

  void fixBlockedRx(TxContext) {
    ++state()->blocked;
    if (!admission) return;
    if (state()->scenario == Scenario::PendingClose) { state()->pending.post(); return; }
    auto reply = ZuMv(admission);
    // Defer admission until a second control frame is actually paused. An
    // application owner then posts only the fixed admission continuation.
    self()->app()->mx()->run([reply = ZuMv(reply)] { reply(true); }, 5);
  }

  void fixControlRx(ZmRef<Msg> msg) {
    if (state()->signing) {
      bool verified = false;
      ZuSwitch::dispatch<Session::Msg::N>(msg->data().control.type(),
	[&verified, &msg]<typename I>(I) {
	  using O = Session::Msg::Type<I{}>;
	  if constexpr (!ZuIsSame<O, void>{}) verified = verifySignature<O>(
	    msg->data().frame, msg->data().control.template p<O>().signature);
	});
      if (!verified) state()->fail();
      ++state()->verifiedAdmin;
    }
    if (client || !state()->cutReplay
      || !msg->data().control.template is<Session::ResendReq>()) return;
    // Recovery's fixed lifecycle event has been admitted. Leave exactly that
    // reservation in the hub so the following replay command must be refused.
    state()->replayCut = self()->adminPosted();
    if (!state()->replayCut.count ||
	!self()->app()->fixAdminRx().limits_({state()->replayCut.bytes})) state()->fail();
    state()->cutReplay = false;
  }

  void fixControlTx(TxBody::Ref body, TxContext) {
    if (body->msgType() == "0" && !body->body().length())
      if (state()->periodic.xchAdd(1) == 1) state()->timerDone.post();
  }

  void fixEventRx(SessionEvent e, ZmRef<Msg>) {
    if (!client && state()->workCase == 3 && (e.actions & SessionAction::Drained))
      state()->workDrained.post();
    if (!self()->app()->rxInvoked()) state()->fail();
    if (state()->adminTest && e.state == State::Disconnected) ++state()->downEvents;
    if (e.actions & SessionAction::Failure) {
      log("# session failure role=", client ? "client" : "server", " mode=", state()->mode,
	" lane=", state()->lane, " scenario=", state()->scenario, " work=", state()->workCase,
	" admin=", unsigned(state()->adminTest), " state=", e.state, " error=", e.error, " tag=",
	e.tag, " conn=", e.connection, " epoch=", e.epoch, " actions=", e.actions);
      ++state()->failures;
      if (e.error == SessionError::Overload) ++state()->overloadEvents;
      if (state()->scenario == Scenario::Normal && state()->workCase != 2) state()->fail();
      else state()->done.post();
    }
    if ((e.actions & SessionAction::Ready) && !submitted) {
      submitted = true; ++state()->ready;
      // Deliberate application traffic supplies the second frame that tests
      // pending admission's backpressure. Ordinary readiness sends no probe.
      if (!client && state()->asyncAdmission) self()->probe_();
      if (!client && state()->adminTest)
	state()->rxSample = [link = ZmRef<Impl>{self()}] { return link->rxStats(); };
      if (!client && state()->adminTest) {
	state()->adminSample = [link = ZmRef<Impl>{self()}] { return link->adminPosted(); };
	state()->adminTx = [link = ZmRef<Impl>{self()}] { return link->txStats(); };
	state()->adminProgress = [link = ZmRef<Impl>{self()}] { return link->txProgress(); };
      }
      if (client != state()->reverse) {
	Fmt::Error error;
	if (state()->forceBusy) {
	  auto b = TxBody::encode<OrderMsg>(Order{"req", state()->lane}, 101, error);
	  if (error) state()->fail();
	  else self()->app()->txRun([this, b = ZuMv(b)]() mutable {
	    // Marker and submit keep the existing deterministic Busy fixture.
	    self()->queued();
	    self()->app()->txRun([this, b = ZuMv(b)] { if (!self()->submit(b)) state()->fail();
	      });
	  });
	} else if (!sendOrder(Order{"req", state()->lane}, client ? 101 : 201,
	  error)) state()->fail();
      }
    }
  }

  bool sendOrder(const Order &order, uint64_t id, Fmt::Error &error) {
    auto value = order;
    if (state()->signing) value.data = ZuBSpan{SignedPayload};
    switch (value.lane) {
      case 0: return self()->template send<Lane0>(value, id, error);
      case 1: return self()->template send<Lane1>(value, id, error);
      case 2: return self()->template send<Lane2>(value, id, error);
      case 3: return self()->template send<Lane3>(value, id, error);
      default: error = {Fmt::ErrorCode::Value, 7001}; return false;
    }
  }

  TxBody::Ref fixDequeueTx() {
    auto node = queue.shift(); return node ? ZuMv(node->data()) : TxBody::Ref{};
  }

  void fixSelectedTx(TxBody::Ref, TxContext, unsigned result) {
    bool cancelled = state()->scenario == Scenario::LoadClose ||
      (!client && state()->scenario == Scenario::DrainHold);
    if (!self()->app()->txInvoked() ||
	(result != TxSelect::Accepted && !(cancelled
	  && result == TxSelect::Refused))) state()->fail();
  }

  void fixSubmittedTx(TxBody::Ref body, unsigned result) {
    if (result == TxSelect::Busy || result == TxSelect::Held) {
      if (result == TxSelect::Busy) {
	if (!state()->forceBusy)
	  log("# deferred Busy submission client=", client, " lane=", state()->lane);
	++state()->busy;
      }
      queue.push(ZuMv(body)); self()->queued(); return;
    }
    bool pending = state()->scenario == Scenario::LoadClose ||
      (!client && state()->scenario == Scenario::DrainHold);
    if (result != (pending ? TxSelect::Pending : TxSelect::Accepted)) {
      log("# submission result=", result, " client=", client, " lane=", state()->lane);
      state()->fail();
    }
  }

  template <typename Entry>
  Fmt::Error fixReceiveRx(Entry *, const Order &o, const RequestContext &context) {
    const auto &meta = context.context();
    const auto &session = self()->session_();
    if (!context || meta.session != session.instance_() || meta.epoch != session.epoch_() ||
	meta.connection != session.connection_() || meta.seqNo != o.seqNo ||
	meta.version != Version::Wire::v2s(session.config_().identity.version) ||
	meta.replay != o.possDup ||
	meta.possDup != o.possDup || context.sender() != session.config_().identity.peer ||
	context.target() != session.config_().identity.local || !*context.received()
	  || !*context.utc()) state()->fail();
    if constexpr ((ZuIsSame<Entry, Lane0>{} || ZuIsSame<Entry,
      Reply0>{})) { if (o.lane) state()->fail(); }
    else if constexpr ((ZuIsSame<Entry, Lane1>{} || ZuIsSame<Entry,
      Reply1>{})) { if (o.lane != 1) state()->fail(); }
    else if constexpr ((ZuIsSame<Entry, Lane2>{} || ZuIsSame<Entry,
      Reply2>{})) { if (o.lane != 2) state()->fail(); }
    else if constexpr ((ZuIsSame<Entry, Lane3>{} || ZuIsSame<Entry,
      Reply3>{})) { if (o.lane != 3) state()->fail(); }
    else state()->fail();
    if (state()->signing) {
      Fmt::FieldPos pos;
      if (o.data != ZuBSpan{SignedPayload} || !verifySignature<Order>(context.frame(), o.signature) ||
	  Fmt::inspect<Order>(context.frame().content, 96, pos) || pos.offset == UINT_MAX ||
	  o.data.data() != reinterpret_cast<const uint8_t *>(context.frame().content.data() +
	    pos.offset)) state()->fail();
      ++state()->verifiedData;
    }
    if (!client && state()->keepContext) {
      state()->retained = context; state()->retainedID = o.id;
      state()->retainedData = o.data; state()->retainedSignature = o.signature;
      state()->retainedWire = context.frame().wire.data();
      state()->contextReady.post();
    }
    if (!self()->app()->rxInvoked() || o.id != (client != state()->reverse ? "res" : "req")
      || o.lane != state()->lane) state()->fail();
    ++state()->received;
    if constexpr (Entry::Responses::N) {
      if (client != state()->reverse) { state()->fail(); return {}; }
      using Response = ZuType<0, typename Entry::Responses>;
      if (state()->workCase && !client) {
	constexpr uint64_t RunBytes =
	  ZmHeapAllocSize<sizeof(WorkRun<Entry>) + sizeof(ZmRingFn<ZmRingFn_Defaults>)>{} +
	  ZmHeapAllocSize<sizeof(WorkReply<Entry>) + sizeof(ZmRingFn<ZmRingFn_Defaults>)>{};
	constexpr uint64_t DropBytes =
	  ZmHeapAllocSize<sizeof(DropWork) + sizeof(ZmRingFn<ZmRingFn_Defaults>)>{} +
	  ZmHeapAllocSize<sizeof(DropDispatch) + sizeof(ZmRingFn<ZmRingFn_Defaults>)>{};
	auto work = admitWork<Entry>(context, state()->workCase >= 2 ? DropBytes : RunBytes);
	if (!work) return {Fmt::ErrorCode::Size};
	if (state()->workCase >= 2) {
	  pendingWork = ZuMv(work);
	  state()->workRelease = ZmFn<void()>{self(), [](Impl *link) {
	    link->app()->rxRun(DropDispatch{link});
	  }};
	  if (state()->workCase == 3) {
	    state()->workIsDrained = ZmFn<bool()>{self(),
	      [](Impl *link) { return link->session_().drained_(); }};
	    state()->workDrain = ZmFn<void()>{self(), [](Impl *link) {
	      link->app()->rxRun([link] {
		const auto &session = link->session_();
		if (!link->reset_({session.connection_(), session.epoch_(),
		  ResetOp::Drain})) link->state()->fail();
		Fmt::Error error;
		if (link->template work<Entry>(link->pendingWork->input(), error) ||
		    error.code != Fmt::ErrorCode::Output) link->state()->fail();
		link->state()->workStarted.post();
	      });
	    }};
	  }
	  state()->workReady.post(); return {};
	}
	self()->app()->mx()->run(WorkRun<Entry>{this, work, o}, 5);
	return {};
      }
      Fmt::Error error; unsigned before = replyCalls;
      if (self()->template reply<Entry, Response>({}, Order{"res", state()->lane}, 201, error) ||
	error.code != Fmt::ErrorCode::Unresolved || error.tag != 35
	  || replyCalls != before) state()->fail();
      if (state()->lane != 3 && (self()->template reply<Lane3, Reply3>(context,
	  Order{"res", 3}, 201, error) || error.code != Fmt::ErrorCode::Unresolved ||
	  error.tag != 35 || replyCalls != before)) state()->fail();
      refuseReply = true;
      if (self()->template reply<Entry, Response>(context, Order{"res", state()->lane}, 201,
	error) ||
	error.code != Fmt::ErrorCode::Value || error.tag != 11 || error.offset != 37 ||
	replyCalls != before + 1) state()->fail();
      refuseReply = false;
      Order response{"res", state()->lane};
      if (state()->signing) response.data = ZuBSpan{SignedPayload};
      if (!self()->template reply<Entry, Response>(context,
	response, client ? 101 : 201, error)) return error;
      if (replyCalls != before + 2) state()->fail();
    } else {
      if (client == state()->reverse) state()->fail();
      Fmt::Error error; unsigned before = replyCalls;
      if (self()->template reply<Lane0, Reply0>(context, Order{"res", state()->lane}, 201,
	error) ||
	error.code != Fmt::ErrorCode::Unresolved || error.tag != 35
	  || replyCalls != before) state()->fail();
      state()->done.post();
    }
    return {};
  }

  template <typename Request, typename Response>
  Fmt::Error fixReplyRx(Request *, Response *, const RequestContext &context) {
    if (!self()->app()->rxInvoked()) state()->fail();
    ++replyCalls;
    if (refuseReply) return {Fmt::ErrorCode::Value, 11, 37};
    // This fixture permits replies in the same runtime session/epoch, including
    // physical reconnect, but leaves replay/business-key policy explicit above.
    const auto &session = self()->session_();
    if (context.context().session != session.instance_() ||
	context.context().epoch != session.epoch_()) return {Fmt::ErrorCode::Unresolved, 35};
    return {};
  }

  void fixStoppedRx(bool) {
    if (!self()->app()->rxInvoked()
      || self()->session_().state_() != State::Disconnected) state()->fail();
    ++stopReports;
    state()->stopped();
  }
};

struct CliHub : public Zfix::Client<CliHub, Dictionary, Network> {
  using Base = Zfix::Client<CliHub, Dictionary, Network>;
  struct Link;
  TestState	*state;

  explicit CliHub(TestState *s, HubLimits limits = {}) : Base{limits}, state{s} { }

  unsigned reconnFreq() const { return state->reconn; }

  bool init(Network::ClientParams params);
};

struct CliHub::Link : public ZmNHeap<Link>, public Zfix::CliLink<CliHub, Link, Dictionary,
  Network>, public Peer<Link> {
friend inline ZuStringT<"Zfix.session_io_test.Link"> ZmNHeapID(Link *);
  using ZmNHeap<Link>::operator new;
  using ZmNHeap<Link>::operator delete;
  using Base = Zfix::CliLink<CliHub, Link, Dictionary, Network>;
  using Base::app;
  TestState	*test;

  explicit Link(CliHub *app, TestState *s = nullptr, ZuID id = ZuID{"fixClient"}) : Base{app,
    ZuMv(id),
    config(true, s ? s : app->state)}, Peer<Link>{true}, test{s ? s : app->state} { }

  bool connectSocket_(bool replacing) {
    bool accepted = Base::connectSocket_(replacing);
    if (test->setup) test->socketInstalled.post();
    return accepted;
  }

  void connectFailed(bool transient) {
    if (!test->setup) { Base::connectFailed(transient); return; }
    if (!app()->rxInvoked()) test->fail();
    ++test->setupFailures;
    if (test->reconn) Base::connectFailed(transient); // opt into native retry
  }

  void connectIP_(ZiIP ip) {
    Base::connectIP_(ip);
    if (test->socketGate) test->socketSubmitted.post();
  }

  using Peer<Link>::SignMsg;
  using Peer<Link>::signMsg;
  using Peer<Link>::fixAssociateTx;
  using Peer<Link>::fixPublishedTx;
  using Peer<Link>::fixRetrieveTx;
  using Peer<Link>::fixCompleteTx;
  using Peer<Link>::fixAckdTx;
  using Peer<Link>::fixLogonTx;
  using Peer<Link>::fixTraceRx;
  using Peer<Link>::fixTraceTx;
  using Peer<Link>::fixFaultTx;
  using Peer<Link>::fixAdmissionRx;
  using Peer<Link>::fixBlockedRx;
  using Peer<Link>::fixControlTx;
  using Peer<Link>::fixControlRx;
  using Peer<Link>::fixEventRx;
  using Peer<Link>::fixSubmittedTx;
  using Peer<Link>::fixDequeueTx;
  using Peer<Link>::fixSelectedTx;
  using Peer<Link>::fixReceiveRx;
  using Peer<Link>::fixReplyRx;
  using Peer<Link>::fixStoppedRx;
  using Peer<Link>::fixStopRx;
  using Peer<Link>::fixStopTx;
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

struct SrvHub : public Zfix::Server<SrvHub, Dictionary, Network> {
  using Base = Zfix::Server<SrvHub, Dictionary, Network>;
  using Link = ServerLink;
  TestState		*state;
  Items<TestState *>	routes; // immutable after worker start
  unsigned		next = 0; // native network Rx owner, used only by accepted()

  explicit SrvHub(TestState *s, HubLimits limits = {}) : Base{limits}, state{s} { }

  void final();

  ZiConnection *accepted(const ZiCxnInfo &);

  ZiIP localIP() const { return ZiIP{"127.0.0.1"}; }
  unsigned localPort() const { return state->listenPort; }
  unsigned nAccepts() const { return 1; }

  void listening(const ZiListenInfo &i) { state->port = i.port; state->listening.post(); }

  void listenFailed(bool) {
    ++state->listenFailures;
    if (!state->expectListenFailure) state->fail();
    state->listening.post();
  }

  bool init(Network::ServerParams params);
};

struct ServerLink : public ZmNHeap<ServerLink>,
  public Zfix::SrvLink<SrvHub, ServerLink, Dictionary, Network>, public Peer<ServerLink> {
friend inline ZuStringT<"Zfix.session_io_test.Link"> ZmNHeapID(ServerLink *);
  using ZmNHeap<ServerLink>::operator new;
  using ZmNHeap<ServerLink>::operator delete;
  using Base = Zfix::SrvLink<SrvHub, ServerLink, Dictionary, Network>;
  using Base::app;
  TestState	*test;

  explicit ServerLink(SrvHub *app, TestState *s) : Base{app, config(false, s)},
    Peer<ServerLink>{false}, test{s} { }

  using Peer<ServerLink>::SignMsg;
  using Peer<ServerLink>::signMsg;
  using Peer<ServerLink>::fixAssociateTx;
  using Peer<ServerLink>::fixPublishedTx;
  using Peer<ServerLink>::fixRetrieveTx;
  using Peer<ServerLink>::fixCompleteTx;
  using Peer<ServerLink>::fixAckdTx;
  using Peer<ServerLink>::fixLogonTx;
  using Peer<ServerLink>::fixTraceRx;
  using Peer<ServerLink>::fixTraceTx;
  using Peer<ServerLink>::fixFaultTx;
  using Peer<ServerLink>::fixAdmissionRx;
  using Peer<ServerLink>::fixBlockedRx;
  using Peer<ServerLink>::fixControlTx;
  using Peer<ServerLink>::fixControlRx;
  using Peer<ServerLink>::fixEventRx;
  using Peer<ServerLink>::fixSubmittedTx;
  using Peer<ServerLink>::fixDequeueTx;
  using Peer<ServerLink>::fixSelectedTx;
  using Peer<ServerLink>::fixReceiveRx;
  using Peer<ServerLink>::fixReplyRx;
  using Peer<ServerLink>::fixStoppedRx;
  using Peer<ServerLink>::fixStopRx;
  using Peer<ServerLink>::fixStopTx;
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

void SrvHub::final() {
  auto clear = [this](TestState *s) {
    if (s->serverLink && s->serverLink->app() == this) s->serverLink = nullptr;
  };
  clear(state); for (auto *s : routes) clear(s);
  Base::final();
}
ZiConnection *SrvHub::accepted(const ZiCxnInfo &ci) {
  auto *s = routes.length() && next < routes.length() ? routes[next++] : state;
  if (!s->serverLink) s->serverLink = new Link{this, s};
  return new Link::Cxn{s->serverLink, ci};
}
static void loopback(unsigned mode, bool async = false, unsigned heartbeat = 30,
  unsigned scenario = Scenario::Normal, bool forceBusy = false, bool keepContext = false,
  bool reverse = false, unsigned workCase = 0, bool signing = false) {
  ZuTestScope(loopback);
  TestState state; state.mode = mode; state.asyncAdmission = async; state.heartbeat = heartbeat;
    state.scenario = scenario; state.forceBusy = forceBusy;
  state.asymmetric = true; state.signing = signing;
  HubLimits sl, cl;
  sl.link.rx.codec.frameBytes = 8192; sl.link.tx.codec.frameBytes = 2048;
  cl.link.rx.codec.frameBytes = 4096; cl.link.tx.codec.frameBytes = 1024;
  state.keepContext = keepContext; state.reverse = reverse; state.workCase = workCase;
  if (workCase) sl.link.work.count = sl.work.count = 1;
  SrvHub server{&state, sl}; CliHub client{&state, cl};
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(5)
      .thread(1, [](auto &t) { t.isolated(1); })
      .thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); })
      .thread(4, [](auto &t) { t.isolated(1); })
      .thread(5, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  Net::ServerParams sp{&mx, "3", "4"}; Net::ClientParams cp{&mx, "3", "4"};
#ifdef ZFIX_TEST_TLS
  const char *cert = getenv("ZFIX_TEST_CERT"), *key = getenv("ZFIX_TEST_KEY");
  if (!cert || !key) { ZuCheck(cert && key); mx.stop(); return; }
  sp.certPath(cert).keyPath(key); cp.caPath(cert);
#endif
  bool si = server.init(ZuMv(sp)), ci = client.init(ZuMv(cp)); ZuCheck(si && ci);
  bool started = mx.start(); ZuCheck(started); if (!started) return;
  bool ss = si && server.start(), cs = ci && client.start(); ZuCheck(ss && cs);
  ZmRef<CliHub::Link> link;
  if (ss && cs) {
    server.listen();
    bool listening = state.listening.timedwait(Zm::now(5)) == 0; ZuCheck(listening && state.port);
    if (listening && state.port) {
      link = new CliHub::Link{&client};
#ifdef ZFIX_TEST_TLS
      link->connect("localhost", state.port, ZiIP{"127.0.0.1"});
#else
      link->connect("127.0.0.1", state.port);
#endif
      if (workCase >= 2) {
	ZuCheck(state.workReady.timedwait(Zm::now(5)) == 0);
	ZuCheck(state.workSent.timedwait(Zm::now(5)) == 0);
	if (workCase == 2) {
	  link->disconnect(); ZuCheck(state.workStop.timedwait(Zm::now(5)) == 0);
	} else {
	  ZuCheck(bool(state.workDrain)); if (state.workDrain) state.workDrain();
	  ZuCheck(state.workStarted.timedwait(Zm::now(5)) == 0);
	}
	ZmBlock<>{}([&server](auto wake) {
	  server.txRun([&server, wake = ZuMv(wake)]() mutable {
	    server.rxRun([&server, wake = ZuMv(wake)]() mutable {
	      server.txRun([&server, wake = ZuMv(wake)]() mutable {
		server.rxRun([wake = ZuMv(wake)]() mutable { wake(); });
	      });
	    });
	  });
	});
	if (workCase == 2) {
	  ZuCheck(state.closed.load_() == 1 && state.drained.trywait() != 0);
	} else {
	  bool drained = true;
	  ZmBlock<>{}([&server, &state, &drained](auto wake) {
	    server.rxRun([&state, &drained, wake = ZuMv(wake)]() mutable {
	      if (state.workIsDrained) drained = state.workIsDrained(); wake();
	    });
	  });
	  ZuCheck(!drained && state.workDrained.trywait() != 0);
	}
	ZuCheck(bool(state.workRelease));
	if (state.workRelease) { auto release = ZuMv(state.workRelease); release(); }
	if (workCase == 3) {
	  ZuCheck(state.workDrained.timedwait(Zm::now(5)) == 0);
	  link->logout();
	}
      } else
      switch (scenario) {
	case Scenario::PendingClose:
	case Scenario::LoadClose:
	  ZuCheck(state.pending.timedwait(Zm::now(5)) == 0); link->disconnect(); break;
	case Scenario::PrepareRefused:
	case Scenario::Refused:
	  ZuCheck(state.done.timedwait(Zm::now(5)) == 0); break;
	default:
	  ZuCheck(state.done.timedwait(Zm::now(5)) == 0);
	  if (heartbeat == 1) ZuCheck(state.timerDone.timedwait(Zm::now(5)) == 0);
	  if (async) ZuCheck(state.blocked.load_() > 0);
	  if (keepContext) ZuCheck(state.contextReady.timedwait(Zm::now(5)) == 0);
	  link->logout(); break;
      }
      ZuCheck(state.drained.timedwait(Zm::now(5)) == 0);
      if (workCase >= 2) {
	ZuCheck(state.ready.load_() == 2 && state.received.load_() == 1
	  && state.businessSent.load_() == 1);
      } else if (scenario == Scenario::Normal) {
	ZuCheck(state.ready.load_() == 2 && state.received.load_() == 2);
	ZuCheck(state.admission.load_() == 2 && state.businessSent.load_() == 2);
	if (forceBusy) ZuCheck(state.busy.load_() > 0);
      } else {
	ZuCheck(!state.received.load_() && !state.businessSent.load_() && state.failures.load_());
	if (scenario == Scenario::PendingClose || scenario == Scenario::LoadClose)
	  ZuCheck(state.cancelled.load_() == 1);
      }
    }
  }
  if (workCase) {
    WorkState work;
    ZmBlock<>{}([&server, &work](auto wake) {
      server.rxRun([&server, &work, wake = ZuMv(wake)]() mutable {
	work = server.fixWorkRx().state_(); wake();
      });
    });
    ZuCheck(!work.count && !work.bytes && work.peakCount == 2 && work.refused == 4);
  }
  if (cs) { bool stopped = client.stop(); ZuCheck(stopped); }
  if (ss) { bool stopped = server.stop(); ZuCheck(stopped); }
  client.final(); server.final(); mx.stop(); link = nullptr;
  if (keepContext) {
    auto copy = state.retained;
    state.retained = {};
    ZuCheck(bool(copy) && copy.context().session && copy.context().epoch == 1);
    ZuCheck(copy.context().connection == 1 && copy.context().seqNo >= 2
      && !copy.context().replay);
    ZuCheck(copy.sender() == "CLIENT" && copy.target() == "SERVER");
    ZuCheck(state.retainedID == "req" && copy.frame().wire.length());
    auto parsed = Fmt::frame(copy.frame().wire);
    ZuCheck(parsed.state == Fmt::FrameState::Complete && parsed.view.msgType == "D");
    if (signing) {
      ZuCheck(copy.frame().wire.data() == state.retainedWire);
      ZuCheck(state.retainedData == ZuBSpan{SignedPayload} &&
	verifySignature<Order>(copy.frame(), state.retainedSignature));
      Fmt::FieldPos data, signature;
      ZuCheck(!Fmt::inspect<Order>(copy.frame().content, 96, data));
      ZuCheck(!Fmt::inspect<Order>(copy.frame().content, 89, signature));
      ZuCheck(state.retainedData.data() == reinterpret_cast<const uint8_t *>(
	copy.frame().content.data() + data.offset));
      ZuCheck(state.retainedSignature.data() == reinterpret_cast<const uint8_t *>(
	copy.frame().content.data() + signature.offset));
    }
  }
  if (signing) {
    ZuCheck(state.signedClient.load_() && state.signedServer.load_());
    ZuCheck(state.verifiedData.load_() == 2 && state.verifiedAdmin.load_() >= 2);
  }
  ZuCheck(!state.errors.load_() && state.closed.load_() == 2);
  ZuCheck(state.snapshots.load_() == 4);
}
// Independent application-routed logical links share the same hub owners.
// Closing one must not disturb traffic or lifecycle on the other links.
static void routeMatrix(unsigned mode) {
  ZuTestScope(routeMatrix);
  TestState first, second, third;
  Items<TestState *> states; states << &first << &second << &third;
  unsigned lane = 0;
  for (auto *s : states) {
    s->mode = mode; s->lane = ++lane;
  }
  SrvHub server{&first}, other{&third};
  server.routes << &first << &second; other.routes << &third;
  CliHub client{&first};
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(5)
      .thread(1, [](auto &t) { t.isolated(1); }).thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); }).thread(4, [](auto &t) { t.isolated(1); })
      .thread(5, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  auto srvParams = [&mx] {
    Net::ServerParams p{&mx, "3", "4"};
#ifdef ZFIX_TEST_TLS
    p.certPath(getenv("ZFIX_TEST_CERT")).keyPath(getenv("ZFIX_TEST_KEY"));
#endif
    return p;
  };
  auto cliParams = [&mx] {
    Net::ClientParams p{&mx, "3", "4"};
#ifdef ZFIX_TEST_TLS
    p.caPath(getenv("ZFIX_TEST_CERT"));
#endif
    return p;
  };
  bool si = server.init(srvParams()), oi = other.init(srvParams());
  bool ci = client.init(cliParams()); ZuCheck(si && oi && ci);
  bool started = mx.start(); ZuCheck(started); if (!started) return;
  bool ss = si && server.start(), os = oi && other.start();
  bool cs = ci && client.start(); ZuCheck(ss && os && cs);
  server.listen(); other.listen();
  ZuCheck(first.listening.timedwait(Zm::now(5)) == 0);
  ZuCheck(third.listening.timedwait(Zm::now(5)) == 0);
  Items<ZmRef<CliHub::Link>> links;
  auto connect = [&links](CliHub *hub, TestState *s, unsigned port) {
    ZmRef<CliHub::Link> link = new CliHub::Link{hub, s}; links << link;
#ifdef ZFIX_TEST_TLS
    link->connect("localhost", port, ZiIP{"127.0.0.1"});
#else
    link->connect("127.0.0.1", port);
#endif
  };
  connect(&client, &first, first.port);
  ZuCheck(first.done.timedwait(Zm::now(5)) == 0);
  connect(&client, &second, first.port);
  ZuCheck(second.done.timedwait(Zm::now(5)) == 0);
  connect(&client, &third, third.port);
  ZuCheck(third.done.timedwait(Zm::now(5)) == 0);
  links[0]->logout(); ZuCheck(first.drained.timedwait(Zm::now(5)) == 0);
  ZuCheck(!second.closed.load_() && !third.closed.load_());
  unsigned begin = 1, end = links.length();
  for (unsigned i = begin; i < end; ++i) {
    auto *s = states[i]; Fmt::Error error;
    auto body = TxBody::encode<OrderMsg>(Order{"req", s->lane}, 301 + i, error);
    ZuCheck(body && !error);
    client.rxRun([link = links[i], body = ZuMv(body), s] {
      if (!link->submit(body)) ++s->errors;
    });
  }
  for (unsigned i = begin; i < end; ++i) {
    auto *s = states[i]; ZuCheck(s->done.timedwait(Zm::now(5)) == 0);
    links[i]->logout(); ZuCheck(s->drained.timedwait(Zm::now(5)) == 0);
  }
  bool cstop = client.stop(), sstop = server.stop(), ostop = other.stop();
  ZuCheck(cstop && sstop && ostop);
  client.final(); server.final(); other.final(); mx.stop(); links.length(0);
  unsigned n = states.length();
  for (unsigned i = 0; i < n; ++i) {
    auto *s = states[i]; unsigned traffic = !i ? 2 : 4;
    ZuCheck(!s->errors.load_() && s->closed.load_() == 2 && s->snapshots.load_() == 4,
      log_("lane=", s->lane, " errors=", s->errors.load_(), " closed=", s->closed.load_(),
	" snapshots=", s->snapshots.load_()));
    ZuCheck(s->ready.load_() == 2 && s->admission.load_() == 2);
    ZuCheck(s->received.load_() == traffic && s->businessSent.load_() == traffic,
      log_("lane=", s->lane, " received=", s->received.load_(), " sent=",
	s->businessSent.load_(), " expected=", traffic));
  }
}
static Net::ServerParams srvParams(ZiMultiplex *mx, const char *rx = "3",
  const char *tx = "4") {
  Net::ServerParams p{mx, rx, tx};
#ifdef ZFIX_TEST_TLS
  p.certPath(getenv("ZFIX_TEST_CERT")).keyPath(getenv("ZFIX_TEST_KEY"));
#endif
  return p;
}
static Net::ClientParams cliParams(ZiMultiplex *mx, const char *rx = "3",
  const char *tx = "4") {
  Net::ClientParams p{mx, rx, tx};
#ifdef ZFIX_TEST_TLS
  p.caPath(getenv("ZFIX_TEST_CERT"));
#endif
  return p;
}
// A live multiplexer must outlast hub stop/restart. Rebinding the exact old
// ephemeral address proves the old listening socket was actually removed.
static void listenerCleanup() {
  ZuTestScope(listenerCleanup);
  TestState state, retry; retry.expectListenFailure = true;
  SrvHub server{&state}, rival{&retry};
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(4)
      .thread(1, [](auto &t) { t.isolated(1); }).thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); }).thread(4, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  bool si = server.init(srvParams(&mx)); ZuCheck(si);
  bool ri = rival.init(srvParams(&mx)); ZuCheck(ri);
  bool started = mx.start(); ZuCheck(started); if (!started) return;
  bool ss = si && server.start(); ZuCheck(ss);
  server.listen(); ZuCheck(state.listening.timedwait(Zm::now(5)) == 0 && state.port);
  retry.listenPort = state.port;
  bool rs = ri && rival.start(); ZuCheck(ri && rs);
  rival.listen(); ZuCheck(retry.listening.timedwait(Zm::now(5)) == 0);
  ZuCheck(retry.listenFailures.load_() == 1 && !retry.port);
  bool rstop = rival.stop(), sstop = server.stop(); ZuCheck(rstop && sstop);
  rs = rival.start(); ZuCheck(rs);
  rival.listen(); ZuCheck(retry.listening.timedwait(Zm::now(5)) == 0);
  ZuCheck(retry.listenFailures.load_() == 1 && retry.port == retry.listenPort);
  rstop = rival.stop(); ZuCheck(rstop);
  ss = server.start(); ZuCheck(ss);
  server.listen(); ZuCheck(state.listening.timedwait(Zm::now(5)) == 0 && state.port);
  sstop = server.stop(); ZuCheck(sstop);
  rs = rival.start(); ZuCheck(rs);
  rival.listen(); ZuCheck(retry.listening.timedwait(Zm::now(5)) == 0);
  ZuCheck(retry.listenFailures.load_() == 1 && retry.port == retry.listenPort);
  rstop = rival.stop(); ZuCheck(rstop);
  state.listenPort = retry.listenPort;
  // Do not wait for bind completion before requesting stop. Each exact-port
  // rebind after stop proves a late success cannot leave its listener alive.
  for (unsigned i = 0; i < 32; ++i) {
    ss = server.start(); ZuCheck(ss); server.listen();
    sstop = server.stop(); ZuCheck(sstop);
    rs = rival.start(); ZuCheck(rs); rival.listen();
    ZuCheck(retry.listening.timedwait(Zm::now(5)) == 0);
    ZuCheck(retry.listenFailures.load_() == 1);
    rstop = rival.stop(); ZuCheck(rstop);
  }
  rival.final(); server.final(); mx.stop();
  ZuCheck(!state.errors.load_() && !retry.errors.load_());
}
static void startupCleanup(unsigned mode) {
  ZuTestScope(startupCleanup);
  TestState state; state.mode = mode;
  SrvHub server{&state}; CliHub client{&state};
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(4)
      .thread(1, [](auto &t) { t.isolated(1); }).thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); }).thread(4, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  ZmAtomic<unsigned> errors{0};
  auto failInit = [&server, &client, &errors](auto sp, auto cp) {
    sp.errorFn([&errors](ZeException) { ++errors; });
    cp.errorFn([&errors](ZeException) { ++errors; });
    bool si = server.init(ZuMv(sp)), ci = client.init(ZuMv(cp));
    bool stopped = server.ZmEngine<SrvHub>::state() == ZmEngineState::Stopped &&
      client.ZmEngine<CliHub>::state() == ZmEngineState::Stopped;
    client.final(); server.final(); return !si && !ci && stopped;
  };
  ZuCheck(failInit(srvParams(nullptr), cliParams(nullptr)));
  ZuCheck(failInit(srvParams(&mx, "3", "3"), cliParams(&mx, "3", "3")));
  ZuCheck(failInit(srvParams(&mx, "9", "4"), cliParams(&mx, "9", "4")));
  ZuCheck(errors.load_() == 6);
#ifdef ZFIX_TEST_TLS
  // Fail after CA/certificate allocation, then explicitly finalize. No rollback
  // or implicit reinitialization policy is imposed by the FIX endpoint wrapper.
  for (unsigned i = 0; i < 2; ++i) {
    SessionString missing; missing << getenv("ZFIX_TEST_CERT") << ".missing";
    auto sp = srvParams(&mx); sp.keyPath(missing);
    auto cp = cliParams(&mx); cp.caPath(missing);
    ZuCheck(failInit(ZuMv(sp), ZuMv(cp)));
  }
#endif
  bool si = server.init(srvParams(&mx)), ci = client.init(cliParams(&mx)); ZuCheck(si && ci);
  bool started = mx.start(); ZuCheck(started); if (!started) return;
  bool ss = si && server.start(), cs = ci && client.start(); ZuCheck(ss && cs);
  server.listen(); ZuCheck(state.listening.timedwait(Zm::now(5)) == 0 && state.port);
  ZmRef<CliHub::Link> link = new CliHub::Link{&client};
#ifdef ZFIX_TEST_TLS
  link->connect("localhost", state.port, ZiIP{"127.0.0.1"});
#else
  link->connect("127.0.0.1", state.port);
#endif
  ZuCheck(state.done.timedwait(Zm::now(5)) == 0); link->logout();
  ZuCheck(state.drained.timedwait(Zm::now(5)) == 0);
  bool cstop = client.stop(), sstop = server.stop(); ZuCheck(cstop && sstop);
  client.final(); server.final(); mx.stop(); link = nullptr;
  ZuCheck(!state.errors.load_() && state.closed.load_() == 2 && state.snapshots.load_() == 4);
  ZuCheck(state.ready.load_() == 2 && state.admission.load_() == 2);
  ZuCheck(state.received.load_() == 2 && state.businessSent.load_() == 2);
}

// A raw peer accepts TCP and deliberately never answers the TLS handshake.
// Its lifetime is explicit; no read timeout or external network is involved.
class SetupSink : public ZmNHeap<SetupSink>, public ZiConnection {
friend inline ZuStringT<"Zfix.SessionIO.SetupSink"> ZmNHeapID(SetupSink *);
public:
  using ZmNHeap<SetupSink>::operator new;
  using ZmNHeap<SetupSink>::operator delete;

  SetupSink(ZiMultiplex *mx, const ZiCxnInfo &ci, ZmSemaphore *closed) :
    ZiConnection{mx, ci}, m_closed{closed} { }

  void connected(ZiRxContext &io) override { io.complete(); }

  void disconnected(bool) override { m_closed->post(); }
private:
  ZmSemaphore	*m_closed;
};

static void setupCleanup(unsigned mode) {
  ZuTestScope(setupCleanup);
  TestState state; state.mode = mode; state.setup = true;
  SrvHub server{&state}; CliHub client{&state};
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(5)
      .thread(1, [](auto &t) { t.isolated(1); }).thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); }).thread(4, [](auto &t) { t.isolated(1); })
      .thread(5, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  Net::ServerParams sp{&mx, "3", "4"}; Net::ClientParams cp{&mx, "3", "4"};
#ifdef ZFIX_TEST_TLS
  sp.certPath(getenv("ZFIX_TEST_CERT")).keyPath(getenv("ZFIX_TEST_KEY"));
  cp.caPath(getenv("ZFIX_TEST_CERT"));
#endif
  bool si = server.init(ZuMv(sp)), ci = client.init(ZuMv(cp)); ZuCheck(si && ci);
  bool started = mx.start(); ZuCheck(started); if (!started) return;
  bool ss = si && server.start(), cs = ci && client.start(); ZuCheck(ss && cs);
  server.listen(); ZuCheck(state.listening.timedwait(Zm::now(5)) == 0 && state.port);
  // A bound UDP socket with no responder makes DNS wait deterministically.
  auto dns = ::socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in addr{}; addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bool bound = !Zi::nullSocket(dns) && !::bind(dns, reinterpret_cast<sockaddr *>(&addr),
    sizeof(addr));
  ZuCheck(bound);
  socklen_t size = sizeof(addr);
  ZuCheck(!::getsockname(dns, reinterpret_cast<sockaddr *>(&addr), &size));
  Zi::Name servers; servers << "127.0.0.1:" << ZuBoxed(ntohs(addr.sin_port));
  ZiResolver::init(ZiResolverParams{}.ipv6(false).timeoutMS(200).tries(1).servers(servers));
  auto refused = ::socket(AF_INET, SOCK_STREAM, 0);
  addr.sin_port = 0;
  ZuCheck(!Zi::nullSocket(refused) && !::bind(refused, reinterpret_cast<sockaddr *>(&addr),
    sizeof(addr)));
  size = sizeof(addr);
  ZuCheck(!::getsockname(refused, reinterpret_cast<sockaddr *>(&addr), &size));
  unsigned refusedPort = ntohs(addr.sin_port);
  ZmRef<CliHub::Link> link = new CliHub::Link{&client};
  Progress progress;
  auto snapshot = [&client, &link, &progress] {
    return ZmBlock<bool>{}([&client, &link, &progress](auto wake) {
      client.rxRun([&client, &link, &progress, wake = ZuMv(wake)]() mutable {
	progress.epoch = link->session_().epoch_(); progress.rx = link->rxProgress();
	client.txRun([&link, &progress, wake = ZuMv(wake)]() mutable {
	  progress.tx = link->txProgress(); wake(true);
	});
      });
    });
  };
  auto restore = [&client, &link, &progress] {
    return ZmBlock<bool>{}([&client, &link, &progress](auto wake) {
      client.rxRun([&client, &link, &progress, wake = ZuMv(wake)]() mutable {
	bool ok = link->restoreRx(progress.epoch, progress.rx);
	client.txRun([&link, &progress, ok, wake = ZuMv(wake)]() mutable {
	  wake(link->restoreTx(progress.epoch, progress.tx) && ok);
	});
      });
    });
  };
  ZuCheck(snapshot());
  progress.rx.received = progress.rx.processed = 19;
  progress.tx.loaded = progress.tx.sent = progress.tx.acknowledged = 27;
  ZuCheck(restore());
  // Stop during numeric/DNS/socket setup. Submitted operations must finish
  // normally before the endpoint reports stopped; no setup deadline cancels them.
  for (unsigned i = 0; i < 6; ++i) {
    ZmSemaphore blocked, release, cancelled;
    if (i == 5) {
      mx.rxRun([&blocked, &release] { blocked.post(); release.wait(); });
      ZuCheck(blocked.timedwait(Zm::now(5)) == 0);
    }
    client.rxRun([link, &state, i, refusedPort] {
      state.socketGate = i == 5;
      link->connect(i == 2 || i == 3 ? "setup.test" : "127.0.0.1",
	i >= 4 ? refusedPort : state.port);
      if (i < 3) link->disconnect_(false);
    });
    if (i == 5) {
      ZuCheck(state.socketSubmitted.timedwait(Zm::now(5)) == 0);
      client.rxRun([link, &cancelled] { link->disconnect_(false); cancelled.post(); });
      ZuCheck(cancelled.timedwait(Zm::now(5)) == 0);
      ZuCheck(state.stoppedOne.trywait() != 0);
      release.post();
    }
    ZuCheck(state.stoppedOne.timedwait(Zm::now(3)) == 0);
    ZuCheck(snapshot());
    ZuCheck(progress.epoch == 1 && progress.rx.received == 19 && progress.rx.processed == 19);
    ZuCheck(progress.tx.loaded == 27 && progress.tx.sent == 27 && progress.tx.acknowledged == 27);
    ZuCheck(!state.ready.load_() && !state.admission.load_() && !link->lastLoad);
  }
#ifdef ZFIX_TEST_TLS
  for (unsigned i = 0; i < 2; ++i) {
    ZmSemaphore listening, accepted, closed;
    ZmRef<SetupSink> sink; unsigned port = 0;
    mx.listen([&listening, &port](const ZiListenInfo &info) {
      port = info.port; listening.post();
    }, [&listening](bool) { listening.post(); },
      [&mx, &sink, &closed, &accepted](const ZiCxnInfo &info) -> ZiConnection * {
	sink = new SetupSink{&mx, info, &closed}; accepted.post(); return sink;
      }, ZiIP{"127.0.0.1"}, 0, 1, server.rxAllocator());
    ZuCheck(listening.timedwait(Zm::now(5)) == 0 && port);
    link->connect("localhost", port, ZiIP{"127.0.0.1"});
    ZuCheck(accepted.timedwait(Zm::now(5)) == 0 && sink);
    ZuCheck(state.socketInstalled.timedwait(Zm::now(5)) == 0);
    if (i) link->disconnect();
    else if (sink) sink->close();
    ZuCheck(state.stoppedOne.timedwait(Zm::now(3)) == 0);
    ZuCheck(snapshot());
    ZuCheck(progress.rx.received == 19 && progress.tx.loaded == 27 && !state.ready.load_()
      && !link->lastLoad);
    if (sink) sink->close();
    ZuCheck(closed.timedwait(Zm::now(5)) == 0); sink = nullptr;
    mx.stopListening(ZiIP{"127.0.0.1"}, port);
  }
#endif
  ZuCheck(state.setupFailures.load_() == 4);
  // A later prepared owner completes normal Session::Logon/business/logout.
  progress.rx.received = progress.rx.processed = 1;
  progress.tx.loaded = progress.tx.sent = progress.tx.acknowledged = 1;
#ifdef ZFIX_TEST_TLS
  // TLS socket association now allocates its FIX connection before auth.
  // Restoring this used owner must fail; cold restore uses a fresh owner after
  // the preceding setup/owner stop obligations have all completed.
  ZuCheck(!restore());
  link = nullptr; link = new CliHub::Link{&client};
#endif
  ZuCheck(restore());
  client.rxRun([link, &state, refusedPort] {
    state.socketGate = false; state.reconn = 1;
    link->connect("localhost", refusedPort);
  });
  ZuCheck(state.stoppedOne.timedwait(Zm::now(5)) == 0);
  // The second failure is an automatic native attempt on the same owner.
  ZuCheck(state.stoppedOne.timedwait(Zm::now(5)) == 0);
  ZuCheck(state.setupFailures.load_() == 6);
  ZuCheck(!state.ready.load_() && !link->lastLoad);
  bool stopped = server.stop(); ZuCheck(stopped);
  Zi::closeSocket(refused);
  state.listenPort = refusedPort;
  bool restarted = server.start(); ZuCheck(restarted);
  server.listen();
  ZuCheck(state.listening.timedwait(Zm::now(5)) == 0 && state.port == refusedPort);
  unsigned earlier = state.closed.load_();
  // The next automatic attempt reaches the new listener and exchanges FIX.
  ZuCheck(state.done.timedwait(Zm::now(5)) == 0); link->logout();
  ZuCheck(state.stoppedOne.timedwait(Zm::now(5)) == 0);
  ZuCheck(state.stoppedOne.timedwait(Zm::now(5)) == 0);
  ZuCheck(state.closed.load_() == earlier + 2 && state.ready.load_() == 2
    && state.received.load_() == 2);
  ZuCheck(!state.errors.load_());
  bool cstop = client.stop(), sstop = server.stop(); ZuCheck(cstop && sstop);
  client.final(); server.final(); ZiResolver::stop(); ZiResolver::final();
  Zi::closeSocket(dns); mx.stop(); link = nullptr;
}
struct AdminGate {
  ZmSemaphore	entered, release, done;

  void block() { entered.post(); release.wait(); }
};

// Public history completion admission is independent of application history
// retention. Repeated stale responses still need bounded owner handoffs.
static void reloadBudget(bool local) {
  ZuTestScope(reloadBudget);
  TestState state; state.heartbeat = 0; state.scenario = Scenario::PostedOverflow;
  HubLimits limits;
  // Between the jumbo response's serialized bytes and heap cache class.
  if (local) limits.link.postedAdminBytes = 24 * 1024;
  CliHub client{&state, limits}; SrvHub server{&state};
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(6)
      .thread(1, [](auto &t) { t.isolated(1); }).thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); }).thread(4, [](auto &t) { t.isolated(1); })
      .thread(5, [](auto &t) { t.isolated(1); }).thread(6, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  bool si = server.init(srvParams(&mx)), ci = client.init(cliParams(&mx, "5", "6"));
  ZuCheck(si && ci);
  bool started = mx.start(); ZuCheck(started); if (!started) return;
  bool ss = si && server.start(), cs = ci && client.start(); ZuCheck(ss && cs);
  server.listen(); ZuCheck(state.listening.timedwait(Zm::now(5)) == 0 && state.port);
  ZmRef<CliHub::Link> link = new CliHub::Link{&client};
#ifdef ZFIX_TEST_TLS
  link->connect("localhost", state.port, ZiIP{"127.0.0.1"});
#else
  link->connect("127.0.0.1", state.port);
#endif
  ZuCheck(state.done.timedwait(Zm::now(5)) == 0 && state.received.load_() == 2);
  PostedState posted, hub;
  bool clientStopped = false;
  auto sample = [&client, &link, &posted, &hub, &clientStopped] {
    ZmBlock<>{}([&client, &link, &posted, &hub, &clientStopped](auto wake) {
      client.rxRun([&client, &link, &posted, &hub, &clientStopped,
	  wake = ZuMv(wake)]() mutable {
	posted = link->adminPosted(); hub = client.fixAdminRx().state_();
	clientStopped = link->stopReports != 0; wake();
      });
    });
  };
  auto settle = [&sample, &posted, &hub] {
    auto deadline = Zm::now(5);
    do { sample(); if (!posted.count && !hub.count) return true; } while (Zm::now() < deadline);
    return false;
  };
  ZuCheck(settle());
  ZtString text{16 * 1024, 16 * 1024 + 1};
  memset(text.data(), 'x', text.length());
  Fmt::Error error;
  TxHistory history{TxBody::encode<OrderMsg>(Order{text}, 999, error), {}, 1, 1};
  ZuCheck(!error && history.body && !history.body->builtin());
  auto original = history.body;
  ZuCheck(!link->reloaded(UINT64_MAX, history)); // main is not an owner
  bool nativeRefused = false;
  ZmBlock<>{}([&mx, &link, &history, &nativeRefused](auto wake) {
    mx.run([&link, &history, &nativeRefused, wake = ZuMv(wake)]() mutable {
      nativeRefused = !link->reloaded(UINT64_MAX, history); wake();
    }, 1);
  }); ZuCheck(nativeRefused && history.body == original);
  bool accepted = false, empty = true, tuned = true;
  ZmBlock<>{}([&client, &link, &history, &accepted, &empty, &tuned, local](auto wake) {
    client.rxRun([&client, &link, &history, &accepted, &empty, &tuned, local,
      wake = ZuMv(wake)]() mutable {
      if (!local) tuned = client.fixAdminRx().limits_({0});
      accepted = link->reloaded(UINT64_MAX, history);
      empty = link->reloaded(UINT64_MAX, {}); wake();
    });
  });
  ZuCheck(tuned && !accepted && empty == local && history.body == original);
  ZuCheck(settle()); sample();
  ZuCheck(posted.refused == unsigned(local ? 1 : 2) && hub.refused == posted.refused);
  ZuCheck(!state.failures.load_() && !state.overloadEvents.load_() && !state.closed.load_());
  if (!local) {
    ZmBlock<>{}([&client](auto wake) {
      client.rxRun([&client, wake = ZuMv(wake)]() mutable {
	client.fixAdminRx().limits_({PostedLimits{}.bytes}); wake();
      });
    });
    AdminGate tx; client.txRun([&tx] { tx.block(); });
    ZuCheck(tx.entered.timedwait(Zm::now(5)) == 0);
    ZmBlock<>{}([&client, &link, &history, &accepted](auto wake) {
      client.rxRun([&link, &history, &accepted, wake = ZuMv(wake)]() mutable {
	accepted = link->reloaded(UINT64_MAX, history); wake();
      });
    }); ZuCheck(accepted);
    sample(); auto one = posted;
    ZuCheck(one.count == 1 && one.bytes > history.body->memory() && one.bytes == hub.bytes);
    bool refused = false;
    ZmBlock<>{}([&client, &link, &history, &refused, &tuned, one](auto wake) {
      client.rxRun([&client, &link, &history, &refused, &tuned, one,
	wake = ZuMv(wake)]() mutable {
	tuned = client.fixAdminRx().limits_({one.bytes});
	refused = !link->reloaded(UINT64_MAX, history); wake();
      });
    }); ZuCheck(tuned && refused && history.body == original);
    sample(); ZuCheck(posted.bytes == one.bytes && posted.count == 1 && posted.refused == 3);
    AdminGate rx; PostedState returning;
    client.rxRun([&link, &rx, &returning] {
      rx.entered.post(); rx.release.wait(); returning = link->adminPosted(); rx.done.post();
    });
    ZuCheck(rx.entered.timedwait(Zm::now(5)) == 0);
    tx.release.post();
    ZmBlock<>{}([&client](auto wake) { client.txRun([wake = ZuMv(wake)]() mutable { wake(); });
      });
    rx.release.post(); ZuCheck(rx.done.timedwait(Zm::now(5)) == 0);
    ZuCheck(returning.count == 1 && returning.bytes == one.bytes);
    ZuCheck(settle());
    // Reuse the exact returned reservation and close while Tx is held.
    AdminGate closing; client.txRun([&closing] { closing.block(); });
    ZuCheck(closing.entered.timedwait(Zm::now(5)) == 0);
    ZmBlock<>{}([&client, &link, &history, &accepted](auto wake) {
      client.rxRun([&link, &history, &accepted, wake = ZuMv(wake)]() mutable {
	accepted = link->reloaded(UINT64_MAX, history); wake();
      });
    }); ZuCheck(accepted);
    link->disconnect();
    // Only client Tx is held. The peer may finish its independent close first;
    // the client must retain this reservation until its own owner drain.
    sample();
    ZuCheck(posted.count == 1 && posted.bytes == one.bytes && !clientStopped,
      log_("count=", posted.count, " bytes=", posted.bytes, " expected=", one.bytes,
	" clientStopped=", clientStopped, " peerCloses=", state.closed.load_()));
    closing.release.post();
  } else link->disconnect();
  ZuCheck(state.drained.timedwait(Zm::now(5)) == 0);
  ZuCheck(settle());
  bool lateRefused = false, direct = false;
  ZmBlock<>{}([&client, &link, &history, &lateRefused, &direct](auto wake) {
    client.rxRun([&client, &link, &history, &lateRefused, &direct, wake = ZuMv(wake)]() mutable {
      lateRefused = !link->reloaded(UINT64_MAX, history);
      client.txRun([&link, &history, &direct, wake = ZuMv(wake)]() mutable {
	direct = link->reloaded(UINT64_MAX, history); wake();
      });
    });
  });
  ZuCheck(lateRefused && direct && history.body == original);
  bool cstop = client.stop(), sstop = server.stop(); ZuCheck(cstop && sstop);
  client.final(); server.final(); mx.stop(); link = nullptr;
  ZuCheck(!state.errors.load_() && state.closed.load_() == 2);
}
static void adminBudget(unsigned kind) {
  ZuTestScope(adminBudget);
  TestState state; state.scenario = Scenario::PostedOverflow;
  state.heartbeat = 0; state.adminTest = true; state.cutReplay = kind == 4;
  HubLimits cliLimits;
  if (kind == 0) cliLimits.link.postedAdminBytes = 0;
  if (kind == 1 || kind == 8) cliLimits.admin.bytes = 0;
  TestState other; other.scenario = Scenario::PostedOverflow;
  other.heartbeat = 0; other.adminTest = true; other.lane = 1;
  CliHub client{&state, cliLimits}; SrvHub server{&state};
  if (kind == 6) { server.routes << &state << &other; }
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(6)
      .thread(1, [](auto &t) { t.isolated(1); }).thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); }).thread(4, [](auto &t) { t.isolated(1); })
      .thread(5, [](auto &t) { t.isolated(1); }).thread(6, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  bool si = server.init(srvParams(&mx)), ci = client.init(cliParams(&mx, kind == 8 ? "1" : "5",
    kind == 8 ? "2" : "6")); ZuCheck(si && ci);
  bool started = mx.start(); ZuCheck(started); if (!started) return;
  bool ss = si && server.start(), cs = ci && client.start(); ZuCheck(ss && cs);
  server.listen(); ZuCheck(state.listening.timedwait(Zm::now(5)) == 0 && state.port);
  ZmRef<CliHub::Link> link = new CliHub::Link{&client}, rival;
#ifdef ZFIX_TEST_TLS
  link->connect("localhost", state.port, ZiIP{"127.0.0.1"});
#else
  link->connect("127.0.0.1", state.port);
#endif
  if (kind < 2 || kind == 8) {
    ZuCheck(state.drained.timedwait(Zm::now(5)) == 0);
    PostedState local, hub; TxProgress p;
    ZmBlock<>{}([&client, &link, &local, &hub](auto wake) {
      client.rxRun([&client, &link, &local, &hub, wake = ZuMv(wake)]() mutable {
	local = link->adminPosted(); hub = client.fixAdminRx().state_(); wake();
      });
    });
    ZmBlock<>{}([&client, &link, &p](auto wake) {
      client.txRun([&link, &p, wake = ZuMv(wake)]() mutable { p = link->txProgress(); wake(); });
    });
    ZuCheck(!local.bytes && !local.count && !local.peak && local.refused == 1);
    ZuCheck(!hub.bytes && !hub.count && !hub.peak && hub.refused == 1);
    ZuCheck(!state.ready.load_() && !state.received.load_() && !link->lastLoad);
    ZuCheck(p.loaded == 1 && p.sent == 1 && p.acknowledged == 1);
  } else {
    ZuCheck(state.done.timedwait(Zm::now(5)) == 0 && state.received.load_() == 2);
    if (kind == 6) {
      rival = new CliHub::Link{&client, &other, ZuID{"fixSecond"}};
#ifdef ZFIX_TEST_TLS
      rival->connect("localhost", state.port, ZiIP{"127.0.0.1"});
#else
      rival->connect("127.0.0.1", state.port);
#endif
      ZuCheck(other.done.timedwait(Zm::now(5)) == 0 && other.received.load_() == 2);
    }
    PostedState local, hub; RxStats rx; TxStats tx, otherTx;
    auto sampleRx = [&server, &state, &local, &hub, &rx] {
      ZmBlock<>{}([&server, &state, &local, &hub, &rx](auto wake) {
	server.rxRun([&server, &state, &local, &hub, &rx, wake = ZuMv(wake)]() mutable {
	  local = state.adminSample(); hub = server.fixAdminRx().state_();
	  rx = state.rxSample(); wake();
	});
      });
    };
    auto settle = [&server, &state, &other, &sampleRx, &local, &hub, &tx, &otherTx, kind] {
      auto deadline = Zm::now(5);
      do {
	ZmBlock<>{}([&server, &state, &other, &tx, &otherTx, kind](auto wake) {
	  server.txRun([&state, &other, &tx, &otherTx, kind, wake = ZuMv(wake)]() mutable {
	    tx = state.adminTx(); if (kind == 6) otherTx = other.adminTx(); wake();
	  });
	});
	sampleRx();
	if (!local.count && !hub.count && !tx.wire.queuedCount && !tx.inFlight
	  && !tx.controlBytes &&
	    !otherTx.wire.queuedCount && !otherTx.inFlight && !otherTx.controlBytes) return true;
      } while (Zm::now() < deadline);
      return false;
    };
    ZuCheck(settle());
    if (kind == 7) {
      bool tuned = false;
      ZmBlock<>{}([&server, &tuned](auto wake) {
	server.rxRun([&server, &tuned, wake = ZuMv(wake)]() mutable {
	  tuned = server.fixAdminRx().limits_({0}); wake();
	});
      }); ZuCheck(tuned);
      link->disconnect(); ZuCheck(state.drained.timedwait(Zm::now(5)) == 0);
      sampleRx();
      ZuCheck(!local.bytes && !local.count && local.refused == 1 && hub.refused == 1);
      ZuCheck(state.failures.load_() == 2 && state.downEvents.load_() == 2);
    } else {
      auto initial = rx.frames;
      Session::Msg control; control.p<Session::HeartBt>(Session::HeartBt{});
      Fmt::Error error; auto body = TxBody::control(control, error); ZuCheck(body && !error);
      unsigned size = body->size;
      if (size < SrvHub::Link::RxBufAlloc::Size) size = SrvHub::Link::RxBufAlloc::Size;
      ZtString text(size + 1, size + 2);
      memset(text.data(), 'x', text.length());
      switch (kind) {
	case 2: case 6: control.p<Session::TestReq>(Session::TestReq{.testReqID = text}); break;
	case 3: control.p<Session::Reject>(Session::Reject{.refSeqNo = 2,
	  .refTagID = 11, .reason = 5,
	  .text = text}); break;
	case 4:
	  control.p<Session::ResendReq>(Session::ResendReq{
	    .beginSeqNo = 1, .endSeqNo = 1}); break;
	default:
	  control.p<Session::ResendReq>(Session::ResendReq{
	    .beginSeqNo = 2, .endSeqNo = 1}); break;
      }
      body = TxBody::control(control, error); ZuCheck(body && !error);
      auto inject = [&client, &link, &rival, &body, kind](unsigned ordinal, bool second = false) {
	auto peer = second ? rival : link;
	client.txRun([link = ZuMv(peer), body, ordinal, kind] {
	  auto &state = *link->test;
	  Fmt::Error error;
	  auto cfg = txConfig(config(true, &state));
	  TxContext c{.seqNo = link->lastLoad + ordinal, .sendingTime = ZuDateTime{Zm::now()}};
	  auto frame = txFrame(link->fixAllocTx(0), *body, cfg, c, error); unsigned head = 0;
#ifdef ZFIX_TEST_TLS
	  head = link->template txHeadRoom<false>();
#endif
	  auto buf = link->fixAllocTx(head);
	  if (error || !frame || !buf || !buf->append(frame->cspan())) { state.fail(); return; }
	  if (kind == 5 && ordinal == 3) {
	    // Two complete frames in one native write/TLS record. Overload on the
	    // first must stop parsing the second, even before physical down arrives.
	    ++c.seqNo; frame = txFrame(link->fixAllocTx(0), *body, cfg, c, error);
	    if (error || !frame || !buf->append(frame->cspan())) { state.fail(); return; }
	  }
	  if (!link->sendRecord(ZuMv(buf))) state.fail();
	});
      };
      auto received = [&sampleRx, &rx, &local, &hub, initial](unsigned n) {
	auto deadline = Zm::now(5);
	do { sampleRx(); if (rx.frames >= initial + n) return true;
	  } while (Zm::now() < deadline);
	log("# wait frames=", rx.frames, " initial=", initial, " n=", n, " work=", local.count,
	  " bytes=", local.bytes, " refused=", local.refused, " hub=", hub.bytes);
	return false;
      };
      bool separated = false;
      ZmBlock<>{}([&server, &separated](auto wake) {
	server.rxRun([&server, &separated, wake = ZuMv(wake)]() mutable {
	  separated = server.fixSubmitRx().limits_({0}); wake();
	});
      }); ZuCheck(separated);
      TxProgress baseline;
      ZmBlock<>{}([&server, &state, &baseline](auto wake) {
	server.txRun([&state, &baseline, wake = ZuMv(wake)]() mutable {
	  baseline = state.adminProgress(); wake();
	});
      });
      AdminGate first; server.txRun([&first] { first.block(); });
      ZuCheck(first.entered.timedwait(Zm::now(5)) == 0);
      inject(1); ZuCheck(received(1));
      auto one = local;
      ZuCheck(one.count && one.bytes && one.bytes == hub.bytes && one.count == hub.count);
      if (kind == 2 || kind == 6) ZuCheck(one.bytes > body->memory());
      if (kind == 3) ZuCheck(one.bytes > body->body().length());
      if (kind == 4) {
	ZuCheck(state.done.timedwait(Zm::now(5)) == 0);
	sampleRx();
	ZuCheck(state.replayCut.count && local.count == state.replayCut.count &&
	  local.bytes == state.replayCut.bytes && local.refused == 1);
	ZuCheck(hub.bytes == local.bytes && hub.refused == 1 && state.closed.load_() < 2);
	first.release.post(); ZuCheck(state.drained.timedwait(Zm::now(5)) == 0);
	sampleRx(); ZuCheck(!local.bytes && !local.count && !hub.bytes && !hub.count);
	TxProgress after;
	ZmBlock<>{}([&server, &state, &after](auto wake) {
	  server.txRun([&state, &after, wake = ZuMv(wake)]() mutable {
	    after = state.adminProgress(); wake();
	  });
	});
	ZuCheck(after.loaded == baseline.loaded);
      } else {
	// Work may finish on Tx while its credit return waits behind Rx work.
	// Both observations stay on Rx; the ledger must remain reserved until
	// that owner can actually consume the return.
	AdminGate refund; PostedState returning;
	server.rxRun([&state, &refund, &returning] {
	  refund.entered.post(); refund.release.wait();
	  returning = state.adminSample(); refund.done.post();
	});
	ZuCheck(refund.entered.timedwait(Zm::now(5)) == 0);
	first.release.post();
	ZmBlock<>{}([&server](auto wake) {
	  server.txRun([wake = ZuMv(wake)]() mutable { wake(); });
	});
	refund.release.post(); ZuCheck(refund.done.timedwait(Zm::now(5)) == 0);
	ZuCheck(returning.count == one.count && returning.bytes == one.bytes);
	ZuCheck(settle());
	// Reuse exactly one packet's observed work reservation, then refuse the
	// next packet before it adds a second retained command to the Tx owner.
	bool tuned = false;
	ZmBlock<>{}([&server, &tuned, one](auto wake) {
	  server.rxRun([&server, &tuned, one, wake = ZuMv(wake)]() mutable {
	    tuned = server.fixAdminRx().limits_({one.bytes}); wake();
	  });
	}); ZuCheck(tuned);
	TxProgress before, after, rivalBefore, rivalAfter;
	ZmBlock<>{}([&server, &state, &other, &before, &rivalBefore, kind](auto wake) {
	  server.txRun([&state, &other, &before, &rivalBefore, kind,
	    wake = ZuMv(wake)]() mutable {
	    before = state.adminProgress();
	    if (kind == 6) rivalBefore = other.adminProgress(); wake();
	  });
	});
	AdminGate second; server.txRun([&second] { second.block(); });
	ZuCheck(second.entered.timedwait(Zm::now(5)) == 0);
	inject(2); ZuCheck(received(2));
	auto held = local;
	ZuCheck(held.bytes == one.bytes && held.count == one.count && !held.refused);
	auto &victim = kind == 6 ? other : state;
	inject(kind == 6 ? 1 : 3, kind == 6);
	ZuCheck(victim.done.timedwait(Zm::now(5)) == 0);
	sampleRx();
	ZuCheck(local.bytes == held.bytes && local.count == held.count
	  && local.refused == unsigned(kind != 6));
	ZuCheck(hub.bytes == held.bytes && hub.refused == 1 && local.peak == held.peak);
	ZuCheck(victim.closed.load_() < 2); // server Tx still owns admitted work
	second.release.post();
	ZuCheck(victim.drained.timedwait(Zm::now(5)) == 0);
	if (kind == 6) {
	  ZuCheck(settle());
	  ZuCheck(!state.closed.load_() && !state.overloadEvents.load_());
	  inject(3); ZuCheck(received(3)); ZuCheck(settle());
	  link->disconnect(); ZuCheck(state.drained.timedwait(Zm::now(5)) == 0);
	}
	sampleRx(); ZuCheck(!local.bytes && !local.count && !hub.bytes && !hub.count);
	ZuCheck(!rx.pendingBytes);
	if (kind == 5) ZuCheck(rx.frames == initial + 3 && !rx.framingErrors);
	ZmBlock<>{}([&server, &state, &other, &after, &rivalAfter, kind](auto wake) {
	  server.txRun([&state, &other, &after, &rivalAfter, kind, wake = ZuMv(wake)]() mutable {
	    after = state.adminProgress();
	    if (kind == 6) rivalAfter = other.adminProgress(); wake();
	  });
	});
	if (kind == 6) {
	  ZuCheck(after.loaded <= before.loaded + 2 && rivalAfter.loaded == rivalBefore.loaded);
	  PostedState refused;
	  ZmBlock<>{}([&server, &other, &refused](auto wake) {
	    server.rxRun([&other, &refused, wake = ZuMv(wake)]() mutable {
	      refused = other.adminSample(); wake();
	    });
	  });
	  ZuCheck(!refused.count && !refused.bytes && refused.refused == 1);
	} else if (kind == 2 || kind == 5) ZuCheck(after.loaded <= before.loaded + 1);
	else ZuCheck(after.loaded == before.loaded);
      }
    }
    state.adminSample = {}; state.adminTx = {}; state.rxSample = {}; state.adminProgress = {};
  }
  auto &failed = kind == 6 ? other : state;
  ZuCheck(failed.overloadEvents.load_() == unsigned(kind != 7) && failed.closed.load_() == 2);
  if (kind == 6) {
    ZuCheck(!state.overloadEvents.load_() && other.snapshots.load_() == 4
      && !other.errors.load_());
    other.adminSample = {}; other.adminTx = {}; other.rxSample = {}; other.adminProgress = {};
  }
  ZuCheck(state.snapshots.load_() == 4);
  bool cstop = client.stop(), sstop = server.stop(); ZuCheck(cstop && sstop);
  client.final(); server.final(); mx.stop(); link = rival = nullptr;
  ZuCheck(!state.errors.load_());
}
static void readAheadOverload() {
  ZuTestScope(readAheadOverload);
  TestState state; state.scenario = Scenario::PostedOverflow; state.trace = true;
  HubLimits limits;
  {
    WireTrace sizing{TraceLimits{.bytes = UINT64_MAX}};
    auto record = sizing.capture_(ZuCSpan{}, {});
    ZuCheck(record);
    if (!record) return;
    // Admit two actual records, including the pool and jumbo size classes.
    limits.link.traceRx.bytes = limits.link.traceTx.bytes = 2 * record->data().charge;
  }
  CliHub client{&state, limits};
  limits.link.rx.readAheadCount = 0;
  SrvHub server{&state, limits};
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(5)
      .thread(1, [](auto &t) { t.isolated(1); }).thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); }).thread(4, [](auto &t) { t.isolated(1); })
      .thread(5, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  bool si = server.init(srvParams(&mx)), ci = client.init(cliParams(&mx)); ZuCheck(si && ci);
  bool started = mx.start(); ZuCheck(started); if (!started) return;
  bool ss = si && server.start(), cs = ci && client.start(); ZuCheck(ss && cs);
  server.listen(); ZuCheck(state.listening.timedwait(Zm::now(5)) == 0 && state.port);
  ZmRef<CliHub::Link> link = new CliHub::Link{&client};
#ifdef ZFIX_TEST_TLS
  link->connect("localhost", state.port, ZiIP{"127.0.0.1"});
#else
  link->connect("127.0.0.1", state.port);
#endif
  ZuCheck(state.done.timedwait(Zm::now(5)) == 0);
  // Refusal at the public handoff must not invoke Tx selection or reserve a
  // sequence. The retained handle remains available for a later accepted post.
  Fmt::Error refusedError;
  auto refusedBody = TxBody::encode<OrderMsg>(Order{"req", 0}, 300, refusedError);
  ZuCheck(refusedBody && !refusedError && !link->submit(refusedBody));
  ZuCheck(!link->send<Lane0>(Order{"req", 0}, 300, refusedError) &&
    refusedError.code == Fmt::ErrorCode::Output);
  bool refused = false;
  PostedState submitState;
  TxProgress before, after;
  ZmBlock<>{}([&client, &link, &before](auto wake) {
    client.txRun([&link, &before, wake = ZuMv(wake)]() mutable {
      before = link->txProgress(); wake();
    });
  });
  ZmBlock<>{}([&client, &link, &refused, &submitState](auto wake) {
    client.rxRun([&client, &link, &refused, &submitState,
	wake = ZuMv(wake)]() mutable {
      auto &budget = client.fixSubmitRx();
      bool tuned = budget.limits_({0});
      Fmt::Error error;
      refused = tuned && !link->send<Lane0>(Order{"req", 0}, 300, error) &&
	error.code == Fmt::ErrorCode::Output;
      submitState = budget.state_(); budget.limits_({PostedLimits{}.bytes}); wake();
    });
  });
  ZmBlock<>{}([&client, &link, &after](auto wake) {
    client.txRun([&link, &after, wake = ZuMv(wake)]() mutable {
      after = link->txProgress(); wake();
    });
  });
  ZuCheck(refused && refusedBody && !submitState.bytes && !submitState.count
    && submitState.refused == 1);
  ZuCheck(before.loaded == after.loaded && before.sent == after.sent &&
    before.acknowledged == after.acknowledged && state.received.load_() == 2);
  // Exercise rotation with an explicit application watermark exchange after
  // the two-record Logon/business baseline, waiting for its actual echo.
  link->probe();
  ZuCheck(state.done.timedwait(Zm::now(5)) == 0);
  Fmt::Error error; auto body = TxBody::encode<OrderMsg>(Order{"req", 0}, 301, error);
  ZuCheck(body && !error);
  // Inject one valid but high-sequence FIX frame below managed publication.
  // The zero hub read-ahead count must refuse it before business dispatch.
  client.txRun([link, body = ZuMv(body), &state] {
    Fmt::Error error;
    auto cfg = txConfig(config(true, &state));
    TxContext c{.seqNo = link->txProgress().loaded + 1, .sendingTime = ZuDateTime{Zm::now()}};
    auto frame = txFrame(link->fixAllocTx(0), *body, cfg, c, error);
    unsigned head = 0;
#ifdef ZFIX_TEST_TLS
    head = link->template txHeadRoom<false>();
#endif
    auto buf = link->fixAllocTx(head);
    if (error || !frame || !buf || !buf->append(frame->cspan()) ||
	  !link->sendRecord(ZuMv(buf))) state.fail();
  });
  ZuCheck(state.drained.timedwait(Zm::now(5)) == 0);
  ZuCheck(state.overloadEvents.load_() == 1);
  ZuCheck(state.received.load_() == 2 && state.closed.load_() == 2
    && state.snapshots.load_() == 4);
  RxStats rx; TxStats tx; TraceStats rxTrace, txTrace;
  ZmBlock<>{}([&client, &link, &rx, &rxTrace](auto wake) {
    client.rxRun([&link, &rx, &rxTrace, wake = ZuMv(wake)]() mutable {
      rx = link->rxStats(); rxTrace = link->rxTraceStats(); wake();
    });
  });
  ZmBlock<>{}([&client, &link, &tx, &txTrace](auto wake) {
    client.txRun([&link, &tx, &txTrace, wake = ZuMv(wake)]() mutable {
      tx = link->txStats(); txTrace = link->txTraceStats(); wake();
    });
  });
  ZuCheck(rx.frames > 0 && rx.bytes > 0 && !rx.pendingBytes);
  ZuCheck(tx.wire.admitted == tx.wire.completed + tx.wire.failed && !tx.wire.queuedCount
    && !tx.inFlight);
  ZuCheck(tx.wire.bytes > 0 && !tx.loadRefused);
  ZuCheck(state.traceLogons.load_() == 4 && state.traceRx.load_() > 4
    && state.traceTx.load_() > 4);
  ZuCheck(rxTrace.records > 0 && txTrace.records > 0 && txTrace.rotated > 0);
  ZuCheck(rxTrace.bytes <= limits.link.traceRx.bytes
    && txTrace.bytes <= limits.link.traceTx.bytes);
  bool cstop = client.stop(), sstop = server.stop(); ZuCheck(cstop && sstop);
  client.final(); server.final(); mx.stop(); link = nullptr;
  ZuCheck(!state.errors.load_());
}
// Exercise the real sequence receiver, timer queue and owner drain. Link's
// private timer dispatch is not exposed here. Before workers start, the open
// scheduler ring deterministically holds an already-due raw-owner callback.
struct GapDrainState {
  ZmSemaphore	done;
  unsigned	stage = 0, callbacks = 0, requests = 0, retries = 0;
  unsigned	arms = 0, cancels = 0, processed = 0, errors = 0;
};

struct GapDrainOwner : public ZmNHeap<GapDrainOwner>, public ZmObject,
    public SeqRx<GapDrainOwner, RxQueue> {
friend inline ZuStringT<"Zfix.SessionIOTest.Gap"> ZmNHeapID(GapDrainOwner *);
  ZmScheduler		&scheduler;
  GapDrainState		&state;
  ZmScheduler::Timer	gap;
  ZmRef<GapDrainOwner>	teardown;

  GapDrainOwner(ZmScheduler &s, GapDrainState &t) : scheduler{s}, state{t} { }

  ~GapDrainOwner() {
    if (state.stage != 3 || !scheduler.invoked(1)) ++state.errors;
    state.stage = 4;
    state.done.post();
  }

  void processRx(Msg *) { ++state.processed; }

  void requestRx(Span, Span) { ++state.requests; }

  void reRequestRx(Span) { ++state.retries; }

  void postRx(uint64_t ticket) {
    scheduler.run([this, ticket] { drain_(ticket); }, 1);
  }

  void armGap(uint64_t ticket) {
    ++state.arms;
    scheduler.add(&gap, Zm::now(), ZmScheduler::Update,
      [this, ticket](auto &&arm) {
	return arm([this, ticket] {
	  if (state.stage || !held_() || !scheduler.invoked(1)) ++state.errors;
	  ++state.callbacks;
	  state.stage = 1;
	  retry_(ticket);
	  if (expected_() != 5 || received_() != 10 || retained_()) ++state.errors;
	});
      }, 1);
  }

  void cancelGap() { ++state.cancels; scheduler.del(&gap); }

  void stop_() {
    if (!scheduler.invoked(1)) ++state.errors;
    hold_();
    scheduler.run([this] {
      if (state.stage != 1 || !scheduler.invoked(1)) ++state.errors;
      state.stage = 2;
      scheduler.run([this] {
	if (state.stage != 2 || !scheduler.invoked(2)) ++state.errors;
	state.stage = 3;
	scheduler.run([this] { teardown = nullptr; }, 1);
      }, 2);
    }, 1);
  }
};

static void gapTimerDrain()
{
  ZuTestScope(gapTimerDrain);
  GapDrainState state;
  ZmScheduler scheduler{ZmSchedParams{}.nThreads(2)
    .thread(1, [](auto &t) { t.isolated(1); })
    .thread(2, [](auto &t) { t.isolated(1); })};
  ZmRef<GapDrainOwner> owner = new GapDrainOwner{scheduler, state};
  // Cold restoration has one owner before workers exist. Queue shutdown first;
  // its Rx turn cancels the timer after its callback is already in the ring.
  owner->hold_();
  ZuCheck(owner->restore_(10, 5));
  owner->teardown = owner;
  auto raw = owner.ptr();
  scheduler.run([raw] { raw->stop_(); }, 1);
  auto stopBytes = scheduler.count_();
  owner->resume_();
  ZuCheck(state.requests == 1 && state.arms == 1 && !state.callbacks &&
    !state.stage && stopBytes && scheduler.count_() > stopBytes);
  owner = nullptr;
  ZuCheck(state.done.trywait() != 0);
  bool started = scheduler.start();
  ZuCheck(started);
  if (!started) { raw->teardown = nullptr; return; }
  ZuCheck(state.done.timedwait(Zm::now(5)) == 0);
  ZuCheck(scheduler.stop());
  ZuCheck(state.stage == 4 && state.callbacks == 1 && state.requests == 1 &&
    !state.retries && state.arms == 1 && state.cancels == 1 &&
    !state.processed && !state.errors);
}
struct OwnSrvLink;
struct OwnCliLink;
struct OwnState {
  const unsigned	mode;
  const bool		established, preauth, terminal, chain, independent;
  ZmSemaphore		listening, admission, bound, business, oldClosed, physicalOne,
    stoppedA, stoppedB, stoppedServer, workReady, loadStarted, authenticated,
    readyB, lateAccepted, clientLogon, physicalAny, stoppedMiddle, prefix;
  ZmAtomic<unsigned>	errors{0}, accepted{0}, logical{0}, physicalOpen{0}, physicalClosed{0},
    serverStops{0}, aReady{0}, bReady{0}, bStarted{0}, served{0}, received{0}, oldDecisions{0},
    loadPublished{0}, workReplied{0};
  unsigned		port = 0;
  uint64_t		bLogon = 0; // client Tx; published before business semaphore
  AdmitFn		admit; // server AppRx
  ZmRef<OwnSrvLink>	serverLink; // native Rx; released after hub workers drain
  ZmFn<>		oldClose; // native Rx; retains A's physical close obligation
  bool			holdOld = true; // native Rx
  TxContext		loadContext; // one Tx offer, published by loadStarted
  AdminGate		stopGate;

  explicit OwnState(unsigned m, bool live = false,
      bool socketOnly = false, bool stopping = false, bool chained = false,
      bool separate = false) :
    mode{m}, established{live}, preauth{socketOnly}, terminal{stopping},
    chain{chained}, independent{separate} { }

  unsigned connection() const { return independent ? 1U : preauth || chain ? 3U : 2U; }

  void fail(const char *file = __builtin_FILE(), unsigned line = __builtin_LINE()) {
    log("# replacement failure ", file, ":", line, " mode=", mode);
    ++errors;
  }
};

static SessionConfig ownConfig(bool client, const OwnState *s)
{
  SessionConfig c; c.mode = s->mode; c.initiator = client;
  c.heartBtInt = 0; c.appVerID = AppVersion::V5_0_SP2;
  c.identity.version = c.mode == Transport::FIXT11 ? Version::VT1_1 : Version::V4_4;
  c.identity.local = client ? "CLIENT" : "SERVER";
  c.identity.peer = client ? "SERVER" : "CLIENT";
  return c;
}

// The application retains exactly this fixture's two business rounds. Missing
// old administrative history remains unavailable and is explicitly gap-filled.
struct OwnOutput {
  enum { Calls = 2 };
  PendingQueue		pending;
  Items<TxHistory>	history;

  bool enqueue(TxBody::Ref body) {
    if (pending.count_() >= Calls) return false;
    pending.push(ZuMv(body)); return true;
  }

  TxBody::Ref dequeue() {
    auto node = pending.shift();
    return node ? ZuMv(node->data()) : TxBody::Ref{};
  }

  bool published(TxBody::Ref body, TxContext c, ZuDateTime time) {
    if (c.replay || body->administrative()) return true;
    if (history.length() >= Calls) return false;
    history.push(TxHistory{ZuMv(body), time, c.epoch, c.seqNo}); return true;
  }

  TxReload retrieve(TxContext c) const {
    for (const auto &record : history)
      if (record.epoch == c.epoch && record.seqNo == c.seqNo) return {record};
    return {};
  }

  void archive(uint64_t epoch, uint64_t begin, uint64_t end) {
    for (unsigned i = 0; i < history.length();) {
      const auto &record = history[i];
      if (record.epoch == epoch && record.seqNo >= begin && record.seqNo < end)
	history.splice(i, 1);
      else ++i;
    }
  }
};

template <typename Impl>
static void ownSubmitted(Impl *link, TxBody::Ref body, unsigned result)
{
  switch (result) {
    case TxSelect::Busy: case TxSelect::Held:
      if (!link->output.enqueue(ZuMv(body))) link->test->fail();
      else link->queued();
      return;
    case TxSelect::Accepted: case TxSelect::Pending: return;
    default: link->test->fail(); return;
  }
}

template <typename Impl> static void ownStopped(Impl *link)
{
  Ztc::LinkTelemetry data;
  link->telemetry(data);
  if (data.state != Ztc::LinkState::Down) link->test->fail();
}

struct OwnServer : Zfix::Server<OwnServer, Dictionary, Network> {
  using Base = Zfix::Server<OwnServer, Dictionary, Network>;
  using Link = OwnSrvLink;
  OwnState	*const state;
  OwnState	*side = nullptr; // init-stable application routing for the chain fixture

  explicit OwnServer(OwnState *s) : Base{[] {
    HubLimits limits; limits.work.count = limits.link.work.count = 1; return limits;
  }()}, state{s} { }

  ZiIP localIP() const { return ZiIP{"127.0.0.1"}; }
  unsigned localPort() const { return 0; }
  unsigned nAccepts() const { return 1; }

  void listening(const ZiListenInfo &info) {
    state->port = info.port; state->listening.post();
  }

  void listenFailed(bool) { state->fail(); state->listening.post(); }

  ZiConnection *accepted(const ZiCxnInfo &);

  void linkConnected_() { Base::linkConnected_(); ++state->physicalOpen; }

  void linkDisconnected_() {
    Base::linkDisconnected_();
    if (state->physicalClosed.xchAdd(1) == 0) state->physicalOne.post();
    state->physicalAny.post();
  }

  void stop_() {
    if (state->terminal) this->rxRun([state = state] { state->stopGate.block(); });
    Base::stop_();
  }

  bool init(Network::ServerParams params);
};

struct OwnSrvLink : ZmNHeap<OwnSrvLink>,
    Zfix::SrvLink<OwnServer, OwnSrvLink, Dictionary, Network> {
friend inline ZuStringT<"Zfix.Own.Server"> ZmNHeapID(OwnSrvLink *);
  using ZmNHeap<OwnSrvLink>::operator new;
  using ZmNHeap<OwnSrvLink>::operator delete;
  using Base = Zfix::SrvLink<OwnServer, OwnSrvLink, Dictionary, Network>;
  using Base::app;
  using W = Zfix::Work<RequestContext, OwnSrvLink>;
  OwnOutput	output; // AppTx, following the engine's Tx group
  uint64_t	pendingLoad = 0;

  alignas(Zm::CacheLineSize)
  OwnState	*const test; // init-stable; published with the link
  bool		prefixSeen = false; // AppRx
  W::Ref	held; // AppRx; input owns heldID until the reply is encoded
  ZuCSpan	heldID;

  OwnSrvLink(OwnServer *hub, OwnState *s) : Base{hub, ownConfig(false, s)}, test{s} { }

  int process(typename Network::RxStream &stream) {
    int n = Base::process(stream);
    if (test->chain && !prefixSeen && session_().connection_() == 1 &&
	!n && stream.length() == 3 && stream.span() == ZuCSpan{"8=F"}) {
      prefixSeen = true; test->prefix.post();
    }
    return n;
  }

  bool connectSocket_(bool replacing) {
    if (!Base::connectSocket_(replacing)) return false;
    auto state = test;
    if (replacing || ((state->preauth || state->chain) && session_().connection_() >= 2)) {
      const auto &s = session_();
      if (s.connection_() < 2 || s.connection_() > state->connection() ||
	  s.state_() != State::Disconnected ||
	  s.ready_() || *s.deadline_(SessionTimer::Logon)) state->fail();
      if (s.connection_() == 2) {
	if (state->established) {
	  if (!held || held->input().context().connection != 1) state->fail();
	} else {
	  auto reply = ZuMv(state->admit);
	  if (!reply) state->fail();
	  else { reply(true); ++state->oldDecisions; }
	}
      }
      state->bound.post();
    }
    return true;
  }

  void connected(typename Network::Connected e) {
    Base::connected(e);
    if (session_().connection_() >= 2) test->authenticated.post();
  }

  unsigned fixAdmissionRx(SessionEvent event, ZmRef<Zfix::RxQueue::Node>,
      AdmitRequest &request) {
    if (test->established || test->independent || event.connection != 1)
      return Admission::Accepted;
    test->admit = request.defer(); test->admission.post();
    return Admission::Pending;
  }

  unsigned fixAssociateTx(TxBody::Ref body, TxContext c, uint64_t ticket) {
    auto state = test;
    if (state->established && body->id() == 502 && !state->loadContext.seqNo) {
      pendingLoad = ticket; state->loadContext = c; state->loadStarted.post();
      return TxLoad::Pending;
    }
    return TxLoad::Accepted;
  }

  void fixSubmittedTx(TxBody::Ref body, unsigned result) {
    ownSubmitted(this, ZuMv(body), result);
  }

  bool fixEnqueueTx(TxBody::Ref body) { return output.enqueue(ZuMv(body)); }
  TxBody::Ref fixDequeueTx() { return output.dequeue(); }

  void fixSelectedTx(TxBody::Ref, TxContext, unsigned result) {
    if (result != TxSelect::Accepted && result != TxSelect::Pending) test->fail();
  }

  void fixPublishedTx(TxBody::Ref body, TxContext c, ZuDateTime time) {
    if (test->established && body->id() == 502 && !c.replay) {
      if (c.epoch != 1 || c.connection != test->connection() ||
	  c.seqNo != test->loadContext.seqNo)
	test->fail();
      ++test->loadPublished;
    }
    if (!output.published(ZuMv(body), c, time)) test->fail();
  }

  TxReload fixRetrieveTx(TxContext c, uint64_t, uint64_t) { return output.retrieve(c); }

  void fixArchiveTx(uint64_t epoch, uint64_t begin, uint64_t end) {
    output.archive(epoch, begin, end);
  }

  Fmt::Error fixReplyRx(Lane0 *, Reply0 *, const RequestContext &context) {
    return context.context().session == session_().instance_() &&
      context.context().epoch == session_().epoch_() ? Fmt::Error{} :
      Fmt::Error{Fmt::ErrorCode::Unresolved, 35};
  }

  Fmt::Error fixReceiveRx(Lane0 *, const Order &order, const RequestContext &context) {
    auto state = test;
    if (context.context().epoch != 1) state->fail();
    ++test->served;
    Fmt::Error error;
    if (state->established && order.id == "held") {
      if (held || context.context().connection != 1) state->fail();
      held = work<Lane0>(context, error); heldID = order.id;
      if (!held || error) state->fail();
      state->workReady.post(); return error;
    }
    auto id = state->established ? 502U : 501U;
    if ((state->established && (order.id != "load" || context.context().connection != 1)) ||
	(!state->established && session_().connection_() != state->connection()))
      state->fail();
    if (!reply<Lane0, Reply0>(context, order, id, error) || error) state->fail();
    return error;
  }

  void completeWork() {
    auto work = ZuMv(held);
    if (!work) { test->fail(); return; }
    const auto &context = work->input();
    if (session_().connection_() != test->connection() || context.context().connection != 1 ||
	context.context().epoch != 1 || context.context().session != session_().instance_() ||
	heldID != "held") test->fail();
    Fmt::Error error;
    if (!reply<Lane0, Reply0>(context, Order{heldID, 0}, 503, error) || error)
      test->fail();
    heldID = {}; ++test->workReplied;
  }

  void fixEventRx(SessionEvent e, ZmRef<Zfix::RxQueue::Node>) {
    if (e.actions & SessionAction::Failure) {
      log("# replacement server failure state=", e.state, " error=", e.error, " tag=", e.tag,
	" actions=", e.actions, " conn=", e.connection, " epoch=", e.epoch);
      test->fail();
    }
  }

  void fixFaultTx(TxContext c, Fmt::Error error) {
    log("# replacement server Tx fault code=", error.code, " tag=", error.tag, " epoch=",
      c.epoch, " conn=", c.connection, " seq=", c.seqNo);
    test->fail();
  }

  void fixStopRx(uint64_t, uint64_t) {
    auto reply = ZuMv(test->admit);
    if (reply) reply(false);
    held = nullptr; heldID = {};
  }

  void fixStopTx(uint64_t) {
    if (!pendingLoad) return;
    auto ticket = pendingLoad; pendingLoad = 0;
    loaded(ticket, false);
  }

  void fixStoppedRx(bool) {
    ownStopped(this);
    ++test->serverStops; test->stoppedServer.post();
  }
};

bool OwnServer::init(Network::ServerParams params) {
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
  return Zfix::Server<OwnServer, Dictionary, Network>::init(ZuMv(params));
}

struct OwnCxn : ZmNHeap<OwnCxn>, OwnSrvLink::Cxn {
friend inline ZuStringT<"Zfix.Own.Cxn"> ZmNHeapID(OwnCxn *);
  using ZmNHeap<OwnCxn>::operator new;
  using ZmNHeap<OwnCxn>::operator delete;
  using Base = OwnSrvLink::Cxn;
  OwnState	*const state;
  const bool	first;
  bool		held = false; // native Rx

  OwnCxn(OwnSrvLink *link, const ZiCxnInfo &info, bool initial) :
    Base{ZmRef<OwnSrvLink>{link}, info}, state{link->test}, first{initial} { }

  void disconnected(bool peer) override {
    if (held) return;
    if (!first || !state->holdOld) { Base::disconnected(peer); return; }
    held = true;
    state->oldClose = [self = ZmRef(this), peer] { self->Base::disconnected(peer); };
    state->oldClosed.post();
  }
};

ZiConnection *OwnServer::accepted(const ZiCxnInfo &info)
{
  unsigned ordinal = state->accepted.xchAdd(1);
  auto selected = state;
  if (state->chain) switch (ordinal) {
    case 0: case 2: case 3: break; // A, B, C share the application-owned session.
    case 1: selected = side; break;
    default: state->fail(); return nullptr;
  }
  if (!selected) { state->fail(); return nullptr; }
  if (!selected->serverLink) {
    selected->serverLink = new OwnSrvLink{this, selected}; ++selected->logical;
  }
  auto cxn = new OwnCxn{selected->serverLink.ptr(), info, ordinal == 0};
  if (state->terminal && ordinal) state->lateAccepted.post();
  return cxn;
}

struct OwnClient : Zfix::Client<OwnClient, Dictionary, Network> {
  using Link = OwnCliLink;
  OwnState	*const state;

  explicit OwnClient(OwnState *s) : state{s} { }

  bool init(Network::ClientParams params);
};

struct OwnCliLink : ZmNHeap<OwnCliLink>,
    Zfix::CliLink<OwnClient, OwnCliLink, Dictionary, Network> {
friend inline ZuStringT<"Zfix.Own.Client"> ZmNHeapID(OwnCliLink *);
  using ZmNHeap<OwnCliLink>::operator new;
  using ZmNHeap<OwnCliLink>::operator delete;
  using Base = Zfix::CliLink<OwnClient, OwnCliLink, Dictionary, Network>;
  using Base::app;

  // Application Rx starts the workload once; recovery can report Ready again.
  alignas(Zm::CacheLineSize)
  OwnState	*const test; // init-stable attachment; also read by AppTx
  const bool	primary, middle;
  bool		started = false;

  alignas(Zm::CacheLineSize)
  OwnOutput	output; // AppTx

  OwnCliLink(OwnClient *hub, bool first, OwnState *s = nullptr,
      ZuID id = ZuID{"ownB"}, bool interim = false) :
    Base{hub, first ? ZuID{"ownA"} : id, ownConfig(true, s ? s : hub->state)},
    test{s ? s : hub->state}, primary{first}, middle{interim} { }

  unsigned fixAssociateTx(TxBody::Ref, TxContext, uint64_t) { return TxLoad::Accepted; }

  void fixSubmittedTx(TxBody::Ref body, unsigned result) {
    ownSubmitted(this, ZuMv(body), result);
  }

  bool fixEnqueueTx(TxBody::Ref body) { return output.enqueue(ZuMv(body)); }
  TxBody::Ref fixDequeueTx() { return output.dequeue(); }

  void fixSelectedTx(TxBody::Ref, TxContext, unsigned result) {
    if (result != TxSelect::Accepted && result != TxSelect::Pending) test->fail();
  }

  TxReload fixRetrieveTx(TxContext c, uint64_t, uint64_t) { return output.retrieve(c); }

  void fixArchiveTx(uint64_t epoch, uint64_t begin, uint64_t end) {
    output.archive(epoch, begin, end);
  }

  void business(ZuCSpan id) {
    Fmt::Error error;
    if (!send<Lane0>(Order{id, 0}, 501, error) || error) test->fail();
  }

  void fixEventRx(SessionEvent e, ZmRef<Zfix::RxQueue::Node>) {
    if ((e.actions & SessionAction::Failure) && !primary &&
	(e.error != SessionError::Transport ||
	 (!test->terminal && !(middle && test->accepted.load_() >= 4)))) {
      log("# replacement B failure state=", e.state, " error=", e.error, " tag=", e.tag,
	" actions=", e.actions, " conn=", e.connection, " epoch=", e.epoch);
      test->fail();
    }
    if (!(e.actions & SessionAction::Ready)) return;
    if (test->terminal || middle) { test->fail(); return; }
    if (primary) {
      if (!test->established) { test->fail(); return; }
      if (started) return;
      started = true; ++test->aReady;
      business("held"); business("load"); return;
    }
    auto ready = test->bReady.xchAdd(1) + 1;
    log("# replacement B ready=", ready, " mode=", test->mode, " conn=", e.connection, " epoch=",
      e.epoch, " actions=", e.actions);
    if (started) return;
    started = true; ++test->bStarted; test->readyB.post();
    if (!test->established && !test->independent) business("before");
  }

  void fixPublishedTx(TxBody::Ref body, TxContext c, ZuDateTime time) {
    if (!primary && !c.replay && body->msgType() == "A") {
      test->bLogon = c.seqNo; test->clientLogon.post();
    }
    if (!output.published(ZuMv(body), c, time)) test->fail();
  }

  Fmt::Error fixReceiveRx(Reply0 *, const Order &order, const RequestContext &context) {
    auto state = test;
    unsigned index = state->received.xchAdd(1);
    ZuCSpan expected = state->established ? (index ? "held" : "load") :
      (index ? "after" : "before");
    if (primary || index >= OwnOutput::Calls || order.id != expected ||
	context.context().epoch != 1) state->fail();
    state->business.post(); return {};
  }

  void fixFaultTx(TxContext c, Fmt::Error error) {
    if (primary) return;
    log("# replacement B Tx fault code=", error.code, " tag=", error.tag, " epoch=", c.epoch,
      " conn=", c.connection, " seq=", c.seqNo);
    test->fail();
  }

  void fixStoppedRx(bool) {
    ownStopped(this);
    (primary ? test->stoppedA : middle ? test->stoppedMiddle : test->stoppedB).post();
  }
};

bool OwnClient::init(Network::ClientParams params) {
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
  return Zfix::Client<OwnClient, Dictionary, Network>::init(ZuMv(params));
}

static void nativeReplacement(unsigned mode, bool established = false)
{
  ZuTestScope(nativeReplacement);
  OwnState state{mode, established}; OwnServer server{&state}; OwnClient client{&state};
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(6)
      .thread(1, [](auto &t) { t.isolated(1); }).thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); }).thread(4, [](auto &t) { t.isolated(1); })
      .thread(5, [](auto &t) { t.isolated(1); }).thread(6, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  bool si = server.init(srvParams(&mx)), ci = client.init(cliParams(&mx, "5", "6"));
  ZuCheck(si && ci);
  bool started = mx.start(); ZuCheck(started); if (!started) return;
  bool ss = si && server.start(), cs = ci && client.start(); ZuCheck(ss && cs);
  ZmRef<OwnCliLink> a, b;
  bool attemptedA = false, attemptedB = false, exchangedAll = false;
  if (ss && cs) do {
    server.listen();
    bool listening = state.listening.timedwait(Zm::now(5)) == 0 && state.port;
    ZuCheck(listening); if (!listening) break;
    a = new OwnCliLink{&client, true}; b = new OwnCliLink{&client, false};
    auto connect = [&state](OwnCliLink *link) {
#ifdef ZFIX_TEST_TLS
      link->connect("localhost", state.port, ZiIP{"127.0.0.1"});
#else
      link->connect("127.0.0.1", state.port);
#endif
    };
    attemptedA = true; connect(a.ptr());
    bool pending = established ? state.workReady.timedwait(Zm::now(5)) == 0 &&
      state.loadStarted.timedwait(Zm::now(5)) == 0 :
      state.admission.timedwait(Zm::now(5)) == 0;
    ZuCheck(pending); if (!pending) break;
    if (established) ZuCheck(state.aReady.load_() == 1 && state.loadContext.connection == 1 &&
      state.loadContext.epoch == 1 && state.loadContext.seqNo);
    Progress snapshot;
    bool saved = ZmBlock<bool>{}([&client, &a, &snapshot](auto wake) {
      client.rxRun([&client, &a, &snapshot, wake = ZuMv(wake)]() mutable {
	snapshot.epoch = a->session_().epoch_(); snapshot.rx = a->rxProgress();
	client.txRun([&a, &snapshot, wake = ZuMv(wake)]() mutable {
	  snapshot.tx = a->txProgress(); wake(true);
	});
      });
    });
    ZuCheck(saved && snapshot.epoch == 1 && snapshot.tx.loaded == (established ? 4U : 2U) &&
      snapshot.tx.sent == snapshot.tx.loaded && snapshot.rx.processed == (established ? 2U : 1U));
    bool restored = ZmBlock<bool>{}([&client, &b, &snapshot](auto wake) {
      client.rxRun([&client, &b, &snapshot, wake = ZuMv(wake)]() mutable {
	bool ok = b->restoreRx(snapshot.epoch, snapshot.rx);
	client.txRun([&b, &snapshot, ok, wake = ZuMv(wake)]() mutable {
	  wake(b->restoreTx(snapshot.epoch, snapshot.tx) && ok);
	});
      });
    });
    ZuCheck(restored); if (!restored) break;
    attemptedB = true; connect(b.ptr());
    ZuCheck(state.bound.timedwait(Zm::now(5)) == 0);
    if (established) {
      ZuCheck(state.authenticated.timedwait(Zm::now(5)) == 0);
      bool completed = ZmBlock<bool>{}([&server, link = state.serverLink](auto wake) {
	server.txRun([link, wake = ZuMv(wake)]() mutable {
	  auto ticket = link->pendingLoad; link->pendingLoad = 0;
	  if (ticket) link->loaded(ticket, true);
	  wake(bool(ticket));
	});
      });
      ZuCheck(completed);
    }
    bool exchanged = state.business.timedwait(Zm::now(5)) == 0;
    ZuCheck(exchanged); if (!exchanged) break;
    if (established) ZuCheck(state.readyB.timedwait(Zm::now(5)) == 0);
    ZuCheck(state.oldClosed.timedwait(Zm::now(5)) == 0);
    ZuCheck(state.logical.load_() == 1 && state.accepted.load_() == 2 &&
      state.physicalOpen.load_() == 2 && !state.physicalClosed.load_() &&
      !state.serverStops.load_() && state.oldDecisions.load_() == unsigned(!established) &&
      state.bReady.load_() >= 1 && state.bStarted.load_() == 1 &&
      state.bLogon == snapshot.tx.loaded);
    mx.rxRun([&state] {
      auto close = ZuMv(state.oldClose); if (close) close();
    });
    ZuCheck(state.physicalOne.timedwait(Zm::now(5)) == 0);
    ZuCheck(!state.serverStops.load_());
    if (established) {
      WorkState local, hub;
      auto sample = [&server, &state, &local, &hub] {
	ZmBlock<>{}([&server, &state, &local, &hub](auto wake) {
	  server.rxRun([&server, link = state.serverLink, &local, &hub,
	      wake = ZuMv(wake)]() mutable {
	    local = link->workState(); hub = server.fixWorkRx().state_(); wake();
	  });
	});
      };
      sample();
      ZuCheck(local.count == 1 && local.bytes && hub.count == 1 && hub.bytes == local.bytes &&
	state.loadPublished.load_() == 1);
      server.rxRun([link = state.serverLink] { link->completeWork(); });
      ZuCheck(state.business.timedwait(Zm::now(5)) == 0);
      sample();
      ZuCheck(!local.count && !local.bytes && !hub.count && !hub.bytes &&
	state.workReplied.load_() == 1);
    } else {
      client.rxRun([b] { b->business("after"); });
      ZuCheck(state.business.timedwait(Zm::now(5)) == 0);
    }
    ZuCheck(state.received.load_() == 2 && state.served.load_() == 2);
    b->logout();
    exchangedAll = true;
  } while (false);
  // Release held callbacks on their native owner even after an earlier failure.
  ZmBlock<>{}([&mx, &state](auto wake) {
    mx.rxRun([&state, wake = ZuMv(wake)]() mutable {
      state.holdOld = false;
      auto close = ZuMv(state.oldClose); if (close) close();
      wake();
    });
  });
  if (!exchangedAll) {
    if (attemptedA) a->disconnect(); if (attemptedB) b->disconnect();
  }
  // Logical callbacks retire before either hub finalizes its owner queues.
  // Never destroy an active fixture owner to conceal a failed protocol/drain.
  bool drained = true;
  if (attemptedA) {
    bool stopped = state.stoppedA.timedwait(Zm::now(5)) == 0;
    ZuCheck(stopped); drained &= stopped;
  }
  if (attemptedB) {
    bool stopped = state.stoppedB.timedwait(Zm::now(5)) == 0;
    ZuCheck(stopped); drained &= stopped;
  }
  if (state.accepted.load_()) {
    bool stopped = state.stoppedServer.timedwait(Zm::now(5)) == 0;
    ZuCheck(stopped); drained &= stopped;
  }
  if (!drained) {
    log_("Bail out! replacement owner did not drain mode=", mode);
    abort();
  }
  bool cstop = client.stop(), sstop = server.stop(); ZuCheck(cstop && sstop);
  client.final(); server.final(); mx.stop();
  state.serverLink = nullptr; a = b = nullptr;
  ZuCheck(exchangedAll && !state.errors.load_() && !state.admit && !state.oldClose);
  ZuCheck(state.serverStops.load_() == 1 && state.physicalClosed.load_() == 2 &&
    state.bReady.load_() >= 1 && state.bStarted.load_() == 1 &&
    state.received.load_() == 2 && state.served.load_() == 2);
}

// A/SIDE/B/C is the fixture's application routing order. A retains accepted
// work and an outstanding Tx association while SIDE exchanges across both swaps.
static void nativeChain(unsigned mode)
{
  ZuTestScope(nativeChain);
  OwnState state{mode, true, false, false, true}, side{mode, false, false, false,
    false, true};
  OwnServer server{&state}; server.side = &side;
  OwnClient client{&state};
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(6)
      .thread(1, [](auto &t) { t.isolated(1); }).thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); }).thread(4, [](auto &t) { t.isolated(1); })
      .thread(5, [](auto &t) { t.isolated(1); }).thread(6, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  bool si = server.init(srvParams(&mx)), ci = client.init(cliParams(&mx, "5", "6"));
  ZuCheck(si && ci);
  bool started = mx.start(); ZuCheck(started); if (!started) return;
  bool ss = si && server.start(), cs = ci && client.start(); ZuCheck(ss && cs);
  ZmRef<OwnCliLink> a, b, c, other;
  bool triedA = false, triedB = false, triedC = false, triedSide = false, exchanged = false;
  auto connect = [&state](OwnCliLink *link) {
#ifdef ZFIX_TEST_TLS
    link->connect("localhost", state.port, ZiIP{"127.0.0.1"});
#else
    link->connect("127.0.0.1", state.port);
#endif
  };
  auto checkpoint = [&client](OwnCliLink *link, Progress &snapshot, bool restore) {
    return ZmBlock<bool>{}([&client, link, &snapshot, restore](auto wake) {
      client.rxRun([&client, link, &snapshot, restore, wake = ZuMv(wake)]() mutable {
	bool ok = true;
	if (restore) ok = link->restoreRx(snapshot.epoch, snapshot.rx);
	else {
	  snapshot.epoch = link->session_().epoch_(); snapshot.rx = link->rxProgress();
	}
	client.txRun([link, &snapshot, restore, ok, wake = ZuMv(wake)]() mutable {
	  if (restore) ok = link->restoreTx(snapshot.epoch, snapshot.tx) && ok;
	  else snapshot.tx = link->txProgress();
	  wake(ok);
	});
      });
    });
  };
  auto physical = [&state](unsigned count) {
    auto deadline = Zm::now(5);
    while (state.physicalClosed.load_() < count)
      if (state.physicalAny.timedwait(deadline)) return false;
    return state.physicalClosed.load_() == count;
  };
  if (ss && cs) do {
    server.listen();
    bool listening = state.listening.timedwait(Zm::now(5)) == 0 && state.port;
    ZuCheck(listening); if (!listening) break;
    a = new OwnCliLink{&client, true};
    b = new OwnCliLink{&client, false, &state, ZuID{"ownB"}, true};
    c = new OwnCliLink{&client, false, &state, ZuID{"ownC"}};
    other = new OwnCliLink{&client, false, &side, ZuID{"ownSide"}};
    triedA = true; connect(a.ptr());
    bool pending = state.workReady.timedwait(Zm::now(5)) == 0 &&
      state.loadStarted.timedwait(Zm::now(5)) == 0;
    ZuCheck(pending); if (!pending) break;
    ZuCheck(state.aReady.load_() == 1 && state.loadContext.connection == 1 &&
      state.loadContext.epoch == 1 && state.loadContext.seqNo == 2);
    client.txRun([a, &state] {
      auto output = a->txStream_(); output << "8=F";
      if (!output.flush()) state.fail();
    });
    bool prefix = state.prefix.timedwait(Zm::now(5)) == 0;
    ZuCheck(prefix); if (!prefix) break;
    Progress snapshot;
    bool saved = checkpoint(a.ptr(), snapshot, false);
    ZuCheck(saved && snapshot.epoch == 1 && snapshot.tx.loaded == 4 &&
      snapshot.tx.sent == 4 && snapshot.rx.processed == 2);
    if (!saved) break;
    triedSide = true; connect(other.ptr());
    bool sideReady = side.readyB.timedwait(Zm::now(5)) == 0;
    ZuCheck(sideReady); if (!sideReady) break;
    bool restored = checkpoint(b.ptr(), snapshot, true);
    ZuCheck(restored); if (!restored) break;
    triedB = true; connect(b.ptr());
    bool boundB = state.bound.timedwait(Zm::now(5)) == 0;
    ZuCheck(boundB); if (!boundB) break;
    client.rxRun([other] { other->business("before"); });
    bool sideBefore = side.business.timedwait(Zm::now(5)) == 0;
    ZuCheck(sideBefore); if (!sideBefore) break;
    ZuCheck(state.authenticated.timedwait(Zm::now(5)) == 0);
    bool logonB = state.clientLogon.timedwait(Zm::now(5)) == 0;
    ZuCheck(logonB); if (!logonB) break;
    Progress successor;
    bool savedB = checkpoint(b.ptr(), successor, false);
    ZuCheck(savedB && successor.epoch == 1 && successor.tx.loaded == 5 &&
      successor.tx.sent == 5 && successor.rx.processed == 2 && state.bLogon == 4);
    if (!savedB) break;
    restored = checkpoint(c.ptr(), successor, true);
    ZuCheck(restored); if (!restored) break;
    triedC = true; connect(c.ptr());
    bool boundC = state.bound.timedwait(Zm::now(5)) == 0;
    ZuCheck(boundC); if (!boundC) break;
    client.rxRun([other] { other->business("after"); });
    bool sideAfter = side.business.timedwait(Zm::now(5)) == 0;
    ZuCheck(sideAfter); if (!sideAfter) break;
    ZuCheck(state.authenticated.timedwait(Zm::now(5)) == 0);
    ZuCheck(state.clientLogon.timedwait(Zm::now(5)) == 0);
    ZuCheck(!state.bStarted.load_() && !state.loadPublished.load_() &&
      !state.workReplied.load_() && side.received.load_() == 2 && side.served.load_() == 2);
    bool completed = ZmBlock<bool>{}([&server, link = state.serverLink](auto wake) {
      server.txRun([link, wake = ZuMv(wake)]() mutable {
	auto ticket = link->pendingLoad; link->pendingLoad = 0;
	if (ticket) link->loaded(ticket, true);
	wake(bool(ticket));
      });
    });
    ZuCheck(completed);
    bool received = state.business.timedwait(Zm::now(5)) == 0;
    ZuCheck(received); if (!received) break;
    ZuCheck(state.readyB.timedwait(Zm::now(5)) == 0);
    ZuCheck(state.oldClosed.timedwait(Zm::now(5)) == 0);
    ZuCheck(physical(1)); // B retired, while A's physical callback remains held.
    ZuCheck(state.logical.load_() == 1 && side.logical.load_() == 1 &&
      state.accepted.load_() == 4 && state.physicalOpen.load_() == 4 &&
      !state.serverStops.load_() && !side.serverStops.load_() &&
      state.bStarted.load_() == 1 && state.bLogon == 5);
    WorkState local, hub;
    auto sample = [&server, &state, &local, &hub] {
      ZmBlock<>{}([&server, &state, &local, &hub](auto wake) {
	server.rxRun([&server, link = state.serverLink, &local, &hub,
	    wake = ZuMv(wake)]() mutable {
	  local = link->workState(); hub = server.fixWorkRx().state_(); wake();
	});
      });
    };
    sample();
    ZuCheck(local.count == 1 && local.bytes && hub.count == 1 && hub.bytes == local.bytes &&
      state.loadPublished.load_() == 1);
    mx.rxRun([&state] { auto close = ZuMv(state.oldClose); if (close) close(); });
    ZuCheck(physical(2));
    ZuCheck(!state.serverStops.load_() && !side.serverStops.load_());
    server.rxRun([link = state.serverLink] { link->completeWork(); });
    ZuCheck(state.business.timedwait(Zm::now(5)) == 0);
    sample();
    ZuCheck(!local.count && !local.bytes && !hub.count && !hub.bytes &&
      state.workReplied.load_() == 1 && state.received.load_() == 2 &&
      state.served.load_() == 2);
    c->logout(); other->logout(); exchanged = true;
  } while (false);
  ZmBlock<>{}([&mx, &state](auto wake) {
    mx.rxRun([&state, wake = ZuMv(wake)]() mutable {
      state.holdOld = false;
      auto close = ZuMv(state.oldClose); if (close) close();
      wake();
    });
  });
  if (!exchanged) {
    if (triedA) a->disconnect(); if (triedB) b->disconnect();
    if (triedC) c->disconnect(); if (triedSide) other->disconnect();
  }
  bool drained = true;
  auto stop = [&drained](bool attempted, ZmSemaphore &done) {
    if (!attempted) return;
    bool stopped = done.timedwait(Zm::now(5)) == 0;
    ZuCheck(stopped); drained &= stopped;
  };
  stop(triedA, state.stoppedA); stop(triedB, state.stoppedMiddle);
  stop(triedC, state.stoppedB); stop(triedSide, side.stoppedB);
  stop(bool(state.logical.load_()), state.stoppedServer);
  stop(bool(side.logical.load_()), side.stoppedServer);
  if (!drained) {
    log_("Bail out! replacement chain owner did not drain mode=", mode);
    abort();
  }
  bool cstop = client.stop(), sstop = server.stop(); ZuCheck(cstop && sstop);
  client.final(); server.final(); mx.stop();
  state.serverLink = side.serverLink = nullptr; a = b = c = other = nullptr;
  ZuCheck(exchanged && !state.errors.load_() && !side.errors.load_() &&
    !state.admit && !side.admit && !state.oldClose);
  ZuCheck(state.serverStops.load_() == 1 && side.serverStops.load_() == 1 &&
    state.physicalClosed.load_() == 4 && state.bStarted.load_() == 1 &&
    side.bStarted.load_() == 1 && state.received.load_() == 2 &&
    state.served.load_() == 2 && side.received.load_() == 2 && side.served.load_() == 2);
}

#include "session_preauth_test.hh"

int main(int argc, char **argv) {
  parse(argc, argv); ZuTestMain();
  ZuTestCall(gapTimerDrain);
  ZuTestCall(loopback, Transport::FIX4, false, 0, Scenario::Normal, false, true, false, 0, true);
  ZuTestCall(loopback, Transport::FIX44A, false, 0, Scenario::Normal, false, true, false, 0, true);
  ZuTestCall(loopback, Transport::FIXT11, false, 0, Scenario::Normal, false, true, false, 0, true);
  ZuTestCall(loopback, Transport::FIX4, false, 0, Scenario::Normal, false, false, false, 1);
  ZuTestCall(loopback, Transport::FIX44A, false, 0, Scenario::Normal, false, false, false, 1);
  ZuTestCall(loopback, Transport::FIXT11, false, 0, Scenario::Normal, false, false, false, 1);
  ZuTestCall(loopback, Transport::FIX4, false, 0, Scenario::Normal, false, true, false, 2);
  ZuTestCall(loopback, Transport::FIX44A, false, 0, Scenario::Normal, false, true, false, 2);
  ZuTestCall(loopback, Transport::FIXT11, false, 0, Scenario::Normal, false, true, false, 2);
  ZuTestCall(loopback, Transport::FIX4, false, 0, Scenario::Normal, false, true, false, 3);
  ZuTestCall(loopback, Transport::FIX44A, false, 0, Scenario::Normal, false, true, false, 3);
  ZuTestCall(loopback, Transport::FIXT11, false, 0, Scenario::Normal, false, true, false, 3);
  ZuTestCall(loopback, Transport::FIX4);
  ZuTestCall(loopback, Transport::FIX4, false, 0, Scenario::Normal, false, false, true);
  ZuTestCall(loopback, Transport::FIX44A, false, 0, Scenario::Normal, false, false, true);
  ZuTestCall(loopback, Transport::FIXT11, false, 0, Scenario::Normal, false, false, true);
  ZuTestCall(loopback, Transport::FIX4, false, 0, Scenario::Normal, false, true);
  ZuTestCall(loopback, Transport::FIX44A, false, 30, Scenario::Normal, true);
  ZuTestCall(loopback, Transport::FIX44A);
  ZuTestCall(loopback, Transport::FIXT11);
  ZuTestCall(loopback, Transport::FIX44A, true);
  ZuTestCall(loopback, Transport::FIX4, false, 1);
  ZuTestCall(loopback, Transport::FIX4, false, 30, Scenario::Refused);
  ZuTestCall(loopback, Transport::FIX44A, true, 30, Scenario::PendingClose);
  ZuTestCall(loopback, Transport::FIX4, false, 30, Scenario::LoadClose);
  ZuTestCall(loopback, Transport::FIX4, false, 30, Scenario::PrepareRefused);
  ZuTestCall(routeMatrix, Transport::FIX4);
  ZuTestCall(routeMatrix, Transport::FIX44A);
  ZuTestCall(routeMatrix, Transport::FIXT11);
  ZuTestCall(setupCleanup, Transport::FIX4);
  ZuTestCall(setupCleanup, Transport::FIX44A);
  ZuTestCall(setupCleanup, Transport::FIXT11);
  ZuTestCall(reloadBudget, false);
  ZuTestCall(reloadBudget, true);
  ZuTestCall(adminBudget, 0);
  ZuTestCall(adminBudget, 1);
  ZuTestCall(adminBudget, 2);
  ZuTestCall(adminBudget, 3);
  ZuTestCall(adminBudget, 4);
  ZuTestCall(adminBudget, 5);
  ZuTestCall(adminBudget, 6);
  ZuTestCall(adminBudget, 7);
  ZuTestCall(adminBudget, 8);
  ZuTestCall(readAheadOverload);
  ZuTestCall(listenerCleanup);
  ZuTestCall(startupCleanup, Transport::FIX4);
  ZuTestCall(startupCleanup, Transport::FIX44A);
  ZuTestCall(startupCleanup, Transport::FIXT11);
  ZuTestCall(nativeReplacement, Transport::FIX4);
  ZuTestCall(nativeReplacement, Transport::FIX44A);
  ZuTestCall(nativeReplacement, Transport::FIXT11);
  ZuTestCall(nativeReplacement, Transport::FIX4, true);
  ZuTestCall(nativeReplacement, Transport::FIX44A, true);
  ZuTestCall(nativeReplacement, Transport::FIXT11, true);
  ZuTestCall(nativeChain, Transport::FIX4);
  ZuTestCall(nativeChain, Transport::FIX44A);
  ZuTestCall(nativeChain, Transport::FIXT11);
#if defined(__linux__) && defined(ZFIX_TEST_TLS)
  ZuTestCall(nativePreauth, Transport::FIX4, false);
  ZuTestCall(nativePreauth, Transport::FIX44A, false);
  ZuTestCall(nativePreauth, Transport::FIXT11, false);
  ZuTestCall(nativePreauth, Transport::FIX4, true);
  ZuTestCall(nativePreauth, Transport::FIX44A, true);
  ZuTestCall(nativePreauth, Transport::FIXT11, true);
#endif
#ifdef __linux__
  ZuTestCall(nativeStopBeforeBind, Transport::FIX4);
  ZuTestCall(nativeStopBeforeBind, Transport::FIX44A);
  ZuTestCall(nativeStopBeforeBind, Transport::FIXT11);
#endif
  return 0;
}
