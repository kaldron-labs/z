//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmAssert.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZfixRx.hh>

using namespace ZuTestUtil;
using namespace Zfix;
namespace A = SessionAction;
using Values = ZtArray<uint64_t, ZtArrayHeapID<"Zfix.RxTest.Values">>;
using Events = ZtArray<SessionEvent, ZtArrayHeapID<"Zfix.RxTest.Events">>;

struct Order {
  int		id = 0;
  ZuCSpan	note;
};

ZfStruct(, (Order, FIX),
  (id, (FIX::ID<11>, Required), Int32),
  (note, (FIX::ID<58>), String));
ZfStructImpl(Order);
ZfStructImpl(Order, FIX);

static const ZuDateTime stamp{ZuDateTimeScan::FIX{}, "20261027-00:00:00"};
static SessionConfig config(unsigned mode = Transport::FIX4)
{
  SessionConfig c;
  c.header.version = Version::V4_4;
  c.header.local = "LOCAL"; c.header.peer = "PEER";
  c.mode = mode; c.appVerID = AppVersion::V5_0_SP2;
  return c;
}
static ZmRef<RxQueue::Node> rawFrame(ZuCSpan content)
{
  ZmRef<ZiIOBuf> buffer = new ZiIOBufAlloc<>;
  ZuSpan<char> storage{buffer->data(), buffer->size};
  Fmt::FrameWriter writer{storage, "FIX.4.4"};
  writer.body() << content;
  auto output = writer.finish();
  ZmAssert(output);
  buffer->skip = output.data.data() - storage.data();
  buffer->length = output.data.length();
  auto parsed = Fmt::frame(output.data);
  ZmAssert(parsed.state == Fmt::FrameState::Complete);
  ZmRef<RxQueue::Node> result = new RxQueue::Node;
  result->data().buffer = ZuMv(buffer);
  result->data().frame = parsed.view;
  return result;
}

static ZmRef<RxQueue::Node> frame(uint64_t seqNo, ZuCSpan type, ZuCSpan body,
    bool dup = false, int age = 0, ZuCSpan sender = "PEER")
{
  ZmRef<ZiIOBuf> buffer = new ZiIOBufAlloc<>;
  Fmt::Header h;
  h.beginString = "FIX.4.4"; h.msgType = type;
  h.sender = sender; h.target = "LOCAL"; h.seqNo = seqNo;
  h.sendingTime = stamp - ZuTime{age};
  h.possDup = dup;
  if (dup) h.origSendingTime = h.sendingTime - ZuTime{1};
  ZuSpan<char> storage{buffer->data(), buffer->size};
  Fmt::FrameWriter writer{storage, h.beginString};
  auto error = Fmt::writeHeader(writer.body(), h);
  ZmAssert(!error);
  writer.body() << body;
  auto output = writer.finish();
  ZmAssert(output);
  buffer->skip = output.data.data() - storage.data();
  buffer->length = output.data.length();
  auto parsed = Fmt::frame(output.data);
  ZmAssert(parsed.state == Fmt::FrameState::Complete);
  ZmRef<RxQueue::Node> result = new RxQueue::Node;
  result->data().buffer = ZuMv(buffer);
  result->data().frame = parsed.view;
  return result;
}

struct Receiver : public Rx<Receiver> {
  using PeerRejects = ZtArray<PeerReject, ZtArrayHeapID<"Zfix.RxTest.PeerReject">>;
  ZuTime	now{100};
  Events	events;
  Events	changes;
  bool		changeBeforeEvent = true;
  PeerRejects	peerRejects;
  unsigned	epochChanges = 0;
  uint64_t	oldEpoch = 0;
  uint64_t	newEpoch = 0;
  RxProgress	oldProgress;
  Values	tasks;
  Values	rejected;
  Values	business;
  Values	replay;
  Values	controls;
  Values	outputOrder;
  SessionString	echo;
  uint64_t	admission = 0;
  unsigned	task = 0;
  unsigned	echoed = 0;
  unsigned	rawCount = 0;
  unsigned	logouts = 0;
  unsigned	codecError = 0;
  unsigned	clocks = 0;
  uint64_t	gapTimer = 0;

  using Rx::Rx;

  ZuTime nowRx() { ++clocks; return now; }

  void rxPost(uint64_t ticket) { tasks.push(ticket); }

  void gapArm(uint64_t ticket) { gapTimer = ticket; }
  void gapCancel() { gapTimer = 0; }

  void epochRx(uint64_t connection, uint64_t previous, uint64_t next, RxProgress progress) {
    ZmAssert(connection == session_().connection_());
    ++epochChanges; oldEpoch = previous; newEpoch = next; oldProgress = progress;
  }

  void stateChangeRx(SessionEvent e) { changes.push(e); }

  void eventRx(SessionEvent e, ZmRef<Msg> msg) {
    if (e.actions & A::Changed)
      changeBeforeEvent &= changes.length() && changes[changes.length() - 1].change == e.change;
    events.push(e);
    if (e.actions & A::Admission) admission = e.ticket;
    if ((e.actions & A::Observe) && msg && msg->data().frame.msgType == "5") ++logouts;
    if (e.actions & A::RequestGap) outputOrder.push(2);
    if (e.actions & A::Echo) {
      ++echoed;
      if (msg) echo = msg->data().control.p<Session::TestReq>().testReqID;
    }
  }

  void rejectRx(ZmRef<Msg> msg, unsigned tag, int reason) {
    codecError = msg->data().error.code;
    rejected.push(msg->data().header.seqNo);
    rejected.push(tag); rejected.push(reason);
  }

  void resendRx(uint64_t begin, uint64_t end, ZmRef<Msg> msg) {
    outputOrder.push(1);
    replay.push(begin); replay.push(end); replay.push(msg->data().header.seqNo);
  }

  void peerRejectRx(PeerReject report) { peerRejects.push(ZuMv(report)); }

  void sessionRx(ZmRef<Msg> msg) { controls.push(msg->data().header.seqNo); }

  Fmt::Error headerRx(const Fmt::FrameView &frame, Fmt::Header &header, Fmt::Limits limits) {
    if (frame.msgType == "D") return Fmt::readHeader<Order>(frame, header, limits);
    return Fmt::readHeader(frame, header, limits);
  }

  Fmt::Error businessRx(Msg *msg, Fmt::Limits limits) {
    if (msg->data().frame.msgType != "D") return {Fmt::ErrorCode::Unresolved, 35};
    Fmt::MessageHandler<Order> h{msg->data().frame, limits};
    if (!h.valid()) return h.error();
    auto order = h.ctor();
    if (!h.valid()) return h.error();
    business.push(order.id);
    return {};
  }

  bool rawRx(uint64_t, ZmRef<Msg> msg, ZuTime, ZuDateTime) {
    ++rawCount;
    return !msg->data().header.seqNo;
  }

