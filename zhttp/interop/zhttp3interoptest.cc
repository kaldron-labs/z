//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZiResolver.hh>
#include <zlib/Ztcp.hh>
#include <zlib/Ztls.hh>
#include <zlib/Zquic.hh>
#include <zlib/Zhttp.hh>

#include "ZhttpTestUtil.hh"

using namespace ZuTestUtil;

namespace Zhttp3InteropTest_ {

using Zhttp::Test::TempDir;
using Zhttp::Test::haveCurlH3;
using Zhttp::Test::loopbackPort;
using Zhttp::Test::printFile;
using Zhttp::Test::retry;
using Zhttp::Test::runCurlH3Retry;
using Zhttp::Test::systemOK;
using Zhttp::Test::writeLocalhostCert;
using Zquic::Test::CaddyProcess;
using Zquic::Test::haveCaddy;
using Zquic::Test::waitCaddyReady;
using Zquic::Test::writeCaddyfile;

using RequestHeaders = ZhttpHeaders("content-length");
using ResponseHeaders = ZhttpHeaders("content-type", "content-length");

ZuCSpan Path = "/zhttp-interop";
ZuCSpan Host = "localhost";
bool frag = false;
bool yield = false;

void usage(ZuCSpan name)
{
  std::cerr <<
    "Usage: " << name << " [OPTION]...\n\n"
    "Options:\n"
    "  --frag    fragment ZiMultiplex I/O in debug builds\n"
    "  --yield   yield in ZiMultiplex in debug builds\n"
    "  -q        quiet output (default when test-harnessed)\n";
  ::exit(1);
}

void parseArgs(int argc, char **argv)
{
  verbose = !::getenv("HARNESS_ACTIVE");
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "-q")) {
      verbose = false;
      continue;
    }
    if (!strcmp(argv[i], "--frag")) {
      frag = true;
      continue;
    }
    if (!strcmp(argv[i], "--yield")) {
      yield = true;
      continue;
    }
    ZuCSpan argv0 = argv[0];
    for (int j = argv0.length(); --j >= 0; ) {
      auto c = argv0[j];
      if (c == '/'
#ifdef _WIN32
	  || c == '\\'
#endif
	) {
	argv0.offset(j + 1);
	break;
      }
    }
    usage(argv0);
  }
}

ZiMxParams mxParams(bool h3 = false)
{
  auto params = ZiMxParams()
    .scheduler([](auto &s) {
      s.nThreads(5)
	.thread(1, [](auto &t) { t.isolated(1); })
	.thread(2, [](auto &t) { t.isolated(1); })
	.thread(3, [](auto &t) { t.isolated(1); })
	.thread(4, [](auto &t) { t.isolated(1); });
    })
    .rxThread(1).txThread(2);
#ifdef ZiMultiplex_DEBUG
  if (frag && !h3) params.frag(true);
  if (yield) params.yield(true);
#endif
  return params;
}

bool validQUICInfo(Zquic::Connected info)
{
  return
    info.version == Zquic::Version1 &&
    info.alpn == "h3";
}

struct ResponseBody {
  ZuCSpan	value;
};

template <typename Impl>
struct H1ResponseBuilder_ :
  public Zhttp::H1Response<Impl, ResponseHeaders, ZuTypeList<>, true> {
  using Base = Zhttp::H1Response<Impl, ResponseHeaders, ZuTypeList<>, true>;
};

template <typename Impl>
struct H3ResponseBuilder_ :
  public Zhttp::H3Response<Impl, ResponseHeaders, ZuTypeList<>, true> {
  using Base = Zhttp::H3Response<Impl, ResponseHeaders, ZuTypeList<>, true>;
};

template <template <typename> typename Builder_>
struct ResponseBuilder : public Builder_<ResponseBuilder<Builder_>> {
  using Base = Builder_<ResponseBuilder<Builder_>>;

  ResponseBuilder(ZuCSpan body_) : content{body_} { }

  unsigned status() const { return 200; }
  template <typename L> void reason(L &&l) const { l("OK"); }
  uint64_t contentLength() const { return content.length(); }
  Zhttp::H3::QPackTxTable *qpackTx() const { return qpackTx_; }
  bool qpackEncoderWrite(ZuBSpan span) const {
    return qpackEncoderWrite_ && qpackEncoderWrite_(qpackEncoder_, span);
  }
  uint64_t streamID() const { return streamID_; }
  template <typename Key, typename L>
  void header(L &&l) const {
    if constexpr (Key{}() == "content-type")
      l("text/plain");
    else if constexpr (Key{}() == "content-length")
      l(ZuBoxed(content.length()));
    else
      l("");
  }

  ZuCSpan			content;
  Zhttp::H3::QPackTxTable	*qpackTx_ = nullptr;
  void				*qpackEncoder_ = nullptr;
  bool				(*qpackEncoderWrite_)(void *, ZuBSpan) = nullptr;
  uint64_t			streamID_ = 0;
};

using H1ResponseBuilder = ResponseBuilder<H1ResponseBuilder_>;
using H3ResponseBuilder = ResponseBuilder<H3ResponseBuilder_>;

template <typename Stream>
void sendH1Response(Stream &stream, ZuCSpan body)
{
  auto tx = stream.txStream();
  H1ResponseBuilder builder{body};
  builder.begin(tx);
  if (body) {
    auto bodyTx = builder.body(tx);
    bodyTx << body;
  }
  builder.finish(tx);
}

template <typename Stream>
void flushH3Message(Stream &stream)
{
  auto ref = stream.link()->findStream(stream.id());
  if (ref) stream.link()->send(ref, "", true);
}

template <typename Stream>
void sendH3Response(Stream &stream, ZuCSpan body)
{
  auto ref = stream.link()->findStream(stream.id());
  if (!ref) return;
  ZtString<> body_{body};
  stream.link()->app()->txInvoke([
    link = stream.link(), ref, body_ = ZuMv(body_)
  ]() mutable {
    auto tx = ref->txStream_();
    H3ResponseBuilder builder{body_};
    builder.qpackTx_ = link->qpackTx();
    builder.qpackEncoder_ = &link->h3;
    builder.qpackEncoderWrite_ = [](void *ptr, ZuBSpan span) {
      return static_cast<ZuDecay<decltype(link->h3)> *>(ptr)->
	qpackEncoderWrite(span);
    };
    builder.streamID_ = uint64_t(ref->id());
    builder.begin(tx);
    if (body_) {
      auto bodyTx = builder.body(tx);
      bodyTx << body_;
      bodyTx.flush();
    }
    builder.finish(tx);
    link->send_(ref, "", true);
  });
}

