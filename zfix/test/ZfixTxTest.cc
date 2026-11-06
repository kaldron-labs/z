//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmSingleton.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZiTx.hh>
#include <zlib/ZfixTx.hh>
#include <zlib/ZfixRxMsg.hh>

static ZmRef<ZiIOBuf> frameBuf() {
  using Allocator = Zi::AppBufAllocator<ZiTxBuf, ZiIOBuf_DefltSize>;
  return ZmSingleton<Allocator>::instance()->allocBuf();
}

using namespace ZuTestUtil;
using namespace Zfix;
using namespace Zfix::Fmt;

struct Order {
  ZuCSpan	id;
  ZuCSpan sender, target, senderSub, targetLocation, appVerID;
  uint64_t seqNo = 0;
  bool possDup = false;
  ZuDateTime sendingTime, origSendingTime;
};

ZfStruct(, (Order), (id, (FIX::ID<11>), String));
using OrderFIX = ZuTypeConcat<SessionFields<Order>, ZfFields_Order>;
OrderFIX ZuFields_(Order *, ZuFacet::FIX *);
ZfStructImpl(Order);
ZfStructImpl(Order, FIX);
using OrderMsg = MT<Order, "D">;

static SessionConfig config()
{
  SessionConfig c;
  c.identity.version = Version::V4_4;
  c.identity.local = "CLIENT";
  c.identity.peer = "SERVER";
  return c;
}
static Fmt::Error readControl(const Fmt::FrameView &frame, Session::Msg &message, RxMsg &input) {
  input = {}; input.frame = frame;
  if (auto error = readSession(input)) return error;
  return Session::read(frame, message);
}
struct Task {
  unsigned	kind;
  uint64_t	ticket;
};

struct Limit {
  TxContext	context;
  uint64_t	maximum;
  unsigned	headroom;
};

struct Ack {
  TxContext	context;
  uint64_t	previous;
  uint64_t	next;
};

struct Reset {
  TxContext	context;
  uint64_t	epoch;
  TxProgress	previous;
  uint64_t	id;
};

struct ResetFail {
  TxContext	context;
  uint64_t	epoch;
  Fmt::Error	error;
  uint64_t	id;
};

struct Published {
  TxBody::Ref		body;
  TxContext		context;
  ZmRef<ZiIOBuf>	wire;
  ZuDateTime		original;
};

struct Association {
  TxBody::Ref	body;
  TxContext	context;
};

struct StoredHistory {
  SessionString	bytes;
  ZuDateTime	original;
  uint64_t	id = 0;
  uint64_t	epoch = 0;
  uint64_t	seqNo = 0;
};

template <typename T> using Values = ZtArray<T, ZtArrayHeapID<"Zfix.TxTest">>;

struct Engine : Zfix::Tx<Engine> {
  Values<Task>		tasks;
  Values<Limit>		limits;
  Values<Ack>		acks;
  Values<Reset>		resets;
  Values<ResetFail>	resetFailures;
  Values<Published>	output;
  Values<Association>	associations;
  Values<TxHistory>	history;
  Values<TxContext>	archives;
  Values<TxContext>	lookups;
  Values<uint64_t>	archiveEnds;
  Values<StoredHistory>	stored;
  Values<Association>	unloaded;
  ZuDateTime		now{ZuDateTimeScan::FIX{}, "20261027-12:00:00"};
  Fmt::Error		error;
  unsigned		task = 0;
  unsigned		historyCalls = 0;
  unsigned		outputLimit = 0;
  unsigned		loadResult = TxLoad::Accepted;
  uint64_t		loadTicket = 0;
  uint64_t		unloadTicket = 0;
  uint64_t		historyTicket = 0;
  bool			refuseOutput = false;
  bool			holdOnLogon = false, detachOnLogon = false;
  bool			delayUnload = false;
  bool			delayHistory = false;
  bool			missingHistory = false;
  bool			detachHistory = false, reattachedHistory = false;
  bool			associationBeforeWire = true;

  explicit Engine(TxLimits limits = {}, SessionConfig c = config(), uint64_t epoch = 1) :
    Zfix::Tx<Engine>{ZuMv(c), limits, epoch} { }

  void txPost(unsigned kind, uint64_t ticket) { tasks.push(Task{kind, ticket}); }

  ZuDateTime utcTx() { return now; }

  unsigned loadTx(TxBody::Ref body, TxContext context, uint64_t ticket) {
    loadTicket = ticket;
    if (loadResult == TxLoad::Accepted) associations.push(Association{body, context});
    return loadResult;
  }

  bool unloadTx(TxBody::Ref body, TxContext context, uint64_t ticket) {
    unloadTicket = ticket;
    unloaded.push(Association{body, context});
    return !delayUnload;
  }

  ZmRef<ZiIOBuf> allocTx() { return frameBuf(); }

  bool frameTx(TxBody::Ref body, TxContext context, ZmRef<ZiIOBuf> wire, ZuDateTime original) {
    if (refuseOutput || (outputLimit && output.length() >= outputLimit)) return false;
    bool associated = context.replay;
    for (unsigned i = 0, n = associations.length(); i < n; ++i) {
      const auto &a = associations[i];
      if (a.body.ptr() == body.ptr() && a.context.seqNo == context.seqNo &&
	  a.context.epoch == context.epoch) associated = true;
    }
    associationBeforeWire &= associated;
    if (!context.replay && (!body->administrative() || body->msgType() == "3"))
      history.push(TxHistory{body, original, context.epoch, context.seqNo});
    bool logon = body->msgType() == "A";
    output.push(Published{ZuMv(body), context, ZuMv(wire), original});
    if (logon) {
      if (detachOnLogon) detach_(context.connection);
      else if (holdOnLogon) hold_();
    }
    return true;
  }

  TxReload historyTx(TxContext context, uint64_t, uint64_t ticket) {
    lookups.push(context);
    auto epoch = context.epoch, key = context.seqNo;
    ++historyCalls;
    historyTicket = ticket;
    if (detachHistory) {
      detachHistory = false;
      detach_(context.connection);
      reattachedHistory = attach_(context.connection + 1);
    }
    if (delayHistory) return {{}, true};
    if (missingHistory) return {};
    for (unsigned i = 0, n = history.length(); i < n; ++i)
      if (history[i].epoch == epoch && history[i].seqNo == key) return {history[i]};
    for (const auto &record : stored) {
      if (record.epoch != epoch || record.seqNo != key) continue;
      auto body = TxBody::restore<OrderMsg>(record.bytes, record.id, error);
      if (!body) return {};
      return {TxHistory{ZuMv(body), record.original, epoch, key}};
    }
    return {};
  }