  unsigned send(uint64_t seqNo, ZuCSpan type, ZuCSpan body = {}, bool dup = false,
      int age = 0, ZuCSpan sender = "PEER") {
    auto msg = frame(seqNo, type, body, dup, age, sender);
    return receive_(session_().connection_(), msg, now, stamp);
  }

  void drain() { while (task < tasks.length()) drain_(tasks[task++]); }

  void begin(ZuCSpan logon = "98=0\1" "108=30\1") {
    auto e = open_(now);
    logonSent_(e.connection, 1, now);
    send(1, "A", logon);
  }

  void admit() { admit_(session_().connection_(), admission, true, now); drain(); }

  // The ordinary Heartbeat keeps existing sequence fixtures at their explicit
  // business start. Readiness is confirmed by Tx, without requiring that packet.
  void heartbeat(uint64_t seqNo = 2) {
    txRecovery_(session_().connection_(), true, now);
    SessionString body;
    if (session_().probeID_().length()) {
      notice_(TxNotice{.context = {session_().epoch_(), session_().connection_(), seqNo},
	.marker = session_().probe_(), .kind = TxNoticeKind::Probe}, now);
      body << "112=" << session_().probeID_() << '\1';
    }
    send(expected_(), "0", body);
    drain();
  }

  void ready() { begin(); admit(); heartbeat(); events.clear(); }
};

static void admission()
{
  ZuTestScope(admission);
  Receiver r{config()};
  r.begin();
  ZuCheck(r.admission && r.expected_() == 1 && r.session_().state_() == State::Logon);
  auto business = frame(2, "D", "11=42\1");
  ZuCheck(r.receive_(r.session_().connection_(), business, r.now, stamp) == RxResult::Blocked);
  ZuCheck(business && r.expected_() == 1 && !r.retained_());
  r.admit();
  ZuCheck(r.session_().state_() == State::Established && r.expected_() == 2);
  ZuCheck(r.receive_(r.session_().connection_(), business, r.now, stamp) == RxResult::Accepted);
  ZuCheck(!business && r.expected_() == 3 && r.business.length() == 1 && r.business[0] == 42);
  r.heartbeat();
  ZuCheck(r.session_().ready_());

  Receiver zero{config(), RxLimits{.readAheadBytes = 0}};
  zero.ready();
  ZuCheck(zero.session_().ready_() && zero.expected_() == 3);
}
static void ownerReadiness()
{
  ZuTestScope(ownerReadiness);
  Receiver r{config()}; r.begin(); r.admit();
  ZuCheck(r.expected_() == 2 && !r.session_().ready_() &&
    !r.session_().probeID_().length());
  for (const auto &e : r.events) ZuCheck(!(e.actions & (A::Ready | A::SendProbe)));
  r.txRecovery_(1, false, r.now);
  ZuCheck(!r.session_().ready_());
  auto frames = r.stats_().frames;
  r.txRecovery_(1, true, r.now);
  ZuCheck(r.session_().ready_() && r.expected_() == 2 && r.stats_().frames == frames &&
    !r.session_().probeID_().length() && (r.events[r.events.length() - 1].actions & A::Ready));
  r.txRecovery_(1, false, r.now);
  ZuCheck(!r.session_().ready_() && (r.events[r.events.length() - 1].actions & A::Pause));
  r.txRecovery_(1, true, r.now);
  ZuCheck(r.session_().ready_() && r.stats_().frames == frames);

  Receiver peer{config()}; peer.begin(); peer.admit();
  ZuCheck(peer.send(4, "5") == RxResult::Accepted && !peer.session_().ready_());
  peer.txRecovery_(1, true, peer.now);
  ZuCheck(peer.send(2, "4", "36=4\1" "123=Y\1", true) == RxResult::Accepted);
  peer.drain();
  ZuCheck(peer.session_().state_() == State::Closing && !peer.session_().ready_());
  for (const auto &e : peer.events) ZuCheck(!(e.actions & A::Ready));
}

