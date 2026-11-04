//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Retained ordinary request producers and response/retirement ordering

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuUnion.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZtcHeap.hh>
#include <zlib/ZiResolver.hh>
#include <zlib/Zquic.hh>

#include <zlib/ZhttpClient.hh>
#include <zlib/ZhttpServer.hh>
#include "../src/ZhttpClientImpl.hh"

#include "ZhttpTestUtil.hh"
#include "ZhttpITestPorts.hh"

using namespace ZuTestUtil;

namespace zhttpclientstreamtest_ {

namespace Mode {
  enum { Turns, Single, Replay, Failed, Stop, Early, High, Channel, Restart, Peer };
}
enum { HighTurns = 256 }; // exposes per-turn producer allocation or retention

struct State {
  ZiMultiplex *mx = nullptr;
  ZiMultiplex *srvMx = nullptr;
  ZmSemaphore listening;
  ZmSemaphore opened;
  ZmSemaphore headers;
  ZmSemaphore data;
  ZmSemaphore closed;
  ZmSemaphore completed;
  ZmSemaphore retired;
  ZmSemaphore turn;
  ZmSemaphore stopped;
  ZmSemaphore disposed;
  ZmSemaphore peerClosed;
  ZmSemaphore peerFailed;
  ZmAtomic<unsigned> errors = 0;
  ZmAtomic<unsigned> closeCount = 0;
  ZmAtomic<unsigned> writes = 0;
  ZmAtomic<unsigned> replayReads = 0;
  ZmAtomic<unsigned> rxBytes = 0;
  ZmAtomic<uint64_t> heldBytes = 0;
  ZmAtomic<unsigned> requestEnds = 0;
  ZmAtomic<unsigned> results = 0;
  ZmAtomic<unsigned> retirements = 0;
  ZmAtomic<unsigned> responseEnds = 0;
  ZmAtomic<unsigned> requestFailures = 0;
  ZuUnion<void, Zhttp::BodyEmitter> live;
  ZuUnion<void, Zhttp::BodyEmitter> late;
  Zhttp::Result result;
  Zhttp::ChannelRef channel;
  Zhttp::ChannelRef peerChannel; // server Rx only
  unsigned mode = Mode::Turns;
  bool holdBody = false;
  bool initial = false; // client Tx only
  bool writing = false; // client Tx only
  bool admitted = false; // published by turn semaphore
  bool stopOK = false; // published by stopped semaphore

  void rx() { if (!mx->invoked(1)) ++errors; }
  void tx() { if (!mx->invoked(2)) ++errors; }
  void srvRx() { if (!(srvMx ? srvMx : mx)->invoked(3)) ++errors; }
};

uint64_t producers()
{
  uint64_t count = 0;
  Ztc::HeapMgr::all(Ztc::HeapMgr::AllFn{[&count](Ztc::Heap *heap) {
    Ztc::HeapTelemetry data;
    heap->telemetry(data);
    if (ZuCSpan{data.id} == "Zhttp.Client.Producer") count += data.allocated();
  }});
  return count;
}

using ResponseFields = ZhttpHeaders("content-length");
ZhttpHdrCatalogDerive(ResponseHeaders, ResponseFields);
ZhttpHdrCatalogImpl(ResponseHeaders)
struct Response : public ZmObject, public Zhttp::ResBuilder {
  using HdrCatalog = ResponseHeaders;
  using Zhttp::ResBuilder::header;
  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (Key{}() == "content-length") l("0");
  }
};
using Responses = Zhttp::ResponseQ<Response,
  ZuStringT<"Zhttp.ClientStream.Response">>;

struct Server;
struct ServerParser : public Zhttp::Parser {
  State *state = nullptr;
  Zhttp::Channel *channel = nullptr;
  bool replied = false;

