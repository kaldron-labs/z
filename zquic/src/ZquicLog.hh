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
#include <zlib/ZmQueue.hh>
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

namespace Zquic_ {
using namespace Zquic;
}

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

using namespace Zquic_;

enum {
  FrameMax = 8,
  AckRangeMax = 16,
  AckPacketMax = 64,
  VersionMax = 8
};

ZuDerive(Versions, (ZuArray<uint32_t, VersionMax>));

struct QAckRange {
  uint64_t	first = 0;
  uint64_t	largest = 0;

  struct Traits : public ZuBaseTraits<QAckRange> {
    enum { IsPOD = 1 };
  };
  friend Traits ZuTraitsType(QAckRange *);
};

ZuDerive(QAckRanges, (ZuArray<QAckRange, AckRangeMax>));

struct DgramEvent {
  uint64_t		size = 0;
  EcnMark::T		ecn = EcnMark::N;
  LinkInfo		linkInfo;
};

struct StreamType {
  ZtEnum(StreamType, int8_t, Duplex, Simplex);
  ZtEnumMap(StreamType, JSON, "bidirectional", "unidirectional");
};

struct FrameEvent {
  FrameType::T		type = FrameType::Unknown;
  uint64_t		streamID = 0;
  uint64_t		offset = 0;
  uint64_t		length = 0;
  uint64_t		value = 0;
  uint64_t		errorCode = 0;
  uint64_t		largestAcked = 0;
  uint64_t		ackDelayUS = 0;
  uint64_t		ect0 = 0;
  uint64_t		ect1 = 0;
  uint64_t		ce = 0;
  QAckRanges		ackRanges;
  StreamType::T		streamType = StreamType::Duplex;
  CxnID			cxnID;
  ResetToken		resetToken;
  uint8_t		rangeCount = 0;
  bool			fin = false;

  struct Traits : public ZuBaseTraits<FrameEvent> {
    enum { IsPOD = 1 };
  };
  friend Traits ZuTraitsType(FrameEvent *);
};

struct PktEvent {
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
  using Frames = ZuArray<FrameEvent, FrameMax>;

  PktType::T		packetType = PktType::Initial;
  PktNumSpace::T	packetSpace = PktNumSpace::Initial;
  uint64_t		packetNumber = 0;
  uint64_t		packetSize = 0;
  uint64_t		payloadSize = 0;
  EcnMark::T		ecn = EcnMark::N;
  Reason::T		reason = Reason::None;
  uint64_t		bytesInFlight = 0;
  uint8_t		frameCount = 0;
  bool			framesTruncated = false;
  Frames		frames;
  bool			ackEliciting = false;
  LinkInfo		linkInfo;
};

struct AckEvent {
  using PacketNumbers = ZuArray<uint64_t, AckPacketMax>;

  PktNumSpace::T	packetSpace = PktNumSpace::Initial;
  uint64_t		largestAcked = 0;
  uint64_t		ackDelayUS = 0;
  uint64_t		ackedBytes = 0;
  uint64_t		lostBytes = 0;
  uint8_t		rangeCount = 0;
  uint8_t		ackedFrames = 0;
  uint8_t		lostFrames = 0;
  bool			packetNumbersTruncated = false;
  PacketNumbers		packetNumbers;
  LinkInfo		linkInfo;
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
  using Frames = ZuArray<FrameEvent, FrameMax>;

