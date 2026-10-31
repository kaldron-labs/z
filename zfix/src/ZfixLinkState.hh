//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Rx-owned FIX lifecycle, admission and directional recovery

#ifndef ZfixLinkState_HH
#define ZfixLinkState_HH

#ifndef ZfixLib_HH
#include <zlib/ZfixLib.hh>
#endif

#include <zlib/ZuTime.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZtEnum.hh>

#include <zlib/Zfix.hh>
#include <zlib/ZfixFrame.hh>
#include <zlib/ZfixSession.hh>

namespace Zfix {

ZtEnumNS(ZfixAPI, ResetPath, int8_t, Live, Reconnect);

using SessionString = ZtString<ZtStringHeapID<"Zfix.LinkState">>;
// Wire identities belong to the link configuration.
struct HeaderConfig {
  unsigned	version = Version::N;
  SessionString	local;
  SessionString	peer;
  SessionString	localSub;
  SessionString	peerSub;
  SessionString	localLocation;
  SessionString	peerLocation;

  bool valid() const { return version < Version::N && local.length() && peer.length(); }

  ZfixAPI unsigned mismatch(const Fmt::Header &) const;
};

struct SessionConfig {
  HeaderConfig	header;
  unsigned	appVerID = AppVersion::N;
  unsigned	mode = Transport::FIX4;
  bool		initiator = true;
  uint32_t	heartBtInt = 30;
  uint32_t	receivePct = 120;
  uint32_t	probePct = 120;
  uint64_t	sequenceLimit = 2147483647;
  ZuTime	logonTimeout{10};
  ZuTime	logoutTimeout{2};
  ZuTime	drainTimeout{30};
  ZuTime	sendingTolerance{120};

  ZfixAPI bool valid() const;
};

namespace SessionAction {

  enum {
    Changed = 1U<<0, Timers = 1U<<1, SendLogon = 1U<<3,
    Admission = 1U<<4, RetryInput = 1U<<5, SendProbe = 1U<<6,
    Ready = 1U<<7, Pause = 1U<<8, SendHeartbeat = 1U<<9,
    Echo = 1U<<10, SendLogout = 1U<<11, Close = 1U<<12,
    CloseAfterTx = 1U<<13, Refuse = 1U<<14, Failure = 1U<<15,
    Replay = 1U<<16, RequestGap = 1U<<17, Observe = 1U<<18,
    Bypass = 1U<<19, NeedReset = 1U<<20, Recovery = 1U<<21, Ackd = 1U<<22,
    Drain = 1U<<23, Drained = 1U<<24, ResetPrepare = 1U<<25,
    ResetReady = 1U<<26, ResetAbort = 1U<<27
  };
}

namespace SessionError {

  enum { None, Config, Busy, Identity, Version, Type, Sequence, Heartbeat,
    AppVersion, Admission, Sync, Timeout, Transport, Output, Limit,
    Timestamp, Duplicate, Protocol, Overload };
}

struct SessionFault {
  unsigned	error = 0;
  unsigned	tag = 0;
  int		reason = 99;
  bool		fatal = false;

  explicit operator bool() const { return error; }
};

namespace SessionTimer {

  enum { Logon, Receive, Probe, Logout, Drain };
}

struct SessionEvent {
  unsigned	actions = 0;
  unsigned	state = State::Disconnected;
  unsigned	error = SessionError::None;
  unsigned	tag = 0;
  uint64_t	connection = 0;
  uint64_t	epoch = 0;
  uint64_t	change = 0;
  uint64_t	ticket = 0; // Admission reply or ResetReady/ResetAbort request ack
  uint64_t	replayBegin = 0;
  uint64_t	replayEnd = 0;
  uint64_t	gapBegin = 0;
  uint64_t	gapEnd = 0;
  uint64_t	probe = 0;
  uint64_t	acknowledged = 0;
  uint64_t	drain = 0;
  uint64_t	nextEpoch = 0;
  bool		resetInitiator = false;
  uint128_t	instance = 0;
  uint32_t	heartBtInt = 0;
};

struct SessionDeadlines {
  ZuTime	logon;
  ZuTime	receive;
  ZuTime	probe;
  ZuTime	logout;
  ZuTime	drain;
};

// Identity and epoch outlive each connection. This object belongs entirely to
// Rx. Events carry only fixed metadata: the enclosing receive item retains any
// variable input once. Endpoint adapters post events to Tx/application owners,
// rather than reading their live state here. Every clock is supplied by caller.
class ZfixAPI LinkState {
public:
  explicit LinkState(SessionConfig config, uint64_t epoch = 1);
  LinkState(const LinkState &) = delete;
  LinkState &operator =(const LinkState &) = delete;

  const SessionConfig &config_() const { return m_config; }
  uint128_t instance_() const { return m_instance; }
  unsigned state_() const { return m_state; }
  uint64_t connection_() const { return m_connection; }
  uint64_t epoch_() const { return m_epoch; }
  uint64_t probe_() const { return m_probe; }
  uint32_t heartBtInt_() const { return m_heartBtInt; }
  bool ready_() const { return m_ready; }
  bool txDone_() const { return m_txDone; }
  bool resetArmed_() const { return m_nextEpoch && m_nextEpoch == m_epoch; }
  bool resetPreparing_() const { return m_nextEpoch > m_epoch; }
  bool draining_() const { return m_draining; }
  uint64_t drainBoundary_() const { return m_drainBoundary; }
  bool drained_() const { return m_state == State::Established && m_draining && m_drained; }
  bool admissionPending_() const { return m_admission; }
  const SessionString &probeID_() const { return m_probeID; }