  void init(Server &);
  bool operation(Zhttp::Method::T method, Zhttp::Target &target) {
    return method == Zhttp::Method::POST && target.path == "/stream";
  }
  void reply(Zhttp::Channel *channel) {
    if (replied) return;
    replied = true;
    Zhttp::Route<Responses::Node> route{channel, Zhttp::Method::POST};
    route.send(ZmRef<Responses::Node>{new Responses::Node{}});
  }
  bool bodyInfo(Zhttp::Channel *channel, Zhttp::BodyType::T, uint64_t) {
    state->srvRx();
    this->channel = channel;
    if (state->mode == Mode::Peer) state->peerChannel = channel;
    state->headers.post();
    if (state->mode == Mode::Early && !state->holdBody) reply(channel);
    return true;
  }
  template <typename Rx> bool body(Rx &rx) {
    state->srvRx();
    if (state->holdBody) {
      state->heldBytes = rx.length();
      reply(channel);
      return true;
    }
    return Zhttp::bodyEach(rx, [this](ZuSpan<uint8_t> span) {
      state->rxBytes += span.length();
      state->data.post();
    });
  }
  void complete(Zhttp::Channel *channel, bool ok) {
    state->srvRx();
    if (ok) {
      ++state->requestEnds;
      reply(channel);
    } else {
      ++state->requestFailures;
      state->disposed.post();
    }
  }
  void reset() { state = nullptr; channel = nullptr; replied = false; }
};

using Selection = Zhttp::Profiles<Zhttp::H1TCP, Zhttp::H2TLS, Zhttp::H3QUIC>;
struct Server : public Zhttp::Server<Server, ServerParser, Responses, Selection> {
  using HTTP = Zhttp::Server<Server, ServerParser, Responses, Selection>;
  using HTTP::connected;
  using HTTP::disconnected;
  State *state;
  explicit Server(State *state_) : state{state_} { }
  void connected(int8_t) { state->srvRx(); }
  void disconnected(int8_t) { state->srvRx(); }
  void listening(int8_t, uint16_t) { state->listening.post(); }
  void listenFailed(int8_t, bool) { ++state->errors; state->listening.post(); }
};
void ServerParser::init(Server &server) { state = server.state; }

struct Request : public ZmObject, public Zhttp::ReqBuilder {
  State *state = nullptr;
  uint64_t sequence = 1;

  void connected(Zhttp::Channel *channel, const Zhttp::ConnectedInfo &) {
    state->rx();
    if (state->mode == Mode::Channel) state->channel = channel;
  }
  uint64_t key() const { return sequence; }
  bool replayable() const {
    ++state->replayReads;
    return state->mode == Mode::Replay || state->mode == Mode::Single;
  }
  Zhttp::BodyPolicy::T bodyPolicy() const { return Zhttp::BodyPolicy::Stream; }
  template <typename L> void operation(L &&l) const {
    l(Zhttp::Method::POST, [](auto &&emit) {
      emit([](auto &tx) { tx << "/stream"; });
    });
  }
  void body(Zhttp::BodyEmitter emit) {
    state->tx();
    state->initial = true;
    state->live = emit;
    state->late = emit;
    if (!emit([this](auto &out) {
      ++state->writes;
      state->writing = true;
      if (state->mode == Mode::Single || state->mode == Mode::Restart) out << 'z';
      state->writing = false;
      return state->mode == Mode::Single || state->mode == Mode::Restart ?
	Zhttp::WriteOutcome::End : Zhttp::WriteOutcome::Stream;
    })) ++state->errors;
    state->initial = false;
    state->opened.post();
  }
  void close() {
    state->tx();
    if (state->initial || state->writing) ++state->errors;
    state->live = {};
    ++state->closeCount;
    state->closed.post();
  }
  void completed(const Zhttp::Result &result) {
    state->rx();
    state->result = result;
    ++state->results;
    state->completed.post();
  }
  void retired() {
    state->rx();
    ++state->retirements;
    state->retired.post();
  }
};
ZmPQueueDerive(Requests, Request,
  ZmPQueueOverlap<false, ZmPQueueNode<Request,
    ZmPQueueHeapID<"Zhttp.ClientStream.Request">>>);

struct ResponseParser : public Zhttp::Parser {
  State *state = nullptr;
  unsigned code = 0;