struct RequestSeen {
  Zhttp::Method::T	method = -1;
  ZtString<>		path;
  ZtString<>		body;
  unsigned		complete = 0;
  unsigned		errors = 0;
};

template <typename Impl, bool H3>
struct RequestParserBase_;
template <typename Impl>
struct RequestParserBase_<Impl, false> :
  public Zhttp::H1RequestParser<Impl, RequestHeaders> {
  using Base = Zhttp::H1RequestParser<Impl, RequestHeaders>;
  RequestParserBase_() : Base{1<<20} { }
};
template <typename Impl>
struct RequestParserBase_<Impl, true> :
  public Zhttp::H3RequestParser<Impl, RequestHeaders> {
  using Base = Zhttp::H3RequestParser<Impl, RequestHeaders>;
  RequestParserBase_() : Base{1<<20} { }
};

template <bool H3>
struct RequestParser : public RequestParserBase_<RequestParser<H3>, H3> {
  using Base = RequestParserBase_<RequestParser<H3>, H3>;
  using State = typename Base::State;

  Zhttp::H3::QPackRxTable *qpackRx() const { return qpackRx_; }
  bool qpackDecoderWrite(ZuBSpan span) const {
    return qpackDecoderWrite_ && qpackDecoderWrite_(qpackDecoder_, span);
  }
  uint64_t streamID() const { return streamID_; }
  void operation(
    Zhttp::Method::T method, const Zhttp::Target &target) {
    seen.method = method;
    seen.path = target.pathQuery;
  }
  template <typename Rx>
  bool body(Rx &rx) {
    return Zhttp::bodyEach(rx,
      [this](ZuBSpan body) { seen.body << body; });
  }
  void complete(typename State::T state) {
    if (state == State::Complete) seen.complete = 1;
    else if (state == State::Error) seen.errors = 1;
    else if constexpr (H3) {
      if (state == State::Cancelled) seen.errors = 1;
    }
  }

  RequestSeen			seen;
  Zhttp::H3::QPackRxTable	*qpackRx_ = nullptr;
  void				*qpackDecoder_ = nullptr;
  bool				(*qpackDecoderWrite_)(void *, ZuBSpan) = nullptr;
  uint64_t			streamID_ = 0;
};

struct ResponseSeen {
  unsigned	status = 0;
  ZtString<>	body;
  unsigned	complete = 0;
  unsigned	errors = 0;
};

template <typename Impl, bool H3>
struct ResponseParserBase_;
template <typename Impl>
struct ResponseParserBase_<Impl, false> :
  public Zhttp::H1ResponseParser<Impl, ResponseHeaders> {
  using Base = Zhttp::H1ResponseParser<Impl, ResponseHeaders>;
  ResponseParserBase_() : Base{4<<20} { }
};
template <typename Impl>
struct ResponseParserBase_<Impl, true> :
  public Zhttp::H3ResponseParser<Impl, ResponseHeaders> {
  using Base = Zhttp::H3ResponseParser<Impl, ResponseHeaders>;
  ResponseParserBase_() : Base{4<<20} { }
};

template <bool H3>
struct ResponseParser : public ResponseParserBase_<ResponseParser<H3>, H3> {
  using Base = ResponseParserBase_<ResponseParser<H3>, H3>;
  using State = typename Base::State;

  Zhttp::H3::QPackRxTable *qpackRx() const { return qpackRx_; }
  bool qpackDecoderWrite(ZuBSpan span) const {
    return qpackDecoderWrite_ && qpackDecoderWrite_(qpackDecoder_, span);
  }
  uint64_t streamID() const { return streamID_; }
  void status(unsigned status_) { seen.status = status_; }
  template <typename Rx>
  bool body(Rx &rx) {
    return Zhttp::bodyEach(rx,
      [this](ZuBSpan body) { seen.body << body; });
  }
  void complete(typename State::T state) {
    if (state == State::Complete) seen.complete = 1;
    else if (state == State::Error) seen.errors = 1;
    else if constexpr (H3) {
      if (state == State::Cancelled) seen.errors = 1;
    }
  }

  ResponseSeen			seen;
  Zhttp::H3::QPackRxTable	*qpackRx_ = nullptr;
  void				*qpackDecoder_ = nullptr;
  bool				(*qpackDecoderWrite_)(void *, ZuBSpan) = nullptr;
  uint64_t			streamID_ = 0;
};

template <typename Impl>
struct RequestBuilder_ :
  public Zhttp::H1Request<Impl, ZuTypeList<>, ZuTypeList<>, false> { };
template <typename Impl>
struct RequestBuilderH3_ :
  public Zhttp::H3Request<Impl, ZuTypeList<>, ZuTypeList<>, false> { };

template <template <typename> typename Builder_>
struct RequestBuilder : public Builder_<RequestBuilder<Builder_>> {
  using Base = Builder_<RequestBuilder<Builder_>>;

  RequestBuilder(ZuCSpan body_) : content{body_} { }

  template <typename L> void operation(L &&l) const {
    l(Zhttp::Method::GET, [](auto &&emit) {
      emit([](auto &tx) { tx << Path; });
    });
  }
  template <typename L> void host(L &&l) const { l(Host); }
  uint64_t contentLength() const { return content.length(); }
  Zhttp::H3::QPackTxTable *qpackTx() const { return qpackTx_; }
  bool qpackEncoderWrite(ZuBSpan span) const {
    return qpackEncoderWrite_ && qpackEncoderWrite_(qpackEncoder_, span);
  }
  uint64_t streamID() const { return streamID_; }

  ZuCSpan			content;
  Zhttp::H3::QPackTxTable	*qpackTx_ = nullptr;
  void				*qpackEncoder_ = nullptr;
  bool				(*qpackEncoderWrite_)(void *, ZuBSpan) = nullptr;
  uint64_t			streamID_ = 0;
};

using H1RequestBuilder = RequestBuilder<RequestBuilder_>;
using H3RequestBuilder = RequestBuilder<RequestBuilderH3_>;

template <typename Stream>
void sendH1Request(Stream &stream, ZuCSpan body)
{
  auto tx = stream.txStream();
  H1RequestBuilder builder{body};
  builder.begin(tx);
  builder.finish(tx);
}