static void steadyRecovery()
{
  ZuTestScope(steadyRecovery);
  Receiver r{config()}; r.ready();
  auto clocks = r.clocks;
  auto events = r.events.length(), changes = r.changes.length();
  ZuCheck(r.send(3, "D", "11=33\1") == RxResult::Accepted);
  ZuCheck(r.expected_() == 4 && r.business.length() == 1 && r.session_().ready_());
  ZuCheck(r.clocks == clocks && r.events.length() == events && r.changes.length() == changes);

  // Ready remains true during an ordinary liveness probe. Its echo must still
  // consume the challenge and acknowledge the published boundary.
  r.probe_(1, r.now);
  r.notice_(TxNotice{.context = {1, 1, 3}, .marker = r.session_().probe_(),
    .kind = TxNoticeKind::Probe}, r.now);
  SessionString echo; echo << "112=" << r.session_().probeID_() << '\1';
  clocks = r.clocks;
  ZuCheck(r.session_().ready_() && r.session_().probeID_().length());
  ZuCheck(r.send(4, "0", echo) == RxResult::Accepted);
  ZuCheck(r.session_().ready_() && !r.session_().probeID_().length() && r.clocks == clocks);
  ZuCheck(r.events[r.events.length() - 1].actions & A::Ackd);

  clocks = r.clocks;
  ZuCheck(r.send(6, "D", "11=66\1") == RxResult::Accepted);
  ZuCheck(!r.session_().ready_() && r.retained_() == 1 && r.clocks > clocks);
  ZuCheck(r.send(5, "D", "11=55\1") == RxResult::Accepted);
  r.drain(); r.heartbeat(4);
  ZuCheck(r.session_().ready_() && r.business.length() == 3);

  Receiver d{config()}; d.ready();
  ZuCheck(d.drainBegin_(1, d.now));
  auto ticket = d.events[d.events.length() - 1].drain;
  d.notice_(TxNotice{.context = {1, 1, 0}, .marker = ticket, .loaded = 3,
    .kind = TxNoticeKind::DrainBoundary}, d.now);
  clocks = d.clocks;
  ZuCheck(d.send(3, "D", "11=31\1") == RxResult::Accepted);
  ZuCheck(d.clocks > clocks && d.session_().draining_() && !d.session_().ready_());
  d.heartbeat(3);
  ZuCheck(d.session_().drained_() && !d.session_().ready_());
}
static void ordered()
{
  ZuTestScope(ordered);
  Receiver r{config()}; r.ready();
  ZuCheck(r.send(4, "D", "11=44\1") == RxResult::Accepted);
  ZuCheck(!r.session_().ready_() && r.expected_() == 3 && !r.business.length());
  ZuCheck(r.retained_() == 1
    && r.retainedBytes_() >= ZiIOBufAlloc<>::Size + sizeof(RxQueue::Node));
  ZuCheck(r.send(3, "D", "11=33\1") == RxResult::Accepted);
  r.drain();
  ZuCheck(r.business.length() == 2 && r.business[0] == 33 && r.business[1] == 44);
  ZuCheck(r.expected_() == 5 && !r.retainedBytes_() && r.session_().ready_());
  r.heartbeat();
  ZuCheck(r.session_().ready_() && r.expected_() == 6);
  ZuCheck(r.send(3, "D", "11=33\1", true) == RxResult::Ignored);
  ZuCheck(r.expected_() == 6 && r.business.length() == 2);
}
static void rejects()
{
  ZuTestScope(rejects);
  Receiver r{config()}; r.ready();
  ZuCheck(r.send(3, "1", "112=a\1" "112=b\1") == RxResult::Accepted);
  ZuCheck(r.expected_() == 4 && r.rejected.length() == 3);
  ZuCheck(r.rejected[0] == 3 && r.rejected[1] == 112 && r.rejected[2] == 13);
  ZuCheck(r.send(4, "D", "11=bad\1") == RxResult::Accepted);
  ZuCheck(r.expected_() == 5 && !r.business.length() && r.rejected.length() == 6);
  ZuCheck(r.rejected[3] == 4 && r.rejected[4] == 11 && r.rejected[5] == 5);
  // Unsupported binary metadata is not guessed. The original header is usable
  // without interpreting the fake header fields inside an unknown body.
  ZuCheck(r.send(5, "Z", "95=17\1" "96=A\1" "34=999\1\0x\1"_z) == RxResult::Accepted);
  ZuCheck(r.expected_() == 6 && r.rejected.length() == 9);
  ZuCheck(r.rejected[6] == 5 && r.rejected[7] == 35 && r.rejected[8] == 11);
}
static void earlyControls()
{
  ZuTestScope(earlyControls);
  Receiver r{config()}; r.ready();
  ZuCheck(r.send(5, "1", "112=opaque/001\1") == RxResult::Accepted);
  ZuCheck(!r.echoed && r.expected_() == 3);
  ZuCheck(r.send(3, "4", "36=5\1" "123=Y\1", true) == RxResult::Accepted);
  r.drain();
  ZuCheck(r.expected_() == 6 && r.echoed == 1 && r.echo == "opaque/001" && !r.retained_());
  r.heartbeat();
  ZuCheck(r.session_().ready_());

  Receiver resend{config()}; resend.ready();
  ZuCheck(resend.send(7, "2", "7=2\1" "16=0\1") == RxResult::Accepted);
  ZuCheck(resend.replay.length() == 3 && resend.replay[0] == 2 && resend.replay[1] == 0);
  ZuCheck(resend.outputOrder.length() >= 2 && resend.outputOrder[0] == 1
    && resend.outputOrder[1] == 2);
  ZuCheck(resend.send(3, "4", "36=8\1" "123=Y\1", true) == RxResult::Accepted);
  resend.drain();
  ZuCheck(resend.expected_() == 8 && resend.replay.length() == 3 && !resend.session_().ready_());
  resend.txRecovery_(resend.session_().connection_(), true, resend.now);
  resend.heartbeat();
  ZuCheck(resend.session_().ready_());
}
static void gapFill()
{
  ZuTestScope(gapFill);
  Receiver r{config()}; r.ready();
  r.send(5, "D", "11=55\1");
  r.send(3, "4", "36=5\1" "123=Y\1", true); r.drain();
  ZuCheck(r.expected_() == 6 && r.business.length() == 1 && r.business[0] == 55);
  ZuCheck(r.send(3, "4", "36=5\1" "123=Y\1", true) == RxResult::Ignored);
  ZuCheck(r.expected_() == 6);
  r.send(6, "4", "36=6\1" "123=Y\1", true);
  ZuCheck(r.expected_() == 7 && r.rejected.length() == 3 && r.rejected[1] == 36);
  r.send(1, "4", "36=9\1"); r.drain();
  ZuCheck(r.expected_() == 9 && r.session_().epoch_() == 1);
  r.send(1, "4", "36=8\1");
  ZuCheck(r.expected_() == 9 && r.rejected.length() == 6);
}
static void headerFailures()
{
  ZuTestScope(headerFailures);
  Receiver r{config()}; r.ready();
  r.send(3, "0", {}, false, 121);
  ZuCheck(r.expected_() == 4 && r.session_().state_() == State::Logout);
  ZuCheck(r.rejected.length() == 3 && r.rejected[1] == 52 && r.rejected[2] == 10);
  Receiver identity{config()}; identity.ready();
  identity.send(3, "D", "11=42\1", false, 0, "WRONG");
  ZuCheck(identity.expected_() == 4 && identity.business.length() == 0);
  ZuCheck(identity.session_().state_() == State::Logout && identity.rejected.length() == 3);
  Receiver low{config()}; low.ready();
  ZuCheck(low.send(1, "0") == RxResult::Refused);
  ZuCheck(low.expected_() == 3 && low.session_().state_() == State::Logout);
}
static void modes()
{
  ZuTestScope(modes);
  Receiver r{config(Transport::FIX44A)};
  r.begin("98=0\1" "108=30\1" "789=2\1"); r.admit(); r.heartbeat();
  ZuCheck(r.session_().ready_());
  Receiver ahead{config(Transport::FIX44A)};
  auto e = ahead.open_(ahead.now);
  ahead.logonSent_(e.connection, 1, ahead.now);
  ahead.send(5, "A", "98=0\1" "108=30\1" "789=2\1"); ahead.admit();
  bool request = false;
  for (const auto &event : ahead.events) if (event.actions & A::RequestGap) request = true;
  ZuCheck(!request && ahead.expected_() == 1 && !ahead.session_().ready_());
  ahead.send(1, "4", "36=5\1" "123=Y\1", true); ahead.drain(); ahead.heartbeat();
  ZuCheck(ahead.session_().ready_() && ahead.expected_() == 7);
}
static void unsafeHeaders()
{
  ZuTestScope(unsafeHeaders);
  Receiver r{config()}; r.ready();
  auto msg = rawFrame("35=0\1" "49=PEER\1" "56=LOCAL\1" "52=20261027-00:00:00\1");
  ZuCheck(r.receive_(r.session_().connection_(), msg, r.now, stamp) == RxResult::Refused);
  ZuCheck(r.expected_() == 3 && r.session_().state_() == State::Logout && !r.rejected.length());
  Receiver pair{config()}; pair.ready();
  msg = rawFrame("35=0\1" "49=PEER\1" "56=LOCAL\1" "34=3\1"
    "52=20261027-00:00:00\1" "91=orphan\1");
  ZuCheck(pair.receive_(pair.session_().connection_(), msg, pair.now,
    stamp) == RxResult::Refused);
  ZuCheck(pair.expected_() == 3 && pair.session_().state_() == State::Closing);
  Receiver original{config()}; original.ready();
  msg = rawFrame("35=0\1" "49=PEER\1" "56=LOCAL\1" "34=3\1"
    "43=Y\1" "52=20261027-00:00:00\1");
  ZuCheck(original.receive_(original.session_().connection_(), msg, original.now,
    stamp) == RxResult::Accepted);
  ZuCheck(original.expected_() == 4 && original.rejected.length() == 3 &&
    original.rejected[1] == 122 && original.session_().state_() == State::Established);
}
static void reconnect()
{
  ZuTestScope(reconnect);
  Receiver r{config()}; r.ready();
  auto old = r.session_().connection_();
  r.send(5, "D", "11=55\1");
  r.send(6, "5");
  ZuCheck(r.retained_() == 2 && r.expected_() == 3);
  r.lost_(old); r.closed_(old);
  auto e = r.open_(r.now);
  ZuCheck(e.connection != old && r.expected_() == 3 && r.retained_() == 2);
  r.logonSent_(e.connection, 8, r.now);
  r.send(3, "A", "98=0\1" "108=30\1"); r.admit();
  r.send(4, "D", "11=44\1"); r.drain();
  ZuCheck(r.business.length() == 2 && r.business[0] == 44 && r.business[1] == 55);
  ZuCheck(r.expected_() == 7 && r.session_().state_() == State::Established);
  ZuCheck(!r.controls.length() && r.logouts == 1);
  r.heartbeat();
  ZuCheck(r.session_().ready_() && r.session_().epoch_() == 1);
}
static void rejectedAdmission()
{
  ZuTestScope(rejectedAdmission);
  Receiver r{config()}; r.begin();
  auto old = r.session_().connection_();
  auto msg = frame(2, "D", "11=42\1");
  ZuCheck(r.receive_(old, msg, r.now, stamp) == RxResult::Blocked && msg);
  r.admit_(old, r.admission, false, r.now);
  ZuCheck(r.expected_() == 1 && !r.retained_());
  r.lost_(old); r.closed_(old);
  auto e = r.open_(r.now);
  r.logonSent_(e.connection, 2, r.now);
  ZuCheck(r.receive_(e.connection, msg, r.now, stamp) == RxResult::Ignored && !msg);
  r.send(1, "A", "98=0\1" "108=30\1"); r.admit(); r.heartbeat();
  ZuCheck(r.session_().ready_() && r.expected_() == 3 && !r.business.length());
}

