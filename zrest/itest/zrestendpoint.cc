//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZrestClient.hh>
#include <zlib/ZrestServer.hh>

#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiResolver.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZtcHeap.hh>
#include <zlib/ZtcQueue.hh>

#include <stdio.h>

#include "ZhttpTestUtil.hh"
#include "ZiTestResidue.hh"

using namespace ZuTestUtil;

namespace ZrestEndpointTest_ {

struct RequestObject : public ZmObject { };
struct ResponseObject : public ZmObject {
  ZtString<> location;
};
struct BodyObject : public ZmObject {
  ZtString<> value;
};
ZfStruct(, (BodyObject, JSON),
  (value, (Required), String));

struct ServerResponse : public Zrest::ResBuilder<ServerResponse,
    ResponseObject> {
  enum { Status = 200, Body = Zrest::BodyPolicy::Zero };
};
struct ServerUnavailable : public Zrest::ResBuilder<ServerUnavailable,
    ResponseObject> {
  enum { Status = 503, Body = Zrest::BodyPolicy::Zero };
};
struct ServerRedirect : public Zrest::ResBuilder<ServerRedirect,
    ResponseObject> {
  using Base = Zrest::ResBuilder<ServerRedirect, ResponseObject>;
  using Base::header;
  using Headers = ZhttpHeaders("location", "content-length");
  enum { Status = 302, Body = Zrest::BodyPolicy::Raw };
  ZuCSpan bodyObject(const ResponseObject *) const { return "redirect"; }
  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (Key{}() == "location") l(this->object->location);
    else Base::template header<Key>(ZuFwd<L>(l));
  }
};
struct ServerRequest : public Zrest::ReqParser<ServerRequest,
    RequestObject> {
  using Path = ZuStringT<"/ping">;
  using Responses = ZuTypeList<ServerResponse, ServerUnavailable,
    ServerRedirect>;
  enum { Exact = 1 };
};
struct ServerSSE : public Zrest::SSEBuilder<ServerSSE> {
  using Base = Zrest::SSEBuilder<ServerSSE>;
  using Base::header;
  using Headers = ZhttpHeaders("content-type");
  enum { Status = 200, Body = Zrest::BodyPolicy::SSE };
  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (Key{}() == "content-type") l("text/event-stream");
  }
};
struct ServerEvents : public Zrest::ReqParser<ServerEvents,
    RequestObject> {
  using Path = ZuStringT<"/events">;
  using Responses = ZuTypeList<ServerSSE>;
  enum { Exact = 1 };
};
struct ServerRawResponse : public Zrest::ResBuilder<ServerRawResponse,
    BodyObject> {
  enum { Status = 200, Body = Zrest::BodyPolicy::Raw };
  const auto &bodyObject(const BodyObject *object) const {
    return object->value;
  }
};
struct ServerRawRequest : public Zrest::ReqParser<ServerRawRequest,
    BodyObject> {
  using Path = ZuStringT<"/raw">;
  using Responses = ZuTypeList<ServerRawResponse>;
  enum { Method = Zhttp::Method::POST, Exact = 1,
    Body = Zrest::BodyPolicy::Raw };
  auto &bodyObject(BodyObject *object) { return object->value; }
};
struct ServerJSONResponse : public Zrest::ResBuilder<ServerJSONResponse,
    BodyObject> {
  using Base = Zrest::ResBuilder<ServerJSONResponse, BodyObject>;
  using Base::header;
  using Headers = ZhttpHeaders("content-type", "content-length");
  enum { Status = 200, Body = Zrest::BodyPolicy::JSON };
  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (Key{}() == "content-type") l("application/json");
    else Base::template header<Key>(ZuFwd<L>(l));
  }
};
struct ServerJSONRequest : public Zrest::ReqParser<ServerJSONRequest,
    BodyObject> {
  using Path = ZuStringT<"/json">;
  using Responses = ZuTypeList<ServerJSONResponse>;
  enum { Method = Zhttp::Method::POST, Exact = 1,
    Body = Zrest::BodyPolicy::JSON };
};
using ServerRequests = ZuTypeList<ServerRequest, ServerEvents,
  ServerRawRequest, ServerJSONRequest>;
ZrestCatalogDerive(ServerCatalog, ServerRequests);
ZrestCatalogImpl(ServerCatalog)

struct ClientResponse : public Zrest::ResParser<ClientResponse,
    ResponseObject> {
  enum { Status = 200, Body = Zrest::BodyPolicy::Zero };
};
struct ClientUnavailable : public Zrest::ResParser<ClientUnavailable,
    ResponseObject> {
  enum { Status = 503, Body = Zrest::BodyPolicy::Zero };
};
struct ClientRequest : public Zrest::ReqBuilder<ClientRequest,
    RequestObject> {
  using Path = ZuStringT<"/ping">;
  using Responses = ZuTypeList<ClientResponse, ClientUnavailable>;
  enum { Exact = 1 };
};
struct ClientSSE : public Zrest::ResParser<ClientSSE,
    ResponseObject> {
  using Headers = ZhttpHeaders("content-type");
  enum { Status = 200, Body = Zrest::BodyPolicy::SSE };
};
struct ClientEvents : public Zrest::ReqBuilder<ClientEvents,
    RequestObject> {
  using Path = ZuStringT<"/events">;
  using Responses = ZuTypeList<ClientSSE>;
  enum { Exact = 1 };
};
struct ClientPartialEvents : public Zrest::ReqBuilder<ClientPartialEvents,
    RequestObject> {
  using Path = ZuStringT<"/partial-events">;
  using Responses = ZuTypeList<ClientSSE>;
  enum { Exact = 1 };
};
struct ClientRawResponse : public Zrest::ResParser<ClientRawResponse,
    BodyObject> {
  enum { Status = 200, Body = Zrest::BodyPolicy::Raw };
  auto &bodyObject(BodyObject *object) { return object->value; }
};
struct ClientRawRequest : public Zrest::ReqBuilder<ClientRawRequest,
    BodyObject> {
  using Path = ZuStringT<"/raw">;
  using Responses = ZuTypeList<ClientRawResponse>;
  enum { Method = Zhttp::Method::POST, Exact = 1,
    Body = Zrest::BodyPolicy::Raw };
  const auto &bodyObject(const BodyObject *object) const {
    return object->value;
  }
};
struct ClientJSONResponse : public Zrest::ResParser<ClientJSONResponse,
    BodyObject> {
  using Headers = ZhttpHeaders("content-type", "content-length");
  enum { Status = 200, Body = Zrest::BodyPolicy::JSON };
};
struct ClientJSONRequest : public Zrest::ReqBuilder<ClientJSONRequest,
    BodyObject> {
  using Path = ZuStringT<"/json">;
  using Responses = ZuTypeList<ClientJSONResponse>;
  enum { Method = Zhttp::Method::POST, Exact = 1,
    Body = Zrest::BodyPolicy::JSON };
};
using ClientRequests = ZuTypeList<ClientRequest, ClientEvents,
  ClientPartialEvents, ClientRawRequest, ClientJSONRequest>;
ZrestCatalogDerive(ClientCatalog, ClientRequests);
ZrestCatalogImpl(ClientCatalog)

struct App;
using Decoder = Zrest::MReqParser<ServerCatalog>;
using Server = Zrest::Server<App, ServerCatalog, Decoder>;

