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

namespace ZquicLog_ {

enum {
  FrameMax = 8,
  AckRangeMax = 16,
  AckPacketMax = 64,
  VersionMax = 8
};

ZuDerive(VersionArray,
  (ZuArray<uint32_t, VersionMax>));

struct QAckRange {
  uint64_t	first = 0;
  uint64_t	largest = 0;

  struct Traits : public ZuBaseTraits<QAckRange> {
    enum { IsPOD = 1 };
  };
  friend Traits ZuTraitsType(QAckRange *);
};

struct DgramEvent {
	uint64_t	size = 0;
	Zquic::EcnMark::T ecn = Zquic::EcnMark::N;
	Zquic::LinkInfo linkInfo;
};

struct StreamType {
  ZtEnum(StreamType, int8_t, Duplex, Simplex);
  ZtEnumMap(StreamType, JSON, "bidirectional", "unidirectional");
};

struct FrameEvent {
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
  ZuArray<QAckRange, AckRangeMax> ackRanges;
  StreamType::T streamType = StreamType::Duplex;
  Zquic::CxnID	cxnID;
  Zquic::ResetToken resetToken;
  uint8_t	rangeCount = 0;
  bool		fin = false;

  struct Traits : public ZuBaseTraits<FrameEvent> {
    enum { IsPOD = 1 };
  };
  friend Traits ZuTraitsType(FrameEvent *);
};

struct PktEvent {
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
      ParseShort, BadKeyPhase, AntiAmp, ProbeAdmit,
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
  ZuArray<FrameEvent, FrameMax> frames;
  bool		ackEliciting = false;
  Zquic::LinkInfo linkInfo;
};

struct AckEvent {
	Zquic::PktNumSpace::T packetSpace = Zquic::PktNumSpace::Initial;
  uint64_t	largestAcked = 0;
  uint64_t	ackDelayUS = 0;
  uint64_t	ackedBytes = 0;
  uint64_t	lostBytes = 0;
  uint8_t	rangeCount = 0;
  uint8_t	ackedFrames = 0;
  uint8_t	lostFrames = 0;
  bool		packetNumbersTruncated = false;
  ZuArray<uint64_t, AckPacketMax> packetNumbers;
  Zquic::LinkInfo linkInfo;
};

struct RecKind {
  ZtEnum(RecKind, int8_t,
    Aggregate, Loss, PTO, RTT, NewReno);
  ZtEnumMap(RecKind, JSON,
    "aggregate", "loss", "pto", "rtt", "newreno");
};

struct RecReason {
  ZtEnum(RecReason, int8_t,
    None, TimeThreshold, PacketThreshold, Canceled, Armed, NoLevel, Ack,
    PMTUDAck, Loss, PMTUDLoss, Expired, Probe, Backoff);
  ZtEnumMap(RecReason, JSON,
    "", "time_threshold", "packet_threshold", "canceled", "armed",
    "no_level", "ack", "pmtud_ack", "loss", "pmtud_loss",
    "expired", "probe", "backoff");
};

struct RecEvent {
	RecKind::T kind = RecKind::Aggregate;
	Zquic::PktNumSpace::T packetSpace = Zquic::PktNumSpace::Initial;
  RecReason::T reason = RecReason::None;
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
  ZuArray<FrameEvent, FrameMax> frames;
  Zquic::LinkInfo linkInfo;
};

struct ECNState {
  ZtEnum(ECNState, int8_t, Unknown, Capable, Failed);
  ZtEnumMap(ECNState, JSON, "unknown", "capable", "failed");
};

struct ECNReason {
  ZtEnum(ECNReason, int8_t,
    AckECN, ECT0Decrease, ECT1Decrease, CEDecrease, ECTOverflow, CEOverflow,
    CounterExceedsAck);
  ZtEnumMap(ECNReason, JSON,
    "ack_ecn", "ect0_decrease", "ect1_decrease", "ce_decrease",
    "ect_overflow", "ce_overflow", "counter_exceeds_ack");
};

struct ECNEvent {
	Zquic::PktNumSpace::T packetSpace = Zquic::PktNumSpace::Initial;
  ECNState::T state = ECNState::Capable;
  ECNReason::T reason = ECNReason::AckECN;
  uint64_t	ect0 = 0;
  uint64_t	ect1 = 0;
  uint64_t	ce = 0;
  uint64_t	previousECT0 = 0;
  uint64_t	previousECT1 = 0;
  uint64_t	previousCE = 0;
  uint64_t	largestAcked = 0;
  bool		disabled = false;
  Zquic::LinkInfo linkInfo;
};

struct SecKind {
  ZtEnum(SecKind, int8_t,
    KeyUpdated, KeyRetired, TransportParams, ALPN, TLS, Retry, Token,
    VersionNeg, StatelessRst, PktProtect);
  ZtEnumMap(SecKind, JSON,
    "key_updated", "key_retired", "transport_parameters", "alpn", "tls",
    "retry", "token", "version_negotiation", "stateless_reset",
    "packet_protection");
};

struct SecKeyType {
  ZtEnum(SecKeyType, int8_t, None, RX, RXOld, TX);
  ZtEnumMap(SecKeyType, JSON, "", "rx", "rx_old", "tx");
};

struct KeyType {
  ZtEnum(KeyType, int8_t,
    ServerInit, ClientInit, ServerHS, ClientHS,
    Server0RTT, Client0RTT, Server1RTT, Client1RTT);
  ZtEnumMap(KeyType, JSON,
    "server_initial_secret", "client_initial_secret",
    "server_handshake_secret", "client_handshake_secret",
    "server_0rtt_secret", "client_0rtt_secret",
    "server_1rtt_secret", "client_1rtt_secret");
};

struct SecTrigger {
  ZtEnum(SecTrigger, int8_t,
    None, Sent, Received, Validated, Local, Remote, Peer, Selected, Timer,
    HSComplete, RX, TX);
  ZtEnumMap(SecTrigger, JSON,
    "", "sent", "received", "validated", "local", "remote", "peer",
    "selected", "timer", "handshake_complete", "rx", "tx");
};

struct KeyTrigger {
  ZtEnum(KeyTrigger, int8_t, TLS, RemoteUpdate, LocalUpdate);
  ZtEnumMap(KeyTrigger, JSON,
    "tls", "remote_update", "local_update");
};

struct Initiator {
  ZtEnum(Initiator, int8_t, Local, Remote);
  ZtEnumMap(Initiator, JSON, "local", "remote");
};

struct SecReason {
  ZtEnum(SecReason, int8_t,
    None, Unknown, OK, Handshake, KeyPhase, KeyUpdate, PeerUpdate,
    PacketSpace, AddrValid, MissingToken, NewToken, NewTokenPolicy,
    UnsupVersion, UnknownCID, TokenMatch, Validation, RetrySCID, Expired,
    Auth, Address, Malformed, Kind, ODCID, Protect, MissingKeys, Protection,
    BadKeyPhase, ZeroRTT);
  ZtEnumMap(SecReason, JSON,
    "", "unknown", "ok", "handshake", "key_phase", "key_update",
    "peer_update", "packet_space", "address_validation", "missing_token",
    "new_token", "new_token_policy", "unsupported_version", "unknown_cid",
    "token_match", "validation", "retry_scid", "expired", "auth", "address",
    "malformed", "kind", "odcid", "protect", "missing_keys", "protection",
    "invalid_key_phase", "0rtt");
};

struct SecEvent {
	SecKind::T kind = SecKind::TLS;
	Zquic::PktNumSpace::T packetSpace = Zquic::PktNumSpace::Initial;
  SecKeyType::T keyType = SecKeyType::None;
  SecTrigger::T trigger = SecTrigger::None;
  ZeString	alpn;
  SecReason::T reason = SecReason::None;
  uint64_t	value = 0;
  bool		success = true;
  Zquic::LinkInfo linkInfo;
};

struct ParamsEvent {
  Initiator::T initiator = Initiator::Local;
  Zquic::CxnID	origDCID;
  Zquic::CxnID	initialSCID;
  Zquic::CxnID	retrySCID;
  Zquic::ResetToken statelessResetToken;
  uint64_t	maxIdleTimeout = 0;
  uint64_t	maxUDPPayloadSize = 0;
  uint64_t	ackDelayExponent = 0;
  uint64_t	maxAckDelay = 0;
  uint64_t	activeCxnIDLimit = 0;
  uint64_t	initialMaxData = 0;
  uint64_t	initialMaxStreamDataBidiLocal = 0;
  uint64_t	initialMaxStreamDataBidiRemote = 0;
  uint64_t	initialMaxStreamDataUni = 0;
  uint64_t	initialMaxStreamsBidi = 0;
  uint64_t	initialMaxStreamsUni = 0;
  bool		statelessResetTokenPresent = false;
  bool		disableActiveMigration = false;
  Zquic::LinkInfo linkInfo;
};

struct VersionEvent {
  VersionArray serverVersions;
  VersionArray clientVersions;
  uint32_t	chosenVersion = 0;
  bool		chosenVersionPresent = false;
  Zquic::LinkInfo linkInfo;
};

struct PathKind {
  ZtEnum(PathKind, int8_t, Path, PathValid, PMTUD);
  ZtEnumMap(PathKind, JSON,
    "path", "path_validation", "pmtud");
};

struct PathAction {
  ZtEnum(PathAction, int8_t,
    Created, Validated, Received, Observed, ChallengeTx, ResponseUnk,
    ResponseRx, Updated, Failed, Blocked, Expired, Hint, Acked, Lost,
    Sent);
  ZtEnumMap(PathAction, JSON,
    "created", "validated", "received", "observed", "challenge_sent",
    "response_unknown", "response_received", "updated", "failed", "blocked",
    "expired", "hint", "acked", "lost", "sent");
};

struct PathReason {
  ZtEnum(PathReason, int8_t,
    None, Client, Server, Initial, Datagram, PeerAddrChange, Mismatch,
    Matched, Response, Promoted, Timeout, AntiAmp, ProbeAdmit,
    PathHint, Probe, Admission, SendFail);
  ZtEnumMap(PathReason, JSON,
    "", "client", "server", "initial", "datagram", "peer_address_change",
    "mismatch", "matched", "response", "promoted", "timeout",
    "anti_amplification", "probe_admission", "path_hint", "probe",
    "admission", "send_failure");
};

struct PathEvent {
  PathKind::T kind = PathKind::Path;
  PathAction::T action = PathAction::Updated;
  PathReason::T reason = PathReason::None;
  uint64_t	tupleID = 0;
  uint64_t	bytes = 0;
  uint64_t	antiAmplification = 0;
  uint64_t	deadlineUS = 0;
  uint32_t	mtu = 0;
  bool		validated = false;
  Zquic::LinkInfo linkInfo;
};

struct CIDKind {
  ZtEnum(CIDKind, int8_t, CxnID);
  ZtEnumMap(CIDKind, JSON, "connection_id");
};

struct CIDAction {
  ZtEnum(CIDAction, int8_t,
    Issued, Updated, Retired, RouteBound, Tombstone);
  ZtEnumMap(CIDAction, JSON,
    "issued", "updated", "retired", "route_bound", "tombstone");
};

struct CIDReason {
  ZtEnum(CIDReason, int8_t,
    None, PeerRequest, PathPromoted, RouteInstall, RouteRetire,
    RouteTombstone, Sequence, ID, ResetToken, RetirePrior);
  ZtEnumMap(CIDReason, JSON,
    "", "peer_request", "path_promoted", "route_install", "route_retire",
    "route_tombstone", "sequence", "id", "reset_token", "retire_prior_to");
};

struct CIDEvent {
  CIDKind::T kind = CIDKind::CxnID;
  CIDAction::T action = CIDAction::Updated;
  CIDReason::T reason = CIDReason::None;
  Zquic::CxnID	cxnID;
  uint64_t	sequence = 0;
  uint8_t	length = 0;
  bool		local = false;
  bool		associated = false;
  bool		resetToken = false;
  Zquic::LinkInfo linkInfo;
};

struct StreamState {
  ZtEnum(StreamState, int8_t, Idle, Open, Closed);
  ZtEnumMap(StreamState, JSON, "idle", "open", "closed");
};

struct StreamSide {
  ZtEnum(StreamSide, int8_t, Sending, Receiving);
  ZtEnumMap(StreamSide, JSON, "sending", "receiving");
};

struct StreamReason {
  ZtEnum(StreamReason, int8_t, None, LocalOpen, PeerOpen, Reaped);
  ZtEnumMap(StreamReason, JSON,
    "", "local_open", "peer_open", "reaped");
};

struct StreamEvent {
  StreamType::T streamType = StreamType::Duplex;
  StreamState::T oldState = StreamState::Idle;
  StreamState::T newState = StreamState::Open;
  StreamSide::T streamSide = StreamSide::Sending;
  StreamReason::T reason = StreamReason::None;
  uint64_t	streamID = 0;
  uint64_t	offset = 0;
  uint64_t	length = 0;
  uint64_t	errorCode = 0;
  bool		fin = false;
  Zquic::LinkInfo linkInfo;
};

struct StreamDataLoc {
  ZtEnum(StreamDataLoc, int8_t, Application, Transport, Network);
  ZtEnumMap(StreamDataLoc, JSON,
    "application", "transport", "network");
};

struct StreamDataInfo {
  ZtEnum(StreamDataInfo, int8_t, None, FinSet);
  ZtEnumMap(StreamDataInfo, JSON, "", "fin_set");
};

struct StreamDataEvent {
  StreamDataLoc::T from = StreamDataLoc::Transport;
  StreamDataLoc::T to = StreamDataLoc::Application;
  StreamDataInfo::T additionalInfo = StreamDataInfo::None;
  uint64_t	streamID = 0;
  uint64_t	offset = 0;
  uint64_t	length = 0;
  Zquic::LinkInfo linkInfo;
};

struct BlockedState {
  ZtEnum(BlockedState, int8_t, Blocked, Unblocked);
  ZtEnumMap(BlockedState, JSON, "blocked", "unblocked");
};

struct BlockedReason {
  ZtEnum(BlockedReason, int8_t,
    Scheduling, Pacing, AmpProtect, CongCtrl,
    CxnFlowCtrl, StreamFlowCtrl, StreamID, Application);
  ZtEnumMap(BlockedReason, JSON,
    "scheduling", "pacing", "amplification_protection",
    "congestion_control", "connection_flow_control", "stream_flow_control",
    "stream_id", "application");
};

struct BlockedEvent {
  BlockedState::T oldState = BlockedState::Unblocked;
  BlockedState::T newState = BlockedState::Blocked;
  BlockedReason::T reason =
    BlockedReason::CxnFlowCtrl;
  uint64_t	streamID = 0;
  Zquic::LinkInfo linkInfo;
};

struct CloseInitiator {
  ZtEnum(CloseInitiator, int8_t, Local, Remote);
  ZtEnumMap(CloseInitiator, JSON, "local", "remote");
};

struct CloseTrigger {
  ZtEnum(CloseTrigger, int8_t,
    Application, Error, IdleTimeout, Aborted);
  ZtEnumMap(CloseTrigger, JSON,
    "application", "error", "idle_timeout", "aborted");
};

struct CloseReason {
  ZtEnum(CloseReason, int8_t,
    None, LocalClose, PeerCloseFrame, DrainExpired, Idle);
  ZtEnumMap(CloseReason, JSON,
    "", "local_close", "peer_close_frame", "drain_expired", "idle");
};

struct CloseError {
  ZtEnum(CloseError, int8_t,
    NoError, InternalError, CxnRefused, FlowControl,
    StreamLimit, StreamState, FinalSize, FrameEncoding, TransportParam,
    CxnIDLimit, ProtViolation, None, Unknown);
  ZtEnumMap(CloseError, JSON,
    "no_error", "internal_error", "connection_refused",
    "flow_control_error", "stream_limit_error", "stream_state_error",
    "final_size_error", "frame_encoding_error", "transport_parameter_error",
    "connection_id_limit_error", "protocol_violation", "", "unknown");
};

struct CloseEvent {
	CloseInitiator::T initiator = CloseInitiator::Local;
	CloseTrigger::T trigger = CloseTrigger::Error;
  CloseReason::T reason = CloseReason::None;
  CloseError::T connectionError = CloseError::None;
  CloseError::T applicationError = CloseError::None;
  uint64_t	errorCode = 0;
  Zquic::LinkInfo linkInfo;
};

struct CxnStateEvent {
  Zquic::LinkState::T oldState = Zquic::LinkState::Starting;
  Zquic::LinkState::T newState = Zquic::LinkState::Handshaking;
  Zquic::LinkInfo linkInfo;
};

struct CxnStartedEvent {
  ZiSockAddr	local;
  ZiSockAddr	remote;
  Zquic::LinkInfo linkInfo;
};

struct EventName {
  ZtEnum(EventName, int8_t,
    CxnStarted, UDPTx, UDPRx, PktSent, PktRecv, PktBuf, PktDrop, PktsAcked,
    PktLost, MarkRetrans, MetricsUpd, TimerUpd, CongestionUpd, ECNUpd, KeyUpd,
    KeyDiscarded, ParamsSet, ALPNInfo, TLSAlert, RetrySent, RetryValid,
    TokenIssued, TokenValid, TokenReject, VersionInfo, StatelessRst,
    PktProtectFail, ZeroRTTReject, TupleAssigned, PathValidated, MTUUpd, CIDUpd,
    StreamStateUpd, StreamDataMoved, CxnDataBlockedUpd,
    StreamDataBlockedUpd, CxnClosed, CxnStateUpd);
  ZtEnumMap(EventName, JSON,
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

} // namespace ZquicLog_

using namespace ZquicLog_;

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
  static bool linkInfo(Zquic::Vantage::T vantage) {
    return instance()->linkInfo_(Zquic::LinkInfo{
      .vantage = vantage
    });
  }
  static bool linkInfo(const Zquic::LinkInfo &linkInfo) {
    return instance()->linkInfo_(linkInfo);
  }
  static void start() { instance()->start_(); }
  static void stop() { instance()->stop_(); }
  static void final() { instance()->final_(); }
  static ZquicLogDiag diag() { return instance()->diag_(); }

