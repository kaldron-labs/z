//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC qlog

#include <zlib/ZmSingleton.hh>

#include <zlib/ZtJSON.hh>
#include <zlib/ZtBuiltin.hh>

#include <zlib/ZquicLog.hh>

#ifdef Zquic_DEBUG

struct QLogVantagePoint {
  ZuCSpan	type;
};

ZtStruct((QLogVantagePoint, JSON),
  (((type)), (String)));

struct QLogImplementation {
  ZuCSpan	name;
  ZuCSpan	version;
};

ZtStruct((QLogImplementation, JSON),
  (((name)), (String)),
  (((version)), (String)));

struct QLogCommonFields {
  ZuBSpan	originalDCID;
  ZuBSpan	groupID;
  ZuBSpan	dcid;
  ZuBSpan	scid;
};

ZtStruct((QLogCommonFields, JSON),
  (((originalDCID), (JSON::ID<"ODCID">, JSON::Hex)), (Bytes)),
  (((groupID), (JSON::ID<"group_id">, JSON::Hex)), (Bytes)),
  (((dcid), (JSON::ID<"DCID">, JSON::Hex)), (Bytes)),
  (((scid), (JSON::ID<"SCID">, JSON::Hex)), (Bytes)));

struct QLogTrace {
  QLogVantagePoint	vantagePoint;
  QLogImplementation	implementation;
  QLogCommonFields	commonFields;
};

ZtStruct((QLogTrace, JSON),
  (((vantagePoint), (JSON::ID<"vantage_point">)), (UDT)),
  (((implementation)), (UDT)),
  (((commonFields), (JSON::ID<"common_fields">)), (UDT)));

struct QLogHeader {
  ZuCSpan	qlogVersion;
  ZuCSpan	qlogFormat;
  ZuCSpan	title;
  QLogTrace	trace;
};

ZtStruct((QLogHeader, JSON),
  (((qlogVersion), (JSON::ID<"qlog_version">)), (String)),
  (((qlogFormat), (JSON::ID<"qlog_format">)), (String)),
  (((title)), (String)),
  (((trace)), (UDT)));

struct QLogLifecycleEvent {
  uint64_t	time = 0;
  ZquicLogLifecycle::T name = ZquicLogLifecycle::Started;
};

ZtStruct((QLogLifecycleEvent, JSON),
  (((time)), (UInt64)),
  (((name), (Enum<ZquicLogLifecycle::JSON>)), (Int8)));

struct QLogDatagramData {
	uint64_t	size = 0;
	Zquic::EcnMark::T ecn = Zquic::EcnMark::N;
};

ZtStruct((QLogDatagramData, JSON),
  (((size)), (UInt64)),
  (((ecn), (Enum<Zquic::EcnMark::JSON>)), (Int8)));

struct QLogDatagramEvent {
  uint64_t	time = 0;
  ZuCSpan	name;
  QLogDatagramData data;
};

ZtStruct((QLogDatagramEvent, JSON),
  (((time)), (UInt64)),
  (((name)), (String)),
  (((data)), (UDT)));

struct QLogFrameData {
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

  struct Traits : public ZuBaseTraits<QLogFrameData> {
    enum { IsPOD = 1 };
  };
  friend Traits ZuTraitsType(QLogFrameData *);
};

ZtStruct((QLogFrameData, JSON),
  (((type), (Enum<Zquic::FrameType::JSON>)), (Int8)),
  (((streamID), (JSON::ID<"stream_id">)), (UInt64)),
  (((offset)), (UInt64)),
  (((length)), (UInt64)),
  (((value)), (UInt64)),
  (((errorCode), (JSON::ID<"error_code">)), (UInt64)),
  (((largestAcked), (JSON::ID<"largest_acked">)), (UInt64)),
  (((ackDelayUS), (JSON::ID<"ack_delay_us">)), (UInt64)),
  (((ect0)), (UInt64)),
  (((ect1)), (UInt64)),
  (((ce)), (UInt64)),
  (((rangeCount), (JSON::ID<"range_count">)), (UInt8)),
  (((fin)), (Bool)));

ZuDerive(QLogFrameArray,
  (ZuArray<QLogFrameData, ZquicLogFrameMax>));
inline ZtJSON::AsArray<ZtFieldTC::UDT> ZtJSON_Fmt(QLogFrameArray *);

struct QLogPacketData {
	Zquic::PktType::T packetType = Zquic::PktType::Initial;
	Zquic::PktNumSpace::T packetSpace = Zquic::PktNumSpace::Initial;
	uint64_t	packetNumber = 0;
	uint64_t	packetSize = 0;
	uint64_t	payloadSize = 0;
	Zquic::EcnMark::T ecn = Zquic::EcnMark::N;
  ZquicLogPacketEvent::Reason::T reason = ZquicLogPacketEvent::Reason::None;
  uint64_t	bytesInFlight = 0;
  uint8_t	frameCount = 0;
  bool		framesTruncated = false;
  QLogFrameArray frames;
  bool		ackEliciting = false;
};

