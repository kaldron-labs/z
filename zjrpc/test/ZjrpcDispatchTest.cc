//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZjrpcDispatch.hh>

using namespace ZuTestUtil;

struct Value { int value = 0; };
ZfStruct(, (Value, JSON), (value, (Ctor<0>, Required), Int32));
struct OK : public Zjrpc::Response { using Body = Value; };
struct Echo : public Zjrpc::Request {
  using Object = Value;
  using Method = ZuStringT<"echo">;
  using Responses = ZuTypeList<OK>;
};
struct Text { ZuCSpan value; };
ZfStruct(, (Text, JSON), (value, (Ctor<0>, Required), String));
struct TextOK : public Zjrpc::Response { using Body = Text; };
struct EchoText : public Zjrpc::Request {
  using Object = Text;
  using Method = ZuStringT<"echoText">;
  using Responses = ZuTypeList<TextOK>;
  enum { Borrowed = 1 };
};
using Catalog = ZuTypeList<Echo, EchoText>;

struct App {
  ZmFn<bool()> deferred;
  ZmFn<void()> onCancel;
  unsigned requests = 0;
  unsigned notifications = 0;
  unsigned cancellations = 0;
  bool completedOnCancel = true;
  template <typename Req, typename Token>
  void cancelled(Req *, Token *token, ZuCSpan) {
    ++cancellations;
    if (onCancel) onCancel();
    if constexpr (ZuIsSame<Req, Echo>{})
      completedOnCancel = token->complete(Zjrpc::Reply<OK>{Value{8}});
    else
      completedOnCancel = token->complete(Zjrpc::Reply<TextOK>{Text{"closed"}});
  }
  template <typename Token>
  void request(EchoText *, const Text &text, Token token) {
    ++requests;
    deferred = [text, token = ZuMv(token)]() {
      return token->complete(Zjrpc::Reply<TextOK>{text});
    };
  }
  template <typename Token>
  void request(Echo *, const Value &value, Token token) {
    if (!token) { ++notifications; return; }
    ++requests;
    if (value.value == 8) {
      deferred = [token = ZuMv(token)]() {
	return token->complete(Zjrpc::Reply<OK>{Value{8}});
      };
      return;
    }
    token->complete(Zjrpc::Reply<OK>{value});
  }
};

class Harness : public Zjrpc::Dispatcher<Harness, App, Catalog>,
    public Zjrpc::Caller<Harness, Catalog> {
  using Base = Zjrpc::Dispatcher<Harness, App, Catalog>;
  using Calls = Zjrpc::Caller<Harness, Catalog>;
  friend Calls;
  friend Zjrpc::BatchCaller<Harness>;
public:
  Harness() {
    m_limits.workBatch = 2;
    Calls::init(m_limits.maxPending);
    m_inbound.init(Zjrpc::Default::HistSize);
  }
  ~Harness() { close(); }
  App &app() { return m_app; }
  App *impl() { return &m_app; }
  const Zjrpc::Limits &limits() const { return m_limits; }
  bool up() const { return m_up; }
  Zjrpc::CompletionRoute completionRoute() const { return {}; }
  bool invoked() const { return true; }
  void fail_() { m_failed = true; }
  bool failed() const { return m_failed; }
  template <typename L> bool ownerRun(L &&l) { l(); return true; }
  template <typename L> void continue_(L &&l) { m_turns.push(ZuFwd<L>(l)); }
  using Calls::response;
  bool pendingID(int64_t id) { return Calls::pending(Zjrpc::ID{id}); }
  unsigned sent() const { return m_sent; }
  template <typename M> bool send(const M &) { ++m_sent; return true; }

  bool receive(ZuCSpan input, bool retain = false,
      Zjrpc::WorkControl *control = nullptr, bool peer = true) {
    ZmRef<ZiIOBuf> body = new Zjrpc::BatchBuf{};
    *body << input;
    return Base::receive(ZuMv(body), [this, retain](auto message, int) {
      if (retain) { m_messages.push(ZuMv(message)); return true; }
      if (!message.empty()) {
	ZtString<> output;
	message.write(output);
	m_output.push(ZuMv(output));
      }
      return true;
    }, peer ? &m_inbound : nullptr, nullptr, control);
  }
  void flush() {
    for (const auto &message : m_messages) {
      ZtString<> output;
      message.write(output);
      m_output.push(ZuMv(output));
    }
    m_messages.length(0);
  }
  unsigned liveCount() const { return m_inbound.count(); }
  bool closePeer() { return m_inbound.close(2); }
  bool turn() {
    auto fn = m_turns.shift();
    if (!fn) return false;
    fn->data()();
    return true;
  }
  ZtArray<ZtString<>> &output() { return m_output; }
  void close() {
    m_up = false;
    for (;;) {
      bool done = Base::close();
      done = Calls::closeCalls() && done;
      if (done) break;
      (void)turn();
    }
    while (turn()) { }
    (void)m_inbound.close(0);
  }

private:
  App m_app;
  Zjrpc::Limits m_limits;
  Zjrpc::InboundCalls m_inbound;
  ZmList<ZmFn<void()>> m_turns;
  ZtArray<ZtString<>> m_output;
  ZtArray<typename Base::Message> m_messages;
  unsigned m_sent = 0;
  bool m_up = true;
  bool m_failed = false;
};

