//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZiMultiplex.hh>

#include <zlib/ZhttpClient.hh>
#include <zlib/ZhttpServer.hh>

#include "ZhttpTestUtil.hh"

using namespace ZuTestUtil;

namespace ZhttpH2HubTest_ {

struct State {
  ZmSemaphore	listening;
  ZmSemaphore	response;
  ZmSemaphore	goaway;
  ZmSemaphore	stopped;
  ZmAtomic<unsigned> errors = 0;
  ZmAtomic<unsigned> closed = 0;
  ZmAtomic<unsigned> admissions = 0;
  ZmAtomic<unsigned> releases = 0;
  unsigned	clientNative = 0;
  unsigned	maxClientNative = 0;
  uint16_t	port = 0;
};

struct FlowLogical : public ZmObject { };

struct FlowApp {
  enum { Rx, Tx };

  ZiMultiplex *mx() { return &multiplex; }
  unsigned rxThread() const { return 0; }
  bool rxInvoked() const { return shard == Rx; }
  bool txInvoked() const { return shard == Tx; }
  template <typename L> void rxRun(L &&l) { run(Rx, ZuFwd<L>(l)); }
  template <typename L> void txRun(L &&l) { run(Tx, ZuFwd<L>(l)); }
  template <typename L> void runTx(L &&l) { run(Tx, ZuFwd<L>(l)); }

private:
  template <typename L> void run(int shard_, L &&l) {
    int prev = shard;
    shard = shard_;
    ZuFwd<L>(l)();
    shard = prev;
  }

public:

  ZiMultiplex multiplex;
  int shard = Rx;
};

struct FlowWire : public Zhttp::H2_::Wire<FlowWire, FlowLogical> {
  using BufAlloc = Zi::IOBufAlloc<
    Zhttp::Transport_::TxBufNode, 65544, 65544,
    ZuStringT<"Zhttp.H2.FlowTest">>;

  struct Tx : public ZiTxStream<Tx> {
    using Base = ZiTxStream<Tx>;

    Tx(FlowWire *wire_) : Base{65544, 0, 0}, wire{wire_} { }

    ZmRef<ZiIOBuf> allocBuf_(unsigned headRoom) {
      ZmRef<ZiIOBuf> buf = new BufAlloc{};
      buf->skip = headRoom;
      buf->length = 0;
      return buf;
    }
    bool sendBuf_(ZmRef<ZiIOBuf> buf, bool) {
      if (!buf) return false;
      ++wire->sends;
      return true;
    }

    FlowWire *wire;
  };

  FlowWire(FlowApp *app_) : app_{app_} { }

  FlowApp *app() const { return app_; }
  Tx txStream() { return Tx{this}; }
  Tx directTxStream() { return Tx{this}; }
  void h2CapacityTx_(bool value) {
    capacity = value;
    ++capacityCalls;
    if (value) ++saturatedCalls;
  }
  void h2LocalEnd(uint32_t) { }
  void h2Cancel(uint32_t) { }
  bool h2Closed(uint32_t id) const { return streamClosed(id); }
  void h2ResetLogical(uint32_t, Zhttp::H2::Error::T) { }
  void disconnect_(bool) { }
  void disconnectNative() {
    ++disconnects;
    disconnectShard = app_->shard;
  }

