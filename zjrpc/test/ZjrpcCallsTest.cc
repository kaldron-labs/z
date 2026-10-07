//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZjrpcPending.hh>
#include <zlib/ZjrpcInbound.hh>
#include <zlib/ZjrpcCompletion.hh>

using namespace ZuTestUtil;

struct Body { int value = 0; };
ZfStruct(, (Body, JSON), (value, (Ctor<0>), Int32));
struct OK : public Zjrpc::Response { using Body = ::Body; };
struct Echo : public Zjrpc::Request {
  using Object = Body;
  using Method = ZuStringT<"echo">;
  using Responses = ZuTypeList<OK>;
};

template <typename Heap = ZuVoid>
struct Call_ : public Heap, public ZmObject {
  Zjrpc::PendingCalls *pending;
  Zjrpc::ID id;
  int result = 0;
  unsigned failures = 0;
  bool removed = false;

  Call_(Zjrpc::PendingCalls *pending_, Zjrpc::ID id_) :
    pending{pending_}, id{ZuMv(id_)} { }

  void process(Zjrpc::Reply<OK> reply) {
    removed = !pending->contains(id);
    result = reply.body.value;
  }
  void failed() { removed = !pending->contains(id); ++failures; }
  void failed(const Zjrpc::Error &) { failed(); }
};
using CallHeap = ZmHeap<"Zjrpc.Test.Call", Call_<>>;
using Call = Call_<CallHeap>;

static void pendingTest()
{
  ZuTestScope(pending);
  Zjrpc::PendingCalls pending{1};
  Zjrpc::ID id{int64_t{42}};
  ZmRef<Call> call = new Call{&pending, id};
  ZuCheck(pending.add<Echo>(id, call));
  ZuCheck(!pending.add<Echo>(id, call));
  char input[] = "{\"jsonrpc\":\"2.0\",\"id\":42,\"result\":{\"value\":7}}";
  auto parsed = Zjrpc::parse(input, sizeof(input));
  ZuCheck(pending.receive(parsed.envelope));
  ZuCheck(call->removed && call->result == 7);
  ZuCheck(pending.count() == 0);
  ZuCheck(pending.receive(parsed.envelope));
  ZuCheck(call->failures == 0);
  ZuCheck(pending.add<Echo>(id, call));
  ZuCheck(!pending.close(0));
  ZuCheck(pending.close(1));
  ZuCheck(call->removed && call->failures == 1);
  ZuCheck(!pending.add<Echo>(id, call));
}

struct Work : public Zjrpc::InboundHash::Node {
  unsigned cancels = 0;
  unsigned closes = 0;
  bool cancel_(const Zjrpc::ID &, ZuCSpan) { ++cancels; return true; }
  void close_() { ++closes; }
};

static void inboundTest()
{
  ZuTestScopeRT(inbound);
  Zjrpc::InboundCalls inbound{8};
  Work first, duplicate;
  Zjrpc::ID id{int64_t{42}};
  ZuCheckRT(inbound.begin(id, &first));
  ZuCheckRT(!inbound.begin(id, &duplicate));
  ZuCheckRT(inbound.cancel(id, {}));
  ZuCheckRT(first.cancels == 1 && !duplicate.cancels);
  inbound.finish(id, &duplicate);
  ZuCheckRT(inbound.count() == 1);
  inbound.finish(id, &first);
  ZuCheckRT(inbound.count() == 0);
  ZuCheckRT(!inbound.begin(id, &duplicate));
  // Eviction affects history only; the live call survives a full window.
  Zjrpc::ID live{int64_t{43}};
  ZuCheckRT(inbound.begin(live, &first));
  unsigned histSize = inbound.histSize();
  for (unsigned n = 0; n < histSize; ++n) {
    Zjrpc::ID used{int64_t{100 + n}};
    ZuCheckRT(inbound.begin(used, &duplicate));
    inbound.finish(used, &duplicate);
  }
  ZuCheckRT(!inbound.begin(live, &duplicate));
  ZuCheckRT(inbound.begin(id, &duplicate));
  ZuCheckRT(!inbound.close(1));
  ZuCheckRT(inbound.close(1));
  ZuCheckRT(first.closes == 1 && duplicate.closes == 1);
  ZuCheckRT(inbound.begin(live, &first));
  ZuCheckRT(inbound.close(1));
}

template <typename Heap = ZuVoid>
class Token_ : public Heap, public ZmObject {
  friend struct Zjrpc::CompletionEntry;
public:
  Token_(Zjrpc::ID id_, uint64_t generation_) :
    m_id{ZuMv(id_)}, m_generation{generation_} { }
  const Zjrpc::ID &id() const { return m_id; }
  uint64_t generation() const { return m_generation; }
  bool live() const { return m_live; }
  bool cancelled() const { return m_cancelled; }

private:
  void invalidate_() { m_live = false; }
  void cancel_(ZuCSpan) { m_cancelled = true; }

  Zjrpc::ID m_id;
  uint64_t m_generation;
  bool m_live = true;
  bool m_cancelled = false;
};

using TokenHeap = ZmHeap<"Zjrpc.Test.Token", Token_<>>;