ZtStruct((QLogPacketData, JSON),
  (((packetType), (JSON::ID<"packet_type">, Enum<Zquic::PktType::JSON>)),
      (Int8)),
  (((packetSpace), (JSON::ID<"packet_space">, Enum<Zquic::PktNumSpace::JSON>)),
      (Int8)),
  (((packetNumber), (JSON::ID<"packet_number">)), (UInt64)),
  (((packetSize), (JSON::ID<"packet_size">)), (UInt64)),
  (((payloadSize), (JSON::ID<"payload_size">)), (UInt64)),
  (((ecn), (Enum<Zquic::EcnMark::JSON>)), (Int8)),
  (((reason), (Enum<ZquicLogPacketEvent::Reason::JSON>)), (Int8)),
  (((bytesInFlight), (JSON::ID<"bytes_in_flight">)), (UInt64)),
  (((frameCount), (JSON::ID<"frame_count">)), (UInt8)),
  (((framesTruncated), (JSON::ID<"frames_truncated">)), (Bool)),
  (((frames)), (UDT)),
  (((ackEliciting), (JSON::ID<"ack_eliciting">)), (Bool)));

struct QLogPacketEvent {
  uint64_t	time = 0;
  ZuCSpan	name;
  QLogPacketData data;
};

ZtStruct((QLogPacketEvent, JSON),
  (((time)), (UInt64)),
  (((name)), (String)),
  (((data)), (UDT)));

struct QLogAckData {
	Zquic::PktNumSpace::T packetSpace = Zquic::PktNumSpace::Initial;
  uint64_t	largestAcked = 0;
  uint64_t	ackDelayUS = 0;
  uint64_t	ackedBytes = 0;
  uint64_t	lostBytes = 0;
  uint8_t	rangeCount = 0;
  uint8_t	ackedFrames = 0;
  uint8_t	lostFrames = 0;
};

ZtStruct((QLogAckData, JSON),
  (((packetSpace), (JSON::ID<"packet_space">, Enum<Zquic::PktNumSpace::JSON>)),
      (Int8)),
  (((largestAcked), (JSON::ID<"largest_acked">)), (UInt64)),
  (((ackDelayUS), (JSON::ID<"ack_delay_us">)), (UInt64)),
  (((ackedBytes), (JSON::ID<"acked_bytes">)), (UInt64)),
  (((lostBytes), (JSON::ID<"lost_bytes">)), (UInt64)),
  (((rangeCount), (JSON::ID<"range_count">)), (UInt8)),
  (((ackedFrames), (JSON::ID<"acked_frames">)), (UInt8)),
  (((lostFrames), (JSON::ID<"lost_frames">)), (UInt8)));

struct QLogAckEvent {
  uint64_t	time = 0;
  ZuCSpan	name;
  QLogAckData data;
};

ZtStruct((QLogAckEvent, JSON),
  (((time)), (UInt64)),
  (((name)), (String)),
  (((data)), (UDT)));