  RecKind::T		kind = RecKind::Aggregate;
  PktNumSpace::T	packetSpace = PktNumSpace::Initial;
  RecReason::T		reason = RecReason::None;
  uint64_t		value = 0;
  uint64_t		packetNumber = 0;
  uint64_t		bytes = 0;
  uint64_t		deadlineUS = 0;
  uint64_t		latestRTTUS = 0;
  uint64_t		smoothedRTTUS = 0;
  uint64_t		rttVarianceUS = 0;
  uint64_t		minRTTUS = 0;
  uint64_t		cwnd = 0;
  uint64_t		ssthresh = 0;
  uint64_t		bytesInFlight = 0;
  uint8_t		frameCount = 0;
  Frames		frames;
  LinkInfo		linkInfo;
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
  PktNumSpace::T	packetSpace = PktNumSpace::Initial;
  ECNState::T		state = ECNState::Capable;
  ECNReason::T		reason = ECNReason::AckECN;
  uint64_t		ect0 = 0;
  uint64_t		ect1 = 0;
  uint64_t		ce = 0;
  uint64_t		previousECT0 = 0;
  uint64_t		previousECT1 = 0;
  uint64_t		previousCE = 0;
  uint64_t		largestAcked = 0;
  bool			disabled = false;
  LinkInfo		linkInfo;
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
  SecKind::T		kind = SecKind::TLS;
  PktNumSpace::T	packetSpace = PktNumSpace::Initial;
  SecKeyType::T		keyType = SecKeyType::None;
  SecTrigger::T		trigger = SecTrigger::None;
  ZeString		alpn;
  SecReason::T		reason = SecReason::None;
  uint64_t		value = 0;
  bool			success = true;
  LinkInfo		linkInfo;
};

struct ParamsEvent {
  Initiator::T		initiator = Initiator::Local;
  CxnID			origDCID;
  CxnID			initialSCID;
  CxnID			retrySCID;
  ResetToken		statelessResetToken;
  uint64_t		maxIdleTimeout = 0;
  uint64_t		maxUDPPayloadSize = 0;
  uint64_t		ackDelayExponent = 0;
  uint64_t		maxAckDelay = 0;
  uint64_t		activeCxnIDLimit = 0;
  uint64_t		initialMaxData = 0;
  uint64_t		initialMaxStreamDataBidiLocal = 0;
  uint64_t		initialMaxStreamDataBidiRemote = 0;
  uint64_t		initialMaxStreamDataUni = 0;
  uint64_t		initialMaxStreamsBidi = 0;
  uint64_t		initialMaxStreamsUni = 0;
  bool			statelessResetTokenPresent = false;
  bool			disableActiveMigration = false;
  LinkInfo		linkInfo;
};

struct VersionEvent {
  Versions		serverVersions;
  Versions		clientVersions;
  uint32_t		chosenVersion = 0;
  bool			chosenVersionPresent = false;
  LinkInfo		linkInfo;
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
  PathKind::T		kind = PathKind::Path;
  PathAction::T		action = PathAction::Updated;
  PathReason::T		reason = PathReason::None;
  uint64_t		tupleID = 0;
  uint64_t		bytes = 0;
  uint64_t		antiAmplification = 0;
  uint64_t		deadlineUS = 0;
  uint32_t		mtu = 0;
  bool			validated = false;
  LinkInfo		linkInfo;
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
  CIDKind::T		kind = CIDKind::CxnID;
  CIDAction::T		action = CIDAction::Updated;
  CIDReason::T		reason = CIDReason::None;
  CxnID			cxnID;
  uint64_t		sequence = 0;
  uint8_t		length = 0;
  bool			local = false;
  bool			associated = false;
  bool			resetToken = false;
  LinkInfo		linkInfo;
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
  StreamType::T		streamType = StreamType::Duplex;
  StreamState::T	oldState = StreamState::Idle;
  StreamState::T	newState = StreamState::Open;
  StreamSide::T		streamSide = StreamSide::Sending;
  StreamReason::T	reason = StreamReason::None;
  uint64_t		streamID = 0;
  uint64_t		offset = 0;
  uint64_t		length = 0;
  uint64_t		errorCode = 0;
  bool			fin = false;
  LinkInfo		linkInfo;
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
  StreamDataLoc::T	from = StreamDataLoc::Transport;
  StreamDataLoc::T	to = StreamDataLoc::Application;
  StreamDataInfo::T	additionalInfo = StreamDataInfo::None;
  uint64_t		streamID = 0;
  uint64_t		offset = 0;
  uint64_t		length = 0;
  LinkInfo		linkInfo;
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
  BlockedState::T	oldState = BlockedState::Unblocked;
  BlockedState::T	newState = BlockedState::Blocked;
  BlockedReason::T	reason =
    BlockedReason::CxnFlowCtrl;
  uint64_t		streamID = 0;
  LinkInfo		linkInfo;
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
  CloseInitiator::T	initiator = CloseInitiator::Local;
  CloseTrigger::T	trigger = CloseTrigger::Error;
  CloseReason::T	reason = CloseReason::None;
  CloseError::T		connectionError = CloseError::None;
  CloseError::T		applicationError = CloseError::None;
  uint64_t		errorCode = 0;
  LinkInfo		linkInfo;
};

