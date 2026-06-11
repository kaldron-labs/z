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

#include <zlib/Zhttp.hh>

struct Options {
 ZuCSpan	ca;
 ZuCSpan	output{"index.html"};
 ZuCSpan	url;
 bool		help = false;
};

ZtStruct((Options, CLI),
  (((ca),     (CLI::Opt<'c'>, CLI::Long<"ca">)),     (String)),
  (((output), (CLI::Opt<'o'>, CLI::Long<"output">)), (String, "index.html")),
  (((url),    (CLI::Arg<1>)),                        (String)),
  (((help),   (CLI::Flag<'h'>, CLI::Long<"help">)),  (Bool)));

void usage(int code = 1)
{
  std::cerr <<
    "Usage: zhttpclient [OPTION]... URL\n\n"
    "Options:\n"
    "  -c, --ca=PATH       CA path for https:\n"
    "  -o, --output=PATH   response body output path\n"
    "  -h, --help          show help\n" << std::flush;
  ::exit(code);
}

using RequestHeaders = ZhttpHeaders(
  ("user-agent", "zhttpclient/1.0"),
  ("accept", "*/*"));
using ResponseHeaders = ZhttpHeaders(
  "content-type",
  "location",
  "server");

struct URL {
  ZuCSpan	scheme;
  ZtString<>	host;
  uint16_t	port = 0;
  ZtString<>	target;
};

struct State {
  URL		url;
  Options	options;
  ZiFile	bodyFile;
  int64_t	contentLength = -1;
  uint64_t	bodyBytes = 0;
  unsigned	bodyChunks = 0;
  bool		bodyFileOpen = false;
  bool		chunked = false;
  bool		framingLogged = false;
  bool		done = false;
};

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
    url.host = host;
    url.port = p;
    url.target = target;
    return true;
  }

  url.host = authority;
  url.target = target;
  return true;
}

struct RequestBuilder :
  public Zhttp::H1::Builder<RequestBuilder, RequestHeaders> {
  using Base = Zhttp::H1::Builder<RequestBuilder, RequestHeaders>;

  RequestBuilder(const State &state_) : state{&state_} { }

  template <typename L>
  void operation(L &&l) {
    ZuCSpan target = state->url.target;
    ZuCSpan path = target;
    ZuCSpan query;
    if (auto i = target.find([](auto c) { return c == '?'; }); i >= 0) {
      path.trunc(i);
      query = target;
      query.offset(i + 1);
    }
    if (!path) path = "/";
    l(Zhttp::Method::GET, path, query);
  }
  template <typename L>
  void host(L &&l) { l(ZuCSpan{state->url.host}); }

  const State *state = nullptr;
};

template <typename StreamRef>
void sendRequest(State &state, StreamRef stream)
{
  auto tx = stream->txStream();
  RequestBuilder builder{state};
  builder.request(tx);
  builder.finish(tx);
  tx << Zi::flush();
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
struct ResponseParser :
  public Zhttp::H1::Parser<ResponseParser<Link>, false, ResponseHeaders, 100<<20> {
  using Base =
    Zhttp::H1::Parser<ResponseParser<Link>, false, ResponseHeaders, 100<<20>;

  ResponseParser(Link *link_, State *state_) : link{link_}, state{state_} { }

  void status(unsigned status) {
    std::cerr << "status: " << status << '\n' << std::flush;
  }
  void contentLength(uint64_t contentLength) {
    state->contentLength = contentLength;
  }
  void chunked() { state->chunked = true; }

  template <typename Key>
  void header(ZuBSpan value) {
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

  void complete(Zhttp::H1::ParserState::T) {
    std::cerr << "body complete: " << state->bodyBytes << " bytes in " <<
      state->bodyChunks << " chunks\n" << std::flush;
    state->done = true;
  }

  Link		*link = nullptr;
  State		*state = nullptr;
};

template <typename Link, typename Stream>
int processResponse(Link &link, State &state, Stream &rx)
{
  if (!link.parser) link.parser = new ResponseParser<Link>{&link, &state};
  auto parserState = link.parser->process(rx);
  if (state.done) return -1;
  if (parserState == Zhttp::H1::ParserState::Error) return -1;
  if (parserState != Zhttp::H1::ParserState::Complete) return 0;
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

template <typename App_, typename Base_>
struct CliLink : public Base_ {
  using App = App_;
  using Base = Base_;

  using Base::Base;

  CliLink(App *app) : Base{app} { }
  ~CliLink() { delete parser; }

  void connected(Zi::Connected info) {
    logConnected(this->app()->state, info);
    typename Base::StreamRef stream = this->stream();
    if (!stream) {
      this->app()->done();
      return;
    }
    sendRequest(this->app()->state, stream);
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
    return processResponse(*this, this->app()->state, rx);
  }

  ResponseParser<CliLink> *parser = nullptr;
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

template <typename Client>
int run(ZiMultiplex &mx, const Options &options, URL url) {
  Client client;
  client.state.url = ZuMv(url);
  client.state.options.output = options.output;

  if constexpr (Client::Transport == Zi::Transport::TCP) {
    if (!client.init(Ztcp::ClientParams(&mx, "3", "4"))) {
      std::cerr << "TCP client initialization failed\n" << std::flush;
      return 1;
    }
  } else if constexpr (Client::Transport == Zi::Transport::TLS) {
    ZuCSpan alpn[] = { "http/1.1" };
    if (!client.init(
	  Ztls::ClientParams(&mx, "3", "4").alpn(alpn).caPath(options.ca))) {
      std::cerr << "TLS client initialization failed\n" << std::flush;
      return 1;
    }
  }

  {
    using Link = typename Client::Link;
    ZmRef<Link> link = new Link(&client);
    link->connect(client.state.url.host, client.state.url.port);
    client.sem.wait();
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
  else
    rc = run<TLSClient>(mx, options, ZuMv(url));

  mx.stop();
  ZiLog::stop();

  return rc;
}
