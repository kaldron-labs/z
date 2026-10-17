//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiResolver.hh>

#include <zlib/ZhttpClient.hh>
#include <zlib/ZhttpServer.hh>

#include "ZhttpTestUtil.hh"

using namespace ZuTestUtil;

namespace ZhttpMessageCounts {
ZmAtomic<unsigned> forbidden = 0, pseudo = 0;
}
// Test-binary interposition uses the same concrete classifier as the library.
// Production helpers and records carry no instrumentation.
namespace Zhttp::Fields {
bool forbidden(ZuBSpan name) {
  ++ZhttpMessageCounts::forbidden;
  return classify(name).forbidden;
}
int pseudo(ZuBSpan name) {
  ++ZhttpMessageCounts::pseudo;
  return classify(name).pseudo;
}
}

namespace ZhttpMessageHubTest_ {

#if defined(ZHTTP_TEST_H1TCP)
enum { TestPort = ZhttpTestPort::MsgH1TCP };
#elif defined(ZHTTP_TEST_H1TLS)
enum { TestPort = ZhttpTestPort::MsgH1TLS };
#elif defined(ZHTTP_TEST_H2)
enum { TestPort = ZhttpTestPort::MsgH2 };
#elif defined(ZHTTP_TEST_H3)
enum { TestPort = ZhttpTestPort::MsgH3 };
#endif

using TestHeaderList = ZhttpHeaders("x-test", "x-trailer");
ZhttpHdrCatalogDerive(TestHeaders, TestHeaderList);
ZhttpHdrCatalogImpl(TestHeaders)
using RequestHeaderList =
  ZhttpHeaders(("x-fixed", "fixed"), "content-length");
ZhttpHdrCatalogDerive(RequestHeaders, RequestHeaderList);
ZhttpHdrCatalogImpl(RequestHeaders)
using ResponseHeaderList = ZhttpHeaders("x-test", ("x-seed", "shared"));
ZhttpHdrCatalogDerive(ResponseHeaders, ResponseHeaderList);
ZhttpHdrCatalogImpl(ResponseHeaders)

using DuplicateList = ZhttpHeaders("connection", ":path", "connection", ":path");
ZhttpHdrCatalogDerive(DuplicateHeaders, DuplicateList);
ZhttpHdrCatalogImpl(DuplicateHeaders)

void catalogs()
{
  ZuTestScope(catalogs);
  auto forbidden = ZhttpMessageCounts::forbidden.load_();
  auto pseudo = ZhttpMessageCounts::pseudo.load_();
  const auto &duplicate = Zhttp::CatalogData<DuplicateHeaders>::configure();
  ZuCHECK(ZhttpMessageCounts::forbidden == forbidden + 2 &&
    ZhttpMessageCounts::pseudo == pseudo + 2,
    "each classifier runs once per distinct configured name");
  ZuCHECK(duplicate.fields[0].classification == duplicate.fields[2].classification &&
    duplicate.fields[1].classification == duplicate.fields[3].classification,
    "duplicate names share immutable classification");
  (void)Zhttp::CatalogData<DuplicateHeaders>::configure();
  ZuCHECK(ZhttpMessageCounts::forbidden == forbidden + 2 &&
    ZhttpMessageCounts::pseudo == pseudo + 2, "reconfiguration reuses published metadata");
  (void)Zhttp::CatalogData<RequestHeaders>::configure();
  (void)Zhttp::CatalogData<ResponseHeaders>::configure();
  (void)Zhttp::CatalogData<TestHeaders>::configure();
  ZuCHECK(ZhttpMessageCounts::forbidden == forbidden + 8 &&
    ZhttpMessageCounts::pseudo == pseudo + 8,
    "request and response catalogs configure all distinct names once");
}

struct State {
  ZmSemaphore	listening;
  ZmSemaphore	response;
  ZmAtomic<unsigned> errors = 0;
  ZmAtomic<unsigned> admissions = 0;
  ZmAtomic<unsigned> statuses = 0;
  ZmAtomic<unsigned> bodyBytes = 0;
  ZmAtomic<unsigned> completions = 0;
  bool channel = false, multiplexed = false;
  int resultCode = -1;
  ZtString<> receivedPing, receivedPong;
  unsigned pingCalls = 0, pongCalls = 0;
  static int ping(void *ctx, Zhttp::Channel *channel, Zhttp::Input &input) {
    auto &state = *static_cast<State *>(ctx);
    if (!Zhttp::bodyEach(input, [&state](ZuSpan<uint8_t> bytes) {
      state.receivedPing << bytes;
    })) return -1;
    if (state.receivedPing == "ping") {
      ++state.pingCalls;
      channel->streamTx([](Zhttp::Writer &out) { out << "pong"; });
    }
    return 1;
  }
  static int pong(void *ctx, Zhttp::Channel *, Zhttp::Input &input) {
    auto &state = *static_cast<State *>(ctx);
    if (!Zhttp::bodyEach(input, [&state](ZuSpan<uint8_t> bytes) {
      state.receivedPong << bytes;
    })) return -1;
    if (state.receivedPong == "pong") {
      ++state.pongCalls;
      state.response.post();
    }
    return 1;
  }
  unsigned	expected = 1;
  uint16_t	port = 0;
};

template <typename P, typename S = Zhttp::Profiles<P>>
struct Case : P { using Selection = S; };

template <typename Profile> struct Client;
template <typename Profile> struct Pool;
template <typename Profile> struct Server;

template <typename Profile>
constexpr int transport() {
  if constexpr (ZuIsSame<typename Profile::Protocol, Zhttp::TCP>{})
    return Zhttp::Transport::TCP;
  else if constexpr (ZuIsSame<typename Profile::Protocol, Zhttp::TLS>{})
    return Zhttp::Transport::TLS;
  else return Zhttp::Transport::QUIC;
}

template <typename Profile>
struct RequestBuilder : public ZmObject, public Zhttp::ReqBuilder {
  using HdrCatalog = RequestHeaders;
  uint64_t key() const { return id; }
  Zhttp::BodyPolicy::T bodyPolicy() const {
    return state->channel ? Zhttp::BodyPolicy::None : Zhttp::BodyPolicy::Fixed;
  }
  template <typename L> void protocol(L &&l) {
    if (state->channel) l("opaque");
  }
  template <typename L>
  void operation(L &&l) {
    l(state->channel ? (Profile::Multiplexed ? Zhttp::Method::CONNECT :
      Zhttp::Method::GET) : Zhttp::Method::PUT, [](auto &&emit) {
      emit([](auto &tx) { tx << '/'; });
    });
  }
  template <typename Key, typename Value, typename L>
  void header(L &&l) {
    ++fixedCalls;
    l();
  }
  template <typename Key, typename L>
  void header(L &&l) {
    ++keyedCalls;
    if constexpr (Key{}() == "content-length") l(Zhttp::contentLengthPad());
  }
  template <typename L>
  void header(L &&l) {
    ++runtimeCalls;
    if (state->channel && !Profile::Multiplexed) {
      l("connection", "Upgrade");
      l("upgrade", "opaque");
    }
    l("x-runtime-one", "one");
    l("x-runtime-two", "two");
    ZtString<> growth;
    growth.ensure(4096);
    for (unsigned i = 0; i < 4096; ++i) growth << 'x';
    l(ZtString<>{"x-growth"}, growth);
  }
  template <typename Emit>
  void body(Emit &&emit) {
    emit([](Zhttp::Writer &body) {
      body << ZuCSpan{"he"} << ZuCSpan{"llo"};
      return Zhttp::WriteOutcome::End;
    });
  }
  template <typename L>
  void bodyHdrs(L &&patch) { Zhttp::contentLengthSet(patch, 5); }
  void connected(const Zhttp::ConnectedInfo &info) {
    if (info.httpVersion != Profile::HTTPVersion ||
	info.transport != transport<Profile>() ||
	info.multiplexed != bool(Profile::Multiplexed)) ++state->errors;
  }
  void completed(const Zhttp::Result &result) {
    if (state->channel) { state->resultCode = result.code; return; }
    if (!result.ok() || keyedCalls != 2 || runtimeCalls != 1 || fixedCalls != 1)
      ++state->errors;
    state->response.post();
  }

