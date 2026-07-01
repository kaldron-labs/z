//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC qlog

#ifndef ZquicLog_HH
#define ZquicLog_HH

#ifndef ZquicLib_HH
#include <zlib/ZquicLib.hh>
#endif

#include <zlib/ZuTime.hh>
#include <zlib/ZuArray.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmPLock.hh>
#include <zlib/ZmRing.hh>
#include <zlib/ZmRingFn.hh>
#include <zlib/ZmThread.hh>

#include <zlib/ZtString.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiFile.hh>

#include <zlib/ZquicTypes.hh>

#if defined(ZDEBUG) && !defined(Zquic_DEBUG)
#define Zquic_DEBUG	// enable testing / debugging
#endif

struct ZquicLogDiag {
  uint64_t	recordsEnqueued = 0;
  uint64_t	recordsWritten = 0;
  uint64_t	recordsDropped = 0;
  uint64_t	ringBackPressure = 0;
  uint64_t	writerFailures = 0;
  uint64_t	bytesWritten = 0;
};

struct ZquicLogParams {
  ZquicLogParams &enabled(bool v) { m_enabled = v; return *this; }
  ZquicLogParams &path(ZuCSpan v) { m_path = v; return *this; }
  ZquicLogParams &thread(ZuCSpan v) { m_thread = v; return *this; }
  ZquicLogParams &ringSize(unsigned v) { m_ringSize = v; return *this; }

  bool enabled() const { return m_enabled; }
  ZuCSpan path() const { return m_path; }
  ZuCSpan thread() const { return m_thread; }
  unsigned ringSize() const { return m_ringSize; }

private:
  bool		m_enabled = false;
  ZtString<>	m_path;
  ZtString<>	m_thread;
  unsigned	m_ringSize = (1<<20);
};

struct ZquicLogMetadata {
  ZuCSpan	vantagePoint;
  ZuBSpan	originalDCID;
  ZuBSpan	groupID;
  ZuBSpan	dcid;
  ZuBSpan	scid;
};

enum { ZquicLogFrameMax = 8 };

struct ZquicLogDatagramEvent {
	uint64_t	size = 0;
	Zquic::EcnMark::T ecn = Zquic::EcnMark::N;
};

struct ZquicLogFrameEvent {
	Zquic::FrameType::T type = Zquic::FrameType::Unknown;
  uint64_t	streamID = 0;
  uint64_t	offset = 0;
  uint64_t	length = 0;
  uint64_t	value = 0;
  uint64_t	errorCode = 0;
  uint64_t	largestAcked = 0;
  uint64_t	ackDelayUS = 0;
  uint64_t	ect0 = 0;
  uint64_t	ect1 = 0;
  uint64_t	ce = 0;
  uint8_t	rangeCount = 0;
  bool		fin = false;

  struct Traits : public ZuBaseTraits<ZquicLogFrameEvent> {
    enum { IsPOD = 1 };
  };
  friend Traits ZuTraitsType(ZquicLogFrameEvent *);
};

struct ZquicLogPacketEvent {
	Zquic::PktType::T packetType = Zquic::PktType::Initial;
	Zquic::PktNumSpace::T packetSpace = Zquic::PktNumSpace::Initial;
	uint64_t	packetNumber = 0;
	uint64_t	packetSize = 0;
	uint64_t	payloadSize = 0;
	Zquic::EcnMark::T ecn = Zquic::EcnMark::N;
  struct Reason {
    ZtEnum(Reason, int8_t,
      None, Coalescing, ParseLong, PacketLength, PrepareLong,
      UnsupportedLongType, DiscardedSpace, MissingKeys, Protection, Duplicate,
      ParseShort, InvalidKeyPhase, AntiAmplification, ProbeAdmission,
      AppSend);
    ZtEnumMap(Reason, JSON,
      "", "coalescing", "parse_long", "packet_length", "prepare_long",
      "unsupported_long_type", "discarded_space", "missing_keys",
      "protection", "duplicate", "parse_short", "invalid_key_phase",
      "anti_amplification", "probe_admission", "app_send");
  };
  Reason::T	reason = Reason::None;
  uint64_t	bytesInFlight = 0;
  uint8_t	frameCount = 0;
  bool		framesTruncated = false;
  ZuArray<ZquicLogFrameEvent, ZquicLogFrameMax> frames;
  bool		ackEliciting = false;
};

struct ZquicLogAckEvent {
	Zquic::PktNumSpace::T packetSpace = Zquic::PktNumSpace::Initial;
  uint64_t	largestAcked = 0;
  uint64_t	ackDelayUS = 0;
  uint64_t	ackedBytes = 0;
  uint64_t	lostBytes = 0;
  uint8_t	rangeCount = 0;
  uint8_t	ackedFrames = 0;
  uint8_t	lostFrames = 0;
};

