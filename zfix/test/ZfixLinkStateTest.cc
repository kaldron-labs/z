//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZfixLinkState.hh>

using namespace ZuTestUtil;
using namespace Zfix;
namespace A = SessionAction;
namespace E = SessionError;
namespace T = SessionTimer;

static SessionConfig config(bool initiator = true, unsigned mode = Transport::FIX4,
    unsigned version = Version::V4_4)
{
  SessionConfig cfg;
  cfg.header.version = version;
  cfg.header.local = "LOCAL";
  cfg.header.peer = "PEER";
  cfg.appVerID = AppVersion::V5_0_SP2;
  cfg.initiator = initiator;
  cfg.mode = mode;
  return cfg;
}
static Fmt::Header header(uint64_t seqNo = 1, ZuCSpan version = "FIX.4.4")
{
  Fmt::Header h;
  h.beginString = version; h.msgType = "A";
  h.sender = "PEER"; h.target = "LOCAL"; h.seqNo = seqNo;
  return h;
}
static SessionEvent start(LinkState &s, uint64_t seqNo = 1)
{
  auto e = s.open_(ZuTime{100});
  if (s.config_().initiator) s.logonSent_(e.connection, seqNo, ZuTime{102});
  return e;
}
static SessionEvent establish(LinkState &s, uint64_t peerSeq = 1, uint64_t expected = 1,
    uint64_t nextExpected = 2, uint64_t localSeq = 1)
{
  auto e = start(s, localSeq);
  Session::Logon logon; logon.nextExpected = nextExpected; logon.defaultAppVerID = "9";
  auto h = header(peerSeq, Version::Wire::v2s(s.config_().header.version));
  auto admission = s.peerLogon_(e.connection, h, logon, expected, ZuTime{102});
  s.admit_(e.connection, admission.ticket, true, ZuTime{103});
  if (!s.config_().initiator) s.logonSent_(e.connection, localSeq, ZuTime{104});
  return s.peerLogon_(e.connection, h, logon, expected, ZuTime{104});
}
static SessionEvent recover(LinkState &s, ZuTime now = ZuTime{105})
{
  return s.recovery_(s.connection_(), true, true, now);
}

static void initiator()
{
  ZuTestScope(initiator);
  LinkState s{config()};
  auto e = s.open_(ZuTime{100});
  auto connection = e.connection;
  ZuCheck(e.state == State::Logon && (e.actions & A::SendLogon));
  ZuCheck(s.deadline_(T::Logon) == ZuTime{110});
  auto busy = s.open_(ZuTime{100});
  ZuCheck(busy.error == E::Busy && (busy.actions & A::Refuse) &&
    busy.connection == connection && busy.change == e.change);
  Session::Logon peer;
  e = s.peerLogon_(connection, header(), peer, 1, ZuTime{102});
  ZuCheck((e.actions & A::Admission) && e.ticket && s.admissionPending_());
  auto ticket = e.ticket;
  ZuCheck(!s.peerLogon_(connection, header(), peer, 1, ZuTime{102}).actions);
  ZuCheck(!s.admit_(connection, ticket + 1, true, ZuTime{103}).actions);
  e = s.admit_(connection, ticket, true, ZuTime{103});
  ZuCheck((e.actions & A::RetryInput) && !s.admissionPending_());
  ZuCheck(!s.peerLogon_(connection, header(), peer, 1, ZuTime{103}).actions);
  ZuCheck(s.state_() == State::Logon && !s.ready_());
  e = s.logonSent_(connection, 1, ZuTime{104});
  ZuCheck(e.actions & A::RetryInput);
  e = s.peerLogon_(connection, header(), peer, 1, ZuTime{104});
  ZuCheck(e.state == State::Established && !(e.actions & (A::SendProbe | A::Ready)) &&
    !s.ready_() && !s.txDone_() && !s.probeID_().length());
  ZuCheck(!*s.deadline_(T::Logon) && s.deadline_(T::Receive) == ZuTime{140});
  auto before = e.change;
  e = s.recovery_(connection, true, false, ZuTime{105});
  ZuCheck(!s.ready_() && !(e.actions & A::Ready));
  e = recover(s);
  ZuCheck((e.actions & A::Ready) && s.ready_() && e.change > before);
  ZuCheck(!*s.deadline_(T::Probe) && !s.probeID_().length());
  ZuCheck(!s.admit_(connection, ticket, true, ZuTime{106}).actions);
}

static void acceptor()
{
  ZuTestScope(acceptor);
  LinkState s{config(false)};
  auto e = s.open_(ZuTime{100});
  auto connection = e.connection;
  ZuCheck(e.state == State::Accepting && !(e.actions & A::SendLogon));
  Session::Logon peer; peer.heartBtInt = 7;
  e = s.peerLogon_(connection, header(), peer, 1, ZuTime{101});
  ZuCheck((e.actions & A::Admission) && s.heartBtInt_() == 7 && e.heartBtInt == 7);
  auto ticket = e.ticket;
  e = s.admit_(connection, ticket, true, ZuTime{102});
  ZuCheck(e.state == State::Logon && (e.actions & A::SendLogon));
  s.logonSent_(connection, 1, ZuTime{103});
  e = s.peerLogon_(connection, header(), peer, 1, ZuTime{103});
  ZuCheck(e.state == State::Established && !(e.actions & (A::SendProbe | A::Ready)));
  ZuCheck(!*s.deadline_(T::Probe) &&
    s.deadline_(T::Receive) == ZuTime(111, 400000000));
  e = recover(s, ZuTime{104});
  ZuCheck(s.ready_() && (e.actions & A::Ready));
  ZuCheck(s.testRequest_(connection, ZuTime{105}).actions & A::Echo);

  LinkState denied{config(false)};
  e = denied.open_(ZuTime{100}); connection = e.connection;
  e = denied.peerLogon_(connection, header(), peer, 1, ZuTime{101});
  e = denied.admit_(connection, e.ticket, false, ZuTime{102});
  ZuCheck(e.error == E::Admission && e.state == State::Logout && (e.actions & A::SendLogout));
  ZuCheck(!denied.ready_() && !*denied.deadline_(T::Logon));
}

static void identities()
{
  ZuTestScope(identities);
  auto cfg = config(false);
  cfg.header.localSub = "LS"; cfg.header.peerSub = "PS";
  cfg.header.localLocation = "LL"; cfg.header.peerLocation = "PL";
  auto h = header();
  ZuCheck(cfg.header.mismatch(h) == 50);
  h.senderSub = "PS"; h.targetSub = "LS";
  ZuCheck(cfg.header.mismatch(h) == 142);
  h.senderLocation = "PL"; h.targetLocation = "LL";
  ZuCheck(!cfg.header.mismatch(h));
  h.target = "PEER";
  ZuCheck(cfg.header.mismatch(h) == 56);
  LinkState s{ZuMv(cfg)};
  auto e = s.open_(ZuTime{100});
  e = s.peerLogon_(e.connection, h, Session::Logon{}, 1, ZuTime{101});
  ZuCheck(e.error == E::Identity && e.tag == 56 && e.state == State::Closing);
  ZuCheck((e.actions & A::Refuse) && !(e.actions & (A::Admission | A::SendLogout)));
}

static void modes()
{
  ZuTestScope(modes);
  // Behavior mode and declared BeginString vary independently.
  for (unsigned mode = Transport::FIX4; mode <= Transport::FIXT11; ++mode) {
    for (unsigned version : {Version::V4_2, Version::V4_4, Version::VT1_1}) {
      LinkState s{config(true, mode, version)};
      auto e = establish(s);
      ZuCheck(e.state == State::Established && !(e.actions & (A::SendProbe | A::Ready)));
      ZuCheck((recover(s).actions & A::Ready) && s.ready_());
    }
  }
  LinkState ahead{config(true, Transport::FIX44A)};
  auto e = establish(ahead, 10, 5, 2, 8);
  ZuCheck(e.state == State::Established && (e.actions & A::Replay));
  ZuCheck(e.replayBegin == 2 && e.replayEnd == 8 && !(e.actions & A::RequestGap));
  ZuCheck(!(e.actions & A::SendProbe) && !ahead.ready_());
  e = ahead.recovery_(e.connection, true, false, ZuTime{105});
  ZuCheck(!(e.actions & A::SendProbe));
  e = ahead.recovery_(e.connection, true, true, ZuTime{106});
  ZuCheck((e.actions & A::Ready) && !(e.actions & A::SendProbe));
  ZuCheck(ahead.ready_());
  LinkState normal{config()};
  e = establish(normal, 10, 5);
  ZuCheck(e.actions & A::RequestGap);
  ZuCheck(e.gapBegin == 5 && e.gapEnd == 9 && !(e.actions & A::SendProbe));
}

