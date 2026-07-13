//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC qlog

#ifndef Zquic_HH
#error "include zlib/Zquic.hh before this header"
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

namespace ZquicLog_ {

using namespace Zquic_;

struct Diag {
  uint64_t	recordsEnqueued = 0;
  uint64_t	recordsWritten = 0;
  uint64_t	recordsDropped = 0;
  uint64_t	ringBackPressure = 0;
  uint64_t	writerFailures = 0;
  uint64_t	bytesWritten = 0;
};

struct Params {
  Params &enabled(bool v) { m_enabled = v; return *this; }
  Params &path(ZuCSpan v) { m_path = v; return *this; }
  Params &ringSize(unsigned v) { m_ringSize = v; return *this; }
  Params &age(unsigned v) { m_age = v; return *this; }

  bool enabled() const { return m_enabled; }
  ZuCSpan path() const { return m_path; }
  unsigned ringSize() const { return m_ringSize; }
  unsigned age() const { return m_age; }

private:
  bool		m_enabled = false;
  ParamString	m_path;
  unsigned	m_ringSize = (1<<20);
  unsigned	m_age = 8;
};

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

struct DgramEvt {
  LinkInfo		linkInfo;
  uint64_t		size = 0;
  EcnMark::T		ecn = EcnMark::N;
};

struct StreamType {
  ZtEnum(StreamType, int8_t, Duplex, Simplex);
  ZtEnumMap(StreamType, JSON, "bidirectional", "unidirectional");
};

struct FrameEvt {
  QAckRanges		ackRanges;
  CxnID			cxnID;
  ResetToken		resetToken;
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
  FrameType::T		type = FrameType::Unknown;
  StreamType::T		streamType = StreamType::Duplex;
  uint8_t		rangeCount = 0;
  bool			fin = false;

  struct Traits : public ZuBaseTraits<FrameEvt> {
    enum { IsPOD = 1 };
  };
  friend Traits ZuTraitsType(FrameEvt *);
};

struct PktEvt {
  struct Reason {
    ZtEnum(Reason, int8_t,
	      None, Coalescing, ParseLong, PacketLength, PrepareLong,
	      UnsupportedLongType, DiscardedSpace, MissingKeys, Protection, Duplicate,
	      ParseShort, BadKeyPhase, AntiAmp, ProbeAdmit,
	      AppSend, AfterOneRTT, FramePolicy);
	  ZtEnumMap(Reason, JSON,
	    "", "coalescing", "parse_long", "packet_length", "prepare_long",
	    "unsupported_long_type", "discarded_space", "missing_keys",
	    "protection", "duplicate", "parse_short", "invalid_key_phase",
	    "anti_amplification", "probe_admission", "app_send",
	    "0rtt_after_1rtt", "frame_policy");
  };
  using Frames = ZuArray<FrameEvt, FrameMax>;

  Frames		frames;
  LinkInfo		linkInfo;
  uint64_t		packetNumber = 0;
  uint64_t		packetSize = 0;
  uint64_t		payloadSize = 0;
  uint64_t		bytesInFlight = 0;
  PktType::T		packetType = PktType::Initial;
  PktNumSpace::T	packetSpace = PktNumSpace::Initial;
  EcnMark::T		ecn = EcnMark::N;
  Reason::T		reason = Reason::None;
  uint8_t		frameCount = 0;
  bool			framesTruncated = false;
  bool			ackEliciting = false;
};

struct AckEvt {
  using PacketNumbers = ZuArray<uint64_t, AckPacketMax>;

  PacketNumbers		packetNumbers;
  LinkInfo		linkInfo;
  uint64_t		largestAcked = 0;
  uint64_t		ackDelayUS = 0;
  uint64_t		ackedBytes = 0;
  uint64_t		lostBytes = 0;
  PktNumSpace::T	packetSpace = PktNumSpace::Initial;
  uint8_t		rangeCount = 0;
  uint8_t		ackedFrames = 0;
  uint8_t		lostFrames = 0;
  bool			packetNumbersTruncated = false;
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
    PMTUDAck, Loss, PMTUDLoss, Expired, Probe, Backoff, ECNCE);
  ZtEnumMap(RecReason, JSON,
    "", "time_threshold", "packet_threshold", "canceled", "armed",
    "no_level", "ack", "pmtud_ack", "loss", "pmtud_loss",
    "expired", "probe", "backoff", "ecn_ce");
};

struct RecLogEvt {
  using Frames = ZuArray<FrameEvt, FrameMax>;

  Frames		frames;
  LinkInfo		linkInfo;
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
  RecKind::T		kind = RecKind::Aggregate;
  PktNumSpace::T	packetSpace = PktNumSpace::Initial;
  RecReason::T		reason = RecReason::None;
  uint8_t		frameCount = 0;
};

struct ECNState {
  ZtEnum(ECNState, int8_t, Unknown, Disabled, Testing, Capable, Failed);
  ZtEnumMap(ECNState, JSON,
    "unknown", "disabled", "testing", "capable", "failed");
};

