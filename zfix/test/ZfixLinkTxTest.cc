//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZfixLinkTx.hh>

using namespace ZuTestUtil;
using namespace Zfix;
using namespace Zfix::Fmt;

template <typename T> using Values = ZtArray<T, ZtArrayHeapID<"Zfix.LinkTxTest">>;
static SessionConfig config(unsigned mode = Transport::FIX4)
{
  SessionConfig c;
  c.id = "PRIMARY"; c.header.version = Version::V4_4;
  c.header.local = "LOCAL";
  c.header.peer = "PEER";
  c.mode = mode;
  c.appVerID = AppVersion::V5_0_SP2;
  return c;
}
struct Order {
  ZuCSpan	id;
};

ZfStruct(, (Order, FIX), (id, (FIX::ID<11>), String));
ZfStructImpl(Order);
ZfStructImpl(Order, FIX);
using OrderMsg = MT<Order, "D">;
struct Task {
  unsigned	kind;
  uint64_t	ticket;
};

// Shared core TxBody objects cannot carry this fixture FIFO's links.
using AppQueue = ZmList<TxBody::Ref, ZmListHeapID<"Zfix.LinkTxTest.App">>;
struct Observation {
  TxBody::Ref	body;
  TxContext	context;
  unsigned	admitted;
  unsigned	flushed;
};

struct Selection {
  TxBody::Ref	body;
  TxContext	context;
  unsigned	result;
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

struct Output {
  TxBody::Ref		body;
  TxContext		context;
  ZmRef<ZiIOBuf>	frame;
  ZuDateTime		original;
  bool			completed = false;
};

struct Engine : LinkTx<Engine> {
  Values<Task>		tasks;
  Values<Observation>	observed;
  Values<TxHistory>	published;
  Values<PeerReject>	peerRejects;
  AppQueue		appQueue;
  Values<Selection>	selections;
  uint64_t		appBytes = 0;
  uint64_t		appLimit = Fmt::Limits::FrameBytes;
  unsigned		dequeues = 0;
  Values<Limit>		limits;
  Values<Ack>		acks;
  Values<TxNotice>	notices;
  Values<Output>	output;
  Values<TxHistory>	history;
  Values<TxContext>	archives;
  Values<uint64_t>	archiveEnds;
  Values<TxContext>	released;
  Values<unsigned>	order;
  ZuDateTime		stamp{ZuDateTimeScan::FIX{}, "20261027-12:00:00"};
  ZuTime		now{100};
  ZuTime		armed;
  uint64_t		idleConnection = 0;
  uint64_t		idleTicket = 0;
  unsigned		idleArms = 0;
  unsigned		idleCancels = 0;
  Error			error;
  unsigned		task = 0;
  unsigned		loadResult = TxLoad::Accepted;
  uint64_t		loadTicket = 0;
  uint64_t		releaseTicket = 0;
  uint64_t		change = 0;
  unsigned		flushed = 0;
  unsigned		completions = 0;
  bool			block = false;
  bool			delayRelease = false;
  bool			delayHistory = false;
  uint64_t		historyTicket = 0;

  explicit Engine(ControlLimits limits = {}, SessionConfig c = config(), TxLimits tx = {}) :
    LinkTx<Engine>{ZuMv(c), tx, limits} { }

  bool enqueueTx_(TxBody::Ref body) {
    auto charge = uint64_t(body->size) + sizeof(AppQueue::Node);
    if (charge > appLimit || appBytes > appLimit - charge) return false;
    appBytes += charge; appQueue.push(ZuMv(body)); return true;
  }

  TxBody::Ref dequeueTx_() {
    ++dequeues;
    auto node = appQueue.shift();
    if (!node) return {};
    appBytes -= uint64_t(node->data()->size) + sizeof(AppQueue::Node);
    return ZuMv(node->data());
  }

  void selectedTx_(TxBody::Ref body, TxContext context, unsigned result) {
    selections.push(Selection{ZuMv(body), context, result});
  }

  void sessionTx_(TxBody::Ref body, TxContext context) {
    observed.push(Observation{ZuMv(body), context, unsigned(output.length()), flushed});
  }

  void publishedTx_(TxBody::Ref b, TxContext c, ZuDateTime original) {
    if (!c.replay && (!b->administrative() || b->msgType() == "3"))
      history.push(TxHistory{b, original, c.epoch, c.seqNo});
    published.push(TxHistory{ZuMv(b), original, c.epoch, c.seqNo});
  }

  void rejectedTx_(PeerReject report) { peerRejects.push(ZuMv(report)); }

  bool queueOrder(uint64_t id) {
    Error e;
    auto body = TxBody::encode<OrderMsg>(Order{"queued"}, id, e);
    return !e && enqueue_(ZuMv(body));
  }

  void txSchedule_(unsigned kind, uint64_t ticket) { tasks.push(Task{kind, ticket}); }

  unsigned associateTx_(TxBody::Ref, TxContext, uint64_t ticket) {
    loadTicket = ticket; return loadResult;
  }

  bool releaseTx_(TxBody::Ref, TxContext context, uint64_t ticket) {
    releaseTicket = ticket; released.push(context); return !delayRelease;
  }

  TxReload retrieveTx_(TxContext context, uint64_t, uint64_t ticket) {
    auto epoch = context.epoch, key = context.seqNo;
    historyTicket = ticket;
    if (delayHistory) return {{}, true};
    for (unsigned i = 0, n = history.length(); i < n; ++i)
      if (history[i].epoch == epoch && history[i].seqNo == key) return {history[i]};
    return {};
  }

  void archiveTx_(uint64_t epoch, uint64_t begin, uint64_t end) {
    archives.push(TxContext{epoch, 0, begin});
    archiveEnds.push(end);
  }

  ZuDateTime clockTx_() { return stamp; }
  ZuTime nowTx_() { return now; }

  void idleArmTx_(uint64_t connection, uint64_t ticket, ZuTime deadline) {
    idleConnection = connection; idleTicket = ticket; armed = deadline; ++idleArms;
  }

  void idleCancelTx_() { armed.null(); ++idleCancels; }

  void faultTx_(TxContext, Error e) { error = e; }

  bool admitFrameTx_(TxBody::Ref body, TxContext context, ZmRef<ZiIOBuf> frame,
    ZuDateTime original) {
    if (block) return false;
    order.push(1);
    output.push(Output{ZuMv(body), context, ZuMv(frame), original});
    return true;
  }

  void notifyTx_(TxNotice n) { order.push(2); notices.push(n); }

  void flushTx_() { order.push(3); flushed = output.length(); }

  void completeTx_(TxBody::Ref, TxContext, bool) { ++completions; }

  void sequenceLimitTx_(TxContext context, uint64_t maximum, unsigned headroom) {
    limits.push(Limit{context, maximum, headroom});
  }

  void acknowledgedTx_(TxContext context, uint64_t previous, uint64_t next) {
    acks.push(Ack{context, previous, next});
  }

  bool step() {
    if (task == tasks.length()) return false;
    auto value = tasks[task++]; run_(value.kind, value.ticket); return true;
  }

  void drain() { while (step()); }

  SessionEvent event(unsigned state, unsigned actions = 0, uint64_t connection = 1) {
    return {.actions = actions, .state = state, .connection = connection,
      .epoch = 1, .change = ++change, .heartBtInt = 30};
  }

  unsigned send(SessionEvent e, unsigned action, ZuCSpan echo = {}) {
    LinkState source{config()};
    Error result;
    auto body = controlBody(action, e, source, 1, echo, result, Limits::FrameBytes);
    if (result) return TxSelect::Invalid;
    return apply_(e, ZuMv(body));
  }

  void start(uint64_t connection = 1) {
    apply_(event(State::Logon, 0, connection));
    auto e = event(State::Logon, SessionAction::SendLogon, connection);
    send(e, SessionAction::SendLogon); drain();
  }

