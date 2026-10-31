//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmPlatform.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZiMultiplex.hh>
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
  ZuCSpan	value;
  ZuBSpan	data;
};

ZfStruct(, (Packet, FIX), (value, (FIX::ID<11>), String),
  (data, (FIX::ID<96>, FIX::Len<95>), Bytes));
ZfStructImpl(Packet); ZfStructImpl(Packet, FIX);
// A deliberately external schema for a built-in control MsgType. There are
// no managed Session::Logon fields, admission, header matching or sequence semantics.
using Response = MT<Packet, "8">;
using Entry = MT<Packet, "A", ZuFacet::FIX, NoDiscrim, ZuTypeList<Response>>;
using Dictionary = MTList<Entry, Response>;
// RawOutput is a shared core object; its application FIFO needs its own links.
using OutputQueue = ZmList<RawOutput::Ref, ZmListHeapID<"Zfix.RawIOTest.Queue">>;
struct TestState {
  ZmSemaphore		listening, done, drained, stoppedOne, stopPending, rxStopped;
  ZmFn<void()>		release;
  ZmAtomic<unsigned>	errors{0}, received{0}, published{0}, complete{0}, closed{0}, paused{0},
    cancelled{0}, refused{0};
  unsigned		port = 0, payload = 4;
  bool			pause = false, deferStop = false, quota = false, setup = false;
  ZmRef<ZiIOBuf>	binary;

  void fail() { ++errors; done.post(); }

  void stopped() { if (closed.xchAdd(1) == 1) drained.post(); stoppedOne.post(); }
};

static SessionConfig config() {
  SessionConfig c; c.mode = Transport::Raw; c.header.version = Version::N;
  c.header.local = "UNUSED"; c.header.peer = "UNUSED";
  c.heartBtInt = 1; c.logonTimeout = ZuTime{0, 1}; return c;
}
static LinkLimits limits(TestState *s) {
  LinkLimits l; if (s->quota) l.wire.queuedBytes = l.wire.adminBytes = 0; return l;
}
static unsigned seq(unsigned i) { return i == 1 ? 2 : 17; }
template <typename Impl, typename Base, bool Client>
struct Peer : Base {
  using Base::app;
  // Application Rx owns input progression and deferred resume state.
  unsigned		received = 0;
  bool			allowed = false, retried = false;
  const RawInput	*pausedInput = nullptr;
  // Manager output belongs only to Tx.
  alignas(Zm::CacheLineSize)
  OutputQueue		queue;
  using W = Zfix::Work<ZmRef<const RawInput>, Impl>;
  struct WorkReply {
    Peer		*peer;
    typename W::Ref	work;
    ZuBSpan		data;
    unsigned		index;

    void operator ()() {
      auto h = peer->header(index); Fmt::Error error;
      if (!peer->template reply<Entry, Response>(*work->input(), Packet{"res", data},
	201 + index, h, {0, work->input()->connection, h.seqNo, h.possDup}, true,
	  error)) peer->state()->fail();
    }
  };

  struct WorkRun {
    Peer		*peer;
    typename W::Ref	work;
    ZuBSpan		data;
    unsigned		index;

    void operator ()() {
      if (peer->app()->rxInvoked() || peer->app()->txInvoked() ||
	data != peer->state()->binary->cspan() ||
	Fmt::frame(work->input()->frame.wire).state != Fmt::FrameState::Complete)
	peer->state()->fail();
      peer->app()->rxRun(WorkReply{peer, work, data, index});
    }
  };

  template <typename Hub> explicit Peer(Hub *hub) : Base{hub, config()} { }

  auto self() { return static_cast<Impl *>(this); }
  auto state() { return this->app()->state; }

  Fmt::Header header(unsigned i) {
    Fmt::Header h; h.beginString = "FIX.9.9";
    h.sender = Client ? "EXTERNAL-C" : "EXTERNAL-S";
    h.target = Client ? "EXTERNAL-S" : "EXTERNAL-C";
    h.seqNo = seq(i); h.sendingTime = {ZuDateTimeScan::FIX{}, "20000101-00:00:00"};
    h.possDup = i == 3; if (h.possDup) h.origSendingTime = h.sendingTime;
    return h;
  }

