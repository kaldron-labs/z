//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// HTTP small-message and held-response measurements. Reuses the message-hub
// workload, scheduler, certificate fixture and framework heap telemetry.

#include <stdio.h>
#include <stdlib.h>
#include <zlib/ZuBox.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZtcHeap.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiResolver.hh>
#include <zlib/ZhttpClient.hh>
#include <zlib/ZhttpServer.hh>
#include "ZhttpTestUtil.hh"

namespace ZhttpBench_ {

struct HeapStats {
  uint64_t calls = 0, live = 0;
};
HeapStats heaps(const char *phase = nullptr)
{
  HeapStats stats;
  bool dump = phase && getenv("ZHTTP_BENCH_HEAPS");
  Ztc::HeapMgr::all(Ztc::HeapMgr::AllFn{[&stats, phase, dump](Ztc::Heap *heap) {
    Ztc::HeapTelemetry data;
    heap->telemetry(data);
    stats.calls += data.cacheAllocs + data.heapAllocs;
    stats.live += data.allocated() * data.size;
    if (dump) {
      ZuCSpan id{data.id};
      printf("heap phase=%s id=%.*s size=%u calls=%lu live=%lu\n", phase,
	int(id.length()), id.data(), data.size,
	data.cacheAllocs + data.heapAllocs, data.allocated());
    }
  }});
  return stats;
}

using HeadersList = ZhttpHeaders("content-length");
ZhttpHdrCatalogDerive(Headers, HeadersList);
ZhttpHdrCatalogImpl(Headers)

struct Response : ZmObject, Zhttp::ResBuilder {
  using HdrCatalog = ZhttpBench_::Headers;
  Zhttp::BodyPolicy::T bodyPolicy() const { return Zhttp::BodyPolicy::Fixed; }
  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (Key{}() == "content-length") l(Zhttp::contentLengthPad());
  }
  template <typename L> void header(L &&) const { }
  template <typename Emit> void body(Emit &&emit) {
    emit([](auto &out) {
      out << ZuCSpan{"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"};
      return Zhttp::WriteOutcome::End;
    });
  }
  template <typename L> void bodyHdrs(L &&l) const { Zhttp::contentLengthSet(l, 64); }
};
using Responses = Zhttp::ResponseQ<Response, ZuStringT<"Zhttp.Bench.Response">>;
using Reply = Responses::Node;
using Route = Zhttp::Route<Reply>;

struct State {
  using Times = ZtArray<uint64_t, ZtArrayHeapID<"Zhttp.Bench.Times">>;
  using Routes = ZtArray<Route, ZtArrayHeapID<"Zhttp.Bench.Routes">>;
  ZmSemaphore done, ready, listening;
  Times samples;
  Routes held;
  unsigned expected = 0, completed = 0, errors = 0;
  uint64_t bytes = 0;
  bool hold = false;
  void finish() { if (++completed == expected) done.post(); }
};

template <unsigned Mask> struct Client;
template <unsigned Mask> struct Pool;
template <unsigned Mask> struct Server;

struct Request : ZmObject, Zhttp::ReqBuilder {
  State *state = nullptr;
  ZuTime sent;
  uint64_t id = 0;
  uint64_t key() const { return id; }
  uint64_t length() const { return 1; }
  void completed(const Zhttp::Result &result) {
    if (!result.ok()) ++state->errors;
    state->samples.push(uint64_t((Zm::now() - sent).nanosecs()));
    state->finish();
  }
};
struct ResParser : Zhttp::Parser {
  State *state = nullptr;
  void init(Request &request) { state = request.state; }
  template <typename Rx> bool body(Rx &input) {
    return Zhttp::bodyEach(input, [this](ZuSpan<uint8_t> bytes) {
      state->bytes += bytes.length();
    });
  }
};
struct Requests : ZmPQueue<Request, ZmPQueueOverlap<false,
  ZmPQueueNode<Request, ZmPQueueHeapID<"Zhttp.Bench.Request">>>> { };
template <unsigned Mask>
using Tx = ZmPQTx<Pool<Mask>, Requests, ZmPQTxOrdered<false>>;

template <unsigned> struct Selection;
template <> struct Selection<1> { using T = Zhttp::Profiles<Zhttp::H1TCP>; };
template <> struct Selection<2> { using T = Zhttp::Profiles<Zhttp::H1TLS>; };
template <> struct Selection<4> { using T = Zhttp::Profiles<Zhttp::H2TLS>; };
template <> struct Selection<8> { using T = Zhttp::Profiles<Zhttp::H3QUIC>; };
template <> struct Selection<6> { using T = Zhttp::Profiles<Zhttp::H1TLS, Zhttp::H2TLS>; };
template <> struct Selection<12> { using T = Zhttp::Profiles<Zhttp::H2TLS, Zhttp::H3QUIC>; };
template <> struct Selection<15> { using T = Zhttp::AllProfiles; };

