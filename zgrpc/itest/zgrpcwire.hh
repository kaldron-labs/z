//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zgrpcwire_HH
#define zgrpcwire_HH

#include <zlib/ZmSemaphore.hh>
#include <zlib/ZiPlatform.hh>
#include <zlib/ZgrpcHTTP.hh>

using namespace Zgrpc;
using namespace ZuTestUtil;

// Probe after the multiplex starts, including Windows socket initialization.
inline unsigned peerPort(bool h3)
{
  Zi::Socket sock = ::socket(AF_INET, h3 ? SOCK_DGRAM : SOCK_STREAM, 0);
  if (Zi::nullSocket(sock)) return 0;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  socklen_t size = sizeof(addr);
  unsigned port = 0;
  if (::bind(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0 &&
      ::getsockname(sock, reinterpret_cast<sockaddr *>(&addr), &size) == 0)
    port = ntohs(addr.sin_port);
  Zi::closeSocket(sock);
  return port;
}

struct Packet { uint64_t seq = 0; ZuBSpan payload; };
ZfStruct(, (Packet, PB), (seq, (Ctor<0>, PB::FieldID<1>), UInt64),
  (payload, (Ctor<1>, PB::FieldID<2>), Bytes));
Zpb::AsMessage<"zgrpc.interop.Packet"> Zpb_Fmt(Packet *);
struct Unary : Method {
  using Service = ZuStringT<"zgrpc.interop.Matrix">;
  using Name = ZuStringT<"Unary">;
  using Request = Packet;
  using Response = Packet;
  enum { ReqBorrowed = 1, ResBorrowed = 1 };
};
struct State {
  ZiMultiplex *mx = nullptr;
  ZmSemaphore listening, done;
  ZmAtomic<unsigned> errors = 0;
  unsigned received = 0, headers = 0;
  ZmSemaphore *holdRx = nullptr, *releaseRx = nullptr;
  unsigned failures = 0;
  Code::T expectedFailure = Code::OK;
  ZmAtomic<unsigned> requestClosed = 0, peerRequests = 0;
  bool redirect = false;
  bool peerCancel = false;
  unsigned altPort = 0;
  Zhttp::Version::T version = Zhttp::Version::H2;
  Bytes payload;
  Decoded<Packet> result;
};

struct Response : ZmObject, ResBuilder<Response, Unary> {
  using Base = ResBuilder<Response, Unary>;
  using Base::header;
  Message<Packet> message;
  State *state;
  Response(Message<Packet> m, State *s) : message{ZuMv(m)}, state{s} { }
  unsigned status() const { return state->redirect ? 307 : 200; }
  Zhttp::BodyPolicy::T bodyPolicy() const {
    return state->redirect ? Zhttp::BodyPolicy::None : Base::bodyPolicy();
  }
  template <typename L> void header(L &&emit) const {
    Base::header(emit);
    if (state->redirect) emit("location", "/zgrpc.interop.Matrix/Unary");
    if (state->altPort) {
      ZtString<> value;
      value << "h3=\":" << state->altPort << "\"; ma=60";
      emit("alt-svc", value);
    }
  }
  void producerStarted() {
    if (!state->mx->invoked(4)) ++state->errors;
    if (!send_(ZuMv(message)) || !finish_()) ++state->errors;
  }
  void productionFailed(Code::T code) { log_("server output failure ", int(code)); ++state->errors; }
};
using Responses = Zhttp::ResponseQ<Response, ZuStringT<"Zgrpc.Wire.Response">>;
struct ServerRx : ZmObject, ZmNHeap<ServerRx> {
friend inline ZuStringT<"Zgrpc.Wire.ServerRx"> ZmNHeapID(ServerRx *);
  explicit ServerRx(State *value) : state{value} { }
  void accepted(Zhttp::Channel *channel, RxInput &) {
    if (state->peerCancel) { channel->disconnect(); return; }
    if (!state->mx->invoked(3)) ++state->errors;
    route = {channel, Zhttp::Method::POST};
  }
  bool message(Message<Packet> value) {
    if (!state->mx->invoked(3)) ++state->errors;
    request = ZuMv(value);
    return true;
  }
  void finished(RxInput &, bool ok) {
    if (state->peerCancel) return;
    if (!state->mx->invoked(3)) ++state->errors;
    if (!ok || !request.is<Message<Packet>>()) { log_("server receive failure"); ++state->errors; return; }
    ++state->peerRequests;
    ZmRef<Responses::Node> response = new Responses::Node{
      ZuMv(request.p<Message<Packet>>()), state};
    if (state->holdRx) {
      state->mx->run([held = state->holdRx, release = state->releaseRx]() {
        held->post(); release->wait();
      }, 3);
    }
    route.send(ZuMv(response));
    route.reset();
  }
  State *state;
  Zhttp::Route<Responses::Node> route;
  Decoded<Packet> request;
};
struct App;
struct RequestParser : ReqParser<RequestParser, Unary> {
  using Base = ReqParser<RequestParser, Unary>;
  void init(App &);
};
struct App : Zhttp::Server<App, RequestParser, Responses, Selection> {
  explicit App(State *value) : state{value} { }
  void listening(int8_t, unsigned) { state->listening.post(); }
  void listenFailed(int8_t, bool) { ++state->errors; state->listening.post(); }
  void connected(int8_t) { }
  void disconnected(int8_t) { }
  void connected(Zhttp::Session) { }
  void disconnected(Zhttp::Session) { }
  State *state;
};
inline void RequestParser::init(App &app)
{
  Base::init(ZmRef<ServerRx>{new ServerRx{app.state}});
}

#endif /* zgrpcwire_HH */