struct ZquicLogRecoveryKind {
  ZtEnum(ZquicLogRecoveryKind, int8_t,
    Aggregate, Loss, PTO, RTT, NewReno);
  ZtEnumMap(ZquicLogRecoveryKind, JSON,
    "aggregate", "loss", "pto", "rtt", "newreno");
};

struct ZquicLogRecoveryReason {
  ZtEnum(ZquicLogRecoveryReason, int8_t,
    None, TimeThreshold, PacketThreshold, Canceled, Armed, NoLevel, Ack,
    PMTUDAck, Loss, PMTUDLoss, Expired, Probe, Backoff);
  ZtEnumMap(ZquicLogRecoveryReason, JSON,
    "", "time_threshold", "packet_threshold", "canceled", "armed",
    "no_level", "ack", "pmtud_ack", "loss", "pmtud_loss",
    "expired", "probe", "backoff");
};

struct ZquicLogRecoveryEvent {
	ZquicLogRecoveryKind::T kind = ZquicLogRecoveryKind::Aggregate;
	Zquic::PktNumSpace::T packetSpace = Zquic::PktNumSpace::Initial;
  ZquicLogRecoveryReason::T reason = ZquicLogRecoveryReason::None;
  uint64_t	value = 0;
  uint64_t	packetNumber = 0;
  uint64_t	bytes = 0;
  uint64_t	deadlineUS = 0;
  uint64_t	latestRTTUS = 0;
  uint64_t	smoothedRTTUS = 0;
  uint64_t	rttVarianceUS = 0;
  uint64_t	minRTTUS = 0;
  uint64_t	cwnd = 0;
  uint64_t	ssthresh = 0;
  uint64_t	bytesInFlight = 0;
  uint8_t	frameCount = 0;
};

struct ZquicLogECNState {
  ZtEnum(ZquicLogECNState, int8_t, Validated, Disabled);
  ZtEnumMap(ZquicLogECNState, JSON, "validated", "disabled");
};

struct ZquicLogECNReason {
  ZtEnum(ZquicLogECNReason, int8_t,
    AckECN, ECT0Decrease, ECT1Decrease, CEDecrease, ECTOverflow, CEOverflow,
    CounterExceedsAck);
  ZtEnumMap(ZquicLogECNReason, JSON,
    "ack_ecn", "ect0_decrease", "ect1_decrease", "ce_decrease",
    "ect_overflow", "ce_overflow", "counter_exceeds_ack");
};

struct ZquicLogECNEvent {
	Zquic::PktNumSpace::T packetSpace = Zquic::PktNumSpace::Initial;
  ZquicLogECNState::T state = ZquicLogECNState::Validated;
  ZquicLogECNReason::T reason = ZquicLogECNReason::AckECN;
  uint64_t	ect0 = 0;
  uint64_t	ect1 = 0;
  uint64_t	ce = 0;
  uint64_t	previousECT0 = 0;
  uint64_t	previousECT1 = 0;
  uint64_t	previousCE = 0;
  uint64_t	largestAcked = 0;
  bool		disabled = false;
};

struct ZquicLogSecurityKind {
  ZtEnum(ZquicLogSecurityKind, int8_t,
    KeyUpdated, KeyRetired, TransportParameters, ALPN, TLS, Retry, Token,
    VersionNegotiation, StatelessReset, PacketProtection);
  ZtEnumMap(ZquicLogSecurityKind, JSON,
    "key_updated", "key_retired", "transport_parameters", "alpn", "tls",
    "retry", "token", "version_negotiation", "stateless_reset",
    "packet_protection");
};

struct ZquicLogSecurityKeyType {
  ZtEnum(ZquicLogSecurityKeyType, int8_t, None, RX, RXOld, TX);
  ZtEnumMap(ZquicLogSecurityKeyType, JSON, "", "rx", "rx_old", "tx");
};

struct ZquicLogSecurityTrigger {
  ZtEnum(ZquicLogSecurityTrigger, int8_t,
    None, Sent, Received, Validated, Local, Remote, Peer, Selected, Timer,
    HandshakeComplete, RX, TX);
  ZtEnumMap(ZquicLogSecurityTrigger, JSON,
    "", "sent", "received", "validated", "local", "remote", "peer",
    "selected", "timer", "handshake_complete", "rx", "tx");
};

struct ZquicLogSecurityReason {
  ZtEnum(ZquicLogSecurityReason, int8_t,
    None, Unknown, OK, Handshake, KeyPhase, KeyUpdate, PeerUpdate,
    PacketSpace, AddressValidation, MissingToken, NewToken, NewTokenPolicy,
    UnsupportedVersion, UnknownCID, TokenMatch, Validation, RetrySCID, Expired,
    Auth, Address, Malformed, Kind, ODCID, Protect, MissingKeys, Protection,
    InvalidKeyPhase);
  ZtEnumMap(ZquicLogSecurityReason, JSON,
    "", "unknown", "ok", "handshake", "key_phase", "key_update",
    "peer_update", "packet_space", "address_validation", "missing_token",
    "new_token", "new_token_policy", "unsupported_version", "unknown_cid",
    "token_match", "validation", "retry_scid", "expired", "auth", "address",
    "malformed", "kind", "odcid", "protect", "missing_keys", "protection",
    "invalid_key_phase");
};