template <typename Stream>
void sendH3Request(Stream &stream, ZuCSpan body)
{
  auto ref = stream.link()->findStream(stream.id());
  if (!ref) return;
  ZtString<> body_{body};
  stream.link()->app()->txInvoke([
    link = stream.link(), ref, body_ = ZuMv(body_)
  ]() mutable {
    auto tx = ref->txStream_();
    H3RequestBuilder builder{body_};
    builder.qpackTx_ = link->qpackTx();
    builder.qpackEncoder_ = &link->h3;
    builder.qpackEncoderWrite_ = [](void *ptr, ZuBSpan span) {
      return static_cast<ZuDecay<decltype(link->h3)> *>(ptr)->
	qpackEncoderWrite(span);
    };
    builder.streamID_ = uint64_t(ref->id());
    builder.begin(tx);
    builder.finish(tx);
    link->send_(ref, "", true);
  });
}

struct ServerTest {
  ZmSemaphore	listening;
  ZmSemaphore	done;
  ZtString<>	body{"zhttp-ok"};
  RequestSeen	request;
  unsigned	port = 0;
  ZmAtomic<unsigned> errors = 0;
  ZmAtomic<uint64_t> lastH3StreamID = 0;
  ZmAtomic<unsigned> h3UniStreams = 0;
  ZmAtomic<unsigned> h3BidiStreams = 0;
  ZmAtomic<int>	h3State = Zhttp::H3::CxnState::Init;
  ZmAtomic<int>	h3ParserState = RequestParser<true>::State::Initial;
  ZmAtomic<uint64_t> h3Error = 0;
};

template <typename App>
struct H1ServerLinkOps {
  void respond(auto &stream) {
    auto link = static_cast<App *>(this);
    auto &state = *link->app()->state;
    sendH1Response(stream, state.body);
    state.done.post();
  }
};

struct TCPServer : public Ztcp::Server<TCPServer> {
  struct Link;

  TCPServer(ServerTest *state_) : state{state_} { }

  ZiConnection *accepted(const ZiCxnInfo &ci);
  ZiIP localIP() const { return ZiIP("127.0.0.1"); }
  unsigned localPort() const { return state->port; }
  void listening(const ZiListenInfo &info) {
    state->port = info.port;
    state->listening.post();
  }
  void listenFailed(bool) { state->errors = 1; state->done.post(); }

  ServerTest	*state = nullptr;
};

struct TCPServer::Link :
  public Ztcp::SrvLink<TCPServer, TCPServer::Link>,
  public H1ServerLinkOps<TCPServer::Link> {
  using Base = Ztcp::SrvLink<TCPServer, TCPServer::Link>;
  using Base::Base;

  Link(TCPServer *app) : Base{app} { }
  void connected(Ztcp::Connected) { }
  void disconnected(bool) { }
  int process(Ztcp::RxStream &rx) {
    auto s = parser.process(rx);
    if (s == RequestParser<false>::State::Error) {
      app()->state->errors = 1;
      app()->state->done.post();
      return -1;
    }
    if (s == RequestParser<false>::State::Complete) {
      app()->state->request = parser.seen;
      respond(*this);
      return 1;
    }
    return 0;
  }

  RequestParser<false>	parser;
};

ZiConnection *TCPServer::accepted(const ZiCxnInfo &ci)
{
  return new Link::Cxn(new Link(this), ci);
}

struct TLSServer : public Ztls::Server<TLSServer> {
  using RxBufAlloc = Ztls::RxBufAlloc<8<<10, 4<<20>;
  using TxBufAlloc = Ztls::TxBufAlloc<8<<10, 4<<20>;
  struct Link;

  TLSServer(ServerTest *state_) : state{state_} { }

  ZiConnection *accepted(const ZiCxnInfo &ci);
  ZiIP localIP() const { return ZiIP("127.0.0.1"); }
  unsigned localPort() const { return state->port; }
  void listening(const ZiListenInfo &info) {
    state->port = info.port;
    state->listening.post();
  }
  void listenFailed(bool) { state->errors = 1; state->done.post(); }

  ServerTest	*state = nullptr;
};

struct TLSServer::Link :
  public Ztls::SrvLink<TLSServer, TLSServer::Link,
    TLSServer::RxBufAlloc, TLSServer::TxBufAlloc>,
  public H1ServerLinkOps<TLSServer::Link> {
  using Base = Ztls::SrvLink<TLSServer, TLSServer::Link,
    TLSServer::RxBufAlloc, TLSServer::TxBufAlloc>;
  using Base::Base;

  Link(TLSServer *app) : Base{app} { }
  void connected(Ztls::Connected) { }
  void disconnected(bool) { }
  int process(Ztls::RxStream &rx) {
    auto s = parser.process(rx);
    if (s == RequestParser<false>::State::Error) {
      app()->state->errors = 1;
      app()->state->done.post();
      return -1;
    }
    if (s == RequestParser<false>::State::Complete) {
      app()->state->request = parser.seen;
      respond(*this);
      return 1;
    }
    return 0;
  }

  RequestParser<false>	parser;
};

ZiConnection *TLSServer::accepted(const ZiCxnInfo &ci)
{
  return new Link::Cxn(new Link(this), ci);
}

struct H3ServerLink;
struct H3ServerStream;
struct H3Server : public Zquic::Server<H3Server, H3ServerLink> {
  using Link = H3ServerLink;
  using Stream = H3ServerStream;

  H3Server(ServerTest *state_) : state{state_} { }

  ZmRef<Link> accepted(const Zquic::InitialInfo &);
  ZmRef<Link> link() const { return link_; }
  void clearLink() { link_ = nullptr; }
  ZiIP localIP() const { return ZiIP("127.0.0.1"); }
  void listening() {
    state->port = this->local().port();
    state->listening.post();
  }
  void listenFailed(bool) { state->errors = 1; state->done.post(); }

  ServerTest	*state = nullptr;
  ZmRef<Link>	link_;
};

struct H3ServerStream :
  public Zquic::SrvStream<H3ServerLink, H3ServerStream>,
  public Zhttp::H3::CxnParser<H3ServerStream> {
  using Base = Zquic::SrvStream<H3ServerLink, H3ServerStream>;
  using CxnParser = Zhttp::H3::CxnParser<H3ServerStream>;
  using Base::Base;

  int process(Zquic::RxStream &);
  Zhttp::H3::CxnState::T h3State() const;
  void h3State(Zhttp::H3::CxnState::T);
  void h3Error(uint64_t);
  bool peerControlStream();
  bool peerEncoderStream();
  bool peerDecoderStream();
  Zhttp::H3::QPackRxTable *qpackRx();
  bool qpackTxInsn(Zhttp::H3::QPackInsn::T, uint64_t);
  bool qpackTxMaxCapacity(uint64_t);

  RequestParser<true>	parser;
};