  void historyArchive(uint64_t epoch, uint64_t begin, uint64_t end) {
    archives.push(TxContext{epoch, 0, begin});
    archiveEnds.push(end);
  }

  void resetFailedTx(TxContext context, uint64_t epoch, uint64_t id, Fmt::Error error) {
    resetFailures.push(ResetFail{context, epoch, error, id});
  }

  void resetTx(TxContext context, uint64_t epoch, TxProgress previous, uint64_t id) {
    resets.push(Reset{context, epoch, previous, id});
  }

  void errorTx(TxContext, Fmt::Error e) { error = e; }

  void limitTx(TxContext context, uint64_t maximum, unsigned headroom) {
    limits.push(Limit{context, maximum, headroom});
  }

  void ackdTx(TxContext context, uint64_t previous, uint64_t next) {
    acks.push(Ack{context, previous, next});
  }

  bool step() {
    if (task == tasks.length()) return false;
    auto value = tasks[task++];
    run_(value.kind, value.ticket);
    return true;
  }

  void drain() { while (step()); }

  void start() { attach_(1); ready_(1, true); resume_(); }

  unsigned send(uint64_t id, ZuCSpan text = "order") {
    Fmt::Error e;
    auto body = TxBody::encode<OrderMsg>(Order{text}, id, e);
    return select_(ZuMv(body));
  }

  unsigned control(Session::Msg c) {
    Fmt::Error e;
    auto body = TxBody::control(c, e);
    return select_(ZuMv(body));
  }
};

static Fmt::FrameView view(const Published &output)
{
  return frame(ZuCSpan{output.wire->cspan()}).view;
}

static void assignment()
{
  ZuTestScope(assignment);
  Engine tx;
  tx.start();
  tx.loadResult = TxLoad::Pending;
  Fmt::Error error;
  auto body = TxBody::encode<OrderMsg>(Order{"first"}, 81, error);
  ZuCheck(body && !error && tx.next_() == 1);
  ZuCheck(tx.select_(body) == TxSelect::Pending && tx.loading_() && tx.next_() == 1);
  ZuCheck(tx.send(82) == TxSelect::Busy);
  tx.drain();
  ZuCheck(!tx.output.length());
  auto ticket = tx.loadTicket;
  TxContext association{1, 1, 1};
  tx.associations.push(Association{body, association});
  tx.now = ZuDateTime{ZuDateTimeScan::FIX{}, "20261027-13:00:00"};
  ZuCheck(tx.loaded_(ticket, true) == TxSelect::Accepted && tx.next_() == 2);
  ZuCheck(tx.loaded_(ticket, true) == TxSelect::Invalid);
  tx.drain();
  ZuCheck(tx.output.length() == 1 && tx.associationBeforeWire);
  MessageHandler<Order> received{view(tx.output[0])};
  ZuCheck(received.valid() && received.ctor().id == "first");
  ZuCheck(received.ctor().sendingTime == tx.now && received.ctor().seqNo == 1);
  ZuCheck(tx.published_() == 2 && tx.acknowledged_() == 1);
  tx.loadResult = TxLoad::Refused;
  ZuCheck(tx.send(82) == TxSelect::Refused && tx.next_() == 2);
  ZuCheck(tx.stats_().loadRefused == 1);
  tx.loadResult = TxLoad::Accepted;
  ZuCheck(tx.send(83) == TxSelect::Accepted && tx.next_() == 3);
  tx.drain();
  ZuCheck(tx.output.length() == 2 && tx.output[1].body->id() == 83 &&
    tx.output[1].context.seqNo == 2 && tx.associationBeforeWire);
}

static void staleLoad()
{
  ZuTestScope(staleLoad);
  Engine tx;
  tx.start();
  tx.loadResult = TxLoad::Pending;
  ZuCheck(tx.send(11) == TxSelect::Pending);
  auto ticket = tx.loadTicket;
  ZuCheck(tx.detach_(1) && tx.attach_(2) && tx.ready_(2, true));
  tx.resume_();
  ZuCheck(tx.loaded_(ticket, true) == TxSelect::Accepted);
  ZuCheck(!tx.loading_() && !tx.unloaded.length() && tx.next_() == 2);
  ZuCheck(tx.loaded_(ticket, true) == TxSelect::Invalid);
  tx.drain();
  ZuCheck(tx.output.length() == 1 && tx.output[0].context.connection == 2 &&
    tx.output[0].context.seqNo == 1 && tx.output[0].body->id() == 11);
  tx.loadResult = TxLoad::Accepted;
  ZuCheck(tx.send(12) == TxSelect::Accepted);
  tx.drain();
  ZuCheck(tx.output.length() == 2 && tx.output[1].context.connection == 2 &&
    tx.output[1].context.seqNo == 2 && tx.output[1].body->id() == 12);
}

static void detachedLoad()
{
  ZuTestScope(detachedLoad);
  Engine tx;
  tx.start();
  tx.loadResult = TxLoad::Pending;
  ZuCheck(tx.send(11) == TxSelect::Pending);
  auto ticket = tx.loadTicket;
  ZuCheck(tx.detach_(1));
  ZuCheck(tx.loaded_(ticket, true) == TxSelect::Accepted);
  tx.drain();
  ZuCheck(tx.next_() == 2 && !tx.loading_() && !tx.unloaded.length() &&
    !tx.output.length() && tx.queuedBytes_());
  ZuCheck(tx.attach_(2) && tx.ready_(2, true) && tx.resume_());
  tx.drain();
  ZuCheck(tx.output.length() == 1 && tx.output[0].context.connection == 2 &&
    tx.output[0].context.seqNo == 1 && tx.output[0].body->id() == 11);
}