static void routeCloseTest()
{
  ZuTestScope(routeClose);
  Harness harness;
  ZuCheck(harness.receive(
    "{\"jsonrpc\":\"2.0\",\"id\":90,\"method\":\"echo\",\"params\":[8]}"));
  Zjrpc::WorkControl control;
  ZuCheck(harness.receive(
    "[{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"echo\",\"params\":[8]},"
    "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"echo\",\"params\":[8]},"
    "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"echo\",\"params\":[8]},"
    "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"echo\",\"params\":[8]},"
    "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"echo\",\"params\":[8]}]",
    false, &control, false));
  ZuCheck(harness.app().requests == 3);
  ZuCheck(harness.turn());
  ZuCheck(harness.app().requests == 5);
  control.close();
  ZuCheck(harness.app().cancellations == 2);
  ZuCheck(!harness.app().completedOnCancel);
  while (harness.turn()) { }
  ZuCheck(harness.app().requests == 5);
  ZuCheck(harness.app().cancellations == 4);
  ZuCheck(!harness.app().deferred());
  control.close(); // Terminal work cleared the route's back-pointer.
  ZuCheck(harness.app().cancellations == 4);
  harness.close();
  ZuCheck(harness.app().cancellations == 5); // Unrelated work survived route closure.
}

static void abortHistoryTest()
{
  ZuTestScope(abortHistory);
  Harness harness;
  Zjrpc::WorkControl control;
  ZuCheck(harness.receive(
    "{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"echo\",\"params\":[8]}",
    false, &control));
  unsigned live = 1;
  bool accepted = false;
  harness.app().onCancel = [&harness, &live, &accepted]() {
    live = harness.liveCount();
    accepted = harness.receive(
      "{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"echo\",\"params\":[7]}");
  };
  control.close();
  ZuCheck(!live && accepted);
  ZuCheck(harness.app().requests == 1 && harness.app().cancellations == 1);
  ZuCheck(harness.output().length() == 1, (return));
  auto output = Zjrpc::parse(harness.output()[0].span(), harness.output()[0].length());
  ZuCheck(output && output.envelope.kind == Zjrpc::MessageKind::Error, (return));
  ZuCheck(Zjrpc::loadError(Zjrpc::raw(output.envelope.error())).code ==
    Zjrpc::ErrorCode::InvalidRequest);
  harness.app().onCancel = {};
}

