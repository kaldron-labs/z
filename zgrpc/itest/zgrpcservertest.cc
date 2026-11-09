//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZtcHeap.hh>
#include <zlib/ZiResolver.hh>
#include <zlib/ZiTx.hh>
#include <zlib/Zquic.hh>
#include <zlib/ZgrpcHTTP.hh>
#include "ZhttpTestUtil.hh"

using namespace ZuTestUtil;
using namespace Zgrpc;

#include "zgrpcwire.hh"

#include <zlib/ZgrpcClient.hh>
#include <zlib/ZgrpcServer.hh>

struct ServerStream : Unary {
  using Name = ZuStringT<"ServerStream">;
  enum { Kind = Zgrpc::Kind::ServerStream };
};
struct ClientStream : Unary {
  using Name = ZuStringT<"ClientStream">;
  enum { Kind = Zgrpc::Kind::ClientStream };
};
struct Duplex : Unary {
  using Name = ZuStringT<"Duplex">;
  enum { Kind = Zgrpc::Kind::Duplex };
};
struct Early : Duplex { using Name = ZuStringT<"Early">; };
using Methods = ZuTypeList<Unary, ServerStream, ClientStream, Duplex, Early>;
ZgrpcCatalogDerive(Matrix, Methods);
ZgrpcCatalogImpl(Matrix);

struct TestState : State {
  bool sensitive = false;
  unsigned policy = 0; // Tx only; inspected after endpoint and scheduler drain
};

static bool metadataHas(const Metadata &fields, ZuBSpan name, ZuBSpan value)
{
  for (const auto &field : fields.fields())
    if (fields.name(field) == name && fields.value(field) == value) return true;
  return false;
}

