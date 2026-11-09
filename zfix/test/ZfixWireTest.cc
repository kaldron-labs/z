//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmSingleton.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZiTx.hh>
#include <zlib/ZfixLinkTx.hh>
#include <zlib/ZfixLink.hh>
#include <zlib/ZfixWire.hh>

static ZmRef<ZiIOBuf> frameBuf() {
  using Allocator = Zi::IOBufBuiltinAllocator<Zi::IOBufType::App, ZiTxBuf, ZiIOBuf_DefltSize>;
  return ZmSingleton<Allocator>::instance()->allocBuf();
}

using namespace ZuTestUtil;
using namespace Zfix;
template <typename T> using Items = ZtArray<T, ZtArrayHeapID<"Zfix.WireTest">>;
struct Order {
  ZuCSpan	id;
};

ZfStruct(, (Order), (id, (FIX::ID<11>), String));
using OrderFIXFields = ZuTypeConcat<SessionFields<Order>, ZfFields_Order>;
OrderFIXFields ZuFields_(Order *, ZuFacet::FIX *);
ZfStructImpl(Order); ZfStructImpl(Order, FIX);
using Entry = MT<Order, "D">;
constexpr auto Wire = "8=FIX.4.4\1" "9=12\1" "35=D\1" "11=abc\1" "10=171\1"_z;
static TxBody::Ref body(bool admin = false) {
  Fmt::Error error;
  if (!admin) return TxBody::encode<Entry>(Order{"abc"}, 99, error);
  Session::Msg c; c.p<Session::HeartBt>(Session::HeartBt{}); return TxBody::control(c, error);
}
static auto frame() {
  auto buf = frameBuf();
  buf->append(Wire); return buf;
}
struct Completion {
  TxBody::Ref	body;
  TxContext	context;
  bool		ok;
};

struct Harness : public WireTx<Harness> {
  using Alloc = Zi::IOBufBuiltin<ZiTxBuf, 64, ZiIOBuf_Align, ZiIOBuf_HeapID, ZiIOBuf_DefltMaxSize>;
  using Base = WireTx<Harness>;
  Items<ZmRef<ZiIOBuf>>	records;
  Items<Completion>	done;
  unsigned		refuseAt = 0, sends = 0;
  bool			refuseAlloc = false;
  bool			notice = false, early = false;

  explicit Harness(WireLimits limits = {}) : Base{limits} { }

  ZmRef<ZiIOBuf> recordAlloc(unsigned head) {
    if (refuseAlloc) return {};
    ZmRef<ZiIOBuf> buf = ZmSingleton<Zi::IOBufBuiltinAllocator<Zi::IOBufType::App, ZiTxBuf, 64>>::instance()->allocBuf();
    buf->skip = head; return buf;
  }

  bool recordSend(ZmRef<ZiIOBuf> buf) {
    if (!notice) early = true;
    if (++sends == refuseAt) return false;
    records.push(ZuMv(buf)); return true;
  }

  void outputDone(TxBody::Ref body, TxContext c, bool ok) {
    // Record the work an endpoint posts; don't call back into LinkTx here.
    done.push(Completion{ZuMv(body), c, ok});
  }

  void flush() { notice = true; flush_(); }

  WireGeometry geometry() const {
    // Five plaintext bytes per record exercise every partial completion.
    return {7, 1, 1, sizeof(Alloc) + Alloc::Size};
  }

  void finish(unsigned i, bool ok = true) {
    auto b = records[i]; record_(b.ptr(), ok);
  }
};