static void replay()
{
  ZuTestScope(replay);
  Engine tx;
  tx.start();
  ZuCheck(tx.send(1, "first") == TxSelect::Accepted);
  Session::Msg control;
  control.p<Session::HeartBt>(Session::HeartBt{});
  ZuCheck(tx.control(control) == TxSelect::Accepted);
  Session::Reject reject;
  reject.refSeqNo = 17;
  reject.reason = 5;
  control.p<Session::Reject>(reject);
  ZuCheck(tx.control(control) == TxSelect::Accepted);
  ZuCheck(tx.send(2, "second") == TxSelect::Accepted);
  tx.drain();
  ZuCheck(tx.output.length() == 4 && tx.next_() == 5 && tx.published_() == 5);
  auto original = tx.now;
  tx.now = ZuDateTime{ZuDateTimeScan::FIX{}, "20261027-14:00:00"};
  ZuCheck(tx.replay_(tx.epoch_(), 1));
  tx.drain();
  ZuCheck(tx.output.length() == 8 && tx.next_() == 5 && tx.published_() == 5);
  MessageHandler<Order> first{view(tx.output[4])};
  ZuCheck(first.valid() && first.ctor().id == "first");
  ZuCheck(first.ctor().possDup && first.ctor().seqNo == 1 &&
    first.ctor().sendingTime == tx.now && first.ctor().origSendingTime == original);
  Session::Msg fill;
  RxMsg h;

  ZuCheck(!readControl(view(tx.output[5]), fill, h));
  ZuCheck(fill.is<Session::SeqReset>() && fill.p<Session::SeqReset>().gapFill &&
    fill.p<Session::SeqReset>().newSeqNo == 3 && h.seqNo == 2 && h.possDup);
  ZuCheck(!readControl(view(tx.output[6]), fill, h) && fill.is<Session::Reject>());
  ZuCheck(h.seqNo == 3 && h.origSendingTime == original);
  ZuCheck(tx.acknowledge_(tx.epoch_(), 5));
  tx.drain();
  ZuCheck(!tx.retained_() && tx.history.length() == 3 && tx.acknowledged_() == 5);
  ZuCheck(tx.replay_(tx.epoch_(), 1, 1));
  tx.drain();
  ZuCheck(tx.output.length() == 9 && tx.output[8].body->id() == 1);
}

static void failedOutput()
{
  ZuTestScope(failedOutput);
  Engine tx;
  tx.start();
  tx.refuseOutput = true;
  ZuCheck(tx.send(1) == TxSelect::Accepted);
  tx.drain();
  ZuCheck(!tx.output.length() && tx.failed_() && tx.next_() == 2 && tx.published_() == 1);
  ZuCheck(tx.stats_().outputRefused == 1);
  tx.refuseOutput = false;
  tx.now = ZuDateTime{ZuDateTimeScan::FIX{}, "20261027-15:00:00"};
  tx.resume_(); tx.drain();
  ZuCheck(tx.output.length() == 1 && tx.output[0].context.seqNo == 1 &&
    tx.output[0].original == tx.now && tx.associations.length() == 1);
  // A partial write advances transport storage, never the sequence association.
  tx.output[0].wire->advance(7);
  ZuCheck(tx.next_() == 2 && tx.published_() == 2 && tx.acknowledged_() == 1);
  ZuCheck(tx.replay_(tx.epoch_(), 1, 1)); tx.drain();
  MessageHandler<Order> replayed{view(tx.output[1])};
  ZuCheck(replayed.valid() && replayed.ctor().seqNo == 1 && replayed.ctor().possDup);
}

static void recovery()
{
  ZuTestScope(recovery);
  Engine tx;
  tx.start(); tx.send(1); tx.drain();
  tx.delayHistory = true;
  ZuCheck(tx.replay_(1, 1)); tx.drain();
  auto ticket = tx.historyTicket;
  auto history = tx.history[0];
  ZuCheck(tx.waiting_() && !tx.retained_());
  ++history.epoch;
  ZuCheck(tx.reloaded_(ticket, history) && tx.error && !tx.waiting_());
  ZuCheck(!tx.reloaded_(ticket, tx.history[0]));
  tx.error = {}; tx.delayHistory = false; tx.resume_(); tx.drain();
  ZuCheck(tx.output.length() == 2 && tx.output[1].context.replay);

  Engine missing;
  missing.start(); missing.send(1); missing.drain();
  missing.missingHistory = true;
  ZuCheck(missing.replay_(1, 1)); missing.drain();
  Session::Msg fill;
  RxMsg h;

  ZuCheck(missing.output.length() == 2 &&
    !readControl(view(missing.output[1]), fill, h));
  ZuCheck(fill.is<Session::SeqReset>() && fill.p<Session::SeqReset>().newSeqNo == 2);
}

static void limits()
{
  ZuTestScope(limits);
  TxLimits cfg;
  cfg.queuedOutputBytes = 1;
  cfg.adminReserveBytes = 4096;
  Engine limited{cfg};
  limited.start();
  ZuCheck(limited.send(1) == TxSelect::Overload && limited.next_() == 1);
  Session::Msg hb;
  hb.p<Session::HeartBt>(Session::HeartBt{});
  ZuCheck(limited.control(hb) == TxSelect::Accepted);
  limited.drain();
  ZuCheck(limited.output.length() == 1 && limited.published_() == 2);
  Engine paused;
  ZuCheck(paused.attach_(1)); paused.resume_();
  ZuCheck(paused.send(1) == TxSelect::Held && paused.next_() == 1);
  ZuCheck(paused.control(hb) == TxSelect::Accepted);
  paused.drain();
  ZuCheck(paused.output.length() == 1 && paused.output[0].context.seqNo == 1);
}

static void sequenceLimit()
{
  ZuTestScope(sequenceLimit);
  auto c = config();
  c.sequenceLimit = 4;
  TxLimits limits;
  limits.controlHeadroom = 2;
  Engine tx{limits, ZuMv(c)};
  tx.start();
  ZuCheck(tx.send(1) == TxSelect::Accepted && tx.send(2) == TxSelect::Accepted);
  ZuCheck(tx.send(3) == TxSelect::Limit && tx.next_() == 3);
  Session::Msg heartbeat;
  heartbeat.p<Session::HeartBt>(Session::HeartBt{});
  ZuCheck(tx.control(heartbeat) == TxSelect::Accepted &&
    tx.control(heartbeat) == TxSelect::Accepted);
  ZuCheck(tx.control(heartbeat) == TxSelect::Limit && tx.next_() == 5);
  tx.drain();
  ZuCheck(tx.output.length() == 4 && tx.published_() == 5);
  ZuCheck(tx.replay_(tx.epoch_(), 1)); tx.drain();
  ZuCheck(tx.output.length() == 7 && tx.next_() == 5);
  Session::Msg fill;
  RxMsg header;

  if (tx.output.length() == 7)
    ZuCheck(!readControl(view(tx.output[6]), fill, header) &&
      fill.is<Session::SeqReset>() && header.seqNo == 3 &&
      fill.p<Session::SeqReset>().newSeqNo == 5);
}

