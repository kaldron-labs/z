//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// WebSocket small-message latency and allocation benchmark

#include <stdlib.h>

#include <iostream>

#include <zlib/ZfCLI.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>

#include <zlib/ZtcHeap.hh>

#include <zlib/Zws.hh>

#include "ZhttpTestUtil.hh"

namespace ZwsBench_ {

using Zhttp::Test::TempDir;
using Zhttp::Test::loopbackPort;

struct Options {
  uint32_t	count = 10000;
  uint32_t	size = 32;
  uint32_t	warmup = 1000;
  bool		binary = false;
  bool		control = false;
  bool		tls = false;
  bool		txOwner = false;
  bool		help = false;
};

ZfStruct((Options, CLI),
  (((count),    (CLI::Opt<'n'>, CLI::Long<"count">)),     (UInt32, 10000)),
  (((size),     (CLI::Opt<'s'>, CLI::Long<"size">)),      (UInt32, 32)),
  (((warmup),   (CLI::Opt<'w'>, CLI::Long<"warmup">)),    (UInt32, 1000)),
  (((binary),   (CLI::Long<"binary">)),                   (Bool, false)),
  (((control),  (CLI::Long<"control">)),                  (Bool, false)),
  (((tls),      (CLI::Long<"tls">)),                      (Bool, false)),
  (((txOwner),  (CLI::Long<"tx-owner">)),                 (Bool, false)),
  (((help),     (CLI::Flag<'h'>, CLI::Long<"help">)),     (Bool, false)));

void usage(int code = 1)
{
  std::cerr <<
    "Usage: ZwsBench [OPTION]...\n\n"
    "  -n, --count=N       measured round trips, default 10000\n"
    "  -s, --size=N        payload bytes, default 32\n"
    "  -w, --warmup=N      unmeasured round trips, default 1000\n"
    "  --binary            use binary rather than text messages\n"
    "  --control           measure ping/pong (size must be <= 125)\n"
    "  --tls               use wss/TLS rather than ws/TCP\n"
    "  --tx-owner          initiate client Tx on the Tx shard\n"
    "  -h, --help          show help\n" << std::flush;
  ::exit(code);
}

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

struct HeapStats {
  uint64_t heapAllocs = 0;
  uint64_t cacheAllocs = 0;
  uint64_t frees = 0;
};

HeapStats heapStats()
{
  HeapStats stats;
  Ztc::HeapMgr::all(Ztc::HeapMgr::AllFn{[&stats](Ztc::Heap *heap) {
    Ztc::HeapTelemetry data;
    heap->telemetry(data);
    stats.heapAllocs += data.heapAllocs;
    stats.cacheAllocs += data.cacheAllocs;
    stats.frees += data.frees;
  }});
  return stats;
}

struct State {
  using Bytes = ZtArray<uint8_t, ZtArrayHeapID<"Zws.Bench.Payload">>;
  using Samples = ZtArray<uint64_t, ZtArrayHeapID<"Zws.Bench.Samples">>;

  ZmSemaphore		listening;
  ZmSemaphore		done;
  Bytes			payload;
  Samples		samples;
  HeapStats		before;
  ZuTime		sent;
  unsigned		port = 0;
  unsigned		count = 0;
  unsigned		warmup = 0;
  unsigned		completed = 0;
  uint64_t		rxBytes = 0;
  uint64_t		appCopyBytes = 0;
  Zws::Opcode::T	opcode = Zws::Opcode::Text;
  bool			control = false;
  bool			txOwner = false;
  bool			failed = false;

  bool measuring() const { return completed >= warmup; }
  bool finished() const { return completed >= warmup + count; }
};

struct ServerApp {
  struct LinkState {
    unsigned			offset = 0;
    Zws::Opcode::T	opcode = Zws::Opcode::Binary;
  };

  State *state;

