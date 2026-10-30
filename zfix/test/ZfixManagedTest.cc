//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Complete-frame fake transport; actual Rx/Tx owners and protocol codecs.
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZfixLinkTx.hh>
#include <zlib/ZfixRx.hh>

using namespace ZuTestUtil;
using namespace Zfix;
using namespace Zfix::Fmt;
template <typename T> using Items = ZtArray<T, ZtArrayHeapID<"Zfix.ManagedTest">>;
struct Order {
  ZuCSpan	id;
};

ZfStruct(, (Order, FIX), (id, (FIX::ID<11>), String));
ZfStructImpl(Order);
ZfStructImpl(Order, FIX);
using OrderMsg = MT<Order, "D">;

struct Endpoint;
struct Loop;
namespace WorkKind {

  enum { Open, Event, Control, Task, RxTask, Input, Notice,
    Admit, Sent, Resend, Logout, Close, Lost, Idle, RxTimer, Probe, Drain, Prepare, ResetStart,
    ResetDisconnect, InitialReset, PeerReject, Write, Piece, OutputFailed, Associate,
    Associated, Release, Released, AppReject, FinishAssociate, AppSelected };
}

struct Work {
  Endpoint		*endpoint = nullptr;
  unsigned		kind = 0;
  SessionEvent		event;
  TxNotice		notice;
  Zfix::PeerReject	report;
  TxContext		context;
  TxBody::Ref		body;
  ZmRef<ZiIOBuf>	frame;
  uint64_t		ticket = 0;
  uint64_t		begin = 0;
  uint64_t		end = 0;
  unsigned		task = 0;
  unsigned		path = ResetPath::Live;
  bool			ok = true;
};

struct Selected {
  TxBody::Ref	body;
  TxContext	context;
  unsigned	result;
};

struct Wire {
  TxBody::Ref	body;
  TxContext	context;
  bool		client = false;
  ZuDateTime	sendingTime;
  ZuDateTime	origSendingTime;
  bool		possDup = false;
};

struct ArchivedRange {
  uint64_t	epoch, begin, end;
};

struct RxSide : Rx<RxSide> {
  Endpoint		*endpoint;
  Framer		parser;
  ZmRef<ZiIOBuf>	bytes;
  uint64_t		byteConnection = 0;
  unsigned		pieces = 0;
  unsigned		incomplete = 0;
  void piece_(ZmRef<ZiIOBuf>, TxContext, unsigned, unsigned);
  void abandon_(uint64_t);
  RxSide(Endpoint *, SessionConfig);
  ZuTime nowRx_() const;
  void eventRx_(SessionEvent, ZmRef<Msg>);
  void rejectRx_(ZmRef<Msg>, unsigned, int);
  void resendRx_(uint64_t, uint64_t, ZmRef<Msg>);

  void sessionRx_(ZmRef<Msg>) { }
  void peerRejectRx_(Zfix::PeerReject);
  void rxPost_(uint64_t);

  void gapArm_(uint64_t) { }
  void gapCancel_() { }

  Error headerRx_(const FrameView &f, Header &h, Limits l) { return readHeader<Order>(f, h, l); }
  Error businessRx_(Msg *, Limits);
};

// Shared core TxBody objects cannot carry this fixture FIFO's links.
using AppQueue = ZmList<TxBody::Ref, ZmListHeapID<"Zfix.ManagedTest.App">>;
struct QueuedFrame {
  TxBody::Ref		body;
  TxContext		context;
  ZmRef<ZiIOBuf>	frame;
  uint64_t		ticket;
  uint64_t		charge;
};

using FrameQueue = ZmList<QueuedFrame, ZmListHeapID<"Zfix.ManagedTest.Output">>;
struct TxSide : LinkTx<TxSide> {
  FrameQueue		behind;
  uint64_t		queuedBytes = 0;
  uint64_t		transportLimit = TxLimits{}.queuedOutputBytes;
  bool			queueBehind = false;
  AppQueue		appQueue;
  uint64_t		appBytes = 0;
  bool enqueueTx_(TxBody::Ref);
  TxBody::Ref dequeueTx_();
  void selectedTx_(TxBody::Ref, TxContext, unsigned);
  void rejectedTx_(Zfix::PeerReject);
  Endpoint		*endpoint;
  TxBody::Ref		pendingBody;
  TxContext		pendingContext;
  ZmRef<ZiIOBuf>	pendingFrame;
  TxBody::Ref		fragmentBody;
  TxContext		fragmentContext;
  ZmRef<ZiIOBuf>	fragmentFrame;
  Items<TxHistory>	history;
  Items<ArchivedRange>	archives;
  bool			delayHistory = false;
  uint64_t		historyTicket = 0;
  unsigned		offset = 0;
  uint64_t		writeSerial = 0;
  uint64_t		writeTicket = 0;
  bool			holdNext = false;
  bool			holdCompletion = false;
  bool write_(TxContext, unsigned, bool, uint64_t);
  ZuTime		idleAt;
  uint64_t		idleConnection = 0;
  uint64_t		idleTicket = 0;
  TxSide(Endpoint *, SessionConfig);
  void txSchedule_(unsigned, uint64_t);
  unsigned associateTx_(TxBody::Ref, TxContext, uint64_t);
  bool releaseTx_(TxBody::Ref, TxContext, uint64_t);

  TxReload retrieveTx_(TxContext context, uint64_t, uint64_t ticket) {
    historyTicket = ticket;
    if (delayHistory) return {{}, true};
    for (const auto &record : history)
      if (record.epoch == context.epoch && record.seqNo == context.seqNo) return {record};
    return {};
  }

  void archiveTx_(uint64_t epoch, uint64_t begin, uint64_t end) {
    archives.push(ArchivedRange{epoch, begin, end});
  }

  void publishedTx_(TxBody::Ref body, TxContext c, ZuDateTime original) {
    // Application retention is independent of local completion and archive.
    // This fixture keeps business/Reject records in its own long-term array.
    if (c.replay || (body->msgType() != "D" && body->msgType() != "3")) return;
    for (auto &record : history)
      if (record.epoch == c.epoch && record.seqNo == c.seqNo) {
	record = TxHistory{ZuMv(body), original, c.epoch, c.seqNo}; return;
      }
    history.push(TxHistory{ZuMv(body), original, c.epoch, c.seqNo});
  }

  ZuDateTime clockTx_();
  ZuTime nowTx_();
  void idleArmTx_(uint64_t, uint64_t, ZuTime);
  void idleCancelTx_();
  void faultTx_(TxContext, Error);
  bool admitFrameTx_(TxBody::Ref, TxContext, ZmRef<ZiIOBuf>, ZuDateTime);
  void notifyTx_(TxNotice);
  void flushTx_();
  void completeTx_(TxBody::Ref, TxContext, bool);
  void acknowledgedTx_(TxContext, uint64_t, uint64_t);
};

static SessionConfig config(bool client, unsigned mode, unsigned version)
{
  SessionConfig c;
  c.id = client ? "CLIENT-SESSION" : "SERVER-SESSION";
  c.header.version = version;
  c.header.local = client ? "CLIENT" : "SERVER";
  c.header.peer = client ? "SERVER" : "CLIENT";
  c.mode = mode;
  c.initiator = client;
  c.appVerID = AppVersion::V5_0_SP2;
  return c;
}
struct Endpoint {
  Loop				*loop;
  Endpoint			*peer = nullptr;
  bool				client;
  RxSide			rx;
  TxSide			tx;
  Items<SessionString>		orders;
  Items<TxContext>		associations;
  Items<uint64_t>		assocIds;
  Items<bool>			assocActive;
  Items<TxContext>		released;
  Items<Zfix::PeerReject>	peerRejects;
  Items<uint64_t>		rejectIds;
  bool				rejectBusiness = false;
  bool				asyncMap = false;
  bool				nativeQueued = false;
  bool				selectedBeforeReject = true;
  Items<Selected>		selected;
  bool				refuseLoad = false;
  bool				deferLoad = false;
  unsigned			refused = 0;
  Work				deferred;
  void map_(TxBody::Ref, TxContext);
  void release_(TxContext);
  void reject_(Zfix::PeerReject);
  bool				dropNextOrder = false;
  uint64_t			dropped = 0;
  unsigned			completions = 0;
  unsigned			failures = 0;
  Items<uint64_t>		acks;
  Items<uint64_t>		epochs;
  ZmRef<ZiIOBuf>		blockedFrame;
  TxContext			blockedContext;
  uint64_t			submission = 1000;

  // application IDs are independent of wire sequences

  Endpoint(Loop *loop_, bool client_, unsigned mode, unsigned version) :
    loop{loop_}, client{client_}, rx{this, config(client_, mode, version)},
    tx{this, config(client_, mode, version)} { }
};

struct Loop {
  Items<Work>	work;
  Items<Wire>	wire;
  ZuTime	now{100};
  ZuDateTime	utc{ZuDateTimeScan::FIX{}, "20261027-12:00:00"};
  unsigned	cursor = 0;
  unsigned	owner = 0;
  unsigned	errors = 0;
  bool		dropOrder = false;
  uint64_t	dropped = 0;

  void post(Work w) { work.push(ZuMv(w)); }
  void drain(unsigned limit = 0);

  bool enqueue(Endpoint &endpoint, ZuCSpan id) {
    owner = 2;
    Error e;
    auto body = TxBody::encode<OrderMsg>(Order{id}, ++endpoint.submission, e);
    bool result = !e && endpoint.tx.enqueue_(ZuMv(body));
    owner = 0; return result;
  }

  unsigned submit(Endpoint &endpoint, ZuCSpan id) {
    owner = 2;
    Error e;
    auto body = TxBody::encode<OrderMsg>(Order{id}, ++endpoint.submission, e);
    auto result = endpoint.tx.candidate_(ZuMv(body));
    owner = 0;
    return result;
  }
};

