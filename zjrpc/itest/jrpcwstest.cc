//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZjrpcClient.hh>
#include <zlib/ZjrpcServer.hh>

#include "ZiTestResidue.hh"
#include "ZquicInteropTest.hh"

using namespace ZuTestUtil;

struct Value { int value = 0; };
ZfStruct(, (Value, JSON), (value, (Ctor<0>, Required), Int32));
struct EchoOK : public Zjrpc::Response { using Body = Value; };
struct Echo : public Zjrpc::Request {
  using Object = Value;
  using Method = ZuStringT<"echo">;
  using Responses = ZuTypeList<EchoOK>;
};
using Catalog = ZuTypeList<Echo>;

static bool wait(ZmSemaphore &semaphore)
{
  return semaphore.timedwait(Zm::now(10)) == 0;
}

template <typename Heap = ZuVoid>
struct Call_ : public Heap, public ZmObject {
  ZmSemaphore done;
  int value = 0;
  unsigned failures = 0;
  void process(Zjrpc::Reply<EchoOK> reply) { value = reply.body.value; done.post(); }
  void failed() { ++failures; done.post(); }
  void failed(const Zjrpc::Error &) { failed(); }
};
using CallHeap = ZmHeap<"ZjrpcWSTest.Call", Call_<>>;
using Call = Call_<CallHeap>;

template <typename Heap = ZuVoid>
struct Aggregate_ : public Heap, public ZmObject {
  ZmSemaphore done;
  Zjrpc::BatchReply reply;
  unsigned completions = 0;
  unsigned failures = 0;
  void process(Zjrpc::BatchReply value) {
    reply = ZuMv(value);
    ++completions;
    done.post();
  }
  void failed() { ++failures; done.post(); }
};
using AggregateHeap = ZmHeap<"ZjrpcWSTest.Aggregate", Aggregate_<>>;
using Aggregate = Aggregate_<AggregateHeap>;

struct App {
  ZmSemaphore notified;
  ZmSemaphore arrived;
  ZmFn<void()> deferred;

  template <typename Req, typename Token>
  void request(Req *, const Value &value, Token token) {
    if (!token) { notified.post(); return; }
    if (value.value == 42) {
      deferred = [value, token = ZuMv(token)]() mutable {
	token->complete(Zjrpc::Reply<EchoOK>{value});
      };
      arrived.post();
      return;
    }
    token->complete(Zjrpc::Reply<EchoOK>{value});
  }
};

template <typename Profile>
struct SrvApp : public App {
  using Server = Zjrpc::WSServer<SrvApp, Catalog, Profile>;
  using Link = typename Server::Link;
  ZmSemaphore listening_;
  ZmSemaphore connected_;
  ZmRef<Link> link;
  unsigned failures = 0;

  void listening() { listening_.post(); }
  void listening(const ZiListenInfo &) { listening_.post(); }
  void listenFailed(bool) { ++failures; listening_.post(); }
  bool accept(Link &, ZuBSpan, ZuBSpan target, ZuBSpan protocols,
      Zws::HandshakeString &selected) {
    if (target != "/rpc" || !Zws::token(protocols, "jrpc")) return false;
    selected = "jrpc";
    return true;
  }
  void connected(Link &value, Zhttp::ConnectedInfo) {
    link = &value;
    connected_.post();
  }
  template <typename Req, typename Token>
  void request(Req *req, const Value &value, Link &, Token token) {
    App::request(req, value, ZuMv(token));
  }
};

struct CliApp : public App {
  ZmSemaphore ready_;
  unsigned failures = 0;
  void ready() { ready_.post(); }
  void failed() { ++failures; ready_.post(); }
  void closed() { }
};

template <typename Profile, bool Server>
static auto profileConfig(const Zquic::Test::TempDir &temp)
{
  if constexpr (ZuIsSame<Profile, Zhttp::H1TCP>{}) return Zhttp::TCPConfig{};
  else {
    using Config = ZuIf<ZuIsSame<Profile, Zhttp::H1TLS>{}, Zhttp::TLSConfig,
      ZuIf<ZuIsSame<Profile, Zhttp::H2TLS>{}, Zhttp::H2Config, Zhttp::QUICConfig>>;
    Config config;
    if constexpr (Server) config.certPath(temp.certPath.cspan()).keyPath(temp.keyPath.cspan());
    else config.caPath(temp.certPath.cspan());
    return config;
  }
}