template <unsigned Mask>
using PoolBase = Zhttp::Pool<Client<Mask>, Tx<Mask>, ResParser,
  typename Selection<Mask>::T>;
template <unsigned Mask>
struct Pool : ZmNHeap<Pool<Mask>>, PoolBase<Mask> {
  friend inline ZuStringT<"Zhttp.Bench.Pool"> ZmNHeapID(Pool *);
  Requests requests;
  Pool(Client<Mask> *client) : PoolBase<Mask>{client} { }
  Requests *txQueue() { return &requests; }
  void archive_(Requests::Node *) { }
  ZmRef<Requests::Node> retrieve_(uint64_t, uint64_t) { return {}; }
};
template <unsigned Mask>
struct Client : Zhttp::Client<Client<Mask>, Pool<Mask>> { };

template <unsigned Mask>
struct ReqParser : Zhttp::Parser {
  State *state = nullptr;
  void init(Server<Mask> &server) { state = server.state; }
  template <typename Link> void complete(Link *link, bool ok) {
    if (!ok) { ++state->errors; return; }
    Route route{link, Zhttp::Method::GET};
    if (state->hold) {
      state->held.push(ZuMv(route));
      if (state->held.length() == state->expected) state->ready.post();
    } else route.send(ZmRef<Reply>{new Reply});
  }
};
template <unsigned Mask>
using ServerBase = Zhttp::Server<Server<Mask>, ReqParser<Mask>, Responses,
  typename Selection<Mask>::T>;
template <unsigned Mask>
struct Server : ServerBase<Mask> {
  using ServerBase<Mask>::connected;
  using ServerBase<Mask>::disconnected;
  State *state;
  Server(State *state_) : state{state_} { }
  void connected(int8_t) { }
  void disconnected(int8_t) { }
  void listening(int8_t, unsigned) { state->listening.post(); }
  void listenFailed(int8_t, bool) { ++state->errors; state->listening.post(); }
};

int cmp(const void *a, const void *b)
{
  auto l = *static_cast<const uint64_t *>(a), r = *static_cast<const uint64_t *>(b);
  return l < r ? -1 : l > r;
}