static void invalidLogon()
{
  ZuTestScope(invalidLogon);
  auto invalid = [](unsigned mode, Fmt::Header h, Session::Logon peer) {
    LinkState s{config(true, mode)};
    auto e = start(s);
    e = s.peerLogon_(e.connection, h, peer, 1, ZuTime{103});
    if (e.actions & A::Admission) {
      s.admit_(e.connection, e.ticket, true, ZuTime{103});
      e = s.peerLogon_(e.connection, h, peer, 1, ZuTime{104});
    }
    return e;
  };
  Session::Logon peer;
  auto wrongType = header(); wrongType.msgType = "D";
  auto e = invalid(Transport::FIX4, wrongType, peer);
  ZuCheck(e.error == E::Type && (e.actions & A::Refuse) &&
    !(e.actions & (A::Admission | A::SendLogout)));
  e = invalid(Transport::FIX4, header(1, "FIX.4.2"), peer);
  ZuCheck(e.error == E::Version && e.tag == 8 && (e.actions & A::SendLogout));
  peer.heartBtInt = 10;
  e = invalid(Transport::FIX4, header(), peer);
  ZuCheck(e.error == E::Heartbeat && e.tag == 108);
  peer.heartBtInt = 30;
  e = invalid(Transport::FIX44A, header(), peer);
  ZuCheck(e.error == E::Sync && e.tag == 789);
  peer.nextExpected = 3;
  e = invalid(Transport::FIX44A, header(), peer);
  ZuCheck(e.error == E::Sync && e.tag == 789);
  e = invalid(Transport::FIXT11, header(), peer);
  ZuCheck(e.error == E::AppVersion && e.tag == 1137);
  peer.defaultAppVerID = "8";
  e = invalid(Transport::FIXT11, header(), peer);
  ZuCheck(e.error == E::AppVersion);
  peer.resetSeqNum = true;
  e = invalid(Transport::FIX4, header(), peer);
  ZuCheck(e.actions == A::NeedReset);
  LinkState low{config()};
  e = start(low);
  e = low.peerLogon_(e.connection, header(1), Session::Logon{}, 2, ZuTime{103});
  ZuCheck(e.error == E::Sequence && !(e.actions & A::Admission));
}

static void clocks()
{
  ZuTestScope(clocks);
  LinkState s{config()};
  establish(s); recover(s);
  auto connection = s.connection_();
  s.activity_(connection, ZuTime{120});
  auto e = s.timer_(connection, T::Receive, ZuTime{141});
  ZuCheck(e.actions == A::Timers && s.deadline_(T::Receive) == ZuTime{156});
  e = s.timer_(connection, T::Receive, ZuTime{156});
  ZuCheck(e.actions & A::SendProbe);
  ZuCheck(s.deadline_(T::Probe) == ZuTime{192});
  ZuCheck(!*s.deadline_(T::Receive));
  s.activity_(connection, ZuTime{170});
  ZuCheck(!*s.deadline_(T::Receive));
  e = s.heartbeat_(connection, "123", ZuTime{180});
  ZuCheck(!(e.actions & A::Ready) && s.deadline_(T::Probe) == ZuTime{192});
  e = s.timer_(connection, T::Probe, ZuTime{192});
  ZuCheck(e.error == E::Timeout && e.state == State::Closing && (e.actions & A::Close));
  ZuCheck(!s.ready_() && !*s.deadline_(T::Receive) && !*s.deadline_(T::Probe));

  LinkState logon{config()};
  e = logon.open_(ZuTime{100});
  e = logon.timer_(e.connection, T::Logon, ZuTime{110});
  ZuCheck(e.error == E::Timeout && e.state == State::Closing);
  LinkState admission{config(false)};
  e = admission.open_(ZuTime{100});
  auto pending = admission.peerLogon_(e.connection, header(), Session::Logon{}, 1, ZuTime{101});
  e = admission.timer_(e.connection, T::Logon, ZuTime{110});
  ZuCheck(e.error == E::Timeout && !admission.admissionPending_());
  ZuCheck(!admission.admit_(e.connection, pending.ticket, true, ZuTime{111}).actions);
}

static void recovery()
{
  ZuTestScope(recovery);
  LinkState s{config()};
  auto e = establish(s);
  auto connection = e.connection;
  ZuCheck(!s.ready_() && !s.txDone_() && !(e.actions & A::SendProbe));
  e = s.recovery_(connection, false, true, ZuTime{105});
  ZuCheck(!s.ready_() && !*s.deadline_(T::Probe) && *s.deadline_(T::Receive));
  e = s.recovery_(connection, true, true, ZuTime{107});
  ZuCheck((e.actions & A::Ready) && !(e.actions & A::SendProbe) && s.ready_());
  auto change = e.change;
  e = s.recovery_(connection, true, false, ZuTime{109});
  ZuCheck((e.actions & A::Pause) && !s.ready_() && e.change > change);
  e = s.probe_(connection, ZuTime{110});
  SessionString id{s.probeID_()};
  ZuCheck(e.actions & A::SendProbe);
  e = s.recovery_(connection, true, true, ZuTime{111});
  ZuCheck((e.actions & A::Ready) && s.ready_() && s.probeID_() == id);
  s.probeSent_(connection, s.probe_(), 7, ZuTime{112});
  e = s.heartbeat_(connection, id, ZuTime{113});
  ZuCheck((e.actions & A::Ackd) && e.acknowledged == 8 && s.ready_());
  e = s.peerLogoutEarly_(connection);
  ZuCheck((e.actions & A::Pause) && !s.ready_());
  ZuCheck(!(recover(s, ZuTime{114}).actions & A::Ready) && !s.ready_());
}

static void logoutReconnect()
{
  ZuTestScope(logoutReconnect);
  LinkState s{config(), 17};
  establish(s); recover(s);
  auto connection = s.connection_();
  auto e = s.logout_(connection, ZuTime{120});
  ZuCheck(e.state == State::Logout && (e.actions & A::SendLogout) && !(e.actions & A::Failure));
  ZuCheck(s.deadline_(T::Logout) == ZuTime{122} && !s.ready_());
  e = s.peerLogout_(connection, ZuTime{121});
  ZuCheck(e.state == State::Closing && !(e.actions & A::SendLogout)
    && (e.actions & A::CloseAfterTx));
  e = s.closed_(connection);
  ZuCheck(e.state == State::Disconnected && e.epoch == 17);
  ZuCheck(!*s.deadline_(T::Logout));
  e = s.open_(ZuTime{130});
  ZuCheck(e.state == State::Logon && e.connection != connection && e.epoch == 17);
  ZuCheck(!s.lost_(connection).actions && !s.timer_(connection, T::Logon, ZuTime{140}).actions);
  e = s.outputFailed_(e.connection);
  ZuCheck(e.state == State::Closing && e.error == E::Output && (e.actions & A::Close));

  LinkState peer{config()}; establish(peer); recover(peer);
  e = peer.peerLogout_(peer.connection_(), ZuTime{120});
  ZuCheck((e.actions & A::SendLogout) && (e.actions & A::CloseAfterTx));
  e = peer.timer_(peer.connection_(), T::Logout, ZuTime{122});
  ZuCheck(e.actions & A::Close);
}

