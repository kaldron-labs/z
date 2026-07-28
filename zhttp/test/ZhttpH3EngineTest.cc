//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include "ZhttpEngineFixture.hh"

namespace ZhttpH3EngineTest_ {

template <typename Link>
void tunnelContract(Link &link)
{
  Zhttp::Tunnel tunnel{link};
  (void)tunnel.peerCap();
  (void)tunnel.localCap();
  tunnel.send([](auto &tx) { tx << ZuCSpan{"x"}; });
  tunnel.end();
  tunnel.reset();
}

template void tunnelContract<
  ZhttpH1EngineTest_::Client<Zhttp::H3QUIC>::Link>(
    ZhttpH1EngineTest_::Client<Zhttp::H3QUIC>::Link &);
template void tunnelContract<
  ZhttpH1EngineTest_::ServerLink<Zhttp::H3QUIC>>(
    ZhttpH1EngineTest_::ServerLink<Zhttp::H3QUIC> &);

struct TunnelState {
  ZmSemaphore		listening;
  ZmSemaphore		response;
  ZmSemaphore		reset;
  ZmSemaphore		stopped;
  ZmAtomic<unsigned>	errors = 0;
  ZmAtomic<unsigned>	admissions = 0;
  ZmAtomic<unsigned>	releases = 0;
  uint16_t		port = 0;
};

struct RequestBuilder :
  public Zhttp::H3::Builder<RequestBuilder> {
  template <typename L>
  void operation(L &&l) { l(Zhttp::Method::GET, "/", ""); }
  template <typename L>
  void host(L &&l) { l("127.0.0.1"); }
};

struct TunnelRequestBuilder :
  public Zhttp::H3::Builder<TunnelRequestBuilder> {
  template <typename L>
  void operation(L &&l) {
    l(Zhttp::Method::CONNECT, "/tunnel", "");
  }
  template <typename L>
  void host(L &&l) { l("127.0.0.1"); }
  template <typename L>
  void protocol(L &&l) { l("opaque"); }
};

struct ResponseBuilder :
  public Zhttp::H3::Builder<
    ResponseBuilder, ZuTypeList<>, ZuTypeList<>, true> {
  unsigned status() const { return 200; }
  uint64_t contentLength() const { return 4; }
};

struct TunnelResponseBuilder :
  public Zhttp::H3::Builder<TunnelResponseBuilder> {
  unsigned status() const { return 200; }
};

struct TunnelClient;
struct TunnelClientLink;

struct ClientParser :
  public Zhttp::H3::Parser<ClientParser, false> {
  using Base = Zhttp::H3::Parser<ClientParser, false>;

  void operation(Zhttp::Method::T, ZuBSpan) { }
  void status(unsigned value) { status_ = value; }
  void contentLength(uint64_t value) { length = value; }
  template <typename Key> void header(ZuBSpan) { }
  void headers(Zhttp::Fields::Section section, bool) {
    if (tunnelExpected && section == Zhttp::Fields::Final &&
	status_ >= 200 && status_ < 300) {
      Base::tunnel();
      ++tunnelEstablished;
    }
  }
  template <typename Rx>
  void body(Rx &rx) {
    Zhttp::bodyEach(rx, [this](ZuBSpan value) { body_ << value; });
  }
  template <typename Rx>
  void tunnelData(Rx &rx) {
    while (rx.input()) {
      const uint8_t *offered = nullptr;
      if (rx.consume(
	  [&offered](ZuBSpan span) -> int64_t {
	    offered = span.data();
	    return 1;
	  },
	  [this, &offered](ZuBSpan span) {
	    tunnelNoCopy &= span.data() == offered;
	    tunnelBody << ZuCSpan{span};
	  }) <= 0)
	break;
    }
  }
  void tunnelEnd() { ++tunnelEnds; }
  void tunnelReset() { ++tunnelResets; }
  void complete(Zhttp::H3::ParserState::T state) { complete_ = state; }

  ZtString<>			body_;
  ZtString<>			tunnelBody;
  uint64_t			length = 0;
  unsigned			status_ = 0;
  unsigned			tunnelEstablished = 0;
  unsigned			tunnelEnds = 0;
  unsigned			tunnelResets = 0;
  bool				tunnelExpected = false;
  bool				tunnelNoCopy = true;
  Zhttp::H3::ParserState::T	complete_ =
    Zhttp::H3::ParserState::Initial;
};

struct TunnelClient :
  public Zhttp::Client<TunnelClient, Zhttp::H3QUIC> {
  using Link = TunnelClientLink;

  TunnelState	*state = nullptr;

  TunnelClient(TunnelState *state_) : state{state_} { }

  void connected(Link &link, Zhttp::ConnectedInfo info);
  void disconnected(Link &, bool) { }
  void connectFailed(Link &, bool) {
    ++state->errors;
    state->response.post();
  }
  int process(Link &link, Zquic::RxStream &rx);
};

struct TunnelClientLink :
  public Zhttp::ClientLink<
    TunnelClient, TunnelClientLink, Zhttp::H3QUIC> {
  using Base = Zhttp::ClientLink<
    TunnelClient, TunnelClientLink, Zhttp::H3QUIC>;
  using Base::Base;

  enum Mode { REST, Echo, Reset };

  ClientParser	parser;
  Mode		mode = REST;
  bool		sent = false;
};

void TunnelClient::connected(
  TunnelClientLink &link, Zhttp::ConnectedInfo info)
{
  if (info.httpVersion != Zhttp::Version::H3 ||
      info.transport != Zhttp::Transport::QUIC ||
      info.alpn != "h3") {
    ++state->errors;
    state->response.post();
    return;
  }
  if (link.mode == TunnelClientLink::REST) {
    RequestBuilder builder;
    auto tx = link.transmit(builder);
    if (!builder.request(tx)) {
      ++state->errors;
      state->response.post();
      return;
    }
    link.finish();
    return;
  }
  link.parser.tunnelExpected = true;
  link.parser.requestMethod(Zhttp::Method::CONNECT);
  TunnelRequestBuilder builder;
  auto tx = link.transmit(builder);
  if (!builder.request(tx)) {
    ++state->errors;
    state->response.post();
  }
}

int TunnelClient::process(
  TunnelClientLink &link, Zquic::RxStream &rx)
{
  auto state_ = link.receive(link.parser, rx);
  switch (state_) {
    case Zhttp::H3::ParserState::Tunnel:
      if (link.sent) return 0;
      link.sent = true;
      if (link.mode == TunnelClientLink::Reset) {
	Zhttp::Tunnel{link}.reset();
	return 0;
      }
      Zhttp::Tunnel{link}.send(
	[](auto &body) { body << ZuCSpan{"ping"}; });
      Zhttp::Tunnel{link}.end();
      return 0;
    case Zhttp::H3::ParserState::RemoteClosed:
      if (link.mode != TunnelClientLink::Echo ||
	  link.parser.tunnelEstablished != 1 ||
	  link.parser.tunnelBody != "ping" ||
	  !link.parser.tunnelNoCopy ||
	  link.parser.tunnelEnds != 1 ||
	  link.parser.tunnelResets)
	++state->errors;
      state->response.post();
      link.disconnect();
      return 0;
    case Zhttp::H3::ParserState::Complete:
      if (link.mode != TunnelClientLink::REST ||
	  link.parser.status_ != 200 ||
	  link.parser.length != 4 || link.parser.body_ != "pong")
	++state->errors;
      state->response.post();
      link.disconnect();
      return 0;
    case Zhttp::H3::ParserState::Error:
      ++state->errors;
      state->response.post();
      return -1;
    default:
      return 0;
  }
}

struct TunnelServer;
struct TunnelServerLink;

struct TunnelServerSession {
  struct Parser : public Zhttp::H3::Parser<Parser, true> {
    using Base = Zhttp::H3::Parser<Parser, true>;

