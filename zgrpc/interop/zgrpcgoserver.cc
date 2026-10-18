//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdio.h>
#include <stdlib.h>

#include <zlib/ZmSemaphore.hh>
#include <zlib/ZiResolver.hh>
#include <zlib/ZgrpcServer.hh>

#include "ZgrpcMatrix.hh"

static Metadata fields(const Context &ctx, bool initial)
{
  Metadata result;
  result.add(initial ? "x-peer" : "x-trailer", initial ? "z" : "done");
  if (ctx.metadata) {
    for (auto &field : ctx.metadata->fields())
      if (ctx.metadata->name(field) == ZuBSpan{"x-test-bin"})
	result.add("x-echo-bin", ctx.metadata->value(field));
  }
  return result;
}
static Status status(unsigned code)
{
  Status result;
  result.code = code <= Code::UNAUTHENTICATED ? Code::T(code) : Code::INVALID_ARGUMENT;
  if (code) result.message = "requested error";
  return result;
}

template <typename M>
struct Stream : ZmObject, ZmNHeap<Stream<M>> {
friend inline ZuStringT<"Zgrpc.Go.Stream"> ZmNHeapID(Stream *);
  ZmRef<ServerCall<M>> call;
  Metadata trailers;
  Decoded<Packet> last;
  uint64_t sum = 0;
  unsigned count = 0;

  Stream(ZmRef<ServerCall<M>> token, Metadata meta) : call{ZuMv(token)}, trailers{ZuMv(meta)} { }
  bool message(Message<Packet> value) {
    if (value.object().code) {
      if constexpr (M::Kind == Kind::Duplex)
	return call->finish(status(value.object().code), ZuMv(trailers));
      else return call->complete(Reply<Packet>{status(value.object().code), ZuMv(trailers)});
    }
    if (value.object().waitCancel) return true;
    if constexpr (M::Kind == Kind::Duplex) return call->send(ZuMv(value));
    else {
      sum += value.object().seq;
      ++count;
      last = ZuMv(value);
      return true;
    }
  }
  void peerEnd() {
    if constexpr (M::Kind == Kind::Duplex) call->finish({}, ZuMv(trailers));
    else {
      Message<Packet> result{Packet{sum, {}, count}};
      if (last.template is<Message<Packet>>()) {
	auto &value = last.template p<Message<Packet>>();
	result.object().payload = value.object().payload;
	result.backing = ZuMv(value.backing);
      }
      call->complete(Reply<Packet>{ZuMv(result), {}, ZuMv(trailers)});
    }
  }
  void cancelled(Code::T) { }
};

struct App {
  ZmSemaphore ready;
  bool listenOK = false;
  void listening(int8_t, unsigned port) {
    listenOK = true;
    printf("READY 127.0.0.1:%u\n", port);
    fflush(stdout);
    ready.post();
  }
  void listenFailed(int8_t, bool) { ready.post(); }
  template <typename M> Metadata headers(M *, Context &ctx) { return fields(ctx, true); }
  void request(Unary *, Context &ctx, Message<Packet> value, ZmRef<ServerCall<Unary>> call) {
    if (value.object().code) call->complete(Reply<Packet>{status(value.object().code)});
    else if (!value.object().waitCancel)
      call->complete(Reply<Packet>{ZuMv(value), {}, fields(ctx, false)});
  }
  void request(ServerStream *, Context &ctx, Message<Packet> value, ZmRef<ServerCall<ServerStream>> call) {
    auto &packet = value.object();
    if (packet.code) { call->finish(status(packet.code)); return; }
    if (packet.waitCancel) return;
    if (packet.count > 65536) { call->finish(status(Code::RESOURCE_EXHAUSTED)); return; }
    for (unsigned i = 0; i < packet.count; ++i) {
      Packet out = packet;
      out.seq += i;
      if (!call->send(Message<Packet>{out, value.backing})) return;
    }
    call->finish({}, fields(ctx, false));
  }
  template <typename M> auto open(M *, Context &ctx, ZmRef<ServerCall<M>> call) {
    return ZmRef<Stream<M>>{new Stream<M>{ZuMv(call), fields(ctx, false)}};
  }
};

int main()
{
  auto cert = getenv("ZGRPC_GO_CERT"), key = getenv("ZGRPC_GO_KEY"), port = getenv("ZGRPC_GO_PORT");
  if (!cert || !key || !port) return 1;
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(4).thread(1, [](auto &t) { t.isolated(1); })
      .thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); })
      .thread(4, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  App app;
  Zgrpc::Server<App, Matrix> server;
  ServerConfig config;
  config.localIP(ZiIP{"127.0.0.1"}).port(atoi(port));
  if (getenv("ZGRPC_GO_H3")) config.quic(Zhttp::QUICConfig{}.certPath(cert).keyPath(key));
  else config.tls(Zhttp::H2Config{}.tls([&cert, &key](auto &tls) {
    tls.certPath(cert).keyPath(key);
  }).policy(Zhttp::H2Policy::Force));
  bool started = mx.start() && server.init(Zhttp::HubConfig{&mx, "3", "4"}, config, &app)
    && server.start();
  bool ready = started && app.ready.timedwait(Zm::now(10)) == 0 && app.listenOK;
  if (ready) (void)getchar(); // The harness owns shutdown through this pipe.
  bool stopped = !started || server.stop();
  server.final();
  ZiResolver::stopSync(); ZiResolver::final();
  bool drained = mx.stop();
  return ready && stopped && drained ? 0 : 1;
}
