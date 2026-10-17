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
#include "../src/ZhttpServerImpl.hh"
#include "../src/ZhttpClientImpl.hh"
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
    PeerReset, Trailers, InitialTrailers, StalledTrailers, StopTrailers, QueueTrailers, ResetTrailers
  };
}

enum {
  HighTurnCount = 256, // enough turns to expose per-turn control allocations
  TrailerBytes = 1024, // exceeds the trailer tests' 128-byte stream window
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
  ZmSemaphore	responseNotified;
  ZmAtomic<unsigned> errors = 0;
  ZmAtomic<unsigned> releaseCount = 0;
  ZmAtomic<unsigned> stopCount = 0;
  ZmAtomic<unsigned> stopBeforeRelease = 0;
  ZmAtomic<unsigned> completedCount = 0;
  ZmAtomic<unsigned> producerClosedCount = 0;
  ZmAtomic<unsigned> writerCalls = 0;
  ZmAtomic<unsigned> responseCompletions = 0;
  ZmAtomic<unsigned> trailerProviders = 0;
  ZmAtomic<unsigned> trailerAdmitted = 0;
  ZmAtomic<unsigned> responseNotifies = 0;
  ZmAtomic<unsigned> responseDiscards = 0;
  ZmAtomic<unsigned> responseFailures = 0;
  unsigned responseTrailers = 0;
  bool bodyActive = false; // Tx-owned; close must run outside body(emit)

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
    responseNotified.post();
  }
};

bool trailers(int mode)
{
  return mode == StreamTurn::Trailers ||
    mode == StreamTurn::InitialTrailers || mode == StreamTurn::StalledTrailers ||
    mode == StreamTurn::StopTrailers || mode == StreamTurn::QueueTrailers ||
    mode == StreamTurn::ResetTrailers;
}

struct FinalHdrs : public Zhttp::Builder {
  State *state;
  ZtString<> value = "done";

  FinalHdrs(State *state_) : state{state_} { }
  bool neverIndex(ZuBSpan name) const { return name == "x-final"; }
  template <typename L> void header(L &&l) {
    state->txCallback();
    ++state->trailerProviders;
    l("x-final", value);
    l("x-final", "two");
  }
};