static void tcpFrame() {
  ZuTestScope(tcpFrame);
  Harness h; ZuCheck(h.open_(1));
  auto bytes = frame(); auto ptr = bytes->data(); auto native = body();
  ZuCheck(h.admit_(native, {1, 1, 1}, bytes));
  ZuCheck(!h.records.length() && !h.done.length());
  h.refuseAlloc = true; // TCP must send the admitted buffer without allocating another.
  h.flush();
  ZuCheck(h.records.length() == 1 && !h.early && h.count_() == 1);
  ZuCheck(h.records[0]->data() == ptr && h.records[0] == bytes);
  ZuCheck(!h.done.length()); h.finish(0);
  ZuCheck(h.done.length() == 1 && h.done[0].ok && h.done[0].body == native);
  ZuCheck(!h.count_() && !h.bytes_());
  h.finish(0); ZuCheck(h.done.length() == 1);
}
static void records() {
  ZuTestScope(records);
  Harness h; ZuCheck(h.open_(1, h.geometry()));
  ZuCheck(h.admit_(body(), {1, 1, 1}, frame())); h.flush();
  unsigned count = Wire.length() / 5 + !!(Wire.length() % 5);
  ZuCheck(h.records.length() == count);
  ZtString<ZtStringHeapID<"Zfix.WireTest">> joined;
  for (unsigned i = 0; i < count; ++i) {
    joined << h.records[i]->cspan();
    ZuCheck(h.records[i]->skip == 1 && h.records[i]->length <= 5);
    h.finish(i);
    ZuCheck(h.done.length() == (i + 1 == count ? 1 : 0));
  }
  ZuCheck(joined == Wire && h.done[0].ok && !h.count_() && !h.bytes_());
}
static void queued() {
  ZuTestScope(queued);
  Harness h; ZuCheck(h.open_(1));
  ZuCheck(h.admit_(body(), {1, 1, 1}, frame())); h.flush();
  ZuCheck(h.admit_(body(), {1, 1, 2}, frame())); h.flush();
  ZuCheck(h.records.length() == 2 && h.count_() == 2);
  h.finish(0);
  ZuCheck(h.records.length() == 2 && h.done.length() == 1);
  h.finish(1);
  ZuCheck(h.done.length() == 2 && h.done[1].context.seqNo == 2 && h.done[1].ok);
  // Identical replay contexts still use distinct output buffers.
  ZuCheck(h.admit_(body(), {1, 1, 1, true}, frame())); h.flush();
  h.finish(0); ZuCheck(h.done.length() == 2);
  h.finish(2); ZuCheck(h.done.length() == 3 && h.done[2].ok);
}
static void failures() {
  ZuTestScope(failures);
  Harness h; ZuCheck(h.open_(1, h.geometry()));
  ZuCheck(h.admit_(body(), {1, 1, 1}, frame())); h.flush();
  ZuCheck(h.admit_(body(), {1, 1, 2}, frame())); h.flush();
  h.finish(0, false);
  h.close_(1); // A native error closes its physical connection.
  ZuCheck(h.done.length() == 2 && !h.count_() && !h.bytes_());
  ZuCheck(!h.done[0].ok && !h.done[1].ok);
  for (unsigned i = 0, n = h.records.length(); i < n; ++i) h.finish(i);
  ZuCheck(h.done.length() == 2);
  ZuCheck(!h.admit_(body(), {1, 1, 3}, frame()));
  ZuCheck(h.open_(2));
  ZuCheck(h.admit_(body(), {1, 2, 3}, frame())); h.flush();
  h.close_(1); ZuCheck(h.count_() == 1);
  h.close_(2); ZuCheck(h.done.length() == 3 && !h.done[2].ok);
  h.close_(2); ZuCheck(h.done.length() == 3);
}
static void synchronous() {
  ZuTestScope(synchronous);
  for (unsigned at = 1; at <= 3; ++at) {
    Harness h; h.refuseAt = at;
    ZuCheck(h.open_(1, h.geometry()));
    ZuCheck(h.admit_(body(), {1, 1, 1}, frame())); h.flush();
    ZuCheck(h.failed_() && h.done.length() == 1 && !h.done[0].ok && !h.count_());
    for (unsigned i = 0, n = h.records.length(); i < n; ++i) h.finish(i);
    ZuCheck(h.done.length() == 1);
  }
  Harness h; h.refuseAlloc = true;
  ZuCheck(h.open_(1, h.geometry()));
  ZuCheck(h.admit_(body(), {1, 1, 1}, frame())); h.flush();
  ZuCheck(h.failed_() && h.done.length() == 1 && !h.done[0].ok);
}

static void replacement() {
  ZuTestScope(replacement);
  for (bool records : {false, true}) {
    Harness h;
    auto geometry = records ? h.geometry() : WireGeometry{};
    ZuCheck(h.open_(1, geometry));
    auto original = frame();
    ZuCheck(h.admit_(body(), {7, 1, 10}, original)); h.flush();
    auto oldCount = h.records.length();
    ZuCheck(oldCount && h.count_() == 1 && !h.done.length());
    if (records) h.finish(0);
    ZuCheck(h.open_(2, geometry));
    ZuCheck(!h.count_() && !h.bytes_() && h.done.length() == 1 &&
      !h.done[0].ok && h.done[0].context.connection == 1);
    if (!records) ZuCheck(h.records[0] == original);
    ZuCheck(h.admit_(body(), {7, 2, 11}, frame())); h.flush();
    for (unsigned i = 0; i < oldCount; ++i) h.finish(i, false);
    h.close_(1);
    ZuCheck(h.count_() == 1 && h.done.length() == 1 && !h.failed_());
    for (unsigned i = oldCount, n = h.records.length(); i < n; ++i) h.finish(i);
    ZuCheck(!h.count_() && !h.bytes_() && h.done.length() == 2 &&
      h.done[1].ok && h.done[1].context.connection == 2);
    ZuCheck(h.open_(3, geometry));
    ZuCheck(h.admit_(body(), {7, 3, 12}, frame()));
    ZuCheck(h.open_(4, geometry));
    h.flush_();
    ZuCheck(!h.count_() && h.done.length() == 3 &&
      !h.done[2].ok && h.done[2].context.connection == 3);
  }
}

static void order() {
  ZuTestScope(order);
  Harness h; ZuCheck(h.open_(1, h.geometry()));
  ZuCheck(h.admit_(body(), {1, 1, 1}, frame()));
  ZuCheck(!h.admit_(body(), {1, 1, 2}, frame()));
  ZuCheck(!h.records.length()); h.flush();
  h.finish(0); h.finish(0);
  ZuCheck(!h.done.length());
  // Earlier records do not complete the frame; FIFO makes the final one sufficient.
  for (unsigned i = 1, n = h.records.length(); i < n; ++i) h.finish(i);
  ZuCheck(h.done.length() == 1 && h.done[0].ok);
  h.finish(1); ZuCheck(h.done.length() == 1);
  h.close_(1);
  ZuCheck(!h.open_(1)); ZuCheck(h.open_(2));
  ZuCheck(!h.admit_(body(), {1, 2, 0}, frame()));
}
static void limits() {
  ZuTestScope(limits);
  Harness measure; ZuCheck(measure.open_(1));
  ZuCheck(measure.admit_(body(), {1, 1, 1}, frame()));
  uint64_t charge = measure.bytes_(); measure.close_(1);
  Harness h{{charge, charge}}; ZuCheck(h.open_(1));
  ZuCheck(h.admit_(body(), {1, 1, 1}, frame())); h.flush();
  ZuCheck(!h.admit_(body(), {1, 1, 2}, frame()));
  ZuCheck(h.admit_(body(true), {1, 1, 2}, frame())); h.flush();
  ZuCheck(!h.admit_(body(true), {1, 1, 3}, frame()));
  ZuCheck(h.bytes_() == 2 * charge);
  h.close_(1); ZuCheck(h.done.length() == 2 && !h.bytes_());
  Harness geometry;
  ZuCheck(!geometry.open_(1, {1, 1, 0, 1}));
  ZuCheck(!geometry.open_(1, {7, 1, 1, 6}));
  ZuCheck(geometry.open_(1));
  ZuCheck(!geometry.admit_(body(), {1, 2, 1}, frame()));
}
struct SessionOwner;
struct SessionOutput : public WireTx<SessionOutput> {
  SessionOwner		*owner;
  Items<ZmRef<ZiIOBuf>>	records;
  Items<Completion>	done;
  bool			refuse = false;
  bool			early = false;

