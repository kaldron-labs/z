//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmcpClient.hh>
#include <zlib/ZmcpServer.hh>
#include <zlib/ZjrpcServer.hh>

#include "ZiTestResidue.hh"
#include "ZquicInteropTest.hh"
#include "ZmcpITestPorts.hh"

#include "zmcpitestbatch.hh"

using namespace ZuTestUtil;

struct Value { int value = 0; };
ZfStruct(, (Value, JSON), (value, (Ctor<0>, Required), Int32));
struct EchoOK : public Zmcp::Response { using Body = Value; };
struct Echo : public Zmcp::Request {
  using Object = Value;
  using OperationID = ZuStringT<"echoValue">;
  using ToolID = ZuStringT<"echo">;
  using Responses = ZuTypeList<EchoOK>;
};
using Catalog = ZuTypeList<Echo>;

static bool wait(ZmSemaphore &semaphore)
{
  return semaphore.timedwait(Zm::now(10)) == 0;
}

struct SrvApp {
  ZmSemaphore listening_;
  ZmSemaphore arrived;
  ZmSemaphore cancelled_;
  ZmSemaphore closed_;
  ZmFn<void()> deferred;
  ZmFn<void()> deferredBatch;
  unsigned failures = 0;
  unsigned calls = 0;

  void listening() { listening_.post(); }
  void listening(const ZiListenInfo &) { listening_.post(); }
  void listenFailed(bool) { ++failures; listening_.post(); }
  bool accept(auto &, ZuBSpan, ZuBSpan target, ZuBSpan protocols,
      Zws::HandshakeString &selected) {
    if (target != "/mcp" || !Zws::token(protocols, "mcp")) return false;
    selected = "mcp";
    return true;
  }
  template <typename Token>
  void tool(Echo *, const Value &value, const auto &, const Zmcp::Context &, Token token) {
    ++calls;
    if (value.value == 43) {
      deferredBatch = [value, token = ZuMv(token)]() mutable {
	token->complete(Zjrpc::Reply<EchoOK>{value});
      };
      arrived.post();
      return;
    }
    if (value.value == 42) {
      (void)token->progress(1, 2, "working");
      (void)token->log("info", "message", "test");
      deferred = [value, token = ZuMv(token)]() mutable {
	token->complete(Zjrpc::Reply<EchoOK>{value});
      };
      arrived.post();
      return;
    }
    token->complete(Zjrpc::Reply<EchoOK>{value});
  }
  void cancelled(auto *, auto *, ZuCSpan) { cancelled_.post(); }
  void disconnected(auto &, bool) { closed_.post(); }
};

template <typename Profile>
struct CliApp {
  using Client = Zmcp::WSClient<CliApp, Catalog, Profile>;
  using Link = typename Client::Link;

  ZmRef<Link> link;
  ZmSemaphore closed_;
  ZmSemaphore ready_;
  ZmSemaphore tools_;
  ZmSemaphore progress_;
  ZmSemaphore logging_;
  Zjrpc::ID progressID;
  unsigned failures = 0;
  unsigned catalogs = 0;
  int era = Zmcp::Era::Unknown;

  void ready(int value) { era = value; ready_.post(); }
  void connected(Link &value, Zhttp::ConnectedInfo) { link = &value; }
  void failed() { ++failures; ready_.post(); closed_.post(); }
  void closed() { closed_.post(); }
  void tools(const ZfJSON::AnyNode *catalog) { if (catalog) ++catalogs; tools_.post(); }
  void toolsFailed() { ++failures; tools_.post(); }
  void progress(const Zjrpc::ID &id, double, double, ZuCSpan) {
    progressID = id;
    progress_.post();
  }
  void logging(ZuCSpan, const ZfJSON::AnyNode *, ZuCSpan) { logging_.post(); }
};

template <typename Heap = ZuVoid>
struct Call_ : public Heap, public ZmObject {
  ZmSemaphore started_;
  ZmSemaphore done;
  Zjrpc::ID id;
  int value = 0;
  unsigned controls = 0;
  unsigned failures = 0;

