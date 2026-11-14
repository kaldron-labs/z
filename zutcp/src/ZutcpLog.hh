//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC qlog

#ifndef Zutcp_HH
#error "include zlib/Zutcp.hh before this header"
#endif

#include <zlib/ZuTime.hh>
#include <zlib/ZuArray.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmEngine.hh>
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

namespace ZutcpLog_ {

using namespace Zutcp_;

struct Diag {
  uint64_t	recordsEnqueued = 0;
  uint64_t	recordsWritten = 0;
  uint64_t	recordsDropped = 0;
  uint64_t	ringBackPressure = 0;
  uint64_t	writerFailures = 0;
  uint64_t	bytesWritten = 0;
};

class Params {
public:
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

struct FrameEvt {
  QAckRanges		ackRanges;
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
  uint8_t		rangeCount = 0;
  bool			fin = false;

  struct Traits : public ZuBaseTraits<FrameEvt> {
    enum { IsPOD = 1 };
  };
  friend Traits ZuTraitsType(FrameEvt *);
};

struct Reason {
  ZtEnum(ZutcpAPI, Reason, int8_t,
    None, ParseLong, PacketLength, PrepareLong,
    UnsupportedLongType, DiscardedSpace, Duplicate,
    ParseShort, AntiAmp, ProbeAdmit,
    AppSend, FramePolicy);
  ZtEnumMap(ZutcpAPI, Reason, JSON,
    "", "parse_long", "packet_length", "prepare_long",
    "unsupported_long_type", "discarded_space", "duplicate", "parse_short",
    "anti_amplification", "probe_admission", "app_send",
    "frame_policy");
};

struct PktEvt {
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
  ZtEnum(ZutcpAPI, RecKind, int8_t,
    Aggregate, Loss, PTO, RTT, NewReno);
  ZtEnumMap(ZutcpAPI, RecKind, JSON,
    "aggregate", "loss", "pto", "rtt", "newreno");
};

struct RecReason {
  ZtEnum(ZutcpAPI, RecReason, int8_t,
    None, TimeThreshold, PacketThreshold, Canceled, Armed, NoLevel, Ack,
    PMTUDAck, Loss, PMTUDLoss, Expired, Probe, Backoff, ECNCE);
  ZtEnumMap(ZutcpAPI, RecReason, JSON,
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
  ZtEnum(ZutcpAPI, ECNState, int8_t, Unknown, Disabled, Testing, Capable, Failed);
  ZtEnumMap(ZutcpAPI, ECNState, JSON,
    "unknown", "disabled", "testing", "capable", "failed");
};

struct ECNReason {
  ZtEnum(ZutcpAPI, ECNReason, int8_t,
    AckECN, ECT0Decrease, ECT1Decrease, CEDecrease, ECTOverflow, CEOverflow,
    CounterExceedsAck, Probe, NoAckECN, MarkFailed, CE);
  ZtEnumMap(ZutcpAPI, ECNReason, JSON,
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

struct Initiator {
  ZtEnum(ZutcpAPI, Initiator, int8_t, Local, Remote);
  ZtEnumMap(ZutcpAPI, Initiator, JSON, "local", "remote");
};

struct ParamsEvt {
  CxnID			origDCID;
  CxnID			initialSCID;
  LinkInfo		linkInfo;
  uint64_t		maxIdleTimeout = 0;
  uint64_t		maxPayloadSize = 0;
  uint64_t		ackDelayExponent = 0;
  uint64_t		maxAckDelay = 0;
  uint64_t		initialMaxData = 0;
  Initiator::T		initiator = Initiator::Local;
};

struct VersionEvt {
  Versions		serverVersions;
  Versions		clientVersions;
  LinkInfo		linkInfo;
  uint32_t		chosenVersion = 0;
  bool			chosenVersionPresent = false;
};

struct PathKind {
  ZtEnum(ZutcpAPI, PathKind, int8_t, Path, PMTUD);
  ZtEnumMap(ZutcpAPI, PathKind, JSON,
    "path", "pmtud");
};

struct PathAction {
  ZtEnum(ZutcpAPI, PathAction, int8_t,
    Created, Updated, Received, Validated, Blocked, Expired, Hint, Acked, Lost, Sent);
  ZtEnumMap(ZutcpAPI, PathAction, JSON,
    "created", "updated", "received", "validated", "blocked", "expired", "hint",
    "acked", "lost", "sent");
};

struct PathReason {
  ZtEnum(ZutcpAPI, PathReason, int8_t,
    None, Client, Server, Initial, Datagram, Timeout, AntiAmp, ProbeAdmit,
    PathHint, Probe, Admission, SendFail);
  ZtEnumMap(ZutcpAPI, PathReason, JSON,
    "", "client", "server", "initial", "datagram", "timeout", "anti_amplification",
    "probe_admission", "path_hint", "probe", "admission", "send_failure");
};

struct PathEvt {
  LinkInfo		linkInfo;
  uint64_t		tupleID = 0;
  uint64_t		bytes = 0;
  uint64_t		antiAmplification = 0;
  uint32_t		mtu = 0;
  PathKind::T		kind = PathKind::Path;
  PathAction::T		action = PathAction::Updated;
  PathReason::T		reason = PathReason::None;
  bool			validated = false;
};

struct CIDKind {
  ZtEnum(ZutcpAPI, CIDKind, int8_t, CxnID);
  ZtEnumMap(ZutcpAPI, CIDKind, JSON, "connection_id");
};

struct CIDAction {
  ZtEnum(ZutcpAPI, CIDAction, int8_t, RouteBound, Tombstone);
  ZtEnumMap(ZutcpAPI, CIDAction, JSON, "route_bound", "tombstone");
};

struct CIDReason {
  ZtEnum(ZutcpAPI, CIDReason, int8_t, None, RouteInstall, RouteTombstone);
  ZtEnumMap(ZutcpAPI, CIDReason, JSON, "", "route_install", "route_tombstone");
};

struct CIDEvt {
  CxnID			cxnID;
  LinkInfo		linkInfo;
  CIDKind::T		kind = CIDKind::CxnID;
  CIDAction::T		action = CIDAction::RouteBound;
  CIDReason::T		reason = CIDReason::None;
  uint8_t		length = 0;
  bool			local = false;
  bool			associated = false;
};

struct BlockedState {
  ZtEnum(ZutcpAPI, BlockedState, int8_t, Blocked, Unblocked);
  ZtEnumMap(ZutcpAPI, BlockedState, JSON, "blocked", "unblocked");
};

struct BlockedReason {
  ZtEnum(ZutcpAPI, BlockedReason, int8_t,
    Scheduling, Pacing, AmpProtect, CongCtrl,
    CxnFlowCtrl, Application);
  ZtEnumMap(ZutcpAPI, BlockedReason, JSON,
    "scheduling", "pacing", "amplification_protection",
    "congestion_control", "connection_flow_control", "application");
};

struct BlockedEvt {
  LinkInfo		linkInfo;
  BlockedState::T	oldState = BlockedState::Unblocked;
  BlockedState::T	newState = BlockedState::Blocked;
  BlockedReason::T	reason =
    BlockedReason::CxnFlowCtrl;
};

struct CloseInitiator {
  ZtEnum(ZutcpAPI, CloseInitiator, int8_t, Local, Remote);
  ZtEnumMap(ZutcpAPI, CloseInitiator, JSON, "local", "remote");
};

struct CloseTrigger {
  ZtEnum(ZutcpAPI, CloseTrigger, int8_t,
    Application, Error, IdleTimeout, Aborted);
  ZtEnumMap(ZutcpAPI, CloseTrigger, JSON,
    "application", "error", "idle_timeout", "aborted");
};

struct CloseReason {
  ZtEnum(ZutcpAPI, CloseReason, int8_t,
    None, LocalClose, PeerCloseFrame, DrainExpired, Idle);
  ZtEnumMap(ZutcpAPI, CloseReason, JSON,
    "", "local_close", "peer_close_frame", "drain_expired", "idle");
};

struct CloseError {
  ZtEnum(ZutcpAPI, CloseError, int8_t,
    NoError, InternalError, CxnRefused, FlowControl,
    DataState, FinalSize, FrameEncoding, TransportParam,
    CxnIDLimit, ProtViolation, None, Unknown);
  ZtEnumMap(ZutcpAPI, CloseError, JSON,
    "no_error", "internal_error", "connection_refused",
    "flow_control_error", "data_state_error",
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
  ZtEnum(ZutcpAPI, EvtName, int8_t,
    CxnStarted, UDPTx, UDPRx, PktSent, PktRecv, PktBuf, PktDrop, PktsAcked,
    PktLost, MarkRetrans, MetricsUpd, TimerUpd, CongestionUpd, ECNUpd,
    ParamsSet, VersionInfo, TupleAssigned,
    MTUUpd, CIDUpd,
    CxnDataBlockedUpd,
    CxnClosed, CxnStateUpd);
  ZtEnumMap(ZutcpAPI, EvtName, JSON,
    "quic:connection_started", "quic:udp_datagrams_sent",
    "quic:udp_datagrams_received", "quic:packet_sent",
    "quic:packet_received", "quic:packet_buffered", "quic:packet_dropped",
    "quic:packets_acked", "quic:packet_lost",
    "quic:marked_for_retransmit", "quic:recovery_metrics_updated",
    "quic:timer_updated", "quic:congestion_state_updated",
    "quic:ecn_state_updated", "quic:parameters_set",
    "quic:version_information",
    "quic:tuple_assigned", "quic:mtu_updated",
    "quic:connection_id_updated", "quic:connection_data_blocked_updated",
    "quic:connection_closed",
    "quic:connection_state_updated");
};

} // namespace ZutcpLog_

using ZutcpLogDiag = ZutcpLog_::Diag;
using ZutcpLogParams = ZutcpLog_::Params;

#ifdef Zutcp_DEBUG

class ZutcpAPI ZutcpLogSink {
public:
  bool init(ZuCSpan, unsigned);
  void final();
  bool write(ZuCSpan);

private:
  Zi::Path	m_path;
  ZiFile	m_file;
};

class ZutcpAPI ZutcpLogger : private ZmEngine<ZutcpLogger> {
friend ZmEngine<ZutcpLogger>;
  using Engine = ZmEngine<ZutcpLogger>;
  using Lock = ZmPLock;
  using Guard = ZmGuard<Lock>;
  ZuDerive(Ring, (ZmRing<ZmRingMW<true>>));
  using Fn = ZmRingFn<ZutcpLogger *>;
  ZmQueueDerive(Queue_, Fn, ZmQueueHeapID<"Zutcp.Log.Queue">);
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
  using CtrlFn = ZmFn<void(bool), ZmFnHeapID<"ZmEngine.CtrlFn">>;

  struct Trace {
    bool		configured = false;
    bool		sinkOpened = false;
    bool		headerWritten = false;
    Zutcp::Vantage::T	vantage = Zutcp::Vantage::Unknown;
    ZutcpLogParams	params;
    ZutcpLogSink		sink;
  };

  ~ZutcpLogger();

  ZutcpLogger(const ZutcpLogger &) = delete;
  ZutcpLogger &operator =(const ZutcpLogger &) = delete;

  static ZutcpLogger *instance();

  static bool enabled() { return instance()->enabled_(); }
  static bool init(
    Trace &trace, const ZutcpLogParams &params, Zutcp::Vantage::T vantage) {
    return instance()->init_(trace, params, vantage);
  }
  static bool start() { return instance()->Engine::start(); }
  static void start(CtrlFn fn) {
    instance()->Engine::start(ZuMv(fn));
  }
  static bool stop() { return instance()->Engine::stop(); }
  static void stop(CtrlFn fn) {
    instance()->Engine::stop(ZuMv(fn));
  }
  static void stopIdle(CtrlFn fn) {
    instance()->stopIdle_(ZuMv(fn));
  }
  template <typename L>
  static void close(Trace &trace, L &&l) {
    instance()->close_(trace, ZuFwd<L>(l));
  }
  static void final(Trace &trace) { instance()->final_(trace); }
  static ZutcpLogDiag diag() { return instance()->diag_(); }

  static void cxnStarted(
    Trace &trace, ZutcpLog_::CxnStartedEvt event) {
    instance()->cxnStarted_(trace, ZuMv(event));
  }
  static void dgramSent(Trace &trace, ZutcpLog_::DgramEvt event) {
    instance()->dgramSent_(trace, ZuMv(event));
  }
  static void dgramRecv(Trace &trace, ZutcpLog_::DgramEvt event) {
    instance()->dgramRecv_(trace, ZuMv(event));
  }
  static void pktSent(Trace &trace, ZutcpLog_::PktEvt event) {
    instance()->pktSent_(trace, ZuMv(event));
  }
  static void pktRecv(Trace &trace, ZutcpLog_::PktEvt event) {
    instance()->pktRecv_(trace, ZuMv(event));
  }
  static void pktBuf(Trace &trace, ZutcpLog_::PktEvt event) {
    instance()->pktBuf_(trace, ZuMv(event));
  }
  static void pktDrop(Trace &trace, ZutcpLog_::PktEvt event) {
    instance()->pktDrop_(trace, ZuMv(event));
  }
  static void pktsAcked(Trace &trace, ZutcpLog_::AckEvt event) {
    instance()->pktsAcked_(trace, ZuMv(event));
  }
  static void pktLost(Trace &trace, ZutcpLog_::RecLogEvt event) {
    instance()->pktLost_(trace, ZuMv(event));
  }
  static void recPktLost(Trace &trace, ZutcpLog_::RecLogEvt event) {
    instance()->recPktLost_(trace, ZuMv(event));
  }
  static void markRetrans(Trace &trace, ZutcpLog_::RecLogEvt event) {
    instance()->markRetrans_(trace, ZuMv(event));
  }
  static void metricsUpd(Trace &trace, ZutcpLog_::RecLogEvt event) {
    instance()->metricsUpd_(trace, ZuMv(event));
  }
  static void lossTimerUpd(Trace &trace, ZutcpLog_::RecLogEvt event) {
    instance()->lossTimerUpd_(trace, ZuMv(event));
  }
  static void congStateUpd(Trace &trace, ZutcpLog_::RecLogEvt event) {
    instance()->congStateUpd_(trace, ZuMv(event));
  }
  static void ecnStateUpd(Trace &trace, ZutcpLog_::ECNEvt event) {
    instance()->ecnStateUpd_(trace, ZuMv(event));
  }


  static void paramsSet(Trace &trace, ZutcpLog_::ParamsEvt event) {
    instance()->paramsSet_(trace, ZuMv(event));
  }

  static void versionInfo(Trace &trace, ZutcpLog_::VersionEvt event) {
    instance()->versionInfo_(trace, ZuMv(event));
  }


  static void pathUpdated(Trace &trace, ZutcpLog_::PathEvt event) {
    instance()->pathUpdated_(trace, ZuMv(event));
  }

  static void pmtudUpdated(Trace &trace, ZutcpLog_::PathEvt event) {
    instance()->pmtudUpdated_(trace, ZuMv(event));
  }
  static void cidUpdated(Trace &trace, ZutcpLog_::CIDEvt event) {
    instance()->cidUpdated_(trace, ZuMv(event));
  }

  static void cxnDataBlockedUpd(
    Trace &trace, ZutcpLog_::BlockedEvt event) {
    instance()->cxnDataBlockedUpd_(trace, ZuMv(event));
  }
  static void cxnClosed(Trace &trace, ZutcpLog_::CloseEvt event) {
    instance()->cxnClosed_(trace, ZuMv(event));
  }
  static void cxnStateUpd(Trace &trace, ZutcpLog_::CxnStateEvt event) {
    instance()->cxnStateUpd_(trace, ZuMv(event));
  }
  template <typename L>
  static void log(Trace &trace, L &&l) {
    instance()->log_(trace, ZuFwd<L>(l));
  }

  void logCxnStarted(
    const ZutcpLog_::CxnStartedEvt &event, ZuTime time) {
    writeCxnStarted_(event, time);
  }
  void logDgramSent(const ZutcpLog_::DgramEvt &event, ZuTime time) {
    writeDatagramEvt_(ZutcpLog_::EvtName::UDPTx, event, time);
  }
  void logDgramRecv(const ZutcpLog_::DgramEvt &event, ZuTime time) {
    writeDatagramEvt_(ZutcpLog_::EvtName::UDPRx, event, time);
  }
  void logPktSent(const ZutcpLog_::PktEvt &event, ZuTime time) {
    writePktEvt_(ZutcpLog_::EvtName::PktSent, event, time);
  }
  void logPktRecv(const ZutcpLog_::PktEvt &event, ZuTime time) {
    writePktEvt_(ZutcpLog_::EvtName::PktRecv, event, time);
  }
  void logPktBuf(const ZutcpLog_::PktEvt &event, ZuTime time) {
    writePktEvt_(ZutcpLog_::EvtName::PktBuf, event, time);
  }
  void logPktDrop(const ZutcpLog_::PktEvt &event, ZuTime time) {
    writePktEvt_(ZutcpLog_::EvtName::PktDrop, event, time);
  }
  void logPktsAcked(const ZutcpLog_::AckEvt &event, ZuTime time) {
    writeAckEvt_(ZutcpLog_::EvtName::PktsAcked, event, time);
  }
  void logPktLost(const ZutcpLog_::RecLogEvt &event, ZuTime time) {
    writePktLost_(event, time);
  }
  void logRecPktLost(const ZutcpLog_::RecLogEvt &event, ZuTime time) {
    writePktLost_(event, time);
  }
  void logMarkRetrans(
    const ZutcpLog_::RecLogEvt &event, ZuTime time) {
    writeMarkRetrans_(event, time);
  }
  void logMetricsUpd(const ZutcpLog_::RecLogEvt &event, ZuTime time) {
    writeRecMetrics_(event, time);
  }
  void logLossTimerUpd(const ZutcpLog_::RecLogEvt &event, ZuTime time) {
    writeTimerEvt_(event, time);
  }
  void logCongStateUpd(
    const ZutcpLog_::RecLogEvt &event, ZuTime time) {
    writeCongState_(event, time);
  }
  void logECNStateUpd(const ZutcpLog_::ECNEvt &event, ZuTime time) {
    writeECNEvt_(event, time);
  }


  void logParamsSet(
    const ZutcpLog_::ParamsEvt &event, ZuTime time) {
    writeParams_(event, time);
  }

  void logVersionInfo(
    const ZutcpLog_::VersionEvt &event, ZuTime time) {
    writeVersion_(event, time);
  }


  void logPathUpdated(const ZutcpLog_::PathEvt &event, ZuTime time) {
    writePathEvt_(ZutcpLog_::EvtName::TupleAssigned, event, time);
  }

  void logPMTUDUpdated(const ZutcpLog_::PathEvt &event, ZuTime time) {
    writeMTUEvt_(event, time);
  }
  void logCIDUpdated(const ZutcpLog_::CIDEvt &event, ZuTime time) {
    writeCIDEvt_(ZutcpLog_::EvtName::CIDUpd, event, time);
  }

  void logCxnDataBlockedUpd(
    const ZutcpLog_::BlockedEvt &event, ZuTime time) {
    writeCxnBlocked_(
      ZutcpLog_::EvtName::CxnDataBlockedUpd, event, time);
  }
  void logCxnClosed(
    const ZutcpLog_::CloseEvt &event, ZuTime time) {
    writeCloseEvt_(ZutcpLog_::EvtName::CxnClosed, event, time);
  }
  void logCxnStateUpd(
    const ZutcpLog_::CxnStateEvt &event, ZuTime time) {
    writeCxnState_(ZutcpLog_::EvtName::CxnStateUpd, event, time);
  }

private:
  ZutcpLogger();

  bool enabled_() const { return state() == ZmEngineState::Running; }
  bool init_(Trace &, const ZutcpLogParams &, Zutcp::Vantage::T);
  void start_();
  void stop_();
  void stopIdle_(CtrlFn);
  void wake();
  template <typename L>
  bool spawn(L &&l) {
    Guard guard(m_lock);
    // A completion callback may start the successor before this thread exits.
    auto previous = ZuMv(m_thread);
    m_thread = ZmThread{
      [l = ZuFwd<L>(l), previous]() mutable {
	l();
	if (previous) previous.join();
      }, ZmThreadParams().name("zutcp-qlog").priority(ZmThreadPriority::Low)};
    if (!m_thread) {
      m_thread = ZuMv(previous);
      return false;
    }
    return true;
  }
  template <typename L>
  void close_(Trace &trace, L &&l) {
    Guard guard(m_lock);
    if (!trace.configured) { guard.unlock(); l(); return; }
    if (enabled_()) {
      auto fn_ = [trace = &trace, l = ZuFwd<L>(l)](ZutcpLogger *this_) mutable {
	this_->closeTrace_(*trace);
	l();
      };
      Fn fn{fn_};
      // The writer can close the last trace and stop itself as soon as this
      // record is visible. Keep its ring mapped through push2's bookkeeping.
      log__(fn);
    } else {
      guard.unlock();
      closeTrace_(trace);
      l();
    }
  }
  void final_(Trace &);
  ZutcpLogDiag diag_() const;
  void cxnStarted_(Trace &, ZutcpLog_::CxnStartedEvt);
  void dgramSent_(Trace &, ZutcpLog_::DgramEvt);
  void dgramRecv_(Trace &, ZutcpLog_::DgramEvt);
  void pktSent_(Trace &, ZutcpLog_::PktEvt);
  void pktRecv_(Trace &, ZutcpLog_::PktEvt);
  void pktBuf_(Trace &, ZutcpLog_::PktEvt);
  void pktDrop_(Trace &, ZutcpLog_::PktEvt);
  void pktsAcked_(Trace &, ZutcpLog_::AckEvt);
  void pktLost_(Trace &, ZutcpLog_::RecLogEvt);
  void recPktLost_(Trace &, ZutcpLog_::RecLogEvt);
  void markRetrans_(Trace &, ZutcpLog_::RecLogEvt);
  void metricsUpd_(Trace &, ZutcpLog_::RecLogEvt);
  void lossTimerUpd_(Trace &, ZutcpLog_::RecLogEvt);
  void congStateUpd_(Trace &, ZutcpLog_::RecLogEvt);
  void ecnStateUpd_(Trace &, ZutcpLog_::ECNEvt);
  void paramsSet_(Trace &, ZutcpLog_::ParamsEvt);
  void versionInfo_(Trace &, ZutcpLog_::VersionEvt);
  void pathUpdated_(Trace &, ZutcpLog_::PathEvt);
  void pmtudUpdated_(Trace &, ZutcpLog_::PathEvt);
  void cidUpdated_(Trace &, ZutcpLog_::CIDEvt);
  void cxnDataBlockedUpd_(Trace &, ZutcpLog_::BlockedEvt);
  void cxnClosed_(Trace &, ZutcpLog_::CloseEvt);
  void cxnStateUpd_(Trace &, ZutcpLog_::CxnStateEvt);
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
    auto fn_ = [trace = &trace, l = ZuFwd<L>(l), time](ZutcpLogger *this_) mutable {
      if (!this_->useTrace(trace)) return;
      l(*this_, time);
    };
    Fn fn{fn_};
    log__(fn);
  }

  void work_();

  bool tryPush_(Fn &);
  bool useTrace(Trace *);
  void closeTrace_(Trace &);
  bool write_(Trace *, ZuCSpan);
  bool writeHeader_(Trace *);
  bool writeCxnStarted_(
    const ZutcpLog_::CxnStartedEvt &, ZuTime);
  bool writeDatagramEvt_(
    ZutcpLog_::EvtName::T, const ZutcpLog_::DgramEvt &, ZuTime);
  bool writePktEvt_(
    ZutcpLog_::EvtName::T, const ZutcpLog_::PktEvt &, ZuTime);
  bool writeAckEvt_(
    ZutcpLog_::EvtName::T, const ZutcpLog_::AckEvt &, ZuTime);
  bool writePktLost_(const ZutcpLog_::RecLogEvt &, ZuTime);
  bool writeMarkRetrans_(const ZutcpLog_::RecLogEvt &, ZuTime);
  bool writeRecMetrics_(const ZutcpLog_::RecLogEvt &, ZuTime);
  bool writeCongState_(const ZutcpLog_::RecLogEvt &, ZuTime);
  bool writeTimerEvt_(const ZutcpLog_::RecLogEvt &, ZuTime);
  bool writeECNEvt_(const ZutcpLog_::ECNEvt &, ZuTime);
  bool writeParams_(
    const ZutcpLog_::ParamsEvt &, ZuTime);
  bool writeVersion_(const ZutcpLog_::VersionEvt &, ZuTime);
  bool writePathEvt_(
    ZutcpLog_::EvtName::T, const ZutcpLog_::PathEvt &, ZuTime);
  bool writeMTUEvt_(const ZutcpLog_::PathEvt &, ZuTime);
  bool writeCIDEvt_(ZutcpLog_::EvtName::T, const ZutcpLog_::CIDEvt &, ZuTime);
  bool writeCxnBlocked_(
    ZutcpLog_::EvtName::T, const ZutcpLog_::BlockedEvt &, ZuTime);
  bool writeCloseEvt_(
    ZutcpLog_::EvtName::T, const ZutcpLog_::CloseEvt &, ZuTime);
  bool writeCxnState_(
    ZutcpLog_::EvtName::T, const ZutcpLog_::CxnStateEvt &, ZuTime);

private:
  unsigned		m_configured = 0;
  ZutcpLogParams	m_params;
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

#else /* Zutcp_DEBUG */

struct ZutcpLogger {
  using CtrlFn = ZmFn<void(bool), ZmFnHeapID<"ZmEngine.CtrlFn">>;

  struct Trace { };
  static constexpr bool enabled() { return false; }
  static bool init(
    Trace &, const ZutcpLogParams &, Zutcp::Vantage::T) {
    return true;
  }
  static bool start() { return true; }
  static void start(CtrlFn fn) { fn(true); }
  static bool stop() { return true; }
  static void stop(CtrlFn fn) { fn(true); }
  static void stopIdle(CtrlFn fn) { fn(true); }
  template <typename L>
  static void close(Trace &, L &&l) { l(); }
  static void final(Trace &) { }
  static ZutcpLogDiag diag() { return {}; }
  static void cxnStarted(Trace &, ZutcpLog_::CxnStartedEvt) { }
  static void dgramSent(Trace &, ZutcpLog_::DgramEvt) { }
  static void dgramRecv(Trace &, ZutcpLog_::DgramEvt) { }
  static void pktSent(Trace &, ZutcpLog_::PktEvt) { }
  static void pktRecv(Trace &, ZutcpLog_::PktEvt) { }
  static void pktBuf(Trace &, ZutcpLog_::PktEvt) { }
  static void pktDrop(Trace &, ZutcpLog_::PktEvt) { }
  static void pktsAcked(Trace &, ZutcpLog_::AckEvt) { }
  static void pktLost(Trace &, ZutcpLog_::RecLogEvt) { }
  static void recPktLost(Trace &, ZutcpLog_::RecLogEvt) { }
  static void markRetrans(Trace &, ZutcpLog_::RecLogEvt) { }
  static void metricsUpd(Trace &, ZutcpLog_::RecLogEvt) { }
  static void lossTimerUpd(Trace &, ZutcpLog_::RecLogEvt) { }
  static void congStateUpd(Trace &, ZutcpLog_::RecLogEvt) { }
  static void ecnStateUpd(Trace &, ZutcpLog_::ECNEvt) { }


  static void paramsSet(Trace &, ZutcpLog_::ParamsEvt) { }

  static void versionInfo(Trace &, ZutcpLog_::VersionEvt) { }


  static void pathUpdated(Trace &, ZutcpLog_::PathEvt) { }

  static void pmtudUpdated(Trace &, ZutcpLog_::PathEvt) { }
  static void cidUpdated(Trace &, ZutcpLog_::CIDEvt) { }

  static void cxnDataBlockedUpd(Trace &, ZutcpLog_::BlockedEvt) { }
  static void cxnClosed(Trace &, ZutcpLog_::CloseEvt) { }
  static void cxnStateUpd(Trace &, ZutcpLog_::CxnStateEvt) { }
  template <typename L>
  static void log(Trace &, L &&) { }
};

#endif /* Zutcp_DEBUG */

template <typename L>
inline void ZutcpLog(ZutcpLogger::Trace &trace, L &&l) {
  ZutcpLogger::log(trace, ZuFwd<L>(l));
}

#ifdef Zutcp_DEBUG
#define ZutcpLOG(trace, msg) \
  do { \
    if (ZutcpLogger::enabled()) { \
      using namespace ZutcpLog_; \
      ZutcpLog(trace, msg); \
    } \
  } while (0)
#else
#define ZutcpLOG(trace, msg) do { } while (0)
#endif