  FlowApp	*app_;
  unsigned	sends = 0;
  unsigned	capacityCalls = 0;
  unsigned	saturatedCalls = 0;
  unsigned	disconnects = 0;
  int		disconnectShard = -1;
  bool		capacity = false;
};

ZmRef<ZiIOBuf> flowData(unsigned length)
{
  ZmRef<ZiIOBuf> buf = new FlowWire::BufAlloc{};
  buf->length = length + Zhttp::H2::FrameHeaderSize;
  uint8_t *data = buf->data();
  data[0] = uint8_t(length>>16);
  data[1] = uint8_t(length>>8);
  data[2] = uint8_t(length);
  data[3] = Zhttp::H2::FrameType::Data;
  data[4] = 0;
  data[5] = data[6] = data[7] = 0;
  data[8] = 1;
  return buf;
}

Zhttp::H2_::HeaderFrames flowHeaders()
{
  Zhttp::H2_::HeaderFrames frames;
  auto buf = flowData(0);
  buf->data()[3] = Zhttp::H2::FrameType::Headers;
  frames.push(ZuMv(buf));
  return frames;
}

void runFlowCapacity()
{
  ZuTestScope(runFlowCapacity);

  FlowApp app;
  FlowWire wire{&app};
  auto config = Zhttp::H2Config()
    .maxConcurrentStreams(2).maxQueuedFrames(16);
  ZuCHECK(wire.initWire(false, config), "initialize H2 flow harness");
  ZmRef<FlowLogical> first = new FlowLogical;
  ZmRef<FlowLogical> second = new FlowLogical;
  auto firstStream = wire.openLocalStream(first);
  auto secondStream = wire.openLocalStream(second);
  ZuCHECK(firstStream && secondStream,
    "open two independent H2 streams");

  ZuCHECK(wire.peerInitialWindow(0),
    "reduce the peer per-stream window to zero");
  wire.capacityCalls = wire.saturatedCalls = 0;
  wire.capacity = false;
  ZuCHECK(wire.sendData(firstStream->id, flowData(1)) &&
      !wire.capacity && !wire.saturatedCalls,
    "one blocked H2 stream does not saturate its connection");

  wire.h2WindowUpdate(firstStream->id, 1);
  wire.h2WindowUpdate(secondStream->id, 65535);
  wire.capacityCalls = wire.saturatedCalls = 0;
  wire.capacity = false;
  ZuCHECK(wire.sendData(secondStream->id, flowData(16384)) &&
      wire.sendData(secondStream->id, flowData(16384)) &&
      wire.sendData(secondStream->id, flowData(16384)) &&
      wire.sendData(secondStream->id, flowData(16382)),
    "consume the H2 connection window");
  ZuCHECK(wire.sendData(secondStream->id, flowData(1)) &&
      wire.capacity && wire.saturatedCalls == 1,
    "connection-level H2 flow control saturates the link (capacity=",
    wire.capacity, ", calls=", wire.capacityCalls,
    ", saturated=", wire.saturatedCalls, ", sends=", wire.sends, ')');

  wire.capacityCalls = wire.saturatedCalls = 0;
  wire.h2WindowUpdate(0, 1);
  ZuCHECK(!wire.capacity && wire.capacityCalls >= 1 &&
      !wire.saturatedCalls,
    "connection WINDOW_UPDATE restores H2 admission capacity (calls=",
    wire.capacityCalls, ')');

  bool cleared = false;
  wire.clearStreams([&cleared]() { cleared = true; });
  ZuCHECK(cleared, "clear H2 flow harness streams");
  wire.finalWire();
}

void runAdmissionErrors()
{
  ZuTestScope(runAdmissionErrors);

  FlowApp app;
  FlowWire wire{&app};
  auto config = Zhttp::H2Config()
    .maxConcurrentStreams(1).maxQueuedFrames(0);
  ZuCHECK(wire.initWire(false, config),
    "initialize strict-shard H2 admission harness");
  ZmRef<FlowLogical> logical = new FlowLogical;
  auto stream = wire.openLocalStream(logical);
  ZuCHECK(stream, "open strict-shard H2 stream");
  if (!stream) {
    wire.finalWire();
    return;
  }
  uint32_t id = stream->id;

  unsigned handled = 0;
  wire.logicalTxErrorFn(id, ZiTxErrorFn{
    [&app, &handled](ZeException &) {
      ZuCHECK(app.txInvoked(), "direct-Tx error callback runs on Tx shard");
      ++handled;
      return true;
    }});
  app.runTx([&wire, id]() {
    ZuCHECK(!wire.sendFrame(id, flowData(0)),
      "direct-Tx admission rejects a frame at zero capacity");
  });
  ZuCHECK(handled == 1 && !wire.disconnects,
    "handled direct-Tx admission error does not disconnect");

  unsigned replaced = 0;
  wire.logicalTxErrorFn(id, ZiTxErrorFn{
    [&app, &replaced](ZeException &) {
      ZuCHECK(app.txInvoked(), "replacement callback runs on Tx shard");
      ++replaced;
      return false;
    }});
  app.runTx([&wire, id]() {
    wire.sendHeaders(id, flowHeaders(), false);
  });
  ZuCHECK(replaced == 1 && wire.disconnects == 1 &&
      wire.disconnectShard == FlowApp::Tx,
    "unhandled direct-Tx header rejection disconnects on Tx shard");

  wire.logicalTxErrorFn(id, {});
  app.runTx([&wire, id]() {
    ZuCHECK(!wire.sendFrame(id, flowData(0)),
      "cleared callback leaves direct-Tx rejection unhandled");
  });
  ZuCHECK(replaced == 1 && wire.disconnects == 1,
    "callback clearing reaches the existing Tx window");

  unsigned rxHandled = 0;
  wire.logicalTxErrorFn(id, ZiTxErrorFn{
    [&app, &rxHandled](ZeException &) {
      ZuCHECK(app.rxInvoked(), "Rx admission callback runs on Rx shard");
      ++rxHandled;
      return true;
    }});
  ZuCHECK(!wire.sendFrame(id, flowData(0)),
    "Rx admission rejects a frame at zero capacity");
  ZuCHECK(rxHandled == 1 && wire.disconnects == 1,
    "Rx rejection uses the Rx-owned callback copy");

  bool cleared = false;
  wire.clearStreams([&cleared]() { cleared = true; });
  ZuCHECK(cleared, "clear strict-shard H2 admission harness");
  wire.finalWire();
}

struct RequestBuilder :
  public Zhttp::H2::Request<RequestBuilder> {
  template <typename L>
  void operation(L &&l) { l(Zhttp::Method::GET, "/"); }
  template <typename L>
  void host(L &&l) { l("127.0.0.1"); }
};

struct StreamRequestBuilder :
  public Zhttp::H2::Request<StreamRequestBuilder,
    ZuTypeList<>, ZuTypeList<>, true> {
  template <typename L>
  void operation(L &&l) { l(Zhttp::Method::CONNECT, "/stream"); }
  template <typename L>
  void host(L &&l) { l("127.0.0.1"); }
  template <typename L>
  void protocol(L &&l) { l("opaque"); }
};

struct ResponseBuilder :
  public Zhttp::H2::Response<
    ResponseBuilder, ZhttpHeaders("content-length"), ZuTypeList<>, true> {
  unsigned status() { return 200; }
  template <typename Key, typename L>
  void header(L &&l) {
    if constexpr (Key{}() == "content-length") l("4");
  }
};

struct StreamResponseBuilder :
  public Zhttp::H2::Response<StreamResponseBuilder> {
  unsigned status() { return 200; }
  bool streamResponse() { return true; }
};

struct Client;
struct ClientLink;
struct ClientParser;

struct ClientStream {
  template <typename Stream, typename Rx>
  int process(Stream stream, Rx &rx);