struct ECNReason {
  ZtEnum(ECNReason, int8_t,
    AckECN, ECT0Decrease, ECT1Decrease, CEDecrease, ECTOverflow, CEOverflow,
    CounterExceedsAck, Probe, NoAckECN, MarkFailed, CE);
  ZtEnumMap(ECNReason, JSON,
    "ack_ecn", "ect0_decrease", "ect1_decrease", "ce_decrease",
    "ect_overflow", "ce_overflow", "counter_exceeds_ack",
    "probe", "no_ack_ecn", "mark_failed", "ce");
};

struct ECNEvt {
  LinkInfo		linkInfo;
  uint64_t		ect0 = 0;
  uint64_t		ect1 = 0;
  uint64_t		ce = 0;
  uint64_t		previousECT0 = 0;
  uint64_t		previousECT1 = 0;
  uint64_t		previousCE = 0;
  uint64_t		largestAcked = 0;
  PktNumSpace::T	packetSpace = PktNumSpace::Initial;
  ECNState::T		oldState = ECNState::Unknown;
  ECNState::T		state = ECNState::Capable;
  ECNReason::T		reason = ECNReason::AckECN;
  bool			disabled = false;
};

struct SecKind {
  ZtEnum(SecKind, int8_t,
    KeyUpdated, KeyRetired, TransportParams, ALPN, TLS, Retry, Token,
    VersionNeg, StatelessReset, PktProtect);
  ZtEnumMap(SecKind, JSON,
    "key_updated", "key_retired", "transport_parameters", "alpn", "tls",
    "retry", "token", "version_negotiation", "stateless_reset",
    "packet_protection");
};

struct SecKeyType {
  ZtEnum(SecKeyType, int8_t, None = -1, RX, RXOld, TX);
  ZtEnumMap(SecKeyType, JSON, "rx", "rx_old", "tx",
    "unknown", "none");
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
    None = -1, Sent, Received, Validated, Local, Remote, Peer, Selected, Timer,
    HSComplete, RX, TX);
  ZtEnumMap(SecTrigger, JSON,
    "sent", "received", "validated", "local", "remote", "peer",
    "selected", "timer", "handshake_complete", "rx", "tx",
    "unknown", "none");
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
    None = -1, Unknown, OK, Handshake, KeyPhase, KeyUpdate, PeerUpdate,
    PacketSpace, AddrValid, MissingToken, NewToken, NewTokenPolicy,
    UnsupVersion, UnknownCID, TokenMatch, Validation, RetrySCID, Expired,
    Auth, Address, Malformed, Kind, ODCID, Protect, MissingKeys, Protection,
    BadKeyPhase, ZeroRTT, ZeroRTTAppParams, ZeroRTTTransportParams,
    ZeroRTTFlowLimit, ZeroRTTStreamLimit, ZeroRTTActiveCIDLimit,
    ZeroRTTFramePolicy, ZeroRTTMissingKeys, ZeroRTTAfterOneRTT);
  ZtEnumMap(SecReason, JSON,
    "unknown", "ok", "handshake", "key_phase", "key_update",
    "peer_update", "packet_space", "address_validation", "missing_token",
    "new_token", "new_token_policy", "unsupported_version", "unknown_cid",
    "token_match", "validation", "retry_scid", "expired", "auth", "address",
    "malformed", "kind", "odcid", "protect", "missing_keys", "protection",
    "invalid_key_phase", "0rtt", "0rtt_app_params",
    "0rtt_transport_params", "0rtt_flow_limit", "0rtt_stream_limit",
    "0rtt_active_connection_id_limit", "0rtt_frame_policy",
    "0rtt_missing_keys", "0rtt_after_1rtt", "unknown", "none");
};

inline SecReason::T zeroRTTSecReason(ZeroRTTReason::T reason)
{
  switch (reason) {
    case ZeroRTTReason::AppParams:
      return SecReason::ZeroRTTAppParams;
    case ZeroRTTReason::TransportParams:
      return SecReason::ZeroRTTTransportParams;
    case ZeroRTTReason::FlowLimit:
      return SecReason::ZeroRTTFlowLimit;
    case ZeroRTTReason::StreamLimit:
      return SecReason::ZeroRTTStreamLimit;
    case ZeroRTTReason::ActiveCIDLimit:
      return SecReason::ZeroRTTActiveCIDLimit;
    case ZeroRTTReason::FramePolicy:
      return SecReason::ZeroRTTFramePolicy;
    case ZeroRTTReason::MissingKeys:
      return SecReason::ZeroRTTMissingKeys;
    case ZeroRTTReason::AfterOneRTT:
      return SecReason::ZeroRTTAfterOneRTT;
    default:
      return SecReason::ZeroRTT;
  }
}