  RawOutput::Ref packet(uint64_t connection, unsigned i) {
    Fmt::Error error;
    auto body = TxBody::encodeRaw<Entry>(Packet{Client ? "req" : "res", state()->binary->cspan()},
      (Client ? 101 : 201) + i, true, error);
    if (error) { state()->fail(); return {}; }
    auto h = header(i);
    auto out = RawOutput::encode(body, h, {0, connection, h.seqNo, h.possDup}, error);
    if (error) state()->fail(); return out;
  }

  void enqueue(RawOutput::Ref out) {
    this->app()->txRun([this, out = ZuMv(out)]() mutable { queue.push(ZuMv(out)); flush(); });
  }

  void flush() {
    while (auto node = queue.headNode()) {
      auto result = this->sendRaw_(node->data());
      if (result == RawSend::Accepted) { queue.shift(); continue; }
      if (state()->quota && result == RawSend::Overload) {
	++state()->refused; state()->done.post(); return;
      }
      if (result != RawSend::Held && result != RawSend::Overload) state()->fail();
      return;
    }
  }

  void fixRawUpRx(uint64_t connection) {
    if (!this->app()->rxInvoked() || connection != 1) state()->fail();
    if constexpr (Client) for (unsigned i = 0; i < 4; ++i) enqueue(packet(connection, i));
  }

  bool fixRawRx(ZmRef<RawInput> msg, Fmt::Limits l) {
    if (!this->app()->rxInvoked() || msg->connection != 1) state()->fail();
    if (!Client && state()->pause && !allowed) {
      if (pausedInput) state()->fail(); pausedInput = msg.ptr(); ++state()->paused;
      this->app()->mx()->run([this, c = msg->connection] {
	this->app()->rxRun([this, c] { allowed = true; this->resumeRaw_(c); });
      }, 5);
      return false;
    }
    if (pausedInput && !retried) { if (msg.ptr() != pausedInput) state()->fail();
      retried = true; }
    Fmt::MessageHandler<Packet> h{msg->frame, l}; auto p = h.ctor();
    if (!h.valid()) { state()->fail(); return true; }
    auto &hdr = h.header();
    if (msg->frame.msgType != (Client ? "8" : "A")) state()->fail();
    if (p.value != (Client ? "res" : "req") || p.data != state()->binary->cspan() ||
	hdr.seqNo != seq(received) || hdr.beginString != "FIX.9.9" ||
	hdr.possDup != (received == 3)) state()->fail();
    ++state()->received;
    if constexpr (!Client) {
      constexpr uint64_t Extra =
	ZmHeapAllocSize<sizeof(WorkRun) + sizeof(ZmRingFn<ZmRingFn_Defaults>)>{} +
	ZmHeapAllocSize<sizeof(WorkReply) + sizeof(ZmRingFn<ZmRingFn_Defaults>)>{};
      Fmt::Error error; auto work = this->work(ZmRef<const RawInput>{msg}, error, Extra);
      if (!work || error) state()->fail();
      else this->app()->mx()->run(WorkRun{this, work, p.data, received}, 5);
    }
    if (++received == 4 && Client) state()->done.post();
    return true;
  }

  Fmt::Error fixReplyRx(Entry *, Response *, const RawInput &input) {
    if (!this->app()->rxInvoked()) state()->fail();
    // Raw manager accepts the fixture's repeated/low wire sequences explicitly.
    return input.connection == this->rawConnection_() ? Fmt::Error{} :
      Fmt::Error{Fmt::ErrorCode::Unresolved, 35};
  }

  void fixRawSubmittedTx(RawOutput::Ref out, unsigned result) {
    if (result == RawSend::Accepted) return;
    if (result == RawSend::Held || result == RawSend::Overload) {
      queue.push(ZuMv(out)); return;
    }
    state()->fail();
  }

  void fixRawPublishedTx(const RawOutput &out) {
    if (!this->app()->txInvoked() || out.context().epoch
      || out.body()->msgType() != (Client ? "A" : "8")) state()->fail();
    ++state()->published;
  }