struct CxnStateEvent {
  LinkState::T		oldState = LinkState::Starting;
  LinkState::T		newState = LinkState::Handshaking;
  LinkInfo		linkInfo;
};

struct CxnStartedEvent {
  ZiSockAddr	local;
  ZiSockAddr	remote;
  LinkInfo	linkInfo;
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
  ZuDerive(Queue_, (ZmQueue<Fn, ZmQueueHeapID<"Zquic.Log.Queue">>));
  struct Queue : public Queue_ {
    using Lock = ZmPLock;
    using Guard = ZmGuard<Lock>;

    void push(Fn fn) {
      Guard guard(m_lock);
      Queue_::push(ZuMv(fn));
    }
    void unshift(Fn fn) {
      Guard guard(m_lock);
      Queue_::unshift(ZuMv(fn));
    }
    Fn shift() {
      Guard guard(m_lock);
      return Queue_::shift();
    }

    Lock	m_lock;
  };

public:
  struct Trace {
    bool		configured = false;
    bool		sinkOpened = false;
    bool		headerWritten = false;
    Zquic::Vantage::T	vantage = Zquic::Vantage::Unknown;
    ZquicLogParams	params;
    ZquicLogSink		sink;
  };

  ~ZquicLogger();

  ZquicLogger(const ZquicLogger &) = delete;
  ZquicLogger &operator =(const ZquicLogger &) = delete;

  static ZquicLogger *instance();

  static bool enabled() { return instance()->enabled_(); }
  static bool init(
    Trace &trace, const ZquicLogParams &params, Zquic::Vantage::T vantage) {
    return instance()->init_(trace, params, vantage);
  }
  static void start() { instance()->start_(); }
  static void stop() { instance()->stop_(); }
  template <typename L>
  static void close(Trace &trace, L l) {
    instance()->close_(trace, ZuMv(l));
  }
  static void final(Trace &trace) { instance()->final_(trace); }
  static ZquicLogDiag diag() { return instance()->diag_(); }