  explicit SessionOutput(SessionOwner *owner_, WireLimits limits = {}) :
    WireTx<SessionOutput>{limits}, owner{owner_} { }

  ZmRef<ZiIOBuf> recordAlloc(unsigned) { return ZmSingleton<Zi::IOBufBuiltinAllocator<Zi::IOBufType::App,
      ZiTxBuf, ZiIOBuf_DefltSize>>::instance()->allocBuf();
  }
  bool recordSend(ZmRef<ZiIOBuf>);

  void outputDone(TxBody::Ref b, TxContext c, bool ok) {
    done.push(Completion{ZuMv(b), c, ok});
  }
};

struct SessionOwner : public LinkTx<SessionOwner> {
  struct Task {
    unsigned	kind;
    uint64_t	ticket;
  };

  Items<Task>	tasks;
  SessionOutput	wire;
  unsigned	task = 0, completion = 0, notices = 0, completed = 0;
  bool		admitted = false, notified = false;
  Fmt::Error	error;

  static SessionConfig config() {
    SessionConfig c; c.identity.version = Version::V4_4;
    c.identity.local = "LOCAL"; c.identity.peer = "PEER"; return c;
  }

  explicit SessionOwner(WireLimits limits = {}) :
    LinkTx<SessionOwner>{config()}, wire{this, limits} { }

  void txSchedule(unsigned kind, uint64_t ticket) { tasks.push(Task{kind, ticket}); }

  unsigned associateTx(TxBody::Ref, TxContext, uint64_t) { return TxLoad::Accepted; }

  bool releaseTx(TxBody::Ref, TxContext, uint64_t) { return true; }
  TxReload retrieveTx(TxContext, uint64_t, uint64_t) { return {}; }
  void archiveTx(uint64_t, uint64_t, uint64_t) { }

  ZuDateTime clockTx() { return {ZuDateTimeScan::FIX{}, "20261027-12:00:00"}; }

  ZuTime nowTx() { return ZuTime{100}; }

  void idleArmTx(uint64_t, uint64_t, ZuTime) { }
  void idleCancelTx() { }

  void faultTx(TxContext, Fmt::Error e) { error = e; }

  ZmRef<ZiIOBuf> allocTx() { return frameBuf(); }

  bool admitFrameTx(TxBody::Ref b, TxContext c, ZmRef<ZiIOBuf> f, ZuDateTime) {
    notified = false; admitted = wire.admit_(ZuMv(b), c, ZuMv(f)); return admitted;
  }

  void notifyTx(TxNotice) { ++notices; notified = admitted; }

  void flushTx() { wire.flush_(); }

  void completeTx(TxBody::Ref, TxContext, bool) { ++completed; }

  void drain() {
    while (task < tasks.length() || completion < wire.done.length()) {
      if (task < tasks.length()) { auto t = tasks[task++]; run_(t.kind, t.ticket); }
      else {
	auto d = ZuMv(wire.done[completion++]); sent_(ZuMv(d.body), d.context, d.ok);
	if (d.ok) writable_(d.context.connection);
      }
    }
  }

  void start() {
    wire.open_(1);
    LinkState source{config()}; Fmt::Error e;
    SessionEvent event{.actions = SessionAction::SendLogon, .state = State::Logon,
      .connection = 1, .epoch = 1, .change = 2, .heartBtInt = 30};
    auto b = controlBody(SessionAction::SendLogon, event, source, 1, {}, e,
      Fmt::Limits::FrameBytes);
    apply_(event, ZuMv(b)); drain();
  }
};