static void zeroHeartbeat()
{
  ZuTestScope(zeroHeartbeat);
  auto cfg = config(); cfg.heartBtInt = 0;
  LinkState s{ZuMv(cfg)};
  auto e = start(s);
  Session::Logon peer; peer.heartBtInt = 0;
  e = s.peerLogon_(e.connection, header(), peer, 1, ZuTime{102});
  s.admit_(e.connection, e.ticket, true, ZuTime{103});
  e = s.peerLogon_(e.connection, header(), peer, 1, ZuTime{104});
  ZuCheck(!(e.actions & (A::SendProbe | A::Ready)) && !*s.deadline_(T::Probe));
  recover(s);
  ZuCheck(s.ready_() && !*s.deadline_(T::Receive) && !e.heartBtInt);
  e = s.probe_(e.connection, ZuTime{120});
  ZuCheck(e.actions & A::SendProbe);
  ZuCheck(s.deadline_(T::Probe) == ZuTime{130});
  SessionString id{s.probeID_()};
  s.probeSent_(1, s.probe_(), 2, ZuTime{121});
  e = s.heartbeat_(1, id, ZuTime{122});
  ZuCheck((e.actions & A::Ackd) && e.acknowledged == 3 && s.ready_() &&
    !*s.deadline_(T::Receive) && !*s.deadline_(T::Probe));
  e = s.drain_(1, ZuTime{123});
  e = s.drainTx_(1, e.drain, 4, ZuTime{124});
  ZuCheck((e.actions & A::SendProbe) && !*s.deadline_(T::Receive) &&
    s.deadline_(T::Probe) == ZuTime{134});
  id = s.probeID_(); s.probeSent_(1, s.probe_(), 4, ZuTime{125});
  e = s.heartbeat_(1, id, ZuTime{126});
  ZuCheck((e.actions & (A::Ackd | A::Drained)) == (A::Ackd | A::Drained) &&
    e.acknowledged == 5 && s.drained_() && !*s.deadline_(T::Receive) &&
    !*s.deadline_(T::Probe));
}

static void configBounds()
{
  ZuTestScope(configBounds);
  SessionConfig empty;
  ZuCheck(empty.header.version == Version::N &&
    empty.appVerID == AppVersion::N && !empty.valid());
  for (unsigned version = 0; version < Version::N; ++version) {
    auto cfg = config(true, Transport::FIX4, version);
    ZuCheck(cfg.valid() && unsigned(Version::Wire::s2v(Version::Wire::v2s(version))) == version);
  }
  ZuCheck(Version::Wire::s2v("FIX.4.0") == Version::V4_0 &&
    Version::Wire::s2v("FIX.4.1") == Version::V4_1 &&
    Version::Wire::s2v("FIX.4.2") == Version::V4_2 &&
    Version::Wire::s2v("FIX.4.3") == Version::V4_3 &&
    Version::Wire::s2v("FIX.4.4") == Version::V4_4 &&
    Version::Wire::s2v("FIXT.1.1") == Version::VT1_1);
  ZuCheck(unsigned(Version::Wire::s2v("FIX.4.5")) >= Version::N &&
    unsigned(Version::Wire::s2v("")) >= Version::N);
  for (unsigned version = 0; version < AppVersion::N; ++version) {
    auto cfg = config(true, Transport::FIXT11);
    cfg.appVerID = version;
    ZuCheck(cfg.valid() &&
      unsigned(AppVersion::Wire::s2v(AppVersion::Wire::v2s(version))) == version);
  }
  ZuCheck(AppVersion::Wire::v2s(AppVersion::V2_7) == "0" &&
    AppVersion::Wire::v2s(AppVersion::V5_0_SP2) == "9" &&
    AppVersion::Wire::v2s(AppVersion::Latest) == "10" &&
    unsigned(AppVersion::Wire::s2v("11")) >= AppVersion::N);
  LinkState bad{SessionConfig{}};
  auto e = bad.open_(ZuTime{100});
  ZuCheck(e.error == E::Config && (e.actions & A::Refuse) && !e.connection);
  auto cfg = config(); cfg.logonTimeout = ZuTime{0};
  ZuCheck(!cfg.valid());
  cfg = config(); cfg.receivePct = 0;
  ZuCheck(!cfg.valid());
  cfg = config(); cfg.sequenceLimit = UINT64_MAX;
  ZuCheck(!cfg.valid());
  cfg = config(true, Transport::FIXT11); cfg.appVerID = AppVersion::N;
  ZuCheck(!cfg.valid());
  cfg = config(); cfg.appVerID = AppVersion::N;
  ZuCheck(cfg.valid());
  cfg.appVerID = AppVersion::N + 1;
  ZuCheck(!cfg.valid());
  cfg = config(); cfg.header.version = Version::N;
  ZuCheck(!cfg.valid());
  cfg.header.version = Version::N + 1;
  ZuCheck(!cfg.valid());
  cfg = config(); cfg.sendingTolerance = ZuTime{0};
  ZuCheck(cfg.valid());
  cfg = config(); cfg.mode = Transport::N;
  ZuCheck(!cfg.valid());
  LinkState mode{cfg}; e = mode.open_(ZuTime{100});
  ZuCheck(e.error == E::Config && (e.actions & A::Refuse) && !e.connection);
  for (unsigned mode = Transport::FIX4; mode <= Transport::FIXT11; ++mode)
    for (bool driver : {false, true}) {
      LinkState epoch{config(driver, mode), 0}; e = epoch.open_(ZuTime{100});
      ZuCheck(e.error == E::Config && (e.actions & A::Refuse) && !e.connection
	&& !epoch.resetOnOpen_(0));
    }
  LinkState raw{config(false, Transport::Raw), 0};
  ZuCheck(raw.open_(ZuTime{100}).actions == A::Bypass);
  cfg = {}; cfg.mode = Transport::Raw; cfg.appVerID = AppVersion::N + 1;
  ZuCheck(cfg.valid());
}

static void raw()
{
  ZuTestScope(raw);
  SessionConfig cfg; cfg.mode = Transport::Raw;
  LinkState s{ZuMv(cfg)};
  ZuCheck(s.open_(ZuTime{100}).actions == A::Bypass);
  ZuCheck(s.peerLogon_(1, {}, {}, 0, ZuTime{100}).actions == A::Bypass);
  ZuCheck(s.admit_(1, 1, false, ZuTime{100}).actions == A::Bypass);
  ZuCheck(s.recovery_(1, false, false, ZuTime{100}).actions == A::Bypass);
  ZuCheck(s.heartbeat_(1, "0", ZuTime{100}).actions == A::Bypass);
  ZuCheck(s.testRequest_(1, ZuTime{100}).actions == A::Bypass);
  ZuCheck(s.probe_(1, ZuTime{100}).actions == A::Bypass);
  ZuCheck(s.drain_(1, ZuTime{100}).actions == A::Bypass);
  ZuCheck(s.drainTx_(1, 1, 1, ZuTime{100}).actions == A::Bypass);
  ZuCheck(s.resetPrepare_(1, 1, 2, true, ZuTime{100}).actions == A::Bypass);
  ZuCheck(s.resetReady_(1, 2, ZuTime{100}).actions == A::Bypass);
  ZuCheck(s.resetStart_(1, ZuTime{100}).actions == A::Bypass);
  ZuCheck(s.resetDisconnect_(1).actions == A::Bypass);
  ZuCheck(s.logout_(1, ZuTime{100}).actions == A::Bypass);
  ZuCheck(s.peerLogout_(1, ZuTime{100}).actions == A::Bypass);
  ZuCheck(s.lost_(1).actions == A::Bypass);
  ZuCheck(s.outputFailed_(1).actions == A::Bypass);
  ZuCheck(s.closed_(1).actions == A::Bypass);
  ZuCheck(s.timer_(1, T::Logon, ZuTime{100}).actions == A::Bypass);
  ZuCheck(s.state_() == State::Disconnected && !s.connection_() && !s.ready_());
}