struct QLogRecoveryData {
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

ZtStruct((QLogRecoveryData, JSON),
  (((kind), (Enum<ZquicLogRecoveryKind::JSON>)), (Int8)),
  (((packetSpace), (JSON::ID<"packet_space">, Enum<Zquic::PktNumSpace::JSON>)),
      (Int8)),
  (((reason), (Enum<ZquicLogRecoveryReason::JSON>)), (Int8)),
  (((value)), (UInt64)),
  (((packetNumber), (JSON::ID<"packet_number">)), (UInt64)),
  (((bytes)), (UInt64)),
  (((deadlineUS), (JSON::ID<"deadline_us">)), (UInt64)),
  (((latestRTTUS), (JSON::ID<"latest_rtt_us">)), (UInt64)),
  (((smoothedRTTUS), (JSON::ID<"smoothed_rtt_us">)), (UInt64)),
  (((rttVarianceUS), (JSON::ID<"rtt_variance_us">)), (UInt64)),
  (((minRTTUS), (JSON::ID<"min_rtt_us">)), (UInt64)),
  (((cwnd)), (UInt64)),
  (((ssthresh)), (UInt64)),
  (((bytesInFlight), (JSON::ID<"bytes_in_flight">)), (UInt64)),
  (((frameCount), (JSON::ID<"frame_count">)), (UInt8)));

struct QLogRecoveryEvent {
  uint64_t	time = 0;
  ZuCSpan	name;
  QLogRecoveryData data;
};

ZtStruct((QLogRecoveryEvent, JSON),
  (((time)), (UInt64)),
  (((name)), (String)),
  (((data)), (UDT)));

struct QLogECNData {
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

ZtStruct((QLogECNData, JSON),
  (((packetSpace), (JSON::ID<"packet_space">, Enum<Zquic::PktNumSpace::JSON>)),
      (Int8)),
  (((state), (Enum<ZquicLogECNState::JSON>)), (Int8)),
  (((reason), (Enum<ZquicLogECNReason::JSON>)), (Int8)),
  (((ect0)), (UInt64)),
  (((ect1)), (UInt64)),
  (((ce)), (UInt64)),
  (((previousECT0), (JSON::ID<"previous_ect0">)), (UInt64)),
  (((previousECT1), (JSON::ID<"previous_ect1">)), (UInt64)),
  (((previousCE), (JSON::ID<"previous_ce">)), (UInt64)),
  (((largestAcked), (JSON::ID<"largest_acked">)), (UInt64)),
  (((disabled)), (Bool)));

struct QLogECNEvent {
  uint64_t	time = 0;
  ZuCSpan	name;
  QLogECNData data;
};

ZtStruct((QLogECNEvent, JSON),
  (((time)), (UInt64)),
  (((name)), (String)),
  (((data)), (UDT)));

struct QLogSecurityData {
	ZquicLogSecurityKind::T kind = ZquicLogSecurityKind::TLS;
	Zquic::PktNumSpace::T packetSpace = Zquic::PktNumSpace::Initial;
  ZquicLogSecurityKeyType::T keyType = ZquicLogSecurityKeyType::None;
  ZquicLogSecurityTrigger::T trigger = ZquicLogSecurityTrigger::None;
  ZeString	alpn;
  ZquicLogSecurityReason::T reason = ZquicLogSecurityReason::None;
  uint64_t	value = 0;
  bool		success = true;
};

ZtStruct((QLogSecurityData, JSON),
  (((kind), (Enum<ZquicLogSecurityKind::JSON>)), (Int8)),
  (((packetSpace), (JSON::ID<"packet_space">, Enum<Zquic::PktNumSpace::JSON>)),
      (Int8)),
  (((keyType), (JSON::ID<"key_type">, Enum<ZquicLogSecurityKeyType::JSON>)),
      (Int8)),
  (((trigger), (Enum<ZquicLogSecurityTrigger::JSON>)), (Int8)),
  (((alpn)), (String)),
  (((reason), (Enum<ZquicLogSecurityReason::JSON>)), (Int8)),
  (((value)), (UInt64)),
  (((success)), (Bool)));

struct QLogSecurityEvent {
  uint64_t	time = 0;
  ZuCSpan	name;
  QLogSecurityData data;
};

ZtStruct((QLogSecurityEvent, JSON),
  (((time)), (UInt64)),
  (((name)), (String)),
  (((data)), (UDT)));

struct QLogPathData {
  ZquicLogPathKind::T kind = ZquicLogPathKind::Path;
  ZquicLogPathAction::T action = ZquicLogPathAction::Updated;
  ZquicLogPathReason::T reason = ZquicLogPathReason::None;
  uint64_t	bytes = 0;
  uint64_t	antiAmplification = 0;
  uint64_t	deadlineUS = 0;
  uint32_t	mtu = 0;
  bool		validated = false;
};

ZtStruct((QLogPathData, JSON),
  (((kind), (Enum<ZquicLogPathKind::JSON>)), (Int8)),
  (((action), (Enum<ZquicLogPathAction::JSON>)), (Int8)),
  (((reason), (Enum<ZquicLogPathReason::JSON>)), (Int8)),
  (((bytes)), (UInt64)),
  (((antiAmplification), (JSON::ID<"anti_amplification">)), (UInt64)),
  (((deadlineUS), (JSON::ID<"deadline_us">)), (UInt64)),
  (((mtu)), (UInt32)),
  (((validated)), (Bool)));

struct QLogPathEvent {
  uint64_t	time = 0;
  ZuCSpan	name;
  QLogPathData data;
};

ZtStruct((QLogPathEvent, JSON),
  (((time)), (UInt64)),
  (((name)), (String)),
  (((data)), (UDT)));

struct QLogCIDData {
  ZquicLogCIDKind::T kind = ZquicLogCIDKind::ConnectionID;
  ZquicLogCIDAction::T action = ZquicLogCIDAction::Updated;
  ZquicLogCIDReason::T reason = ZquicLogCIDReason::None;
  uint64_t	sequence = 0;
  uint8_t	length = 0;
  bool		local = false;
  bool		associated = false;
  bool		resetToken = false;
};

ZtStruct((QLogCIDData, JSON),
  (((kind), (Enum<ZquicLogCIDKind::JSON>)), (Int8)),
  (((action), (Enum<ZquicLogCIDAction::JSON>)), (Int8)),
  (((reason), (Enum<ZquicLogCIDReason::JSON>)), (Int8)),
  (((sequence)), (UInt64)),
  (((length)), (UInt8)),
  (((local)), (Bool)),
  (((associated)), (Bool)),
  (((resetToken), (JSON::ID<"reset_token">)), (Bool)));

struct QLogCIDEvent {
  uint64_t	time = 0;
  ZuCSpan	name;
  QLogCIDData data;
};

ZtStruct((QLogCIDEvent, JSON),
  (((time)), (UInt64)),
  (((name)), (String)),
  (((data)), (UDT)));

struct QLogStreamData {
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

ZtStruct((QLogStreamData, JSON),
  (((streamType), (JSON::ID<"stream_type">,
    Enum<ZquicLogStreamType::JSON>)), (Int8)),
  (((oldState), (JSON::ID<"old">,
    Enum<ZquicLogStreamState::JSON>)), (Int8)),
  (((newState), (JSON::ID<"new">,
    Enum<ZquicLogStreamState::JSON>)), (Int8)),
  (((streamSide), (JSON::ID<"stream_side">,
    Enum<ZquicLogStreamSide::JSON>)), (Int8)),
  (((reason), (Enum<ZquicLogStreamReason::JSON>)), (Int8)),
  (((streamID), (JSON::ID<"stream_id">)), (UInt64)),
  (((offset)), (UInt64)),
  (((length)), (UInt64)),
  (((errorCode), (JSON::ID<"error_code">)), (UInt64)),
  (((fin)), (Bool)));

struct QLogStreamEvent {
  uint64_t	time = 0;
  ZuCSpan	name;
  QLogStreamData data;
};

ZtStruct((QLogStreamEvent, JSON),
  (((time)), (UInt64)),
  (((name)), (String)),
  (((data)), (UDT)));

struct QLogRawInfo {
  uint64_t	length = 0;
};

ZtStruct((QLogRawInfo, JSON),
  (((length)), (UInt64)));

struct QLogStreamMovedData {
  uint64_t	streamID = 0;
  uint64_t	offset = 0;
  ZquicLogStreamDataLoc::T from = ZquicLogStreamDataLoc::Transport;
  ZquicLogStreamDataLoc::T to = ZquicLogStreamDataLoc::Application;
  ZquicLogStreamDataInfo::T additionalInfo = ZquicLogStreamDataInfo::None;
  QLogRawInfo	raw;
};

ZtStruct((QLogStreamMovedData, JSON),
  (((streamID), (JSON::ID<"stream_id">)), (UInt64)),
  (((offset)), (UInt64)),
  (((from), (Enum<ZquicLogStreamDataLoc::JSON>)), (Int8)),
  (((to), (Enum<ZquicLogStreamDataLoc::JSON>)), (Int8)),
  (((additionalInfo), (JSON::ID<"additional_info">,
    Enum<ZquicLogStreamDataInfo::JSON>)), (Int8)),
  (((raw)), (UDT)));

struct QLogStreamMovedEvent {
  uint64_t	time = 0;
  ZuCSpan	name;
  QLogStreamMovedData data;
};

ZtStruct((QLogStreamMovedEvent, JSON),
  (((time)), (UInt64)),
  (((name)), (String)),
  (((data)), (UDT)));

struct QLogConnectionBlockedData {
  ZquicLogBlockedState::T oldState = ZquicLogBlockedState::Unblocked;
  ZquicLogBlockedState::T newState = ZquicLogBlockedState::Blocked;
  ZquicLogBlockedReason::T reason =
    ZquicLogBlockedReason::ConnectionFlowControl;
};

ZtStruct((QLogConnectionBlockedData, JSON),
  (((oldState), (JSON::ID<"old">, Enum<ZquicLogBlockedState::JSON>)), (Int8)),
  (((newState), (JSON::ID<"new">, Enum<ZquicLogBlockedState::JSON>)), (Int8)),
  (((reason), (Enum<ZquicLogBlockedReason::JSON>)), (Int8)));

struct QLogConnectionBlockedEvent {
  uint64_t	time = 0;
  ZuCSpan	name;
  QLogConnectionBlockedData data;
};

ZtStruct((QLogConnectionBlockedEvent, JSON),
  (((time)), (UInt64)),
  (((name)), (String)),
  (((data)), (UDT)));

struct QLogStreamBlockedData {
  ZquicLogBlockedState::T oldState = ZquicLogBlockedState::Unblocked;
  ZquicLogBlockedState::T newState = ZquicLogBlockedState::Blocked;
  uint64_t	streamID = 0;
  ZquicLogBlockedReason::T reason =
    ZquicLogBlockedReason::StreamFlowControl;
};

ZtStruct((QLogStreamBlockedData, JSON),
  (((oldState), (JSON::ID<"old">, Enum<ZquicLogBlockedState::JSON>)), (Int8)),
  (((newState), (JSON::ID<"new">, Enum<ZquicLogBlockedState::JSON>)), (Int8)),
  (((streamID), (JSON::ID<"stream_id">)), (UInt64)),
  (((reason), (Enum<ZquicLogBlockedReason::JSON>)), (Int8)));

struct QLogStreamBlockedEvent {
  uint64_t	time = 0;
  ZuCSpan	name;
  QLogStreamBlockedData data;
};

ZtStruct((QLogStreamBlockedEvent, JSON),
  (((time)), (UInt64)),
  (((name)), (String)),
  (((data)), (UDT)));

struct QLogCloseData {
	ZquicLogCloseInitiator::T initiator = ZquicLogCloseInitiator::Local;
	ZquicLogCloseTrigger::T trigger = ZquicLogCloseTrigger::Error;
  ZquicLogCloseReason::T reason = ZquicLogCloseReason::None;
  ZquicLogCloseError::T connectionError = ZquicLogCloseError::None;
  ZquicLogCloseError::T applicationError = ZquicLogCloseError::None;
  uint64_t	errorCode = 0;
  bool		application = false;
  bool		frame = false;
};

ZtStruct((QLogCloseData, JSON),
  (((initiator), (Enum<ZquicLogCloseInitiator::JSON>)), (Int8)),
  (((trigger), (Enum<ZquicLogCloseTrigger::JSON>)), (Int8)),
  (((reason), (Enum<ZquicLogCloseReason::JSON>)), (Int8)),
  (((connectionError), (JSON::ID<"connection_error">,
    Enum<ZquicLogCloseError::JSON>)), (Int8)),
  (((applicationError), (JSON::ID<"application_error">,
    Enum<ZquicLogCloseError::JSON>)), (Int8)),
  (((errorCode), (JSON::ID<"error_code">)), (UInt64)),
  (((application)), (Bool)),
  (((frame)), (Bool)));

struct QLogCloseEvent {
  uint64_t	time = 0;
  ZuCSpan	name;
  QLogCloseData data;
};

ZtStruct((QLogCloseEvent, JSON),
  (((time)), (UInt64)),
  (((name)), (String)),
  (((data)), (UDT)));

static uint64_t qlogTime_(ZuTime time)
{
  return uint64_t(time.sec()) * 1000000 + uint64_t(time.nsec() / 1000);
}

bool ZquicLogSink::init(ZuCSpan path, unsigned age)
{
  final();
  if (path)
    m_path = path;
  else
    m_path = "zquic.sqlog";
  ZiFile::age(m_path, age);
  m_file.open(m_path, ZiFile::Write | ZiFile::GC);
  return !!m_file;
}

void ZquicLogSink::final()
{
  if (m_file) m_file.close();
  m_path.length(0);
}

bool ZquicLogSink::write(ZuCSpan s)
{
  return m_file && m_file.write(s.data(), s.length()) == Zi::OK;
}

ZquicLogger::ZquicLogger()
{
}

ZquicLogger::~ZquicLogger()
{
  stop_();
  final_();
}

ZquicLogger *ZquicLogger::instance()
{
  static constexpr auto ctor = []() { return new ZquicLogger(); };
  return
    ZmSingleton<ZquicLogger,
      ZmSingletonCtor<ctor,
	ZmSingletonCleanup<ZmCleanup::Library>>>::instance();
}

bool ZquicLogger::init_(const ZquicLogParams &params)
{
  Guard guard(m_lock);
  if (m_started) return false;
  m_params = params;
  m_vantagePoint = "unknown";
  m_originalDCID.length(0);
  m_groupID.length(0);
  m_dcid.length(0);
  m_scid.length(0);
  m_headerWritten = false;
  m_waitForMetadata = false;
  m_configured = params.enabled();
  m_enabled.store_(false);
  m_recordsEnqueued.store_(0);
  m_recordsWritten.store_(0);
  m_recordsDropped.store_(0);
  m_ringBackPressure.store_(0);
  m_writerFailures.store_(0);
  m_bytesWritten.store_(0);
  return true;
}

bool ZquicLogger::metadata_(const ZquicLogMetadata &metadata)
{
  Guard guard(m_lock);
  if (m_headerWritten) return false;
  if (metadata.vantagePoint) {
    if (m_vantagePoint && m_vantagePoint != "unknown" &&
	metadata.vantagePoint != m_vantagePoint)
      return false;
    m_vantagePoint = metadata.vantagePoint;
  }
  if (metadata.originalDCID.length()) {
    m_originalDCID.length(0);
    m_originalDCID.append(
      metadata.originalDCID.data(), metadata.originalDCID.length());
  }
  if (metadata.groupID.length()) {
    m_groupID.length(0);
    m_groupID.append(metadata.groupID.data(), metadata.groupID.length());
  }
  if (metadata.dcid.length()) {
    m_dcid.length(0);
    m_dcid.append(metadata.dcid.data(), metadata.dcid.length());
  }
  if (metadata.scid.length()) {
    m_scid.length(0);
    m_scid.append(metadata.scid.data(), metadata.scid.length());
  }
  bool hasConnectionMetadata =
    m_originalDCID.length() ||
    m_groupID.length() ||
    m_dcid.length() ||
    m_scid.length();
  m_waitForMetadata = m_vantagePoint && !hasConnectionMetadata;
  return true;
}

void ZquicLogger::start_()
{
  Guard guard(m_lock);
  if (!m_configured || m_started) return;
  m_ring.init(ZmRingParams{m_params.ringSize()});
  if (m_ring.open(Ring::Read | Ring::Write) != Zu::OK) {
    ++m_writerFailures;
    return;
  }
  m_enabled.store_(true);
  ZmThreadParams threadParams;
  threadParams.name(m_params.thread() ? m_params.thread() : ZuCSpan{"zquic-qlog"});
  threadParams.priority(ZmThreadPriority::Low);
  m_thread = ZmThread{[this]() { work_(); }, threadParams};
  m_started = true;
}

void ZquicLogger::stop_()
{
  ZmThread thread;
  {
    Guard guard(m_lock);
    m_enabled.store_(false);
    if (!m_started) return;
    thread = m_thread;
    m_thread = {};
    m_started = false;
  }
  if (thread) {
    m_ring.eof(true);
    thread.join();
  }
  m_ring.close();
}

void ZquicLogger::final_()
{
  Guard guard(m_lock);
  m_enabled.store_(false);
  m_configured = false;
  m_params = {};
  m_vantagePoint.length(0);
  m_originalDCID.length(0);
  m_groupID.length(0);
  m_dcid.length(0);
  m_scid.length(0);
  m_headerWritten = false;
  m_waitForMetadata = false;
}

ZquicLogDiag ZquicLogger::diag_() const
{
  return {
    m_recordsEnqueued.load_(),
    m_recordsWritten.load_(),
    m_recordsDropped.load_(),
    m_ringBackPressure.load_(),
    m_writerFailures.load_(),
    m_bytesWritten.load_()
  };
}

void ZquicLogger::lifecycle_(ZquicLogLifecycle::T event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event, time](ZquicLogger *this_) mutable {
    this_->writeLifecycle_(event, time);
  };
  post_(fn_);
}

void ZquicLogger::datagramSent_(ZquicLogDatagramEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeDatagramEvent_(
      "transport:datagrams_sent", event_, time);
  };
  post_(fn_);
}

void ZquicLogger::datagramReceived_(ZquicLogDatagramEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeDatagramEvent_(
      "transport:datagrams_received", event_, time);
  };
  post_(fn_);
}

