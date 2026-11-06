//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmSingleton.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZiTx.hh>
#include <zlib/ZfixRaw.hh>
#include <zlib/ZfixRxMsg.hh>

static ZmRef<ZiIOBuf> frameBuf() {
  using Allocator = Zi::AppBufAllocator<ZiTxBuf, ZiIOBuf_DefltSize>;
  return ZmSingleton<Allocator>::instance()->allocBuf();
}

using namespace ZuTestUtil;
using namespace Zfix;
struct Value {
  ZuCSpan	id;
};

ZfStruct(, (Value, FIX), (id, (FIX::ID<11>), String));
ZfStructImpl(Value); ZfStructImpl(Value, FIX);
using CustomLogon = MT<Value, "A">;
using Order = MT<Value, "D">;
struct RoutingValue {
  ZuCSpan id;
};
ZfStruct(, (RoutingValue), (id, (FIX::ID<11>), String));
using RoutingFIXFields = ZuTypeConcat<SessionFields<RoutingValue>, ZfFields_RoutingValue>;
RoutingFIXFields ZuFields_(RoutingValue *, ZuFacet::FIX *);
ZfStructImpl(RoutingValue); ZfStructImpl(RoutingValue, FIX);
using RoutingLogon = MT<RoutingValue, "A">;
constexpr auto Wire = "8=FIX.4.4\1" "9=12\1" "35=D\1" "11=abc\1" "10=171\1"_z;
static auto buffer(ZuCSpan bytes) {
  auto b = frameBuf();
  b->append(bytes); return b;
}
static void controls() {
  ZuTestScope(controls);
  Fmt::Error error;
  ZuCheck(!TxBody::encode<CustomLogon>(Value{"abc"}, 42,
    error) && error.code == Fmt::ErrorCode::Reserved);
  auto body = TxBody::encodeRaw<CustomLogon>(Value{"abc"}, 42, true, error);
  ZuCheck(body && !error && body->msgType() == "A" && body->administrative() && body->id() == 42);
  auto restored = TxBody::restoreRaw<CustomLogon>("9999=private\1" "11=abc\1", 77, false, error);
  ZuCheck(restored && !error && !restored->administrative() && restored->id() == 77);
  ZuCheck(restored->body() == "9999=private\1" "11=abc\1"_z);
}
static void routingFields() {
  ZuTestScope(routingFields);
  Fmt::Error error;
  auto body = TxBody::encodeRaw<RoutingLogon>(RoutingValue{"abc"}, 51, true, error);
  TxConfig cfg; cfg.beginString = "FIX.9.9";
  cfg.identity.local = "EXTERNAL"; cfg.identity.peer = "PEER";
  TxContext ctx{.connection = 5, .seqNo = 17,
    .sendingTime = ZuDateTime{ZuDateTimeScan::FIX{}, "20000101-00:00:00"}};
  auto out = RawOutput::encode(frameBuf(), body, cfg, ctx, error);
  ZuCheck(out && !error && out->context().epoch == 0 && out->context().seqNo == 17);
  auto parsed = Fmt::frame(ZuCSpan{out->frame()->cspan()});
  RxMsg got; got.frame = parsed.view;
  ZuCheck(!catalogSession<MTList<RoutingLogon>>(got) && got.seqNo == 17 &&
    parsed.view.beginString == "FIX.9.9");
  ZuCheck(got.sender == "EXTERNAL" && got.sendingTime == ctx.sendingTime
    && parsed.view.msgType == "A");
  ctx.seqNo = 2; ctx.replay = true; ctx.possDup = true; ctx.origSendingTime = ctx.sendingTime;
  auto low = RawOutput::encode(frameBuf(), body, cfg, ctx, error);
  ZuCheck(low && !error && low->originalTime() == ctx.origSendingTime && low->context().replay);
}
static void opaque() {
  ZuTestScope(opaque);
  Fmt::Error error; auto body = TxBody::encode<Order>(Value{"abc"}, 23, error);
  auto bytes = buffer(Wire);
  auto out = RawOutput::fromFrame(body, {0, 1, 0}, bytes, error);
  ZuCheck(out && !error && out->frame() == bytes && out->body() == body);
  ZuCheck(out->context().epoch == 0 && out->context().seqNo == 0 && !*out->originalTime());
  auto wrong = TxBody::encodeRaw<CustomLogon>(Value{"abc"}, 24, true, error);
  ZuCheck(!RawOutput::fromFrame(wrong, {0, 1, 0}, bytes, error) && error.tag == 35);
  auto two = buffer(Wire); two->append(Wire);
  ZuCheck(!RawOutput::fromFrame(body, {0, 1, 0}, two,
    error) && error.code == Fmt::ErrorCode::Envelope);
  auto shortFrame = buffer({Wire.data(), Wire.length() - 1});
  ZuCheck(!RawOutput::fromFrame(body, {0, 1, 0}, shortFrame,
    error) && error.code == Fmt::ErrorCode::Truncated);
  bytes->data()[bytes->length - 2] = '2';
  ZuCheck(!RawOutput::fromFrame(body, {0, 1, 0}, bytes,
    error) && error.code == Fmt::ErrorCode::Checksum);
}
static void limits() {
  ZuTestScope(limits);
  Fmt::Error error; auto body = TxBody::encode<Order>(Value{"abc"}, 1, error);
  Fmt::Limits l; l.frameBytes = Wire.length() - 1;
  ZuCheck(!RawOutput::fromFrame(body, {0, 1, 0}, buffer(Wire), error,
    l) && error.code == Fmt::ErrorCode::Size);
  ZuCheck(!RawOutput::fromFrame({}, {}, buffer(Wire),
    error) && error.code == Fmt::ErrorCode::Output);
}