  void init(const Request &request) { state = request.state; }
  void status(unsigned value) { code = value; }
  bool bodyInfo(Zhttp::BodyType::T, uint64_t) { return true; }
  template <typename Rx> bool body(Rx &rx) { return Zhttp::bodyDrain(rx); }
  void complete(Zhttp::Channel *, bool ok) {
    state->rx();
    if (ok && code != 200) ++state->errors;
    if (ok) ++state->responseEnds;
    else if (state->mode == Mode::Peer) state->peerFailed.post();
  }
  void reset() { state = nullptr; code = 0; }
};

struct Client;
struct Pool;
using TxQ = ZmPQTx<Pool, Requests, ZmPQTxOrdered<false>>;
struct Pool : public ZmNHeap<Pool>,
    public Zhttp::Pool<Client, TxQ, ResponseParser, Selection> {
  friend inline ZuStringT<"Zhttp.ClientStream.Pool"> ZmNHeapID(Pool *);
  Requests requests;
  explicit Pool(Client *);
  Requests *txQueue() { return &requests; }
  void archive_(Requests::Node *) { }
  ZmRef<Requests::Node> retrieve_(uint64_t, uint64_t) { return {}; }
};
struct Client : public Zhttp::Client<Client, Pool> { };
Pool::Pool(Client *client) : Zhttp::Pool<Client, TxQ, ResponseParser, Selection>{client} { }

unsigned testPort()
{
  static unsigned next = ZhttpITestPort::ClientStream;
  return Zhttp::Test::loopbackPort(next++);
}

bool await(ZmSemaphore &sem) { return sem.timedwait(Zm::now(10)) == 0; }

void emit(State &state, ZuCSpan bytes, Zhttp::WriteOutcome::T outcome)
{
  state.mx->run([&state, bytes, outcome]() {
    state.tx();
    state.admitted = false;
    if (state.live.is<Zhttp::BodyEmitter>()) {
      auto producer = state.live.p<Zhttp::BodyEmitter>();
      state.admitted = producer([&state, bytes, outcome](auto &out) {
	++state.writes;
	state.writing = true;
	out << bytes;
	state.writing = false;
	return outcome;
      });
    }
    state.turn.post();
  }, 2);
}

bool rejectLate(State &state, bool release = true)
{
  unsigned writes = state.writes.load_();
  state.mx->run([&state, release]() {
    state.admitted = true;
    if (state.late.is<Zhttp::BodyEmitter>()) {
      auto late = state.late.p<Zhttp::BodyEmitter>();
      state.admitted = late([&state](auto &) {
	++state.writes;
	return Zhttp::WriteOutcome::End;
      });
      if (release) state.late = {};
    }
    state.turn.post();
  }, 2);
  return await(state.turn) && !state.admitted && state.writes.load_() == writes;
}