struct H3ServerLink :
  public Zquic::SrvLink<H3Server, H3ServerLink, H3ServerStream> {
  using Base = Zquic::SrvLink<H3Server, H3ServerLink, H3ServerStream>;
  using H3Cxn = Zhttp::H3::Cxn<H3ServerLink, ZmRef<Stream>>;

  H3ServerLink(H3Server *app) : Base{app} { }
  void connected(Zquic::Connected info) {
    if (!validQUICInfo(info)) app()->state->errors = 1;
    h3.link_ = this;
    ZiTxErrorFn txError = h3.txError;
    auto link = ZmMkRef(this);
    app()->txInvoke([link, txError = ZuMv(txError)]() mutable {
      auto limits = Zhttp::H3::Params{}.qpackLimits();
      bool ok = link->h3Tx.init(limits.txCapacity, limits.txSections);
      typename H3Cxn::LocalStreams streams;
      if (ok) streams = H3Cxn::openLocalStreams(*link, txError);
      link->app()->rxInvoke([
	link = ZuMv(link), streams = ZuMv(streams), ok
      ]() mutable {
	if (!ok || !link->h3.openLocal(*link, ZuMv(streams)))
	  link->app()->state->errors = 1;
      });
    });
  }
  void disconnected(bool) { }
  void streamed(ZmRef<Stream>) { }

  Zhttp::H3::QPackTxTable *qpackTx() { return &h3Tx; }

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  H3Cxn				h3;

  // Tx thread exclusive
  alignas(Zm::CacheLineSize)
  Zhttp::H3::QPackTxTable	h3Tx;
};

ZmRef<H3Server::Link> H3Server::accepted(const Zquic::InitialInfo &)
{
  link_ = new Link{this};
  return link_;
}

int H3ServerStream::process(Zquic::RxStream &rx)
{
  auto state = this->link()->app()->state;
  state->lastH3StreamID = uint64_t(this->id());
  if (Zquic::StreamID::uni(uint64_t(this->id()))) {
    ++state->h3UniStreams;
    auto s = this->CxnParser::process(*this);
    state->h3State = s;
    if (s == Zhttp::H3::CxnState::Error) {
      state->errors = 1;
      state->done.post();
      return -1;
    }
    (void)rx;
    return 0;
  }
  ++state->h3BidiStreams;
  parser.qpackRx_ = &this->link()->h3.qpackRxTable;
  using H3Cxn = ZuDecay<decltype(this->link()->h3)>;
  parser.qpackDecoder_ = &this->link()->h3;
  parser.qpackDecoderWrite_ = [](void *ptr, ZuBSpan span) {
    return static_cast<H3Cxn *>(ptr)->qpackDecoderWrite(span);
  };
  parser.streamID_ = uint64_t(this->id());
  auto s = parser.process(*this);
  state->h3ParserState = s;
  if (s == RequestParser<true>::State::Error ||
      s == RequestParser<true>::State::Cancelled) {
    state->errors = 1;
    state->done.post();
    return -1;
  }
  if (s == RequestParser<true>::State::Complete) {
    state->request = parser.seen;
    sendH3Response(*this, state->body);
    state->done.post();
    return 1;
  }
  (void)rx;
  return 0;
}

Zhttp::H3::CxnState::T H3ServerStream::h3State() const
{
  return this->link()->h3.state;
}

void H3ServerStream::h3State(Zhttp::H3::CxnState::T state)
{
  this->link()->h3.state = state;
}

void H3ServerStream::h3Error(uint64_t error)
{
  this->link()->app()->state->h3Error = error;
}

bool H3ServerStream::peerControlStream()
{
  return this->link()->h3.peerControlStream();
}

bool H3ServerStream::peerEncoderStream()
{
  return this->link()->h3.peerEncoderStream();
}

bool H3ServerStream::peerDecoderStream()
{
  return this->link()->h3.peerDecoderStream();
}

Zhttp::H3::QPackRxTable *H3ServerStream::qpackRx()
{
  return this->link()->h3.qpackRx();
}

bool H3ServerStream::qpackTxInsn(
  Zhttp::H3::QPackInsn::T type, uint64_t value)
{
  return this->link()->h3.qpackTxInsn(type, value);
}

bool H3ServerStream::qpackTxMaxCapacity(uint64_t capacity)
{
  return this->link()->h3.qpackTxMaxCapacity(capacity);
}

struct ClientState {
  ZmSemaphore	done;
  ZmSemaphore	disconnected;
  ResponseSeen	response;
  ZuCSpan	body{"zhttp-client"};
  ZmAtomic<unsigned> errors = 0;
};

template <bool H3, typename App, typename Parser, typename Stream>
int processResponse(App *app, Parser &parser, Stream &stream)
{
  using State = typename Parser::State;
  auto s = parser.process(stream);
  if (s == State::Error) {
    app->state->errors = 1;
    app->state->done.post();
    return -1;
  }
  if constexpr (H3) {
    if (s == State::Cancelled) {
      app->state->errors = 1;
      app->state->done.post();
      return -1;
    }
  }
  if (s == State::Complete) {
    app->state->response = parser.seen;
    app->state->done.post();
    return 1;
  }
  return 0;
}

template <typename App, typename Base>
struct H1ClientLinkOps : public Base {
  using Base::Base;

  H1ClientLinkOps(App *app) : Base{app} { }

  template <typename Connected>
  void connected(Connected) {
    sendH1Request(*this, this->app()->state->body);
  }
  void disconnected(bool) {
    auto state = this->app()->state;
    if (!state->response.complete) {
      auto s = parser.eof();
      using State = typename ResponseParser<false>::State;
      if (s == State::Complete)
	state->response = parser.seen;
      else
	state->errors = 1;
      state->done.post();
    }
    this->app()->rxRun([state]() { state->disconnected.post(); });
  }
  void connectFailed(bool) {
    this->app()->state->errors = 1;
    this->app()->state->done.post();
  }
  template <typename Rx>
  int process(Rx &rx) {
    return processResponse<false>(this->app(), parser, rx);
  }

  ResponseParser<false>	parser;
};