  ClientParser	*parser = nullptr;
};

struct ClientParser :
  public Zhttp::H2::Parser<ClientParser, false> {
  ClientParser() : consumer{this} { }

  void bind(ClientLink &link) { dispatch.init(link, consumer); }
  void final() {
    dispatch.disable_();
    dispatch.final_();
  }
  void operation(Zhttp::Method::T, const Zhttp::RequestTarget &) { }
  void status(unsigned value) { status_ = value; }
  template <typename Key>
  void header(Zhttp::HdrSection, ZuBSpan) { }
  template <typename Rx>
  void body(Rx &rx) {
    ++bodyCalls;
    Zhttp::bodyEach(rx, [this](ZuBSpan value) { body_ << value; });
  }
  void complete(Zhttp::H2::ParserState::T value) { complete_ = value; }
  void bodyInfo(Zhttp::BodyType::T type, uint64_t length_) {
    if (type == Zhttp::BodyType::Fixed) length = length_;
    if (streamExpected && status_ >= 200 && status_ < 300 &&
	type != Zhttp::BodyType::None) {
      stream();
      ++streamEstablished;
      ++streamStarts;
    }
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
	  [&offered](ZuBSpan span) -> int64_t {
	    offered = span.data();
	    return 1;
	  },
	  [this, &offered](ZuBSpan span) {
	    streamNoCopy &= span.data() == offered;
	    streamBody << ZuCSpan{span};
	  });
      if (n <= 0) break;
    }
  }

  ZtString<>			body_;
  ZtString<>			streamBody;
  Zhttp::StreamDispatch<ClientLink, ClientStream> dispatch;
  ClientStream			consumer;
  uint64_t			length = 0;
  unsigned			status_ = 0;
  unsigned			bodyCalls = 0;
  unsigned			streamEstablished = 0;
  unsigned			streamStarts = 0;
  unsigned			streamEnds = 0;
  unsigned			streamResets = 0;
  bool				streamNoCopy = true;
  bool				streamExpected = false;
  Zhttp::H2::ParserState::T	complete_ =
    Zhttp::H2::ParserState::Initial;
};

template <typename Stream, typename Rx>
int ClientStream::process(Stream, Rx &rx)
{
  parser->processStream(rx);
  return 1;
}

struct Client : public Zhttp::ClientHub<Client, Zhttp::H2TLS> {
  using Link = ClientLink;
  State *state = nullptr;

  Client(State *state_) : state{state_} { }

  void connected(Link &link, Zhttp::ConnectedInfo info);
  void disconnected(Link &link, bool);
  void connectFailed(Link &, bool) {
    ++state->errors;
    state->response.post();
  }
  int process(Link &link, Zhttp::H2_::EventRx &rx);
};

struct ClientLink :
  public Zhttp::ClientLink<Client, ClientLink, Zhttp::H2TLS> {
  using Base =
    Zhttp::ClientLink<Client, ClientLink, Zhttp::H2TLS>;
  using Base::Base;

  ClientParser parser;
  unsigned	id = 0;
  unsigned	disconnects = 0;
  bool		streamSent = false;
  bool		holdOpen = false;
};

void Client::disconnected(Link &link, bool)
{
  link.parser.final();
  ++link.disconnects;
  ++state->closed;
}

void Client::connected(Link &link, Zhttp::ConnectedInfo info)
{
  if (info.httpVersion != Zhttp::Version::H2 ||
      info.transport != Zhttp::Transport::TLS ||
      info.alpn != "h2") {
    ++state->errors;
    state->response.post();
    return;
  }
  link.parser.bind(link);
  auto tx = link.txStream();
  if (link.parser.streamExpected) {
    StreamRequestBuilder builder;
    if (!builder.begin(tx)) {
      ++state->errors;
      state->response.post();
      link.disconnect();
    }
  } else {
    RequestBuilder builder;
    builder.begin(tx);
    builder.finish(tx);
  }
}

int Client::process(Link &link, Zhttp::H2_::EventRx &rx)
{
  auto state_ = link.receive(link.parser, rx);
  if (state_ == Zhttp::H2::ParserState::Error) {
    ++state->errors;
    state->response.post();
    return -1;
  }
  if (link.parser.streamExpected) {
    if (state_ == Zhttp::H2::ParserState::Stream &&
	!link.streamSent) {
      link.streamSent = true;
      if (link.holdOpen) {
	state->response.post();
	return 0;
      }
      Zhttp::Stream stream{link};
      stream.txStream([](auto &body) {
	body << ZuCSpan{"ping"};
	body.flush();
      });
      stream.end();
    }
    if (state_ == Zhttp::H2::ParserState::RemoteClosed) {
      if (link.parser.streamEstablished != 1 ||
	  link.parser.streamStarts != 1 ||
	  link.parser.streamBody != "ping" ||
	  !link.parser.streamNoCopy ||
	  link.parser.streamEnds != 1 || link.parser.streamResets)
	++state->errors;
      state->response.post();
    }
    return 0;
  }
  if (state_ != Zhttp::H2::ParserState::Complete) return 0;
  if (link.parser.status_ != 200 || link.parser.length != 4 ||
      link.parser.body_ != "pong" || link.parser.bodyCalls != 4)
    ++state->errors;
  state->response.post();
  return 1;
}

struct Server;
struct ServerLink;

struct ServerParser;

struct ServerStream {
  template <typename Stream, typename Rx>
  int process(Stream stream, Rx &rx);

  ServerParser	*parser = nullptr;
};

struct ServerParser : public Zhttp::H2::Parser<ServerParser, true> {
  ServerParser() : consumer{this} { }