  void started(const Zjrpc::ID &value_) { id = value_; started_.post(); }
  void process(Zjrpc::Reply<EchoOK> reply) { value = reply.body.value; done.post(); }
  void process() { ++controls; done.post(); }
  void failed() { ++failures; done.post(); }
  void failed(const Zjrpc::Error &) { failed(); }
};
using CallHeap = ZmHeap<"ZmcpWSTest.Call", Call_<>>;
using Call = Call_<CallHeap>;

// No server/discover declaration: the generic dispatcher returns MethodNotFound,
// exercising ClientPeer's real legacy fallback without a production mode flag.
struct LegacyInitOK : public Zjrpc::Response { using Body = Zmcp::InitializeResult; };
struct LegacyInit : public Zjrpc::Request {
  using Method = ZuStringT<"initialize">;
  using Object = Zmcp::InitializeParams;
  using Responses = ZuTypeList<LegacyInitOK>;
};
struct LegacyToolsOK : public Zjrpc::Response {
  using Body = Zmcp::SchemaModel_::ToolsResult<Catalog>;
};
struct LegacyTools : public Zjrpc::Request {
  using Method = ZuStringT<"tools/list">;
  using Object = Zmcp::MetaParams;
  using Responses = ZuTypeList<LegacyToolsOK>;
};
struct LegacyBody : public ZuStructShim<LegacyBody, Zmcp::CallResultShape> {
  Value value;

  explicit LegacyBody(Value v) : value{v} { }
  Zmcp::ServerMetaOpt meta() const { return {}; }
  ZuCSpan resultType() const { return {}; }
  Zmcp::EmptyContent content() const { return {}; }
  auto structuredContent() const { return Zmcp::StructuredResult{EchoOK::Status, value}; }
  bool isError() const { return false; }
};
struct LegacyCallOK : public Zjrpc::Response { using Body = LegacyBody; };
struct LegacyParams {
  ZuCSpan name;
  Value arguments;
  Zmcp::ClientMetaOpt meta;
};
ZfStruct(, (LegacyParams, JSON),
  (name, (Mutable), String),
  (arguments, (Mutable), UDT),
  (meta, (Mutable, JSON::Opt, JSON::ID<"_meta">), UDT));
struct LegacyCall : public Zjrpc::Request {
  using Method = ZuStringT<"tools/call">;
  using Object = LegacyParams;
  using Responses = ZuTypeList<LegacyCallOK>;
};
using LegacyCatalog = ZuTypeList<LegacyInit, LegacyTools, LegacyCall>;

struct LegacyApp {
  ZmSemaphore listening_;
  unsigned failures = 0;
  unsigned calls = 0;
  bool valid = true;

