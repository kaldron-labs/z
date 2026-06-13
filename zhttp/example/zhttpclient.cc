//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// basic test HTTP client

#include <iostream>
#include <string.h>

#include <zlib/ZuLib.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZtCLI.hh>
#include <zlib/Ztcp.hh>
#include <zlib/Ztls.hh>
#include <zlib/Zquic.hh>

#include <zlib/Zhttp.hh>

struct Options {
 ZuCSpan	ca;
 ZuCSpan	output{"index.html"};
 ZuCSpan	url;
 bool		http3 = false;
 bool		http3Only = false;
 bool		help = false;
};

ZtStruct((Options, CLI),
  (((ca),        (CLI::Opt<'c'>,  CLI::Long<"ca">)),         (String)),
  (((output),    (CLI::Opt<'o'>,  CLI::Long<"output">)),     (String, "index.html")),
  (((http3),     (CLI::Flag<1>,   CLI::Long<"http3">)),      (Bool)),
  (((http3Only), (CLI::Flag<2>,   CLI::Long<"http3-only">)), (Bool)),
  (((url),       (CLI::Arg<1>)),                             (String)),
  (((help),      (CLI::Flag<'h'>, CLI::Long<"help">)),       (Bool)));

void usage(int code = 1)
{
  std::cerr <<
    "Usage: zhttpclient [OPTION]... URL\n\n"
    "Options:\n"
    "  -c, --ca=PATH       CA path for https:\n"
    "  -o, --output=PATH   response body output path\n"
    "      --http3         try HTTP/3, fall back to HTTP/1.1\n"
    "      --http3-only    force HTTP/3, do not fall back\n"
    "  -h, --help          show help\n" << std::flush;
  ::exit(code);
}

using RequestHeaders = ZhttpHeaders(
  "user-agent",
  "accept");
using ResponseHeaders = ZhttpHeaders(
  "alt-svc",
  "content-length",
  "content-type",
  "location",
  "server");

struct URL {
  ZuCSpan	scheme;
  ZtString<>	host;
  Zi::Hostname	dnsHost;
  uint16_t	port = 0;
  ZtString<>	target;
};

namespace Protocol {
  ZtEnum(Protocol, int8_t, H1, H3);
}

constexpr unsigned ClientTimeout = 10;
constexpr unsigned MaxRedirects = 8;

struct State {
  URL		url;
  Options	options;
  Protocol::T	protocol = Protocol::H1;
  Zhttp::H3::CxnState::T h3State = Zhttp::H3::CxnState::Init;
  ZtString<>	altSvcHost;
  uint16_t	altSvcPort = 0;
  ZtString<>	location;
  int64_t	responseStreamID = -1;
  unsigned	status = 0;
  ZiFile	bodyFile;
  int64_t	contentLength = -1;
  uint64_t	bodyBytes = 0;
  unsigned	bodyChunks = 0;
  bool		bodyFileOpen = false;
  bool		chunked = false;
  bool		altSvcH3 = false;
  bool		redirect = false;
  bool		framingLogged = false;
  bool		done = false;
  bool		failed = false;
};

struct AltSvcEndpoint {
  ZtString<>	host;
  Zi::Hostname	dnsHost;
  uint16_t	port = 0;
  bool		h3 = false;
};

struct RequestResult {
  unsigned	status = 0;
  ZtString<>	location;
  AltSvcEndpoint altSvc;
};

void setHost(URL &url, ZuCSpan host)
{
  url.host = host;
#ifndef _WIN32
  url.dnsHost = host;
#else
  url.dnsHost.length(ZuUTF<wchar_t, char>::cvt(
    ZuSpan<wchar_t>(url.dnsHost.data(), url.dnsHost.size() - 1), host));
#endif
}