static void sequenceNotice()
{
  ZuTestScope(sequenceNotice);
  auto c = config(); c.sequenceLimit = 4;
  TxLimits limits; limits.controlHeadroom = 2;
  Engine tx{limits, c}; tx.start();
  ZuCheck(!tx.limits.length() && tx.send(1) == TxSelect::Accepted);
  tx.loadResult = TxLoad::Pending;
  ZuCheck(tx.send(2) == TxSelect::Pending && !tx.limits.length());
  ZuCheck(tx.loaded_(tx.loadTicket, false) == TxSelect::Refused &&
    !tx.limits.length() && tx.next_() == 2);
  ZuCheck(tx.send(2) == TxSelect::Pending &&
    tx.loaded_(tx.loadTicket, true) == TxSelect::Accepted);
  ZuCheck(tx.limits.length() == 1 && tx.limits[0].context.epoch == 1 &&
    tx.limits[0].context.connection == 1 && tx.limits[0].context.seqNo == 3 &&
    tx.limits[0].maximum == 4 && tx.limits[0].headroom == 2);
  tx.loadResult = TxLoad::Accepted;
  Session::Msg heartbeat; heartbeat.p<Session::HeartBt>(Session::HeartBt{});
  ZuCheck(tx.send(3) == TxSelect::Limit && tx.control(heartbeat) == TxSelect::Accepted &&
    tx.control(heartbeat) == TxSelect::Accepted && tx.control(heartbeat) == TxSelect::Limit);
  tx.drain();
  ZuCheck(tx.replay_(1, 1)); tx.drain();
  ZuCheck(tx.limits.length() == 1 && tx.detach_(1) && tx.attach_(2) &&
    tx.limits.length() == 1);
  ZuCheck(tx.resume_() && tx.acknowledge_(1, 5)); tx.drain(); tx.hold_();
  ZuCheck(tx.reset_(1, 2) && tx.ready_(2, true) && tx.resume_());
  ZuCheck(tx.send(3) == TxSelect::Accepted && tx.send(4) == TxSelect::Accepted &&
    tx.limits.length() == 2 && tx.limits[1].context.epoch == 2 &&
    tx.limits[1].context.connection == 2);

  Engine restored{limits, c};
  ZuCheck(restored.restore_(1, 3, 3, 3) && !restored.limits.length());
  restored.start();
  ZuCheck(restored.limits.length() == 1 && restored.limits[0].context.seqNo == 3);
  Engine admin{limits, c}; admin.start();
  ZuCheck(admin.control(heartbeat) == TxSelect::Accepted &&
    admin.control(heartbeat) == TxSelect::Accepted && admin.limits.length() == 1);

  // All sequence arithmetic stays bounded even at the largest supported limit.
  c.sequenceLimit = UINT64_MAX - 1; limits.controlHeadroom = 0;
  Engine edge{limits, c};
  ZuCheck(edge.restore_(1, c.sequenceLimit, c.sequenceLimit, c.sequenceLimit));
  edge.start();
  ZuCheck(!edge.limits.length() && edge.send(1) == TxSelect::Accepted &&
    edge.next_() == UINT64_MAX && edge.limits.length() == 1 &&
    edge.limits[0].context.seqNo == UINT64_MAX && edge.send(2) == TxSelect::Limit);
  c.sequenceLimit = 2; limits.controlHeadroom = 2;
  Engine reserved{limits, c}; reserved.start();
  ZuCheck(reserved.limits.length() == 1 && reserved.limits[0].context.seqNo == 1 &&
    reserved.send(1) == TxSelect::Limit && reserved.control(heartbeat) == TxSelect::Accepted);
}

static void clockReversal()
{
  ZuTestScope(clockReversal);
  Engine tx;
  tx.start();
  ZuCheck(tx.send(1) == TxSelect::Accepted); tx.drain();
  auto original = tx.now;
  tx.now = ZuDateTime{ZuDateTimeScan::FIX{}, "20261027-11:59:59"};
  ZuCheck(tx.replay_(tx.epoch_(), 1)); tx.drain();
  ZuCheck(tx.output.length() == 2 && !tx.failed_());
  ZuCheck(tx.next_() == 2 && tx.published_() == 2);
  tx.now = original;
  tx.resume_(); tx.drain();
  ZuCheck(tx.output.length() == 2 && !tx.failed_());
  MessageHandler<Order> replayed{view(tx.output[1])};
  ZuCheck((replayed.valid() && replayed.ctor().origSendingTime == original &&
    replayed.ctor().sendingTime == ZuDateTime{ZuDateTimeScan::FIX{},
      "20261027-11:59:59"}));
}

static void modes()
{
  ZuTestScope(modes);
  for (unsigned mode = Transport::FIX4; mode <= Transport::FIXT11; ++mode) {
    auto c = config();
    c.mode = mode;
    c.identity.version = mode == Transport::FIXT11 ? Version::V4_4 : Version::VT1_1;
    c.appVerID = AppVersion::V5_0_SP2;
    c.identity.localSub = "DESK";
    c.identity.peerLocation = "LOC";
    auto version = Version::Wire::v2s(c.identity.version);
    Engine tx{{}, ZuMv(c)};
    tx.start();
    ZuCheck(tx.send(1) == TxSelect::Accepted); tx.drain();
    MessageHandler<Order> message{view(tx.output[0])};
    ZuCheck(message.valid() && view(tx.output[0]).beginString == version);
    ZuCheck(message.ctor().senderSub == "DESK" && message.ctor().targetLocation == "LOC");
    if (mode == Transport::FIXT11) ZuCheck(message.ctor().appVerID == "9");
    else ZuCheck(!message.ctor().appVerID.data());
  }
}

static void reconnectRetained()
{
  ZuTestScope(reconnectRetained);
  Engine tx;
  tx.start();
  ZuCheck(tx.send(1) == TxSelect::Accepted); tx.drain();
  tx.hold_();
  ZuCheck(tx.send(2) == TxSelect::Accepted);
  Session::Msg heartbeat;
  heartbeat.p<Session::HeartBt>(Session::HeartBt{});
  ZuCheck(tx.control(heartbeat) == TxSelect::Accepted);
  ZuCheck(tx.send(3) == TxSelect::Accepted && tx.next_() == 5 && tx.published_() == 2);
  auto bytes = tx.queuedBytes_();
  ZuCheck(tx.detach_(1) && tx.attach_(2));
  ZuCheck(tx.next_() == 5 && tx.queuedBytes_() == bytes && !tx.unloaded.length());
  Session::Msg logon;
  logon.p<Session::Logon>(Session::Logon{});
  ZuCheck(tx.control(logon) == TxSelect::Accepted && tx.resume_()); tx.drain();
  ZuCheck(tx.output.length() == 5 && tx.output[1].body->msgType() == "A" &&
    tx.output[1].context.seqNo == 5 && tx.output[1].context.connection == 2);
  ZuCheck(tx.output[2].body->id() == 2 && tx.output[2].context.seqNo == 2 &&
    tx.output[2].context.connection == 2 && !tx.output[2].context.replay);
  Session::Msg fill;
  RxMsg header;

  ZuCheck(!readControl(view(tx.output[3]), fill, header));
  ZuCheck(fill.is<Session::SeqReset>() && fill.p<Session::SeqReset>().gapFill &&
    fill.p<Session::SeqReset>().newSeqNo == 4 && header.seqNo == 3 && header.possDup);
  ZuCheck(tx.output[4].body->id() == 3 && tx.output[4].context.seqNo == 4 &&
    tx.output[4].context.connection == 2);
  ZuCheck(tx.next_() == 6 && tx.published_() == 6 && tx.acknowledged_() == 1 &&
    !tx.queuedBytes_() && !tx.unloaded.length() && tx.associations.length() == 5);
  tx.resume_(); tx.drain();
  ZuCheck(tx.output.length() == 5 && tx.replay_(tx.epoch_(), 1, 1)); tx.drain();
  MessageHandler<Order> old{view(tx.output[5])};
  ZuCheck(old.valid() && old.ctor().id == "order" && old.ctor().seqNo == 1 &&
    old.ctor().possDup);
}