void ZquicLogger::packetSent_(ZquicLogPacketEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writePacketEvent_(
      "transport:packet_sent", event_, time);
  };
  post_(fn_);
}

void ZquicLogger::packetReceived_(ZquicLogPacketEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writePacketEvent_(
      "transport:packet_received", event_, time);
  };
  post_(fn_);
}

void ZquicLogger::packetBuffered_(ZquicLogPacketEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writePacketEvent_(
      "transport:packet_buffered", event_, time);
  };
  post_(fn_);
}

void ZquicLogger::packetDropped_(ZquicLogPacketEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writePacketEvent_(
      "transport:packet_dropped", event_, time);
  };
  post_(fn_);
}

void ZquicLogger::packetsAcked_(ZquicLogAckEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeAckEvent_("transport:packets_acked", event_, time);
  };
  post_(fn_);
}

void ZquicLogger::packetLost_(ZquicLogRecoveryEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeRecoveryEvent_("transport:packet_lost", event_, time);
  };
  post_(fn_);
}

void ZquicLogger::recoveryPacketLost_(ZquicLogRecoveryEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeRecoveryEvent_("recovery:packet_lost", event_, time);
  };
  post_(fn_);
}

void ZquicLogger::markedForRetransmit_(ZquicLogRecoveryEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeRecoveryEvent_(
      "recovery:marked_for_retransmit", event_, time);
  };
  post_(fn_);
}