static void headerScope()
{
  ZuTestScope(headerScope);
  Receiver r{config()}; r.ready();
  auto msg = rawFrame("35=D\1" "49=PEER\1" "56=LOCAL\1" "9001=unknown\1"
    "34=3\1" "52=20261027-00:00:00\1" "11=42\1");
  ZuCheck(r.receive_(r.session_().connection_(), msg, r.now, stamp) == RxResult::Accepted);
  ZuCheck(r.expected_() == 4 && r.business.length() == 1 && r.business[0] == 42);
  Receiver late{config()}; late.ready();
  msg = rawFrame("35=D\1" "49=PEER\1" "56=LOCAL\1" "11=42\1"
    "34=3\1" "52=20261027-00:00:00\1");
  ZuCheck(late.receive_(late.session_().connection_(), msg, late.now,
    stamp) == RxResult::Refused);
  ZuCheck(late.expected_() == 3 && !late.business.length());
}

static void highLogout()
{
  ZuTestScope(highLogout);
  Receiver r{config()}; r.ready();
  ZuCheck(r.send(5, "5", "58=bye\1") == RxResult::Accepted);
  ZuCheck(r.session_().state_() == State::Established && r.logouts == 1
    && !r.session_().ready_());
  r.send(3, "4", "36=5\1" "123=Y\1", true); r.drain();
  ZuCheck(r.expected_() == 6 && r.session_().state_() == State::Closing && r.logouts == 1);
  unsigned replies = 0;
  for (const auto &e : r.events) if (e.actions & A::SendLogout) ++replies;
  ZuCheck(replies == 1);
}

static void businessLimits()
{
  ZuTestScope(businessLimits);
  RxLimits limits;
  limits.codec.fieldBytes = 64; // fits timestamps and an explicit probe ID.
  Receiver r{config(), limits}; r.ready();
  SessionString body; body << "11=42\1" "58=";
  for (unsigned i = 0; i <= limits.codec.fieldBytes; ++i) body << 'X';
  body << '\1';
  ZuCheck(r.send(3, "D", body) == RxResult::Accepted);
  ZuCheck(r.expected_() == 4 && !r.business.length() && r.codecError == Fmt::ErrorCode::Size);
  ZuCheck(r.rejected.length() == 3 && r.rejected[1] == 58);
}

static void limitsAndRaw()
{
  ZuTestScope(limitsAndRaw);
  enum { Charge = ZiIOBufAlloc<>::Size + sizeof(RxQueue::Node) };
  Receiver r{config(), RxLimits{.readAheadBytes = Charge}}; r.ready();
  r.send(5, "D", "11=55\1");
  ZuCheck(r.retainedBytes_() == Charge);
  ZuCheck(r.send(6, "1", "112=not-admitted\1") == RxResult::Overload);
  ZuCheck(!r.echoed && r.expected_() == 3 && r.session_().state_() == State::Closing);
  Receiver count{config(), RxLimits{.readAheadCount = 0}}; count.ready();
  ZuCheck(count.send(5, "D", "11=55\1") == RxResult::Overload);
  ZuCheck(!count.retained_() && count.expected_() == 3 &&
    count.session_().state_() == State::Closing);
  auto c = config(Transport::Raw);
  Receiver raw{ZuMv(c)};
  auto msg = frame(1, "A", "98=invalid\1");
  ZuCheck(raw.receive_(999, msg, raw.now, stamp) == RxResult::Accepted && !msg);
  ZuCheck(raw.rawCount == 1 && raw.expected_() == 1 && !raw.session_().connection_());
}

static void publicationNotices()
{
  ZuTestScope(publicationNotices);
  Receiver r{config()};
  r.begin(); r.admit();
  r.probe_(r.session_().connection_(), r.now);
  SessionString heartbeat;
  heartbeat << "112=" << r.session_().probeID_() << '\1';
  ZuCheck(r.send(r.expected_(), "0", heartbeat) == RxResult::Accepted);
  r.drain();
  ZuCheck(!r.session_().ready_());
  TxNotice notice{.context = {r.session_().epoch_() + 1, r.session_().connection_(), 0},
    .marker = r.session_().probe_(), .kind = TxNoticeKind::Probe};
  r.notice_(notice, r.now);
  ZuCheck(r.send(r.expected_(), "0", heartbeat) == RxResult::Accepted);
  r.drain();
  ZuCheck(!r.session_().ready_());
  r.heartbeat();
  ZuCheck(r.session_().ready_());
  r.events.clear();
  ZuCheck(r.send(r.expected_(), "5") == RxResult::Accepted);
  r.drain();
  ZuCheck(r.session_().state_() == State::Closing);
  notice.kind = TxNoticeKind::Drained;
  notice.context.epoch = r.session_().epoch_();
  r.notice_(notice, r.now);
  bool close = false;
  for (unsigned i = 0, n = r.events.length(); i < n; ++i)
    close |= bool(r.events[i].actions & A::Close);
  ZuCheck(close);
  r.closed_(r.session_().connection_());
  ZuCheck(r.session_().state_() == State::Disconnected);
}