bool parseURL(ZuCSpan input, URL &url, ZeException *error = nullptr)
{
  auto fail = [error](ZuCSpan msg) {
    if (error) *error = ZeEXCEPT(Error, "Zhttp", msg);
    return false;
  };

  ZuCSpan rest;
  if (input.starts("http://")) {
    url.scheme = "http";
    url.port = 80;
    rest = input;
    rest.offset(7);
  } else if (input.starts("https://")) {
    url.scheme = "https";
    url.port = 443;
    rest = input;
    rest.offset(8);
  } else
    return fail("unsupported URL scheme");

  auto slash = rest.find([](auto c) { return c == '/'; });
  if (slash < 0) slash = rest.length();

  ZuCSpan authority = rest;
  authority.trunc(slash);
  ZuCSpan target = rest;
  target.offset(slash);
  if (!target) target = "/";

  if (!authority) return fail("missing URL host");
  for (unsigned i = 0; i < authority.length(); ++i) {
    if (authority[i] != ':') continue;
    ZuCSpan host = authority;
    host.trunc(i);
    ZuCSpan port = authority;
    port.offset(i + 1);
    if (!host || !port) return fail("invalid URL authority");
    unsigned p = ZuBox<unsigned>(port);
    if (!p || p > 65535) return fail("invalid URL port");
    setHost(url, host);
    url.port = p;
    url.target = target;
    return true;
  }

  setHost(url, authority);
  url.target = target;
  return true;
}

void splitTarget(ZuCSpan target, ZuCSpan &path, ZuCSpan &query)
{
  path = target;
  query = {};
  if (auto i = target.find([](auto c) { return c == '?'; }); i >= 0) {
    path.trunc(i);
    query = target;
    query.offset(i + 1);
  }
  if (!path) path = "/";
}

bool parsePort(ZuCSpan s, uint16_t &port)
{
  unsigned p = ZuBox<unsigned>(s);
  if (!p || p > 65535) return false;
  port = p;
  return true;
}

bool parseAltSvc(State &state, ZuCSpan value)
{
  if (value.starts("clear")) return false;
  int h = -1;
  for (unsigned i = 0; i + 3 <= value.length(); ++i) {
    if (value[i] == 'h' && value[i + 1] == '3' && value[i + 2] == '=') {
      h = i;
      break;
    }
  }
  if (h < 0) return false;
  ZuCSpan rest = value;
  rest.offset(h + 3);
  if (!rest || rest[0] != '"') return false;
  rest.offset(1);
  int q = rest.find([](auto c) { return c == '"'; });
  if (q < 0) return false;
  ZuCSpan authority = rest;
  authority.trunc(q);
  if (!authority) return false;

  ZuCSpan host = state.url.host;
  uint16_t port = state.url.port;
  if (authority[0] == ':') {
    ZuCSpan port_ = authority;
    port_.offset(1);
    if (!parsePort(port_, port)) return false;
  } else {
    host = authority;
    if (auto i = authority.find([](auto c) { return c == ':'; }); i >= 0) {
      host = authority;
      host.trunc(i);
      ZuCSpan port_ = authority;
      port_.offset(i + 1);
      if (!host || !parsePort(port_, port)) return false;
    }
  }

  state.altSvcHost = host;
  state.altSvcPort = port;
  state.altSvcH3 = true;
  return true;
}

bool redirectStatus(unsigned status)
{
  return status == 301 || status == 302 || status == 303 ||
    status == 307 || status == 308;
}

bool parseLocation(const URL &base, ZuCSpan location, URL &url)
{
  ZeException error;
  if (location.starts("http://") || location.starts("https://"))
    return parseURL(location, url, &error);
  url = base;
  if (location.starts("//")) {
    ZtString<> absolute;
    absolute << base.scheme << ':' << location;
    return parseURL(absolute, url, &error);
  }
  if (location.starts("/")) {
    url.target = location;
    return true;
  }

  ZuCSpan target{base.target};
  auto q = target.find([](auto c) { return c == '?'; });
  if (q >= 0) target.trunc(q);
  int slash = -1;
  for (unsigned i = 0; i < target.length(); ++i)
    if (target[i] == '/') slash = i;
  ZtString<> next;
  if (slash >= 0) {
    ZuCSpan dir = target;
    dir.trunc(slash + 1);
    next << dir;
  } else
    next << '/';
  next << location;
  url.target = next;
  return true;
}

struct RequestOps {
  RequestOps(const State &state_) : state{&state_} { }