  void listening() { listening_.post(); }
  void listening(const ZiListenInfo &) { listening_.post(); }
  void listenFailed(bool) { ++failures; listening_.post(); }
  bool accept(auto &, ZuBSpan, ZuBSpan target, ZuBSpan protocols,
      Zws::HandshakeString &selected) {
    if (target != "/mcp" || !Zws::token(protocols, "mcp")) return false;
    selected = "mcp";
    return true;
  }
  void request(LegacyInit *, const Zmcp::InitializeParams &params, auto &, auto token) {
    valid &= params.protocolVersion == Zmcp::LegacyVersion{}();
    token->complete(Zjrpc::Reply<LegacyInitOK>{
      {Zmcp::LegacyVersion{}(), {}, {"legacy-test", "1"}}});
  }
  void request(LegacyTools *, const Zmcp::MetaParams &, auto &, auto token) {
    token->complete(Zjrpc::Reply<LegacyToolsOK>{{Zmcp::Era::Legacy}});
  }
  void request(LegacyCall *, const LegacyCall::Object &params, auto &, auto token) {
    valid &= params.name == Echo::ToolID{}();
    // Modern client metadata must be suppressed after legacy negotiation,
    // including members stamped by prepareBatch on the owner shard.
    valid &= params.meta.template is<void>();
    ++calls;
    token->complete(Zjrpc::Reply<LegacyCallOK>{LegacyBody{params.arguments}});
  }
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
  SrvApp srv;
  CliApp<Profile> cli;
  Zmcp::WSServer<SrvApp, Catalog, Profile> server;
  typename CliApp<Profile>::Client client;
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
    << "://127.0.0.1:" << port << "/mcp";
  bool ready = listening && Zws::URI::parse(uri, text).ok() && client.start() &&
    client.connect(uri, "mcp") && wait(cli.ready_) && !cli.failures;
  ZuCheck(ready);
  if (ready) {
    ZuCheck(cli.era == Zmcp::Era::Modern);
    ZuCheck(client.tools());
    ZuCheck(wait(cli.tools_) && cli.catalogs == 1);
    ZmRef<Call> ping = new Call{};
    ZuCheck(client.ping(ping));
    ZuCheck(wait(ping->done) && ping->controls == 1 && !ping->failures);
    Zmcp::Batch<Catalog> batch;
    ZuCheck(batch.template request<Echo>(int64_t{1001}, Value{11}));
    ZuCheck(batch.template request<Echo>(int64_t{1002}, Value{12}));
    ZmRef<ZmcpITest::BatchCall> aggregate = new ZmcpITest::BatchCall{};
    ZuCheck(client.callBatch(ZuMv(batch), aggregate));
    ZuCheck(wait(aggregate->done) && aggregate->completions == 1 && !aggregate->failures);
    ZuCheck(aggregate->ids(1001, 1002));
    // MCP ignores tools/call notifications; the shared batch path must not
    // manufacture responses or leave pending calls behind for these members.
    Zmcp::Batch<Catalog> notifications;
    notifications.template notify<Echo>(Value{11});
    notifications.template notify<Echo>(Value{12});
    ZuCheck(client.notifyBatch(ZuMv(notifications)));
    ZmRef<Call> call = new Call{};
    Zjrpc::ID progress{int64_t{123}};
    ZuCheck(client.template callProgressLog<Echo>(
      Value{42}, progress, Zmcp::LogLevel::Info, call));
    bool arrived = wait(srv.arrived);
    ZuCheck(arrived && wait(call->started_));
    ZuCheck(wait(cli.progress_) && cli.progressID == progress);
    ZuCheck(wait(cli.logging_));
    ZuCheck(client.cancel(call->id, "stop"));
    ZuCheck(wait(srv.cancelled_));
    if (arrived) { auto complete = ZuMv(srv.deferred); complete(); }
    ZuCheck(wait(call->done) && call->value == 42 && !call->failures);
    Zmcp::Batch<Catalog> interrupted;
    ZuCheck(interrupted.template request<Echo>(int64_t{2001}, Value{43}));
    ZuCheck(interrupted.template request<Echo>(int64_t{2002}, Value{45}));
    ZmRef<ZmcpITest::BatchCall> pendingBatch = new ZmcpITest::BatchCall{};
    ZuCheck(client.callBatch(ZuMv(interrupted), pendingBatch));
    ZuCheck(wait(srv.arrived));
    ZuCheck(client.cancel(Zjrpc::ID{int64_t{2001}}, "batch stop"));
    ZuCheck(wait(srv.cancelled_));
    ZmRef<Call> abandoned = new Call{};
    ZuCheck(client.template call<Echo>(Value{42}, abandoned));
    ZuCheck(wait(srv.arrived));
    ZuCheck(client.stop());
    ZuCheck(wait(abandoned->done) && abandoned->failures == 1);
    ZuCheck(wait(pendingBatch->done) && pendingBatch->failures == 1 &&
      !pendingBatch->completions);
  }
  cli.link = nullptr;
  client.final();
  ZuCheck(server.stop());
  server.final();
  srv.deferred = {};
  srv.deferredBatch = {};
  mx.stop();
  ZuCheck(!srv.failures && !cli.failures);
}