void ZquicLogger::metricsUpdated_(ZquicLogRecoveryEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeRecoveryEvent_("recovery:metrics_updated", event_, time);
  };
  post_(fn_);
}

void ZquicLogger::lossTimerUpdated_(ZquicLogRecoveryEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeRecoveryEvent_("recovery:loss_timer_updated", event_, time);
  };
  post_(fn_);
}

void ZquicLogger::congestionStateUpdated_(ZquicLogRecoveryEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeRecoveryEvent_(
      "recovery:congestion_state_updated", event_, time);
  };
  post_(fn_);
}

void ZquicLogger::ecnStateUpdated_(ZquicLogECNEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeECNEvent_("recovery:ecn_state_updated", event_, time);
  };
  post_(fn_);
}

void ZquicLogger::keyUpdated_(ZquicLogSecurityEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeSecurityEvent_("security:key_updated", event_, time);
  };
  post_(fn_);
}

void ZquicLogger::keyRetired_(ZquicLogSecurityEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeSecurityEvent_("security:key_retired", event_, time);
  };
  post_(fn_);
}

void ZquicLogger::transportParametersSet_(ZquicLogSecurityEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeSecurityEvent_(
      "security:transport_parameters_set", event_, time);
  };
  post_(fn_);
}

void ZquicLogger::alpnInformation_(ZquicLogSecurityEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeSecurityEvent_("security:alpn_information", event_, time);
  };
  post_(fn_);
}