struct ZquicLogSecurityEvent {
	ZquicLogSecurityKind::T kind = ZquicLogSecurityKind::TLS;
	Zquic::PktNumSpace::T packetSpace = Zquic::PktNumSpace::Initial;
  ZquicLogSecurityKeyType::T keyType = ZquicLogSecurityKeyType::None;
  ZquicLogSecurityTrigger::T trigger = ZquicLogSecurityTrigger::None;
  ZeString	alpn;
  ZquicLogSecurityReason::T reason = ZquicLogSecurityReason::None;
  uint64_t	value = 0;
  bool		success = true;
};

struct ZquicLogPathKind {
  ZtEnum(ZquicLogPathKind, int8_t, Path, PathValidation, PMTUD);
  ZtEnumMap(ZquicLogPathKind, JSON,
    "path", "path_validation", "pmtud");
};

struct ZquicLogPathAction {
  ZtEnum(ZquicLogPathAction, int8_t,
    Created, Validated, Received, Observed, ChallengeSent, ResponseUnknown,
    ResponseReceived, Updated, Failed, Blocked, Expired, Hint, Acked, Lost,
    Sent);
  ZtEnumMap(ZquicLogPathAction, JSON,
    "created", "validated", "received", "observed", "challenge_sent",
    "response_unknown", "response_received", "updated", "failed", "blocked",
    "expired", "hint", "acked", "lost", "sent");
};

struct ZquicLogPathReason {
  ZtEnum(ZquicLogPathReason, int8_t,
    None, Client, Server, Initial, Datagram, PeerAddressChange, Mismatch,
    Matched, Response, Promoted, Timeout, AntiAmplification, ProbeAdmission,
    PathHint, Probe, Admission, SendFailure);
  ZtEnumMap(ZquicLogPathReason, JSON,
    "", "client", "server", "initial", "datagram", "peer_address_change",
    "mismatch", "matched", "response", "promoted", "timeout",
    "anti_amplification", "probe_admission", "path_hint", "probe",
    "admission", "send_failure");
};

struct ZquicLogPathEvent {
  ZquicLogPathKind::T kind = ZquicLogPathKind::Path;
  ZquicLogPathAction::T action = ZquicLogPathAction::Updated;
  ZquicLogPathReason::T reason = ZquicLogPathReason::None;
  uint64_t	bytes = 0;
  uint64_t	antiAmplification = 0;
  uint64_t	deadlineUS = 0;
  uint32_t	mtu = 0;
  bool		validated = false;
};

struct ZquicLogCIDKind {
  ZtEnum(ZquicLogCIDKind, int8_t, ConnectionID);
  ZtEnumMap(ZquicLogCIDKind, JSON, "connection_id");
};

struct ZquicLogCIDAction {
  ZtEnum(ZquicLogCIDAction, int8_t,
    Issued, Updated, Retired, RouteBound, Tombstone);
  ZtEnumMap(ZquicLogCIDAction, JSON,
    "issued", "updated", "retired", "route_bound", "tombstone");
};

struct ZquicLogCIDReason {
  ZtEnum(ZquicLogCIDReason, int8_t,
    None, PeerRequest, PathPromoted, RouteInstall, RouteRetire,
    RouteTombstone, Sequence, ID, ResetToken, RetirePriorTo);
  ZtEnumMap(ZquicLogCIDReason, JSON,
    "", "peer_request", "path_promoted", "route_install", "route_retire",
    "route_tombstone", "sequence", "id", "reset_token", "retire_prior_to");
};

struct ZquicLogCIDEvent {
  ZquicLogCIDKind::T kind = ZquicLogCIDKind::ConnectionID;
  ZquicLogCIDAction::T action = ZquicLogCIDAction::Updated;
  ZquicLogCIDReason::T reason = ZquicLogCIDReason::None;
  uint64_t	sequence = 0;
  uint8_t	length = 0;
  bool		local = false;
  bool		associated = false;
  bool		resetToken = false;
};

struct ZquicLogStreamType {
  ZtEnum(ZquicLogStreamType, int8_t, Bidirectional, Unidirectional);
  ZtEnumMap(ZquicLogStreamType, JSON, "bidirectional", "unidirectional");
};

struct ZquicLogStreamState {
  ZtEnum(ZquicLogStreamState, int8_t, Idle, Open, Closed);
  ZtEnumMap(ZquicLogStreamState, JSON, "idle", "open", "closed");
};