template <typename Profile, unsigned Turn>
void run()
{
  ZuTestScopeRT(run);

  State state;
  state.mode = Turn;
  uint64_t baseline = producers();
  Zhttp::Test::TempDir temp;
  ZtString<> certPath, keyPath;
  bool cert = temp.init("ZhttpClientStream") &&
    Zhttp::Test::writeLocalhostCert(temp, certPath, keyPath);
  ZuCheckRT(cert);
  if (!cert) return;
  uint16_t port = testPort();
  ZuCheckRT(port);
  if (!port) return;
  ZiMultiplex mx{ZiMxParams{}
    .udpBufAllocator<Zquic::PktRxBufAllocator<>>()
    .scheduler([](auto &s) {
    s.nThreads(4);
  }).rxThread(1).txThread(2)};
  state.mx = &mx;
  bool up = mx.start();
  ZuCheckRT(up);
  if (!up) return;
  ZiMultiplex peerMx{ZiMxParams{}
    .udpBufAllocator<Zquic::PktRxBufAllocator<>>()
    .scheduler([](auto &s) {
    s.nThreads(4);
  }).rxThread(3).txThread(4)};
  if constexpr (Turn == Mode::Peer) {
    // Holding client Tx must not also hold the peer's physical I/O queue.
    bool peerUp = peerMx.start();
    ZuCheckRT(peerUp);
    if (!peerUp) { mx.stop(); return; }
    state.srvMx = &peerMx;
  }

  constexpr bool tcp = ZuIsSame<Profile, Zhttp::H1TCP>{};
  constexpr bool h3 = ZuIsSame<Profile, Zhttp::H3QUIC>{};
  Server server{&state};
  auto serverConfig = Zhttp::ServerConfig{}
    .localIP(ZiIP{"127.0.0.1"}).port(port).bodyMax(uint64_t(-1))
    .retainedBodyMax(2);
  if constexpr (tcp) serverConfig.tcp();
  else if constexpr (h3)
    serverConfig.quic(Zhttp::QUICConfig{}.certPath(certPath.cspan())
	.keyPath(keyPath.cspan()).maxStreamData(128));
  else
    serverConfig.tls(Zhttp::H2Config{}.tls([&certPath, &keyPath](auto &tls) {
      tls.certPath(certPath.cspan()).keyPath(keyPath.cspan());
    }).initialWindowSize(128));
  bool srv = server.init(Zhttp::HubConfig{state.srvMx ? state.srvMx : &mx, "3", "4"},
    ZuMv(serverConfig));
  bool listening = srv && server.start() && await(state.listening);
  ZuCheckRT(listening && !state.errors.load_());

  Client client;
  auto config = Zhttp::Config{}.links(1).concurrency(1).linkMax(1)
    .maxRetries(0).maxRedirects(0).secure(!tcp)
    .tcp(tcp).tls(!tcp && !h3).quic(h3).blindH3(h3)
    .protocol(h3 ? Zhttp::ProtoPolicy::ForceH3 : Zhttp::ProtoPolicy::DisableH3)
    .h2Policy(Zhttp::H2Policy::Force);
  bool cli = listening && client.init(Zhttp::HubConfig{&mx, "1", "2"}, 1,
    config, Zhttp::TCPConfig{}, Zhttp::H2Config{}.tls([&certPath](auto &tls) {
      tls.caPath(certPath.cspan());
    })
	.policy(Zhttp::H2Policy::Force),
    Zhttp::QUICConfig{}.caPath(certPath.cspan()));
  bool started = cli && client.pool(0, Zhttp::Destination{"localhost", port}) &&
    client.start();
  ZuCheckRT(started);
  ZmRef<Requests::Node> request;
  if (started) {
    request = new Requests::Node{};
    request->state = &state;
  }
  bool opened = request && client.send(0, request) && await(state.opened);
  ZuCheckRT(opened);

  if constexpr (Turn == Mode::Turns || Turn == Mode::High || Turn == Mode::Failed ||
      Turn == Mode::Stop || Turn == Mode::Channel || Turn == Mode::Peer) {
    bool headers = opened && await(state.headers);
    ZuCheckRT(headers && !state.requestEnds.load_() && !state.closeCount.load_());
    if (headers) {
      emit(state, "ab", Zhttp::WriteOutcome::Stream);
      bool delivered = await(state.turn) && state.admitted && await(state.data);
      ZuCheckRT(delivered && state.rxBytes.load_() == 2 &&
	  !state.requestEnds.load_() && !state.closeCount.load_());
    }
  }
  if constexpr (Turn == Mode::High) {
    bool all = true;
    for (unsigned i = 0; i < HighTurns; ++i) {
      emit(state, "x", Zhttp::WriteOutcome::Stream);
      if (!await(state.turn) || !state.admitted || !await(state.data)) {
	all = false;
	break;
      }
    }
    ZuCheckRT(all && state.rxBytes.load_() == HighTurns + 2 &&
	producers() == baseline + 1 && !state.closeCount.load_());
    emit(state, {}, Zhttp::WriteOutcome::End);
    ZuCheckRT(await(state.turn) && state.admitted);
  } else if constexpr (Turn == Mode::Turns) {
    emit(state, "c", Zhttp::WriteOutcome::End);
    ZuCheckRT(await(state.turn) && state.admitted);
  } else if constexpr (Turn == Mode::Failed) {
    emit(state, {}, Zhttp::WriteOutcome::Failed);
    ZuCheckRT(await(state.turn) && state.admitted);
  }

  if constexpr (Turn == Mode::Channel) {
    ZmSemaphore held, release, requested;
    mx.run([&held, &release]() { held.post(); release.wait(); }, 2);
    bool blocked = await(held);
    ZuCheckRT(blocked);
    if (blocked) {
      // This accepted turn precedes channel cancellation on Tx. The Rx abort
      // must not clear the native link while this writer is still queued.
      emit(state, "d", Zhttp::WriteOutcome::Stream);
      mx.run([&state, &requested]() {
        if (state.channel) state.channel->disconnect();
        else ++state.errors;
        requested.post();
      }, 1);
      ZuCheckRT(await(requested));
      ZuCheckRT(!state.closeCount.load_() && !state.results.load_());
      release.post();
      ZuCheckRT(await(state.turn) && state.admitted);
    }
  }
  if constexpr (Turn == Mode::Peer) {
    ZmSemaphore held, release;
    mx.run([&held, &release]() { held.post(); release.wait(); }, 2);
    bool blocked = await(held);
    ZuCheckRT(blocked);
    if (blocked) {
      emit(state, "d", Zhttp::WriteOutcome::Stream);
      peerMx.run([&state]() {
	state.srvRx();
	if (state.peerChannel) state.peerChannel->disconnect();
	else { ++state.errors; state.peerFailed.post(); }
	state.peerChannel.reset();
      }, 3);
      bool failed = await(state.peerFailed);
      ZuCheckRT(failed);
      if (failed) {
	ZmSemaphore observed;
	// The first hop follows the parser callback; the second follows its
	// deferred native close. Tx remains held throughout both Rx hops.
	mx.run([&mx, &observed]() {
	  mx.run([&observed]() { observed.post(); }, 1);
	}, 1);
	ZuCheckRT(await(observed));
      }
      ZuCheckRT(!state.closeCount.load_() && !state.results.load_());
    }
    release.post();
    if (blocked) ZuCheckRT(await(state.turn));
  }
  bool stopped = false;
  if constexpr (Turn == Mode::Stop) {
    client.stop([&state](bool ok) {
      state.stopOK = ok && state.closeCount.load_() == 1;
      state.stopped.post();
    });
    stopped = await(state.stopped) && state.stopOK;
    ZuCheckRT(stopped);
  }
  bool completed = opened && await(state.completed);
  bool retired = completed && await(state.retired);
  ZuCheckRT(completed && retired && state.results.load_() == 1 &&
      state.retirements.load_() == 1 && state.replayReads.load_() == 1);
  bool closed = completed && await(state.closed);
  ZuCheckRT(closed && state.closeCount.load_() == 1 && !state.errors.load_());
  if constexpr (Turn == Mode::Replay || Turn == Mode::Failed || Turn == Mode::Stop ||
      Turn == Mode::Channel || Turn == Mode::Peer)
    ZuCheckRT(completed && !state.result.ok());
  else
    ZuCheckRT(completed && state.result.ok() && state.responseEnds.load_() == 1);
  if constexpr (Turn == Mode::Turns)
    ZuCheckRT(state.result.requestBodyProduced == 3 &&
	state.result.requestBodyCommitted == 3 && !state.result.requestBodyReset);
  state.channel.reset();
  request = nullptr;
  if constexpr (tcp && Turn == Mode::Early) {
    State next;
    next.mx = &mx;
    next.mode = Mode::Single;
    ZmRef<Requests::Node> second = new Requests::Node{};
    second->state = &next;
    second->sequence = 2;
    bool received = client.send(0, second) && await(next.completed);
    bool retired = received && await(next.retired);
    ZuCheckRT(received && retired && next.result.ok() &&
	next.responseEnds.load_() == 1 && !next.errors.load_());
    mx.run([&next]() { next.late = {}; next.turn.post(); }, 2);
    ZuCheckRT(await(next.turn));
  }
  if constexpr (Turn == Mode::Restart) {
    client.stop([&state](bool ok) { state.stopOK = ok; state.stopped.post(); });
    stopped = await(state.stopped) && state.stopOK;
    ZuCheckRT(stopped);
    ZuCheckRT(server.stop());
    State next;
    next.mx = &mx;
    next.mode = Mode::Single;
    server.state = &next; // both endpoints are stopped and drained
    bool listening = server.start() && await(next.listening);
    ZuCheckRT(listening);
    bool running = listening && client.start();
    ZuCheckRT(running);
    ZuCheckRT(rejectLate(state, false));
    ZmRef<Requests::Node> second = new Requests::Node{};
    second->state = &next;
    second->sequence = 2;
    bool received = running && client.send(0, second) && await(next.completed);
    bool retired = received && await(next.retired);
    bool closed = retired && await(next.closed);
    ZuCheckRT(received && retired && closed && next.result.ok() &&
	next.results.load_() == 1 && next.retirements.load_() == 1 &&
	next.closeCount.load_() == 1 && next.responseEnds.load_() == 1 &&
	!next.errors.load_());
    client.stop([&next](bool ok) { next.stopOK = ok; next.stopped.post(); });
    stopped = await(next.stopped) && next.stopOK;
    ZuCheckRT(stopped);
    ZuCheckRT(server.stop());
    server.state = &state;
    mx.run([&next]() { next.late = {}; next.turn.post(); }, 2);
    ZuCheckRT(await(next.turn));
  }
  if (cli && !stopped) {
    client.stop([&state](bool ok) { state.stopOK = ok; state.stopped.post(); });
    stopped = await(state.stopped) && state.stopOK;
    ZuCheckRT(stopped);
  }
  if (cli && stopped) client.final();
  ZuCheckRT(rejectLate(state) && state.closeCount.load_() == 1 &&
      producers() == baseline);
  if (srv) {
    ZmSemaphore done;
    bool ok = false;
    server.stop([&ok, &server, &done](bool stopped) {
      ok = stopped;
      server.final();
      done.post();
    });
    ZuCheckRT(await(done) && ok);
  }
  mx.stop();
  if constexpr (Turn == Mode::Peer) peerMx.stop();
}

