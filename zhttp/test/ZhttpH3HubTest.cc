//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include "ZhttpHubFixture.hh"

namespace ZhttpH3HubTest_ {

template <typename Link>
void streamContract(Link &link)
{
  Zhttp::Stream stream{link};
  (void)stream.peerCap();
  (void)stream.localCap();
  stream.txStream([](auto &tx) {
    tx << ZuCSpan{"x"};
    tx.flush();
  });
  stream.end();
  stream.reset();
}

template void streamContract<
  ZhttpH1HubTest_::Client<Zhttp::H3QUIC>::Link>(
    ZhttpH1HubTest_::Client<Zhttp::H3QUIC>::Link &);
template void streamContract<
  ZhttpH1HubTest_::ServerLink<Zhttp::H3QUIC>>(
    ZhttpH1HubTest_::ServerLink<Zhttp::H3QUIC> &);

struct StreamState {
  ZmSemaphore		listening;
  ZmSemaphore		response;
  ZmSemaphore		cleanEnd;
  ZmSemaphore		reset;
  ZmSemaphore		malformed;
  ZmSemaphore		stopped;
  ZmAtomic<unsigned>	errors = 0;
  ZmAtomic<unsigned>	admissions = 0;
  ZmAtomic<unsigned>	cleanEnds = 0;
  ZmAtomic<unsigned>	malformedErrors = 0;
  ZmAtomic<unsigned>	releases = 0;
  uint16_t		port = 0;
};

struct RequestBuilder :
  public Zhttp::Builder,
  public Zhttp::H3::Request<RequestBuilder> {
  using Headers = ZuTypeList<>;
  template <typename L>
  void operation(L &&l) {
    l(Zhttp::Method::GET, [](auto &&emit) {
      emit([](auto &tx) { tx << '/'; });
    });
  }
  template <typename L>
  void host(L &&l) { l("127.0.0.1"); }
};

struct StreamRequestBuilder :
  public Zhttp::Builder,
  public Zhttp::H3::Request<StreamRequestBuilder> {
  using Headers = ZuTypeList<>;
  template <typename L>
  void operation(L &&l) {
    l(Zhttp::Method::CONNECT, [](auto &&emit) {
      emit([](auto &tx) { tx << "/stream"; });
    });
  }
  template <typename L>
  void host(L &&l) { l("127.0.0.1"); }
  template <typename L>
  void protocol(L &&l) { l("opaque"); }
};

struct ResponseBuilder :
  public Zhttp::Builder,
  public Zhttp::H3::Response<
    ResponseBuilder, ZhttpHeaders("content-length"), true> {
  using Headers = ZhttpHeaders("content-length");
  using Base = Zhttp::H3::Response<ResponseBuilder, Headers, true>;
  using Base::body;
  unsigned status() const { return 200; }
  template <typename Key, typename L>
  void header(L &&l) const {
    if constexpr (Key{}() == "content-length") l("4");
  }
  template <typename L> void header(L &&) const { }
};

struct StreamResponseBuilder :
  public Zhttp::Builder,
  public Zhttp::H3::Response<StreamResponseBuilder> {
  using Headers = ZuTypeList<>;
  using Base = Zhttp::H3::Response<StreamResponseBuilder>;
  using Base::body;
  unsigned status() const { return 200; }
};

struct StreamClient;
struct StreamClientLink;
struct ClientParser;

struct ClientStream {
  template <typename Stream, typename Rx>
  int process(Stream stream, Rx &rx);