struct ZquicLogStreamSide {
  ZtEnum(ZquicLogStreamSide, int8_t, Sending, Receiving);
  ZtEnumMap(ZquicLogStreamSide, JSON, "sending", "receiving");
};

struct ZquicLogStreamReason {
  ZtEnum(ZquicLogStreamReason, int8_t, None, LocalOpen, PeerOpen, Reaped);
  ZtEnumMap(ZquicLogStreamReason, JSON,
    "", "local_open", "peer_open", "reaped");
};

struct ZquicLogStreamEvent {
  ZquicLogStreamType::T streamType = ZquicLogStreamType::Bidirectional;
  ZquicLogStreamState::T oldState = ZquicLogStreamState::Idle;
  ZquicLogStreamState::T newState = ZquicLogStreamState::Open;
  ZquicLogStreamSide::T streamSide = ZquicLogStreamSide::Sending;
  ZquicLogStreamReason::T reason = ZquicLogStreamReason::None;
  uint64_t	streamID = 0;
  uint64_t	offset = 0;
  uint64_t	length = 0;
  uint64_t	errorCode = 0;
  bool		fin = false;
};

struct ZquicLogStreamDataLoc {
  ZtEnum(ZquicLogStreamDataLoc, int8_t, Application, Transport, Network);
  ZtEnumMap(ZquicLogStreamDataLoc, JSON,
    "application", "transport", "network");
};

struct ZquicLogStreamDataInfo {
  ZtEnum(ZquicLogStreamDataInfo, int8_t, None, FinSet);
  ZtEnumMap(ZquicLogStreamDataInfo, JSON, "", "fin_set");
};

struct ZquicLogStreamDataEvent {
  ZquicLogStreamDataLoc::T from = ZquicLogStreamDataLoc::Transport;
  ZquicLogStreamDataLoc::T to = ZquicLogStreamDataLoc::Application;
  ZquicLogStreamDataInfo::T additionalInfo = ZquicLogStreamDataInfo::None;
  uint64_t	streamID = 0;
  uint64_t	offset = 0;
  uint64_t	length = 0;
};

struct ZquicLogBlockedState {
  ZtEnum(ZquicLogBlockedState, int8_t, Blocked, Unblocked);
  ZtEnumMap(ZquicLogBlockedState, JSON, "blocked", "unblocked");
};

struct ZquicLogBlockedReason {
  ZtEnum(ZquicLogBlockedReason, int8_t,
    Scheduling, Pacing, AmplificationProtection, CongestionControl,
    ConnectionFlowControl, StreamFlowControl, StreamID, Application);
  ZtEnumMap(ZquicLogBlockedReason, JSON,
    "scheduling", "pacing", "amplification_protection",
    "congestion_control", "connection_flow_control", "stream_flow_control",
    "stream_id", "application");
};

struct ZquicLogBlockedEvent {
  ZquicLogBlockedState::T oldState = ZquicLogBlockedState::Unblocked;
  ZquicLogBlockedState::T newState = ZquicLogBlockedState::Blocked;
  ZquicLogBlockedReason::T reason =
    ZquicLogBlockedReason::ConnectionFlowControl;
  uint64_t	streamID = 0;
};

struct ZquicLogCloseInitiator {
  ZtEnum(ZquicLogCloseInitiator, int8_t, Local, Remote);
  ZtEnumMap(ZquicLogCloseInitiator, JSON, "local", "remote");
};

struct ZquicLogCloseTrigger {
  ZtEnum(ZquicLogCloseTrigger, int8_t,
    Application, Error, IdleTimeout, Aborted);
  ZtEnumMap(ZquicLogCloseTrigger, JSON,
    "application", "error", "idle_timeout", "aborted");
};

struct ZquicLogCloseReason {
  ZtEnum(ZquicLogCloseReason, int8_t,
    None, LocalClose, PeerCloseFrame, DrainExpired, Idle);
  ZtEnumMap(ZquicLogCloseReason, JSON,
    "", "local_close", "peer_close_frame", "drain_expired", "idle");
};

struct ZquicLogCloseError {
  ZtEnum(ZquicLogCloseError, int8_t, None, Unknown, NoError);
  ZtEnumMap(ZquicLogCloseError, JSON, "", "unknown", "no_error");
};

struct ZquicLogCloseEvent {
	ZquicLogCloseInitiator::T initiator = ZquicLogCloseInitiator::Local;
	ZquicLogCloseTrigger::T trigger = ZquicLogCloseTrigger::Error;
  ZquicLogCloseReason::T reason = ZquicLogCloseReason::None;
  ZquicLogCloseError::T connectionError = ZquicLogCloseError::None;
  ZquicLogCloseError::T applicationError = ZquicLogCloseError::None;
  uint64_t	errorCode = 0;
  bool		application = false;
	bool		frame = false;
};