  void listening(const ZiListenInfo &) { state->listening.post(); }
  void listening() { state->listening.post(); }
  void listenFailed(bool) {
    state->failed = true;
    state->done.post();
  }
  bool accept(
      auto &, ZuCSpan, ZuCSpan target, ZuCSpan,
      Zws::HandshakeString &) {
    return target == "/bench";
  }
  void connected(auto &, const Zhttp::ConnectedInfo &) { }
  int process(auto &link, auto &rx) {
    auto &linkState = link.state();
    auto events = rx.events();
    if (events & Zi::RxEvent::Start()) {
      linkState.offset = 0;
      linkState.opcode = link.messageOpcode();
    }
    while (rx.input()) {
      int64_t n = rx.consume(
	[](ZuBSpan span) -> int64_t { return span.length(); },
	[this, &linkState](ZuBSpan span) {
	  unsigned n = span.length();
	  if (linkState.offset + n > state->payload.length() ||
	      span != ZuBSpan{
		state->payload.data() + linkState.offset, n})
	    state->failed = true;
	  linkState.offset += n;
	  state->rxBytes += n;
	});
      if (n <= 0) break;
    }
    events |= rx.events();
    if (events & Zi::RxEvent::Error()) return -1;
    if (events & Zi::RxEvent::Final()) {
      if (linkState.offset != state->payload.length()) return -1;
      link.txStream([this](auto &tx) {
	tx << state->payload;
	tx.flush();
      }, linkState.opcode);
    }
    return 1;
  }
  void disconnected(auto &, bool) { }
  void error(auto &, Zws::Failure::T) {
    state->failed = true;
    state->done.post();
  }
};

struct ClientApp {
  struct LinkState {
    unsigned offset = 0;
  };

  State *state;

  void connected(auto &link, const Zhttp::ConnectedInfo &) {
    if (!state->warmup) state->before = heapStats();
    send_(link);
  }

  int process(auto &link, auto &rx) {
    auto &linkState = link.state();
    auto events = rx.events();
    if (events & Zi::RxEvent::Start()) linkState.offset = 0;
    while (rx.input()) {
      int64_t n = rx.consume(
	[](ZuBSpan span) -> int64_t { return span.length(); },
	[this, &linkState](ZuBSpan span) {
	  unsigned n = span.length();
	  if (linkState.offset + n > state->payload.length() ||
	      span != ZuBSpan{
		state->payload.data() + linkState.offset, n})
	    state->failed = true;
	  linkState.offset += n;
	  state->rxBytes += n;
	});
      if (n <= 0) break;
    }
    events |= rx.events();
    if (events & Zi::RxEvent::Error()) return -1;
    if (events & Zi::RxEvent::Final()) complete_(link);
    return state->failed ? -1 : 1;
  }

  void pong(auto &link, ZuBSpan payload) {
    if (payload != state->payload) state->failed = true;
    complete_(link);
  }

  void disconnected(auto &, bool) { state->done.post(); }
  void connectFailed(auto &, bool) {
    state->failed = true;
    state->done.post();
  }
  void error(auto &, Zws::Failure::T) {
    state->failed = true;
    state->done.post();
  }

private:
  void complete_(auto &link) {
    ZuTime now = Zm::now();
    if (state->measuring())
      state->samples.push(uint64_t((now - state->sent).nanosecs()));
    ++state->completed;
    if (state->completed == state->warmup) {
      state->before = heapStats();
      state->rxBytes = 0;
      state->appCopyBytes = 0;
    }
    if (state->failed || state->finished())
      link.close();
    else
      send_(link);
  }