struct App {
  using Token = Zrest::Completion<ServerRequest, Server>;
  ZmSemaphore listeningSem;
  ZmSemaphore deferredSem;
  ZmSemaphore observedSem;
  ZmAtomic<unsigned> received = 0;
  ZmAtomic<unsigned> invalid = 0;
  ZmAtomic<unsigned> responseAccepted = 0;
  ZmAtomic<unsigned> deferNext = 0;
  ZmAtomic<unsigned> redirectNext = 0;
  ZmAtomic<unsigned> observeAt = 0;
  ZmAtomic<unsigned> errorAt = 0;
  ZmRef<Token> deferred;
  unsigned port = 0;
  Zhttp::Scheme::T expectedScheme = Zhttp::Scheme::http;

  void listening(int8_t, uint16_t port_) {
    port = port_;
    listeningSem.post();
  }
  void listenFailed(int8_t, bool) { listeningSem.post(); }
  void connected(int8_t) { }
  void disconnected(int8_t) { }

  void request(ServerRequest *, const ServerRequest &request,
      const Zrest::RequestContext &context,
      ZmRef<Token> token) {
    unsigned count = ++received;
    if (!context.ok() || !request.object ||
	context.target != "/ping" ||
	context.scheme != expectedScheme) ++invalid;
    if (deferNext.xch(0)) {
      deferred = ZuMv(token);
      deferredSem.post();
      return;
    }
    bool accepted;
    if (redirectNext.xch(0)) {
      ZmRef<ResponseObject> object = new ResponseObject{};
      object->location = "/ping";
      accepted = token->complete(Zrest::Reply<ServerRedirect>{
	ZuMv(object)});
    } else if (count == errorAt.load_())
      accepted = token->complete(Zrest::Reply<ServerUnavailable>{
	ZmRef<ResponseObject>{new ResponseObject{}}});
    else
      accepted = token->complete(Zrest::Reply<ServerResponse>{
	ZmRef<ResponseObject>{new ResponseObject{}}});
    if (accepted) ++responseAccepted;
    if (count == observeAt.load_()) observedSem.post();
  }

  void request(ServerEvents *, const ServerEvents &,
      const Zrest::RequestContext &context,
      ZmRef<Zrest::Completion<ServerEvents, Server>> token) {
    if (!context.ok()) { ++invalid; return; }
    auto producer = token->producer();
    if (!producer ||
	!producer->emit("first\nline", "one", 100, false) ||
	!producer->emit("second", "two", -1, true)) {
      ++invalid;
      return;
    }
    ServerSSE response;
    response.init(producer);
    if (!token->complete(Zrest::Reply<ServerSSE>{ZuMv(response)}))
      ++invalid;
  }

  void request(ServerRawRequest *, const ServerRawRequest &request,
      const Zrest::RequestContext &context,
      ZmRef<Zrest::Completion<ServerRawRequest, Server>> token) {
    ZmRef<BodyObject> object = request.object;
    if (!context.ok() || !object || object->value != "raw payload")
      ++invalid;
    if (!object) object = new BodyObject{};
    if (!token->complete(Zrest::Reply<ServerRawResponse>{ZuMv(object)}))
      ++invalid;
  }
  void request(ServerJSONRequest *, const ServerJSONRequest &request,
      const Zrest::RequestContext &context,
      ZmRef<Zrest::Completion<ServerJSONRequest, Server>> token) {
    ZmRef<BodyObject> object = request.object;
    if (!context.ok() || !object || object->value != "json payload")
      ++invalid;
    if (!object) object = new BodyObject{};
    if (!token->complete(Zrest::Reply<ServerJSONResponse>{ZuMv(object)}))
      ++invalid;
  }
};

struct ClientApp { };
using Client = Zrest::Client<ClientApp, ClientCatalog>;
struct Call : public ZmObject {
  ZmSemaphore done;
  ZmAtomic<unsigned> processed = 0;
  ZmAtomic<unsigned> failures = 0;
  ZmAtomic<unsigned> unavailable = 0;
  ZmAtomic<unsigned> *order = nullptr;
  ZmAtomic<unsigned> ordinal = 0;
  void process(Zrest::Reply<ClientResponse> reply) {
    if (reply.response().object) ++processed;
    if (order) ordinal = ++*order;
    done.post();
  }
  void process(Zrest::Reply<ClientUnavailable> reply) {
    if (reply.response().object) {
      ++processed;
      ++unavailable;
    }
    if (order) ordinal = ++*order;
    done.post();
  }
  void failed() { ++failures; done.post(); }
};
struct EventCall : public ZmObject {
  ZmSemaphore done;
  ZmAtomic<unsigned> events = 0;
  ZmAtomic<unsigned> completed = 0;
  ZmAtomic<unsigned> failures = 0;
  ZmAtomic<unsigned> invalid = 0;
  bool receiveSSE(Zhttp::SSEEvent event,
      Zrest::SSEMeta<ClientSSE> meta) {
    unsigned n = ++events;
    if (meta.status != 200 ||
	(n == 1 && (event.data() != "first\nline" || event.id != "one" ||
	  event.retry != 100)) ||
	(n == 2 && (event.data() != "second" || event.id != "two" ||
	  event.retry != -1)) || n > 2) ++invalid;
    return true;
  }
  void process() {
    if (events.load_() != 2) ++invalid;
    ++completed;
    done.post();
  }
  void failed() { ++failures; done.post(); }
};
struct PartialEventCall : public ZmObject {
  ZmSemaphore done;
  ZmSemaphore received;
  ZmAtomic<unsigned> events = 0;
  ZmAtomic<unsigned> invalid = 0;
  ZmAtomic<unsigned> processed = 0;
  ZmAtomic<unsigned> failures = 0;
  bool receiveSSE(Zhttp::SSEEvent event, Zrest::SSEMeta<ClientSSE> meta) {
    if (event.data() != "ready" || meta.status != 200) ++invalid;
    ++events;
    received.post();
    return true;
  }
  void process() { ++processed; done.post(); }
  void failed() { ++failures; done.post(); }
};
struct BodyCall : public ZmObject {
  ZmSemaphore done;
  ZtString<> expected;
  ZmAtomic<unsigned> processed = 0;
  ZmAtomic<unsigned> failures = 0;
  ZmAtomic<unsigned> invalid = 0;
  void process(Zrest::Reply<ClientRawResponse> reply) {
    if (!reply.response().object ||
	reply.response().object->value != expected) ++invalid;
    ++processed;
    done.post();
  }
  void process(Zrest::Reply<ClientJSONResponse> reply) {
    if (!reply.response().object ||
	reply.response().object->value != expected) ++invalid;
    ++processed;
    done.post();
  }
  void failed() { ++failures; done.post(); }
};

struct BodyResult {
  bool rawAccepted = false;
  bool rawOK = false;
  bool jsonAccepted = false;
  bool jsonOK = false;
};

struct AuditSnapshot {
  uint64_t request = 0;
  uint64_t work = 0;
  uint64_t completion = 0;
  uint64_t response = 0;
  uint64_t input = 0;
  uint64_t sse = 0;
  uint64_t rxPosts = 0;
  uint64_t txPosts = 0;
};