struct ClientApp {
  TestState *state = nullptr;
  bool neverIndex(ZuBSpan name) const {
    if (!state || name != "x-private-req") return false;
    if (!state->mx->invoked(4)) ++state->errors;
    state->policy |= 4;
    return true;
  }
};
template <typename M>
struct RPCStream : ZmObject, ZmNHeap<RPCStream<M>> {
friend inline ZuStringT<"Zgrpc.ServerTest.Stream"> ZmNHeapID(RPCStream *);
  TestState *state;
  ZmRef<ServerCall<M>> call;
  Decoded<Packet> last;
  ZmSemaphore cancelReady;
  Code::T cancelCode = Code::OK;
  RPCStream(TestState *s, ZmRef<ServerCall<M>> c) : state{s}, call{ZuMv(c)} { }
  bool message(Message<Packet> value) {
    if (!state->mx->invoked(3)) ++state->errors;
    if constexpr (ZuIsSame<M, Early>{})
      return call->send(ZuMv(value)) && call->finish();
    else if constexpr (M::Kind == Kind::Duplex) return call->send(ZuMv(value));
    else { last = ZuMv(value); return true; }
  }
  void peerEnd() {
    if (!state->mx->invoked(3)) ++state->errors;
    if constexpr (ZuIsSame<M, Early>{}) ++state->errors;
    else if constexpr (M::Kind == Kind::Duplex) {
      if (!call->finish()) ++state->errors;
    } else {
      Message<Packet> result{Packet{42, {}}};
      if (last.template is<Message<Packet>>()) result = ZuMv(last.template p<Message<Packet>>());
      if (!call->reply(ZuMv(result))) ++state->errors;
    }
  }
  void cancelled(Code::T code) {
    if (!state->mx->invoked(3)) ++state->errors;
    if constexpr (ZuIsSame<M, Early>{}) ++state->errors;
    cancelCode = code;
    cancelReady.post();
  }
};
struct RPCApp {
  TestState *state;
  ZmRef<RPCStream<Duplex>> duplex;
  unsigned requests = 0;
  void listening(int8_t, unsigned) { state->listening.post(); }
  void listenFailed(int8_t, bool) { ++state->errors; state->listening.post(); }
  template <typename M> Metadata headers(M *, Context &ctx) {
    Metadata fields;
    if (state->sensitive) {
      if (!ctx.metadata || !metadataHas(*ctx.metadata, "x-private-req", "request secret"))
	++state->errors;
      if (!fields.add("x-private-in", "initial secret")) ++state->errors;
    }
    return fields;
  }
  bool neverIndex(ZuBSpan name) const {
    unsigned bit = name == "x-private-in" ? 1 : name == "x-private-out" ? 2 : 0;
    if (!state || !bit) return false;
    if (!state->mx->invoked(4)) ++state->errors;
    state->policy |= bit;
    return true;
  }
  void request(Unary *, Context &, Message<Packet> message, ZmRef<ServerCall<Unary>> call) {
    ++requests;
    if (!state->mx->invoked(3)) ++state->errors;
    Metadata trailers;
    if (state->sensitive && !trailers.add("x-private-out", "trailing secret")) ++state->errors;
    if (!call->complete(Reply<Packet>{ZuMv(message), {}, ZuMv(trailers)})) ++state->errors;
  }
  void request(ServerStream *, Context &, Message<Packet> value, ZmRef<ServerCall<ServerStream>> call) {
    if (!state->mx->invoked(3)) ++state->errors;
    for (unsigned i = 0; i < 3; ++i)
      if (!call->send(Message<Packet>{value.object(), value.backing})) ++state->errors;
    if (!call->finish()) ++state->errors;
  }
  template <typename M> auto open(M *, Context &, ZmRef<ServerCall<M>> call) {
    if (!state->mx->invoked(3)) ++state->errors;
    auto observer = ZmRef<RPCStream<M>>{new RPCStream<M>{state, ZuMv(call)}};
    if constexpr (ZuIsSame<M, Duplex>{}) duplex = observer;
    return observer;
  }

};
struct Call : ZmObject, ZmNHeap<Call> {
friend inline ZuStringT<"Zgrpc.ClientTest.Call"> ZmNHeapID(Call *);
  explicit Call(TestState *s) : state{s} { }
  void headers(const Metadata &fields) {
    if (!state->mx->invoked(3) || (state->sensitive &&
	!metadataHas(fields, "x-private-in", "initial secret"))) ++state->errors;
    ++state->headers;
  }
  void process(Result<Packet> result) {
    if (state->sensitive &&
	(!metadataHas(result.metadata, "x-private-in", "initial secret") ||
	 !metadataHas(result.trailers, "x-private-out", "trailing secret"))) ++state->errors;
    if (!state->mx->invoked(3) || result.status.code != Code::OK ||
	!result.message.is<Message<Packet>>() || result.http.httpVersion != state->version) {
      log_("unexpected unary result: status=", int(result.status.code),
        " HTTP=", int(result.http.httpVersion), " expected=", int(state->version));
      ++state->errors;
    }
    if (result.message.is<Message<Packet>>()) {
      auto &message = result.message.p<Message<Packet>>();
      if (message.object().seq != 42 || message.object().payload != ZuBSpan{state->payload})
	++state->errors;
      state->result = ZuMv(result.message);
      ++state->received;
    }
    state->done.post();
  }
  bool message(Message<Packet> value) {
    if (!state->mx->invoked(3) || value.object().seq != 42 ||
        value.object().payload != ZuBSpan{state->payload}) ++state->errors;
    state->result = ZuMv(value);
    ++state->received;
    messageReady.post();
    return true;
  }
  void finished(Status status) {
    if (!state->mx->invoked(3) || status.code != state->expectedFailure) {
      log_("unexpected stream status ", int(status.code));
      ++state->errors;
    }
    if (status.code != Code::OK) ++state->failures;
    state->done.post();
  }
  ZmSemaphore messageReady;
  void failed(Failure failure) {
    if (!state->mx->invoked(3) || failure.code != state->expectedFailure) {
      log_("unexpected client failure ", int(failure.code));
      ++state->errors;
    }
    ++state->failures;
    state->done.post();
  }
  TestState *state;
};

struct Mode { enum { Normal, Expired, Ingress, Deadline, QueueBytes, QueueCount, Long, Discovery, Restart, Sensitive }; };
// Four stable heap samples over 64 MiB per direction, beyond input retention.
enum { LongMessages = 256, SampleMessages = LongMessages / 4 };

