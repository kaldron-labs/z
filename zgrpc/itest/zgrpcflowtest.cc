//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// A transport-stalled response beside an active RPC on the same connection

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZiResolver.hh>
#include <zlib/ZiTx.hh>
#include <zlib/Zquic.hh>
#include <zlib/ZgrpcHTTP.hh>
#include "ZhttpTestUtil.hh"

using namespace ZuTestUtil;
using namespace Zgrpc;

#include "zgrpcwire.hh"
#include <zlib/ZgrpcServer.hh>

struct Flow : Unary {
  using Name = ZuStringT<"Flow">;
  enum { Kind = Zgrpc::Kind::ServerStream };
};
using Methods = ZuTypeList<Flow>;
ZgrpcCatalogDerive(Matrix, Methods);
ZgrpcCatalogImpl(Matrix);

// The response exceeds stream credit; connection credit permits the live RPC.
enum { StreamWindow = 4096, PayloadBytes = 256U << 10 };
struct FlowState {
  ZiMultiplex *mx = nullptr;
  ZmSemaphore listening, blocked, live, stalled, result;
  ZmAtomic<unsigned> errors = 0;
  Zhttp::Session blockedPeer, livePeer;
  Zhttp::Version::T version = Zhttp::Version::H2;
  Bytes payload;
  unsigned blockedCalls = 0;
  ZmAtomic<unsigned> messages = 0, liveEnds = 0, stalledEnds = 0;
  ZmAtomic<unsigned> results = 0, retired = 0;
  ZmAtomic<unsigned> requests = 0, connected = 0, producers = 0;
  ZmAtomic<uint64_t> heldBytes = 0;
};
struct RPCApp {
  FlowState *state;
  void listening(int8_t, unsigned) { state->listening.post(); }
  void listenFailed(int8_t, bool) { ++state->errors; state->listening.post(); }
  void request(Flow *, Context &, Message<Packet> value, ZmRef<ServerCall<Flow>> call) {
    ++state->requests;
    if (!state->mx->invoked(3) || !call->send(ZuMv(value)) || !call->finish())
      ++state->errors;
  }
};
struct ClientRx : ZmObject, ZmNHeap<ClientRx> {
friend inline ZuStringT<"Zgrpc.Flow.ClientRx"> ZmNHeapID(ClientRx *);
  FlowState *state;
  uint64_t sequence;
  ClientRx(FlowState *s, uint64_t n) : state{s}, sequence{n} { }
  void accepted(Zhttp::Channel *channel, RxInput &) {
    if (!state->mx->invoked(5)) ++state->errors;
    if (sequence == 1) state->blockedPeer = channel->session();
    else state->livePeer = channel->session();
  }
  bool message(Message<Packet> value) {
    if (!state->mx->invoked(5) || sequence == 1 || value.object().seq != sequence ||
	value.object().payload != ZuBSpan{"live"}) ++state->errors;
    ++state->messages;
    return true;
  }
  void finished(RxInput &input, bool ok) {
    if (!state->mx->invoked(5)) ++state->errors;
    if (sequence == 1) {
      if (ok) ++state->errors;
      ++state->stalledEnds;
      state->stalled.post();
    } else {
      if (!ok || input.meta().status().code != Code::OK) ++state->errors;
      ++state->liveEnds;
      state->live.post();
    }
  }
};
struct Request : ZmObject, ReqBuilder<Request, Flow> {
  FlowState *state = nullptr;
  void connected(Zhttp::Channel *, const Zhttp::ConnectedInfo &) { ++state->connected; }
  void producerStarted() {
    ++state->producers;
    if (!state->mx->invoked(6)) ++state->errors;
    auto bytes = sequence == 1 ? ZuBSpan{state->payload} : ZuBSpan{"live"};
    if (!send_(Packet{sequence, bytes}) || !end_()) ++state->errors;
  }
  void completed(const Zhttp::Result &result) {
    if (!state->mx->invoked(5) || result.httpVersion != state->version ||
	(sequence == 1 ? result.code != Zhttp::ResultCode::Cancelled : !result.ok()))
      ++state->errors;
    ++state->results;
    state->result.post();
  }
  void retired() { ++state->retired; }
};
using Requests = ZmPQueue<Request, ZmPQueueOverlap<false,
  ZmPQueueNode<Request, ZmPQueueHeapID<"Zgrpc.Flow.Request">>>>;
struct ReplyParser : ResParser<ReplyParser, Flow> {
  using Base = ResParser<ReplyParser, Flow>;
  FlowState *state = nullptr;
  bool stall = false;
  void init(Request &request) {
    state = request.state;
    stall = request.sequence == 1;
    Base::init(ZmRef<ClientRx>{new ClientRx{state, request.sequence}});
  }
  template <typename Rx> bool body(Rx &rx) {
    if (!stall) return Base::body(rx);
    // Leave every byte in the native receive queue, withholding stream credit.
    // No scheduler thread is held: other streams on this connection can run.
    state->heldBytes = rx.length();
    if (!state->blockedCalls++) state->blocked.post();
    return true;
  }
  void reset() { Base::reset(); state = nullptr; stall = false; }
};
struct Client;
struct Pool;
using TxQ = ZmPQTx<Pool, Requests, ZmPQTxOrdered<false>>;
struct Pool : ZmNHeap<Pool>, Zhttp::Pool<Client, TxQ, ReplyParser, Selection> {
friend inline ZuStringT<"Zgrpc.Flow.Pool"> ZmNHeapID(Pool *);
  using Base = Zhttp::Pool<Client, TxQ, ReplyParser, Selection>;
  explicit Pool(Client *client) : Base{client} { }
  Requests *txQueue() { return &requests; }
  void archive_(Requests::Node *) { }
  ZmRef<Requests::Node> retrieve_(Requests::Key, Requests::Key) { return {}; }
  Requests requests;
};
struct Client : Zhttp::Client<Client, Pool> { };

