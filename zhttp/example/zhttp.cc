//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// basic test HTTP client

#include <iostream>
#include <string.h>

#include <zlib/ZuLib.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiResolver.hh>
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
 bool		verbose = false;
 bool		help = false;
};

ZtStruct((Options, CLI),
  (((ca),        (CLI::Opt<'c'>,  CLI::Long<"ca">)),         (String)),
  (((output),    (CLI::Opt<'o'>,  CLI::Long<"output">)),     (String, "index.html")),
  (((http3),     (CLI::Flag<1>,   CLI::Long<"http3">)),      (Bool)),
  (((verbose),   (CLI::Flag<'v'>, CLI::Long<"verbose">)),    (Bool)),
  (((url),       (CLI::Arg<1>)),                             (String)),
  (((help),      (CLI::Flag<'h'>, CLI::Long<"help">)),       (Bool)));

void usage(int code = 1)
{
  std::cerr <<
    "Usage: zhttp [OPTION]... URL\n\n"
    "Options:\n"
    "  -c, --ca=PATH       CA path for https:\n"
    "  -o, --output=PATH   response body output path\n"
    "      --http3         force HTTP/3 over QUIC for https:\n"
    "  -v, --verbose       show DNS and Alt-Svc probing\n"
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
constexpr uint64_t RespBodyMax = 100<<20;
constexpr unsigned BufBuiltin = 8<<10;
constexpr unsigned BufMax = 100<<20;
constexpr uint64_t H3DataMax = 100<<20;
constexpr uint64_t H3StreamDataMax = 16<<20;
constexpr uint64_t H3BidiMax = 16;
constexpr uint64_t H3UniMax = 16;

using H3CxnState = Zhttp::H3::CxnState;

struct State {
  URL		url;
  Options	options;
  Protocol::T	protocol = Protocol::H1;
  H3CxnState::T h3State = Zhttp::H3::CxnState::Init;
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
      l("zhttp/1.0");
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

  using RequestOps::host;
  using RequestOps::header;
  using RequestOps::operation;
};
template <typename Impl, typename Headers>
using H1RequestBuilder_ = Zhttp::H1ReqBuilder<Impl, Headers>;
using H1RequestBuilder = RequestBuilder<H1RequestBuilder_>;

template <typename H3Cxn_>
struct H3RequestBuilder :
  public Zhttp::H3ReqBuilder<H3RequestBuilder<H3Cxn_>, RequestHeaders>,
  public RequestOps
{
  using Base = Zhttp::H3ReqBuilder<H3RequestBuilder<H3Cxn_>, RequestHeaders>;
  using H3Cxn = H3Cxn_;

  H3RequestBuilder(const State &state_, H3Cxn &h3_, uint64_t streamID_) :
    RequestOps{state_}, h3{&h3_}, streamID_{streamID_} { }

  H3Cxn &h3Cxn() const { return *h3; }
  uint64_t streamID() const { return streamID_; }

  using RequestOps::host;
  using RequestOps::header;
  using RequestOps::operation;

  H3Cxn		*h3 = nullptr;
  uint64_t	streamID_ = 0;
};

template <typename StreamRef>
void sendH1Request(State &state, StreamRef stream)
{
  auto tx = stream->txStream();
  H1RequestBuilder builder{state};
  builder.request(tx);
  builder.finish(tx);
}

template <typename StreamRef>
void sendH3Request(State &state, StreamRef stream)
{
  auto tx = stream->txStream();
  using H3Cxn = ZuDecay<decltype(stream->link()->h3)>;
  H3RequestBuilder<H3Cxn> builder{
    state, stream->link()->h3, uint64_t(stream->id())};
  builder.request(tx);
  builder.finish(tx);
  stream->link()->send(stream, {}, true);
}

void logFraming(State &state)
{
  if (state.framingLogged) return;
  ZiLOG(Info, "zhttp.response", ([&state](auto &s) {
    s << "framing: ";
    if (state.chunked)
      s << "chunked";
    else if (state.contentLength >= 0)
      s << "content-length=" << state.contentLength;
    else
      s << "no content-length";
  }));
  state.framingLogged = true;
}

template <typename Link>
struct ResponseSink {
  ResponseSink(Link *link_, State *state_) : link{link_}, state{state_} { }

  void status(unsigned status) {
    state->status = status;
    state->redirect = redirectStatus(status);
    ZiLOG(Info, "zhttp.response", ([status](auto &s) {
      s << "status: " << status;
    }));
  }
  void contentLength(uint64_t contentLength) {
    state->contentLength = contentLength;
  }
  void chunked() { state->chunked = true; }

  template <typename Key>
  void header(ZuBSpan value) {
    if constexpr (ZuIsSame<Key, ZuStringT<"alt-svc">>{}) {
      if (parseAltSvc(*state, ZuCSpan(value)))
	ZiLOG(Info, "zhttp.response", ([state = state](auto &s) {
	  s << "alt-svc: h3=\"" << state->altSvcHost << ':' <<
	    state->altSvcPort << '"';
	}));
    } else if constexpr (ZuIsSame<Key, ZuStringT<"location">>{}) {
      state->location = ZuCSpan(value);
    }
    ZiLOG(Info, "zhttp.response", ([value](auto &s) {
      s << "header " << Key{}() << ": " << ZuCSpan(value);
    }));
  }

  void body(ZuBSpan span) {
    logFraming(*state);
    if (!span || state->redirect) return;
    if (!state->bodyFileOpen) {
      state->bodyFile =
	ZiFile(state->options.output, ZiFile::Write | ZiFile::GC);
      if (!state->bodyFile) {
	ZiLOG(Error, "zhttp", ([state = state](auto &s) {
	  s << "failed to open " << state->options.output;
	}));
	state->failed = true;
	state->done = true;
	return;
      }
      state->bodyFileOpen = true;
    }
    if (state->bodyFile.write(span.data(), span.length()) != Zi::OK) {
      ZiLOG(Error, "zhttp", "failed to write body chunk");
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
      ZiLOG(Info, "zhttp.response", ([state = state](auto &s) {
	s << "body complete: " << state->bodyBytes << " bytes in " <<
	  state->bodyChunks << " chunks";
      }));
    else
      ZiLOG(Error, "zhttp.response", ([parserState](auto &s) {
	s << "response " << parserState;
      }));
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
  public Parser<ResponseParser<Link, Parser>, false, ResponseHeaders, RespBodyMax>,
  public ResponseSink<Link> {
  using ParserBase =
    Parser<ResponseParser<Link, Parser>, false, ResponseHeaders, RespBodyMax>;
  using SinkBase = ResponseSink<Link>;
  using State = typename ParserBase::State;

  ResponseParser(Link *link_, ::State *state_) : SinkBase{link_, state_} { }

  auto &h3Cxn() const { return this->link->h3; }
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
  Zhttp::H1RespParser<Impl, Headers, MaxBody>;
template <typename Link>
using H1ResponseParser = ResponseParser<Link, H1ResponseParser_>;

template <typename Impl, bool Request, typename Headers, uint64_t MaxBody>
using H3ResponseParser_ =
  Zhttp::H3RespParser<Impl, Headers, MaxBody>;
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
  auto parserState = link.parser.process(rx);
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
  ZiLOG(Info, "zhttp", ([&state, info](auto &s) {
    s << transportName(info.transport) << " connected (hostname: " <<
      state.url.host;
    if (info.version) s << " version: " << info.version;
    if (info.alpn) s << " ALPN: " << info.alpn;
    s << ')';
  }));
}

template <typename App_, typename Base_, bool H3_ = false>
struct CliLink : public Base_ {
  using App = App_;
  using Base = Base_;
  enum { H3 = H3_ };
  using Parser = typename ResponseParser_<CliLink, H3>::T;

  using H3Cxn = Zhttp::H3::Cxn<CliLink, typename Base::StreamRef>;

  CliLink(App *app) : Base{app}, parser{this, &app->state} { }

  void connected(Zi::Connected info) {
    logConnected(this->app()->state, info);
    typename Base::StreamRef stream;
    if constexpr (H3) {
      if (!h3.openLocal(*this)) {
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
    ZiLOG(Info, "zhttp", "disconnected");
    if (!this->app()->state.done) this->app()->state.failed = true;
    this->app()->done();
  }
  void connectFailed(bool transient) {
    ZiLOG(Error, "zhttp", ([transient](auto &s) {
      s << "failed to connect";
      if (transient) s << " (transient)";
    }));
    this->app()->state.failed = true;
    this->app()->done();
  }
  template <typename Rx>
  int process(Rx &rx) {
    return processResponse<H3>(*this, this->app()->state, rx);
  }

  Parser		parser;
  H3Cxn			h3;
};

template <
  typename App,
  template <typename> class Client_,
  template <typename, typename, typename, typename> class Link__>
struct Client : public Client_<App> {
  auto impl() const { return static_cast<const App *>(this); }
  auto impl() { return static_cast<App *>(this); }

  using RxBufAlloc = Ztcp::RxBufAlloc<BufBuiltin, BufMax, "Zhttp.Buf">;
  using TxBufAlloc = Ztcp::TxBufAlloc<BufBuiltin, BufMax, "Zhttp.Buf">;
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
  uint64_t maxStreamsBidi() const { return H3BidiMax; }
  uint64_t maxStreamsUni() const { return H3UniMax; }
};

struct QUICClient::Stream :
  public Zquic::CliStream<QUICClient::Link, QUICClient::Stream>,
  public Zhttp::H3::CxnParser<QUICClient::Stream> {
  using Base = Zquic::CliStream<QUICClient::Link, QUICClient::Stream>;
  using CxnParser = Zhttp::H3::CxnParser<QUICClient::Stream>;
  using Base::Base;

  int process(Zquic::RxStream &);
  H3CxnState::T h3State() const;
  void h3State(H3CxnState::T);
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
    if (s == H3CxnState::Error) {
      this->link()->app()->done();
      return -1;
    }
    return 0;
  }
  if (this->id() != this->link()->app()->state.responseStreamID) return 0;
  return this->link()->process(*this);
}

H3CxnState::T QUICClient::Stream::h3State() const
{
  return this->link()->h3.state;
}

void QUICClient::Stream::h3State(H3CxnState::T state)
{
  this->link()->h3.state = state;
}

bool QUICClient::Stream::peerControlStream()
{
  return this->link()->h3.peerControlStream();
}

bool QUICClient::Stream::peerEncoderStream()
{
  return this->link()->h3.peerEncoderStream();
}

bool QUICClient::Stream::peerDecoderStream()
{
  return this->link()->h3.peerDecoderStream();
}

Zhttp::H3::QPackRxTable *QUICClient::Stream::qpackRx()
{
  return this->link()->h3.qpackRx();
}

Zhttp::H3::QPackTxTable *QUICClient::Stream::qpackTx()
{
  return this->link()->h3.qpackTx();
}

template <typename Client>
int run(
  ZiMultiplex &mx, const Options &options, URL url,
  RequestResult *result = nullptr)
{
  Client client;
  client.state.url = ZuMv(url);
  client.state.options = options;

  if constexpr (Client::Transport == Zi::Transport::TCP) {
    client.state.protocol = Protocol::H1;
    if (!client.init(Ztcp::ClientParams(&mx, "3", "4"))) {
      ZiLOG(Error, "zhttp", "TCP client initialization failed");
      return 1;
    }
  } else if constexpr (Client::Transport == Zi::Transport::TLS) {
    client.state.protocol = Protocol::H1;
    ZuCSpan alpn[] = { "http/1.1" };
    if (!client.init(
	  Ztls::ClientParams(&mx, "3", "4").alpn(alpn).caPath(options.ca))) {
      ZiLOG(Error, "zhttp", "TLS client initialization failed");
      return 1;
    }
  } else if constexpr (Client::Transport == Zi::Transport::QUIC) {
    client.state.protocol = Protocol::H3;
    ZuCSpan alpn[] = { "h3" };
    if (!client.init(
	  Zquic::ClientParams(&mx, "3", "4")
	    .alpn(alpn)
	    .caPath(options.ca)
	    .maxData(H3DataMax)
	    .maxStreamData(H3StreamDataMax)
	    .maxStreamsBidi(H3BidiMax)
	    .maxStreamsUni(H3UniMax))) {
      ZiLOG(Error, "zhttp", "QUIC client initialization failed");
      return 1;
    }
  }

  {
    using Link = typename Client::Link;
    ZmRef<Link> link = new Link(&client);
    link->connect(client.state.url.host, client.state.url.port);
    if (client.sem.timedwait(Zm::now(ClientTimeout)) != 0) {
      ZiLOG(Error, "zhttp", "timed out");
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

bool resolveH3(const URL &url, ZiResolver::H3Policy policy)
{
  ZiResolver::H3Endpoint eps[ZiResolver::H3MaxIPs];
  unsigned n = 0;
  bool advertised = false;
  ZeError e;
  int rc = ZiResolver::http3(url.dnsHost, url.dnsHost, url.port, policy,
    ZmFn<bool(const ZiResolver::H3Endpoint &)>{[&](const auto &ep) {
      for (unsigned i = 0; i < n; ++i)
	if (eps[i].ip == ep.ip && eps[i].port == ep.port) return true;
      advertised |= ep.fromHTTPS;
      if (n < ZiResolver::H3MaxIPs) eps[n++] = ep;
      return n < ZiResolver::H3MaxIPs;
    }}, &e);
  if (rc != Zi::OK || !n) {
    ZiLOG(Info, "zhttp", ([&url](auto &s) {
      s << "DNS did not advertise HTTP/3 for " << url.host;
    }));
    return false;
  }
  ZiLOG(Info, "zhttp", ([&url, &eps, n, advertised](auto &s) {
    s << "DNS: " << url.host <<
      (advertised ? " advertised HTTP/3 at" : " blind HTTP/3 probe at");
    for (unsigned i = 0; i < n; ++i)
      s << ' ' << eps[i].ip << ':' << ZuBoxed(eps[i].port);
  }));
  return true;
}

URL altSvcURL(const URL &url, const AltSvcEndpoint &altSvc)
{
  URL h3URL = url;
  if (altSvc.host) setHost(h3URL, altSvc.host);
  if (altSvc.port) h3URL.port = altSvc.port;
  return h3URL;
}

int runH3Direct(
  ZiMultiplex &mx, const Options &options, const URL &url,
  RequestResult &result)
{
  ZiLOG(Info, "zhttp", ([&url](auto &s) {
    s << "HTTP/3 direct: " << url.host << ':' << ZuBoxed(url.port);
  }));
  return run<QUICClient>(mx, options, url, &result);
}

int runH1AltSvcFirst(
  ZiMultiplex &mx, const Options &options, const URL &url,
  RequestResult &result)
{
  int rc = run<TLSClient>(mx, options, url, &result);
  if (rc || redirectStatus(result.status) || !result.altSvc.h3) return rc;

  RequestResult h3Result;
  URL h3URL = altSvcURL(url, result.altSvc);
  ZiLOG(Info, "zhttp", ([&h3URL](auto &s) {
    s << "Alt-Svc HTTP/3 probe: " << h3URL.host << ':' <<
      ZuBoxed(h3URL.port);
  }));
  int h3rc = run<QUICClient>(mx, options, ZuMv(h3URL), &h3Result);
  if (!h3rc) {
    result = ZuMv(h3Result);
    return 0;
  }

  // Restore output if a failed H3 upgrade wrote a partial body.
  result = {};
  return run<TLSClient>(mx, options, url, &result);
}

int runH3DNSAltSvcFallback(
  ZiMultiplex &mx, const Options &options, const URL &url,
  RequestResult &result)
{
  RequestResult h3Result;
  if (resolveH3(url, ZiResolver::H3Policy::DNSOnly) &&
      !run<QUICClient>(mx, options, url, &h3Result)) {
    result = ZuMv(h3Result);
    return 0;
  }

  result = {};
  return runH1AltSvcFirst(mx, options, url, result);
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

  ZiLog::init("zhttp");
  ZiLog::level(options.verbose ? Ze::Info : Ze::Warning);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  ZiMultiplex mx(mxParams());
  if (!mx.start()) {
    ZiLOG(Error, "zhttp", "ZiMultiplex start failed");
    return 1;
  }

  int rc = 1;
  for (unsigned redirect = 0; redirect <= MaxRedirects; ++redirect) {
    RequestResult result;
    if (url.scheme == "http")
      rc = run<TCPClient>(mx, options, url, &result);
    else if (options.http3)
      rc = runH3Direct(mx, options, url, result);
    else
      rc = runH3DNSAltSvcFallback(mx, options, url, result);

    if (rc || !redirectStatus(result.status) || !result.location) break;
    URL next;
    if (!parseLocation(url, result.location, next)) {
      ZiLOG(Error, "zhttp", ([&result](auto &s) {
	s << "invalid redirect location: " << result.location;
      }));
      rc = 1;
      break;
    }
    ZiLOG(Info, "zhttp", ([&next](auto &s) {
      s << "redirect: " << next.scheme << "://" << next.host << next.target;
    }));
    url = ZuMv(next);
    if (redirect == MaxRedirects) {
      ZiLOG(Error, "zhttp", "too many redirects");
      rc = 1;
    }
  }

  mx.stop();
  ZiLog::stop();

  return rc;
}
