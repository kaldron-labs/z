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

#include <zlib/ZjrpcServer.hh>

#include "zjrpcexample.hh"

struct Options {
  unsigned port = 8080;
  bool stdio = false;
  bool ws = false;
  bool help = false;
};

ZfStruct(, (Options, CLI),
  (stdio, (CLI::Long<"stdio">),			Bool),
  (ws, (CLI::Long<"ws">),				Bool),
  (port, (CLI::Opt<'p'>, CLI::Long<"port">),		UInt32),
  (help, (CLI::Opt<'h'>, CLI::Long<"help">),		Bool));

static ZmSemaphore done;

static void usage(int code)
{
  std::cerr <<
    "Usage: zjrpcd [OPTION]...\n\n"
    "  --stdio          serve JSON-RPC on stdin/stdout instead of HTTP\n"
    "  --ws             serve JSON-RPC over WebSockets on /rpc\n"
    "  -p, --port=N     loopback network port, default 8080\n"
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
  const Options *options = nullptr;

  void listening(int, unsigned port) {
    ZiLOG(Info, "zjrpcd", ([port](auto &s) { s << "listening port=" << port; }));
  }
  void listenFailed(int, bool) { done.post(); }
  void listening() { listening(0, options->port); }
  void listening(const ZiListenInfo &) { listening(); }
  void listenFailed(bool) { done.post(); }
  bool accept(auto &, ZuBSpan, ZuBSpan target, ZuBSpan protocols,
      Zws::HandshakeString &selected) {
    if (target != "/rpc" || !Zws::token(protocols, "jrpc")) return false;
    selected = "jrpc";
    return true;
  }
  void connected(int) { }
  void disconnected(int) { }
  void closed() { done.post(); }
  void failed() { done.post(); }

  template <typename Req, typename Token>
  void request(Req *, const AddRequest &value, Token token) {
    if (token) token->complete(Zjrpc::Reply<AddOK>{AddResult{value.lhs + value.rhs}});
  }
  template <typename Req, typename Context, typename Token>
  void request(Req *req, const AddRequest &value, const Context &, Token token) {
    request(req, value, ZuMv(token));
  }

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

  ZiLog::init("zjrpcd");
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZmTrap::sigintFn(interrupted);
  ZmTrap::trap();

  ZiMultiplex mx{mxParams()};
  if (!mx.start()) return 1;

  App app{&options};
  if (options.stdio) {
    Zjrpc::IOServer<App, ExampleCatalog> server;
    auto config = Zjrpc::StdioConfig{}
      .rxThread("stdioRx").txThread("stdioTx");
    return run(server, server.init(&mx, ZuMv(config), &app), mx);
  } else if (options.ws) {
    Zjrpc::WSServer<App, ExampleCatalog> server;
    return run(server, server.init(Zhttp::HubConfig{&mx}, ZiIP{"127.0.0.1"},
	options.port, Zhttp::TCPConfig{}, Zjrpc::WSConfig{}, &app), mx);
  } else {
    Zjrpc::HTTPServer<App, ExampleCatalog> server;
    Zjrpc::ServerConfig config;
    config.endpoint("/rpc").http([&options](auto &http) {
      http.localIP(ZiIP{"127.0.0.1"}).port(options.port).tcp();
    });
    return run(server, server.init(
      Zhttp::HubConfig{&mx}, ZuMv(config), &app), mx);
  }
}