  static void cxnStarted(
    Trace &trace, ZquicLog_::CxnStartedEvent event) {
    instance()->cxnStarted_(trace, ZuMv(event));
  }
  static void dgramSent(Trace &trace, ZquicLog_::DgramEvent event) {
    instance()->dgramSent_(trace, ZuMv(event));
  }
  static void dgramRecv(Trace &trace, ZquicLog_::DgramEvent event) {
    instance()->dgramRecv_(trace, ZuMv(event));
  }
  static void pktSent(Trace &trace, ZquicLog_::PktEvent event) {
    instance()->pktSent_(trace, ZuMv(event));
  }
  static void pktRecv(Trace &trace, ZquicLog_::PktEvent event) {
    instance()->pktRecv_(trace, ZuMv(event));
  }
  static void pktBuf(Trace &trace, ZquicLog_::PktEvent event) {
    instance()->pktBuf_(trace, ZuMv(event));
  }
  static void pktDrop(Trace &trace, ZquicLog_::PktEvent event) {
    instance()->pktDrop_(trace, ZuMv(event));
  }
  static void pktsAcked(Trace &trace, ZquicLog_::AckEvent event) {
    instance()->pktsAcked_(trace, ZuMv(event));
  }
  static void pktLost(Trace &trace, ZquicLog_::RecEvent event) {
    instance()->pktLost_(trace, ZuMv(event));
  }
  static void recPktLost(Trace &trace, ZquicLog_::RecEvent event) {
    instance()->recPktLost_(trace, ZuMv(event));
  }
  static void markRetrans(Trace &trace, ZquicLog_::RecEvent event) {
    instance()->markRetrans_(trace, ZuMv(event));
  }
  static void metricsUpd(Trace &trace, ZquicLog_::RecEvent event) {
    instance()->metricsUpd_(trace, ZuMv(event));
  }
  static void lossTimerUpd(Trace &trace, ZquicLog_::RecEvent event) {
    instance()->lossTimerUpd_(trace, ZuMv(event));
  }
  static void congStateUpd(Trace &trace, ZquicLog_::RecEvent event) {
    instance()->congStateUpd_(trace, ZuMv(event));
  }
  static void ecnStateUpd(Trace &trace, ZquicLog_::ECNEvent event) {
    instance()->ecnStateUpd_(trace, ZuMv(event));
  }
  static void keyUpdated(Trace &trace, ZquicLog_::SecEvent event) {
    instance()->keyUpdated_(trace, ZuMv(event));
  }
  static void keyRetired(Trace &trace, ZquicLog_::SecEvent event) {
    instance()->keyRetired_(trace, ZuMv(event));
  }
  static void paramsSet(Trace &trace, ZquicLog_::ParamsEvent event) {
    instance()->paramsSet_(trace, ZuMv(event));
  }
  static void alpnInfo(Trace &trace, ZquicLog_::SecEvent event) {
    instance()->alpnInfo_(trace, ZuMv(event));
  }
  static void versionInfo(Trace &trace, ZquicLog_::VersionEvent event) {
    instance()->versionInfo_(trace, ZuMv(event));
  }
  static void tlsAlert(Trace &trace, ZquicLog_::SecEvent event) {
    instance()->tlsAlert_(trace, ZuMv(event));
  }
  static void secEvent(
    Trace &trace, ZquicLog_::EventName::T name,
    ZquicLog_::SecEvent event) {
    instance()->secEvent_(trace, name, ZuMv(event));
  }
  static void pathUpdated(Trace &trace, ZquicLog_::PathEvent event) {
    instance()->pathUpdated_(trace, ZuMv(event));
  }
  static void pathValidUpd(Trace &trace, ZquicLog_::PathEvent event) {
    instance()->pathValidUpd_(trace, ZuMv(event));
  }
  static void pmtudUpdated(Trace &trace, ZquicLog_::PathEvent event) {
    instance()->pmtudUpdated_(trace, ZuMv(event));
  }
  static void cidUpdated(Trace &trace, ZquicLog_::CIDEvent event) {
    instance()->cidUpdated_(trace, ZuMv(event));
  }
  static void streamStateUpd(Trace &trace, ZquicLog_::StreamEvent event) {
    instance()->streamStateUpd_(trace, ZuMv(event));
  }
  static void streamDataMoved(
    Trace &trace, ZquicLog_::StreamDataEvent event) {
    instance()->streamDataMoved_(trace, ZuMv(event));
  }
  static void cxnDataBlockedUpd(
    Trace &trace, ZquicLog_::BlockedEvent event) {
    instance()->cxnDataBlockedUpd_(trace, ZuMv(event));
  }
  static void streamDataBlockedUpd(
    Trace &trace, ZquicLog_::BlockedEvent event) {
    instance()->streamDataBlockedUpd_(trace, ZuMv(event));
  }
  static void cxnClosed(Trace &trace, ZquicLog_::CloseEvent event) {
    instance()->cxnClosed_(trace, ZuMv(event));
  }
  static void cxnStateUpd(Trace &trace, ZquicLog_::CxnStateEvent event) {
    instance()->cxnStateUpd_(trace, ZuMv(event));
  }
  template <typename L>
  static void log(Trace &trace, L l) {
    instance()->log_(trace, ZuMv(l));
  }