template <typename Profile>
static void testLegacyWS(const Zquic::Test::TempDir &temp, unsigned port)
{
  ZuTestScope(testLegacyWS);
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &scheduler) {
    scheduler.nThreads(2)
      .thread(1, [](auto &thread) { thread.isolated(1); })
      .thread(2, [](auto &thread) { thread.isolated(1); });
  }).rxThread(1).txThread(2)};
  ZuCheck(mx.start(), (return));
  LegacyApp srv;
  CliApp<Profile> cli;
  Zjrpc::WSServer<LegacyApp, LegacyCatalog, Profile> server;
  typename CliApp<Profile>::Client client;
  Zhttp::HubConfig hub{&mx, "1", "2"};
  Zjrpc::Limits limits;
  limits.workBatch = 1;
  bool initialized = server.init(hub, ZiIP{"127.0.0.1"}, port,
    profileConfig<Profile, true>(temp), Zjrpc::WSConfig{}.limits(limits), &srv) &&
    client.init(hub, profileConfig<Profile, false>(temp),
      Zjrpc::WSConfig{}.limits(limits), &cli);
  ZuCheck(initialized);
  if (!initialized) { client.final(); server.final(); mx.stop(); return; }
  bool started = server.start();
  ZuCheck(started);
  bool listening = started && wait(srv.listening_) && !srv.failures;
  ZuCheck(listening);
  Zws::URI uri;
  ZtString<> text;
  text << (ZuIsSame<Profile, Zhttp::H1TCP>{} ? "ws" : "wss")
    << "://127.0.0.1:" << port << "/mcp";
  bool ready = listening && Zws::URI::parse(uri, text).ok() && client.start() &&
    client.connect(uri, "mcp") && wait(cli.ready_) && !cli.failures;
  ZuCheck(ready);
  if (ready) {
    ZuCheck(cli.era == Zmcp::Era::Legacy);
    ZuCheck(client.tools());
    ZuCheck(wait(cli.tools_) && cli.catalogs == 1);
    ZmRef<Call> call = new Call{};
    ZuCheck(client.template call<Echo>(Value{17}, call));
    ZuCheck(wait(call->done) && call->value == 17 && !call->failures);
    Zmcp::Batch<Catalog> batch;
    ZuCheck(batch.template request<Echo>(int64_t{1001}, Value{18}));
    ZuCheck(batch.template request<Echo>(int64_t{1002}, Value{19}));
    ZmRef<ZmcpITest::BatchCall> aggregate = new ZmcpITest::BatchCall{};
    ZuCheck(client.callBatch(ZuMv(batch), aggregate));
    ZuCheck(wait(aggregate->done) && aggregate->completions == 1 && !aggregate->failures);
    ZuCheck(aggregate->ids(1001, 1002));
  }
  ZuCheck(client.stop());
  cli.link = nullptr;
  client.final();
  ZuCheck(server.stop());
  server.final();
  mx.stop();
  ZuCheck(!srv.failures && !cli.failures);
  ZuCheck(srv.calls == 3);
  ZuCheck(srv.valid);
}

