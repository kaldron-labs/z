//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZiResolver.hh>
#include <zlib/ZgrpcClient.hh>

using namespace ZuTestUtil;
using namespace Zgrpc;

#include "ZgrpcMatrix.hh"

// Instrumented peers need time for large transfers while retaining real deadlines.
static unsigned peerTimeout()
{
  if (auto value = getenv("ZGRPC_PEER_TIMEOUT"))
    if (unsigned seconds = unsigned(strtoul(value, nullptr, 10))) return seconds;
  return 10;
}

struct App { };
using Endpoint = Client<App, Matrix>;
struct State {
  ZiMultiplex *mx;
  Zhttp::Version::T version = Zhttp::Version::H2;
  Bytes payload;
  ZmSemaphore done, received, headers;
  unsigned messages = 0, terminals = 0;
  ZmAtomic<unsigned> errors = 0;
  uint64_t seq = 42;
  unsigned count = 0;
  Code::T code = Code::OK;
  bool binary = false, sequence = false, handshake = false, waitHeaders = false;

  void checkMeta(const Metadata &meta) {
    unsigned n = 0;
    for (auto &field : meta.fields()) {
      if (meta.name(field) != ZuBSpan{"x-echo-bin"}) continue;
      auto value = meta.value(field);
      if (n == 0) {
        if (value.length() != 3 || value[0] || value[1] != ',' || value[2] != 255) ++errors;
      } else if (n == 1) {
        if (value != ZuBSpan{"second"}) ++errors;
      } else ++errors;
      ++n;
    }
    if (n != 2) ++errors;
  }
  void message(const Packet &value) {
    if (!mx->invoked(1) || value.seq != seq + (sequence ? messages : 0) ||
        value.payload != ZuBSpan{payload} || value.count != count) {
      log_("message mismatch: rx=", mx->invoked(1), " seq=", value.seq,
	" expected=", seq, " count=", value.count, " expected=", count,
	" bytes=", value.payload.length(), " expected=", payload.length());
      ++errors;
    }
    ++messages;
    if (handshake) received.post();
  }
  void finish(Code::T value) {
    if (!mx->invoked(1) || value != code) {
      log_("terminal ", int(value), " expected ", int(code));
      ++errors;
    }
    ++terminals;
    done.post();
  }
};
struct Observer : ZmObject, ZmNHeap<Observer> {
friend inline ZuStringT<"Zgrpc.Go.Observer"> ZmNHeapID(Observer *);
  explicit Observer(State *value) : state{value} { }
  void headers(const Metadata &meta) {
    if (!state->mx->invoked(1)) ++state->errors;
    if (state->binary) state->checkMeta(meta);
    if (state->waitHeaders) state->headers.post();
  }
  void trailers(const Metadata &meta) { if (state->binary) state->checkMeta(meta); }
  void process(Result<Packet> result) {
    if (result.http.httpVersion != state->version) {
      log_("unexpected HTTP version ", int(result.http.httpVersion));
      ++state->errors;
    }
    if (result.message.is<Message<Packet>>()) state->message(result.message.p<Message<Packet>>().object());
    if (state->binary) { state->checkMeta(result.metadata); state->checkMeta(result.trailers); }
    state->finish(result.status.code);
  }
  bool message(Message<Packet> value) { state->message(value.object()); return true; }
  void finished(Status status) { state->finish(status.code); }
  void failed(Failure failure) {
    if (failure.code != state->code) {
      const auto &http = failure.http;
      log_("unexpected failure: grpc=", int(failure.code),
	" metadata=", int(failure.metadata), " http=", int(http.code),
	" status=", http.status, " attempt=", http.attempt,
	" produced=", http.requestBodyProduced,
	" committed=", http.requestBodyCommitted,
	" received=", http.responseBodyReceived);
    }
    state->finish(failure.code);
  }
  State *state;
};

