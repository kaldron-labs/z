//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// basic test HTTP client

#include <iostream>
#include <string.h>

#include <zlib/ZuLib.hh>
#include <zlib/ZuICmp.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiResolver.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmLock.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZtCLI.hh>
#include <zlib/Ztcp.hh>
#include <zlib/Ztls.hh>
#include <zlib/Zquic.hh>

#include <zlib/Zhttp.hh>

struct Options {
 ZuCSpan	ca;
 ZuCSpan	output{"index.html"};
 uint32_t	requests = 1;
 uint32_t	concurrency = 1;
 ZuCSpan	url;
 bool		http3 = false;
 bool		verbose = false;
 bool		help = false;
};

ZtStruct((Options, CLI),
  (((ca),        (CLI::Opt<'c'>,  CLI::Long<"ca">)),         (String)),
  (((output),    (CLI::Opt<'o'>,  CLI::Long<"output">)),     (String, "index.html")),
  (((requests),  (CLI::Opt<'n'>,  CLI::Long<"requests">)),   (UInt32, 1)),
  (((concurrency), (CLI::Opt<'j'>, CLI::Long<"jobs">)),       (UInt32, 1)),
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
    "  -n, --requests=N    submit N GET requests, default 1\n"
    "  -j, --jobs=M        run up to M requests concurrently, default 1;\n"
    "                      valid only when N > 1; M must be <= N\n"
    "      --http3         force HTTP/3 over QUIC for https:\n"
    "  -v, --verbose       show DNS and Alt-Svc probing\n"
    "  -h, --help          show help\n\n"
    "For N > 1, response bodies are written to PATH.0, PATH.1, ...\n" <<
    std::flush;
  ::exit(code);
}

bool validateOptions(const Options &options, int argc)
{
  if (argc < 0 || argc != 2) return false;
  if (!options.requests || !options.concurrency) return false;
  if (options.concurrency > options.requests) return false;
  return true;
}

ZtString<> outputPath(ZuCSpan base, unsigned reqID, unsigned requests)
{
  ZtString<> path;
  path << base;
  if (requests > 1) path << '.' << ZuBoxed(reqID);
  return path;
}

using RequestHeaders = ZhttpHeaders(
  "user-agent",
  "accept");