  void fixRawCompleteTx(TxBody::Ref b, TxContext c, bool ok) {
    if (!this->app()->txInvoked() || c.epoch || !ok
      || b->msgType() != (Client ? "A" : "8")) state()->fail();
    ++state()->complete;
  }

  void fixRawWritableTx(uint64_t) { flush(); }

  void fixRawFaultRx(uint64_t, Fmt::Error) { state()->fail(); }

  void fixRawFaultTx(uint64_t, Fmt::Error) { state()->fail(); }

  void fixRawStopRx(uint64_t, RawStopRequest &request) {
    if (!this->app()->rxInvoked()) state()->fail();
    if (!state()->deferStop) return;
    this->app()->mx()->run([this, done = request.defer()] {
      ++state()->cancelled; done();
      if (state()->setup) this->app()->rxRun([this] { state()->rxStopped.post(); });
    }, 5);
  }

  void fixRawStopTx(uint64_t, RawStopRequest &request) {
    if (!this->app()->txInvoked()) state()->fail();
    queue.clean();
    if (!state()->deferStop) return;
    if (state()->setup) {
      state()->release = ZmFn<void()>{[this, done = request.defer()] {
	this->app()->txRun([this, done] { ++state()->cancelled; done(); });
      }};
      state()->stopPending.post(); return;
    }
    this->app()->mx()->run([this, done = request.defer()] {
      ++state()->cancelled; done();
    }, 5);
  }

  void fixRawStoppedRx(uint64_t c, bool) {
    if (!this->app()->rxInvoked() || c != (state()->setup ? 0 : 1)) state()->fail();
      state()->stopped();
  }
};

struct CliHub : RawClient<CliHub, Dictionary, Network> {
  using Base = RawClient<CliHub, Dictionary, Network>;
  struct Link;
  TestState	*state;

  explicit CliHub(TestState *s) : Base{HubLimits{.link = limits(s)}}, state{s} { }

  unsigned reconnFreq() const { return 0; }
};

struct CliHub::Link : public ZmNHeap<Link>, Peer<Link, RawCliLink<CliHub, Link, Dictionary,
  Network>, true> {
friend inline ZuStringT<"Zfix.raw_io_test.Link"> ZmNHeapID(Link *);
  using ZmNHeap<Link>::operator new;
  using ZmNHeap<Link>::operator delete; using Peer::Peer; };

struct SrvHub : RawServer<SrvHub, Dictionary, Network> {
  using Base = RawServer<SrvHub, Dictionary, Network>;
  struct Link;
  TestState	*state;

  explicit SrvHub(TestState *s) : Base{HubLimits{.link = limits(s)}}, state{s} { }

  ZiConnection *accepted(const ZiCxnInfo &);

  ZiIP localIP() const { return ZiIP{"127.0.0.1"}; }
  unsigned localPort() const { return 0; }
  unsigned nAccepts() const { return 1; }

  void listening(const ZiListenInfo &i) { state->port = i.port; state->listening.post(); }

  void listenFailed(bool) { state->fail(); state->listening.post(); }
};

struct SrvHub::Link : public ZmNHeap<Link>, Peer<Link, RawSrvLink<SrvHub, Link, Dictionary,
  Network>, false> {
friend inline ZuStringT<"Zfix.raw_io_test.Link"> ZmNHeapID(Link *);
  using ZmNHeap<Link>::operator new;
  using ZmNHeap<Link>::operator delete; using Peer::Peer; };

