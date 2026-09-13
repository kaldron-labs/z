//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Zhttp server stream lifecycle integration test

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuID.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZtcHeap.hh>

#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiResolver.hh>

#include <zlib/ZhttpClient.hh>
#include <zlib/ZhttpServer.hh>

#include "ZhttpTestUtil.hh"
#include "ZhttpITestPorts.hh"

using namespace ZuTestUtil;

namespace zhttpserverstreamtest_ {

using RequestHeaderList = ZhttpHeaders("content-length");
ZhttpHdrCatalogDerive(RequestHeaders, RequestHeaderList);
ZhttpHdrCatalogImpl(RequestHeaders)

using StreamEmitFn = ZmFn<bool(), ZmFnHeapID<"Zhttp.Server.Emit">>;

namespace StreamTurn {
  enum {
    Idle, End, Abort, AbortInitial, Failed, FailedInitial, StreamEnd,
    ZeroSequence, InitialSequence, HighTurns, Saturate, OptionalAbsent,
    PeerReset
  };
}

enum {
  HighTurnCount = 256, // enough turns to expose per-turn control allocations
  SaturationBytes = 16<<20 // exceeds socket and configured frame queues
};

struct HeapCounts {
  bool operator ==(const HeapCounts &) const = default;

  uint64_t emitter = 0;
  uint64_t driver = 0;
  uint64_t task = 0;
};

HeapCounts streamHeapCounts()
{
  HeapCounts counts;
  Ztc::HeapMgr::all(Ztc::HeapMgr::AllFn{
    [&counts](Ztc::Heap *heap) {
      Ztc::HeapTelemetry data;
      heap->telemetry(data);
      ZuCSpan id{data.id};
      if (id == "Zhttp.Server.BodyEmit")
	counts.emitter += data.allocated();
      else if (id == "Zhttp.Server.StreamBody")
	counts.driver += data.allocated();
      else if (id == "Zhttp.StreamTask")
	counts.task += data.allocated();
    }});
  return counts;
}

struct State {
  ZiMultiplex	*mx = nullptr;
  ZmSemaphore	listening;
  ZmSemaphore	admitted;
  ZmSemaphore	connected;
  ZmSemaphore	requestStarted;
  ZmSemaphore	producerStarted;
  ZmSemaphore	disconnected;
  ZmSemaphore	released;
  ZmSemaphore	stopped;
  ZmSemaphore	completed;
  ZmSemaphore	producerClosed;
  ZmSemaphore	turnDone;
  ZmSemaphore	responseComplete;
  ZmAtomic<unsigned> errors = 0;
  ZmAtomic<unsigned> releaseCount = 0;
  ZmAtomic<unsigned> stopCount = 0;
  ZmAtomic<unsigned> stopBeforeRelease = 0;
  ZmAtomic<unsigned> completedCount = 0;
  ZmAtomic<unsigned> producerClosedCount = 0;
  ZmAtomic<unsigned> writerCalls = 0;
  ZmAtomic<unsigned> responseCompletions = 0;
  ZtString<>	responseBody;
  uint16_t	port = 0;
  int8_t	expectedTransport = Zhttp::Transport::QUIC;
  bool		openRequest = false;
  bool		completeRequest = false;
  bool		checkThreads = false;
  int8_t	streamTurn = StreamTurn::Idle;
  bool		turnOK = false;
  bool		expectResponse = false;
  StreamEmitFn	streamEmit;
  StreamEmitFn	lateEmit;

  void rxCallback() {
    if (checkThreads && (!mx || !mx->invoked(3))) fail();
  }
  void txCallback() {
    if (checkThreads && (!mx || !mx->invoked(4))) fail();
  }

  void responseCallback() {
    txCallback();
  }

  void fail() {
    errors = 1;
    listening.post();
    admitted.post();
    connected.post();
    requestStarted.post();
    producerStarted.post();
    disconnected.post();
    released.post();
    stopped.post();
    completed.post();
    producerClosed.post();
    turnDone.post();
    responseComplete.post();
  }
};

struct App {
  struct ResBuilder_ : public ZmObject, public Zhttp::ResBuilder {
    struct EmitProbe {
      template <typename Body>
      Zhttp::WriteOutcome::T operator ()(Body &) const;
    };

