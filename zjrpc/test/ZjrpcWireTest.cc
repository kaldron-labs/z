//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZjrpcHTTP.hh>
#include <zlib/ZjrpcStdio.hh>
#include <zlib/ZjrpcWS.hh>

using namespace ZuTestUtil;

struct ErrorMessage {
  template <typename S>
  void write(S &out) const {
    Zjrpc::saveError(out, Zjrpc::ID{int64_t{1}}, -1, "message");
  }
};

struct Message {
  template <typename S>
  void write(S &out) const {
    Zjrpc::saveRequest(out, Zjrpc::ID{int64_t{7}}, "echo", Zjrpc::EmptyObject{});
  }
};

struct BodyOut {
  ZtString<> data;
  bool flushOK = true;
  bool failure = false;

  template <typename V>
  BodyOut &operator <<(V &&v) { data << ZuFwd<V>(v); return *this; }
  bool failed() const { return failure; }
  bool flush() { if (!flushOK) failure = true; return !failure; }
  uint64_t produced() const { return data.length(); }
};

struct Rx {
  ZuSpan<uint8_t> data;
  explicit operator bool() const { return data.length(); }
  template <typename Available, typename Consume>
  int64_t consume(Available &&available, Consume &&consume) {
    int64_t n = available(data);
    if (n > 0) {
      auto span = data;
      span.trunc(n);
      consume(span);
      data = data.offset(n);
    }
    return n;
  }
};

using AppHeaders = ZhttpHeaders("x-peer");
using PeerHeader = ZuStringT<"x-peer">;
struct ReqParser : public Zjrpc::HTTPRequestParser<ReqParser, AppHeaders> {
  Zjrpc::Limits bound;
  ZmRef<ZiIOBuf> received;
  Zjrpc::HTTPHeaders<AppHeaders> headers;
  unsigned corrupt = 0;

  ZuCSpan endpoint() const { return "/rpc"; }
  const Zjrpc::Limits &limits() const { return bound; }
  template <typename Link> void corruptHTTP(Link *) { ++corrupt; }
  template <typename Link>
  void receiveHTTP(Link *, ZmRef<ZiIOBuf> body, Zjrpc::HTTPHeaders<AppHeaders> app) {
    received = ZuMv(body);
    headers = ZuMv(app);
  }
};

static void sseTest()
{
  ZuTestScope(sse);
  Zjrpc::SSEDecoder decoder{128, 128};
  unsigned events = 0;
  ZtString<> data, id;
  int64_t retry = -1;
  auto event = [&events, &data, &id, &retry](const Zjrpc::SSEEvent &event) {
    ++events;
    data.length_(0);
    data << event.data();
    id.length_(0);
    id << event.id;
    retry = event.retry;
  };
  ZuCheck(decoder.feed("id: 7\ndata: {\"json", event));
  ZuCheck(decoder.feed("rpc\":\"2.0\"}\nretry: 250\n\n", event));
  ZuCheck(events == 1);
  ZuCheck(id == "7");
  ZuCheck(data == "{\"jsonrpc\":\"2.0\"}");
  ZuCheck(retry == 250);
  ZuCheck(decoder.feed("data: {}\n\ndata: {\"id\":2}\n\n", event));
  ZuCheck(events == 3);

  Zjrpc::SSEDecoder bounded{8, 128};
  ZuCheck(bounded.feed("data: {}\n\n", event));
  ZuCheck(events == 4);
  ZuCheck(!bounded.feed("data: {}x\n\n", event));
  ZuCheck(bounded.state() == Zjrpc::SSEDecoder::Closed && events == 4);
  bounded.reset(8, 128);
  ZuCheck(bounded.feed("data: ", event));
  ZuCheck(!bounded.feed("{}x\n\n", event));
  ZuCheck(bounded.state() == Zjrpc::SSEDecoder::Closed && events == 4);

  ZtString<> out;
  Zjrpc::saveSSE(out, "9", 1000, "{\"result\":{}}");
  ZuCheck(out ==
    "id: 9\nretry: 1000\ndata: {\"result\":{}}\n\n");
}

static void sseLifetimeTest()
{
  ZuTestScope(sseLifetime);
  Zjrpc::SSEDecoder decoder{128, 128};
  ZmRef<ZiIOBuf> first, next;
  auto event = [&first, &next](Zjrpc::SSEEvent event) {
    if (!first) first = ZuMv(event.body);
    else next = ZuMv(event.body);
  };
  ZuCheck(decoder.feed("data: {\"jsonrpc\":\"2.0\",\"id\":1,", event));
  ZuCheck(decoder.feed("\"result\":\"retained\"}\n\n", event));
  ZuCheck(bool(first), (return));
  auto parsed = Zjrpc::parse(first->span(), first->length);
  ZuCheck(parsed.envelope.kind == Zjrpc::MessageKind::Result);
  ZuCheck(decoder.feed("data: {}\n\n", event));
  ZuCheck(next && next != first && ZuCSpan{*next} == "{}");
  auto result = Zjrpc::raw(parsed.envelope.result());
  ZuCheck(result && result->data<ZfJSON::AnyNode::String>() == "retained");
}