ZiConnection *SrvHub::accepted(const ZiCxnInfo &i) { return new Link::Cxn{new Link{this}, i}; }
static void loopback(bool pause, bool stop, bool quota, unsigned payload = 4) {
  ZuTestScope(loopback);
  TestState state; state.pause = pause; state.deferStop = stop; state.quota = quota;
    state.payload = payload;
  state.binary = new ZiIOBufAlloc<>; state.binary->ensure(payload);
  constexpr auto Bits = "A\1\0="_z;
  for (unsigned i = 0; i < payload; ++i) state.binary->data()[i] = Bits[i % Bits.length()];
  state.binary->length = payload;
  SrvHub server{&state}; CliHub client{&state};
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(5)
      .thread(1, [](auto &t) { t.isolated(1); }).thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); }).thread(4, [](auto &t) { t.isolated(1); })
      .thread(5, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  bool started = mx.start(); ZuCheck(started); if (!started) return;
  typename Network::ServerParams sp{&mx, "3", "4"}; typename Network::ClientParams cp{&mx, "3",
    "4"};
#ifdef ZFIX_TEST_TLS
  const char *cert = getenv("ZFIX_TEST_CERT"), *key = getenv("ZFIX_TEST_KEY");
  if (!cert || !key) { ZuCheck(cert && key); mx.stop(); return; }
  sp.certPath(cert).keyPath(key); cp.caPath(cert);
#endif
  bool si = server.init(ZuMv(sp)), ci = client.init(ZuMv(cp)); ZuCheck(si && ci);
  bool ss = si && server.start(), cs = ci && client.start(); ZuCheck(ss && cs);
  ZmRef<CliHub::Link> link = new CliHub::Link{&client};
  server.listen(); ZuCheck(state.listening.timedwait(Zm::now(5)) == 0);
#ifdef ZFIX_TEST_TLS
  link->connect("localhost", state.port, ZiIP{"127.0.0.1"});
#else
  link->connect("127.0.0.1", state.port);
#endif
  ZuCheck(state.done.timedwait(Zm::now(5)) == 0);
  link->disconnect(); ZuCheck(state.drained.timedwait(Zm::now(5)) == 0);
  bool cstop = client.stop(), sstop = server.stop(); ZuCheck(cstop && sstop);
  client.final(); server.final(); mx.stop(); link = nullptr;
  ZuCheck(!state.errors.load_() && state.closed.load_() == 2);
  if (quota) ZuCheck(state.refused.load_() && !state.published.load_()
    && !state.complete.load_() && !state.received.load_());
  else {
    ZuCheck(state.published.load_() == 8 && state.complete.load_() == 8
      && state.received.load_() == 8);
    ZuCheck(state.paused.load_() == unsigned(pause));
  }
  ZuCheck(state.cancelled.load_() == (stop ? 4 : 0));
}
// Repeated stop during setup drains its continuation before notifying the manager.
// Each stop must await both owners even when the connection counter stays zero.
static void setupCleanup(bool deferred) {
  ZuTestScope(setupCleanup);
  TestState state; state.setup = true; state.deferStop = deferred;
  CliHub client{&state};
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(5)
      .thread(1, [](auto &t) { t.isolated(1); }).thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); }).thread(4, [](auto &t) { t.isolated(1); })
      .thread(5, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  bool started = mx.start(); ZuCheck(started); if (!started) return;
  typename Network::ClientParams params{&mx, "3", "4"};
#ifdef ZFIX_TEST_TLS
  params.caPath(getenv("ZFIX_TEST_CERT"));
#endif
  bool init = client.init(ZuMv(params)); ZuCheck(init);
  bool up = init && client.start(); ZuCheck(up);
  ZmRef<CliHub::Link> link = new CliHub::Link{&client};
  for (unsigned i = 0; i < 3; ++i) {
    client.rxRun([link] { link->connect("127.0.0.1", 1); link->disconnect_(false); });
    if (deferred) {
      ZuCheck(state.stopPending.timedwait(Zm::now(5)) == 0 && state.release);
      ZuCheck(state.rxStopped.timedwait(Zm::now(5)) == 0);
      ZuCheck(state.stoppedOne.trywait() != 0);
      auto release = ZuMv(state.release); if (release) release();
    }
    ZuCheck(state.stoppedOne.timedwait(Zm::now(5)) == 0);
    ZuCheck(state.closed.load_() == i + 1);
    ZuCheck(state.cancelled.load_() == (deferred ? 2*(i + 1) : 0));
    ZuCheck(!state.published.load_() && !state.complete.load_() && !state.received.load_());
  }
  bool stopped = client.stop(); ZuCheck(stopped);
  client.final(); mx.stop(); link = nullptr;
  ZuCheck(!state.errors.load_());
}

