//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef _WIN32
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <zlib/ZuBox.hh>
#include <zlib/ZuObject.hh>
#include <zlib/ZuRef.hh>
#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmHeap.hh>

#include <zlib/ZiMultiplex.hh>

#include <zlib/ZmcpClient.hh>
#include <zlib/ZmcpServer.hh>

#ifndef _WIN32
#include "../../zquic/test/ZquicInteropTest.hh"
#endif

#include "ZmcpITestPorts.hh"

using namespace ZuTestUtil;

struct EchoReq { int value = 0; };
struct EchoResult { int value = 0; };
ZfStruct((EchoReq, JSON),
  (((value), (Ctor<0>, Required)), (Int32)));
ZfStruct((EchoResult, JSON),
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
struct EchoStream : public Zmcp::Request {
  using Object = EchoReq;
  using OperationID = ZuStringT<"echoStreamValue">;
  using ToolID = ZuStringT<"echo_stream">;
  using Responses = ZuTypeList<EchoOK>;
  enum { ResponseBody = Zmcp::BodyPolicy::SSE };
};
using Catalog = ZuTypeList<Echo, EchoStream>;

template <typename Heap>
struct TransportContext_ : public Heap, public ZuObject {
  TransportContext_(uintptr_t id_) : id{id_} { }
  uintptr_t id;
};
using TransportContextHeap =
  ZmHeap<"ZmcpITest.Transport", TransportContext_<ZuVoid>>;
struct TransportContext : public TransportContext_<TransportContextHeap> {
  using Base = TransportContext_<TransportContextHeap>;
  using Base::Base;
};

template <typename Heap>
struct SessionContext_ : public Heap, public ZuObject { };
using SessionContextHeap =
  ZmHeap<"ZmcpITest.Session", SessionContext_<ZuVoid>>;
struct SessionContext : public SessionContext_<SessionContextHeap> { };

template <typename Heap>
struct StreamContext_ : public Heap, public ZuObject { };
using StreamContextHeap =
  ZmHeap<"ZmcpITest.Stream", StreamContext_<ZuVoid>>;
struct StreamContext : public StreamContext_<StreamContextHeap> { };

struct App {
  using Headers = ZhttpHeaders("authorization");
  using Authorization = ZuStringT<"authorization">;

  ZmSemaphore listening_;
  ZmSemaphore cancelled_;
  ZmSemaphore called_;
  ZmSemaphore sessionClosed_;
  ZmFn<bool()> complete_;
  ZmFn<bool()> complete1_;
  ZmFn<bool()> complete2_;
  unsigned failures = 0;
  unsigned cancellations = 0;
  unsigned transportOpens = 0;
  unsigned transportCloses = 0;
  unsigned sessionOpens = 0;
  unsigned sessionCloses = 0;
  unsigned streamOpens = 0;
  unsigned streamCloses = 0;
  unsigned modernContexts = 0;
  unsigned legacyContexts = 0;
  unsigned authorizedCalls = 0;

  ZuRef<TransportContext> open(
      Zmcp::TransportTag, Zhttp::Session session) {
    ++transportOpens;
    return new TransportContext{session.id};
  }
  ZuRef<SessionContext> open(
      Zmcp::SessionTag, bool stdio, ZuCSpan id) {
    if (!stdio && id) ++sessionOpens;
    return new SessionContext{};
  }
  ZuRef<StreamContext> open(
      Zmcp::StreamTag, const Zmcp::Context &context) {
    if (context.transport<TransportContext>()) ++streamOpens;
    return new StreamContext{};
  }
  void close(Zmcp::TransportTag, TransportContext *) {
    ++transportCloses;
  }
  void close(Zmcp::SessionTag, SessionContext *) {
    ++sessionCloses;
    sessionClosed_.post();
  }
  void close(Zmcp::StreamTag, StreamContext *) { ++streamCloses; }

  bool origin(ZuCSpan) const { return true; }
  void listening(int, unsigned) { listening_.post(); }
  void listenFailed(int, bool) { ++failures; listening_.post(); }
  void connected(int) { }
  void disconnected(int) { }

  template <typename Req, typename Token>
  void tool(
      Req *, const EchoReq &request, const auto &headers,
      const Zmcp::Context &context, Token token) {
    auto authorization = headers.template get<Authorization>();
    if (authorization.count == 1 && authorization.value == "Bearer test")
      ++authorizedCalls;
    if (context.transport<TransportContext>() &&
	context.stream<StreamContext>()) {
      if (context.session<SessionContext>()) ++legacyContexts;
      else ++modernContexts;
    }
    if (request.value == 65) {
      (void)token->log("debug", "hidden", "echo");
      (void)token->log("warning", "visible", "echo");
      (*token)(Zmcp::ToolReply<EchoOK>{EchoResult{request.value}});
      return;
    }
    ZmFn<bool()> complete;
    switch (request.value) {
      case 52:
      case 53:
      case 63:
      case 66:
      case 73:
      case 74:
	complete = [token = ZuMv(token), request]() mutable {
	  return (*token)(Zmcp::ToolReply<EchoOK>{EchoResult{request.value}});
	};
	break;
      default:
	(*token)(Zmcp::ToolReply<EchoOK>{EchoResult{request.value}});
	return;
    }
    switch (request.value) {
      case 73: complete1_ = ZuMv(complete); break;
      case 74: complete2_ = ZuMv(complete); break;
      default: complete_ = ZuMv(complete); break;
    }
    called_.post();
  }

  template <typename Req, typename Token>
  void cancelled(Req *, Token *, ZuCSpan) {
    ++cancellations;
    cancelled_.post();
  }
};

struct ClientApp {
  using Headers = ZhttpHeaders("authorization");

  template <typename Key, typename L>
  void header(L &&l) const {
    if constexpr (Key{}() == "authorization") l("Bearer test");
  }

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
  void tools(const ZfJSON::AnyNode *catalog) {
    if (Zmcp::member(catalog, "tools")) ++catalogs;
    tools_.post();
  }
  void toolsFailed() {
    ++toolFailures;
    tools_.post();
  }
  void cancelled(const Zmcp::ID &, bool ok) {
    cancelOK = ok;
    cancelled_.post();
  }
  void terminated(bool ok) {
    terminateOK = ok;
    terminated_.post();
  }
  void logging(
      ZuCSpan level_, const ZfJSON::AnyNode *data_, ZuCSpan logger_) {
    level = level_;
    logger = logger_;
    if (data_ && data_->template has<ZfJSON::AnyNode::String>())
      data = data_->template data<ZfJSON::AnyNode::String>();
    ++logs;
    logged_.post();
  }

  ZmSemaphore ready_;
  ZmSemaphore closed_;
  ZmSemaphore tools_;
  ZmSemaphore cancelled_;
  ZmSemaphore terminated_;
  ZmSemaphore logged_;
  ZtString<> level;
  ZtString<> logger;
  ZtString<> data;
  int era = Zmcp::Era::Unknown;
  unsigned closes = 0;
  unsigned failures = 0;
  unsigned catalogs = 0;
  unsigned toolFailures = 0;
  unsigned logs = 0;
  bool cancelOK = false;
  bool terminateOK = false;
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
  ZmHeap<"ZmcpITest.ClientCall", ClientCall_<ZuVoid>>;
ZuDerive(ClientCall, (ClientCall_<ClientCallHeap>));

#ifndef _WIN32
static int connectLoopback(unsigned port)
{
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(port);
  if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

static bool sendAll(int fd, ZuCSpan data)
{
  while (data) {
    ssize_t n = ::send(fd, data.data(), data.length(), 0);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return false;
    data.offset(unsigned(n));
  }
  return true;
}

static bool receiveAll(int fd, ZtString<> &out)
{
  ZmRef<ZiIOBuf> buf = new Zmcp::HTTPBodyBuf{};
  if (!buf->alloc(ZiIOBuf_DefltSize)) return false;
  for (;;) {
    ssize_t n = ::recv(fd, buf->data(), buf->size, 0);
    if (n < 0 && errno == EINTR) continue;
    if (n < 0) return false;
    if (!n) return true;
    buf->length = unsigned(n);
    out << ZuCSpan{buf->cspan()};
    if (out.length() >
	Zmcp::Default::MaxJSONBytes + Zmcp::Default::MaxLineBytes)
      return false;
    int64_t headerEnd = out.find("\r\n\r\n");
    if (headerEnd < 0) continue;
    ZuCSpan headers{out.data(), unsigned(headerEnd)};
    int64_t statusEnd = headers.find("\r\n");
    ZuCSpan status = statusEnd >= 0 ?
	ZuCSpan{headers.data(), unsigned(statusEnd)} : headers;
    if (status.find(" 202 ") >= 0 || status.find(" 204 ") >= 0)
      return true;
    int64_t lengthOff = headers.find("content-length:");
    if (lengthOff < 0) lengthOff = headers.find("Content-Length:");
    if (lengthOff < 0) {
      int64_t chunked = headers.find("transfer-encoding: chunked");
      if (chunked < 0)
	chunked = headers.find("Transfer-Encoding: chunked");
      if (chunked >= 0 && out.find("\r\n0\r\n\r\n") >= 0)
	return true;
      continue;
    }
    unsigned begin = unsigned(lengthOff) + sizeof("content-length:") - 1;
    while (begin < headers.length() && headers[begin] == ' ') ++begin;
    unsigned end = begin;
    while (end < headers.length() &&
	headers[end] >= '0' && headers[end] <= '9') ++end;
    if (end == begin) return false;
    unsigned length = ZuBox<unsigned>{
      ZuCSpan{headers.data() + begin, end - begin}};
    unsigned bodyOff = unsigned(headerEnd) + 4;
    if (out.length() - bodyOff >= length) return true;
  }
}

static ZtString<> headerValue(const ZtString<> &response, ZuCSpan name)
{
  int64_t offset = response.find(name);
  if (offset < 0) return {};
  unsigned begin = unsigned(offset) + name.length();
  while (begin < response.length() && response[begin] == ' ') ++begin;
  ZuCSpan tail{response};
  tail.offset(begin);
  int64_t end = tail.find("\r\n");
  if (end < 0) return {};
  return ZuCSpan{response.data() + begin, unsigned(end)};
}
#endif

struct LegacyApp {
  using ResBuilderQ = Zmcp::HTTPBuilderQ<Catalog>;

  static constexpr ZuCSpan SessionID{"legacy-session"};

  ZmSemaphore listening_;
  Zmcp::Peer<Catalog> peer;
  Zmcp::Limits limits;
  unsigned connections = 0;
  unsigned failures = 0;
  unsigned initializes = 0;
  unsigned toolCalls = 0;
  bool rejectSession = false;
  bool dropTransport = false;
  bool sessionless = false;

  class Parser : public Zmcp::HTTPParser<Parser> {
    using Base = Zmcp::HTTPParser<Parser>;

  public:
    void init(LegacyApp &app) { m_app = &app; }
    ZuCSpan endpoint() const { return "/mcp"; }
    const Zmcp::Limits &limits() const { return m_app->limits; }
    bool origin(ZuCSpan) const { return true; }

    template <typename Link>
    void receiveHTTP(
        Link *link, ZmRef<ZiIOBuf> body, const Zmcp::HTTPMeta &meta) {
      m_app->receive(link, ZuMv(body), meta);
    }

    template <typename Link>
    void deleteHTTP(Link *link, const Zmcp::HTTPMeta &) {
      m_app->accepted(link);
    }

    template <typename Link>
    void originHTTP(Link *link) { m_app->accepted(link); }

    template <typename Link>
    void corruptHTTP(Link *link) { link->disconnect(); }

    void reset() {
      Base::reset();
      m_app = nullptr;
    }

  private:
    LegacyApp *m_app = nullptr;
  };

  template <typename Link>
  void receive(
      Link *link, ZmRef<ZiIOBuf> body, const Zmcp::HTTPMeta &meta) {
    if (!link || !body) return;
    if (dropTransport && meta.sessionID) {
      dropTransport = false;
      link->disconnect();
      return;
    }
    if (rejectSession && meta.sessionID) {
      rejectSession = false;
      ZmRef<typename ResBuilderQ::Node> response =
	new typename ResBuilderQ::Node{};
      response->data().missing();
      link->send(ZuMv(response));
      return;
    }
    auto parsed = Zmcp::parse<Catalog>(
      ZuSpan<char>{body->span()}, limits.maxJSONBytes);
    if (!parsed) {
      link->disconnect();
      return;
    }
    bool sent = false;
    auto emit = [this, link, &sent](auto message) {
      ZmRef<typename ResBuilderQ::Node> response =
	new typename ResBuilderQ::Node{};
      auto fixed = response->data().fixed(ZuMv(message));
      if (!sessionless) fixed->sessionID(SessionID);
      link->send(ZuMv(response));
      sent = true;
    };
    if (parsed.envelope.method() == "server/discover")
      emit(Zmcp::ErrorMessage{parsed.envelope.id(),
	"Method not found", Zmcp::ErrorCode::MethodNotFound});
    else {
      if (parsed.envelope.method() == "initialize") ++initializes;
      auto tool = [this](auto *, const EchoReq &request, auto complete) {
	++toolCalls;
	complete(Zmcp::ToolReply<EchoOK>{EchoResult{request.value}});
      };
      if (!peer.dispatch(parsed.envelope, emit, tool)) {
	link->disconnect();
	return;
      }
    }
    if (!sent) accepted(link);
  }

  template <typename Link>
  void accepted(Link *link) {
    ZmRef<typename ResBuilderQ::Node> response =
      new typename ResBuilderQ::Node{};
    auto fixed = response->data().accepted();
    if (!sessionless) fixed->sessionID(SessionID);
    link->send(ZuMv(response));
  }

  void listening(int, unsigned) { listening_.post(); }
  void listenFailed(int, bool) { ++failures; listening_.post(); }
  void connected(int) { ++connections; }
  void disconnected(int) { }
  void connected(Zhttp::Session) { }
  void disconnected(Zhttp::Session) { }

};

static void httpTest()
{
  ZuTestScope(http);
#ifdef _WIN32
  ZuCheck(true);
#else
  unsigned port = Zquic::Test::loopbackPort(ZmcpITestPort::HTTP);
  ZuCheck(port);
  if (!port) return;

  auto params = ZiMxParams()
    .scheduler([](auto &scheduler) {
      scheduler.nThreads(2)
	.thread(1, [](auto &thread) { thread.isolated(1); })
	.thread(2, [](auto &thread) { thread.isolated(1); });
    }).rxThread(1).txThread(2);
  ZiMultiplex mx{ZuMv(params)};
  ZuCheck(mx.start());

  App app;
  Zmcp::Server<App, Catalog> server;
  Zmcp::ServerConfig config;
  config.localIP(ZiIP{"127.0.0.1"}).port(port).tcp();
  config.absentOrigin(true).legacyLifetime(1);
  bool initialized = server.init(
    Zhttp::HubConfig{&mx, "1", "2"}, ZuMv(config), &app);
  ZuCheck(initialized);
  bool started = initialized && server.start();
  ZuCheck(started);
  if (started) app.listening_.wait();

  int fd = started ? connectLoopback(port) : -1;
  ZuCheck(fd >= 0);
  ZtString<> body;
  body << "{\"jsonrpc\":\"2.0\",\"id\":42,\"method\":\"tools/call\","
    "\"params\":{\"name\":\"echo\",\"arguments\":{\"value\":42}}}";
  ZtString<> request;
  request << "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\n"
    "Content-Type: application/json\r\nAccept: application/json\r\n"
    "Connection: close\r\nContent-Length: " << body.length()
    << "\r\n\r\n" << body;
  ZuCheck(fd >= 0 && sendAll(fd, request));
  ZtString<> response;
  ZuCheck(fd >= 0 && receiveAll(fd, response));
  if (fd >= 0) ::close(fd);
  ZuCheck(response.find("HTTP/1.1 200") >= 0);
  ZuCheck(response.find(
    "{\"jsonrpc\":\"2.0\",\"id\":42,\"result\":{"
    "\"_meta\":{\"io.modelcontextprotocol/serverInfo\":{"
      "\"name\":\"zmcp\",\"version\":\"" Z_VERNAME "\"}},"
    "\"resultType\":\"complete\",\"content\":[],"
    "\"structuredContent\":{\"code\":200,\"data\":{\"value\":42}},"
    "\"isError\":false}}") >= 0);

  fd = started ? connectLoopback(port) : -1;
  ZuCheck(fd >= 0);
  body.length_(0);
  body << "{\"jsonrpc\":\"2.0\",\"id\":44,\"method\":\"tools/call\","
    "\"params\":{\"name\":\"echo\",\"arguments\":{\"value\":44}}}";
  request.length_(0);
  request << "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\n"
    "Mcp-Method: ping\r\nMcp-Name: missing\r\n"
    "Transfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
    << ZuBoxed(body.length()).hex<false>() << "\r\n" << body
    << "\r\n0\r\n\r\n";
  ZuCheck(fd >= 0 && sendAll(fd, request));
  response.length_(0);
  ZuCheck(fd >= 0 && receiveAll(fd, response));
  if (fd >= 0) ::close(fd);
  ZuCheck(response.find("HTTP/1.1 200") >= 0);
  ZuCheck(response.find("\"id\":44") >= 0);
  ZuCheck(response.find("\"value\":44") >= 0);

  fd = started ? connectLoopback(port) : -1;
  ZuCheck(fd >= 0);
  body.length_(0);
  body << "{\"jsonrpc\":\"2.0\",\"id\":43,\"method\":\"tools/call\","
    "\"params\":{\"name\":\"echo_stream\","
    "\"arguments\":{\"value\":43}}}";
  request.length_(0);
  request << "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\n"
    "Content-Type: application/json\r\nAccept: text/event-stream\r\n"
    "Connection: close\r\nContent-Length: " << body.length()
    << "\r\n\r\n" << body;
  ZuCheck(fd >= 0 && sendAll(fd, request));
  response.length_(0);
  ZuCheck(fd >= 0 && receiveAll(fd, response));
  if (fd >= 0) ::close(fd);
  ZuCheck(response.find("HTTP/1.1 200") >= 0);
  ZuCheck(response.find("text/event-stream") >= 0);
  ZuCheck(response.find("data: {\"jsonrpc\":\"2.0\",\"id\":43,"
    "\"result\":{\"_meta\":{\"io.modelcontextprotocol/serverInfo\":{"
      "\"name\":\"zmcp\",\"version\":\"" Z_VERNAME "\"}},"
    "\"resultType\":\"complete\",\"content\":[],"
    "\"structuredContent\":{\"code\":200,"
    "\"data\":{\"value\":43}},\"isError\":false}}") >= 0);

  fd = started ? connectLoopback(port) : -1;
  ZuCheck(fd >= 0);
  body = "{\"jsonrpc\":\"2.0\",\"id\":50,\"method\":\"initialize\","
    "\"params\":{\"protocolVersion\":\"2025-11-25\"}}";
  request.length_(0);
  request << "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\n"
    "Connection: close\r\nContent-Length: " << body.length()
    << "\r\n\r\n" << body;
  ZuCheck(fd >= 0 && sendAll(fd, request));
  response.length_(0);
  ZuCheck(fd >= 0 && receiveAll(fd, response));
  if (fd >= 0) ::close(fd);
  auto sessionID = headerValue(response, "mcp-session-id:");
  if (!sessionID)
    sessionID = headerValue(response, "Mcp-Session-Id:");
  ZuCheck(sessionID.length() == Zmcp::Default::SessionIDBytes * 2);
  ZuCheck(response.find("\"protocolVersion\":\"2025-11-25\"") >= 0);

  fd = started ? connectLoopback(port) : -1;
  ZuCheck(fd >= 0);
  body = "{\"jsonrpc\":\"2.0\","
    "\"method\":\"notifications/initialized\"}";
  request.length_(0);
  request << "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\n"
    "MCP-Session-Id: " << sessionID << "\r\nConnection: close\r\n"
    "Content-Length: " << body.length() << "\r\n\r\n" << body;
  ZuCheck(fd >= 0 && sendAll(fd, request));
  response.length_(0);
  ZuCheck(fd >= 0 && receiveAll(fd, response));
  if (fd >= 0) ::close(fd);
  ZuCheck(response.find("HTTP/1.1 202") >= 0);

  fd = started ? connectLoopback(port) : -1;
  ZuCheck(fd >= 0);
  body = "{\"jsonrpc\":\"2.0\",\"id\":51,\"method\":\"tools/call\","
    "\"params\":{\"name\":\"echo\",\"arguments\":{\"value\":51}}}";
  request.length_(0);
  request << "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\n"
    "MCP-Session-Id: " << sessionID << "\r\nConnection: close\r\n"
    "Content-Length: " << body.length() << "\r\n\r\n" << body;
  ZuCheck(fd >= 0 && sendAll(fd, request));
  response.length_(0);
  ZuCheck(fd >= 0 && receiveAll(fd, response));
  if (fd >= 0) ::close(fd);
  ZuCheck(response.find("\"id\":51") >= 0);
  ZuCheck(response.find("\"value\":51") >= 0);

  int callFD = started ? connectLoopback(port) : -1;
  ZuCheck(callFD >= 0);
  body = "{\"jsonrpc\":\"2.0\",\"id\":52,\"method\":\"tools/call\","
    "\"params\":{\"name\":\"echo\",\"arguments\":{\"value\":52}}}";
  request.length_(0);
  request << "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\n"
    "MCP-Session-Id: " << sessionID << "\r\nConnection: close\r\n"
    "Content-Length: " << body.length() << "\r\n\r\n" << body;
  ZuCheck(callFD >= 0 && sendAll(callFD, request));
  app.called_.wait();

  fd = started ? connectLoopback(port) : -1;
  ZuCheck(fd >= 0);
  body = "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\","
    "\"params\":{\"requestId\":52,\"reason\":\"superseded\"}}";
  request.length_(0);
  request << "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\n"
    "MCP-Session-Id: " << sessionID << "\r\nConnection: close\r\n"
    "Content-Length: " << body.length() << "\r\n\r\n" << body;
  ZuCheck(fd >= 0 && sendAll(fd, request));
  response.length_(0);
  ZuCheck(fd >= 0 && receiveAll(fd, response));
  if (fd >= 0) ::close(fd);
  ZuCheck(response.find("HTTP/1.1 202") >= 0);
  app.cancelled_.wait();
  ZuCheck(app.cancellations == 1);
  ZuCheck(bool(app.complete_));
  ZuCheck(app.complete_ && !app.complete_());
  response.length_(0);
  ZuCheck(callFD >= 0 && receiveAll(callFD, response));
  if (callFD >= 0) ::close(callFD);
  ZuCheck(response.find("\"id\":52") < 0);

  callFD = started ? connectLoopback(port) : -1;
  ZuCheck(callFD >= 0);
  body = "{\"jsonrpc\":\"2.0\",\"id\":53,\"method\":\"tools/call\","
    "\"params\":{\"name\":\"echo\",\"arguments\":{\"value\":53}}}";
  request.length_(0);
  request << "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\n"
    "MCP-Session-Id: " << sessionID << "\r\nConnection: close\r\n"
    "Content-Length: " << body.length() << "\r\n\r\n" << body;
  ZuCheck(callFD >= 0 && sendAll(callFD, request));
  app.called_.wait();
  app.sessionClosed_.wait();
  ZuCheck(app.complete_ && !app.complete_());
  response.length_(0);
  ZuCheck(callFD >= 0 && receiveAll(callFD, response));
  if (callFD >= 0) ::close(callFD);
  ZuCheck(response.find("\"id\":53") < 0);

  fd = started ? connectLoopback(port) : -1;
  ZuCheck(fd >= 0);
  request.length_(0);
  request << "DELETE /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\n"
    "MCP-Session-Id: " << sessionID
    << "\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
  ZuCheck(fd >= 0 && sendAll(fd, request));
  response.length_(0);
  ZuCheck(fd >= 0 && receiveAll(fd, response));
  if (fd >= 0) ::close(fd);
  ZuCheck(response.find("HTTP/1.1 202") >= 0);
  ZuCheck(!app.failures);

  ClientApp clientApp;
  Zmcp::HTTPClient<ClientApp, Catalog> client;
  Zmcp::ClientConfig clientConfig;
  clientConfig.endpoint("/mcp");
  clientConfig.secure(false).tcp(true).tls(false).quic(false)
    .protocol(Zhttp::ProtoPolicy::DisableH3)
    .h2Policy(Zhttp::H2Policy::Disable)
    .links(1).concurrency(4).linkMax(1);
  clientConfig.requestTimeout(1);
  unsigned transportOpens = app.transportOpens;
  bool clientInited = client.init(
    Zhttp::HubConfig{&mx, "1", "2"},
    Zhttp::Destination{"127.0.0.1", uint16_t(port)},
    ZuMv(clientConfig), &clientApp);
  ZuCheck(clientInited);
  bool clientStarted = clientInited && client.start();
  ZuCheck(clientStarted);
  if (clientStarted) clientApp.ready_.wait();
  ZuCheck(clientApp.era == Zmcp::Era::Modern);

  ZmRef<ClientCall> ping = new ClientCall{};
  ZuCheck(client.ping(ping));
  ping->done.wait();
  ZuCheck(ping->controls == 1 && !ping->failures);

  ZmRef<ClientCall> setLevel = new ClientCall{};
  ZuCheck(client.setLevel("debug", setLevel));
  setLevel->done.wait();
  ZuCheck(!setLevel->controls && setLevel->failures == 1);

  ZuCheck(client.tools());
  clientApp.tools_.wait();
  ZuCheck(clientApp.catalogs == 1 && !clientApp.toolFailures);
  ZuCheck(client.tools());
  clientApp.tools_.wait();
  ZuCheck(clientApp.catalogs == 2 && !clientApp.toolFailures);
  ZuCheck(client.discardCatalog());
  ZuCheck(client.tools());
  clientApp.tools_.wait();
  ZuCheck(clientApp.catalogs == 3 && !clientApp.toolFailures);

  ZmRef<ClientCall> fixedCall = new ClientCall{};
  ZuCheck(client.call<Echo>(EchoReq{61}, fixedCall));
  fixedCall->done.wait();
  ZuCheck(!fixedCall->failures && fixedCall->value == 61);

  ZmRef<ClientCall> streamCall = new ClientCall{};
  ZuCheck(client.call<EchoStream>(EchoReq{62}, streamCall));
  streamCall->done.wait();
  ZuCheck(!streamCall->failures && streamCall->value == 62);

  ZmRef<ClientCall> loggedCall = new ClientCall{};
  ZuCheck(client.callLog<EchoStream>(EchoReq{65}, "warning", loggedCall));
  clientApp.logged_.wait();
  loggedCall->done.wait();
  ZuCheck(!loggedCall->failures && loggedCall->value == 65);
  ZuCheck(clientApp.logs == 1 && clientApp.level == "warning" &&
    clientApp.logger == "echo" && clientApp.data == "visible");

  ZmRef<ClientCall> timeoutCall = new ClientCall{};
  ZuCheck(client.call<Echo>(EchoReq{66}, timeoutCall));
  app.called_.wait();
  timeoutCall->done.wait();
  app.cancelled_.wait();
  ZuCheck(timeoutCall->failures == 1 && app.cancellations == 2);
  ZuCheck(bool(app.complete_) && !app.complete_());

  ZmRef<ClientCall> cancelCall = new ClientCall{};
  ZuCheck(client.call<Echo>(EchoReq{63}, cancelCall));
  cancelCall->started_.wait();
  app.called_.wait();
  ZuCheck(client.cancel(cancelCall->id, "no longer needed"));
  clientApp.cancelled_.wait();
  app.cancelled_.wait();
  cancelCall->done.wait();
  ZuCheck(clientApp.cancelOK && cancelCall->failures == 1);
  ZuCheck(app.cancellations == 3);
  ZuCheck(bool(app.complete_) && !app.complete_());
  ZuCheck(app.authorizedCalls == 5);

  if (clientInited) ZuCheck(client.stop());
  ZuCheck(clientApp.closes == 1 && !clientApp.failures);
  client.final();
  ZuCheck(app.transportOpens == transportOpens + 2);

  unsigned legacyPort =
    Zquic::Test::loopbackPort(ZmcpITestPort::Legacy);
  ZuCheck(legacyPort);
  LegacyApp legacyApp;
  Zhttp::Server<LegacyApp> legacyServer;
  Zhttp::ServerConfig legacyConfig;
  legacyConfig.localIP(ZiIP{"127.0.0.1"}).port(legacyPort).tcp();
  bool legacyInited = legacyServer.init(
    Zhttp::HubConfig{&mx, "1", "2"}, ZuMv(legacyConfig), &legacyApp);
  ZuCheck(legacyInited);
  bool legacyStarted = legacyInited && legacyServer.start();
  ZuCheck(legacyStarted);
  if (legacyStarted) legacyApp.listening_.wait();

  ClientApp legacyClientApp;
  Zmcp::HTTPClient<ClientApp, Catalog> legacyClient;
  Zmcp::ClientConfig legacyClientConfig;
  legacyClientConfig.endpoint("/mcp");
  legacyClientConfig.secure(false).tcp(true).tls(false).quic(false)
    .protocol(Zhttp::ProtoPolicy::DisableH3)
    .h2Policy(Zhttp::H2Policy::Disable)
    .links(1).concurrency(1).linkMax(1);
  bool legacyClientInited = legacyClient.init(
    Zhttp::HubConfig{&mx, "1", "2"},
    Zhttp::Destination{"127.0.0.1", uint16_t(legacyPort)},
    ZuMv(legacyClientConfig), &legacyClientApp);
  ZuCheck(legacyClientInited);
  bool legacyClientStarted = legacyClientInited && legacyClient.start();
  ZuCheck(legacyClientStarted);
  if (legacyClientStarted) legacyClientApp.ready_.wait();
  ZuCheck(legacyClientApp.era == Zmcp::Era::Legacy);
  ZmRef<ClientCall> legacyPing = new ClientCall{};
  ZuCheck(legacyClient.ping(legacyPing));
  legacyPing->done.wait();
  ZuCheck(legacyPing->controls == 1 && !legacyPing->failures);
  ZmRef<ClientCall> legacyLevel = new ClientCall{};
  ZuCheck(legacyClient.setLevel("warning", legacyLevel));
  legacyLevel->done.wait();
  ZuCheck(legacyLevel->controls == 1 && !legacyLevel->failures);
  ZmRef<ClientCall> legacyCall = new ClientCall{};
  ZmRef<ClientCall> legacyCall2 = new ClientCall{};
  ZmRef<ClientCall> legacyCall3 = new ClientCall{};
  ZuCheck(legacyClient.call<Echo>(EchoReq{64}, legacyCall));
  ZuCheck(legacyClient.call<Echo>(EchoReq{70}, legacyCall2));
  ZuCheck(legacyClient.call<Echo>(EchoReq{71}, legacyCall3));
  legacyCall->done.wait();
  legacyCall2->done.wait();
  legacyCall3->done.wait();
  ZuCheck(!legacyCall->failures && legacyCall->value == 64);
  ZuCheck(!legacyCall2->failures && legacyCall2->value == 70);
  ZuCheck(!legacyCall3->failures && legacyCall3->value == 71);
  ZuCheck(legacyApp.initializes == 1);

  legacyApp.dropTransport = true;
  ZmRef<ClientCall> transportFailed = new ClientCall{};
  ZuCheck(legacyClient.call<Echo>(EchoReq{72}, transportFailed));
  transportFailed->done.wait();
  ZuCheck(transportFailed->failures == 1);
  ZmRef<ClientCall> preservedCall = new ClientCall{};
  ZuCheck(legacyClient.call<Echo>(EchoReq{73}, preservedCall));
  preservedCall->done.wait();
  ZuCheck(!preservedCall->failures && preservedCall->value == 73);
  ZuCheck(legacyApp.initializes == 1);

  legacyApp.rejectSession = true;
  ZmRef<ClientCall> expiredCall = new ClientCall{};
  ZuCheck(legacyClient.call<Echo>(EchoReq{68}, expiredCall));
  expiredCall->done.wait();
  ZuCheck(expiredCall->failures == 1);
  legacyClientApp.ready_.wait();
  ZuCheck(legacyClientApp.era == Zmcp::Era::Legacy);
  ZmRef<ClientCall> recoveredCall = new ClientCall{};
  ZuCheck(legacyClient.call<Echo>(EchoReq{69}, recoveredCall));
  recoveredCall->done.wait();
  ZuCheck(!recoveredCall->failures && recoveredCall->value == 69);
  ZuCheck(legacyApp.initializes == 2);
  ZuCheck(legacyApp.toolCalls == 5);

  ZuCheck(legacyClient.terminate());
  legacyClientApp.terminated_.wait();
  ZuCheck(legacyClientApp.terminateOK);
  ZuCheck(!legacyClient.call<Echo>(EchoReq{67}, legacyCall));
  if (legacyClientInited) ZuCheck(legacyClient.stop());
  legacyClient.final();
  ZuCheck(legacyClientApp.closes == 1 && !legacyClientApp.failures);

  unsigned legacyConnections = legacyApp.connections;
  legacyApp.sessionless = true;
  ClientApp sessionlessApp;
  Zmcp::HTTPClient<ClientApp, Catalog> sessionlessClient;
  Zmcp::ClientConfig sessionlessConfig;
  sessionlessConfig.endpoint("/mcp").legacySessions(false);
  sessionlessConfig.secure(false).tcp(true).tls(false).quic(false)
    .protocol(Zhttp::ProtoPolicy::DisableH3)
    .h2Policy(Zhttp::H2Policy::Disable)
    .links(1).concurrency(1).linkMax(1);
  bool sessionlessInited = sessionlessClient.init(
    Zhttp::HubConfig{&mx, "1", "2"},
    Zhttp::Destination{"127.0.0.1", uint16_t(legacyPort)},
    ZuMv(sessionlessConfig), &sessionlessApp);
  ZuCheck(sessionlessInited);
  bool sessionlessStarted =
    sessionlessInited && sessionlessClient.start();
  ZuCheck(sessionlessStarted);
  if (sessionlessStarted) sessionlessApp.ready_.wait();
  ZuCheck(sessionlessApp.era == Zmcp::Era::Legacy);
  ZmRef<ClientCall> sessionlessCall = new ClientCall{};
  ZuCheck(sessionlessClient.call<Echo>(EchoReq{75}, sessionlessCall));
  sessionlessCall->done.wait();
  ZuCheck(!sessionlessCall->failures && sessionlessCall->value == 75);
  ZuCheck(sessionlessClient.terminate());
  sessionlessApp.terminated_.wait();
  ZuCheck(sessionlessApp.terminateOK);
  if (sessionlessInited) ZuCheck(sessionlessClient.stop());
  sessionlessClient.final();
  ZuCheck(sessionlessApp.closes == 1 && !sessionlessApp.failures);
  ZuCheck(legacyApp.connections > legacyConnections && !legacyApp.failures);
  if (legacyStarted) ZuCheck(legacyServer.stop());
  legacyServer.final();

  unsigned statelessPort =
    Zquic::Test::loopbackPort(ZmcpITestPort::Stateless);
  ZuCheck(statelessPort);
  App statelessApp;
  Zmcp::Server<App, Catalog> statelessServer;
  Zmcp::ServerConfig statelessConfig;
  statelessConfig.localIP(ZiIP{"127.0.0.1"}).port(statelessPort).tcp();
  statelessConfig.absentOrigin(true).legacySessions(false);
  bool statelessInited = statelessServer.init(
    Zhttp::HubConfig{&mx, "1", "2"},
    ZuMv(statelessConfig), &statelessApp);
  ZuCheck(statelessInited);
  bool statelessStarted = statelessInited && statelessServer.start();
  ZuCheck(statelessStarted);
  if (statelessStarted) statelessApp.listening_.wait();

  fd = statelessStarted ? connectLoopback(statelessPort) : -1;
  ZuCheck(fd >= 0);
  body = "{\"jsonrpc\":\"2.0\",\"id\":80,\"method\":\"initialize\","
    "\"params\":{\"protocolVersion\":\"2025-11-25\"}}";
  request.length_(0);
  request << "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\n"
    "MCP-Protocol-Version: 2025-11-25\r\nConnection: close\r\n"
    "Content-Length: " << body.length() << "\r\n\r\n" << body;
  ZuCheck(fd >= 0 && sendAll(fd, request));
  response.length_(0);
  ZuCheck(fd >= 0 && receiveAll(fd, response));
  if (fd >= 0) ::close(fd);
  ZuCheck(response.find("\"protocolVersion\":\"2025-11-25\"") >= 0);
  ZuCheck(!headerValue(response, "mcp-session-id:") &&
    !headerValue(response, "Mcp-Session-Id:"));

  fd = statelessStarted ? connectLoopback(statelessPort) : -1;
  ZuCheck(fd >= 0);
  body = "{\"jsonrpc\":\"2.0\",\"id\":81,\"method\":\"tools/call\","
    "\"params\":{\"name\":\"echo\",\"arguments\":{\"value\":81}}}";
  request.length_(0);
  request << "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\n"
    "MCP-Protocol-Version: 2025-11-25\r\nConnection: close\r\n"
    "Content-Length: " << body.length() << "\r\n\r\n" << body;
  ZuCheck(fd >= 0 && sendAll(fd, request));
  response.length_(0);
  ZuCheck(fd >= 0 && receiveAll(fd, response));
  if (fd >= 0) ::close(fd);
  ZuCheck(response.find("\"id\":81") >= 0);
  ZuCheck(response.find("\"value\":81") >= 0);
  ZuCheck(response.find("\"resultType\"") < 0);
  ZuCheck(statelessApp.sessionOpens == 0);
  if (statelessStarted) ZuCheck(statelessServer.stop());
  ZuCheck(statelessApp.streamOpens == statelessApp.streamCloses);
  statelessServer.final();

  if (initialized) ZuCheck(server.stop());
  ZuCheck(app.transportOpens > 0);
  ZuCheck(app.transportCloses == app.transportOpens);
  ZuCheck(app.sessionOpens == 1);
  ZuCheck(app.sessionCloses == 1);
  ZuCheck(app.streamOpens == app.streamCloses);
  ZuCheck(app.modernContexts == 8);
  ZuCheck(app.legacyContexts == 3);
  server.final();
  mx.stop();
#endif
}

#ifndef _WIN32
namespace Secure {
  enum { H1, H2, H3, N };
}

static void secureTest()
{
  ZuTestScopeRT(secure);
  Zquic::Test::TempDir temp;
  bool tempOK = temp.init("ZmcpHTTPTest");
  ZuCheckRT(tempOK);
  if (!tempOK) return;
  ZtString<> cert, key;
  bool certOK = Zquic::Test::writeLocalhostCert(temp, cert, key);
  ZuCheckRT(certOK);
  if (!certOK) return;

  for (int transport = Secure::H1; transport < Secure::N; ++transport) {
    unsigned port = Zquic::Test::loopbackPort(
      ZmcpITestPort::SecureH1 + transport);
    ZuCheckRT(port);
    if (!port) continue;

    auto params = ZiMxParams()
      .scheduler([](auto &scheduler) {
	scheduler.nThreads(2)
	  .thread(1, [](auto &thread) { thread.isolated(1); })
	  .thread(2, [](auto &thread) { thread.isolated(1); });
      }).rxThread(1).txThread(2);
    ZiMultiplex mx{ZuMv(params)};
    bool mxUp = mx.start();
    ZuCheckRT(mxUp);
    if (!mxUp) continue;

    bool h3 = transport == Secure::H3;
    bool multiplexed = transport != Secure::H1;
    App app;
    Zmcp::Server<App, Catalog> server;
    Zmcp::ServerConfig config;
    config.localIP(ZiIP{"127.0.0.1"}).port(port);
    config.absentOrigin(true);
    if (!h3)
      config.tls(Zhttp::H2Config{}.certPath(cert).keyPath(key).policy(
	multiplexed ? Zhttp::H2Policy::Force : Zhttp::H2Policy::Disable));
    else
      config.quic(Zhttp::QUICConfig{}.certPath(cert).keyPath(key)
	.maxIdleTimeout(10000));
    Zhttp::HubConfig hub{&mx, "1", "2"};
    bool initialized = server.init(hub, ZuMv(config), &app);
    ZuCheckRT(initialized);
    bool started = initialized && server.start();
    ZuCheckRT(started);
    if (started) app.listening_.wait();

    ClientApp clientApp;
    Zmcp::HTTPClient<ClientApp, Catalog> client;
    Zmcp::ClientConfig clientConfig;
    clientConfig.endpoint("/mcp");
    clientConfig.secure(true).tcp(false).tls(!h3).quic(h3)
      .protocol(h3 ? Zhttp::ProtoPolicy::ForceH3 :
	Zhttp::ProtoPolicy::DisableH3)
      .blindH3(h3)
      .h2Policy(multiplexed ?
	Zhttp::H2Policy::Force : Zhttp::H2Policy::Disable)
      .links(1).concurrency(4).linkMax(multiplexed ? 4 : 1);
    Zhttp::H2Config h2;
    h2.caPath(cert).policy(multiplexed ?
      Zhttp::H2Policy::Force : Zhttp::H2Policy::Disable);
    Zhttp::QUICConfig quic;
    quic.caPath(cert).maxIdleTimeout(10000);
    bool clientInited = started && client.init(
      hub, Zhttp::Destination{"localhost", uint16_t(port)},
      ZuMv(clientConfig), &clientApp, {}, h2, quic);
    ZuCheckRT(clientInited);
    bool clientStarted = clientInited && client.start();
    ZuCheckRT(clientStarted);
    if (clientStarted) clientApp.ready_.wait();
    ZuCheckRT(clientApp.era == Zmcp::Era::Modern);

    ZmRef<ClientCall> fixedCall = new ClientCall{};
    ZmRef<ClientCall> streamCall = new ClientCall{};
    bool fixed = client.call<Echo>(EchoReq{71}, fixedCall);
    bool streamed = client.call<EchoStream>(EchoReq{72}, streamCall);
    ZuCheckRT(fixed);
    ZuCheckRT(streamed);
    if (fixed) fixedCall->done.wait();
    if (streamed) streamCall->done.wait();
    ZuCheckRT(!fixedCall->failures && fixedCall->value == 71);
    ZuCheckRT(!streamCall->failures && streamCall->value == 72);

    ZmRef<ClientCall> first = new ClientCall{};
    ZmRef<ClientCall> second = new ClientCall{};
    bool firstSent = client.call<Echo>(EchoReq{73}, first);
    ZuCheckRT(firstSent);
    if (firstSent) app.called_.wait();
    bool secondSent;
    if (multiplexed) {
      secondSent = client.call<Echo>(EchoReq{74}, second);
      ZuCheckRT(secondSent);
      if (secondSent) app.called_.wait();
      ZuCheckRT(bool(app.complete1_) && bool(app.complete2_));
      ZuCheckRT(app.complete1_ && app.complete1_());
      ZuCheckRT(app.complete2_ && app.complete2_());
    } else {
      ZuCheckRT(app.complete1_ && app.complete1_());
      if (firstSent) first->done.wait();
      secondSent = client.call<Echo>(EchoReq{74}, second);
      ZuCheckRT(secondSent);
      if (secondSent) app.called_.wait();
      ZuCheckRT(app.complete2_ && app.complete2_());
    }
    if (firstSent && multiplexed) first->done.wait();
    if (secondSent) second->done.wait();
    ZuCheckRT(!first->failures && first->value == 73);
    ZuCheckRT(!second->failures && second->value == 74);

    ZmRef<ClientCall> cancelCall = new ClientCall{};
    bool cancelSent = client.call<Echo>(EchoReq{63}, cancelCall);
    ZuCheckRT(cancelSent);
    if (cancelSent) {
      cancelCall->started_.wait();
      app.called_.wait();
    }
    bool cancelled = cancelSent &&
      client.cancel(cancelCall->id, "transport matrix");
    ZuCheckRT(cancelled);
    if (cancelled) {
      clientApp.cancelled_.wait();
      app.cancelled_.wait();
      cancelCall->done.wait();
    }
    ZuCheckRT(clientApp.cancelOK && cancelCall->failures == 1);
    ZuCheckRT(app.cancellations == 1);
    ZuCheckRT(app.complete_ && !app.complete_());
    ZuCheckRT(app.transportOpens == 1);
    ZuCheckRT(app.modernContexts == 5);
    ZuCheckRT(app.authorizedCalls == 5);

    if (clientInited) ZuCheckRT(client.stop());
    client.final();
    if (started) ZuCheckRT(server.stop());
    ZuCheckRT(app.transportCloses == app.transportOpens);
    ZuCheckRT(app.streamOpens == app.streamCloses);
    server.final();
    mx.stop();
  }
}
#endif

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(httpTest);
#ifndef _WIN32
  ZuTestCall(secureTest);
#endif
}
