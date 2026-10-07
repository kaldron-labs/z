//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZjrpcClient.hh>
#include <zlib/ZjrpcServer.hh>

using namespace ZuTestUtil;

struct Value { int value = 0; };
ZfStruct(, (Value, JSON), (value, (Ctor<0>, Required), Int32));
struct EchoOK : public Zjrpc::Response {
  using Body = Value;
  using Headers = ZhttpHeaders("x-result");
};
struct Echo : public Zjrpc::Request {
  using Object = Value;
  using Method = ZuStringT<"echo">;
  using Responses = ZuTypeList<EchoOK>;
  using Headers = ZhttpHeaders("x-call");
};
struct Stream : public Echo {
  using Method = ZuStringT<"stream">;
  enum { ResponseBody = Zjrpc::BodyPolicy::SSE };
};
using Catalog = ZuTypeList<Echo, Stream>;
using CallHdr = ZuStringT<"x-call">;
using ResultHdr = ZuStringT<"x-result">;

struct SrvApp {
  ZmSemaphore listening_;
  ZmSemaphore arrived;
  ZmSemaphore notified;
  ZmFn<void()> deferred;
  ZuRef<Zjrpc::HTTPPeer> context;
  unsigned calls = 0;
  unsigned enriched = 0;
  unsigned failures = 0;

  void listening(int, unsigned) { listening_.post(); }
  void listenFailed(int, bool) { ++failures; listening_.post(); }
  void connected(int) { }
  void disconnected(int) { }
  ZuRef<Zjrpc::HTTPPeer> peer(const auto &) { return context; }
  template <typename Req, typename Token>
  void request(Req *, const Value &value, const auto &http, Token token) {
    auto field = http.headers.template get<CallHdr>();
    if (field.count == 1 && field.value == "call") ++enriched;
    if (!token) { notified.post(); return; }
    ++calls;
    if (value.value == 42) {
      deferred = [value, token = ZuMv(token)]() mutable {
	token->complete(Zjrpc::Reply<EchoOK>{value});
      };
      arrived.post();
    } else token->complete(Zjrpc::Reply<EchoOK>{value});
  }
  template <typename Res, typename Key, typename L>
  void responseHeader(const Zjrpc::Reply<Res> &, L &&l) {
    if constexpr (ZuIsSame<Key, ResultHdr>{}) l("result");
  }
};

struct CliApp {
  ZmSemaphore ready_;
  unsigned enriched = 0;
  unsigned failures = 0;
  void ready() { ready_.post(); }
  void failed() { ++failures; }
  template <typename Req, typename Key, typename L>
  void requestHeader(const Value &, L &&l) {
    if constexpr (ZuIsSame<Key, CallHdr>{}) l("call");
  }
  void headers(const Zjrpc::ID &, const auto &headers) {
    auto field = headers.template get<ResultHdr>();
    if (field.count == 1 && field.value == "result") ++enriched;
  }
  template <typename Req, typename Token>
  void request(Req *, const Value &, Token) { ++failures; }
};

template <typename Heap = ZuVoid>
struct Call_ : public Heap, public ZmObject {
  ZmSemaphore done;
  int value = 0;
  unsigned failures = 0;
  void process(Zjrpc::Reply<EchoOK> reply) { value = reply.body.value; done.post(); }
  void failed() { ++failures; done.post(); }
  void failed(const Zjrpc::Error &) { failed(); }
};
using CallHeap = ZmHeap<"ZjrpcHTTPTest.Call", Call_<>>;
using Call = Call_<CallHeap>;

template <typename Heap = ZuVoid>
struct BatchCall_ : public Heap, public ZmObject {
  ZmSemaphore done;
  Zjrpc::BatchReply reply;
  unsigned completions = 0;
  unsigned failures = 0;
  void process(Zjrpc::BatchReply reply_) {
    reply = ZuMv(reply_);
    ++completions;
    done.post();
  }
  void failed() { ++failures; done.post(); }
};
using BatchCallHeap = ZmHeap<"ZjrpcITest.BatchCall", BatchCall_<>>;
using BatchCall = BatchCall_<BatchCallHeap>;

static void testHTTP()
{
  ZuTestScope(testHTTP);
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &scheduler) {
    scheduler.nThreads(2)
      .thread(1, [](auto &thread) { thread.isolated(1); })
      .thread(2, [](auto &thread) { thread.isolated(1); });
  }).rxThread(1).txThread(2)};
  ZuCheck(mx.start(), (return));
  SrvApp srv;
  CliApp cli;
  Zjrpc::HTTPServer<SrvApp, Catalog> server;
  Zjrpc::HTTPClient<CliApp, Catalog> client;
  Zhttp::HubConfig hub{&mx, "1", "2"};
  Zjrpc::ServerConfig serverConfig;
  serverConfig.endpoint("/rpc").http([](auto &http) {
    http.localIP(ZiIP{"127.0.0.1"}).port(21100).tcp();
  });
  bool initialized = server.init(hub, ZuMv(serverConfig), &srv);
  ZuCheck(initialized);
  if (!initialized) { mx.stop(); return; }
  srv.context = Zjrpc::HTTPPeer::create(server);
  bool started = server.start();
  ZuCheck(started);
  if (!started) { server.final(); mx.stop(); return; }
  srv.listening_.wait();
  Zjrpc::ClientConfig clientConfig;
  clientConfig.endpoint("/rpc").http([](auto &http) {
    http.secure(false).tcp(true).tls(false).quic(false)
      .protocol(Zhttp::ProtoPolicy::DisableH3).h2Policy(Zhttp::H2Policy::Disable)
      .links(1).linkMax(1).concurrency(4).requestTimeout(5);
  });
  bool clientReady = !srv.failures && client.init(hub,
    Zhttp::Destination{"127.0.0.1", 21100}, ZuMv(clientConfig), &cli) && client.start();
  ZuCheck(clientReady);
  if (clientReady) {
    cli.ready_.wait();
    ZmRef<Call> fixed = new Call{};
    ZuCheck(client.call<Echo>(Value{42}, fixed));
    srv.arrived.wait();
    auto complete = ZuMv(srv.deferred);
    complete();
    fixed->done.wait();
    ZuCheck(fixed->value == 42 && !fixed->failures);
    ZmRef<Call> stream = new Call{};
    ZuCheck(client.call<Stream>(Value{73}, stream));
    stream->done.wait();
    ZuCheck(stream->value == 73 && !stream->failures);
    Zjrpc::Batch<Catalog> batch;
    ZuCheck(batch.request<Echo>(Zjrpc::ID{int64_t{2001}}, Value{12}));
    ZuCheck(batch.request<Stream>(Zjrpc::ID{int64_t{2002}}, Value{13}));
    ZmRef<BatchCall> aggregate = new BatchCall{};
    ZuCheck(client.callBatch(ZuMv(batch), aggregate));
    aggregate->done.wait();
    ZuCheck(aggregate->completions == 1 && !aggregate->failures);
    ZuCheck(aggregate->reply.entries().length() == 2);
    ZuCheck(client.notify<Echo>(Value{1}));
    srv.notified.wait();
    ZuCheck(client.stop());
  }
  client.final();
  ZuCheck(server.stop());
  server.final();
  mx.stop();
  ZuCheck(srv.calls == 4 && srv.enriched == 3 && !srv.failures);
  ZuCheck(cli.enriched == 2 && !cli.failures);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testHTTP);
  return 0;
}
