//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// basic test HTTP server

#include <iostream>
#include <string.h>

#include <zlib/ZmSemaphore.hh>
#include <zlib/ZtCLI.hh>
#include <zlib/Ztcp.hh>
#include <zlib/Ztls.hh>
#include <zlib/Zquic.hh>
#include <zlib/Zhttp.hh>

struct Options {
  ZuCSpan	addr{"127.0.0.1"};
  uint16_t	port = 8080;
  ZuCSpan	cert;
  ZuCSpan	key;
  ZuCSpan	body{"zhttp-ok"};
  bool		http = true;
  bool		https = false;
  bool		http3 = false;
  bool		help = false;
};

ZtStruct((Options, CLI),
  (((addr),  (CLI::Opt<'a'>,  CLI::Long<"addr">)),  (String, "127.0.0.1")),
  (((port),  (CLI::Opt<'p'>,  CLI::Long<"port">)),  (UInt16, 8080)),
  (((cert),  (CLI::Opt<'c'>,  CLI::Long<"cert">)),  (String)),
  (((key),   (CLI::Opt<'k'>,  CLI::Long<"key">)),   (String)),
  (((http),  (CLI::Flag<1>,   CLI::Long<"http">)),  (Bool)),
  (((https), (CLI::Flag<2>,   CLI::Long<"https">)), (Bool)),
  (((http3), (CLI::Flag<3>,   CLI::Long<"http3">)), (Bool)),
  (((body),  (CLI::Long<"body">)),		   (String, "zhttp-ok")),
  (((help),  (CLI::Flag<'h'>, CLI::Long<"help">)),  (Bool)));

void usage(int code = 1)
{
  std::cerr <<
    "Usage: zhttpserver [OPTION]...\n\n"
    "Options:\n"
    "  -a, --addr=IP        listen address, default 127.0.0.1\n"
    "  -p, --port=PORT      listen port, default 8080\n"
    "  -c, --cert=PATH      TLS certificate path for HTTPS/H3\n"
    "  -k, --key=PATH       TLS private key path for HTTPS/H3\n"
    "      --http           enable HTTP/1.1 over plain TCP\n"
    "      --https          enable HTTP/1.1 over TLS\n"
    "      --http3          enable HTTP/3 over QUIC\n"
    "      --body=TEXT      response body, default zhttp-ok\n"
    "  -h, --help           show help\n" << std::flush;
  ::exit(code);
}

bool loadOptions(Options &options, int argc, char **argv)
{
  for (int i = 1; i < argc; ++i) {
    ZuCSpan arg{argv[i]};
    auto value = [&]() -> ZuCSpan {
      if (auto eq = arg.find([](auto c) { return c == '='; }); eq >= 0) {
	ZuCSpan v = arg;
	v.offset(eq + 1);
	return v;
      }
      if (i + 1 >= argc) return {};
      return ZuCSpan{argv[++i]};
    };
    if (arg == "-h" || arg == "--help") options.help = true;
    else if (arg == "--http") options.http = true;
    else if (arg == "--https") options.https = true;
    else if (arg == "--http3") options.http3 = true;
    else if (arg == "-a" || arg.starts("--addr")) options.addr = value();
    else if (arg == "-p" || arg.starts("--port")) {
      unsigned port = ZuBox<unsigned>{value()};
      if (!port || port > 65535) return false;
      options.port = port;
    } else if (arg == "-c" || arg.starts("--cert")) options.cert = value();
    else if (arg == "-k" || arg.starts("--key")) options.key = value();
    else if (arg.starts("--body")) options.body = value();
    else return false;
  }
  return true;
}

using ReqHeaders = ZhttpHeaders("content-length");
using RespHeaders = ZhttpHeaders("content-type");

struct State {
  Options		options;
  ZmSemaphore		done;
  ZtString<>		body;
  ZmAtomic<unsigned>	requests = 0;
  ZmAtomic<unsigned>	errors = 0;
};

struct ReqSink {
  void operation(Zhttp::Method::T method_, ZuBSpan path_) {
    method = method_;
    path = ZuCSpan{path_};
  }
  template <typename Key> void header(ZuBSpan) { }
  void contentLength(uint64_t) { }
  void body(ZuBSpan span) { requestBody << ZuCSpan{span}; }
  template <typename ParserState>
  void complete(typename ParserState::T state) {
    complete_ = state == ParserState::Complete;
    failed = !complete_;
  }