// Keep the application's server link while three physical clients displace one
// another. Old manager drains and a real native sent notification stay pending
// while successors exchange complete frames through the Raw engine.
struct SwapState {
  ZmSemaphore		listening, clientUp, serverUp, reply, oldOutput, rxPending, txPending,
    rxReleased, recordReleased, clientStopped, serverStopped, outputDone, lastClosed;
  ZmAtomic<unsigned>	errors{0}, clientUps{0}, serverUps{0}, replies{0}, clientStops{0},
    serverStops{0}, rxStops{0}, txStops{0}, completed{0}, failed{0};
  unsigned		port = 0;

  void fail() { ++errors; reply.post(); }
};

template <typename Impl, typename Base, bool Client>
struct SwapPeer : Base {
  // The application owns each deferred stop handle on its named shard.
  RawStopFn		rxStop;
  alignas(Zm::CacheLineSize)
  RawStopFn		txStop;
  ZmRef<ZiTxBuf>	oldOutput;
  uint64_t		oldToken = 0;

  template <typename Hub> explicit SwapPeer(Hub *hub) : Base{hub, config()} { }

  auto state() { return this->app()->state; }

  void fixRawUpRx(uint64_t c) {
    if (!this->app()->rxInvoked()) state()->fail();
    if constexpr (Client) {
      if (c != 1) state()->fail();
      ++state()->clientUps; state()->clientUp.post();
    } else {
      if (c != state()->serverUps.xchAdd(1) + 1) state()->fail();
      state()->serverUp.post();
    }
  }

  Fmt::Header header() {
    Fmt::Header h; h.beginString = "FIX.9.9";
    h.sender = Client ? "EXTERNAL-C" : "EXTERNAL-S";
    h.target = Client ? "EXTERNAL-S" : "EXTERNAL-C";
    h.seqNo = 17; h.sendingTime = {ZuDateTimeScan::FIX{}, "20000101-00:00:00"};
    return h;
  }

  void request() {
    this->app()->rxRun([this] {
      Fmt::Error error; auto h = header();
      if (!this->template send<Entry>(Packet{"req", "A\1\0="_z}, 101, h,
	  {0, this->rawConnection_(), h.seqNo}, true, error)) state()->fail();
    });
  }

  bool fixRawRx(ZmRef<RawInput> msg, Fmt::Limits limits) {
    Fmt::MessageHandler<Packet> handler{msg->frame, limits}; auto p = handler.ctor();
    if (!this->app()->rxInvoked() || !handler.valid() ||
	msg->connection != this->rawConnection_() || p.data != "A\1\0="_z ||
	p.value != (Client ? "res" : "req") || handler.header().seqNo != 17) {
      state()->fail(); return true;
    }
    if constexpr (Client) { ++state()->replies; state()->reply.post(); }
    else {
      Fmt::Error error; auto h = header();
      if (!this->template reply<Entry, Response>(*msg, Packet{"res", p.data}, 201, h,
	  {0, msg->connection, h.seqNo}, true, error)) state()->fail();
    }
    return true;
  }

  bool fixSendTx(ZmRef<ZiIOBuf> b) {
    if constexpr (!Client) if (!oldToken) oldToken = static_cast<WireBuf *>(b.ptr())->token;
    return Base::fixSendTx(ZuMv(b));
  }

  Fmt::Error fixReplyRx(Entry *, Response *, const RawInput &input) {
    return input.connection == this->rawConnection_() ? Fmt::Error{} :
      Fmt::Error{Fmt::ErrorCode::Unresolved, 35};
  }

  void sent(ZmRef<ZiTxBuf> b, bool ok) {
    if constexpr (Client) Base::sent(ZuMv(b), ok);
    else {
      // Keep native socket accounting intact. Hold only this real buffer's Raw
      // completion, which Wire must retire when the next connection is bound.
      if (static_cast<WireBuf *>(b.ptr())->token)
	this->app()->txRun([this, b, ok]() mutable {
	  if (static_cast<WireBuf *>(b.ptr())->token == oldToken) {
	    if (oldOutput || !ok) state()->fail();
	    oldOutput = ZuMv(b); state()->oldOutput.post(); return;
	  }
	  this->transportSent(static_cast<WireBuf *>(b.ptr()), ok);
	});
      using Native = typename Network::template SrvLink<
	typename Impl::Hub, Impl, typename Base::RxBufAlloc, typename Base::TxBufAlloc>;
      Native::sent(ZuMv(b), ok);
    }
  }