template <typename T> using Items = ZtArray<T, ZtArrayHeapID<"Zfix.RawTest">>;
struct Task {
  ZmFn<>	fn;
  bool		rx;
};

struct Owner {
  using Queue = ZmList<Task, ZmListHeapID<"Zfix.RawTest.Tasks">>;
  Queue		queue;
  unsigned	destroyed = 0;
  bool		rx = true;

  bool rxInvoked() const { return rx; }
  bool txInvoked() const { return !rx; }

  template <typename Fn> void rxRun(Fn &&fn) {
    queue.push(Task{ZmFn<>{ZuFwd<Fn>(fn)}, true});
  }

  template <typename Fn> void txRun(Fn &&fn) {
    queue.push(Task{ZmFn<>{ZuFwd<Fn>(fn)}, false});
  }

  void drain() {
    while (auto task = queue.shift()) {
      rx = task->data().rx;
      task->data().fn();
    }
    rx = true;
  }
};

struct Endpoint : ZmNHeap<Endpoint>, ZmObject, Raw<Endpoint> {
friend inline ZuStringT<"Zfix.RawTest.Endpoint"> ZmNHeapID(Endpoint *);
  struct Stop {
    uint64_t	connection;
    RawStopFn	fn;
  };

  struct Record {
    ZmRef<ZiIOBuf>	buf;
  };

  Owner		*owner;
  Items<Stop>		rxStops, txStops;
  Items<Record>		records;
  Items<uint64_t>	up, writable, stopped;
  unsigned		completed = 0, faults = 0, closes = 0;
  bool			deferred = true;

  static SessionConfig config() {
    SessionConfig c; c.mode = Transport::Raw; return c;
  }

  explicit Endpoint(Owner *p) : Raw{config()}, owner{p} { }

  ~Endpoint() { ++owner->destroyed; }

  Owner *app() const { return owner; }
  PostedState txPosted_() const { return {}; }
  WorkState workState_() const { return {}; }
  WireGeometry fixGeometryTx() const { return {}; }

  ZmRef<ZiIOBuf> fixAllocTx(unsigned head) {
    ZmRef<ZiIOBuf> b = ZmSingleton<Zi::AppBufAllocator<
      ZiTxBuf, ZiIOBuf_DefltSize>>::instance()->allocBuf();
    b->skip = head; return b;
  }

  bool fixSendTx(ZmRef<ZiIOBuf> b) { records.push(Record{ZuMv(b)}); return true; }
  void disconnect_(bool = true) { ++closes; }
  void resumeRx_() { }
  void fixRawUpRx(uint64_t c) { up.push(c); }
  void fixRawWritableTx(uint64_t c) { writable.push(c); }
  void fixRawFaultTx(uint64_t, Fmt::Error) { ++faults; }
  void fixRawCompleteTx(TxBody::Ref, TxContext, bool) { ++completed; }

  void fixRawStopRx(uint64_t c, RawStopRequest &request) {
    if (deferred) rxStops.push(Stop{c, request.defer()});
  }

  void fixRawStopTx(uint64_t c, RawStopRequest &request) {
    if (deferred) txStops.push(Stop{c, request.defer()});
  }

  void fixRawStoppedRx(uint64_t c, bool) { stopped.push(c); }

