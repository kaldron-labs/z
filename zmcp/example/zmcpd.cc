//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <iostream>

#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTrap.hh>

#include <zlib/ZfCLI.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>

#include <zlib/ZmcpServer.hh>

#include "zmcpexample.hh"

struct Options {
  ZuCSpan token;
  unsigned port = 8080;
  bool stdio = false;
  bool ws = false;
  bool help = false;
};

ZfStruct(, (Options, CLI),
  (stdio, (CLI::Long<"stdio">),			Bool),
  (ws, (CLI::Long<"ws">),				Bool),
  (port, (CLI::Opt<'p'>, CLI::Long<"port">),		UInt32),
  (token, (CLI::Long<"token">),					String),
  (help, (CLI::Opt<'h'>, CLI::Long<"help">),		Bool));

static ZmSemaphore done;

static void usage(int code)
{
  std::cerr <<
    "Usage: zmcpd [OPTION]...\n\n"
    "  --stdio          serve MCP on stdin/stdout instead of HTTP\n"
    "  --ws             serve MCP over WebSockets on /mcp\n"
    "  -p, --port=N     loopback network port, default 8080\n"
    "  --token=TOKEN    require 'Authorization: Bearer TOKEN' for HTTP tools\n"
    "  -h, --help       show help\n" << std::flush;
  ::exit(code);
}

static ZiMxParams mxParams()
{
  return ZiMxParams()
    .scheduler([](auto &scheduler) {
      scheduler.nThreads(4)
	.thread(1, [](auto &thread) { thread.isolated(1); })
	.thread(2, [](auto &thread) { thread.isolated(1); })
	.thread(3, [](auto &thread) {
	  thread.isolated(1).name("stdioRx");
	})
	.thread(4, [](auto &thread) {
	  thread.isolated(1).name("stdioTx");
	});
    });
}

struct App {
  using Headers = ZhttpHeaders("authorization");
  using Authorization = ZuStringT<"authorization">;

  const Options *options = nullptr;

  bool origin(ZuCSpan origin) const {
    auto starts = [origin](ZuCSpan prefix) {
      return origin.prefix(prefix) == prefix.length();
    };
    return starts("http://127.0.0.1") || starts("http://localhost") ||
      starts("https://127.0.0.1") || starts("https://localhost");
  }
  void listening(int, unsigned port) {
    ZiLOG(Info, "zmcpd", ([port](auto &s) { s << "listening port=" << port; }));
  }
  void listenFailed(int, bool) { done.post(); }
  void listening() { listening(0, options->port); }
  void listening(const ZiListenInfo &) { listening(); }
  void listenFailed(bool) { done.post(); }
  bool accept(auto &, ZuBSpan, ZuBSpan target, ZuBSpan protocols,
      Zws::HandshakeString &selected) {
    if (target != "/mcp" || !Zws::token(protocols, "mcp")) return false;
    selected = "mcp";
    return true;
  }
  void connected(int) { }
  void disconnected(int) { }
  void closed() { done.post(); }
  void failed() { done.post(); }

  template <typename Req, typename Completion>
  void tool(
      Req *, const AddRequest &request, const auto &headers,
      const Zmcp::Context &context, Completion completion) {
    if (context.transport() && options->token) {
      auto authorization = headers.template get<Authorization>();
      auto expected = ZtScratch(TextScratch,
	options->token.length() + ZuStringT<"Bearer ">{}().length());
      expected << "Bearer " << options->token;
      if (authorization.count != 1 || authorization.value != expected) {
	completion->complete(Zjrpc::Reply<AddUnauthorized>{});
	return;
      }
    }
    completion->complete(
      Zjrpc::Reply<AddOK>{AddResult{request.lhs + request.rhs}});
  }

  template <typename Req, typename Completion>
  void cancelled(Req *, Completion *, ZuCSpan) { }
};

static void interrupted() { done.post(); }

template <typename Server>
static int run(Server &server, bool initialized, ZiMultiplex &mx)
{
  bool started = initialized && server.start();
  if (!started) {
    server.final();
    mx.stop();
    ZiLog::stop();
    return 1;
  }
  done.wait();
  bool stopped = server.stop();
  server.final();
  mx.stop();
  ZiLog::stop();
  return stopped ? 0 : 1;
}

int main(int argc, char **argv)
{
  Options options;
  try {
    argc = ZfCLI::load(options, argc, argv);
  } catch (const ZeException &e) {
    std::cerr << e << '\n';
    usage(1);
  }
  if (options.help) usage(0);
  if (argc != 1 || (options.stdio && options.ws) ||
      !options.port || options.port > UINT16_MAX) usage(1);

  ZiLog::init("zmcpd");
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZmTrap::sigintFn(interrupted);
  ZmTrap::trap();

  ZiMultiplex mx{mxParams()};
  if (!mx.start()) return 1;

  App app{&options};
  if (options.stdio) {
    Zmcp::IOServer<App, ExampleCatalog> server;
    auto config = Zjrpc::StdioConfig{}
      .rxThread("stdioRx").txThread("stdioTx");
    return run(server, server.init(&mx, ZuMv(config), &app), mx);
  } else if (options.ws) {
    Zmcp::WSServer<App, ExampleCatalog> server;
    return run(server, server.init(Zhttp::HubConfig{&mx}, ZiIP{"127.0.0.1"},
	options.port, Zhttp::TCPConfig{}, Zjrpc::WSConfig{}, &app), mx);
  } else {
    Zmcp::HTTPServer<App, ExampleCatalog> server;
    Zmcp::ServerConfig config;
    config.localIP(ZiIP{"127.0.0.1"}).port(options.port).tcp();
    config.absentOrigin(true);
    return run(server, server.init(
      Zhttp::HubConfig{&mx}, ZuMv(config), &app), mx);
  }
}