static void earlyProbeReply()
{
  ZuTestScope(earlyProbeReply);
  Receiver r{config()}; r.ready(); r.events.clear();
  r.now = r.session_().deadline_(SessionTimer::Receive);
  r.timer_(r.session_().connection_(), SessionTimer::Receive, r.now);
  SessionString id{r.session_().probeID_()};
  r.notice_(TxNotice{.context = {1, r.session_().connection_(), 7},
    .marker = r.session_().probe_(), .kind = TxNoticeKind::Probe}, r.now);
  SessionString body; body << "112=" << id << '\1';
  ZuCheck(r.send(5, "0", body) == RxResult::Accepted && !r.session_().ready_());
  for (const auto &event : r.events) ZuCheck(!(event.actions & A::Ackd));
  ZuCheck(r.send(3, "4", "36=5\1" "123=Y\1", true) == RxResult::Accepted);
  r.drain();
  ZuCheck(r.expected_() == 6 && !r.session_().probeID_().length() && r.session_().ready_());
  for (const auto &event : r.events) ZuCheck(!(event.actions & A::Ackd));
}

static void restoredDrainGap()
{
  ZuTestScope(restoredDrainGap);
  Receiver r{config()};
  ZuCheck(r.restore_(1, RxProgress{8, 1, 2, 8}));
  r.begin(); r.admit();
  ZuCheck(r.expected_() == 2 && !r.retained_() && !r.session_().ready_() &&
    !r.session_().probeID_().length());
  r.txRecovery_(r.session_().connection_(), true, r.now);
  r.drainBegin_(r.session_().connection_(), r.now);
  auto ticket = r.events[r.events.length() - 1].drain;
  r.notice_(TxNotice{.context = {1, r.session_().connection_(), 0}, .marker = ticket,
    .loaded = 2, .kind = TxNoticeKind::DrainBoundary}, r.now);
  ZuCheck(ticket && !r.session_().drained_() && !r.session_().probeID_().length());
  ZuCheck(r.send(2, "4", "36=8\1" "123=Y\1", true) == RxResult::Accepted);
  r.drain();
  ZuCheck(r.expected_() == 8 && r.session_().probeID_().length());
  r.heartbeat();
  ZuCheck(r.expected_() == 9 && r.session_().drained_() && !r.session_().ready_());
}

static void resetPreparation()
{
  ZuTestScope(resetPreparation);
  Receiver r{config()}; r.ready();
  ZuCheck(!r.prepareReset_(1, 1, 2, true, r.now));
  r.drainBegin_(1, r.now);
  auto ticket = r.events[r.events.length() - 1].drain;
  r.notice_(TxNotice{.context = {1, 1, 0}, .marker = ticket,
    .loaded = 2, .kind = TxNoticeKind::DrainBoundary}, r.now);
  r.heartbeat();
  auto expected = r.expected_();
  ZuCheck(r.session_().drained_() && r.prepareReset_(1, 1, 2, true, r.now));
  auto msg = frame(1, "A", "98=0\1" "108=30\1" "141=Y\1");
  ZuCheck(r.receive_(1, msg, r.now, stamp) == RxResult::Blocked && msg &&
    !msg->data().epoch && r.expected_() == expected);
  TxNotice ready{.context = {1, 1, 0}, .marker = ticket,
    .kind = TxNoticeKind::ResetReady, .nextEpoch = 2, .request = 1};
  auto bad = ready; bad.request = 0; r.notice_(bad, r.now);
  bad = ready; bad.loaded = 2; r.notice_(bad, r.now);
  ZuCheck(r.session_().epoch_() == 1 && !r.epochChanges);
  r.notice_(ready, r.now);
  ZuCheck(r.session_().epoch_() == 2 && r.expected_() == 1 && r.epochChanges == 1 &&
    r.oldEpoch == 1 && r.newEpoch == 2 && r.oldProgress.processed == expected);
  ZuCheck(!r.startReset_(1, 1, r.now) && r.startReset_(1, 2, r.now));
  r.logonSent_(1, 1, r.now);
  ZuCheck(r.receive_(1, msg, r.now, stamp) == RxResult::Accepted && !msg);
  r.heartbeat();
  ZuCheck(r.session_().ready_() && r.session_().epoch_() == 2);
  auto events = r.events.length();
  r.outputFailed_(1, 1); r.outputFailed_(2, 2);
  ZuCheck(r.session_().ready_() && r.session_().epoch_() == 2 && r.events.length() == events);
  auto old = frame(1, "0", {});
  old->data().epoch = 1; old->data().connection = 1; old->data().received = r.now;
  expected = r.expected_();
  ZuCheck(r.receive_(1, old, r.now,
    stamp) == RxResult::Ignored && !old && r.expected_() == expected);
  r.notice_(ready, r.now);
  ZuCheck(r.epochChanges == 1 && r.expected_() == expected);
}

static void resetReplacement()
{
  ZuTestScope(resetReplacement);
  Receiver r{config()}; r.ready();
  r.drainBegin_(1, r.now);
  auto ticket = r.events[r.events.length() - 1].drain;
  r.notice_(TxNotice{.context = {1, 1, 0}, .marker = ticket,
    .loaded = 2, .kind = TxNoticeKind::DrainBoundary}, r.now);
  r.heartbeat();
  auto expected = r.expected_();
  ZuCheck(r.session_().drained_() && r.prepareReset_(1, 1, 2, true, r.now));
  // Tx has committed, but its owner notification is still queued behind B.
  TxNotice ready{.context = {1, 1, 0}, .marker = ticket,
    .kind = TxNoticeKind::ResetReady, .nextEpoch = 2, .request = 1};
  auto e = r.replace_(r.now);
  ZuCheck(e.connection == 2 && e.epoch == 1 && e.nextEpoch == 2 &&
    !(e.actions & A::SendLogon) && r.session_().resetPreparing_());
  auto msg = frame(1, "A", "98=0\1" "108=30\1" "141=Y\1");
  ZuCheck(r.receive_(2, msg, r.now, stamp) == RxResult::Blocked && msg &&
    !msg->data().epoch && r.expected_() == expected);
  auto bad = ready; ++bad.marker;
  r.notice_(bad, r.now);
  ZuCheck(r.session_().epoch_() == 1 && !r.epochChanges);
  r.notice_(ready, r.now);
  ZuCheck(r.session_().connection_() == 2 && r.session_().epoch_() == 2 &&
    r.expected_() == 1 && r.epochChanges == 1 && r.oldProgress.processed == expected &&
    r.session_().resetArmed_() && !r.session_().resetPreparing_());
  r.logonSent_(2, 1, r.now);
  ZuCheck(r.receive_(2, msg, r.now, stamp) == RxResult::Held && !msg);
  r.admit(); r.heartbeat();
  ZuCheck(r.session_().ready_() && r.session_().epoch_() == 2 &&
    r.session_().connection_() == 2);
  r.notice_(ready, r.now);
  ZuCheck(r.epochChanges == 1 && r.session_().ready_());
}