struct TCPClient : public Ztcp::Client<TCPClient> {
  struct Link :
    public H1ClientLinkOps<TCPClient, Ztcp::CliLink<TCPClient, Link>> {
    using Base =
      H1ClientLinkOps<TCPClient, Ztcp::CliLink<TCPClient, Link>>;
    using Base::Base;
  };
  TCPClient(ClientState *state_) : state{state_} { }
  unsigned reconnFreq() const { return 0; }
  ClientState *state = nullptr;
};

struct TLSClient : public Ztls::Client<TLSClient> {
  using RxBufAlloc = Ztls::RxBufAlloc<8<<10, 4<<20>;
  using TxBufAlloc = Ztls::TxBufAlloc<8<<10, 4<<20>;
  struct Link :
    public H1ClientLinkOps<TLSClient,
      Ztls::CliLink<TLSClient, Link, RxBufAlloc, TxBufAlloc>> {
    using Base = H1ClientLinkOps<TLSClient,
      Ztls::CliLink<TLSClient, Link, RxBufAlloc, TxBufAlloc>>;
    using Base::Base;
  };
  TLSClient(ClientState *state_) : state{state_} { }
  unsigned reconnFreq() const { return 0; }
  ClientState *state = nullptr;
};

struct H3Client : public Zquic::Client<H3Client> {
  struct Link;
  struct Stream;

  H3Client(ClientState *state_) : state{state_} { }
  unsigned reconnFreq() const { return 0; }
  uint64_t maxStreamsDuplex() const { return 8; }
  uint64_t maxStreamsSimplex() const { return 8; }

  ClientState	*state = nullptr;
  int64_t	responseStreamID = -1;
};

struct H3Client::Stream :
  public Zquic::CliStream<H3Client::Link, H3Client::Stream>,
  public Zhttp::H3::CxnParser<H3Client::Stream> {
  using Base = Zquic::CliStream<H3Client::Link, H3Client::Stream>;
  using CxnParser = Zhttp::H3::CxnParser<H3Client::Stream>;
  using Base::Base;

  int process(Zquic::RxStream &);
  Zhttp::H3::CxnState::T h3State() const;
  void h3State(Zhttp::H3::CxnState::T);
  bool peerControlStream();
  bool peerEncoderStream();
  bool peerDecoderStream();
  Zhttp::H3::QPackRxTable *qpackRx();
  bool qpackTxInsn(Zhttp::H3::QPackInsn::T, uint64_t);
  bool qpackTxMaxCapacity(uint64_t);

  ResponseParser<true>	parser;
};

struct H3Client::Link :
  public Zquic::CliLink<H3Client, H3Client::Link, H3Client::Stream> {
  using Base = Zquic::CliLink<H3Client, H3Client::Link, H3Client::Stream>;
  using H3Cxn = Zhttp::H3::Cxn<Link, ZmRef<Stream>>;

  Link(H3Client *app) : Base{app} { }

  void connected(Zquic::Connected info) {
    if (!validQUICInfo(info)) {
      app()->state->errors = 1;
      app()->state->done.post();
      return;
    }
    h3.link_ = this;
    ZiTxErrorFn txError = h3.txError;
    auto link = ZmMkRef(this);
    app()->txInvoke([link, txError = ZuMv(txError)]() mutable {
      auto limits = Zhttp::H3::Params{}.qpackLimits();
      bool ok = link->h3Tx.init(limits.txCapacity, limits.txSections);
      typename H3Cxn::LocalStreams streams;
      ZmRef<Stream> request;
      if (ok) streams = H3Cxn::openLocalStreams(*link, txError);
      if (ok && streams) request = link->stream(Zquic::StreamType::Duplex);
      link->app()->rxInvoke([
	link = ZuMv(link), streams = ZuMv(streams),
	request = ZuMv(request), ok
      ]() mutable {
	if (!ok || !request ||
	    !link->h3.openLocal(*link, ZuMv(streams))) {
	  link->app()->state->errors = 1;
	  link->app()->state->done.post();
	  return;
	}
	link->opened(ZuMv(request));
      });
    });
  }
  void opened(ZmRef<Stream> request_) {
    request = ZuMv(request_);
    app()->responseStreamID = request->id();
    sendH3Request(*request, app()->state->body);
  }
  void disconnected(bool) {
    auto state = app()->state;
    if (!state->response.complete)
      state->errors = 1;
    state->done.post();
  }
  void connectFailed(bool) {
    app()->state->errors = 1;
    app()->state->done.post();
  }
  void streamed(ZmRef<Stream>) { }

  Zhttp::H3::QPackTxTable *qpackTx() { return &h3Tx; }

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  ZmRef<Stream>			request;
  H3Cxn				h3;

  // Tx thread exclusive
  alignas(Zm::CacheLineSize)
  Zhttp::H3::QPackTxTable	h3Tx;
};

int H3Client::Stream::process(Zquic::RxStream &rx)
{
  if (Zquic::StreamID::uni(uint64_t(this->id()))) {
    auto s = this->CxnParser::process(*this);
    if (s == Zhttp::H3::CxnState::Error) {
      auto state = this->link()->app()->state;
      state->errors = 1;
      state->done.post();
      return -1;
    }
    (void)rx;
    return 0;
  }
  if (this->id() != this->link()->app()->responseStreamID) return 0;
  parser.qpackRx_ = &this->link()->h3.qpackRxTable;
  using H3Cxn = ZuDecay<decltype(this->link()->h3)>;
  parser.qpackDecoder_ = &this->link()->h3;
  parser.qpackDecoderWrite_ = [](void *ptr, ZuBSpan span) {
    return static_cast<H3Cxn *>(ptr)->qpackDecoderWrite(span);
  };
  parser.streamID_ = uint64_t(this->id());
  return processResponse<true>(this->link()->app(), parser, *this);
}

Zhttp::H3::CxnState::T H3Client::Stream::h3State() const
{
  return this->link()->h3.state;
}

void H3Client::Stream::h3State(Zhttp::H3::CxnState::T state)
{
  this->link()->h3.state = state;
}

bool H3Client::Stream::peerControlStream()
{
  return this->link()->h3.peerControlStream();
}

bool H3Client::Stream::peerEncoderStream()
{
  return this->link()->h3.peerEncoderStream();
}

bool H3Client::Stream::peerDecoderStream()
{
  return this->link()->h3.peerDecoderStream();
}

Zhttp::H3::QPackRxTable *H3Client::Stream::qpackRx()
{
  return this->link()->h3.qpackRx();
}