  bool complete(unsigned i, bool ok = true) {
    auto &o = output[i];
    if (o.completed) return false;
    o.completed = true;
    sent_(o.body, o.context, ok); return true;
  }

  unsigned sendOrder(uint64_t id) {
    Error e;
    auto body = TxBody::encode<OrderMsg>(Order{"order"}, id, e);
    return candidate_(ZuMv(body));
  }
};

static void factory()
{
  ZuTestScope(factory);
  for (unsigned mode = Transport::FIX4; mode <= Transport::FIXT11; ++mode) {
    LinkState s{config(mode)};
    Error e;
    SessionEvent event{.connection = 2, .epoch = 9, .probe = 41};
    auto body = controlBody(SessionAction::SendLogon, event, s, 17, {}, e, 8192,
      Limits{.frameBytes = 1024});
    ZuCheck(body && !e && body->msgType() == "A");
    Session::Logon logon;
    ZuCheck(!load(logon, body->body()));
    ZuCheck(logon.maxMessageSize == 8192);
    ZuCheck(logon.nextExpected == (mode == Transport::FIX44A ? 17 : 0));
    ZuCheck(logon.defaultAppVerID == (mode == Transport::FIXT11 ? "9" : ""));
    auto refused = controlBody(SessionAction::SendLogon, event, s, 17, {}, e,
      8192, Limits{.frameBytes = 1});
    ZuCheck(!refused && e.code == ErrorCode::Size);
    e = {};
    auto probe = controlBody(SessionAction::SendProbe, event, s, 17, {}, e, Limits::FrameBytes);
    Session::TestReq request;
    ZuCheck(probe && probe->id() == 41 && !load(request, probe->body()) &&
      request.testReqID == "Zfix/0/9/2/41");
    auto wide = event;
    wide.instance = uint128_t(-1) - 1;
    wide.epoch = wide.connection = wide.probe = UINT64_MAX - 1;
    auto largest = controlBody(SessionAction::SendProbe, wide, s, 17, {}, e,
      Limits::FrameBytes);
    constexpr auto expected = "Zfix/340282366920938463463374607431768211454/"
      "18446744073709551614/18446744073709551614/18446744073709551614"_z;
    ZuCheck(largest && !e && !load(request, largest->body()) && request.testReqID == expected);
    constexpr auto opaque = "0001 abc/xyz"_z;
    auto echo = controlBody(SessionAction::Echo, event, s, 17, opaque, e, Limits::FrameBytes);
    Session::HeartBt hb;
    ZuCheck(echo && !load(hb, echo->body()) && hb.testReqID == opaque);
    auto empty = controlBody(SessionAction::Echo, event, s, 17, ""_z, e, Limits::FrameBytes);
    ZuCheck(empty && empty->body() == "112=\1"_z);
    ZuCheck(!controlBody(SessionAction::Echo, event, s, 17, {}, e,
      Limits::FrameBytes) && e.tag == 112);
  }
}

static void publication()
{
  ZuTestScope(publication);
  Engine tx;
  tx.start();
  ZuCheck(tx.output.length() == 1 && tx.flushed == 1 && tx.inFlight_() == 1);
  ZuCheck(tx.published.length() == 1 && tx.published[0].originalTime == tx.stamp &&
    tx.published[0].body == tx.output[0].body && tx.published[0].seqNo == 1);
  ZuCheck(tx.order.length() >= 3 && tx.order[0] == 1 && tx.order[1] == 2 && tx.order[2] == 3);
  ZuCheck(tx.notices[0].kind == TxNoticeKind::Logon && tx.notices[0].context.seqNo == 1 &&
    tx.notices[0].loaded == 2 && tx.notices[0].published == 2);
  ZuCheck(!tx.controlBytes_() && tx.acknowledged_() == 1 && tx.complete(0));
  ZuCheck(!tx.inFlight_() && tx.acknowledged_() == 1 && tx.completions == 1);
  auto e = tx.event(State::Established, SessionAction::Ready);
  ZuCheck(tx.apply_(e) == TxSelect::Accepted && tx.sendOrder(7) == TxSelect::Accepted);
  tx.drain();
  ZuCheck(tx.output.length() == 2 && tx.output[1].context.seqNo == 2
    && tx.output[1].body->id() == 7);
}

static void pending()
{
  ZuTestScope(pending);
  Engine tx;
  tx.loadResult = TxLoad::Pending;
  tx.start();
  ZuCheck(!tx.output.length() && tx.loading_() && tx.controlBytes_() && tx.next_() == 1);
  auto ticket = tx.loadTicket;
  auto e = tx.event(State::Logon, SessionAction::SendHeartbeat);
  ZuCheck(tx.send(e, SessionAction::SendHeartbeat) == TxSelect::Accepted);
  tx.drain();
  ZuCheck(!tx.output.length() && tx.next_() == 1);
  tx.loadResult = TxLoad::Accepted;
  ZuCheck(tx.loaded_(ticket, true) == TxSelect::Accepted); tx.drain();
  ZuCheck(tx.output.length() == 2 && tx.output[0].body->msgType() == "A" &&
    tx.output[1].body->msgType() == "0" && tx.output[1].context.seqNo == 2
      && !tx.controlBytes_());
  ZuCheck(tx.loaded_(ticket, true) == TxSelect::Invalid);
}

static void reconnect()
{
  ZuTestScope(reconnect);
  Engine tx;
  tx.block = true;
  tx.start();
  ZuCheck(!tx.output.length() && tx.next_() == 2 && tx.published_() == 1);
  tx.apply_(tx.event(State::Closing, SessionAction::Close));
  tx.apply_(tx.event(State::Logon, 0, 2));
  auto e = tx.event(State::Logon, SessionAction::SendLogon, 2);
  ZuCheck(tx.send(e, SessionAction::SendLogon) == TxSelect::Accepted); tx.drain();
  ZuCheck(!tx.output.length() && tx.next_() == 3 && tx.published_() == 1 &&
    !tx.released.length());
  tx.block = false;
  ZuCheck(tx.writable_(2)); tx.drain();
  ZuCheck(tx.output.length() == 2 && tx.output[0].context.connection == 2 &&
    tx.output[0].context.seqNo == 2 && tx.output[0].body->msgType() == "A");
  Session::Msg fill;
  Fmt::Header header;
  Fmt::Trailer trailer;
  ZuCheck(!Session::read(frame(ZuCSpan{tx.output[1].frame->cspan()}).view,
    fill, header, trailer));
  ZuCheck(fill.is<Session::SeqReset>() && fill.p<Session::SeqReset>().gapFill &&
    fill.p<Session::SeqReset>().newSeqNo == 2 && header.seqNo == 1 && header.possDup);
  unsigned logons = 0;
  for (const auto &notice : tx.notices)
    if (notice.kind == TxNoticeKind::Logon) ++logons;
  ZuCheck(logons == 1 && tx.next_() == 3 && tx.published_() == 3 &&
    tx.acknowledged_() == 1 && !tx.queuedBytes_() && !tx.released.length());
  ZuCheck(tx.writable_(2)); tx.drain();
  ZuCheck(tx.output.length() == 2);
}

static void displaceLive()
{
  ZuTestScope(displaceLive);
  Engine tx; tx.start();
  tx.apply_(tx.event(State::Established, SessionAction::Ready)); tx.drain();
  ZuCheck(tx.sendOrder(101) == TxSelect::Accepted); tx.drain();
  auto oldCount = tx.output.length();
  auto next = tx.next_();
  ZuCheck(oldCount == 2 && tx.inFlight_() == oldCount);
  // No predecessor Close event or completion is needed before opening B.
  ZuCheck(tx.apply_(tx.event(State::Logon, 0, 2)) == TxSelect::Accepted);
  auto e = tx.event(State::Logon, SessionAction::SendLogon, 2);
  ZuCheck(tx.send(e, SessionAction::SendLogon) == TxSelect::Accepted); tx.drain();
  ZuCheck(tx.connection_() == 2 && tx.epoch_() == 1 && tx.next_() == next + 1 &&
    tx.output.length() == oldCount + 1 && tx.inFlight_() == 1);
  ZuCheck(tx.complete(0, false) && tx.complete(1, false));
  ZuCheck(!tx.error && tx.connection_() == 2 && tx.inFlight_() == 1);
  tx.close_(1);
  ZuCheck(tx.complete(oldCount) && !tx.inFlight_() && !tx.error);
  tx.apply_(tx.event(State::Established, SessionAction::Ready, 2)); tx.drain();
  ZuCheck(tx.sendOrder(102) == TxSelect::Accepted); tx.drain();
  ZuCheck(tx.output.length() == oldCount + 2 &&
    tx.output[oldCount + 1].context.connection == 2 &&
    tx.output[oldCount + 1].context.seqNo == next + 1);
}

static void replayBeforeLogon()
{
  ZuTestScope(replayBeforeLogon);
  Engine tx;
  tx.start();
  tx.apply_(tx.event(State::Established, SessionAction::Ready));
  ZuCheck(tx.sendOrder(3) == TxSelect::Accepted); tx.drain();
  ZuCheck(tx.acknowledge_(1, 1, 3)); tx.drain();
  tx.block = true;
  ZuCheck(tx.replay_(1, 1, 2, 2)); tx.drain();
  tx.apply_(tx.event(State::Closing, SessionAction::Close));
  tx.start(2);
  ZuCheck(tx.output.length() == 2 && tx.next_() == 4 && tx.published_() == 3);
  tx.block = false;
  ZuCheck(tx.writable_(2)); tx.drain();
  ZuCheck(tx.output.length() == 4 && tx.output[2].body->msgType() == "A" &&
    tx.output[2].context.connection == 2 && !tx.output[2].context.replay);
  ZuCheck(tx.output[3].body->msgType() == "D" && tx.output[3].context.seqNo == 2 &&
    tx.output[3].context.replay && tx.published_() == 4);
}

static void priorityPendingRecovery()
{
  ZuTestScope(priorityPendingRecovery);
  for (unsigned mode = Transport::FIX4; mode <= Transport::FIXT11; ++mode)
    for (bool lifecycle : {false, true}) {
      Engine tx{{}, config(mode)};
      Error error;
      auto body = TxBody::encode<OrderMsg>(Order{"pending"}, 42, error);
      ZuCheck(tx.restore_(1, 2, 1, 1));
      tx.delayHistory = true;
      tx.start();
      ZuCheck(tx.output.length() == 1 && tx.output[0].body->msgType() == "A" &&
	tx.output[0].context.seqNo == 2 && tx.published_() == 1 && tx.waiting_());
      auto event = tx.event(State::Established, lifecycle ? SessionAction::Replay : 0);
      if (lifecycle) { event.replayBegin = 1; event.replayEnd = 3; }
      ZuCheck(tx.apply_(event) == TxSelect::Accepted);
      if (!lifecycle) ZuCheck(tx.replay_(1, 1, 1));
      auto progress = tx.progress_();
      ZuCheck(progress.replayBegin == 1 && progress.replayEnd == 3);
      tx.drain();
      ZuCheck(tx.waiting_() && tx.output.length() == 1 && !tx.error);
      auto before = tx.notices.length();
      tx.delayHistory = false;
      ZuCheck(tx.reloaded_(tx.historyTicket, TxHistory{body, {}, 1, 1})); tx.drain();
      bool recovered = false;
      for (unsigned i = before, n = tx.notices.length(); i < n; ++i)
	if (tx.notices[i].kind == TxNoticeKind::Recovery && tx.notices[i].done)
	  recovered = true;
      ZuCheck(recovered && !tx.waiting_() && !tx.error && tx.next_() == 3 &&
	tx.published_() == 3 && !tx.queuedBytes_());
      ZuCheck(tx.output.length() == 4 && tx.output[0].body->msgType() == "A" &&
	tx.output[0].context.seqNo == 2 && tx.output[1].context.seqNo == 1 &&
	!tx.output[1].context.replay && tx.output[2].context.seqNo == 1 &&
	tx.output[2].context.replay);
    }
}

static void closeAfterOutput()
{
  ZuTestScope(closeAfterOutput);
  Engine tx;
  tx.start(); tx.complete(0);
  auto e = tx.event(State::Closing, SessionAction::SendLogout | SessionAction::CloseAfterTx);
  ZuCheck(tx.send(e, SessionAction::SendLogout) == TxSelect::Accepted); tx.drain();
  ZuCheck(tx.output.length() == 2 && tx.inFlight_() == 1 && tx.notices.length() == 1);
  ZuCheck(tx.complete(1));
  ZuCheck(!tx.inFlight_() && tx.notices.length() == 2 &&
    tx.notices[1].kind == TxNoticeKind::Drained && tx.acknowledged_() == 1);
  ZuCheck(!tx.writable_(1));
}

static void bounds()
{
  ZuTestScope(bounds);
  Engine tx{{1}};
  tx.start();
  ZuCheck(!tx.output.length() && tx.next_() == 1 && tx.controlBytes_() == 0);
  LinkState s{config()}; Error e;
  auto event = tx.event(State::Logon, SessionAction::SendHeartbeat);
  auto body = controlBody(SessionAction::SendHeartbeat, event, s, 1, {}, e, Limits::FrameBytes);
  ZuCheck(tx.control_(1, 1, body) == TxSelect::Overload && tx.next_() == 1);
  ZuCheck(tx.control_(2, 1, body) == TxSelect::Invalid && tx.control_(1, 2,
    body) == TxSelect::Invalid);
  auto closing = tx.event(State::Closing,
    SessionAction::SendLogout | SessionAction::CloseAfterTx);
  ZuCheck(tx.send(closing, SessionAction::SendLogout) == TxSelect::Overload);
  tx.drain();
  ZuCheck(tx.notices.length() == 0 && !tx.inFlight_());
  auto stale = event; stale.change = 0;
  ZuCheck(tx.apply_(stale, body) == TxSelect::Invalid);
}

static void controlMemory()
{
  ZuTestScope(controlMemory);
  Session::Msg c; c.p<Session::HeartBt>(Session::HeartBt{});
  Error error; auto body = TxBody::control(c, error);
  ZuCheck(body && !error);
  auto length = uint64_t(body->size) + 1;
  ZtString text{length, length + 1};
  memset(text.data(), 'x', text.length());
  c.p<Session::TestReq>(Session::TestReq{.testReqID = text});
  body = TxBody::control(c, error);
  ZuCheck(body && !error && (body->data__ & ZiIOBuf::Jumbo));
  // Exposed capacity omits the body object/builtin storage and jumbo heap
  // prefix/cache class. A limit sufficient for the old charge must refuse.
  uint64_t capacity = body->size + sizeof(ControlQueue::Node);
  Engine small{{capacity}};
  small.apply_(small.event(State::Logon, 0));
  auto next = small.next_();
  ZuCheck(small.control_(1, 1, body) == TxSelect::Overload &&
    !small.controlBytes_() && small.next_() == next);
  uint64_t charge = body->memory() + ZmHeapAllocSize<sizeof(ControlQueue::Node)>{};
  ZuCheck(charge > capacity);
  Engine exact{{charge}};
  exact.apply_(exact.event(State::Logon, 0));
  ZuCheck(exact.control_(1, 1, body) == TxSelect::Accepted && exact.controlBytes_() == charge);
  ZuCheck(exact.control_(1, 1, body) == TxSelect::Overload && exact.next_() == 1);
  exact.close_(1); ZuCheck(!exact.controlBytes_());
  exact.apply_(exact.event(State::Logon, 0, 2));
  ZuCheck(exact.control_(2, 1, body) == TxSelect::Accepted && exact.controlBytes_() == charge);
  exact.close_(2); ZuCheck(!exact.controlBytes_());
}

static void restoredRecovery()
{
  ZuTestScope(restoredRecovery);
  Engine tx;
  Checkpoint state;
  ZuCheck(tx.snapshot_(state));
  state.tx = {3, 3, 1, 1, 2};
  ZuCheck(tx.restore_(state));
  tx.delayHistory = true;
  tx.start();
  ZuCheck(tx.waiting_() && tx.output.length() == 1 && tx.output[0].body->msgType() == "A");
  ZuCheck(tx.apply_(tx.event(State::Established)) == TxSelect::Accepted);
  ZuCheck(tx.notices.length() == 2 && tx.notices[1].kind == TxNoticeKind::Recovery &&
    !tx.notices[1].done);
  Fmt::Error error;
  auto body = TxBody::encode<OrderMsg>(Order{"history"}, 17, error);
  ZuCheck(tx.reloaded_(tx.historyTicket, TxHistory{body, tx.stamp, 1, 1}));
  tx.drain();
  ZuCheck(tx.output.length() == 2 && tx.output[1].context.replay &&
    tx.output[1].context.seqNo == 1 && tx.next_() == 4 && tx.published_() == 4);
  ZuCheck(tx.notices.length() == 3 && tx.notices[2].kind == TxNoticeKind::Recovery &&
    tx.notices[2].done);
}

static void idleTraffic()
{
  ZuTestScope(idleTraffic);
  Engine tx;
  tx.start();
  ZuCheck(!*tx.idleDeadline_() && !tx.idleArms);
  auto event = tx.event(State::Established, SessionAction::Ready);
  event.heartBtInt = 7;
  ZuCheck(tx.apply_(event) == TxSelect::Accepted && tx.idleDeadline_() == ZuTime{107});
  ZuCheck(tx.armed == ZuTime{107} && tx.idleArms == 1);
  auto first = tx.idleTicket;
  tx.now = ZuTime{105};
  ZuCheck(tx.sendOrder(17) == TxSelect::Accepted); tx.drain();
  ZuCheck(tx.idleDeadline_() == ZuTime{112} && tx.idleArms == 1);
  tx.now = ZuTime{107};
  ZuCheck(tx.idle_(1, first) && tx.armed == ZuTime{112} && tx.idleArms == 2);
  ZuCheck(!tx.idle_(1, first) && tx.output.length() == 2);
  tx.now = ZuTime{112};
  auto ticket = tx.idleTicket;
  ZuCheck(tx.idle_(1, ticket) && !tx.idle_(1, ticket)); tx.drain();
  ZuCheck(tx.output.length() == 3 && tx.output[2].body->msgType() == "0" &&
    !tx.output[2].body->body().length() && tx.output[2].context.seqNo == 3);
  ZuCheck(tx.idleDeadline_() == ZuTime{119} && tx.acknowledged_() == 1);
  tx.now = ZuTime{114};
  ZuCheck(tx.replay_(1, 1, 2, 2)); tx.drain();
  ZuCheck(tx.output.length() == 4 && tx.output[3].context.replay &&
    tx.idleDeadline_() == ZuTime{121} && tx.next_() == 4);
  tx.now = ZuTime{117};
  ZuCheck(tx.complete(3) && tx.idleDeadline_() == ZuTime{121});
}

static void idleBackpressure()
{
  ZuTestScope(idleBackpressure);
  Engine tx;
  tx.start();
  auto event = tx.event(State::Established, SessionAction::Ready);
  event.heartBtInt = 7;
  ZuCheck(tx.apply_(event) == TxSelect::Accepted);
  tx.now = ZuTime{107}; tx.block = true;
  ZuCheck(tx.idle_(1, tx.idleTicket)); tx.drain();
  ZuCheck(tx.output.length() == 1 && tx.next_() == 3 && tx.published_() == 2);
  tx.now = ZuTime{114};
  ZuCheck(tx.idle_(1, tx.idleTicket)); tx.drain();
  ZuCheck(tx.next_() == 3 && tx.output.length() == 1 && !tx.controlBytes_());
  tx.now = ZuTime{115}; tx.block = false;
  ZuCheck(tx.writable_(1)); tx.drain();
  ZuCheck(tx.output.length() == 2 && tx.output[1].body->msgType() == "0" &&
    tx.output[1].context.seqNo == 2 && tx.idleDeadline_() == ZuTime{122});
  auto ticket = tx.idleTicket;
  ZuCheck(tx.apply_(tx.event(State::Closing, SessionAction::Close)) == TxSelect::Refused);
  ZuCheck(tx.idleCancels == 1 && !*tx.idleDeadline_() && !tx.idle_(1, ticket));
  tx.start(2);
  auto next = tx.event(State::Established, SessionAction::Ready, 2);
  next.heartBtInt = 7;
  ZuCheck(tx.apply_(next) == TxSelect::Accepted);
  tx.now = ZuTime{130};
  ZuCheck(!tx.idle_(1, ticket) && !tx.idle_(2, ticket));
  auto count = tx.output.length();
  ZuCheck(tx.idle_(2, tx.idleTicket)); tx.drain();
  ZuCheck(tx.output.length() == count + 1 && tx.output[count].body->msgType() == "0");
}

static void idleZeroAndLogout()
{
  ZuTestScope(idleZeroAndLogout);
  Engine zero;
  zero.start();
  auto event = zero.event(State::Established, SessionAction::Ready);
  event.heartBtInt = 0;
  ZuCheck(zero.apply_(event) == TxSelect::Accepted && !zero.idleArms);
  zero.now = ZuTime{1000};
  ZuCheck(zero.sendOrder(1) == TxSelect::Accepted); zero.drain();
  ZuCheck(!*zero.idleDeadline_() && !zero.idle_(1, 0));

  Engine tx;
  tx.start();
  ZuCheck(tx.apply_(tx.event(State::Established)) == TxSelect::Accepted && tx.idleArms == 1);
  auto ticket = tx.idleTicket;
  // The high-Session::Logout early acknowledgement still has Established state.
  auto logout = tx.event(State::Established, SessionAction::SendLogout | SessionAction::Pause);
  ZuCheck(tx.send(logout, SessionAction::SendLogout) == TxSelect::Accepted); tx.drain();
  ZuCheck(tx.idleCancels == 1 && !*tx.idleDeadline_());
  tx.now = ZuTime{200};
  ZuCheck(!tx.idle_(1, ticket) && tx.output.length() == 2 &&
    tx.output[1].body->msgType() == "5");
}

static void acknowledgement()
{
  ZuTestScope(acknowledgement);
  Engine tx;
  tx.start();
  ZuCheck(tx.apply_(tx.event(State::Established, SessionAction::Ready)) == TxSelect::Accepted);
  ZuCheck(tx.sendOrder(17) == TxSelect::Accepted && tx.sendOrder(18) == TxSelect::Accepted);
    tx.drain();
  ZuCheck(tx.published_() == 4 && tx.acknowledged_() == 1 && !tx.acks.length());
  auto e = tx.event(State::Established, SessionAction::Ackd);
  e.acknowledged = 3;
  ZuCheck(tx.apply_(e) == TxSelect::Accepted && tx.acknowledged_() == 3);
  ZuCheck(tx.acks.length() == 1 && tx.acks[0].context.epoch == 1 &&
    tx.acks[0].context.connection == 1 && tx.acks[0].previous == 1 && tx.acks[0].next == 3);
  ZuCheck(tx.apply_(e) == TxSelect::Accepted && tx.acks.length() == 1);
  e.acknowledged = 5;
  ZuCheck(tx.apply_(e) == TxSelect::Invalid && tx.acks.length() == 1);
  e.acknowledged = 3; ++e.epoch;
  ZuCheck(tx.apply_(e) == TxSelect::Invalid && tx.acks.length() == 1);
  --e.epoch; ++e.connection;
  ZuCheck(tx.apply_(e) == TxSelect::Invalid && tx.acks.length() == 1);
  ZuCheck(!tx.acknowledge_(1, 2, 3) && !tx.replay_(1, 2, 1, 2) &&
    tx.acks.length() == 1 && tx.acknowledged_() == 3);

}

static void drainBoundary()
{
  ZuTestScope(drainBoundary);
  Engine tx;
  tx.start();
  ZuCheck(tx.apply_(tx.event(State::Established, SessionAction::Ready)) == TxSelect::Accepted);
  tx.loadResult = TxLoad::Pending;
  ZuCheck(tx.sendOrder(17) == TxSelect::Pending);
  auto e = tx.event(State::Established, SessionAction::Drain | SessionAction::Pause);
  e.drain = 23;
  ZuCheck(tx.apply_(e) == TxSelect::Accepted && tx.sendOrder(18) == TxSelect::Held);
  auto count = tx.notices.length();
  ZuCheck(tx.loaded_(tx.loadTicket, true) == TxSelect::Accepted); tx.drain();
  ZuCheck(tx.output.length() == 2 && tx.notices.length() == count);
  ZuCheck(tx.complete(0) && tx.notices.length() == count);
  ZuCheck(tx.complete(1) && tx.notices.length() == count + 1);
  const auto &notice = tx.notices[count];
  ZuCheck(notice.kind == TxNoticeKind::DrainBoundary && notice.marker == 23 &&
    notice.loaded == 3 && notice.published == 3 && notice.acknowledged == 1);
  ZuCheck(tx.apply_(tx.event(State::Established, SessionAction::Ready)) == TxSelect::Accepted &&
    tx.sendOrder(19) == TxSelect::Held);
  tx.loadResult = TxLoad::Accepted;
  Session::Msg heartbeat; heartbeat.p<Session::HeartBt>(Session::HeartBt{});
  Error error;
  auto body = TxBody::control(heartbeat, error);
  ZuCheck(tx.control_(1, 1, body) == TxSelect::Accepted); tx.drain();
  ZuCheck(tx.output.length() == 3 && tx.complete(2) && tx.notices.length() == count + 1);
  ++e.drain; e.change = ++tx.change;
  ZuCheck(tx.apply_(e) == TxSelect::Invalid);

  Engine refused;
  refused.start(); refused.complete(0);
  refused.apply_(refused.event(State::Established, SessionAction::Ready));
  refused.loadResult = TxLoad::Pending;
  ZuCheck(refused.sendOrder(7) == TxSelect::Pending);
  auto drain = refused.event(State::Established, SessionAction::Drain);
  drain.drain = 1;
  ZuCheck(refused.apply_(drain) == TxSelect::Accepted);
  auto before = refused.notices.length();
  ZuCheck(refused.loaded_(refused.loadTicket, false) == TxSelect::Refused);
  ZuCheck(refused.notices.length() == before + 1 &&
    refused.notices[before].kind == TxNoticeKind::DrainBoundary &&
    refused.notices[before].loaded == 2 && refused.next_() == 2);
}

static void drainReset(Engine &tx)
{
  tx.start(); tx.complete(0);
  tx.apply_(tx.event(State::Established, SessionAction::Ready));
  tx.sendOrder(17); tx.drain(); tx.complete(1);
  auto e = tx.event(State::Established, SessionAction::Drain | SessionAction::Pause);
  e.drain = 7; tx.apply_(e);
  e = tx.event(State::Established, SessionAction::Drained | SessionAction::Ackd);
  e.drain = 7; e.acknowledged = 3; tx.apply_(e); tx.drain();
}

static unsigned prepareReset(Engine &tx)
{
  auto e = tx.event(State::Resetting, SessionAction::ResetPrepare | SessionAction::Pause);
  e.drain = 7; e.nextEpoch = 2;
  return tx.apply_(e);
}

static void resetPreparation()
{
  ZuTestScope(resetPreparation);
  Engine tx; drainReset(tx);
  ZuCheck(!tx.retained_() && tx.archives.length() == 1 &&
    tx.archives[0].seqNo == 1 && tx.archiveEnds[0] == 3);
  ZuCheck(prepareReset(tx) == TxSelect::Accepted && tx.epoch_() == 1); tx.drain();
  ZuCheck(tx.epoch_() == 2 && tx.preparing_() && !tx.retained_() && !tx.error &&
    tx.next_() == 1 && tx.output.length() == 2);
  const auto &notice = tx.notices[tx.notices.length() - 1];
  ZuCheck(notice.kind == TxNoticeKind::ResetReady && notice.context.epoch == 1 &&
    notice.nextEpoch == 2 && notice.marker == 7 && notice.request);
  auto ready = tx.event(State::Resetting, SessionAction::ResetReady);
  ready.epoch = ready.nextEpoch = 2; ready.drain = 7; ready.ticket = notice.request;
  auto bad = ready; ++bad.ticket;
  ZuCheck(tx.apply_(bad) == TxSelect::Invalid && tx.preparing_());
  bad = ready; ++bad.drain;
  ZuCheck(tx.apply_(bad) == TxSelect::Invalid && tx.preparing_());
  bad = ready; ++bad.nextEpoch;
  ZuCheck(tx.apply_(bad) == TxSelect::Invalid && tx.preparing_());
  bad = ready; bad.epoch = 1;
  ZuCheck(tx.apply_(bad) == TxSelect::Invalid && tx.preparing_());
  bad = ready; ++bad.connection;
  ZuCheck(tx.apply_(bad) == TxSelect::Invalid && tx.preparing_());
  ZuCheck(tx.apply_(ready) == TxSelect::Accepted && !tx.preparing_());
  ZuCheck(tx.sendOrder(18) == TxSelect::Held);
  auto old = tx.event(State::Closing, SessionAction::Close);
  ZuCheck(tx.apply_(old) == TxSelect::Invalid && tx.epoch_() == 2);

  Engine aborted; drainReset(aborted);
  ZuCheck(prepareReset(aborted) == TxSelect::Accepted); aborted.drain();
  ZuCheck(aborted.epoch_() == 2 && aborted.preparing_());
  old = aborted.event(State::Closing, SessionAction::Close);
  old.nextEpoch = 2; old.drain = 7;
  ZuCheck(aborted.apply_(old) == TxSelect::Refused && !aborted.preparing_() &&
    aborted.epoch_() == 2 && aborted.next_() == 1 && aborted.output.length() == 2);
}

static void resetReplacement()
{
  ZuTestScope(resetReplacement);
  for (bool committed : {false, true}) {
    Engine tx; drainReset(tx);
    ZuCheck(prepareReset(tx) == TxSelect::Accepted);
    if (committed) tx.drain();
    auto e = tx.event(State::Logon, SessionAction::Pause, 2);
    e.drain = 7; e.nextEpoch = 2;
    ZuCheck(tx.apply_(e) == TxSelect::Accepted && tx.connection_() == 2 &&
      tx.preparing_() && !tx.inFlight_() && tx.epoch_() == (committed ? 2 : 1));
    // Wire's old failed completion can arrive before the saved reset notice.
    // It settles its physical attempt without failing the successor/reset.
    auto completed = tx.completions;
    tx.sent_(tx.output[0].body, tx.output[0].context, false);
    ZuCheck(!tx.error && tx.completions == completed + 1 && tx.preparing_());
    tx.drain();
    ZuCheck(tx.epoch_() == 2 && tx.next_() == 1 && tx.preparing_() && !tx.error);
    const auto &notice = tx.notices[tx.notices.length() - 1];
    ZuCheck(notice.kind == TxNoticeKind::ResetReady && notice.nextEpoch == 2 &&
      notice.context.epoch == 1 && notice.context.connection == (committed ? 1 : 2));
    e = tx.event(State::Logon, SessionAction::ResetReady |
      SessionAction::SendLogon | SessionAction::RetryInput, 2);
    e.epoch = e.nextEpoch = 2; e.drain = 7; e.ticket = notice.request;
    ZuCheck(tx.send(e, SessionAction::SendLogon) == TxSelect::Accepted);
    tx.drain();
    ZuCheck(!tx.error && !tx.preparing_() && tx.output.length() == 3 &&
      tx.output[2].context.connection == 2 && tx.output[2].context.epoch == 2 &&
      tx.output[2].context.seqNo == 1 && tx.output[2].body->msgType() == "A");
  }
}

static void resetBinding()
{
  ZuTestScope(resetBinding);
  for (bool committed : {false, true}) {
    Engine tx; drainReset(tx);
    ZuCheck(prepareReset(tx) == TxSelect::Accepted);
    if (committed) tx.drain();
    auto bound = tx.event(State::Disconnected, 0, 2);
    bound.nextEpoch = 2; bound.drain = 7;
    ZuCheck(tx.bind_(bound) == TxSelect::Accepted && tx.connection_() == 2 &&
      tx.preparing_() && tx.output.length() == 2);
    tx.drain();
    ZuCheck(tx.epoch_() == 2 && tx.preparing_() && !tx.error);
    auto ready = tx.event(State::Disconnected, SessionAction::ResetReady, 2);
    ready.epoch = ready.nextEpoch = 2; ready.drain = 7;
    ready.ticket = tx.notices[tx.notices.length() - 1].request;
    ZuCheck(tx.apply_(ready) == TxSelect::Accepted && !tx.preparing_());
    tx.drain();
    ZuCheck(tx.output.length() == 2 && !tx.error && tx.sendOrder(18) == TxSelect::Held);
    auto opened = tx.event(State::Logon, SessionAction::SendLogon, 2);
    opened.epoch = opened.nextEpoch = 2;
    ZuCheck(tx.send(opened, SessionAction::SendLogon) == TxSelect::Accepted);
    tx.drain();
    ZuCheck(tx.output.length() == 3 && tx.output[2].context.connection == 2 &&
      tx.output[2].context.epoch == 2 && tx.output[2].context.seqNo == 1 && !tx.error);
  }
}

static void earlyBinding()
{
  ZuTestScope(earlyBinding);
  Engine initial;
  ZuCheck(initial.bind_(initial.event(State::Disconnected)) == TxSelect::Accepted);
  ZuCheck(initial.queueOrder(99)); initial.drain();
  ZuCheck(initial.connection_() == 1 && initial.output.length() == 0 &&
    initial.next_() == 1 && initial.appQueue.count_() == 1 &&
    initial.sendOrder(100) == TxSelect::Held);
  initial.start();
  ZuCheck(initial.output.length() == 1 && initial.output[0].body->msgType() == "A");
  initial.apply_(initial.event(State::Established, SessionAction::Ready)); initial.drain();
  ZuCheck(initial.output.length() == 2 && initial.output[1].body->id() == 99);

  Engine tx; tx.start(); tx.complete(0);
  tx.apply_(tx.event(State::Established, SessionAction::Ready));
  tx.loadResult = TxLoad::Pending;
  ZuCheck(tx.queueOrder(101)); tx.drain();
  auto ticket = tx.loadTicket;
  ZuCheck(tx.loading_() && tx.next_() == 2);
  ZuCheck(tx.bind_(tx.event(State::Disconnected, 0, 2)) == TxSelect::Accepted &&
    tx.connection_() == 2 && tx.loading_() && tx.sendOrder(102) == TxSelect::Held);
  tx.loadResult = TxLoad::Accepted;
  ZuCheck(tx.loaded_(ticket, true) == TxSelect::Accepted); tx.drain();
  ZuCheck(!tx.loading_() && tx.next_() == 3 && tx.output.length() == 1 &&
    tx.selections.length() == 1 && tx.selections[0].body->id() == 101 &&
    tx.selections[0].result == TxSelect::Accepted && !tx.released.length());
  tx.sent_(tx.output[0].body, tx.output[0].context, false);
  ZuCheck(!tx.error && !tx.inFlight_());
  tx.start(2);
  // Assigned catch-up may follow the authenticated Logon before Ready; no
  // new application admission is needed to publish this already accepted work.
  ZuCheck(tx.output.length() == 3 && tx.output[1].body->msgType() == "A" &&
    tx.output[1].context.seqNo == 3 && tx.output[1].context.connection == 2 &&
    tx.output[2].body->id() == 101 && tx.output[2].context.seqNo == 2 &&
    tx.output[2].context.connection == 2);
  tx.apply_(tx.event(State::Established, SessionAction::Ready, 2)); tx.drain();
  ZuCheck(tx.output.length() == 3 && tx.output[2].body->id() == 101 &&
    tx.output[2].context.seqNo == 2 && tx.output[2].context.connection == 2 && !tx.error);
}

static void sequenceNotice()
{
  ZuTestScope(sequenceNotice);
  auto c = config(); c.sequenceLimit = 4;
  TxLimits limits; limits.controlHeadroom = 2;
  Engine tx{{}, c, limits}; tx.start();
  ZuCheck(!tx.limits.length() && tx.next_() == 2);
  ZuCheck(tx.apply_(tx.event(State::Established, SessionAction::Ready)) == TxSelect::Accepted);
  ZuCheck(tx.sendOrder(1) == TxSelect::Accepted && tx.limits.length() == 1 &&
    tx.limits[0].context.epoch == 1 && tx.limits[0].context.connection == 1 &&
    tx.limits[0].context.seqNo == 3 && tx.limits[0].maximum == 4 && tx.limits[0].headroom == 2);
  ZuCheck(tx.sendOrder(2) == TxSelect::Limit);
  auto e = tx.event(State::Established, SessionAction::SendHeartbeat);
  ZuCheck(tx.send(e, SessionAction::SendHeartbeat) == TxSelect::Accepted); tx.drain();
  ZuCheck(tx.output.length() == 3 && tx.next_() == 4 && tx.limits.length() == 1);
  e = tx.event(State::Logout, SessionAction::SendLogout | SessionAction::Pause);
  ZuCheck(tx.send(e, SessionAction::SendLogout) == TxSelect::Accepted); tx.drain();
  ZuCheck(!tx.error && tx.output.length() == 4 && tx.next_() == 5 && tx.limits.length() == 1);
}

static void initialReset()
{
  ZuTestScope(initialReset);
  for (unsigned n = 0; n < 3; ++n) {
    Engine tx;
    Checkpoint state; state.id = config().id; state.epoch = 1; state.fields = StateMask::Tx;
    switch (n) {
      case 0: state.tx = {3, 3, 3}; break;
      case 1: state.tx = {3, 3, 1}; break;
      default: state.tx = {3, 1, 1}; break;
    }
    ZuCheck(tx.restore_(state));
    auto e = tx.event(State::Logon, 0); e.nextEpoch = 1;
    auto before = tx.progress_();
    ZuCheck(tx.apply_(e) == TxSelect::Invalid && !tx.connection_() &&
      !tx.output.length() && tx.next_() == before.loaded && tx.published_() == before.sent);
  }
  Engine fresh;
  auto e = fresh.event(State::Logon, 0); e.nextEpoch = 2;
  ZuCheck(fresh.apply_(e) == TxSelect::Invalid && !fresh.connection_());
  e.nextEpoch = 1;
  ZuCheck(fresh.apply_(e) == TxSelect::Accepted && fresh.next_() == 1);
  Engine pending; pending.loadResult = TxLoad::Pending; pending.start();
  ZuCheck(pending.loading_() && pending.next_() == 1);
  pending.apply_(pending.event(State::Closing, SessionAction::Close));
  e = pending.event(State::Logon, 0, 2); e.nextEpoch = 1;
  ZuCheck(pending.apply_(e) == TxSelect::Invalid && pending.connection_() == 1 &&
    pending.loading_() && pending.next_() == 1 && !pending.output.length());
}

static void applicationQueue()
{
  ZuTestScope(applicationQueue);
  Engine tx;
  ZuCheck(tx.queueOrder(101) && tx.queueOrder(102) && tx.appQueue.count_() == 2 &&
    tx.next_() == 1 && !tx.tasks.length());
  tx.start();
  ZuCheck(tx.output.length() == 1 && tx.next_() == 2 && !tx.dequeues);
  auto e = tx.event(State::Logon, SessionAction::SendHeartbeat);
  ZuCheck(tx.send(e, SessionAction::SendHeartbeat) == TxSelect::Accepted);
  ZuCheck(tx.apply_(tx.event(State::Established, SessionAction::Ready)) == TxSelect::Accepted);
  // The first scheduled pump selects the older control before either order.
  ZuCheck(tx.step() && tx.next_() == 3 && !tx.selections.length());
  ZuCheck(tx.step() && tx.output.length() == 2 && tx.output[1].body->msgType() == "0");
  ZuCheck(tx.step() && tx.selections.length() == 1 && tx.output.length() == 2 &&
    tx.selections[0].body->id() == 101 && tx.selections[0].context.seqNo == 3 &&
    tx.selections[0].result == TxSelect::Accepted);
  tx.drain();
  ZuCheck(tx.output.length() == 4 && tx.output[2].body->id() == 101 &&
    tx.output[3].body->id() == 102 && tx.output[3].context.seqNo == 4 &&
    tx.selections.length() == 2 && !tx.appBytes && !tx.appQueue.count_());
  auto dequeues = tx.dequeues;
  tx.queued_(); tx.drain();
  ZuCheck(tx.dequeues == dequeues + 1 && tx.task == tx.tasks.length());
}

static void queueAssociation()
{
  ZuTestScope(queueAssociation);
  Engine tx; tx.start();
  tx.apply_(tx.event(State::Established, SessionAction::Ready)); tx.drain();
  tx.loadResult = TxLoad::Pending;
  ZuCheck(tx.queueOrder(101) && tx.queueOrder(102)); tx.drain();
  auto ticket = tx.loadTicket;
  ZuCheck(tx.loading_() && tx.next_() == 2 && tx.appQueue.count_() == 1 &&
    !tx.selections.length() && tx.output.length() == 1);
  auto e = tx.event(State::Established, SessionAction::SendHeartbeat);
  ZuCheck(tx.send(e, SessionAction::SendHeartbeat) == TxSelect::Accepted); tx.drain();
  ZuCheck(tx.output.length() == 1 && tx.next_() == 2);
  tx.loadResult = TxLoad::Accepted;
  ZuCheck(tx.loaded_(ticket, false) == TxSelect::Refused); tx.drain();
  ZuCheck(tx.selections.length() == 1 && tx.selections[0].result == TxSelect::Refused &&
    tx.selections[0].body->id() == 101 && tx.selections[0].context.seqNo == 2 &&
    tx.appQueue.count_() == 1 && tx.output.length() == 2 && tx.output[1].context.seqNo == 2);
  ZuCheck(tx.loaded_(ticket, true) == TxSelect::Invalid && tx.selections.length() == 1);
  tx.queued_(); tx.drain();
  ZuCheck(tx.output.length() == 3 && tx.output[2].body->id() == 102 &&
    tx.output[2].context.seqNo == 3 && tx.selections.length() == 2);
  // The application explicitly retries the refused unsequenced body.
  ZuCheck(tx.enqueue_(tx.selections[0].body)); tx.drain();
  ZuCheck(tx.output.length() == 4 && tx.output[3].body->id() == 101 &&
    tx.output[3].context.seqNo == 4);
  tx.loadResult = TxLoad::Pending;
  ZuCheck(tx.queueOrder(103)); tx.drain(); ticket = tx.loadTicket;
  ZuCheck(tx.loaded_(ticket, true) == TxSelect::Accepted && tx.selections.length() == 4 &&
    tx.selections[3].body->id() == 103 && tx.selections[3].context.seqNo == 5);
  tx.drain();
  ZuCheck(tx.output.length() == 5 && tx.output[4].body->id() == 103 &&
    tx.loaded_(ticket, true) == TxSelect::Invalid && tx.selections.length() == 4);
}

static void queueReconnect()
{
  ZuTestScope(queueReconnect);
  Engine tx; tx.start();
  ZuCheck(tx.queueOrder(101)); tx.drain();
  ZuCheck(!tx.dequeues && tx.appQueue.count_() == 1);
  tx.apply_(tx.event(State::Closing, SessionAction::Close));
  tx.start(2);
  ZuCheck(tx.appQueue.count_() == 1 && tx.output.length() == 2 && !tx.dequeues);
  tx.apply_(tx.event(State::Established, SessionAction::Ready, 2)); tx.drain();
  ZuCheck(tx.output.length() == 3 && tx.output[2].body->id() == 101 &&
    tx.output[2].context.seqNo == 3 && tx.output[2].context.connection == 2 &&
    tx.selections.length() == 1 && tx.selections[0].context.connection == 2);
}

static void queueReplacement()
{
  ZuTestScope(queueReplacement);
  Engine tx; tx.start();
  tx.apply_(tx.event(State::Established, SessionAction::Ready)); tx.drain();
  tx.loadResult = TxLoad::Pending;
  ZuCheck(tx.queueOrder(101)); tx.drain();
  auto ticket = tx.loadTicket;
  auto next = tx.next_();
  ZuCheck(tx.loading_() && tx.apply_(tx.event(State::Logon, 0, 2)) == TxSelect::Accepted);
  ZuCheck(tx.loaded_(ticket, true) == TxSelect::Accepted && !tx.loading_() &&
    !tx.released.length() && tx.next_() == next + 1 && tx.selections.length() == 1);
  tx.loadResult = TxLoad::Accepted;
  auto e = tx.event(State::Logon, SessionAction::SendLogon, 2);
  ZuCheck(tx.send(e, SessionAction::SendLogon) == TxSelect::Accepted); tx.drain();
  tx.apply_(tx.event(State::Established, SessionAction::Ready, 2)); tx.drain();
  ZuCheck(tx.output.length() == 3 && tx.output[1].body->msgType() == "A" &&
    tx.output[2].body->id() == 101 && tx.output[2].context.connection == 2 &&
    tx.output[2].context.seqNo == next && tx.selections.length() == 1);
}

static void queueClose(bool physical = false)
{
  ZuTestScope(queueClose);
  Engine tx; tx.start();
  tx.apply_(tx.event(State::Established, SessionAction::Ready)); tx.drain();
  tx.loadResult = TxLoad::Pending;
  ZuCheck(tx.queueOrder(101)); tx.drain(); auto ticket = tx.loadTicket;
  if (physical) {
    tx.close_(2); ZuCheck(tx.loading_());
    tx.close_(1); tx.close_(1);
  } else tx.apply_(tx.event(State::Closing, SessionAction::Close));
  tx.loadResult = TxLoad::Accepted;
  ZuCheck(tx.loaded_(ticket, true) == TxSelect::Accepted && !tx.released.length() &&
    tx.selections.length() == 1 && tx.selections[0].body->id() == 101 &&
    tx.selections[0].result == TxSelect::Accepted &&
    tx.selections[0].context.connection == 1 && tx.next_() == 3 && tx.output.length() == 1);
  tx.start(2);
  if (physical) tx.close_(1); // an old physical close cannot detach the new owner
  tx.apply_(tx.event(State::Established, SessionAction::Ready, 2)); tx.drain();
  ZuCheck(tx.output.length() == 3 && tx.output[1].body->msgType() == "A" &&
    tx.output[1].context.seqNo == 3 && tx.output[2].body->id() == 101 &&
    tx.output[2].context.connection == 2 && tx.output[2].context.seqNo == 2 &&
    tx.selections.length() == 1);
}

static void queueBounds()
{
  ZuTestScope(queueBounds);
  Engine bounded; bounded.appLimit = 1;
  ZuCheck(!bounded.queueOrder(101) && !bounded.appBytes && !bounded.appQueue.count_());
  ZuCheck(bounded.stats_().enqueueRefused == 1);
  ZuCheck(!bounded.enqueue_({}));
  ZuCheck(bounded.stats_().enqueueRefused == 1);
  Error error; Session::Msg hb; hb.p<Session::HeartBt>(Session::HeartBt{});
  ZuCheck(!bounded.enqueue_(TxBody::control(hb, error)) && !bounded.appBytes);
  TxLimits limits; limits.queuedOutputBytes = 1;
    limits.adminReserveBytes = Fmt::Limits::FrameBytes;
  Engine tx{{}, config(), limits}; tx.start();
  tx.apply_(tx.event(State::Established, SessionAction::Ready));
  ZuCheck(tx.queueOrder(101)); tx.drain();
  ZuCheck(tx.selections.length() == 1 && tx.selections[0].body->id() == 101 &&
    tx.selections[0].result == TxSelect::Overload && tx.next_() == 2 && tx.output.length() == 1);
  ZuCheck(tx.task == tx.tasks.length() && !tx.loading_());
}

static PeerReject rejectReport(uint64_t refSeqNo = 2)
{
  Session::Msg c;
  c.p<Session::Reject>(Session::Reject{.refSeqNo = refSeqNo, .refTagID = 11, .reason = 5,
    .text = "bad value"});
  Error error;
  auto body = TxBody::control(c, error);
  auto wire = txFrame(*body, txHeader(txConfig(config()), 9,
    ZuDateTime{ZuDateTimeScan::FIX{}, "20261027-12:00:00"}), error);
  ZmAssert(wire && !error);
  auto parsed = Fmt::frame(ZuCSpan{wire->cspan()});
  Fmt::Header h; Fmt::Trailer t;
  error = Session::read(parsed.view, c, h, t); ZmAssert(!error);
  return {.buffer = ZuMv(wire), .context = {1, 1, 9}, .message = c.p<Session::Reject>(),
    .wire = parsed.view.wire};
}
static void peerReject()
{
  ZuTestScope(peerReject);
  Engine tx;
  auto report = rejectReport();
  ZuCheck(!tx.peerReject_(report) && !tx.peerRejects.length());
  tx.start(); auto next = tx.next_(); auto ackd = tx.acknowledged_();
  ZuCheck(tx.peerReject_(report) && tx.peerRejects.length() == 1 &&
    tx.peerRejects[0].message.text == "bad value" && tx.next_() == next
      && tx.acknowledged_() == ackd);
  tx.apply_(tx.event(State::Closing, SessionAction::Close)); tx.start(2);
  ZuCheck(tx.peerReject_(report) && tx.peerRejects.length() == 2 &&
    tx.peerRejects[1].context.connection == 1);
  report.context.connection = 3; ZuCheck(!tx.peerReject_(report));
  report.context.connection = 0; ZuCheck(!tx.peerReject_(report));
  report.context.connection = 2; report.context.epoch = 2; ZuCheck(!tx.peerReject_(report));
  report.context.epoch = 1; report.context.seqNo = 0; ZuCheck(!tx.peerReject_(report));
  report.context.seqNo = 9; report.message.refSeqNo = 0; ZuCheck(!tx.peerReject_(report));
  report.message.refSeqNo = 2; report.buffer = nullptr; ZuCheck(!tx.peerReject_(report));
  // Application policy also receives references no longer in library history.
  report = rejectReport(999);
  ZuCheck(tx.peerReject_(report) && tx.peerRejects.length() == 3 &&
    tx.peerRejects[2].message.refSeqNo == 999);
}

static void sessionObservation()
{
  ZuTestScope(sessionObservation);
  Engine tx; tx.start();
  ZuCheck(tx.observed.length() == 1 && tx.observed[0].body->msgType() == "A" &&
    tx.observed[0].context.seqNo == 1 && tx.observed[0].context.epoch == 1 &&
    tx.observed[0].context.connection == 1 && !tx.observed[0].context.replay &&
    tx.observed[0].admitted == 1 && !tx.observed[0].flushed);
  tx.block = true;
  auto e = tx.event(State::Logon, SessionAction::SendHeartbeat);
  ZuCheck(tx.send(e, SessionAction::SendHeartbeat) == TxSelect::Accepted); tx.drain();
  ZuCheck(tx.observed.length() == 1 && tx.output.length() == 1);
  tx.block = false; ZuCheck(tx.writable_(1)); tx.drain();
  ZuCheck(tx.observed.length() == 2 && tx.observed[1].body->msgType() == "0" &&
    tx.observed[1].context.seqNo == 2 && tx.observed[1].admitted == 2
      && tx.observed[1].flushed == 1);
  tx.apply_(tx.event(State::Established, SessionAction::Ready));
  ZuCheck(tx.sendOrder(101) == TxSelect::Accepted); tx.drain();
  ZuCheck(tx.output.length() == 3 && tx.observed.length() == 2 && tx.complete(2) &&
    tx.observed.length() == 2);
  ZuCheck(tx.replay_(1, 1, 1, 3)); tx.drain();
  bool valid = tx.observed.length() > 2;
  for (unsigned i = 2, n = tx.observed.length(); i < n; ++i) {
    const auto &o = tx.observed[i];
    valid &= o.context.replay && o.body->msgType() == "4" && o.admitted > o.flushed;
  }
  ZuCheck(valid && tx.next_() == 4);
  auto n = tx.observed.length();
  ZuCheck(tx.writable_(1)); tx.drain();
  ZuCheck(tx.observed.length() == n);
  Engine refused; refused.start(); refused.loadResult = TxLoad::Refused;
  e = refused.event(State::Logon, SessionAction::SendHeartbeat);
  ZuCheck(refused.send(e, SessionAction::SendHeartbeat) == TxSelect::Accepted); refused.drain();
  ZuCheck(refused.observed.length() == 1 && refused.output.length() == 1);
}

static void managedAbort()
{
  ZuTestScope(managedAbort);
  Engine tx; tx.start(); tx.complete(0);
  tx.apply_(tx.event(State::Established, SessionAction::Ready));
  ZuCheck(tx.sendOrder(17) == TxSelect::Accepted && tx.next_() == 3);
  tx.close_(1);
  ZuCheck(!tx.abortTx_(2, 2) && tx.abortTx_(1, 2) && !tx.abortTx_(1, 2));
  ZuCheck(tx.next_() == 3 && !tx.queuedBytes_() && !tx.retained_());
  tx.start(2);
  ZuCheck(tx.output.length() == 3 && tx.output[1].body->msgType() == "A" &&
    tx.output[1].context.seqNo == 3 && tx.output[2].context.seqNo == 2 &&
    tx.output[2].body->msgType() == "4" && tx.published_() == 4 && !tx.retained_());
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(factory);
  ZuTestCall(publication);
  ZuTestCall(pending);
  ZuTestCall(reconnect);
  ZuTestCall(displaceLive);
  ZuTestCall(replayBeforeLogon);
  ZuTestCall(priorityPendingRecovery);
  ZuTestCall(closeAfterOutput);
  ZuTestCall(bounds);
  ZuTestCall(controlMemory);
  ZuTestCall(restoredRecovery);
  ZuTestCall(idleTraffic);
  ZuTestCall(idleBackpressure);
  ZuTestCall(idleZeroAndLogout);
  ZuTestCall(acknowledgement);
  ZuTestCall(drainBoundary);
  ZuTestCall(resetPreparation);
  ZuTestCall(resetReplacement);
  ZuTestCall(resetBinding);
  ZuTestCall(earlyBinding);
  ZuTestCall(sequenceNotice);
  ZuTestCall(initialReset);
  ZuTestCall(applicationQueue);
  ZuTestCall(queueAssociation);
  ZuTestCall(queueReconnect);
  ZuTestCall(queueReplacement);
  ZuTestCall(queueBounds);
  ZuTestCall(queueClose);
  ZuTestCall(queueClose, true);
  ZuTestCall(peerReject);
  ZuTestCall(sessionObservation);
  ZuTestCall(managedAbort);
  return 0;
}