static void initialReset()
{
  ZuTestScope(initialReset);
  Receiver r{config()};
  RxProgress state{2, 2};
  ZuCheck(r.restore_(1, state) && !r.resetOnOpen_(1) && r.expected_() == 2);
  state = {};
  ZuCheck(r.restore_(1, state) && !r.resetOnOpen_(2) && r.resetOnOpen_(1));
  state = {2, 2};
  ZuCheck(!r.restore_(1, state) && r.expected_() == 1 && r.session_().resetOnOpen_());
  ZuCheck(r.resetOnOpen_(1, false) && r.restore_(1, state) && !r.resetOnOpen_(1));
  state = {}; ZuCheck(r.restore_(1, state) && r.resetOnOpen_(1));
  r.begin("98=0\1" "108=30\1" "141=Y\1");
  ZuCheck(r.admission && !r.session_().ready_() && r.expected_() == 1);
  r.admit(); r.heartbeat();
  ZuCheck(r.session_().ready_() && r.session_().epoch_() == 1 && !r.epochChanges &&
    !r.resetOnOpen_(1) && r.send(r.expected_(), "D", "11=42\1") == RxResult::Accepted);
  ZuCheck(r.business.length() == 1 && r.business[0] == 42);
  Receiver raw{config(Transport::Raw)};
  ZuCheck(!raw.resetOnOpen_(1));
}

static void peerReject()
{
  ZuTestScope(peerReject);
  Receiver r{config()}; r.ready();
  auto seqNo = r.expected_();
  ZuCheck(r.send(seqNo + 1, "3",
    "45=17\1" "371=11\1" "373=5\1" "58=held text\1") == RxResult::Accepted);
  ZuCheck(!r.peerRejects.length());
  ZuCheck(r.send(seqNo, "0") == RxResult::Accepted); r.drain();
  ZuCheck(r.peerRejects.length() == 1 && !r.retained_());
  const auto &report = r.peerRejects[0];
  ZuCheck(report.buffer && report.context.epoch == 1 && report.context.connection == 1 &&
    report.context.seqNo == seqNo + 1 && report.message.refSeqNo == 17 &&
    report.message.refTagID == 11 && report.message.reason == 5 &&
    report.message.text == "held text" && !report.possDup);
  ZuCheck(Fmt::frame(report.wire).state == Fmt::FrameState::Complete &&
    report.message.text.data() >= report.wire.data() &&
    report.message.text.data() + report.message.text.length() <=
      report.wire.data() + report.wire.length());
  ZuCheck(r.send(seqNo + 1, "3", "45=17\1",
    true) == RxResult::Ignored && r.peerRejects.length() == 1);
  ZuCheck(r.send(r.expected_(), "3",
    "371=11\1") == RxResult::Accepted && r.peerRejects.length() == 1);
  auto stale = frame(r.expected_(), "3", "45=17\1");
  stale->data().epoch = 2; stale->data().connection = 1; stale->data().received = r.now;
  ZuCheck(r.receive_(1, stale, r.now,
    stamp) == RxResult::Ignored && !stale && r.peerRejects.length() == 1);
}

static void stateChanges()
{
  ZuTestScope(stateChanges);
  Receiver r{config()};
  auto e = r.open_(r.now);
  ZuCheck(r.changes.length() == 1 && r.changes[0].state == State::Logon &&
    r.changes[0].connection == e.connection && r.changes[0].epoch == 1);
  auto n = r.changes.length();
  r.logonSent_(1, 1, r.now);
  ZuCheck(n == 1 && r.changes.length() == n);
  r.send(1, "A", "98=0\1" "108=30\1");
  ZuCheck(r.admission && r.changes.length() == n);
  r.admit(); r.heartbeat();
  ZuCheck(r.session_().ready_() && r.changeBeforeEvent && r.changes.length() > n);
  uint64_t previous = 0;
  bool ordered = true;
  for (const auto &change : r.changes) {
    ordered &= change.change > previous && change.epoch == 1 && change.connection == 1 &&
      (change.actions & A::Changed);
    previous = change.change;
  }
  ZuCheck(ordered);
  n = r.changes.length();
  r.lost_(1); r.closed_(1);
  ZuCheck(r.changes.length() == n + 2 && r.changes[n].state == State::Closing &&
    r.changes[n + 1].state == State::Disconnected && r.changeBeforeEvent);
  r.closed_(1); r.timer_(1, SessionTimer::Receive, r.now + ZuTime{100});
  ZuCheck(r.changes.length() == n + 2);
  e = r.open_(r.now);
  ZuCheck(e.connection == 2 && r.changes.length() &&
    r.changes[r.changes.length() - 1].change > previous);
  Receiver timeout{config()}; timeout.open_(timeout.now);
  timeout.timer_(1, SessionTimer::Logon, timeout.now + ZuTime{10});
  ZuCheck(timeout.changes.length() == 2 && timeout.changes[1].state == State::Closing &&
    timeout.changes[1].error == SessionError::Timeout && timeout.changeBeforeEvent);
  Receiver raw{config(Transport::Raw)}; raw.open_(raw.now);
  ZuCheck(!raw.changes.length());
}