static void repeatedReconnect()
{
  ZuTestScope(repeatedReconnect);
  Engine tx;
  tx.start();
  ZuCheck(tx.send(1) == TxSelect::Accepted); tx.drain();
  tx.hold_();
  ZuCheck(tx.send(2) == TxSelect::Accepted);
  Session::Msg logon;
  logon.p<Session::Logon>(Session::Logon{});
  ZuCheck(tx.detach_(1) && tx.attach_(2));
  tx.outputLimit = 2;
  ZuCheck(tx.control(logon) == TxSelect::Accepted && tx.resume_()); tx.drain();
  ZuCheck(tx.output.length() == 2 && tx.output[1].body->msgType() == "A" &&
    tx.output[1].context.seqNo == 3 && tx.output[1].context.connection == 2 &&
    tx.next_() == 4 && tx.published_() == 2);
  ZuCheck(tx.detach_(2) && tx.attach_(3));
  tx.outputLimit = 3;
  ZuCheck(tx.control(logon) == TxSelect::Accepted && tx.resume_()); tx.drain();
  ZuCheck(tx.output.length() == 3 && tx.output[2].body->msgType() == "A" &&
    tx.output[2].context.seqNo == 4 && tx.output[2].context.connection == 3 &&
    tx.next_() == 5 && tx.published_() == 2);
  tx.outputLimit = 0;
  ZuCheck(tx.resume_()); tx.drain();
  ZuCheck(tx.output.length() == 5 && tx.output[3].body->id() == 2 &&
    tx.output[3].context.seqNo == 2 && tx.output[3].context.connection == 3);
  Session::Msg fill;
  RxMsg header;

  ZuCheck(!readControl(view(tx.output[4]), fill, header));
  ZuCheck(fill.is<Session::SeqReset>() && fill.p<Session::SeqReset>().gapFill &&
    fill.p<Session::SeqReset>().newSeqNo == 4 && header.seqNo == 3 && header.possDup);
  ZuCheck(tx.next_() == 5 && tx.published_() == 5 && tx.acknowledged_() == 1 &&
    !tx.queuedBytes_() && !tx.unloaded.length() && tx.associations.length() == 4);
}

static void priorityReentry()
{
  ZuTestScope(priorityReentry);
  for (bool detach : {false, true}) {
    Engine tx;
    tx.start(); tx.hold_();
    ZuCheck(tx.send(31) == TxSelect::Accepted && tx.next_() == 2);
    ZuCheck(tx.detach_(1) && tx.attach_(2));
    Session::Msg logon;
    logon.p<Session::Logon>(Session::Logon{});
    ZuCheck(tx.control(logon) == TxSelect::Accepted);
    tx.holdOnLogon = !detach;
    tx.detachOnLogon = detach;
    ZuCheck(!tx.resume_());
    ZuCheck(tx.output.length() == 1 && tx.output[0].body->msgType() == "A" &&
      tx.output[0].context.seqNo == 2 && tx.output[0].context.connection == 2 &&
      tx.next_() == 3 && tx.published_() == 1);
    auto bytes = tx.queuedBytes_();
    ZuCheck(!tx.abortTx_(tx.epoch_(), 2));
    ZuCheck(tx.abortTx_(tx.epoch_(), 1) && tx.next_() == 3 && tx.queuedBytes_() < bytes);
    tx.drain();
    ZuCheck(tx.output.length() == 1 && tx.published_() == 1 &&
      tx.next_() == 3 && !tx.unloaded.length());
    ZuCheck(tx.ready_(2, true) == !detach);
    if (tx.attached_()) ZuCheck(tx.detach_(2));
    ZuCheck(tx.attach_(3) && !tx.abortTx_(tx.epoch_(), 2));
  }
}

static void restoredRetained()
{
  ZuTestScope(restoredRetained);
  Engine tx;
  Fmt::Error error;
  auto body = TxBody::encode<OrderMsg>(Order{"restored"}, 42, error);
  ZuCheck(tx.restore_(1, 3, 1, 1) && tx.attach_(1));
  tx.delayHistory = true;
  Session::Msg logon;
  logon.p<Session::Logon>(Session::Logon{});
  ZuCheck(tx.control(logon) == TxSelect::Accepted && tx.resume_()); tx.drain();
  ZuCheck(tx.output.length() == 1 && tx.output[0].body->msgType() == "A" &&
    tx.output[0].context.seqNo == 3 && tx.next_() == 4 && tx.published_() == 1);
  ZuCheck(tx.waiting_() && tx.historyCalls == 1 && tx.queuedBytes_() > 0);
  auto startupCharge = tx.queuedBytes_();
  auto ticket = tx.historyTicket;
  ZuCheck(tx.reloaded_(ticket, TxHistory{body, {}, 1, 1})); tx.drain();
  ZuCheck(tx.output.length() == 2 && tx.output[1].body->id() == 42 &&
    tx.output[1].context.seqNo == 1 && tx.waiting_() &&
    tx.queuedBytes_() == startupCharge);
  ZuCheck(!tx.reloaded_(ticket, TxHistory{body, {}, 1, 1}));
  ZuCheck(tx.reloaded_(tx.historyTicket, TxHistory{body, {}, 1, 2})); tx.drain();
  ZuCheck(tx.output.length() == 3 && tx.output[2].context.seqNo == 2 &&
    tx.next_() == 4 && tx.published_() == 4 && !tx.queuedBytes_() &&
    !tx.waiting_() && !tx.unloaded.length() && !tx.failed_());
}