RxSide::RxSide(Endpoint *e, SessionConfig c) : Rx<RxSide>{ZuMv(c)}, endpoint{e} { }
TxSide::TxSide(Endpoint *e, SessionConfig c) : LinkTx<TxSide>{ZuMv(c)}, endpoint{e} { }
ZuTime RxSide::nowRx_() const { return endpoint->loop->now; }
void RxSide::rxPost_(uint64_t ticket)
{
  endpoint->loop->post(Work{.endpoint = endpoint, .kind = WorkKind::RxTask, .ticket = ticket});
}
void RxSide::eventRx_(SessionEvent event, ZmRef<Msg> msg)
{
  auto loop = endpoint->loop;
  ZmAssert(loop->owner == 1);
  if (event.actions & SessionAction::Failure) ++loop->errors;
  ZuCSpan echo;
  if ((event.actions & SessionAction::Echo)
    && msg) echo = msg->data().control.p<Session::TestReq>().testReqID;
  auto error = controlCommands(event, session_(), expected_(), echo, Limits::FrameBytes,
    [this, loop](SessionEvent e, TxBody::Ref body) {
      loop->post(Work{.endpoint = endpoint, .kind = WorkKind::Event, .event = e,
	.body = ZuMv(body)});
      return true;
    });
  if (error) ++loop->errors;
  if (event.actions & SessionAction::Admission)
    loop->post(Work{.endpoint = endpoint, .kind = WorkKind::Admit, .event = event});
  if (event.actions & SessionAction::Close)
    loop->post(Work{.endpoint = endpoint, .kind = WorkKind::Close, .event = event});
  if ((event.actions & SessionAction::RetryInput) && endpoint->blockedFrame)
    loop->post(Work{.endpoint = endpoint, .kind = WorkKind::Input,
      .context = endpoint->blockedContext, .frame = ZuMv(endpoint->blockedFrame)});
}
void RxSide::rejectRx_(ZmRef<Msg> msg, unsigned tag, int reason)
{
  Session::Msg c;
  c.p<Session::Reject>(Session::Reject{.refSeqNo = msg->data().header.seqNo,
    .refTagID = tag, .reason = reason, .text = "bad value"});
  Error e;
  auto body = TxBody::control(c, e);
  if (e) { ++endpoint->loop->errors; return; }
  endpoint->loop->post(Work{.endpoint = endpoint, .kind = WorkKind::Control,
    .context = {session_().epoch_(), session_().connection_(), 0}, .body = ZuMv(body)});
}
void RxSide::resendRx_(uint64_t begin, uint64_t end, ZmRef<Msg>)
{
  endpoint->loop->post(Work{.endpoint = endpoint, .kind = WorkKind::Resend,
    .context = {session_().epoch_(), session_().connection_(), 0}, .begin = begin, .end = end});
}
Error RxSide::businessRx_(Msg *msg, Limits limits)
{
  ZmAssert(endpoint->loop->owner == 1);
  MessageHandler<Order> h{msg->data().frame, limits};
  auto value = h.ctor();
  if (!h.valid()) return h.error();
  if (endpoint->rejectBusiness) return {ErrorCode::Value, 11};
  endpoint->orders.push(SessionString{value.id});
  endpoint->epochs.push(msg->data().epoch);
  return {};
}
bool TxSide::enqueueTx_(TxBody::Ref body)
{
  ZmAssert(endpoint->loop->owner == 2);
  auto charge = uint64_t(body->size) + sizeof(AppQueue::Node);
  if (charge > Fmt::Limits::FrameBytes
    || appBytes > Fmt::Limits::FrameBytes - charge) return false;
  appBytes += charge; appQueue.push(ZuMv(body)); return true;
}
TxBody::Ref TxSide::dequeueTx_()
{
  ZmAssert(endpoint->loop->owner == 2);
  auto node = appQueue.shift();
  if (!node) return {};
  appBytes -= uint64_t(node->data()->size) + sizeof(AppQueue::Node);
  return ZuMv(node->data());
}
void TxSide::selectedTx_(TxBody::Ref body, TxContext context, unsigned result)
{
  ZmAssert(endpoint->loop->owner == 2);
  if (endpoint->asyncMap) {
    endpoint->loop->post(Work{.endpoint = endpoint, .kind = WorkKind::AppSelected,
      .context = context, .body = ZuMv(body), .task = result});
    return;
  }
  if (result != TxSelect::Accepted || context.epoch != epoch_() ||
      context.connection != connection_()) ++endpoint->loop->errors;
}
void RxSide::peerRejectRx_(Zfix::PeerReject report)
{
  ZmAssert(endpoint->loop->owner == 1);
  endpoint->loop->post(Work{.endpoint = endpoint, .kind = WorkKind::PeerReject,
    .report = ZuMv(report)});
}
void Endpoint::map_(TxBody::Ref body, TxContext context)
{
  ZmAssert(loop->owner == (asyncMap ? 3 : 2));
  associations.push(context); assocIds.push(body->id()); assocActive.push(true);
}
void Endpoint::release_(TxContext context)
{
  ZmAssert(loop->owner == (asyncMap ? 3 : 2));
  released.push(context);
  for (unsigned i = 0, n = associations.length(); i < n; ++i) {
    const auto &a = associations[i];
    if (a.epoch == context.epoch && a.connection == context.connection
      && a.seqNo == context.seqNo)
      assocActive[i] = false;
  }
}
void Endpoint::reject_(Zfix::PeerReject report)
{
  ZmAssert(loop->owner == (asyncMap ? 3 : 2));
  uint64_t id = 0;
  for (unsigned i = 0, n = associations.length(); i < n; ++i) {
    const auto &a = associations[i];
    if (assocActive[i] && a.epoch == report.context.epoch && a.seqNo == report.message.refSeqNo) {
      id = assocIds[i]; break;
    }
  }
  if (nativeQueued) {
    bool found = false;
    for (const auto &item : selected)
      if (item.context.epoch == report.context.epoch
	&& item.context.seqNo == report.message.refSeqNo &&
	  item.result == TxSelect::Accepted && item.body->id() == id) found = true;
    selectedBeforeReject &= found;
  }
  rejectIds.push(id);
  peerRejects.push(ZuMv(report));
}
void TxSide::rejectedTx_(Zfix::PeerReject report)
{
  ZmAssert(endpoint->loop->owner == 2);
  if (endpoint->asyncMap) {
    endpoint->loop->post(Work{.endpoint = endpoint, .kind = WorkKind::AppReject,
      .report = ZuMv(report)});
    return;
  }
  endpoint->reject_(ZuMv(report));
}
void TxSide::txSchedule_(unsigned kind, uint64_t ticket)
{
  endpoint->loop->post(Work{.endpoint = endpoint, .kind = WorkKind::Task, .ticket = ticket,
    .task = kind});
}
unsigned TxSide::associateTx_(TxBody::Ref body, TxContext context, uint64_t ticket)
{
  ZmAssert(endpoint->loop->owner == 2);
  if (endpoint->asyncMap) {
    endpoint->loop->post(Work{.endpoint = endpoint, .kind = WorkKind::Associate,
      .context = context, .body = ZuMv(body), .ticket = ticket});
    return TxLoad::Pending;
  }
  endpoint->map_(body, context);
  return TxLoad::Accepted;
}
bool TxSide::releaseTx_(TxBody::Ref body, TxContext context, uint64_t ticket)
{
  ZmAssert(endpoint->loop->owner == 2);
  if (endpoint->asyncMap) {
    endpoint->loop->post(Work{.endpoint = endpoint, .kind = WorkKind::Release,
      .context = context, .body = ZuMv(body), .ticket = ticket});
    return false;
  }
  endpoint->release_(context);
  if (body->administrative()) return true;
  if (enqueue_(ZuMv(body))) return true;
  ++endpoint->loop->errors; return false;
}
ZuDateTime TxSide::clockTx_() { return endpoint->loop->utc; }
ZuTime TxSide::nowTx_() { return endpoint->loop->now; }
void TxSide::idleArmTx_(uint64_t connection, uint64_t ticket, ZuTime deadline)
{
  ZmAssert(endpoint->loop->owner == 2);
  idleConnection = connection; idleTicket = ticket; idleAt = deadline;
}
void TxSide::idleCancelTx_()
{
  ZmAssert(endpoint->loop->owner == 2);
  idleAt.null();
}
void TxSide::faultTx_(TxContext context, Error) {
  ++endpoint->loop->errors;
  endpoint->loop->post(Work{.endpoint = endpoint, .kind = WorkKind::OutputFailed,
    .context = context});
}
bool TxSide::admitFrameTx_(TxBody::Ref body, TxContext context, ZmRef<ZiIOBuf> frame, ZuDateTime)
{
  ZmAssert(endpoint->loop->owner == 2 && !pendingFrame);
  if (fragmentFrame) {
    auto charge = uint64_t(frame->size) + sizeof(FrameQueue::Node);
    if (!queueBehind || charge > transportLimit
      || fragmentFrame->size > transportLimit - charge ||
	queuedBytes > transportLimit - charge - fragmentFrame->size) return false;
  }
  pendingBody = ZuMv(body); pendingContext = context; pendingFrame = ZuMv(frame);
  return true;
}
void TxSide::notifyTx_(TxNotice notice)
{
  endpoint->loop->post(Work{.endpoint = endpoint, .kind = WorkKind::Notice, .notice = notice});
}
void TxSide::flushTx_()
{
  auto loop = endpoint->loop;
  auto context = pendingContext;
  auto parsed = Fmt::frame(ZuCSpan{pendingFrame->cspan()}); Header header;
  ZmAssert(parsed.state == FrameState::Complete && !readHeader(parsed.view, header));
  loop->wire.push(Wire{pendingBody, context, endpoint->client,
    header.sendingTime, header.origSendingTime, header.possDup});
  if (fragmentFrame) {
    auto charge = uint64_t(pendingFrame->size) + sizeof(FrameQueue::Node);
    queuedBytes += charge;
    behind.push(QueuedFrame{ZuMv(pendingBody), context, ZuMv(pendingFrame), ++writeSerial,
      charge}); return;
  }
  if (holdNext && pendingBody->msgType() == "D" && !context.replay) {
    holdNext = false; offset = 0; writeTicket = ++writeSerial;
    fragmentBody = ZuMv(pendingBody); fragmentContext = context;
    fragmentFrame = ZuMv(pendingFrame); return;
  }
  bool drop = loop->dropOrder && !loop->dropped && pendingBody->msgType() == "D"
    && !context.replay;
  if (endpoint->dropNextOrder && pendingBody->msgType() == "D" && !context.replay) {
    endpoint->dropNextOrder = false; endpoint->dropped = context.seqNo; drop = true;
  }
  if (drop) loop->dropped = context.seqNo;
  else loop->post(Work{.endpoint = endpoint->peer, .kind = WorkKind::Input, .context = context,
    .frame = ZuMv(pendingFrame)});
  pendingFrame = nullptr;
  loop->post(Work{.endpoint = endpoint, .kind = WorkKind::Sent, .context = context,
    .body = ZuMv(pendingBody)});
}
void RxSide::abandon_(uint64_t connection)
{
  ZmAssert(endpoint->loop->owner == 1);
  if (byteConnection != connection) return;
  bytes = nullptr; byteConnection = 0; parser.reset();
}
void RxSide::piece_(ZmRef<ZiIOBuf> source, TxContext context, unsigned start, unsigned count)
{
  auto loop = endpoint->loop;
  ZmAssert(loop->owner == 1);
  if (context.connection != session_().connection_()
    || session_().state_() == State::Disconnected)
    return;
  if (byteConnection != context.connection) {
    bytes = nullptr; parser.reset(); byteConnection = context.connection;
  }
  if (!bytes) bytes = new ZiIOBufAlloc<>;
  ZmAssert(source && start <= source->length && count <= source->length - start &&
    bytes->length == start && count <= Fmt::Limits::FrameBytes - bytes->length);
  auto dst = bytes->ensure(bytes->length + count); ZmAssert(dst);
  memcpy(dst + bytes->length, source->data() + start, count);
  bytes->length += count; ++pieces;
  auto result = parser.scan(ZuCSpan{bytes->cspan()});
  if (result.state == FrameState::Incomplete) { ++incomplete; return; }
  ZmAssert(result.state == FrameState::Complete && result.consumed == bytes->length);
  parser.reset();
  loop->post(Work{.endpoint = endpoint, .kind = WorkKind::Input,
    .context = {0, context.connection, 0}, .frame = ZuMv(bytes)});
}
bool TxSide::write_(TxContext context, unsigned count, bool ok, uint64_t ticket)
{
  auto loop = endpoint->loop;
  ZmAssert(loop->owner == 2);
  if (!fragmentFrame || !ticket || ticket != writeTicket
    || context.epoch != fragmentContext.epoch ||
      context.connection != fragmentContext.connection
	|| context.seqNo != fragmentContext.seqNo ||
      context.replay != fragmentContext.replay || context.epoch != epoch_()
	|| context.connection != connection_()) return false;
  if (ok) {
    if (count > fragmentFrame->length - offset) return false;
    if (count) {
      loop->post(Work{.endpoint = endpoint->peer, .kind = WorkKind::Piece,
	.context = context, .frame = fragmentFrame, .begin = offset, .end = count});
      offset += count;
    }
    if (offset != fragmentFrame->length || holdCompletion) return true;
  }
  auto body = ZuMv(fragmentBody); fragmentFrame = nullptr; offset = 0;
  sent_(ZuMv(body), context, ok);
  if (!ok) {
    while (auto node = behind.shift()) {
      auto &item = node->data(); queuedBytes -= item.charge;
      sent_(ZuMv(item.body), item.context, false);
    }
    return true;
  }
  if (auto node = behind.shift()) {
    auto &item = node->data(); queuedBytes -= item.charge;
    fragmentBody = ZuMv(item.body); fragmentContext = item.context;
    fragmentFrame = ZuMv(item.frame); writeTicket = item.ticket;
  }
  writable_(context.connection);
  return true;
}
void TxSide::completeTx_(TxBody::Ref, TxContext, bool ok)
{
  ZmAssert(endpoint->loop->owner == 2);
  ++endpoint->completions;
  if (!ok) ++endpoint->failures;
}
void TxSide::acknowledgedTx_(TxContext context, uint64_t previous, uint64_t next)
{
  ZmAssert(endpoint->loop->owner == 2 && context.epoch == epoch_() &&
    context.connection == connection_() && next > previous);
  endpoint->acks.push(next);
}
void Loop::drain(unsigned limit)
{
  // Deterministic failure deadline, far above a complete handshake/recovery's
  // expected work. It detects protocol/task cycling without wall-clock sleeps.
  enum { MaxSteps = 10000 };
  unsigned steps = 0;
  while (cursor < work.length() && steps++ < (limit ? limit : MaxSteps)) {
    auto w = ZuMv(work[cursor++]);
    auto ep = w.endpoint;
    owner = 1;
    switch (w.kind) {
      case WorkKind::Open: {
	auto e = ep->rx.open_(now);
	ZmAssert(e.connection);
	break;
      }
      case WorkKind::Admit: ep->rx.admit_(w.event.connection, w.event.ticket, true, now); break;
      case WorkKind::Notice: ep->rx.notice_(w.notice, now); break;
      case WorkKind::Input: {
	auto parsed = frame(ZuCSpan{w.frame->cspan()});
	ZmAssert(parsed.state == FrameState::Complete);
	ZmRef<RxQueue::Node> msg = new RxQueue::Node;
	msg->data().buffer = ZuMv(w.frame); msg->data().frame = parsed.view;
	auto connection = w.context.connection ? w.context.connection :
	  ep->rx.session_().connection_();
	if (ep->rx.receive_(connection, msg, now, utc) == RxResult::Blocked) {
	  ZmAssert(!ep->blockedFrame);
	  ep->blockedFrame = msg->data().buffer;
	  ep->blockedContext = w.context;
	}
	break;
      }
      case WorkKind::Logout: ep->rx.logout_(ep->rx.session_().connection_(), now); break;
      case WorkKind::Probe: ep->rx.probe_(ep->rx.session_().connection_(), now); break;
      case WorkKind::Drain: ep->rx.drainBegin_(ep->rx.session_().connection_(), now); break;
      case WorkKind::InitialReset:
	if (!ep->rx.resetOnOpen_(w.context.epoch)) ++errors;
	break;
      case WorkKind::Prepare:
	if (!ep->rx.prepareReset_(ep->rx.session_().connection_(), w.context.epoch,
	  w.event.nextEpoch,
	    w.event.resetInitiator, now, w.path)) ++errors;
	break;
      case WorkKind::ResetStart:
	if (!ep->rx.startReset_(ep->rx.session_().connection_(), w.context.epoch, now)) ++errors;
	break;
      case WorkKind::ResetDisconnect:
	if (!ep->rx.disconnectReset_(ep->rx.session_().connection_(), w.context.epoch)) ++errors;
	break;
      case WorkKind::Close:
	ep->rx.abandon_(w.event.connection);
	ep->rx.closed_(w.event.connection);
	post(Work{.endpoint = ep->peer, .kind = WorkKind::Lost,
	  .context = {0, w.event.connection, 0}}); break;
      case WorkKind::Lost:
	ep->rx.abandon_(w.context.connection); ep->rx.lost_(w.context.connection); break;
      case WorkKind::OutputFailed:
	ep->rx.outputFailed_(w.context.connection, w.context.epoch); break;
      case WorkKind::Write:
	owner = 2;
	if (!ep->tx.write_(w.context, w.begin, w.ok, w.ticket)) ++errors;
	break;
      case WorkKind::Piece:
	ep->rx.piece_(ZuMv(w.frame), w.context, w.begin, w.end); break;
      case WorkKind::RxTask: ep->rx.drain_(w.ticket); break;
      case WorkKind::Task:
	owner = 2; ep->tx.run_(w.task, w.ticket); break;
      case WorkKind::Event:
	owner = 2;
	if (auto result = ep->tx.apply_(w.event, w.body);
	    result != TxSelect::Accepted && (w.body || result == TxSelect::Invalid ||
	      result == TxSelect::Overload)) ++errors;
	break;
      case WorkKind::PeerReject:
	owner = 2;
	if (!ep->tx.peerReject_(ZuMv(w.report))) ++errors;
	break;
      case WorkKind::Associate: {
	owner = 3;
	bool accepted = w.body->administrative() || !ep->refuseLoad;
	if (accepted) ep->map_(w.body, w.context);
	else ++ep->refused;
	if (accepted && !w.body->administrative() && ep->deferLoad && !ep->deferred.body) {
	  ep->deferred = ZuMv(w); break;
	}
	post(Work{.endpoint = ep, .kind = WorkKind::Associated, .context = w.context,
	  .ticket = w.ticket, .ok = accepted}); break;
      }
      case WorkKind::FinishAssociate: {
	owner = 3; auto delayed = ZuMv(ep->deferred);
	ZmAssert(delayed.body);
	post(Work{.endpoint = ep, .kind = WorkKind::Associated, .context = delayed.context,
	  .ticket = delayed.ticket}); break;
      }
      case WorkKind::Associated: {
	owner = 2;
	auto result = ep->tx.loaded_(w.ticket, w.ok);
	if (result != TxSelect::Accepted && result != TxSelect::Refused) ++errors;
	break;
      }
      case WorkKind::Release:
	owner = 3; ep->release_(w.context);
	post(Work{.endpoint = ep, .kind = WorkKind::Released, .context = w.context,
	  .body = ZuMv(w.body), .ticket = w.ticket}); break;
      case WorkKind::Released:
	owner = 2;
	if (!w.body->administrative() && !ep->tx.enqueue_(ZuMv(w.body))) ++errors;
	if (!ep->tx.unloaded_(w.ticket)) ++errors;
	break;
      case WorkKind::AppReject: owner = 3; ep->reject_(ZuMv(w.report)); break;
      case WorkKind::AppSelected:
	owner = 3; ep->selected.push(Selected{ZuMv(w.body), w.context, w.task}); break;
      case WorkKind::Control:
	owner = 2;
	if (ep->tx.control_(w.context.connection, w.context.epoch,
	  w.body) != TxSelect::Accepted) ++errors;
	break;
      case WorkKind::Sent: owner = 2; ep->tx.sent_(w.body, w.context, true); break;
      case WorkKind::Idle:
	owner = 2; ep->tx.idle_(w.context.connection, w.ticket); break;
      case WorkKind::RxTimer: ep->rx.timer_(w.context.connection, w.task, now); break;
      case WorkKind::Resend:
	owner = 2;
	if (!ep->tx.replay_(w.context.connection, w.context.epoch, w.begin, w.end)) ++errors;
	break;
    }
  }
  owner = 0;
  if (!limit && cursor != work.length()) ++errors;
}