  static void cxnStarted(CxnStartedEvent event) {
    instance()->cxnStarted_(ZuMv(event));
  }
  static void dgramSent(DgramEvent event) {
    instance()->dgramSent_(ZuMv(event));
  }
  static void dgramRecv(DgramEvent event) {
    instance()->dgramRecv_(ZuMv(event));
  }
  static void pktSent(PktEvent event) {
    instance()->pktSent_(ZuMv(event));
  }
  static void pktRecv(PktEvent event) {
    instance()->pktRecv_(ZuMv(event));
  }
  static void pktBuf(PktEvent event) {
    instance()->pktBuf_(ZuMv(event));
  }
  static void pktDrop(PktEvent event) {
    instance()->pktDrop_(ZuMv(event));
  }
  static void pktsAcked(AckEvent event) {
    instance()->pktsAcked_(ZuMv(event));
  }
  static void pktLost(RecEvent event) {
    instance()->pktLost_(ZuMv(event));
  }
  static void recPktLost(RecEvent event) {
    instance()->recPktLost_(ZuMv(event));
  }
  static void markRetrans(RecEvent event) {
    instance()->markRetrans_(ZuMv(event));
  }
  static void metricsUpd(RecEvent event) {
    instance()->metricsUpd_(ZuMv(event));
  }
  static void lossTimerUpd(RecEvent event) {
    instance()->lossTimerUpd_(ZuMv(event));
  }
  static void congStateUpd(RecEvent event) {
    instance()->congStateUpd_(ZuMv(event));
  }
  static void ecnStateUpd(ECNEvent event) {
    instance()->ecnStateUpd_(ZuMv(event));
  }
  static void keyUpdated(SecEvent event) {
    instance()->keyUpdated_(ZuMv(event));
  }
  static void keyRetired(SecEvent event) {
    instance()->keyRetired_(ZuMv(event));
  }
  static void paramsSet(ParamsEvent event) {
    instance()->paramsSet_(ZuMv(event));
  }
  static void alpnInfo(SecEvent event) {
    instance()->alpnInfo_(ZuMv(event));
  }
  static void versionInfo(VersionEvent event) {
    instance()->versionInfo_(ZuMv(event));
  }
  static void tlsAlert(SecEvent event) {
    instance()->tlsAlert_(ZuMv(event));
  }
  static void secEvent(
    EventName::T name, SecEvent event) {
    instance()->secEvent_(name, ZuMv(event));
  }
  static void pathUpdated(PathEvent event) {
    instance()->pathUpdated_(ZuMv(event));
  }
  static void pathValidUpd(PathEvent event) {
    instance()->pathValidUpd_(ZuMv(event));
  }
  static void pmtudUpdated(PathEvent event) {
    instance()->pmtudUpdated_(ZuMv(event));
  }
  static void cidUpdated(CIDEvent event) {
    instance()->cidUpdated_(ZuMv(event));
  }
  static void streamStateUpd(StreamEvent event) {
    instance()->streamStateUpd_(ZuMv(event));
  }
  static void streamDataMoved(StreamDataEvent event) {
    instance()->streamDataMoved_(ZuMv(event));
  }
  static void cxnDataBlockedUpd(
    BlockedEvent event) {
    instance()->cxnDataBlockedUpd_(ZuMv(event));
  }
  static void streamDataBlockedUpd(BlockedEvent event) {
    instance()->streamDataBlockedUpd_(ZuMv(event));
  }
  static void cxnClosed(CloseEvent event) {
    instance()->cxnClosed_(ZuMv(event));
  }
  static void cxnStateUpd(CxnStateEvent event) {
    instance()->cxnStateUpd_(ZuMv(event));
  }
  template <typename L>
  static void log(L l) {
    instance()->log_(ZuMv(l));
  }

