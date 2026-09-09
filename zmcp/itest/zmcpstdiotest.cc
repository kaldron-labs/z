//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuObject.hh>
#include <zlib/ZuRef.hh>

#include <zlib/ZmFn.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiPlatform.hh>

#include <zlib/Zmcp.hh>
#include <zlib/ZmcpClient.hh>
#include <zlib/ZmcpServer.hh>

using namespace ZuTestUtil;

static void testFramer()
{
  ZuTestScope(testFramer);

  Zmcp::StdioFramer framer{12};
  ZtString<> first;
  ZtString<> second;
  unsigned frames = 0;
  auto receive = [&first, &second, &frames](ZmRef<ZiIOBuf> frame) {
    (++frames == 1 ? first : second) << frame->cspan();
  };
  ZuCheck(framer.feed("{\"one\":", receive));
  ZuCheck(!frames);
  ZuCheck(framer.feed("1}\n{\"two\":2}\n", receive));
  ZuCheck(frames == 2);
  ZuCheck(first == "{\"one\":1}");
  ZuCheck(second == "{\"two\":2}");
  ZuCheck(framer.eof());

  Zmcp::StdioFramer incomplete{12};
  ZuCheck(incomplete.feed("{\"one\":1}", receive));
  ZuCheck(!incomplete.eof());

  Zmcp::StdioFramer bounded{4};
  ZuCheck(!bounded.feed("12345", receive));
  ZuCheck(bounded.state() == Zmcp::StdioFramer::Closed);
}

struct Pipe {
  Zi::Handle read = Zi::nullHandle();
  Zi::Handle write = Zi::nullHandle();

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

struct Harness {
  bool stdioFrame(ZmRef<ZiIOBuf> frame) {
    if (!frames) first << frame->cspan();
    else second << frame->cspan();
    ++frames;
    received.post();
    return true;
  }

  void stdioClosed() {
    ++closed;
    stopped.post();
  }

  void stdioFailed() {
    ++failed;
    stopped.post();
  }

  ZmSemaphore received;
  ZmSemaphore stopped;
  ZtString<> first;
  ZtString<> second;
  unsigned frames = 0;
  unsigned closed = 0;
  unsigned failed = 0;
};

struct EchoReq { int value = 0; };
struct EchoResult { int value = 0; };
ZfStruct(, (EchoReq, JSON),
  (((value), (Ctor<0>, Required)), (Int32)));
ZfStruct(, (EchoResult, JSON),
  (((value), (Ctor<0>, Required)), (Int32)));
struct EchoOK : public Zmcp::Response {
  using Body = EchoResult;
};
struct Echo : public Zmcp::Request {
  using Object = EchoReq;
  using OperationID = ZuStringT<"echoValue">;
  using ToolID = ZuStringT<"echo">;
  using Responses = ZuTypeList<EchoOK>;
};
using EchoCatalog = ZuTypeList<Echo>;

template <typename Heap>
struct PeerContext_ : public Heap, public ZuObject { };
using PeerContextHeap =
  ZmHeap<"ZmcpTest.PeerContext", PeerContext_<ZuVoid>>;
struct PeerContext : public PeerContext_<PeerContextHeap> { };

template <typename Heap>
struct StreamContext_ : public Heap, public ZuObject {
  StreamContext_(unsigned id_) : id{id_} { }
  unsigned id;
};
using StreamContextHeap =
  ZmHeap<"ZmcpTest.StreamContext", StreamContext_<ZuVoid>>;
struct StreamContext : public StreamContext_<StreamContextHeap> {
  using Base = StreamContext_<StreamContextHeap>;
  using Base::Base;
};

struct ServerApp {
  ZuRef<PeerContext> open(Zmcp::SessionTag, bool stdio, ZuCSpan id) {
    if (stdio && !id) ++peerOpens;
    return new PeerContext{};
  }
  ZuRef<StreamContext> open(
      Zmcp::StreamTag, const Zmcp::Context &context) {
    if (context.session<PeerContext>()) ++streamOpens;
    return new StreamContext{streamOpens};
  }
  void close(Zmcp::SessionTag, PeerContext *) { ++peerCloses; }
  void close(Zmcp::StreamTag, StreamContext *) { ++streamCloses; }