  void logCxnStarted(
    const ZquicLog_::CxnStartedEvent &event, ZuTime time) {
    writeCxnStarted_(event, time);
  }
  void logDgramSent(const ZquicLog_::DgramEvent &event, ZuTime time) {
    writeDatagramEvent_(ZquicLog_::EventName::UDPTx, event, time);
  }
  void logDgramRecv(const ZquicLog_::DgramEvent &event, ZuTime time) {
    writeDatagramEvent_(ZquicLog_::EventName::UDPRx, event, time);
  }
  void logPktSent(const ZquicLog_::PktEvent &event, ZuTime time) {
    writePktEvent_(ZquicLog_::EventName::PktSent, event, time);
  }
  void logPktRecv(const ZquicLog_::PktEvent &event, ZuTime time) {
    writePktEvent_(ZquicLog_::EventName::PktRecv, event, time);
  }
  void logPktBuf(const ZquicLog_::PktEvent &event, ZuTime time) {
    writePktEvent_(ZquicLog_::EventName::PktBuf, event, time);
  }
  void logPktDrop(const ZquicLog_::PktEvent &event, ZuTime time) {
    writePktEvent_(ZquicLog_::EventName::PktDrop, event, time);
  }
  void logPktsAcked(const ZquicLog_::AckEvent &event, ZuTime time) {
    writeAckEvent_(ZquicLog_::EventName::PktsAcked, event, time);
  }
  void logPktLost(const ZquicLog_::RecEvent &event, ZuTime time) {
    writePktLost_(event, time);
  }
  void logRecPktLost(const ZquicLog_::RecEvent &event, ZuTime time) {
    writePktLost_(event, time);
  }
  void logMarkRetrans(
    const ZquicLog_::RecEvent &event, ZuTime time) {
    writeMarkRetrans_(event, time);
  }
  void logMetricsUpd(const ZquicLog_::RecEvent &event, ZuTime time) {
    writeRecMetrics_(event, time);
  }
  void logLossTimerUpd(const ZquicLog_::RecEvent &event, ZuTime time) {
    writeTimerEvent_(event, time);
  }
  void logCongStateUpd(
    const ZquicLog_::RecEvent &event, ZuTime time) {
    writeCongState_(event, time);
  }
  void logECNStateUpd(const ZquicLog_::ECNEvent &event, ZuTime time) {
    writeECNEvent_(event, time);
  }
  void logKeyUpdated(const ZquicLog_::SecEvent &event, ZuTime time) {
    writeKeyEvent_(ZquicLog_::EventName::KeyUpd, event, time);
  }
  void logKeyRetired(const ZquicLog_::SecEvent &event, ZuTime time) {
    writeKeyEvent_(ZquicLog_::EventName::KeyDiscarded, event, time);
  }
  void logParamsSet(
    const ZquicLog_::ParamsEvent &event, ZuTime time) {
    writeParams_(event, time);
  }
  void logALPNInfo(const ZquicLog_::SecEvent &event, ZuTime time) {
    writeALPNEvent_(event, time);
  }
  void logVersionInfo(
    const ZquicLog_::VersionEvent &event, ZuTime time) {
    writeVersion_(event, time);
  }
  void logTLSAlert(const ZquicLog_::SecEvent &event, ZuTime time) {
    writeSecEvent_(ZquicLog_::EventName::TLSAlert, event, time);
  }
  void logSecEvent(
    ZquicLog_::EventName::T name,
    const ZquicLog_::SecEvent &event, ZuTime time) {
    writeSecEvent_(name, event, time);
  }
  void logPathUpdated(const ZquicLog_::PathEvent &event, ZuTime time) {
    writePathEvent_(ZquicLog_::EventName::TupleAssigned, event, time);
  }
  void logPathValid(
    const ZquicLog_::PathEvent &event, ZuTime time) {
    writePathValid_(event, time);
  }
  void logPMTUDUpdated(const ZquicLog_::PathEvent &event, ZuTime time) {
    writeMTUEvent_(event, time);
  }
  void logCIDUpdated(const ZquicLog_::CIDEvent &event, ZuTime time) {
    writeCIDEvent_(ZquicLog_::EventName::CIDUpd, event, time);
  }
  void logStreamStateUpd(
    const ZquicLog_::StreamEvent &event, ZuTime time) {
    writeStreamEvent_(ZquicLog_::EventName::StreamStateUpd, event, time);
  }
  void logStreamDataMoved(
    const ZquicLog_::StreamDataEvent &event, ZuTime time) {
    writeStreamData_(ZquicLog_::EventName::StreamDataMoved, event, time);
  }
  void logCxnDataBlockedUpd(
    const ZquicLog_::BlockedEvent &event, ZuTime time) {
    writeCxnBlocked_(
      ZquicLog_::EventName::CxnDataBlockedUpd, event, time);
  }
  void logStreamDataBlockedUpd(
    const ZquicLog_::BlockedEvent &event, ZuTime time) {
    writeStreamBlocked_(
      ZquicLog_::EventName::StreamDataBlockedUpd, event, time);
  }
  void logCxnClosed(
    const ZquicLog_::CloseEvent &event, ZuTime time) {
    writeCloseEvent_(ZquicLog_::EventName::CxnClosed, event, time);
  }
  void logCxnStateUpd(
    const ZquicLog_::CxnStateEvent &event, ZuTime time) {
    writeCxnState_(ZquicLog_::EventName::CxnStateUpd, event, time);
  }

private:
  ZquicLogger();