using PartialFields = ZhttpHeaders("content-length");
ZhttpHdrCatalogDerive(PartialHeaders, PartialFields);
ZhttpHdrCatalogImpl(PartialHeaders)

template <typename Profile>
struct WirePeer : public Zhttp::ClientHub<WirePeer<Profile>, Profile> {
  using Message = Zhttp::MessageTraits<Profile>;
  State *state;
  explicit WirePeer(State *state_) : state{state_} { }

  struct Builder : public Zhttp::Builder,
      public Message::template Request<Builder, true, false> {
    using Base = Message::template Request<Builder, true, false>;
    using Base::body;
    using HdrCatalog = PartialHeaders;
    using Zhttp::Builder::header;
    void fields(Zhttp::FieldOutput &out) {
      Zhttp::BuilderFields<Builder> fields{
	this, &Zhttp::CatalogData<HdrCatalog>::configure()};
      fields.fields(out);
    }
    template <typename Key> void headerOffset(uint64_t, unsigned) { }
    template <typename Key, typename L> void header(L &&l) const {
      if constexpr (Key{}() == "content-length") l("7");
    }
    template <typename L> void operation(L &&l) const {
      l(Zhttp::Method::POST, [](auto &&emit) {
	emit([](auto &tx) { tx << "/stream"; });
      });
    }
    template <typename L> void host(L &&l) const { l("localhost"); }
  };