bool H3Client::Stream::qpackTxInsn(
  Zhttp::H3::QPackInsn::T type, uint64_t value)
{
  return this->link()->h3.qpackTxInsn(type, value);
}

bool H3Client::Stream::qpackTxMaxCapacity(uint64_t capacity)
{
  return this->link()->h3.qpackTxMaxCapacity(capacity);
}

bool waitDone(ZmSemaphore &sem)
{
  return sem.timedwait(Zm::now(15)) == 0;
}

void waitThread(ZiMultiplex &mx, unsigned thread)
{
  ZmSemaphore done;
  mx.invoke([&done]() { done.post(); }, thread);
  done.wait();
}

void waitDisconnect(ZiMultiplex &mx)
{
  waitThread(mx, mx.txThread());
  waitThread(mx, mx.rxThread());
  waitThread(mx, mx.txThread());
}

template <typename Server>
bool stopServer(Server &server)
{
  bool stopped = ZmBlock<bool>{}([&server](auto wake) mutable {
    server.stop([wake = ZuMv(wake)](bool ok) mutable {
      wake(ok);
    });
  });
  if (stopped) server.final();
  return stopped;
}

bool writeHttpCaddyfile(ZuCSpan path, unsigned port, ZuCSpan body)
{
  ZtString<> filePath;
  filePath << path;
  FILE *f = fopen(filePath.data(), "w");
  if (!f) return false;
  int n = fprintf(f,
	    "{\n"
	    "  admin off\n"
	    "}\n"
	    "http://127.0.0.1:%u {\n"
	    "  header Content-Length \"%lu\"\n"
	    "  respond %.*s \"%.*s\" 200\n"
	    "}\n",
	    port, body.length(), int(Path.length()), Path.data(),
	    int(body.length()), body.data());
  return n > 0 && !fclose(f);
}

bool waitHttpCaddyReady(unsigned port)
{
  return retry(100, 100, [port]() {
    ZtString<> cmd;
    cmd <<
      "curl --http1.1 --fail -sS --connect-timeout 1 --max-time 2 "
      "http://127.0.0.1:" << port << Path << " >/dev/null 2>&1";
    return systemOK(::system(cmd.data()));
  });
}

void testZhttpClientCaddyHttp()
{
  ZuTestScope(testZhttpClientCaddyHttp);

  TempDir temp;
  ZuCHECK(temp.init("ZhttpClientCaddyHttp"),
    "Zhttp client->Caddy HTTP temporary directory failed");
  unsigned port = loopbackPort();
  ZuCHECK(port, "Zhttp client->Caddy HTTP port allocation failed");
  if (!port) return;
  auto caddyfile = temp.pathOf("Caddyfile");
  ZuCHECK(writeHttpCaddyfile(caddyfile, port, "caddy-http-ok"),
    "Zhttp client->Caddy HTTP Caddyfile generation failed");
  CaddyProcess caddy;
  ZuCHECK(caddy.start(temp, caddyfile.cspan()),
    "Zhttp client->Caddy HTTP start failed");
  ZuCHECK(waitHttpCaddyReady(port),
    "Zhttp client->Caddy HTTP did not become ready");

  ZiMultiplex mx(mxParams());
  bool mxStarted = mx.start();
  ZuCHECK(mxStarted, "Zhttp client->Caddy HTTP multiplexer start failed");
  if (!mxStarted) return;
  ClientState state;
  state.body = "";
  TCPClient client{&state};
  ZuCHECK(client.init(Ztcp::ClientParams(&mx, "3", "4")),
    "Zhttp client->Caddy HTTP client init failed");
  ZuCHECK(client.start(), "Zhttp client->Caddy HTTP client start failed");
  ZmRef<TCPClient::Link> link = new TCPClient::Link{&client};
  link->connect("127.0.0.1", port);
  ZuCHECK(waitDone(state.done), "Zhttp client->Caddy HTTP timed out");
  ZuCHECK(!state.errors && state.response.complete &&
      state.response.status == 200,
    "Zhttp client->Caddy HTTP response mismatch");
  link->disconnect();
  ZuCHECK(waitDone(state.disconnected),
    "Zhttp client->Caddy HTTP disconnect timed out");
  link = nullptr;
  client.stop();
  client.final();
  mx.stop();
}

void testZhttpClientCaddyHttpsH1()
{
  ZuTestScope(testZhttpClientCaddyHttpsH1);

  TempDir temp;
  ZuCHECK(temp.init("ZhttpClientCaddyHttpsH1"),
    "Zhttp client->Caddy HTTPS/H1 temporary directory failed");
  ZtString<> certPath, keyPath;
  ZuCHECK(writeLocalhostCert(temp, certPath, keyPath),
    "Zhttp client->Caddy HTTPS/H1 certificate generation failed");
  unsigned port = loopbackPort();
  ZuCHECK(port, "Zhttp client->Caddy HTTPS/H1 port allocation failed");
  if (!port) return;
  auto caddyfile = temp.pathOf("Caddyfile");
  ZuCHECK(writeCaddyfile(
      caddyfile, port, certPath, keyPath, Path, "caddy-h1-ok"),
    "Zhttp client->Caddy HTTPS/H1 Caddyfile generation failed");
  CaddyProcess caddy;
  ZuCHECK(caddy.start(temp, caddyfile.cspan()),
    "Zhttp client->Caddy HTTPS/H1 start failed");
  ZuCHECK(waitCaddyReady(port, Path),
    "Zhttp client->Caddy HTTPS/H1 did not become ready");

  ZiMultiplex mx(mxParams());
  bool mxStarted = mx.start();
  ZuCHECK(mxStarted, "Zhttp client->Caddy HTTPS/H1 multiplexer start failed");
  if (!mxStarted) return;
  ClientState state;
  state.body = "";
  TLSClient client{&state};
  ZuCSpan alpn[] = { "http/1.1" };
  ZuCHECK(client.init(
      Ztls::ClientParams(&mx, "3", "4").caPath(certPath.cspan()).alpn(alpn)),
    "Zhttp client->Caddy HTTPS/H1 client init failed");
  ZuCHECK(client.start(), "Zhttp client->Caddy HTTPS/H1 client start failed");
  ZmRef<TLSClient::Link> link = new TLSClient::Link{&client};
  link->connect("localhost", port);
  ZuCHECK(waitDone(state.done), "Zhttp client->Caddy HTTPS/H1 timed out");
  ZuCHECK(!state.errors && state.response.complete &&
      state.response.status == 200 && state.response.body == "caddy-h1-ok",
    "Zhttp client->Caddy HTTPS/H1 response mismatch");
  link->disconnect();
  ZuCHECK(waitDone(state.disconnected),
    "Zhttp client->Caddy HTTPS/H1 disconnect timed out");
  link = nullptr;
  client.stop();
  client.final();
  mx.stop();
}