bool SessionOutput::recordSend(ZmRef<ZiIOBuf> b) {
  early |= !owner->notified || !owner->inFlight_();
  if (refuse) return false;
  records.push(ZuMv(b)); return true;
}
static void session() {
  ZuTestScope(session);
  SessionOwner tx; tx.start();
  ZuCheck(tx.published_() == 2 && tx.inFlight_() == 1 && !tx.completed);
  ZuCheck(!tx.wire.early && tx.wire.records.length() == 1);
  tx.apply_({.actions = SessionAction::Ready, .state = State::Established,
    .connection = 1, .epoch = 1, .change = 3, .heartBtInt = 30});
  ZuCheck(tx.candidate_(body()) == TxSelect::Accepted); tx.drain();
  ZuCheck(tx.published_() == 3 && tx.inFlight_() == 2 && tx.wire.count_() == 2);
  auto first = tx.wire.records[0];
  ZuCheck(tx.wire.record_(first.ptr(), true));
  // Socket completion posts work; publication and peer ack remain separate.
  ZuCheck(tx.inFlight_() == 2 && !tx.completed && tx.acknowledged_() == 1);
  tx.drain(); ZuCheck(tx.inFlight_() == 1 && tx.completed == 1);
  tx.wire.close_(1); tx.drain();
  ZuCheck(!tx.inFlight_() && tx.completed == 2 && tx.error.code == Fmt::ErrorCode::Output);
  ZuCheck(tx.published_() == 3 && tx.acknowledged_() == 1);
  SessionOwner refused; refused.wire.refuse = true; refused.start();
  ZuCheck(!refused.wire.early && refused.completed == 1 && !refused.inFlight_());
  ZuCheck(refused.published_() == 2 && refused.error.code == Fmt::ErrorCode::Output);
}
static void writable() {
  ZuTestScope(writable);
  Harness measure; measure.open_(1);
  measure.admit_(body(), {1, 1, 1}, frame());
  uint64_t limit = measure.bytes_(); measure.close_(1);
  SessionOwner tx{{limit, 0}}; tx.start();
  ZuCheck(tx.wire.count_() == 1 && tx.published_() == 2 && !tx.error);
  tx.apply_({.actions = SessionAction::Ready, .state = State::Established,
    .connection = 1, .epoch = 1, .change = 3, .heartBtInt = 30});
  ZuCheck(tx.candidate_(body()) == TxSelect::Accepted); tx.drain();
  ZuCheck(tx.next_() == 3 && tx.published_() == 2 && tx.wire.count_() == 1);
  auto first = tx.wire.records[0];
  ZuCheck(tx.wire.record_(first.ptr(), true)); tx.drain();
  ZuCheck(tx.next_() == 3 && tx.published_() == 3 && tx.wire.count_() == 1);
  ZuCheck(tx.completed == 1 && tx.wire.records.length() == 2 && tx.acknowledged_() == 1);
  auto second = tx.wire.records[1];
  ZuCheck(tx.wire.record_(second.ptr(), true)); tx.drain();
  ZuCheck(tx.completed == 2 && !tx.inFlight_() && !tx.error && tx.acknowledged_() == 1);
}
static void rawMetadata() {
  ZuTestScope(rawMetadata);
  Harness h; ZuCheck(h.open_(1));
  ZuCheck(!h.admit_(body(), {0, 1, 0}, frame()));
  ZuCheck(h.admitRaw(body(), {0, 1, 0}, frame())); h.flush();
  h.finish(0);
  ZuCheck(h.done.length() == 1 && h.done[0].ok && !h.done[0].context.epoch
    && !h.done[0].context.seqNo);
  ZuCheck(!h.admitRaw(body(), {0, 2, 0}, frame()));
}
static void statistics() {
  ZuTestScope(statistics);
  Harness h; ZuCheck(h.open_(1));
  auto native = body(); auto bytes = frame();
  ZuCheck(!h.admit_(native, {0, 1, 1}, bytes));
  ZuCheck(h.admit_(native, {1, 1, 1}, bytes));
  auto s = h.stats_();
  ZuCheck(s.admitted == 1 && !s.published && s.refused == 1 && s.queuedCount == 1);
  ZuCheck(s.queuedBytes == h.bytes_() && s.peakBytes == s.queuedBytes);
  h.flush(); h.finish(0); h.finish(0); // Duplicate completion is inert.
  s = h.stats_();
  ZuCheck(s.published == 1 && s.bytes == Wire.length() && s.completed == 1 && !s.failed);
  ZuCheck(!s.queuedBytes && !s.queuedCount);
  h.close_(1); ZuCheck(h.open_(2));
  ZuCheck(h.admit_(native, {1, 2, 1, true}, bytes)); h.flush(); h.close_(2);
  s = h.stats_();
  ZuCheck(s.admitted == 2 && s.published == 2 && s.replays == 1 && s.failed == 1);
  ZuCheck(s.bytes == 2 * Wire.length() && s.completed == 1 && s.refused == 1);
  ZuCheck(h.open_(3));
  ZuCheck(h.admit_(native, {1, 3, 2}, bytes)); h.close_(3);
  s = h.stats_();
  ZuCheck(s.admitted == 3 && s.published == 2 && s.failed == 2 && !s.queuedCount);
}
// Controlled owner FIFOs instantiate the real managed Link. Timer arms run
// only when the fixture explicitly delivers their saved callback.
struct AdmitMux {
  struct Arm {
    ZmFn<>	&callback;

    template <typename Fn> bool operator ()(Fn &&fn) {
      callback = ZmFn<>{ZuFwd<Fn>(fn)}; return true;
    }
  };

  ZmScheduler::Timer	*rxNode = nullptr, *txNode = nullptr;
  ZmFn<>		rxTimer, txTimer;
  ZuTime		rxAt;

  void del(ZmScheduler::Timer *node) {
    if (node == rxNode) { rxTimer = {}; rxNode = nullptr; }
    if (node == txNode) { txTimer = {}; txNode = nullptr; }
  }

  template <typename Fn>
  void add(ZmScheduler::Timer *node, ZuTime at, unsigned, Fn &&fn, unsigned thread) {
    if (thread == 1) {
      rxNode = node; rxAt = at;
      Arm arm{rxTimer}; fn(arm);
    } else {
      txNode = node;
      Arm arm{txTimer}; fn(arm);
    }
  }
};

struct AdmitTask {
  ZmFn<>	fn;
  bool		rx;
};

struct AdmitApp {
  using Queue = ZmList<AdmitTask, ZmListHeapID<"Zfix.WireTest.Owner">>;
  Queue		queue;
  AdmitMux	mux;
  PostedIngress	admin;
  bool		rx = true;
  unsigned	destroyed = 0;

  AdmitMux *mx() { return &mux; }
  unsigned rxThread() const { return 1; }
  unsigned txThread() const { return 2; }
  bool rxInvoked() const { return rx; }
  bool txInvoked() const { return !rx; }
  PostedIngress &fixAdminRx() { return admin; }