  template <typename L>
  void operation(L &&l) {
    ZuCSpan path;
    ZuCSpan query;
    splitTarget(state->url.target, path, query);
    l(Zhttp::Method::GET, path, query);
  }
  template <typename L>
  void host(L &&l) { l(ZuCSpan{state->url.host}); }
  template <typename Key, typename L>
  void header(L &&l) {
    if constexpr (ZuIsSame<Key, ZuStringT<"user-agent">>{})
      l("zhttpclient/1.0");
    else if constexpr (ZuIsSame<Key, ZuStringT<"accept">>{})
      l("*/*");
    else
      l("");
  }

  const State *state = nullptr;
};

template <template <typename, typename> typename Builder>
struct RequestBuilder :
  public Builder<RequestBuilder<Builder>, RequestHeaders>,
  public RequestOps
{
  using Base = Builder<RequestBuilder<Builder>, RequestHeaders>;

  RequestBuilder(const State &state_) : RequestOps{state_} { }

  Zhttp::H3::QPackTxTable *qpackTx() const { return qpackTx_; }
  Zhttp::H3::QPackEncoderTx *qpackEncoderTx() const { return qpackEncoderTx_; }
  uint64_t streamID() const { return streamID_; }

  using RequestOps::host;
  using RequestOps::header;
  using RequestOps::operation;

  Zhttp::H3::QPackTxTable	*qpackTx_ = nullptr;
  Zhttp::H3::QPackEncoderTx	*qpackEncoderTx_ = nullptr;
  uint64_t			streamID_ = 0;
};
template <typename Impl, typename Headers>
using H1RequestBuilder_ = Zhttp::H1::Builder<Impl, Headers>;
using H1RequestBuilder = RequestBuilder<H1RequestBuilder_>;
template <typename Impl, typename Headers>
using H3RequestBuilder_ = Zhttp::H3::Builder<Impl, Headers>;
using H3RequestBuilder = RequestBuilder<H3RequestBuilder_>;

template <typename StreamRef>
void sendH1Request(State &state, StreamRef stream)
{
  auto tx = stream->txStream();
  H1RequestBuilder builder{state};
  builder.request(tx);
  builder.finish(tx);
  tx << Zi::flush();
}

template <typename StreamRef>
void sendH3Request(State &state, StreamRef stream)
{
  using Scratch = ZtArray<char, ZtArrayHeapID<"ZhttpClient.H3Request">>;
  struct Tx {
    void operator <<(char c) { bytes->push(c); }
    void operator <<(ZuBSpan span) {
      for (unsigned i = 0; i < span.length(); ++i) bytes->push(char(span[i]));
    }
    void flush() { }
    Scratch	*bytes = nullptr;
  };
  auto bytes = ZtLocalArray(Scratch, 2048);
  Tx tx{&bytes};
  H3RequestBuilder builder{state};
  builder.qpackTx_ = &stream->link()->qpackTxTable;
  builder.qpackEncoderTx_ = &stream->link()->qpackEncoder;
  builder.streamID_ = uint64_t(stream->id());
  builder.request(tx);
  builder.finish(tx);
  stream->link()->send(stream, bytes, true);
}

void logFraming(State &state)
{
  if (state.framingLogged) return;
  std::cerr << "framing: ";
  if (state.chunked)
    std::cerr << "chunked";
  else if (state.contentLength >= 0)
    std::cerr << "content-length=" << state.contentLength;
  else
    std::cerr << "no content-length";
  std::cerr << '\n' << std::flush;
  state.framingLogged = true;
}

template <typename Link>
struct ResponseSink {
  ResponseSink(Link *link_, State *state_) : link{link_}, state{state_} { }

  void status(unsigned status) {
    state->status = status;
    state->redirect = redirectStatus(status);
    std::cerr << "status: " << status << '\n' << std::flush;
  }
  void contentLength(uint64_t contentLength) {
    state->contentLength = contentLength;
  }
  void chunked() { state->chunked = true; }