  ClientParser	*parser = nullptr;
};

struct ClientParser :
  public Zhttp::Parser,
  public Zhttp::H3::Parser<ClientParser, false> {
  using Base = Zhttp::H3::Parser<ClientParser, false>;
  using Headers = ZuTypeList<>;

  ClientParser() : consumer{this} { }

  void bind(StreamClientLink &link) { dispatch.init(link, consumer); }
  void final() {
    dispatch.disable_();
    dispatch.final_();
  }
  bool operation(Zhttp::Method::T, Zhttp::Target &) { return true; }
  void status(unsigned value) { status_ = value; }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t>) { }
  bool bodyInfo(Zhttp::BodyType::T type, uint64_t length_) {
    if (type == Zhttp::BodyType::Fixed) length = length_;
    if (streamExpected && status_ >= 200 && status_ < 300 &&
	type != Zhttp::BodyType::None) {
      Base::stream();
      ++streamEstablished;
      ++streamStarts;
    }
    return true;
  }
  template <typename Rx>
  bool body(Rx &rx) {
    return Zhttp::bodyEach(
      rx, [this](ZuSpan<uint8_t> value) { body_ << value; });
  }
  template <typename Rx>
  void streamRx_(Rx &rx) { dispatch.process(rx); }
  void streamPeerEnd_() { ++streamEnds; dispatch.peerEnd(); }
  void streamError_() { ++streamResets; dispatch.error(); }
  template <typename Rx>
  void processStream(Rx &rx) {
    while (rx) {
      const uint8_t *offered = nullptr;
      int64_t n = rx.consume(
	  [&offered](ZuSpan<uint8_t> span) -> int64_t {
	    offered = span.data();
	    return 1;
	  },
	  [this, &offered](ZuSpan<uint8_t> span) {
	    streamNoCopy &= span.data() == offered;
	    streamBody << span;
	  });
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

template <typename Stream, typename Rx>
int ClientStream::process(Stream, Rx &rx)
{
  parser->processStream(rx);
  return 1;
}

struct StreamClient :
  public Zhttp::ClientHub<StreamClient, Zhttp::H3QUIC> {
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

  struct Mode { enum { REST, Echo, Reset, Malformed }; };

  ClientParser	parser;
  unsigned	id = 0;
  int8_t	mode = Mode::REST;
  bool		sent = false;
};

void StreamClient::disconnected(Link &link, bool)
{
  link.parser.final();
  if (link.mode == StreamClientLink::Mode::Echo) {
    ++state->cleanEnds;
    state->cleanEnd.post();
  } else if (link.mode == StreamClientLink::Mode::Malformed) {
    state->malformed.post();
  }
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
  if (link.mode == StreamClientLink::Mode::Malformed) {
    static constexpr uint8_t dataBeforeHeaders[] = {0, 0};
    auto tx = link.txStream();
    tx << dataBeforeHeaders << Zi::flush();
    link.finish();
    return;
  }
  if (link.mode == StreamClientLink::Mode::REST) {
    RequestBuilder builder;
    auto tx = link.transmit(builder);
    if (!builder.begin(tx)) {
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
  if (!builder.begin(tx)) {
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
      Zhttp::Stream{link}.txStream([](auto &body) {
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
  template <typename Stream, typename Rx>
  int process(Stream stream, Rx &rx);

  ServerParser	*parser = nullptr;
};

struct ServerParser :
  public Zhttp::Parser,
  public Zhttp::H3::Parser<ServerParser, true> {
  using Base = Zhttp::H3::Parser<ServerParser, true>;
  using Headers = ZuTypeList<>;

  ServerParser() : consumer{this} { }

  void bind(StreamServerLink &link) { dispatch.init(link, consumer); }
  void final() {
    dispatch.disable_();
    dispatch.final_();
  }
  bool operation(
    Zhttp::Method::T method_, Zhttp::Target &target) {
    method = method_;
    path = target.path;
    protocol_ = target.protocol;
    if (protocol_) {
      Base::stream();
      ++streamEstablished;
      ++streamStarts;
    }
    return true;
  }
  bool bodyInfo(Zhttp::BodyType::T, uint64_t) { return true; }
  void status(unsigned) { }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t>) { }
  template <typename Rx>
  bool body(Rx &rx) { return Zhttp::bodyDrain(rx); }
  template <typename Rx>
  void streamRx_(Rx &rx) { dispatch.process(rx); }
  void streamPeerEnd_() { remoteEnded = true; dispatch.peerEnd(); }
  void streamError_() { ++streamResets; dispatch.error(); }
  template <typename Rx>
  void processStream(Rx &rx) {
    while (rx) {
      int64_t n = rx.consume(
	[](ZuSpan<uint8_t> span) -> int64_t { return span.length(); },
	[this](ZuSpan<uint8_t> value) { streamBody << value; });
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

template <typename Stream, typename Rx>
int ServerStream::process(Stream, Rx &rx)
{
  parser->processStream(rx);
  return 1;
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
	  builder.begin(tx);
	}
	if (parser.streamBody) {
	  Zhttp::Stream{link}.txStream([this](auto &body) {
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
	builder.begin(tx);
	auto body = builder.body(tx);
	body << ZuCSpan{"pong"};
	body.flush();
	link.finish();
	return 0;
      }
      case Zhttp::H3::ParserState::Error:
	link.app()->state->malformedErrors = 1;
	return -1;
      default:
	return 0;
    }
  }

  bool	responseSent = false;
};

struct StreamServer :
  public Zhttp::ProtocolServer<StreamServer, Zhttp::H3QUIC> {
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

  ZiMultiplex mx{ZhttpH1HubTest_::mxParams()};
  ZuCHECK(mx.start(), "H3 stream multiplexer start failed");
  if (!mx.running()) return;

  Zhttp::HubConfig hub{&mx, "3", "4"};
  auto serverConfig = Zhttp::QUICConfig{}
    .certPath(temp.certPath.cspan()).keyPath(temp.keyPath.cspan())
    .maxStreamsDuplex(2)
    .extendedConnect(true);
  auto clientConfig = Zhttp::QUICConfig{}
    .caPath(temp.certPath.cspan()).extendedConnect(true);
  StreamServer server{&state};
  StreamClient client{&state};
  bool initialized =
    server.init(hub, serverConfig) && client.init(hub, clientConfig);
  ZuCHECK(initialized, "H3 stream hubs initialized");
  if (!initialized) {
    client.final();
    server.final();
    mx.stop();
    return;
  }
  bool started = server.start() && client.start();
  ZuCHECK(started, "H3 stream hubs started");
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
  bool cleanEnd = state.cleanEnd.timedwait(Zm::now(10)) == 0;
  ZuCHECK(first && cleanEnd && state.cleanEnds == 1 &&
      !state.errors && state.admissions == 1,
    "H3 stream-credit queue preserves one shared QUIC session");

  ZmRef<StreamClientLink> reset = new StreamClientLink{&client};
  reset->mode = StreamClientLink::Mode::Reset;
  if (first) reset->connect("127.0.0.1", state.port);
  bool resetSeen = state.reset.timedwait(Zm::now(10)) == 0;
  ZuCHECK(resetSeen && !state.errors && state.admissions == 1,
    "H3 stream reset preserved its shared QUIC session");
  reset->disconnect();

  ZmRef<StreamClientLink> malformed = new StreamClientLink{&client};
  malformed->mode = StreamClientLink::Mode::Malformed;
  if (resetSeen) malformed->connect("127.0.0.1", state.port);
  bool malformedSeen = state.malformed.timedwait(Zm::now(10)) == 0;
  ZuCHECK(malformedSeen && state.malformedErrors == 1 && !state.errors &&
      state.admissions == 1,
    "malformed H3 request reset only its stream (seen=", malformedSeen,
    ", parserErrors=", state.malformedErrors.load_(),
    ", errors=", state.errors.load_(),
    ", admissions=", state.admissions.load_(), ')');

  ZmRef<StreamClientLink> after = new StreamClientLink{&client};
  if (malformedSeen) after->connect("127.0.0.1", state.port);
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
    "H3 stream hubs drained before stop completion");

  stream = nullptr;
  rest = nullptr;
  queued = nullptr;
  reset = nullptr;
  after = nullptr;
  client.final();
  server.final();
  mx.stop();
}

} // namespace ZhttpH3HubTest_

int main(int argc, char **argv)
{
  using namespace ZhttpH1HubTest_;

  parse(argc, argv);
  ZiLog::init("ZhttpH3HubTest");
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
    ZuTestCall(ZhttpH3HubTest_::runStream, temp);
  }

  ZiLog::stop();
  return 0;
}