    void operation(Zhttp::Method::T method_, ZuBSpan path_) {
      method = method_;
      path = path_;
    }
    void protocol(ZuBSpan value) {
      protocol_ = value;
      Base::tunnel();
      ++tunnelEstablished;
    }
    void headers(Zhttp::Fields::Section, bool) { }
    void contentLength(uint64_t) { }
    void status(unsigned) { }
    template <typename Key> void header(ZuBSpan) { }
    template <typename Rx>
    void body(Rx &rx) { Zhttp::bodyDrain(rx); }
    template <typename Rx>
    void tunnelData(Rx &rx) {
      Zhttp::bodyEach(rx,
	[this](ZuBSpan value) { tunnelBody << value; });
    }
    void tunnelEnd() { remoteEnded = true; }
    void tunnelReset() { ++tunnelResets; }
    void complete(Zhttp::H3::ParserState::T state) { complete_ = state; }

    ZtString<>			path;
    ZtString<>			protocol_;
    ZtString<>			tunnelBody;
    Zhttp::Method::T		method = -1;
    Zhttp::H3::ParserState::T	complete_ =
      Zhttp::H3::ParserState::Initial;
    unsigned			tunnelEstablished = 0;
    unsigned			tunnelResets = 0;
    bool			remoteEnded = false;
  } parser;