struct ZquicLogLifecycle {
  ZtEnum(ZquicLogLifecycle, int8_t, Started, Closed);
  ZtEnumMap(ZquicLogLifecycle, JSON,
    "connectivity:connection_started", "connectivity:connection_closed");
};

#ifdef Zquic_DEBUG

class ZquicAPI ZquicLogSink {
public:
  bool init(ZuCSpan);
  void final();
  bool write(ZuCSpan);

private:
  Zi::Path	m_path;
  ZiFile	m_file;
};

class ZquicAPI ZquicLogger {
  using Lock = ZmPLock;
  using Guard = ZmGuard<Lock>;
  ZuDerive(Ring, (ZmRing<ZmRingMW<true>>));
  using Fn = ZmRingFn<ZquicLogger *>;

public:
  ~ZquicLogger();

  ZquicLogger(const ZquicLogger &) = delete;
  ZquicLogger &operator =(const ZquicLogger &) = delete;

  static ZquicLogger *instance();

  static bool enabled() { return instance()->enabled_(); }
  static bool init(const ZquicLogParams &params) {
    return instance()->init_(params);
  }
  static bool metadata(ZuCSpan vantagePoint) {
    return instance()->metadata_(ZquicLogMetadata{
      .vantagePoint = vantagePoint
    });
  }
  static bool metadata(const ZquicLogMetadata &metadata) {
    return instance()->metadata_(metadata);
  }
  static void start() { instance()->start_(); }
  static void stop() { instance()->stop_(); }
  static void final() { instance()->final_(); }
  static ZquicLogDiag diag() { return instance()->diag_(); }

  static void lifecycle(ZquicLogLifecycle::T event) {
    instance()->lifecycle_(event);
  }
  static void datagramSent(ZquicLogDatagramEvent event) {
    instance()->datagramSent_(ZuMv(event));
  }
  static void datagramReceived(ZquicLogDatagramEvent event) {
    instance()->datagramReceived_(ZuMv(event));
  }
  static void packetSent(ZquicLogPacketEvent event) {
    instance()->packetSent_(ZuMv(event));
  }
  static void packetReceived(ZquicLogPacketEvent event) {
    instance()->packetReceived_(ZuMv(event));
  }
  static void packetBuffered(ZquicLogPacketEvent event) {
    instance()->packetBuffered_(ZuMv(event));
  }
  static void packetDropped(ZquicLogPacketEvent event) {
    instance()->packetDropped_(ZuMv(event));
  }
  static void packetsAcked(ZquicLogAckEvent event) {
    instance()->packetsAcked_(ZuMv(event));
  }
  static void packetLost(ZquicLogRecoveryEvent event) {
    instance()->packetLost_(ZuMv(event));
  }
  static void recoveryPacketLost(ZquicLogRecoveryEvent event) {
    instance()->recoveryPacketLost_(ZuMv(event));
  }
  static void markedForRetransmit(ZquicLogRecoveryEvent event) {
    instance()->markedForRetransmit_(ZuMv(event));
  }
  static void metricsUpdated(ZquicLogRecoveryEvent event) {
    instance()->metricsUpdated_(ZuMv(event));
  }
  static void lossTimerUpdated(ZquicLogRecoveryEvent event) {
    instance()->lossTimerUpdated_(ZuMv(event));
  }
  static void congestionStateUpdated(ZquicLogRecoveryEvent event) {
    instance()->congestionStateUpdated_(ZuMv(event));
  }
  static void ecnStateUpdated(ZquicLogECNEvent event) {
    instance()->ecnStateUpdated_(ZuMv(event));
  }
  static void keyUpdated(ZquicLogSecurityEvent event) {
    instance()->keyUpdated_(ZuMv(event));
  }
  static void keyRetired(ZquicLogSecurityEvent event) {
    instance()->keyRetired_(ZuMv(event));
  }
  static void transportParametersSet(ZquicLogSecurityEvent event) {
    instance()->transportParametersSet_(ZuMv(event));
  }
  static void alpnInformation(ZquicLogSecurityEvent event) {
    instance()->alpnInformation_(ZuMv(event));
  }
  static void tlsAlert(ZquicLogSecurityEvent event) {
    instance()->tlsAlert_(ZuMv(event));
  }
  static void securityEvent(
    ZuCSpan name, ZquicLogSecurityEvent event) {
    instance()->securityEvent_(ZeString{name}, ZuMv(event));
  }
  static void pathUpdated(ZquicLogPathEvent event) {
    instance()->pathUpdated_(ZuMv(event));
  }
  static void pathValidationUpdated(ZquicLogPathEvent event) {
    instance()->pathValidationUpdated_(ZuMv(event));
  }
  static void pmtudUpdated(ZquicLogPathEvent event) {
    instance()->pmtudUpdated_(ZuMv(event));
  }
  static void cidUpdated(ZquicLogCIDEvent event) {
    instance()->cidUpdated_(ZuMv(event));
  }
  static void streamStateUpdated(ZquicLogStreamEvent event) {
    instance()->streamStateUpdated_(ZuMv(event));
  }
  static void streamDataMoved(ZquicLogStreamDataEvent event) {
    instance()->streamDataMoved_(ZuMv(event));
  }
  static void connectionDataBlockedUpdated(
    ZquicLogBlockedEvent event) {
    instance()->connectionDataBlockedUpdated_(ZuMv(event));
  }
  static void streamDataBlockedUpdated(ZquicLogBlockedEvent event) {
    instance()->streamDataBlockedUpdated_(ZuMv(event));
  }
  static void connectionClosed(ZquicLogCloseEvent event) {
    instance()->connectionClosed_(ZuMv(event));
  }
  template <typename L>
  static void log(L l) {
    instance()->log_(ZuMv(l));
  }