  void bind(ServerLink &link) { dispatch.init(link, consumer); }
  void final() {
    dispatch.disable_();
    dispatch.final_();
  }
  void operation(
    Zhttp::Method::T method_, const Zhttp::RequestTarget &target) {
    method = method_;
    path = target.raw;
    protocol_ = target.protocol;
  }
  void bodyInfo(Zhttp::BodyType::T type, uint64_t) {
    if (method == Zhttp::Method::CONNECT && protocol_ &&
	type != Zhttp::BodyType::None) {
      stream();
      ++streamEstablished;
      ++streamStarts;
    }
  }
  void status(unsigned) { }
  template <typename Key>
  void header(Zhttp::HdrSection, ZuBSpan) { }
  template <typename Rx>
  void body(Rx &rx) { Zhttp::bodyDrain(rx); }
  template <typename Rx>
  void streamRx_(Rx &rx) { dispatch.process(rx); }
  void streamPeerEnd_() { remoteEnded = true; dispatch.peerEnd(); }
  void streamError_() { ++streamResets; dispatch.error(); }
  template <typename Rx>
  void processStream(Rx &rx) {
    while (rx) {
      const uint8_t *offered = nullptr;
      int64_t n = rx.consume(
	  [&offered](ZuBSpan span) -> int64_t {
	    offered = span.data();
	    return 1;
	  },
	  [this, &offered](ZuBSpan span) {
	    streamNoCopy &= span.data() == offered;
	    streamData << ZuCSpan{span};
	  });
      if (n <= 0) break;
    }
  }
  void complete(Zhttp::H2::ParserState::T value) { complete_ = value; }

  ZtString<>			path;
  ZtString<>			protocol_;
  ZtString<>			streamData;
  Zhttp::StreamDispatch<ServerLink, ServerStream> dispatch;
  ServerStream			consumer;
  Zhttp::Method::T		method = -1;
  Zhttp::H2::ParserState::T	complete_ =
    Zhttp::H2::ParserState::Initial;
  unsigned			streamEstablished = 0;
  unsigned			streamStarts = 0;
  unsigned			streamResets = 0;
  bool				remoteEnded = false;
  bool				streamNoCopy = true;
};

template <typename Stream, typename Rx>
int ServerStream::process(Stream, Rx &rx)
{
  parser->processStream(rx);
  return 1;
}

struct ServerSession {
  ServerParser parser;

  template <typename Link>
  void connected(Link &link) {
    connected_ = true;
    if constexpr (ZuIsSame<ZuDecay<Link>, ServerLink>{})
      parser.bind(link);
    parser.extendedConnect(Zhttp::Stream{link}.localCap());
  }
  template <typename Link>
  void disconnected(Link &, bool) {
    if constexpr (ZuIsSame<ZuDecay<Link>, ServerLink>{})
      parser.final();
  }
  template <typename Link>
  int process(Link &link, Zhttp::H2_::EventRx &rx) {
    if (!connected_) ++link.app()->state->errors;
    parser.streamData.null();
    parser.remoteEnded = false;
    auto state = link.receive(parser, rx);
    if (state == Zhttp::H2::ParserState::Error) return -1;
    if (state == Zhttp::H2::ParserState::Stream ||
	state == Zhttp::H2::ParserState::RemoteClosed) {
      if (!parser.streamNoCopy || parser.streamStarts != 1)
	++link.app()->state->errors;
      if (!streamResponse_) {
	streamResponse_ = true;
	auto tx = link.txStream();
	StreamResponseBuilder builder;
	builder.begin(tx);
      }
      if (parser.streamData) {
	Zhttp::Stream{link}.txStream([this](auto &body) {
	  body << parser.streamData;
	  body.flush();
	});
      }
      if (parser.remoteEnded) Zhttp::Stream{link}.end();
      return 0;
    }
    if (state != Zhttp::H2::ParserState::Complete) return 0;
    if (parser.method != Zhttp::Method::GET || parser.path != "/")
      ++link.app()->state->errors;
    auto tx = link.txStream();
    ResponseBuilder builder;
    builder.begin(tx);
    auto body = builder.body(tx);
    body << ZuCSpan{"pong"};
    body.flush();
    builder.finish(tx);
    return 1;
  }

  bool connected_ = false;
  bool streamResponse_ = false;
};

struct Server : public Zhttp::ProtocolServer<Server, Zhttp::H2TLS> {
  using Link = ServerLink;
  State *state = nullptr;

  Server(State *state_) : state{state_} { }

  ZiIP localIP() const { return ZiIP{"127.0.0.1"}; }
  unsigned localPort() const { return state->port; }
  ZuTime idleTimeout() const { return ZuTime{0}; }
  void listening(const ZiListenInfo &) { state->listening.post(); }
  void listenFailed(bool) {
    ++state->errors;
    state->listening.post();
  }
  bool admit(const ZiCxnInfo &) {
    ++state->admissions;
    return true;
  }
  void release() { ++state->releases; }
  void disconnected(Link &, bool) { ++state->closed; }
};

struct ServerLink :
  public Zhttp::ServerLink<
    Server, ServerLink, Zhttp::H2TLS, ServerSession> {
  using Base = Zhttp::ServerLink<
    Server, ServerLink, Zhttp::H2TLS, ServerSession>;
  using Base::Base;
};

struct H1Client;
struct H1ClientLink;

struct H1Client : public Zhttp::ClientHub<H1Client, Zhttp::H1TLS> {
  using Link = H1ClientLink;
  State *state = nullptr;

  H1Client(State *state_) : state{state_} { }