  template <typename Completion>
  void tool(
      Echo *, const EchoReq &request, const auto &,
      const Zmcp::Context &context, Completion completion) {
    if (context.session<PeerContext>() && context.stream<StreamContext>())
      ++contextCalls;
    ++calls;
    if (async) {
      complete = [completion = ZuMv(completion), request]() mutable {
	completion->complete(Zmcp::ToolReply<EchoOK>{EchoResult{request.value}});
      };
      return;
    }
    completion->complete(Zmcp::ToolReply<EchoOK>{EchoResult{request.value}});
  }

  template <typename Req, typename Completion>
  void cancelled(Req *, Completion *, ZuCSpan) {
    ++cancellations;
    cancelled_.post();
  }

  bool origin(ZuCSpan) const { return true; }
  void listening(int, unsigned) { }
  void listenFailed(int, bool) { }
  void connected(int) { }
  void disconnected(int) { }

  unsigned calls = 0;
  unsigned cancellations = 0;
  unsigned peerOpens = 0;
  unsigned peerCloses = 0;
  unsigned streamOpens = 0;
  unsigned streamCloses = 0;
  unsigned contextCalls = 0;
  bool async = false;
  ZmFn<void()> complete;
  ZmSemaphore cancelled_;
};

struct ClientApp {
  void ready(int era_) {
    era = era_;
    ready_.post();
  }
  void closed() {
    ++closes;
    closed_.post();
  }
  void failed() {
    ++failures;
    closed_.post();
  }
  void progress(
      const Zmcp::ID &token_, double value_, double total_, ZuCSpan message_) {
    token = token_;
    value = value_;
    total = total_;
    message = message_;
    progressed_.post();
  }
  void logging(
      ZuCSpan level_, const ZfJSON::AnyNode *data_, ZuCSpan logger_) {
    level = level_;
    logger = logger_;
    if (data_ && data_->template has<ZfJSON::AnyNode::String>())
      data = data_->template data<ZfJSON::AnyNode::String>();
    logged_.post();
  }
  void cancelled(const Zmcp::ID &id_, bool ok) {
    cancelledID = id_;
    cancelOK = ok;
    cancelled_.post();
  }

  ZmSemaphore ready_;
  ZmSemaphore closed_;
  ZmSemaphore progressed_;
  ZmSemaphore logged_;
  ZmSemaphore cancelled_;
  Zmcp::ID token;
  Zmcp::ID cancelledID;
  ZtString<> message;
  ZtString<> level;
  ZtString<> logger;
  ZtString<> data;
  double value = 0;
  double total = 0;
  int era = Zmcp::Era::Unknown;
  unsigned closes = 0;
  unsigned failures = 0;
  bool cancelOK = false;
};

template <typename Heap>
struct ClientCall_ : public Heap, public ZmObject {
  void started(const Zmcp::ID &id_) {
    id = id_;
    started_.post();
  }
  void process() {
    ++controls;
    done.post();
  }
  template <typename Reply>
  void process(const Reply &reply) {
    value = reply.body.value;
    done.post();
  }
  void failed(const Zmcp::Error &) {
    ++failures;
    done.post();
  }
  void failed() {
    ++failures;
    done.post();
  }

  ZmSemaphore done;
  ZmSemaphore started_;
  Zmcp::ID id;
  int value = 0;
  unsigned controls = 0;
  unsigned failures = 0;
};

using ClientCallHeap =
  ZmHeap<"ZmcpTest.ClientCall", ClientCall_<ZuVoid>>;
ZuDerive(ClientCall, (ClientCall_<ClientCallHeap>));

static ZiMxParams mxParams()
{
  return ZiMxParams{}.scheduler([](auto &sched) {
    sched.nThreads(5)
      .thread(1, [](auto &thread) { thread.isolated(true).name("netRx"); })
      .thread(2, [](auto &thread) { thread.isolated(true).name("netTx"); })
      .thread(3, [](auto &thread) { thread.isolated(true).name("owner"); })
      .thread(4, [](auto &thread) { thread.isolated(true).name("stdioRx"); })
      .thread(5, [](auto &thread) { thread.isolated(true).name("stdioTx"); });
  }).rxThread(1).txThread(2);
}

static void testPipe()
{
  ZuTestScope(testPipe);

  Pipe input;
  Pipe output;
  ZuCHECK(input.open() && output.open(), "pipe creation failed");

  ZiFile inputWrite;
  ZiFile outputRead;
  ZuCHECK(inputWrite.init(input.write, ZiFile::WriteOnly | ZiFile::GC) == Zi::OK,
    "input writer initialization failed");
  ZuCHECK(outputRead.init(output.read, ZiFile::ReadOnly | ZiFile::GC) == Zi::OK,
    "output reader initialization failed");

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "multiplexer start failed");