// Exercise the binding's rejection boundary, leaving frame validation and
// close-handshake algorithms to Zws's existing protocol tests.
template <typename Profile>
static void testBadWS(const Zquic::Test::TempDir &temp, unsigned port, int kind)
{
  ZuTestScope(testBadWS);
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &scheduler) {
    scheduler.nThreads(2)
      .thread(1, [](auto &thread) { thread.isolated(1); })
      .thread(2, [](auto &thread) { thread.isolated(1); });
  }).rxThread(1).txThread(2)};
  ZuCheck(mx.start(), (return));
  SrvApp srv;
  CliApp<Profile> cli;
  Zmcp::WSServer<SrvApp, Catalog, Profile> server;
  typename CliApp<Profile>::Client client;
  Zhttp::HubConfig hub{&mx, "1", "2"};
  Zjrpc::Limits limits;
  limits.maxJSONBytes = 1024;
  bool initialized = server.init(hub, ZiIP{"127.0.0.1"}, port,
    profileConfig<Profile, true>(temp), Zjrpc::WSConfig{}.limits(limits), &srv) &&
    client.init(hub, profileConfig<Profile, false>(temp),
      Zjrpc::WSConfig{}.limits(limits), &cli);
  ZuCheck(initialized);
  if (!initialized) { client.final(); server.final(); mx.stop(); return; }
  bool started = server.start();
  ZuCheck(started);
  bool listening = started && wait(srv.listening_) && !srv.failures;
  ZuCheck(listening);
  Zws::URI uri;
  ZtString<> text;
  text << (ZuIsSame<Profile, Zhttp::H1TCP>{} ? "ws" : "wss")
    << "://127.0.0.1:" << port << "/mcp";
  bool ready = listening && Zws::URI::parse(uri, text).ok() && client.start() &&
    client.connect(uri, "mcp") && wait(cli.ready_) && !cli.failures;
  ZuCheck(ready);
  if (ready) {
    client.ws().txRun([link = cli.link, kind, size = limits.maxJSONBytes]() mutable {
      link->txStream_([kind, size](auto &tx) {
	switch (kind) {
	  case 0: tx << "{}"; break;
	  case 1: tx << "{"; break;
	  case 2: tx << "{\"jsonrpc\":\"2.0\",\"id\":null,\"method\":\"tools/call\"}"; break;
	  case 3:
	    for (unsigned i = 0; i <= size; ++i) tx << ' ';
	    break;
	}
	tx.flush();
      }, kind == 0 ? Zws::Opcode::Binary : Zws::Opcode::Text);
    });
    ZuCheck(wait(srv.closed_));
    ZuCheck(wait(cli.closed_));
  }
  // Rejected input may close normally or report a native protocol failure.
  bool stopped = client.stop();
  ZuCheck(stopped == !cli.failures);
  cli.link = nullptr;
  client.final();
  ZuCheck(server.stop());
  server.final();
  mx.stop();
  ZuCheck(!srv.failures && !srv.calls);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZiTestResidue::init("zmcpwstest");
  ZuTestMain();
  Zquic::Test::TempDir temp;
  bool initialized = temp.init();
  ZuCheck(initialized);
  if (initialized) {
    ZuTestCall(testWS<Zhttp::H1TCP>, temp, ZmcpITestPort::WS);
    ZuTestCall(testWS<Zhttp::H1TLS>, temp, ZmcpITestPort::WSS);
    ZuTestCall(testWS<Zhttp::H2TLS>, temp, ZmcpITestPort::WSH2);
    ZuTestCall(testWS<Zhttp::H3QUIC>, temp, ZmcpITestPort::WSH3);
    ZuTestCall(testLegacyWS<Zhttp::H1TCP>, temp, ZmcpITestPort::WS);
    ZuTestCall(testLegacyWS<Zhttp::H1TLS>, temp, ZmcpITestPort::WSS);
    ZuTestCall(testLegacyWS<Zhttp::H2TLS>, temp, ZmcpITestPort::WSH2);
    ZuTestCall(testLegacyWS<Zhttp::H3QUIC>, temp, ZmcpITestPort::WSH3);
    ZuTestCall(testBadWS<Zhttp::H1TCP>, temp, ZmcpITestPort::WS, 0);
    ZuTestCall(testBadWS<Zhttp::H1TLS>, temp, ZmcpITestPort::WSS, 1);
    ZuTestCall(testBadWS<Zhttp::H2TLS>, temp, ZmcpITestPort::WSH2, 2);
    ZuTestCall(testBadWS<Zhttp::H3QUIC>, temp, ZmcpITestPort::WSH3, 3);
  }
  return 0;
}