  void connected(Link &link, Zhttp::ConnectedInfo info);
  void disconnected(Link &, bool) { ++state->closed; }
  void connectFailed(Link &, bool) {
    ++state->errors;
    state->response.post();
  }
  int process(Link &, Ztls::RxStream &) { return -1; }
};

struct H1ClientLink :
  public Zhttp::ClientLink<H1Client, H1ClientLink, Zhttp::H1TLS> {
  using Base =
    Zhttp::ClientLink<H1Client, H1ClientLink, Zhttp::H1TLS>;
  using Base::Base;
};

struct MismatchClient;
struct MismatchLink;

struct MismatchClient :
  public Zhttp::ClientHub<MismatchClient, Zhttp::H2TLS> {
  using Link = MismatchLink;
  State *state = nullptr;

  MismatchClient(State *state_) : state{state_} { }

  void connected(Link &, Zhttp::ConnectedInfo) {
    ++state->errors;
    state->response.post();
  }
  void disconnected(Link &, bool) { ++state->errors; }
  void connectFailed(Link &link, bool);
  int process(Link &, Zhttp::H2_::EventRx &) {
    ++state->errors;
    return -1;
  }
};

struct MismatchLink :
  public Zhttp::ClientLink<
    MismatchClient, MismatchLink, Zhttp::H2TLS> {
  using Base = Zhttp::ClientLink<
    MismatchClient, MismatchLink, Zhttp::H2TLS>;
  using Base::Base;

  unsigned id = 0;
};

void MismatchClient::connectFailed(Link &link, bool)
{
  if (link.result() != Zhttp::ResultCode::Unprocessed)
    ++state->errors;
  state->response.post();
}

void H1Client::connected(Link &link, Zhttp::ConnectedInfo info)
{
  if (info.httpVersion != Zhttp::Version::H1 ||
      info.transport != Zhttp::Transport::TLS ||
      info.alpn != "http/1.1")
    ++state->errors;
  state->response.post();
  link.disconnect();
}

struct SharedServer;
struct SharedH1Link;
struct SharedH2Link;
struct SharedClient;
struct SharedClientH1Link;
struct SharedClientH2Link;

struct SharedH1Session {
  template <typename Link> void connected(Link &) { }
  template <typename Link> void disconnected(Link &, bool) { }
  template <typename Link>
  int process(Link &, Ztls::RxStream &) { return -1; }
};

struct SharedServer : public Zhttp::TLS_::ServerHub<SharedServer> {
  using H1Link = SharedH1Link;
  using H2Link = SharedH2Link;
  State *state = nullptr;

  SharedServer(State *state_) : state{state_} { }

  ZiIP localIP() const { return ZiIP{"127.0.0.1"}; }
  unsigned localPort() const { return state->port; }
  ZuTime idleTimeout() const { return ZuTime{0}; }
  void listening(const ZiListenInfo &) { state->listening.post(); }
  void listenFailed(bool) {
    ++state->errors;
    state->listening.post();
  }
  bool admit(const ZiCxnInfo &) {
    ++state->admissions;
    return true;
  }
  void release() { ++state->releases; }
  template <typename Link>
  void connected(Link &, Zhttp::ConnectedInfo info) {
    if (info.transport != Zhttp::Transport::TLS ||
	(info.httpVersion != Zhttp::Version::H1 &&
	 info.httpVersion != Zhttp::Version::H2))
      ++state->errors;
  }
  template <typename Link>
  void disconnected(Link &, bool) { ++state->closed; }
};

struct SharedH1Link :
  public Zhttp::TLS_::ServerH1Logical<
    SharedServer, SharedH1Link, SharedH1Session,
    Zhttp::TLS_::SrvLink<SharedServer>> {
  using Base = Zhttp::TLS_::ServerH1Logical<
    SharedServer, SharedH1Link, SharedH1Session,
    Zhttp::TLS_::SrvLink<SharedServer>>;
  using Base::Base;
};

struct SharedH2Link :
  public Zhttp::H2_::ServerLogical<
    SharedServer, SharedH2Link, ServerSession,
    Zhttp::TLS_::SrvLink<SharedServer>> {
  using Base = Zhttp::H2_::ServerLogical<
    SharedServer, SharedH2Link, ServerSession,
    Zhttp::TLS_::SrvLink<SharedServer>>;
  using Base::Base;
};

struct SharedClient :
  public Zhttp::TLS_::ClientHub<
    SharedClient, SharedClientH1Link, SharedClientH2Link> {
  using H1Link = SharedClientH1Link;
  using H2Link = SharedClientH2Link;
  State *state = nullptr;

  SharedClient(State *state_) : state{state_} { }

  void connected(H1Link &link, Zhttp::ConnectedInfo info);
  void connected(H2Link &link, Zhttp::ConnectedInfo info);
  template <typename Link>
  void disconnected(Link &, bool) { ++state->closed; }
  template <typename Link>
  void connectFailed(Link &, bool) {
    ++state->errors;
    state->response.post();
  }
  template <typename Native>
  void nativeUp(
      unsigned, uint64_t, const ZmRef<Native> &) {
    if (++state->clientNative > state->maxClientNative)
      state->maxClientNative = state->clientNative;
  }
  void nativeDown(unsigned, uint64_t) {
    ZmAssert(state->clientNative);
    --state->clientNative;
  }
  void goaway(uint32_t) { state->goaway.post(); }
  int process(H1Link &, Ztls::RxStream &) { return -1; }
  int process(H2Link &link, Zhttp::H2_::EventRx &rx);
};