  ZuTime deadline_(unsigned timer) const;
  unsigned nextTimer_(ZuTime &at) const;

  SessionFault validateHeader_(const Fmt::Header &, const ZuDateTime &utc) const;
  SessionEvent fault_(uint64_t connection, unsigned error, unsigned tag,
    ZuTime now, bool logout);

  // Explicit initial reset agreement; only on a never-opened session owner.
  bool resetOnOpen_() const { return m_resetOnOpen; }

  bool resetOnOpen_(uint64_t epoch, bool enabled = true);
  // Binding revokes predecessor authority without starting FIX or its timers.
  SessionEvent bind_(bool replace);
  // Start only after the current native association has authenticated.
  SessionEvent start_(ZuTime now);
  SessionEvent open_(ZuTime now);
  SessionEvent replace_(ZuTime now);
  SessionEvent logonSent_(uint64_t connection, uint64_t seqNo, ZuTime now);
  SessionEvent peerLogon_(uint64_t connection, const Fmt::Header &,
    const Session::Logon &, uint64_t expected, ZuTime now);
  SessionEvent admit_(uint64_t connection, uint64_t ticket, bool accepted, ZuTime now);
  SessionEvent recovery_(uint64_t connection, bool rxDone, bool txDone, ZuTime now);
  SessionEvent probeSent_(uint64_t connection, uint64_t marker, uint64_t seqNo, ZuTime now);
  SessionEvent flushed_(uint64_t connection);
  SessionEvent resetPrepare_(uint64_t connection, uint64_t epoch,
    uint64_t nextEpoch, bool initiator, ZuTime now, unsigned path = ResetPath::Live);
  bool resetMatches_(uint64_t connection, uint64_t epoch,
    uint64_t nextEpoch, uint64_t drain) const;
  SessionEvent resetReady_(uint64_t connection, uint64_t nextEpoch,
    ZuTime now, bool associated = false);
  SessionEvent resetStart_(uint64_t connection, ZuTime now);
  SessionEvent resetDisconnect_(uint64_t connection);
  SessionEvent resetAbort_(uint64_t connection, uint64_t nextEpoch, uint64_t drain);
  SessionEvent drain_(uint64_t connection, ZuTime now);
  SessionEvent drainTx_(uint64_t connection, uint64_t ticket, uint64_t boundary, ZuTime now);
  SessionEvent heartbeat_(uint64_t connection, ZuCSpan id, ZuTime now);
  SessionEvent testRequest_(uint64_t connection, ZuTime now);
  SessionEvent probe_(uint64_t connection, ZuTime now);
  void activity_(uint64_t connection, ZuTime now);
  SessionEvent logout_(uint64_t connection, ZuTime now);
  SessionEvent peerLogoutEarly_(uint64_t connection);
  SessionEvent peerLogout_(uint64_t connection, ZuTime now);
  SessionEvent lost_(uint64_t connection);
  SessionEvent outputFailed_(uint64_t connection);
  SessionEvent closed_(uint64_t connection);
  SessionEvent timer_(uint64_t connection, unsigned timer, ZuTime now);

  // The control factory creates the same opaque text from the fixed snapshot.
  template <typename S> static void printProbe(S &out, const SessionEvent &e) {
    out << "Zfix/" << ZuBox<uint128_t>{e.instance} << '/' << ZuBox<uint64_t>{e.epoch} << '/' <<
      ZuBox<uint64_t>{e.connection} << '/' << ZuBox<uint64_t>{e.probe};
  }

private:
  SessionEvent open(ZuTime now, bool replace);
  bool current(uint64_t connection) const;
  SessionEvent event(unsigned actions = 0, unsigned error = 0, unsigned tag = 0) const;
  SessionEvent transition(unsigned state, unsigned actions);
  SessionEvent reject(unsigned error, unsigned tag, ZuTime now);
  SessionEvent close(unsigned error);
  SessionEvent synchronize(ZuTime now);
  SessionEvent startProbe(ZuTime now);
  ZuTime receiveInterval() const;
  ZuTime probeInterval() const;
  const SessionConfig	m_config;
  const uint128_t	m_instance;

  // Rx-owned lifecycle, challenge and logical clock state.
  SessionString		m_probeID;
  SessionDeadlines	m_deadlines;
  uint64_t		m_epoch;
  uint64_t		m_connection = 0;
  uint64_t		m_change = 0;
  uint64_t		m_ticket = 0;
  uint64_t		m_admission = 0;
  uint64_t		m_txBoundary = 0;
  uint64_t		m_probe = 0;
  uint64_t		m_probeBoundary = 0;
  uint64_t		m_drain = 0;
  uint64_t		m_drainBoundary = 0;
  // Zero: no reset; greater than epoch: preparing; equal to epoch: armed.
  uint64_t		m_nextEpoch = 0;
  uint32_t		m_heartBtInt = 0;
  unsigned		m_state = State::Disconnected;
  bool			m_admitted = false;
  bool			m_peerLogout = false;
  bool			m_rxDone = false;
  bool			m_txDone = false;
  bool			m_ready = false;
  bool			m_resetInitiator = false;
  bool			m_resetOnOpen = false;
  ResetPath::T		m_resetPath = ResetPath::Live;
  bool			m_draining = false;
  bool			m_drained = false;
};

} // Zfix

#endif /* ZfixLinkState_HH */
