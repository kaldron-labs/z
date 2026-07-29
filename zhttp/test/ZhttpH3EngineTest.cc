//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include "ZhttpEngineFixture.hh"

namespace ZhttpH3EngineTest_ {

template <typename Link>
void streamContract(Link &link)
{
  Zhttp::Stream stream{link};
  (void)stream.peerCap();
  (void)stream.localCap();
  stream.tx([](auto &tx) {
    tx << ZuCSpan{"x"};
    tx.flush();
  });
  stream.end();
  stream.reset();
}

template void streamContract<
  ZhttpH1EngineTest_::Client<Zhttp::H3QUIC>::Link>(
    ZhttpH1EngineTest_::Client<Zhttp::H3QUIC>::Link &);
template void streamContract<
  ZhttpH1EngineTest_::ServerLink<Zhttp::H3QUIC>>(
    ZhttpH1EngineTest_::ServerLink<Zhttp::H3QUIC> &);

struct StreamState {
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

struct StreamRequestBuilder :
  public Zhttp::H3::Builder<StreamRequestBuilder> {
  template <typename L>
  void operation(L &&l) {
    l(Zhttp::Method::CONNECT, "/stream", "");
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

struct StreamResponseBuilder :
  public Zhttp::H3::Builder<StreamResponseBuilder> {
  unsigned status() const { return 200; }
};

struct StreamClient;
struct StreamClientLink;
struct ClientParser;

struct ClientStream {
  template <typename Stream>
  void streamProcess(Stream stream);

  ClientParser	*parser = nullptr;
};

struct ClientParser :
  public Zhttp::H3::Parser<ClientParser, false> {
  using Base = Zhttp::H3::Parser<ClientParser, false>;

  ClientParser() : consumer{this} { }

  void bind(StreamClientLink &link) { dispatch.init(link, consumer); }
  void final() {
    dispatch.disable_();
    dispatch.final_();
  }
  void operation(Zhttp::Method::T, ZuBSpan) { }
  void status(unsigned value) { status_ = value; }
  void contentLength(uint64_t value) { length = value; }
  template <typename Key> void header(ZuBSpan) { }
  void headers(Zhttp::Fields::Section section, bool) {
    if (streamExpected && section == Zhttp::Fields::Final &&
	status_ >= 200 && status_ < 300) {
      Base::stream();
      ++streamEstablished;
    }
  }
  template <typename Rx>
  void body(Rx &rx) {
    Zhttp::bodyEach(rx, [this](ZuBSpan value) { body_ << value; });
  }
  template <typename Rx>
  void streamProcess(Rx &rx) { dispatch.process(rx); }
  void events_(Zi::RxEvent::T events) {
    if (events & Zi::RxEvent::Start()) ++streamStarts;
    if (events & Zi::RxEvent::Final()) ++streamEnds;
    if (events & Zi::RxEvent::Error()) ++streamResets;
    if (events & (Zi::RxEvent::Final() | Zi::RxEvent::Error()))
      dispatch.disable_();
  }
  template <typename Rx>
  void processStream(Rx &rx) {
    for (;;) {
      bool input = rx.input();
      events_(rx.events());
      if (!input) break;
      const uint8_t *offered = nullptr;
      int64_t n = rx.consume(
	  [&offered](ZuBSpan span) -> int64_t {
	    offered = span.data();
	    return 1;
	  },
	  [this, &offered](ZuBSpan span) {
	    streamNoCopy &= span.data() == offered;
	    streamBody << ZuCSpan{span};
	  });
      events_(rx.events());
      if (n <= 0) break;
    }
  }
  void complete(Zhttp::H3::ParserState::T state) { complete_ = state; }

  ZtString<>			body_;
  ZtString<>			streamBody;
  Zhttp::StreamDispatch<StreamClientLink, ClientStream> dispatch;
  ClientStream			consumer;
  uint64_t			length = 0;
  unsigned			status_ = 0;
  unsigned			streamEstablished = 0;
  unsigned			streamStarts = 0;
  unsigned			streamEnds = 0;
  unsigned			streamResets = 0;
  bool				streamExpected = false;
  bool				streamNoCopy = true;
  Zhttp::H3::ParserState::T	complete_ =
    Zhttp::H3::ParserState::Initial;
};

template <typename Stream>
void ClientStream::streamProcess(Stream stream)
{
  parser->processStream(stream.rx());
}

struct StreamClient :
  public Zhttp::Client<StreamClient, Zhttp::H3QUIC> {
  using Link = StreamClientLink;

  StreamState	*state = nullptr;

  StreamClient(StreamState *state_) : state{state_} { }

  void connected(Link &link, Zhttp::ConnectedInfo info);
  void disconnected(Link &link, bool);
  void connectFailed(Link &, bool) {
    ++state->errors;
    state->response.post();
  }
  int process(Link &link, Zquic::RxStream &rx);
};

struct StreamClientLink :
  public Zhttp::ClientLink<
    StreamClient, StreamClientLink, Zhttp::H3QUIC> {
  using Base = Zhttp::ClientLink<
    StreamClient, StreamClientLink, Zhttp::H3QUIC>;
  using Base::Base;

  struct Mode { enum { REST, Echo, Reset }; };

  ClientParser	parser;
  int8_t	mode = Mode::REST;
  bool		sent = false;
};

void StreamClient::disconnected(Link &link, bool)
{
  link.parser.final();
}

void StreamClient::connected(
  StreamClientLink &link, Zhttp::ConnectedInfo info)
{
  if (info.httpVersion != Zhttp::Version::H3 ||
      info.transport != Zhttp::Transport::QUIC ||
      info.alpn != "h3") {
    ++state->errors;
    state->response.post();
    return;
  }
  link.parser.bind(link);
  if (link.mode == StreamClientLink::Mode::REST) {
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
  link.parser.streamExpected = true;
  link.parser.requestMethod(Zhttp::Method::CONNECT);
  StreamRequestBuilder builder;
  auto tx = link.transmit(builder);
  if (!builder.request(tx)) {
    ++state->errors;
    state->response.post();
  }
}

int StreamClient::process(
  StreamClientLink &link, Zquic::RxStream &rx)
{
  auto state_ = link.receive(link.parser, rx);
  switch (state_) {
    case Zhttp::H3::ParserState::Stream:
      if (link.sent) return 0;
      link.sent = true;
      if (link.mode == StreamClientLink::Mode::Reset) {
	Zhttp::Stream{link}.reset();
	return 0;
      }
      Zhttp::Stream{link}.tx([](auto &body) {
	body << ZuCSpan{"ping"};
	body.flush();
      });
      Zhttp::Stream{link}.end();
      return 0;
    case Zhttp::H3::ParserState::RemoteClosed:
      if (link.mode != StreamClientLink::Mode::Echo ||
	  link.parser.streamEstablished != 1 ||
	  link.parser.streamStarts != 1 ||
	  link.parser.streamBody != "ping" ||
	  !link.parser.streamNoCopy ||
	  link.parser.streamEnds != 1 ||
	  link.parser.streamResets)
	++state->errors;
      state->response.post();
      link.disconnect();
      return 0;
    case Zhttp::H3::ParserState::Complete:
      if (link.mode != StreamClientLink::Mode::REST ||
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

struct StreamServer;
struct StreamServerLink;
struct ServerParser;

struct ServerStream {
  template <typename Stream>
  void streamProcess(Stream stream);

  ServerParser	*parser = nullptr;
};

struct ServerParser : public Zhttp::H3::Parser<ServerParser, true> {
  using Base = Zhttp::H3::Parser<ServerParser, true>;

  ServerParser() : consumer{this} { }

  void bind(StreamServerLink &link) { dispatch.init(link, consumer); }
  void final() {
    dispatch.disable_();
    dispatch.final_();
  }
  void operation(Zhttp::Method::T method_, ZuBSpan path_) {
    method = method_;
    path = path_;
  }
  void protocol(ZuBSpan value) {
    protocol_ = value;
    Base::stream();
    ++streamEstablished;
  }
  void headers(Zhttp::Fields::Section, bool) { }
  void contentLength(uint64_t) { }
  void status(unsigned) { }
  template <typename Key> void header(ZuBSpan) { }
  template <typename Rx>
  void body(Rx &rx) { Zhttp::bodyDrain(rx); }
  template <typename Rx>
  void streamProcess(Rx &rx) { dispatch.process(rx); }
  void events_(Zi::RxEvent::T events) {
    if (events & Zi::RxEvent::Start()) ++streamStarts;
    if (events & Zi::RxEvent::Final()) remoteEnded = true;
    if (events & Zi::RxEvent::Error()) ++streamResets;
    if (events & (Zi::RxEvent::Final() | Zi::RxEvent::Error()))
      dispatch.disable_();
  }
  template <typename Rx>
  void processStream(Rx &rx) {
    for (;;) {
      bool input = rx.input();
      events_(rx.events());
      if (!input) break;
      int64_t n = rx.consume(
	[](ZuBSpan span) -> int64_t { return span.length(); },
	[this](ZuBSpan value) { streamBody << value; });
      events_(rx.events());
      if (n <= 0) break;
    }
  }
  void complete(Zhttp::H3::ParserState::T state) { complete_ = state; }

  ZtString<>			path;
  ZtString<>			protocol_;
  ZtString<>			streamBody;
  Zhttp::StreamDispatch<StreamServerLink, ServerStream> dispatch;
  ServerStream			consumer;
  Zhttp::Method::T		method = -1;
  Zhttp::H3::ParserState::T	complete_ =
    Zhttp::H3::ParserState::Initial;
  unsigned			streamEstablished = 0;
  unsigned			streamStarts = 0;
  unsigned			streamResets = 0;
  bool				remoteEnded = false;
};

template <typename Stream>
void ServerStream::streamProcess(Stream stream)
{
  parser->processStream(stream.rx());
}

struct StreamServerSession {
  ServerParser parser;

  template <typename Link>
  void connected(Link &link) {
    parser.bind(link);
    parser.extendedConnect(Zhttp::Stream{link}.localCap());
  }
  template <typename Link>
  void disconnected(Link &, bool) { parser.final(); }
  template <typename Link>
  int process(Link &link, Zquic::RxStream &rx) {
    auto state = link.receive(parser, rx);
    switch (state) {
      case Zhttp::H3::ParserState::Stream:
      case Zhttp::H3::ParserState::RemoteClosed:
	if (!responseSent) {
	  responseSent = true;
	  StreamResponseBuilder builder;
	  auto tx = link.transmit(builder);
	  builder.response(tx);
	}
	if (parser.streamBody) {
	  Zhttp::Stream{link}.tx([this](auto &body) {
	    body << parser.streamBody;
	    body.flush();
	  });
	  parser.streamBody.null();
	}
	if (parser.remoteEnded) Zhttp::Stream{link}.end();
	return 0;
      case Zhttp::H3::ParserState::Cancelled:
	if (parser.streamResets == 1)
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

struct StreamServer :
  public Zhttp::Server<StreamServer, Zhttp::H3QUIC> {
  using Link = StreamServerLink;

  StreamState	*state = nullptr;

  StreamServer(StreamState *state_) : state{state_} { }

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

struct StreamServerLink :
  public Zhttp::ServerLink<
    StreamServer, StreamServerLink, Zhttp::H3QUIC,
    StreamServerSession> {
  using Base = Zhttp::ServerLink<
    StreamServer, StreamServerLink, Zhttp::H3QUIC,
    StreamServerSession>;
  using Base::Base;
};

void runStream(const Zhttp::Test::TempDir &temp)
{
  ZuTestScope(runStream);

  StreamState state;
  state.port = Zhttp::Test::loopbackPort();
  ZuCHECK(state.port, "H3 stream port allocation failed");
  if (!state.port) return;

  ZiMultiplex mx{ZhttpH1EngineTest_::mxParams()};
  ZuCHECK(mx.start(), "H3 stream multiplexer start failed");
  if (!mx.running()) return;

  Zhttp::EngineConfig engine{&mx, "3", "4"};
  auto serverConfig = Zhttp::QUICConfig{}
    .certPath(temp.certPath.cspan()).keyPath(temp.keyPath.cspan())
    .maxStreamsDuplex(2)
    .extendedConnect(true);
  auto clientConfig = Zhttp::QUICConfig{}
    .caPath(temp.certPath.cspan()).extendedConnect(true);
  StreamServer server{&state};
  StreamClient client{&state};
  bool initialized =
    server.init(engine, serverConfig) && client.init(engine, clientConfig);
  ZuCHECK(initialized, "H3 stream engines initialized");
  if (!initialized) {
    client.final();
    server.final();
    mx.stop();
    return;
  }
  bool started = server.start() && client.start();
  ZuCHECK(started, "H3 stream engines started");
  bool listening = state.listening.timedwait(Zm::now(10)) == 0;
  ZuCHECK(listening, "H3 stream server listening");

  ZmRef<StreamClientLink> stream = new StreamClientLink{&client};
  ZmRef<StreamClientLink> rest = new StreamClientLink{&client};
  ZmRef<StreamClientLink> queued = new StreamClientLink{&client};
  stream->mode = StreamClientLink::Mode::Echo;
  if (listening) {
    stream->connect("127.0.0.1", state.port);
    rest->connect("127.0.0.1", state.port);
    queued->connect("127.0.0.1", state.port);
  }
  bool first =
    state.response.timedwait(Zm::now(10)) == 0 &&
    state.response.timedwait(Zm::now(10)) == 0 &&
    state.response.timedwait(Zm::now(10)) == 0;
  ZuCHECK(first && !state.errors && state.admissions == 1,
    "H3 stream-credit queue preserves one shared QUIC session");

  ZmRef<StreamClientLink> reset = new StreamClientLink{&client};
  reset->mode = StreamClientLink::Mode::Reset;
  if (first) reset->connect("127.0.0.1", state.port);
  bool resetSeen = state.reset.timedwait(Zm::now(10)) == 0;
  ZuCHECK(resetSeen && !state.errors && state.admissions == 1,
    "H3 stream reset preserved its shared QUIC session");
  reset->disconnect();

  ZmRef<StreamClientLink> after = new StreamClientLink{&client};
  if (resetSeen) after->connect("127.0.0.1", state.port);
  bool survived = state.response.timedwait(Zm::now(10)) == 0;
  ZuCHECK(survived && !state.errors && state.admissions == 1,
    "H3 request succeeded after stream reset on shared session");

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
    "H3 stream engines drained before stop completion");

  stream = nullptr;
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
    ZuTestCall(ZhttpH3EngineTest_::runStream, temp);
  }

  ZiLog::stop();
  return 0;
}