  void send_(auto &link) {
    state->sent = Zm::now();
    if (state->control) {
      link.ping(state->payload);
      return;
    }
    auto send = [this](auto &link_) {
      link_.txStream([this](auto &tx) {
	tx << state->payload;
	tx.flush();
      }, state->opcode);
    };
    if (!state->txOwner) {
      send(link);
      return;
    }
    link.app()->txRun([
      link = ZmMkRef(&link), send = ZuMv(send)]() mutable {
      send(*link);
    });
  }
};

int cmpSample(const void *l, const void *r)
{
  uint64_t l_ = *static_cast<const uint64_t *>(l);
  uint64_t r_ = *static_cast<const uint64_t *>(r);
  return l_ < r_ ? -1 : l_ > r_;
}

void report(const Options &options, State &state, const HeapStats &after)
{
  unsigned n = state.samples.length();
  if (!n) return;
  qsort(state.samples.data(), n, sizeof(uint64_t), cmpSample);
  uint64_t total = 0;
  for (unsigned i = 0; i < n; ++i) total += state.samples[i];
  auto percentile = [&state, n](unsigned p) {
    unsigned i = ((uint64_t(n) * p + 99) / 100);
    return state.samples[i ? i - 1 : 0];
  };
  std::cout <<
    "transport=" << (options.tls ? "tls" : "tcp") <<
    " tx-path=" << (options.txOwner ? "tx-owner" : "caller") <<
    " kind=" << (options.control ? "ping/pong" :
      options.binary ? "binary" : "text") <<
    " size=" << options.size << " samples=" << n << '\n' <<
    "latency-ns mean=" << (total / n) <<
    " p50=" << percentile(50) <<
    " p90=" << percentile(90) <<
    " p99=" << percentile(99) <<
    " max=" << state.samples[n - 1] << '\n' <<
    "heap-allocs=" << (after.heapAllocs - state.before.heapAllocs) <<
    " cache-allocs=" << (after.cacheAllocs - state.before.cacheAllocs) <<
    " frees=" << (after.frees - state.before.frees) <<
    " rx-bytes=" << state.rxBytes <<
    " application-copy-bytes=" << state.appCopyBytes << '\n';
}

template <typename Profile>
int run(const Options &options, const TempDir &temp)
{
  State state;
  state.port = loopbackPort();
  state.count = options.count;
  state.warmup = options.warmup;
  state.control = options.control;
  state.txOwner = options.txOwner;
  state.opcode =
    options.binary ? Zws::Opcode::Binary : Zws::Opcode::Text;
  state.samples.size(options.count);
  state.payload.length(options.size, false);
  const unsigned payloadLen = state.payload.length();
  uint8_t *payloadData = state.payload.data();
  for (unsigned i = 0; i < payloadLen; ++i)
    payloadData[i] = 'a' + (i % 26);

  ZiMultiplex mx{mxParams()};
  if (!state.port || !mx.start()) return 1;
  ServerApp serverApp{&state};
  ClientApp clientApp{&state};
  Zws::Server<ServerApp, Profile> server{
    &serverApp, ZiIP{"127.0.0.1"}, state.port};
  Zws::Client<ClientApp, Profile> client{&clientApp};
  typename Zws::Server<ServerApp, Profile>::Config serverConfig;
  typename Zws::Client<ClientApp, Profile>::Config clientConfig;
  if constexpr (ZuIsSame<Profile, Zhttp::H1TLS>{}) {
    serverConfig.certPath(temp.certPath).keyPath(temp.keyPath);
    clientConfig.caPath(temp.certPath);
  }
  Zhttp::EngineConfig engine{&mx, "3", "4"};
  bool serverInit = server.init(engine, serverConfig);
  bool clientInit = client.init(engine, clientConfig);
  bool serverStart = serverInit && server.start();
  bool clientStart = clientInit && client.start();
  if (!serverStart || !clientStart) {
    if (clientInit) client.final();
    if (serverInit) server.final();
    mx.stop();
    return 1;
  }

  if (state.listening.timedwait(Zm::now(10))) state.failed = true;
  using Link = typename Zws::Client<ClientApp, Profile>::Link;
  ZmRef<Link> link;
  if (!state.failed) {
    Zws::URI uri;
    ZtString<> text;
    text << (options.tls ? "wss" : "ws") <<
      "://127.0.0.1:" << state.port << "/bench";
    if (!Zws::URI::parse(uri, text).ok())
      state.failed = true;
    else {
      link = new Link{&client, uri};
      link->connect();
      if (state.done.timedwait(Zm::now(60))) state.failed = true;
    }
  }

  HeapStats after = heapStats();
  server.stopAccepting();
  ZmSemaphore stopped;
  bool clientStop = false;
  bool serverStop = false;
  client.stop([&clientStop, &stopped](bool ok) {
    clientStop = ok;
    stopped.post();
  });
  server.stop([&serverStop, &stopped](bool ok) {
    serverStop = ok;
    stopped.post();
  });
  stopped.wait();
  stopped.wait();
  link = nullptr;
  client.final();
  server.final();
  mx.stop();
  if (!state.failed) report(options, state, after);
  return state.failed || !clientStop || !serverStop;
}

} // namespace ZwsBench_

using namespace ZwsBench_;

int main(int argc, char **argv)
{
  Options options;
  try {
    argc = ZfCLI::load(options, argc, argv);
  } catch (const ZeException &e) {
    std::cerr << e << '\n';
    usage();
  }
  if (options.help) usage(0);
  if (argc < 0 || argc != 1 || !options.count ||
      (options.control && options.size > Zws::MaxControl))
    usage();

  ZiLog::init("ZwsBench");
  ZiLog::level(Ze::Warning);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  TempDir temp;
  int rc = !temp.init() ? 1 :
    options.tls ?
      run<Zhttp::H1TLS>(options, temp) :
      run<Zhttp::H1TCP>(options, temp);
  ZiLog::stop();
  return rc;
}