static uint64_t rpcAllocated()
{
  uint64_t count = 0;
  Ztc::HeapMgr::all(Ztc::HeapMgr::AllFn{[&count](Ztc::Heap *heap) {
    Ztc::HeapTelemetry data;
    heap->telemetry(data);
    ZuCSpan id{data.id};
    if (id.length() >= 6 && !memcmp(id.data(), "Zgrpc.", 6)) count += data.allocated();
  }});
  return count;
}

struct HeapUse {
  uint64_t count = 0, bytes = 0;
  void add(const Ztc::HeapTelemetry &data) {
    uint64_t allocs = data.cacheAllocs + data.heapAllocs;
    uint64_t frees = data.cacheFrees + data.heapFrees;
    // Transport counters can change while ACK/recovery work runs.
    if (frees > allocs) { transient = true; return; }
    count += allocs - frees;
    bytes += (allocs - frees) * data.size;
  }
  bool transient = false;
};
static void heapSample(bool h3, unsigned messages)
{
  HeapUse rpc, http, tls, quic, io, pb, other;
  Ztc::HeapMgr::all(Ztc::HeapMgr::AllFn{
    [&rpc, &http, &tls, &quic, &io, &pb, &other](Ztc::Heap *heap) {
      Ztc::HeapTelemetry data;
      heap->telemetry(data);
      ZuCSpan id{data.id};
      if (id.length() >= 6 && !memcmp(id.data(), "Zgrpc.", 6)) rpc.add(data);
      else if (id.length() >= 6 && !memcmp(id.data(), "Zhttp.", 6)) http.add(data);
      else if (id.length() >= 5 && !memcmp(id.data(), "Ztls.", 5)) tls.add(data);
      else if (id.length() >= 6 && !memcmp(id.data(), "Zquic.", 6)) quic.add(data);
      else if (id.length() >= 2 && !memcmp(id.data(), "Zi", 2)) io.add(data);
      else if (id.length() >= 4 && !memcmp(id.data(), "Zpb.", 4)) pb.add(data);
      else other.add(data);
    }});
  auto report = [h3, messages](ZuCSpan name, const HeapUse &use) {
    log_(h3 ? "H3" : "H2", " heap sample messages=", messages,
      " ", name, " live=", use.count, " bytes=", use.bytes,
      use.transient ? " transient counters" : "");
  };
  report("RPC", rpc); report("HTTP", http); report("TLS", tls);
  report("QUIC", quic); report("IO", io); report("PB", pb);
  report("other", other);
}