void testZhttpClientCaddyHttpsH3()
{
  ZuTestScope(testZhttpClientCaddyHttpsH3);

  TempDir temp;
  ZuCHECK(temp.init("ZhttpClientCaddyHttpsH3"),
    "Zhttp client->Caddy HTTPS/H3 temporary directory failed");
  ZtString<> certPath, keyPath;
  ZuCHECK(writeLocalhostCert(temp, certPath, keyPath),
    "Zhttp client->Caddy HTTPS/H3 certificate generation failed");
  unsigned port = loopbackPort();
  ZuCHECK(port, "Zhttp client->Caddy HTTPS/H3 port allocation failed");
  if (!port) return;
  auto caddyfile = temp.pathOf("Caddyfile");
  ZuCHECK(writeCaddyfile(
      caddyfile, port, certPath, keyPath, Path, "caddy-h3-ok"),
    "Zhttp client->Caddy HTTPS/H3 Caddyfile generation failed");
  CaddyProcess caddy;
  ZuCHECK(caddy.start(temp, caddyfile.cspan()),
    "Zhttp client->Caddy HTTPS/H3 start failed");
  ZuCHECK(waitCaddyReady(port, Path),
    "Zhttp client->Caddy HTTPS/H3 did not become ready");
  ZuCHECK(runCurlH3Retry(temp, port, Path, "caddy-h3-ok"),
    "Zhttp client->Caddy HTTPS/H3 did not become H3-ready");

  ZiMultiplex mx(mxParams(true));
  bool mxStarted = mx.start();
  ZuCHECK(mxStarted, "Zhttp client->Caddy HTTPS/H3 multiplexer start failed");
  if (!mxStarted) return;
  ClientState state;
  state.body = "";
  H3Client client{&state};
  ZuCSpan alpn[] = { "h3" };
  ZuCHECK(client.init(
      Zquic::ClientParams(&mx, "3", "4").caPath(certPath.cspan()).alpn(alpn)
	.maxData(32768).maxStreamData(8192)
	.maxStreamsDuplex(8).maxStreamsSimplex(8)),
    "Zhttp client->Caddy HTTPS/H3 client init failed");
  ZuCHECK(client.start(), "Zhttp client->Caddy HTTPS/H3 client start failed");
  ZmRef<H3Client::Link> link = new H3Client::Link{&client};
  link->connect(Zquic::Host{"localhost"}, port);
  ZuCHECK(waitDone(state.done), "Zhttp client->Caddy HTTPS/H3 timed out");
  if (!state.response.complete || state.errors)
    printFile("caddy log", caddy.logPath);
  ZuCHECK(!state.errors && state.response.complete &&
      state.response.status == 200 && state.response.body == "caddy-h3-ok",
    "Zhttp client->Caddy HTTPS/H3 response mismatch");
  ZmSemaphore disconnected;
  link->disconnect([&disconnected]() { disconnected.post(); });
  ZuCHECK(waitDone(disconnected),
    "Zhttp client->Caddy HTTPS/H3 disconnect timed out");
  waitDisconnect(mx);
  link = nullptr;
  client.stop();
  client.final();
  mx.stop();
}

bool curlHTTP(unsigned port, ZuCSpan expectedBody)
{
  TempDir temp;
  if (!temp.init("ZhttpCurlHTTP")) return false;
  auto body = temp.pathOf("body");
  auto version = temp.pathOf("version");
  auto err = temp.pathOf("curl.err");
  ZtString<> cmd;
  cmd <<
    "curl --http1.1 --fail -sS --connect-timeout 2 --max-time 5 "
    "-d curl-body -o " << body << " -w '%{http_version}' "
    "http://127.0.0.1:" << port << Path <<
    " >" << version << " 2>" << err;
  if (!systemOK(::system(cmd.data()))) {
    printFile("curl stderr", err);
    return false;
  }
  cmd.length(0);
  cmd << "grep -qx '" << expectedBody << "' " << body <<
    " && grep -qx '1.1' " << version;
  return systemOK(::system(cmd.data()));
}

bool curlHTTPSH1(unsigned port, ZuCSpan certPath, ZuCSpan expectedBody)
{
  TempDir temp;
  if (!temp.init("ZhttpCurlH1")) return false;
  auto body = temp.pathOf("body");
  auto version = temp.pathOf("version");
  auto err = temp.pathOf("curl.err");
  ZtString<> cmd;
  cmd <<
    "curl --http1.1 --cacert " << certPath <<
    " --fail -sS --connect-timeout 2 --max-time 5 "
    "--resolve localhost:" << port << ":127.0.0.1 "
    "-d curl-body -o " << body << " -w '%{http_version}' "
    "https://localhost:" << port << Path <<
    " >" << version << " 2>" << err;
  if (!systemOK(::system(cmd.data()))) {
    printFile("curl stderr", err);
    return false;
  }
  cmd.length(0);
  cmd << "grep -qx '" << expectedBody << "' " << body <<
    " && grep -qx '1.1' " << version;
  return systemOK(::system(cmd.data()));
}

void printH3ServerTest(ServerTest &state)
{
  std::cout <<
    "# h3 server state:"
    " errors=" << unsigned(state.errors) <<
    " requestComplete=" << state.request.complete <<
    " requestErrors=" << state.request.errors <<
    " method=" << int(state.request.method) <<
    " path=\"" << state.request.path << '"' <<
    " uniStreams=" << unsigned(state.h3UniStreams) <<
    " bidiStreams=" << unsigned(state.h3BidiStreams) <<
    " lastStreamID=" << uint64_t(state.lastH3StreamID) <<
    " cxnState=" << int(state.h3State) <<
    " parserState=" << int(state.h3ParserState) <<
    " h3Error=" << uint64_t(state.h3Error) <<
    '\n';
}

