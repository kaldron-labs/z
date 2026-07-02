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
#include <zlib/ZiIP.hh>

#include <zlib/ZquicPacket.hh>
#include <zlib/ZquicTypes.hh>
#include <zlib/ZquicTransport.hh>

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
  ZquicLogParams &age(unsigned v) { m_age = v; return *this; }

  bool enabled() const { return m_enabled; }
  ZuCSpan path() const { return m_path; }
  ZuCSpan thread() const { return m_thread; }
  unsigned ringSize() const { return m_ringSize; }
  unsigned age() const { return m_age; }

private:
  bool		m_enabled = false;
  ZtString<>	m_path;
  ZtString<>	m_thread;
  unsigned	m_ringSize = (1<<20);
  unsigned	m_age = 8;
};

struct ZquicLogMetadata {
  ZuCSpan	vantagePoint;
  Zquic::CxnID	originalDCID;
  Zquic::CxnID	groupID;
  Zquic::CxnID	dcid;
  Zquic::CxnID	scid;
};

struct ZquicLogCIDMeta {
  Zquic::CxnID	origDCID;
  Zquic::CxnID	groupID;
  Zquic::CxnID	dcid;
  Zquic::CxnID	scid;

  bool operator !() const {
    return !origDCID.length() && !groupID.length() &&
      !dcid.length() && !scid.length();
  }
  ZuOpBool
};

enum {
  ZquicLogFrameMax = 8,
  ZquicLogAckRangeMax = 16,
  ZquicLogAckPacketMax = 64,
  ZquicLogVersionMax = 8
};

ZuDerive(ZquicLogVersionArray,
  (ZuArray<uint32_t, ZquicLogVersionMax>));

struct ZquicLogAckRange {
  uint64_t	first = 0;
  uint64_t	largest = 0;

  struct Traits : public ZuBaseTraits<ZquicLogAckRange> {
    enum { IsPOD = 1 };
  };
  friend Traits ZuTraitsType(ZquicLogAckRange *);
};

struct ZquicLogDatagramEvent {
	uint64_t	size = 0;
	Zquic::EcnMark::T ecn = Zquic::EcnMark::N;
	ZquicLogCIDMeta metadata;
};

struct ZquicLogStreamType {
  ZtEnum(ZquicLogStreamType, int8_t, Bidirectional, Unidirectional);
  ZtEnumMap(ZquicLogStreamType, JSON, "bidirectional", "unidirectional");
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
  ZuArray<ZquicLogAckRange, ZquicLogAckRangeMax> ackRanges;
  ZquicLogStreamType::T streamType = ZquicLogStreamType::Bidirectional;
  Zquic::CxnID	connectionID;
  Zquic::ResetToken resetToken;
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
  ZquicLogCIDMeta metadata;
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
  bool		packetNumbersTruncated = false;
  ZuArray<uint64_t, ZquicLogAckPacketMax> packetNumbers;
  ZquicLogCIDMeta metadata;
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
  ZuArray<ZquicLogFrameEvent, ZquicLogFrameMax> frames;
  ZquicLogCIDMeta metadata;
};