struct SharedClientH1Link :
  public Zhttp::TLS_::ClientH1Logical<
    SharedClient, SharedClientH1Link,
    Zhttp::TLS_::CliLink<
      SharedClient, SharedClientH1Link, SharedClientH2Link>> {
  using Base = Zhttp::TLS_::ClientH1Logical<
    SharedClient, SharedClientH1Link,
    Zhttp::TLS_::CliLink<
      SharedClient, SharedClientH1Link, SharedClientH2Link>>;
  using Base::Base;
};

struct SharedClientH2Link :
  public Zhttp::H2_::ClientLogical<
    SharedClient, SharedClientH2Link,
    Zhttp::TLS_::CliLink<
      SharedClient, SharedClientH1Link, SharedClientH2Link>> {
  using Base = Zhttp::H2_::ClientLogical<
    SharedClient, SharedClientH2Link,
    Zhttp::TLS_::CliLink<
      SharedClient, SharedClientH1Link, SharedClientH2Link>>;
  using Base::Base;

  ClientParser parser;
};

void SharedClient::connected(
  H1Link &link, Zhttp::ConnectedInfo info)
{
  if (info.httpVersion != Zhttp::Version::H1 ||
      info.transport != Zhttp::Transport::TLS ||
      info.alpn != "http/1.1")
    ++state->errors;
  link.complete([state = state]() { state->response.post(); });
}

void SharedClient::connected(
  H2Link &link, Zhttp::ConnectedInfo info)
{
  if (info.httpVersion != Zhttp::Version::H2 ||
      info.transport != Zhttp::Transport::TLS ||
      info.alpn != "h2") {
    ++state->errors;
    state->response.post();
    return;
  }
  auto tx = link.txStream();
  RequestBuilder builder;
  builder.begin(tx);
  builder.finish(tx);
}

int SharedClient::process(
  H2Link &link, Zhttp::H2_::EventRx &rx)
{
  auto state_ = rx.process(link.parser);
  if (state_ == Zhttp::H2::ParserState::Error) {
    ++state->errors;
    state->response.post();
    return -1;
  }
  if (state_ != Zhttp::H2::ParserState::Complete) return 0;
  if (link.parser.status_ != 200 || link.parser.length != 4 ||
      link.parser.body_ != "pong" || link.parser.bodyCalls != 4)
    ++state->errors;
  state->response.post();
  return 1;
}

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

void run()
{
  ZuTestScope(run);

  Zhttp::Test::TempDir cert;
  bool certOK = cert.init();
  ZuCHECK(certOK, "certificate fixture failed");
  if (!certOK) return;
  State state;
  state.port = Zhttp::Test::loopbackPort();
  ZuCHECK(state.port, "port allocation failed");
  if (!state.port) return;

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "multiplexer start failed");
  if (!mx.running()) return;

  Zhttp::HubConfig hub{&mx, "3", "4"};
  auto serverConfig = Zhttp::H2Config()
    .certPath(cert.certPath).keyPath(cert.keyPath)
    .maxConcurrentStreams(1).maxQueuedFrames(16);
  auto clientConfig = Zhttp::H2Config()
    .caPath(cert.certPath).initialWindowSize(1)
    .maxConcurrentStreams(1);
  Server server{&state};
  Client client{&state};
  bool initialized =
    server.init(hub, serverConfig) && client.init(hub, clientConfig);
  ZuCHECK(initialized, "H2 hubs initialized");
  if (!initialized) {
    client.final();
    server.final();
    mx.stop();
    return;
  }
  bool started = server.start() && client.start();
  ZuCHECK(started, "H2 hubs started");
  bool listening =
    state.listening.timedwait(Zm::now(10)) == 0;
  ZuCHECK(listening, "H2 server listening");

  ZmRef<ClientLink> link1 = new ClientLink{&client};
  ZmRef<ClientLink> link2 = new ClientLink{&client};
  if (listening) {
    link1->connect("127.0.0.1", state.port);
    link2->connect("127.0.0.1", state.port);
  }
  bool response =
    state.response.timedwait(Zm::now(10)) == 0 &&
    state.response.timedwait(Zm::now(10)) == 0;
  ZuCHECK(response && !state.errors && state.admissions == 1,
    "two logical requests shared one H2 TLS session");

  client.stop([&state](bool ok) {
    if (!ok) ++state.errors;
    state.stopped.post();
  });
  client.stop([&state](bool ok) {
    if (!ok) ++state.errors;
    state.stopped.post();
  });
  server.stop([&state](bool ok) {
    if (!ok || state.releases != 1) ++state.errors;
    state.stopped.post();
  });
  server.stop([&state](bool ok) {
    if (!ok || state.releases != 1) ++state.errors;
    state.stopped.post();
  });
  bool stopped =
    state.stopped.timedwait(Zm::now(10)) == 0 &&
    state.stopped.timedwait(Zm::now(10)) == 0 &&
    state.stopped.timedwait(Zm::now(10)) == 0 &&
    state.stopped.timedwait(Zm::now(10)) == 0;
  ZuCHECK(stopped && !state.errors && state.closed == 4 &&
      state.releases == 1,
    "H2 asynchronous stop follows logical disconnect and admission release");
  client.final();
  server.final();
  mx.stop();
}