  Harness harness;
  Zmcp::Limits limits;
  limits.workBatch = 1;
  auto config = Zmcp::StdioConfig{}
    .limits(limits)
    .rxThread("stdioRx")
    .txThread("stdioTx")
    .input(input.read)
    .output(output.write);
  Zmcp::StdioIO<Harness> io{
    &harness, &mx, mx.sid("owner"), ZuMv(config)};

  ZmSemaphore started;
  bool startOK = false;
  mx.run([&io, &startOK, &started]() {
    startOK = io.start_();
    started.post();
  }, mx.sid("owner"));
  started.wait();
  ZuCheck(startOK);

  ZuCSpan request{"{\"one\":1}\n{\"two\":2}\r\n"};
  ZuCHECK(inputWrite.write(request.data(), request.length()) == Zi::OK,
    "stdin write failed: ", inputWrite.error());
  harness.received.wait();
  harness.received.wait();
  ZuCheck(harness.frames == 2);
  ZuCheck(harness.first == "{\"one\":1}");
  ZuCheck(harness.second == "{\"two\":2}");

  enum { WakeCycles = 32 };
  bool writesOK = true;
  for (unsigned i = 0; i < WakeCycles; ++i) {
    ZtString<> frame;
    frame << "{\"cycle\":" << i << "}\n";
    writesOK &= inputWrite.write(frame.data(), frame.length()) == Zi::OK;
    harness.received.wait();
  }
  ZuCheck(writesOK);
  ZuCheck(harness.frames == WakeCycles + 2);

  ZmSemaphore sent;
  bool sendOK = false;
  mx.run([&io, &sendOK, &sent]() {
    sendOK = io.send_(ZuCSpan{"{\"result\":3}"});
    sent.post();
  }, mx.sid("owner"));
  sent.wait();
  ZuCheck(sendOK);

  ZtArray<char> response;
  response.length(13, false);
  int length = outputRead.read(response.data(), response.length());
  ZuCHECK(length == 13, "stdout read failed: ", outputRead.error());
  ZuCSpan response_{response.data(), unsigned(length)};
  ZuCheck(response_ == "{\"result\":3}\n");

  enum { TerminalFrames = 32 };
  ZtString<> expected;
  bool terminalOK = true;
  ZmSemaphore terminalSent;
  mx.run([&io, &expected, &terminalOK, &terminalSent]() {
    for (unsigned i = 0; i < TerminalFrames; ++i) {
      ZtString<> frame;
      frame << "{\"terminal\":" << i << '}';
      expected << frame << '\n';
      terminalOK &= io.send_(ZuCSpan{frame});
    }
    terminalSent.post();
  }, mx.sid("owner"));
  terminalSent.wait();
  ZuCheck(terminalOK);
  inputWrite.close();
  harness.stopped.wait();
  ZuCheck(harness.closed == 1);
  ZuCheck(harness.failed == 0);
  response.length(expected.length(), false);
  length = outputRead.read(response.data(), response.length());
  ZuCHECK(length == int(expected.length()),
    "terminal stdout read failed: ", outputRead.error());
  response_ = ZuCSpan{response.data(), unsigned(length)};
  ZuCheck(response_ == expected);

  outputRead.close();
  ZuCheck(mx.stop());
}