  template <typename Key>
  void header(ZuBSpan value) {
    if constexpr (ZuIsSame<Key, ZuStringT<"alt-svc">>{}) {
      if (parseAltSvc(*state, ZuCSpan(value)))
	std::cerr << "alt-svc: h3=\"" << state->altSvcHost << ':' <<
	  state->altSvcPort << "\"\n" << std::flush;
    } else if constexpr (ZuIsSame<Key, ZuStringT<"location">>{}) {
      state->location = ZuCSpan(value);
    }
    std::cerr << "header " << Key{}() << ": " << ZuCSpan(value) <<
      '\n' << std::flush;
  }

  void body(ZuBSpan span) {
    logFraming(*state);
    if (!span || state->redirect) return;
    if (!state->bodyFileOpen) {
      state->bodyFile =
	ZiFile(state->options.output, ZiFile::Write | ZiFile::GC);
      if (!state->bodyFile) {
	std::cerr << "failed to open " << state->options.output << '\n' <<
	  std::flush;
	state->failed = true;
	state->done = true;
	return;
      }
      state->bodyFileOpen = true;
    }
    if (state->bodyFile.write(span.data(), span.length()) != Zi::OK) {
      std::cerr << "failed to write body chunk\n" << std::flush;
      state->failed = true;
      state->done = true;
      return;
    }
    state->bodyBytes += span.length();
    ++state->bodyChunks;
  }

  template <typename ParserState>
  void complete(typename ParserState::T parserState) {
    if (state->done) return;
    if (parserState == ParserState::Complete)
      std::cerr << "body complete: " << state->bodyBytes << " bytes in " <<
	state->bodyChunks << " chunks\n" << std::flush;
    else
      std::cerr << "response " << parserState << '\n' << std::flush;
    if (parserState != ParserState::Complete) state->failed = true;
    state->done = true;
    if (state->protocol == Protocol::H3) link->disconnect();
  }

  Link		*link = nullptr;
  State		*state = nullptr;
};

template <
  typename Link,
  template <typename, bool, typename, uint64_t> typename Parser>
struct ResponseParser :
  public Parser<ResponseParser<Link, Parser>, false, ResponseHeaders, (100<<20)>,
  public ResponseSink<Link> {
  using ParserBase =
    Parser<ResponseParser<Link, Parser>, false, ResponseHeaders, (100<<20)>;
  using SinkBase = ResponseSink<Link>;
  using State = typename ParserBase::State;

  ResponseParser(Link *link_, ::State *state_) : SinkBase{link_, state_} { }

  Zhttp::H3::QPackRxTable *qpackRx() const {
    return &this->link->qpackRxTable;
  }
  Zhttp::H3::QPackEncoderTx *qpackDecoderTx() const {
    return &this->link->qpackDecoder;
  }
  uint64_t streamID() const {
    return uint64_t(this->link->app()->state.responseStreamID);
  }
  void complete(typename State::T state) {
    SinkBase::template complete<State>(state);
  }

  using SinkBase::body;
  using SinkBase::chunked;
  using SinkBase::contentLength;
  using SinkBase::header;
  using SinkBase::status;
};

template <typename Impl, bool Request, typename Headers, uint64_t MaxBody>
using H1ResponseParser_ =
  Zhttp::H1::Parser<Impl, Request, Headers, MaxBody>;
template <typename Link>
using H1ResponseParser = ResponseParser<Link, H1ResponseParser_>;

template <typename Impl, bool Request, typename Headers, uint64_t MaxBody>
using H3ResponseParser_ =
  Zhttp::H3::Parser<Impl, Request, Headers, MaxBody>;
template <typename Link>
using H3ResponseParser = ResponseParser<Link, H3ResponseParser_>;

template <typename Link, bool H3> struct ResponseParser_;
template <typename Link> struct ResponseParser_<Link, false> {
  using T = H1ResponseParser<Link>;
};
template <typename Link> struct ResponseParser_<Link, true> {
  using T = H3ResponseParser<Link>;
};