AuditSnapshot audit(ZiMultiplex &mx)
{
  AuditSnapshot result;
  Ztc::HeapMgr::all(Ztc::HeapMgr::AllFn{[&result](Ztc::Heap *heap) {
    Ztc::HeapTelemetry data;
    heap->telemetry(data);
    ZuCSpan id{data.id};
    uint64_t n = data.cacheAllocs + data.heapAllocs;
    if (id == "Zrest.Client.Request") result.request += n;
    else if (id == "Zrest.Server.Work") result.work += n;
    else if (id == "Zrest.Completion") result.completion += n;
    else if (id == "Zrest.Server.Response") result.response += n;
    else if (id == "Zrest.Input") result.input += n;
    else if (id == "Zrest.SSE.Buffer") result.sse += n;
  }});
  mx.allQueues(Ztc::QueueMgr::AllFn{[&result](Ztc::Queue *queue) {
    Ztc::QueueTelemetry data;
    queue->telemetry(data);
    if (data.type != Ztc::QueueType::Thread) return;
    ZuCSpan id{data.id};
    if (id == "rx") result.rxPosts += data.inCount;
    else if (id == "tx") result.txPosts += data.inCount;
  }});
  return result;
}

BodyResult bodyRoundTrips(Client &client)
{
  BodyResult result;
  ZmRef<BodyObject> rawObject = new BodyObject{};
  rawObject->value = "raw payload";
  ZmRef<BodyCall> raw = new BodyCall{};
  raw->expected = rawObject->value;
  result.rawAccepted = client.call<ClientRawRequest>(0,
    ZuMv(rawObject), raw);
  bool rawFinished = result.rawAccepted &&
    raw->done.timedwait(Zm::now(10)) == 0;
  result.rawOK = rawFinished && raw->processed.load_() == 1 &&
    !raw->failures.load_() && !raw->invalid.load_();

  ZmRef<BodyObject> jsonObject = new BodyObject{};
  jsonObject->value = "json payload";
  ZmRef<BodyCall> json = new BodyCall{};
  json->expected = jsonObject->value;
  result.jsonAccepted = client.call<ClientJSONRequest>(0,
    ZuMv(jsonObject), json);
  bool jsonFinished = result.jsonAccepted &&
    json->done.timedwait(Zm::now(10)) == 0;
  result.jsonOK = jsonFinished && json->processed.load_() == 1 &&
    !json->failures.load_() && !json->invalid.load_();
  return result;
}

struct StreamState {
  ZmSemaphore done;
  ZmAtomic<unsigned> reported = 0;
  ZtString<> body;
  unsigned status = 0;
  bool complete = false;
  bool json = false;

  void report() {
    if (!reported.xch(1)) done.post();
  }
};

template <typename Profile>
struct StreamClient : public Zhttp::ClientHub<StreamClient<Profile>, Profile> {
  struct Builder : public Zhttp::Builder,
      public Zhttp::MessageTraits<Profile>::template Request<Builder,
        Zhttp::DefltHdrCatalog, true, true> {
    using Base = typename Zhttp::MessageTraits<Profile>::template Request<
      Builder, Zhttp::DefltHdrCatalog, true, true>;
    using HdrCatalog = Zhttp::DefltHdrCatalog;
    using Base::body;
    bool json = false;

    template <typename L> void operation(L &&l) const {
      l(Zhttp::Method::POST, [this](auto &&emit) {
	emit([this](auto &tx) { tx << (json ? "/json" : "/raw"); });
      });
    }
    template <typename L> void host(L &&l) const { l("localhost"); }
    template <typename Key, typename L> void header(L &&) const { }
    template <typename L> void header(L &&) const { }
  };

  struct Link : public Zhttp::ClientLink<StreamClient, Link, Profile> {
    using Base = Zhttp::ClientLink<StreamClient, Link, Profile>;
    using Base::Base;
    struct Parser : public Zhttp::Parser,
        public Zhttp::MessageTraits<Profile>::template ResponseParser<
          Parser, Zhttp::DefltHdrCatalog> {
      using Base = typename Zhttp::MessageTraits<Profile>::template
        ResponseParser<Parser, Zhttp::DefltHdrCatalog>;
      using State = typename Base::State;
      using HdrCatalog = Zhttp::DefltHdrCatalog;
      using Base::reset;
      StreamState *state = nullptr;

      void status(unsigned value) { state->status = value; }
      bool bodyInfo(Zhttp::BodyType::T, uint64_t) { return true; }
      template <typename Key>
      void header(Zhttp::FieldSection::T, ZuSpan<uint8_t>) { }
      template <typename Rx> bool body(Rx &rx) {
	return Zhttp::bodyEach(rx, [this](ZuSpan<uint8_t> value) {
	  state->body << value;
	});
      }
      void complete(typename State::T value) {
	state->complete = value == State::Complete;
	state->report();
      }
      bool finReceived() const { return false; }
    } parser;
    unsigned id = 0;
  };

  explicit StreamClient(StreamState *state_) : state{state_} { }
  void connected(Link &link, const Zhttp::ConnectedInfo &) {
    this->txRun([link = &link, json = state->json]() {
      Builder builder;
      builder.json = json;
      auto tx = link->transmit_(builder);
      builder.begin(tx);
      auto body = builder.body(tx);
      if (json) {
	body << "{\"value\":\"json ";
	body.flush();
	body << "payload\"}";
      } else {
	body << "raw ";
	body.flush();
	body << "payload";
      }
      body.flush();
      builder.finish(tx);
      link->finish();
    });
  }
  void disconnected(Link &, bool) {
    state->report();
  }
  void connectFailed(Link &, bool) {
    state->report();
  }
  template <typename Rx> int process(Link &link, Rx &rx) {
    auto result = link.receive(link.parser, rx);
    if (result == Link::Parser::State::Error) return -1;
    return result == Link::Parser::State::Complete;
  }
  StreamState *state;
};

template <typename Profile>
struct StreamProbe {
  using Protocol = typename Zhttp::ProfileTraits<Profile>::Protocol;
  StreamState state;
  StreamClient<Profile> client{&state};
  using Link = typename StreamClient<Profile>::Link;
  ZmRef<Link> link;
  bool inited = false;

  bool run(const Zhttp::HubConfig &hub, uint16_t port, ZuCSpan cert,
      bool json)
  {
    state.json = json;
    if constexpr (ZuIsSame<Protocol, Zhttp::TCP>{})
      inited = client.init(hub, Zhttp::TCPConfig{});
    else if constexpr (ZuIsSame<Protocol, Zhttp::TLS>{})
      inited = client.init(hub, Zhttp::H2Config{}
	.caPath(cert).policy(Zhttp::H2Policy::Force));
    else
      inited = client.init(hub, Zhttp::QUICConfig{}
	.caPath(cert).maxIdleTimeout(10000));
    bool up = inited && client.start();
    if (up) {
      link = new Link{&client};
      link->parser.state = &state;
      link->connect("localhost", port);
    }
    bool done = link && state.done.timedwait(Zm::now(10)) == 0;
    return done && state.complete && state.status == 200 &&
      (json ? state.body.find("\"json payload\"") >= 0 :
	state.body == "raw payload");
  }

  void final() {
    if (inited) {
      (void)client.stop();
      client.final();
    }
  }
};

using StreamResHeaderList = ZhttpHeaders("content-type", "content-length");
ZhttpHdrCatalogDerive(StreamResHeaders, StreamResHeaderList);
ZhttpHdrCatalogImpl(StreamResHeaders)

struct StreamResBuilder_ : public ZmObject, public Zhttp::ResBuilder {
  using HdrCatalog = StreamResHeaders;
  bool json = false;
  bool events = false;
  bool partial = false;
  ZmSemaphore *partialWritten = nullptr;
  mutable uint64_t bodyLength = 0;

