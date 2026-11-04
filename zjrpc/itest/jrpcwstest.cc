//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/Zquic.hh>
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

struct Call : public ZmNHeap<Call>, public ZmObject {
friend inline ZuStringT<"ZjrpcWSTest.Call"> ZmNHeapID(Call *);

  ZmSemaphore done;
  int value = 0;
  unsigned failures = 0;
  void process(Zjrpc::Reply<EchoOK> reply) { value = reply.body.value; done.post(); }
  void failed() { ++failures; done.post(); }
  void failed(const Zjrpc::Error &) { failed(); }
};

struct Aggregate : public ZmNHeap<Aggregate>, public ZmObject {
friend inline ZuStringT<"ZjrpcWSTest.Aggregate"> ZmNHeapID(Aggregate *);

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
  using Link = Server::Link;
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
    auto configure = [&temp](auto &tls) {
      if constexpr (Server)
	tls.certPath(temp.certPath.cspan()).keyPath(temp.keyPath.cspan());
      else tls.caPath(temp.certPath.cspan());
    };
    if constexpr (ZuIsSame<Profile, Zhttp::H2TLS>{}) config.tls(configure);
    else configure(config);
    return config;
  }
}

template <typename Profile>
static void testWS(const Zquic::Test::TempDir &temp, unsigned port)
{
  ZuTestScopeRT(testWS);
  ZiMultiplex mx{ZiMxParams{}
    .udpBufAllocator<Zquic::PktRxBufAllocator<>>()
    .scheduler([](auto &scheduler) {
    scheduler.nThreads(2)
      .thread(1, [](auto &thread) { thread.isolated(1); })
      .thread(2, [](auto &thread) { thread.isolated(1); });
  }).rxThread(1).txThread(2)};
  ZuCheckRT(mx.start(), (return));
  SrvApp<Profile> srv;
  CliApp cli;
  typename SrvApp<Profile>::Server server;
  Zjrpc::WSClient<CliApp, Catalog, Profile> client;
  Zhttp::HubConfig hub{&mx, "1", "2"};
  bool initialized = server.init(hub, ZiIP{"127.0.0.1"}, port,
    profileConfig<Profile, true>(temp), Zjrpc::WSConfig{}, &srv) &&
    client.init(hub, profileConfig<Profile, false>(temp),
      Zhttp::Destination{"127.0.0.1", uint16_t(port)}, Zjrpc::WSConfig{}, &cli);
  ZuCheckRT(initialized);
  if (!initialized) { client.final(); server.final(); mx.stop(); return; }
  bool started = server.start();
  ZuCheckRT(started);
  bool listening = started && wait(srv.listening_) && !srv.failures;
  ZuCheckRT(listening);
  Zws::URI uri;
  ZtString<> text;
  text << (ZuIsSame<Profile, Zhttp::H1TCP>{} ? "ws" : "wss")
    << "://127.0.0.1:" << port << "/rpc";
  bool clientStarted = listening && Zws::URI::parse(uri, text).ok() && client.start();
  bool ready = clientStarted && client.connect(uri, "jrpc") &&
    wait(cli.ready_) && !cli.failures && wait(srv.connected_);
  ZuCheckRT(ready);
  if (ready) {
    ZmRef<Call> outbound = new Call{};
    ZuCheckRT(client.template call<Echo>(Value{42}, outbound));
    bool arrived = wait(srv.arrived);
    ZuCheckRT(arrived);
    if (arrived) { auto complete = ZuMv(srv.deferred); complete(); }
    ZuCheckRT(wait(outbound->done));
    ZuCheckRT(outbound->value == 42 && !outbound->failures);
    ZmRef<Call> reverse = new Call{};
    ZuCheckRT(server.template call<Echo>(*srv.link, Value{73}, reverse));
    ZuCheckRT(wait(reverse->done));
    ZuCheckRT(reverse->value == 73 && !reverse->failures);
    Zjrpc::Batch<Catalog> batch;
    ZuCheckRT(batch.request<Echo>(Zjrpc::ID{int64_t{1001}}, Value{1}));
    ZuCheckRT(batch.request<Echo>(Zjrpc::ID{int64_t{1002}}, Value{2}));
    ZmRef<Aggregate> aggregate = new Aggregate{};
    ZuCheckRT(client.callBatch(ZuMv(batch), aggregate));
    ZuCheckRT(wait(aggregate->done));
    ZuCheckRT(aggregate->completions == 1 && !aggregate->failures);
    ZuCheckRT(aggregate->reply.entries().length() == 2);
    ZuCheckRT(client.template notify<Echo>(Value{3}));
    ZuCheckRT(wait(srv.notified));
    ZuCheckRT(server.template notify<Echo>(*srv.link, Value{4}));
    ZuCheckRT(wait(cli.notified));
    ZmRef<Call> abandoned = new Call{};
    ZuCheckRT(client.template call<Echo>(Value{42}, abandoned));
    ZuCheckRT(wait(srv.arrived));
    ZuCheckRT(client.stop());
    ZuCheckRT(wait(abandoned->done));
    ZuCheckRT(abandoned->failures == 1);
  }
  if (clientStarted && !ready) ZuCheckRT(client.stop());
  ZuCheckRT(server.stop());
  srv.deferred = {};
  srv.link = nullptr;
  if (ready) {
    bool serverRestarted = server.start();
    bool listeningAgain = serverRestarted && wait(srv.listening_);
    ZuCheckRT(listeningAgain);
    bool clientRestarted = listeningAgain && client.start();
    bool readyAgain = clientRestarted && client.connect(uri, "jrpc") &&
      wait(cli.ready_) && wait(srv.connected_);
    ZuCheckRT(readyAgain);
    if (readyAgain) {
      ZmRef<Call> fresh = new Call{};
      bool sent = client.template call<Echo>(Value{19}, fresh);
      bool completed = sent && wait(fresh->done);
      ZuCheckRT(completed);
      if (completed) ZuCheckRT(fresh->value == 19 && !fresh->failures);
    }
    if (clientRestarted) ZuCheckRT(client.stop());
    ZuCheckRT(client.stop());
    ZuCheckRT(server.stop());
    srv.link = nullptr;
  }
  client.final();
  server.final();
  mx.stop();
  ZuCheckRT(!srv.failures && !cli.failures);
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

ZjrpcHTTPCatalogImpl()
