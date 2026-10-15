//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Exercise the daemon's actual callbacks without sockets or OAuth timing.
#define main ZtcHubMain
#include "../src/ztchub.cc"
#undef main

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZtScratch.hh>

using namespace ZuTestUtil;

namespace ZtcHubRxTest_ {

struct Timers {
  template <typename ...Args> void add(Args &&...) { }
  template <typename T> void del(T *) { }
};
struct Server {
  ZtcHub_::App *application;
  Timers timers;
  ZmFn<void()> pending;

  auto app() { return application; }
  auto mx() { return &timers; }
  unsigned rxThread() { return 1; }
  template <typename L> void rxRun(L &&fn) {
    ZmAssert(!pending);
    pending = ZuFwd<L>(fn);
  }
  void run() {
    auto fn = ZuMv(pending);
    if (fn) fn();
  }
  template <typename L> void txRun(L &&fn) { fn(); }
};
struct Sink {
  Sink &operator <<(ZuBSpan) { return *this; }
  void flush() { }
};
template <typename Heap = ZuVoid>
struct Link_ : public Heap, public ZmObject {
  Server *server;
  ZtcHub_::App::LinkState input;
  unsigned closeCode = 0;

  Link_(Server *server_) : server{server_} { }
  auto app() { return server; }
  auto &state() { return input; }
  void close(unsigned code) { closeCode = code; }
  template <typename L> void txStream_(L &&fn, Zws::Opcode::T) {
    Sink sink;
    fn(sink);
  }
};
ZuDerive(LinkHeap, (ZmHeap<"Ztc.Test.HubLink", Link_<>>));
ZuDerive(Link, Link_<LinkHeap>);
ZuDerive(Bytes, (ZtArray<uint8_t, ZtArrayHeapID<"Ztc.Test.HubBytes">>));
struct Rx {
  ZuSpan<uint8_t> bytes;

  explicit operator bool() const { return bool(bytes); }
  template <typename F, typename L> int64_t consume(F &&f, L &&l) {
    auto n = f(bytes);
    if (!l(bytes)) return -1;
    bytes.offset(n);
    return n;
  }
};

static Ztc::HubdCf config(unsigned frames)
{
  Ztc::HubdCf cf;
  cf.listeners.push(Ztc::ListenerCf{
    "127.0.0.1", "/ztc", "server.crt", "server.key", "/session", {}});
  cf.issuerURL = "https://zum.example/oauth2/7";
  cf.audience = "https://ztchub.example";
  cf.managementIssuer = "https://zum.example";
  cf.managementClientID = "ztchub-manager";
  cf.actions.push("Request");
  cf.actions.push("Telemetry");
  cf.roles.push("Client");
  cf.roles.push("Agent");
  cf.maxFrame = 256;
  cf.controlFrames = frames;
  cf.controlBytes = 512;
  return cf;
}

template <auto &ZuTest_scope>
struct Fixture {
  Ztc::Hubd hub;
  ZtcHub_::App app{&hub};
  Server server{&app};
  ZmRef<Link> link{new Link{&server}};