    template <typename Emit>
    struct Producer : public ZmPolymorph {
      Producer(State *state_, Emit emit_) :
	state{state_}, emit{ZuMv(emit_)} { }
      bool resume() {
	return emit([this](auto &body) {
	  ++state->writerCalls;
	  if (state->streamTurn == StreamTurn::Saturate) {
	    for (unsigned i = 0;
		i < SaturationBytes && body.valid(); ++i)
	      body << 'x';
	  } else if (state->streamTurn != StreamTurn::ZeroSequence &&
	      state->streamTurn != StreamTurn::FailedInitial) {
	    body << 'x';
	  }
	  switch (state->streamTurn) {
	    case StreamTurn::Abort: return Zhttp::WriteOutcome::Abort;
	    case StreamTurn::Failed: return Zhttp::WriteOutcome::Failed;
	    case StreamTurn::StreamEnd: return Zhttp::WriteOutcome::Stream;
	    case StreamTurn::ZeroSequence:
	      return state->writerCalls.load_() < 2 ?
		Zhttp::WriteOutcome::Stream : Zhttp::WriteOutcome::End;
	    case StreamTurn::InitialSequence:
	      return state->writerCalls.load_() < 3 ?
		Zhttp::WriteOutcome::Stream : Zhttp::WriteOutcome::End;
	    case StreamTurn::HighTurns:
	      return state->writerCalls.load_() < HighTurnCount ?
		Zhttp::WriteOutcome::Stream : Zhttp::WriteOutcome::End;
	    case StreamTurn::Saturate: return Zhttp::WriteOutcome::Stream;
	    default: return Zhttp::WriteOutcome::End;
	  }
	});
      }
      State *state;
      Emit emit;
    };

    using HdrCatalog = Zhttp::DefltHdrCatalog;
    Zhttp::BodyPolicy::T bodyPolicy() const {
      return state->streamTurn == StreamTurn::OptionalAbsent ?
	Zhttp::BodyPolicy::OptionalStream : Zhttp::BodyPolicy::Stream;
    }
    Zhttp::Method::T method() const { return Zhttp::Method::POST; }
    unsigned status() const { state->responseCallback(); return 200; }
    template <typename Key, typename L> void header(L &&) const {
      state->responseCallback();
    }
    template <typename L> void header(L &&) const {
      state->responseCallback();
    }
    bool disconnect() const { state->responseCallback(); return false; }
    template <typename Emit>
    void body(Emit &&emit) {
      state->responseCallback();
      using EmitResult = decltype(
	ZuDeclVal<Emit &>()(ZuDeclVal<EmitProbe>()));
      if constexpr (ZuIsSame<EmitResult, bool>{}) {
	  if (state->streamTurn == StreamTurn::OptionalAbsent) {
	    state->producerStarted.post();
	    return;
	  }
	  using Producer_ = Producer<ZuDecay<Emit>>;
	  ZmRef<Producer_> producer = new Producer_{state, emit};
	  StreamEmitFn resume{StreamEmitFn::fn(
	    ZuMv(producer), [](Producer_ *producer_) {
	      return producer_->resume();
	    })};
	  state->streamEmit = resume;
	  state->lateEmit = ZuMv(resume);
	  emit([this](auto &) {
	    state->producerStarted.post();
	    if (state->streamTurn == StreamTurn::AbortInitial)
	      return Zhttp::WriteOutcome::Abort;
	    if (state->streamTurn == StreamTurn::FailedInitial)
	      return Zhttp::WriteOutcome::Failed;
	    return Zhttp::WriteOutcome::Stream;
	  });
	  if (state->streamTurn == StreamTurn::InitialSequence) {
	    bool first = state->streamEmit();
	    bool second = state->streamEmit();
	    bool third = state->streamEmit();
	    bool late = state->streamEmit();
	    if (!first || !second || !third || late) state->fail();
	  }
	return;
      }
      emit([](auto &body) {
	body << 'x';
	return Zhttp::WriteOutcome::End;
      });
    }
    void close() {
      state->responseCallback();
      state->streamEmit = {};
      ++state->producerClosedCount;
      state->producerClosed.post();
    }