  uint64_t id = 0;
  State *state = nullptr;
  unsigned keyedCalls = 0;
  unsigned runtimeCalls = 0;
  unsigned fixedCalls = 0;
};

template <typename Profile>
struct ClientParser : public Zhttp::Parser {
  using HdrCatalog = TestHeaders;
  void init(const RequestBuilder<Profile> &request) { shared = request.state; }
  bool enable1xx() const { return true; }
  void status(unsigned value) {
    status_ = value;
    ++statusCalls;
    ++shared->statuses;
  }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (Key{}() == "x-test") xTest = value;
    else if constexpr (Key{}() == "x-trailer") xTrailer = value;
  }
  void header(Zhttp::FieldSection::T, ZuBSpan name, ZuSpan<uint8_t> value) {
    if (name == "x-seed" && value == "shared") ++seedCalls;
  }
  bool bodyInfo(Zhttp::Channel *channel, Zhttp::BodyType::T, uint64_t) {
    if (!shared->channel) return true;
    if (status_ != (shared->multiplexed ? 200 : 101) ||
	!channel->streamEnable() || !channel->streamAccept({shared, State::pong}) ||
	!channel->streamLocalCap() || !channel->streamPeerCap()) {
      ++shared->errors;
      return false;
    }
    channel->streamTx([](Zhttp::Writer &out) { out << "ping"; });
    return true;
  }
  bool body(Zhttp::Input &rx) {
    return Zhttp::bodyEach(rx, [this](ZuSpan<uint8_t> value) {
      shared->bodyBytes += value.length();
      body_ << value;
    });
  }
  void complete(Zhttp::Channel *, bool ok) {
    if (shared->channel) return;
    ++shared->completions;
    if (!ok || status_ != 200 || statusCalls != 2 ||
	body_ != "pong" || xTest != "final" || xTrailer || seedCalls != 2) ++shared->errors;
  }
  void reset() {
    body_.null();
    xTest.null();
    xTrailer.null();
    status_ = statusCalls = seedCalls = 0;
  }
  ZtString<> body_, xTest, xTrailer;
  unsigned status_ = 0;
  unsigned statusCalls = 0;
  unsigned seedCalls = 0;
  State *shared = nullptr;
};