static void flow(bool h3)
{
  ZuTestScope(flow);
  Zhttp::Test::TempDir temp;
  ZtString<> cert, key;
  ZuCheck(temp.init("ZgrpcFlow") && Zhttp::Test::writeLocalhostCert(temp, cert, key), (return));
  // Retention is bounded by StreamWindow.
  ZiMultiplex mx{ZiMxParams{}
    .scheduler([](auto &s) {
    s.nThreads(6);
    for (unsigned i = 1; i <= 6; ++i) s.thread(i, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  FlowState state;
  state.mx = &mx;
  state.version = h3 ? Zhttp::Version::H3 : Zhttp::Version::H2;
  state.payload.length(PayloadBytes);
  memset(state.payload.data(), 0x71, state.payload.length());
  unsigned port = peerPort(h3);
  ZuCheck(port, ({ mx.stop(); return; }));
  RPCApp app{&state};
  Zgrpc::Server<RPCApp, Matrix> server;
  ServerConfig config;
  config.localIP(ZiIP{"127.0.0.1"}).port(port).bodyMax(uint64_t(-1))
    .retainedBodyMax(2 * PayloadBytes);
  if (h3) config.quic(Zhttp::QUICConfig{}.certPath(cert).keyPath(key));
  else config.tls(Zhttp::H2Config{}.tls([&cert, &key](auto &tls) {
    tls.certPath(cert).keyPath(key);
  }));
  bool initialized = server.init(Zhttp::HubConfig{&mx, "3", "4"}, config, &app);
  ZuCheck(initialized);
  Client client;
  Zhttp::Config clientConfig;
  clientConfig.protocol(h3 ? Zhttp::ProtoPolicy::ForceH3 : Zhttp::ProtoPolicy::DisableH3)
    .links(1).linkMax(2).concurrency(2).bodyMax(uint64_t(-1))
    .retainedBodyMax(PayloadBytes).maxRetries(0).maxRedirects(0);
  bool cliInit = initialized && client.init(Zhttp::HubConfig{&mx, "5", "6"}, 1, clientConfig,
    {}, Zhttp::H2Config{}.tls([&cert](auto &tls) {
      tls.caPath(cert);
    }).policy(Zhttp::H2Policy::Force)
	.initialWindowSize(StreamWindow),
    Zhttp::QUICConfig{}.caPath(cert).maxStreamData(StreamWindow).maxData(PayloadBytes));
  ZuCheck(cliInit);
  bool pooled = cliInit &&
    client.pool(0, Zhttp::Destination{"localhost", uint16_t(port)});
  ZuCheck(pooled);
  ZuCheck(mx.start(), (return));
  bool started = initialized && server.start();
  ZuCheck(started);
  bool listening = started && state.listening.timedwait(Zm::now(10)) == 0;
  ZuCheck(listening && !state.errors.load_());
  bool cliUp = pooled && listening && client.start();
  ZuCheck(cliUp);
  if (cliUp) {
    auto send = [&client, &state](uint64_t sequence) {
      ZmRef<Requests::Node> request = new Requests::Node{};
      request->state = &state;
      request->sequence = sequence;
      return client.send(0, ZuMv(request));
    };
    ZuCheck(send(1));
    bool blocked = state.blocked.timedwait(Zm::now(15)) == 0;
    ZuCheck(blocked);
    if (blocked) {
      ZuCheck(send(2));
      ZuCheck(state.live.timedwait(Zm::now(15)) == 0);
      // The live RPC completed before any stalled response bytes were consumed.
      ZuCheck(state.blockedPeer && state.blockedPeer == state.livePeer &&
	state.messages == 1 && state.liveEnds == 1 && !state.stalledEnds &&
	state.heldBytes && state.heldBytes <= StreamWindow);
    }
    ZuCheck(client.cancel(0, 1));
    ZuCheck(state.stalled.timedwait(Zm::now(10)) == 0);
    ZuCheck(state.result.timedwait(Zm::now(10)) == 0);
    ZuCheck(state.result.timedwait(Zm::now(10)) == 0);
    ZuCheck(send(3));
    bool resumed = state.live.timedwait(Zm::now(15)) == 0;
    ZuCheck(resumed, log_("H3=", h3, " requests=", state.requests.load_(),
	" connected=", state.connected.load_(), " producers=", state.producers.load_()));
    ZuCheck(state.result.timedwait(Zm::now(10)) == 0);
    ZuCheck(state.blockedPeer == state.livePeer);
    client.stop();
  }
  client.final();
  if (started) ZuCheck(server.stop());
  ZuCheck(!server.pending());
  server.final();
  ZiResolver::stop(); ZiResolver::final();
  ZuCheck(mx.stop());
  ZuCheck(state.messages == 2 && state.liveEnds == 2 && state.stalledEnds == 1 &&
    state.results == 3 && state.retired == 3 && !state.errors.load_());
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZiTestResidue::init("zgrpcflowtest");
  ZuTestMain();
  ZuTestCall(flow, false);
  ZuTestCall(flow, true);
}