static void testFailure()
{
  ZuTestScope(testFailure);

  Pipe input;
  Pipe output;
  ZuCHECK(input.open() && output.open(), "pipe creation failed");

  ZiFile inputWrite;
  ZiFile outputRead;
  ZuCHECK(inputWrite.init(input.write, ZiFile::WriteOnly | ZiFile::GC) == Zi::OK,
    "input writer initialization failed");
  ZuCHECK(outputRead.init(output.read, ZiFile::ReadOnly | ZiFile::GC) == Zi::OK,
    "output reader initialization failed");

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "multiplexer start failed");

  enum { TestFrameBytes = 128U << 10 };
  Zmcp::Limits limits;
  limits.maxLineBytes = TestFrameBytes;
  limits.maxQueue = 1;
  limits.maxQueueBytes = TestFrameBytes;
  auto config = Zmcp::StdioConfig{}
    .limits(limits)
    .rxThread("stdioRx")
    .txThread("stdioTx")
    .input(input.read)
    .output(output.write);
  Harness harness;
  Zmcp::StdioIO<Harness> io{
    &harness, &mx, mx.sid("owner"), ZuMv(config)};

  ZmSemaphore started;
  bool startOK = false;
  mx.run([&io, &startOK, &started]() {
    startOK = io.start_();
    started.post();
  }, mx.sid("owner"));
  started.wait();
  ZuCheck(startOK);

  ZmRef<ZiIOBuf> first = new Zmcp::StdioBuf{};
  ZmRef<ZiIOBuf> second = new Zmcp::StdioBuf{};
  ZuCHECK(first->alloc(TestFrameBytes) && second->alloc(TestFrameBytes),
    "test frame allocation failed");
  memset(first->data(), 'x', TestFrameBytes);
  memset(second->data(), 'y', TestFrameBytes);
  first->length = second->length = TestFrameBytes;
  first->end()[-1] = second->end()[-1] = '\n';

  outputRead.close();
  ZmSemaphore sent;
  bool firstOK = false;
  bool secondOK = true;
  mx.run([&io, &firstOK, &secondOK, &first, &second, &sent]() mutable {
    firstOK = io.send_(ZuMv(first));
    secondOK = io.send_(ZuMv(second));
    sent.post();
  }, mx.sid("owner"));
  sent.wait();
  ZuCheck(firstOK && !secondOK);
  harness.stopped.wait();
  ZuCheck(harness.failed == 1 && !harness.closed);
  inputWrite.close();
  ZuCheck(mx.stop());
}

static ZtString<> readLine(ZiFile &file)
{
  ZtString<> line;
  for (;;) {
    ZmRef<ZiIOBuf> buf = new Zmcp::StdioBuf{};
    int length = file.read(buf->data(), buf->size, false);
    if (length <= 0) return {};
    buf->length = unsigned(length);
    line << ZuCSpan{buf->cspan()};
    if (line && line.end()[-1] == '\n') {
      line.length_(line.length() - 1);
      return line;
    }
  }
}

static void testServer()
{
  ZuTestScope(testServer);

  Pipe input;
  Pipe output;
  ZuCHECK(input.open() && output.open(), "pipe creation failed");

  ZiFile inputWrite;
  ZiFile outputRead;
  ZuCHECK(inputWrite.init(input.write, ZiFile::WriteOnly | ZiFile::GC) == Zi::OK,
    "input writer initialization failed");
  ZuCHECK(outputRead.init(output.read, ZiFile::ReadOnly | ZiFile::GC) == Zi::OK,
    "output reader initialization failed");

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "multiplexer start failed");

  ServerApp app;
  Zmcp::Server<ServerApp, EchoCatalog> server;
  auto config = Zmcp::StdioConfig{}
    .rxThread("stdioRx")
    .txThread("stdioTx")
    .input(input.read)
    .output(output.write);
  ZuCheck(server.init(&mx, ZuMv(config), &app));
  ZuCheck(server.start());

  ZuCSpan discover{
    "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"server/discover\","
    "\"params\":{}}\n"};
  ZuCHECK(inputWrite.write(discover.data(), discover.length()) == Zi::OK,
    "discover write failed: ", inputWrite.error());
  auto response = readLine(outputRead);
  ZuCheck(response.find("\"id\":1") >= 0);
  ZuCheck(response.find("\"supportedVersions\"") >= 0);

  ZuCSpan call{
    "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\","
    "\"params\":{\"name\":\"echo\",\"arguments\":{\"value\":7}}}\n"};
  ZuCHECK(inputWrite.write(call.data(), call.length()) == Zi::OK,
    "tool write failed: ", inputWrite.error());
  response = readLine(outputRead);
  ZuCheck(response.find("\"id\":2") >= 0);
  ZuCheck(response.find("\"value\":7") >= 0);
  ZuCheck(app.calls == 1);

  app.async = true;
  ZuCSpan asyncCall{
    "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\","
    "\"params\":{\"name\":\"echo\",\"arguments\":{\"value\":8}}}\n"};
  ZuCHECK(inputWrite.write(asyncCall.data(), asyncCall.length()) == Zi::OK,
    "async tool write failed: ", inputWrite.error());
  ZuCSpan cancel{
    "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\","
    "\"params\":{\"requestId\":3,\"reason\":\"superseded\"}}\n"};
  ZuCHECK(inputWrite.write(cancel.data(), cancel.length()) == Zi::OK,
    "cancellation write failed: ", inputWrite.error());
  app.cancelled_.wait();
  ZuCheck(app.cancellations == 1);
  app.complete();
  response = readLine(outputRead);
  ZuCheck(response.find("\"id\":3") >= 0);
  ZuCheck(response.find("\"value\":8") >= 0);

  inputWrite.close();
  ZuCheck(server.stop());
  ZuCheck(app.peerOpens == 1);
  ZuCheck(app.peerCloses == 1);
  ZuCheck(app.streamOpens >= 4);
  ZuCheck(app.streamCloses == app.streamOpens);
  ZuCheck(app.contextCalls == 2);
  outputRead.close();
  ZuCheck(mx.stop());
}