  template <typename Fn> void rxRun(Fn &&fn) {
    queue.push(AdmitTask{ZmFn<>{ZuFwd<Fn>(fn)}, true});
  }

  template <typename Fn> void txRun(Fn &&fn) {
    queue.push(AdmitTask{ZmFn<>{ZuFwd<Fn>(fn)}, false});
  }

  bool step() {
    auto node = queue.shift();
    if (!node) return false;
    bool previous = rx;
    rx = node->data().rx;
    node->data().fn();
    rx = previous;
    return true;
  }

  // Select an isolated owner while preserving that owner's FIFO. The other
  // shard may legitimately lag behind a completed close or reset transaction.
  bool step(bool selectedRx) {
    Queue::NodeMvRef task;
    {
      auto it = queue.iter();
      while (auto node = it()) {
	if (node->data().rx != selectedRx) continue;
	task = it.del(); break;
      }
    }
    if (!task) return false;
    bool previous = rx; rx = selectedRx;
    task->data().fn(); rx = previous;
    return true;
  }

  void drain() { while (step()); }
};

struct AdmitLink : ZmNHeap<AdmitLink>, ZmObject, Link<AdmitLink> {
friend inline ZuStringT<"Zfix.WireTest.Link"> ZmNHeapID(AdmitLink *);
  using MessageCatalog = MTList<Entry>;
  using RxBufAlloc = Zi::IOBufBuiltin<ZiRxQueue::Node, ZiIOBuf_DefltSize, ZiIOBuf_Align, ZuStringT<"Zfix.WireTest.Rx">, ZiIOBuf_DefltMaxSize>;
  using Stream = ZiRxStream<ZiRxQueue>;
  using Records = Items<ZmRef<ZiIOBuf>>;
  // ZmFn's POD relocation trait deliberately omits array element destruction.
  // A containing object gives this owning array normal callback destruction.
  struct Reply {
    AdmitFn	fn;
  };

  AdmitApp		*const owner;
  Items<Reply>		replies;
  Records		records;
  Items<Completion>	completions;
  unsigned		stopped = 0, stops = 0, closes = 0;
  unsigned		pureTimers = 0, faults = 0, geometries = 0, epochs = 0;
  bool			refuseLogon = false, native = false, holdOutput = false;
  bool			bindOnEpoch = false;

  static SessionConfig config() {
    auto c = SessionOwner::config(); c.initiator = false; return c;
  }

  explicit AdmitLink(AdmitApp *p, LinkLimits limits = {}, bool initiator = false) :
    Link{[initiator] { auto c = config(); c.initiator = initiator; return c; }(), limits},
    owner{p} { }

  ~AdmitLink() { ++owner->destroyed; }

  AdmitApp *app() const { return owner; }
  WorkState workState() const { return {}; }
  PostedState txPosted() const { return {}; }
  WireGeometry fixGeometryTx() { ++geometries; return {}; }

  ZmRef<ZiRxQueue::Node> fixAllocRx() {
    return ZmSingleton<Zi::IOBufBuiltinAllocator<Zi::IOBufType::App,
      ZiRxQueue::Node, ZiIOBuf_DefltSize>>::instance()->allocBuf();
  }

  ZmRef<ZiIOBuf> fixAllocTx(unsigned head) {
    ZmRef<ZiIOBuf> buf = ZmSingleton<Zi::IOBufBuiltinAllocator<Zi::IOBufType::App,
      ZiTxBuf, ZiIOBuf_DefltSize>>::instance()->allocBuf();
    buf->skip = head; return buf;
  }

  bool fixSendTx(ZmRef<ZiIOBuf> buf) {
    if (holdOutput) records.push(ZuMv(buf));
    else transportSent(ZuMv(buf), true);
    return true;
  }

  void finish(unsigned i, bool ok) { transportSent(records[i], ok); }

  void resumeRx() { }
  void disconnect_(bool = true) { ++closes; }

  AdmitLink *cxn() { return native ? this : nullptr; }

  bool bind() {
    bool replacing = native; native = true;
    return transportBind(replacing);
  }

  void up() { ZmAssert_(bind()); transportUp_0(); }

  void down(bool peer) { native = false; transportDown_0(peer); }

  unsigned fixAdmissionRx(SessionEvent, ZmRef<Msg>, AdmitRequest &request) {
    replies.push(Reply{request.defer()}); return Admission::Pending;
  }

  void fixStopRx(uint64_t, uint64_t) { ++stops; }
  void fixStoppedRx(bool) { ++stopped; }

  TxBody::Ref fixLogonTx(TxBody::Ref body) { return refuseLogon ? TxBody::Ref{} : body; }

  void fixEventRx(SessionEvent e, ZmRef<Msg>) {
    if (e.actions == SessionAction::Timers) ++pureTimers;
  }

  void fixEpochTx(TxContext, uint64_t, TxProgress, uint64_t) {
    ++epochs;
    if (!bindOnEpoch) return;
    bindOnEpoch = false;
    app()->rxRun([this] { ZmAssert_(bind()); });
  }

  void fixFaultTx(TxContext, Fmt::Error) { ++faults; }

  void fixCompleteTx(TxBody::Ref body, TxContext c, bool ok) {
    completions.push(Completion{ZuMv(body), c, ok});
  }

  int control(uint64_t seqNo, const Session::Msg &msg) {
    Fmt::Error error;
    auto body = TxBody::control(msg, error);
    auto peer = config(); peer.identity.local = "PEER"; peer.identity.peer = "LOCAL";
    auto cfg = txConfig(ZuMv(peer));
    auto frame = txFrame(frameBuf(), *body, cfg,
      TxContext{.seqNo = seqNo, .sendingTime = ZuDateTime{Zm::now()}}, error);
    ZmAssert(frame && !error);
    auto input = fixAllocRx(); input->append(frame->cspan());
    Stream stream; stream.push(ZuMv(input));
    return process_(stream);
  }