void runStream()
{
  ZuTestScope(runStream);

  Zhttp::Test::TempDir cert;
  bool certOK = cert.init();
  ZuCHECK(certOK, "certificate fixture failed");
  if (!certOK) return;
  State state;
  state.port = Zhttp::Test::loopbackPort();
  ZuCHECK(state.port, "port allocation failed");
  if (!state.port) return;

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "multiplexer start failed");
  if (!mx.running()) return;

  Zhttp::HubConfig hub{&mx, "3", "4"};
  auto serverConfig = Zhttp::H2Config()
    .certPath(cert.certPath).keyPath(cert.keyPath)
    .extendedConnect(true).maxConcurrentStreams(4)
    .initialWindowSize(1);
  auto clientConfig = Zhttp::H2Config()
    .caPath(cert.certPath)
    .extendedConnect(true).maxConcurrentStreams(4)
    .initialWindowSize(1);
  Server server{&state};
  Client client{&state};
  bool initialized =
    server.init(hub, serverConfig) && client.init(hub, clientConfig);
  ZuCHECK(initialized, "Extended CONNECT hubs initialized");
  if (!initialized) {
    client.final();
    server.final();
    mx.stop();
    return;
  }
  bool started = server.start() && client.start();
  ZuCHECK(started, "Extended CONNECT hubs started");
  bool listening =
    state.listening.timedwait(Zm::now(10)) == 0;
  ZuCHECK(listening, "Extended CONNECT server listening");

  ZmRef<ClientLink> rest = new ClientLink{&client};
  ZmRef<ClientLink> stream1 = new ClientLink{&client};
  ZmRef<ClientLink> stream2 = new ClientLink{&client};
  ZmRef<ClientLink> active = new ClientLink{&client};
  stream1->parser.streamExpected = true;
  stream2->parser.streamExpected = true;
  active->parser.streamExpected = true;
  active->holdOpen = true;
  if (listening) {
    rest->connect("127.0.0.1", state.port);
    stream1->connect("127.0.0.1", state.port);
    stream2->connect("127.0.0.1", state.port);
    active->connect("127.0.0.1", state.port);
  }
  bool responses =
    state.response.timedwait(Zm::now(10)) == 0 &&
    state.response.timedwait(Zm::now(10)) == 0 &&
    state.response.timedwait(Zm::now(10)) == 0 &&
    state.response.timedwait(Zm::now(10)) == 0;
  ZuCHECK(responses && !state.errors && state.admissions == 1,
    "REST and three flow-controlled streams share one H2 connection");

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
  ZuCHECK(stopped && !state.errors && state.releases == 1 &&
      active->disconnects == 1,
    "active stream disconnects once before physical stop completion");
  client.final();
  server.final();
  mx.stop();
}

void runSharedTLS()
{
  ZuTestScope(runSharedTLS);

  Zhttp::Test::TempDir cert;
  bool certOK = cert.init();
  ZuCHECK(certOK, "certificate fixture failed");
  if (!certOK) return;
  State state;
  state.port = Zhttp::Test::loopbackPort();
  ZuCHECK(state.port, "port allocation failed");
  if (!state.port) return;

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "multiplexer start failed");
  if (!mx.running()) return;

  Zhttp::HubConfig hub{&mx, "3", "4"};
  auto serverConfig = Zhttp::H2Config()
    .certPath(cert.certPath).keyPath(cert.keyPath)
    .policy(Zhttp::H2Policy::Prefer);
  auto h2Config = Zhttp::H2Config()
    .caPath(cert.certPath).initialWindowSize(1)
    .maxStreamID(1)
    .policy(Zhttp::H2Policy::Prefer);
  auto h1Config = Zhttp::H2Config()
    .caPath(cert.certPath)
    .policy(Zhttp::H2Policy::Disable);
  SharedServer server{&state};
  SharedClient h2Client{&state};
  SharedClient h1Client{&state};
  bool initialized =
    server.init(hub, serverConfig) &&
    h2Client.init(hub, h2Config) &&
    h1Client.init(hub, h1Config);
  ZuCHECK(initialized, "shared TLS hubs initialized");
  if (!initialized) {
    h1Client.final();
    h2Client.final();
    server.final();
    mx.stop();
    return;
  }
  bool started =
    server.start() && h2Client.start() && h1Client.start();
  ZuCHECK(started, "shared TLS hubs started");
  bool listening =
    state.listening.timedwait(Zm::now(10)) == 0;
  ZuCHECK(listening, "shared TLS server listening");

  ZmRef<SharedClientH1Link> h2H1 =
    new SharedClientH1Link{&h2Client};
  ZmRef<SharedClientH2Link> h2H2 =
    new SharedClientH2Link{&h2Client};
  ZmRef<SharedClientH1Link> h2H1_2 =
    new SharedClientH1Link{&h2Client};
  ZmRef<SharedClientH2Link> h2H2_2 =
    new SharedClientH2Link{&h2Client};
  ZmRef<SharedClientH1Link> h2H1_3 =
    new SharedClientH1Link{&h2Client};
  ZmRef<SharedClientH2Link> h2H2_3 =
    new SharedClientH2Link{&h2Client};
  ZmRef<SharedClientH1Link> h1H1 =
    new SharedClientH1Link{&h1Client};
  ZmRef<SharedClientH2Link> h1H2 =
    new SharedClientH2Link{&h1Client};
  ZmRef<SharedClientH1Link> h1H1_2 =
    new SharedClientH1Link{&h1Client};
  ZmRef<SharedClientH2Link> h1H2_2 =
    new SharedClientH2Link{&h1Client};
  if (listening)
    h2Client.connect(
      h2H1, h2H2, Ztls::Host{"127.0.0.1"}, state.port, 0);
  bool firstH2 =
    state.response.timedwait(Zm::now(10)) == 0;
  ZuCHECK(firstH2 && !state.errors,
    "prefer-H2 client completed its first logical request");
  if (firstH2)
    server.drain([&state]() { state.goaway.post(); });
  bool drained =
    state.goaway.timedwait(Zm::now(10)) == 0 &&
    state.goaway.timedwait(Zm::now(10)) == 0;
  ZuCHECK(drained, "client observed server GOAWAY drain");
  if (drained)
    h2Client.connect(
      h2H1_2, h2H2_2, Ztls::Host{"127.0.0.1"}, state.port, 0);
  bool secondH2 =
    state.response.timedwait(Zm::now(10)) == 0;
  ZuCHECK(secondH2 && !state.errors && state.admissions == 2,
    "post-GOAWAY H2 request used a replacement TLS connection");
  ZuCHECK(state.maxClientNative == 1,
    "post-GOAWAY replacement waits for the prior connection to close");
  if (secondH2)
    h2Client.connect(
      h2H1_3, h2H2_3, Ztls::Host{"127.0.0.1"}, state.port, 0);
  bool thirdH2 =
    state.response.timedwait(Zm::now(10)) == 0;
  ZuCHECK(thirdH2 && !state.errors && state.admissions == 3,
    "stream-ID exhaustion rotated the H2 TLS connection");
  ZuCHECK(state.maxClientNative == 1,
    "stream-ID replacement preserves one live connection per link");
  if (thirdH2)
    h1Client.connect(
      h1H1, h1H2, Ztls::Host{"127.0.0.1"}, state.port, 0);
  if (thirdH2)
    h1Client.connect(
      h1H1_2, h1H2_2, Ztls::Host{"127.0.0.1"}, state.port, 0);
  bool h1Responses =
    state.response.timedwait(Zm::now(10)) == 0 &&
    state.response.timedwait(Zm::now(10)) == 0;
  ZuCHECK(h1Responses,
    "one TLS connection completed two overlapping H1 requests");
  ZuCHECK(!state.errors, "shared TLS message paths reported no errors");
  ZuCHECK(state.admissions == 4,
    "shared TLS listener admitted rotated H2 plus H1 connections");
  ZuCHECK(!h1H1->active() && !h1H1_2->active() &&
      !state.errors && state.admissions == 4,
    "overlapping H1 operations retire without closing their TLS link");

  h1Client.stop([&state](bool ok) {
    if (!ok) ++state.errors;
    state.stopped.post();
  });
  h2Client.stop([&state](bool ok) {
    if (!ok) ++state.errors;
    state.stopped.post();
  });
  server.stop([&state](bool ok) {
    if (!ok || state.releases != 4) ++state.errors;
    state.stopped.post();
  });
  bool stopped =
    state.stopped.timedwait(Zm::now(10)) == 0 &&
    state.stopped.timedwait(Zm::now(10)) == 0 &&
    state.stopped.timedwait(Zm::now(10)) == 0;
  ZuCHECK(stopped, "shared TLS stop continuations completed");
  ZuCHECK(!state.errors, "shared TLS stop reported no errors");
  ZuCHECK(state.closed == 7 &&
      !h1H1->active() && !h1H1_2->active(),
    "shared TLS stop disconnected H2 links and released H1 operations");
  ZuCHECK(state.releases == 4,
    "shared TLS stop released every physical admission");
  h1Client.final();
  h2Client.final();
  server.final();
  mx.stop();
}

