//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZiResolver.hh>
#include <zlib/ZiTx.hh>
#include <zlib/ZgrpcHTTP.hh>
#include "ZhttpTestUtil.hh"

using namespace ZuTestUtil;
using namespace Zgrpc;

#include "ZgrpcWire.hh"

#include <zlib/ZgrpcServer.hh>

struct Duplex : Unary {
  using Name = ZuStringT<"Duplex">;
  enum { Kind = Zgrpc::Kind::Duplex };
};
using Methods = ZuTypeList<Duplex>;
ZgrpcCatalogDerive(Matrix, Methods);
ZgrpcCatalogImpl(Matrix);

struct DeadlineState {
  ZiMultiplex *mx = nullptr;
  ZmSemaphore listening, opened, cancelled, received, completed, held, release;
  ZmRef<ServerCall<Duplex>> token;
  ZmAtomic<unsigned> errors = 0, txHeld = 0;
  unsigned opens = 0, cancels = 0, messages = 0, headers = 0;
  bool httpOK = false;
  Code::T status = Code::OK;
};
struct Observer : ZmObject, ZmNHeap<Observer> {
friend inline ZuStringT<"Zgrpc.Deadline.Observer"> ZmNHeapID(Observer *);
  DeadlineState *state;
  explicit Observer(DeadlineState *value) : state{value} { }
  bool message(Message<Packet>) { ++state->messages; ++state->errors; return false; }
  void peerEnd() { ++state->errors; }
  void cancelled(Code::T code) {
    if (!state->mx->invoked(3) || !state->txHeld.load_() || code != Code::DEADLINE_EXCEEDED ||
        state->token->live() || !state->token->cancelled()) ++state->errors;
    ++state->cancels;
    state->cancelled.post();
  }
};
struct RPCApp {
  DeadlineState *state;
  void listening(int8_t, unsigned) { state->listening.post(); }
  void listenFailed(int8_t, bool) { ++state->errors; state->listening.post(); }
  Metadata headers(Duplex *, Context &) {
    ++state->headers;
    return Metadata{};
  }
  auto open(Duplex *, Context &ctx, ZmRef<ServerCall<Duplex>> call) {
    if (!state->mx->invoked(3) || ctx.deadline < 0) ++state->errors;
    state->token = ZuMv(call);
    ++state->opens;
    state->mx->run([s = state]() {
      s->txHeld = 1; s->held.post(); s->release.wait(); s->txHeld = 0;
    }, 4);
    state->opened.post();
    return ZmRef<Observer>{new Observer{state}};
  }
};
struct ClientRx : ZmObject, ZmNHeap<ClientRx> {
friend inline ZuStringT<"Zgrpc.Deadline.ClientRx"> ZmNHeapID(ClientRx *);
  DeadlineState *state;
  explicit ClientRx(DeadlineState *value) : state{value} { }
  void accepted(Zhttp::Channel *, RxInput &) { }
  bool message(Message<Packet>) { ++state->errors; return false; }
  void finished(RxInput &input, bool ok) {
    if (!state->mx->invoked(5) || !ok) ++state->errors;
    state->status = input.code();
    state->received.post();
  }
};
struct Request : ZmObject, ReqBuilder<Request, Duplex> {
  DeadlineState *state = nullptr;
  void completed(const Zhttp::Result &result) {
    if (!state->mx->invoked(5) || !result.ok()) ++state->errors;
    state->httpOK = result.ok();
    state->completed.post();
  }
};
using Requests = ZmPQueue<Request, ZmPQueueOverlap<false,
  ZmPQueueNode<Request, ZmPQueueHeapID<"Zgrpc.Deadline.Request">>>>;
struct ReplyParser : ResParser<ReplyParser, Duplex> {
  using Base = ResParser<ReplyParser, Duplex>;
  void init(Request &request) { Base::init(ZmRef<ClientRx>{new ClientRx{request.state}}); }
};
struct Client;
struct Pool;
using TxQ = ZmPQTx<Pool, Requests, ZmPQTxOrdered<false>>;
struct Pool : ZmNHeap<Pool>, Zhttp::Pool<Client, TxQ, ReplyParser, Selection> {
friend inline ZuStringT<"Zgrpc.Deadline.Pool"> ZmNHeapID(Pool *);
  using Base = Zhttp::Pool<Client, TxQ, ReplyParser, Selection>;
  explicit Pool(Client *client) : Base{client} { }
  Requests *txQueue() { return &requests; }
  void archive_(Requests::Node *) { }
  ZmRef<Requests::Node> retrieve_(Requests::Key, Requests::Key) { return {}; }
  Requests requests;
};
struct Client : Zhttp::Client<Client, Pool> { };

