//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZiResolver.hh>
#include <zlib/ZiTx.hh>
#include <zlib/Zquic.hh>
#include <zlib/ZgrpcHTTP.hh>
#include "ZhttpTestUtil.hh"

using namespace ZuTestUtil;
using namespace Zgrpc;

#include "ZgrpcWire.hh"

struct ClientRx : ZmObject, ZmNHeap<ClientRx> {
friend inline ZuStringT<"Zgrpc.Wire.ClientRx"> ZmNHeapID(ClientRx *);
  explicit ClientRx(State *value) : state{value} { }
  void accepted(Zhttp::Channel *, RxInput &) {
    if (!state->mx->invoked(3)) ++state->errors;
  }
  bool message(Message<Packet> value) {
    if (!state->mx->invoked(3)) ++state->errors;
    ++state->received;
    if (value.object().seq != 42 || value.object().payload != ZuBSpan{state->payload})
      ++state->errors;
    state->result = ZuMv(value);
    return true;
  }
  void finished(RxInput &input, bool ok) {
    if (!ok || input.code() != Code::OK) { log_("client receive failure ", int(input.code()), " metadata ", int(input.meta().error())); ++state->errors; }
    state->done.post();
  }
  State *state;
};
struct Request : ZmObject, ReqBuilder<Request, Unary> {
  using Base = ReqBuilder<Request, Unary>;
  ZuUnion<void, Zhttp::BodyEmitter> saved; // Tx-owned late-copy probe
  void body(Zhttp::BodyEmitter emit) { saved = emit; Base::body(ZuMv(emit)); }
  void close() {
    if (!state->mx->invoked(4)) ++state->errors;
    ++state->requestClosed;
    Base::close();
  }
  bool late() {
    if (!saved.is<Zhttp::BodyEmitter>()) return false;
    bool invoked = false;
    bool admitted = saved.p<Zhttp::BodyEmitter>()([&invoked](auto &) {
      invoked = true; return Zhttp::WriteOutcome::End;
    });
    saved = {};
    return !invoked && !admitted;
  }
  State *state = nullptr;
  void producerStarted() {
    if (!state->mx->invoked(4)) ++state->errors;
    if (!send_(Packet{42, state->payload}) || !end_()) ++state->errors;
  }
  void productionFailed(Code::T code) { log_("client output failure ", int(code)); ++state->errors; state->done.post(); }
  void completed(const Zhttp::Result &result) {
    if (!result.ok()) { log_("HTTP result ", int(result.code), " received ", result.responseBodyReceived); ++state->errors; state->done.post(); }
    if (result.httpVersion != state->version) ++state->errors;
  }
};
using Requests = ZmPQueue<Request, ZmPQueueOverlap<false,
  ZmPQueueNode<Request, ZmPQueueHeapID<"Zgrpc.Wire.Request">>>>;
struct ReplyParser : ResParser<ReplyParser, Unary> {
  using Base = ResParser<ReplyParser, Unary>;
  void init(Request &request) {
    Base::init(ZmRef<ClientRx>{new ClientRx{request.state}});
  }
};
struct Client;
struct Pool;
using TxQ = ZmPQTx<Pool, Requests, ZmPQTxOrdered<false>>;
struct Pool : ZmNHeap<Pool>, Zhttp::Pool<Client, TxQ, ReplyParser, Selection> {
friend inline ZuStringT<"Zgrpc.Wire.Pool"> ZmNHeapID(Pool *);
  using Base = Zhttp::Pool<Client, TxQ, ReplyParser, Selection>;
  explicit Pool(Client *client) : Base{client} { }
  Requests *txQueue() { return &requests; }
  void archive_(Requests::Node *) { }
  ZmRef<Requests::Node> retrieve_(Requests::Key, Requests::Key) { return {}; }
  Requests requests;
};
struct Client : Zhttp::Client<Client, Pool> { };

static void wire(bool h3, bool empty)
{
  ZuTestScopeRT(wire);
  Zhttp::Test::TempDir temp;
  ZuCheckRT(temp.init("ZgrpcWire"), (return));
  ZtString<> cert, key;
  ZuCheckRT(Zhttp::Test::writeLocalhostCert(temp, cert, key), (return));
  State state;
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
  Zhttp::HubConfig hub{&mx, "3", "4"};
  App server{&state};
  Zhttp::ServerConfig serverConfig;
  serverConfig.localIP(ZiIP{"127.0.0.1"}).port(port).bodyMax(uint64_t(-1))
    .retainedBodyMax(1U << 20);
  if (h3) serverConfig.quic(Zhttp::QUICConfig{}.certPath(cert).keyPath(key));
  else serverConfig.tls(Zhttp::H2Config{}.tls([&cert, &key](auto &tls) {
    tls.certPath(cert).keyPath(key);
  })
    .policy(Zhttp::H2Policy::Force));
  bool initialized = server.init(hub, serverConfig);
  ZuCheckRT(initialized);
  Client client;
  Zhttp::Config config;
  config.bodyMax(uint64_t(-1)).retainedBodyMax(1U << 20)
    .maxRedirects(0).protocol(h3 ? Zhttp::ProtoPolicy::ForceH3 : Zhttp::ProtoPolicy::DisableH3);
  bool cliInit = initialized && client.init(hub, 1, config, {},
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
  bool admitted = false;
  ZmRef<Requests::Node> request;
  if (cliUp) {
    request = new Requests::Node{};
    request->state = &state;
    admitted = client.send(0, request);
  }
  ZuCheckRT(admitted);
  bool done = admitted && state.done.timedwait(Zm::now(15)) == 0;
  ZuCheckRT(done);
  if (done) {
    ZmSemaphore checked;
    bool rejected = false;
    mx.run([request, &checked, &rejected]() { rejected = request->late(); checked.post(); }, 4);
    checked.wait();
    ZuCheckRT(rejected && state.requestClosed.load_() == 1);
  }
  if (cliUp) client.stop();
  client.final();
  request = nullptr;
  if (started) ZuCheckRT(server.stop());
  server.final();
  ZiResolver::stop();
  ZiResolver::final();
  ZuCheckRT(mx.stop());
  ZuCheckRT(!state.errors.load_() && state.received == 1);
  if (state.result.is<Message<Packet>>()) {
    ZuCheckRT(state.result.p<Message<Packet>>().object().payload == ZuBSpan{state.payload});
  } else ZuCheckRT(false);
  // Release retained receive backing before its mux allocator is destroyed.
  state.result = {};

}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZiTestResidue::init("zgrpcwiretest");
  ZuTestMain();
  ZuTestCall(wire, false, false);
  ZuTestCall(wire, false, true);
  ZuTestCall(wire, true, false);
  ZuTestCall(wire, true, true);
}