template <bool H3, typename Link, typename Stream>
int processResponse(Link &link, State &state, Stream &rx)
{
  using Parser = typename ResponseParser_<Link, H3>::T;
  if (!link.parser) link.parser = new Parser{&link, &state};
  auto parserState = link.parser->process(rx);
  if (state.done) return -1;
  if (parserState == Parser::State::Error) return -1;
  if (parserState != Parser::State::Complete) return 0;
  return 1;
}

ZuCSpan transportName(Zi::Transport::T transport)
{
  if (transport == Zi::Transport::TLS) return "TLS";
  if (transport == Zi::Transport::QUIC) return "QUIC";
  return "TCP";
}

void logConnected(const State &state, Zi::Connected info)
{
  std::cerr << transportName(info.transport) << " connected (hostname: " <<
    state.url.host;
  if (info.version) std::cerr << " version: " << info.version;
  if (info.alpn) std::cerr << " ALPN: " << info.alpn;
  std::cerr << ")\n" << std::flush;
}

template <typename Link>
bool openH3LocalStreams(Link &link, State &state)
{
  link.control = link.stream(Zi::StreamType::Simplex);
  link.enc = link.stream(Zi::StreamType::Simplex);
  link.dec = link.stream(Zi::StreamType::Simplex);
  if (!link.control || !link.enc || !link.dec) return false;
  using Scratch = ZtArray<char, ZtArrayHeapID<"ZhttpClient.H3Control">>;
  auto payload = ZtLocalArray(Scratch, 64);
  Zhttp::H3::CountBytes count;
  if (Zhttp::H3::putVar(count, 0x01) < 0 ||
      Zhttp::H3::putVar(count, 0) < 0 ||
      Zhttp::H3::putVar(count, 0x06) < 0 ||
      Zhttp::H3::putVar(count, 65536) < 0 ||
      Zhttp::H3::putVar(count, 0x07) < 0 ||
      Zhttp::H3::putVar(count, 0) < 0)
    return false;
  if (Zhttp::H3::putVar(payload, 0x00) < 0 ||
      Zhttp::H3::putVar(payload, 0x04) < 0 ||
      Zhttp::H3::putVar(payload, count.length()) < 0 ||
      Zhttp::H3::putVar(payload, 0x01) < 0 ||
      Zhttp::H3::putVar(payload, 0) < 0 ||
      Zhttp::H3::putVar(payload, 0x06) < 0 ||
      Zhttp::H3::putVar(payload, 65536) < 0 ||
      Zhttp::H3::putVar(payload, 0x07) < 0 ||
      Zhttp::H3::putVar(payload, 0) < 0)
    return false;
  if (!link.send(link.control, payload, false))
    return false;
  {
    auto tx = link.enc->txStream();
    Zhttp::H3::TxBytes out{tx};
    if (Zhttp::H3::putVar(out, 0x02) < 0) return false;
    tx.flush();
  }
  {
    auto tx = link.dec->txStream();
    Zhttp::H3::TxBytes out{tx};
    if (Zhttp::H3::putVar(out, 0x03) < 0) return false;
    tx.flush();
  }
  state.h3State = Zhttp::H3::CxnState::LocalControlOpen;
  state.h3State = Zhttp::H3::CxnState::Ready;
  return true;
}

template <typename App_, typename Base_, bool H3_ = false>
struct CliLink : public Base_ {
  using App = App_;
  using Base = Base_;
  enum { H3 = H3_ };
  using Parser = typename ResponseParser_<CliLink, H3>::T;

  struct QPackStreamTx : public Zhttp::H3::QPackEncoderTx {
    using StreamRef = typename Base::StreamRef;

    QPackStreamTx(CliLink *link_, StreamRef *stream_) :
      link{link_}, stream{stream_} { }

    bool write(ZuBSpan span) override {
      if constexpr (H3) {
	if (!link || !stream || !*stream || !span) return false;
	return link->send(*stream, span, false);
      } else {
	return false;
      }
    }

    CliLink	*link = nullptr;
    StreamRef	*stream = nullptr;
  };

  CliLink(App *app) :
    Base{app}, qpackEncoder{this, &enc}, qpackDecoder{this, &dec} { }
  ~CliLink() { delete parser; }