static void probePublication()
{
  ZuTestScope(probePublication);
  LinkState s{config()}; establish(s); recover(s);
  auto event = s.probe_(s.connection_(), ZuTime{105});
  ZuCheck(event.actions & A::SendProbe);
  SessionString id{s.probeID_()};
  auto early = s.heartbeat_(s.connection_(), id, ZuTime{105});
  ZuCheck(!(early.actions & A::Ackd) && s.ready_() && s.probeID_() == id);
  auto deadline = s.deadline_(T::Probe);
  ZuCheck(!s.probeSent_(s.connection_(), s.probe_() + 1, 2, ZuTime{106}).actions);
  ZuCheck(s.deadline_(T::Probe) == deadline);
  ZuCheck(s.probeSent_(s.connection_(), s.probe_(), 2, ZuTime{106}).actions & A::Timers);
  auto published = s.deadline_(T::Probe);
  ZuCheck(published > deadline);
  ZuCheck(!s.probeSent_(s.connection_(), s.probe_(), 2, ZuTime{107}).actions &&
    s.deadline_(T::Probe) == published);
  event = s.heartbeat_(s.connection_(), id, ZuTime{108});
  ZuCheck((event.actions & A::Ackd) && event.acknowledged == 3 && s.ready_() &&
    !(event.actions & A::Ready));
}

static void recreatedProbe()
{
  ZuTestScope(recreatedProbe);
  SessionString stale;
  {
    LinkState old{config(), 17}; establish(old); recover(old);
    old.probe_(1, ZuTime{105}); stale = old.probeID_();
  }
  LinkState restored{config(), 17}; establish(restored); recover(restored);
  auto event = restored.probe_(1, ZuTime{105});
  ZuCheck(restored.epoch_() == 17 && event.connection == 1);
  ZuCheck(restored.probeID_() != stale);
  restored.probeSent_(1, restored.probe_(), 2, ZuTime{105});
  auto ignored = restored.heartbeat_(1, stale, ZuTime{106});
  ZuCheck(restored.ready_() && !(ignored.actions & A::Ackd));
  SessionString current{restored.probeID_()};
  auto ack = restored.heartbeat_(1, current, ZuTime{107});
  ZuCheck(restored.ready_() && (ack.actions & A::Ackd) && ack.acknowledged == 3);
}

static void probeWatermark()
{
  ZuTestScope(probeWatermark);
  LinkState s{config(), 17};
  establish(s); recover(s); s.probe_(s.connection_(), ZuTime{104});
  SessionString id{s.probeID_()};
  auto e = s.heartbeat_(s.connection_(), id, ZuTime{105});
  ZuCheck(!(e.actions & A::Ackd) && !e.acknowledged);
  ZuCheck(!s.probeSent_(s.connection_(), s.probe_(), 0, ZuTime{106}).actions);
  ZuCheck(!s.probeSent_(s.connection_(), s.probe_(), UINT64_MAX, ZuTime{106}).actions);
  ZuCheck(s.probeSent_(s.connection_(), s.probe_(), 7, ZuTime{106}).actions & A::Timers);
  e = s.heartbeat_(s.connection_(), "8", ZuTime{107});
  ZuCheck(!(e.actions & A::Ackd) && !e.acknowledged);
  e = s.heartbeat_(s.connection_(), id, ZuTime{108});
  ZuCheck((e.actions & A::Ackd) && e.acknowledged == 8 && e.epoch == 17);
  e = s.heartbeat_(s.connection_(), id, ZuTime{109});
  ZuCheck(!(e.actions & A::Ackd) && !e.acknowledged);

  s.probe_(s.connection_(), ZuTime{110});
  id = s.probeID_();
  s.probeSent_(s.connection_(), s.probe_(), 11, ZuTime{111});
  s.recovery_(s.connection_(), true, false, ZuTime{112});
  e = s.heartbeat_(s.connection_(), id, ZuTime{113});
  ZuCheck(!(e.actions & A::Ackd) && !e.acknowledged && !s.ready_());
  e = s.recovery_(s.connection_(), true, true, ZuTime{114});
  ZuCheck((e.actions & A::Ready) && !s.probeID_().length());
  s.probe_(s.connection_(), ZuTime{114});
  id = s.probeID_();
  s.probeSent_(s.connection_(), s.probe_(), 15, ZuTime{115});
  e = s.heartbeat_(s.connection_(), id, ZuTime{116});
  ZuCheck((e.actions & A::Ackd) && e.acknowledged == 16 && s.ready_());

  LinkState alt{config(true, Transport::FIX44A), 17};
  e = establish(alt, 1, 1, 5, 7);
  ZuCheck((e.actions & A::Ackd) && e.acknowledged == 5 &&
    (e.actions & A::Replay) && e.replayBegin == 5 && e.replayEnd == 7);
}

static void draining()
{
  ZuTestScope(draining);
  auto cfg = config();
  ZuCheck(cfg.drainTimeout == ZuTime{30});
  cfg.drainTimeout = ZuTime{10};
  LinkState s{cfg}; establish(s); recover(s);
  auto e = s.drain_(s.connection_(), ZuTime{110});
  auto ticket = e.drain;
  ZuCheck((e.actions & A::Drain) && (e.actions & A::Pause) && ticket &&
    s.draining_() && !s.drained_() && !s.ready_());
  ZuCheck(s.deadline_(T::Drain) == ZuTime{120});
  ZuCheck(!s.drain_(s.connection_(), ZuTime{111}).actions);
  ZuCheck(!s.drainTx_(s.connection_(), ticket + 1, 8, ZuTime{111}).actions);
  ZuCheck(!s.drainTx_(s.connection_(), ticket, 0, ZuTime{111}).actions);
  e = s.drainTx_(s.connection_(), ticket, 8, ZuTime{111});
  ZuCheck(e.actions & A::SendProbe);
  SessionString id{s.probeID_()};
  s.probeSent_(s.connection_(), s.probe_(), 2, ZuTime{111});
  e = s.heartbeat_(s.connection_(), id, ZuTime{112});
  ZuCheck(!(e.actions & A::Drained) && (e.actions & A::SendProbe) && !s.drained_());
  id = s.probeID_();
  s.probeSent_(s.connection_(), s.probe_(), 9, ZuTime{112});
  e = s.heartbeat_(s.connection_(), id, ZuTime{113});
  ZuCheck((e.actions & A::Drained) && (e.actions & A::Ackd) &&
    e.acknowledged == 10 && e.drain == ticket && s.drained_() && !s.ready_());
  ZuCheck(!*s.deadline_(T::Drain));
  ZuCheck(!(s.heartbeat_(s.connection_(), id, ZuTime{114}).actions & A::Drained));
  s.recovery_(s.connection_(), false, true, ZuTime{115});
  ZuCheck(!s.drained_() && s.deadline_(T::Drain) == ZuTime{125});
  e = s.recovery_(s.connection_(), true, true, ZuTime{116});
  ZuCheck(e.actions & A::SendProbe);
  id = s.probeID_();
  s.probeSent_(s.connection_(), s.probe_(), 12, ZuTime{117});
  e = s.heartbeat_(s.connection_(), id, ZuTime{118});
  ZuCheck((e.actions & A::Drained) && s.drained_() && !s.ready_());

  LinkState timeout{cfg}; establish(timeout); recover(timeout);
  timeout.drain_(timeout.connection_(), ZuTime{110});
  e = timeout.timer_(timeout.connection_(), T::Drain, ZuTime{120});
  ZuCheck(e.error == E::Timeout && e.state == State::Closing && !timeout.drained_());
  cfg.drainTimeout = ZuTime{0};
  ZuCheck(!cfg.valid());
}