  Zhttp::BodyPolicy::T bodyPolicy() const {
    return events ? Zhttp::BodyPolicy::Fixed : Zhttp::BodyPolicy::Stream;
  }
  Zhttp::Method::T method() const {
    return events || partial ? Zhttp::Method::GET : Zhttp::Method::POST;
  }
  unsigned status() const { return 200; }
  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (Key{}() == "content-type") {
      if (events || partial) l("text/event-stream");
      else if (json) l("application/json");
    } else if constexpr (Key{}() == "content-length") {
      if (events) l(bodyLength);
    }
  }
  template <typename L> void header(L &&) const { }
  bool disconnect() const { return false; }
  template <typename Emit> void body(Emit &&emit) {
    emit([this](auto &body) {
      if (partial) {
	body << "data: ready\n\ndata: unfinished";
	body.flush();
	if (partialWritten) partialWritten->post();
	return Zhttp::WriteOutcome::Stream;
      } else if (events) {
	body << "data: first\ndata: line\nid: one\nretry: 100\n\n"
	  "data: second\nid: two\n\n";
	body.flush();
	bodyLength = body.produced();
      } else if (json) body << "{\"value\":\"json payload\"}";
      else body << "raw payload";
      return Zhttp::WriteOutcome::End;
    });
  }
  void close() { }
};
using StreamResQ = ZmList<StreamResBuilder_,
  ZmListNode<StreamResBuilder_, ZmListHeapID<"Zrest.Stream.Response">>>;

struct StreamResServer;
struct StreamResParser : public Zhttp::Parser {
  using HdrCatalog = Zhttp::DefltHdrCatalog;
  bool json = false;
  bool events = false;
  bool partial = false;
  ZmSemaphore *partialWritten = nullptr;

  void init(StreamResServer &);
  bool operation(Zhttp::Method::T method, Zhttp::Target &target) {
    json = target.path == "/json";
    events = target.path == "/events";
    partial = target.path == "/partial-events";
    return ((events || partial) && method == Zhttp::Method::GET) ||
      (method == Zhttp::Method::POST &&
	(json || target.path == "/raw"));
  }
  bool bodyInfo(Zhttp::BodyType::T, uint64_t) { return true; }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t>) { }
  template <typename Rx> bool body(Rx &rx) {
    return Zhttp::bodyDrain(rx);
  }
  template <typename Link> void complete(Link *link, bool ok) {
    if (!ok) return;
    ZmRef<StreamResQ::Node> response = new StreamResQ::Node{};
    response->json = json;
    response->events = events;
    response->partial = partial;
    response->partialWritten = partialWritten;
    link->send(ZuMv(response));
  }
  void reset() { json = events = partial = false; }
};

struct StreamResServer : public Zhttp::Server<StreamResServer,
    StreamResParser, StreamResQ> {
  using Base = Zhttp::Server<StreamResServer, StreamResParser, StreamResQ>;
  using Base::connected;
  using Base::disconnected;
  ZmSemaphore listeningSem;
  ZmSemaphore partialWritten;
  ZmAtomic<unsigned> listenFailures = 0;
  void listening(int8_t, uint16_t) { listeningSem.post(); }
  void listenFailed(int8_t, bool) {
    ++listenFailures;
    listeningSem.post();
  }
  void connected(int8_t) { }
  void disconnected(int8_t) { }
};

inline void StreamResParser::init(StreamResServer &server) {
  partialWritten = &server.partialWritten;
}

struct StreamResFixture {
  StreamResServer server;
  ClientApp clientApp;
  Client client;
  bool serverInited = false;
  bool clientInited = false;
  bool serverStopped = false;
  bool clientStopped = false;

  struct Result {
    BodyResult body;
    bool eventsAccepted = false;
    bool eventsOK = false;
    bool partialAccepted = false;
    bool partialWritten = false;
    bool partialReceived = false;
    bool partialStopped = false;
    bool partialFinished = false;
    unsigned partialEvents = 0;
    unsigned partialProcessed = 0;
    unsigned partialFailures = 0;
    unsigned partialInvalid = 0;
    bool partialOK = false;
  };

  Result run(const Zhttp::HubConfig &hub, uint16_t port, ZuCSpan cert,
      ZuCSpan key, bool h3, bool h1)
  {
    Zhttp::ServerConfig serverConfig;
    serverConfig.localIP(ZiIP{"127.0.0.1"}).port(port)
      .retainedBytesMax(4096);
    if (h1) serverConfig.tcp();
    else if (h3)
      serverConfig.quic(Zhttp::QUICConfig{}.certPath(cert).keyPath(key)
	.maxIdleTimeout(10000));
    else
      serverConfig.tls(Zhttp::H2Config{}.certPath(cert).keyPath(key)
	.policy(Zhttp::H2Policy::Force));
    serverInited = server.init(hub, ZuMv(serverConfig));
    if (!serverInited || !server.start() ||
	server.listeningSem.timedwait(Zm::now(10)) != 0 ||
	server.listenFailures.load_()) return {};

    Zrest::ClientConfig clientConfig;
    clientConfig.links(1).concurrency(2).linkMax(2)
      .retainedBodyMax(4096)
      .protocol(h3 ? Zhttp::ProtoPolicy::ForceH3 :
	Zhttp::ProtoPolicy::DisableH3)
      .h2Policy(Zhttp::H2Policy::Force).blindH3(h3)
      .secure(!h1).tcp(h1).tls(!h1 && !h3).quic(h3);
    if (h1)
      clientInited = client.init(hub, 1, ZuMv(clientConfig), &clientApp);
    else
      clientInited = client.init(hub, 1, ZuMv(clientConfig), &clientApp,
	Zhttp::TCPConfig{},
	Zhttp::H2Config{}.caPath(cert).policy(Zhttp::H2Policy::Force),
	Zhttp::QUICConfig{}.caPath(cert).maxIdleTimeout(10000));
    if (!clientInited || !client.pool(0,
	Zhttp::Destination{h1 ? "127.0.0.1" : "localhost", port, false}) ||
	!client.start()) return {};
    Result result;
    result.body = bodyRoundTrips(client);
    ZmRef<EventCall> events = new EventCall{};
    result.eventsAccepted = client.call<ClientEvents>(0,
      ZmRef<RequestObject>{new RequestObject{}}, events);
    bool finished = result.eventsAccepted &&
      events->done.timedwait(Zm::now(10)) == 0;
    result.eventsOK = finished && events->events.load_() == 2 &&
      events->completed.load_() == 1 && !events->failures.load_() &&
      !events->invalid.load_();
    ZmRef<PartialEventCall> partial = new PartialEventCall{};
    result.partialAccepted = client.call<ClientPartialEvents>(0,
      ZmRef<RequestObject>{new RequestObject{}}, partial);
    result.partialWritten = result.partialAccepted &&
      server.partialWritten.timedwait(Zm::now(10)) == 0;
    result.partialReceived = result.partialWritten &&
      partial->received.timedwait(Zm::now(10)) == 0;
    if (result.partialReceived) clientStopped = client.stop();
    if (clientStopped) serverStopped = server.stop();
    result.partialStopped = clientStopped && serverStopped;
    result.partialFinished = result.partialReceived &&
      partial->done.timedwait(Zm::now(10)) == 0;
    result.partialEvents = partial->events.load_();
    result.partialProcessed = partial->processed.load_();
    result.partialFailures = partial->failures.load_();
    result.partialInvalid = partial->invalid.load_();
    result.partialOK = result.partialFinished && result.partialStopped &&
      result.partialEvents == 1 && !result.partialInvalid &&
      !result.partialProcessed && result.partialFailures == 1;
    return result;
  }