struct App;

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
      if (trailers(state->streamTurn)) {
	if (!emit([this](auto &body) {
	  ++state->writerCalls;
	  FinalHdrs nested{state};
	  if (emit.trailers(nested) || state->trailerProviders.load_())
	    state->fail();
	  unsigned n = (state->streamTurn == StreamTurn::StalledTrailers ||
	    state->streamTurn == StreamTurn::StopTrailers ||
	    state->streamTurn == StreamTurn::ResetTrailers) ? TrailerBytes : 1;
	  if (state->streamTurn == StreamTurn::QueueTrailers) n = 128;
	  for (unsigned i = 0; i < n; ++i) body << 'x';
	  return Zhttp::WriteOutcome::Stream;
	})) {
	  unsigned calls = state->trailerProviders.load_();
	  FinalHdrs fields{state};
	  if (emit.trailers(fields) || state->trailerProviders.load_() != calls)
	    state->fail();
	  return false;
	}
	FinalHdrs fields{state};
	bool accepted = emit.trailers(fields);
	fields.value = "overwritten";
	state->trailerAdmitted = accepted;
	if (state->expectedTransport == Zhttp::Transport::TCP) {
	  if (accepted || state->trailerProviders.load_()) state->fail();
	  return emit([](auto &) { return Zhttp::WriteOutcome::End; });
	}
	return accepted;
      }
	return emit([this](auto &body) {
	  ++state->writerCalls;
	  if (state->streamTurn == StreamTurn::Saturate) {
	    for (unsigned i = 0;
		i < SaturationBytes && !body.failed(); ++i)
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
    state->bodyActive = true;
    struct Leave {
      State *state;
      ~Leave() { state->bodyActive = false; }
    } leave{state};
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
	  if (state->streamTurn == StreamTurn::InitialTrailers) {
	    if (!state->streamEmit()) state->fail();
	  }
	  if (state->streamTurn == StreamTurn::FailedInitial) {
	    FinalHdrs fields{state};
	    if (emit.trailers(fields) || state->trailerProviders.load_())
	      state->fail();
	  }
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
    if (state->bodyActive) state->fail();
    state->streamEmit = {};
    ++state->producerClosedCount;
    state->producerClosed.post();
  }

  void completed(const Zhttp::ResponseResult &result) {
    state->txCallback();
    ++state->responseNotifies;
    switch (result.code) {
      case Zhttp::ResponseCode::Discarded: ++state->responseDiscards; break;
      case Zhttp::ResponseCode::Failed: ++state->responseFailures; break;
      default: break;
    }
    state->responseNotified.post();
  }

  State *state = nullptr;
};
using ResBuilderQ = Zhttp::ResponseQ<ResBuilder_,
  ZuStringT<"Zhttp.Server.Idle.ResBuilder">>;
using ResBuilder = ResBuilderQ::Node;

struct Parser : public Zhttp::Parser {
  using HdrCatalog = Zhttp::DefltHdrCatalog;

  void init(App &app);

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

struct App : public Zhttp::Server<App, zhttpserverstreamtest_::Parser, ResBuilderQ,
  Zhttp::AllProfiles> {
  using HTTP = Zhttp::Server<App, zhttpserverstreamtest_::Parser, ResBuilderQ,
    Zhttp::AllProfiles>;
  using HTTP::connected;
  using HTTP::disconnected;

  App(State *state_) : testState{state_} { }

  void listening(int8_t transport, uint16_t port) {
    if (transport != testState->expectedTransport || port != testState->port)
      testState->fail();
    else
      testState->listening.post();
  }
  void listenFailed(int8_t, bool) { testState->fail(); }
  void connected(int8_t transport) {
    if (transport != testState->expectedTransport)
      testState->fail();
    else
      testState->admitted.post();
  }
  void disconnected(int8_t transport) {
    if (transport != testState->expectedTransport)
      testState->fail();
    else {
      ++testState->releaseCount;
      testState->released.post();
    }
  }

  State	*testState;
};

inline void Parser::init(App &app) { state = app.testState; }

template <typename Profile>
struct Client : public Zhttp::ClientHub<Client<Profile>, Profile> {
  struct Builder :
    public Zhttp::Builder,
    public Zhttp::MessageTraits<Profile>::template Request<
      Builder, true, false> {
  void fields(Zhttp::FieldOutput &out) {
    using B = ZuDecay<decltype(*this)>;
    Zhttp::BuilderFields<B> adapter{this, fieldCatalog};
    adapter.fields(out);
  }
  const Zhttp::CatalogView *fieldCatalog =
    &Zhttp::CatalogData<HdrCatalog>::configure();
  template <typename Key> void headerOffset(uint64_t, unsigned) { }
  bool streamResponse() const { return false; }

    using Base = Zhttp::MessageTraits<Profile>::template Request<
      Builder, true, false>;
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

    // This wire peer deliberately leaves a Content-Length request partial;
    // keep that workload below the complete-request role facade.
    template <typename Stream, typename Rx>
    int process(Stream stream, Rx &rx) {
      return Zhttp::Link_::process(*this->app(), ZuMv(stream), rx, 0);
    }
    template <typename Stream> void peerEnd(Stream stream) {
      Zhttp::Link_::peerEnd(*this->app(), ZuMv(stream), 0);
    }
    template <typename Stream> void error(Stream stream) {
      Zhttp::Link_::error(*this->app(), ZuMv(stream), 0);
    }
    template <typename Rx> int process(Rx &rx) { return Base::process(rx); }

    struct Parser :
      public Zhttp::Parser,
      public Zhttp::MessageTraits<Profile>::template ResponseParser<
	Parser> {
  const Zhttp::CatalogView &catalog() const { return *fieldCatalog; }
  void header(Zhttp::FieldSection::T section, int id,
      ZuBSpan name, ZuSpan<uint8_t> value) {
    int fixed = id < 0 ? -1 : fieldCatalog->valueMatch(id, value);
    Zhttp::parserHeader(*this, section, id, fixed, name, value);
  }
  const Zhttp::CatalogView *fieldCatalog =
    &Zhttp::CatalogData<HdrCatalog>::configure();

      using Base =
	Zhttp::MessageTraits<Profile>::template ResponseParser<
	  Parser>;
      using State = Base::State;
      using HdrCatalog = Zhttp::DefltHdrCatalog;
      using Base::reset;

      void status(unsigned value) { status_ = value; }
      bool bodyInfo(Zhttp::BodyType::T, uint64_t) { return true; }
      template <typename Key>
      void header(Zhttp::FieldSection::T, ZuSpan<uint8_t>) { }
      void header(Zhttp::FieldSection::T section,
	  ZuBSpan name, ZuSpan<uint8_t> value) {
	if (name != "x-final") return;
	unsigned n = state->streamTurn == StreamTurn::StalledTrailers ?
	  TrailerBytes : 1;
	if (section != Zhttp::FieldSection::Trailers ||
	    state->responseBody.length() != n ||
	    value != (state->responseTrailers ? "two" : "done"))
	  state->fail();
	++state->responseTrailers;
      }
      template <typename Rx>
      bool body(Rx &rx) {
	if (state->streamTurn == StreamTurn::StopTrailers ||
	    state->streamTurn == StreamTurn::ResetTrailers) return true;
	return Zhttp::bodyEach(rx, [this](ZuSpan<uint8_t> value) {
	  state->responseBody << value;
	});
      }
      void complete(State::T value) {
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
    if (state->openRequest) {
      this->txRun([link = &link, complete = state->completeRequest]() {
	Builder builder;
	auto tx = link->transmit_(builder);
	builder.begin(tx);
	if (complete) {
	  auto body = builder.body(tx, 1);
	  body << 'x';
	  body.flush();
	  builder.finish(tx);
	  link->finish();
	}
      });
    }
    state->connected.post();
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
  using HTTP = Zhttp::NativeTraits<Profile>;
  using Protocol = HTTP::Protocol;
  state.expectedTransport = HTTP::Transport::ID;
  state.openRequest = true;
  state.streamTurn = StreamResult;
  state.completeRequest = true;
  state.expectResponse = StreamResult == StreamTurn::StreamEnd ||
    StreamResult == StreamTurn::ZeroSequence ||
    StreamResult == StreamTurn::InitialSequence ||
    StreamResult == StreamTurn::HighTurns ||
    StreamResult == StreamTurn::OptionalAbsent ||
    (trailers(StreamResult) && StreamResult != StreamTurn::StopTrailers &&
      StreamResult != StreamTurn::QueueTrailers &&
      StreamResult != StreamTurn::ResetTrailers);
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
    if constexpr (StreamResult == StreamTurn::QueueTrailers)
      tls.maxQueuedFrames(1);
    serverConfig.tls(ZuMv(tls));
  }
  else
    serverConfig.quic(
      Zhttp::QUICConfig{}.certPath(cert).keyPath(key).maxIdleTimeout(10000)
	.maxQueuedFrames(StreamResult == StreamTurn::QueueTrailers ?
	  1 : Zhttp::QUICConfig{}.maxQueuedFrames()));
  bool serverInited =
    app.init(hub, ZuMv(serverConfig));
  ZuCHECK(serverInited, "initialize server");
  bool serverUp = serverInited && app.start();
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
	  .policy(Zhttp::H2Policy::Force)
	  .initialWindowSize((StreamResult == StreamTurn::StalledTrailers ||
	    StreamResult == StreamTurn::StopTrailers ||
	    StreamResult == StreamTurn::QueueTrailers ||
	    StreamResult == StreamTurn::ResetTrailers) ?
	    128 : Zhttp::H2::DefltWindow));
    else
      clientInited =
	listening && client.init(hub, Zhttp::TLSConfig{}.caPath(cert));
  else
    clientInited = listening && client.init(
      hub, Zhttp::QUICConfig{}.caPath(cert).maxIdleTimeout(10000)
	.maxStreamData((StreamResult == StreamTurn::StalledTrailers ||
	    StreamResult == StreamTurn::StopTrailers ||
	    StreamResult == StreamTurn::QueueTrailers ||
	    StreamResult == StreamTurn::ResetTrailers) ?
	  128 : Zhttp::QUICConfig{}.maxStreamData()));
  ZuCHECK(clientInited, "initialize client");
  bool clientUp = clientInited && client.start();
  ZuCHECK(clientUp, "start client");
  using ClientLink = Client<Profile>::Link;
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
    ZuCHECK(app.activeConnections() == 1,
	"server accounts for active link");
  bool requestStarted = admitted &&
    state.requestStarted.timedwait(Zm::now(10)) == 0;
  ZuCHECK(requestStarted, "server admits partial request");
  if (requestStarted)
    ZuCHECK(state.completeRequest || app.activeRequests() == 1,
	"server accounts for admitted request");
  bool producerStarted =
    state.producerStarted.timedwait(Zm::now(10)) == 0;
  ZuCHECK(producerStarted, "asynchronous body producer starts");
  ZuCHECK(!app.retainedBytes(),
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
	StreamResult != StreamTurn::InitialTrailers &&
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
      if (!turned || state.turnOK != (StreamResult != StreamTurn::QueueTrailers))
	turnsOK = false;
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
    StreamResult == StreamTurn::OptionalAbsent ||
    StreamResult == StreamTurn::Trailers ||
    StreamResult == StreamTurn::InitialTrailers ||
    StreamResult == StreamTurn::StalledTrailers;
  if constexpr (CompletedResponse) {
    bool completed =
      state.responseComplete.timedwait(Zm::now(10)) == 0;
    ZuCHECK(completed && state.responseCompletions.load_() == 1,
	"client observes one normally completed streamed response: body=",
	state.responseBody.length(), " trailers=", state.responseTrailers,
	" admitted=", state.trailerAdmitted.load_());
    ZuCSpan expected;
    if constexpr (StreamResult == StreamTurn::StreamEnd)
      expected = "xx";
    else if constexpr (StreamResult == StreamTurn::InitialSequence)
      expected = "xxx";
    if constexpr (StreamResult == StreamTurn::Trailers ||
	StreamResult == StreamTurn::InitialTrailers)
      expected = "x";
    if constexpr (StreamResult == StreamTurn::HighTurns ||
	StreamResult == StreamTurn::StalledTrailers) {
      constexpr unsigned n = StreamResult == StreamTurn::HighTurns ?
	HighTurnCount : TrailerBytes;
      bool bodyOK = state.responseBody.length() == n;
      for (unsigned i = 0; bodyOK && i < n; ++i)
	if (state.responseBody[i] != 'x') bodyOK = false;
      ZuCHECK(bodyOK,
	"high-turn stream preserves exact response-body byte order");
    } else {
      ZuCHECK(state.responseBody == expected,
	"stream turns preserve exact response-body byte order");
    }
    if constexpr (StreamResult == StreamTurn::Trailers ||
	StreamResult == StreamTurn::InitialTrailers ||
	StreamResult == StreamTurn::StalledTrailers) {
      constexpr bool Supported = Profile::HTTPVersion != Zhttp::Version::H1;
      ZuCHECK(state.responseTrailers == (Supported ? 2U : 0U) &&
	state.trailerAdmitted.load_() == unsigned(Supported) &&
	state.trailerProviders.load_() == unsigned(Supported),
	"terminal admission preserves repeated owned fields after complete DATA");
    }
  } else if constexpr (StreamResult == StreamTurn::Idle) {
    ZuCHECK(!state.writerCalls.load_(),
	"idle stream causes no library-driven producer polling");
  }
  if constexpr ((StreamResult == StreamTurn::Abort ||
      StreamResult == StreamTurn::Saturate) && HTTP::Multiplexed)
    ZuCHECK(app.activeConnections() == 1,
      "stream-local termination preserves the multiplexed transport session");

  if constexpr (StreamResult == StreamTurn::StopTrailers)
    ZuCHECK(state.trailerAdmitted.load_() && !state.responseNotifies.load_(),
      "closing producer admission preserves the response behind stalled DATA");

  if constexpr (StreamResult == StreamTurn::ResetTrailers) {
    mx.run([&state, link]() mutable {
      link->disconnect();
      state.turnDone.post();
    }, 4);
    ZuCHECK(state.turnDone.timedwait(Zm::now(10)) == 0 &&
	state.responseNotified.timedwait(Zm::now(10)) == 0 &&
	state.responseNotifies.load_() == 1 && state.responseDiscards.load_() == 1,
      "peer reset drains the accepted terminal command and response once");
  }

  ZmAtomic<unsigned> stopReturned = 0;
  if (serverInited) app.stop([
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
  if (!stopped && serverInited) (void)app.stop();
  ZuCHECK(stopped, "active server stop completes");
  ZuCHECK(state.stopCount.load_() == 1,
    "active server stop callback completes exactly once");
  ZuCHECK(!state.stopBeforeRelease.load_(),
    "active server admission releases before stop completion");
  ZuCHECK(state.completedCount.load_() == 1,
    "active server stop completes partial request exactly once");
  ZuCHECK(app.activeRequests() == 0,
	"active server stop releases request admission");
  ZuCHECK(!app.serverFaults() && !app.parseFailures() &&
	!app.bodyFailures() &&
	app.responseBuildFailures() ==
	  unsigned(StreamResult == StreamTurn::Failed ||
	    StreamResult == StreamTurn::FailedInitial),
	"stream outcome has the expected build-failure accounting");
  ZuCHECK(app.transportFailures() ==
      unsigned(StreamResult == StreamTurn::Saturate ||
	StreamResult == StreamTurn::QueueTrailers),
    "stream outcome has the expected transport-failure accounting");
  if constexpr (StreamResult == StreamTurn::StopTrailers)
    ZuCHECK(state.responseNotifies.load_() == 1 && state.responseDiscards.load_() == 1,
      "stop drains the pending terminal command and discards its response once");
  if constexpr (StreamResult == StreamTurn::QueueTrailers)
    ZuCHECK(!state.trailerAdmitted.load_() && state.trailerProviders.load_() == 1 &&
	state.responseNotifies.load_() == 1 && state.responseFailures.load_() == 1,
      "terminal queue refusal after rendering fails its response once");
  if (clientInited) (void)client.stop();
  ZuCHECK(state.disconnected.timedwait(Zm::now(10)) == 0,
    "client stop completes active disconnect");
  ZuCHECK(app.activeConnections() == 0,
	"active server stop drains admission");

  link = nullptr;
  if (clientInited) client.final();
  if (serverInited) app.final();
  ZiResolver::stop();
  ZiResolver::final();
  mx.stop();
  if constexpr (StreamResult == StreamTurn::HighTurns ||
      StreamResult == StreamTurn::Trailers ||
      StreamResult == StreamTurn::InitialTrailers ||
      StreamResult == StreamTurn::StalledTrailers ||
      StreamResult == StreamTurn::StopTrailers ||
      StreamResult == StreamTurn::QueueTrailers ||
      StreamResult == StreamTurn::ResetTrailers)
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
  ZuTestCall((activeStop<Zhttp::H1TCP, StreamTurn::Trailers>));
  ZuTestCall((activeStop<Zhttp::H2TLS, StreamTurn::Trailers>));
  ZuTestCall((activeStop<Zhttp::H3QUIC, StreamTurn::Trailers>));
  ZuTestCall((activeStop<Zhttp::H2TLS, StreamTurn::InitialTrailers>));
  ZuTestCall((activeStop<Zhttp::H3QUIC, StreamTurn::InitialTrailers>));
  ZuTestCall((activeStop<Zhttp::H2TLS, StreamTurn::StalledTrailers>));
  ZuTestCall((activeStop<Zhttp::H3QUIC, StreamTurn::StalledTrailers>));
  ZuTestCall((activeStop<Zhttp::H2TLS, StreamTurn::StopTrailers>));
  ZuTestCall((activeStop<Zhttp::H3QUIC, StreamTurn::StopTrailers>));
  ZuTestCall((activeStop<Zhttp::H2TLS, StreamTurn::QueueTrailers>));
  ZuTestCall((activeStop<Zhttp::H3QUIC, StreamTurn::QueueTrailers>));
  ZuTestCall((activeStop<Zhttp::H2TLS, StreamTurn::ResetTrailers>));
  ZuTestCall((activeStop<Zhttp::H3QUIC, StreamTurn::ResetTrailers>));
  // One normal multi-turn stream and one abort per wire protocol.
  ZuTestCall((activeStop<Zhttp::H1TCP, StreamTurn::StreamEnd>));
  ZuTestCall((activeStop<Zhttp::H2TLS, StreamTurn::StreamEnd>));
  ZuTestCall((activeStop<Zhttp::H3QUIC, StreamTurn::StreamEnd>));
  ZuTestCall((activeStop<Zhttp::H1TCP, StreamTurn::Abort>));
  ZuTestCall((activeStop<Zhttp::H2TLS, StreamTurn::Abort>));
  ZuTestCall((activeStop<Zhttp::H3QUIC, StreamTurn::Abort>));
  ZuTestCall((activeStop<Zhttp::H2TLS, StreamTurn::AbortInitial>));
  ZuTestCall((activeStop<Zhttp::H3QUIC, StreamTurn::AbortInitial>));
  // An idle producer exercises cancellation during server teardown.
  ZuTestCall((activeStop<Zhttp::H1TCP>));
  ZuTestCall((activeStop<Zhttp::H2TLS>));
  ZuTestCall((activeStop<Zhttp::H3QUIC>));
  // Common state-machine behavior only needs one transport instance.
  ZuTestCall((activeStop<Zhttp::H2TLS, StreamTurn::InitialSequence>));
  ZuTestCall((activeStop<Zhttp::H3QUIC, StreamTurn::InitialSequence>));
  ZuTestCall((activeStop<Zhttp::H2TLS, StreamTurn::ZeroSequence>));
  ZuTestCall((activeStop<Zhttp::H2TLS, StreamTurn::HighTurns>));
  ZuTestCall((activeStop<Zhttp::H1TCP, StreamTurn::OptionalAbsent>));
  ZuTestCall((activeStop<Zhttp::H1TCP, StreamTurn::Failed>));
  ZuTestCall((activeStop<Zhttp::H2TLS, StreamTurn::FailedInitial>));
  ZuTestCall((activeStop<Zhttp::H2TLS, StreamTurn::Saturate>));
  ZuTestCall((activeStop<Zhttp::H2TLS, StreamTurn::PeerReset>));
  return 0;
}
