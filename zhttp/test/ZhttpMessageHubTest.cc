//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZiMultiplex.hh>

#include <zlib/Zhttp.hh>
#include <zlib/ZhttpServer.hh>

#include "ZhttpTestUtil.hh"

using namespace ZuTestUtil;

namespace ZhttpMessageHubTest_ {

using TestHeaders = ZhttpHeaders("x-test", "x-trailer");

struct State {
  ZmSemaphore	listening;
  ZmSemaphore	response;
  ZmAtomic<unsigned> errors = 0;
  ZmAtomic<unsigned> admissions = 0;
  ZmAtomic<unsigned> statuses = 0;
  ZmAtomic<unsigned> bodyBytes = 0;
  ZmAtomic<unsigned> completions = 0;
  unsigned	expected = 1;
  uint16_t	port = 0;
};

template <typename Profile>
struct RequestBuilder :
  public Zhttp::MessageTraits<Profile>::template Request<
    RequestBuilder<Profile>, ZhttpHeaders("content-length"),
    ZuTypeList<>, true, false> {
  template <typename L>
  void operation(L &&l) { l(Zhttp::Method::PUT, "/"); }
  template <typename L>
  void host(L &&l) { l("127.0.0.1"); }
  template <typename Key, typename L>
  void header(L &&l) {
    if constexpr (Key{}() == "content-length") l("5");
  }
};

template <typename Profile>
struct InfoBuilder :
  public Zhttp::MessageTraits<Profile>::template Response<
    InfoBuilder<Profile>, ZhttpHeaders("x-test"),
    ZuTypeList<>, false, false> {
  unsigned status() { return 103; }
  template <typename L> void reason(L &&l) { l("Early Hints"); }
  template <typename Key, typename L>
  void header(L &&l) {
    if constexpr (Key{}() == "x-test") l("early");
  }
};

template <typename Profile>
struct ResponseBuilder :
  public Zhttp::MessageTraits<Profile>::template Response<
    ResponseBuilder<Profile>, ZhttpHeaders("x-test"),
    ZhttpHeaders("x-trailer"), true, true> {
  unsigned status() { return 200; }
  template <typename L> void reason(L &&l) { l("OK"); }
  uint64_t contentLength() { return 4; }
  template <typename Key, typename L>
  void header(L &&l) {
    if constexpr (Key{}() == "x-test")
      l("final");
    else if constexpr (Key{}() == "x-trailer")
      l("done");
  }
};

template <typename Profile> struct Client;
template <typename Profile> struct ClientLink;

template <typename Profile>
struct ClientParser :
  public Zhttp::MessageTraits<Profile>::template ResponseParser<
    ClientParser<Profile>, TestHeaders, Zhttp::DefltMaxBody> {
  using Base = typename Zhttp::MessageTraits<Profile>::template ResponseParser<
    ClientParser, TestHeaders, Zhttp::DefltMaxBody>;
  using State = typename Base::State;

  void operation(Zhttp::Method::T, const Zhttp::RequestTarget &) { }
  void status(unsigned value) {
    status_ = value;
    ++statusCalls;
    ++shared->statuses;
  }
  void contentLength(uint64_t value) { length = value; }
  void chunked() { }
  void version(ZuBSpan) { }
  template <typename Key> void header(ZuBSpan value) {
    if constexpr (Key{}() == "x-test")
      xTest = value;
    else if constexpr (Key{}() == "x-trailer")
      xTrailer = value;
  }
  template <typename Rx>
  void body(Rx &rx) {
    Zhttp::bodyEach(rx, [this](ZuBSpan value) {
      shared->bodyBytes += value.length();
      body_ << value;
    });
  }
  void complete(typename State::T value) {
    ++shared->completions;
    complete_ = value;
  }
  bool finReceived() const { return false; }

  ZtString<>	body_;
  ZtString<>	xTest;
  ZtString<>	xTrailer;
  uint64_t	length = 0;
  unsigned	status_ = 0;
  unsigned	statusCalls = 0;
  typename State::T complete_ = State::Initial;
  ZhttpMessageHubTest_::State *shared = nullptr;
};

template <typename Profile>
struct Client : public Zhttp::ClientHub<Client<Profile>, Profile> {
  using Link = ClientLink<Profile>;
  using HTTP = Zhttp::ProfileTraits<Profile>;
  State *state = nullptr;

  Client(State *state_) : state{state_} { }