  void logCxnStarted(
    const CxnStartedEvent &event, ZuTime time) {
    writeCxnStarted_(event, time);
  }
  void logDgramSent(const DgramEvent &event, ZuTime time) {
    writeDatagramEvent_(EventName::UDPTx, event, time);
  }
  void logDgramRecv(const DgramEvent &event, ZuTime time) {
    writeDatagramEvent_(EventName::UDPRx, event, time);
  }
  void logPktSent(const PktEvent &event, ZuTime time) {
    writePktEvent_(EventName::PktSent, event, time);
  }
  void logPktRecv(const PktEvent &event, ZuTime time) {
    writePktEvent_(EventName::PktRecv, event, time);
  }
  void logPktBuf(const PktEvent &event, ZuTime time) {
    writePktEvent_(EventName::PktBuf, event, time);
  }
  void logPktDrop(const PktEvent &event, ZuTime time) {
    writePktEvent_(EventName::PktDrop, event, time);
  }
  void logPktsAcked(const AckEvent &event, ZuTime time) {
    writeAckEvent_(EventName::PktsAcked, event, time);
  }
  void logPktLost(const RecEvent &event, ZuTime time) {
    writePktLost_(event, time);
  }
  void logRecPktLost(const RecEvent &event, ZuTime time) {
    writePktLost_(event, time);
  }
  void logMarkRetrans(
    const RecEvent &event, ZuTime time) {
    writeMarkRetrans_(event, time);
  }
  void logMetricsUpd(const RecEvent &event, ZuTime time) {
    writeRecMetrics_(event, time);
  }
  void logLossTimerUpd(const RecEvent &event, ZuTime time) {
    writeTimerEvent_(event, time);
  }
  void logCongStateUpd(
    const RecEvent &event, ZuTime time) {
    writeCongState_(event, time);
  }
  void logECNStateUpd(const ECNEvent &event, ZuTime time) {
    writeECNEvent_(event, time);
  }
  void logKeyUpdated(const SecEvent &event, ZuTime time) {
    writeKeyEvent_(EventName::KeyUpd, event, time);
  }
  void logKeyRetired(const SecEvent &event, ZuTime time) {
    writeKeyEvent_(EventName::KeyDiscarded, event, time);
  }
  void logParamsSet(
    const ParamsEvent &event, ZuTime time) {
    writeParams_(event, time);
  }
  void logALPNInfo(const SecEvent &event, ZuTime time) {
    writeALPNEvent_(event, time);
  }
  void logVersionInfo(
    const VersionEvent &event, ZuTime time) {
    writeVersion_(event, time);
  }
  void logTLSAlert(const SecEvent &event, ZuTime time) {
    writeSecEvent_(EventName::TLSAlert, event, time);
  }
  void logSecEvent(
    EventName::T name,
    const SecEvent &event, ZuTime time) {
    writeSecEvent_(name, event, time);
  }
  void logPathUpdated(const PathEvent &event, ZuTime time) {
    writePathEvent_(EventName::TupleAssigned, event, time);
  }
  void logPathValid(
    const PathEvent &event, ZuTime time) {
    writePathValid_(event, time);
  }
  void logPMTUDUpdated(const PathEvent &event, ZuTime time) {
    writeMTUEvent_(event, time);
  }
  void logCIDUpdated(const CIDEvent &event, ZuTime time) {
    writeCIDEvent_(EventName::CIDUpd, event, time);
  }
  void logStreamStateUpd(
    const StreamEvent &event, ZuTime time) {
    writeStreamEvent_(EventName::StreamStateUpd, event, time);
  }
  void logStreamDataMoved(
    const StreamDataEvent &event, ZuTime time) {
    writeStreamData_(EventName::StreamDataMoved, event, time);
  }
  void logCxnDataBlockedUpd(
    const BlockedEvent &event, ZuTime time) {
    writeCxnBlocked_(
      EventName::CxnDataBlockedUpd, event, time);
  }
  void logStreamDataBlockedUpd(
    const BlockedEvent &event, ZuTime time) {
    writeStreamBlocked_(
      EventName::StreamDataBlockedUpd, event, time);
  }
  void logCxnClosed(
    const CloseEvent &event, ZuTime time) {
    writeCloseEvent_(EventName::CxnClosed, event, time);
  }
  void logCxnStateUpd(
    const CxnStateEvent &event, ZuTime time) {
    writeCxnState_(EventName::CxnStateUpd, event, time);
  }

private:
  ZquicLogger();