static void stdioTest()
{
  ZuTestScope(stdio);
  Zjrpc::StdioFramer framer{64};
  unsigned frames = 0;
  ZtString<> last;
  auto frame = [&frames, &last](ZmRef<ZiIOBuf> buf) {
    ++frames;
    last = ZuCSpan{*buf};
  };
  ZuCheck(framer.feed("{\"id\":1", frame));
  ZuCheck(framer.feed("}\r\n{}\n", frame));
  ZuCheck(frames == 2);
  ZuCheck(last == "{}");
  ZuCheck(framer.eof());

  Zjrpc::StdioFramer bounded{4};
  ZuCheck(!bounded.feed("12345", frame));
  ZuCheck(bounded.state() == Zjrpc::StdioFramer::Closed);

  auto out = Zjrpc::stdioFrame(ZuCSpan{"{}"});
  ZuCheck(out->length == 3);
  ZuCheck(ZuCSpan{*out} == "{}\n");

  auto boundedOut = Zjrpc::stdioFrame(
    ErrorMessage{}, 16);
  ZuCheck(!boundedOut);

  Zjrpc::BufOutput failedOut{*out, 64};
  ZuBSpan bytes{"x"};
  ZuCheck(!out->append(bytes.data(), UINT_MAX));
  ZuCheck(failedOut.failed() && !failedOut);
  failedOut << "ignored";
  ZuCheck(ZuCSpan{*out} == "{}\n");
}

static void httpInputTest()
{
  ZuTestScope(httpInput);
  ReqParser parser;
  Zhttp::Target target;
  char path[] = "/rpc";
  target.path = ZuSpan<char>{path, sizeof(path) - 1};
  ZuCheck(parser.operation(Zhttp::Method::POST, target));
  ZuCheck(!parser.operation(Zhttp::Method::GET, target));
  char peer[] = "peer-7";
  parser.header<PeerHeader>(Zhttp::FieldSection::Final,
    ZuSpan<char>{peer, sizeof(peer) - 1});
  parser.bound.maxJSONBytes = 7;
  ZuCheck(parser.bodyInfo(Zhttp::BodyType::Fixed, 7));
  char first[] = "{\"x\":";
  char last[] = "1}";
  Rx rx1{ZuSpan<char>{first, sizeof(first) - 1}};
  Rx rx2{ZuSpan<char>{last, sizeof(last) - 1}};
  ZuCheck(parser.body(rx1));
  ZuCheck(parser.body(rx2));
  parser.complete(static_cast<void *>(nullptr), true);
  ZuCheck(parser.received && ZuCSpan{*parser.received} == "{\"x\":1}");
  auto header = parser.headers.get<PeerHeader>();
  ZuCheck(header.count == 1 && header.value == "peer-7");
  parser.reset();
  ZuCheck(!parser.bodyInfo(Zhttp::BodyType::Fixed, 8));
  parser.complete(static_cast<void *>(nullptr), false);
  ZuCheck(parser.corrupt == 1);
  parser.reset();
  ZuCheck(parser.bodyInfo(Zhttp::BodyType::Streamed, 0));
  Rx over1{ZuSpan<char>{first, sizeof(first) - 1}};
  Rx over2{ZuSpan<char>{first, sizeof(first) - 1}};
  ZuCheck(parser.body(over1));
  ZuCheck(!parser.body(over2));
  parser.complete(static_cast<void *>(nullptr), true);
  ZuCheck(parser.corrupt == 2);
}

static void httpOutputTest()
{
  ZuTestScope(httpOutput);
  Zjrpc::HTTPRequestBuilder<Message> request;
  request.endpoint = "/rpc";
  request.sequence = 17;
  ZtString<> path;
  int method = -1;
  request.operation([&path, &method](auto op, auto emit) {
    method = op;
    emit([&path](auto write) { write(path); });
  });
  ZuCheck(method == Zhttp::Method::POST && path == "/rpc");
  ZuCheck(request.key() == 17);
  BodyOut out;
  int outcome = -1;
  request.body([&out, &outcome](auto write) { outcome = write(out); });
  ZuCheck(outcome == Zhttp::WriteOutcome::End);
  ZuCheck(out.data == "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"echo\",\"params\":{}}");
  request.maxBodyBytes = 8;
  BodyOut bounded;
  request.body([&bounded, &outcome](auto write) { outcome = write(bounded); });
  ZuCheck(outcome == Zhttp::WriteOutcome::Abort);
  ZuCheck(bounded.data.length() <= request.maxBodyBytes);

  Zjrpc::HTTPFixedBuilder<ErrorMessage> response;
  ZuCheck(response.status() == 200);
  BodyOut failed;
  failed.flushOK = false;
  response.body([&failed, &outcome](auto write) { outcome = write(failed); });
  ZuCheck(failed.failed() && outcome == Zhttp::WriteOutcome::Abort);
}