static void freshDrainProbe()
{
  ZuTestScope(freshDrainProbe);
  LinkState s{config()}; establish(s); recover(s);
  auto e = s.drain_(1, ZuTime{110});
  auto ticket = e.drain;
  s.probe_(1, ZuTime{111});
  SessionString old{s.probeID_()}; auto marker = s.probe_();
  s.probeSent_(1, marker, 3, ZuTime{111});
  e = s.drainTx_(1, ticket, 8, ZuTime{112});
  ZuCheck((e.actions & A::SendProbe) && s.probeID_() != old && s.probe_() > marker);
  ZuCheck(!s.probeSent_(1, marker, 9, ZuTime{112}).actions);
  ZuCheck(!(s.heartbeat_(1, old, ZuTime{113}).actions & A::Drained));
  SessionString pending{s.probeID_()}; marker = s.probe_();
  s.probeSent_(1, marker, 9, ZuTime{113});
  s.recovery_(1, false, true, ZuTime{114});
  ZuCheck(!s.probeID_().length() && !s.drained_());
  ZuCheck(!(s.heartbeat_(1, pending, ZuTime{115}).actions & (A::Ackd | A::Drained)));
  e = recover(s, ZuTime{116});
  ZuCheck((e.actions & A::SendProbe) && s.probe_() > marker);
  SessionString current{s.probeID_()};
  s.probeSent_(1, s.probe_(), 11, ZuTime{117});
  e = s.heartbeat_(1, current, ZuTime{118});
  ZuCheck((e.actions & (A::Ackd | A::Drained)) == (A::Ackd | A::Drained) &&
    e.acknowledged == 12 && s.drained_() && !*s.deadline_(T::Drain));
  ZuCheck(!(recover(s, ZuTime{119}).actions & (A::Drained | A::SendProbe)));
  s.probe_(1, ZuTime{120});
  current = s.probeID_(); s.probeSent_(1, s.probe_(), 12, ZuTime{120});
  e = s.heartbeat_(1, current, ZuTime{121});
  ZuCheck((e.actions & A::Ackd) && !(e.actions & A::Drained) && s.drained_());

  LinkState pendingRx{config()}; establish(pendingRx); recover(pendingRx);
  e = pendingRx.drain_(1, ZuTime{110}); ticket = e.drain;
  pendingRx.recovery_(1, false, true, ZuTime{111});
  pendingRx.probe_(1, ZuTime{112});
  old = pendingRx.probeID_(); marker = pendingRx.probe_();
  e = pendingRx.drainTx_(1, ticket, 8, ZuTime{113});
  ZuCheck((e.actions & A::Timers) && !(e.actions & A::SendProbe) &&
    !pendingRx.probeID_().length() && !*pendingRx.deadline_(T::Probe) &&
    pendingRx.deadline_(T::Receive) == ZuTime{149});
  e = recover(pendingRx, ZuTime{114});
  ZuCheck((e.actions & A::SendProbe) && pendingRx.probeID_() != old &&
    pendingRx.probe_() > marker && !pendingRx.drained_());
}

static void resetPreparation()
{
  ZuTestScope(resetPreparation);
  auto drain = [](LinkState &session) {
    establish(session); recover(session);
    auto e = session.drain_(session.connection_(), ZuTime{110});
    session.drainTx_(session.connection_(), e.drain, 3, ZuTime{111});
    SessionString id{session.probeID_()};
    session.probeSent_(session.connection_(), session.probe_(), 3, ZuTime{112});
    return session.heartbeat_(session.connection_(), id, ZuTime{113});
  };
  LinkState session{config()};
  auto e = drain(session);
  auto ticket = e.drain;
  ZuCheck(!session.resetPrepare_(1, 1, 1, true, ZuTime{114}).actions);
  ZuCheck(!session.resetPrepare_(1, 1, 2, false, ZuTime{114}, ResetPath::Reconnect).actions);
  e = session.resetPrepare_(1, 1, 2, true, ZuTime{114});
  ZuCheck((e.actions & A::ResetPrepare) && e.epoch == 1 && e.nextEpoch == 2 &&
    session.resetPreparing_() && e.state == State::Resetting);
  ZuCheck(!session.resetMatches_(1, 1, 2, ticket + 1) && session.resetMatches_(1, 1, 2, ticket));
  ZuCheck(!session.resetReady_(1, 3, ZuTime{115}).actions && session.epoch_() == 1);
  e = session.resetReady_(1, 2, ZuTime{115});
  ZuCheck((e.actions & A::ResetReady) && e.epoch == 2 && session.resetArmed_());
  e = session.resetStart_(1, ZuTime{116});
  ZuCheck((e.actions & A::SendLogon) && e.state == State::Logon);
  session.logonSent_(1, 1, ZuTime{117});
  Session::Logon logon; logon.resetSeqNum = true;
  e = session.peerLogon_(1, header(), logon, 1, ZuTime{118});
  ZuCheck(e.state == State::Established && !(e.actions & A::Admission) && !session.resetArmed_());
  recover(session, ZuTime{119});
  ZuCheck(session.ready_() && session.epoch_() == 2 && session.connection_() == 1);

  LinkState timeout{config()}; drain(timeout);
  timeout.resetPrepare_(1, 1, 2, true, ZuTime{114});
  e = timeout.timer_(1, T::Logon, ZuTime{124});
  ZuCheck(e.state == State::Closing && e.error == E::Timeout && timeout.epoch_() == 1);

  LinkState bad{config()}; drain(bad);
  bad.resetPrepare_(1, 1, 2, true, ZuTime{114}); bad.resetReady_(1, 2, ZuTime{115});
  bad.resetStart_(1, ZuTime{116}); bad.logonSent_(1, 1, ZuTime{117});
  e = bad.peerLogon_(1, header(), Session::Logon{}, 1, ZuTime{118});
  ZuCheck(e.state == State::Logout && e.tag == 141 && !bad.ready_());
}

static void association()
{
  ZuTestScope(association);
  for (unsigned mode = Transport::FIX4; mode <= Transport::FIXT11; ++mode)
    for (bool initiator : {false, true}) {
      LinkState cold{config(initiator, mode)};
      auto initial = cold.bind_(false);
      ZuTime at;
      cold.nextTimer_(at);
      ZuCheck(initial.connection == 1 && initial.state == State::Disconnected &&
	!cold.ready_() && !*at && !(initial.actions & (A::SendLogon | A::Ready)));
      initial = cold.start_(ZuTime{1000});
      ZuCheck(initial.connection == 1 && cold.deadline_(T::Logon) == ZuTime{1010});
      LinkState s{config(initiator, mode)};
      ZuCheck(!s.start_(ZuTime{99}).actions && !s.connection_());
      establish(s); recover(s);
      s.probe_(1, ZuTime{106});
      auto marker = s.probe_();
      SessionString id{s.probeID_()};
      auto e = s.bind_(true);
      ZuCheck(e.connection == 2 && e.epoch == 1 && e.state == State::Disconnected &&
	(e.actions & (A::Pause | A::Timers)) == (A::Pause | A::Timers) &&
	!(e.actions & (A::Close | A::Failure | A::SendLogon | A::Ready)) &&
	!s.ready_() && !s.probeID_().length());
      ZuTime deadline;
      s.nextTimer_(deadline);
      ZuCheck(!*deadline && !s.timer_(1, T::Probe, ZuTime{1000}).actions &&
	!s.heartbeat_(1, id, ZuTime{1000}).actions &&
	!s.probeSent_(1, marker, 3, ZuTime{1000}).actions &&
	!s.outputFailed_(1).actions && !s.recovery_(1, true, true, ZuTime{1000}).actions);
      ZuCheck(!s.outputFailed_(2).actions && !s.lost_(2).actions &&
	!s.logonSent_(2, 3, ZuTime{1000}).actions);
      e = s.start_(ZuTime{1000});
      ZuCheck(e.connection == 2 && e.epoch == 1 &&
	e.state == (initiator ? State::Logon : State::Accepting) &&
	bool(e.actions & A::SendLogon) == initiator && !(e.actions & A::Ready) &&
	s.deadline_(T::Logon) == ZuTime{1010});
      ZuCheck(!s.start_(ZuTime{1001}).actions && s.connection_() == 2 &&
	s.deadline_(T::Logon) == ZuTime{1010});
      e = s.bind_(true);
      ZuCheck(e.connection == 3 && e.state == State::Disconnected);
      e = s.bind_(true);
      ZuCheck(e.connection == 4 && e.state == State::Disconnected &&
	!(e.actions & (A::Close | A::Failure | A::SendLogon)));
      e = s.start_(ZuTime{2000});
      ZuCheck(e.connection == 4 && s.deadline_(T::Logon) == ZuTime{2010});
    }
}

