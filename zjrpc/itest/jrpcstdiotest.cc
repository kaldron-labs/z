//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZiPlatform.hh>
#include <zlib/ZjrpcClient.hh>
#include <zlib/ZjrpcServer.hh>

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

struct Pipe {
  Zi::Handle read = Zi::nullHandle();
  Zi::Handle write = Zi::nullHandle();
  ~Pipe() {
    if (!Zi::nullHandle(read)) {
      ZiFile file;
      (void)file.init(read, ZiFile::ReadOnly | ZiFile::GC);
    }
    if (!Zi::nullHandle(write)) {
      ZiFile file;
      (void)file.init(write, ZiFile::WriteOnly | ZiFile::GC);
    }
  }
  bool open() {
#ifndef _WIN32
    int handles[2];
    if (::pipe(handles)) return false;
    read = handles[0];
    write = handles[1];
#else
    if (!CreatePipe(&read, &write, nullptr, 0)) return false;
#endif
    return true;
  }
};

struct SrvApp {
  ZmFn<void()> deferred;
  ZmSemaphore arrived;
  ZmSemaphore notified;
  unsigned calls = 0;
  unsigned closes = 0;
  unsigned failures = 0;

  template <typename Token>
  void request(Echo *, const Value &value, Token token) {
    if (!token) { notified.post(); return; }
    ++calls;
    if (value.value != 42) {
      token->complete(Zjrpc::Reply<EchoOK>{value});
      return;
    }
    deferred = [value, token = ZuMv(token)]() mutable {
      token->complete(Zjrpc::Reply<EchoOK>{value});
    };
    arrived.post();
  }
  void closed() { ++closes; }
  void failed() { ++failures; }
};

struct CliApp {
  ZmSemaphore notified;
  unsigned calls = 0;
  unsigned closes = 0;
  unsigned failures = 0;
  template <typename Token>
  void request(Echo *, const Value &value, Token token) {
    if (!token) { notified.post(); return; }
    ++calls;
    token->complete(Zjrpc::Reply<EchoOK>{value});
  }
  void ready() { }
  void closed() { ++closes; }
  void failed() { ++failures; }
};

struct Call : public ZmNHeap<Call>, public ZmObject {
friend inline ZuStringT<"ZjrpcITest.Call"> ZmNHeapID(Call *);

  ZmSemaphore done;
  int value = 0;
  unsigned failures = 0;
  void process(Zjrpc::Reply<EchoOK> reply) { value = reply.body.value; done.post(); }
  void failed() { ++failures; done.post(); }
  void failed(const Zjrpc::Error &) { failed(); }
};