  void fixRawCompleteTx(TxBody::Ref, TxContext c, bool ok) {
    if (!this->app()->txInvoked() || c.epoch) state()->fail();
    if constexpr (!Client) {
      if (!ok) {
	if (c.connection != 1) state()->fail();
	++state()->failed;
      } else { ++state()->completed; state()->outputDone.post(); }
    } else if (!ok) state()->fail();
  }

  void fixRawSubmittedTx(RawOutput::Ref, unsigned result) {
    if (result != RawSend::Accepted) state()->fail();
  }

  void fixRawFaultRx(uint64_t, Fmt::Error) { state()->fail(); }
  void fixRawFaultTx(uint64_t, Fmt::Error) { state()->fail(); }

  void fixRawStopRx(uint64_t c, RawStopRequest &request) {
    if (!this->app()->rxInvoked()) state()->fail();
    if constexpr (!Client) {
      ++state()->rxStops;
      if (c == 1) { rxStop = request.defer(); state()->rxPending.post(); }
      if (c == 3) state()->lastClosed.post();
    }
  }

  void fixRawStopTx(uint64_t c, RawStopRequest &request) {
    if (!this->app()->txInvoked()) state()->fail();
    if constexpr (!Client) {
      ++state()->txStops;
      if (c == 1) { txStop = request.defer(); state()->txPending.post(); }
      if (c == 3) state()->lastClosed.post();
    }
  }

  void fixRawStoppedRx(uint64_t c, bool) {
    if (!this->app()->rxInvoked()) state()->fail();
    if constexpr (Client) {
      if (c != 1) state()->fail();
      ++state()->clientStops; state()->clientStopped.post();
    } else {
      if (c != 3) state()->fail();
      ++state()->serverStops; state()->serverStopped.post();
    }
  }

  void releaseRx() {
    this->app()->rxRun([this] {
      auto done = ZuMv(rxStop); if (!done) { state()->fail(); return; }
      done(); done();
      this->app()->rxRun([this] { state()->rxReleased.post(); });
    });
  }

  void releaseRecord() {
    this->app()->txRun([this] {
      auto b = ZuMv(oldOutput); if (!b) { state()->fail(); return; }
      this->transportSent(static_cast<WireBuf *>(b.ptr()), true);
      this->app()->txRun([this] { state()->recordReleased.post(); });
    });
  }

  void releaseTx() {
    this->app()->txRun([this] {
      auto done = ZuMv(txStop); if (!done) { state()->fail(); return; }
      done(); done();
    });
  }
};

struct SwapClient : RawClient<SwapClient, Dictionary, Network> {
  struct Link;
  SwapState	*state;

  explicit SwapClient(SwapState *s) : state{s} { }

  unsigned reconnFreq() const { return 0; }
};

struct SwapClient::Link : ZmNHeap<Link>, SwapPeer<Link,
  RawCliLink<SwapClient, Link, Dictionary, Network>, true> {
friend inline ZuStringT<"Zfix.RawSwap.Client"> ZmNHeapID(Link *);
  using SwapPeer::SwapPeer;
  using ZmNHeap<Link>::operator new;
  using ZmNHeap<Link>::operator delete;
};

struct SwapServer : RawServer<SwapServer, Dictionary, Network> {
  struct Link;
  SwapState	*state;
  ZmRef<Link>	link;

  explicit SwapServer(SwapState *s) : state{s} { }

  ZiConnection *accepted(const ZiCxnInfo &);
  ZiIP localIP() const { return ZiIP{"127.0.0.1"}; }
  unsigned localPort() const { return 0; }
  unsigned nAccepts() const { return 1; }
  void listening(const ZiListenInfo &i) { state->port = i.port; state->listening.post(); }
  void listenFailed(bool) { state->fail(); state->listening.post(); }
};

struct SwapServer::Link : ZmNHeap<Link>, SwapPeer<Link,
  RawSrvLink<SwapServer, Link, Dictionary, Network>, false> {
friend inline ZuStringT<"Zfix.RawSwap.Server"> ZmNHeapID(Link *);
  using Hub = SwapServer;
  using SwapPeer::SwapPeer;
  using ZmNHeap<Link>::operator new;
  using ZmNHeap<Link>::operator delete;
};