  Zhttp::Method::T	method = -1;
  ZtString<>		path;
  ZtString<>		requestBody;
  bool			complete_ = false;
  bool			failed = false;
};

struct ReqParser :
  public Zhttp::H1ReqParser<ReqParser, ReqHeaders, (1<<20)>,
  public ReqSink {
  using Base = Zhttp::H1ReqParser<ReqParser, ReqHeaders, (1<<20)>;
  using State = typename Base::State;
  void complete(State::T state) { ReqSink::template complete<State>(state); }
  using ReqSink::body;
  using ReqSink::contentLength;
  using ReqSink::header;
  using ReqSink::operation;
};

struct RespOps {
  RespOps(ZuCSpan body_) : body{body_} { }
  unsigned status() const { return 200; }
  template <typename L> void reason(L &&l) const { l("OK"); }
  uint64_t contentLength() const { return body.length(); }
  template <typename Key, typename L>
  void header(L &&l) const {
    if constexpr (ZuIsSame<Key, ZuStringT<"content-type">>{})
      l("text/plain");
    else
      l("");
  }

  ZuCSpan	body;
};

struct RespBuilder :
  public Zhttp::H1RespBuilder<RespBuilder, RespHeaders, ZuTypeList<>, true>,
  public RespOps {
  using Base =
    Zhttp::H1RespBuilder<RespBuilder, RespHeaders, ZuTypeList<>, true>;
  RespBuilder(ZuCSpan body_) : RespOps{body_} { }
  using RespOps::contentLength;
  using RespOps::header;
  using RespOps::reason;
  using RespOps::status;
};

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

struct HTTPServer : public Ztcp::Server<HTTPServer> {
  struct Link;

  HTTPServer(State *state_) : state{state_} { }

  ZiConnection *accepted(const ZiCxnInfo &ci);
  ZiIP localIP() const { return ZiIP(state->options.addr); }
  unsigned localPort() const { return state->options.port; }
  void listening(const ZiListenInfo &info) {
    std::cerr << "http listening: " << info.port << '\n' << std::flush;
  }
  void listenFailed(bool) {
    state->errors = 1;
    state->done.post();
  }

  State	*state = nullptr;
};

struct HTTPServer::Link :
  public Ztcp::SrvLink<HTTPServer, HTTPServer::Link> {
  using Base = Ztcp::SrvLink<HTTPServer, HTTPServer::Link>;
  using Base::Base;

  Link(HTTPServer *app) : Base{app} { }
  void connected(Zi::Connected) { }
  void disconnected() { }
  int process(Ztcp::RxStream &rx) {
    auto s = parser.process(rx);
    if (s == ReqParser::State::Error) {
      app()->state->errors = 1;
      return -1;
    }
    if (s == ReqParser::State::Complete) {
      ++app()->state->requests;
      auto tx = this->txStream();
      RespBuilder builder{ZuCSpan{app()->state->body}};
      builder.response(tx);
      if (app()->state->body) {
	auto body = builder.Base::body(tx);
	body << ZuCSpan{app()->state->body};
      }
      builder.finish(tx);
      parser.reset();
      return 1;
    }
    return 0;
  }

  ReqParser	parser;
};

ZiConnection *HTTPServer::accepted(const ZiCxnInfo &ci)
{
  return new Link::Cxn(new Link(this), ci);
}

int main(int argc, char **argv)
{
  Options options;
  if (!loadOptions(options, argc, argv)) usage();
  if (options.help) usage(0);
  if (!options.http && !options.https && !options.http3) usage();
  if (options.https || options.http3)
    std::cerr << "warning: HTTPS/H3 options are parsed but this example "
      "currently starts the HTTP listener\n" << std::flush;

  ZiLog::init("zhttpserver");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  State state;
  state.options = options;
  state.body = options.body;

  ZiMultiplex mx(mxParams());
  if (!mx.start()) {
    std::cerr << "ZiMultiplex start failed\n" << std::flush;
    return 1;
  }
  HTTPServer server{&state};
  if (!server.init(Ztcp::ServerParams(&mx, "3", "4"))) {
    std::cerr << "HTTP server initialization failed\n" << std::flush;
    mx.stop();
    return 1;
  }
  server.listen();
  state.done.wait();
  server.final();
  mx.stop();
  ZiLog::stop();
  return state.errors ? 1 : 0;
}