static void handshake()
{
  ZuTestScope(handshake);
  for (unsigned mode = Transport::FIX4; mode <= Transport::FIXT11; ++mode) {
    Loop loop;
    auto version = mode == Transport::FIXT11 ? Version::V4_4 : Version::VT1_1;
    Endpoint client{&loop, true, mode, version};
    Endpoint server{&loop, false, mode, version};
    client.peer = &server; server.peer = &client;
    loop.post(Work{.endpoint = &server, .kind = WorkKind::Open});
    loop.post(Work{.endpoint = &client, .kind = WorkKind::Open});
    loop.drain();
    ZuCheck(!loop.errors && client.rx.session_().ready_() && server.rx.session_().ready_());
    ZuCheck(client.tx.inFlight_() == 0 && server.tx.inFlight_() == 0 &&
      loop.wire.length() == 2);
    for (const auto &wire : loop.wire) ZuCheck(wire.body->msgType() == "A");
    auto clientAck = client.tx.acknowledged_(), serverAck = server.tx.acknowledged_();
    ZuCheck(loop.submit(client, "first") == TxSelect::Accepted); loop.drain();
    ZuCheck(server.orders.length() == 1 && server.orders[0] == "first");
    ZuCheck(client.tx.acknowledged_() == clientAck && server.tx.acknowledged_() == serverAck);
    loop.post(Work{.endpoint = &client, .kind = WorkKind::Logout}); loop.drain();
    ZuCheck(!loop.errors && client.rx.session_().state_() == State::Disconnected &&
      server.rx.session_().state_() == State::Disconnected);
  }
}
static void recovery()
{
  ZuTestScope(recovery);
  Loop loop;
  Endpoint client{&loop, true, Transport::FIX4, Version::V4_4};
  Endpoint server{&loop, false, Transport::FIX4, Version::V4_4};
  client.peer = &server; server.peer = &client;
  loop.post(Work{.endpoint = &server, .kind = WorkKind::Open});
  loop.post(Work{.endpoint = &client, .kind = WorkKind::Open}); loop.drain();
  ZuCheck(client.rx.session_().ready_() && server.rx.session_().ready_());
  loop.dropOrder = true;
  ZuCheck(loop.submit(client, "first") == TxSelect::Accepted);
  ZuCheck(loop.submit(client, "second") == TxSelect::Accepted); loop.drain();
  ZuCheck(!loop.errors && loop.dropped && server.orders.length() == 2 &&
    server.orders[0] == "first" && server.orders[1] == "second");
  bool replayed = false;
  for (unsigned i = 0, n = loop.wire.length(); i < n; ++i)
    if (loop.wire[i].client && loop.wire[i].context.replay &&
	loop.wire[i].context.seqNo == loop.dropped
	  && loop.wire[i].body->msgType() == "D") replayed = true;
  ZuCheck(replayed && client.rx.session_().ready_() && server.rx.session_().ready_());
}
static void priorityPendingRecovery()
{
  ZuTestScope(priorityPendingRecovery);
  for (unsigned mode = Transport::FIX4; mode <= Transport::FIXT11; ++mode) {
    Loop loop;
    Endpoint client{&loop, true, mode, Version::V4_4};
    Endpoint server{&loop, false, mode, Version::V4_4};
    client.peer = &server; server.peer = &client;
    Error error;
    auto body = TxBody::encode<OrderMsg>(Order{"pending"}, 42, error);
    // The previous connection exchanged Logon1 in both directions. Business2
    // was assigned but never published before the owners were reconstructed.
    loop.owner = 1;
    for (auto ep : {&client, &server}) {
      Checkpoint state;
      ZuCheck(ep->rx.snapshot_(state));
      state.rx = {2, 2, 0, 0};
      ZuCheck(ep->rx.restore_(state));
    }
    loop.owner = 2;
    ZuCheck(client.tx.restore_(1, 3, 2, 1));
    ZuCheck(server.tx.restore_(1, 2, 2, 1));
    client.map_(body, {1, 0, 2});
    client.tx.delayHistory = true;
    loop.owner = 0;
    loop.post(Work{.endpoint = &server, .kind = WorkKind::Open});
    loop.post(Work{.endpoint = &client, .kind = WorkKind::Open}); loop.drain();
    ZuCheck(!loop.errors && client.tx.waiting_() && client.tx.published_() == 2 &&
      !server.orders.length() && !server.rx.session_().ready_());
    auto progress = client.tx.progress_();
    ZuCheck(progress.replayBegin == 2 && progress.replayEnd == 3);
    bool first = true;
    for (const auto &wire : loop.wire) {
      if (!wire.client) continue;
      if (first) {
	ZuCheck(wire.body->msgType() == "A" && wire.context.seqNo == 3);
	first = false;
      }
    }
    loop.owner = 2;
    client.tx.delayHistory = false;
    ZuCheck(client.tx.reloaded_(client.tx.historyTicket, TxHistory{body, {}, 1, 2}));
    loop.owner = 0;
    loop.drain();
    ZuCheck(!loop.errors && client.rx.session_().ready_() && server.rx.session_().ready_() &&
      !client.tx.waiting_() && server.orders.length() == 1 && server.orders[0] == "pending" &&
      client.tx.epoch_() == 1 && !client.released.length());
    bool replayed = false;
    for (const auto &wire : loop.wire)
      if (wire.client && wire.body->msgType() == "D" && wire.context.seqNo == 2 &&
	wire.context.replay && wire.possDup && *wire.origSendingTime) replayed = true;
    ZuCheck(replayed && client.tx.next_() == client.tx.published_() &&
      client.tx.next_() >= 4 && !client.tx.queuedBytes_());
  }
}