  void connected(Link &link, Zhttp::ConnectedInfo info) {
    if (info.httpVersion != HTTP::HTTPVersion ||
	info.transport != HTTP::Transport::ID ||
	info.multiplexed != bool(HTTP::Multiplexed)) {
      ++state->errors;
      state->response.post();
      return;
    }
    auto tx = link.transmit(request);
    request.begin(tx);
    {
      auto body = request.body(tx, 5);
      body << ZuCSpan{"he"} << ZuCSpan{"llo"};
      body.flush();
      if (!body.complete()) ++state->errors;
    }
    request.finish(tx);
    link.finish();
  }
  void disconnected(Link &, bool) { }
  void connectFailed(Link &, bool) {
    ++state->errors;
    state->response.post();
  }
  template <typename Rx>
  int process(Link &link, Rx &rx) {
    auto state_ = link.receive(link.parser, rx);
    if (state_ == ClientParser<Profile>::State::Error) {
      ++state->errors;
      state->response.post();
      return -1;
    }
    if (state_ != ClientParser<Profile>::State::Complete) return 0;
    if (link.parser.status_ != 200 || link.parser.statusCalls != 2 ||
	link.parser.body_ != "pong" ||
	link.parser.xTest != "final" || link.parser.xTrailer != "done")
      ++state->errors;
    state->response.post();
    link.disconnect();
    return 1;
  }

  RequestBuilder<Profile> request;
};

template <typename Profile>
struct ClientLink :
  public Zhttp::ClientLink<Client<Profile>, ClientLink<Profile>, Profile> {
  using Base =
    Zhttp::ClientLink<Client<Profile>, ClientLink, Profile>;

  ClientLink(Client<Profile> *app) : Base{app} {
    parser.shared = app->state;
  }

  ClientParser<Profile> parser;
};

template <typename Profile> struct Server;
template <typename Profile> struct ServerLink;

template <typename Profile>
struct ServerSession {
  using Message = Zhttp::MessageTraits<Profile>;

  struct Parser :
    public Message::template RequestParser<
      Parser, ZuTypeList<>, Zhttp::DefltMaxBody> {
    using Base = typename Message::template RequestParser<
      Parser, ZuTypeList<>, Zhttp::DefltMaxBody>;
    using State = typename Base::State;

    void operation(
      Zhttp::Method::T method_, const Zhttp::RequestTarget &target) {
      method = method_;
      path = target.raw;
    }
    void status(unsigned) { }
    void contentLength(uint64_t) { }
    void chunked() { }
    void version(ZuBSpan) { }
    template <typename Key> void header(ZuBSpan) { }
    template <typename Rx>
    void body(Rx &rx) {
      Zhttp::bodyEach(rx, [this](ZuBSpan span) { body_ << span; });
    }
    void complete(typename State::T value) { complete_ = value; }
    bool finReceived() const { return false; }

    ZtString<>		path;
    ZtString<>		body_;
    Zhttp::Method::T	method = -1;
    typename State::T	complete_ = State::Initial;
  } parser;

  template <typename Link>
  void connected(Link &) { connected_ = true; }
  template <typename Link>
  void disconnected(Link &, bool) { }
  template <typename Link, typename Rx>
  int process(Link &link, Rx &rx) {
    if (!connected_) ++link.app()->state->errors;
    auto state = link.receive(parser, rx);
    if (state == Parser::State::Error) return -1;
    if (state != Parser::State::Complete) return 0;
    if (parser.method != Zhttp::Method::PUT || parser.path != "/" ||
	parser.body_ != "hello")
      ++link.app()->state->errors;
    {
      InfoBuilder<Profile> info;
      auto infoTx = link.transmit(info);
      info.begin(infoTx);
      info.finish(infoTx);
    }
    ResponseBuilder<Profile> response;
    auto tx = link.transmit(response);
    response.begin(tx);
    {
      auto body = response.body(tx);
      body << ZuCSpan{"pong"};
      body.flush();
    }
    response.finish(tx);
    link.finish();
    if constexpr (!Message::OneMessagePerLink) parser.reset();
    return 1;
  }

  bool connected_ = false;
};

template <typename Profile>
struct Server : public Zhttp::ProtocolServer<Server<Profile>, Profile> {
  using Link = ServerLink<Profile>;
  State *state = nullptr;

  Server(State *state_) : state{state_} { }