  struct Link : public Zhttp::ClientLink<WirePeer, Link, Profile> {
    using Base = Zhttp::ClientLink<WirePeer, Link, Profile>;
    using Base::Base;
    unsigned id = 0;

    struct Parser : public Zhttp::Parser,
        public Message::template ResponseParser<Parser> {
      using HdrCatalog = Zhttp::DefltHdrCatalog;
      using Zhttp::Parser::header;
      using Base = Message::template ResponseParser<Parser>;
      using StateT = Base::State;
      State *state = nullptr;
      unsigned code = 0;
      const Zhttp::CatalogView &catalog() const {
	return Zhttp::CatalogData<HdrCatalog>::configure();
      }
      void header(Zhttp::FieldSection::T section, int id,
	  ZuBSpan name, ZuSpan<uint8_t> value) {
	int fixed = id < 0 ? -1 : catalog().valueMatch(id, value);
	Zhttp::parserHeader(*this, section, id, fixed, name, value);
      }
      void status(unsigned value) { code = value; }
      bool bodyInfo(Zhttp::BodyType::T, uint64_t) { return true; }
      template <typename Rx> bool body(Rx &rx) { return Zhttp::bodyDrain(rx); }
      void complete(typename StateT::T result) {
	if (result != StateT::Complete || code != 200) ++state->errors;
	else ++state->responseEnds;
	state->completed.post();
      }
    } parser;
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
  };

