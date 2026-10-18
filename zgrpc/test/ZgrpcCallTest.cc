//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZgrpcCall.hh>
#include <zlib/ZgrpcEndpoint.hh>

using namespace ZuTestUtil;
using namespace Zgrpc;

struct Item { int value; };
ZfStruct(, (Item, PB), (value, (Ctor<0>, PB::FieldID<1>), Int32));
Zpb::AsMessage<"test.Item"> Zpb_Fmt(Item *);
struct Unary : Method {
  using Request = Item;
  using Response = Item;
};
struct Streaming : Unary { enum { Kind = Zgrpc::Kind::Duplex }; };

struct RxOwner {
  unsigned completions = 0;
  unsigned cancellations = 0;
  bool identity = true;
  int value = 0;
};
struct TxOwner {
  Direction output{true};
  Bytes order;
  unsigned sent = 0;
  unsigned finished = 0;
  unsigned ended = 0;
  bool identity = true;
};
using Ops = CallOps<Item, Item>;
static const Ops ops{
  [](void *ptr, uint64_t id, uint64_t gen, Message<Item> message) {
    auto tx = static_cast<TxOwner *>(ptr);
    tx->identity &= id == 7 && gen == 9;
    if (!tx->output.message()) return false;
    ++tx->sent;
    tx->order.push(message.object().value);
    return true;
  },
  [](void *ptr, uint64_t id, uint64_t gen) {
    auto tx = static_cast<TxOwner *>(ptr);
    tx->identity &= id == 7 && gen == 9;
    ++tx->ended;
    return tx->output.end();
  },
  [](void *ptr, uint64_t id, uint64_t gen, FinishData) {
    auto tx = static_cast<TxOwner *>(ptr);
    tx->identity &= id == 7 && gen == 9;
    ++tx->finished;
    tx->order.push(99);
    return tx->output.end();
  },
  [](void *ptr, uint64_t id, uint64_t gen, Reply<Item> reply) {
    auto rx = static_cast<RxOwner *>(ptr);
    rx->identity &= id == 7 && gen == 9;
    ++rx->completions;
    if (reply.message.is<Message<Item>>())
      rx->value = reply.message.p<Message<Item>>().object().value;
    return true;
  },
  [](void *ptr, uint64_t id, uint64_t gen) {
    auto rx = static_cast<RxOwner *>(ptr);
    rx->identity &= id == 7 && gen == 9;
    ++rx->cancellations;
  }
};

template <typename T, typename = void> struct HasSend : ZuFalse { };
template <typename T> struct HasSend<T,
  decltype(ZuDeclVal<T &>().send(Message<Item>{Item{1}}), void())> : ZuTrue { };
template <typename T, typename = void> struct HasEnd : ZuFalse { };
template <typename T> struct HasEnd<T,
  decltype(ZuDeclVal<T &>().end(), void())> : ZuTrue { };
template <typename T, typename = void> struct HasComplete : ZuFalse { };
template <typename T> struct HasComplete<T,
  decltype(ZuDeclVal<T &>().complete(Reply<Item>{}), void())> : ZuTrue { };
ZuAssert((HasSend<ClientCall<Unary>>{}));
ZuAssert((!HasSend<ServerCall<Unary>>{}));
ZuAssert((HasSend<ServerCall<Streaming>>{}));
ZuAssert((HasEnd<ClientCall<Streaming>>{}));
ZuAssert((!HasEnd<ServerCall<Streaming>>{}));
ZuAssert((HasComplete<ServerCall<Unary>>{}));
ZuAssert((!HasComplete<ClientCall<Unary>>{}));
ZuAssert((!HasComplete<ServerCall<Streaming>>{}));