ZiConnection *SwapServer::accepted(const ZiCxnInfo &i) {
  if (!link) link = new Link{this};
  return new Link::Cxn{link, i};
}

static void replacement() {
  ZuTestScope(replacement);
  SwapState state; SwapServer server{&state}; SwapClient client{&state};
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(4)
      .thread(1, [](auto &t) { t.isolated(1); }).thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); }).thread(4, [](auto &t) { t.isolated(1); });
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
  server.listen(); ZuCheck(state.listening.timedwait(Zm::now(5)) == 0);
  ZmRef<SwapClient::Link> a = new SwapClient::Link{&client};
  ZmRef<SwapClient::Link> b = new SwapClient::Link{&client};
  ZmRef<SwapClient::Link> c = new SwapClient::Link{&client};
  for (auto link : {a, b, c}) {
#ifdef ZFIX_TEST_TLS
    link->connect("localhost", state.port, ZiIP{"127.0.0.1"});
#else
    link->connect("127.0.0.1", state.port);
#endif
    ZuCheck(state.clientUp.timedwait(Zm::now(5)) == 0);
    ZuCheck(state.serverUp.timedwait(Zm::now(5)) == 0);
    link->request(); ZuCheck(state.reply.timedwait(Zm::now(5)) == 0);
    ZuCheck(state.replies.load_() == state.clientUps.load_());
    if (link == a) ZuCheck(state.oldOutput.timedwait(Zm::now(5)) == 0);
    else ZuCheck(state.outputDone.timedwait(Zm::now(5)) == 0);
    if (link == b) {
      ZuCheck(state.rxPending.timedwait(Zm::now(5)) == 0);
      ZuCheck(state.txPending.timedwait(Zm::now(5)) == 0);
      ZuCheck(!state.serverStops.load_());
      server.link->releaseRx(); ZuCheck(state.rxReleased.timedwait(Zm::now(5)) == 0);
      ZuCheck(!state.serverStops.load_());
    }
  }
  server.link->releaseRecord(); ZuCheck(state.recordReleased.timedwait(Zm::now(5)) == 0);
  c->request(); ZuCheck(state.reply.timedwait(Zm::now(5)) == 0);
  ZuCheck(state.replies.load_() == 4);
  ZuCheck(state.outputDone.timedwait(Zm::now(5)) == 0);
  ZuCheck(state.clientUps.load_() == 3 && state.serverUps.load_() == 3 &&
    state.replies.load_() == 4 && state.failed.load_() == 1 && !state.serverStops.load_());
  c->disconnect();
  for (unsigned i = 0; i < 3; ++i)
    ZuCheck(state.clientStopped.timedwait(Zm::now(5)) == 0);
  // Both actual C stop hooks ran. A's external Tx drain remains outstanding.
  for (unsigned i = 0; i < 2; ++i)
    ZuCheck(state.lastClosed.timedwait(Zm::now(5)) == 0);
  ZuCheck(state.rxStops.load_() == 3 && state.txStops.load_() == 3 &&
    !state.serverStops.load_());
  server.link->releaseTx(); ZuCheck(state.serverStopped.timedwait(Zm::now(5)) == 0);
  bool cstop = client.stop(), sstop = server.stop(); ZuCheck(cstop && sstop);
  client.final(); server.final(); mx.stop(); a = b = c = nullptr; server.link = nullptr;
  ZuCheck(!state.errors.load_() && state.serverStops.load_() == 1 &&
    state.clientStops.load_() == 3 && state.completed.load_() == 3);
}

int main(int argc, char **argv) {
  parse(argc, argv); ZuTestMain();
  ZuTestCall(setupCleanup, false);
  ZuTestCall(setupCleanup, true);
  ZuTestCall(loopback, false, false, false);
  ZuTestCall(loopback, true, true, false);
  ZuTestCall(loopback, false, false, true);
  ZuTestCall(loopback, true, true, false, 2 * (1U << 14) + 1);
  ZuTestCall(replacement);
  return 0;
}