  void connected(Link &link, const Zhttp::ConnectedInfo &) {
    link.parser.state = state;
    link.parser.requestMethod(Zhttp::Method::POST);
    this->txRun([link = ZmRef(&link)]() {
      Builder builder;
      auto tx = link->transmit_(builder);
      builder.begin(tx);
      auto body = builder.body(tx, 7);
      body << 'a';
      body.flush();
      // Keep the request open after one byte of its declared seven.
    });
  }
  void connectFailed(Link &, bool) { ++state->errors; state->completed.post(); }
  void disconnected(Link &, bool) {
    if (state->responseEnds.load_() != 1) ++state->errors;
    state->peerClosed.post();
  }
  template <typename Rx> int process(Link &link, Rx &rx) {
    auto result = link.receive(link.parser, rx);
    return result == Link::Parser::StateT::Error ? -1 : 0;
  }
};

template <typename Profile>
void earlyWire()
{
  ZuTestScope(earlyWire);
  State state;
  state.mode = Mode::Early;
  state.holdBody = true;
  Zhttp::Test::TempDir temp;
  ZtString<> cert, key;
  bool tls = temp.init("ZhttpEarlyWire") &&
    Zhttp::Test::writeLocalhostCert(temp, cert, key);
  ZuCheck(tls);
  if (!tls) return;
  unsigned port = testPort();
  ZiMultiplex mx{ZiMxParams{}
    .udpBufAllocator<Zquic::PktRxBufAllocator<>>()
    .scheduler([](auto &s) { s.nThreads(4); })
    .rxThread(1).txThread(2)};
  state.mx = &mx;
  bool up = port && mx.start();
  ZuCheck(up);
  if (!up) return;
  Server server{&state};
  auto config = Zhttp::ServerConfig{}.localIP(ZiIP{"127.0.0.1"}).port(port)
    .bodyMax(uint64_t(-1)).retainedBodyMax(2);
  if constexpr (ZuIsSame<Profile, Zhttp::H3QUIC>{})
    config.quic(Zhttp::QUICConfig{}.certPath(cert.cspan()).keyPath(key.cspan()));
  else
    config.tls(Zhttp::H2Config{}.tls([&cert, &key](auto &tls) {
      tls.certPath(cert.cspan()).keyPath(key.cspan());
    }));
  bool srv = server.init(Zhttp::HubConfig{&mx, "3", "4"}, ZuMv(config));
  bool listening = srv && server.start() && await(state.listening);
  ZuCheck(listening);
  WirePeer<Profile> peer{&state};
  bool cli;
  if constexpr (ZuIsSame<Profile, Zhttp::H3QUIC>{})
    cli = peer.init(Zhttp::HubConfig{&mx, "1", "2"},
	Zhttp::QUICConfig{}.caPath(cert.cspan()));
  else
    cli = peer.init(Zhttp::HubConfig{&mx, "1", "2"},
	Zhttp::H2Config{}.tls([&cert](auto &tls) {
	  tls.caPath(cert.cspan());
	}).policy(Zhttp::H2Policy::Force));
  bool started = listening && cli && peer.start();
  ZuCheck(started);
  using Link = WirePeer<Profile>::Link;
  ZmRef<Link> link;
  if (started) {
    link = new Link{&peer};
    link->connect("localhost", port);
  }
  bool completed = started && await(state.completed);
  ZuCheck(completed && state.responseEnds.load_() == 1 && !state.errors.load_());
  bool disposed = completed && await(state.disposed);
  ZuCheck(disposed);
  bool closed = disposed && await(state.peerClosed);
  ZmSemaphore drained;
  mx.run([&drained]() { drained.post(); }, 3);
  bool settled = await(drained);
  ZuCheck(closed && settled && !state.requestEnds.load_() &&
      state.heldBytes.load_() == 1 && !state.rxBytes.load_() &&
      state.requestFailures.load_() == 1 && !server.activeRequests() &&
      state.responseEnds.load_() == 1 && !state.errors.load_());
  if (cli) { ZuCheck(peer.stop()); peer.final(); }
  if (srv) { ZuCheck(server.stop()); server.final(); }
  link = nullptr;
  mx.stop();
}