static void batchTest()
{
  ZuTestScope(batch);
  Harness harness;
  ZuCheck(harness.receive(
    "[{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"echo\",\"params\":[7]},"
    "{\"jsonrpc\":\"2.0\",\"method\":\"echo\",\"params\":[9]},42,"
    "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"missing\"},"
    "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"echo\",\"params\":[8]}]"));
  ZuCheck(harness.app().requests == 1 && harness.app().notifications == 1);
  ZuCheck(!harness.output().length());
  ZuCheck(harness.turn());
  ZuCheck(!harness.output().length());
  ZuCheck(harness.turn());
  ZuCheck(harness.app().requests == 2 && !harness.output().length());
  ZuCheck(harness.app().deferred());
  ZuCheck(harness.output().length() == 1);
  auto &output = harness.output()[0];
  auto parsed = Zjrpc::parse(output.span(), output.length());
  ZuCheck(parsed.batch());
  const auto &entries = parsed.value()->data<ZfJSON::AnyNode::Array>();
  ZuCheck(entries.length() == 4);
  auto first = Zjrpc::decode(entries[0].ptr());
  auto invalid = Zjrpc::decode(entries[1].ptr());
  auto missing = Zjrpc::decode(entries[2].ptr());
  auto deferred = Zjrpc::decode(entries[3].ptr());
  ZuCheck(first.kind == Zjrpc::MessageKind::Result && first.id() == Zjrpc::ID{int64_t{1}});
  ZuCheck(invalid.kind == Zjrpc::MessageKind::Error && invalid.id().is<Zjrpc::Null>());
  ZuCheck(missing.kind == Zjrpc::MessageKind::Error && missing.id() == Zjrpc::ID{int64_t{2}});
  ZuCheck(deferred.kind == Zjrpc::MessageKind::Result && deferred.id() == Zjrpc::ID{int64_t{3}});
  ZuCheck(!harness.app().deferred());
  ZuCheck(!harness.failed());
}

static void duplicateTest()
{
  ZuTestScope(duplicate);
  Harness harness;
  ZuCheck(harness.receive(
    "[{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"echo\",\"params\":[8]},"
    "{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"echo\",\"params\":[7]}]"));
  ZuCheck(harness.app().requests == 1 && !harness.output().length());
  ZuCheck(harness.app().deferred());
  ZuCheck(harness.output().length() == 1);
  ZuCheck(harness.receive(
    "{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"echo\",\"params\":[7]}"));
  ZuCheck(harness.app().requests == 1 && harness.output().length() == 2);
  ZuCheck(!harness.failed());
}

static void emptyTest()
{
  ZuTestScope(empty);
  Harness notifications;
  ZuCheck(notifications.receive(
    "[{\"jsonrpc\":\"2.0\",\"method\":\"echo\",\"params\":[7]},"
    "{\"jsonrpc\":\"2.0\",\"method\":\"echo\",\"params\":[8]}]"));
  ZuCheck(notifications.app().notifications == 2 && !notifications.output().length());
  Harness empty;
  ZuCheck(empty.receive("[]"));
  ZuCheck(empty.output().length() == 1);
  auto &output = empty.output()[0];
  auto parsed = Zjrpc::parse(output.span(), output.length());
  ZuCheck(!parsed.batch() && parsed.envelope.kind == Zjrpc::MessageKind::Error);
}

static void closeTest()
{
  ZuTestScope(close);
  Harness harness;
  ZuCheck(harness.receive(
    "[{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"echo\",\"params\":[8]},"
    "{\"jsonrpc\":\"2.0\",\"method\":\"echo\",\"params\":[7]},"
    "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"echo\",\"params\":[7]}]"));
  harness.close();
  ZuCheck(harness.app().requests == 1);
  ZuCheck(harness.app().cancellations == 1);
  ZuCheck(!harness.app().completedOnCancel);
  ZuCheck(!harness.app().deferred());
  ZuCheck(!harness.output().length() && !harness.failed());
}