struct SecEvt {
  ZeString		alpn;
  LinkInfo		linkInfo;
  uint64_t		value = 0;
  SecKind::T		kind = SecKind::TLS;
  PktNumSpace::T	packetSpace = PktNumSpace::Initial;
  PktKeyLevel::T	keyLevel = PktKeyLevel::OneRTT;
  SecKeyType::T		keyType = SecKeyType::None;
  SecTrigger::T		trigger = SecTrigger::None;
  SecReason::T		reason = SecReason::None;
  bool			success = true;
};

struct ParamsEvt {
  CxnID			origDCID;
  CxnID			initialSCID;
  CxnID			retrySCID;
  ResetToken		statelessResetToken;
  LinkInfo		linkInfo;
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
  Initiator::T		initiator = Initiator::Local;
  bool			statelessResetTokenPresent = false;
  bool			disableActiveMigration = false;
};

struct VersionEvt {
  Versions		serverVersions;
  Versions		clientVersions;
  LinkInfo		linkInfo;
  uint32_t		chosenVersion = 0;
  bool			chosenVersionPresent = false;
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
    NATRebind, Matched, Response, Promoted, Timeout, AntiAmp, ProbeAdmit,
    PathHint, Probe, Admission, SendFail, Active, Endpoint, PeerDisabled,
    NoPeerCID, Validation);
  ZtEnumMap(PathReason, JSON,
    "", "client", "server", "initial", "datagram", "peer_address_change",
    "mismatch", "nat_rebind", "matched", "response", "promoted", "timeout",
    "anti_amplification", "probe_admission", "path_hint", "probe",
    "admission", "send_failure", "active", "endpoint", "peer_disabled",
    "no_peer_cid", "validation");
};

struct PathEvt {
  LinkInfo		linkInfo;
  uint64_t		tupleID = 0;
  uint64_t		attemptID = U64Null;
  uint64_t		bytes = 0;
  uint64_t		antiAmplification = 0;
  uint64_t		deadlineUS = 0;
  uint32_t		mtu = 0;
  PathKind::T		kind = PathKind::Path;
  PathAction::T		action = PathAction::Updated;
  PathReason::T		reason = PathReason::None;
  bool			validated = false;
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
    RouteTombstone, Sequence, ID, ResetToken, RetirePrior, Migration);
  ZtEnumMap(CIDReason, JSON,
    "", "peer_request", "path_promoted", "route_install", "route_retire",
    "route_tombstone", "sequence", "id", "reset_token", "retire_prior_to",
    "migration");
};

struct CIDEvt {
  CxnID			cxnID;
  LinkInfo		linkInfo;
  uint64_t		sequence = 0;
  uint64_t		attemptID = U64Null;
  CIDKind::T		kind = CIDKind::CxnID;
  CIDAction::T		action = CIDAction::Updated;
  CIDReason::T		reason = CIDReason::None;
  uint8_t		length = 0;
  bool			local = false;
  bool			associated = false;
  bool			resetToken = false;
};

struct MigrationAction {
  ZtEnum(MigrationAction, int8_t,
    Requested, Rejected, Started, RebindStart, RebindOK, RebindFail,
    CIDSelected, CIDUnavailable, ChallengeQueued, ResponseMatched,
    ResponseMismatch, Promoted, Abandoned, Failed, Closed);
  ZtEnumMap(MigrationAction, JSON,
    "requested", "rejected", "started", "rebind_start", "rebind_ok",
    "rebind_fail", "cid_selected", "cid_unavailable", "challenge_queued",
    "response_matched", "response_mismatch", "promoted", "abandoned",
    "failed", "closed", "unknown");
};

struct MigrationEvt {
  LinkInfo			linkInfo;
  ZiSockAddr			activeLocal;
  ZiSockAddr			activeRemote;
  ZiSockAddr			candidateLocal;
  ZiSockAddr			candidateRemote;
  uint64_t			attemptID = 0;
  uint64_t			peerCIDSeq = U64Null;
  uint64_t			deadlineUS = 0;
  uint32_t			mtu = 0;
  MigrationAction::T		action = MigrationAction::Requested;
  Zquic::MigrationReason::T	reason = Zquic::MigrationReason::None;
  Zquic::MigrationState::T	state = Zquic::MigrationState::Idle;
  Zquic::PathRole::T		pathRole = Zquic::PathRole::Candidate;
  bool				localRebind = false;
  bool				validated = false;
  bool				closeOnFailure = false;
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
  ZtEnum(StreamReason, int8_t, None = -1, LocalOpen, PeerOpen, Reaped);
  ZtEnumMap(StreamReason, JSON,
    "local_open", "peer_open", "reaped", "unknown", "none");
};

struct StreamEvt {
  LinkInfo		linkInfo;
  uint64_t		streamID = 0;
  uint64_t		offset = 0;
  uint64_t		length = 0;
  uint64_t		errorCode = 0;
  StreamType::T		streamType = StreamType::Duplex;
  StreamState::T	oldState = StreamState::Idle;
  StreamState::T	newState = StreamState::Open;
  StreamSide::T		streamSide = StreamSide::Sending;
  StreamReason::T	reason = StreamReason::None;
  bool			fin = false;
};