template <typename Profile>
static void testWS(const Zquic::Test::TempDir &temp, unsigned port)
{
  ZuTestScope(testWS);
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &scheduler) {
    scheduler.nThreads(2)
      .thread(1, [](auto &thread) { thread.isolated(1); })
      .thread(2, [](auto &thread) { thread.isolated(1); });
  }).rxThread(1).txThread(2)};
  ZuCheck(mx.start(), (return));
  SrvApp<Profile> srv;
  CliApp cli;
  typename SrvApp<Profile>::Server server;
  Zjrpc::WSClient<CliApp, Catalog, Profile> client;
  Zhttp::HubConfig hub{&mx, "1", "2"};
  bool initialized = server.init(hub, ZiIP{"127.0.0.1"}, port,
    profileConfig<Profile, true>(temp), Zjrpc::WSConfig{}, &srv) &&
    client.init(hub, profileConfig<Profile, false>(temp), Zjrpc::WSConfig{}, &cli);
  ZuCheck(initialized);
  if (!initialized) { client.final(); server.final(); mx.stop(); return; }
  bool started = server.start();
  ZuCheck(started);
  bool listening = started && wait(srv.listening_) && !srv.failures;
  ZuCheck(listening);
  Zws::URI uri;
  ZtString<> text;
  text << (ZuIsSame<Profile, Zhttp::H1TCP>{} ? "ws" : "wss")
    << "://127.0.0.1:" << port << "/rpc";
  bool ready = listening && Zws::URI::parse(uri, text).ok() && client.start() &&
    client.connect(uri, "jrpc") && wait(cli.ready_) && !cli.failures && wait(srv.connected_);
  ZuCheck(ready);
  if (ready) {
    ZmRef<Call> outbound = new Call{};
    ZuCheck(client.template call<Echo>(Value{42}, outbound));
    bool arrived = wait(srv.arrived);
    ZuCheck(arrived);
    if (arrived) { auto complete = ZuMv(srv.deferred); complete(); }
    ZuCheck(wait(outbound->done));
    ZuCheck(outbound->value == 42 && !outbound->failures);
    ZmRef<Call> reverse = new Call{};
    ZuCheck(server.template call<Echo>(*srv.link, Value{73}, reverse));
    ZuCheck(wait(reverse->done));
    ZuCheck(reverse->value == 73 && !reverse->failures);
    Zjrpc::Batch<Catalog> batch;
    ZuCheck(batch.request<Echo>(Zjrpc::ID{int64_t{1001}}, Value{1}));
    ZuCheck(batch.request<Echo>(Zjrpc::ID{int64_t{1002}}, Value{2}));
    ZmRef<Aggregate> aggregate = new Aggregate{};
    ZuCheck(client.callBatch(ZuMv(batch), aggregate));
    ZuCheck(wait(aggregate->done));
    ZuCheck(aggregate->completions == 1 && !aggregate->failures);
    ZuCheck(aggregate->reply.entries().length() == 2);
    ZuCheck(client.template notify<Echo>(Value{3}));
    ZuCheck(wait(srv.notified));
    ZuCheck(server.template notify<Echo>(*srv.link, Value{4}));
    ZuCheck(wait(cli.notified));
    ZmRef<Call> abandoned = new Call{};
    ZuCheck(client.template call<Echo>(Value{42}, abandoned));
    ZuCheck(wait(srv.arrived));
    ZuCheck(client.stop());
    ZuCheck(wait(abandoned->done));
    ZuCheck(abandoned->failures == 1);
  }
  client.final();
  ZuCheck(server.stop());
  srv.deferred = {};
  srv.link = nullptr;
  server.final();
  mx.stop();
  ZuCheck(!srv.failures && !cli.failures);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZiTestResidue::init("jrpcwstest");
  ZuTestMain();
  Zquic::Test::TempDir temp;
  bool initialized = temp.init();
  ZuCheck(initialized);
  if (initialized) {
    ZuTestCall(testWS<Zhttp::H1TCP>, temp, 21110);
    ZuTestCall(testWS<Zhttp::H1TLS>, temp, 21111);
    ZuTestCall(testWS<Zhttp::H2TLS>, temp, 21112);
    ZuTestCall(testWS<Zhttp::H3QUIC>, temp, 21113);
  }
  return 0;
}