struct ZquicLogECNState {
  ZtEnum(ZquicLogECNState, int8_t, Unknown, Capable, Failed);
  ZtEnumMap(ZquicLogECNState, JSON, "unknown", "capable", "failed");
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
  ZquicLogECNState::T state = ZquicLogECNState::Capable;
  ZquicLogECNReason::T reason = ZquicLogECNReason::AckECN;
  uint64_t	ect0 = 0;
  uint64_t	ect1 = 0;
  uint64_t	ce = 0;
  uint64_t	previousECT0 = 0;
  uint64_t	previousECT1 = 0;
  uint64_t	previousCE = 0;
  uint64_t	largestAcked = 0;
  bool		disabled = false;
  ZquicLogCIDMeta metadata;
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

struct ZquicLogQKeyType {
  ZtEnum(ZquicLogQKeyType, int8_t,
    ServerInitial, ClientInitial, ServerHandshake, ClientHandshake,
    Server0RTT, Client0RTT, Server1RTT, Client1RTT);
  ZtEnumMap(ZquicLogQKeyType, JSON,
    "server_initial_secret", "client_initial_secret",
    "server_handshake_secret", "client_handshake_secret",
    "server_0rtt_secret", "client_0rtt_secret",
    "server_1rtt_secret", "client_1rtt_secret");
};

struct ZquicLogSecurityTrigger {
  ZtEnum(ZquicLogSecurityTrigger, int8_t,
    None, Sent, Received, Validated, Local, Remote, Peer, Selected, Timer,
    HandshakeComplete, RX, TX);
  ZtEnumMap(ZquicLogSecurityTrigger, JSON,
    "", "sent", "received", "validated", "local", "remote", "peer",
    "selected", "timer", "handshake_complete", "rx", "tx");
};

struct ZquicLogQKeyTrigger {
  ZtEnum(ZquicLogQKeyTrigger, int8_t, TLS, RemoteUpdate, LocalUpdate);
  ZtEnumMap(ZquicLogQKeyTrigger, JSON,
    "tls", "remote_update", "local_update");
};

struct ZquicLogQInitiator {
  ZtEnum(ZquicLogQInitiator, int8_t, Local, Remote);
  ZtEnumMap(ZquicLogQInitiator, JSON, "local", "remote");
};

struct ZquicLogSecurityReason {
  ZtEnum(ZquicLogSecurityReason, int8_t,
    None, Unknown, OK, Handshake, KeyPhase, KeyUpdate, PeerUpdate,
    PacketSpace, AddressValidation, MissingToken, NewToken, NewTokenPolicy,
    UnsupportedVersion, UnknownCID, TokenMatch, Validation, RetrySCID, Expired,
    Auth, Address, Malformed, Kind, ODCID, Protect, MissingKeys, Protection,
    InvalidKeyPhase, ZeroRTT);
  ZtEnumMap(ZquicLogSecurityReason, JSON,
    "", "unknown", "ok", "handshake", "key_phase", "key_update",
    "peer_update", "packet_space", "address_validation", "missing_token",
    "new_token", "new_token_policy", "unsupported_version", "unknown_cid",
    "token_match", "validation", "retry_scid", "expired", "auth", "address",
    "malformed", "kind", "odcid", "protect", "missing_keys", "protection",
    "invalid_key_phase", "0rtt");
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
  ZquicLogCIDMeta metadata;
};

struct ZquicLogTransportParamsEvent {
  ZquicLogQInitiator::T initiator = ZquicLogQInitiator::Local;
  Zquic::CxnID	originalDCID;
  Zquic::CxnID	initialSCID;
  Zquic::CxnID	retrySCID;
  Zquic::ResetToken statelessResetToken;
  uint64_t	maxIdleTimeout = 0;
  uint64_t	maxUDPPayloadSize = 0;
  uint64_t	ackDelayExponent = 0;
  uint64_t	maxAckDelay = 0;
  uint64_t	activeConnectionIDLimit = 0;
  uint64_t	initialMaxData = 0;
  uint64_t	initialMaxStreamDataBidiLocal = 0;
  uint64_t	initialMaxStreamDataBidiRemote = 0;
  uint64_t	initialMaxStreamDataUni = 0;
  uint64_t	initialMaxStreamsBidi = 0;
  uint64_t	initialMaxStreamsUni = 0;
  bool		statelessResetTokenPresent = false;
  bool		disableActiveMigration = false;
  ZquicLogCIDMeta metadata;
};

struct ZquicLogVersionEvent {
  ZquicLogVersionArray serverVersions;
  ZquicLogVersionArray clientVersions;
  uint32_t	chosenVersion = 0;
  bool		chosenVersionPresent = false;
  ZquicLogCIDMeta metadata;
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
  uint64_t	tupleID = 0;
  uint64_t	bytes = 0;
  uint64_t	antiAmplification = 0;
  uint64_t	deadlineUS = 0;
  uint32_t	mtu = 0;
  bool		validated = false;
  ZquicLogCIDMeta metadata;
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
  Zquic::CxnID	connectionID;
  uint64_t	sequence = 0;
  uint8_t	length = 0;
  bool		local = false;
  bool		associated = false;
  bool		resetToken = false;
  ZquicLogCIDMeta metadata;
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
  ZquicLogCIDMeta metadata;
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
  ZquicLogCIDMeta metadata;
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
  ZquicLogCIDMeta metadata;
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
  ZtEnum(ZquicLogCloseError, int8_t,
    None, Unknown, NoError, Internal, ConnectionRefused, FlowControl,
    StreamLimit, StreamState, FinalSize, FrameEncoding, TransportParameter,
    ConnectionIDLimit, ProtocolViolation);
  ZtEnumMap(ZquicLogCloseError, JSON,
    "", "unknown", "no_error", "internal_error", "connection_refused",
    "flow_control_error", "stream_limit_error", "stream_state_error",
    "final_size_error", "frame_encoding_error", "transport_parameter_error",
    "connection_id_limit_error", "protocol_violation");
};

struct ZquicLogCloseEvent {
	ZquicLogCloseInitiator::T initiator = ZquicLogCloseInitiator::Local;
	ZquicLogCloseTrigger::T trigger = ZquicLogCloseTrigger::Error;
  ZquicLogCloseReason::T reason = ZquicLogCloseReason::None;
  ZquicLogCloseError::T connectionError = ZquicLogCloseError::None;
  ZquicLogCloseError::T applicationError = ZquicLogCloseError::None;
  uint64_t	errorCode = 0;
  ZquicLogCIDMeta metadata;
};

struct ZquicLogConnectionState {
  ZtEnum(ZquicLogConnectionState, int8_t,
    Attempted, HandshakeStarted, HandshakeComplete, Closing, Draining, Closed);
  ZtEnumMap(ZquicLogConnectionState, JSON,
    "attempted", "handshake_started", "handshake_complete",
    "closing", "draining", "closed");
};

struct ZquicLogConnectionStateEvent {
  ZquicLogConnectionState::T oldState = ZquicLogConnectionState::Attempted;
  ZquicLogConnectionState::T newState =
    ZquicLogConnectionState::HandshakeStarted;
  ZquicLogCIDMeta metadata;
};

struct ZquicLogConnectionStartedEvent {
  ZiSockAddr	local;
  ZiSockAddr	remote;
  ZquicLogCIDMeta metadata;
};

struct ZquicLogEventName {
  ZtEnum(ZquicLogEventName, int8_t,
    ConnStarted, UDPTx, UDPRx, PktSent, PktRecv, PktBuf, PktDrop, PktsAcked,
    PktLost, MarkRetrans, MetricsUpd, TimerUpd, CongestionUpd, ECNUpd, KeyUpd,
    KeyDiscarded, ParamsSet, ALPNInfo, TLSAlert, RetrySent, RetryValid,
    TokenIssued, TokenValid, TokenReject, VersionInfo, StatelessReset,
    PktProtectFail, ZeroRTTReject, TupleAssigned, PathValidated, MTUUpd, CIDUpd,
    StreamStateUpd, StreamDataMoved, ConnDataBlockedUpd,
    StreamDataBlockedUpd, ConnClosed, ConnStateUpd);
  ZtEnumMap(ZquicLogEventName, JSON,
    "quic:connection_started", "quic:udp_datagrams_sent",
    "quic:udp_datagrams_received", "quic:packet_sent",
    "quic:packet_received", "quic:packet_buffered", "quic:packet_dropped",
    "quic:packets_acked", "quic:packet_lost",
    "quic:marked_for_retransmit", "quic:recovery_metrics_updated",
    "quic:timer_updated", "quic:congestion_state_updated",
    "quic:ecn_state_updated", "quic:key_updated", "quic:key_discarded",
    "quic:parameters_set", "quic:alpn_information", "zquic:tls_alert",
    "zquic:retry_sent", "zquic:retry_validated", "zquic:token_issued",
    "zquic:token_validated", "zquic:token_rejected",
    "quic:version_information", "zquic:stateless_reset",
    "zquic:packet_protection_failed", "zquic:zero_rtt_rejected",
    "quic:tuple_assigned", "quic:path_validated", "quic:mtu_updated",
    "quic:connection_id_updated", "quic:stream_state_updated",
    "quic:stream_data_moved", "quic:connection_data_blocked_updated",
    "quic:stream_data_blocked_updated", "quic:connection_closed",
    "quic:connection_state_updated");
};

#ifdef Zquic_DEBUG

class ZquicAPI ZquicLogSink {
public:
  bool init(ZuCSpan, unsigned);
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