  void connected(Zi::Connected info) {
    logConnected(this->app()->state, info);
    typename Base::StreamRef stream;
    if constexpr (H3) {
      if (!openH3LocalStreams(*this, this->app()->state)) {
	this->app()->state.failed = true;
	this->app()->done();
	return;
      }
      stream = this->stream(Zi::StreamType::Duplex);
      this->app()->state.responseStreamID = stream ? stream->id() : -1;
    } else
      stream = this->stream();
    if (!stream) {
      this->app()->state.failed = true;
      this->app()->done();
      return;
    }
    if constexpr (H3)
      sendH3Request(this->app()->state, stream);
    else
      sendH1Request(this->app()->state, stream);
  }
  void disconnected() {
    std::cerr << "disconnected\n" << std::flush;
    if (!this->app()->state.done) this->app()->state.failed = true;
    this->app()->done();
  }
  void connectFailed(bool transient) {
    std::cerr << "failed to connect" << (transient ? " (transient)" : "") <<
      '\n' << std::flush;
    this->app()->state.failed = true;
    this->app()->done();
  }
  template <typename Rx>
  int process(Rx &rx) {
    return processResponse<H3>(*this, this->app()->state, rx);
  }

  Parser			*parser = nullptr;
  typename Base::StreamRef	control;
  typename Base::StreamRef	enc;
  typename Base::StreamRef	dec;
  bool				peerControl = false;
  bool				peerEncoder = false;
  bool				peerDecoder = false;
  Zhttp::H3::QPackRxTable	qpackRxTable;
  Zhttp::H3::QPackTxTable	qpackTxTable;
  QPackStreamTx			qpackEncoder;
  QPackStreamTx			qpackDecoder;
};

template <
  typename App,
  template <typename> class Client_,
  template <typename, typename, typename, typename> class Link__>
struct Client : public Client_<App> {
  auto impl() const { return static_cast<const App *>(this); }
  auto impl() { return static_cast<App *>(this); }

  using RxBufAlloc = Ztcp::RxBufAlloc<8<<10, 100<<20, "Zhttp.Buf">;
  using TxBufAlloc = Ztcp::TxBufAlloc<8<<10, 100<<20, "Zhttp.Buf">;
  template <typename Impl>
  using Link_ = Link__<App, Impl, RxBufAlloc, TxBufAlloc>;
  struct Link : public CliLink<App, Link_<Link>> {
    using Base = CliLink<App, Link_<Link>>;
    using Base::Base;
  };

  ZmSemaphore sem;
  State state;

  void done() { sem.post(); }
  unsigned reconnFreq() const { return 0; }
};

struct TCPClient : public Client<TCPClient, Ztcp::Client, Ztcp::CliLink> { };
struct TLSClient : public Client<TLSClient, Ztls::Client, Ztls::CliLink> { };

struct QUICClient : public Zquic::Client<QUICClient> {
  struct Link;
  struct Stream;

  ZmSemaphore sem;
  State state;

  void done() { sem.post(); }
  unsigned reconnFreq() const { return 0; }
  uint64_t maxStreamsBidi() const { return 16; }
  uint64_t maxStreamsUni() const { return 16; }
};

struct QUICClient::Stream :
  public Zquic::CliStream<QUICClient::Link, QUICClient::Stream>,
  public Zhttp::H3::CxnParser<QUICClient::Stream> {
  using Base = Zquic::CliStream<QUICClient::Link, QUICClient::Stream>;
  using CxnParser = Zhttp::H3::CxnParser<QUICClient::Stream>;
  using Base::Base;

  int process(Zquic::RxStream &);
  Zhttp::H3::CxnState::T h3State() const;
  void h3State(Zhttp::H3::CxnState::T);
  bool peerControlStream();
  bool peerEncoderStream();
  bool peerDecoderStream();
  Zhttp::H3::QPackRxTable *qpackRx();
  Zhttp::H3::QPackTxTable *qpackTx();
};

struct QUICClient::Link :
  public CliLink<QUICClient,
    Zquic::CliLink<QUICClient, QUICClient::Link, QUICClient::Stream>, true> {
  using Base = CliLink<QUICClient,
    Zquic::CliLink<QUICClient, QUICClient::Link, QUICClient::Stream>, true>;
  using Base::Base;

  void streamed(ZmRef<Stream>) { }
};