static void resetAssociation()
{
  ZuTestScope(resetAssociation);
  for (bool initiator : {false, true})
    for (unsigned stage = 0; stage < 3; ++stage) {
      LinkState s{config(initiator)};
      establish(s); recover(s);
      auto e = s.drain_(1, ZuTime{110});
      auto ticket = e.drain;
      s.drainTx_(1, ticket, 3, ZuTime{111});
      SessionString id{s.probeID_()};
      s.probeSent_(1, s.probe_(), 3, ZuTime{112});
      s.heartbeat_(1, id, ZuTime{113});
      ZuCheck(s.resetPrepare_(1, 1, 2, initiator, ZuTime{114}).actions & A::ResetPrepare);
      e = s.bind_(true);
      if (stage == 1) e = s.bind_(true); // C wins before B authenticates.
      auto connection = e.connection;
      ZuCheck(e.state == State::Disconnected && s.resetPreparing_() &&
	e.drain == ticket && !(e.actions & A::SendLogon) &&
	!s.resetAbort_(1, 3, ticket).actions &&
	!s.resetAbort_(connection + 1, 2, ticket).actions);
      e = s.resetReady_(1, 2, ZuTime{115}, stage != 2);
      ZuCheck(e.connection == connection && e.epoch == 2 &&
	e.state == State::Disconnected && !s.resetPreparing_() &&
	s.resetArmed_() == (stage != 2) && !s.ready_() &&
	(e.actions & A::ResetReady) &&
	!(e.actions & (A::SendLogon | A::RetryInput | A::Close)));
      ZuTime deadline;
      s.nextTimer_(deadline);
      ZuCheck(!*deadline && !s.resetReady_(1, 2, ZuTime{116}, true).actions);
      if (stage == 2) continue; // No current socket: retain closed-reset reconciliation.
      if (!stage) {
	e = s.bind_(true); // C replaces B after commit, before authentication.
	++connection;
	s.nextTimer_(deadline);
	ZuCheck(e.connection == connection && e.epoch == 2 && s.resetArmed_() &&
	  e.state == State::Disconnected && !*deadline && !(e.actions & A::SendLogon));
      }
      e = s.start_(ZuTime{200});
      ZuCheck(e.connection == connection && e.epoch == 2 && s.resetArmed_() &&
	bool(e.actions & A::SendLogon) == initiator &&
	s.deadline_(T::Logon) == ZuTime{210});
    }
}

static void resetReplacement()
{
  ZuTestScope(resetReplacement);
  for (bool initiator : {false, true}) {
    LinkState s{config(initiator)};
    establish(s); recover(s);
    auto e = s.drain_(1, ZuTime{110});
    auto ticket = e.drain;
    s.drainTx_(1, ticket, 3, ZuTime{111});
    SessionString id{s.probeID_()};
    s.probeSent_(1, s.probe_(), 3, ZuTime{112});
    s.heartbeat_(1, id, ZuTime{113});
    e = s.resetPrepare_(1, 1, 2, initiator, ZuTime{114});
    ZuCheck((e.actions & A::ResetPrepare) && s.resetPreparing_());
    e = s.replace_(ZuTime{115});
    ZuCheck(e.connection == 2 && e.epoch == 1 && e.nextEpoch == 2 &&
      e.drain == ticket && !(e.actions & A::SendLogon) && s.resetPreparing_());
    ZuCheck(s.resetMatches_(1, 1, 2, ticket) &&
      !s.resetMatches_(1, 1, 2, ticket + 1));
    e = s.resetReady_(1, 2, ZuTime{116});
    ZuCheck(e.connection == 2 && e.epoch == 2 && s.resetArmed_() &&
      !s.resetPreparing_() && !s.draining_() && (e.actions & A::RetryInput) &&
      bool(e.actions & A::SendLogon) == initiator);
    ZuCheck(!s.resetReady_(1, 2, ZuTime{117}).actions &&
      !s.closed_(1).actions && !s.ready_());
    e = s.replace_(ZuTime{118});
    ZuCheck(e.connection == 3 && e.epoch == 2 && s.resetArmed_());
  }
}

static void initialReset()
{
  ZuTestScope(initialReset);
  for (unsigned mode = Transport::FIX4; mode <= Transport::FIXT11; ++mode)
    for (bool driver : {false, true}) {
      LinkState s{config(driver, mode), 7};
      ZuCheck(!s.resetOnOpen_(6) && !s.resetOnOpen_());
      ZuCheck(s.resetOnOpen_(7) && s.resetOnOpen_() && s.resetOnOpen_(7, false) &&
	!s.resetOnOpen_() && s.resetOnOpen_(7));
      auto e = s.open_(ZuTime{100});
      ZuCheck(e.nextEpoch == 7 && s.resetArmed_() && !s.resetOnOpen_() && !s.resetOnOpen_(7));
      if (driver) s.logonSent_(1, 1, ZuTime{101});
      Session::Logon logon; logon.resetSeqNum = true; logon.nextExpected = 1;
      logon.defaultAppVerID = "9";
      e = s.peerLogon_(1, header(), logon, 1, ZuTime{102});
      ZuCheck((e.actions & A::Admission) && !s.ready_());
      s.admit_(1, e.ticket, true, ZuTime{103});
      if (!driver) s.logonSent_(1, 1, ZuTime{103});
      e = s.peerLogon_(1, header(), logon, 1, ZuTime{104});
      ZuCheck(e.state == State::Established && !s.resetArmed_() && s.epoch_() == 7);
      recover(s, ZuTime{105});
      ZuCheck(s.ready_());
      s.lost_(1); s.closed_(1);
      e = s.open_(ZuTime{106});
      ZuCheck(!e.nextEpoch && !s.resetArmed_() && s.epoch_() == 7 && e.connection == 2);
    }
  LinkState plain{config(false)};
  plain.open_(ZuTime{100});
  Session::Logon logon; logon.resetSeqNum = true;
  auto e = plain.peerLogon_(1, header(), logon, 1, ZuTime{101});
  ZuCheck((e.actions & A::NeedReset) && !plain.admissionPending_() && plain.epoch_() == 1);
  LinkState missing{config(false)};
  ZuCheck(missing.resetOnOpen_(1)); missing.open_(ZuTime{100});
  e = missing.peerLogon_(1, header(), Session::Logon{}, 1, ZuTime{101});
  ZuCheck(e.state == State::Logout && e.tag == 141 && !missing.admissionPending_());
  LinkState raw{config(false, Transport::Raw)};
  ZuCheck(!raw.resetOnOpen_(1) && !raw.resetOnOpen_());
}