static void testClient()
{
  ZuTestScope(testClient);

  Pipe requests;
  Pipe responses;
  ZuCHECK(requests.open() && responses.open(), "pipe creation failed");

  ZiFile requestRead;
  ZiFile responseWrite;
  ZuCHECK(requestRead.init(
      requests.read, ZiFile::ReadOnly | ZiFile::GC) == Zi::OK,
    "request reader initialization failed");
  ZuCHECK(responseWrite.init(
      responses.write, ZiFile::WriteOnly | ZiFile::GC) == Zi::OK,
    "response writer initialization failed");

  ZiMultiplex mx{mxParams()};
  ZuCHECK(mx.start(), "multiplexer start failed");

  ClientApp app;
  Zmcp::Client<ClientApp, EchoCatalog> client;
  auto config = Zmcp::StdioConfig{}
    .rxThread("stdioRx")
    .txThread("stdioTx")
    .input(responses.read)
    .output(requests.write);
  ZuCheck(client.init(&mx, ZuMv(config), &app));
  ZuCheck(client.start());

  auto request = readLine(requestRead);
  ZuCheck(request.find("\"method\":\"server/discover\"") >= 0);
  ZuCSpan discover{
    "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{"
    "\"supportedVersions\":[\"2026-07-28\"]}}\n"};
  ZuCHECK(responseWrite.write(discover.data(), discover.length()) == Zi::OK,
    "discovery response write failed: ", responseWrite.error());
  app.ready_.wait();
  ZuCheck(app.era == Zmcp::Era::Modern);

  ZmRef<ClientCall> ping = new ClientCall{};
  ZuCheck(client.ping(ping));
  request = readLine(requestRead);
  ZuCheck(request.find("\"id\":2") >= 0);
  ZuCheck(request.find("\"method\":\"ping\"") >= 0);
  ZuCSpan pong{
    "{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{}}\n"};
  ZuCHECK(responseWrite.write(pong.data(), pong.length()) == Zi::OK,
    "ping response write failed: ", responseWrite.error());
  ping->done.wait();
  ZuCheck(ping->controls == 1 && !ping->failures);

  ZmRef<ClientCall> setLevel = new ClientCall{};
  ZuCheck(client.setLevel("debug", setLevel));
  setLevel->done.wait();
  ZuCheck(!setLevel->controls && setLevel->failures == 1);

  ZmRef<ClientCall> call = new ClientCall{};
  ZuCheck(client.template call<Echo>(EchoReq{11}, call));
  request = readLine(requestRead);
  ZuCheck(request.find("\"method\":\"tools/call\"") >= 0);
  ZuCheck(request.find("\"value\":11") >= 0);
  ZuCSpan result{
    "{\"jsonrpc\":\"2.0\",\"id\":3,\"result\":{\"content\":[],"
    "\"structuredContent\":{\"code\":200,\"data\":{\"value\":11}},"
    "\"isError\":false}}\n"};
  ZuCHECK(responseWrite.write(result.data(), result.length()) == Zi::OK,
    "tool response write failed: ", responseWrite.error());
  call->done.wait();
  ZuCheck(call->value == 11);
  ZuCheck(!call->failures);

  ZmRef<ClientCall> reported = new ClientCall{};
  ZuCheck(client.callProgressLog<Echo>(
    EchoReq{12}, Zmcp::IDString{"progress-12"},
    Zmcp::LogLevel::Info, reported));
  request = readLine(requestRead);
  ZuCheck(request.find("\"id\":4") >= 0);
  ZuCheck(request.find("\"progressToken\":\"progress-12\"") >= 0);
  ZuCheck(request.find(
    "\"io.modelcontextprotocol/logLevel\":\"info\"") >= 0);
  ZuCSpan progress{
    "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/progress\","
    "\"params\":{\"progressToken\":\"progress-12\","
    "\"progress\":0.5,\"total\":1,\"message\":\"half\"}}\n"};
  ZuCSpan logging{
    "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/message\","
    "\"params\":{\"level\":\"info\",\"logger\":\"echo\","
    "\"data\":\"working\"}}\n"};
  ZuCSpan reportedResult{
    "{\"jsonrpc\":\"2.0\",\"id\":4,\"result\":{\"content\":[],"
    "\"structuredContent\":{\"code\":200,\"data\":{\"value\":12}},"
    "\"isError\":false}}\n"};
  ZuCHECK(responseWrite.write(progress.data(), progress.length()) == Zi::OK,
    "progress write failed: ", responseWrite.error());
  ZuCHECK(responseWrite.write(logging.data(), logging.length()) == Zi::OK,
    "logging write failed: ", responseWrite.error());
  ZuCHECK(responseWrite.write(
      reportedResult.data(), reportedResult.length()) == Zi::OK,
    "reported response write failed: ", responseWrite.error());
  app.progressed_.wait();
  app.logged_.wait();
  reported->done.wait();
  ZuCheck(app.token == Zmcp::IDString{"progress-12"} &&
    app.value == .5 && app.total == 1 && app.message == "half");
  ZuCheck(app.level == "info" && app.logger == "echo" &&
    app.data == "working");
  ZuCheck(!reported->failures && reported->value == 12);

  ZmRef<ClientCall> cancelled = new ClientCall{};
  ZuCheck(client.call<Echo>(EchoReq{13}, cancelled));
  request = readLine(requestRead);
  cancelled->started_.wait();
  ZuCheck(request.find("\"id\":5") >= 0 &&
    request.find("\"value\":13") >= 0);
  ZuCheck(client.cancel(cancelled->id, "superseded"));
  request = readLine(requestRead);
  ZuCheck(request.find("\"method\":\"notifications/cancelled\"") >= 0);
  ZuCheck(request.find("\"requestId\":5") >= 0);
  app.cancelled_.wait();
  ZuCheck(app.cancelOK && app.cancelledID == cancelled->id);
  ZuCSpan cancelledResult{
    "{\"jsonrpc\":\"2.0\",\"id\":5,\"result\":{\"content\":[],"
    "\"structuredContent\":{\"code\":200,\"data\":{\"value\":13}},"
    "\"isError\":false}}\n"};
  ZuCHECK(responseWrite.write(
      cancelledResult.data(), cancelledResult.length()) == Zi::OK,
    "cancelled response write failed: ", responseWrite.error());
  cancelled->done.wait();
  ZuCheck(!cancelled->failures && cancelled->value == 13);

  responseWrite.close();
  app.closed_.wait();
  ZuCheck(app.closes == 1);
  ZuCheck(!app.failures);
  requestRead.close();
  ZuCheck(client.stop());
  ZuCheck(mx.stop());
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testFramer);
  ZuTestCall(testPipe);
  ZuTestCall(testFailure);
  ZuTestCall(testServer);
  ZuTestCall(testClient);
  return 0;
}