  int logon() {
    Session::Msg msg; msg.p<Session::Logon>(Session::Logon{});
    return control(1, msg);
  }
};

static void earlyAssociation()
{
  ZuTestScope(earlyAssociation);
  AdmitApp app;
  ZmRef<AdmitLink> link = new AdmitLink{&app, LinkLimits{}, true};
  link->holdOutput = true;
  link->up(); app.drain();
  ZuCheck(link->records.length() == 1 && link->geometries == 1);
  ZuCheck(link->logon() == 1); app.drain();
  ZuCheck(link->replies.length() == 1 && link->session_().admissionPending());
  auto admission = link->replies[0].fn;
  auto timer = app.mux.rxTimer;
  ZuCheck(timer);
  // Leave A's actual Tx failure/notice queued to Rx, then associate B first.
  link->finish(0, false);
  while (!link->faults && app.step());
  ZuCheck(link->faults == 1 && !link->closes);
  ZuCheck(link->bind());
  auto observed = link->pureTimers;
  admission(true); admission(false); timer(); timer = {};
  app.drain();
  ZuCheck(link->session_().connection_() == 2 &&
    link->session_().state_() == State::Disconnected && !link->session_().ready_() &&
    !link->closes && !link->stopped && link->geometries == 1 &&
    link->pureTimers == observed && link->faults == 1);
  ZuCheck(link->completions.length() == 1 &&
    link->completions[0].context.connection == 1 && !link->completions[0].ok);
  link->transportUp_0(); app.drain();
  ZuCheck(link->session_().connection_() == 2 && link->geometries == 2 &&
    link->records.length() == 2 && link->session_().state_() == State::Logon);
  link->finish(1, true); app.drain();
  ZuCheck(link->logon() == 1); app.drain();
  ZuCheck(link->replies.length() == 2);
  link->replies[1].fn(true); app.drain();
  ZuCheck(link->session_().connection_() == 2 && link->session_().ready_() &&
    !link->closes && link->faults == 1);
  link->down(true); app.drain();
  ZuCheck(link->stopped == 1);
  admission = {}; link->replies.clear(); link = nullptr;
  ZuCheck(app.destroyed == 1);
}

static void preauthClose()
{
  ZuTestScope(preauthClose);
  for (bool rapid : {false, true}) {
    AdmitApp app;
    ZmRef<AdmitLink> link = new AdmitLink{&app};
    link->up(); app.drain();
    ZuCheck(link->logon() == 1); app.drain();
    ZuCheck(link->replies.length() == 1);
    auto admission = link->replies[0].fn;
    link->down(true); app.drain();
    ZuCheck(link->stops == 1 && !link->stopped);
    ZuCheck(link->bind()); app.drain();
    if (rapid) { ZuCheck(link->bind()); app.drain(); }
    auto connection = rapid ? 3U : 2U;
    ZuCheck(link->session_().connection_() == connection &&
      link->session_().state_() == State::Disconnected && !link->session_().ready_() &&
      link->geometries == 1 && !link->stopped && !link->closes);
    link->down(true); app.drain();
    ZuCheck(link->stops == 2 && !link->stopped);
    admission(true); admission(false); app.drain();
    ZuCheck(link->stopped == 1 && !link->faults && !app.queue.count_() &&
      !app.admin.state_().count);
    admission = {}; link->replies.clear(); link = nullptr;
    ZuCheck(app.destroyed == 1);
  }
}

static void resetTimerOrder()
{
  ZuTestScope(resetTimerOrder);
  AdmitApp app;
  ZmRef<AdmitLink> link = new AdmitLink{&app, LinkLimits{}, true};
  link->up(); app.drain();
  ZuCheck(link->logon() == 1); app.drain();
  ZuCheck(link->replies.length() == 1);
  link->replies[0].fn(true); app.drain();
  ZuCheck(link->session_().ready_());
  ResetRequest request{.connection = 1, .epoch = 1, .nextEpoch = 2,
    .op = ResetOp::Drain, .initiator = true};
  ZuCheck(link->reset_(request)); app.drain();
  SessionString id{link->session_().probeID()};
  ZuCheck(id.length());
  Session::Msg echo; echo.p<Session::HeartBt>(Session::HeartBt{.testReqID = id});
  ZuCheck(link->control(2, echo) == 1); app.drain();
  ZuCheck(link->session_().drained_());
  request.op = ResetOp::Prepare;
  ZuCheck(link->reset_(request));
  // Execute ResetPrepare's Tx lifecycle post, leaving its reset task queued.
  ZuCheck(app.step() && !link->faults && link->session_().epoch_() == 1);
  auto pending = app.queue.count_();
  auto reserved = app.admin.state_();
  auto observed = link->pureTimers;
  ZuCheck(app.mux.rxTimer && app.mux.rxAt > Zm::now());
  auto timer = ZuMv(app.mux.rxTimer);
  timer(); timer = {};
  ZuCheck(link->pureTimers == observed + 1 && app.queue.count_() == pending &&
    app.admin.state_().count == reserved.count && app.admin.state_().bytes == reserved.bytes);
  // Tx commits before Rx adopts ResetReady. The old-epoch timer remains an
  // Rx deadline notification, never a Tx failure or a new administrative post.
  app.drain();
  ZuCheck(link->session_().epoch_() == 2 && link->session_().resetArmed() &&
    !link->faults && !link->closes && !app.admin.state_().count);
  link->down(true); app.drain();
  ZuCheck(link->stopped == 1);
  link->replies.clear(); link = nullptr;
  ZuCheck(app.destroyed == 1);
}