static void cardinality()
{
  ZuTestScope(cardinality);
  Direction req;
  ZuCheck(req.message());
  ZuCheck(!req.message());
  ZuCheck(req.end());
  ZuCheck(!req.message() && !req.end());
  Direction empty;
  ZuCheck(!empty.end());
  Direction error;
  ZuCheck(error.end(false));
  Direction stream{true};
  for (unsigned i = 0; i < 100; i++) ZuCheck(stream.message());
  ZuCheck(stream.count() == 100 && stream.end());
  Direction noMessages{true};
  ZuCheck(noMessages.end());
}

static void terminalClaims()
{
  ZuTestScope(terminalClaims);
  RxOwner rx;
  TxOwner tx;
  ZmRef<ServerCall<Unary>> call = new ServerCall<Unary>{7, 9, {}, &rx, {}, &tx, 2, ops};
  ZuCheck(call->live() && call->reply(Item{8}));
  ZuCheck(call->live()); // command admission does not claim the Rx terminal decision
  ZuCheck(!call->reply(Item{9}) && !call->finish());
  ZuCheck(rx.completions == 1 && rx.value == 8 && rx.identity);
  ZuCheck(call->cancel() && !call->cancel());
  ZuCheck(rx.cancellations == 1);
  call->invalidateRx_(true);
  call->invalidateTx_();
  call->invalidateRx_();
  ZuCheck(!call->live() && call->cancelled());
  ZuCheck(!call->cancel() && !call->finish());
  ZmRef<ClientCall<Unary>> client = new ClientCall<Unary>{7, 9, {}, &rx, {}, &tx, 2, ops};
  ZuCheck(client->send(Item{1}));
  ZuCheck(client->end() && !client->end());
  client->invalidateRx_();
  client->invalidateTx_();
  ZuCheck(!client->send(Item{2}));
}

static void posts()
{
  ZuTestScope(posts);
  ZmScheduler scheduler{ZmSchedParams{}.nThreads(1)
    .thread(1, [](auto &thread) { thread.isolated(1); })};
  RxOwner rx;
  TxOwner tx;
  ZmRef<ClientCall<Streaming>> call =
    new ClientCall<Streaming>{7, 9, {}, &rx, {&scheduler, 1}, &tx, 2, ops};
  ZmAtomic<unsigned> admitted = 0;
  ZmSemaphore ready, go;
  auto submit = [call, &admitted, &ready, &go]() {
    ready.post();
    go.wait();
    for (unsigned i = 0; i < 128; i++)
      if (call->send(Item{1})) ++admitted;
  };
  // Unstarted Tx is a deterministic barrier: both workers submit while no
  // accepted command can run or decrement its pending-post count.
  ZmThread a{submit}, b{submit};
  ready.wait(); ready.wait();
  go.post(); go.post();
  a.join(); b.join();
  ZuCheck(admitted.load_() == 2);
  ZmSemaphore drained;
  scheduler.run([&drained]() { drained.post(); }, 1);
  ZuCheck(scheduler.start(), (return));
  drained.wait();
  ZuCheck(tx.sent == 2 && tx.identity);
  ZuCheck(call->send(Item{2}));
  scheduler.run([&drained]() { drained.post(); }, 1);
  drained.wait();
  ZuCheck(tx.sent == 3);
  // On-Tx sends bypass pending-post accounting and return the actual result.
  bool direct = false;
  scheduler.run([call, &direct, &drained]() {
    direct = call->send(Item{3});
    call->invalidateTx_();
    drained.post();
  }, 1);
  drained.wait();
  ZuCheck(direct && tx.sent == 4);
  call->invalidateRx_();
  ZuCheck(!call->send(Item{4}));
  ZuCheck(scheduler.stop());
}