void runForceMismatch()
{
  ZuTestScope(runForceMismatch);

  Zhttp::Test::TempDir cert;
  bool certOK = cert.init();
  ZuCHECK(certOK, "certificate fixture failed");
  if (!certOK) return;
  State state;
  state.port = Zhttp::Test::loopbackPort();
  ZuCHECK(state.port, "port allocation failed");
  if (!state.port) return;

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "multiplexer start failed");
  if (!mx.running()) return;

  Zhttp::HubConfig hub{&mx, "3", "4"};
  auto serverConfig = Zhttp::H2Config()
    .certPath(cert.certPath).keyPath(cert.keyPath)
    .policy(Zhttp::H2Policy::Disable);
  auto clientConfig = Zhttp::H2Config().caPath(cert.certPath);
  SharedServer server{&state};
  MismatchClient client{&state};
  bool initialized =
    server.init(hub, serverConfig) &&
    client.init(hub, clientConfig);
  ZuCHECK(initialized, "mismatch hubs initialized");
  if (!initialized) {
    client.final();
    server.final();
    mx.stop();
    return;
  }
  bool started = server.start() && client.start();
  ZuCHECK(started, "mismatch hubs started");
  bool listening =
    state.listening.timedwait(Zm::now(10)) == 0;
  ZuCHECK(listening, "H1-only TLS server listening");

  ZmRef<MismatchLink> link = new MismatchLink{&client};
  if (listening) link->connect("127.0.0.1", state.port);
  bool failed = state.response.timedwait(Zm::now(10)) == 0;
  ZuCHECK(failed && !state.errors,
    "force-H2 mismatch failed before an HTTP logical link opened");

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
  ZuCHECK(stopped && !state.errors && !state.closed &&
      state.admissions == 1 && state.releases == 1,
    "failed ALPN drained one physical admission without logical callbacks");
  client.final();
  server.final();
  mx.stop();
}

} // namespace ZhttpH2HubTest_

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(ZhttpH2HubTest_::runFlowCapacity);
  ZuTestCall(ZhttpH2HubTest_::runAdmissionErrors);
  ZuTestCall(ZhttpH2HubTest_::run);
  ZuTestCall(ZhttpH2HubTest_::runStream);
  ZuTestCall(ZhttpH2HubTest_::runSharedTLS);
  ZuTestCall(ZhttpH2HubTest_::runForceMismatch);
}