  void final() {
    if (clientInited) {
      if (!clientStopped) (void)client.stop();
      client.final();
    }
    if (serverInited) {
      if (!serverStopped) (void)server.stop();
      server.final();
    }
  }
};

ZiMxParams mxParams()
{
  return ZiMxParams().scheduler([](auto &scheduler) {
    scheduler.nThreads(2)
      .thread(1, [](auto &thread) {
	thread.name("rx");
	thread.isolated(1);
      })
      .thread(2, [](auto &thread) {
	thread.name("tx");
	thread.isolated(1);
      });
  }).rxThread(1).txThread(2);
}

void multiplexed(bool h3)
{
  ZuTestScope(multiplexed);
  StreamProbe<Zhttp::H2TLS> h2Raw, h2JSON;
  StreamProbe<Zhttp::H3QUIC> h3Raw, h3JSON;
  StreamResFixture responseStream;
  Zhttp::Test::TempDir temp;
  bool tempOK = temp.init(h3 ? "ZrestH3" : "ZrestH2");
  ZuCheck(tempOK);
  if (!tempOK) return;
  ZtString<> cert, key;
  bool certOK = Zhttp::Test::writeLocalhostCert(temp, cert, key);
  ZuCheck(certOK);
  if (!certOK) return;

  ZiMultiplex mx{mxParams()};
  bool mxUp = mx.start();
  ZuCheck(mxUp);
  if (!mxUp) return;
  Zhttp::HubConfig hub{&mx, "rx", "tx"};
  uint16_t port = uint16_t(Zhttp::Test::loopbackPort(20810 + h3));
  ZuCheck(port);

  App app;
  app.expectedScheme = Zhttp::Scheme::https;
  app.errorAt = 2;
  Server server;
  Zrest::ServerConfig serverConfig;
  serverConfig.localIP(ZiIP{"127.0.0.1"}).port(port)
    .retainedBodyMax(4096);
  if (h3)
    serverConfig.quic(Zhttp::QUICConfig{}
      .certPath(cert).keyPath(key).maxIdleTimeout(10000));
  else
    serverConfig.tls(Zhttp::H2Config{}
      .certPath(cert).keyPath(key).policy(Zhttp::H2Policy::Force));
  bool serverInited = server.init(hub, ZuMv(serverConfig), &app);
  ZuCheck(serverInited);
  bool serverUp = serverInited && server.start();
  ZuCheck(serverUp);
  bool listening = serverUp &&
    app.listeningSem.timedwait(Zm::now(10)) == 0;
  ZuCheck(listening && app.port == port);

  ClientApp clientApp;
  Client client;
  Zrest::ClientConfig clientConfig;
  Zrest::Limits limits;
  limits.maxPending = 2;
  clientConfig.links(1).concurrency(2).linkMax(2)
    .retainedBodyMax(4096)
    .protocol(h3 ? Zhttp::ProtoPolicy::ForceH3 :
      Zhttp::ProtoPolicy::DisableH3)
    .h2Policy(Zhttp::H2Policy::Force).blindH3(h3)
    .secure(true).tcp(false).tls(!h3).quic(h3);
  clientConfig.limits(limits);
  bool clientInited = listening && client.init(hub, 1,
    ZuMv(clientConfig), &clientApp, Zhttp::TCPConfig{},
    Zhttp::H2Config{}.caPath(cert).policy(Zhttp::H2Policy::Force),
    Zhttp::QUICConfig{}.caPath(cert).maxIdleTimeout(10000));
  ZuCheck(clientInited);
  bool pooled = clientInited && client.pool(0,
    Zhttp::Destination{"localhost", port, false});
  ZuCheck(pooled);
  bool clientUp = pooled && client.start();
  ZuCheck(clientUp);
  if (clientUp) {
    auto bodyResult = bodyRoundTrips(client);
    ZuCheck(bodyResult.rawAccepted);
    ZuCheck(bodyResult.rawOK);
    ZuCheck(bodyResult.jsonAccepted);
    ZuCheck(bodyResult.jsonOK);
    if (h3) {
      ZuCheck(h3Raw.run(hub, port, cert, false));
      ZuCheck(h3JSON.run(hub, port, cert, true));
    } else {
      ZuCheck(h2Raw.run(hub, port, cert, false));
      ZuCheck(h2JSON.run(hub, port, cert, true));
    }
  }
  if (clientUp) {
    auto responseResult = responseStream.run(hub,
      uint16_t(Zhttp::Test::loopbackPort(20820 + h3)), cert, key,
      h3, false);
    ZuCheck(responseResult.body.rawAccepted && responseResult.body.rawOK);
    ZuCheck(responseResult.body.jsonAccepted && responseResult.body.jsonOK);
    ZuCheck(responseResult.eventsAccepted && responseResult.eventsOK);
    ZuCheck(responseResult.partialWritten && responseResult.partialReceived &&
      responseResult.partialStopped &&
      responseResult.partialFinished);
    ZuCheck(responseResult.partialEvents == 1 &&
      !responseResult.partialInvalid &&
      !responseResult.partialProcessed &&
      responseResult.partialFailures == 1);
    ZuCheck(responseResult.partialAccepted && responseResult.partialOK);
  }

  app.deferNext = 1;
  ZmRef<Call> first = new Call{};
  bool firstAccepted = clientUp && client.call<ClientRequest>(0,
    ZmRef<RequestObject>{new RequestObject{}}, first);
  ZuCheck(firstAccepted);
  bool firstDeferred = firstAccepted &&
    app.deferredSem.timedwait(Zm::now(10)) == 0;
  ZuCheck(firstDeferred && app.deferred && app.deferred->live());

  ZmRef<Call> second = new Call{};
  bool secondAccepted = firstDeferred && client.call<ClientRequest>(0,
    ZmRef<RequestObject>{new RequestObject{}}, second);
  ZuCheck(secondAccepted);
  bool secondFinished = secondAccepted &&
    second->done.timedwait(Zm::now(10)) == 0;
  ZuCheck(secondFinished && second->processed.load_() == 1 &&
    second->unavailable.load_() == 1 && !second->failures.load_());
  ZuCheck(!first->processed.load_() && !first->failures.load_());

  app.redirectNext = 1;
  ZmRef<Call> redirected = new Call{};
  bool redirectedAccepted = secondFinished && client.call<ClientRequest>(0,
    ZmRef<RequestObject>{new RequestObject{}}, redirected);
  ZuCheck(redirectedAccepted);
  bool redirectedFinished = redirectedAccepted &&
    redirected->done.timedwait(Zm::now(10)) == 0;
  ZuCheck(redirectedFinished && redirected->processed.load_() == 1 &&
    !redirected->failures.load_() && app.received.load_() == 4);
  ZuCheck(!first->processed.load_() && !first->failures.load_());

  ZmRef<EventCall> events = new EventCall{};
  bool eventAccepted = redirectedFinished && client.call<ClientEvents>(0,
    ZmRef<RequestObject>{new RequestObject{}}, events);
  ZuCheck(eventAccepted);
  bool eventFinished = eventAccepted &&
    events->done.timedwait(Zm::now(10)) == 0;
  ZuCheck(eventFinished && events->events.load_() == 2 &&
    events->completed.load_() == 1 && !events->failures.load_() &&
    !events->invalid.load_());
  ZuCheck(!first->processed.load_() && !first->failures.load_());

  if (firstDeferred)
    ZuCheck(app.deferred->complete(Zrest::Reply<ServerResponse>{
      ZmRef<ResponseObject>{new ResponseObject{}}}));
  bool firstFinished = firstDeferred &&
    first->done.timedwait(Zm::now(10)) == 0;
  ZuCheck(firstFinished && first->processed.load_() == 1 &&
    !first->failures.load_());
  app.deferred = nullptr;
  ZuCheck(app.received.load_() == 4 && !app.invalid.load_());

  ZmSemaphore serverDone, clientDone;
  ZmAtomic<int> serverStopOK = -1, clientStopOK = -1;
  if (serverInited) server.stop([&](bool ok) {
    serverStopOK = ok;
    serverDone.post();
  });
  ZuCheck(serverInited &&
    serverDone.timedwait(Zm::now(10)) == 0 && serverStopOK.load_() == 1);
  if (clientInited) client.stop([&](bool ok) {
    clientStopOK = ok;
    clientDone.post();
  });
  ZuCheck(clientInited &&
    clientDone.timedwait(Zm::now(10)) == 0 && clientStopOK.load_() == 1);
  h2Raw.final();
  h2JSON.final();
  h3Raw.final();
  h3JSON.final();
  responseStream.final();
  client.final();
  server.final();
  mx.stop();
}