    State *state = nullptr;
  };
  using ResBuilderQ = ZmList<ResBuilder_,
    ZmListNode<ResBuilder_, ZmListHeapID<"Zhttp.Server.Idle.ResBuilder">>>;
  using ResBuilder = ResBuilderQ::Node;

  struct Parser : public Zhttp::Parser {
    using HdrCatalog = Zhttp::DefltHdrCatalog;

    void init(App &app) { state = app.state; }

    bool operation(Zhttp::Method::T, Zhttp::Target &) {
      if (state) {
	state->rxCallback();
	state->requestStarted.post();
      }
      return true;
    }
    bool bodyInfo(Zhttp::BodyType::T, uint64_t) {
      if (state) state->rxCallback();
      return true;
    }
    template <typename Key>
    void header(Zhttp::FieldSection::T, ZuSpan<uint8_t>) {
      if (state) state->rxCallback();
    }
    template <typename Rx>
    bool body(Rx &rx) {
      if (state) state->rxCallback();
      return Zhttp::bodyDrain(rx);
    }
    template <typename Link>
    void complete(Link *link, bool ok) {
      if (state) state->rxCallback();
      if (ok) {
	ZmRef<ResBuilder> response = new ResBuilder{};
	response->state = state;
	link->send(ZuMv(response));
      }
      if (state->openRequest) {
	++state->completedCount;
	state->completed.post();
      }
    }

    void reset() { state = nullptr; }

    State *state = nullptr;
  };

  App(State *state_) : state{state_} { }

  void listening(int8_t transport, uint16_t port) {
    if (transport != state->expectedTransport || port != state->port)
      state->fail();
    else
      state->listening.post();
  }
  void listenFailed(int8_t, bool) { state->fail(); }
  void connected(int8_t transport) {
    if (transport != state->expectedTransport)
      state->fail();
    else
      state->admitted.post();
  }
  void disconnected(int8_t transport) {
    if (transport != state->expectedTransport)
      state->fail();
    else {
      ++state->releaseCount;
      state->released.post();
    }
  }

  State	*state;
};

using Server = Zhttp::Server<App>;

template <typename Profile>
struct Client : public Zhttp::ClientHub<Client<Profile>, Profile> {
  struct Builder :
    public Zhttp::Builder,
    public Zhttp::MessageTraits<Profile>::template Request<
      Builder, RequestHeaders, true, false> {
    using Base = typename Zhttp::MessageTraits<Profile>::template Request<
      Builder, RequestHeaders, true, false>;
    using HdrCatalog = RequestHeaders;
    using Base::body;

    template <typename L>
    void operation(L &&l) const {
      l(Zhttp::Method::POST, [](auto &&emit) {
	emit([](auto &tx) { tx << '/'; });
      });
    }
    template <typename L>
    void host(L &&l) const { l("localhost"); }
    template <typename Key, typename L>
    void header(L &&l) const {
      if constexpr (Key{}() == "content-length") l("1");
    }
    template <typename L> void header(L &&) const { }
  };

  struct Link :
    public Zhttp::ClientLink<Client, Link, Profile> {
    using Base = Zhttp::ClientLink<Client, Link, Profile>;
    using Base::Base;

    struct Parser :
      public Zhttp::Parser,
      public Zhttp::MessageTraits<Profile>::template ResponseParser<
	Parser, Zhttp::DefltHdrCatalog> {
      using Base =
	typename Zhttp::MessageTraits<Profile>::template ResponseParser<
	  Parser, Zhttp::DefltHdrCatalog>;
      using State = typename Base::State;
      using HdrCatalog = Zhttp::DefltHdrCatalog;
      using Base::reset;

      void status(unsigned value) { status_ = value; }
      bool bodyInfo(Zhttp::BodyType::T, uint64_t) { return true; }
      template <typename Key>
      void header(Zhttp::FieldSection::T, ZuSpan<uint8_t>) { }
      template <typename Rx>
      bool body(Rx &rx) {
	return Zhttp::bodyEach(rx, [this](ZuSpan<uint8_t> value) {
	  state->responseBody << value;
	});
      }
      void complete(typename State::T value) {
	if (state->expectResponse &&
	    (value != State::Complete || status_ != 200))
	  state->fail();
	++state->responseCompletions;
	state->responseComplete.post();
      }
      bool finReceived() const { return false; }

      zhttpserverstreamtest_::State *state = nullptr;
      unsigned status_ = 0;
    } parser;

    unsigned id = 0;
  };