static void priorityPendingReplay()
{
  ZuTestScope(priorityPendingReplay);
  for (bool bounded : {false, true}) {
    Engine tx;
    Fmt::Error error;
    auto body = TxBody::encode<OrderMsg>(Order{"pending"}, 42, error);
      ZuCheck(tx.restore_(1, 2, 1, 1) && tx.attach_(1));
    tx.delayHistory = true;
    Session::Msg logon;
    logon.p<Session::Logon>(Session::Logon{});
    ZuCheck(tx.control(logon) == TxSelect::Accepted && tx.resume_()); tx.drain();
    ZuCheck(tx.output.length() == 1 && tx.output[0].body->msgType() == "A" &&
      tx.output[0].context.seqNo == 2 && tx.published_() == 1 && tx.waiting_());
    ZuCheck(tx.replay_(1, 1, bounded ? 1 : 0));
    auto progress = tx.progress_();
    ZuCheck(progress.replayBegin == 1 && progress.replayEnd == (bounded ? 2 : 3));
    tx.drain();
    ZuCheck(tx.output.length() == 1 && tx.waiting_());
    tx.delayHistory = false;
    ZuCheck(tx.reloaded_(tx.historyTicket, TxHistory{body, {}, 1, 1})); tx.drain();
    ZuCheck(tx.output.length() == (bounded ? 3 : 4) && !tx.waiting_() &&
      !tx.replaying_() && !tx.failed_() && !tx.error && tx.next_() == 3 &&
      tx.published_() == 3 && !tx.queuedBytes_());
    if (tx.output.length() >= 3) {
      ZuCheck(tx.output[1].body->id() == 42 && tx.output[1].context.seqNo == 1 &&
	!tx.output[1].context.replay && tx.output[2].body->id() == 42 &&
	tx.output[2].context.seqNo == 1 && tx.output[2].context.replay);
      MessageHandler<Order> replayed{view(tx.output[2])};
      ZuCheck(replayed.valid() && replayed.ctor().possDup &&
	replayed.ctor().origSendingTime == tx.output[1].original);
    }
  }
}

static void unavailableAssigned()
{
  ZuTestScope(unavailableAssigned);
  Engine tx;
  ZuCheck(tx.restore_(1, 3, 1, 1) && tx.attach_(1));
  tx.delayHistory = true;
  Session::Msg logon;
  logon.p<Session::Logon>(Session::Logon{});
  ZuCheck(tx.control(logon) == TxSelect::Accepted && tx.resume_()); tx.drain();
  ZuCheck(tx.output.length() == 1 && tx.output[0].body->msgType() == "A" &&
    tx.output[0].context.seqNo == 3 && tx.waiting_() && tx.historyCalls == 1);
  ZuCheck(tx.reloaded_(tx.historyTicket, {})); tx.drain();
  ZuCheck(tx.waiting_() && tx.historyCalls == 2 && tx.output.length() == 1);
  ZuCheck(tx.reloaded_(tx.historyTicket, {})); tx.drain();
  ZuCheck(tx.output.length() == 2 && tx.next_() == 4 && tx.published_() == 4 &&
    tx.acknowledged_() == 1 && !tx.queuedBytes_() && !tx.waiting_() && !tx.error);
  Session::Msg fill;
  RxMsg header;

  if (tx.output.length() == 2) {
    ZuCheck(!readControl(view(tx.output[1]), fill, header));
    ZuCheck(fill.is<Session::SeqReset>() && fill.p<Session::SeqReset>().gapFill &&
      fill.p<Session::SeqReset>().newSeqNo == 3 && header.seqNo == 1 && header.possDup);
  }
  ZuCheck(!tx.unloaded.length() && !tx.failed_());
}

static void abortedAssigned()
{
  ZuTestScope(abortedAssigned);
  Engine tx;
  tx.start();
  ZuCheck(tx.send(1) == TxSelect::Accepted); tx.drain();
  tx.hold_();
  ZuCheck(tx.send(2) == TxSelect::Accepted);
  Session::Msg heartbeat;
  heartbeat.p<Session::HeartBt>(Session::HeartBt{});
  ZuCheck(tx.control(heartbeat) == TxSelect::Accepted);
  ZuCheck(tx.send(3) == TxSelect::Accepted && tx.next_() == 5);
  auto bytes = tx.queuedBytes_();
  ZuCheck(tx.abortTx_(tx.epoch_(), 2) && tx.next_() == 5 && tx.queuedBytes_() < bytes);
  auto after = tx.queuedBytes_();
  ZuCheck(!tx.abortTx_(tx.epoch_(), 2) && !tx.abortTx_(tx.epoch_(), 1) &&
    tx.queuedBytes_() == after);
  ZuCheck(tx.abortTx_(tx.epoch_(), 4) && tx.next_() == 5 && tx.published_() == 2 &&
    tx.retained_() == 1 && tx.queuedBytes_() < after);
  ZuCheck(tx.detach_(1) && tx.attach_(2));
  Session::Msg logon;
  logon.p<Session::Logon>(Session::Logon{});
  ZuCheck(tx.control(logon) == TxSelect::Accepted && tx.resume_()); tx.drain();
  ZuCheck(tx.output.length() >= 3 && tx.output[1].body->msgType() == "A" &&
    tx.output[1].context.seqNo == 5 && tx.output[1].context.connection == 2);
  uint64_t next = 2;
  for (unsigned i = 2, n = tx.output.length(); i < n; ++i) {
    Session::Msg fill;
    RxMsg header;

    ZuCheck(!readControl(view(tx.output[i]), fill, header));
    ZuCheck(fill.is<Session::SeqReset>() && fill.p<Session::SeqReset>().gapFill &&
      header.seqNo == next && header.possDup);
    auto end = fill.p<Session::SeqReset>().newSeqNo;
    ZuCheck(end > next && end <= 5);
    next = end;
  }
  ZuCheck(next == 5 && tx.next_() == 6 && tx.published_() == 6 &&
    tx.acknowledged_() == 1 && !tx.queuedBytes_() && !tx.unloaded.length());
}