void testCurlZhttpHttpServer()
{
  ZuTestScope(testCurlZhttpHttpServer);

  ServerTest state;
  state.body = "server-http-ok";
  state.port = loopbackPort();
  ZuCHECK(state.port, "curl->Zhttp HTTP port allocation failed");
  if (!state.port) return;
  ZiMultiplex mx(mxParams());
  bool mxStarted = mx.start();
  ZuCHECK(mxStarted, "curl->Zhttp HTTP multiplexer start failed");
  if (!mxStarted) return;
  TCPServer server{&state};
  ZuCHECK(server.init(Ztcp::ServerParams(&mx, "3", "4")),
    "curl->Zhttp HTTP server init failed");
  ZuCHECK(server.start(), "curl->Zhttp HTTP server start failed");
  server.listen();
  ZuCHECK(waitDone(state.listening), "curl->Zhttp HTTP listen timed out");
  ZuCHECK(curlHTTP(state.port, state.body),
    "curl HTTP request to local Zhttp server failed");
  ZuCHECK(waitDone(state.done), "curl->Zhttp HTTP server timed out");
  ZuCHECK(!state.errors && state.request.complete &&
      state.request.method == Zhttp::Method::POST &&
      state.request.path == Path && state.request.body == "curl-body",
    "curl->Zhttp HTTP decoded request mismatch");
  ZuCHECK(stopServer(server), "Zhttp interop HTTP server stop failed");
  mx.stop();
}

void testCurlZhttpHttpsH1Server()
{
  ZuTestScope(testCurlZhttpHttpsH1Server);

  TempDir temp;
  ZuCHECK(temp.init("ZhttpCurlHttpsH1"),
    "curl->Zhttp HTTPS/H1 temporary directory failed");
  ZtString<> certPath, keyPath;
  ZuCHECK(writeLocalhostCert(temp, certPath, keyPath),
    "curl->Zhttp HTTPS/H1 certificate generation failed");
  ServerTest state;
  state.body = "server-h1-ok";
  state.port = loopbackPort();
  ZuCHECK(state.port, "curl->Zhttp HTTPS/H1 port allocation failed");
  if (!state.port) return;
  ZiMultiplex mx(mxParams());
  bool mxStarted = mx.start();
  ZuCHECK(mxStarted, "curl->Zhttp HTTPS/H1 multiplexer start failed");
  if (!mxStarted) return;
  TLSServer server{&state};
  ZuCSpan alpn[] = { "http/1.1" };
  ZuCHECK(server.init(
      Ztls::ServerParams(&mx, "3", "4")
	.certPath(certPath.cspan()).keyPath(keyPath.cspan()).alpn(alpn)),
    "curl->Zhttp HTTPS/H1 server init failed");
  ZuCHECK(server.start(), "curl->Zhttp HTTPS/H1 server start failed");
  server.listen();
  ZuCHECK(waitDone(state.listening), "curl->Zhttp HTTPS/H1 listen timed out");
  ZuCHECK(curlHTTPSH1(state.port, certPath.cspan(), state.body),
    "curl HTTPS/H1 request to local Zhttp server failed");
  ZuCHECK(waitDone(state.done), "curl->Zhttp HTTPS/H1 server timed out");
  ZuCHECK(!state.errors && state.request.complete &&
      state.request.method == Zhttp::Method::POST &&
      state.request.path == Path && state.request.body == "curl-body",
    "curl->Zhttp HTTPS/H1 decoded request mismatch");
  ZuCHECK(stopServer(server), "Zhttp interop TLS server stop failed");
  mx.stop();
}

void testCurlZhttpHttpsH3Server()
{
  ZuTestScope(testCurlZhttpHttpsH3Server);

  TempDir temp;
  ZuCHECK(temp.init("ZhttpCurlHttpsH3"),
    "curl->Zhttp HTTPS/H3 temporary directory failed");
  ZtString<> certPath, keyPath;
  ZuCHECK(writeLocalhostCert(temp, certPath, keyPath),
    "curl->Zhttp HTTPS/H3 certificate generation failed");
  ServerTest state;
  state.body = "server-h3-ok";
  ZiMultiplex mx(mxParams(true));
  bool mxStarted = mx.start();
  ZuCHECK(mxStarted, "curl->Zhttp HTTPS/H3 multiplexer start failed");
  if (!mxStarted) return;
  H3Server server{&state};
  ZuCSpan alpn[] = { "h3" };
  ZuCHECK(server.init(
      Zquic::ServerParams(&mx, "3", "4")
	.certPath(certPath.cspan()).keyPath(keyPath.cspan()).alpn(alpn)
	.maxData(32768).maxStreamData(8192)
	.maxStreamsDuplex(8).maxStreamsSimplex(8)),
    "curl->Zhttp HTTPS/H3 server init failed");
  ZuCHECK(server.start(), "curl->Zhttp HTTPS/H3 server listen failed");
  ZuCHECK(waitDone(state.listening),
    "curl->Zhttp HTTPS/H3 server did not become ready");
  ZuCHECK(runCurlH3Retry(temp, server.local().port(), Path, state.body),
    "curl HTTPS/H3 request to local Zhttp server failed");
  ZuCHECK(waitDone(state.done), "curl->Zhttp HTTPS/H3 server timed out");
  if (state.errors || !state.request.complete ||
      state.request.method != Zhttp::Method::GET || state.request.path != Path)
    printH3ServerTest(state);
  ZuCHECK(!state.errors && state.request.complete &&
      state.request.method == Zhttp::Method::GET &&
      state.request.path == Path,
    "curl->Zhttp HTTPS/H3 decoded request mismatch");
  server.stop();
  waitDisconnect(mx);
  server.clearLink();
  server.final();
  mx.stop();
}

void testInteropPrerequisites()
{
  ZuTestScope(testInteropPrerequisites);

  ZuCHECK(haveCaddy(), "caddy is required for Zhttp interop tests");
  ZuCHECK(haveCurlH3(),
    "curl with HTTP3/ngtcp2/nghttp3 is required for Zhttp H3 interop tests");
}

} // namespace Zhttp3InteropTest_

using namespace Zhttp3InteropTest_;

int main(int argc, char **argv)
{
  parseArgs(argc, argv);
  ZuTestMain();
  ZuTestCall(testInteropPrerequisites);
  ZuTestCall(testZhttpClientCaddyHttp);
  ZuTestCall(testZhttpClientCaddyHttpsH1);
  ZuTestCall(testCurlZhttpHttpServer);
  ZuTestCall(testCurlZhttpHttpsH1Server);
  ZuTestCall(testCurlZhttpHttpsH3Server);
  ZuTestCall(testZhttpClientCaddyHttpsH3);
  ZiResolver::stop();
  ZiResolver::final();
  ZiLog::stop();
}