  static void connectionStarted(ZquicLogConnectionStartedEvent event) {
    instance()->connectionStarted_(ZuMv(event));
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
  static void transportParametersSet(ZquicLogTransportParamsEvent event) {
    instance()->transportParametersSet_(ZuMv(event));
  }
  static void alpnInformation(ZquicLogSecurityEvent event) {
    instance()->alpnInformation_(ZuMv(event));
  }
  static void versionInformation(ZquicLogVersionEvent event) {
    instance()->versionInformation_(ZuMv(event));
  }
  static void tlsAlert(ZquicLogSecurityEvent event) {
    instance()->tlsAlert_(ZuMv(event));
  }
  static void securityEvent(
    ZquicLogEventName::T name, ZquicLogSecurityEvent event) {
    instance()->securityEvent_(name, ZuMv(event));
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
  static void connectionStateUpdated(ZquicLogConnectionStateEvent event) {
    instance()->connectionStateUpdated_(ZuMv(event));
  }
  template <typename L>
  static void log(L l) {
    instance()->log_(ZuMv(l));
  }

  void logConnectionStarted(
    const ZquicLogConnectionStartedEvent &event, ZuTime time) {
    writeConnectionStartedEvent_(event, time);
  }
  void logDatagramSent(const ZquicLogDatagramEvent &event, ZuTime time) {
    writeDatagramEvent_(ZquicLogEventName::UDPTx, event, time);
  }
  void logDatagramReceived(const ZquicLogDatagramEvent &event, ZuTime time) {
    writeDatagramEvent_(ZquicLogEventName::UDPRx, event, time);
  }
  void logPacketSent(const ZquicLogPacketEvent &event, ZuTime time) {
    writePacketEvent_(ZquicLogEventName::PktSent, event, time);
  }
  void logPacketReceived(const ZquicLogPacketEvent &event, ZuTime time) {
    writePacketEvent_(ZquicLogEventName::PktRecv, event, time);
  }
  void logPacketBuffered(const ZquicLogPacketEvent &event, ZuTime time) {
    writePacketEvent_(ZquicLogEventName::PktBuf, event, time);
  }
  void logPacketDropped(const ZquicLogPacketEvent &event, ZuTime time) {
    writePacketEvent_(ZquicLogEventName::PktDrop, event, time);
  }
  void logPacketsAcked(const ZquicLogAckEvent &event, ZuTime time) {
    writeAckEvent_(ZquicLogEventName::PktsAcked, event, time);
  }
  void logPacketLost(const ZquicLogRecoveryEvent &event, ZuTime time) {
    writePacketLostEvent_(event, time);
  }
  void logRecoveryPacketLost(const ZquicLogRecoveryEvent &event, ZuTime time) {
    writePacketLostEvent_(event, time);
  }
  void logMarkedForRetransmit(
    const ZquicLogRecoveryEvent &event, ZuTime time) {
    writeMarkedForRetransmitEvent_(event, time);
  }
  void logMetricsUpdated(const ZquicLogRecoveryEvent &event, ZuTime time) {
    writeRecoveryMetricsEvent_(event, time);
  }
  void logLossTimerUpdated(const ZquicLogRecoveryEvent &event, ZuTime time) {
    writeTimerEvent_(event, time);
  }
  void logCongestionStateUpdated(
    const ZquicLogRecoveryEvent &event, ZuTime time) {
    writeCongestionStateEvent_(event, time);
  }
  void logECNStateUpdated(const ZquicLogECNEvent &event, ZuTime time) {
    writeECNEvent_(event, time);
  }
  void logKeyUpdated(const ZquicLogSecurityEvent &event, ZuTime time) {
    writeKeyEvent_(ZquicLogEventName::KeyUpd, event, time);
  }
  void logKeyRetired(const ZquicLogSecurityEvent &event, ZuTime time) {
    writeKeyEvent_(ZquicLogEventName::KeyDiscarded, event, time);
  }
  void logTransportParametersSet(
    const ZquicLogTransportParamsEvent &event, ZuTime time) {
    writeTransportParamsEvent_(event, time);
  }
  void logALPNInformation(const ZquicLogSecurityEvent &event, ZuTime time) {
    writeALPNEvent_(event, time);
  }
  void logVersionInformation(
    const ZquicLogVersionEvent &event, ZuTime time) {
    writeVersionEvent_(event, time);
  }
  void logTLSAlert(const ZquicLogSecurityEvent &event, ZuTime time) {
    writeSecurityEvent_(ZquicLogEventName::TLSAlert, event, time);
  }
  void logSecurityEvent(
    ZquicLogEventName::T name,
    const ZquicLogSecurityEvent &event, ZuTime time) {
    writeSecurityEvent_(name, event, time);
  }
  void logPathUpdated(const ZquicLogPathEvent &event, ZuTime time) {
    writePathEvent_(ZquicLogEventName::TupleAssigned, event, time);
  }
  void logPathValidationUpdated(
    const ZquicLogPathEvent &event, ZuTime time) {
    writePathValidationEvent_(event, time);
  }
  void logPMTUDUpdated(const ZquicLogPathEvent &event, ZuTime time) {
    writeMTUEvent_(event, time);
  }
  void logCIDUpdated(const ZquicLogCIDEvent &event, ZuTime time) {
    writeCIDEvent_(ZquicLogEventName::CIDUpd, event, time);
  }
  void logStreamStateUpdated(
    const ZquicLogStreamEvent &event, ZuTime time) {
    writeStreamEvent_(ZquicLogEventName::StreamStateUpd, event, time);
  }
  void logStreamDataMoved(
    const ZquicLogStreamDataEvent &event, ZuTime time) {
    writeStreamDataEvent_(ZquicLogEventName::StreamDataMoved, event, time);
  }
  void logConnectionDataBlockedUpdated(
    const ZquicLogBlockedEvent &event, ZuTime time) {
    writeConnectionBlockedEvent_(
      ZquicLogEventName::ConnDataBlockedUpd, event, time);
  }
  void logStreamDataBlockedUpdated(
    const ZquicLogBlockedEvent &event, ZuTime time) {
    writeStreamBlockedEvent_(
      ZquicLogEventName::StreamDataBlockedUpd, event, time);
  }
  void logConnectionClosed(
    const ZquicLogCloseEvent &event, ZuTime time) {
    writeCloseEvent_(ZquicLogEventName::ConnClosed, event, time);
  }
  void logConnectionStateUpdated(
    const ZquicLogConnectionStateEvent &event, ZuTime time) {
    writeConnectionStateEvent_(ZquicLogEventName::ConnStateUpd, event, time);
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
  void connectionStarted_(ZquicLogConnectionStartedEvent);
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
  void transportParametersSet_(ZquicLogTransportParamsEvent);
  void alpnInformation_(ZquicLogSecurityEvent);
  void versionInformation_(ZquicLogVersionEvent);
  void tlsAlert_(ZquicLogSecurityEvent);
  void securityEvent_(ZquicLogEventName::T, ZquicLogSecurityEvent);
  void pathUpdated_(ZquicLogPathEvent);
  void pathValidationUpdated_(ZquicLogPathEvent);
  void pmtudUpdated_(ZquicLogPathEvent);
  void cidUpdated_(ZquicLogCIDEvent);
  void streamStateUpdated_(ZquicLogStreamEvent);
  void streamDataMoved_(ZquicLogStreamDataEvent);
  void connectionDataBlockedUpdated_(ZquicLogBlockedEvent);
  void streamDataBlockedUpdated_(ZquicLogBlockedEvent);
  void connectionClosed_(ZquicLogCloseEvent);
  void connectionStateUpdated_(ZquicLogConnectionStateEvent);
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
  bool writeHeader_();
  bool writeConnectionStartedEvent_(
    const ZquicLogConnectionStartedEvent &, ZuTime);
  bool writeDatagramEvent_(
    ZquicLogEventName::T, const ZquicLogDatagramEvent &, ZuTime);
  bool writePacketEvent_(
    ZquicLogEventName::T, const ZquicLogPacketEvent &, ZuTime);
  bool writeAckEvent_(
    ZquicLogEventName::T, const ZquicLogAckEvent &, ZuTime);
  bool writePacketLostEvent_(const ZquicLogRecoveryEvent &, ZuTime);
  bool writeMarkedForRetransmitEvent_(const ZquicLogRecoveryEvent &, ZuTime);
  bool writeRecoveryMetricsEvent_(const ZquicLogRecoveryEvent &, ZuTime);
  bool writeCongestionStateEvent_(const ZquicLogRecoveryEvent &, ZuTime);
  bool writeTimerEvent_(const ZquicLogRecoveryEvent &, ZuTime);
  bool writeECNEvent_(const ZquicLogECNEvent &, ZuTime);
  bool writeKeyEvent_(
    ZquicLogEventName::T, const ZquicLogSecurityEvent &, ZuTime);
  bool writeTransportParamsEvent_(
    const ZquicLogTransportParamsEvent &, ZuTime);
  bool writeALPNEvent_(const ZquicLogSecurityEvent &, ZuTime);
  bool writeVersionEvent_(const ZquicLogVersionEvent &, ZuTime);
  bool writeSecurityEvent_(
    ZquicLogEventName::T, const ZquicLogSecurityEvent &, ZuTime);
  bool writePathEvent_(
    ZquicLogEventName::T, const ZquicLogPathEvent &, ZuTime);
  bool writeMTUEvent_(const ZquicLogPathEvent &, ZuTime);
  bool writePathValidationEvent_(const ZquicLogPathEvent &, ZuTime);
  bool writeCIDEvent_(ZquicLogEventName::T, const ZquicLogCIDEvent &, ZuTime);
  bool writeStreamEvent_(
    ZquicLogEventName::T, const ZquicLogStreamEvent &, ZuTime);
  bool writeStreamDataEvent_(
    ZquicLogEventName::T, const ZquicLogStreamDataEvent &, ZuTime);
  bool writeConnectionBlockedEvent_(
    ZquicLogEventName::T, const ZquicLogBlockedEvent &, ZuTime);
  bool writeStreamBlockedEvent_(
    ZquicLogEventName::T, const ZquicLogBlockedEvent &, ZuTime);
  bool writeCloseEvent_(
    ZquicLogEventName::T, const ZquicLogCloseEvent &, ZuTime);
  bool writeConnectionStateEvent_(
    ZquicLogEventName::T, const ZquicLogConnectionStateEvent &, ZuTime);

private:
  ZmAtomic<int>		m_enabled = 0;
  bool			m_configured = false;
  bool			m_started = false;
  bool			m_headerWritten = false;
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
  static void connectionStarted(ZquicLogConnectionStartedEvent) { }
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
  static void transportParametersSet(ZquicLogTransportParamsEvent) { }
  static void alpnInformation(ZquicLogSecurityEvent) { }
  static void versionInformation(ZquicLogVersionEvent) { }
  static void tlsAlert(ZquicLogSecurityEvent) { }
  static void securityEvent(ZquicLogEventName::T, ZquicLogSecurityEvent) { }
  static void pathUpdated(ZquicLogPathEvent) { }
  static void pathValidationUpdated(ZquicLogPathEvent) { }
  static void pmtudUpdated(ZquicLogPathEvent) { }
  static void cidUpdated(ZquicLogCIDEvent) { }
  static void streamStateUpdated(ZquicLogStreamEvent) { }
  static void streamDataMoved(ZquicLogStreamDataEvent) { }
  static void connectionDataBlockedUpdated(ZquicLogBlockedEvent) { }
  static void streamDataBlockedUpdated(ZquicLogBlockedEvent) { }
  static void connectionClosed(ZquicLogCloseEvent) { }
  static void connectionStateUpdated(ZquicLogConnectionStateEvent) { }
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