void ZquicLogger::tlsAlert_(ZquicLogSecurityEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeSecurityEvent_("security:tls_alert", event_, time);
  };
  post_(fn_);
}

void ZquicLogger::securityEvent_(
  ZeString name, ZquicLogSecurityEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [
    name_ = ZuMv(name), event_ = ZuMv(event), time
  ](ZquicLogger *this_) mutable {
    this_->writeSecurityEvent_(name_, event_, time);
  };
  post_(fn_);
}

void ZquicLogger::pathUpdated_(ZquicLogPathEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writePathEvent_("path:path_updated", event_, time);
  };
  post_(fn_);
}

void ZquicLogger::pathValidationUpdated_(ZquicLogPathEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writePathEvent_("path:path_validation_updated", event_, time);
  };
  post_(fn_);
}

void ZquicLogger::pmtudUpdated_(ZquicLogPathEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writePathEvent_("path:pmtud_updated", event_, time);
  };
  post_(fn_);
}

void ZquicLogger::cidUpdated_(ZquicLogCIDEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeCIDEvent_("connectivity:connection_id_updated", event_, time);
  };
  post_(fn_);
}

void ZquicLogger::streamStateUpdated_(ZquicLogStreamEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeStreamEvent_(
      "transport:stream_state_updated", event_, time);
  };
  post_(fn_);
}