  ZiIP localIP() const { return ZiIP{"127.0.0.1"}; }
  unsigned localPort() const { return state->port; }
  bool admit(const auto &) {
    ++state->admissions;
    return true;
  }
  void release() { }
  void listening(const ZiListenInfo &) { state->listening.post(); }
  void listening() { state->listening.post(); }
  void listenFailed(bool) {
    ++state->errors;
    state->listening.post();
  }
  void disconnected(Link &, bool) { }
};

template <typename Profile>
struct ServerLink :
  public Zhttp::ServerLink<
    Server<Profile>, ServerLink<Profile>, Profile,
    ServerSession<Profile>> {
  using Base = Zhttp::ServerLink<
    Server<Profile>, ServerLink, Profile, ServerSession<Profile>>;
  using Base::Base;
};

template <typename Profile> struct Config;
template <> struct Config<Zhttp::H1TCP> {
  static auto client(const Zhttp::Test::TempDir &) {
    return Zhttp::TCPConfig{};
  }
  static auto server(const Zhttp::Test::TempDir &) {
    return Zhttp::TCPConfig{};
  }
};
template <> struct Config<Zhttp::H1TLS> {
  static auto client(const Zhttp::Test::TempDir &cert) {
    return Zhttp::TLSConfig{}.caPath(cert.certPath.cspan());
  }
  static auto server(const Zhttp::Test::TempDir &cert) {
    return Zhttp::TLSConfig{}
      .certPath(cert.certPath.cspan()).keyPath(cert.keyPath.cspan());
  }
};
template <> struct Config<Zhttp::H2TLS> {
  static auto client(const Zhttp::Test::TempDir &cert) {
    return Zhttp::H2Config{}.caPath(cert.certPath.cspan());
  }
  static auto server(const Zhttp::Test::TempDir &cert) {
    return Zhttp::H2Config{}
      .certPath(cert.certPath.cspan()).keyPath(cert.keyPath.cspan());
  }
};
template <> struct Config<Zhttp::H3QUIC> {
  static auto client(const Zhttp::Test::TempDir &cert) {
    return Zhttp::QUICConfig{}.caPath(cert.certPath.cspan());
  }
  static auto server(const Zhttp::Test::TempDir &cert) {
    return Zhttp::QUICConfig{}
      .certPath(cert.certPath.cspan()).keyPath(cert.keyPath.cspan());
  }
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
void run(const Zhttp::Test::TempDir &cert)
{
  ZuTestScope(run);

  State state;
  state.expected = Zhttp::ProfileTraits<Profile>::Multiplexed ? 2 : 1;
  state.port = Zhttp::Test::loopbackPort();
  ZuCHECK(state.port, "port allocation failed");
  if (!state.port) return;

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "multiplexer start failed");
  if (!mx.running()) return;

  Zhttp::HubConfig hub{&mx, "3", "4"};
  Server<Profile> server{&state};
  Client<Profile> client{&state};
  bool serverInit = server.init(hub, Config<Profile>::server(cert));
  bool clientInit = client.init(hub, Config<Profile>::client(cert));
  bool initialized = serverInit && clientInit;
  ZuCHECK(serverInit, "server hub initialized");
  ZuCHECK(clientInit, "client hub initialized");
  if (!initialized) {
    if (clientInit) client.final();
    if (serverInit) server.final();
    mx.stop();
    return;
  }
  bool started = server.start() && client.start();
  ZuCHECK(started, "hubs started");
  bool listening = state.listening.timedwait(Zm::now(10)) == 0;
  ZuCHECK(listening, "server listening");

  using Link = ClientLink<Profile>;
  ZtArray<ZmRef<Link>,
    ZtArrayHeapID<"Zhttp.Test.MessageLinks">> links;
  if (listening)
    for (unsigned i = 0; i < state.expected; ++i) {
      ZmRef<Link> link = new Link{&client};
      link->connect("127.0.0.1", state.port);
      links.push(ZuMv(link));
    }
  bool responses = true;
  for (unsigned i = 0; i < state.expected; ++i)
    responses &= state.response.timedwait(Zm::now(10)) == 0;
  unsigned expectedAdmissions =
    Zhttp::ProfileTraits<Profile>::Multiplexed ? 1 : state.expected;
  ZuCHECK(state.statuses == state.expected * 2,
    "informational and final statuses received");
  ZuCHECK(state.bodyBytes == state.expected * 4,
    "response bodies received");
  ZuCHECK(state.completions == state.expected,
    "final responses completed exactly once");
  ZuCHECK(responses, "all responses received");
  ZuCHECK(!state.errors, "common message workload has no errors");
  ZuCHECK(state.admissions == expectedAdmissions,
    "physical connection admissions normalized");

  bool stopped = client.stop() && server.stop();
  ZuCHECK(stopped && !state.errors, "hubs stopped");
  client.final();
  server.final();
  mx.stop();
}

} // namespace ZhttpMessageHubTest_

int main(int argc, char **argv)
{
  using namespace ZhttpMessageHubTest_;

  parse(argc, argv);
  ZuTestMain();
  Zhttp::Test::TempDir cert;
  if (!cert.init()) return 1;
#if defined(ZHTTP_TEST_H1TCP)
  ZuTestCall((run<Zhttp::H1TCP>), cert);
#elif defined(ZHTTP_TEST_H1TLS)
  ZuTestCall((run<Zhttp::H1TLS>), cert);
#elif defined(ZHTTP_TEST_H2)
  ZuTestCall((run<Zhttp::H2TLS>), cert);
#elif defined(ZHTTP_TEST_H3)
  ZuTestCall((run<Zhttp::H3QUIC>), cert);
#else
#error HTTP profile test is not selected
#endif
}