void ownedResolver()
{
  ZuTestScope(ownedResolver);
  ZiMultiplex mx{ZiMxParams{}
    .udpBufAllocator<Zquic::PktRxBufAllocator<>>()
    .scheduler([](auto &s) {
    s.nThreads(4);
  }).rxThread(1).txThread(2)};
  bool up = mx.start();
  ZuCheck(up);
  if (!up) return;
  Client client;
  bool initialized = client.init(Zhttp::HubConfig{&mx, "3", "4"}, 1,
    Zhttp::Config{}.secure(false).tcp(true).tls(false).quic(false)) && client.pool(0,
      Zhttp::Destination{"127.0.0.1", ZhttpITestPort::ClientStream});
  ZuCheck(initialized);
  bool started = initialized && client.start();
  ZuCheck(started && ZiResolver::instance()->running());
  ZmSemaphore done;
  bool ok = false;
  if (initialized) client.stop([&ok, &done](bool stopped) {
    ok = stopped && !ZiResolver::instance()->running();
    done.post();
  });
  ZuCheck(initialized && await(done) && ok);
  client.final();
  ZuCheck(!ZiResolver::instance()->initialized());
  mx.stop();
}

} // namespace zhttpclientstreamtest_

int main(int argc, char **argv)
{
  using namespace zhttpclientstreamtest_;
  parse(argc, argv);
  ZiTestResidue::init("zhttpclientstreamtest");
  ZiResolver::startSync();
  ZuTestMain();
  ZuTestCall((run<Zhttp::H1TCP, Mode::Restart>));
  ZuTestCall((run<Zhttp::H2TLS, Mode::Restart>));
  ZuTestCall((run<Zhttp::H3QUIC, Mode::Restart>));
  ZuTestCall((run<Zhttp::H2TLS, Mode::Turns>));
  ZuTestCall((run<Zhttp::H3QUIC, Mode::Turns>));
  ZuTestCall((run<Zhttp::H1TCP, Mode::Turns>));
  ZuTestCall((run<Zhttp::H2TLS, Mode::Single>));
  ZuTestCall((run<Zhttp::H3QUIC, Mode::Single>));
  ZuTestCall((run<Zhttp::H2TLS, Mode::Replay>));
  ZuTestCall((run<Zhttp::H3QUIC, Mode::Replay>));
  ZuTestCall((run<Zhttp::H2TLS, Mode::Failed>));
  ZuTestCall((run<Zhttp::H3QUIC, Mode::Failed>));
  ZuTestCall((run<Zhttp::H2TLS, Mode::Stop>));
  ZuTestCall((run<Zhttp::H3QUIC, Mode::Stop>));
  ZuTestCall((run<Zhttp::H2TLS, Mode::Early>));
  ZuTestCall((run<Zhttp::H3QUIC, Mode::Early>));
  ZuTestCall((run<Zhttp::H1TCP, Mode::Early>));
  ZuTestCall((run<Zhttp::H2TLS, Mode::High>));
  ZuTestCall((run<Zhttp::H3QUIC, Mode::High>));
  ZuTestCall((run<Zhttp::H2TLS, Mode::Peer>));
  ZuTestCall((run<Zhttp::H3QUIC, Mode::Peer>));
  ZuTestCall((run<Zhttp::H2TLS, Mode::Channel>));
  ZuTestCall((run<Zhttp::H3QUIC, Mode::Channel>));
  ZuTestCall((run<Zhttp::H1TCP, Mode::Channel>));
  ZuTestCall(earlyWire<Zhttp::H2TLS>);
  ZuTestCall(earlyWire<Zhttp::H3QUIC>);
  ZiResolver::stopSync();
  ZiResolver::final();
  ZuTestCall(ownedResolver);
  return 0;
}