void h2()
{
  multiplexed(false);
}

void h3()
{
  multiplexed(true);
}

struct RetryCall : public ZmObject {
  ZmSemaphore done;
  ZmAtomic<unsigned> processed = 0;
  ZmAtomic<unsigned> failures = 0;
  ZmAtomic<unsigned> retries = 0;
  ZmAtomic<unsigned> code = unsigned(-1);

  void process(Zrest::Reply<ClientResponse>) {
    ++processed;
    done.post();
  }
  void process(Zrest::Reply<ClientUnavailable>) {
    ++processed;
    done.post();
  }
  void failed(const Zrest::Failure &failure) {
    retries = failure.http.retries;
    code = failure.code;
    ++failures;
    done.post();
  }
};

void retryLimit()
{
  ZuTestScope(retryLimit);
  ZiMultiplex mx{mxParams()};
  bool mxUp = mx.start();
  ZuCheck(mxUp);
  if (!mxUp) return;
  Zhttp::HubConfig hub{&mx, "rx", "tx"};
  uint16_t port = uint16_t(Zhttp::Test::loopbackPort(20840));
  ZuCheck(port);
  ClientApp app;
  Client client;
  Zrest::ClientConfig config;
  config.links(1).concurrency(1).linkMax(1)
    .protocol(Zhttp::ProtoPolicy::DisableH3)
    .secure(false).tcp(true).tls(false).quic(false);
  config.maxRetries(2);
  bool inited = client.init(hub, 1, ZuMv(config), &app);
  ZuCheck(inited);
  bool pooled = inited && client.pool(0,
    Zhttp::Destination{"127.0.0.1", port, false});
  ZuCheck(pooled);
  bool up = pooled && client.start();
  ZuCheck(up);
  ZmRef<RetryCall> call = new RetryCall{};
  bool accepted = up && client.call<ClientRequest>(0,
    ZmRef<RequestObject>{new RequestObject{}}, call);
  ZuCheck(accepted);
  bool finished = accepted && call->done.timedwait(Zm::now(10)) == 0;
  ZuCheck(finished && !call->processed.load_() &&
    call->failures.load_() == 1 && call->retries.load_() == 2 &&
    call->code.load_() == Zrest::Failure::Transport);
  if (inited) {
    ZuCheck(client.stop());
    client.final();
  }
  mx.stop();
}