  void logLifecycle(ZquicLogLifecycle::T event, ZuTime time) {
    writeLifecycle_(event, time);
  }
  void logDatagramSent(const ZquicLogDatagramEvent &event, ZuTime time) {
    writeDatagramEvent_("transport:datagrams_sent", event, time);
  }
  void logDatagramReceived(const ZquicLogDatagramEvent &event, ZuTime time) {
    writeDatagramEvent_("transport:datagrams_received", event, time);
  }
  void logPacketSent(const ZquicLogPacketEvent &event, ZuTime time) {
    writePacketEvent_("transport:packet_sent", event, time);
  }
  void logPacketReceived(const ZquicLogPacketEvent &event, ZuTime time) {
    writePacketEvent_("transport:packet_received", event, time);
  }
  void logPacketBuffered(const ZquicLogPacketEvent &event, ZuTime time) {
    writePacketEvent_("transport:packet_buffered", event, time);
  }
  void logPacketDropped(const ZquicLogPacketEvent &event, ZuTime time) {
    writePacketEvent_("transport:packet_dropped", event, time);
  }
  void logPacketsAcked(const ZquicLogAckEvent &event, ZuTime time) {
    writeAckEvent_("transport:packets_acked", event, time);
  }
  void logPacketLost(const ZquicLogRecoveryEvent &event, ZuTime time) {
    writeRecoveryEvent_("transport:packet_lost", event, time);
  }
  void logRecoveryPacketLost(const ZquicLogRecoveryEvent &event, ZuTime time) {
    writeRecoveryEvent_("recovery:packet_lost", event, time);
  }
  void logMarkedForRetransmit(
    const ZquicLogRecoveryEvent &event, ZuTime time) {
    writeRecoveryEvent_("recovery:marked_for_retransmit", event, time);
  }
  void logMetricsUpdated(const ZquicLogRecoveryEvent &event, ZuTime time) {
    writeRecoveryEvent_("recovery:metrics_updated", event, time);
  }
  void logLossTimerUpdated(const ZquicLogRecoveryEvent &event, ZuTime time) {
    writeRecoveryEvent_("recovery:loss_timer_updated", event, time);
  }
  void logCongestionStateUpdated(
    const ZquicLogRecoveryEvent &event, ZuTime time) {
    writeRecoveryEvent_("recovery:congestion_state_updated", event, time);
  }
  void logECNStateUpdated(const ZquicLogECNEvent &event, ZuTime time) {
    writeECNEvent_("recovery:ecn_state_updated", event, time);
  }
  void logKeyUpdated(const ZquicLogSecurityEvent &event, ZuTime time) {
    writeSecurityEvent_("security:key_updated", event, time);
  }
  void logKeyRetired(const ZquicLogSecurityEvent &event, ZuTime time) {
    writeSecurityEvent_("security:key_retired", event, time);
  }
  void logTransportParametersSet(
    const ZquicLogSecurityEvent &event, ZuTime time) {
    writeSecurityEvent_("security:transport_parameters_set", event, time);
  }
  void logALPNInformation(const ZquicLogSecurityEvent &event, ZuTime time) {
    writeSecurityEvent_("security:alpn_information", event, time);
  }
  void logTLSAlert(const ZquicLogSecurityEvent &event, ZuTime time) {
    writeSecurityEvent_("security:tls_alert", event, time);
  }
  void logSecurityEvent(
    ZuCSpan name, const ZquicLogSecurityEvent &event, ZuTime time) {
    writeSecurityEvent_(name, event, time);
  }
  void logPathUpdated(const ZquicLogPathEvent &event, ZuTime time) {
    writePathEvent_("path:path_updated", event, time);
  }
  void logPathValidationUpdated(
    const ZquicLogPathEvent &event, ZuTime time) {
    writePathEvent_("path:path_validation_updated", event, time);
  }
  void logPMTUDUpdated(const ZquicLogPathEvent &event, ZuTime time) {
    writePathEvent_("path:pmtud_updated", event, time);
  }
  void logCIDUpdated(const ZquicLogCIDEvent &event, ZuTime time) {
    writeCIDEvent_("connectivity:connection_id_updated", event, time);
  }
  void logStreamStateUpdated(
    const ZquicLogStreamEvent &event, ZuTime time) {
    writeStreamEvent_("transport:stream_state_updated", event, time);
  }
  void logStreamDataMoved(
    const ZquicLogStreamDataEvent &event, ZuTime time) {
    writeStreamDataEvent_("transport:stream_data_moved", event, time);
  }
  void logConnectionDataBlockedUpdated(
    const ZquicLogBlockedEvent &event, ZuTime time) {
    writeConnectionBlockedEvent_(
      "transport:connection_data_blocked_updated", event, time);
  }
  void logStreamDataBlockedUpdated(
    const ZquicLogBlockedEvent &event, ZuTime time) {
    writeStreamBlockedEvent_(
      "transport:stream_data_blocked_updated", event, time);
  }
  void logConnectionClosed(
    const ZquicLogCloseEvent &event, ZuTime time) {
    writeCloseEvent_("connectivity:connection_closed", event, time);
  }

private:
  ZquicLogger();