struct StreamDataLoc {
  ZtEnum(StreamDataLoc, int8_t, Application, Transport, Network);
  ZtEnumMap(StreamDataLoc, JSON,
    "application", "transport", "network");
};

struct StreamDataInfo {
  ZtEnum(StreamDataInfo, int8_t, None = -1, FinSet);
  ZtEnumMap(StreamDataInfo, JSON, "fin_set", "unknown", "none");
};

struct StreamDataEvt {
  LinkInfo		linkInfo;
  uint64_t		streamID = 0;
  uint64_t		offset = 0;
  uint64_t		length = 0;
  StreamDataLoc::T	from = StreamDataLoc::Transport;
  StreamDataLoc::T	to = StreamDataLoc::Application;
  StreamDataInfo::T	additionalInfo = StreamDataInfo::None;
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

struct BlockedEvt {
  LinkInfo		linkInfo;
  uint64_t		streamID = 0;
  BlockedState::T	oldState = BlockedState::Unblocked;
  BlockedState::T	newState = BlockedState::Blocked;
  BlockedReason::T	reason =
    BlockedReason::CxnFlowCtrl;
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

struct CloseEvt {
  LinkInfo		linkInfo;
  uint64_t		errorCode = 0;
  CloseInitiator::T	initiator = CloseInitiator::Local;
  CloseTrigger::T	trigger = CloseTrigger::Error;
  CloseReason::T	reason = CloseReason::None;
  CloseError::T		connectionError = CloseError::None;
  CloseError::T		applicationError = CloseError::None;
};

struct CxnStateEvt {
  LinkInfo		linkInfo;
  LinkState::T		oldState = LinkState::Starting;
  LinkState::T		newState = LinkState::Handshaking;
};

struct CxnStartedEvt {
  ZiSockAddr	local;
  ZiSockAddr	remote;
  LinkInfo	linkInfo;
};

struct EvtName {
  ZtEnum(EvtName, int8_t,
    CxnStarted, UDPTx, UDPRx, PktSent, PktRecv, PktBuf, PktDrop, PktsAcked,
    PktLost, MarkRetrans, MetricsUpd, TimerUpd, CongestionUpd, ECNUpd, KeyUpd,
    KeyDiscarded, ParamsSet, ALPNInfo, TLSAlert, RetrySent, RetryValid,
    TokenIssued, TokenValid, TokenReject, VersionInfo, StatelessReset,
    PktProtectFail, ZeroRTTAccept, ZeroRTTReject, TupleAssigned,
    PathValidated, MTUUpd, CIDUpd, StreamStateUpd, StreamDataMoved,
    CxnDataBlockedUpd,
    StreamDataBlockedUpd, CxnClosed, CxnStateUpd, MigrationUpd);
  ZtEnumMap(EvtName, JSON,
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
    "zquic:packet_protection_failed", "zquic:zero_rtt_accepted",
    "zquic:zero_rtt_rejected",
    "quic:tuple_assigned", "quic:path_validated", "quic:mtu_updated",
    "quic:connection_id_updated", "quic:stream_state_updated",
    "quic:stream_data_moved", "quic:connection_data_blocked_updated",
    "quic:stream_data_blocked_updated", "quic:connection_closed",
    "quic:connection_state_updated", "zquic:migration_updated");
};

} // namespace ZquicLog_

using ZquicLogDiag = ZquicLog_::Diag;
using ZquicLogParams = ZquicLog_::Params;

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
  static void stop() { instance()->stop_(false); }
  static void stopIdle() { instance()->stop_(true); }
  template <typename L>
  static void close(Trace &trace, L &&l) {
    instance()->close_(trace, ZuFwd<L>(l));
  }
  static void final(Trace &trace) { instance()->final_(trace); }
  static ZquicLogDiag diag() { return instance()->diag_(); }