  template <typename Link>
  void connected(Link &link) {
    parser.extendedConnect(Zhttp::Tunnel{link}.localCap());
  }
  template <typename Link>
  void disconnected(Link &, bool) { }
  template <typename Link>
  int process(Link &link, Zquic::RxStream &rx) {
    auto state = link.receive(parser, rx);
    switch (state) {
      case Zhttp::H3::ParserState::Tunnel:
      case Zhttp::H3::ParserState::RemoteClosed:
	if (!responseSent) {
	  responseSent = true;
	  TunnelResponseBuilder builder;
	  auto tx = link.transmit(builder);
	  builder.response(tx);
	}
	if (parser.tunnelBody) {
	  Zhttp::Tunnel{link}.send([this](auto &body) {
	    body << parser.tunnelBody;
	  });
	  parser.tunnelBody.null();
	}
	if (parser.remoteEnded) Zhttp::Tunnel{link}.end();
	return 0;
      case Zhttp::H3::ParserState::Cancelled:
	if (parser.tunnelResets == 1)
	  link.app()->state->reset.post();
	else
	  ++link.app()->state->errors;
	return 0;
      case Zhttp::H3::ParserState::Complete: {
	if (parser.method != Zhttp::Method::GET || parser.path != "/")
	  ++link.app()->state->errors;
	ResponseBuilder builder;
	auto tx = link.transmit(builder);
	builder.response(tx);
	auto body = builder.body(tx);
	body << ZuCSpan{"pong"};
	body.flush();
	link.finish();
	return 0;
      }
      case Zhttp::H3::ParserState::Error:
	++link.app()->state->errors;
	return -1;
      default:
	return 0;
    }
  }