  bool enabled_() const { return m_enabled.load_(); }
  bool init_(const ZquicLogParams &);
  bool metadata_(const ZquicLogMetadata &);
  void start_();
  void stop_();
  void final_();
  ZquicLogDiag diag_() const;
  void lifecycle_(ZquicLogLifecycle::T);
  void datagramSent_(ZquicLogDatagramEvent);
  void datagramReceived_(ZquicLogDatagramEvent);
  void packetSent_(ZquicLogPacketEvent);
  void packetReceived_(ZquicLogPacketEvent);
  void packetBuffered_(ZquicLogPacketEvent);
  void packetDropped_(ZquicLogPacketEvent);
  void packetsAcked_(ZquicLogAckEvent);
  void packetLost_(ZquicLogRecoveryEvent);
  void recoveryPacketLost_(ZquicLogRecoveryEvent);
  void markedForRetransmit_(ZquicLogRecoveryEvent);
  void metricsUpdated_(ZquicLogRecoveryEvent);
  void lossTimerUpdated_(ZquicLogRecoveryEvent);
  void congestionStateUpdated_(ZquicLogRecoveryEvent);
  void ecnStateUpdated_(ZquicLogECNEvent);
  void keyUpdated_(ZquicLogSecurityEvent);
  void keyRetired_(ZquicLogSecurityEvent);
  void transportParametersSet_(ZquicLogSecurityEvent);
  void alpnInformation_(ZquicLogSecurityEvent);
  void tlsAlert_(ZquicLogSecurityEvent);
  void securityEvent_(ZeString, ZquicLogSecurityEvent);
  void pathUpdated_(ZquicLogPathEvent);
  void pathValidationUpdated_(ZquicLogPathEvent);
  void pmtudUpdated_(ZquicLogPathEvent);
  void cidUpdated_(ZquicLogCIDEvent);
  void streamStateUpdated_(ZquicLogStreamEvent);
  void streamDataMoved_(ZquicLogStreamDataEvent);
  void connectionDataBlockedUpdated_(ZquicLogBlockedEvent);
  void streamDataBlockedUpdated_(ZquicLogBlockedEvent);
  void connectionClosed_(ZquicLogCloseEvent);
  template <typename L>
  void post_(L &l) {
    Fn fn{l};
    unsigned size = fn.pushSize();
    if (void *ptr = m_ring.tryPush(size)) {
      fn.push(ptr);
      m_ring.push2(ptr, size);
      ++m_recordsEnqueued;
    } else {
      ++m_recordsDropped;
      ++m_ringBackPressure;
    }
  }
  template <typename L>
  void log_(L l) {
    if (!enabled_()) return;
    ZuTime time = Zm::now();
    auto fn_ = [l = ZuMv(l), time](ZquicLogger *this_) mutable {
      l(*this_, time);
    };
    post_(fn_);
  }