static void reconstructedHistory()
{
  ZuTestScope(reconstructedHistory);
  TxProgress state;
  StoredHistory record;
  {
    Engine old{{}, config(), 17};
    ZuCheck(old.restore_(17, 1, 1, 1));
    old.start();
    ZuCheck(old.send(73, "restored/0007") == TxSelect::Accepted); old.drain();
    ZuCheck(old.output.length() == 1 && old.acknowledge_(old.epoch_(), 2)); old.drain();
    state = old.progress_();
    const auto &published = old.output[0];
    record = {SessionString{published.body->body()}, published.original,
      published.body->id(), published.context.epoch, published.context.seqNo};
  }
  // No native object, pooled payload or old owner survives. The application
  // reconstructs history from stored bytes only when the Tx retrieval hook runs.
  Engine restored{{}, config(), 17};
  restored.stored.push(ZuMv(record));
  ZuCheck(restored.restore_(17, state));
  restored.now = ZuDateTime{ZuDateTimeScan::FIX{}, "20261027-13:00:00"};
  restored.start();
  ZuCheck(restored.replay_(restored.epoch_(), 1, 1)); restored.drain();
  ZuCheck(restored.historyCalls == 1 && restored.output.length() == 1 && !restored.error);
  const auto &published = restored.output[0];
  ZuCheck(published.body->body() == restored.stored[0].bytes && published.body->id() == 73);
  MessageHandler<Order> replayed{view(published)};
  auto order = replayed.ctor();
  ZuCheck(replayed.valid() && order.id == "restored/0007" &&
    replayed.ctor().seqNo == 1 && replayed.ctor().possDup &&
    replayed.ctor().origSendingTime == restored.stored[0].original &&
    replayed.ctor().sendingTime == restored.now);
  ZuCheck(published.context.epoch == 17 && published.context.replay &&
    restored.next_() == 2 && restored.published_() == 2 &&
    restored.acknowledged_() == 2 && !restored.associations.length());
}

static void ackNotifications()
{
  ZuTestScope(ackNotifications);
  Engine tx;
  tx.start();
  ZuCheck(tx.send(17) == TxSelect::Accepted && tx.send(18) == TxSelect::Accepted); tx.drain();
  ZuCheck(tx.published_() == 3 && tx.acknowledged_() == 1 && !tx.acks.length());
  ZuCheck(!tx.acknowledge_(tx.epoch_(), 4) && !tx.acks.length());
  ZuCheck(tx.acknowledge_(tx.epoch_(), 2) && tx.acks.length() == 1);
  ZuCheck(tx.acks[0].context.epoch == 1 && tx.acks[0].context.connection == 1 &&
    tx.acks[0].previous == 1 && tx.acks[0].next == 2);
  ZuCheck(tx.acknowledge_(tx.epoch_(), 2) && tx.acknowledge_(tx.epoch_(),
    1) && tx.acks.length() == 1);
  ZuCheck(tx.acknowledge_(tx.epoch_(), 3) && tx.acks.length() == 2 &&
    tx.acks[1].previous == 2 && tx.acks[1].next == 3);
}

static void epochTurnover()
{
  ZuTestScope(epochTurnover);
  Engine tx;
  tx.start(); ZuCheck(tx.send(17) == TxSelect::Accepted);
  auto stale = tx.tasks[tx.tasks.length() - 1]; tx.drain();
  ZuCheck(!tx.retained_() && tx.history.length() == 1 && !tx.reset_(1, 2));
  tx.ready_(1, false); tx.hold_();
  ZuCheck(tx.reset_(1, 2) && tx.epoch_() == 2 && tx.next_() == 1);
  ZuCheck(tx.resets.length() == 1 && tx.resets[0].previous.loaded == 2 &&
    tx.resets[0].previous.sent == 2 && tx.resets[0].previous.acknowledged == 1);
  ZuCheck(tx.history.length() == 1 && !tx.acks.length() && !tx.archives.length());
  ZuCheck(!tx.acknowledge_(1, 2) && !tx.replay_(1, 1));
  tx.run_(stale.kind, stale.ticket);
  ZuCheck(tx.output.length() == 1 && !tx.retained_());
  tx.ready_(1, true); tx.resume_();
  ZuCheck(tx.send(18) == TxSelect::Accepted); tx.drain();
  ZuCheck(tx.output.length() == 2 && tx.output[1].context.epoch == 2 &&
    tx.output[1].context.seqNo == 1 && tx.history.length() == 2);

  Engine unsent;
  unsent.start(); unsent.hold_(); unsent.send(77); unsent.ready_(1, false);
  ZuCheck(!unsent.reset_(1, 2) && unsent.retained_() == 1);
  Engine pending;
  pending.start(); pending.send(1); pending.drain(); pending.delayHistory = true;
  ZuCheck(pending.replay_(1, 1)); pending.drain(); pending.ready_(1, false); pending.hold_();
  ZuCheck(pending.waiting_() && !pending.reset_(1, 2));
  ZuCheck(pending.reloaded_(pending.historyTicket, {}) && !pending.waiting_());
  Engine max{{}, config(), UINT64_MAX};
  ZuCheck(!max.reset_(UINT64_MAX, 0) && !max.reset_(UINT64_MAX, UINT64_MAX));
}

static void retireControls()
{
  ZuTestScope(retireControls);
  Engine tx; tx.start();
  Session::Msg heartbeat; heartbeat.p<Session::HeartBt>(Session::HeartBt{});
  for (unsigned i = 0; i < 7; ++i) ZuCheck(tx.control(heartbeat) == TxSelect::Accepted);
  tx.drain(); tx.ready_(1, false); tx.hold_();
  ZuCheck(!tx.retained_() && !tx.queuedBytes_());
  auto id = tx.resetBegin_(1, 2);
  ZuCheck(id && tx.resetting_() && tx.epoch_() == 1 && !tx.resets.length());
  ZuCheck(tx.send(17) == TxSelect::Busy && !tx.resume_());
  auto stale = tx.tasks[tx.tasks.length() - 1]; tx.drain();
  ZuCheck(tx.epoch_() == 2 && tx.next_() == 1 && tx.resets.length() == 1 &&
    tx.resets[0].id == id && tx.resets[0].previous.sent == 8);
  tx.run_(stale.kind, stale.ticket);
  ZuCheck(tx.resets.length() == 1 && !tx.history.length() && !tx.archives.length());
}