  bool	responseSent = false;
};

struct TunnelServer :
  public Zhttp::Server<TunnelServer, Zhttp::H3QUIC> {
  using Link = TunnelServerLink;

  TunnelState	*state = nullptr;

  TunnelServer(TunnelState *state_) : state{state_} { }

  ZiIP localIP() const { return ZiIP{"127.0.0.1"}; }
  unsigned localPort() const { return state->port; }
  void listening() { state->listening.post(); }
  void listenFailed(bool) {
    ++state->errors;
    state->listening.post();
  }
  bool admit(const Zhttp::ConnectedInfo &) {
    ++state->admissions;
    return true;
  }
  void release() { ++state->releases; }
  void disconnected(Link &, bool) { }
};

struct TunnelServerLink :
  public Zhttp::ServerLink<
    TunnelServer, TunnelServerLink, Zhttp::H3QUIC,
    TunnelServerSession> {
  using Base = Zhttp::ServerLink<
    TunnelServer, TunnelServerLink, Zhttp::H3QUIC,
    TunnelServerSession>;
  using Base::Base;
};

void runTunnel(const Zhttp::Test::TempDir &temp)
{
  ZuTestScope(runTunnel);

  TunnelState state;
  state.port = Zhttp::Test::loopbackPort();
  ZuCHECK(state.port, "H3 tunnel port allocation failed");
  if (!state.port) return;

  ZiMultiplex mx{ZhttpH1EngineTest_::mxParams()};
  ZuCHECK(mx.start(), "H3 tunnel multiplexer start failed");
  if (!mx.running()) return;

  Zhttp::EngineConfig engine{&mx, "3", "4"};
  auto serverConfig = Zhttp::QUICConfig{}
    .certPath(temp.certPath.cspan()).keyPath(temp.keyPath.cspan())
    .maxStreamsDuplex(2)
    .extendedConnect(true);
  auto clientConfig = Zhttp::QUICConfig{}
    .caPath(temp.certPath.cspan()).extendedConnect(true);
  TunnelServer server{&state};
  TunnelClient client{&state};
  bool initialized =
    server.init(engine, serverConfig) && client.init(engine, clientConfig);
  ZuCHECK(initialized, "H3 tunnel engines initialized");
  if (!initialized) {
    client.final();
    server.final();
    mx.stop();
    return;
  }
  bool started = server.start() && client.start();
  ZuCHECK(started, "H3 tunnel engines started");
  bool listening = state.listening.timedwait(Zm::now(10)) == 0;
  ZuCHECK(listening, "H3 tunnel server listening");

  ZmRef<TunnelClientLink> tunnel = new TunnelClientLink{&client};
  ZmRef<TunnelClientLink> rest = new TunnelClientLink{&client};
  ZmRef<TunnelClientLink> queued = new TunnelClientLink{&client};
  tunnel->mode = TunnelClientLink::Echo;
  if (listening) {
    tunnel->connect("127.0.0.1", state.port);
    rest->connect("127.0.0.1", state.port);
    queued->connect("127.0.0.1", state.port);
  }
  bool first =
    state.response.timedwait(Zm::now(10)) == 0 &&
    state.response.timedwait(Zm::now(10)) == 0 &&
    state.response.timedwait(Zm::now(10)) == 0;
  ZuCHECK(first && !state.errors && state.admissions == 1,
    "H3 stream-credit queue preserves one shared QUIC session");

  ZmRef<TunnelClientLink> reset = new TunnelClientLink{&client};
  reset->mode = TunnelClientLink::Reset;
  if (first) reset->connect("127.0.0.1", state.port);
  bool resetSeen = state.reset.timedwait(Zm::now(10)) == 0;
  ZuCHECK(resetSeen && !state.errors && state.admissions == 1,
    "H3 tunnel reset preserved its shared QUIC session");
  reset->disconnect();

  ZmRef<TunnelClientLink> after = new TunnelClientLink{&client};
  if (resetSeen) after->connect("127.0.0.1", state.port);
  bool survived = state.response.timedwait(Zm::now(10)) == 0;
  ZuCHECK(survived && !state.errors && state.admissions == 1,
    "H3 request succeeded after tunnel reset on shared session");

  client.stop([&state](bool ok) {
    if (!ok) ++state.errors;
    state.stopped.post();
  });
  server.stop([&state](bool ok) {
    if (!ok || state.releases != 1) ++state.errors;
    state.stopped.post();
  });
  bool stopped =
    state.stopped.timedwait(Zm::now(10)) == 0 &&
    state.stopped.timedwait(Zm::now(10)) == 0;
  ZuCHECK(stopped && !state.errors && state.releases == 1,
    "H3 tunnel engines drained before stop completion");

  tunnel = nullptr;
  rest = nullptr;
  queued = nullptr;
  reset = nullptr;
  after = nullptr;
  client.final();
  server.final();
  mx.stop();
}

} // namespace ZhttpH3EngineTest_

int main(int argc, char **argv)
{
  using namespace ZhttpH1EngineTest_;

  parse(argc, argv);
  ZiLog::init("ZhttpH3EngineTest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  TempDir temp;
  ZuTestMain();
  ZuCHECK(temp.init(), "temporary certificate creation failed");
  if (temp.certPath && temp.keyPath) {
    ZuTestCall(run<Zhttp::H3QUIC>, temp, 1U, 2U);
    ZuTestCall(run<Zhttp::H3QUIC>, temp, 1U, 2U);
    ZuTestCall(run<Zhttp::H3QUIC>, temp, 1U, 2U, true);
    ZuTestCall(runServerStop<Zhttp::H3QUIC>, temp);
    ZuTestCall(ZhttpH3EngineTest_::runTunnel, temp);
  }

  ZiLog::stop();
  return 0;
}