static void unary(Endpoint &client, State &state, bool empty, bool binary, Code::T code)
{
  ZuTestScope(unary);
  state.messages = state.terminals = 0;
  state.seq = empty ? 0 : 42;
  state.code = code;
  state.binary = binary;
  state.payload.length(empty ? 0 : (256U << 10) + 7);
  if (state.payload.length()) memset(state.payload.data(), 0x71, state.payload.length());
  Packet request{state.seq, state.payload};
  request.code = code;
  CallOptions options;
  options.timeout = int64_t(peerTimeout()) * 1000000000;
  if (binary) {
    uint8_t value[] = {0, ',', 255};
    ZuCheck(options.metadata.add("x-test-bin", ZuBSpan{value}));
    ZuCheck(options.metadata.add("x-test-bin", "second"));
  }
  ZuCheck(client.call<Unary>(0, request, ZmRef<Observer>{new Observer{&state}}, ZuMv(options)));
  ZuCheck(state.done.timedwait(Zm::now(peerTimeout() + 5)) == 0);
  ZuCheck(state.terminals == 1 && state.messages == (code == Code::OK ? 1 : 0));
  ZuCheck(!state.errors.load_());
  state.binary = false;
}

template <typename M> static void streaming(Endpoint &client, State &state, unsigned count)
{
  ZuTestScope(streaming);
  state.messages = state.terminals = 0;
  state.code = Code::OK;
  state.sequence = M::Kind != Kind::ClientStream;
  state.handshake = M::Kind == Kind::Duplex;
  state.seq = M::Kind == Kind::ClientStream ? count * (count + 1) / 2 : 42;
  state.count = M::Kind == Kind::Duplex ? 0 : count;
  state.payload.length(count ? 256U << 10 : 0);
  if (state.payload.length()) memset(state.payload.data(), 0x7a, state.payload.length());
  CallOptions options;
  options.timeout = int64_t(peerTimeout()) * 1000000000;
  auto token = client.open<M>(0, ZmRef<Observer>{new Observer{&state}}, ZuMv(options));
  ZuCheck(token, (return));
  if constexpr (M::Kind == Kind::ServerStream) {
    ZuCheck(token->send(Packet{42, state.payload, count}));
  } else {
    for (unsigned i = 0; i < count; ++i) {
      ZuCheck(token->send(Packet{M::Kind == Kind::Duplex ? 42 + i : i + 1, state.payload}));
      if constexpr (M::Kind == Kind::Duplex) {
        // A response must arrive before request half-close.
        ZuCheck(state.received.timedwait(Zm::now(peerTimeout() + 5)) == 0);
      }
    }
  }
  ZuCheck(token->end());
  ZuCheck(state.done.timedwait(Zm::now(peerTimeout() + 5)) == 0);
  ZuCheck(state.terminals == 1 && state.messages == (M::Kind == Kind::ClientStream ? 1 : count));
  ZuCheck(!state.errors.load_());
  ZuCheck(!token->live() && !token->send(Packet{}) && !token->end());
}

static void cancellation(Endpoint &client, State &state)
{
  ZuTestScope(cancellation);
  state.messages = state.terminals = 0;
  state.code = Code::CANCELLED;
  state.handshake = false;
  state.waitHeaders = true;
  CallOptions options;
  options.timeout = int64_t(peerTimeout()) * 1000000000;
  auto token = client.open<Duplex>(0, ZmRef<Observer>{new Observer{&state}}, ZuMv(options));
  ZuCheck(token, (return));
  ZuCheck(state.headers.timedwait(Zm::now(peerTimeout() + 5)) == 0);
  ZuCheck(token->cancel());
  ZuCheck(state.done.timedwait(Zm::now(peerTimeout() + 5)) == 0);
  ZuCheck(state.terminals == 1 && !state.messages && !state.errors.load_());
  state.waitHeaders = false;
}