static void retireGuards()
{
  ZuTestScope(retireGuards);
  Engine tx; tx.start(); tx.send(17); tx.drain(); tx.ready_(1, false); tx.hold_();
  auto id = tx.resetBegin_(1, 2);
  ZuCheck(id && tx.resetting_());
  auto stale = tx.tasks[tx.tasks.length() - 1];
  ZuCheck(!tx.cancelReset_(2, id) && !tx.cancelReset_(1, id + 1));
  ZuCheck(tx.cancelReset_(1, id) && !tx.resetting_() && tx.resetFailures.length() == 1);
  tx.run_(stale.kind, stale.ticket);
  ZuCheck(tx.epoch_() == 1 && tx.history.length() == 1 && !tx.resets.length());
  auto retry = tx.resetBegin_(1, 3);
  ZuCheck(retry && retry != id); tx.run_(stale.kind, stale.ticket); tx.drain();
  ZuCheck(tx.epoch_() == 3 && tx.resets.length() == 1 && tx.history.length() == 1);

  Engine lost; lost.start(); lost.send(1); lost.drain();
  lost.ready_(1, false); lost.hold_();
  ZuCheck(lost.resetBegin_(1, 2) && lost.detach_(1)); lost.drain();
  ZuCheck(!lost.resetting_() && lost.epoch_() == 1 && lost.resetFailures.length() == 1);
  Engine restored;
  ZuCheck(restored.restore_(1, 4, 4, 1) && restored.resetBegin_(1, 2)); restored.drain();
  ZuCheck(restored.epoch_() == 2 && restored.resets[0].previous.acknowledged == 1);
}

static void appReplayChoice()
{
  ZuTestScope(appReplayChoice);
  Engine tx; tx.start();
  ZuCheck(tx.send(1, "old-price") == TxSelect::Accepted); tx.drain();
  Fmt::Error error;
  using Quote = MT<Order, "8">;
  auto replacement = TxBody::encode<Quote>(Order{"new-price"}, 991, error);
  ZuCheck(replacement && !error && !tx.retained_() && tx.output[0].body->id() == 1);
  tx.history[0].body = replacement;
  tx.refuseOutput = true;
  ZuCheck(tx.replay_(1, 1)); tx.drain();
  ZuCheck(tx.failed_() && tx.historyCalls == 1 && !tx.retained_());
  auto latest = TxBody::encode<Quote>(Order{"latest-price"}, 992, error);
  tx.history[0].body = latest;
  tx.refuseOutput = false; tx.resume_(); tx.drain();
  ZuCheck(tx.historyCalls == 2 && tx.output.length() == 2 && !tx.retained_());
  ZuCheck(tx.output[1].body->id() == 992 && tx.output[1].body->msgType() == "8" &&
    tx.output[1].context.seqNo == 1 && tx.output[1].context.replay);
  MessageHandler<Order> received{view(tx.output[1])};
  ZuCheck(received.valid() && received.ctor().id == "latest-price" &&
    received.ctor().origSendingTime == tx.output[0].original);
  tx.missingHistory = true;
  ZuCheck(tx.replay_(1, 1)); tx.drain();
  Session::Msg fill; RxMsg header;
  ZuCheck(tx.historyCalls == 3 && tx.output.length() == 3 &&
    !readControl(view(tx.output[2]), fill, header));
  ZuCheck(fill.is<Session::SeqReset>() && header.seqNo == 1);
  ZuCheck(tx.lookups.length() == 3 && tx.lookups[0].replay && tx.lookups[2].replay);
  Engine held;
  held.start(); held.send(1); held.drain(); held.delayHistory = true;
  ZuCheck(held.replay_(1, 1)); held.drain(); held.hold_();
  auto stale = held.history[0]; ++stale.epoch;
  ZuCheck(held.reloaded_(held.historyTicket, stale) && !held.waiting_() && !held.error);
  ZuCheck(held.replaying_() && !held.retained_());
}

static void lookupReattach()
{
  ZuTestScope(lookupReattach);
  for (bool replay : {false, true}) {
    Engine tx;
    if (replay) { tx.start(); tx.send(1); tx.drain(); }
    else ZuCheck(tx.restore_(1, 2, 1, 1));
    tx.delayHistory = true; tx.detachHistory = true;
    if (!replay) tx.start();
    if (replay) ZuCheck(tx.replay_(1, 1));
    else tx.resume_();
    tx.drain();
    ZuCheck(tx.waiting_() && tx.attached_() && tx.reattachedHistory &&
      tx.connection_() == 2);
    auto calls = tx.historyCalls;
    ZuCheck(tx.reloaded_(tx.historyTicket, {}) && !tx.waiting_() && !tx.error);
    ZuCheck(tx.attached_() && tx.connection_() == 2 && tx.historyCalls == calls);
  }
}

static void reloadReservation()
{
  ZuTestScope(reloadReservation);
  Fmt::Error error;
  auto old = TxBody::encode<OrderMsg>(Order{"old"}, 17, error);
  auto next = TxBody::encode<OrderMsg>(Order{"next"}, 18, error);
  uint64_t oldCharge = old->size + sizeof(Engine::Msg);
  uint64_t nextCharge = next->size + sizeof(Engine::Msg);
  TxLimits limits;
  limits.queuedOutputBytes = oldCharge + nextCharge - 1;
  limits.adminReserveBytes = 0;
  Engine tx{limits};
  ZuCheck(tx.restore_(1, 2, 1, 1)); tx.start();
  tx.history.push(TxHistory{old, {}, 1, 1});
  tx.loadResult = TxLoad::Pending;
  ZuCheck(tx.select_(next) == TxSelect::Pending);
  auto ticket = tx.loadTicket;
  tx.drain();
  ZuCheck(tx.failed_() && tx.error.code == ErrorCode::Size && tx.loading_() &&
    !tx.queuedBytes_() && !tx.output.length());
  ZuCheck(tx.loaded_(ticket, true) == TxSelect::Accepted && !tx.loading_() &&
    tx.queuedBytes_() == nextCharge && tx.next_() == 3);
  tx.error = {}; tx.missingHistory = true; tx.resume_(); tx.drain();
  ZuCheck(tx.published_() == 3 && !tx.queuedBytes_() && !tx.retained_());
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(assignment);
  ZuTestCall(staleLoad);
  ZuTestCall(detachedLoad);
  ZuTestCall(replay);
  ZuTestCall(failedOutput);
  ZuTestCall(recovery);
  ZuTestCall(limits);
  ZuTestCall(sequenceLimit);
  ZuTestCall(sequenceNotice);
  ZuTestCall(clockReversal);
  ZuTestCall(modes);
  ZuTestCall(reconnectRetained);
  ZuTestCall(repeatedReconnect);
  ZuTestCall(priorityReentry);
  ZuTestCall(restoredRetained);
  ZuTestCall(priorityPendingReplay);
  ZuTestCall(unavailableAssigned);
  ZuTestCall(abortedAssigned);
  ZuTestCall(reconstructedHistory);
  ZuTestCall(ackNotifications);
  ZuTestCall(epochTurnover);
  ZuTestCall(retireControls);
  ZuTestCall(retireGuards);
  ZuTestCall(appReplayChoice);
  ZuTestCall(lookupReattach);
  ZuTestCall(reloadReservation);
  return 0;
}