  bool enabled_() const { return m_enabled.load_(); }
  bool init_(Trace &, const ZquicLogParams &, Zquic::Vantage::T);
  void start_();
  void stop_();
  template <typename L>
  void close_(Trace &trace, L l) {
    if (!trace.configured) { l(); return; }
    if (enabled_()) {
      auto fn_ = [trace = &trace, l = ZuMv(l)](ZquicLogger *this_) mutable {
	this_->closeTrace_(*trace);
	l();
      };
      Fn fn{fn_};
      log__(fn);
    } else {
      closeTrace_(trace);
      l();
    }
  }
  void final_(Trace &);
  ZquicLogDiag diag_() const;
  void cxnStarted_(Trace &, ZquicLog_::CxnStartedEvent);
  void dgramSent_(Trace &, ZquicLog_::DgramEvent);
  void dgramRecv_(Trace &, ZquicLog_::DgramEvent);
  void pktSent_(Trace &, ZquicLog_::PktEvent);
  void pktRecv_(Trace &, ZquicLog_::PktEvent);
  void pktBuf_(Trace &, ZquicLog_::PktEvent);
  void pktDrop_(Trace &, ZquicLog_::PktEvent);
  void pktsAcked_(Trace &, ZquicLog_::AckEvent);
  void pktLost_(Trace &, ZquicLog_::RecEvent);
  void recPktLost_(Trace &, ZquicLog_::RecEvent);
  void markRetrans_(Trace &, ZquicLog_::RecEvent);
  void metricsUpd_(Trace &, ZquicLog_::RecEvent);
  void lossTimerUpd_(Trace &, ZquicLog_::RecEvent);
  void congStateUpd_(Trace &, ZquicLog_::RecEvent);
  void ecnStateUpd_(Trace &, ZquicLog_::ECNEvent);
  void keyUpdated_(Trace &, ZquicLog_::SecEvent);
  void keyRetired_(Trace &, ZquicLog_::SecEvent);
  void paramsSet_(Trace &, ZquicLog_::ParamsEvent);
  void alpnInfo_(Trace &, ZquicLog_::SecEvent);
  void versionInfo_(Trace &, ZquicLog_::VersionEvent);
  void tlsAlert_(Trace &, ZquicLog_::SecEvent);
  void secEvent_(Trace &, ZquicLog_::EventName::T, ZquicLog_::SecEvent);
  void pathUpdated_(Trace &, ZquicLog_::PathEvent);
  void pathValidUpd_(Trace &, ZquicLog_::PathEvent);
  void pmtudUpdated_(Trace &, ZquicLog_::PathEvent);
  void cidUpdated_(Trace &, ZquicLog_::CIDEvent);
  void streamStateUpd_(Trace &, ZquicLog_::StreamEvent);
  void streamDataMoved_(Trace &, ZquicLog_::StreamDataEvent);
  void cxnDataBlockedUpd_(Trace &, ZquicLog_::BlockedEvent);
  void streamDataBlockedUpd_(Trace &, ZquicLog_::BlockedEvent);
  void cxnClosed_(Trace &, ZquicLog_::CloseEvent);
  void cxnStateUpd_(Trace &, ZquicLog_::CxnStateEvent);
  void log__(Fn &fn) {
    if (tryPush_(fn)) {
      ++m_recordsEnqueued;
    } else {
      ++m_ringBackPressure;
      m_queue.push(ZuMv(fn));
      ++m_queueCount;
      ++m_recordsEnqueued;
    }
  }
  template <typename L>
  void log_(Trace &trace, L l) {
    if (!enabled_()) return;
    ZuTime time = Zm::now();
    auto fn_ = [trace = &trace, l = ZuMv(l), time](ZquicLogger *this_) mutable {
      if (!this_->useTrace_(trace)) return;
      l(*this_, time);
    };
    Fn fn{fn_};
    log__(fn);
  }