static void orderedFinish()
{
  ZuTestScope(orderedFinish);
  ZmScheduler scheduler{ZmSchedParams{}.nThreads(1)
    .thread(1, [](auto &thread) { thread.isolated(1); })};
  RxOwner rx;
  TxOwner tx;
  ZmRef<ServerCall<Streaming>> call =
    new ServerCall<Streaming>{7, 9, {}, &rx, {&scheduler, 1}, &tx, 3, ops};
  ZuCheck(call->send(Item{1}) && call->send(Item{2}));
  ZuCheck(call->finish() && call->live());
  for (unsigned i = 0; i < 100; i++) ZuCheck(!call->finish());
  // Admission behind finish can succeed; Tx later rejects its production.
  ZuCheck(call->send(Item{3}));
  ZmSemaphore drained;
  scheduler.run([call, &drained]() { call->invalidateTx_(); drained.post(); }, 1);
  ZuCheck(scheduler.start(), (return));
  drained.wait();
  const uint8_t order[] = {1, 2, 99};
  ZuCheck(tx.sent == 2 && tx.finished == 1 && ZuBSpan{tx.order} == ZuBSpan{order});
  call->invalidateRx_();
  ZuCheck(scheduler.stop());
}

static void invalidatedPosts()
{
  ZuTestScope(invalidatedPosts);
  ZmScheduler scheduler{ZmSchedParams{}.nThreads(1)
    .thread(1, [](auto &thread) { thread.isolated(1); })};
  RxOwner rx;
  TxOwner tx;
  ZmRef<ClientCall<Streaming>> call =
    new ClientCall<Streaming>{7, 9, {}, &rx, {&scheduler, 1}, &tx, 2, ops};
  scheduler.run([call]() { call->invalidateTx_(); }, 1);
  ZuCheck(call->send(Item{1}) && call->send(Item{2}));
  ZuCheck(!call->send(Item{3}));
  ZmSemaphore drained;
  scheduler.run([&drained]() { drained.post(); }, 1);
  ZuCheck(scheduler.start(), (return));
  drained.wait();
  ZuCheck(!tx.sent);
  // Every discarded command released its slot. Keep the scheduler stopped
  // so the second capacity assertion is exact.
  ZuCheck(scheduler.stop());
  ZuCheck(call->send(Item{1}) && call->send(Item{2}));
  ZuCheck(!call->send(Item{3}));
  scheduler.run([&drained]() { drained.post(); }, 1);
  ZuCheck(scheduler.start(), (return));
  drained.wait();
  ZuCheck(!tx.sent);
  call->invalidateRx_(true);
  ZuCheck(!call->send(Item{4}) && !call->end());
  ZuCheck(scheduler.stop());
}

struct HeapRx : ZmNHeap<HeapRx>, RxOwner {
friend inline ZuStringT<"Zgrpc.Test.Rx"> ZmNHeapID(HeapRx *);
};
struct HeapTx : ZmNHeap<HeapTx>, TxOwner {
friend inline ZuStringT<"Zgrpc.Test.Tx"> ZmNHeapID(HeapTx *);
};

static void destroyedOwners()
{
  ZuTestScope(destroyedOwners);
  ZmScheduler scheduler{ZmSchedParams{}.nThreads(2)
    .thread(1, [](auto &thread) { thread.isolated(1); })
    .thread(2, [](auto &thread) { thread.isolated(1); })};
  auto rx = new HeapRx{};
  auto tx = new HeapTx{};
  CallRoute rxRoute{&scheduler, 1}, txRoute{&scheduler, 2};
  ZmRef<ClientCall<Streaming>> client =
    new ClientCall<Streaming>{7, 9, rxRoute, rx, txRoute, tx, 2, ops};
  ZmRef<ServerCall<Unary>> server =
    new ServerCall<Unary>{7, 9, rxRoute, rx, txRoute, tx, 2, ops};
  scheduler.run([rx, client, server]() {
    client->invalidateRx_(true);
    server->invalidateRx_(true);
    delete rx;
  }, 1);
  scheduler.run([tx, client, server]() {
    client->invalidateTx_();
    server->invalidateTx_();
    delete tx;
  }, 2);
  ZuCheck(client->send(Item{1}) && client->send(Item{2}));
  ZuCheck(client->end() && client->cancel());
  ZuCheck(server->complete(Reply<Item>{Item{3}}));
  ZuCheck(!server->complete(Reply<Item>{Item{4}}) && !server->finish());
  ZmSemaphore rxDone, txDone;
  scheduler.run([&rxDone]() { rxDone.post(); }, 1);
  scheduler.run([&txDone]() { txDone.post(); }, 2);
  ZuCheck(scheduler.start(), (return));
  rxDone.wait(); txDone.wait();
  ZuCheck(!client->live() && client->cancelled() && !server->live());
  ZuCheck(!client->send(Item{5}) && !client->cancel() && !server->reply(Item{6}));
  ZuCheck(scheduler.stop());
}