  bool enabled_() const { return m_enabled.load_(); }
  bool init_(const ZquicLogParams &);
  bool linkInfo_(const Zquic::LinkInfo &);
  void start_();
  void stop_();
  void final_();
  ZquicLogDiag diag_() const;
  void cxnStarted_(CxnStartedEvent);
  void dgramSent_(DgramEvent);
  void dgramRecv_(DgramEvent);
  void pktSent_(PktEvent);
  void pktRecv_(PktEvent);
  void pktBuf_(PktEvent);
  void pktDrop_(PktEvent);
  void pktsAcked_(AckEvent);
  void pktLost_(RecEvent);
  void recPktLost_(RecEvent);
  void markRetrans_(RecEvent);
  void metricsUpd_(RecEvent);
  void lossTimerUpd_(RecEvent);
  void congStateUpd_(RecEvent);
  void ecnStateUpd_(ECNEvent);
  void keyUpdated_(SecEvent);
  void keyRetired_(SecEvent);
  void paramsSet_(ParamsEvent);
  void alpnInfo_(SecEvent);
  void versionInfo_(VersionEvent);
  void tlsAlert_(SecEvent);
  void secEvent_(EventName::T, SecEvent);
  void pathUpdated_(PathEvent);
  void pathValidUpd_(PathEvent);
  void pmtudUpdated_(PathEvent);
  void cidUpdated_(CIDEvent);
  void streamStateUpd_(StreamEvent);
  void streamDataMoved_(StreamDataEvent);
  void cxnDataBlockedUpd_(BlockedEvent);
  void streamDataBlockedUpd_(BlockedEvent);
  void cxnClosed_(CloseEvent);
  void cxnStateUpd_(CxnStateEvent);
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
  bool writeCxnStarted_(
    const CxnStartedEvent &, ZuTime);
  bool writeDatagramEvent_(
    EventName::T, const DgramEvent &, ZuTime);
  bool writePktEvent_(
    EventName::T, const PktEvent &, ZuTime);
  bool writeAckEvent_(
    EventName::T, const AckEvent &, ZuTime);
  bool writePktLost_(const RecEvent &, ZuTime);
  bool writeMarkRetrans_(const RecEvent &, ZuTime);
  bool writeRecMetrics_(const RecEvent &, ZuTime);
  bool writeCongState_(const RecEvent &, ZuTime);
  bool writeTimerEvent_(const RecEvent &, ZuTime);
  bool writeECNEvent_(const ECNEvent &, ZuTime);
  bool writeKeyEvent_(
    EventName::T, const SecEvent &, ZuTime);
  bool writeParams_(
    const ParamsEvent &, ZuTime);
  bool writeALPNEvent_(const SecEvent &, ZuTime);
  bool writeVersion_(const VersionEvent &, ZuTime);
  bool writeSecEvent_(
    EventName::T, const SecEvent &, ZuTime);
  bool writePathEvent_(
    EventName::T, const PathEvent &, ZuTime);
  bool writeMTUEvent_(const PathEvent &, ZuTime);
  bool writePathValid_(const PathEvent &, ZuTime);
  bool writeCIDEvent_(EventName::T, const CIDEvent &, ZuTime);
  bool writeStreamEvent_(
    EventName::T, const StreamEvent &, ZuTime);
  bool writeStreamData_(
    EventName::T, const StreamDataEvent &, ZuTime);
  bool writeCxnBlocked_(
    EventName::T, const BlockedEvent &, ZuTime);
  bool writeStreamBlocked_(
    EventName::T, const BlockedEvent &, ZuTime);
  bool writeCloseEvent_(
    EventName::T, const CloseEvent &, ZuTime);
  bool writeCxnState_(
    EventName::T, const CxnStateEvent &, ZuTime);

private:
  ZmAtomic<int>		m_enabled = 0;
  bool			m_configured = false;
  bool			m_started = false;
  bool			m_headerWritten = false;
  ZquicLogParams		m_params;
  Zquic::Vantage::T	m_vantage = Zquic::Vantage::Unknown;
  ZtBArray<>		m_origDCID;
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
  static bool linkInfo(Zquic::Vantage::T) { return true; }
  static bool linkInfo(const Zquic::LinkInfo &) { return true; }
  static void start() { }
  static void stop() { }
  static void final() { }
  static ZquicLogDiag diag() { return {}; }
  static void cxnStarted(CxnStartedEvent) { }
  static void dgramSent(DgramEvent) { }
  static void dgramRecv(DgramEvent) { }
  static void pktSent(PktEvent) { }
  static void pktRecv(PktEvent) { }
  static void pktBuf(PktEvent) { }
  static void pktDrop(PktEvent) { }
  static void pktsAcked(AckEvent) { }
  static void pktLost(RecEvent) { }
  static void recPktLost(RecEvent) { }
  static void markRetrans(RecEvent) { }
  static void metricsUpd(RecEvent) { }
  static void lossTimerUpd(RecEvent) { }
  static void congStateUpd(RecEvent) { }
  static void ecnStateUpd(ECNEvent) { }
  static void keyUpdated(SecEvent) { }
  static void keyRetired(SecEvent) { }
  static void paramsSet(ParamsEvent) { }
  static void alpnInfo(SecEvent) { }
  static void versionInfo(VersionEvent) { }
  static void tlsAlert(SecEvent) { }
  static void secEvent(EventName::T, SecEvent) { }
  static void pathUpdated(PathEvent) { }
  static void pathValidUpd(PathEvent) { }
  static void pmtudUpdated(PathEvent) { }
  static void cidUpdated(CIDEvent) { }
  static void streamStateUpd(StreamEvent) { }
  static void streamDataMoved(StreamDataEvent) { }
  static void cxnDataBlockedUpd(BlockedEvent) { }
  static void streamDataBlockedUpd(BlockedEvent) { }
  static void cxnClosed(CloseEvent) { }
  static void cxnStateUpd(CxnStateEvent) { }
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
