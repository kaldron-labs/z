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
  (((ca),     (CLI::Opt<'c'>, CLI::Long<"ca">)),     (String)),
  (((output), (CLI::Opt<'o'>, CLI::Long<"output">)), (String, "index.html")),
  (((http3),  (CLI::Flag<1>, CLI::Long<"http3">)),   (Bool)),
  (((http3Only), (CLI::Flag<2>, CLI::Long<"http3-only">)), (Bool)),
  (((url),    (CLI::Arg<1>)),                        (String)),
  (((help),   (CLI::Flag<'h'>, CLI::Long<"help">)),  (Bool)));

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

struct State {
  URL		url;
  Options	options;
  Protocol::T	protocol = Protocol::H1;
  Zhttp::H3::CxnState::T h3State = Zhttp::H3::CxnState::Init;
  ZtString<>	altSvcHost;
  uint16_t	altSvcPort = 0;
  int64_t	responseStreamID = -1;
  ZiFile	bodyFile;
  int64_t	contentLength = -1;
  uint64_t	bodyBytes = 0;
  unsigned	bodyChunks = 0;
  bool		bodyFileOpen = false;
  bool		chunked = false;
  bool		altSvcH3 = false;
  bool		framingLogged = false;
  bool		done = false;
};

struct AltSvcEndpoint {
  ZtString<>	host;
  Zi::Hostname	dnsHost;
  uint16_t	port = 0;
  bool		h3 = false;
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

  using RequestOps::host;
  using RequestOps::header;
  using RequestOps::operation;
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
  using Scratch = ZtArray<uint8_t, ZtArrayHeapID<"ZhttpClient.H3Request">>;
  struct Tx {
    void operator <<(char c) { bytes->push(uint8_t(c)); }
    void flush() { }
    Scratch	*bytes = nullptr;
  };
  auto bytes = ZtLocalArray(Scratch, 2048);
  Tx tx{&bytes};
  H3RequestBuilder builder{state};
  builder.request(tx);
  builder.finish(tx);
  stream->link()->send(stream, ZuCSpan{
    reinterpret_cast<const char *>(bytes.data()), bytes.length()}, true);
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
    }
    std::cerr << "header " << Key{}() << ": " << ZuCSpan(value) <<
      '\n' << std::flush;
  }

  void body(ZuBSpan span) {
    logFraming(*state);
    if (!span) return;
    if (!state->bodyFileOpen) {
      state->bodyFile =
	ZiFile(state->options.output, ZiFile::Write | ZiFile::GC);
      if (!state->bodyFile) {
	std::cerr << "failed to open " << state->options.output << '\n' <<
	  std::flush;
	state->done = true;
	return;
      }
      state->bodyFileOpen = true;
    }
    if (state->bodyFile.write(span.data(), span.length()) != Zi::OK) {
      std::cerr << "failed to write body chunk\n" << std::flush;
      state->done = true;
      return;
    }
    state->bodyBytes += span.length();
    ++state->bodyChunks;
    std::cerr << "body chunk: " << span.length() << " bytes\n" << std::flush;
  }

  template <typename ParserState>
  void complete(ParserState) {
    if (state->done) return;
    std::cerr << "body complete: " << state->bodyBytes << " bytes in " <<
      state->bodyChunks << " chunks\n" << std::flush;
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

  using SinkBase::body;
  using SinkBase::chunked;
  using SinkBase::complete;
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
  auto control = link.stream(Zi::StreamType::Simplex);
  if (!control) return false;
  using Scratch = ZtArray<uint8_t, ZtArrayHeapID<"ZhttpClient.H3Control">>;
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
  if (!link.send(control, ZuCSpan{
	reinterpret_cast<const char *>(payload.data()), payload.length()}, false))
    return false;
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

  using Base::Base;

  CliLink(App *app) : Base{app} { }
  ~CliLink() { delete parser; }

  void connected(Zi::Connected info) {
    logConnected(this->app()->state, info);
    typename Base::StreamRef stream;
    if constexpr (H3) {
      if (!openH3LocalStreams(*this, this->app()->state)) {
	this->app()->done();
	return;
      }
      stream = this->stream(Zi::StreamType::Duplex);
      this->app()->state.responseStreamID = stream ? stream->id() : -1;
    } else
      stream = this->stream();
    if (!stream) {
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
    this->app()->done();
  }
  void connectFailed(bool transient) {
    std::cerr << "failed to connect" << (transient ? " (transient)" : "") <<
      '\n' << std::flush;
    this->app()->done();
  }
  template <typename Rx>
  int process(Rx &rx) {
    return processResponse<H3>(*this, this->app()->state, rx);
  }

  Parser	*parser = nullptr;
  bool		peerControl = false;
  bool		peerEncoder = false;
  bool		peerDecoder = false;
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

template <typename Client>
int run(
  ZiMultiplex &mx, const Options &options, URL url,
  AltSvcEndpoint *altSvc = nullptr) {
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
    client.sem.wait();
  }
  if (altSvc && client.state.altSvcH3) {
    altSvc->host = client.state.altSvcHost;
    altSvc->dnsHost = client.state.url.dnsHost;
    altSvc->port = client.state.altSvcPort;
    altSvc->h3 = true;
  }
  client.final();
  return 0;
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
  if (url.scheme == "http" && options.http3) {
    std::cerr << "HTTP/3 requires https:// URL\n" << std::flush;
    return 1;
  }

  ZiLog::init("zhttpclient");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  ZiMultiplex mx(mxParams());
  if (!mx.start()) {
    std::cerr << "ZiMultiplex start failed\n" << std::flush;
    return 1;
  }

  int rc;
  if (url.scheme == "http")
    rc = run<TCPClient>(mx, options, ZuMv(url));
  else if (options.http3) {
    URL fallbackURL = url;
    rc = resolveForQUIC(url) ? run<QUICClient>(mx, options, ZuMv(url)) : 1;
    if (rc && !options.http3Only)
      rc = run<TLSClient>(mx, options, ZuMv(fallbackURL));
  } else
    rc = run<TLSClient>(mx, options, ZuMv(url));

  mx.stop();
  ZiLog::stop();

  return rc;
}