  unsigned send(uint64_t c) {
    Fmt::Error error; auto body = TxBody::encode<Order>(Value{"abc"}, 23, error);
    auto out = RawOutput::fromFrame(body, {0, c, 0}, buffer(Wire), error);
    owner->rx = false;
    unsigned result = sendRaw_(out);
    owner->rx = true;
    return result;
  }

  void release(unsigned i) {
    rxStops[i].fn(); rxStops[i].fn();
    txStops[i].fn(); txStops[i].fn();
    owner->drain();
  }

  void clearReplies() { rxStops.clear(); txStops.clear(); records.clear(); }
};

static void replacement() {
  ZuTestScope(replacement);
  Owner app; ZmRef<Endpoint> link = new Endpoint{&app};
  ZuCheck(link->transportBind_(false)); link->transportUp_(); app.drain();
  ZuCheck(link->up.length() == 1 && link->up[0] == 1);
  ZuCheck(link->send(1) == RawSend::Accepted);
  // A's queued failed-output completion runs after B opens. It must still
  // complete A's admission, without faulting or waking the replacement.
  ZuCheck(link->transportBind_(true)); link->transportUp_(); app.drain();
  ZuCheck(link->rawConnection_() == 2 && link->completed == 1 &&
    !link->faults && !link->closes && !link->stopped.length());
  ZuCheck(link->writable.length() == 2 && link->writable[1] == 2);
  ZuCheck(link->rxStops.length() == 1 && link->rxStops[0].connection == 1 &&
    link->txStops.length() == 1 && link->txStops[0].connection == 1);
  ZuCheck(link->send(1) == RawSend::Held && link->send(2) == RawSend::Accepted);
  // C supersedes B before authentication. A/B's asynchronous manager drains
  // remain outstanding; neither their replies nor old record callbacks open C.
  ZuCheck(link->transportBind_(true)); app.drain();
  ZuCheck(link->rawConnection_() == 3 && link->completed == 2 &&
    link->up.length() == 2 && link->send(3) == RawSend::Held);
  link->release(0);
  for (const auto &record : link->records)
    link->transportSent(record.buf, false);
  app.drain();
  ZuCheck(!link->faults && !link->closes && !link->stopped.length());
  link->transportUp_(); app.drain();
  ZuCheck(link->up.length() == 3 && link->up[2] == 3);
  link->transportDown_(false); app.drain();
  link->release(2);
  ZuCheck(!link->stopped.length());
  link->release(1);
  ZuCheck(link->stopped.length() == 1 && link->stopped[0] == 3 &&
    !link->faults && !link->closes);
  link->clearReplies(); link = nullptr;
  ZuCheck(app.destroyed == 1 && !app.queue.count_());
}

static void setupStops(bool deferred) {
  ZuTestScope(setupStops);
  Owner app; ZmRef<Endpoint> link = new Endpoint{&app};
  link->deferred = deferred;
  for (unsigned i = 0; i < 3; ++i) {
    link->transportDown_(false); link->transportDown_(false); app.drain();
    if (deferred) {
      ZuCheck(link->rxStops.length() == i + 1 && link->txStops.length() == i + 1 &&
	link->rxStops[i].connection == 0 && link->stopped.length() == i);
      link->release(i);
    } else ZuCheck(!link->rxStops.length() && !link->txStops.length());
    ZuCheck(link->stopped.length() == i + 1 && link->stopped[i] == 0);
  }
  link->clearReplies(); link = nullptr;
  ZuCheck(app.destroyed == 1);
}

static void preauthFailure() {
  ZuTestScope(preauthFailure);
  Owner app; ZmRef<Endpoint> link = new Endpoint{&app};
  ZuCheck(link->transportBind_(false)); link->transportUp_(); app.drain();
  ZuCheck(link->transportBind_(true)); app.drain();
  link->transportDown_(false); app.drain();
  ZuCheck(link->up.length() == 1 && !link->stopped.length());
  link->release(1); link->release(0);
  ZuCheck(link->stopped.length() == 1 && link->stopped[0] == 2 &&
    link->send(1) == RawSend::Held && link->send(2) == RawSend::Held);
  link->clearReplies(); link = nullptr;
  ZuCheck(app.destroyed == 1);
}

int main(int argc, char **argv) {
  parse(argc, argv); ZuTestMain();
  ZuTestCall(controls);
  ZuTestCall(routingFields);
  ZuTestCall(opaque);
  ZuTestCall(limits);
  ZuTestCall(replacement);
  ZuTestCall(setupStops, false);
  ZuTestCall(setupStops, true);
  ZuTestCall(preauthFailure);
  return 0;
}