int QUICClient::Stream::process(Zquic::RxStream &)
{
  if (Zquic::StreamID::uni(uint64_t(this->id()))) {
    auto s = this->CxnParser::process(*this);
    if (s == Zhttp::H3::CxnState::Error) {
      this->link()->app()->done();
      return -1;
    }
    return 0;
  }
  if (this->id() != this->link()->app()->state.responseStreamID) return 0;
  return this->link()->process(*this);
}

Zhttp::H3::CxnState::T QUICClient::Stream::h3State() const
{
  return this->link()->app()->state.h3State;
}

void QUICClient::Stream::h3State(Zhttp::H3::CxnState::T state)
{
  this->link()->app()->state.h3State = state;
}

bool QUICClient::Stream::peerControlStream()
{
  auto link = this->link();
  if (link->peerControl) return false;
  link->peerControl = true;
  return true;
}

bool QUICClient::Stream::peerEncoderStream()
{
  auto link = this->link();
  if (link->peerEncoder) return false;
  link->peerEncoder = true;
  return true;
}

bool QUICClient::Stream::peerDecoderStream()
{
  auto link = this->link();
  if (link->peerDecoder) return false;
  link->peerDecoder = true;
  return true;
}

Zhttp::H3::QPackRxTable *QUICClient::Stream::qpackRx()
{
  return &this->link()->qpackRxTable;
}

Zhttp::H3::QPackTxTable *QUICClient::Stream::qpackTx()
{
  return &this->link()->qpackTxTable;
}

template <typename Client>
int run(
  ZiMultiplex &mx, const Options &options, URL url,
  RequestResult *result = nullptr) {
  Client client;
  client.state.url = ZuMv(url);
  client.state.options = options;

  if constexpr (Client::Transport == Zi::Transport::TCP) {
    client.state.protocol = Protocol::H1;
    if (!client.init(Ztcp::ClientParams(&mx, "3", "4"))) {
      std::cerr << "TCP client initialization failed\n" << std::flush;
      return 1;
    }
  } else if constexpr (Client::Transport == Zi::Transport::TLS) {
    client.state.protocol = Protocol::H1;
    ZuCSpan alpn[] = { "http/1.1" };
    if (!client.init(
	  Ztls::ClientParams(&mx, "3", "4").alpn(alpn).caPath(options.ca))) {
      std::cerr << "TLS client initialization failed\n" << std::flush;
      return 1;
    }
  } else if constexpr (Client::Transport == Zi::Transport::QUIC) {
    client.state.protocol = Protocol::H3;
    ZuCSpan alpn[] = { "h3" };
    if (!client.init(
	  Zquic::ClientParams(&mx, "3", "4")
	    .alpn(alpn)
	    .caPath(options.ca)
	    .maxData(100<<20)
	    .maxStreamData(16<<20)
	    .maxStreamsBidi(16)
	    .maxStreamsUni(16))) {
      std::cerr << "QUIC client initialization failed\n" << std::flush;
      return 1;
    }
  }

  {
    using Link = typename Client::Link;
    ZmRef<Link> link = new Link(&client);
    link->connect(client.state.url.host, client.state.url.port);
    if (client.sem.timedwait(Zm::now(ClientTimeout)) != 0) {
      std::cerr << "timed out\n" << std::flush;
      client.state.failed = true;
      link->disconnect();
      client.sem.timedwait(Zm::now(2));
    }
  }
  if (result) {
    result->status = client.state.status;
    result->location = client.state.location;
    if (client.state.altSvcH3) {
      result->altSvc.host = client.state.altSvcHost;
      result->altSvc.dnsHost = client.state.url.dnsHost;
      result->altSvc.port = client.state.altSvcPort;
      result->altSvc.h3 = true;
    }
  }
  int rc = client.state.failed ? 1 : 0;
  client.final();
  return rc;
}