  Client(State *state_) : state{state_} { }

  void connected(Link &link, const Zhttp::ConnectedInfo &) {
    state->connected.post();
    if (state->openRequest) {
      Builder builder;
      auto tx = link.transmit(builder);
      builder.begin(tx);
      if (state->completeRequest) {
	auto body = builder.body(tx, 1);
	body << 'x';
	body.flush();
	builder.finish(tx);
	link.finish();
      }
    }
  }
  void disconnected(Link &, bool) {
    state->disconnected.post();
  }
  void connectFailed(Link &, bool) { state->fail(); }
  template <typename Rx>
  int process(Link &link, Rx &rx) {
    auto parserState = link.receive(link.parser, rx);
    if (parserState == Link::Parser::State::Error) {
      if (state->expectResponse) state->fail();
      return -1;
    }
    return parserState == Link::Parser::State::Complete;
  }

  State	*state;
};

ZiMxParams mxParams()
{
  return ZiMxParams()
    .scheduler([](auto &s) {
      s.nThreads(4)
	.thread(1, [](auto &t) { t.isolated(1); })
	.thread(2, [](auto &t) { t.isolated(1); })
	.thread(3, [](auto &t) { t.isolated(1); })
	.thread(4, [](auto &t) { t.isolated(1); });
    })
    .rxThread(1).txThread(2);
}

template <typename Profile, int StreamResult = StreamTurn::Idle>
void activeStop()
{
  ZuTestScope(activeStop);

  Zhttp::Test::TempDir temp;
  bool tempOK = temp.init("ZhttpServerStop");
  ZuCHECK(tempOK, "create temporary directory");
  if (!tempOK) return;
  ZtString<> cert, key;
  bool certOK = Zhttp::Test::writeLocalhostCert(temp, cert, key);
  ZuCHECK(certOK, "create localhost certificate");
  if (!certOK) return;

  State state;
  static unsigned nextPort = ZhttpITestPort::ServerStream;
  state.port = Zhttp::Test::loopbackPort(nextPort++);
  using HTTP = Zhttp::ProfileTraits<Profile>;
  using Protocol = typename HTTP::Protocol;
  state.expectedTransport = HTTP::Transport::ID;
  state.openRequest = true;
  state.streamTurn = StreamResult;
  state.completeRequest = true;
  state.expectResponse = StreamResult == StreamTurn::StreamEnd ||
    StreamResult == StreamTurn::ZeroSequence ||
    StreamResult == StreamTurn::InitialSequence ||
    StreamResult == StreamTurn::HighTurns ||
    StreamResult == StreamTurn::OptionalAbsent;
  HeapCounts heapBase = streamHeapCounts();
  ZuCHECK(state.port, "allocate loopback port");
  if (!state.port) return;

  ZiMultiplex mx{mxParams()};
  state.mx = &mx;
  state.checkThreads = true;
  bool mxUp = mx.start();
  ZuCHECK(mxUp, "start multiplexer");
  if (!mxUp) return;
  Zhttp::HubConfig hub{&mx, "3", "4"};

  App app{&state};
  Server server;
  auto serverConfig = Zhttp::ServerConfig()
    .localIP(ZiIP{"127.0.0.1"}).port(state.port)
    .maxRequests(1).retainedBytesMax(17);
  if constexpr (ZuIsSame<Protocol, Zhttp::TCP>{})
    serverConfig.tcp();
  else if constexpr (ZuIsSame<Protocol, Zhttp::TLS>{})
  {
    auto tls = Zhttp::H2Config{}.certPath(cert).keyPath(key)
      .policy(Zhttp::H2Policy::Prefer);
    if constexpr (StreamResult == StreamTurn::Saturate)
      tls.maxQueuedFrames(2);
    serverConfig.tls(ZuMv(tls));
  }
  else
    serverConfig.quic(
      Zhttp::QUICConfig{}.certPath(cert).keyPath(key).maxIdleTimeout(10000));
  bool serverInited =
    server.init(hub, ZuMv(serverConfig), &app);
  ZuCHECK(serverInited, "initialize server");
  bool serverUp = serverInited && server.start();
  ZuCHECK(serverUp, "start server");
  bool listening = serverUp &&
    state.listening.timedwait(Zm::now(10)) == 0;
  ZuCHECK(listening, "server listens for HTTP/3");

  Client<Profile> client{&state};
  bool clientInited = false;
  if constexpr (ZuIsSame<Protocol, Zhttp::TCP>{})
    clientInited = listening && client.init(hub, Zhttp::TCPConfig{});
  else if constexpr (ZuIsSame<Protocol, Zhttp::TLS>{})
    if constexpr (HTTP::Multiplexed)
      clientInited = listening && client.init(
	hub, Zhttp::H2Config{}.caPath(cert)
	  .policy(Zhttp::H2Policy::Force));
    else
      clientInited =
	listening && client.init(hub, Zhttp::TLSConfig{}.caPath(cert));
  else
    clientInited = listening && client.init(
      hub, Zhttp::QUICConfig{}.caPath(cert).maxIdleTimeout(10000));
  ZuCHECK(clientInited, "initialize client");
  bool clientUp = clientInited && client.start();
  ZuCHECK(clientUp, "start client");
  using ClientLink = typename Client<Profile>::Link;
  ZmRef<ClientLink> link;
  if (clientUp) link = new ClientLink{&client};
#ifdef ZmObject_DEBUG
  if (link) {
    if constexpr (HTTP::Multiplexed ||
	ZuIsSame<Protocol, Zhttp::QUIC>{})
      link->ZmObject::debug();
    else
      link->ZmPolymorph::debug();
  }
#endif
  if (link) link->parser.state = &state;
  if (link) link->connect("localhost", state.port);
  bool connected = link &&
    state.connected.timedwait(Zm::now(10)) == 0;
  ZuCHECK(connected, "establish active HTTP/3 logical link");
  bool admitted = connected &&
    state.admitted.timedwait(Zm::now(10)) == 0;
  ZuCHECK(admitted, "server admits active link");
  if (admitted)
    ZuCHECK(server.activeConnections() == 1,
	"server accounts for active link");
  bool requestStarted = admitted &&
    state.requestStarted.timedwait(Zm::now(10)) == 0;
  ZuCHECK(requestStarted, "server admits partial request");
  if (requestStarted)
    ZuCHECK(state.completeRequest || server.activeRequests() == 1,
	"server accounts for admitted request");
  bool producerStarted =
    state.producerStarted.timedwait(Zm::now(10)) == 0;
  ZuCHECK(producerStarted, "asynchronous body producer starts");
  ZuCHECK(!server.retainedBytes(),
    "streaming response retains no transactional body bytes");
  if constexpr (StreamResult == StreamTurn::HighTurns) {
    HeapCounts open = streamHeapCounts();
    ZuCHECK(open.emitter == heapBase.emitter + 1 &&
	open.driver == heapBase.driver + 1 &&
	open.task == heapBase.task + 1,
      "one emitter, driver, and task are allocated per open stream");
  }
  if constexpr (StreamResult == StreamTurn::PeerReset) {
    mx.run([&state, link]() mutable {
      link->disconnect();
      state.turnDone.post();
    }, 4);
    ZuCHECK(state.turnDone.timedwait(Zm::now(10)) == 0,
      "peer resets the active response stream");
  }
  if constexpr (StreamResult != StreamTurn::Idle &&
	StreamResult != StreamTurn::InitialSequence &&
	StreamResult != StreamTurn::AbortInitial &&
	StreamResult != StreamTurn::FailedInitial &&
	StreamResult != StreamTurn::OptionalAbsent &&
	StreamResult != StreamTurn::PeerReset) {
    unsigned turns = 1;
    if constexpr (StreamResult == StreamTurn::StreamEnd ||
	StreamResult == StreamTurn::ZeroSequence)
      turns = 2;
    else if constexpr (StreamResult == StreamTurn::HighTurns)
      turns = HighTurnCount;
    bool turnsOK = true;
    for (unsigned i = 0; i < turns; ++i) {
      StreamEmitFn turn{state.streamEmit};
      mx.run([&state, turn = ZuMv(turn)]() mutable {
	state.turnOK = turn && turn();
	state.turnDone.post();
      }, 4);
      bool turned = state.turnDone.timedwait(Zm::now(10)) == 0;
      if (!turned || !state.turnOK) turnsOK = false;
      if (state.streamTurn == StreamTurn::StreamEnd)
	state.streamTurn = StreamTurn::End;
      if constexpr (StreamResult == StreamTurn::HighTurns)
	if (i + 1 == HighTurnCount / 2) {
	  HeapCounts middle = streamHeapCounts();
	  if (middle.emitter != heapBase.emitter + 1 ||
	      middle.driver != heapBase.driver + 1 ||
	      middle.task != heapBase.task + 1)
	    turnsOK = false;
	}
    }
    ZuCHECK(turnsOK,
      "retained emitter accepts all dispatched response turns");
  }
  constexpr bool CompletedResponse =
    StreamResult == StreamTurn::StreamEnd ||
    StreamResult == StreamTurn::ZeroSequence ||
    StreamResult == StreamTurn::InitialSequence ||
    StreamResult == StreamTurn::HighTurns ||
    StreamResult == StreamTurn::OptionalAbsent;
  if constexpr (CompletedResponse) {
    bool completed =
      state.responseComplete.timedwait(Zm::now(10)) == 0;
    ZuCHECK(completed && state.responseCompletions.load_() == 1,
	"client observes one normally completed streamed response");
    ZuCSpan expected;
    if constexpr (StreamResult == StreamTurn::StreamEnd)
      expected = "xx";
    else if constexpr (StreamResult == StreamTurn::InitialSequence)
      expected = "xxx";
    if constexpr (StreamResult == StreamTurn::HighTurns) {
      bool bodyOK = state.responseBody.length() == HighTurnCount;
      for (unsigned i = 0; bodyOK && i < HighTurnCount; ++i)
	if (state.responseBody[i] != 'x') bodyOK = false;
      ZuCHECK(bodyOK,
	"high-turn stream preserves exact response-body byte order");
    } else {
      ZuCHECK(state.responseBody == expected,
	"stream turns preserve exact response-body byte order");
    }
  } else if constexpr (StreamResult == StreamTurn::Idle) {
    ZuCHECK(!state.writerCalls.load_(),
	"idle stream causes no library-driven producer polling");
  }
  if constexpr ((StreamResult == StreamTurn::Abort ||
      StreamResult == StreamTurn::Saturate) && HTTP::Multiplexed)
    ZuCHECK(server.activeConnections() == 1,
      "stream-local termination preserves the multiplexed transport session");

  ZmAtomic<unsigned> stopReturned = 0;
  if (serverInited) server.stop([
    &state, &stopReturned
  ](bool ok) {
    if (!ok) state.errors = 1;
    if (!stopReturned.load_()) state.errors = 1;
    if (state.releaseCount.load_() != 1)
      ++state.stopBeforeRelease;
    ++state.stopCount;
    state.stopped.post();
  });
  stopReturned = 1;
  bool stoppedEarly = false;
  if (state.stopped.trywait() == 0)
    stoppedEarly = state.stopCount.load_();
  bool cancelled = state.producerClosedCount.load_();
  if (!cancelled)
    cancelled = state.producerClosed.timedwait(Zm::now(10)) == 0;
  ZuCHECK(cancelled,
    "terminal path closes body production before producer release");
  ZuCHECK(state.producerClosedCount.load_() == 1,
    "streaming Builder close runs exactly once");
  StreamEmitFn done{ZuMv(state.lateEmit)};
  if (done) {
    unsigned writerCalls = state.writerCalls.load_();
    mx.run([&state, done = ZuMv(done)]() mutable {
      state.turnOK = !done();
      state.turnDone.post();
    }, 4);
    bool lateDone = state.turnDone.timedwait(Zm::now(10)) == 0;
    ZuCHECK(lateDone && state.turnOK &&
	state.writerCalls.load_() == writerCalls,
      "closed emitter rejects a late turn without invoking its writer");
  }
  bool stopped = !serverInited || stoppedEarly;
  if (!stopped)
    stopped = state.stopped.timedwait(Zm::now(10)) == 0;
  if (!stopped && serverInited) (void)server.stop();
  ZuCHECK(stopped, "active server stop completes");
  ZuCHECK(state.stopCount.load_() == 1,
    "active server stop callback completes exactly once");
  ZuCHECK(!state.stopBeforeRelease.load_(),
    "active server admission releases before stop completion");
  ZuCHECK(state.completedCount.load_() == 1,
    "active server stop completes partial request exactly once");
  ZuCHECK(server.activeRequests() == 0,
	"active server stop releases request admission");
  ZuCHECK(!server.serverFaults() && !server.parseFailures() &&
	!server.bodyFailures() &&
	server.responseBuildFailures() ==
	  unsigned(StreamResult == StreamTurn::Failed ||
	    StreamResult == StreamTurn::FailedInitial),
	"stream outcome has the expected build-failure accounting");
  ZuCHECK(server.transportFailures() ==
      unsigned(StreamResult == StreamTurn::Saturate),
    "stream outcome has the expected transport-failure accounting");
  if (clientInited) (void)client.stop();
  ZuCHECK(state.disconnected.timedwait(Zm::now(10)) == 0,
    "client stop completes active disconnect");
  ZuCHECK(server.activeConnections() == 0,
	"active server stop drains admission");

  link = nullptr;
  if (clientInited) client.final();
  if (serverInited) server.final();
  ZiResolver::stop();
  ZiResolver::final();
  mx.stop();
  if constexpr (StreamResult == StreamTurn::HighTurns)
    ZuCHECK(streamHeapCounts() == heapBase,
	"stream teardown recycles emitter, driver, and task");
  ZuCHECK(!state.errors.load_(), "active server stop has no error");
}

} // namespace zhttpserverstreamtest_