struct BatchCall : public ZmNHeap<BatchCall>, public ZmObject {
friend inline ZuStringT<"ZjrpcITest.BatchCall"> ZmNHeapID(BatchCall *);

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

static ZiMxParams mxParams()
{
  return ZiMxParams{}.scheduler([](auto &scheduler) {
    scheduler.nThreads(6)
      .thread(1, [](auto &thread) { thread.isolated(1); })
      .thread(2, [](auto &thread) { thread.isolated(1); })
      .thread(3, [](auto &thread) { thread.isolated(1).name("srvRx"); })
      .thread(4, [](auto &thread) { thread.isolated(1).name("srvTx"); })
      .thread(5, [](auto &thread) { thread.isolated(1).name("cliRx"); })
      .thread(6, [](auto &thread) { thread.isolated(1).name("cliTx"); });
  }).rxThread(1).txThread(2);
}

static void testIO()
{
  ZuTestScopeRT(testIO);
  Pipe requests, replies;
  ZuCheckRT(requests.open(), (return));
  ZuCheckRT(replies.open(), (return));
  ZiMultiplex mx{mxParams()};
  ZuCheckRT(mx.start(), (return));
  SrvApp srv;
  CliApp cli;
  Zjrpc::IOServer<SrvApp, Catalog> server;
  Zjrpc::IOClient<CliApp, Catalog> client;
  ZuCheckRT(server.init(&mx, Zjrpc::StdioConfig{}
    .input(requests.read).output(replies.write)
    .rxThread("srvRx").txThread("srvTx"), &srv));
  ZuCheckRT(client.init(&mx, Zjrpc::StdioConfig{}
    .input(replies.read).output(requests.write)
    .rxThread("cliRx").txThread("cliTx"), &cli));
  ZuCheckRT(server.start());
  ZuCheckRT(client.start());
  ZmRef<Call> call = new Call{};
  ZuCheckRT(client.call<Echo>(Value{42}, call));
  srv.arrived.wait();
  auto complete = ZuMv(srv.deferred);
  complete(); // Complete from outside the endpoint's owner shard.
  call->done.wait();
  ZuCheckRT(call->value == 42 && !call->failures && srv.calls == 1);
  ZmRef<Call> reverse = new Call{};
  ZuCheckRT(server.call<Echo>(Value{71}, reverse));
  reverse->done.wait();
  ZuCheckRT(reverse->value == 71 && !reverse->failures && cli.calls == 1);
  Zjrpc::Batch<Catalog> batch;
  ZuCheckRT(batch.request<Echo>(Zjrpc::ID{int64_t{2001}}, Value{42}));
  batch.notify<Echo>(Value{0});
  ZuCheckRT(batch.request<Echo>(Zjrpc::ID{int64_t{2002}}, Value{11}));
  ZmRef<BatchCall> aggregate = new BatchCall{};
  ZuCheckRT(client.callBatch(ZuMv(batch), aggregate));
  srv.arrived.wait();
  auto batchComplete = ZuMv(srv.deferred);
  batchComplete();
  aggregate->done.wait();
  srv.notified.wait();
  ZuCheckRT(aggregate->completions == 1 && !aggregate->failures);
  const auto &entries = aggregate->reply.entries();
  ZuCheckRT(entries.length() == 2);
  if (entries.length() == 2) {
    auto first = Zjrpc::decode(entries[0].ptr());
    auto last = Zjrpc::decode(entries[1].ptr());
    ZuCheckRT(first.id() == Zjrpc::ID{int64_t{2002}} && last.id() == Zjrpc::ID{int64_t{2001}});
  }
  Zjrpc::Batch<Catalog> notifications;
  notifications.notify<Echo>(Value{0});
  notifications.notify<Echo>(Value{1});
  ZuCheckRT(client.notifyBatch(ZuMv(notifications)));
  srv.notified.wait();
  srv.notified.wait();
  ZuCheckRT(server.notify<Echo>(Value{1}));
  cli.notified.wait();
  ZuCheckRT(client.notify<Echo>(Value{2}));
  srv.notified.wait();
  ZuCheckRT(client.stop());
  ZuCheckRT(server.stop());
  ZiFile retained;
  ZuCheckRT(retained.init(requests.write, ZiFile::WriteOnly) == Zi::OK);
  bool serverRestarted = server.start();
  bool clientRestarted = client.start();
  ZuCheckRT(serverRestarted && clientRestarted);
  if (serverRestarted && clientRestarted) {
    ZuCheckRT(client.start()); // canonical idempotence does not rerun ready()
    ZmRef<Call> restarted = new Call{};
    bool sent = client.call<Echo>(Value{17}, restarted);
    ZuCheckRT(sent);
    bool completed = sent && !restarted->done.timedwait(Zm::now() + ZuTime{5});
    ZuCheckRT(completed);
    if (completed) ZuCheckRT(restarted->value == 17 && !restarted->failures);
  }
  ZmSemaphore stopped;
  bool stopOK = false;
  client.stop([&stopped, &stopOK](bool ok) { stopOK = ok; stopped.post(); });
  stopped.wait();
  ZuCheckRT(stopOK && client.state() == ZmEngineState::Stopped);
  ZuCheckRT(client.stop());
  ZuCheckRT(server.stop());
  ZuCheckRT(server.stop());
  client.final();
  server.final();
  mx.stop();
  ZuCheckRT(!srv.failures && !cli.failures);
  ZuCheckRT(srv.closes == 2 && cli.closes == 2);
}

static void testInitAndFailure()
{
  ZuTestScopeRT(testInitAndFailure);
  ZiMultiplex mx{mxParams()};
  ZuCheckRT(mx.start(), (return));
  CliApp app;
  Zjrpc::IOClient<CliApp, Catalog> client;
  auto config = Zjrpc::StdioConfig{}.input(Zi::nullHandle())
    .rxThread("cliRx").txThread("cliTx");
  ZuCheckRT(client.init(&mx, config, &app));
  client.final(); // no runtime was constructed
  ZuCheckRT(client.init(&mx, config, &app));
  ZuCheckRT(!client.start());
  ZuCheckRT(client.state() == ZmEngineState::Stopped && !client.up());
  ZuCheckRT(app.failures == 1);
  client.final(); // startup rollback already released its duplicated output
  mx.stop();
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testIO);
  ZuTestCall(testInitAndFailure);
  return 0;
}

ZjrpcHTTPCatalogImpl()