static void peerCloseTest()
{
  ZuTestScope(peerClose);
  Harness harness;
  ZuCheck(harness.receive(
    "[{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"echo\",\"params\":[8]},"
    "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"echo\",\"params\":[8]},"
    "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"echo\",\"params\":[7]}]"));
  ZuCheck(harness.app().requests == 2);
  ZuCheck(harness.closePeer());
  ZuCheck(harness.app().cancellations == 2);
  ZuCheck(!harness.app().completedOnCancel && !harness.app().deferred());
  ZuCheck(harness.turn());
  ZuCheck(harness.app().requests == 2 && !harness.output().length());
  // A closed peer's history is drained; reopening its ownership scope is fresh.
  ZuCheck(harness.receive(
    "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"echo\",\"params\":[7]}"));
  ZuCheck(harness.app().requests == 3 && harness.output().length() == 1);
  ZuCheck(!harness.failed());
}

static void borrowedTest()
{
  ZuTestScope(borrowed);
  Harness harness;
  ZuCheck(harness.receive(
    "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"echoText\","
    "\"params\":{\"value\":\"borrowed through deferred output\"}}", true));
  ZuCheck(harness.app().requests == 1 && !harness.output().length());
  ZuCheck(harness.app().deferred());
  harness.close();
  // Output outlives live work registration and deferred completion.
  harness.flush();
  ZuCheck(harness.output().length() == 1);
  auto &output = harness.output()[0];
  auto parsed = Zjrpc::parse(output.span(), output.length());
  ZuCheck(parsed.envelope.kind == Zjrpc::MessageKind::Result);
  auto handler = ZfJSON::handler<Text>(Zjrpc::raw(parsed.envelope.result()));
  ZuCheck(handler.valid && handler.ctor().value == "borrowed through deferred output");
  ZuCheck(!harness.failed());
}

template <typename Heap = ZuVoid>
struct Aggregate_ : public Heap, public ZmObject {
  Harness *harness;
  Zjrpc::BatchReply reply;
  unsigned completions = 0;
  unsigned failures = 0;
  bool removed = false;

  explicit Aggregate_(Harness *harness_) : harness{harness_} { }
  void process(Zjrpc::BatchReply reply_) {
    removed = !harness->pendingID(42) && !harness->pendingID(43);
    reply = ZuMv(reply_);
    ++completions;
  }
  void failed() {
    removed = !harness->pendingID(42) && !harness->pendingID(43);
    ++failures;
  }
};
using AggregateHeap = ZmHeap<"Zjrpc.Test.Aggregate", Aggregate_<>>;
using Aggregate = Aggregate_<AggregateHeap>;

template <typename Heap = ZuVoid>
struct Payload_ : public Heap, public ZmObject {
  unsigned *destroyed;
  int value = 7;

  explicit Payload_(unsigned *destroyed_) : destroyed{destroyed_} { }
  ~Payload_() { ++*destroyed; }
};
using PayloadHeap = ZmHeap<"Zjrpc.Test.Payload", Payload_<>>;
using Payload = Payload_<PayloadHeap>;
ZfStruct(, (Payload, JSON), (value, (Required), Int32));
struct OwnedEcho : public Zjrpc::Request {
  using Object = Payload;
  using Method = ZuStringT<"ownedEcho">;
  using Responses = ZuTypeList<OK>;
};

static void batchLifetimeTest()
{
  ZuTestScope(batchLifetime);
  unsigned destroyed = 0;
  Harness harness;
  ZmRef<Payload> payload = new Payload{&destroyed};
  Zjrpc::Batch<ZuTypeList<OwnedEcho>> batch;
  ZuCheck(batch.request<OwnedEcho>(int64_t{42}, payload));
  ZuCheck(batch.request<OwnedEcho>(int64_t{43}, ZuMv(payload)));
  ZmRef<Aggregate> aggregate = new Aggregate{&harness};
  ZuCheck(harness.callBatch(ZuMv(batch), aggregate));
  ZuCheck(destroyed == 1);
  ZuCheck(harness.pendingID(42) && harness.pendingID(43));
  ZuCheck(!aggregate->completions && !aggregate->failures);
  harness.close();
  ZuCheck(aggregate->failures == 1 && aggregate->removed);
}