template <typename Profile>
struct RequestQ : ZmPQueue<RequestBuilder<Profile>,
  ZmPQueueOverlap<false, ZmPQueueNode<RequestBuilder<Profile>,
    ZmPQueueHeapID<"Zhttp.Test.MessageReq">>>> { };

template <typename Profile>
using TxQ = ZmPQTx<Pool<Profile>, RequestQ<Profile>, ZmPQTxOrdered<false>>;

template <typename Profile>
struct Pool : public ZmNHeap<Pool<Profile>>,
  public Zhttp::Pool<Client<Profile>, TxQ<Profile>, ClientParser<Profile>,
    typename Profile::Selection> {
  friend inline ZuStringT<"Zhttp.Test.MessagePool"> ZmNHeapID(Pool *);
  using Base = Zhttp::Pool<Client<Profile>, TxQ<Profile>, ClientParser<Profile>,
    typename Profile::Selection>;
  using Request = typename RequestQ<Profile>::Node;
  Pool(Client<Profile> *app) : Base{app} { }
  RequestQ<Profile> *txQueue() { return &requests; }
  void archive_(Request *) { }
  ZmRef<Request> retrieve_(uint64_t, uint64_t) { return {}; }
  RequestQ<Profile> requests;
};

template <typename Profile>
struct Client : public Zhttp::Client<Client<Profile>, Pool<Profile>> { };