static void faultMatrix()
{
  ZuTestScope(faultMatrix);
  enum { Charge = ZiIOBufAlloc<>::Size + sizeof(RxQueue::Node) };
  for (unsigned mode = Transport::FIX4; mode <= Transport::FIXT11; ++mode)
    for (bool driver : {false, true})
      for (unsigned n = 0; n < 11; ++n) {
	auto c = config(mode); c.initiator = driver;
	Receiver r{ZuMv(c), RxLimits{.readAheadBytes = Charge}};
	r.begin("98=0\1" "108=30\1" "789=2\1" "1137=9\1"); r.admit();
	if (!driver) r.logonSent_(1, 1, r.now);
	r.heartbeat();
	ZuCheck(r.session_().ready_()); r.events.clear();
	auto seqNo = r.expected_();
	unsigned result = RxResult::Accepted, state = State::Established,
	  error = SessionError::None;
	unsigned tag = 11; int reason = 5;
	bool consume = true, reject = true;
	switch (n) {
	  case 0: result = r.send(seqNo, "D", "11=bad\1"); break;
	  case 1:
	    result = r.send(seqNo, "1", "112=a\1" "112=b\1"); tag = 112; reason = 13; break;
	  case 2:
	    result = r.send(seqNo, "0", {}, false, 121);
	    state = State::Logout; error = SessionError::Timestamp; tag = 52; reason = 10; break;
	  case 3:
	    result = r.send(seqNo, "D", "11=1\1", false, 0, "WRONG");
	    state = State::Logout; error = SessionError::Identity; tag = 49; reason = 9; break;
	  case 4:
	  case 9: {
	    SessionString content;
	    content << "35=0\1" "49=PEER\1" "56=LOCAL\1" "34=" << seqNo << '\1'
	      << "43=Y\1" "52=20261027-00:00:00\1";
	    if (n == 9) content << "122=20261027-00:00:01\1";
	    auto msg = rawFrame(content);
	    result = r.receive_(1, msg, r.now, stamp); tag = 122; reason = n == 4 ? 1 : 10; break;
	  }
	  case 5: {
	    auto msg = rawFrame("35=0\1" "49=PEER\1" "56=LOCAL\1" "52=20261027-00:00:00\1");
	    result = r.receive_(1, msg, r.now, stamp);
	    state = State::Logout; error = SessionError::Sequence; tag = 34;
	    consume = reject = false; break;
	  }
	  case 6: {
	    SessionString content;
	    content << "35=0\1" "49=PEER\1" "56=LOCAL\1" "34=" << seqNo << '\1'
	      << "52=20261027-00:00:00\1" "91=orphan\1";
	    auto msg = rawFrame(content);
	    result = r.receive_(1, msg, r.now, stamp);
	    state = State::Closing; error = SessionError::Protocol; tag = 91;
	    consume = reject = false; break;
	  }
	  case 7:
	    ZuCheck(r.send(seqNo + 2, "D",
	      "11=55\1") == RxResult::Accepted && r.retainedBytes_() == Charge);
	    result = r.send(seqNo + 3, "1", "112=blocked\1");
	    state = State::Closing; error = SessionError::Overload; tag = 0;
	    consume = reject = false; break;
	  case 8:
	    result = r.send(seqNo - 1, "0");
	    state = State::Logout; error = SessionError::Sequence; tag = 34;
	    consume = reject = false; break;
	  default: result = r.send(seqNo, "Z"); tag = 35; reason = 11; break;
	}
	unsigned expectedResult = RxResult::Accepted;
	if (!consume) expectedResult = n == 7 ? RxResult::Overload : RxResult::Refused;
	ZuCheck(result == expectedResult && r.expected_() == seqNo + unsigned(consume) &&
	  r.session_().state_() == state && !r.business.length() && !r.echoed
	    && !r.peerRejects.length());
	if (reject) ZuCheck(r.rejected.length() == 3 && r.rejected[0] == seqNo &&
	  r.rejected[1] == tag && r.rejected[2] == unsigned(reason));
	else ZuCheck(!r.rejected.length());
	SessionEvent failure;
	for (const auto &event : r.events)
	  if (event.actions & A::Failure) failure = event;
	if (error) ZuCheck(failure.error == error && failure.tag == tag && failure.epoch == 1 &&
	  failure.connection == 1 && !r.session_().ready_());
	else ZuCheck(!(failure.actions & A::Failure) && r.session_().ready_());
	switch (state) {
	  case State::Logout:
	    ZuCheck((failure.actions & A::SendLogout) && !(failure.actions & A::Close) &&
	      r.session_().deadline_(SessionTimer::Logout) == r.now + ZuTime{2} &&
	      !*r.session_().deadline_(SessionTimer::Receive)
		&& !*r.session_().deadline_(SessionTimer::Probe));
	    break;
	  case State::Closing: {
	    bool cancelled = true;
	    for (unsigned timer = SessionTimer::Logon; timer <= SessionTimer::Drain; ++timer)
	      cancelled &= !*r.session_().deadline_(timer);
	    ZuCheck((failure.actions & A::Close) && !(failure.actions & A::SendLogout) &&
	      cancelled && r.retainedBytes_() <= Charge); break;
	  }
	  default: ZuCheck(!*r.session_().deadline_(SessionTimer::Logout)); break;
	}
	ZuCheck(r.changeBeforeEvent);
      }
}

static void statistics()
{
  ZuTestScope(statistics);
  Receiver r{config()}; r.begin();
  auto first = r.stats_();
  ZuCheck(first.frames == 1 && first.bytes > 0 && first.pendingBytes > 0);
  auto blocked = frame(2, "D", "11=22\1");
  ZuCheck(r.receive_(1, blocked, r.now, stamp) == RxResult::Blocked);
  auto observed = r.stats_();
  ZuCheck(observed.frames == 2 && observed.bytes > first.bytes);
  ZuCheck(r.receive_(1, blocked, r.now, stamp) == RxResult::Blocked);
  ZuCheck(r.stats_().frames == 2 && r.stats_().bytes == observed.bytes);
  blocked = nullptr; r.admit(); r.heartbeat();
  auto ready = r.stats_();
  ZuCheck(!ready.pendingBytes && !ready.queuedCount && ready.frames == 3);
  r.send(5, "D", "11=55\1");
  auto high = r.stats_();
  ZuCheck(high.queuedCount == 1 && high.queuedBytes == r.retainedBytes_() && high.gaps > 0);
  r.send(5, "D", "11=55\1", true);
  ZuCheck(r.stats_().duplicates == 1 && r.stats_().ignored == 1);
  r.send(3, "D", "11=bad\1"); r.drain();
  auto bad = r.stats_();
  ZuCheck(bad.decodeErrors == 1 && bad.hookFailures == 1 && bad.rejectsOut == 1);
  auto old = r.session_().connection_(); r.lost_(old); r.closed_(old);
  r.open_(r.now);
  auto reopened = r.stats_();
  ZuCheck(reopened.frames == bad.frames && reopened.rejectsOut == bad.rejectsOut);
  ZuCheck(reopened.queuedCount == 1 && reopened.queuedBytes == high.queuedBytes);
}