static void sseOutputTest()
{
  ZuTestScope(sseOutput);
  Zjrpc::HTTPSSEBuilder builder;
  ZuCheck(builder.status() == 200);
  ZuCheck(builder.emit(Message{}, false));
  ZuCheck(builder.emit(ErrorMessage{}));
  ZuCheck(!builder.emit(Message{}));
  BodyOut out;
  unsigned messages = 0;
  builder.body([&builder, &out, &messages](auto write) {
    ++messages;
    auto outcome = write(out);
    if (outcome == Zhttp::WriteOutcome::End) builder.close();
    return true;
  });
  ZuCheck(messages == 2);
  Zjrpc::SSEDecoder decoder{1024, 1024};
  unsigned events = 0;
  ZuCheck(decoder.feed(out.data, [&events](const auto &) { ++events; }));
  ZuCheck(events == 2);
  Zjrpc::Limits limits;
  limits.maxQueue = 1;
  Zjrpc::HTTPSSEBuilder bounded{limits};
  ZuCheck(bounded.emit(Message{}, false));
  ZuCheck(!bounded.emit(ErrorMessage{}));
  bounded.close();
}

struct WSFixture : public Zjrpc::WSIO<WSFixture> {
  template <typename Heap = ZuVoid>
  struct Link_ : public Heap, public ZmObject {
    WSFixture *owner;
    Zjrpc::WSRx rx;
    unsigned closeCode = 0;
    Link_(WSFixture *owner_) : owner{owner_} { }
    WSFixture *app() { return owner; }
    Link_ &state() { return *this; }
    void close(unsigned code) { closeCode = code; }
  };
  using LinkHeap = ZmHeap<"Zjrpc.Test.WS", Link_<>>;
  using Link = Link_<LinkHeap>;

  Zjrpc::Limits bound;
  ZmRef<ZiIOBuf> received;
  unsigned pongs = 0;
  const Zjrpc::Limits &limits() const { return bound; }
  bool wsAccept() const { return true; }
  WSFixture *impl() { return this; }
  template <typename L> void txRun(L &&l) { l(); }
  template <typename L> void rxRun(L &&l) { l(); }
  void wsFrame_(Link &, ZmRef<ZiIOBuf> body) { received = ZuMv(body); }
  void pong(Link &, ZuBSpan payload) { if (payload == ZuBSpan{"p"}) ++pongs; }
};

static void wsInputTest()
{
  ZuTestScope(wsInput);
  WSFixture fixture;
  ZmRef<WSFixture::Link> link = new WSFixture::Link{&fixture};
  link->rx.open();
  ZuCheck(fixture.messageStart(*link, Zws::Opcode::Text) > 0);
  char first[] = "{\"jsonrpc\":\"2.0\",\"id\":7,";
  char last[] = "\"result\":{}}";
  Rx rx1{ZuSpan<char>{first, sizeof(first) - 1}};
  Rx rx2{ZuSpan<char>{last, sizeof(last) - 1}};
  ZuCheck(fixture.process(*link, rx1) > 0);
  // Zws owns frame/control decoding; its callbacks must preserve RPC assembly.
  fixture.Zjrpc::WSIO<WSFixture>::pong(*link, "p");
  ZuCheck(fixture.pongs == 1 && !fixture.received);
  ZuCheck(fixture.process(*link, rx2) > 0);
  ZuCheck(!fixture.received);
  ZuCheck(fixture.messageEnd(*link) > 0);
  ZuCheck(bool(fixture.received), (return));
  auto parsed = Zjrpc::parse(fixture.received->span(), fixture.received->length);
  ZuCheck(parsed && parsed.envelope.kind == Zjrpc::MessageKind::Result &&
    parsed.envelope.id() == Zjrpc::ID{int64_t{7}});
  ZuCheck(!link->closeCode);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(sseTest);
  ZuTestCall(sseLifetimeTest);
  ZuTestCall(stdioTest);
  ZuTestCall(httpInputTest);
  ZuTestCall(httpOutputTest);
  ZuTestCall(sseOutputTest);
  ZuTestCall(wsInputTest);
  return 0;
}