int main(int argc, char **argv)
{
  using namespace zhttpserverstreamtest_;

  (void)argc;
  (void)argv;
  ZiTestResidue::init("zhttpserverstreamtest");
  ZuTestMain();
  // One normal multi-turn stream and one abort per wire protocol.
  ZuTestCall((activeStop<Zhttp::H1TCP, StreamTurn::StreamEnd>));
  ZuTestCall((activeStop<Zhttp::H2TLS, StreamTurn::StreamEnd>));
  ZuTestCall((activeStop<Zhttp::H3QUIC, StreamTurn::StreamEnd>));
  ZuTestCall((activeStop<Zhttp::H1TCP, StreamTurn::Abort>));
  ZuTestCall((activeStop<Zhttp::H2TLS, StreamTurn::Abort>));
  ZuTestCall((activeStop<Zhttp::H3QUIC, StreamTurn::Abort>));
  ZuTestCall((activeStop<Zhttp::H2TLS, StreamTurn::AbortInitial>));
  // An idle producer exercises cancellation during server teardown.
  ZuTestCall((activeStop<Zhttp::H1TCP>));
  ZuTestCall((activeStop<Zhttp::H2TLS>));
  ZuTestCall((activeStop<Zhttp::H3QUIC>));
  // Common state-machine behavior only needs one transport instance.
  ZuTestCall((activeStop<Zhttp::H2TLS, StreamTurn::InitialSequence>));
  ZuTestCall((activeStop<Zhttp::H2TLS, StreamTurn::ZeroSequence>));
  ZuTestCall((activeStop<Zhttp::H2TLS, StreamTurn::HighTurns>));
  ZuTestCall((activeStop<Zhttp::H1TCP, StreamTurn::OptionalAbsent>));
  ZuTestCall((activeStop<Zhttp::H1TCP, StreamTurn::Failed>));
  ZuTestCall((activeStop<Zhttp::H2TLS, StreamTurn::FailedInitial>));
  ZuTestCall((activeStop<Zhttp::H2TLS, StreamTurn::Saturate>));
  ZuTestCall((activeStop<Zhttp::H2TLS, StreamTurn::PeerReset>));
  return 0;
}