static void association()
{
  ZuTestScope(association);
  Receiver r{config()}; r.begin();
  auto ticket = r.admission;
  ZuCheck(r.stats_().pendingBytes && r.session_().admissionPending_());
  auto e = r.bind_(true);
  ZuCheck(e.connection == 2 && e.state == State::Disconnected &&
    !(e.actions & (A::Failure | A::Close | A::SendLogon)) &&
    !r.stats_().pendingBytes && !r.session_().admissionPending_());
  auto events = r.events.length();
  r.admit_(1, ticket, true, r.now);
  r.outputFailed_(1, 1);
  r.timer_(1, SessionTimer::Logon, r.now + ZuTime{100});
  r.notice_(TxNotice{.context = {1, 1, 1}, .kind = TxNoticeKind::Logon}, r.now);
  r.notice_(TxNotice{.context = {1, 1, 0}, .kind = TxNoticeKind::Recovery,
    .done = true}, r.now);
  ZuCheck(r.events.length() == events && r.session_().state_() == State::Disconnected &&
    !r.session_().ready_() && r.expected_() == 1);
  r.now += ZuTime{100};
  e = r.start_(r.now);
  ZuCheck(e.connection == 2 && e.state == State::Logon && (e.actions & A::SendLogon) &&
    r.session_().deadline_(SessionTimer::Logon) == r.now + ZuTime{10});
  r.logonSent_(2, 1, r.now);
  ZuCheck(r.send(1, "A", "98=0\1" "108=30\1") == RxResult::Held);
  r.admit(); r.txRecovery_(2, true, r.now);
  ZuCheck(r.session_().ready_() && r.expected_() == 2 && r.session_().connection_() == 2);
}

static void resetAssociation()
{
  ZuTestScope(resetAssociation);
  Receiver r{config()}; r.ready();
  r.drainBegin_(1, r.now);
  auto ticket = r.events[r.events.length() - 1].drain;
  r.notice_(TxNotice{.context = {1, 1, 0}, .marker = ticket,
    .loaded = 2, .kind = TxNoticeKind::DrainBoundary}, r.now);
  r.heartbeat();
  ZuCheck(r.session_().drained_() && r.prepareReset_(1, 1, 2, true, r.now));
  TxNotice ready{.context = {1, 1, 0}, .marker = ticket,
    .kind = TxNoticeKind::ResetReady, .nextEpoch = 2, .request = 1};
  auto e = r.bind_(true);
  ZuCheck(e.connection == 2 && e.state == State::Disconnected &&
    r.session_().resetPreparing_());
  auto events = r.events.length();
  r.notice_(ready, r.now, true);
  ZuCheck(r.epochChanges == 1 && r.expected_() == 1 && r.session_().epoch_() == 2 &&
    r.session_().resetArmed_() && r.session_().state_() == State::Disconnected &&
    !r.session_().ready_() && r.events.length() == events + 1);
  ZuTime deadline;
  r.session_().nextTimer_(deadline);
  ZuCheck(!*deadline && (r.events[events].actions & A::ResetReady) &&
    !(r.events[events].actions & (A::SendLogon | A::RetryInput | A::Close)));
  r.notice_(ready, r.now, true);
  ZuCheck(r.epochChanges == 1 && r.events.length() == events + 1);
  e = r.bind_(true); // Preserve the armed reset when C replaces unauthenticated B.
  r.session_().nextTimer_(deadline);
  ZuCheck(e.connection == 3 && e.epoch == 2 && e.state == State::Disconnected &&
    r.epochChanges == 1 && r.session_().resetArmed_() && r.expected_() == 1 &&
    !*deadline && !(e.actions & A::SendLogon));
  e = r.start_(r.now);
  ZuCheck(e.connection == 3 && e.epoch == 2 && e.nextEpoch == 2 &&
    (e.actions & A::SendLogon) && r.session_().resetArmed_());
  r.logonSent_(3, 1, r.now);
  ZuCheck(r.send(1, "A", "98=0\1" "108=30\1" "141=Y\1") == RxResult::Held);
  r.admit(); r.txRecovery_(3, true, r.now);
  ZuCheck(r.session_().ready_() && r.session_().epoch_() == 2 &&
    r.session_().connection_() == 3 && r.expected_() == 2);
}

static void replacement()
{
  ZuTestScope(replacement);
  Receiver r{config()}; r.ready();
  auto instance = r.session_().instance_();
  ZuCheck(r.send(5, "D", "11=55\1") == RxResult::Accepted);
  ZuCheck(r.send(6, "1", "112=old-challenge\1") == RxResult::Accepted);
  auto progress = r.progress_();
  auto bytes = r.retainedBytes_();
  ZuCheck(r.retained_() == 2 && !r.echoed);
  auto e = r.bind_(true);
  ZuCheck(e.connection == 2 && e.instance == instance && e.epoch == 1 &&
    e.state == State::Disconnected && !(e.actions & (A::Failure | A::Close | A::SendLogon)));
  ZuCheck(r.progress_().received == progress.received &&
    r.expected_() == progress.processed && r.retainedBytes_() == bytes);
  e = r.start_(r.now);
  ZuCheck(e.connection == 2 && e.state == State::Logon && (e.actions & A::SendLogon));
  r.logonSent_(2, 2, r.now);
  ZuCheck(r.send(3, "A", "98=0\1" "108=30\1") == RxResult::Held);
  r.admit();
  ZuCheck(r.send(4, "D", "11=44\1") == RxResult::Accepted);
  r.drain(); r.txRecovery_(2, true, r.now);
  ZuCheck(r.expected_() == 7 && !r.retained_() && r.business.length() == 2 &&
    r.business[0] == 44 && r.business[1] == 55 && !r.echoed && r.session_().ready_());

  Receiver pending{config()}; pending.begin();
  auto ticket = pending.admission;
  e = pending.replace_(pending.now);
  pending.admit_(1, ticket, true, pending.now);
  ZuCheck(e.connection == 2 && pending.session_().state_() == State::Logon &&
    !pending.session_().ready_() && !pending.retained_());
  pending.logonSent_(2, 1, pending.now);
  ZuCheck(pending.send(1, "A", "98=0\1" "108=30\1") == RxResult::Held);
  pending.admit(); pending.txRecovery_(2, true, pending.now);
  ZuCheck(pending.expected_() == 2 && pending.session_().ready_());
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(admission);
  ZuTestCall(ownerReadiness);
  ZuTestCall(steadyRecovery);
  ZuTestCall(ordered);
  ZuTestCall(rejects);
  ZuTestCall(earlyControls);
  ZuTestCall(gapFill);
  ZuTestCall(headerFailures);
  ZuTestCall(modes);
  ZuTestCall(limitsAndRaw);
  ZuTestCall(unsafeHeaders);
  ZuTestCall(reconnect);
  ZuTestCall(rejectedAdmission);
  ZuTestCall(headerScope);
  ZuTestCall(highLogout);
  ZuTestCall(businessLimits);
  ZuTestCall(publicationNotices);
  ZuTestCall(earlyProbeReply);
  ZuTestCall(restoredDrainGap);
  ZuTestCall(resetPreparation);
  ZuTestCall(resetReplacement);
  ZuTestCall(initialReset);
  ZuTestCall(peerReject);
  ZuTestCall(stateChanges);
  ZuTestCall(faultMatrix);
  ZuTestCall(statistics);
  ZuTestCall(association);
  ZuTestCall(resetAssociation);
  ZuTestCall(replacement);
  return 0;
}