static void deadline(Endpoint &client, State &state)
{
  ZuTestScope(deadline);
  state.messages = state.terminals = 0;
  state.code = Code::DEADLINE_EXCEEDED;
  CallOptions options;
  options.timeout = 30000000;
  Packet request;
  request.waitCancel = true;
  ZuCheck(client.call<Unary>(0, request, ZmRef<Observer>{new Observer{&state}}, ZuMv(options)));
  ZuCheck(state.done.timedwait(Zm::now(peerTimeout() + 5)) == 0);
  ZuCheck(state.terminals == 1 && !state.messages && !state.errors.load_());
}

static void concurrent(Endpoint &client, State &state)
{
  ZuTestScope(concurrent);
  state.messages = state.terminals = 0;
  state.seq = state.count = 0;
  state.code = Code::OK;
  state.sequence = false;
  state.payload.length(0);
  for (unsigned i = 0; i < 16; ++i) {
    CallOptions options;
    options.timeout = int64_t(peerTimeout()) * 1000000000;
    ZuCheck(client.call<Unary>(0, Packet{}, ZmRef<Observer>{new Observer{&state}}, ZuMv(options)));
  }
  for (unsigned i = 0; i < 16; ++i) ZuCheck(state.done.timedwait(Zm::now(peerTimeout() + 5)) == 0);
  ZuCheck(state.terminals == 16 && state.messages == 16 && !state.errors.load_());
}

static void run()
{
  ZuTestScope(run);
  auto ca = getenv("ZGRPC_GO_CA"), port = getenv("ZGRPC_GO_PORT");
  ZuCheck(ca && port, (return));
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(2).thread(1, [](auto &t) { t.isolated(1); })
      .thread(2, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  State state{&mx};
  bool h3 = getenv("ZGRPC_GO_H3");
  state.version = h3 ? Zhttp::Version::H3 : Zhttp::Version::H2;
  App app;
  Endpoint client;
  ZuCheck(mx.start(), (return));
  ClientConfig config;
  config.links(1).linkMax(16).concurrency(16)
    .protocol(h3 ? Zhttp::ProtoPolicy::ForceH3 : Zhttp::ProtoPolicy::DisableH3);
  Zhttp::QUICConfig quic;
  quic.caPath(ca);
  if (auto path = getenv("ZGRPC_PEER_QLOG")) quic.qlog(true).qlogPath(path);
  bool initialized = client.init(Zhttp::HubConfig{&mx, "1", "2"}, 1, config, &app,
    {}, Zhttp::H2Config{}.tls([&ca](auto &tls) {
      tls.caPath(ca);
    }).policy(Zhttp::H2Policy::Force), quic);
  ZuCheck(initialized);
  bool pooled = initialized && client.pool(0, Zhttp::Destination{"localhost", uint16_t(atoi(port))});
  ZuCheck(pooled);
  bool started = pooled && client.start();
  ZuCheck(started);
  if (started) {
    ZuTestCall(unary, client, state, false, false, Code::OK);
    ZuTestCall(unary, client, state, true, false, Code::OK);
    ZuTestCall(unary, client, state, true, false, Code::NOT_FOUND);
    ZuTestCall(unary, client, state, true, true, Code::OK);
    ZuTestCall((streaming<ServerStream>), client, state, 3);
    ZuTestCall((streaming<ServerStream>), client, state, 0);
    ZuTestCall((streaming<ClientStream>), client, state, 3);
    ZuTestCall((streaming<ClientStream>), client, state, 0);
    ZuTestCall((streaming<Duplex>), client, state, 3);
    ZuTestCall(cancellation, client, state);
    ZuTestCall(deadline, client, state);
    ZuTestCall(concurrent, client, state);
    ZuCheck(client.stop());
  }
  ZuCheck(!client.pending());
  client.final();
  ZiResolver::stopSync();
  ZiResolver::final();
  ZuCheck(mx.stop());
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(run);
}