void call()
{
  ZuTestScope(call);
  StreamProbe<Zhttp::H1TCP> streamRaw, streamJSON;
  StreamResFixture responseStream;
  ZiMultiplex mx{mxParams()};
  bool mxUp = mx.start();
  ZuCheck(mxUp);
  if (!mxUp) return;
  Zhttp::HubConfig hub{&mx, "rx", "tx"};
  uint16_t port = uint16_t(Zhttp::Test::loopbackPort(20800));
  ZuCheck(port);

  App app;
  Zrest::Limits invalidLimits;
  invalidLimits.maxPending = 0;
  Server invalidServer;
  Zrest::ServerConfig invalidServerConfig;
  invalidServerConfig.localIP(ZiIP{"127.0.0.1"}).port(port).tcp();
  invalidServerConfig.limits(invalidLimits);
  ZuCheck(!invalidServer.init(hub, ZuMv(invalidServerConfig), &app));
  invalidServer.final();
  Server server;
  Zrest::ServerConfig serverConfig;
  serverConfig.localIP(ZiIP{"127.0.0.1"}).port(port)
    .retainedBodyMax(4096).tcp();
  bool serverInited = server.init(hub, ZuMv(serverConfig), &app);
  ZuCheck(serverInited);
  bool serverUp = serverInited && server.start();
  ZuCheck(serverUp);
  bool listening = serverUp &&
    app.listeningSem.timedwait(Zm::now(10)) == 0;
  ZuCheck(listening && app.port == port);

  ClientApp clientApp;
  Client invalidClient;
  Zrest::ClientConfig invalidClientConfig;
  invalidClientConfig.links(1).concurrency(1).linkMax(1)
    .protocol(Zhttp::ProtoPolicy::DisableH3)
    .secure(false).tcp(true).tls(false).quic(false);
  invalidClientConfig.limits(invalidLimits);
  ZuCheck(!invalidClient.init(hub, 1, ZuMv(invalidClientConfig),
    &clientApp));
  invalidClient.final();
  Client client;
  Zrest::ClientConfig clientConfig;
  Zrest::Limits clientLimits;
  clientLimits.maxPending = 2;
  clientConfig.links(1).concurrency(2).linkMax(2)
    .retainedBodyMax(4096).protocol(Zhttp::ProtoPolicy::DisableH3)
    .secure(false).tcp(true).tls(false).quic(false);
  clientConfig.limits(clientLimits);
  bool clientInited = listening &&
    client.init(hub, 1, ZuMv(clientConfig), &clientApp);
  ZuCheck(clientInited);
  bool pooled = clientInited && client.pool(0,
    Zhttp::Destination{"127.0.0.1", port, false});
  ZuCheck(pooled);
  bool clientUp = pooled && client.start();
  ZuCheck(clientUp);
  if (clientUp) {
    auto bodyResult = bodyRoundTrips(client);
    ZuCheck(bodyResult.rawAccepted);
    ZuCheck(bodyResult.rawOK);
    ZuCheck(bodyResult.jsonAccepted);
    ZuCheck(bodyResult.jsonOK);
    ZuCheck(streamRaw.run(hub, port, {}, false));
    ZuCheck(streamJSON.run(hub, port, {}, true));
  }
  if (clientUp) {
    auto responseResult = responseStream.run(hub,
      uint16_t(Zhttp::Test::loopbackPort(20830)), {}, {}, false, true);
    ZuCheck(responseResult.body.rawAccepted && responseResult.body.rawOK);
    ZuCheck(responseResult.body.jsonAccepted && responseResult.body.jsonOK);
    ZuCheck(responseResult.eventsAccepted && responseResult.eventsOK);
    ZuCheck(responseResult.partialWritten && responseResult.partialReceived &&
      responseResult.partialStopped &&
      responseResult.partialFinished);
    ZuCheck(responseResult.partialEvents == 1 &&
      !responseResult.partialInvalid &&
      !responseResult.partialProcessed &&
      responseResult.partialFailures == 1);
    ZuCheck(responseResult.partialAccepted && responseResult.partialOK);
  }

  auto auditBefore = audit(mx);
  ZmRef<Call> result = new Call{};
  bool accepted = clientUp && client.call<ClientRequest>(0,
    ZmRef<RequestObject>{new RequestObject{}}, result);
  ZuCheck(accepted);
  bool finished = accepted &&
    result->done.timedwait(Zm::now(10)) == 0;
  if (!finished || result->processed.load_() != 1 ||
      result->failures.load_())
    fprintf(stderr, "endpoint: responseAccepted=%u queued=%u buildFailures=%llu transportFailures=%llu processed=%u failures=%u\n",
      app.responseAccepted.load_(), server.queuedResponses(),
      (unsigned long long)server.responseBuildFailures(),
      (unsigned long long)server.transportFailures(),
      result->processed.load_(), result->failures.load_());
  ZuCheck(finished);
  ZuCheck(result->processed.load_() == 1 &&
    !result->failures.load_());
  ZuCheck(app.received.load_() == 1 && !app.invalid.load_());
  auto auditAfter = audit(mx);
  if (finished) {
    ZuCheck(auditAfter.request == auditBefore.request + 1 &&
      auditAfter.work == auditBefore.work + 1 &&
      auditAfter.completion == auditBefore.completion + 1 &&
      auditAfter.response == auditBefore.response + 1);
    ZuCheck(auditAfter.input == auditBefore.input &&
      auditAfter.sse == auditBefore.sse);
    ZuCheck(auditAfter.rxPosts > auditBefore.rxPosts &&
      auditAfter.txPosts > auditBefore.txPosts);
    ZuTestUtil::log("zrest ping: Rx posts=",
      auditAfter.rxPosts - auditBefore.rxPosts,
      " Tx posts=", auditAfter.txPosts - auditBefore.txPosts,
      " REST request/work/token/response allocations=",
      auditAfter.request - auditBefore.request, "/",
      auditAfter.work - auditBefore.work, "/",
      auditAfter.completion - auditBefore.completion, "/",
      auditAfter.response - auditBefore.response);
  }

  app.deferNext = 1;
  ZmRef<Call> deferred = new Call{};
  bool deferredAccepted = client.call<ClientRequest>(0,
    ZmRef<RequestObject>{new RequestObject{}}, deferred);
  ZuCheck(deferredAccepted);
  bool admitted = deferredAccepted &&
    app.deferredSem.timedwait(Zm::now(10)) == 0;
  ZuCheck(admitted && app.deferred && app.deferred->live());
  if (admitted) {
    ZuCheck(app.deferred->complete(Zrest::Reply<ServerResponse>{
      ZmRef<ResponseObject>{new ResponseObject{}}}));
    ZuCheck(!app.deferred->complete(Zrest::Reply<ServerResponse>{
      ZmRef<ResponseObject>{new ResponseObject{}}}));
  }
  bool deferredFinished = admitted &&
    deferred->done.timedwait(Zm::now(10)) == 0;
  ZuCheck(deferredFinished);
  ZuCheck(deferred->processed.load_() == 1 &&
    !deferred->failures.load_());
  ZmAtomic<int> lateAccepted = -1;
  ZmSemaphore lateDone;
  ZmRef<App::Token> lateToken = app.deferred;
  mx.run([lateToken, &lateAccepted, &lateDone]() {
    lateAccepted = lateToken->complete(Zrest::Reply<ServerResponse>{
      ZmRef<ResponseObject>{new ResponseObject{}}});
    lateDone.post();
  }, mx.sid("tx"));
  ZuCheck(lateDone.timedwait(Zm::now(10)) == 0 &&
    lateAccepted.load_() == 0 && deferred->processed.load_() == 1 &&
    !deferred->failures.load_());
  ZuCheck(app.received.load_() == 2 && !app.invalid.load_());
  app.deferred = nullptr;

  app.redirectNext = 1;
  ZmRef<Call> redirected = new Call{};
  bool redirectedAccepted = client.call<ClientRequest>(0,
    ZmRef<RequestObject>{new RequestObject{}}, redirected);
  ZuCheck(redirectedAccepted);
  bool redirectedFinished = redirectedAccepted &&
    redirected->done.timedwait(Zm::now(10)) == 0;
  ZuCheck(redirectedFinished);
  ZuCheck(redirected->processed.load_() == 1 &&
    !redirected->failures.load_());
  ZuCheck(app.received.load_() == 4 && !app.invalid.load_());

  unsigned reverseEnd = app.received.load_() + 2;
  app.observeAt = reverseEnd;
  app.errorAt = reverseEnd;
  app.deferNext = 1;
  ZmAtomic<unsigned> order = 0;
  ZmRef<Call> first = new Call{}, second = new Call{};
  first->order = second->order = &order;
  bool firstAccepted = client.call<ClientRequest>(0,
    ZmRef<RequestObject>{new RequestObject{}}, first);
  bool secondAccepted = client.call<ClientRequest>(0,
    ZmRef<RequestObject>{new RequestObject{}}, second);
  ZuCheck(firstAccepted && secondAccepted);
  bool firstDeferred = firstAccepted &&
    app.deferredSem.timedwait(Zm::now(10)) == 0;
  bool secondObserved = secondAccepted &&
    app.observedSem.timedwait(Zm::now(10)) == 0;
  ZuCheck(firstDeferred && secondObserved);
  ZuCheck(!first->processed.load_() && !first->failures.load_() &&
    !second->processed.load_() && !second->failures.load_());
  ZmRef<Call> refused = new Call{};
  bool transferred = client.call<ClientRequest>(0,
    ZmRef<RequestObject>{new RequestObject{}}, refused);
  ZuCheck(transferred);
  bool limited = transferred &&
    refused->done.timedwait(Zm::now(10)) == 0;
  ZuCheck(limited && !refused->processed.load_() &&
    refused->failures.load_() == 1);
  if (firstDeferred)
    ZuCheck(app.deferred->complete(Zrest::Reply<ServerResponse>{
      ZmRef<ResponseObject>{new ResponseObject{}}}));
  bool firstFinished = firstDeferred &&
    first->done.timedwait(Zm::now(10)) == 0;
  bool secondFinished = secondObserved &&
    second->done.timedwait(Zm::now(10)) == 0;
  ZuCheck(firstFinished && secondFinished);
  ZuCheck(first->processed.load_() == 1 &&
    second->processed.load_() == 1 &&
    !first->unavailable.load_() && second->unavailable.load_() == 1 &&
    first->ordinal.load_() == 1 && second->ordinal.load_() == 2);
  ZuCheck(app.received.load_() == reverseEnd && !app.invalid.load_());
  app.deferred = nullptr;

  ZmRef<EventCall> events = new EventCall{};
  bool eventAccepted = client.call<ClientEvents>(0,
    ZmRef<RequestObject>{new RequestObject{}}, events);
  ZuCheck(eventAccepted);
  bool eventFinished = eventAccepted &&
    events->done.timedwait(Zm::now(10)) == 0;
  ZuCheck(eventFinished);
  ZuCheck(events->events.load_() == 2 &&
    events->completed.load_() == 1 &&
    !events->failures.load_() && !events->invalid.load_() &&
    !app.invalid.load_());

  ZmSemaphore entered, release;
  mx.run([&]() {
    entered.post();
    release.wait();
  }, mx.sid("rx"));
  bool ingressBlocked = entered.timedwait(Zm::now(10)) == 0;
  ZuCheck(ingressBlocked);
  ZmRef<Call> queued1 = new Call{}, queued2 = new Call{};
  ZmRef<Call> ingressRefused = new Call{};
  bool queuedFirst = client.call<ClientRequest>(0,
    ZmRef<RequestObject>{new RequestObject{}}, queued1);
  bool queuedSecond = client.call<ClientRequest>(0,
    ZmRef<RequestObject>{new RequestObject{}}, queued2);
  bool rejectedIngress = !client.call<ClientRequest>(0,
    ZmRef<RequestObject>{new RequestObject{}}, ingressRefused);
  ZuCheck(queuedFirst && queuedSecond && rejectedIngress);
  ZuCheck(!ingressRefused->processed.load_() &&
    !ingressRefused->failures.load_());
  release.post();
  bool queuedFinished = queuedFirst && queuedSecond &&
    queued1->done.timedwait(Zm::now(10)) == 0 &&
    queued2->done.timedwait(Zm::now(10)) == 0;
  ZuCheck(queuedFinished && queued1->processed.load_() == 1 &&
    queued2->processed.load_() == 1 && !queued1->failures.load_() &&
    !queued2->failures.load_());
  ZmRef<Call> recovered = new Call{};
  bool recoveredAccepted = client.call<ClientRequest>(0,
    ZmRef<RequestObject>{new RequestObject{}}, recovered);
  bool recoveredFinished = recoveredAccepted &&
    recovered->done.timedwait(Zm::now(10)) == 0;
  ZuCheck(recoveredFinished && recovered->processed.load_() == 1 &&
    !recovered->failures.load_());

  Zrest::Limits queueLimits;
  queueLimits.maxQueue = 1;
  ZmRef<Zrest::SSEProducer> bounded = new Zrest::SSEProducer{
    &mx, mx.sid("tx"), queueLimits};
  ZuCheck(bounded->emit("first"));
  ZuCheck(!bounded->emit("second") && bounded->failed());
  ZmSemaphore queueDrained;
  mx.run([&queueDrained]() { queueDrained.post(); }, mx.sid("tx"));
  ZuCheck(queueDrained.timedwait(Zm::now(10)) == 0 &&
    !bounded->queued() && !bounded->queuedBytes());
  bounded = nullptr;

  queueLimits.maxQueueBytes = 8;
  ZmRef<Zrest::SSEProducer> byteBounded = new Zrest::SSEProducer{
    &mx, mx.sid("tx"), queueLimits};
  ZuCheck(!byteBounded->emit("a") && byteBounded->failed());
  ZmSemaphore byteDrained;
  mx.run([&byteDrained]() { byteDrained.post(); }, mx.sid("tx"));
  ZuCheck(byteDrained.timedwait(Zm::now(10)) == 0 &&
    !byteBounded->queued() && !byteBounded->queuedBytes());
  byteBounded = nullptr;

  unsigned responseBeforeStop = app.responseAccepted.load_();
  app.observeAt = app.received.load_() + 2;
  app.errorAt = app.observeAt.load_();
  app.deferNext = 1;
  ZmRef<Call> stopped = new Call{};
  bool stoppedAccepted = client.call<ClientRequest>(0,
    ZmRef<RequestObject>{new RequestObject{}}, stopped);
  ZuCheck(stoppedAccepted);
  bool stopDeferred = stoppedAccepted &&
    app.deferredSem.timedwait(Zm::now(10)) == 0;
  ZuCheck(stopDeferred && app.deferred && app.deferred->live());
  ZmRef<Call> behind = new Call{};
  bool behindAccepted = stopDeferred && client.call<ClientRequest>(0,
    ZmRef<RequestObject>{new RequestObject{}}, behind);
  ZuCheck(behindAccepted);
  bool behindQueued = behindAccepted &&
    app.observedSem.timedwait(Zm::now(10)) == 0;
  ZuCheck(behindQueued &&
    app.responseAccepted.load_() == responseBeforeStop + 1 &&
    !stopped->processed.load_() &&
    !behind->processed.load_() && !behind->failures.load_());
  if (serverInited) ZuCheck(server.stop());
  if (stopDeferred) {
    ZuCheck(!app.deferred->live() && app.deferred->cancelled());
    ZuCheck(!app.deferred->complete(Zrest::Reply<ServerResponse>{
      ZmRef<ResponseObject>{new ResponseObject{}}}));
  }
  bool stoppedFinished = stoppedAccepted &&
    stopped->done.timedwait(Zm::now(10)) == 0;
  ZuCheck(stoppedFinished);
  ZuCheck(!stopped->processed.load_() &&
    stopped->failures.load_() == 1);
  bool behindFinished = behindQueued &&
    behind->done.timedwait(Zm::now(10)) == 0;
  ZuCheck(behindFinished && !behind->processed.load_() &&
    behind->failures.load_() == 1);
  app.deferred = nullptr;

  if (clientInited) ZuCheck(client.stop());
  ZmRef<Call> refusedAfterStop = new Call{};
  ZuCheck(!client.call<ClientRequest>(0,
    ZmRef<RequestObject>{new RequestObject{}}, refusedAfterStop) &&
    !refusedAfterStop->processed.load_() &&
    !refusedAfterStop->failures.load_());
  if (clientInited) client.final();
  if (serverInited) server.final();

  app.errorAt = 0;
  app.observeAt = 0;
  app.deferNext = 0;
  app.redirectNext = 0;
  Zrest::ServerConfig reuseServerConfig;
  reuseServerConfig.localIP(ZiIP{"127.0.0.1"}).port(port)
    .retainedBodyMax(4096).tcp();
  bool serverReused = server.init(hub, ZuMv(reuseServerConfig), &app);
  ZuCheck(serverReused);
  bool serverRestarted = serverReused && server.start() &&
    app.listeningSem.timedwait(Zm::now(10)) == 0;
  ZuCheck(serverRestarted);
  Zrest::ClientConfig reuseClientConfig;
  reuseClientConfig.links(1).concurrency(2).linkMax(2)
    .retainedBodyMax(4096).protocol(Zhttp::ProtoPolicy::DisableH3)
    .secure(false).tcp(true).tls(false).quic(false);
  reuseClientConfig.limits(clientLimits);
  bool clientReused = serverRestarted &&
    client.init(hub, 1, ZuMv(reuseClientConfig), &clientApp);
  ZuCheck(clientReused);
  bool clientRestarted = clientReused && client.pool(0,
    Zhttp::Destination{"127.0.0.1", port, false}) && client.start();
  ZuCheck(clientRestarted);
  if (clientRestarted) {
    ZmRef<Call> reused = new Call{};
    bool reuseAccepted = client.call<ClientRequest>(0,
      ZmRef<RequestObject>{new RequestObject{}}, reused);
    ZuCheck(reuseAccepted);
    bool reuseFinished = reuseAccepted &&
      reused->done.timedwait(Zm::now(10)) == 0;
    ZuCheck(reuseFinished && reused->processed.load_() == 1 &&
      !reused->failures.load_());
  }
  if (clientReused) { ZuCheck(client.stop()); client.final(); }
  if (serverReused) { ZuCheck(server.stop()); server.final(); }

  streamRaw.final();
  streamJSON.final();
  responseStream.final();
  ZiResolver::stop();
  ZiResolver::final();
  mx.stop();
}

} // ZrestEndpointTest_

int main(int argc, char **argv)
{
  ZuTestUtil::parse(argc, argv);
  ZiTestResidue::init("zrestendpoint");
  ZuTestMain();
  ZuTestCall(ZrestEndpointTest_::h2);
  ZuTestCall(ZrestEndpointTest_::h3);
  ZuTestCall(ZrestEndpointTest_::retryLimit);
  ZuTestCall(ZrestEndpointTest_::call);
  return 0;
}