using ResponseHeaders = ZhttpHeaders(
  "alt-svc",
  "connection",
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

struct Req {
  unsigned	id = 0;
  unsigned	requests = 1;
  URL		url;
  Protocol::T	protocol = Protocol::H1;
  H3CxnState::T h3State = Zhttp::H3::CxnState::Init;
  ZtString<>	output;
  ZtString<>	altSvcHost;
  uint16_t	altSvcPort = 0;
  ZtString<>	location;
  int64_t	responseStreamID = -1;
  unsigned	redirects = 0;
  unsigned	status = 0;
  ZiFile	bodyFile;
  int64_t	contentLength = -1;
  uint64_t	bodyBytes = 0;
  unsigned	bodyChunks = 0;
  bool		bodyFileOpen = false;
  bool		chunked = false;
  bool		connectionClose = false;
  bool		connectionKeepAlive = false;
  bool		http10 = false;
  bool		closeDelimited = false;
  bool		altSvcH3 = false;
  bool		redirecting = false;
  bool		framingLogged = false;
  bool		truncateOutput = false;
  bool		done = false;
  bool		failed = false;
};

using State = Req;

struct AltSvcEndpoint {
  ZtString<>	host;
  Zi::Hostname	dnsHost;
  uint16_t	port = 0;
  bool		h3 = false;
};

struct Origin {
  ZtString<>	scheme;
  ZtString<>	host;
  uint16_t	port = 0;

  bool equals(const Origin &o) const {
    return scheme == o.scheme && host == o.host && port == o.port;
  }
  friend bool operator ==(const Origin &l, const Origin &r) {
    return l.equals(r);
  }
  uint32_t hash() const {
    uint32_t h = ZuHash<ZtString<>>::hash(scheme);
    h ^= ZuHash<ZtString<>>::hash(host) + 0x9e3779b9U + (h<<6) + (h>>2);
    h ^= ZuHash<uint16_t>::hash(port) + 0x9e3779b9U + (h<<6) + (h>>2);
    return h;
  }
};

struct OriginDiscovery {
  bool		dnsChecked = false;
  bool		dnsH3 = false;
  AltSvcEndpoint dnsEndpoint;
  bool		altSvcChecked = false;
  bool		altSvcH3 = false;
  AltSvcEndpoint altSvcEndpoint;
};

using DiscoveryCache = ZmHashKV<Origin, OriginDiscovery,
  ZmHashHeapID<"Zhttp.Discovery">>;

struct Run {
  Options	options;
  URL		originalURL;
  ZtArray<Req, ZtArrayHeapID<"Zhttp.Req">> reqs;
  DiscoveryCache discovery;
  ZmSemaphore	done;
  unsigned	scheduled = 0;
  unsigned	active = 0;
  unsigned	complete = 0;
  unsigned	failed = 0;
  bool		fatal = false;
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

void closeBody(Req &req)
{
  if (req.bodyFileOpen) {
    req.bodyFile.close();
    req.bodyFileOpen = false;
  }
}

void resetAttempt(Req &req, bool truncateOutput)
{
  closeBody(req);
  req.altSvcHost.length(0);
  req.altSvcPort = 0;
  req.location.length(0);
  req.responseStreamID = -1;
  req.status = 0;
  req.contentLength = -1;
  req.bodyBytes = 0;
  req.bodyChunks = 0;
  req.chunked = false;
  req.connectionClose = false;
  req.connectionKeepAlive = false;
  req.http10 = false;
  req.closeDelimited = false;
  req.altSvcH3 = false;
  req.redirecting = false;
  req.framingLogged = false;
  req.truncateOutput = truncateOutput;
  req.done = false;
  req.failed = false;
}

template <typename S>
void reqLogPrefix(const Req &req, S &s)
{
  if (req.requests > 1) s << "req=" << req.id << ' ';
}

bool truncateOutputPath(Req &req)
{
  if (!req.truncateOutput) return true;
  ZiFile f{req.output, ZiFile::Write | ZiFile::GC};
  if (!f) {
    ZiLOG(Error, "zhttp", ([&req](auto &s) {
      reqLogPrefix(req, s);
      s << "failed to open " << req.output;
    }));
    req.failed = true;
    req.done = true;
    return false;
  }
  f.close();
  req.truncateOutput = false;
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
    reqLogPrefix(state, s);
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
  ResponseSink() = default;
  ResponseSink(Link *link_, State *state_) : link{link_}, state{state_} { }
  void bind(Link *link_, State *state_) {
    link = link_;
    state = state_;
  }

  void status(unsigned status) {
    state->status = status;
    state->redirecting = redirectStatus(status);
    if (!state->redirecting && !truncateOutputPath(*state)) return;
    ZiLOG(Info, "zhttp.response", ([state = state, status](auto &s) {
      reqLogPrefix(*state, s);
      s << "status: " << status;
    }));
  }
  void contentLength(uint64_t contentLength) {
    state->contentLength = contentLength;
  }
  void chunked() { state->chunked = true; }
  void version(ZuBSpan version) {
    state->http10 = ZuCSpan(version) == "HTTP/1.0";
  }

  template <typename Key>
  void header(ZuBSpan value) {
    if constexpr (ZuIsSame<Key, ZuStringT<"alt-svc">>{}) {
      if (parseAltSvc(*state, ZuCSpan(value)))
	ZiLOG(Info, "zhttp.response", ([state = state](auto &s) {
	  reqLogPrefix(*state, s);
	  s << "alt-svc: h3=\"" << state->altSvcHost << ':' <<
	    state->altSvcPort << '"';
	}));
    } else if constexpr (ZuIsSame<Key, ZuStringT<"connection">>{}) {
      if (ZuICmp<ZuCSpan>::equals(ZuCSpan(value), "close"))
	state->connectionClose = true;
      else if (ZuICmp<ZuCSpan>::equals(ZuCSpan(value), "keep-alive"))
	state->connectionKeepAlive = true;
    } else if constexpr (ZuIsSame<Key, ZuStringT<"location">>{}) {
      state->location = ZuCSpan(value);
    }
    ZiLOG(Info, "zhttp.response", ([state = state, value](auto &s) {
      reqLogPrefix(*state, s);
      s << "header " << Key{}() << ": " << ZuCSpan(value);
    }));
  }

  void body(ZuBSpan span) {
    logFraming(*state);
    if (!span || state->redirecting) return;
    if (!truncateOutputPath(*state)) return;
    if (!state->bodyFileOpen) {
      state->bodyFile = ZiFile(state->output, ZiFile::Write | ZiFile::GC);
      if (!state->bodyFile) {
	ZiLOG(Error, "zhttp", ([state = state](auto &s) {
	  reqLogPrefix(*state, s);
	  s << "failed to open " << state->output;
	}));
	state->failed = true;
	state->done = true;
	return;
      }
      state->bodyFileOpen = true;
    }
    if (state->bodyFile.write(span.data(), span.length()) != Zi::OK) {
      ZiLOG(Error, "zhttp", ([state = state](auto &s) {
	reqLogPrefix(*state, s);
	s << "failed to write body chunk";
      }));
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
	reqLogPrefix(*state, s);
	s << "body complete: " << state->bodyBytes << " bytes in " <<
	  state->bodyChunks << " chunks";
      }));
    else
      ZiLOG(Error, "zhttp.response", ([state = state, parserState](auto &s) {
	reqLogPrefix(*state, s);
	s << "response " << parserState;
      }));
    if (parserState != ParserState::Complete) state->failed = true;
    closeBody(*state);
    state->done = true;
    link->responseComplete(state, parserState == ParserState::Complete);
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

  ResponseParser() = default;
  ResponseParser(Link *link_, ::State *state_) : SinkBase{link_, state_} { }
  void bind(Link *link_, ::State *state_) { SinkBase::bind(link_, state_); }

  auto &h3Cxn() const { return this->link->h3; }
  uint64_t streamID() const {
    return uint64_t(this->state->responseStreamID);
  }
  void complete(typename State::T state) {
    SinkBase::template complete<State>(state);
  }

  using SinkBase::body;
  using SinkBase::chunked;
  using SinkBase::contentLength;
  using SinkBase::header;
  using SinkBase::status;
  using SinkBase::version;
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
  if (parserState == Parser::State::Error) return -1;
  if (parserState == Parser::State::Complete) return 1;
  if (state.done) return -1;
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
    if (!this->app()->state.done) {
      if constexpr (!H3)
	parser.eof();
      if (!this->app()->state.done) this->app()->state.failed = true;
    }
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
  void responseComplete(State *, bool) { this->disconnect(); }
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

template <typename Client>
bool h1TransportOK(const URL &url)
{
  if constexpr (Client::Transport == Zi::Transport::TCP)
    return url.scheme == "http";
  else
    return url.scheme == "https";
}

bool sameOrigin(const URL &a, const URL &b)
{
  return a.scheme == b.scheme && a.host == b.host && a.port == b.port;
}

Origin originOf(const URL &url)
{
  Origin origin;
  origin.scheme = url.scheme;
  origin.host = url.host;
  origin.port = url.port;
  return origin;
}

OriginDiscovery &discoveryFor(Run &run, const URL &url)
{
  Origin origin = originOf(url);
  if (auto node = run.discovery.find(origin)) return node->val();
  run.discovery.add(origin, OriginDiscovery{});
  return run.discovery.find(origin)->val();
}

template <typename App_, typename Base_>
struct H1PoolLink : public Base_ {
  using App = App_;
  using Base = Base_;
  using Parser = H1ResponseParser<H1PoolLink>;

  H1PoolLink(App *app_, unsigned id_) : Base{app_}, id{id_} {
    parser.bind(this, nullptr);
  }

  void assign(Req *req_) {
    req = req_;
    retryConnects = 0;
    if (!req) return;
    resetAttempt(*req, true);
    parser.bind(this, req);
    parser.reset();
  }
  void sendCurrent() {
    if (!req) return;
    parser.bind(this, req);
    parser.reset();
    sendH1Request(*req, this->stream());
  }
  void connected(Zi::Connected info) {
    connectedFlag = true;
    if (req) logConnected(*req, info);
    sendCurrent();
  }
  void disconnected() {
    connectedFlag = false;
    ZiLOG(Info, "zhttp", ([this](auto &s) {
      s << "worker=" << id << " disconnected";
    }));
    if (closing) {
      closing = false;
      if (req) this->connect(req->url.host, req->url.port);
      return;
    }
    if (stopping) {
      stopping = false;
      this->app()->workerStopped(this->impl());
      return;
    }
    if (req && !req->done) {
      if (!req->status && retryConnects < 1) {
	++retryConnects;
	parser.bind(this, req);
	parser.reset();
	this->connect(req->url.host, req->url.port);
	return;
      }
      req->closeDelimited = true;
      parser.eof();
      if (!req || req->done) return;
      req->failed = true;
      req->done = true;
      responseComplete(req, false);
    }
  }
  void connectFailed(bool transient) {
    ZiLOG(Error, "zhttp", ([this, transient](auto &s) {
      s << "worker=" << id << " failed to connect";
      if (transient) s << " (transient)";
    }));
    if (req) {
      req->failed = true;
      req->done = true;
      responseComplete(req, false);
    } else
      this->app()->workerIdle(this->impl(), false);
  }
  void responseComplete(State *state, bool ok) {
    if (!req || state != req) return;
    ok = ok && !req->failed;
    if (ok && req->redirecting) {
      URL next;
      if (!req->location || req->redirects >= MaxRedirects ||
	  !parseLocation(req->url, req->location, next) ||
	  !h1TransportOK<App>(next)) {
	req->failed = true;
	ok = false;
      } else {
	URL prev = req->url;
	req->url = ZuMv(next);
	++req->redirects;
	bool reuse = reusable() && sameOrigin(prev, req->url);
	resetAttempt(*req, true);
	if (reuse)
	  sendCurrent();
	else {
	  closing = true;
	  this->disconnect();
	}
	return;
      }
    }
    bool reuse = ok && reusable();
    this->app()->finishReq(req, ok);
    req = this->app()->nextReq();
    if (!req) {
      stopping = true;
      this->disconnect();
      return;
    }
    assign(req);
    if (reuse)
      sendCurrent();
    else {
      closing = true;
      this->disconnect();
    }
  }
  template <typename Rx>
  int process(Rx &rx) {
    return processResponse<false>(*this, *req, rx);
  }
  bool reusable() const {
    return req && !req->connectionClose && !req->closeDelimited &&
      (!req->http10 || req->connectionKeepAlive) && !req->failed;
  }

  unsigned	id = 0;
  Req		*req = nullptr;
  bool		connectedFlag = false;
  bool		closing = false;
  bool		stopping = false;
  unsigned	retryConnects = 0;
  Parser	parser;
};

template <
  typename App,
  template <typename> class Client_,
  template <typename, typename, typename, typename> class Link__>
struct H1PoolClient : public Client_<App> {
  auto impl() const { return static_cast<const App *>(this); }
  auto impl() { return static_cast<App *>(this); }

  using RxBufAlloc = Ztcp::RxBufAlloc<BufBuiltin, BufMax, "Zhttp.Buf">;
  using TxBufAlloc = Ztcp::TxBufAlloc<BufBuiltin, BufMax, "Zhttp.Buf">;
  template <typename Impl>
  using Link_ = Link__<App, Impl, RxBufAlloc, TxBufAlloc>;
  struct Link : public H1PoolLink<App, Link_<Link>> {
    using Base = H1PoolLink<App, Link_<Link>>;
    Link(App *app, unsigned id) : Base{app, id} { }
  };

  Run			*run = nullptr;
  ZtArray<ZmRef<Link>, ZtArrayHeapID<"Zhttp.H1Worker">> links;
  ZmSemaphore		sem;
  unsigned		next = 0;
  unsigned		complete = 0;
  unsigned		failed = 0;
  unsigned		stopped = 0;

  Req *nextReq() {
    if (!run || next >= run->options.requests) return nullptr;
    return &run->reqs[next++];
  }
  void finishReq(Req *req, bool ok) {
    if (!req) return;
    closeBody(*req);
    req->failed = !ok;
    ++complete;
    if (!ok) ++failed;
  }
  void workerIdle(Link *link, bool reconnect) {
    Req *req = nextReq();
    if (!req) {
      if (complete >= run->options.requests) sem.post();
      return;
    }
    link->assign(req);
    if (reconnect)
      link->connect(req->url.host, req->url.port);
    else
      link->sendCurrent();
  }
  void workerStopped(Link *) {
    ++stopped;
    if (complete >= run->options.requests && stopped >= links.length())
      sem.post();
  }
  unsigned reconnFreq() const { return 0; }
};

struct H1TCPClient :
  public H1PoolClient<H1TCPClient, Ztcp::Client, Ztcp::CliLink> { };
struct H1TLSClient :
  public H1PoolClient<H1TLSClient, Ztls::Client, Ztls::CliLink> { };

struct QUICClient : public Zquic::Client<QUICClient> {
  struct Link;
  struct Stream;

  ZmSemaphore sem;
  State state;
  Run *run = nullptr;
  ZmRef<Link> link;
  ZmLock lock;
  unsigned scheduled = 0;
  unsigned active = 0;
  unsigned complete = 0;
  unsigned failed = 0;
  ZtArray<Req *, ZtArrayHeapID<"Zhttp.H3Pending">> pending;
  unsigned pendingHead = 0;

  void done() { sem.post(); }
  unsigned reconnFreq() const { return 0; }
  uint64_t maxStreamsBidi() const {
    return run && run->options.concurrency > H3BidiMax ?
      run->options.concurrency : H3BidiMax;
  }
  uint64_t maxStreamsUni() const { return H3UniMax; }
  bool multi() const { return run; }
  void openH3Streams(Link *);
  void sendH3Req(Link *, ZmRef<Stream>, Req *);
  void queueH3Req(Req *);
  Req *popH3Req();
  void bindH3Stream(Link *, ZmRef<Stream>);
  void finishH3Req(Link *, Req *, bool);
  void failH3Link();
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

  Req *req = nullptr;
  H3ResponseParser<QUICClient::Link> parser;
};

struct QUICClient::Link :
  public CliLink<QUICClient,
    Zquic::CliLink<QUICClient, QUICClient::Link, QUICClient::Stream>, true> {
  using Base = CliLink<QUICClient,
    Zquic::CliLink<QUICClient, QUICClient::Link, QUICClient::Stream>, true>;
  using Base::Base;

  void connected(Zi::Connected info) {
    if (!this->app()->multi()) return Base::connected(info);
    logConnected(this->app()->run->reqs[0], info);
    if (!this->h3.openLocal(*this)) {
      this->app()->failH3Link();
      return;
    }
    this->app()->link = ZmMkRef(this);
    this->app()->openH3Streams(this);
  }
  void disconnected() {
    if (!this->app()->multi()) return Base::disconnected();
    ZiLOG(Info, "zhttp", "disconnected");
    this->app()->failH3Link();
  }
  void connectFailed(bool transient) {
    if (!this->app()->multi()) return Base::connectFailed(transient);
    ZiLOG(Error, "zhttp", ([transient](auto &s) {
      s << "failed to connect";
      if (transient) s << " (transient)";
    }));
    this->app()->failH3Link();
  }
  void responseComplete(State *state, bool ok) {
    if (!this->app()->multi()) return Base::responseComplete(state, ok);
    if (auto stream = this->findStream(state->responseStreamID))
      stream->req = nullptr;
    this->app()->finishH3Req(this, state, ok);
  }
  void streamed(ZmRef<Stream> stream) {
    if (!this->app()->multi()) return;
    this->app()->bindH3Stream(this, ZuMv(stream));
  }
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
  if (req) {
    auto parserState = parser.process(*this);
    using Parser = ZuDecay<decltype(parser)>;
    if (parserState == Parser::State::Error) return -1;
    if (parserState == Parser::State::Complete) return 1;
    if (req && req->done) return -1;
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

void QUICClient::openH3Streams(Link *link_)
{
  if (!link_) return;
  if (!this->txInvoked()) {
    this->txInvoke([this, link = ZmMkRef(link_)]() mutable {
      openH3Streams(link.ptr());
    });
    return;
  }
  for (;;) {
    Req *req = nullptr;
    {
      ZmGuard<ZmLock> guard(lock);
      if (!run || active >= run->options.concurrency ||
	  scheduled >= run->options.requests)
	return;
      req = &run->reqs[scheduled++];
      ++active;
    }
    resetAttempt(*req, true);
    auto stream = link_->stream(Zi::StreamType::Duplex);
    if (!stream) {
      queueH3Req(req);
      return;
    }
    sendH3Req(link_, ZuMv(stream), req);
  }
}

void QUICClient::sendH3Req(Link *link_, ZmRef<Stream> stream, Req *req)
{
  if (!link_ || !stream || !req) {
    finishH3Req(link_, req, false);
    return;
  }
  if (!this->txInvoked()) {
    this->txInvoke([
      this, link = ZmMkRef(link_), stream = ZuMv(stream), req
    ]() mutable {
      if (!run || req->done) return;
      sendH3Req(link.ptr(), ZuMv(stream), req);
    });
    return;
  }
  req->protocol = Protocol::H3;
  req->responseStreamID = stream->id();
  stream->req = req;
  stream->parser.bind(link_, req);
  stream->parser.reset();
  sendH3Request(*req, stream);
}

void QUICClient::queueH3Req(Req *req)
{
  if (!req) return;
  ZmGuard<ZmLock> guard(lock);
  pending.push(req);
}

Req *QUICClient::popH3Req()
{
  ZmGuard<ZmLock> guard(lock);
  if (pendingHead >= pending.length()) return nullptr;
  Req *req = pending[pendingHead++];
  if (pendingHead >= pending.length()) {
    pending.length(0);
    pendingHead = 0;
  }
  return req;
}

void QUICClient::bindH3Stream(Link *link_, ZmRef<Stream> stream)
{
  Req *req = popH3Req();
  if (!req) return;
  sendH3Req(link_, ZuMv(stream), req);
}

void QUICClient::finishH3Req(Link *link_, Req *req, bool ok)
{
  bool done = false;
  ok = ok && req && !req->failed;
  if (req && ok && req->redirecting) {
    URL next;
    if (!req->location || req->redirects >= MaxRedirects ||
	!parseLocation(req->url, req->location, next) ||
	!sameOrigin(req->url, next)) {
      ok = false;
    } else {
      req->url = ZuMv(next);
      ++req->redirects;
      resetAttempt(*req, true);
      if (!this->txInvoked()) {
	this->txInvoke([this, link = ZmMkRef(link_), req]() mutable {
	  if (!run) return;
	  auto stream = link->stream(Zi::StreamType::Duplex);
	  if (!stream) {
	    queueH3Req(req);
	    return;
	  }
	  sendH3Req(link.ptr(), ZuMv(stream), req);
	});
	return;
      }
    }
  }
  {
    ZmGuard<ZmLock> guard(lock);
    if (active) --active;
    if (req) {
      closeBody(*req);
      req->done = true;
      req->failed = !ok;
    }
    ++complete;
    if (!ok) ++failed;
    done = !run || complete >= run->options.requests;
  }
  if (done)
    sem.post();
  else
    openH3Streams(link_);
}

void QUICClient::failH3Link()
{
  ZmGuard<ZmLock> guard(lock);
  if (!run) {
    state.failed = true;
    done();
    return;
  }
  for (unsigned i = 0; i < run->options.requests; ++i) {
    auto &req = run->reqs[i];
    if (req.done) continue;
    closeBody(req);
    req.done = true;
    req.failed = true;
    ++failed;
    ++complete;
  }
  active = 0;
  sem.post();
}

template <typename Client>
int run(
  ZiMultiplex &mx, const Options &options, Req &req,
  RequestResult *result = nullptr)
{
  Client client;
  client.state.id = req.id;
  client.state.requests = req.requests;
  client.state.url = req.url;
  client.state.output = req.output;

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
  closeBody(client.state);
  req = ZuMv(client.state);
  client.final();
  return rc;
}

template <typename Client>
int runH1Pool(ZiMultiplex &mx, Run &run)
{
  Client client;
  client.run = &run;

  if constexpr (Client::Transport == Zi::Transport::TCP) {
    if (!client.init(Ztcp::ClientParams(&mx, "3", "4"))) {
      ZiLOG(Error, "zhttp", "TCP client initialization failed");
      return 1;
    }
  } else {
    ZuCSpan alpn[] = { "http/1.1" };
    if (!client.init(
	  Ztls::ClientParams(&mx, "3", "4")
	    .alpn(alpn).caPath(run.options.ca))) {
      ZiLOG(Error, "zhttp", "TLS client initialization failed");
      return 1;
    }
  }

  unsigned n = run.options.concurrency;
  if (n > run.options.requests) n = run.options.requests;
  client.links.length(n);
  for (unsigned i = 0; i < n; ++i) {
    auto link = new typename Client::Link(&client, i);
    client.links[i] = link;
    Req *req = client.nextReq();
    if (!req) continue;
    link->assign(req);
    link->connect(req->url.host, req->url.port);
  }

  unsigned timeout = ClientTimeout *
    ((run.options.requests + run.options.concurrency - 1) /
      run.options.concurrency + 1);
  if (client.sem.timedwait(Zm::now(timeout)) != 0) {
    ZiLOG(Error, "zhttp", "timed out");
    for (unsigned i = 0; i < client.links.length(); ++i)
      if (client.links[i]) client.links[i]->disconnect();
    client.sem.timedwait(Zm::now(2));
    client.failed += run.options.requests - client.complete;
  }

  run.complete = client.complete;
  run.failed = client.failed;
  client.final();
  return client.failed ? 1 : 0;
}

int runH3Multi(ZiMultiplex &mx, Run &run)
{
  QUICClient client;
  client.run = &run;
  ZuCSpan alpn[] = { "h3" };
  if (!client.init(
	Zquic::ClientParams(&mx, "3", "4")
	  .alpn(alpn)
	  .caPath(run.options.ca)
	  .maxData(H3DataMax)
	  .maxStreamData(H3StreamDataMax)
	  .maxStreamsBidi(client.maxStreamsBidi())
	  .maxStreamsUni(H3UniMax))) {
    ZiLOG(Error, "zhttp", "QUIC client initialization failed");
    return 1;
  }

  auto link = new QUICClient::Link(&client);
  client.link = link;
  link->connect(run.originalURL.host, run.originalURL.port);
  unsigned timeout = ClientTimeout *
    ((run.options.requests + run.options.concurrency - 1) /
      run.options.concurrency + 1);
  if (client.sem.timedwait(Zm::now(timeout)) != 0) {
    ZiLOG(Error, "zhttp", "timed out");
    link->disconnect();
    client.sem.timedwait(Zm::now(2));
    client.failH3Link();
  } else {
    link->disconnect();
    client.sem.timedwait(Zm::now(2));
  }
  {
    ZmGuard<ZmLock> guard(client.lock);
    run.complete = client.complete;
    run.failed = client.failed;
  }
  client.final();
  return run.failed ? 1 : 0;
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

bool resolveH3Cached(Run &run_, const URL &url, ZiResolver::H3Policy policy)
{
  if (policy != ZiResolver::H3Policy::DNSOnly)
    return resolveH3(url, policy);
  auto &discovery = discoveryFor(run_, url);
  if (discovery.dnsChecked) return discovery.dnsH3;
  discovery.dnsChecked = true;
  discovery.dnsH3 = resolveH3(url, policy);
  return discovery.dnsH3;
}

URL altSvcURL(const URL &url, const AltSvcEndpoint &altSvc)
{
  URL h3URL = url;
  if (altSvc.host) setHost(h3URL, altSvc.host);
  if (altSvc.port) h3URL.port = altSvc.port;
  return h3URL;
}

void cacheAltSvc(Run &run_, const URL &url, const AltSvcEndpoint &altSvc)
{
  auto &discovery = discoveryFor(run_, url);
  discovery.altSvcChecked = true;
  discovery.altSvcH3 = altSvc.h3;
  discovery.altSvcEndpoint = altSvc;
}

bool cachedAltSvc(Run &run_, const URL &url, AltSvcEndpoint &altSvc)
{
  auto &discovery = discoveryFor(run_, url);
  if (!discovery.altSvcChecked || !discovery.altSvcH3) return false;
  altSvc = discovery.altSvcEndpoint;
  return true;
}

int runH3Direct(
  ZiMultiplex &mx, const Options &options, Req &req,
  RequestResult &result)
{
  ZiLOG(Info, "zhttp", ([&req](auto &s) {
    reqLogPrefix(req, s);
    s << "HTTP/3 direct: " << req.url.host << ':' << ZuBoxed(req.url.port);
  }));
  resetAttempt(req, true);
  return run<QUICClient>(mx, options, req, &result);
}

int runH1AltSvcFirst(
  ZiMultiplex &mx, Run &run_, Req &req,
  RequestResult &result)
{
  const auto &options = run_.options;
  AltSvcEndpoint cached;
  if (cachedAltSvc(run_, req.url, cached)) {
    RequestResult h3Result;
    URL h1URL = req.url;
    req.url = altSvcURL(h1URL, cached);
    ZiLOG(Info, "zhttp", ([&req](auto &s) {
      reqLogPrefix(req, s);
      s << "cached Alt-Svc HTTP/3 probe: " << req.url.host << ':' <<
	ZuBoxed(req.url.port);
    }));
    resetAttempt(req, true);
    int h3rc = run<QUICClient>(mx, options, req, &h3Result);
    if (!h3rc) {
      result = ZuMv(h3Result);
      return 0;
    }
    req.url = ZuMv(h1URL);
  }

  resetAttempt(req, true);
  int rc = run<TLSClient>(mx, options, req, &result);
  if (result.altSvc.h3) cacheAltSvc(run_, req.url, result.altSvc);
  if (rc || redirectStatus(result.status) || !result.altSvc.h3) return rc;

  RequestResult h3Result;
  URL h1URL = req.url;
  req.url = altSvcURL(h1URL, result.altSvc);
  ZiLOG(Info, "zhttp", ([&req](auto &s) {
    reqLogPrefix(req, s);
    s << "Alt-Svc HTTP/3 probe: " << req.url.host << ':' <<
      ZuBoxed(req.url.port);
  }));
  resetAttempt(req, true);
  int h3rc = run<QUICClient>(mx, options, req, &h3Result);
  if (!h3rc) {
    result = ZuMv(h3Result);
    return 0;
  }

  req.url = ZuMv(h1URL);
  result = {};
  resetAttempt(req, true);
  return run<TLSClient>(mx, options, req, &result);
}

int runH3DNSAltSvcFallback(
  ZiMultiplex &mx, Run &run_, Req &req,
  RequestResult &result)
{
  const auto &options = run_.options;
  RequestResult h3Result;
  if (resolveH3Cached(run_, req.url, ZiResolver::H3Policy::DNSOnly)) {
    resetAttempt(req, true);
    if (!run<QUICClient>(mx, options, req, &h3Result)) {
      result = ZuMv(h3Result);
      return 0;
    }
  }

  result = {};
  return runH1AltSvcFirst(mx, run_, req, result);
}

int runReqSerial(ZiMultiplex &mx, Run &run_, Req &req)
{
  const auto &options = run_.options;
  int rc = 1;
  for (req.redirects = 0; req.redirects <= MaxRedirects; ++req.redirects) {
    RequestResult result;
    if (req.url.scheme == "http") {
      resetAttempt(req, true);
      rc = run<TCPClient>(mx, options, req, &result);
    } else if (options.http3)
      rc = runH3Direct(mx, options, req, result);
    else
      rc = runH3DNSAltSvcFallback(mx, run_, req, result);

    if (rc || !redirectStatus(result.status) || !result.location) break;
    URL next;
    if (!parseLocation(req.url, result.location, next)) {
      ZiLOG(Error, "zhttp", ([&req, &result](auto &s) {
	reqLogPrefix(req, s);
	s << "invalid redirect location: " << result.location;
      }));
      rc = 1;
      break;
    }
    ZiLOG(Info, "zhttp", ([&req, &next](auto &s) {
      reqLogPrefix(req, s);
      s << "redirect: " << next.scheme << "://" << next.host << next.target;
    }));
    req.url = ZuMv(next);
    if (req.redirects == MaxRedirects) {
      ZiLOG(Error, "zhttp", ([&req](auto &s) {
	reqLogPrefix(req, s);
	s << "too many redirects";
      }));
      rc = 1;
    }
  }
  req.failed = rc != 0;
  return rc;
}

int main(int argc, char **argv)
{
  Options options;
  argc = ZtCLI::load(options, argc, argv);
  if (options.help) usage(0);
  if (!validateOptions(options, argc)) usage();
  if (options.requests == 1) options.concurrency = 1;

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

  Run run;
  run.options = options;
  run.originalURL = url;
  run.reqs.length(options.requests);
  for (unsigned i = 0; i < options.requests; ++i) {
    auto &req = run.reqs[i];
    req.id = i;
    req.requests = options.requests;
    req.url = url;
    req.output = outputPath(options.output, i, options.requests);
  }

  int rc = 0;
  if (options.requests > 1 && options.http3 && url.scheme == "https") {
    rc = runH3Multi(mx, run);
  } else if (options.requests > 1 && url.scheme == "http") {
    rc = runH1Pool<H1TCPClient>(mx, run);
  } else if (options.requests > 1 && url.scheme == "https") {
    rc = runH1Pool<H1TLSClient>(mx, run);
  } else {
    for (unsigned i = 0; i < options.requests; ++i) {
      if (runReqSerial(mx, run, run.reqs[i])) {
	++run.failed;
	rc = 1;
      }
      ++run.complete;
    }
  }

  mx.stop();
  ZiLog::stop();

  return rc;
}