struct CancelState {
  EndpointCalls calls;
  ZtArray<ZmRef<WorkEntry>, ZtArrayHeapID<"Zgrpc.Test.Retired">> retired;
  ZmSemaphore done;
  unsigned cancelled = 0, errors = 0;
};
struct CancelWork : WorkEntry, ZmNHeap<CancelWork> {
friend inline ZuStringT<"Zgrpc.Test.CancelWork"> ZmNHeapID(CancelWork *);
  CancelState *state;
  explicit CancelWork(CancelState *value) : state{value} {
    cancelFn = [](WorkEntry *ptr, Code::T code) {
      auto self = static_cast<CancelWork *>(ptr);
      auto state = self->state;
      if (!state->calls.rxInvoked() || code != Code::CANCELLED) ++state->errors;
      if (!state->cancelled++) {
        // Retire the next batch's cursor and other already-terminal entries
        // while cancellation of the current batch is still in progress.
        for (auto &work : state->retired) state->calls.release(work.ptr());
      }
      self->terminal = true;
      state->calls.release(self);
    };
  }
};
static void endpointDrain()
{
  ZuTestScope(endpointDrain);
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(2).thread(1, [](auto &t) { t.isolated(1); })
      .thread(2, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  CancelState state;
  Limits limits;
  limits.maxPending = 16;
  limits.workBatch = 2;
  ZuCheck(state.calls.init(Zhttp::HubConfig{&mx, "1", "2"}, limits,
    EndpointCalls::StartFn{[](EndpointCalls::DoneFn done) { done(true); }},
    EndpointCalls::StopFn{[](EndpointCalls::DoneFn done) { done(true); }}));
  ZuCheck(mx.start(), (return));
  ZuCheck(state.calls.start());
  mx.run([&state]() {
    for (unsigned i = 0; i < 16; ++i) {
      if (!state.calls.reserve()) { ++state.errors; continue; }
      ZmRef<CancelWork> work = new CancelWork{&state};
      if (i >= 2 && i % 3 == 2) {
        work->terminal = true;
        state.retired.push(work);
      }
      state.calls.insert(ZuMv(work));
    }
    state.calls.stop(EndpointCalls::DoneFn{[&state](bool ok) {
      if (!ok || state.calls.up() || state.calls.pending()) ++state.errors;
      state.done.post();
    }});
  }, 1);
  ZuCheck(state.done.timedwait(Zm::now(10)) == 0);
  ZuCheck(state.cancelled == 11 && !state.errors && !state.calls.pending());
  state.calls.stop(EndpointCalls::DoneFn{[&state](bool ok) {
    if (!ok) ++state.errors;
    state.done.post();
  }});
  ZuCheck(state.done.timedwait(Zm::now(10)) == 0 && !state.errors);
  state.calls.final();
  ZuCheck(mx.stop());
}

struct LifeState {
  EndpointCalls calls;
  EndpointCalls::DoneFn httpStart;
  ZmSemaphore entered, started, stopped, fence;
  unsigned httpStarts = 0, httpStops = 0, starts = 0, stops = 0, errors = 0;
  bool startOK = false;
  auto startDone() {
    return EndpointCalls::DoneFn{[this](bool ok) {
      if (!calls.rxInvoked()) ++errors;
      startOK = ok;
      ++starts;
      started.post();
    }};
  }
  auto stopDone() {
    return EndpointCalls::DoneFn{[this](bool ok) {
      if (!ok || !calls.rxInvoked() || !starts || calls.up() || calls.pending()) ++errors;
      ++stops;
      stopped.post();
    }};
  }
};
static void startupStop(bool ok)
{
  ZuTestScope(startupStop);
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(2).thread(1, [](auto &t) { t.isolated(1); })
      .thread(2, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  LifeState state;
  ZuCheck(state.calls.init(Zhttp::HubConfig{&mx, "1", "2"}, {},
    EndpointCalls::StartFn{[&state](EndpointCalls::DoneFn done) {
      ++state.httpStarts;
      state.httpStart = ZuMv(done);
      state.entered.post();
    }}, EndpointCalls::StopFn{[&state](EndpointCalls::DoneFn done) {
      ++state.httpStops;
      done(true);
    }}));
  ZuCheck(mx.start(), (return));
  state.calls.start(state.startDone());
  ZuCheck(state.entered.timedwait(Zm::now(10)) == 0);
  state.calls.stop(state.stopDone());
  state.calls.stop(state.stopDone());
  mx.run([&state]() { state.fence.post(); }, 1);
  ZuCheck(state.fence.timedwait(Zm::now(10)) == 0);
  ZuCheck(!state.starts && !state.stops && !state.calls.up() && !state.calls.reserve());
  mx.run([&state, ok]() {
    auto done = ZuMv(state.httpStart);
    done(ok);
  }, 1);
  ZuCheck(state.started.timedwait(Zm::now(10)) == 0);
  ZuCheck(state.stopped.timedwait(Zm::now(10)) == 0);
  ZuCheck(state.stopped.timedwait(Zm::now(10)) == 0);
  ZuCheck(state.starts == 1 && !state.startOK && state.stops == 2 && !state.errors);
  ZuCheck(state.httpStarts == 1 && state.httpStops == unsigned(ok));

  // Stop another startup before its first Rx action. This also exercises
  // resetting a completed transport-stop cycle before a cancelled restart.
  state.starts = state.stops = 0;
  ZmSemaphore held, release;
  mx.run([&held, &release]() { held.post(); release.wait(); }, 1);
  ZuCheck(held.timedwait(Zm::now(10)) == 0);
  state.calls.start(state.startDone());
  state.calls.stop(state.stopDone());
  state.calls.stop(state.stopDone());
  release.post();
  ZuCheck(state.started.timedwait(Zm::now(10)) == 0);
  ZuCheck(state.stopped.timedwait(Zm::now(10)) == 0);
  ZuCheck(state.stopped.timedwait(Zm::now(10)) == 0);
  ZuCheck(state.starts == 1 && !state.startOK && state.stops == 2 && !state.errors);
  ZuCheck(state.httpStarts == 1 && state.httpStops == unsigned(ok));

  // A fully drained stop permits a fresh successful start and admission.
  state.starts = state.stops = 0;
  state.calls.start(state.startDone());
  ZuCheck(state.entered.timedwait(Zm::now(10)) == 0);
  mx.run([&state]() { auto done = ZuMv(state.httpStart); done(true); }, 1);
  ZuCheck(state.started.timedwait(Zm::now(10)) == 0);
  ZuCheck(state.startOK && state.starts == 1 && state.calls.up());
  state.calls.stop(state.stopDone());
  ZuCheck(state.stopped.timedwait(Zm::now(10)) == 0);
  ZuCheck(state.stops == 1 && state.httpStarts == 2 && state.httpStops == unsigned(ok) + 1 && !state.errors);
  state.calls.final();
  ZuCheck(mx.stop());
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(cardinality);
  ZuTestCall(terminalClaims);
  ZuTestCall(posts);
  ZuTestCall(orderedFinish);
  ZuTestCall(invalidatedPosts);
  ZuTestCall(destroyedOwners);
  ZuTestCall(endpointDrain);
  ZuTestCall(startupStop, true);
  ZuTestCall(startupStop, false);
}