static void deadline(bool h3, bool zero = false)
{
  ZuTestScope(deadline);
  Zhttp::Test::TempDir temp;
  ZuCheck(temp.init("ZgrpcDeadline"), (return));
  ZtString<> cert, key;
  ZuCheck(Zhttp::Test::writeLocalhostCert(temp, cert, key), (return));
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(6);
    for (unsigned i = 1; i <= 6; ++i)
      s.thread(i, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  DeadlineState state;
  state.mx = &mx;
  ZuCheck(mx.start(), (return));
  unsigned port = peerPort(h3);
  ZuCheck(port, ({ mx.stop(); return; }));
  RPCApp app{&state};
  Zgrpc::Server<RPCApp, Matrix> server;
  ServerConfig config;
  config.localIP(ZiIP{"127.0.0.1"}).port(port);
  if (h3) config.quic(Zhttp::QUICConfig{}.certPath(cert).keyPath(key));
  else config.tls(Zhttp::H2Config{}.tls([&cert, &key](auto &tls) {
    tls.certPath(cert).keyPath(key);
  }));
  bool initialized = server.init(Zhttp::HubConfig{&mx, "3", "4"}, config, &app);
  ZuCheck(initialized);
  bool started = initialized && server.start();
  ZuCheck(started);
  bool listening = started && state.listening.timedwait(Zm::now(10)) == 0;
  ZuCheck(listening && !state.errors.load_());
  Client client;
  Zhttp::Config clientConfig;
  clientConfig.protocol(h3 ? Zhttp::ProtoPolicy::ForceH3 : Zhttp::ProtoPolicy::DisableH3)
    .bodyMax(uint64_t(-1)).maxRetries(0).maxRedirects(0);
  bool cliInit = listening && client.init(Zhttp::HubConfig{&mx, "5", "6"}, 1, clientConfig,
    {}, Zhttp::H2Config{}.tls([&cert](auto &tls) {
      tls.caPath(cert);
    }).policy(Zhttp::H2Policy::Force),
    Zhttp::QUICConfig{}.caPath(cert));
  ZuCheck(cliInit);
  bool pooled = cliInit && client.pool(0, Zhttp::Destination{"localhost", uint16_t(port)});
  ZuCheck(pooled);
  bool cliUp = pooled && client.start();
  ZuCheck(cliUp);
  if (cliUp) {
    ZmRef<Requests::Node> request = new Requests::Node{};
    request->state = &state;
    request->timeout = zero ? 0 : 1000000000; // Server-only timeout; no client RPC timer.
    ZuCheck(client.send(0, ZuMv(request)));
    if (!zero) {
      ZuCheck(state.opened.timedwait(Zm::now(10)) == 0);
      ZuCheck(state.held.timedwait(Zm::now(10)) == 0);
      ZuCheck(state.cancelled.timedwait(Zm::now(10)) == 0);
      ZuCheck(server.pending() == 1 && state.opens == 1 && state.cancels == 1 && !state.messages);
      ZuCheck(state.token && !state.token->live() && state.token->cancelled());
      state.release.post();
    }
    ZuCheck(state.received.timedwait(Zm::now(10)) == 0);
    ZuCheck(state.completed.timedwait(Zm::now(10)) == 0);
    client.stop();
  }
  client.final();
  if (started) ZuCheck(server.stop());
  ZuCheck(!server.pending());
  server.final();
  ZiResolver::stopSync(); ZiResolver::final();
  ZuCheck(mx.stop());
  ZuCheck(state.httpOK && state.status == Code::DEADLINE_EXCEEDED &&
    state.cancels == unsigned(!zero) && !state.errors.load_());
  ZuCheck(state.opens == unsigned(!zero) && state.headers == unsigned(!zero));
  if (state.token) ZuCheck(!state.token->send(Packet{}) && !state.token->finish() && !state.token->cancel());
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZiTestResidue::init("zgrpcdeadlinetest");
  ZuTestMain();
  ZuTestCall(deadline, false);
  ZuTestCall(deadline, true);
  ZuTestCall(deadline, false, true);
  ZuTestCall(deadline, true, true);
}