// Initial connection, pending admission, admitted Session::Logon, established, Session::Logout.
static SessionEvent phase(LinkState &s, unsigned n)
{
  if (!n) return s.open_(ZuTime{100});
  if (n < 3) {
    auto e = start(s);
    Session::Logon peer; peer.nextExpected = 2; peer.defaultAppVerID = "9";
    e = s.peerLogon_(e.connection, header(1, Version::Wire::v2s(s.config_().header.version)),
      peer, 1, ZuTime{102});
    return n == 1 ? e : s.admit_(e.connection, e.ticket, true, ZuTime{103});
  }
  establish(s); auto e = recover(s);
  return n == 3 ? e : s.logout_(e.connection, ZuTime{120});
}
static bool cancelled(const LinkState &s)
{
  for (unsigned timer = T::Logon; timer <= T::Drain; ++timer)
    if (*s.deadline_(timer)) return false;
  return true;
}
static void failureMatrix()
{
  ZuTestScope(failureMatrix);
  for (unsigned mode = Transport::FIX4; mode <= Transport::FIXT11; ++mode)
    for (bool driver : {false, true})
      for (unsigned n = 0; n < 5; ++n)
	for (bool output : {false, true}) {
	  LinkState s{config(driver, mode, driver ? Version::V4_2 : Version::VT1_1), 17};
	  auto before = phase(s, n);
	  unsigned state = State::Logon;
	  switch (n) {
	    case 0: state = driver ? State::Logon : State::Accepting; break;
	    case 1: state = driver ? State::Logon : State::Accepting; break;
	    case 3: state = State::Established; break;
	    case 4: state = State::Logout; break;
	  }
	  ZuCheck(s.state_() == state && bool(s.admissionPending_()) == (n == 1));
	  auto e = output ? s.outputFailed_(1) : s.lost_(1);
	  enum { Actions = A::Close | A::Failure | A::Pause | A::Timers | A::Changed };
	  ZuCheck(e.state == State::Closing && e.error == (output ? E::Output : E::Transport) &&
	    (e.actions & Actions) == Actions && !(e.actions & A::SendLogout) &&
	    e.epoch == 17 && e.connection == 1 && e.change > before.change);
	  ZuCheck(!s.ready_() && !s.admissionPending_() && cancelled(s));
	  ZuCheck(!s.lost_(1).actions && !s.outputFailed_(1).actions &&
	    !s.timer_(1, T::Logon, ZuTime{200}).actions && !s.admit_(1, before.ticket, true,
	      ZuTime{200}).actions);
	  e = s.closed_(1);
	  ZuCheck(e.state == State::Disconnected && e.epoch == 17 && !s.closed_(1).actions
	    && cancelled(s));
	  e = s.open_(ZuTime{210});
	  ZuCheck(e.connection == 2 && e.epoch == 17 && !s.lost_(1).actions &&
	    !s.outputFailed_(1).actions &&
	    !s.timer_(1, T::Logon, ZuTime{250}).actions && s.state_() == e.state);
	}
}
static void timeoutMatrix()
{
  ZuTestScope(timeoutMatrix);
  for (unsigned mode = Transport::FIX4; mode <= Transport::FIXT11; ++mode)
    for (bool driver : {false, true})
      for (unsigned n = 0; n < 6; ++n) {
	LinkState s{config(driver, mode), 9};
	auto e = phase(s, n > 4 ? 3 : n);
	auto ticket = e.ticket;
	unsigned timer = T::Logon;
	switch (n) {
	  case 0: if (driver) timer = T::Logon; break;
	  case 3: {
	    auto deadline = s.deadline_(T::Receive);
	    e = s.timer_(1, T::Receive, deadline);
	    ZuCheck(e.state == State::Established && (e.actions & A::SendProbe) &&
	      !*s.deadline_(T::Receive) && *s.deadline_(T::Probe));
	    timer = T::Probe; break;
	  }
	  case 4: timer = T::Logout; break;
	  case 5: s.drain_(1, ZuTime{120}); timer = T::Drain; break;
	}
	auto deadline = s.deadline_(timer);
	ZuCheck(*deadline);
	auto state = s.state_();
	e = s.timer_(1, timer, deadline - ZuTime{1});
	ZuCheck(e.actions == A::Timers && s.state_() == state && s.deadline_(timer) == deadline);
	e = s.timer_(1, timer, deadline);
	ZuCheck(e.state == State::Closing && e.error == E::Timeout &&
	  (e.actions & (A::Close | A::Failure | A::Pause)) == (
	    A::Close | A::Failure | A::Pause) &&
	  e.epoch == 9 && cancelled(s) && !s.ready_() && !s.admissionPending_());
	ZuCheck(!s.timer_(1, timer, deadline).actions && !s.admit_(1, ticket, true,
	  deadline).actions);
	ZuCheck(s.closed_(1).state == State::Disconnected && s.epoch_() == 9);
      }
}
static void logonMatrix()
{
  ZuTestScope(logonMatrix);
  for (unsigned mode = Transport::FIX4; mode <= Transport::FIXT11; ++mode)
    for (bool driver : {false, true})
      for (unsigned version : {Version::V4_2, Version::V4_4, Version::VT1_1})
	for (uint64_t seqNo = 4; seqNo <= 6; ++seqNo) {
	  LinkState s{config(driver, mode, version), 9}; start(s, 7);
	  Session::Logon peer; peer.nextExpected = 8; peer.defaultAppVerID = "9";
	  if (!driver) peer.heartBtInt = 7;
	  auto h = header(seqNo, Version::Wire::v2s(version));
	  auto e = s.peerLogon_(1, h, peer, 5, ZuTime{103});
	  if (seqNo < 5) {
	    ZuCheck(e.state == State::Logout && e.error == E::Sequence && e.tag == 34 &&
	      (e.actions & A::SendLogout) && !(e.actions & A::Admission) &&
	      s.deadline_(T::Logout) == ZuTime{105});
	    continue;
	  }
	  ZuCheck((e.actions & A::Admission) && e.ticket && !s.ready_());
	  auto ticket = e.ticket;
	  ZuCheck(!s.admit_(1, ticket + 1, true, ZuTime{103}).actions &&
	    !s.peerLogon_(1, h, peer, 5, ZuTime{103}).actions);
	  e = s.admit_(1, ticket, true, ZuTime{104});
	  ZuCheck((e.actions & A::RetryInput) && bool(e.actions & A::SendLogon) == !driver);
	  if (!driver) s.logonSent_(1, 7, ZuTime{104});
	  e = s.peerLogon_(1, h, peer, 5, ZuTime{104});
	  ZuCheck(e.state == State::Established && (e.actions & A::Observe) && !s.ready_() &&
	    !*s.deadline_(T::Logon) && s.heartBtInt_() == peer.heartBtInt);
	  if (seqNo == 5) ZuCheck(!(e.actions & (A::SendProbe | A::RequestGap | A::Replay)) &&
	    *s.deadline_(T::Receive) && !*s.deadline_(T::Probe));
	  else {
	    ZuCheck(!(e.actions & A::SendProbe) && *s.deadline_(T::Receive) &&
	      !*s.deadline_(T::Probe)
		&& bool(e.actions & A::RequestGap) == (mode != Transport::FIX44A));
	    if (mode != Transport::FIX44A) ZuCheck(e.gapBegin == 5 && e.gapEnd == 5);
	    ZuCheck(!(s.recovery_(1, true, false, ZuTime{105}).actions & A::SendProbe));
	    e = s.recovery_(1, true, true, ZuTime{106});
	    ZuCheck((e.actions & A::Ready) && !(e.actions & A::SendProbe));
	  }
	  recover(s, ZuTime{106});
	  s.probe_(1, ZuTime{107});
	  SessionString id{s.probeID_()};
	  s.probeSent_(1, s.probe_(), 8, ZuTime{107});
	  e = s.heartbeat_(1, id, ZuTime{108});
	  ZuCheck(s.ready_() && (e.actions & A::Ackd) && e.acknowledged == 9 && e.epoch == 9);
	}
  for (bool driver : {false, true})
    for (uint64_t next : {uint64_t(0), uint64_t(1), uint64_t(7), uint64_t(8), uint64_t(9)}) {
      LinkState s{config(driver, Transport::FIX44A)}; start(s, 7);
      Session::Logon peer; peer.nextExpected = next;
      auto e = s.peerLogon_(1, header(5), peer, 5, ZuTime{103});
      if (e.actions & A::Admission) {
	s.admit_(1, e.ticket, true, ZuTime{104});
	if (!driver) s.logonSent_(1, 7, ZuTime{104});
	e = s.peerLogon_(1, header(5), peer, 5, ZuTime{104});
      }
      if (!next || next > 8) ZuCheck(e.state == State::Logout && e.error == E::Sync
	&& e.tag == 789 && !e.acknowledged);
      else {
	ZuCheck(e.state == State::Established && e.acknowledged == next &&
	  bool(e.actions & A::Replay) == (next < 7));
	if (next < 7) ZuCheck(e.replayBegin == 1 && e.replayEnd == 7
	  && !(e.actions & A::SendProbe));
	if (next == 7) {
	  ZuCheck(!s.ready_() && !s.txDone_() && !e.replayBegin && !e.replayEnd);
	  e = s.recovery_(1, true, false, ZuTime{105});
	  ZuCheck(!(e.actions & A::Ready));
	  e = recover(s, ZuTime{106});
	  ZuCheck((e.actions & A::Ready) && !(e.actions & A::SendProbe));
	}
      }
    }
}