static void heartbeats()
{
  ZuTestScope(heartbeats);
  Loop loop;
  Endpoint client{&loop, true, Transport::FIX4, Version::V4_4};
  Endpoint server{&loop, false, Transport::FIX4, Version::V4_4};
  client.peer = &server; server.peer = &client;
  loop.post(Work{.endpoint = &server, .kind = WorkKind::Open});
  loop.post(Work{.endpoint = &client, .kind = WorkKind::Open}); loop.drain();
  ZuCheck(!loop.errors && client.rx.session_().ready_() && server.rx.session_().ready_());
  ZuCheck(client.tx.idleAt == ZuTime{130} && server.tx.idleAt == ZuTime{130});
  auto count = loop.wire.length();
  loop.now = ZuTime{130};
  for (auto ep : {&client, &server})
    loop.post(Work{.endpoint = ep, .kind = WorkKind::Idle,
      .context = {1, ep->tx.idleConnection, 0}, .ticket = ep->tx.idleTicket});
  loop.drain();
  ZuCheck(loop.wire.length() == count + 2 && !loop.errors);
  for (unsigned i = count, n = loop.wire.length(); i < n; ++i)
    ZuCheck(loop.wire[i].body->msgType() == "0" && !loop.wire[i].body->body().length());
  loop.now = ZuTime{136};
  for (auto ep : {&client, &server})
    loop.post(Work{.endpoint = ep, .kind = WorkKind::RxTimer,
      .context = {1, 1, 0}, .task = SessionTimer::Receive});
  loop.drain();
  ZuCheck(!loop.errors && loop.wire.length() == count + 2 &&
    client.rx.session_().deadline_(SessionTimer::Receive) == ZuTime{166} &&
    server.rx.session_().deadline_(SessionTimer::Receive) == ZuTime{166});

  loop.now = ZuTime{140};
  ZuCheck(loop.submit(client, "activity") == TxSelect::Accepted); loop.drain();
  ZuCheck(server.orders.length() == 1 && client.tx.idleDeadline_() == ZuTime{170});
  count = loop.wire.length();
  loop.now = ZuTime{160};
  for (auto ep : {&client, &server})
    loop.post(Work{.endpoint = ep, .kind = WorkKind::Idle,
      .context = {1, ep->tx.idleConnection, 0}, .ticket = ep->tx.idleTicket});
  loop.drain();
  ZuCheck(!loop.errors && loop.wire.length() == count + 1 &&
    !loop.wire[count].client && loop.wire[count].body->msgType() == "0");
  ZuCheck(client.rx.session_().ready_() && server.rx.session_().ready_());
  auto stale = client.tx.idleTicket;
  loop.post(Work{.endpoint = &client, .kind = WorkKind::Logout}); loop.drain();
  ZuCheck(!loop.errors && !*client.tx.idleAt && !*server.tx.idleAt);
  count = loop.wire.length();
  loop.post(Work{.endpoint = &client, .kind = WorkKind::Idle,
    .context = {1, 1, 0}, .ticket = stale}); loop.drain();
  ZuCheck(!loop.errors && loop.wire.length() == count);
}

static void probeAcknowledgement()
{
  ZuTestScope(probeAcknowledgement);
  Loop loop;
  Endpoint client{&loop, true, Transport::FIX4, Version::V4_4};
  Endpoint server{&loop, false, Transport::FIX4, Version::V4_4};
  client.peer = &server; server.peer = &client;
  loop.post(Work{.endpoint = &server, .kind = WorkKind::Open});
  loop.post(Work{.endpoint = &client, .kind = WorkKind::Open}); loop.drain();
  auto initialAck = client.tx.acknowledged_();
  ZuCheck(!loop.errors && client.rx.session_().ready_() && loop.wire.length() == 2);
  ZuCheck(loop.submit(client, "before-probe") == TxSelect::Accepted); loop.drain();
  ZuCheck(server.orders.length() == 1 && client.tx.acknowledged_() == initialAck);
  auto probeSeq = client.tx.next_();
  loop.post(Work{.endpoint = &client, .kind = WorkKind::Probe}); loop.drain();
  ZuCheck(!loop.errors && client.tx.next_() == probeSeq + 1 &&
    client.tx.acknowledged_() == probeSeq + 1 && !client.tx.retained_() &&
    client.acks.length() && client.acks[client.acks.length() - 1] == probeSeq + 1);
  ZuCheck(client.tx.archives.length() && client.tx.archives[0].epoch == 1 &&
    client.tx.archives[0].begin == initialAck && client.tx.archives[0].end == probeSeq + 1 &&
    client.tx.history.length() == 1);

  auto lostSeq = client.tx.next_();
  loop.dropOrder = true;
  ZuCheck(loop.submit(client, "recover-before-echo") == TxSelect::Accepted);
  loop.post(Work{.endpoint = &client, .kind = WorkKind::Probe}); loop.drain();
  ZuCheck(!loop.errors && loop.dropped == lostSeq && server.orders.length() == 2 &&
    server.orders[1] == "recover-before-echo");
  bool replayed = false;
  for (const auto &wire : loop.wire)
    if (wire.client && wire.context.replay && wire.context.seqNo == lostSeq &&
	wire.body->msgType() == "D") replayed = true;
  ZuCheck(replayed && client.tx.acknowledged_() >= lostSeq &&
    client.tx.acknowledged_() <= client.tx.published_());
  ZuCheck(client.rx.session_().ready_() && server.rx.session_().ready_());
}

static void draining()
{
  ZuTestScope(draining);
  Loop loop;
  Endpoint client{&loop, true, Transport::FIX4, Version::V4_4};
  Endpoint server{&loop, false, Transport::FIX4, Version::V4_4};
  client.peer = &server; server.peer = &client;
  loop.post(Work{.endpoint = &server, .kind = WorkKind::Open});
  loop.post(Work{.endpoint = &client, .kind = WorkKind::Open}); loop.drain();
  auto lostSeq = client.tx.next_();
  loop.dropOrder = true;
  ZuCheck(loop.submit(client, "old-epoch") == TxSelect::Accepted);
  loop.post(Work{.endpoint = &client, .kind = WorkKind::Drain}); loop.drain();
  ZuCheck(!loop.errors && loop.dropped == lostSeq && server.orders.length() == 1 &&
    server.orders[0] == "old-epoch");
  ZuCheck(client.rx.session_().drained_() && !client.rx.session_().ready_());
  ZuCheck(client.tx.acknowledged_() >= client.rx.session_().drainBoundary_() &&
    client.tx.acknowledged_() <= client.tx.published_());
  auto next = client.tx.next_();
  ZuCheck(loop.submit(client, "new-epoch") == TxSelect::Held && client.tx.next_() == next);
  ZuCheck(!server.rx.session_().draining_() && server.rx.session_().ready_());
  loop.post(Work{.endpoint = &server, .kind = WorkKind::Drain}); loop.drain();
  ZuCheck(!loop.errors && server.rx.session_().drained_() && !server.rx.session_().ready_());
  ZuCheck(loop.submit(server, "held") == TxSelect::Held);
}