  Fixture(unsigned frames = 4) {
    ZuCheck(hub.init(config(frames)) && hub.start());
    link->input.sessionID = 1;
    link->input.generation = 1;
  }
  ~Fixture() {
    app.disconnected(*link, false);
    hub.stop();
    hub.final();
  }
  int start() { return app.messageStart(*link, Zws::Opcode::Binary); }
  int receive(ZuSpan<uint8_t> bytes) {
    Rx rx{bytes};
    return app.process(*link, rx);
  }
  int end() { return app.messageEnd(*link); }
  void authorize(int error = Zum::ServiceError::OK) {
    Zum::ServicePrincipal principal;
    principal.tokenID.issuerURL = hub.config().issuerURL;
    principal.tokenID.jti = "token";
    principal.subject = "agent";
    principal.audience = hub.config().audience;
    principal.expires = Zm::now().sec() + 60;
    principal.actions.push("Telemetry");
    principal.authMethod = "client_credentials";
    if (error == Zum::ServiceError::OK && !link->input.closed)
      std::cout << "# ";
    app.authorize(link, error, ZuMv(principal));
    server.run();
  }
};

static Ztc::HubFrame ack(unsigned seqNo)
{
  Ztc::Builder builder{new ZtcHub_::HubBuf};
  auto nested = builder.startNested();
  auto value = Zfb::save(builder, Ztc::Ack{
    .id = "publisher", .seqNo = seqNo, .status = Ztc::AckStatus::OK});
  builder.finish(Ztc::saveMsg(builder, nested, Ztc::Body::Ack, value));
  return builder.buf();
}
static ZuSpan<uint8_t> bytes(const Ztc::HubFrame &frame)
{
  return {frame->data(), frame->length};
}

static void frameLimit()
{
  ZuTestScope(frameLimit);
  Fixture<ZuTest_scope> f{2};
  ZuCheck(f.start() == 1);
  ZuCheck(f.end() == 1);
  ZuCheck(f.start() == 1);
  ZuCheck(f.end() == 1);
  ZuCheck(f.link->input.pending.length() == 2);
  ZuCheck(f.app.messageStart(*f.link, Zws::Opcode::Binary) < 0);
  ZuCheck(f.link->input.closed && f.link->closeCode == Zws::CloseCode::Policy);
}

static void byteLimit()
{
  ZuTestScope(byteLimit);
  Fixture<ZuTest_scope> f;
  auto data = ZtScratch(Bytes, f.hub.config().maxFrame);
  data.length(f.hub.config().maxFrame);
  memset(data.data(), 0, data.length());
  ZuCheck(f.start() == 1);
  ZuCheck(f.receive(data) == 1);
  ZuCheck(f.end() == 1);
  ZuCheck(f.start() == 1);
  ZuCheck(f.receive(data.span().trunc(data.length() - 1)) == 1);
  ZuCheck(f.end() == 1);
  ZuCheck(f.start() == 1);
  ZuCheck(f.receive(data.span().trunc(1)) == 1);
  ZuCheck(f.receive(data.span().trunc(1)) < 0);
  ZuCheck(f.link->input.pendingBytes == 511 && f.link->input.frame->length == 1);
  f.authorize();
  ZuCheck(f.link->input.closed && !f.link->input.authenticated);
}

static void invalid()
{
  ZuTestScope(invalid);
  Fixture<ZuTest_scope> f;
  ZuCheck(f.start() == 1);
  ZuCheck(f.end() == 1);
  f.authorize();
  ZuCheck(f.link->input.closed && f.link->closeCode == Zws::CloseCode::InvalidData);
}

static void partial()
{
  ZuTestScope(partial);
  Fixture<ZuTest_scope> f;
  auto first = ack(1), second = ack(2);
  ZuCheck(f.start() == 1);
  ZuCheck(f.receive(bytes(first)) == 1);
  ZuCheck(f.end() == 1);
  ZuCheck(f.start() == 1);
  auto half = second->length >> 1;
  ZuCheck(f.receive(bytes(second).trunc(half)) == 1);
  auto saved = f.link->input.frame.ptr();
  f.authorize();
  ZuCheck(f.link->input.authenticated && !f.link->input.closed);
  ZuCheck(!f.link->input.pending.length() && !f.link->input.pendingBytes);
  ZuCheck(f.link->input.frame.ptr() == saved && saved->length == half);
  ZuCheck(f.receive(bytes(second).offset(half)) == 1);
  ZuCheck(f.end() == 1);
  ZuCheck(!f.link->closeCode && !f.link->input.pending.length());
}

static void rejected()
{
  ZuTestScope(rejected);
  Fixture<ZuTest_scope> f;
  auto value = ack(1);
  ZuCheck(f.start() == 1);
  ZuCheck(f.receive(bytes(value)) == 1);
  ZuCheck(f.end() == 1);
  f.authorize(Zum::ServiceError::Unauthorized);
  ZuCheck(f.link->input.closed && !f.link->input.authenticated);
  f.authorize();
  ZuCheck(!f.link->input.authenticated);
  f.app.disconnected(*f.link, false);
  ZuCheck(!f.link->input.pending.length() && !f.link->input.pendingBytes);
}

static void reuse()
{
  ZuTestScope(reuse);
  Fixture<ZuTest_scope> f;
  auto value = ack(1);
  ZuCheck(f.start() == 1);
  ZuCheck(f.receive(bytes(value)) == 1);
  auto saved = f.link->input.frame.ptr();
  ZuCheck(f.end() == 1);
  f.authorize();
  ZuCheck(f.link->input.authenticated && !f.link->input.closed);
  ZuCheck(f.link->input.frame.ptr() == saved);
  ZuCheck(f.start() == 1);
  ZuCheck(f.link->input.frame.ptr() == saved && !saved->length);
}

static void drain()
{
  ZuTestScope(drain);
  Fixture<ZuTest_scope> f;
  auto value = ack(1);
  for (unsigned i = 0; i < 3; ++i) {
    ZuCheck(f.start() == 1);
    ZuCheck(f.receive(bytes(value)) == 1);
    ZuCheck(f.end() == 1);
  }
  f.authorize();
  ZuCheck(f.link->input.authenticated && !f.link->input.closed);
  ZuCheck(f.link->input.pending.length() == 2 && f.server.pending);
  // New arrivals remain behind the frames awaiting their scheduled turn.
  ZuCheck(f.start() == 1);
  ZuCheck(f.receive(bytes(value)) == 1);
  ZuCheck(f.end() == 1);
  ZuCheck(f.link->input.pending.length() == 3);
  f.server.run();
  ZuCheck(f.link->input.pending.length() == 2 && f.server.pending);
  f.server.run();
  ZuCheck(f.link->input.pending.length() == 1 && f.server.pending);
  f.server.run();
  ZuCheck(!f.link->input.pending.length() && !f.link->input.pendingBytes);
  ZuCheck(!f.server.pending && !f.link->closeCode);
}

static void closedDrain()
{
  ZuTestScope(closedDrain);
  Fixture<ZuTest_scope> f;
  auto value = ack(1);
  for (unsigned i = 0; i < 2; ++i) {
    ZuCheck(f.start() == 1);
    ZuCheck(f.receive(bytes(value)) == 1);
    ZuCheck(f.end() == 1);
  }
  f.authorize();
  ZuCheck(f.link->input.pending.length() == 1 && f.server.pending);
  f.app.disconnected(*f.link, false);
  ZuCheck(f.link->input.closed && !f.link->input.pending.length());
  f.server.run();
  ZuCheck(!f.server.pending && !f.link->input.pendingBytes);
  ZuCheck(!f.link->closeCode);
}

} // namespace ZtcHubRxTest_

int main()
{
  using namespace ZtcHubRxTest_;
  ZuTestMain();
  ZiLog::init("ZtcHubRxTest");
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZuTestCall(frameLimit);
  ZuTestCall(byteLimit);
  ZuTestCall(invalid);
  ZuTestCall(partial);
  ZuTestCall(rejected);
  ZuTestCall(reuse);
  ZuTestCall(drain);
  ZuTestCall(closedDrain);
  ZiLog::stop();
}