static void admissionMatrix()
{
  ZuTestScope(admissionMatrix);
  for (unsigned mode = Transport::FIX4; mode <= Transport::FIXT11; ++mode)
    for (bool driver : {false, true}) {
      LinkState s{config(driver, mode), 17};
      auto e = phase(s, 1); auto ticket = e.ticket;
      ZuCheck(ticket && (e.actions & A::Admission));
      e = s.admit_(1, ticket, false, ZuTime{104});
      ZuCheck(e.state == State::Logout && e.error == E::Admission && e.epoch == 17 &&
	(e.actions & (A::SendLogout | A::Pause | A::Failure)) == (
	  A::SendLogout | A::Pause | A::Failure) &&
	!(e.actions & (A::Ready | A::Replay | A::SendProbe)) &&
	s.deadline_(T::Logout) == ZuTime{106} && !*s.deadline_(T::Logon)
	  && !s.admissionPending_());
      ZuCheck(!s.admit_(1, ticket, true, ZuTime{105}).actions && !s.ready_());
      LinkState reset{config(driver, mode), 17}; start(reset);
      Session::Logon peer; peer.resetSeqNum = true; peer.nextExpected = 2;
      peer.defaultAppVerID = "9";
      auto state = reset.state_(); auto deadline = reset.deadline_(T::Logon);
      e = reset.peerLogon_(1, header(), peer, 1, ZuTime{103});
      ZuCheck(e.actions == A::NeedReset && reset.state_() == state && reset.epoch_() == 17 &&
	reset.deadline_(T::Logon) == deadline && !reset.admissionPending_());
      if (mode == Transport::FIXT11) {
	LinkState missing{config(driver, mode)}; start(missing);
	e = missing.peerLogon_(1, header(), Session::Logon{}, 1, ZuTime{103});
	ZuCheck(e.state == State::Logout && e.error == E::AppVersion && e.tag == 1137 &&
	  (e.actions & A::SendLogout) && !missing.admissionPending_());
      }
    }
}


static void faultRouting()
{
  ZuTestScope(faultRouting);
  for (unsigned mode = Transport::FIX4; mode <= Transport::FIXT11; ++mode)
    for (bool driver : {false, true})
      for (unsigned error : {E::Sequence, E::Timestamp, E::Duplicate, E::Protocol, E::Overload,
	E::Limit})
	for (bool logout : {false, true}) {
	  LinkState s{config(driver, mode), 17}; auto before = phase(s, 3);
	  ZuCheck(!s.fault_(2, error, 34, ZuTime{120}, logout).actions && s.ready_());
	  auto e = s.fault_(1, error, 34, ZuTime{120}, logout);
	  ZuCheck(e.error == error && e.tag == 34 && e.epoch == 17 && e.connection == 1 &&
	    e.change > before.change
	      && (e.actions & (A::Failure | A::Pause | A::Changed | A::Timers)) ==
	      (A::Failure | A::Pause | A::Changed | A::Timers) && !s.ready_());
	  if (logout) {
	    ZuCheck(e.state == State::Logout && (e.actions & A::SendLogout)
	      && !(e.actions & A::Close) &&
	      s.deadline_(T::Logout) == ZuTime{122} && !*s.deadline_(T::Receive)
		&& !*s.deadline_(T::Probe));
	    s.timer_(1, T::Logout, ZuTime{122});
	  } else ZuCheck(e.state == State::Closing && (e.actions & A::Close) &&
	    !(e.actions & A::SendLogout) && cancelled(s));
	  ZuCheck(!s.fault_(1, error, 34, ZuTime{123}, logout).actions &&
	    s.closed_(1).state == State::Disconnected && s.epoch_() == 17);
	}
}

static void nextTimerActivity()
{
  ZuTestScope(nextTimerActivity);
  LinkState s{config()}; establish(s); recover(s);
  ZuTime at;
  auto kind = s.nextTimer_(at);
  ZuCheck(kind == T::Receive && at == ZuTime{140});
  s.activity_(1, ZuTime{110});
  auto e = s.timer_(1, kind, at); // retained physical wake after logical activity
  ZuCheck(e.actions == A::Timers && s.deadline_(T::Receive) == ZuTime{146});
  ZuCheck(s.nextTimer_(at) == T::Receive && at == ZuTime{146});
}

static void nextTimerTie()
{
  ZuTestScope(nextTimerTie);
  auto cfg = config();
  cfg.heartBtInt = 1; cfg.receivePct = 100; cfg.drainTimeout = ZuTime{1};
  LinkState s{cfg}; start(s);
  Session::Logon peer; peer.heartBtInt = 1;
  auto admission = s.peerLogon_(1, header(), peer, 1, ZuTime{102});
  s.admit_(1, admission.ticket, true, ZuTime{103});
  s.peerLogon_(1, header(), peer, 1, ZuTime{104});
  recover(s);
  s.drain_(1, ZuTime{105});
  ZuTime at;
  ZuCheck(s.nextTimer_(at) == T::Receive && at == ZuTime{106} &&
    s.deadline_(T::Drain) == at);
  auto e = s.timer_(1, T::Receive, at);
  ZuCheck((e.actions & A::SendProbe) && !(e.actions & A::Failure));
  ZuCheck(s.nextTimer_(at) == T::Drain && at == ZuTime{106});
  e = s.timer_(1, T::Drain, at);
  ZuCheck(e.state == State::Closing && e.error == E::Timeout);
}

static void nextTimerKind()
{
  ZuTestScope(nextTimerKind);
  LinkState s{config()}; establish(s); recover(s);
  ZuTime oldWake;
  ZuCheck(s.nextTimer_(oldWake) == T::Receive && oldWake == ZuTime{140});
  s.probe_(1, ZuTime{111});
  ZuTime at;
  auto kind = s.nextTimer_(at);
  ZuCheck(kind == T::Probe && at == ZuTime{147});
  auto e = s.timer_(1, kind, oldWake); // callback reselects the current kind
  ZuCheck(e.actions == A::Timers && s.deadline_(T::Probe) == at);
}

static void replacement()
{
  ZuTestScope(replacement);
  for (unsigned mode : {Transport::FIX4, Transport::FIX44A, Transport::FIXT11}) {
    auto version = mode == Transport::FIXT11 ? Version::VT1_1 : Version::V4_4;
    for (unsigned phase = 0; phase < 4; ++phase) {
      LinkState s{config(phase != 0, mode, version), 19};
      uint64_t oldTicket = 0;
      if (!phase) {
        auto e = start(s);
        Session::Logon logon; logon.defaultAppVerID = "9"; logon.nextExpected = 2;
        e = s.peerLogon_(e.connection, header(1, Version::Wire::v2s(version)),
          logon, 1, ZuTime{102});
        oldTicket = e.ticket;
        ZuCheck((e.actions & A::Admission) && oldTicket);
      } else {
        establish(s); recover(s);
        if (phase == 2) s.logout_(1, ZuTime{108});
        if (phase == 3) s.lost_(1);
      }
      auto instance = s.instance_();
      auto epoch = s.epoch_();
      auto e = s.replace_(ZuTime{110});
      ZuCheck(e.connection == 2 && e.epoch == epoch && e.instance == instance &&
        e.state == (phase ? State::Logon : State::Accepting));
      ZuCheck((e.actions & A::Pause) && !(e.actions &
        (A::Failure | A::Close | A::Ready | A::Drained)) && !s.ready_());
      ZuCheck(!s.admit_(1, oldTicket, true, ZuTime{111}).actions &&
        !s.timer_(1, T::Logon, ZuTime{1000}).actions && !s.closed_(1).actions &&
        !s.recovery_(1, true, true, ZuTime{111}).actions);
      e = s.replace_(ZuTime{112});
      ZuCheck(e.connection == 3 && e.epoch == epoch &&
        !(e.actions & (A::Failure | A::Close | A::Ready)));
    }
  }
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(initiator);
  ZuTestCall(acceptor);
  ZuTestCall(identities);
  ZuTestCall(modes);
  ZuTestCall(invalidLogon);
  ZuTestCall(clocks);
  ZuTestCall(recovery);
  ZuTestCall(logoutReconnect);
  ZuTestCall(zeroHeartbeat);
  ZuTestCall(raw);
  ZuTestCall(configBounds);
  ZuTestCall(probePublication);
  ZuTestCall(recreatedProbe);
  ZuTestCall(probeWatermark);
  ZuTestCall(draining);
  ZuTestCall(freshDrainProbe);
  ZuTestCall(resetPreparation);
  ZuTestCall(association);
  ZuTestCall(resetAssociation);
  ZuTestCall(resetReplacement);
  ZuTestCall(initialReset);
  ZuTestCall(failureMatrix);
  ZuTestCall(timeoutMatrix);
  ZuTestCall(logonMatrix);
  ZuTestCall(admissionMatrix);
  ZuTestCall(faultRouting);
  ZuTestCall(nextTimerActivity);
  ZuTestCall(nextTimerTie);
  ZuTestCall(nextTimerKind);
  ZuTestCall(replacement);
  return 0;
}