static void liveReset()
{
  ZuTestScope(liveReset);
  for (unsigned mode = Transport::FIX4; mode <= Transport::FIXT11; ++mode) {
    for (bool clientDriver : {true, false}) {
      Loop loop;
      Endpoint client{&loop, true, mode, Version::V4_4};
      Endpoint server{&loop, false, mode, Version::V4_4};
      client.peer = &server; server.peer = &client;
      loop.post(Work{.endpoint = &server, .kind = WorkKind::Open});
      loop.post(Work{.endpoint = &client, .kind = WorkKind::Open}); loop.drain();
      ZuCheck(loop.submit(client, "old") == TxSelect::Accepted); loop.drain();
      for (auto ep : {&client, &server}) loop.post(Work{.endpoint = ep, .kind = WorkKind::Drain});
      loop.drain();
      ZuCheck(!loop.errors && client.rx.session_().drained_() && server.rx.session_().drained_());
      auto before = loop.wire.length();
      auto frontier = client.tx.next_();
      ZuCheck(loop.enqueue(client, "queued")); loop.drain();
      ZuCheck(loop.wire.length() == before && client.tx.next_() == frontier &&
	client.tx.appQueue.count_() == 1);
      for (auto ep : {&client, &server})
	loop.post(Work{.endpoint = ep, .kind = WorkKind::Prepare,
	  .event = {.nextEpoch = 2, .resetInitiator = ep->client == clientDriver},
	    .context = {1, 1, 0}});
      loop.drain();
      ZuCheck(!loop.errors && client.rx.session_().epoch_() == 2
	&& server.rx.session_().epoch_() == 2 &&
	client.tx.epoch_() == 2 && server.tx.epoch_() == 2 && loop.wire.length() == before);
      ZuCheck(client.rx.expected_() == 1 && server.rx.expected_() == 1 &&
	client.tx.next_() == 1 && server.tx.next_() == 1);
      Checkpoint state;
      loop.owner = 1; ZuCheck(client.rx.snapshot_(state));
      loop.owner = 2; ZuCheck(client.tx.snapshot_(state)); loop.owner = 0;
      ZuCheck(state.epoch == 2 && state.fields == StateMask::All &&
	state.rx.processed == 1 && state.tx.loaded == 1);
      auto driver = clientDriver ? &client : &server;
      auto follower = driver->peer;
      // Start the driver first. The prepared peer retains the reset frame until
      // its explicit start releases ingress; neither owner guesses peer readiness.
      loop.post(Work{.endpoint = driver, .kind = WorkKind::ResetStart, .context = {2, 1, 0}});
      loop.drain();
      ZuCheck(!loop.errors && follower->blockedFrame && !driver->rx.session_().ready_());
      loop.post(Work{.endpoint = follower, .kind = WorkKind::ResetStart, .context = {2, 1, 0}});
      loop.drain();
      ZuCheck(!loop.errors && client.rx.session_().ready_() && server.rx.session_().ready_() &&
	!follower->blockedFrame);
      unsigned resetLogons = 0;
      for (unsigned i = before, n = loop.wire.length(); i < n; ++i) {
	const auto &wire = loop.wire[i];
	if (wire.body->msgType() != "A") continue;
	Session::Logon logon;
	ZuCheck(!load(logon, wire.body->body()) && logon.resetSeqNum &&
	  wire.context.epoch == 2 && wire.context.connection == 1 && wire.context.seqNo == 1);
	++resetLogons;
      }
      ZuCheck(resetLogons == 2 && client.rx.session_().connection_() == 1 &&
	server.rx.session_().connection_() == 1);
      loop.owner = 2;
      ZuCheck(client.tx.apply_(SessionEvent{.actions = SessionAction::Close,
	.state = State::Closing, .connection = 1, .epoch = 1}) == TxSelect::Invalid);
      loop.owner = 0;
      ZuCheck(loop.submit(client, "new") == TxSelect::Accepted); loop.drain();
      ZuCheck(server.orders.length() == 3 && server.orders[0] == "old"
	&& server.orders[1] == "queued" &&
	server.orders[2] == "new" && server.epochs[0] == 1 && server.epochs[1] == 2 &&
	server.epochs[2] == 2 && !client.tx.appBytes);
    }
  }
}

static void reconnectReset()
{
  ZuTestScope(reconnectReset);
  for (unsigned mode = Transport::FIX4; mode <= Transport::FIXT11; ++mode) {
    Loop loop;
    Endpoint client{&loop, true, mode, Version::V4_4};
    Endpoint server{&loop, false, mode, Version::V4_4};
    client.peer = &server; server.peer = &client;
    loop.post(Work{.endpoint = &server, .kind = WorkKind::Open});
    loop.post(Work{.endpoint = &client, .kind = WorkKind::Open}); loop.drain();
    ZuCheck(loop.submit(client, "old") == TxSelect::Accepted); loop.drain();
    for (auto ep : {&client, &server}) loop.post(Work{.endpoint = ep, .kind = WorkKind::Drain});
    loop.drain();
    auto before = loop.wire.length();
    auto frontier = client.tx.next_();
    ZuCheck(loop.enqueue(client, "queued")); loop.drain();
    ZuCheck(loop.wire.length() == before && client.tx.next_() == frontier);
    for (auto ep : {&client, &server})
      loop.post(Work{.endpoint = ep, .kind = WorkKind::Prepare,
	.event = {.nextEpoch = 2, .resetInitiator = ep->client}, .context = {1, 1, 0},
	.path = ResetPath::Reconnect});
    loop.drain();
    ZuCheck(!loop.errors && client.rx.session_().epoch_() == 2
      && server.rx.session_().epoch_() == 2);
    for (auto ep : {&client, &server})
      loop.post(Work{.endpoint = ep, .kind = WorkKind::ResetDisconnect, .context = {2, 1, 0}});
    loop.drain();
    ZuCheck(!loop.errors && client.rx.session_().state_() == State::Disconnected &&
      server.rx.session_().state_() == State::Disconnected && loop.wire.length() == before);
    loop.post(Work{.endpoint = &server, .kind = WorkKind::Open});
    loop.post(Work{.endpoint = &client, .kind = WorkKind::Open}); loop.drain();
    ZuCheck(!loop.errors && client.rx.session_().connection_() == 2
      && server.rx.session_().connection_() == 2 &&
      client.rx.session_().ready_() && server.rx.session_().ready_());
    unsigned count = 0;
    for (unsigned i = before, n = loop.wire.length(); i < n; ++i) {
      const auto &wire = loop.wire[i];
      if (wire.body->msgType() != "A") continue;
      Session::Logon logon;
      ZuCheck(!load(logon, wire.body->body()) && logon.resetSeqNum && wire.context.seqNo == 1 &&
	wire.context.epoch == 2 && wire.context.connection == 2);
      ++count;
    }
    ZuCheck(count == 2 && loop.submit(client, "new") == TxSelect::Accepted); loop.drain();
    ZuCheck(server.orders.length() == 3 && server.orders[1] == "queued"
      && server.orders[2] == "new" &&
      server.epochs[1] == 2 && server.epochs[2] == 2 && !client.tx.appBytes);
  }
}

static void continuingReconnect()
{
  ZuTestScope(continuingReconnect);
  Loop loop;
  Endpoint client{&loop, true, Transport::FIX4, Version::V4_4};
  Endpoint server{&loop, false, Transport::FIX4, Version::V4_4};
  client.peer = &server; server.peer = &client;
  loop.post(Work{.endpoint = &server, .kind = WorkKind::Open});
  loop.post(Work{.endpoint = &client, .kind = WorkKind::Open}); loop.drain();
  ZuCheck(loop.submit(client, "old") == TxSelect::Accepted); loop.drain();
  loop.post(Work{.endpoint = &client, .kind = WorkKind::Logout}); loop.drain();
  auto before = loop.wire.length();
  auto next = client.tx.next_();
  loop.post(Work{.endpoint = &server, .kind = WorkKind::Open});
  loop.post(Work{.endpoint = &client, .kind = WorkKind::Open}); loop.drain();
  ZuCheck(!loop.errors && client.rx.session_().ready_() && server.rx.session_().ready_() &&
    client.rx.session_().epoch_() == 1 && client.tx.epoch_() == 1);
  unsigned count = 0;
  for (unsigned i = before, n = loop.wire.length(); i < n; ++i) {
    const auto &wire = loop.wire[i];
    if (wire.body->msgType() != "A") continue;
    Session::Logon logon;
    ZuCheck(!load(logon, wire.body->body()) && !logon.resetSeqNum &&
      wire.context.connection == 2 && wire.context.epoch == 1 && wire.context.seqNo > 1);
    if (wire.client) ZuCheck(wire.context.seqNo == next);
    ++count;
  }
  ZuCheck(count == 2 && loop.submit(client, "new") == TxSelect::Accepted); loop.drain();
  ZuCheck(server.orders.length() == 2 && server.orders[1] == "new" && server.epochs[1] == 1);
}

static void resetCloseRace()
{
  ZuTestScope(resetCloseRace);
  for (bool committed : {false, true}) {
    Loop loop;
    Endpoint client{&loop, true, Transport::FIX4, Version::V4_4};
    Endpoint server{&loop, false, Transport::FIX4, Version::V4_4};
    client.peer = &server; server.peer = &client;
    loop.post(Work{.endpoint = &server, .kind = WorkKind::Open});
    loop.post(Work{.endpoint = &client, .kind = WorkKind::Open}); loop.drain();
    for (auto ep : {&client, &server}) loop.post(Work{.endpoint = ep, .kind = WorkKind::Drain});
    loop.drain();
    auto before = loop.wire.length();
    loop.post(Work{.endpoint = &client, .kind = WorkKind::Prepare,
      .event = {.nextEpoch = 2, .resetInitiator = true}, .context = {1, 1, 0}});
    loop.drain(1);
    if (committed) {
      unsigned steps = 0;
      while (client.tx.epoch_() == 1 && steps++ < 100) loop.drain(1);
      ZuCheck(client.tx.epoch_() == 2 && loop.cursor < loop.work.length());
      // Model an Rx timer already queued before the cross-owner commit notice.
      auto notice = ZuMv(loop.work[loop.cursor]);
      ZuCheck(notice.kind == WorkKind::Notice && notice.notice.kind == TxNoticeKind::ResetReady);
      loop.work[loop.cursor] = Work{.endpoint = &client, .kind = WorkKind::RxTimer,
	.context = {1, 1, 0}, .task = SessionTimer::Logon};
      loop.post(ZuMv(notice));
    } else {
      // Let an already queued Rx deadline run before Tx consumes Prepare.
      // Its Close then follows Prepare on Tx, ahead of the posted reset task.
      auto prepare = ZuMv(loop.work[loop.cursor]);
      ZuCheck(prepare.kind == WorkKind::Event &&
	(prepare.event.actions & SessionAction::ResetPrepare));
      loop.work[loop.cursor] = Work{.endpoint = &client, .kind = WorkKind::RxTimer,
	.context = {1, 1, 0}, .task = SessionTimer::Logon};
      loop.post(ZuMv(prepare));
    }
    loop.now = ZuTime{111}; loop.drain();
    auto epoch = committed ? 2 : 1;
    ZuCheck(client.rx.session_().state_() == State::Disconnected &&
      !client.rx.session_().resetPreparing_() && client.rx.session_().epoch_() == epoch &&
      client.tx.epoch_() == epoch && loop.wire.length() == before);
    if (!committed) {
      auto errors = loop.errors;
      loop.post(Work{.endpoint = &server, .kind = WorkKind::Open});
      loop.post(Work{.endpoint = &client, .kind = WorkKind::Open}); loop.drain();
      ZuCheck(loop.errors == errors && client.rx.session_().ready_()
	&& server.rx.session_().ready_() &&
	client.rx.session_().epoch_() == 1 && client.tx.epoch_() == 1);
    }
  }
}