template <typename M = Unary>
static void wire(bool h3, bool empty, int mode = Mode::Normal, unsigned count = 3)
{
  ZuTestScopeRT(wire);
  Zhttp::Test::TempDir temp;
  ZuCheckRT(temp.init("ZgrpcWire"), (return));
  ZtString<> cert, key;
  ZuCheckRT(Zhttp::Test::writeLocalhostCert(temp, cert, key), (return));
  TestState state;
  state.sensitive = mode == Mode::Sensitive;
  bool queue = mode == Mode::QueueBytes || mode == Mode::QueueCount;
  bool serverQueue = queue && count;
  // A server producer refusal aborts the incomplete wire message with CANCEL.
  if (queue) state.expectedFailure = serverQueue ? Code::CANCELLED : Code::RESOURCE_EXHAUSTED;
  else if (mode == Mode::Ingress) state.expectedFailure = Code::CANCELLED;
  else if (mode == Mode::Expired || mode == Mode::Deadline) state.expectedFailure = Code::DEADLINE_EXCEEDED;
  state.version = h3 ? Zhttp::Version::H3 : Zhttp::Version::H2;
  if (!empty) {
    state.payload.length((256U << 10) + 7);
    memset(state.payload.data(), 0x71, state.payload.length());
  }
  ZiMultiplex mx{ZiMxParams{}
    .scheduler([](auto &s) {
    s.nThreads(4)
      .thread(1, [](auto &t) { t.isolated(1); })
      .thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); })
      .thread(4, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  state.mx = &mx;
  unsigned port = peerPort(h3);
  ZuCheckRT(port, ({ mx.stop(); return; }));
  uint64_t baseline = mode == Mode::Long ? rpcAllocated() : 0;
  if (mode == Mode::Long) heapSample(h3, 0);
  Zhttp::HubConfig hub{&mx, "3", "4"};
  RPCApp rpc{&state, {}};
  Zgrpc::Server<RPCApp, Matrix> server;
  Zgrpc::ServerConfig serverConfig;
  serverConfig.localIP(ZiIP{"127.0.0.1"}).port(port).bodyMax(uint64_t(-1))
    .retainedBodyMax(1U << 20);
  Limits queueLimits;
  queueLimits.maxQueue = mode == Mode::QueueCount ? 1 : 4;
  queueLimits.maxQueueBytes = mode == Mode::QueueBytes ? 10 : 100;
  if (serverQueue) serverConfig.limits(queueLimits);
  if (h3 || mode == Mode::Discovery)
    serverConfig.quic(Zhttp::QUICConfig{}.certPath(cert).keyPath(key));
  if (!h3 || mode == Mode::Discovery) serverConfig.tls(Zhttp::H2Config{}.tls([&cert, &key](auto &tls) {
    tls.certPath(cert).keyPath(key);
  })
    .policy(Zhttp::H2Policy::Force));
  bool initialized = server.init(hub, serverConfig, &rpc);
  ZuCheckRT(initialized);
  ClientApp app{&state};
  Zgrpc::Client<ClientApp, Matrix> client;
  ClientConfig config;
  if (mode == Mode::Ingress || mode == Mode::Deadline) { Limits limits; limits.maxPending = 1; config.limits(limits); }
  if (queue && !serverQueue) config.limits(queueLimits);
  config.bodyMax(uint64_t(-1)).retainedBodyMax(1U << 20)
    .maxRedirects(0).protocol(mode == Mode::Discovery ? Zhttp::ProtoPolicy::PreferH3 :
      h3 ? Zhttp::ProtoPolicy::ForceH3 : Zhttp::ProtoPolicy::DisableH3);
  bool cliInit = initialized && client.init(hub, 1, config, &app, {},
    Zhttp::H2Config{}.tls([&cert](auto &tls) {
      tls.caPath(cert);
    }).policy(Zhttp::H2Policy::Force),
    Zhttp::QUICConfig{}.caPath(cert));
  ZuCheckRT(cliInit);
  ZuCheckRT(mx.start(), (return));
  bool started = initialized && server.start();
  ZuCheckRT(started);
  bool listening = started && state.listening.timedwait(Zm::now(10)) == 0;
  ZuCheckRT(listening && !state.errors.load_());
  bool pooled = listening && cliInit &&
    client.pool(0, Zhttp::Destination{"localhost", uint16_t(port)});
  ZuCheckRT(pooled);
  bool cliUp = pooled && client.start();
  ZuCheckRT(cliUp);
  ZmSemaphore held, release;
  bool blocked = cliUp && (mode == Mode::Ingress || mode == Mode::Deadline);
  if (blocked) {
    mx.run([&held, &release]() { held.post(); release.wait(); }, 4);
    ZuCheckRT(held.timedwait(Zm::now(10)) == 0);
  }
  ZmRef<Call> call = new Call{&state};
  ZmRef<ClientCall<Unary>> token;
  bool admitted = false;
  if (cliUp) {
    CallOptions options;
    if (state.sensitive) ZuCheckRT(options.metadata.add("x-private-req", "request secret"));
    if (mode == Mode::Expired) options.timeout = 0;
    if (mode == Mode::Deadline) options.timeout = 10000000;
    if (blocked) {
      token = client.open<Unary>(0, call, ZuMv(options));
      admitted = bool(token);
      auto denied = client.open<Unary>(0, call);
      ZuCheckRT(!denied && client.pending() == 1);
      if (token && mode == Mode::Ingress) ZuCheckRT(token->cancel());
    } else {
      if constexpr (M::Kind == Kind::Unary)
        admitted = client.call<M>(0, Packet{42, state.payload}, call, ZuMv(options));
      else {
        auto stream = client.open<M>(0, call, ZuMv(options));
        admitted = bool(stream);
        if (stream) {
          unsigned n = queue ? 1 : M::Kind == Kind::ServerStream ? 1 : count;
          uint64_t allocated = 0;
          for (unsigned i = 0; i < n; ++i) {
            bool sent = stream->send(Packet{42, state.payload});
            ZuCheckRT(sent);
            if (!sent) break;
            if constexpr (M::Kind == Kind::Duplex) {
              bool received = call->messageReady.timedwait(Zm::now(15)) == 0;
              ZuCheckRT(received);
              if (!received) break;
              if (mode == Mode::Long && (i + 1) % SampleMessages == 0) {
                // Retire the echo's owner work before taking each heap sample.
                ZmSemaphore sampled;
                mx.run([&mx, &sampled]() {
                  mx.run([&sampled]() { sampled.post(); }, 3);
                }, 4);
                bool drained = sampled.timedwait(Zm::now(10)) == 0;
                ZuCheckRT(drained);
                if (!drained) break;
                heapSample(h3, i + 1);
                auto live = rpcAllocated();
                ZuCheckRT(live);
                if (!allocated) allocated = live;
                ZuCheckRT(live == allocated);
                ZuCheckRT(uint64_t(i + 1) * state.payload.length() > (1U << 20));
              }
            }
          }
          if constexpr (ZuIsSame<M, Early>{}) {
            ZuCheckRT(state.done.timedwait(Zm::now(15)) == 0);
            state.done.post();
            ZuCheckRT(!stream->send(Packet{}) && !stream->end());
          } else if (queue) {
            // The echoed warmup proves both retained producers are ready.
            // Submit on Tx: the pending-post bound cannot mask encoded limits.
            ZmSemaphore submitted;
            bool first = false, second = true;
            mx.run([&rpc, &submitted, &first, &second, serverQueue, stream]() {
              if (serverQueue) {
                first = rpc.duplex->call->send(Packet{42, {}});
                second = rpc.duplex->call->send(Packet{42, {}});
              } else {
                first = stream->send(Packet{42, {}});
                second = stream->send(Packet{42, {}});
              }
              submitted.post();
            }, 4);
            ZuCheckRT(submitted.timedwait(Zm::now(10)) == 0);
            ZuCheckRT(first && !second);
          } else ZuCheckRT(stream->end());
        }
      }
    }
  }
  ZuCheckRT(admitted);
  bool done = admitted && state.done.timedwait(Zm::now(15)) == 0;
  ZuCheckRT(done);
  if (blocked) {
    ZuCheckRT(client.pending() == 1); // logical failure does not bypass Tx drain
    if (token) {
      ZuCheckRT(!token->live() && !token->send(Packet{}) && !token->end() && !token->cancel());
    }
    release.post();
  }
  if (mode == Mode::Discovery && done) {
    // The first H2 response advertises this server's H3 endpoint. Only a new
    // call may switch transport; the completed request is never replayed.
    state.version = Zhttp::Version::H3;
    bool next = client.call<Unary>(0, Packet{42, state.payload}, call);
    ZuCheckRT(next);
    ZuCheckRT(next && state.done.timedwait(Zm::now(15)) == 0);
  }
  if (mode == Mode::Restart && done) {
    ZuCheckRT(client.stop());
    ZuCheckRT(server.stop());
    ZuCheckRT(!client.up() && !client.pending() && !server.up() && !server.pending());
    ZuCheckRT(!client.call<Unary>(0, Packet{42, state.payload}, call));
    bool listening = server.start() && state.listening.timedwait(Zm::now(10)) == 0;
    ZuCheckRT(listening);
    bool running = listening && client.start();
    ZuCheckRT(running);
    bool next = running && client.call<Unary>(0, Packet{42, state.payload}, call);
    ZuCheckRT(next);
    ZuCheckRT(next && state.done.timedwait(Zm::now(15)) == 0);
  }
  if (serverQueue && rpc.duplex) {
    ZuCheckRT(rpc.duplex->cancelReady.timedwait(Zm::now(10)) == 0);
    ZuCheckRT(rpc.duplex->cancelCode == Code::RESOURCE_EXHAUSTED);
  }
  if (cliUp) ZuCheckRT(client.stop());
  ZuCheckRT(!client.pending());
  client.final();
  if (started) ZuCheckRT(server.stop());
  server.final();
  rpc.duplex = nullptr;
  ZiResolver::stop();
  ZiResolver::final();
  ZuCheckRT(mx.stop());
  ZuCheckRT(!state.errors.load_());
  if (mode == Mode::Normal || mode == Mode::Long ||
      mode == Mode::Discovery || mode == Mode::Restart || mode == Mode::Sensitive) {
    bool twice = mode == Mode::Discovery || mode == Mode::Restart;
    unsigned expected = twice ? 2 :
      M::Kind == Kind::ServerStream ? 3 : M::Kind == Kind::Duplex ? count : 1;
    if (twice) ZuCheckRT(rpc.requests == 2);
    if (state.sensitive) ZuCheckRT(state.policy == 7 && state.headers == 1);
    ZuCheckRT(state.received == expected && !state.failures);
    if (state.result.is<Message<Packet>>()) {
      ZuCheckRT(state.result.p<Message<Packet>>().object().payload == ZuBSpan{state.payload});
    } else ZuCheckRT(!expected);
  } else ZuCheckRT(state.received == unsigned(queue) && state.failures == 1);
  // Release retained receive backing before its mux allocator is destroyed.
  state.result = {};
  if (mode == Mode::Long) {
    call = nullptr;
    heapSample(h3, LongMessages + 1);
    ZuCheckRT(rpcAllocated() == baseline);
  }

}

static void initFinal()
{
  ZuTestScope(initFinal);
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(2).thread(1, [](auto &t) { t.isolated(1); })
      .thread(2, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  ClientApp app;
  Zgrpc::Client<ClientApp, Matrix> client;
  ZuCheck(client.init(Zhttp::HubConfig{&mx}, 1, ClientConfig{}, &app));
  client.final(); // no scheduler thread is running to service a posted stop
  ZuCheck(!client.up() && !client.pending());
  RPCApp rpc{nullptr, {}};
  Zgrpc::Server<RPCApp, Matrix> server;
  ServerConfig config;
  Zhttp::Test::TempDir temp;
  ZtString<> cert, key;
  ZuCheck(temp.init("ZgrpcInit"), (return));
  ZuCheck(Zhttp::Test::writeLocalhostCert(temp, cert, key), (return));
  config.port(1).tls(Zhttp::H2Config{}.tls([&cert, &key](auto &tls) {
    tls.certPath(cert).keyPath(key);
  }));
  ZuCheck(server.init(Zhttp::HubConfig{&mx}, config, &rpc));
  ZuCheck(mx.start(), (return));
  server.final();
  ZuCheck(!server.up() && !server.pending());
  ZuCheck(mx.stop());
}

static void inputFailure(Code::T transport)
{
  ZuTestScope(inputFailure);
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(4);
    for (unsigned i = 1; i <= 4; ++i) s.thread(i, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  TestState state;
  state.mx = &mx;
  RPCApp app{&state, {}};
  Zgrpc::Server<RPCApp, Matrix> server;
  Zhttp::Test::TempDir temp;
  ZtString<> cert, key;
  ZuCheck(temp.init("ZgrpcDiagnostic"), (return));
  ZuCheck(Zhttp::Test::writeLocalhostCert(temp, cert, key), (return));
  ServerConfig config;
  config.port(1).tls(Zhttp::H2Config{}.tls([&cert, &key](auto &tls) {
    tls.certPath(cert).keyPath(key);
  }));
  bool initialized = server.init(Zhttp::HubConfig{&mx, "3", "4"}, config, &app);
  ZuCheck(initialized);
  ZuCheck(mx.start(), (return));
  if (initialized) {
    using Work = ServerWorkT<RPCApp, Matrix, Unary>;
    ZmRef<Work> work = new Work{&server, 1, {}, Code::OK};
    mx.run([work = ZuMv(work), &state, transport]() {
      ZmRef<RxInput> input = new RxInput{false, false, Limits{}};
      input->route(work->rx);
      input->callbacks({}, {}, [work, &state, transport](RxInput &input, bool ok) {
	auto expected = transport == Code::INTERNAL ? Code::INTERNAL : Code::UNIMPLEMENTED;
	if (!state.mx->invoked(3) || ok || input.code() != expected) ++state.errors;
	++state.failures;
	work->finished(input, ok);
	state.done.post();
      });
      work->input = input;
      ZuCheck(!input->meta().header(Zhttp::FieldSection::Final, "grpc-encoding", "gzip"));
      ZuCheck(!input->failed() && input->meta().failed());
      work->tx.run([work, transport]() {
	if (transport != Code::OK) work->failFn(work.ptr(), transport);
	work->doneFn(work.ptr(), false);
	work->doneFn(work.ptr(), false);
      });
    }, 3);
    ZuCheck(state.done.timedwait(Zm::now(10)) == 0);
    ZmSemaphore drained;
    mx.run([&mx, &drained]() {
      mx.run([&drained]() { drained.post(); }, 3);
    }, 4);
    ZuCheck(drained.timedwait(Zm::now(10)) == 0);
    server.final();
  }
  ZuCheck(mx.stop());
  ZuCheck(state.failures == 1 && !state.errors.load_());
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZiTestResidue::init("zgrpcservertest");
  ZuTestMain();
  ZuTestCall(initFinal);
  ZuTestCall(inputFailure, Code::OK);
  ZuTestCall(inputFailure, Code::CANCELLED);
  ZuTestCall(inputFailure, Code::INTERNAL);
  ZuTestCall((wire<>), false, false, Mode::Sensitive);
  ZuTestCall((wire<>), true, false, Mode::Sensitive);
  ZuTestCall((wire<>), false, false, Mode::Restart);
  ZuTestCall((wire<>), true, false, Mode::Restart);
  ZuTestCall((wire<>), false, false);
  ZuTestCall((wire<>), false, true);
  ZuTestCall((wire<>), true, false);
  ZuTestCall((wire<>), true, true);
  ZuTestCall((wire<>), false, false, Mode::Discovery);
  ZuTestCall((wire<>), false, true, Mode::Expired);
  ZuTestCall((wire<>), true, true, Mode::Expired);
  ZuTestCall((wire<>), false, true, Mode::Ingress);
  ZuTestCall((wire<>), true, true, Mode::Ingress);
  ZuTestCall((wire<>), false, true, Mode::Deadline);
  ZuTestCall((wire<>), true, true, Mode::Deadline);
  ZuTestCall((wire<ServerStream>), false, false);
  ZuTestCall((wire<ServerStream>), true, false);
  ZuTestCall((wire<ClientStream>), false, false);
  ZuTestCall((wire<ClientStream>), true, false);
  ZuTestCall((wire<ClientStream>), false, true, Mode::Normal, 0);
  ZuTestCall((wire<ClientStream>), true, true, Mode::Normal, 0);
  ZuTestCall((wire<Duplex>), false, false);
  ZuTestCall((wire<Duplex>), true, false);
  ZuTestCall((wire<Duplex>), false, false, Mode::Long, LongMessages);
  ZuTestCall((wire<Duplex>), true, false, Mode::Long, LongMessages);
  ZuTestCall((wire<Duplex>), false, true, Mode::Normal, 0);
  ZuTestCall((wire<Duplex>), true, true, Mode::Normal, 0);
  ZuTestCall((wire<Duplex>), false, true, Mode::QueueBytes, 0);
  ZuTestCall((wire<Duplex>), true, true, Mode::QueueBytes, 0);
  ZuTestCall((wire<Duplex>), false, true, Mode::QueueCount, 0);
  ZuTestCall((wire<Duplex>), true, true, Mode::QueueCount, 0);
  ZuTestCall((wire<Duplex>), false, true, Mode::QueueBytes, 1);
  ZuTestCall((wire<Duplex>), true, true, Mode::QueueBytes, 1);
  ZuTestCall((wire<Duplex>), false, true, Mode::QueueCount, 1);
  ZuTestCall((wire<Duplex>), true, true, Mode::QueueCount, 1);
  ZuTestCall((wire<Early>), false, false, Mode::Normal, 1);
  ZuTestCall((wire<Early>), true, false, Mode::Normal, 1);

}
