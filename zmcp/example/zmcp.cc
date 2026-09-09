//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <zlib/ZmHeap.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZfCLI.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>

#include <zlib/ZmcpClient.hh>

#include "zmcpexample.hh"

struct Options {
  ZuCSpan host{"127.0.0.1"};
  ZuCSpan token;
  int64_t lhs = 20;
  int64_t rhs = 22;
  unsigned port = 8080;
  bool stdio = false;
  bool stream = false;
  bool help = false;
};

ZfStruct(, (Options, CLI),
  (((stdio), (CLI::Long<"stdio">)), (Bool, false)),
  (((host), (CLI::Long<"host">)), (String, "127.0.0.1")),
  (((port), (CLI::Opt<'p'>, CLI::Long<"port">)), (UInt32, 8080)),
  (((token), (CLI::Long<"token">)), (String)),
  (((lhs), (CLI::Long<"lhs">)), (Int64, 20)),
  (((rhs), (CLI::Long<"rhs">)), (Int64, 22)),
  (((stream), (CLI::Long<"stream">)), (Bool, false)),
  (((help), (CLI::Opt<'h'>, CLI::Long<"help">)), (Bool, false)));

static void usage(int code)
{
  ZiFile::stdErr() <<
    "Usage: zmcp [OPTION]...\n\n"
    "  --stdio          use inherited stdin/stdout instead of HTTP\n"
    "  --host=HOST      HTTP host, default 127.0.0.1\n"
    "  -p, --port=N     HTTP port, default 8080\n"
    "  --token=TOKEN    send 'Authorization: Bearer TOKEN'\n"
    "  --lhs=N          left operand, default 20\n"
    "  --rhs=N          right operand, default 22\n"
    "  --stream         call the SSE response alternative\n"
    "  -h, --help       show help\n";
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

  const Options *options = nullptr;
  ZmSemaphore ready_;
  ZmSemaphore closed_;
  int era = Zmcp::Era::Unknown;
  bool failed_ = false;

  template <typename Key, typename L>
  void header(L &&l) const {
    if constexpr (Key{}() == "authorization") {
      if (!options->token) return;
      ZtString<> value;
      value << "Bearer " << options->token;
      l(value);
    }
  }

  void ready(int era_) { era = era_; ready_.post(); }
  void closed() { closed_.post(); }
  void failed() {
    failed_ = true;
    ready_.post();
    closed_.post();
  }
};

template <typename Heap>
struct Call_ : public Heap, public ZmObject {
  ZmSemaphore done;
  int64_t value = 0;
  bool failed_ = false;

  template <typename Reply>
  void process(const Reply &reply) {
    if constexpr (ZuIsSame<typename Reply::Response, AddOK>{})
      value = reply.body.value;
    else
      failed_ = true;
    done.post();
  }
  void failed(const Zmcp::Error &) { failed_ = true; done.post(); }
  void failed() { failed_ = true; done.post(); }
};
using CallHeap = ZmHeap<"ZmcpExample.Call", Call_<ZuVoid>>;
struct Call : public Call_<CallHeap> { };

template <typename Client>
static bool run(Client &client, const Options &options, App &app)
{
  if (!client.start()) return false;
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
    ZiLOG(Info, "zmcp", ([value = call->value](auto &s) {
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
    ZiFile::stdErr() << e << "\n";
    usage(1);
  }
  if (options.help) usage(0);
  if (argc != 1 || !options.host || !options.port ||
      options.port > UINT16_MAX) usage(1);

  ZiLog::init("zmcp");
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  ZiMultiplex mx{mxParams()};
  if (!mx.start()) return 1;
  App app{&options};
  bool ok = false;
  if (options.stdio) {
    Zmcp::Client<App, ExampleCatalog> client;
    auto config = Zmcp::StdioConfig{}
      .rxThread("stdioRx").txThread("stdioTx");
    if (client.init(&mx, ZuMv(config), &app))
      ok = run(client, options, app);
    client.final();
  } else {
    Zmcp::HTTPClient<App, ExampleCatalog> client;
    Zmcp::ClientConfig config;
    config.secure(false);
    if (client.init(
        Zhttp::HubConfig{&mx},
        Zhttp::Destination{options.host, uint16_t(options.port)},
        ZuMv(config), &app))
      ok = run(client, options, app);
    client.final();
  }
  mx.stop();
  ZiLog::stop();
  return ok ? 0 : 1;
}