template <unsigned Mask>
int run(const Zhttp::Test::TempDir &cert, unsigned count, unsigned jobs, unsigned selected = Mask)
{
  const bool tcp = selected == 1;
  const bool h3 = (selected & 8) != 0;
  const bool h2 = (selected & 4) != 0;
  State state;
  state.samples.size(count);
  state.held.size(jobs);
  ZiMultiplex mx{ZiMxParams().scheduler([](auto &s) {
    s.nThreads(4)
      .thread(1, [](auto &t) { t.isolated(1); })
      .thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); })
      .thread(4, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  if (!mx.start()) return 1;
  Zhttp::HubConfig hub{&mx, "3", "4"};
  Server<Mask> server{&state};
  Client<Mask> client;
  unsigned port = Zhttp::Test::loopbackPort(38300);
  auto cfg = Zhttp::ServerConfig{}.localIP(ZiIP{"127.0.0.1"}).port(port);
  if (tcp) cfg.tcp();
  if (selected & 6) cfg.tls(Zhttp::H2Config{}.certPath(cert.certPath.cspan())
    .keyPath(cert.keyPath.cspan()).policy(h2 ?
      Zhttp::H2Policy::Prefer : Zhttp::H2Policy::Disable));
  if (h3) cfg.quic(Zhttp::QUICConfig{}.certPath(cert.certPath.cspan())
    .keyPath(cert.keyPath.cspan()));
  auto policy = Zhttp::Config{}.secure(!tcp).tcp(tcp).tls(bool(selected & 6)).quic(h3)
    .concurrency(jobs).linkMax(jobs).links(1).blindH3(h3)
    .h2Policy(h2 ? Zhttp::H2Policy::Prefer : Zhttp::H2Policy::Disable)
    .protocol(h3 ? Zhttp::ProtoPolicy::ForceH3 : Zhttp::ProtoPolicy::DisableH3);
  bool initialized = server.init(hub, ZuMv(cfg)) && client.init(hub, 1, policy, {},
    Zhttp::H2Config{}.caPath(cert.certPath.cspan()),
    Zhttp::QUICConfig{}.caPath(cert.certPath.cspan()));
  if (!initialized || !client.pool(0, Zhttp::Destination{"127.0.0.1", uint16_t(port)}) ||
      !server.start() || !client.start()) return 1;
  unsigned listeners = unsigned(tcp) + unsigned(bool(selected & 6)) + unsigned(h3);
  for (unsigned i = 0; i < listeners; ++i)
    if (state.listening.timedwait(Zm::now(10))) return 1;
  auto drain = [&mx]() {
    ZmBlock<>{}([&mx](auto wake) {
      mx.run([&mx, wake = ZuMv(wake)]() mutable {
	mx.run(ZuMv(wake), mx.sid("4"));
      }, mx.sid("3"));
    });
  };
  uint64_t key = 0;
  auto wave = [&state, &client, &key](unsigned n) {
    state.expected = n;
    state.completed = 0;
    for (unsigned i = 0; i < n; ++i) {
      ZmRef<Requests::Node> request = new Requests::Node;
      request->state = &state;
      request->id = key++;
      request->sent = Zm::now();
      if (!client.send(0, ZuMv(request))) return false;
    }
    return true;
  };
  drain();
  auto cold = heaps("cold");
  for (unsigned i = 0; i < 10; ++i)
    if (!wave(jobs) || state.done.timedwait(Zm::now(30))) return 1;
  drain();
  state.samples.length(0);
  auto before = heaps("before");
  state.bytes = 0;
  auto begin = Zm::now();
  for (unsigned i = 0; i < count; i += jobs)
    if (!wave(jobs < count - i ? jobs : count - i) ||
	state.done.timedwait(Zm::now(30))) return 1;
  int64_t elapsed = (Zm::now() - begin).nanosecs();
  drain();
  auto after = heaps("after");
  uint64_t bytes = state.bytes;
  state.hold = true;
  auto idle = heaps("idle");
  if (!wave(jobs) || state.ready.timedwait(Zm::now(30))) return 1;
  drain();
  auto held = heaps("held");
  mx.run([&state]() {
    for (auto &route : state.held) route.send(ZmRef<Reply>{new Reply});
    state.held.length(0);
  }, mx.sid("3"));
  if (state.done.timedwait(Zm::now(30))) return 1;
  client.stop();
  server.stop();
  client.final();
  server.final();
  mx.stop();
  qsort(state.samples.data(), count, sizeof(uint64_t), cmp);
  printf("mask=%u jobs=%u samples=%u warmup=%u payload=64 "
    "p50_ns=%lu p99_ns=%lu elapsed_ns=%ld alloc_calls=%lu rx_bytes=%lu "
    "app_copy_bytes=0 idle_heap_bytes=%lu connection_heap_delta=%ld "
    "held_heap_delta=%ld errors=%u\n",
    selected, jobs, count, jobs * 10, state.samples[(count - 1) / 2],
    state.samples[(uint64_t(count) * 99 + 99) / 100 - 1], elapsed,
    after.calls - before.calls, bytes, idle.live, int64_t(idle.live - cold.live),
    int64_t(held.live - idle.live), state.errors);
  return state.errors || bytes != uint64_t(count) * 64;
}
} // namespace ZhttpBench_

int main(int argc, char **argv)
{
  if (argc != 4) {
    fputs("Usage: zhttpbench MASK COUNT JOBS\n"
      "MASK: 1=H1TCP 2=H1TLS 4=H2TLS 8=H3QUIC 6=sharedTLS 12=H2+H3 15=all\n",
      stderr);
    return 1;
  }
  unsigned mask = ZuBox<unsigned>{argv[1]};
  unsigned count = ZuBox<unsigned>{argv[2]};
  unsigned jobs = ZuBox<unsigned>{argv[3]};
  if (!count || !jobs || count < jobs) return 1;
  ZiLog::init("zhttpbench");
  ZiLog::level(Ze::Error);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZiResolver::start();
  ZiTestResidue::init("zhttpbench");
  Zhttp::Test::TempDir cert;
  int result = 1;
  if (cert.init()) switch (mask) {
#define ZhttpBench_Run(M) case M: result = ZhttpBench_::run<M>(cert, count, jobs); break
    ZhttpBench_Run(1); ZhttpBench_Run(2); ZhttpBench_Run(4); ZhttpBench_Run(8);
    ZhttpBench_Run(6); ZhttpBench_Run(12); ZhttpBench_Run(15);
#undef ZhttpBench_Run
  }
  ZiResolver::stop();
  ZiLog::stop();
  return result;
}