  static void cxnStarted(
    Trace &trace, ZquicLog_::CxnStartedEvt event) {
    instance()->cxnStarted_(trace, ZuMv(event));
  }
  static void dgramSent(Trace &trace, ZquicLog_::DgramEvt event) {
    instance()->dgramSent_(trace, ZuMv(event));
  }
  static void dgramRecv(Trace &trace, ZquicLog_::DgramEvt event) {
    instance()->dgramRecv_(trace, ZuMv(event));
  }
  static void pktSent(Trace &trace, ZquicLog_::PktEvt event) {
    instance()->pktSent_(trace, ZuMv(event));
  }
  static void pktRecv(Trace &trace, ZquicLog_::PktEvt event) {
    instance()->pktRecv_(trace, ZuMv(event));
  }
  static void pktBuf(Trace &trace, ZquicLog_::PktEvt event) {
    instance()->pktBuf_(trace, ZuMv(event));
  }
  static void pktDrop(Trace &trace, ZquicLog_::PktEvt event) {
    instance()->pktDrop_(trace, ZuMv(event));
  }
  static void pktsAcked(Trace &trace, ZquicLog_::AckEvt event) {
    instance()->pktsAcked_(trace, ZuMv(event));
  }
  static void pktLost(Trace &trace, ZquicLog_::RecLogEvt event) {
    instance()->pktLost_(trace, ZuMv(event));
  }
  static void recPktLost(Trace &trace, ZquicLog_::RecLogEvt event) {
    instance()->recPktLost_(trace, ZuMv(event));
  }
  static void markRetrans(Trace &trace, ZquicLog_::RecLogEvt event) {
    instance()->markRetrans_(trace, ZuMv(event));
  }
  static void metricsUpd(Trace &trace, ZquicLog_::RecLogEvt event) {
    instance()->metricsUpd_(trace, ZuMv(event));
  }
  static void lossTimerUpd(Trace &trace, ZquicLog_::RecLogEvt event) {
    instance()->lossTimerUpd_(trace, ZuMv(event));
  }
  static void congStateUpd(Trace &trace, ZquicLog_::RecLogEvt event) {
    instance()->congStateUpd_(trace, ZuMv(event));
  }
  static void ecnStateUpd(Trace &trace, ZquicLog_::ECNEvt event) {
    instance()->ecnStateUpd_(trace, ZuMv(event));
  }
  static void keyUpdated(Trace &trace, ZquicLog_::SecEvt event) {
    instance()->keyUpdated_(trace, ZuMv(event));
  }
  static void keyRetired(Trace &trace, ZquicLog_::SecEvt event) {
    instance()->keyRetired_(trace, ZuMv(event));
  }
  static void paramsSet(Trace &trace, ZquicLog_::ParamsEvt event) {
    instance()->paramsSet_(trace, ZuMv(event));
  }
  static void alpnInfo(Trace &trace, ZquicLog_::SecEvt event) {
    instance()->alpnInfo_(trace, ZuMv(event));
  }
  static void versionInfo(Trace &trace, ZquicLog_::VersionEvt event) {
    instance()->versionInfo_(trace, ZuMv(event));
  }
  static void tlsAlert(Trace &trace, ZquicLog_::SecEvt event) {
    instance()->tlsAlert_(trace, ZuMv(event));
  }
  static void secEvt(
    Trace &trace, ZquicLog_::EvtName::T name,
    ZquicLog_::SecEvt event) {
    instance()->secEvt_(trace, name, ZuMv(event));
  }
  static void pathUpdated(Trace &trace, ZquicLog_::PathEvt event) {
    instance()->pathUpdated_(trace, ZuMv(event));
  }
  static void pathValidUpd(Trace &trace, ZquicLog_::PathEvt event) {
    instance()->pathValidUpd_(trace, ZuMv(event));
  }
  static void pmtudUpdated(Trace &trace, ZquicLog_::PathEvt event) {
    instance()->pmtudUpdated_(trace, ZuMv(event));
  }
  static void cidUpdated(Trace &trace, ZquicLog_::CIDEvt event) {
    instance()->cidUpdated_(trace, ZuMv(event));
  }
  static void migrationUpdated(
    Trace &trace, ZquicLog_::MigrationEvt event) {
    instance()->migrationUpdated_(trace, ZuMv(event));
  }
  static void streamStateUpd(Trace &trace, ZquicLog_::StreamEvt event) {
    instance()->streamStateUpd_(trace, ZuMv(event));
  }
  static void streamDataMoved(
    Trace &trace, ZquicLog_::StreamDataEvt event) {
    instance()->streamDataMoved_(trace, ZuMv(event));
  }
  static void cxnDataBlockedUpd(
    Trace &trace, ZquicLog_::BlockedEvt event) {
    instance()->cxnDataBlockedUpd_(trace, ZuMv(event));
  }
  static void streamDataBlockedUpd(
    Trace &trace, ZquicLog_::BlockedEvt event) {
    instance()->streamDataBlockedUpd_(trace, ZuMv(event));
  }
  static void cxnClosed(Trace &trace, ZquicLog_::CloseEvt event) {
    instance()->cxnClosed_(trace, ZuMv(event));
  }
  static void cxnStateUpd(Trace &trace, ZquicLog_::CxnStateEvt event) {
    instance()->cxnStateUpd_(trace, ZuMv(event));
  }
  template <typename L>
  static void log(Trace &trace, L &&l) {
    instance()->log_(trace, ZuFwd<L>(l));
  }