struct ResponseBuilder : public ZmObject, public Zhttp::ResBuilder {
  using HdrCatalog = ZhttpMessageHubTest_::ResponseHeaders;
  using ResponseHeaders = ZuTypeList<HdrCatalog>;
  unsigned status() const {
    return state->channel && !state->multiplexed ? 101 : informational ? 103 : 200;
  }
  bool streamResponse() const { return state->channel; }
  Zhttp::BodyPolicy::T bodyPolicy() const {
    return informational || state->channel ? Zhttp::BodyPolicy::None : Zhttp::BodyPolicy::Stream;
  }
  template <typename Key, typename Value, typename L>
  void header(L &&l) { ++fixedCalls; l(); }
  template <typename Key, typename L>
  void header(L &&l) {
    ++keyedCalls;
    if constexpr (Key{}() == "x-test") l(informational ? "early" : "final");
  }
  template <typename L> void header(L &&l) {
    ++runtimeCalls;
    if (state->channel && !state->multiplexed) {
      l("connection", "Upgrade");
      l("upgrade", "opaque");
    }
  }
  template <typename Emit>
  void body(Emit &&emit) {
    emit([](Zhttp::Writer &body) {
      body << ZuCSpan{"pong"};
      return Zhttp::WriteOutcome::End;
    });
  }
  void completed(const Zhttp::ResponseResult &result) {
    if (!result.ok() || keyedCalls != 2 || fixedCalls != 1 || runtimeCalls != 1) ++state->errors;
  }
  State *state = nullptr;
  bool informational = false;
  unsigned keyedCalls = 0;
  unsigned fixedCalls = 0;
  unsigned runtimeCalls = 0;
};
using ResponseQ = Zhttp::ResponseQ<ResponseBuilder,
  ZuStringT<"Zhttp.Test.MessageRes">>;

template <typename Profile>
struct ServerParser : public Zhttp::Parser {
  using HdrCatalog = Zhttp::DefltHdrCatalog;
  void init(Server<Profile> &app) { state = app.state; }
  bool operation(Zhttp::Method::T method_, Zhttp::Target &target) {
    method = method_;
    path = target.path;
    if (state->channel && (method != (Profile::Multiplexed ?
	Zhttp::Method::CONNECT : Zhttp::Method::GET) || target.path != "/" ||
	(Profile::Multiplexed && target.protocol != "opaque"))) ++state->errors;
    return true;
  }
  void header(Zhttp::FieldSection::T, ZuBSpan name, ZuSpan<uint8_t> value) {
    if (name == "x-fixed" && value == "fixed") fixed = true;
    if (name == "x-runtime-one" && value == "one") runtimeOne = true;
    if (name == "x-runtime-two" && value == "two") runtimeTwo = true;
  }
  bool bodyInfo(Zhttp::Channel *channel, Zhttp::BodyType::T, uint64_t) {
    if (!state->channel) return true;
    if (!channel->streamEnable() || !channel->streamAccept({state, State::ping}) ||
	!channel->streamLocalCap() || !channel->streamPeerCap()) {
      ++state->errors;
      return false;
    }
    ZmRef<ResponseQ::Node> response = new ResponseQ::Node;
    response->state = state;
    channel->send(ZuMv(response));
    return true;
  }
  bool body(Zhttp::Input &rx) {
    return Zhttp::bodyEach(rx, [this](ZuSpan<uint8_t> span) { body_ << span; });
  }
  void complete(Zhttp::Channel *channel, bool ok) {
    if (state->channel) return;
    if (!ok) { ++state->errors; return; }
    if (method != Zhttp::Method::PUT || path != "/" || body_ != "hello" ||
	!fixed || !runtimeOne || !runtimeTwo) ++state->errors;
    for (bool informational: {true, false}) {
      ZmRef<ResponseQ::Node> response = new ResponseQ::Node;
      response->state = state;
      response->informational = informational;
      channel->send(ZuMv(response));
    }
  }
  void reset() {
    path.null();
    body_.null();
    fixed = runtimeOne = runtimeTwo = false;
  }
  State *state = nullptr;
  ZtString<> path, body_;
  Zhttp::Method::T method = -1;
  bool fixed = false, runtimeOne = false, runtimeTwo = false;
};