  void work_();
  bool headerReady_(bool) const;
  bool writeHeader_();
  bool writeLifecycle_(ZquicLogLifecycle::T, ZuTime);
  bool writeDatagramEvent_(
    ZuCSpan name, const ZquicLogDatagramEvent &, ZuTime);
  bool writePacketEvent_(
    ZuCSpan name, const ZquicLogPacketEvent &, ZuTime);
  bool writeAckEvent_(ZuCSpan name, const ZquicLogAckEvent &, ZuTime);
  bool writeRecoveryEvent_(
    ZuCSpan name, const ZquicLogRecoveryEvent &, ZuTime);
  bool writeECNEvent_(ZuCSpan name, const ZquicLogECNEvent &, ZuTime);
  bool writeSecurityEvent_(
    ZuCSpan name, const ZquicLogSecurityEvent &, ZuTime);
  bool writePathEvent_(ZuCSpan name, const ZquicLogPathEvent &, ZuTime);
  bool writeCIDEvent_(ZuCSpan name, const ZquicLogCIDEvent &, ZuTime);
  bool writeStreamEvent_(ZuCSpan name, const ZquicLogStreamEvent &, ZuTime);
  bool writeStreamDataEvent_(
    ZuCSpan name, const ZquicLogStreamDataEvent &, ZuTime);
  bool writeConnectionBlockedEvent_(
    ZuCSpan name, const ZquicLogBlockedEvent &, ZuTime);
  bool writeStreamBlockedEvent_(
    ZuCSpan name, const ZquicLogBlockedEvent &, ZuTime);
  bool writeCloseEvent_(ZuCSpan name, const ZquicLogCloseEvent &, ZuTime);

private:
  ZmAtomic<int>		m_enabled = 0;
  bool			m_configured = false;
  bool			m_started = false;
  bool			m_headerWritten = false;
  bool			m_waitForMetadata = false;
  ZquicLogParams		m_params;
  ZeString		m_vantagePoint;
  ZtBArray<>		m_originalDCID;
  ZtBArray<>		m_groupID;
  ZtBArray<>		m_dcid;
  ZtBArray<>		m_scid;
  ZmThread		m_thread;
  Ring			m_ring;
  ZquicLogSink		m_sink;
  ZeLogBuf		m_buf;
  ZmAtomic<uint64_t>	m_recordsEnqueued = 0;
  ZmAtomic<uint64_t>	m_recordsWritten = 0;
  ZmAtomic<uint64_t>	m_recordsDropped = 0;
  ZmAtomic<uint64_t>	m_ringBackPressure = 0;
  ZmAtomic<uint64_t>	m_writerFailures = 0;
  ZmAtomic<uint64_t>	m_bytesWritten = 0;
  mutable Lock		m_lock;
};

#else /* Zquic_DEBUG */

struct ZquicLogger {
  static constexpr bool enabled() { return false; }
  static bool init(const ZquicLogParams &) { return true; }
  static bool metadata(ZuCSpan) { return true; }
  static bool metadata(const ZquicLogMetadata &) { return true; }
  static void start() { }
  static void stop() { }
  static void final() { }
  static ZquicLogDiag diag() { return {}; }
  static void lifecycle(ZquicLogLifecycle::T) { }
  static void datagramSent(ZquicLogDatagramEvent) { }
  static void datagramReceived(ZquicLogDatagramEvent) { }
  static void packetSent(ZquicLogPacketEvent) { }
  static void packetReceived(ZquicLogPacketEvent) { }
  static void packetBuffered(ZquicLogPacketEvent) { }
  static void packetDropped(ZquicLogPacketEvent) { }
  static void packetsAcked(ZquicLogAckEvent) { }
  static void packetLost(ZquicLogRecoveryEvent) { }
  static void recoveryPacketLost(ZquicLogRecoveryEvent) { }
  static void markedForRetransmit(ZquicLogRecoveryEvent) { }
  static void metricsUpdated(ZquicLogRecoveryEvent) { }
  static void lossTimerUpdated(ZquicLogRecoveryEvent) { }
  static void congestionStateUpdated(ZquicLogRecoveryEvent) { }
  static void ecnStateUpdated(ZquicLogECNEvent) { }
  static void keyUpdated(ZquicLogSecurityEvent) { }
  static void keyRetired(ZquicLogSecurityEvent) { }
  static void transportParametersSet(ZquicLogSecurityEvent) { }
  static void alpnInformation(ZquicLogSecurityEvent) { }
  static void tlsAlert(ZquicLogSecurityEvent) { }
  static void securityEvent(ZuCSpan, ZquicLogSecurityEvent) { }
  static void pathUpdated(ZquicLogPathEvent) { }
  static void pathValidationUpdated(ZquicLogPathEvent) { }
  static void pmtudUpdated(ZquicLogPathEvent) { }
  static void cidUpdated(ZquicLogCIDEvent) { }
  static void streamStateUpdated(ZquicLogStreamEvent) { }
  static void streamDataMoved(ZquicLogStreamDataEvent) { }
  static void connectionDataBlockedUpdated(ZquicLogBlockedEvent) { }
  static void streamDataBlockedUpdated(ZquicLogBlockedEvent) { }
  static void connectionClosed(ZquicLogCloseEvent) { }
  template <typename L>
  static void log(L) { }
};

#endif /* Zquic_DEBUG */

template <typename L>
inline void ZquicLog(L l) {
  ZquicLogger::log(ZuMv(l));
}

#ifdef Zquic_DEBUG
#define ZquicLOG(msg) \
  (ZquicLogger::enabled() ? ZquicLog(msg) : void())
#else
#define ZquicLOG(msg) do { } while (0)
#endif

#endif /* ZquicLog_HH */