void ZquicLogger::streamDataMoved_(ZquicLogStreamDataEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeStreamDataEvent_(
      "transport:stream_data_moved", event_, time);
  };
  post_(fn_);
}

void ZquicLogger::connectionDataBlockedUpdated_(
  ZquicLogBlockedEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeConnectionBlockedEvent_(
      "transport:connection_data_blocked_updated", event_, time);
  };
  post_(fn_);
}

void ZquicLogger::streamDataBlockedUpdated_(
  ZquicLogBlockedEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeStreamBlockedEvent_(
      "transport:stream_data_blocked_updated", event_, time);
  };
  post_(fn_);
}

void ZquicLogger::connectionClosed_(ZquicLogCloseEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeCloseEvent_(
      "connectivity:connection_closed", event_, time);
  };
  post_(fn_);
}

void ZquicLogger::work_()
{
  if (!m_sink.init(m_params.path(), m_params.age())) {
    ++m_writerFailures;
  } else {
    for (;;) {
      if (void *ptr = m_ring.shift()) {
	bool eof = m_ring.readStatus() == Zu::EndOfFile;
	if (!headerReady_(eof)) {
	  Zm::yield();
	  continue;
	}
	if (!writeHeader_()) break;
	m_ring.shift2(Fn::invoke(ptr, this));
      } else {
	if (m_ring.readStatus() == Zu::EndOfFile) break;
      }
    }
  }
  m_sink.final();
}

bool ZquicLogger::headerReady_(bool force) const
{
  Guard guard(m_lock);
  return
    m_headerWritten ||
    force ||
    !m_waitForMetadata ||
    m_originalDCID.length() ||
    m_groupID.length() ||
    m_dcid.length() ||
    m_scid.length();
}