template <typename Profile>
struct Server : public Zhttp::Server<Server<Profile>, ServerParser<Profile>,
  ResponseQ, typename Profile::Selection> {
  using Base = Zhttp::Server<Server<Profile>, ServerParser<Profile>, ResponseQ,
    typename Profile::Selection>;
  using Base::connected;
  using Base::disconnected;
  Server(State *state_) : state{state_} { }
  void connected(int8_t) { }
  void connected(Zhttp::Session) { ++state->admissions; }
  void disconnected(int8_t) { }
  void listening(int8_t, unsigned) { state->listening.post(); }
  void listenFailed(int8_t, bool) { ++state->errors; state->listening.post(); }
  State *state;
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

template <typename Profile>
void run(const Zhttp::Test::TempDir &cert, bool channel = false)
{
  ZuTestScope(run);
  auto forbidden = ZhttpMessageCounts::forbidden.load_();
  auto pseudo = ZhttpMessageCounts::pseudo.load_();
  State state;
  state.channel = channel;
  state.multiplexed = Profile::Multiplexed;
  state.expected = channel ? 1 : 2;
  static unsigned nextPort = TestPort;
  state.port = Zhttp::Test::loopbackPort(nextPort++);
  ZuCHECK(state.port, "port allocation failed");
  if (!state.port) return;
  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "multiplexer start failed");
  if (!mx.running()) return;
  Zhttp::HubConfig hub{&mx, "3", "4"};
  Server<Profile> server{&state};
  Client<Profile> client;
  auto serverConfig = Zhttp::ServerConfig{}.localIP(ZiIP{"127.0.0.1"}).port(state.port);
  Zhttp::H2Config tls;
  Zhttp::QUICConfig quic;
  tls.caPath(cert.certPath.cspan()).extendedConnect(channel);
  quic.caPath(cert.certPath.cspan()).extendedConnect(channel);
  if constexpr (ZuIsSame<typename Profile::Protocol, Zhttp::TCP>{})
    serverConfig.tcp();
  else if constexpr (ZuIsSame<typename Profile::Protocol, Zhttp::TLS>{})
    serverConfig.tls(Zhttp::H2Config{}.certPath(cert.certPath.cspan())
      .keyPath(cert.keyPath.cspan()).extendedConnect(channel).policy(Profile::Multiplexed ?
	(typename Profile::Selection::template Has<Zhttp::H1TLS>{} ?
	  Zhttp::H2Policy::Prefer : Zhttp::H2Policy::Force) :
	Zhttp::H2Policy::Disable));
  else serverConfig.quic(Zhttp::QUICConfig{}.certPath(cert.certPath.cspan())
    .keyPath(cert.keyPath.cspan()).extendedConnect(channel));
  bool serverInit = server.init(hub, ZuMv(serverConfig));
  bool clientInit = client.init(hub, 1,
    Zhttp::Config{}.concurrency(state.expected).linkMax(state.expected)
      .blindH3(Profile::HTTPVersion == Zhttp::Version::H3)
      .protocol(Profile::HTTPVersion == Zhttp::Version::H3 ?
	Zhttp::ProtoPolicy::PreferH3 : Zhttp::ProtoPolicy::DisableH3), {}, tls, quic);
  bool pooled = clientInit && client.pool(0,
    Zhttp::Destination{"127.0.0.1", state.port});
  ZuCHECK(serverInit, "server initialized");
  ZuCHECK(pooled, "client pool initialized");
  bool serverUp = serverInit && server.start();
  bool clientUp = pooled && client.start();
  ZuCHECK(serverUp && clientUp, "endpoints started");
  bool listening = serverUp && state.listening.timedwait(Zm::now(10)) == 0;
  ZuCHECK(listening, "server listening");
  if (listening && clientUp)
    for (unsigned i = 0; i < state.expected; ++i) {
      ZmRef<typename RequestQ<Profile>::Node> request = new typename RequestQ<Profile>::Node;
      request->state = &state;
      request->id = i;
      ZuCHECK(client.send(0, ZuMv(request)), "request submitted");
    }
  bool responses = true;
  if (clientUp)
    for (unsigned i = 0; i < state.expected; ++i)
      responses &= state.response.timedwait(Zm::now(10)) == 0;
  ZuCHECK(state.statuses == state.expected * (channel ? 1 : 2),
    "informational and final statuses received");
  ZuCHECK(channel || state.bodyBytes == state.expected * 4, "response bodies received");
  ZuCHECK(channel || state.completions == state.expected,
    "final responses completed exactly once");
  if (!responses) log_("channel progress: ping=", state.receivedPing ?
      state.receivedPing.data() : "", " pong=", state.receivedPong ?
      state.receivedPong.data() : "", " result=", state.resultCode,
      " statuses=", state.statuses.load_(), " errors=", state.errors.load_());
  ZuCHECK(responses, "all responses received");
  if (channel) ZuCHECK(state.receivedPing == "ping" && state.receivedPong == "pong" &&
      state.pingCalls == 1 && state.pongCalls == 1,
    "Extended CONNECT keeps both channel directions open for framed input");
  ZuCHECK(!state.errors, "common message workload has no errors");
  if (state.admissions != 1) log_("session admissions=", state.admissions.load_());
  ZuCHECK(state.admissions == 1,
    "physical connection admissions normalized");
  if (clientInit) client.stop();
  bool stopped = !serverInit || server.stop();
  ZuCHECK(stopped && !state.errors, "endpoints stopped");
  if (clientInit) client.final();
  if (serverInit) server.final();
  mx.stop();
  ZuCHECK(ZhttpMessageCounts::forbidden == forbidden &&
    ZhttpMessageCounts::pseudo == pseudo,
    "connect, seeds, messages and shutdown perform no classification-helper calls");
}

} // namespace ZhttpMessageHubTest_