static void resetAssociation()
{
  ZuTestScope(resetAssociation);
  for (unsigned stage = 0; stage < 3; ++stage) {
    AdmitApp app;
    ZmRef<AdmitLink> link = new AdmitLink{&app, LinkLimits{}, true};
    link->up(); app.drain();
    ZuCheck(link->logon() == 1); app.drain();
    link->replies[0].fn(true); app.drain();
    ZuCheck(link->session_().ready_());
    ResetRequest request{.connection = 1, .epoch = 1, .nextEpoch = 2,
      .op = ResetOp::Drain, .initiator = true};
    ZuCheck(link->reset_(request)); app.drain();
    Session::Msg echo;
    echo.p<Session::HeartBt>(Session::HeartBt{.testReqID = link->session_().probeID()});
    ZuCheck(link->control(2, echo) == 1); app.drain();
    ZuCheck(link->session_().drained_());
    request.op = ResetOp::Prepare;
    auto before = link->completions.length();
    link->bindOnEpoch = stage == 1;
    ZuCheck(link->reset_(request));
    // Bind before commit, between Tx commit and its Rx notice, or after Rx
    // adopts the reset. All are the same logical reset through new sockets.
    if (stage == 0) ZuCheck(link->bind());
    app.drain();
    if (stage == 2) { ZuCheck(link->bind()); app.drain(); }
    ZuCheck(link->session_().connection_() == 2 && link->session_().epoch_() == 2 &&
      link->session_().state_() == State::Disconnected && link->session_().resetArmed() &&
      !*link->session_().deadline_(SessionTimer::Logon) &&
      link->geometries == 1 && link->completions.length() == before &&
      !link->faults && !link->closes && !app.admin.state_().count);
    ZuCheck(link->bind()); app.drain();
    ZuCheck(link->session_().connection_() == 3 && link->session_().resetArmed());
    link->transportUp_0(); app.drain();
    ZuCheck(link->session_().connection_() == 3 && link->geometries == 2 &&
      link->completions.length() == before + 1);
    const auto &sent = link->completions[before];
    ZuCheck(sent.body->msgType() == "A" && sent.context.connection == 3 &&
      sent.context.epoch == 2 && sent.context.seqNo == 1 && sent.ok);
    Session::Msg logon;
    logon.p<Session::Logon>(Session::Logon{.resetSeqNum = true});
    ZuCheck(link->control(1, logon) == 1); app.drain();
    ZuCheck(link->replies.length() == 2);
    link->replies[1].fn(true); app.drain();
    ZuCheck(link->session_().ready_() && link->session_().epoch_() == 2 &&
      !link->faults && !link->closes);
    link->down(true); app.drain();
    ZuCheck(link->stopped == 1);
    link->replies.clear(); link = nullptr;
    ZuCheck(app.destroyed == 1);
  }
}

static void resetCloseOutcome(bool committed, unsigned stage)
{
  ZuTestScope(resetCloseOutcome);
  AdmitApp app;
  ZmRef<AdmitLink> link = new AdmitLink{&app, LinkLimits{}, true};
  link->up(); app.drain();
  ZuCheck(link->logon() == 1); app.drain();
  link->replies[0].fn(true); app.drain();
  ResetRequest request{.connection = 1, .epoch = 1, .nextEpoch = 2,
    .op = ResetOp::Drain, .initiator = true};
  ZuCheck(link->reset_(request)); app.drain();
  Session::Msg echo;
  echo.p<Session::HeartBt>(Session::HeartBt{.testReqID = link->session_().probeID()});
  ZuCheck(link->control(2, echo) == 1); app.drain();
  ZuCheck(link->session_().drained_());
  request.op = ResetOp::Prepare;
  ZuCheck(link->reset_(request));
  if (committed) {
    ZuCheck(app.step(false) && app.step(false));
    ZuCheck(link->epochs == 1 && link->session_().epoch_() == 1);
  }
  // Uncommitted: close is already queued when Prepare posts its reset task.
  // Committed: Tx has queued ResetReady, but Rx has not consumed that outcome.
  link->down(true);
  while (app.step(false));
  ZuCheck(link->session_().resetPreparing() && link->session_().epoch_() == 1 &&
    link->epochs == unsigned(committed) && !link->faults && !link->closes);
  link->holdOutput = true;
  if (stage) {
    ZuCheck(link->bind());
    if (stage == 2) link->transportUp_0();
    while (app.step(false));
    ZuCheck(link->session_().connection_() == 2 &&
      link->session_().resetPreparing() && !link->records.length() &&
      !link->faults && !link->closes);
  }
  app.drain();
  ZuCheck(link->session_().epoch_() == unsigned(committed ? 2 : 1) &&
    !link->session_().resetPreparing() && !link->faults && !link->closes);
  if (stage == 1) {
    ZuCheck(link->session_().state_() == State::Disconnected &&
      !*link->session_().deadline_(SessionTimer::Logon) && !link->records.length());
    link->transportUp_0();
  } else if (!stage) link->up();
  app.drain();
  ZuCheck(link->records.length() == 1 && link->session_().connection_() == 2);
  Session::Msg logon;
  if (link->records.length() == 1) {
    auto parsed = Fmt::frame(ZuCSpan{link->records[0]->cspan()});
    RxMsg metadata; metadata.frame = parsed.view;
    ZuCheck(!catalogSession<Session::MTs>(metadata) && !Session::read(parsed.view, logon) &&
      logon.is<Session::Logon>() && metadata.seqNo == unsigned(committed ? 1 : 3) &&
      logon.p<Session::Logon>().resetSeqNum == bool(committed && stage));
    link->finish(0, true); app.drain();
  }
  Session::Msg peer;
  peer.p<Session::Logon>(Session::Logon{.resetSeqNum = bool(committed && stage)});
  ZuCheck(link->control(committed ? 1 : 3, peer) == 1); app.drain();
  ZuCheck(link->replies.length() == 2);
  if (link->replies.length() == 2) { link->replies[1].fn(true); app.drain(); }
  ZuCheck(link->session_().ready_() && !link->faults && !link->closes);
  link->down(true); app.drain();
  ZuCheck(link->stopped == unsigned(stage ? 1 : 2) && !app.admin.state_().count);
  link->replies.clear(); link = nullptr;
  ZuCheck(app.destroyed == 1);
}

