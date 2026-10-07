//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuDerive.hh>
#include <stdlib.h>

#include <iostream>

#include <zlib/ZmHeap.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZfCLI.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>

#include <zlib/ZjrpcClient.hh>

#include "zjrpcexample.hh"

struct Options {
  ZuCSpan host{"127.0.0.1"};
  int64_t lhs = 20;
  int64_t rhs = 22;
  unsigned port = 8080;
  bool stdio = false;
  bool ws = false;
  bool stream = false;
  bool help = false;
};

ZfStruct(, (Options, CLI),
  (stdio, (CLI::Long<"stdio">),			Bool),
  (ws, (CLI::Long<"ws">),				Bool),
  (host, (CLI::Long<"host">),			String),
  (port, (CLI::Opt<'p'>, CLI::Long<"port">),		UInt32),
  (lhs, (CLI::Long<"lhs">),			Int64),
  (rhs, (CLI::Long<"rhs">),			Int64),
  (stream, (CLI::Long<"stream">),		Bool),
  (help, (CLI::Opt<'h'>, CLI::Long<"help">),		Bool));

static void usage(int code)
{
  std::cerr <<
    "Usage: zjrpc [OPTION]...\n\n"
    "  --stdio          use inherited stdin/stdout instead of HTTP\n"
    "  --ws             use WebSockets on /rpc instead of HTTP POST\n"
    "  --host=HOST      network host, default 127.0.0.1\n"
    "  -p, --port=N     network port, default 8080\n"
    "  --lhs=N          left operand, default 20\n"
    "  --rhs=N          right operand, default 22\n"
    "  --stream         call the SSE response alternative\n"
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
  ZmSemaphore ready_;
  bool failed_ = false;

  template <typename Req, typename Token>
  void request(Req *, const AddRequest &value, Token token) {
    if (token) token->complete(Zjrpc::Reply<AddOK>{AddResult{value.lhs + value.rhs}});
  }

  void ready() { ready_.post(); }
  void closed() { }
  void failed() {
    failed_ = true;
    ready_.post();
  }
};

template <typename Heap = ZuVoid>
struct Call_ : public Heap, public ZmObject {
  ZmSemaphore done;
  int64_t value = 0;
  bool failed_ = false;

  void process(Zjrpc::Reply<AddOK> reply) {
    value = reply.body.value;
    done.post();
  }
  void failed(const Zjrpc::Error &) { failed_ = true; done.post(); }
  void failed() { failed_ = true; done.post(); }
};
ZuDerive(CallHeap, (ZmHeap<"ZjrpcExample.Call", Call_<>>));
ZuDerive(Call, (Call_<CallHeap>));

template <typename Client>
static bool run(Client &client, const Options &options, App &app)
{
  app.ready_.wait();
  if (app.failed_) return false;
  ZmRef<Call> call = new Call{};
  bool sent = options.stream ?
    client.template call<AddStream>(
      AddRequest{options.lhs, options.rhs}, call) :
    client.template call<Add>(AddRequest{options.lhs, options.rhs}, call);
  if (!sent) return false;
  call->done.wait();
  if (!call->failed_)
    ZiLOG(Info, "zjrpc", ([value = call->value](auto &s) {
      s << "result=" << value;
    }));
  bool ok = !call->failed_ && client.stop();
  return ok;
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
  if (argc != 1 || (options.stdio && options.ws) || !options.host || !options.port ||
      options.port > UINT16_MAX) usage(1);

  ZiLog::init("zjrpc");
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  ZiMultiplex mx{mxParams()};
  if (!mx.start()) return 1;
  App app;
  bool ok = false;
  if (options.stdio) {
    Zjrpc::IOClient<App, ExampleCatalog> client;
    auto config = Zjrpc::StdioConfig{}
      .rxThread("stdioRx").txThread("stdioTx");
    if (client.init(&mx, ZuMv(config), &app) && client.start())
      ok = run(client, options, app);
    client.final();
  } else if (options.ws) {
    Zjrpc::WSClient<App, ExampleCatalog> client;
    Zws::URI uri;
    auto text = ZtScratch(TextScratch, options.host.length() +
	ZuBox<unsigned>{options.port}.length() + ZuStringT<"ws://:/rpc">{}().length());
    text << "ws://" << options.host << ':' << options.port << "/rpc";
    if (Zws::URI::parse(uri, text).ok() && client.init(
	Zhttp::HubConfig{&mx}, Zhttp::TCPConfig{}, Zjrpc::WSConfig{}, &app) &&
	client.start() && client.connect(uri, "jrpc"))
      ok = run(client, options, app);
    client.final();
  } else {
    Zjrpc::HTTPClient<App, ExampleCatalog> client;
    Zjrpc::ClientConfig config;
    config.endpoint("/rpc").http([](auto &http) { http.secure(false); });
    if (client.init(
        Zhttp::HubConfig{&mx},
        Zhttp::Destination{options.host, uint16_t(options.port)},
        ZuMv(config), &app) && client.start())
      ok = run(client, options, app);
    client.final();
  }
  mx.stop();
  ZiLog::stop();
  return ok ? 0 : 1;
}