int main(int argc, char **argv)
{
  using namespace ZhttpMessageHubTest_;

  parse(argc, argv);
#if defined(ZHTTP_TEST_H1TCP)
  ZiTestResidue::init("ZhttpH1MessageTest");
#elif defined(ZHTTP_TEST_H1TLS)
  ZiTestResidue::init("ZhttpH1TLSMessageTest");
#elif defined(ZHTTP_TEST_H2)
  ZiTestResidue::init("ZhttpH2MessageTest");
#elif defined(ZHTTP_TEST_H3)
  ZiTestResidue::init("ZhttpH3MessageTest");
#endif
  ZuTestMain();
  ZuTestCall(catalogs);
  Zhttp::Test::TempDir cert;
  if (!cert.init()) return 1;
  ZiResolver::start();
#if defined(ZHTTP_TEST_H1TCP)
  ZuTestCall((run<Case<Zhttp::H1TCP>>), cert);
  ZuTestCall((run<Case<Zhttp::H1TCP>>), cert, true);
#elif defined(ZHTTP_TEST_H1TLS)
  ZuTestCall((run<Case<Zhttp::H1TLS>>), cert);
  ZuTestCall((run<Case<Zhttp::H1TLS>>), cert, true);
#elif defined(ZHTTP_TEST_H2)
  ZuTestCall((run<Case<Zhttp::H2TLS>>), cert);
  ZuTestCall((run<Case<Zhttp::H2TLS>>), cert, true);
  using SharedTLS = Case<Zhttp::H2TLS,
    Zhttp::Profiles<Zhttp::H1TLS, Zhttp::H2TLS>>;
  using H2H3 = Case<Zhttp::H2TLS,
    Zhttp::Profiles<Zhttp::H2TLS, Zhttp::H3QUIC>>;
  using All = Case<Zhttp::H2TLS, Zhttp::AllProfiles>;
  ZuTestCall((run<SharedTLS>), cert);
  ZuTestCall((run<H2H3>), cert);
  ZuTestCall((run<All>), cert);
#elif defined(ZHTTP_TEST_H3)
  ZuTestCall((run<Case<Zhttp::H3QUIC>>), cert);
  ZuTestCall((run<Case<Zhttp::H3QUIC>>), cert, true);
  using H3H2 = Case<Zhttp::H3QUIC,
    Zhttp::Profiles<Zhttp::H2TLS, Zhttp::H3QUIC>>;
  using AllH3 = Case<Zhttp::H3QUIC, Zhttp::AllProfiles>;
  ZuTestCall((run<H3H2>), cert);
  ZuTestCall((run<AllH3>), cert);
#else
#error HTTP profile test is not selected
#endif
  ZiResolver::stop();
  ZiResolver::final();
}