  void work_();

  bool tryPush_(Fn &);
  bool useTrace_(Trace *);
  void closeTrace_(Trace &);
  bool write_(Trace *, ZuCSpan);
  bool writeHeader_(Trace *);
  bool writeCxnStarted_(
    const ZquicLog_::CxnStartedEvent &, ZuTime);
  bool writeDatagramEvent_(
    ZquicLog_::EventName::T, const ZquicLog_::DgramEvent &, ZuTime);
  bool writePktEvent_(
    ZquicLog_::EventName::T, const ZquicLog_::PktEvent &, ZuTime);
  bool writeAckEvent_(
    ZquicLog_::EventName::T, const ZquicLog_::AckEvent &, ZuTime);
  bool writePktLost_(const ZquicLog_::RecEvent &, ZuTime);
  bool writeMarkRetrans_(const ZquicLog_::RecEvent &, ZuTime);
  bool writeRecMetrics_(const ZquicLog_::RecEvent &, ZuTime);
  bool writeCongState_(const ZquicLog_::RecEvent &, ZuTime);
  bool writeTimerEvent_(const ZquicLog_::RecEvent &, ZuTime);
  bool writeECNEvent_(const ZquicLog_::ECNEvent &, ZuTime);
  bool writeKeyEvent_(
    ZquicLog_::EventName::T, const ZquicLog_::SecEvent &, ZuTime);
  bool writeParams_(
    const ZquicLog_::ParamsEvent &, ZuTime);
  bool writeALPNEvent_(const ZquicLog_::SecEvent &, ZuTime);
  bool writeVersion_(const ZquicLog_::VersionEvent &, ZuTime);
  bool writeSecEvent_(
    ZquicLog_::EventName::T, const ZquicLog_::SecEvent &, ZuTime);
  bool writePathEvent_(
    ZquicLog_::EventName::T, const ZquicLog_::PathEvent &, ZuTime);
  bool writeMTUEvent_(const ZquicLog_::PathEvent &, ZuTime);
  bool writePathValid_(const ZquicLog_::PathEvent &, ZuTime);
  bool writeCIDEvent_(ZquicLog_::EventName::T, const ZquicLog_::CIDEvent &, ZuTime);
  bool writeStreamEvent_(
    ZquicLog_::EventName::T, const ZquicLog_::StreamEvent &, ZuTime);
  bool writeStreamData_(
    ZquicLog_::EventName::T, const ZquicLog_::StreamDataEvent &, ZuTime);
  bool writeCxnBlocked_(
    ZquicLog_::EventName::T, const ZquicLog_::BlockedEvent &, ZuTime);
  bool writeStreamBlocked_(
    ZquicLog_::EventName::T, const ZquicLog_::BlockedEvent &, ZuTime);
  bool writeCloseEvent_(
    ZquicLog_::EventName::T, const ZquicLog_::CloseEvent &, ZuTime);
  bool writeCxnState_(
    ZquicLog_::EventName::T, const ZquicLog_::CxnStateEvent &, ZuTime);

private:
  ZmAtomic<int>		m_enabled = 0;
  bool			m_started = false;
  unsigned		m_startedRefs = 0;
  unsigned		m_configured = 0;
  ZquicLogParams	m_params;
  ZmThread		m_thread;
  Ring			m_ring;
  Queue			m_queue;
  ZmAtomic<unsigned>	m_queueCount = 0;
  Trace			*m_activeTrace = nullptr;
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
  struct Trace { };
  static constexpr bool enabled() { return false; }
  static bool init(
    Trace &, const ZquicLogParams &, Zquic::Vantage::T) {
    return true;
  }
  static void start() { }
  static void stop() { }
  template <typename L>
  static void close(Trace &, L l) { l(); }
  static void final(Trace &) { }
  static ZquicLogDiag diag() { return {}; }
  static void cxnStarted(Trace &, ZquicLog_::CxnStartedEvent) { }
  static void dgramSent(Trace &, ZquicLog_::DgramEvent) { }
  static void dgramRecv(Trace &, ZquicLog_::DgramEvent) { }
  static void pktSent(Trace &, ZquicLog_::PktEvent) { }
  static void pktRecv(Trace &, ZquicLog_::PktEvent) { }
  static void pktBuf(Trace &, ZquicLog_::PktEvent) { }
  static void pktDrop(Trace &, ZquicLog_::PktEvent) { }
  static void pktsAcked(Trace &, ZquicLog_::AckEvent) { }
  static void pktLost(Trace &, ZquicLog_::RecEvent) { }
  static void recPktLost(Trace &, ZquicLog_::RecEvent) { }
  static void markRetrans(Trace &, ZquicLog_::RecEvent) { }
  static void metricsUpd(Trace &, ZquicLog_::RecEvent) { }
  static void lossTimerUpd(Trace &, ZquicLog_::RecEvent) { }
  static void congStateUpd(Trace &, ZquicLog_::RecEvent) { }
  static void ecnStateUpd(Trace &, ZquicLog_::ECNEvent) { }
  static void keyUpdated(Trace &, ZquicLog_::SecEvent) { }
  static void keyRetired(Trace &, ZquicLog_::SecEvent) { }
  static void paramsSet(Trace &, ZquicLog_::ParamsEvent) { }
  static void alpnInfo(Trace &, ZquicLog_::SecEvent) { }
  static void versionInfo(Trace &, ZquicLog_::VersionEvent) { }
  static void tlsAlert(Trace &, ZquicLog_::SecEvent) { }
  static void secEvent(
    Trace &, ZquicLog_::EventName::T, ZquicLog_::SecEvent) { }
  static void pathUpdated(Trace &, ZquicLog_::PathEvent) { }
  static void pathValidUpd(Trace &, ZquicLog_::PathEvent) { }
  static void pmtudUpdated(Trace &, ZquicLog_::PathEvent) { }
  static void cidUpdated(Trace &, ZquicLog_::CIDEvent) { }
  static void streamStateUpd(Trace &, ZquicLog_::StreamEvent) { }
  static void streamDataMoved(Trace &, ZquicLog_::StreamDataEvent) { }
  static void cxnDataBlockedUpd(Trace &, ZquicLog_::BlockedEvent) { }
  static void streamDataBlockedUpd(Trace &, ZquicLog_::BlockedEvent) { }
  static void cxnClosed(Trace &, ZquicLog_::CloseEvent) { }
  static void cxnStateUpd(Trace &, ZquicLog_::CxnStateEvent) { }
  template <typename L>
  static void log(Trace &, L) { }
};

#endif /* Zquic_DEBUG */

template <typename L>
inline void ZquicLog(ZquicLogger::Trace &trace, L l) {
  ZquicLogger::log(trace, ZuMv(l));
}

#ifdef Zquic_DEBUG
#define ZquicLOG(trace, msg) \
  do { \
    if (ZquicLogger::enabled()) { \
      using namespace ZquicLog_; \
      ZquicLog(trace, msg); \
    } \
  } while (0)
#else
#define ZquicLOG(trace, msg) do { } while (0)
#endif

#endif /* ZquicLog_HH */