bool ZquicLogger::writeHeader_()
{
  ZeString vantagePoint;
  ZtBArray<> originalDCID;
  ZtBArray<> groupID;
  ZtBArray<> dcid;
  ZtBArray<> scid;
  {
    Guard guard(m_lock);
    if (m_headerWritten) return true;
    vantagePoint = m_vantagePoint;
    originalDCID = m_originalDCID;
    groupID = m_groupID;
    dcid = m_dcid;
    scid = m_scid;
    m_headerWritten = true;
  }
  QLogHeader header{
    "0.3",
    "JSON-SEQ",
    "zquic",
    QLogTrace{
      QLogVantagePoint{vantagePoint},
      QLogImplementation{"zquic", Z_VERNAME},
      QLogCommonFields{
	.originalDCID = originalDCID,
	.groupID = groupID,
	.dcid = dcid,
	.scid = scid
      }
    }
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, header);
  m_buf << '\n';
  if (!m_sink.write({m_buf.data(), m_buf.length()})) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeLifecycle_(ZquicLogLifecycle::T event, ZuTime time)
{
  QLogLifecycleEvent qevent{
    qlogTime_(time),
    event
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write({m_buf.data(), m_buf.length()})) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeDatagramEvent_(
  ZuCSpan name, const ZquicLogDatagramEvent &event, ZuTime time)
{
  QLogDatagramEvent qevent{
    qlogTime_(time),
    name,
    QLogDatagramData{event.size, event.ecn}
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write({m_buf.data(), m_buf.length()})) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writePacketEvent_(
  ZuCSpan name, const ZquicLogPacketEvent &event, ZuTime time)
{
  QLogFrameArray frames;
  for (unsigned i = 0, n = event.frames.length(); i < n; ++i) {
    const ZquicLogFrameEvent &frame = event.frames[i];
    new (frames.push()) QLogFrameData{
      frame.type,
      frame.streamID,
      frame.offset,
      frame.length,
      frame.value,
      frame.errorCode,
      frame.largestAcked,
      frame.ackDelayUS,
      frame.ect0,
      frame.ect1,
      frame.ce,
      frame.rangeCount,
      frame.fin
    };
  }
  QLogPacketEvent qevent{
    qlogTime_(time),
    name,
    QLogPacketData{
      event.packetType,
      event.packetSpace,
      event.packetNumber,
      event.packetSize,
      event.payloadSize,
      event.ecn,
      event.reason,
      event.bytesInFlight,
      event.frameCount,
      event.framesTruncated,
      ZuMv(frames),
      event.ackEliciting
    }
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write({m_buf.data(), m_buf.length()})) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeAckEvent_(
  ZuCSpan name, const ZquicLogAckEvent &event, ZuTime time)
{
  QLogAckEvent qevent{
    qlogTime_(time),
    name,
    QLogAckData{
      event.packetSpace,
      event.largestAcked,
      event.ackDelayUS,
      event.ackedBytes,
      event.lostBytes,
      event.rangeCount,
      event.ackedFrames,
      event.lostFrames
    }
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write({m_buf.data(), m_buf.length()})) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeRecoveryEvent_(
  ZuCSpan name, const ZquicLogRecoveryEvent &event, ZuTime time)
{
  QLogRecoveryEvent qevent{
    qlogTime_(time),
    name,
    QLogRecoveryData{
      event.kind,
      event.packetSpace,
      event.reason,
      event.value,
      event.packetNumber,
      event.bytes,
      event.deadlineUS,
      event.latestRTTUS,
      event.smoothedRTTUS,
      event.rttVarianceUS,
      event.minRTTUS,
      event.cwnd,
      event.ssthresh,
      event.bytesInFlight,
      event.frameCount
    }
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write({m_buf.data(), m_buf.length()})) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeECNEvent_(
  ZuCSpan name, const ZquicLogECNEvent &event, ZuTime time)
{
  QLogECNEvent qevent{
    qlogTime_(time),
    name,
    QLogECNData{
      event.packetSpace,
      event.state,
      event.reason,
      event.ect0,
      event.ect1,
      event.ce,
      event.previousECT0,
      event.previousECT1,
      event.previousCE,
      event.largestAcked,
      event.disabled
    }
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write({m_buf.data(), m_buf.length()})) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeSecurityEvent_(
  ZuCSpan name, const ZquicLogSecurityEvent &event, ZuTime time)
{
  QLogSecurityEvent qevent{
    qlogTime_(time),
    name,
    QLogSecurityData{
      event.kind,
      event.packetSpace,
      event.keyType,
      event.trigger,
      event.alpn,
      event.reason,
      event.value,
      event.success
    }
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write({m_buf.data(), m_buf.length()})) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writePathEvent_(
  ZuCSpan name, const ZquicLogPathEvent &event, ZuTime time)
{
  QLogPathEvent qevent{
    qlogTime_(time),
    name,
    QLogPathData{
      event.kind,
      event.action,
      event.reason,
      event.bytes,
      event.antiAmplification,
      event.deadlineUS,
      event.mtu,
      event.validated
    }
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write({m_buf.data(), m_buf.length()})) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeCIDEvent_(
  ZuCSpan name, const ZquicLogCIDEvent &event, ZuTime time)
{
  QLogCIDEvent qevent{
    qlogTime_(time),
    name,
    QLogCIDData{
      event.kind,
      event.action,
      event.reason,
      event.sequence,
      event.length,
      event.local,
      event.associated,
      event.resetToken
    }
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write({m_buf.data(), m_buf.length()})) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeStreamEvent_(
  ZuCSpan name, const ZquicLogStreamEvent &event, ZuTime time)
{
  QLogStreamEvent qevent{
    qlogTime_(time),
    name,
    QLogStreamData{
      event.streamType,
      event.oldState,
      event.newState,
      event.streamSide,
      event.reason,
      event.streamID,
      event.offset,
      event.length,
      event.errorCode,
      event.fin
    }
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write({m_buf.data(), m_buf.length()})) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeStreamDataEvent_(
  ZuCSpan name, const ZquicLogStreamDataEvent &event, ZuTime time)
{
  QLogStreamMovedEvent qevent{
    qlogTime_(time),
    name,
    QLogStreamMovedData{
      event.streamID,
      event.offset,
      event.from,
      event.to,
      event.additionalInfo,
      QLogRawInfo{event.length}
    }
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write({m_buf.data(), m_buf.length()})) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeConnectionBlockedEvent_(
  ZuCSpan name, const ZquicLogBlockedEvent &event, ZuTime time)
{
  QLogConnectionBlockedEvent qevent{
    qlogTime_(time),
    name,
    QLogConnectionBlockedData{
      event.oldState,
      event.newState,
      event.reason
    }
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write({m_buf.data(), m_buf.length()})) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeStreamBlockedEvent_(
  ZuCSpan name, const ZquicLogBlockedEvent &event, ZuTime time)
{
  QLogStreamBlockedEvent qevent{
    qlogTime_(time),
    name,
    QLogStreamBlockedData{
      event.oldState,
      event.newState,
      event.streamID,
      event.reason
    }
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write({m_buf.data(), m_buf.length()})) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeCloseEvent_(
  ZuCSpan name, const ZquicLogCloseEvent &event, ZuTime time)
{
  QLogCloseEvent qevent{
    qlogTime_(time),
    name,
    QLogCloseData{
      event.initiator,
      event.trigger,
      event.reason,
      event.connectionError,
      event.applicationError,
      event.errorCode,
      event.application,
      event.frame
    }
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write({m_buf.data(), m_buf.length()})) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

#endif /* Zquic_DEBUG */