static void outboundTest()
{
  ZuTestScope(outbound);
  Harness harness;
  Zjrpc::Batch<Catalog> batch;
  ZuCheck(batch.request<Echo>(int64_t{42}, Value{7}));
  batch.notify<Echo>(Value{9});
  ZuCheck(batch.request<Echo>(int64_t{43}, Value{8}));
  ZmRef<Aggregate> aggregate = new Aggregate{&harness};
  ZuCheck(harness.callBatch(ZuMv(batch), aggregate));
  ZuCheck(harness.sent() == 0);
  ZuCheck(harness.turn());
  ZuCheck(harness.sent() == 1 && !aggregate->completions && !aggregate->failures);
  ZuCheck(harness.receive(
    "[{\"jsonrpc\":\"2.0\",\"id\":43,\"error\":{\"code\":-32000,\"message\":\"failed\"}},"
    "{\"jsonrpc\":\"2.0\",\"id\":42,\"result\":{\"value\":7}}]"));
  ZuCheck(aggregate->completions == 1 && !aggregate->failures && aggregate->removed);
  ZuCheck(aggregate->reply.entries().length() == 2);
  auto first = Zjrpc::decode(aggregate->reply.entries()[0].ptr());
  ZuCheck(first.kind == Zjrpc::MessageKind::Error && first.id() == Zjrpc::ID{int64_t{43}});
  ZuCheck(!harness.output().length());
  // A late whole-array response cannot complete the aggregate a second time.
  ZuCheck(harness.receive(
    "[{\"jsonrpc\":\"2.0\",\"id\":42,\"result\":{\"value\":7}}]"));
  ZuCheck(aggregate->completions == 1 && !aggregate->failures);
}

static void shortBatchTest()
{
  ZuTestScope(shortBatch);
  Harness harness;
  Zjrpc::Batch<Catalog> batch;
  ZuCheck(batch.request<Echo>(int64_t{42}, Value{7}));
  ZuCheck(batch.request<Echo>(int64_t{43}, Value{8}));
  ZuCheck(batch.request<Echo>(int64_t{44}, Value{9}));
  ZmRef<Aggregate> aggregate = new Aggregate{&harness};
  ZuCheck(harness.callBatch(ZuMv(batch), aggregate));
  ZuCheck(harness.turn());
  ZuCheck(harness.receive(
    "[{\"jsonrpc\":\"2.0\",\"id\":43,\"result\":{\"value\":8}}]"));
  ZuCheck(!aggregate->completions && !aggregate->failures);
  ZuCheck(harness.turn());
  ZuCheck(aggregate->failures == 1 && !aggregate->completions && aggregate->removed);
  ZuCheck(!harness.pendingID(44));
}

static void batchCollisionTest()
{
  ZuTestScope(batchCollision);
  Harness harness;
  Zjrpc::Batch<Catalog> first, duplicate;
  ZuCheck(first.request<Echo>(int64_t{42}, Value{7}));
  ZuCheck(duplicate.request<Echo>(int64_t{43}, Value{8}));
  ZuCheck(duplicate.request<Echo>(int64_t{42}, Value{9}));
  ZmRef<Aggregate> original = new Aggregate{&harness};
  ZmRef<Aggregate> rejected = new Aggregate{&harness};
  ZuCheck(harness.callBatch(ZuMv(first), original));
  ZuCheck(harness.callBatch(ZuMv(duplicate), rejected));
  ZuCheck(harness.sent() == 1 && rejected->failures == 1);
  ZuCheck(harness.pendingID(42) && !harness.pendingID(43));
  ZuCheck(!original->failures && !original->completions);
  harness.close();
  ZuCheck(original->failures == 1 && original->removed);
  ZuCheck(rejected->failures == 1);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(batchTest);
  ZuTestCall(routeCloseTest);
  ZuTestCall(abortHistoryTest);
  ZuTestCall(duplicateTest);
  ZuTestCall(emptyTest);
  ZuTestCall(closeTest);
  ZuTestCall(peerCloseTest);
  ZuTestCall(borrowedTest);
  ZuTestCall(outboundTest);
  ZuTestCall(shortBatchTest);
  ZuTestCall(batchCollisionTest);
  ZuTestCall(batchLifetimeTest);
  return 0;
}