  void logCxnStarted(
    const ZquicLog_::CxnStartedEvt &event, ZuTime time) {
    writeCxnStarted_(event, time);
  }
  void logDgramSent(const ZquicLog_::DgramEvt &event, ZuTime time) {
    writeDatagramEvt_(ZquicLog_::EvtName::UDPTx, event, time);
  }
  void logDgramRecv(const ZquicLog_::DgramEvt &event, ZuTime time) {
    writeDatagramEvt_(ZquicLog_::EvtName::UDPRx, event, time);
  }
  void logPktSent(const ZquicLog_::PktEvt &event, ZuTime time) {
    writePktEvt_(ZquicLog_::EvtName::PktSent, event, time);
  }
  void logPktRecv(const ZquicLog_::PktEvt &event, ZuTime time) {
    writePktEvt_(ZquicLog_::EvtName::PktRecv, event, time);
  }
  void logPktBuf(const ZquicLog_::PktEvt &event, ZuTime time) {
    writePktEvt_(ZquicLog_::EvtName::PktBuf, event, time);
  }
  void logPktDrop(const ZquicLog_::PktEvt &event, ZuTime time) {
    writePktEvt_(ZquicLog_::EvtName::PktDrop, event, time);
  }
  void logPktsAcked(const ZquicLog_::AckEvt &event, ZuTime time) {
    writeAckEvt_(ZquicLog_::EvtName::PktsAcked, event, time);
  }
  void logPktLost(const ZquicLog_::RecLogEvt &event, ZuTime time) {
    writePktLost_(event, time);
  }
  void logRecPktLost(const ZquicLog_::RecLogEvt &event, ZuTime time) {
    writePktLost_(event, time);
  }
  void logMarkRetrans(
    const ZquicLog_::RecLogEvt &event, ZuTime time) {
    writeMarkRetrans_(event, time);
  }
  void logMetricsUpd(const ZquicLog_::RecLogEvt &event, ZuTime time) {
    writeRecMetrics_(event, time);
  }
  void logLossTimerUpd(const ZquicLog_::RecLogEvt &event, ZuTime time) {
    writeTimerEvt_(event, time);
  }
  void logCongStateUpd(
    const ZquicLog_::RecLogEvt &event, ZuTime time) {
    writeCongState_(event, time);
  }
  void logECNStateUpd(const ZquicLog_::ECNEvt &event, ZuTime time) {
    writeECNEvt_(event, time);
  }
  void logKeyUpdated(const ZquicLog_::SecEvt &event, ZuTime time) {
    writeKeyEvt_(ZquicLog_::EvtName::KeyUpd, event, time);
  }
  void logKeyRetired(const ZquicLog_::SecEvt &event, ZuTime time) {
    writeKeyEvt_(ZquicLog_::EvtName::KeyDiscarded, event, time);
  }
  void logParamsSet(
    const ZquicLog_::ParamsEvt &event, ZuTime time) {
    writeParams_(event, time);
  }
  void logALPNInfo(const ZquicLog_::SecEvt &event, ZuTime time) {
    writeALPNEvt_(event, time);
  }
  void logVersionInfo(
    const ZquicLog_::VersionEvt &event, ZuTime time) {
    writeVersion_(event, time);
  }
  void logTLSAlert(const ZquicLog_::SecEvt &event, ZuTime time) {
    writeSecEvt_(ZquicLog_::EvtName::TLSAlert, event, time);
  }
  void logSecEvt(
    ZquicLog_::EvtName::T name,
    const ZquicLog_::SecEvt &event, ZuTime time) {
    writeSecEvt_(name, event, time);
  }
  void logPathUpdated(const ZquicLog_::PathEvt &event, ZuTime time) {
    writePathEvt_(ZquicLog_::EvtName::TupleAssigned, event, time);
  }
  void logPathValid(
    const ZquicLog_::PathEvt &event, ZuTime time) {
    writePathValid_(event, time);
  }
  void logPMTUDUpdated(const ZquicLog_::PathEvt &event, ZuTime time) {
    writeMTUEvt_(event, time);
  }
  void logCIDUpdated(const ZquicLog_::CIDEvt &event, ZuTime time) {
    writeCIDEvt_(ZquicLog_::EvtName::CIDUpd, event, time);
  }
  void logMigrationUpdated(
    const ZquicLog_::MigrationEvt &event, ZuTime time) {
    writeMigrationEvt_(event, time);
  }
  void logStreamStateUpd(
    const ZquicLog_::StreamEvt &event, ZuTime time) {
    writeStreamEvt_(ZquicLog_::EvtName::StreamStateUpd, event, time);
  }
  void logStreamDataMoved(
    const ZquicLog_::StreamDataEvt &event, ZuTime time) {
    writeStreamData_(ZquicLog_::EvtName::StreamDataMoved, event, time);
  }
  void logCxnDataBlockedUpd(
    const ZquicLog_::BlockedEvt &event, ZuTime time) {
    writeCxnBlocked_(
      ZquicLog_::EvtName::CxnDataBlockedUpd, event, time);
  }
  void logStreamDataBlockedUpd(
    const ZquicLog_::BlockedEvt &event, ZuTime time) {
    writeStreamBlocked_(
      ZquicLog_::EvtName::StreamDataBlockedUpd, event, time);
  }
  void logCxnClosed(
    const ZquicLog_::CloseEvt &event, ZuTime time) {
    writeCloseEvt_(ZquicLog_::EvtName::CxnClosed, event, time);
  }
  void logCxnStateUpd(
    const ZquicLog_::CxnStateEvt &event, ZuTime time) {
    writeCxnState_(ZquicLog_::EvtName::CxnStateUpd, event, time);
  }

private:
  ZquicLogger();