static void initialReset()
{
  ZuTestScope(initialReset);
  for (unsigned mode = Transport::FIX4; mode <= Transport::FIXT11; ++mode) {
    Loop loop;
    auto version = mode == Transport::FIXT11 ? Version::VT1_1 : Version::V4_4;
    Endpoint client{&loop, true, mode, version};
    Endpoint server{&loop, false, mode, version};
    client.peer = &server; server.peer = &client;
    for (auto ep : {&client, &server})
      loop.post(Work{.endpoint = ep, .kind = WorkKind::InitialReset, .context = {1, 0, 0}});
    loop.post(Work{.endpoint = &server, .kind = WorkKind::Open});
    loop.post(Work{.endpoint = &client, .kind = WorkKind::Open}); loop.drain();
    ZuCheck(!loop.errors && client.rx.session_().ready_() && server.rx.session_().ready_());
    unsigned count = 0;
    for (const auto &wire : loop.wire) {
      if (wire.body->msgType() != "A") continue;
      Session::Logon logon;
      ZuCheck(!load(logon, wire.body->body()) && logon.resetSeqNum &&
	wire.context.seqNo == 1 && wire.context.epoch == 1 && wire.context.connection == 1);
      ++count;
    }
    ZuCheck(count == 2 && loop.submit(client, "new") == TxSelect::Accepted); loop.drain();
    ZuCheck(server.orders.length() == 1 && server.orders[0] == "new" && server.epochs[0] == 1);
    loop.post(Work{.endpoint = &client, .kind = WorkKind::Logout}); loop.drain();
    auto before = loop.wire.length();
    loop.post(Work{.endpoint = &server, .kind = WorkKind::Open});
    loop.post(Work{.endpoint = &client, .kind = WorkKind::Open}); loop.drain();
    ZuCheck(!loop.errors && client.rx.session_().ready_() && server.rx.session_().ready_());
    count = 0;
    for (unsigned i = before, n = loop.wire.length(); i < n; ++i) {
      const auto &wire = loop.wire[i]; if (wire.body->msgType() != "A") continue;
      Session::Logon logon;
      ZuCheck(!load(logon, wire.body->body()) && !logon.resetSeqNum &&
	wire.context.seqNo > 1 && wire.context.epoch == 1 && wire.context.connection == 2);
      ++count;
    }
    ZuCheck(count == 2);
  }
}

static void peerReject()
{
  ZuTestScope(peerReject);
  for (unsigned mode = Transport::FIX4; mode <= Transport::FIXT11; ++mode) {
    Loop loop;
    Endpoint client{&loop, true, mode, Version::V4_4};
    Endpoint server{&loop, false, mode, Version::V4_4};
    client.peer = &server; server.peer = &client; server.rejectBusiness = true;
    loop.post(Work{.endpoint = &server, .kind = WorkKind::Open});
    loop.post(Work{.endpoint = &client, .kind = WorkKind::Open}); loop.drain();
    loop.post(Work{.endpoint = &client, .kind = WorkKind::Probe}); loop.drain();
    auto seqNo = client.tx.next_(); auto ackd = client.tx.acknowledged_();
    ZuCheck(loop.submit(client, "bad") == TxSelect::Accepted); loop.drain();
    ZuCheck(!loop.errors && client.peerRejects.length() == 1 && client.rejectIds.length() == 1 &&
      client.rejectIds[0] == client.submission && client.rejectIds[0] != seqNo);
    const auto &report = client.peerRejects[0];
    ZuCheck(report.context.epoch == 1 && report.context.connection == 1 &&
      report.message.refSeqNo == seqNo && report.message.refTagID == 11 &&
      report.message.reason == 5 && report.message.text == "bad value" && !report.possDup &&
      client.tx.acknowledged_() == ackd);
    auto parsed = Fmt::frame(report.wire);
    ZuCheck(parsed.state == FrameState::Complete && parsed.view.msgType == "3" &&
      !server.orders.length());
    loop.post(Work{.endpoint = &server, .kind = WorkKind::Resend,
      .context = {1, 1, 0}, .begin = report.context.seqNo, .end = report.context.seqNo});
    loop.drain();
    ZuCheck(!loop.errors && client.peerRejects.length() == 1 && client.rejectIds.length() == 1);
    auto old = client.peerRejects[0];
    for (auto ep : {&client, &server}) loop.post(Work{.endpoint = ep, .kind = WorkKind::Drain});
    loop.drain();
    for (auto ep : {&client, &server})
      loop.post(Work{.endpoint = ep, .kind = WorkKind::Prepare,
	.event = {.nextEpoch = 2, .resetInitiator = ep->client}, .context = {1, 1, 0}});
    loop.drain();
    for (auto ep : {&client, &server})
      loop.post(Work{.endpoint = ep, .kind = WorkKind::ResetStart, .context = {2, 1, 0}});
    loop.drain();
    ZuCheck(!loop.errors && client.rx.session_().ready_() && client.tx.epoch_() == 2);
    loop.owner = 2;
    ZuCheck(!client.tx.peerReject_(old) && client.peerRejects.length() == 1);
    loop.owner = 0;
    loop.post(Work{.endpoint = &client, .kind = WorkKind::Probe}); loop.drain();
    ZuCheck(client.tx.next_() == seqNo && loop.submit(client, "bad again") == TxSelect::Accepted);
    loop.drain();
    ZuCheck(!loop.errors && client.peerRejects.length() == 2 && client.rejectIds.length() == 2 &&
      client.peerRejects[1].context.epoch == 2
	&& client.peerRejects[1].message.refSeqNo == seqNo &&
      client.rejectIds[1] == client.submission && client.rejectIds[1] != client.rejectIds[0] &&
      old.message.text == "bad value");
  }
}

static void simultaneousRecovery()
{
  ZuTestScope(simultaneousRecovery);
  for (unsigned mode = Transport::FIX4; mode <= Transport::FIXT11; ++mode) {
    Loop loop;
    Endpoint client{&loop, true, mode, Version::V4_4};
    Endpoint server{&loop, false, mode, Version::V4_4};
    client.peer = &server; server.peer = &client;
    loop.post(Work{.endpoint = &server, .kind = WorkKind::Open});
    loop.post(Work{.endpoint = &client, .kind = WorkKind::Open}); loop.drain();
    client.dropNextOrder = server.dropNextOrder = true;
    ZuCheck(loop.submit(client, "c1") == TxSelect::Accepted &&
      loop.submit(client, "c2") == TxSelect::Accepted &&
      loop.submit(server, "s1") == TxSelect::Accepted &&
      loop.submit(server, "s2") == TxSelect::Accepted);
    unsigned steps = 0;
    while (!(client.rx.retained_() && server.rx.retained_()) && steps++ < 100) loop.drain(1);
    ZuCheck(!loop.errors && client.dropped && server.dropped &&
      client.rx.retained_() && server.rx.retained_() && !client.rx.session_().ready_() &&
      !server.rx.session_().ready_() && client.rx.expected_() == server.dropped &&
      server.rx.expected_() == client.dropped);
    loop.drain();
    ZuCheck(!loop.errors && client.rx.session_().ready_() && server.rx.session_().ready_() &&
      !client.rx.retained_() && !server.rx.retained_());
    ZuCheck(client.orders.length() == 2 && client.orders[0] == "s1" && client.orders[1] == "s2" &&
      server.orders.length() == 2 && server.orders[0] == "c1" && server.orders[1] == "c2");
    bool cReplay = false, sReplay = false;
    for (const auto &wire : loop.wire) {
      if (!wire.context.replay || wire.body->msgType() != "D") continue;
      if (wire.client && wire.context.seqNo == client.dropped) cReplay = true;
      if (!wire.client && wire.context.seqNo == server.dropped) sReplay = true;
    }
    ZuCheck(cReplay && sReplay && loop.submit(client, "c3") == TxSelect::Accepted &&
      loop.submit(server, "s3") == TxSelect::Accepted); loop.drain();
    ZuCheck(!loop.errors && client.orders.length() == 3 && client.orders[2] == "s3" &&
      server.orders.length() == 3 && server.orders[2] == "c3");
  }
}