static void initialRefusal()
{
  ZuTestScope(initialRefusal);
  for (unsigned failure = 0; failure < 3; ++failure) {
    AdmitApp app;
    LinkLimits limits;
    if (failure == 0) limits.postedAdminBytes = 0;
    if (failure == 1) ZuCheck(app.admin.limits_({0}));
    ZmRef<AdmitLink> link = new AdmitLink{&app, limits, true};
    link->refuseLogon = failure == 2;
    link->up(); app.drain();
    ZuCheck(link->session_().connection_() == 1 && link->closes &&
      !link->replies.length() && !link->stopped);
    link->down(true); app.drain();
    ZuCheck(link->stopped == 1 && link->stops == 1 &&
      link->session_().state_() == State::Disconnected &&
      !app.admin.state_().count && !app.queue.count_());
    link = nullptr;
    ZuCheck(app.destroyed == 1);
  }
}

static void setupRetirement()
{
  ZuTestScope(setupRetirement);
  AdmitApp app;
  ZmRef<AdmitLink> link = new AdmitLink{&app};
  // Numeric/DNS/socket/TLS setup may retire before managed transportUp_. Each
  // completed attempt still reports its owner drain so shutdown can proceed.
  for (unsigned attempt = 1; attempt <= 3; ++attempt) {
    link->down(false);
    link->down(false); // repeated notice during the same drain
    app.drain();
    ZuCheck(link->stopped == attempt && link->stops == attempt &&
      !link->session_().connection_() && link->session_().state_() == State::Disconnected &&
      !app.admin.state_().count && !app.queue.count_());
  }
  link->up(); app.drain();
  ZuCheck(link->session_().connection_() == 1 && link->stopped == 3);
  link->down(true); app.drain();
  ZuCheck(link->stopped == 4 && link->stops == 4 &&
    link->session_().state_() == State::Disconnected);
  link = nullptr;
  ZuCheck(app.destroyed == 1);
}

static void overlapAdmission()
{
  ZuTestScope(overlapAdmission);
  for (bool closeOld : {false, true}) {
    for (bool oldFirst : {false, true}) {
      AdmitApp app;
      ZmRef<AdmitLink> link = new AdmitLink{&app};
      link->up(); app.drain();
      ZuCheck(link->logon() == 1); app.drain();
      ZuCheck(link->replies.length() == 1 && link->session_().admissionPending());
      if (closeOld) { link->down(true); app.drain(); }
      link->up(); app.drain();
      ZuCheck(link->session_().connection_() == 2 && !link->stopped);
      ZuCheck(link->logon() == 1); app.drain();
      ZuCheck(link->replies.length() == 2 && link->session_().admissionPending());
      auto a = link->replies[0].fn, b = link->replies[1].fn;
      if (oldFirst) {
        auto closes = link->closes;
        a(true); a(false); app.drain();
        ZuCheck(link->session_().connection_() == 2 &&
          link->session_().admissionPending() && !link->session_().ready_() &&
          link->closes == closes);
      }
      link->down(true); app.drain();
      ZuCheck(!link->stopped && link->stops == (closeOld ? 2 : 1));
      b(false); b(true); app.drain();
      ZuCheck(link->stopped == (oldFirst ? 1 : 0));
      if (!oldFirst) { a(true); a(false); app.drain(); }
      ZuCheck(link->stopped == 1 && link->session_().state_() == State::Disconnected &&
        !app.admin.state_().count && !app.queue.count_());
      a = {}; b = {}; link->replies.clear();
      link = nullptr;
      ZuCheck(app.destroyed == 1);
    }
  }
}

int main(int argc, char **argv) {
  parse(argc, argv); ZuTestMain();
  ZuTestCall(tcpFrame);
  ZuTestCall(records);
  ZuTestCall(queued);
  ZuTestCall(failures);
  ZuTestCall(replacement);
  ZuTestCall(synchronous);
  ZuTestCall(order);
  ZuTestCall(limits);
  ZuTestCall(session);
  ZuTestCall(writable);
  ZuTestCall(rawMetadata);
  ZuTestCall(statistics);
  ZuTestCall(earlyAssociation);
  ZuTestCall(preauthClose);
  ZuTestCall(resetTimerOrder);
  ZuTestCall(resetAssociation);
  ZuTestCall(resetCloseOutcome, false, 0);
  ZuTestCall(resetCloseOutcome, false, 1);
  ZuTestCall(resetCloseOutcome, false, 2);
  ZuTestCall(resetCloseOutcome, true, 0);
  ZuTestCall(resetCloseOutcome, true, 1);
  ZuTestCall(resetCloseOutcome, true, 2);
  ZuTestCall(initialRefusal);
  ZuTestCall(setupRetirement);
  ZuTestCall(overlapAdmission);
  return 0;
}