  bool enabled_() const {
    Guard guard(m_lock);
    return m_thread;
  }
  bool init_(Trace &, const ZquicLogParams &, Zquic::Vantage::T);
  void start_();
  void stop_(bool);
  template <typename L>
  void close_(Trace &trace, L &&l) {
    if (!trace.configured) { l(); return; }
    if (enabled_()) {
      auto fn_ = [trace = &trace, l = ZuFwd<L>(l)](ZquicLogger *this_) mutable {
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
  void cxnStarted_(Trace &, ZquicLog_::CxnStartedEvt);
  void dgramSent_(Trace &, ZquicLog_::DgramEvt);
  void dgramRecv_(Trace &, ZquicLog_::DgramEvt);
  void pktSent_(Trace &, ZquicLog_::PktEvt);
  void pktRecv_(Trace &, ZquicLog_::PktEvt);
  void pktBuf_(Trace &, ZquicLog_::PktEvt);
  void pktDrop_(Trace &, ZquicLog_::PktEvt);
  void pktsAcked_(Trace &, ZquicLog_::AckEvt);
  void pktLost_(Trace &, ZquicLog_::RecLogEvt);
  void recPktLost_(Trace &, ZquicLog_::RecLogEvt);
  void markRetrans_(Trace &, ZquicLog_::RecLogEvt);
  void metricsUpd_(Trace &, ZquicLog_::RecLogEvt);
  void lossTimerUpd_(Trace &, ZquicLog_::RecLogEvt);
  void congStateUpd_(Trace &, ZquicLog_::RecLogEvt);
  void ecnStateUpd_(Trace &, ZquicLog_::ECNEvt);
  void keyUpdated_(Trace &, ZquicLog_::SecEvt);
  void keyRetired_(Trace &, ZquicLog_::SecEvt);
  void paramsSet_(Trace &, ZquicLog_::ParamsEvt);
  void alpnInfo_(Trace &, ZquicLog_::SecEvt);
  void versionInfo_(Trace &, ZquicLog_::VersionEvt);
  void tlsAlert_(Trace &, ZquicLog_::SecEvt);
  void secEvt_(Trace &, ZquicLog_::EvtName::T, ZquicLog_::SecEvt);
  void pathUpdated_(Trace &, ZquicLog_::PathEvt);
  void pathValidUpd_(Trace &, ZquicLog_::PathEvt);
  void pmtudUpdated_(Trace &, ZquicLog_::PathEvt);
  void cidUpdated_(Trace &, ZquicLog_::CIDEvt);
  void migrationUpdated_(Trace &, ZquicLog_::MigrationEvt);
  void streamStateUpd_(Trace &, ZquicLog_::StreamEvt);
  void streamDataMoved_(Trace &, ZquicLog_::StreamDataEvt);
  void cxnDataBlockedUpd_(Trace &, ZquicLog_::BlockedEvt);
  void streamDataBlockedUpd_(Trace &, ZquicLog_::BlockedEvt);
  void cxnClosed_(Trace &, ZquicLog_::CloseEvt);
  void cxnStateUpd_(Trace &, ZquicLog_::CxnStateEvt);
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
  void log_(Trace &trace, L &&l) {
    if (!enabled_()) return;
    ZuTime time = Zm::now();
    auto fn_ = [trace = &trace, l = ZuFwd<L>(l), time](ZquicLogger *this_) mutable {
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
    const ZquicLog_::CxnStartedEvt &, ZuTime);
  bool writeDatagramEvt_(
    ZquicLog_::EvtName::T, const ZquicLog_::DgramEvt &, ZuTime);
  bool writePktEvt_(
    ZquicLog_::EvtName::T, const ZquicLog_::PktEvt &, ZuTime);
  bool writeAckEvt_(
    ZquicLog_::EvtName::T, const ZquicLog_::AckEvt &, ZuTime);
  bool writePktLost_(const ZquicLog_::RecLogEvt &, ZuTime);
  bool writeMarkRetrans_(const ZquicLog_::RecLogEvt &, ZuTime);
  bool writeRecMetrics_(const ZquicLog_::RecLogEvt &, ZuTime);
  bool writeCongState_(const ZquicLog_::RecLogEvt &, ZuTime);
  bool writeTimerEvt_(const ZquicLog_::RecLogEvt &, ZuTime);
  bool writeECNEvt_(const ZquicLog_::ECNEvt &, ZuTime);
  bool writeKeyEvt_(
    ZquicLog_::EvtName::T, const ZquicLog_::SecEvt &, ZuTime);
  bool writeParams_(
    const ZquicLog_::ParamsEvt &, ZuTime);
  bool writeALPNEvt_(const ZquicLog_::SecEvt &, ZuTime);
  bool writeVersion_(const ZquicLog_::VersionEvt &, ZuTime);
  bool writeSecEvt_(
    ZquicLog_::EvtName::T, const ZquicLog_::SecEvt &, ZuTime);
  bool writePathEvt_(
    ZquicLog_::EvtName::T, const ZquicLog_::PathEvt &, ZuTime);
  bool writeMTUEvt_(const ZquicLog_::PathEvt &, ZuTime);
  bool writePathValid_(const ZquicLog_::PathEvt &, ZuTime);
  bool writeCIDEvt_(ZquicLog_::EvtName::T, const ZquicLog_::CIDEvt &, ZuTime);
  bool writeMigrationEvt_(const ZquicLog_::MigrationEvt &, ZuTime);
  bool writeStreamEvt_(
    ZquicLog_::EvtName::T, const ZquicLog_::StreamEvt &, ZuTime);
  bool writeStreamData_(
    ZquicLog_::EvtName::T, const ZquicLog_::StreamDataEvt &, ZuTime);
  bool writeCxnBlocked_(
    ZquicLog_::EvtName::T, const ZquicLog_::BlockedEvt &, ZuTime);
  bool writeStreamBlocked_(
    ZquicLog_::EvtName::T, const ZquicLog_::BlockedEvt &, ZuTime);
  bool writeCloseEvt_(
    ZquicLog_::EvtName::T, const ZquicLog_::CloseEvt &, ZuTime);
  bool writeCxnState_(
    ZquicLog_::EvtName::T, const ZquicLog_::CxnStateEvt &, ZuTime);

private:
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
  static void stopIdle() { }
  template <typename L>
  static void close(Trace &, L &&l) { l(); }
  static void final(Trace &) { }
  static ZquicLogDiag diag() { return {}; }
  static void cxnStarted(Trace &, ZquicLog_::CxnStartedEvt) { }
  static void dgramSent(Trace &, ZquicLog_::DgramEvt) { }
  static void dgramRecv(Trace &, ZquicLog_::DgramEvt) { }
  static void pktSent(Trace &, ZquicLog_::PktEvt) { }
  static void pktRecv(Trace &, ZquicLog_::PktEvt) { }
  static void pktBuf(Trace &, ZquicLog_::PktEvt) { }
  static void pktDrop(Trace &, ZquicLog_::PktEvt) { }
  static void pktsAcked(Trace &, ZquicLog_::AckEvt) { }
  static void pktLost(Trace &, ZquicLog_::RecLogEvt) { }
  static void recPktLost(Trace &, ZquicLog_::RecLogEvt) { }
  static void markRetrans(Trace &, ZquicLog_::RecLogEvt) { }
  static void metricsUpd(Trace &, ZquicLog_::RecLogEvt) { }
  static void lossTimerUpd(Trace &, ZquicLog_::RecLogEvt) { }
  static void congStateUpd(Trace &, ZquicLog_::RecLogEvt) { }
  static void ecnStateUpd(Trace &, ZquicLog_::ECNEvt) { }
  static void keyUpdated(Trace &, ZquicLog_::SecEvt) { }
  static void keyRetired(Trace &, ZquicLog_::SecEvt) { }
  static void paramsSet(Trace &, ZquicLog_::ParamsEvt) { }
  static void alpnInfo(Trace &, ZquicLog_::SecEvt) { }
  static void versionInfo(Trace &, ZquicLog_::VersionEvt) { }
  static void tlsAlert(Trace &, ZquicLog_::SecEvt) { }
  static void secEvt(
    Trace &, ZquicLog_::EvtName::T, ZquicLog_::SecEvt) { }
  static void pathUpdated(Trace &, ZquicLog_::PathEvt) { }
  static void pathValidUpd(Trace &, ZquicLog_::PathEvt) { }
  static void pmtudUpdated(Trace &, ZquicLog_::PathEvt) { }
  static void cidUpdated(Trace &, ZquicLog_::CIDEvt) { }
  static void migrationUpdated(Trace &, ZquicLog_::MigrationEvt) { }
  static void streamStateUpd(Trace &, ZquicLog_::StreamEvt) { }
  static void streamDataMoved(Trace &, ZquicLog_::StreamDataEvt) { }
  static void cxnDataBlockedUpd(Trace &, ZquicLog_::BlockedEvt) { }
  static void streamDataBlockedUpd(Trace &, ZquicLog_::BlockedEvt) { }
  static void cxnClosed(Trace &, ZquicLog_::CloseEvt) { }
  static void cxnStateUpd(Trace &, ZquicLog_::CxnStateEvt) { }
  template <typename L>
  static void log(Trace &, L &&) { }
};

#endif /* Zquic_DEBUG */

template <typename L>
inline void ZquicLog(ZquicLogger::Trace &trace, L &&l) {
  ZquicLogger::log(trace, ZuFwd<L>(l));
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
