//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZmRandom.hh>

#include <zlib/ZfixLinkState.hh>
#include <zlib/ZfixRxMsg.hh>

namespace Zfix {

ZtEnumImplNS(ResetPath);

int32_t PeerConfig::mismatch(const RxMsg &h) const
{
  if (h.sender != peer) return 49;
  if (h.target != local) return 56;
  if (h.senderSub != peerSub) return 50;
  if (h.targetSub != localSub) return 57;
  if (h.senderLocation != peerLocation) return 142;
  if (h.targetLocation != localLocation) return 143;
  return 0;
}

bool SessionConfig::valid() const
{
  if (mode >= Transport::N) return false;
  if (mode == Transport::Raw) return true;
  return identity.valid() && appVerID <= AppVersion::N &&
    sequenceLimit && sequenceLimit < UINT64_MAX &&
    receivePct && probePct &&
    *logonTimeout && logonTimeout > ZuTime{0} &&
    *logoutTimeout && logoutTimeout > ZuTime{0} &&
    *drainTimeout && drainTimeout > ZuTime{0} &&
    (mode != Transport::FIXT11 || appVerID < AppVersion::N);
}

static ZuTime interval(uint32_t seconds, uint32_t pct)
{
  uint64_t ticks = uint64_t(seconds)*pct;
  return ZuTime{int64_t(ticks/100), int32_t((ticks%100)*10000000)};
}

// An owner incarnation is independent of durable sequence epochs. It prevents
// a delayed echo from matching a probe after process/session recreation. This
// nonce identifies a liveness challenge; it is not an authentication token.
static uint128_t sessionInstance()
{
  uint128_t value = 0;
  for (unsigned i = 0; i < 4; ++i) value = (value<<32) | ZmRand::randInt();
  return value;
}

LinkState::LinkState(SessionConfig config, uint64_t epoch) :
  m_config{ZuMv(config)}, m_instance{sessionInstance()}, m_epoch{epoch},
  m_heartBtInt{m_config.heartBtInt} { }

bool LinkState::current(uint64_t connection) const
{
  return connection && connection == m_connection && m_state != State::Disconnected;
}
SessionEvent LinkState::event(unsigned actions, unsigned error, int32_t tag) const
{
  return {.actions = actions, .state = m_state, .error = error, .tag = tag,
    .connection = m_connection, .epoch = m_epoch, .change = m_change,
    .probe = m_probe, .drain = m_draining || resetPreparing_() || resetArmed_() ? m_drain : 0,
    .nextEpoch = m_nextEpoch, .resetInitiator = m_resetInitiator,
    .instance = m_instance, .heartBtInt = m_heartBtInt};
}
SessionEvent LinkState::transition(unsigned state, unsigned actions)
{
  if (m_state != state) { m_state = state; ++m_change; actions |= SessionAction::Changed; }
  return event(actions);
}
ZuTime LinkState::receiveInterval() const { return interval(m_heartBtInt, m_config.receivePct); }
ZuTime LinkState::probeInterval() const
{
  return m_heartBtInt ? interval(m_heartBtInt, m_config.probePct) : m_config.logonTimeout;
}
ZuTime LinkState::deadline_(unsigned timer) const
{
  switch (timer) {
    case SessionTimer::Logon: return m_deadlines.logon;
    case SessionTimer::Receive: return m_deadlines.receive;
    case SessionTimer::Probe: return m_deadlines.probe;
    case SessionTimer::Logout: return m_deadlines.logout;
    case SessionTimer::Drain: return m_deadlines.drain;
    default: return {};
  }
}
unsigned LinkState::nextTimer_(ZuTime &at) const
{
  at.null();
  unsigned kind = SessionTimer::Logon;
  for (unsigned i = SessionTimer::Logon; i <= SessionTimer::Drain; ++i) {
    auto deadline = deadline_(i);
    if (*deadline && (!*at || deadline < at)) { at = deadline; kind = i; }
  }
  return kind;
}
SessionFault LinkState::validateSession_(const RxMsg &h) const
{
  if (m_config.mode == Transport::Raw) return {};
  if (!h.seqNo || h.seqNo > m_config.sequenceLimit)
    return {SessionError::Sequence, 34, 5, true};
  if (h.frame.beginString != Version::Wire::v2s(m_config.identity.version))
    return {SessionError::Version, 8, 5, true};
  if (auto tag = m_config.identity.mismatch(h)) return {SessionError::Identity, tag, 9, true};
  if (h.appVerID.data() && m_config.appVerID < AppVersion::N &&
      h.appVerID != AppVersion::Wire::v2s(m_config.appVerID))
    return {SessionError::AppVersion, 1128, 5, true};
  return {};
}
SessionEvent LinkState::fault_(uint64_t connection, unsigned error, int32_t tag,
    ZuTime now, bool logout)
{
  if (m_config.mode == Transport::Raw) return event(SessionAction::Bypass);
  if (!current(connection) || m_state == State::Closing) return event();
  if (logout) return reject(error, tag, now);
  auto e = close(error); e.tag = tag;
  return e;
}

bool LinkState::resetOnOpen_(uint64_t epoch, bool enabled)
{
  if (m_config.mode == Transport::Raw || !m_config.valid() || !epoch || epoch != m_epoch ||
      m_connection || m_state != State::Disconnected || resetPreparing_()) return false;
  m_resetOnOpen = enabled;
  return true;
}
SessionEvent LinkState::open_(ZuTime now)
{
  return open(now, false);
}

SessionEvent LinkState::replace_(ZuTime now)
{
  return open(now, true);
}

SessionEvent LinkState::open(ZuTime now, bool replace)
{
  auto e = bind_(replace);
  if (e.actions & (SessionAction::Refuse | SessionAction::Bypass)) return e;
  return start_(now);
}

SessionEvent LinkState::bind_(bool replace)
{
  if (m_config.mode == Transport::Raw) return event(SessionAction::Bypass);
  if (!m_epoch || !m_config.valid()) return event(SessionAction::Refuse, SessionError::Config);
  if (!replace && (m_state != State::Disconnected || resetPreparing_()))
    return event(SessionAction::Refuse, SessionError::Busy);
  if (m_connection == UINT64_MAX) return event(SessionAction::Refuse, SessionError::Limit);
  bool preparing = replace && resetPreparing_();
  bool armed = replace && resetArmed_();
  ++m_connection;
  m_admission = m_txBoundary = 0;
  m_admitted = m_peerLogout = m_ready = false;
  m_rxDone = m_txDone = false;
  if (!preparing) {
    m_draining = m_drained = false; m_drainBoundary = 0;
    m_nextEpoch = m_resetOnOpen || armed ? m_epoch : 0;
    m_resetInitiator = m_config.initiator;
  }
  m_resetOnOpen = false;
  m_probeID.clear();
  m_heartBtInt = m_config.heartBtInt;
  m_deadlines = {};
  return transition(State::Disconnected, SessionAction::Timers | SessionAction::Pause);
}

SessionEvent LinkState::start_(ZuTime now)
{
  if (m_config.mode == Transport::Raw) return event(SessionAction::Bypass);
  if (!m_connection || m_state != State::Disconnected) return event();
  m_deadlines.logon = now + m_config.logonTimeout;
  return transition(m_config.initiator ? State::Logon : State::Accepting,
    SessionAction::Timers | SessionAction::Pause |
      (m_config.initiator && !resetPreparing_() ? SessionAction::SendLogon : 0));
}
SessionEvent LinkState::logonSent_(uint64_t connection, uint64_t seqNo, ZuTime)
{
  if (m_config.mode == Transport::Raw) return event(SessionAction::Bypass);
  if (!current(connection) || m_state != State::Logon || m_txBoundary) return event();
  if (!seqNo || seqNo > m_config.sequenceLimit || (resetArmed_() && seqNo != 1))
    return close(SessionError::Sequence);
  // This snapshot is after the Session::Logon assignment, before later new traffic.
  m_txBoundary = seqNo + 1;
  return event(m_admitted ? SessionAction::RetryInput : 0);
}
SessionEvent LinkState::reject(unsigned error, int32_t tag, ZuTime now)
{
  m_admission = 0;
  m_ready = false;
  m_probeID.clear();
  m_deadlines = {};
  m_deadlines.logout = now + m_config.logoutTimeout;
  auto e = transition(State::Logout, SessionAction::SendLogout | SessionAction::Pause |
    SessionAction::Timers | (error ? SessionAction::Failure : 0));
  e.error = error; e.tag = tag;
  return e;
}
SessionEvent LinkState::peerLogon_(uint64_t connection, const RxMsg &h,
    const Session::Logon &logon, uint64_t expected, ZuTime now)
{
  if (m_config.mode == Transport::Raw) return event(SessionAction::Bypass);
  if (!current(connection) || (m_state != State::Accepting && m_state != State::Logon))
    return event();
  if (h.frame.msgType != "A") {
    auto e = close(SessionError::Type);
    e.actions |= SessionAction::Refuse; e.tag = 35;
    return e;
  }
  if (h.frame.beginString != Version::Wire::v2s(m_config.identity.version))
    return reject(SessionError::Version, 8, now);
  if (auto tag = m_config.identity.mismatch(h)) {
    auto e = close(SessionError::Identity);
    e.actions |= SessionAction::Refuse; e.tag = tag;
    return e;
  }
  if (!h.seqNo || !expected || h.seqNo > m_config.sequenceLimit)
    return reject(SessionError::Sequence, 34, now);
  // Reset negotiation is a separate, drain-coordinated operation. Do not
  // consume the peer's reset proposal or silently reset continuing progress.
  if (logon.resetSeqNum && !resetArmed_()) return event(SessionAction::NeedReset);
  if (resetArmed_() && (!logon.resetSeqNum || h.seqNo != 1 || expected != 1 || h.possDup))
    return reject(SessionError::Sync, 141, now);
  if (h.seqNo < expected) return reject(SessionError::Sequence, 34, now);
  if ((resetArmed_() ? m_resetInitiator : m_config.initiator) &&
      logon.heartBtInt != m_config.heartBtInt)
    return reject(SessionError::Heartbeat, 108, now);
  if (m_config.mode == Transport::FIXT11 &&
      logon.defaultAppVerID != AppVersion::Wire::v2s(m_config.appVerID))
    return reject(SessionError::AppVersion, 1137, now);
  if (m_config.mode == Transport::FIX44A && !logon.nextExpected)
    return reject(SessionError::Sync, 789, now);
  if (resetArmed_() && m_admitted && !m_resetInitiator && !m_txBoundary &&
      m_state == State::Accepting)
    return transition(State::Logon, SessionAction::SendLogon | SessionAction::RetryInput);
  if (!m_admitted) {
    if (m_admission) return event();
    m_heartBtInt = logon.heartBtInt;
    m_admission = ++m_ticket;
    auto e = event(SessionAction::Admission);
    e.ticket = m_admission;
    return e;
  }
  if (!m_txBoundary) return event(); // retained input retries after logonSent_
  if (m_config.mode == Transport::FIX44A && logon.nextExpected > m_txBoundary)
    return reject(SessionError::Sync, 789, now);
  bool reset = resetArmed_();
  m_nextEpoch = 0;
  m_deadlines.logon.null();
  bool rxGap = h.seqNo > expected;
  // The peer's Logon snapshot can precede receipt of this connection's new
  // Logon. Infer catch-up only for historical assignments before that Logon;
  // explicit ResendReq still uses the complete admitted wire frontier.
  auto replayEnd = m_txBoundary - 1;
  bool txReplay = !reset && m_config.mode == Transport::FIX44A &&
    logon.nextExpected < replayEnd;
  // Logon describes wire progress, not completion on either owner. Rx confirms
  // after ordered disposition; Tx confirms its actual replay/retrieval state.
  m_rxDone = m_txDone = false;
  auto e = transition(State::Established, SessionAction::Timers | SessionAction::Observe);
  if (rxGap && m_config.mode != Transport::FIX44A) {
    e.actions |= SessionAction::RequestGap;
    e.gapBegin = expected; e.gapEnd = h.seqNo - 1;
  }
  if (m_config.mode == Transport::FIX44A) {
    e.actions |= SessionAction::Ackd;
    e.acknowledged = logon.nextExpected;
  }
  if (txReplay) {
    e.actions |= SessionAction::Replay;
    e.replayBegin = logon.nextExpected; e.replayEnd = replayEnd;
  }
  if (m_heartBtInt) m_deadlines.receive = now + receiveInterval();
  return e;
}
SessionEvent LinkState::admit_(uint64_t connection, uint64_t ticket, bool accepted, ZuTime now)
{
  if (m_config.mode == Transport::Raw) return event(SessionAction::Bypass);
  if (!current(connection) || !m_admission || ticket != m_admission ||
      (m_state != State::Accepting && m_state != State::Logon)) return event();
  m_admission = 0;
  if (!accepted) return reject(SessionError::Admission, 0, now);
  m_admitted = true;
  if (m_state == State::Accepting)
    return transition(State::Logon, SessionAction::SendLogon | SessionAction::RetryInput);
  return event(SessionAction::RetryInput);
}
SessionEvent LinkState::startProbe(ZuTime now)
{
  if (m_probeID.length()) return event();
  if (m_probe == UINT64_MAX) return close(SessionError::Limit);
  ++m_probe;
  m_probeBoundary = 0;
  m_probeID.clear();
  auto e = event(SessionAction::SendProbe | SessionAction::Timers);
  printProbe(m_probeID, e);
  m_deadlines.probe = now + probeInterval();
  m_deadlines.receive.null();
  return e;
}
SessionEvent LinkState::synchronize(ZuTime now)
{
  if (!m_rxDone || !m_txDone || m_peerLogout || m_state != State::Established) return event();
  if (m_draining) {
    if (!m_drainBoundary || m_drained) return event();
    return startProbe(now);
  }
  if (m_ready) return event();
  m_ready = true;
  ++m_change;
  return event(SessionAction::Ready | SessionAction::Changed);
}
SessionEvent LinkState::recovery_(uint64_t connection, bool rxDone, bool txDone, ZuTime now)
{
  if (m_config.mode == Transport::Raw) return event(SessionAction::Bypass);
  if (!current(connection) || m_state != State::Established) return event();
  unsigned actions = 0;
  if (m_rxDone != rxDone || m_txDone != txDone) {
    actions |= SessionAction::Recovery | SessionAction::Changed;
    ++m_change;
  }
  m_rxDone = rxDone; m_txDone = txDone;
  if (!rxDone || !txDone) {
    if (m_draining && m_drained) {
      m_deadlines.drain = now + m_config.drainTimeout;
      actions |= SessionAction::Timers;
    }
    m_drained = false;
    if (m_ready) {
      m_ready = false; actions |= SessionAction::Pause | SessionAction::Changed;
    }
    if (m_probeID.length() && m_draining) {
      m_probeID.clear(); m_deadlines.probe.null();
      if (m_heartBtInt) m_deadlines.receive = now + receiveInterval();
      actions |= SessionAction::Timers;
    }
  } else actions |= synchronize(now).actions;
  return event(actions);
}
void LinkState::activity_(uint64_t connection, ZuTime now)
{
  if (current(connection) && m_state == State::Established &&
      m_heartBtInt && !m_probeID.length()) {
    auto next = now + receiveInterval();
    if (!*m_deadlines.receive || next > m_deadlines.receive) m_deadlines.receive = next;
  }
}
SessionEvent LinkState::probeSent_(uint64_t connection, uint64_t marker,
    uint64_t seqNo, ZuTime now)
{
  if (m_config.mode == Transport::Raw) return event(SessionAction::Bypass);
  if (!current(connection) || m_state != State::Established || !m_probeID.length() ||
      m_probeBoundary || marker != m_probe || !seqNo || seqNo > m_config.sequenceLimit)
    return event();
  m_probeBoundary = seqNo + 1;
  m_deadlines.probe = now + probeInterval();
  return event(SessionAction::Timers);
}
SessionEvent LinkState::drain_(uint64_t connection, ZuTime now)
{
  if (m_config.mode == Transport::Raw) return event(SessionAction::Bypass);
  if (!current(connection) || m_state != State::Established || m_draining) return event();
  if (m_drain == UINT64_MAX) return close(SessionError::Limit);
  ++m_drain; m_draining = true; m_drained = false;
  m_deadlines.drain = now + m_config.drainTimeout;
  m_drainBoundary = 0; m_ready = false;
  m_probeID.clear(); m_deadlines.probe.null();
  if (m_heartBtInt) m_deadlines.receive = now + receiveInterval();
  ++m_change;
  return event(SessionAction::Drain | SessionAction::Pause |
    SessionAction::Changed | SessionAction::Timers);
}
SessionEvent LinkState::drainTx_(uint64_t connection, uint64_t ticket,
    uint64_t boundary, ZuTime now)
{
  if (m_config.mode == Transport::Raw) return event(SessionAction::Bypass);
  if (!current(connection) || m_state != State::Established || !m_draining ||
      ticket != m_drain || !boundary || boundary - 1 > m_config.sequenceLimit ||
      m_drainBoundary) return event();
  m_drainBoundary = boundary;
  m_probeID.clear(); m_deadlines.probe.null();
  if (m_heartBtInt) m_deadlines.receive = now + receiveInterval();
  auto e = synchronize(now);
  e.actions |= SessionAction::Timers;
  return e;
}
SessionEvent LinkState::resetPrepare_(uint64_t connection, uint64_t epoch,
    uint64_t nextEpoch, bool initiator, ZuTime now, unsigned path)
{
  if (m_config.mode == Transport::Raw) return event(SessionAction::Bypass);
  if (!current(connection) || epoch != m_epoch || nextEpoch <= epoch || !drained_() ||
      path > ResetPath::Reconnect ||
      (path == ResetPath::Reconnect && initiator != m_config.initiator))
    return event();
  m_nextEpoch = nextEpoch;
  m_resetInitiator = initiator;
  m_resetPath = path;
  m_ready = false;
  m_probeID.clear();
  m_deadlines = {}; m_deadlines.logon = now + m_config.logonTimeout;
  return transition(State::Resetting,
    SessionAction::ResetPrepare | SessionAction::Pause | SessionAction::Timers);
}
bool LinkState::resetMatches_(uint64_t connection, uint64_t epoch,
    uint64_t nextEpoch, uint64_t drain) const
{
  return connection && connection <= m_connection && resetPreparing_() &&
    epoch == m_epoch && nextEpoch == m_nextEpoch && drain == m_drain;
}
SessionEvent LinkState::resetReady_(uint64_t connection, uint64_t nextEpoch,
    ZuTime now, bool associated)
{
  if (m_config.mode == Transport::Raw) return event(SessionAction::Bypass);
  if (!connection || connection > m_connection || !resetPreparing_() ||
      nextEpoch != m_nextEpoch) return event();
  bool started = m_state == State::Logon || m_state == State::Accepting;
  bool pending = associated && m_state == State::Disconnected;
  bool replaced = started || pending;
  bool active = m_state == State::Resetting;
  m_epoch = nextEpoch;
  if (!active && !replaced) m_nextEpoch = 0;
  m_admitted = !replaced; m_admission = m_txBoundary = 0;
  m_peerLogout = m_ready = false;
  m_rxDone = m_txDone = false;
  m_draining = m_drained = false; m_drainBoundary = 0;
  m_heartBtInt = m_config.heartBtInt;
  m_deadlines = {};
  if (active || started) m_deadlines.logon = now + m_config.logonTimeout;
  ++m_change;
  auto e = event(SessionAction::ResetReady |
    (started ? SessionAction::RetryInput |
      (m_config.initiator ? SessionAction::SendLogon : 0) : 0) |
    SessionAction::Changed | SessionAction::Timers);
  // A logical outcome is acknowledged even after physical close. Preserve its
  // target in the event; the session's nextEpoch still describes future Logon.
  e.nextEpoch = nextEpoch;
  e.drain = m_drain;
  return e;
}
SessionEvent LinkState::resetStart_(uint64_t connection, ZuTime now)
{
  if (m_config.mode == Transport::Raw) return event(SessionAction::Bypass);
  if (!current(connection) || m_state != State::Resetting ||
      resetPreparing_() || !resetArmed_() ||
      m_resetPath != ResetPath::Live)
    return event();

  m_deadlines.logon = now + m_config.logonTimeout;
  return transition(m_resetInitiator ? State::Logon : State::Accepting,
    SessionAction::RetryInput | SessionAction::Timers |
      (m_resetInitiator ? SessionAction::SendLogon : 0));
}
SessionEvent LinkState::resetDisconnect_(uint64_t connection)
{
  if (m_config.mode == Transport::Raw) return event(SessionAction::Bypass);
  if (!current(connection) || m_state != State::Resetting ||
      resetPreparing_() || !resetArmed_() ||
      m_resetPath != ResetPath::Reconnect) return event();
  m_resetOnOpen = true;
  return close(SessionError::None);
}
SessionEvent LinkState::resetAbort_(uint64_t connection, uint64_t nextEpoch, uint64_t drain)
{
  if (!connection || connection > m_connection || !resetPreparing_() ||
      nextEpoch != m_nextEpoch || drain != m_drain ||
      (m_state != State::Closing && m_state != State::Disconnected &&
       m_state != State::Logon && m_state != State::Accepting)) return event();
  bool started = m_state == State::Logon || m_state == State::Accepting;
  m_nextEpoch = 0;
  m_draining = m_drained = false; m_drainBoundary = 0;
  ++m_change;
  auto e = event(SessionAction::ResetAbort | SessionAction::Changed |
    (started ? SessionAction::RetryInput |
      (m_config.initiator ? SessionAction::SendLogon : 0) : 0));
  e.nextEpoch = nextEpoch;
  e.drain = drain;
  return e;
}
SessionEvent LinkState::flushed_(uint64_t connection)
{
  if (m_config.mode == Transport::Raw) return event(SessionAction::Bypass);
  if (!current(connection) || m_state != State::Closing) return event();
  return event(SessionAction::Close);
}
SessionEvent LinkState::heartbeat_(uint64_t connection, ZuCSpan id, ZuTime now)
{
  if (m_config.mode == Transport::Raw) return event(SessionAction::Bypass);
  if (!current(connection) || (m_state != State::Established && m_state != State::Logout))
    return event();
  activity_(connection, now);
  unsigned actions = SessionAction::Observe;
  uint64_t acknowledged = 0;
  if (m_probeID.length() && m_probeBoundary && id == m_probeID) {
    m_probeID.clear(); m_deadlines.probe.null();
    activity_(connection, now);
    actions |= SessionAction::Timers;
    // The exact published challenge bounds peer session progress. An early
    // Session::HeartBt or unresolved replay/gap is liveness evidence only.
    if (m_rxDone && m_txDone) {
      actions |= SessionAction::Ackd;
      acknowledged = m_probeBoundary;
    }
    if (m_state == State::Established && !m_peerLogout && m_draining &&
	!m_drained && m_drainBoundary && m_rxDone && m_txDone &&
	m_probeBoundary >= m_drainBoundary) {
      m_drained = true;
      m_deadlines.drain.null();
      actions |= SessionAction::Drained;
    }
    actions |= synchronize(now).actions;
  }
  auto e = event(actions);
  e.acknowledged = acknowledged;
  return e;
}
SessionEvent LinkState::testRequest_(uint64_t connection, ZuTime now)
{
  if (m_config.mode == Transport::Raw) return event(SessionAction::Bypass);
  if (!current(connection) || (m_state != State::Established && m_state != State::Logout))
    return event();
  activity_(connection, now);
  // Echo borrows the enclosing retained input, never a numeric interpretation.
  return event(SessionAction::Observe | SessionAction::Echo);
}
SessionEvent LinkState::probe_(uint64_t connection, ZuTime now)
{
  if (m_config.mode == Transport::Raw) return event(SessionAction::Bypass);
  if (!current(connection) || m_state != State::Established) return event();
  return startProbe(now);
}
SessionEvent LinkState::logout_(uint64_t connection, ZuTime now)
{
  if (m_config.mode == Transport::Raw) return event(SessionAction::Bypass);
  if (!current(connection) || m_state == State::Logout || m_state == State::Closing)
    return event();
  if (m_state == State::Resetting) return close(SessionError::None);
  return reject(SessionError::None, 0, now);
}
SessionEvent LinkState::peerLogoutEarly_(uint64_t connection)
{
  if (m_config.mode == Transport::Raw) return event(SessionAction::Bypass);
  if (!current(connection) || m_state != State::Established || m_peerLogout) return event();
  m_peerLogout = true;
  unsigned actions = SessionAction::Observe | SessionAction::SendLogout;
  if (m_ready) {
    m_ready = false; ++m_change;
    actions |= SessionAction::Pause | SessionAction::Changed;
  }
  // The responder acknowledges a high Session::Logout but continues requested recovery;
  // the requestor closes the connection. Ordered disposition can close later.
  return event(actions);
}
SessionEvent LinkState::peerLogout_(uint64_t connection, ZuTime now)
{
  if (m_config.mode == Transport::Raw) return event(SessionAction::Bypass);
  if (!current(connection) || m_state == State::Closing) return event();
  unsigned actions = SessionAction::Observe | SessionAction::Pause |
    SessionAction::Timers | SessionAction::CloseAfterTx;
  if (m_state != State::Logout && !m_peerLogout) actions |= SessionAction::SendLogout;
  m_admission = 0; m_ready = false;
  m_probeID.clear(); m_deadlines = {};
  m_deadlines.logout = now + m_config.logoutTimeout;
  return transition(State::Closing, actions);
}
SessionEvent LinkState::close(unsigned error)
{
  m_admission = 0; m_ready = false;
  m_probeID.clear(); m_deadlines = {};
  auto e = transition(State::Closing, SessionAction::Pause | SessionAction::Timers |
    SessionAction::Close | (error ? SessionAction::Failure : 0));
  e.error = error;
  return e;
}
SessionEvent LinkState::lost_(uint64_t connection)
{
  if (m_config.mode == Transport::Raw) return event(SessionAction::Bypass);
  if (!current(connection) || m_state == State::Closing) return event();
  return close(SessionError::Transport);
}
SessionEvent LinkState::outputFailed_(uint64_t connection)
{
  if (m_config.mode == Transport::Raw) return event(SessionAction::Bypass);
  if (!current(connection) || m_state == State::Closing) return event();
  return close(SessionError::Output);
}
SessionEvent LinkState::closed_(uint64_t connection)
{
  if (m_config.mode == Transport::Raw) return event(SessionAction::Bypass);
  if (!current(connection) || m_state != State::Closing) return event();
  m_deadlines = {};
  return transition(State::Disconnected, SessionAction::Timers);
}
SessionEvent LinkState::timer_(uint64_t connection, unsigned timer, ZuTime now)
{
  if (m_config.mode == Transport::Raw) return event(SessionAction::Bypass);
  if (!current(connection)) return event();
  auto deadline = deadline_(timer);
  if (!*deadline) return event();
  if (now < deadline) return event(SessionAction::Timers);
  switch (timer) {
    case SessionTimer::Logon: return close(SessionError::Timeout);
    case SessionTimer::Receive:
      m_deadlines.receive.null();
      if (m_probeID.length()) return event();
      return startProbe(now);
    case SessionTimer::Probe: return close(SessionError::Timeout);
    case SessionTimer::Logout:
    case SessionTimer::Drain: return close(SessionError::Timeout);
    default: return event();
  }
}
} // Zfix