using Token = Token_<TokenHeap>;

struct Slot : public Zjrpc::CompletionSlot {
  ZmRef<Token> make(Zjrpc::ID id) {
    if (!available(id)) return {};
    ZmRef<Token> token = new Token{ZuMv(id), generation()};
    attach(token);
    return token;
  }
  bool complete(Token *token) {
    auto removed = take(token);
    return removed.object && !token->live() && !count();
  }
};

static void completionTest()
{
  ZuTestScope(completion);
  Slot slot;
  Zjrpc::ID id{int64_t{9}};
  auto token = slot.make(id);
  ZuCheck(token && token->live());
  ZuCheck(!slot.make(Zjrpc::ID{int64_t{10}}));
  ZuCheck(slot.cancel(id));
  ZuCheck(token->cancelled() && token->live());
  ZuCheck(slot.complete(token));
  ZuCheck(!slot.complete(token));
  auto next = slot.make(id);
  ZuCheck(bool(next));
  ZuCheck(!slot.complete(token));
  ZuCheck(slot.close(1));
  ZuCheck(!next->live());
  ZuCheck(!slot.make(id));

  // The slot must retain a string ID even after the caller releases its token.
  Slot retained;
  Zjrpc::ID stringID{Zjrpc::IDString{"retained-completion"}};
  auto owned = retained.make(stringID);
  owned = nullptr;
  ZuCheck(retained.cancel(stringID));
  ZuCheck(retained.fail(stringID));
  ZuCheck(!retained.count());
}

struct Responder : public Zjrpc::CompletionSet<Responder, Responder> {
  using Base = Zjrpc::CompletionSet<Responder, Responder>;
  using Base::complete;
  using Base::cancelled;

  Zjrpc::CompletionRoute route;
  unsigned completed = 0;
  unsigned cancellations = 0;
  int value = 0;
  bool removed = false;

  Zjrpc::CompletionRoute completionRoute_() const { return route; }

  template <typename Req, typename T>
  void made(Req *, T *) { }

  template <typename T, typename Res>
  bool completion(T *token, Zjrpc::Reply<Res> reply) {
    return this->complete_(token, ZuMv(reply));
  }
  void complete(const Zjrpc::ID &, Zjrpc::Reply<OK> reply) {
    removed = !count();
    value = reply.body.value;
    ++completed;
  }
  template <typename Req, typename T>
  void cancelled(Req *, T *token, ZuCSpan) {
    if (token->cancelled()) ++cancellations;
  }
};

static void typedCompletionTest()
{
  ZuTestScope(typedCompletion);
  Responder responder;
  auto token = responder.make<Echo>(Zjrpc::ID{int64_t{1}});
  ZuCheck(token && token->live());
  ZuCheck(responder.cancel(token->id()));
  ZuCheck(responder.cancel(token->id()));
  ZuCheck(responder.cancellations == 1);
  ZuCheck(token->cancelled() && token->live());
  ZuCheck(token->complete(Zjrpc::Reply<OK>{Body{7}}));
  ZuCheck(!token->live());
  ZuCheck(responder.removed && responder.completed == 1 && responder.value == 7);
  ZuCheck(!token->complete(Zjrpc::Reply<OK>{Body{9}}));
  auto next = responder.make<Echo>(Zjrpc::ID{int64_t{2}});
  ZuCheck(bool(next));
  ZuCheck(responder.close(1));
  ZuCheck(!next->live());
  ZuCheck(!next->complete(Zjrpc::Reply<OK>{Body{10}}));
  ZuCheck(responder.completed == 1);
}

template <typename Heap = ZuVoid>
struct DeferredResponder_ : public Heap, public Responder { };
using DeferredRespHeap = ZmHeap<"Zjrpc.Test.Responder", DeferredResponder_<>>;
using DeferredResponder = DeferredResponder_<DeferredRespHeap>;

static void deferredCloseTest()
{
  ZuTestScope(deferredClose);
  ZmScheduler scheduler{ZmSchedParams{}.nThreads(1)
    .thread(1, [](auto &thread) { thread.isolated(1); })};
  auto responder = new DeferredResponder{};
  responder->route = {&scheduler, 1};
  auto token = responder->make<Echo>(Zjrpc::ID{int64_t{1}});
  // Queue before starting the worker so close/destruction precedes the already
  // accepted completion deterministically, without blocking a scheduler thread.
  scheduler.run([responder]() {
    responder->close(1);
    delete responder;
  }, 1);
  ZuCheck(token->complete(Zjrpc::Reply<OK>{Body{7}}));
  ZmSemaphore drained;
  scheduler.run([&drained]() { drained.post(); }, 1);
  ZuCheck(scheduler.start(), (return));
  drained.wait();
  ZuCheck(!token->live());
  ZuCheck(!token->complete(Zjrpc::Reply<OK>{Body{8}}));
  ZuCheck(scheduler.stop());
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(pendingTest);
  ZuTestCall(inboundTest);
  ZuTestCall(completionTest);
  ZuTestCall(typedCompletionTest);
  ZuTestCall(deferredCloseTest);
  return 0;
}