static ZuDateTime frameTime(const ZiIOBuf *buffer)
{
  auto parsed = Fmt::frame(ZuCSpan{buffer->cspan()});
  Header h;
  ZmAssert(parsed.state == FrameState::Complete && !readHeader(parsed.view, h));
  return h.sendingTime;
}
static void partialWrites()
{
  ZuTestScope(partialWrites);
  for (unsigned mode = Transport::FIX4; mode <= Transport::FIXT11; ++mode) {
    unsigned length = 0;
    for (unsigned cut = 0; !length || cut <= length; ++cut) {
      Loop loop;
      auto version = mode == Transport::FIXT11 ? Version::VT1_1 : Version::V4_4;
      Endpoint client{&loop, true, mode, version};
      Endpoint server{&loop, false, mode, version};
      client.peer = &server; server.peer = &client;
      loop.post(Work{.endpoint = &server, .kind = WorkKind::Open});
      loop.post(Work{.endpoint = &client, .kind = WorkKind::Open}); loop.drain();
      auto seqNo = client.tx.next_(); auto ackd = client.tx.acknowledged_();
      auto completed = client.completions; auto associated = client.associations.length();
      client.tx.holdNext = true;
      ZuCheck(loop.submit(client, "fragmented") == TxSelect::Accepted); loop.drain();
      ZuCheck(client.tx.fragmentFrame && !server.orders.length() && client.tx.inFlight_() == 1 &&
	client.tx.next_() == seqNo + 1 && client.tx.published_() == seqNo + 1);
      if (!client.tx.fragmentFrame) break;
      auto context = client.tx.fragmentContext;
      auto ticket = client.tx.writeTicket;
      auto framePtr = client.tx.fragmentFrame.ptr();
      if (!length) length = framePtr->length;
      ZuCheck(length == framePtr->length && context.seqNo == seqNo && !context.replay);
      auto original = frameTime(framePtr);
      Session::Msg hb; hb.p<Session::HeartBt>(Session::HeartBt{}); Error error;
      auto body = TxBody::control(hb, error);
      loop.post(Work{.endpoint = &client, .kind = WorkKind::Control,
	.context = {1, 1, 0}, .body = ZuMv(body)}); loop.drain();
      ZuCheck(!error && client.tx.next_() == seqNo + 2 && client.tx.published_() == seqNo + 1 &&
	client.completions == completed && client.associations.length() == associated + 2);
      loop.utc += ZuTime{1};
      loop.post(Work{.endpoint = &client, .kind = WorkKind::Write, .context = context,
	.ticket = ticket, .begin = cut});
      loop.drain();
      if (cut < length) {
	ZuCheck(!loop.errors && !server.orders.length() && client.completions == completed &&
	  client.tx.inFlight_() == 1 && client.tx.fragmentFrame.ptr() == framePtr &&
	  frameTime(framePtr) == original && client.tx.published_() == seqNo + 1 &&
	  client.tx.acknowledged_() == ackd);
	ZuCheck(server.rx.incomplete == unsigned(cut != 0));
	loop.post(Work{.endpoint = &client, .kind = WorkKind::Write, .context = context,
	  .ticket = ticket}); loop.drain();
	ZuCheck(client.completions == completed && client.tx.offset == cut);
	loop.post(Work{.endpoint = &client, .kind = WorkKind::Write,
	  .context = context, .ticket = ticket, .begin = length - cut}); loop.drain();
      }
      ZuCheck(!loop.errors && server.orders.length() == 1 && server.orders[0] == "fragmented" &&
	client.completions == completed + 2 && !client.failures && !client.tx.fragmentFrame &&
	!client.tx.inFlight_() && client.tx.published_() == seqNo + 2
	  && client.tx.acknowledged_() == ackd);
      loop.owner = 2;
      ZuCheck(!client.tx.write_(context, 0, true, ticket) && client.completions == completed + 2);
      loop.owner = 0;
      ZuCheck(server.rx.expected_() == seqNo + 2
	&& client.associations.length() == associated + 2);
    }
  }
}
static void partialFailure()
{
  ZuTestScope(partialFailure);
  for (unsigned mode = Transport::FIX4; mode <= Transport::FIXT11; ++mode)
    for (bool delivered : {false, true}) {
      Loop loop;
      Endpoint client{&loop, true, mode, Version::V4_4};
      Endpoint server{&loop, false, mode, Version::V4_4};
      client.peer = &server; server.peer = &client;
      loop.post(Work{.endpoint = &server, .kind = WorkKind::Open});
      loop.post(Work{.endpoint = &client, .kind = WorkKind::Open}); loop.drain();
      auto seqNo = client.tx.next_(); auto ackd = client.tx.acknowledged_();
      auto completed = client.completions;
      client.tx.holdNext = client.tx.holdCompletion = true;
      ZuCheck(loop.submit(client, "uncertain") == TxSelect::Accepted); loop.drain();
      ZuCheck(client.tx.fragmentFrame && client.tx.inFlight_() == 1);
      if (!client.tx.fragmentFrame) continue;
      auto context = client.tx.fragmentContext;
      auto ticket = client.tx.writeTicket;
      auto original = frameTime(client.tx.fragmentFrame);
      auto count = delivered ? client.tx.fragmentFrame->length :
	client.tx.fragmentFrame->length>>1;
      loop.post(Work{.endpoint = &client, .kind = WorkKind::Write, .context = context,
	.ticket = ticket, .begin = count});
      loop.drain();
      ZuCheck(!loop.errors && client.completions == completed && client.tx.inFlight_() == 1 &&
	server.orders.length() == unsigned(delivered) && client.tx.acknowledged_() == ackd);
      ZuCheck(loop.enqueue(client, "retained"));
      loop.post(Work{.endpoint = &client, .kind = WorkKind::Write, .context = context,
	.ticket = ticket, .ok = false});
      loop.drain();
      ZuCheck(client.rx.session_().state_() == State::Disconnected &&
	server.rx.session_().state_() == State::Disconnected && client.failures == 1 &&
	client.completions == completed + 1 && !client.tx.inFlight_()
	  && !client.tx.fragmentFrame &&
	!server.rx.bytes && client.tx.epoch_() == 1 && client.tx.published_() == seqNo + 1);
      auto errors = loop.errors;
      auto assigned = client.tx.next_();
      ZuCheck(assigned == seqNo + 2 && !client.released.length());
      loop.utc += ZuTime{5};
      loop.post(Work{.endpoint = &server, .kind = WorkKind::Open});
      loop.post(Work{.endpoint = &client, .kind = WorkKind::Open}); loop.drain();
      ZuCheck(loop.errors == errors && client.rx.session_().ready_()
	&& server.rx.session_().ready_() &&
	client.tx.epoch_() == 1 && client.tx.connection_() == 2 && server.orders.length() == 2 &&
	server.orders[0] == "uncertain" && server.orders[1] == "retained" && !client.tx.appBytes);
      bool continuing = false, replayed = false, retained = false, first = true;
      uint64_t sent = assigned;
      for (const auto &wire : loop.wire) {
	if (!wire.client || wire.context.connection != 2) continue;
	if (!wire.context.replay && wire.context.seqNo >= assigned) {
	  ZuCheck(wire.context.seqNo == sent && wire.body->administrative());
	  ++sent;
	}
	if (first) {
	  ZuCheck(wire.body->msgType() == "A" && wire.context.seqNo == assigned);
	  first = false;
	}
	if (wire.body->msgType() == "A") {
	  Session::Logon logon;
	  ZuCheck(!load(logon, wire.body->body()) && !logon.resetSeqNum &&
	    wire.context.seqNo == assigned);
	  continuing = true;
	}
	if (wire.body->msgType() != "D") continue;
	if (wire.context.replay && wire.context.seqNo == seqNo) replayed = true;
	if (!wire.context.replay && wire.context.seqNo == seqNo + 1) retained = true;
      }
      unsigned mappings = 0;
      for (unsigned i = 0, n = client.associations.length(); i < n; ++i) {
	const auto &mapping = client.associations[i];
	if (mapping.epoch == 1 && mapping.seqNo == seqNo + 1) {
	  ZuCheck(mapping.connection == 1 && client.assocActive[i]);
	  ++mappings;
	}
      }
      ZuCheck(continuing && retained && (delivered || replayed) && mappings == 1 &&
	!client.released.length() && sent > assigned && client.tx.next_() == sent &&
	client.tx.published_() == sent);
      // The application archive permits an explicit old-sequence replay after
      // reconnect, including the already delivered/uncertain-completion case.
      loop.post(Work{.endpoint = &client, .kind = WorkKind::Resend,
	.context = {1, 2, 0}, .begin = seqNo, .end = seqNo}); loop.drain();
      ZuCheck(loop.errors == errors && server.orders.length() == 2 && client.failures == 1);
      bool originalPreserved = false;
      for (const auto &record : client.tx.history)
	if (record.epoch == 1
	  && record.seqNo == seqNo) originalPreserved = record.originalTime == original;
      ZuCheck(originalPreserved);
      loop.owner = 2;
      ZuCheck(!client.tx.write_(context, 0, false, ticket) && client.failures == 1);
      loop.owner = 0;
    }
}

static void queuedPartialWrites()
{
  ZuTestScope(queuedPartialWrites);
  for (unsigned mode = Transport::FIX4; mode <= Transport::FIXT11; ++mode)
    for (bool fail : {false, true}) {
      Loop loop;
      Endpoint client{&loop, true, mode, Version::V4_4};
      Endpoint server{&loop, false, mode, Version::V4_4};
      client.peer = &server; server.peer = &client;
      loop.post(Work{.endpoint = &server, .kind = WorkKind::Open});
      loop.post(Work{.endpoint = &client, .kind = WorkKind::Open}); loop.drain();
      auto seqNo = client.tx.next_(); auto ackd = client.tx.acknowledged_();
      auto completed = client.completions;
      client.tx.holdNext = client.tx.queueBehind = true;
      ZuCheck(loop.submit(client, "first") == TxSelect::Accepted &&
	loop.submit(client, "second") == TxSelect::Accepted && loop.submit(client,
	  "third") == TxSelect::Accepted);
      loop.drain();
      ZuCheck(!loop.errors && client.tx.fragmentFrame && client.tx.behind.count_() == 2 &&
	client.tx.inFlight_() == 3 && client.tx.next_() == seqNo + 3 &&
	client.tx.published_() == seqNo + 3 && !server.orders.length());
      if (!client.tx.fragmentFrame) continue;
      auto context = client.tx.fragmentContext;
      auto ticket = client.tx.writeTicket;
      auto original = frameTime(client.tx.fragmentFrame);
      loop.post(Work{.endpoint = &client, .kind = WorkKind::Write, .context = context,
	.ticket = ticket,
	.begin = client.tx.fragmentFrame->length / 2}); loop.drain();
      ZuCheck(client.completions == completed && server.orders.length() == 0 &&
	client.tx.inFlight_() == 3 && client.tx.acknowledged_() == ackd);
      if (fail) {
	loop.post(Work{.endpoint = &client, .kind = WorkKind::Write, .context = context,
	  .ticket = ticket, .ok = false});
	loop.drain();
	ZuCheck(client.failures == 3 && client.completions == completed + 3
	  && !client.tx.inFlight_() &&
	  !client.tx.fragmentFrame && !client.tx.queuedBytes && !client.tx.behind.count_() &&
	  client.rx.session_().state_() == State::Disconnected && !server.rx.bytes
	    && !client.released.length());
	auto errors = loop.errors; loop.utc += ZuTime{5};
	loop.post(Work{.endpoint = &server, .kind = WorkKind::Open});
	loop.post(Work{.endpoint = &client, .kind = WorkKind::Open}); loop.drain();
	ZuCheck(loop.errors == errors && client.rx.session_().ready_()
	  && server.rx.session_().ready_());
	for (uint64_t key = seqNo; key < seqNo + 3; ++key) {
	  bool replayed = false, preserved = false;
	  for (const auto &wire : loop.wire)
	    if (wire.client && wire.context.connection == 2 && wire.context.replay &&
		wire.body->msgType() == "D" && wire.context.seqNo == key) replayed = true;
	  for (const auto &record : client.tx.history)
	    if (record.epoch == 1
	      && record.seqNo == key) preserved = record.originalTime == original;
	  ZuCheck(replayed && preserved);
	}
      } else {
	unsigned i = 0;
	while (client.tx.fragmentFrame && i < 3) {
	  auto current = client.tx.fragmentContext;
	  auto currentTicket = client.tx.writeTicket;
	  ZuCheck(current.seqNo == seqNo + i);
	  loop.post(Work{.endpoint = &client, .kind = WorkKind::Write, .context = current,
	    .ticket = currentTicket,
	    .begin = client.tx.fragmentFrame->length - client.tx.offset}); loop.drain(); ++i;
	  ZuCheck(client.completions == completed + i && client.tx.inFlight_() == 3 - i);
	  loop.owner = 2; ZuCheck(!client.tx.write_(current, 0, true, currentTicket));
	    loop.owner = 0;
	}
	ZuCheck(!loop.errors && i == 3 && !client.failures && !client.tx.fragmentFrame &&
	  !client.tx.queuedBytes && !client.tx.behind.count_()
	    && client.tx.acknowledged_() == ackd);
      }
      ZuCheck(server.orders.length() == 3 && server.orders[0] == "first" &&
	server.orders[1] == "second" && server.orders[2] == "third" && !client.released.length());
      for (uint64_t id = client.submission - 2; id <= client.submission; ++id) {
	unsigned mappings = 0;
	for (unsigned i = 0, n = client.assocIds.length(); i < n; ++i)
	  if (client.assocIds[i] == id && client.assocActive[i]) ++mappings;
	ZuCheck(mappings == 1);
      }
    }
}