ZiMxParams mxParams()
{
  return ZiMxParams()
    .scheduler([](auto &s) {
      s.nThreads(4)
      .thread(1, [](auto &t) { t.isolated(1); })
      .thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); })
      .thread(4, [](auto &t) { t.isolated(1); }); })
    .rxThread(1).txThread(2);
}

bool resolveForQUIC(const URL &url)
{
  ZiIP ips[8];
  unsigned n = 0;
  ZeError e;
  int rc = Zi::resolve(url.dnsHost,
    ZmFn<bool(ZiIP)>{[&ips, &n](ZiIP ip) {
      for (unsigned i = 0; i < n; ++i)
	if (ips[i] == ip) return true;
      if (n < 8) ips[n++] = ip;
      return n < 8;
    }}, &e);
  if (rc != Zi::OK || !n) {
    std::cerr << "DNS resolution failed for " << url.host << '\n' <<
      std::flush;
    return false;
  }
  std::cerr << "DNS: " << url.host << " resolved to";
  for (unsigned i = 0; i < n; ++i) std::cerr << ' ' << ips[i];
  std::cerr << '\n' << std::flush;
  return true;
}

URL altSvcURL(const URL &url, const AltSvcEndpoint &altSvc)
{
  URL h3URL = url;
  if (altSvc.host) setHost(h3URL, altSvc.host);
  if (altSvc.port) h3URL.port = altSvc.port;
  return h3URL;
}

int runH3DNSFirst(
  ZiMultiplex &mx, const Options &options, const URL &url,
  RequestResult &result, bool fallback)
{
  URL h1URL = url;
  int rc = resolveForQUIC(url) ?
    run<QUICClient>(mx, options, url, &result) : 1;
  if (rc && fallback)
    rc = run<TLSClient>(mx, options, ZuMv(h1URL), &result);
  return rc;
}

int runH1AltSvcFirst(
  ZiMultiplex &mx, const Options &options, const URL &url,
  RequestResult &result)
{
  int rc = run<TLSClient>(mx, options, url, &result);
  if (rc || redirectStatus(result.status) || !result.altSvc.h3) return rc;

  RequestResult h3Result;
  URL h3URL = altSvcURL(url, result.altSvc);
  int h3rc = resolveForQUIC(h3URL) ?
    run<QUICClient>(mx, options, ZuMv(h3URL), &h3Result) : 1;
  if (!h3rc) {
    result = ZuMv(h3Result);
    return 0;
  }

  // Restore output if a failed H3 upgrade wrote a partial body.
  result = {};
  return run<TLSClient>(mx, options, url, &result);
}

int main(int argc, char **argv)
{
  Options options;
  argc = ZtCLI::load(options, argc, argv);
  if (options.help) usage(0);
  if (argc != 2) usage();

  URL url;
  ZeException error;
  if (!parseURL(options.url, url, &error)) {
    ZiLogEvent(ZuMv(error));
    usage();
  }
  if (options.http3Only) options.http3 = true;

  ZiLog::init("zhttpclient");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  ZiMultiplex mx(mxParams());
  if (!mx.start()) {
    std::cerr << "ZiMultiplex start failed\n" << std::flush;
    return 1;
  }

  int rc = 1;
  for (unsigned redirect = 0; redirect <= MaxRedirects; ++redirect) {
    RequestResult result;
    if (url.scheme == "http")
      rc = run<TCPClient>(mx, options, url, &result);
    else if (options.http3)
      rc = runH3DNSFirst(mx, options, url, result, !options.http3Only);
    else
      rc = runH1AltSvcFirst(mx, options, url, result);

    if (rc || !redirectStatus(result.status) || !result.location) break;
    URL next;
    if (!parseLocation(url, result.location, next)) {
      std::cerr << "invalid redirect location: " << result.location << '\n' <<
	std::flush;
      rc = 1;
      break;
    }
    std::cerr << "redirect: " << next.scheme << "://" << next.host <<
      next.target << '\n' << std::flush;
    url = ZuMv(next);
    if (redirect == MaxRedirects) {
      std::cerr << "too many redirects\n" << std::flush;
      rc = 1;
    }
  }

  mx.stop();
  ZiLog::stop();

  return rc;
}