static void replayDuringPartial()
{
  ZuTestScope(replayDuringPartial);
  for (unsigned mode = Transport::FIX4; mode <= Transport::FIXT11; ++mode) {
    Loop loop;
    Endpoint client{&loop, true, mode, Version::V4_4};
    Endpoint server{&loop, false, mode, Version::V4_4};
    client.peer = &server; server.peer = &client;
    loop.post(Work{.endpoint = &server, .kind = WorkKind::Open});
    loop.post(Work{.endpoint = &client, .kind = WorkKind::Open}); loop.drain();
    auto seqNo = client.tx.next_(); auto completed = client.completions;
    client.tx.holdNext = client.tx.queueBehind = true;
    ZuCheck(loop.submit(client, "repeated") == TxSelect::Accepted); loop.drain();
    for (unsigned i = 0; i < 2; ++i) {
      loop.post(Work{.endpoint = &client, .kind = WorkKind::Resend,
	.context = {1, 1, 0}, .begin = seqNo, .end = seqNo}); loop.drain();
    }
    uint64_t priorTicket = 0;
    TxContext prior;
    unsigned replayed = 0, writes = 0;
    while (client.tx.fragmentFrame && writes++ < loop.wire.length()) {
      auto context = client.tx.fragmentContext;
      auto ticket = client.tx.writeTicket;
      if (context.replay && context.seqNo == seqNo) {
	if (priorTicket) {
	  loop.owner = 2;
	  ZuCheck(prior.epoch == context.epoch && prior.connection == context.connection &&
	    prior.seqNo == context.seqNo && prior.replay == context.replay
	      && priorTicket != ticket &&
	    !client.tx.write_(prior, 0, true, priorTicket));
	  loop.owner = 0;
	}
	prior = context; priorTicket = ticket; ++replayed;
      }
      loop.post(Work{.endpoint = &client, .kind = WorkKind::Write, .context = context,
	.ticket = ticket,
	.begin = client.tx.fragmentFrame->length - client.tx.offset}); loop.drain();
    }
    ZuCheck(!loop.errors && replayed == 2 && server.orders.length() == 1
      && server.orders[0] == "repeated" &&
      !client.failures && !client.tx.fragmentFrame && !client.tx.inFlight_() &&
      client.completions == completed + writes && client.rx.session_().ready_()
	&& server.rx.session_().ready_());
  }
}

static void associationOwner()
{
  ZuTestScope(associationOwner);
  for (unsigned mode = Transport::FIX4; mode <= Transport::FIXT11; ++mode)
    for (bool driver : {false, true})
      for (bool refuse : {false, true}) {
	Loop loop;
	Endpoint client{&loop, driver, mode, Version::V4_4};
	Endpoint server{&loop, !driver, mode, Version::V4_4};
	client.peer = &server; server.peer = &client;
	client.asyncMap = true; client.refuseLoad = refuse; server.rejectBusiness = true;
	loop.post(Work{.endpoint = &server, .kind = WorkKind::Open});
	loop.post(Work{.endpoint = &client, .kind = WorkKind::Open}); loop.drain();
	auto seqNo = client.tx.next_(); auto count = loop.wire.length();
	auto maps = client.associations.length();
	ZuCheck(loop.submit(client, "request") == TxSelect::Pending && client.tx.loading_() &&
	  client.tx.next_() == seqNo && loop.wire.length() == count);
	Session::Msg hb; hb.p<Session::HeartBt>(Session::HeartBt{}); Error error;
	auto body = TxBody::control(hb, error);
	ZmAssert(body && !error);
	loop.post(Work{.endpoint = &client, .kind = WorkKind::Control,
	  .context = {1, 1, 0}, .body = ZuMv(body)});
	loop.drain(1); // application association decision, before Tx completion
	ZuCheck(loop.wire.length() == count && client.tx.next_() == seqNo &&
	  client.associations.length() == maps + unsigned(!refuse));
	loop.utc += ZuTime{1}; loop.drain();
	if (refuse) ZuCheck(!loop.errors && client.refused == 1
	  && client.tx.next_() == seqNo + 1 &&
	  !client.peerRejects.length() && !client.tx.loading_()
	    && loop.wire.length() == count + 1);
	else ZuCheck(!loop.errors && !client.refused && client.peerRejects.length() == 1 &&
	  client.rejectIds[0] == client.submission
	    && client.peerRejects[0].message.refSeqNo == seqNo &&
	  client.peerRejects[0].message.text == "bad value" && !client.tx.loading_());
	if (!refuse) {
	  bool current = false;
	  for (const auto &wire : loop.wire)
	    if (wire.client == client.client && wire.body->msgType() == "D"
	      && wire.body->id() == client.submission)
	      current = wire.sendingTime == loop.utc && !*wire.origSendingTime && !wire.possDup;
	  ZuCheck(current);
	}
      }
}
static void deferredOwnerClose()
{
  ZuTestScope(deferredOwnerClose);
  for (unsigned mode = Transport::FIX4; mode <= Transport::FIXT11; ++mode)
    for (bool early : {false, true}) {
      Loop loop;
      Endpoint client{&loop, true, mode, Version::V4_4};
      Endpoint server{&loop, false, mode, Version::V4_4};
      client.peer = &server; server.peer = &client;
      client.asyncMap = client.deferLoad = true; server.rejectBusiness = true;
      loop.post(Work{.endpoint = &server, .kind = WorkKind::Open});
      loop.post(Work{.endpoint = &client, .kind = WorkKind::Open}); loop.drain();
      auto seqNo = client.tx.next_(); auto count = loop.wire.length();
      ZuCheck(loop.submit(client, "delayed") == TxSelect::Pending); loop.drain();
      ZuCheck(client.deferred.body && client.tx.loading_() && client.tx.next_() == seqNo
	&& loop.wire.length() == count);
      auto ticket = client.deferred.ticket;
      auto oldMap = client.associations.length() - 1;
      loop.post(Work{.endpoint = &client, .kind = WorkKind::Lost, .context = {1, 1, 0}});
	loop.drain();
      auto errors = loop.errors;
      ZuCheck(client.rx.session_().state_() == State::Disconnected
	&& server.rx.session_().state_() == State::Disconnected);
      // The application changes policy on its own owner; the old request stays pending.
      loop.owner = 3; client.deferLoad = false; loop.owner = 0;
      loop.utc += ZuTime{5};
      if (early) {
	loop.post(Work{.endpoint = &server, .kind = WorkKind::Open});
	loop.post(Work{.endpoint = &client, .kind = WorkKind::Open}); loop.drain();
	ZuCheck(loop.errors == errors && client.tx.connection_() == 2 && client.tx.loading_() &&
	  client.tx.next_() == seqNo && client.tx.published_() == seqNo
	    && !client.released.length() &&
	  loop.wire.length() == count);
      }
      loop.post(Work{.endpoint = &client, .kind = WorkKind::FinishAssociate}); loop.drain();
      if (!early) {
	ZuCheck(loop.errors == errors && !client.tx.loading_() && client.tx.next_() == seqNo + 1 &&
	  !client.tx.appQueue.count_() && loop.wire.length() == count);
	loop.post(Work{.endpoint = &server, .kind = WorkKind::Open});
	loop.post(Work{.endpoint = &client, .kind = WorkKind::Open}); loop.drain();
      }
      ZuCheck(loop.errors == errors && client.rx.session_().ready_()
	&& server.rx.session_().ready_() &&
	client.tx.epoch_() == 1 && client.tx.connection_() == 2 && !client.tx.appBytes
	  && !client.tx.loading_() &&
	!client.released.length() && client.assocActive[oldMap] &&
	client.peerRejects.length() == 1 && client.rejectIds[0] == client.submission &&
	client.peerRejects[0].message.refSeqNo == seqNo);
      bool current = false;
      for (const auto &wire : loop.wire)
	if (wire.client && wire.context.connection == 2 && wire.body->msgType() == "D")
	  current |= wire.sendingTime == loop.utc && wire.context.seqNo == seqNo &&
	    (!wire.possDup || (wire.context.replay && *wire.origSendingTime));
      ZuCheck(current);
      loop.owner = 2;
      ZuCheck(client.tx.loaded_(ticket,
	true) == TxSelect::Invalid && !client.tx.unloaded_(ticket) && !client.tx.loading_());
      loop.owner = 0; loop.drain();
      ZuCheck(loop.errors == errors && client.peerRejects.length() == 1);
    }
}

static void queuedAssociationOwner()
{
  ZuTestScope(queuedAssociationOwner);
  for (unsigned mode = Transport::FIX4; mode <= Transport::FIXT11; ++mode)
    for (bool refuse : {false, true}) {
      Loop loop;
      Endpoint client{&loop, true, mode, Version::V4_4};
      Endpoint server{&loop, false, mode, Version::V4_4};
      client.peer = &server; server.peer = &client;
      client.asyncMap = client.nativeQueued = true; client.refuseLoad = refuse;
      server.rejectBusiness = true;
      loop.post(Work{.endpoint = &server, .kind = WorkKind::Open});
      loop.post(Work{.endpoint = &client, .kind = WorkKind::Open}); loop.drain();
      auto seqNo = client.tx.next_(); auto count = loop.wire.length();
      ZuCheck(loop.enqueue(client, "queued") && client.tx.next_() == seqNo);
      loop.drain(1); // Tx dequeue creates only a proposed association.
      ZuCheck(client.tx.loading_() && !client.selected.length() && client.tx.next_() == seqNo &&
	loop.wire.length() == count && !client.tx.appQueue.count_());
      Session::Msg hb; hb.p<Session::HeartBt>(Session::HeartBt{}); Error error;
      auto body = TxBody::control(hb, error); ZmAssert(body && !error);
      loop.post(Work{.endpoint = &client, .kind = WorkKind::Control,
	.context = {1, 1, 0}, .body = ZuMv(body)}); loop.drain();
      ZuCheck(!loop.errors && client.selected.length() == 1 &&
	client.selected[0].body->id() == client.submission
	  && client.selected[0].context.seqNo == seqNo &&
	client.selected[0].result == (refuse ? TxSelect::Refused : TxSelect::Accepted)
	  && !client.tx.loading_());
      if (refuse) ZuCheck(client.tx.next_() == seqNo + 1 && client.refused == 1 &&
	!client.peerRejects.length() && loop.wire.length() == count + 1);
      else ZuCheck(client.tx.next_() == seqNo + 2 && client.peerRejects.length() == 1 &&
	client.rejectIds[0] == client.submission && client.selectedBeforeReject);
    }
}

int main(int argc, char **argv)
{
  parse(argc, argv); ZuTestMain();
  ZuTestCall(handshake);
  ZuTestCall(recovery);
  ZuTestCall(priorityPendingRecovery);
  ZuTestCall(heartbeats);
  ZuTestCall(probeAcknowledgement);
  ZuTestCall(draining);
  ZuTestCall(liveReset);
  ZuTestCall(reconnectReset);
  ZuTestCall(continuingReconnect);
  ZuTestCall(resetCloseRace);
  ZuTestCall(initialReset);
  ZuTestCall(peerReject);
  ZuTestCall(simultaneousRecovery);
  ZuTestCall(partialWrites);
  ZuTestCall(partialFailure);
  ZuTestCall(queuedPartialWrites);
  ZuTestCall(replayDuringPartial);
  ZuTestCall(associationOwner);
  ZuTestCall(deferredOwnerClose);
  ZuTestCall(queuedAssociationOwner);
  return 0;
}
