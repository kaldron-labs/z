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

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props = ZuTypeList<>,
  typename S, typename T>
static void qlogJSONField_(S &, bool &, ZuCSpan, const T &);

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

struct QLogReferenceTime {
  ZuCSpan	clockType;
  ZuCSpan	epoch;
};

ZtStruct((QLogReferenceTime, JSON),
  (((clockType), (JSON::ID<"clock_type">)), (String)),
  (((epoch)), (String)));

struct QLogCommonFields {
  ZuBSpan	originalDCID;
  ZuBSpan	groupID;
  ZuBSpan	dcid;
  ZuBSpan	scid;
  ZuCSpan	timeFormat;
  QLogReferenceTime	referenceTime;
};

struct QLogCommonFieldsJSON {
  template <typename O, typename Facet>
  struct Handler {
    template <template <typename> class Filter, typename S>
    static void save(S &s, const QLogCommonFields &fields)
    {
      using BytesHexProps = ZuTypeList<ZuFieldProp::JSON::Hex>;

      bool comma = false;
      s << '{';
      if (fields.originalDCID)
	qlogJSONField_<Facet, Filter, ZtFieldTC::Bytes, BytesHexProps>(
	  s, comma, "ODCID", fields.originalDCID);
      if (fields.groupID)
	qlogJSONField_<Facet, Filter, ZtFieldTC::Bytes, BytesHexProps>(
	  s, comma, "group_id", fields.groupID);
      if (fields.dcid)
	qlogJSONField_<Facet, Filter, ZtFieldTC::Bytes, BytesHexProps>(
	  s, comma, "DCID", fields.dcid);
      if (fields.scid)
	qlogJSONField_<Facet, Filter, ZtFieldTC::Bytes, BytesHexProps>(
	  s, comma, "SCID", fields.scid);
      qlogJSONField_<Facet, Filter, ZtFieldTC::String>(
	s, comma, "time_format", fields.timeFormat);
      qlogJSONField_<Facet, Filter, ZtFieldTC::UDT>(
	s, comma, "reference_time", fields.referenceTime);
      s << '}';
    }
  };
};

inline QLogCommonFieldsJSON ZtJSON_Fmt(QLogCommonFields *);

ZtStruct((ZquicLogCIDMeta, JSON),
  (((origDCID), (JSON::ID<"ODCID">, JSON::Hex)), (Bytes)),
  (((groupID), (JSON::ID<"group_id">, JSON::Hex)), (Bytes)),
  (((dcid), (JSON::ID<"DCID">, JSON::Hex)), (Bytes)),
  (((scid), (JSON::ID<"SCID">, JSON::Hex)), (Bytes)));

template <typename Event>
struct QLogEventJSON {
  template <typename O, typename Facet>
  struct Handler {
    template <template <typename> class Filter, typename S>
    static void save(S &s, const Event &event)
    {
      using NameProps =
	ZuTypeList<ZuFieldProp::Enum<ZquicLogEventName::JSON>>;

      bool comma = false;
      s << '{';
      qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	s, comma, "time", event.time);
      qlogJSONField_<Facet, Filter, ZtFieldTC::Int8, NameProps>(
	s, comma, "name", event.name);
      qlogJSONField_<Facet, Filter, ZtFieldTC::UDT>(
	s, comma, "data", event.data);
      if (event.metadata)
	qlogJSONField_<Facet, Filter, ZtFieldTC::UDT>(
	  s, comma, "common_fields", event.metadata);
      s << '}';
    }
  };
};

#define QLogEventFmt(Type) inline QLogEventJSON<Type> ZtJSON_Fmt(Type *)

ZuDerive(QLogEventSchemas,
  (ZuArray<ZuCSpan, 2>));
inline ZtJSON::AsArray<ZtFieldTC::String> ZtJSON_Fmt(QLogEventSchemas *);

struct QLogTrace {
  QLogVantagePoint	vantagePoint;
  QLogCommonFields	commonFields;
  QLogEventSchemas	eventSchemas;
};

ZtStruct((QLogTrace, JSON),
  (((vantagePoint), (JSON::ID<"vantage_point">)), (UDT)),
  (((commonFields), (JSON::ID<"common_fields">)), (UDT)),
  (((eventSchemas), (JSON::ID<"event_schemas">)), (UDT)));

struct QLogHeader {
  ZuCSpan	fileSchema;
  ZuCSpan	serializationFormat;
  ZuCSpan	title;
  QLogImplementation	implementation;
  QLogTrace	trace;
};

ZtStruct((QLogHeader, JSON),
  (((fileSchema), (JSON::ID<"file_schema">)), (String)),
  (((serializationFormat), (JSON::ID<"serialization_format">)), (String)),
  (((title)), (String)),
  (((implementation)), (UDT)),
  (((trace)), (UDT)));

struct QLogEndpointInfo {
  bool		present = false;
  ZiIP		ip;
  uint16_t	port = 0;

  bool operator !() const { return !present; }
  ZuOpBool
};

ZtStruct((QLogEndpointInfo, JSON),
  (((ip), (JSON::ID<"ip_v4">)), (UDT)),
  (((port), (JSON::ID<"port_v4">)), (UInt16)));

struct QLogConnectionStartedData {
  QLogEndpointInfo local;
  QLogEndpointInfo remote;
};

struct QLogConnectionStartedDataJSON;
inline QLogConnectionStartedDataJSON ZtJSON_Fmt(QLogConnectionStartedData *);

struct QLogConnectionStartedEvent {
  uint64_t	time = 0;
  ZquicLogEventName::T name = ZquicLogEventName::ConnStarted;
  QLogConnectionStartedData data;
  ZquicLogCIDMeta metadata;
};

QLogEventFmt(QLogConnectionStartedEvent);

struct QLogConnectionStateData {
  ZquicLogConnectionState::T oldState = ZquicLogConnectionState::Attempted;
  ZquicLogConnectionState::T newState =
    ZquicLogConnectionState::HandshakeStarted;
};

ZtStruct((QLogConnectionStateData, JSON),
  (((oldState), (JSON::ID<"old">,
    Enum<ZquicLogConnectionState::JSON>)), (Int8)),
  (((newState), (JSON::ID<"new">,
    Enum<ZquicLogConnectionState::JSON>)), (Int8)));

struct QLogConnectionStateEvent {
  uint64_t	time = 0;
  ZquicLogEventName::T name = ZquicLogEventName::ConnStateUpd;
  QLogConnectionStateData data;
  ZquicLogCIDMeta metadata;
};

QLogEventFmt(QLogConnectionStateEvent);

struct QLogRawInfo {
  uint64_t	length = 0;
  uint64_t	payloadLength = 0;
};

ZtStruct((QLogRawInfo, JSON),
  (((length)), (UInt64)),
  (((payloadLength), (JSON::ID<"payload_length">)), (UInt64)));

ZuDerive(QLogRawInfoArray,
  (ZuArray<QLogRawInfo, 1>));
inline ZtJSON::AsArray<ZtFieldTC::UDT> ZtJSON_Fmt(QLogRawInfoArray *);

ZuDerive(QLogECNArray,
  (ZuArray<Zquic::EcnMark::T, 1>));
inline ZtJSON::AsArray<
  ZtFieldTC::Int8,
  ZuTypeList<ZuFieldProp::Enum<Zquic::EcnMark::JSON>>> ZtJSON_Fmt(
    QLogECNArray *);

ZuDerive(QLogAckRange,
  (ZuArray<uint64_t, 2>));
inline ZtJSON::AsArray<ZtFieldTC::UInt64> ZtJSON_Fmt(QLogAckRange *);

ZuDerive(QLogAckRangeArray,
  (ZuArray<QLogAckRange, ZquicLogAckRangeMax>));
inline ZtJSON::AsArray<ZtFieldTC::UDT> ZtJSON_Fmt(QLogAckRangeArray *);

struct QLogDatagramData {
	uint16_t	count = 0;
	QLogRawInfoArray raw;
	QLogECNArray	ecn;
};

ZtStruct((QLogDatagramData, JSON),
  (((count)), (UInt16)),
  (((raw)), (UDT)),
  (((ecn)), (UDT)));

struct QLogDatagramEvent {
  uint64_t	time = 0;
  ZquicLogEventName::T name = ZquicLogEventName::UDPTx;
  QLogDatagramData data;
  ZquicLogCIDMeta metadata;
};

QLogEventFmt(QLogDatagramEvent);

struct QLogFrameData {
	Zquic::FrameType::T frameType = Zquic::FrameType::Unknown;
  uint64_t	streamID = 0;
  uint64_t	offset = 0;
  QLogRawInfo	raw;
  double	ackDelay = 0;
  QLogAckRangeArray ackedRanges;
  ZquicLogStreamType::T streamType = ZquicLogStreamType::Bidirectional;
  uint64_t	maximum = 0;
  uint64_t	limit = 0;
  uint64_t	errorCode = 0;
  uint64_t	finalSize = 0;
  uint64_t	sequenceNumber = 0;
  uint64_t	retirePriorTo = 0;
  uint64_t	connectionIDLength = 0;
  ZuBSpan	connectionID;
  ZuBSpan	statelessResetToken;
  uint64_t	ect0 = 0;
  uint64_t	ect1 = 0;
  uint64_t	ce = 0;
  bool		fin = false;

  struct Traits : public ZuBaseTraits<QLogFrameData> {
    enum { IsPOD = 1 };
  };
  friend Traits ZuTraitsType(QLogFrameData *);
};

struct QLogFrameDataJSON;
inline QLogFrameDataJSON ZtJSON_Fmt(QLogFrameData *);

ZuDerive(QLogFrameArray,
  (ZuArray<QLogFrameData, ZquicLogFrameMax>));
inline ZtJSON::AsArray<ZtFieldTC::UDT> ZtJSON_Fmt(QLogFrameArray *);

struct QLogPacketHeader {
	Zquic::PktType::T packetType = Zquic::PktType::Initial;
	uint64_t	packetNumber = 0;
};

ZtStruct((QLogPacketHeader, JSON),
  (((packetType), (JSON::ID<"packet_type">, Enum<Zquic::PktType::JSON>)),
      (Int8)),
  (((packetNumber), (JSON::ID<"packet_number">)), (UInt64)));

struct ZquicLogPacketTrigger {
  ZtEnum(ZquicLogPacketTrigger, int8_t,
    None, Backpressure, KeysUnavailable, InternalError, Rejected, Unsupported,
    Invalid, Duplicate, ConnectionUnknown, DecryptionFailure, KeyUnavailable,
    General);
  ZtEnumMap(ZquicLogPacketTrigger, JSON,
    "", "backpressure", "keys_unavailable", "internal_error", "rejected",
    "unsupported", "invalid", "duplicate", "connection_unknown",
    "decryption_failure", "key_unavailable", "general");
};

struct QLogPacketData {
	QLogPacketHeader header;
	Zquic::PktNumSpace::T packetSpace = Zquic::PktNumSpace::Initial;
	QLogRawInfo	raw;
	Zquic::EcnMark::T ecn = Zquic::EcnMark::N;
  ZquicLogPacketTrigger::T trigger = ZquicLogPacketTrigger::None;
  uint64_t	bytesInFlight = 0;
  uint8_t	frameCount = 0;
  bool		framesTruncated = false;
  QLogFrameArray frames;
  bool		ackEliciting = false;
};

struct QLogPacketDataJSON;
inline QLogPacketDataJSON ZtJSON_Fmt(QLogPacketData *);

struct QLogPacketEvent {
  uint64_t	time = 0;
  ZquicLogEventName::T name = ZquicLogEventName::PktSent;
  QLogPacketData data;
  ZquicLogCIDMeta metadata;
};

QLogEventFmt(QLogPacketEvent);

ZuDerive(QLogPacketNumberArray,
  (ZuArray<uint64_t, ZquicLogAckPacketMax>));
inline ZtJSON::AsArray<ZtFieldTC::UInt64> ZtJSON_Fmt(
  QLogPacketNumberArray *);

struct QLogAckData {
	Zquic::PktNumSpace::T packetSpace = Zquic::PktNumSpace::Initial;
	QLogPacketNumberArray packetNumbers;
};

ZtStruct((QLogAckData, JSON),
  (((packetSpace), (JSON::ID<"packet_number_space">,
    Enum<Zquic::PktNumSpace::JSON>)),
      (Int8)),
  (((packetNumbers), (JSON::ID<"packet_numbers">)), (UDT)));

struct QLogAckEvent {
  uint64_t	time = 0;
  ZquicLogEventName::T name = ZquicLogEventName::PktsAcked;
  QLogAckData data;
  ZquicLogCIDMeta metadata;
};

QLogEventFmt(QLogAckEvent);

struct ZquicLogPacketLostTrigger {
  ZtEnum(ZquicLogPacketLostTrigger, int8_t,
    ReorderingThreshold, TimeThreshold, PTOExpired);
  ZtEnumMap(ZquicLogPacketLostTrigger, JSON,
    "reordering_threshold", "time_threshold", "pto_expired");
};

struct QLogPacketLostData {
  QLogPacketHeader header;
  ZquicLogPacketLostTrigger::T trigger =
    ZquicLogPacketLostTrigger::ReorderingThreshold;
};

ZtStruct((QLogPacketLostData, JSON),
  (((header)), (UDT)),
  (((trigger), (Enum<ZquicLogPacketLostTrigger::JSON>)), (Int8)));

struct QLogPacketLostEvent {
  uint64_t	time = 0;
  ZquicLogEventName::T name = ZquicLogEventName::PktLost;
  QLogPacketLostData data;
  ZquicLogCIDMeta metadata;
};

QLogEventFmt(QLogPacketLostEvent);

struct QLogMarkedForRetransmitData {
  QLogFrameArray frames;
};

ZtStruct((QLogMarkedForRetransmitData, JSON),
  (((frames)), (UDT)));

struct QLogMarkedForRetransmitEvent {
  uint64_t	time = 0;
  ZquicLogEventName::T name = ZquicLogEventName::MarkRetrans;
  QLogMarkedForRetransmitData data;
  ZquicLogCIDMeta metadata;
};

QLogEventFmt(QLogMarkedForRetransmitEvent);

struct QLogRecoveryMetricsData {
  double	latestRTT = 0;
  double	smoothedRTT = 0;
  double	rttVariance = 0;
  double	minRTT = 0;
  uint64_t	congestionWindow = 0;
  uint64_t	ssthresh = 0;
  uint64_t	bytesInFlight = 0;
};

ZtStruct((QLogRecoveryMetricsData, JSON),
  (((latestRTT), (JSON::ID<"latest_rtt">)), (Float)),
  (((smoothedRTT), (JSON::ID<"smoothed_rtt">)), (Float)),
  (((rttVariance), (JSON::ID<"rtt_variance">)), (Float)),
  (((minRTT), (JSON::ID<"min_rtt">)), (Float)),
  (((congestionWindow), (JSON::ID<"congestion_window">)), (UInt64)),
  (((ssthresh)), (UInt64)),
  (((bytesInFlight), (JSON::ID<"bytes_in_flight">)), (UInt64)));

struct QLogRecoveryMetricsEvent {
  uint64_t	time = 0;
  ZquicLogEventName::T name = ZquicLogEventName::MetricsUpd;
  QLogRecoveryMetricsData data;
  ZquicLogCIDMeta metadata;
};

QLogEventFmt(QLogRecoveryMetricsEvent);

struct ZquicLogCongestionState {
  ZtEnum(ZquicLogCongestionState, int8_t,
    SlowStart, CongestionAvoidance, ApplicationLimited, Recovery);
  ZtEnumMap(ZquicLogCongestionState, JSON,
    "slow_start", "congestion_avoidance", "application_limited",
    "recovery");
};

struct ZquicLogCongestionTrigger {
  ZtEnum(ZquicLogCongestionTrigger, int8_t,
    Ack, PMTUDAck, Loss, PMTUDLoss);
  ZtEnumMap(ZquicLogCongestionTrigger, JSON,
    "ack", "pmtud_ack", "loss", "pmtud_loss");
};

struct QLogCongestionStateData {
  ZquicLogCongestionState::T newState =
    ZquicLogCongestionState::CongestionAvoidance;
  ZquicLogCongestionTrigger::T trigger = ZquicLogCongestionTrigger::Ack;
};

ZtStruct((QLogCongestionStateData, JSON),
  (((newState), (JSON::ID<"new">,
    Enum<ZquicLogCongestionState::JSON>)), (Int8)),
  (((trigger), (Enum<ZquicLogCongestionTrigger::JSON>)), (Int8)));

struct QLogCongestionStateEvent {
  uint64_t	time = 0;
  ZquicLogEventName::T name = ZquicLogEventName::CongestionUpd;
  QLogCongestionStateData data;
  ZquicLogCIDMeta metadata;
};

QLogEventFmt(QLogCongestionStateEvent);

struct ZquicLogTimerType {
  ZtEnum(ZquicLogTimerType, int8_t,
    Loss, PTO);
  ZtEnumMap(ZquicLogTimerType, JSON,
    "loss_timeout", "pto");
};

struct ZquicLogTimerEventType {
  ZtEnum(ZquicLogTimerEventType, int8_t,
    Set, Expired, Cancelled);
  ZtEnumMap(ZquicLogTimerEventType, JSON,
    "set", "expired", "cancelled");
};

struct QLogTimerData {
  ZquicLogTimerType::T timerType = ZquicLogTimerType::Loss;
  Zquic::PktNumSpace::T packetSpace = Zquic::PktNumSpace::Initial;
  ZquicLogTimerEventType::T eventType = ZquicLogTimerEventType::Set;
  double	delta = 0;
};

ZtStruct((QLogTimerData, JSON),
  (((timerType), (JSON::ID<"timer_type">,
    Enum<ZquicLogTimerType::JSON>)), (Int8)),
  (((packetSpace), (JSON::ID<"packet_number_space">,
    Enum<Zquic::PktNumSpace::JSON>)), (Int8)),
  (((eventType), (JSON::ID<"event_type">,
    Enum<ZquicLogTimerEventType::JSON>)), (Int8)),
  (((delta)), (Float)));

struct QLogTimerEvent {
  uint64_t	time = 0;
  ZquicLogEventName::T name = ZquicLogEventName::TimerUpd;
  QLogTimerData data;
  ZquicLogCIDMeta metadata;
};

QLogEventFmt(QLogTimerEvent);

struct QLogECNData {
  ZquicLogECNState::T oldState = ZquicLogECNState::Unknown;
  ZquicLogECNState::T newState = ZquicLogECNState::Unknown;
};

ZtStruct((QLogECNData, JSON),
  (((oldState), (JSON::ID<"old">, Enum<ZquicLogECNState::JSON>)), (Int8)),
  (((newState), (JSON::ID<"new">, Enum<ZquicLogECNState::JSON>)), (Int8)));

struct QLogECNEvent {
  uint64_t	time = 0;
  ZquicLogEventName::T name = ZquicLogEventName::ECNUpd;
  QLogECNData data;
  ZquicLogCIDMeta metadata;
};

QLogEventFmt(QLogECNEvent);

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
  (((packetSpace), (JSON::ID<"packet_number_space">,
    Enum<Zquic::PktNumSpace::JSON>)), (Int8)),
  (((keyType), (JSON::ID<"key_type">, Enum<ZquicLogSecurityKeyType::JSON>)),
      (Int8)),
  (((trigger), (Enum<ZquicLogSecurityTrigger::JSON>)), (Int8)),
  (((alpn)), (String)),
  (((reason), (Enum<ZquicLogSecurityReason::JSON>)), (Int8)),
  (((value)), (UInt64)),
  (((success)), (Bool)));

struct QLogSecurityEvent {
  uint64_t	time = 0;
  ZquicLogEventName::T name = ZquicLogEventName::TLSAlert;
  QLogSecurityData data;
  ZquicLogCIDMeta metadata;
};

QLogEventFmt(QLogSecurityEvent);

static constexpr uint64_t QLogKeyPhaseNull = ~uint64_t{0};

struct QLogKeyData {
  ZquicLogQKeyType::T keyType = ZquicLogQKeyType::Client1RTT;
  uint64_t	keyPhase = QLogKeyPhaseNull;
  ZquicLogQKeyTrigger::T trigger = ZquicLogQKeyTrigger::TLS;
};

struct QLogKeyDataJSON;
inline QLogKeyDataJSON ZtJSON_Fmt(QLogKeyData *);

struct QLogKeyEvent {
  uint64_t	time = 0;
  ZquicLogEventName::T name = ZquicLogEventName::KeyUpd;
  QLogKeyData data;
  ZquicLogCIDMeta metadata;
};

QLogEventFmt(QLogKeyEvent);

struct QLogTransportParamsData {
  ZquicLogQInitiator::T initiator = ZquicLogQInitiator::Local;
  ZuBSpan	originalDCID;
  ZuBSpan	initialSCID;
  ZuBSpan	retrySCID;
  ZuBSpan	statelessResetToken;
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
  bool		disableActiveMigration = false;
};

struct QLogTransportParamsDataJSON;
inline QLogTransportParamsDataJSON ZtJSON_Fmt(QLogTransportParamsData *);

struct QLogTransportParamsEvent {
  uint64_t	time = 0;
  ZquicLogEventName::T name = ZquicLogEventName::ParamsSet;
  QLogTransportParamsData data;
  ZquicLogCIDMeta metadata;
};

QLogEventFmt(QLogTransportParamsEvent);

struct QLogALPNIdentifier {
  ZeString	stringValue;
};

ZtStruct((QLogALPNIdentifier, JSON),
  (((stringValue), (JSON::ID<"string_value">)), (String)));

struct QLogALPNData {
  QLogALPNIdentifier chosenALPN;
};

ZtStruct((QLogALPNData, JSON),
  (((chosenALPN), (JSON::ID<"chosen_alpn">)), (UDT)));

struct QLogALPNEvent {
  uint64_t	time = 0;
  ZquicLogEventName::T name = ZquicLogEventName::ALPNInfo;
  QLogALPNData data;
  ZquicLogCIDMeta metadata;
};

QLogEventFmt(QLogALPNEvent);

struct QLogVersionData {
  ZquicLogVersionArray serverVersions;
  ZquicLogVersionArray clientVersions;
  uint32_t	chosenVersion = 0;
  bool		chosenVersionPresent = false;
};

struct QLogVersionDataJSON;
inline QLogVersionDataJSON ZtJSON_Fmt(QLogVersionData *);

struct QLogVersionEvent {
  uint64_t	time = 0;
  ZquicLogEventName::T name = ZquicLogEventName::VersionInfo;
  QLogVersionData data;
  ZquicLogCIDMeta metadata;
};

QLogEventFmt(QLogVersionEvent);

struct QLogTupleAssignedData {
  uint64_t	tupleID = 0;
};

struct QLogTupleAssignedDataJSON;
inline QLogTupleAssignedDataJSON ZtJSON_Fmt(QLogTupleAssignedData *);

struct QLogPathEvent {
  uint64_t	time = 0;
  ZquicLogEventName::T name = ZquicLogEventName::TupleAssigned;
  QLogTupleAssignedData data;
  ZquicLogCIDMeta metadata;
};

QLogEventFmt(QLogPathEvent);

struct QLogMTUData {
  uint32_t	newMTU = 0;
  bool		done = false;
};

ZtStruct((QLogMTUData, JSON),
  (((newMTU), (JSON::ID<"new">)), (UInt32)),
  (((done)), (Bool)));

struct QLogMTUEvent {
  uint64_t	time = 0;
  ZquicLogEventName::T name = ZquicLogEventName::MTUUpd;
  QLogMTUData data;
  ZquicLogCIDMeta metadata;
};

QLogEventFmt(QLogMTUEvent);

struct ZquicLogQVantagePoint {
  ZtEnum(ZquicLogQVantagePoint, int8_t, Unknown, Client, Server);
  ZtEnumMap(ZquicLogQVantagePoint, JSON, "unknown", "client", "server");
};

struct QLogPathValidationData {
  bool		success = false;
  ZquicLogQVantagePoint::T vantagePoint = ZquicLogQVantagePoint::Unknown;
};

ZtStruct((QLogPathValidationData, JSON),
  (((success)), (Bool)),
  (((vantagePoint), (JSON::ID<"vantagePoint">,
    Enum<ZquicLogQVantagePoint::JSON>)), (Int8)));

struct QLogPathValidationEvent {
  uint64_t	time = 0;
  ZquicLogEventName::T name = ZquicLogEventName::PathValidated;
  QLogPathValidationData data;
  ZquicLogCIDMeta metadata;
};

QLogEventFmt(QLogPathValidationEvent);

struct QLogCIDData {
  ZquicLogQInitiator::T initiator = ZquicLogQInitiator::Remote;
  ZuBSpan	oldCID;
  ZuBSpan	newCID;
};

struct QLogCIDDataJSON;
inline QLogCIDDataJSON ZtJSON_Fmt(QLogCIDData *);

struct QLogCIDEvent {
  uint64_t	time = 0;
  ZquicLogEventName::T name = ZquicLogEventName::CIDUpd;
  QLogCIDData data;
  ZquicLogCIDMeta metadata;
};

QLogEventFmt(QLogCIDEvent);

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
  ZquicLogEventName::T name = ZquicLogEventName::StreamStateUpd;
  QLogStreamData data;
  ZquicLogCIDMeta metadata;
};

QLogEventFmt(QLogStreamEvent);

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
  ZquicLogEventName::T name = ZquicLogEventName::StreamDataMoved;
  QLogStreamMovedData data;
  ZquicLogCIDMeta metadata;
};

QLogEventFmt(QLogStreamMovedEvent);

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
  ZquicLogEventName::T name = ZquicLogEventName::ConnDataBlockedUpd;
  QLogConnectionBlockedData data;
  ZquicLogCIDMeta metadata;
};

QLogEventFmt(QLogConnectionBlockedEvent);

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
  ZquicLogEventName::T name = ZquicLogEventName::StreamDataBlockedUpd;
  QLogStreamBlockedData data;
  ZquicLogCIDMeta metadata;
};

QLogEventFmt(QLogStreamBlockedEvent);

struct QLogCloseData {
	ZquicLogCloseInitiator::T initiator = ZquicLogCloseInitiator::Local;
	ZquicLogCloseTrigger::T trigger = ZquicLogCloseTrigger::Error;
  ZquicLogCloseReason::T reason = ZquicLogCloseReason::None;
  ZquicLogCloseError::T connectionError = ZquicLogCloseError::None;
  ZquicLogCloseError::T applicationError = ZquicLogCloseError::None;
  uint64_t	errorCode = 0;
};

struct QLogCloseDataJSON;
inline QLogCloseDataJSON ZtJSON_Fmt(QLogCloseData *);

struct QLogCloseEvent {
  uint64_t	time = 0;
  ZquicLogEventName::T name = ZquicLogEventName::ConnClosed;
  QLogCloseData data;
  ZquicLogCIDMeta metadata;
};

QLogEventFmt(QLogCloseEvent);

static uint64_t qlogTime_(ZuTime time)
{
  return uint64_t(time.sec()) * 1000000 + uint64_t(time.nsec() / 1000);
}

static double qlogMS_(uint64_t us)
{
  return double(us) / 1000.0;
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
    m_originalDCID = metadata.originalDCID;
  }
  if (metadata.groupID.length()) {
    m_groupID.length(0);
    m_groupID = metadata.groupID;
  }
  if (metadata.dcid.length()) {
    m_dcid.length(0);
    m_dcid = metadata.dcid;
  }
  if (metadata.scid.length()) {
    m_scid.length(0);
    m_scid = metadata.scid;
  }
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

void ZquicLogger::connectionStarted_(ZquicLogConnectionStartedEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeConnectionStartedEvent_(event_, time);
  };
  post_(fn_);
}

void ZquicLogger::datagramSent_(ZquicLogDatagramEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeDatagramEvent_(ZquicLogEventName::UDPTx, event_, time);
  };
  post_(fn_);
}

void ZquicLogger::datagramReceived_(ZquicLogDatagramEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeDatagramEvent_(ZquicLogEventName::UDPRx, event_, time);
  };
  post_(fn_);
}

void ZquicLogger::packetSent_(ZquicLogPacketEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writePacketEvent_(ZquicLogEventName::PktSent, event_, time);
  };
  post_(fn_);
}

void ZquicLogger::packetReceived_(ZquicLogPacketEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writePacketEvent_(ZquicLogEventName::PktRecv, event_, time);
  };
  post_(fn_);
}

void ZquicLogger::packetBuffered_(ZquicLogPacketEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writePacketEvent_(ZquicLogEventName::PktBuf, event_, time);
  };
  post_(fn_);
}

void ZquicLogger::packetDropped_(ZquicLogPacketEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writePacketEvent_(ZquicLogEventName::PktDrop, event_, time);
  };
  post_(fn_);
}

void ZquicLogger::packetsAcked_(ZquicLogAckEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeAckEvent_(ZquicLogEventName::PktsAcked, event_, time);
  };
  post_(fn_);
}

void ZquicLogger::packetLost_(ZquicLogRecoveryEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writePacketLostEvent_(event_, time);
  };
  post_(fn_);
}

void ZquicLogger::recoveryPacketLost_(ZquicLogRecoveryEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writePacketLostEvent_(event_, time);
  };
  post_(fn_);
}

void ZquicLogger::markedForRetransmit_(ZquicLogRecoveryEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeMarkedForRetransmitEvent_(event_, time);
  };
  post_(fn_);
}

void ZquicLogger::metricsUpdated_(ZquicLogRecoveryEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeRecoveryMetricsEvent_(event_, time);
  };
  post_(fn_);
}

void ZquicLogger::lossTimerUpdated_(ZquicLogRecoveryEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeTimerEvent_(event_, time);
  };
  post_(fn_);
}

void ZquicLogger::congestionStateUpdated_(ZquicLogRecoveryEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeCongestionStateEvent_(event_, time);
  };
  post_(fn_);
}

void ZquicLogger::ecnStateUpdated_(ZquicLogECNEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeECNEvent_(event_, time);
  };
  post_(fn_);
}

void ZquicLogger::keyUpdated_(ZquicLogSecurityEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeKeyEvent_(ZquicLogEventName::KeyUpd, event_, time);
  };
  post_(fn_);
}

void ZquicLogger::keyRetired_(ZquicLogSecurityEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeKeyEvent_(ZquicLogEventName::KeyDiscarded, event_, time);
  };
  post_(fn_);
}

void ZquicLogger::transportParametersSet_(ZquicLogTransportParamsEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeTransportParamsEvent_(event_, time);
  };
  post_(fn_);
}

void ZquicLogger::alpnInformation_(ZquicLogSecurityEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeALPNEvent_(event_, time);
  };
  post_(fn_);
}

void ZquicLogger::versionInformation_(ZquicLogVersionEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeVersionEvent_(event_, time);
  };
  post_(fn_);
}

void ZquicLogger::tlsAlert_(ZquicLogSecurityEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeSecurityEvent_(ZquicLogEventName::TLSAlert, event_, time);
  };
  post_(fn_);
}

void ZquicLogger::securityEvent_(
  ZquicLogEventName::T name, ZquicLogSecurityEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [name_ = name, event_ = ZuMv(event), time](
    ZquicLogger *this_) mutable {
    this_->writeSecurityEvent_(name_, event_, time);
  };
  post_(fn_);
}

void ZquicLogger::pathUpdated_(ZquicLogPathEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writePathEvent_(ZquicLogEventName::TupleAssigned, event_, time);
  };
  post_(fn_);
}

void ZquicLogger::pathValidationUpdated_(ZquicLogPathEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writePathValidationEvent_(event_, time);
  };
  post_(fn_);
}

void ZquicLogger::pmtudUpdated_(ZquicLogPathEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeMTUEvent_(event_, time);
  };
  post_(fn_);
}

void ZquicLogger::cidUpdated_(ZquicLogCIDEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeCIDEvent_(ZquicLogEventName::CIDUpd, event_, time);
  };
  post_(fn_);
}

void ZquicLogger::streamStateUpdated_(ZquicLogStreamEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeStreamEvent_(
      ZquicLogEventName::StreamStateUpd, event_, time);
  };
  post_(fn_);
}

void ZquicLogger::streamDataMoved_(ZquicLogStreamDataEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeStreamDataEvent_(
      ZquicLogEventName::StreamDataMoved, event_, time);
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
      ZquicLogEventName::ConnDataBlockedUpd, event_, time);
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
      ZquicLogEventName::StreamDataBlockedUpd, event_, time);
  };
  post_(fn_);
}

void ZquicLogger::connectionClosed_(ZquicLogCloseEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeCloseEvent_(ZquicLogEventName::ConnClosed, event_, time);
  };
  post_(fn_);
}

void ZquicLogger::connectionStateUpdated_(
  ZquicLogConnectionStateEvent event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    this_->writeConnectionStateEvent_(
      ZquicLogEventName::ConnStateUpd, event_, time);
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
	if (!writeHeader_()) break;
	m_ring.shift2(Fn::invoke(ptr, this));
      } else {
	if (m_ring.readStatus() == Zu::EndOfFile) break;
      }
    }
  }
  m_sink.final();
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
    "urn:ietf:params:qlog:file:sequential",
    "application/qlog+json-seq",
    "zquic",
    QLogImplementation{"zquic", Z_VERNAME},
    QLogTrace{
      QLogVantagePoint{vantagePoint},
      QLogCommonFields{
	.originalDCID = originalDCID,
	.groupID = groupID,
	.dcid = dcid,
	.scid = scid,
	.timeFormat = "relative_to_epoch",
	.referenceTime = QLogReferenceTime{
	  .clockType = "system",
	  .epoch = "1970-01-01T00:00:00.000Z"
	}
      },
      QLogEventSchemas{
	"urn:ietf:params:qlog:events:quic-12",
	"urn:zlib:zquic:qlog:events:zquic"
      }
    }
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, header);
  m_buf << '\n';
  if (!m_sink.write(m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

struct QLogConnectionStartedDataJSON {
  template <typename O, typename Facet>
  struct Handler {
    template <template <typename> class Filter, typename S>
    static void save(S &s, const QLogConnectionStartedData &event)
    {
      bool comma = false;
      s << '{';
      if (event.local)
	qlogJSONField_<Facet, Filter, ZtFieldTC::UDT>(
	  s, comma, "local", event.local);
      if (event.remote)
	qlogJSONField_<Facet, Filter, ZtFieldTC::UDT>(
	  s, comma, "remote", event.remote);
      s << '}';
    }
  };
};

bool ZquicLogger::writeConnectionStartedEvent_(
  const ZquicLogConnectionStartedEvent &event, ZuTime time)
{
  QLogConnectionStartedEvent qevent{
    qlogTime_(time),
    ZquicLogEventName::ConnStarted,
    QLogConnectionStartedData{
      event.local ?
	QLogEndpointInfo{true, event.local.ip(), event.local.port()} :
	QLogEndpointInfo{},
      event.remote ?
	QLogEndpointInfo{true, event.remote.ip(), event.remote.port()} :
	QLogEndpointInfo{}
    },
    event.metadata
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write(m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeDatagramEvent_(
  ZquicLogEventName::T name, const ZquicLogDatagramEvent &event, ZuTime time)
{
  QLogRawInfoArray raw;
  new (raw.push()) QLogRawInfo{event.size, event.size};
  QLogECNArray ecn;
  new (ecn.push()) Zquic::EcnMark::T(event.ecn);
  QLogDatagramEvent qevent{
    qlogTime_(time),
    name,
    QLogDatagramData{1, ZuMv(raw), ZuMv(ecn)},
    event.metadata
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write(m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

template <typename S>
static void qlogJSONSep_(S &s, bool &comma)
{
  if (comma) s << ',';
  comma = true;
}

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props,
  typename S, typename T>
static void qlogJSONField_(S &s, bool &comma, ZuCSpan id, const T &v)
{
  qlogJSONSep_(s, comma);
  s << '"';
  s << id;
  s << "\":";
  ZtJSON::saveValue<Facet, Filter, TypeCode, Props>(s, v);
}

struct QLogPacketDataJSON {
  template <typename O, typename Facet>
  struct Handler {
    template <template <typename> class Filter, typename S>
    static void save(S &s, const QLogPacketData &packet)
    {
      using PktSpaceProps =
	ZuTypeList<ZuFieldProp::Enum<Zquic::PktNumSpace::JSON>>;
      using ECNProps =
	ZuTypeList<ZuFieldProp::Enum<Zquic::EcnMark::JSON>>;
      using TriggerProps =
	ZuTypeList<ZuFieldProp::Enum<ZquicLogPacketTrigger::JSON>>;

      bool comma = false;
      s << '{';
      qlogJSONField_<Facet, Filter, ZtFieldTC::UDT>(
	s, comma, "header", packet.header);
      qlogJSONField_<Facet, Filter, ZtFieldTC::Int8, PktSpaceProps>(
	s, comma, "packet_number_space", packet.packetSpace);
      qlogJSONField_<Facet, Filter, ZtFieldTC::UDT>(
	s, comma, "raw", packet.raw);
      qlogJSONField_<Facet, Filter, ZtFieldTC::Int8, ECNProps>(
	s, comma, "ecn", packet.ecn);
      if (packet.trigger != ZquicLogPacketTrigger::None)
	qlogJSONField_<Facet, Filter, ZtFieldTC::Int8, TriggerProps>(
	  s, comma, "trigger", packet.trigger);
      qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	s, comma, "bytes_in_flight", packet.bytesInFlight);
      qlogJSONField_<Facet, Filter, ZtFieldTC::UInt8>(
	s, comma, "frame_count", packet.frameCount);
      qlogJSONField_<Facet, Filter, ZtFieldTC::Bool>(
	s, comma, "frames_truncated", packet.framesTruncated);
      qlogJSONField_<Facet, Filter, ZtFieldTC::UDT>(
	s, comma, "frames", packet.frames);
      qlogJSONField_<Facet, Filter, ZtFieldTC::Bool>(
	s, comma, "ack_eliciting", packet.ackEliciting);
      s << '}';
    }
  };
};

struct QLogFrameDataJSON {
  template <typename O, typename Facet>
  struct Handler {
    template <template <typename> class Filter, typename S>
    static void save(S &s, const QLogFrameData &frame)
    {
      using FrameTypeProps =
	ZuTypeList<ZuFieldProp::Enum<Zquic::FrameType::JSON>>;
      using StreamTypeProps =
	ZuTypeList<ZuFieldProp::Enum<ZquicLogStreamType::JSON>>;
      using BytesHexProps = ZuTypeList<ZuFieldProp::JSON::Hex>;

      bool comma = false;
      s << '{';
      qlogJSONField_<Facet, Filter, ZtFieldTC::Int8, FrameTypeProps>(
	s, comma, "frame_type", frame.frameType);

      switch (frame.frameType) {
	case Zquic::FrameType::Ack:
	  if (!ZuCmp<double>::null(frame.ackDelay) && frame.ackDelay)
	    qlogJSONField_<Facet, Filter, ZtFieldTC::Float>(
	      s, comma, "ack_delay", frame.ackDelay);
	  if (frame.ackedRanges.length())
	    qlogJSONField_<Facet, Filter, ZtFieldTC::UDT>(
	      s, comma, "acked_ranges", frame.ackedRanges);
	  if (frame.ect0)
	    qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	      s, comma, "ect0", frame.ect0);
	  if (frame.ect1)
	    qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	      s, comma, "ect1", frame.ect1);
	  if (frame.ce)
	    qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	      s, comma, "ce", frame.ce);
	  break;
	case Zquic::FrameType::Crypto:
	  qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	    s, comma, "offset", frame.offset);
	  qlogJSONField_<Facet, Filter, ZtFieldTC::UDT>(
	    s, comma, "raw", frame.raw);
	  break;
	case Zquic::FrameType::Stream:
	  qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	    s, comma, "stream_id", frame.streamID);
	  if (frame.offset)
	    qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	      s, comma, "offset", frame.offset);
	  qlogJSONField_<Facet, Filter, ZtFieldTC::UDT>(
	    s, comma, "raw", frame.raw);
	  if (frame.fin)
	    qlogJSONField_<Facet, Filter, ZtFieldTC::Bool>(
	      s, comma, "fin", frame.fin);
	  break;
	case Zquic::FrameType::ResetStream:
	  qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	    s, comma, "stream_id", frame.streamID);
	  qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	    s, comma, "error_code", frame.errorCode);
	  qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	    s, comma, "final_size", frame.finalSize);
	  break;
	case Zquic::FrameType::StopSending:
	  qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	    s, comma, "stream_id", frame.streamID);
	  qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	    s, comma, "error_code", frame.errorCode);
	  break;
	case Zquic::FrameType::MaxData:
	  qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	    s, comma, "maximum", frame.maximum);
	  break;
	case Zquic::FrameType::MaxStreamData:
	  qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	    s, comma, "stream_id", frame.streamID);
	  qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	    s, comma, "maximum", frame.maximum);
	  break;
	case Zquic::FrameType::MaxStreams:
	  qlogJSONField_<Facet, Filter, ZtFieldTC::Int8, StreamTypeProps>(
	    s, comma, "stream_type", frame.streamType);
	  qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	    s, comma, "maximum", frame.maximum);
	  break;
	case Zquic::FrameType::DataBlocked:
	  qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	    s, comma, "limit", frame.limit);
	  break;
	case Zquic::FrameType::StreamDataBlocked:
	  qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	    s, comma, "stream_id", frame.streamID);
	  qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	    s, comma, "limit", frame.limit);
	  break;
	case Zquic::FrameType::StreamsBlocked:
	  qlogJSONField_<Facet, Filter, ZtFieldTC::Int8, StreamTypeProps>(
	    s, comma, "stream_type", frame.streamType);
	  qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	    s, comma, "limit", frame.limit);
	  break;
	case Zquic::FrameType::NewConnectionID:
	  qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	    s, comma, "sequence_number", frame.sequenceNumber);
	  qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	    s, comma, "retire_prior_to", frame.retirePriorTo);
	  qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	    s, comma, "connection_id_length", frame.connectionIDLength);
	  if (frame.connectionID.length())
	    qlogJSONField_<Facet, Filter, ZtFieldTC::Bytes, BytesHexProps>(
	      s, comma, "connection_id", frame.connectionID);
	  if (frame.statelessResetToken.length())
	    qlogJSONField_<Facet, Filter, ZtFieldTC::Bytes, BytesHexProps>(
	      s, comma, "stateless_reset_token", frame.statelessResetToken);
	  break;
	case Zquic::FrameType::RetireConnectionID:
	  qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	    s, comma, "sequence_number", frame.sequenceNumber);
	  break;
	case Zquic::FrameType::ConnectionClose:
	case Zquic::FrameType::ApplicationClose:
	  qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	    s, comma, "error_code", frame.errorCode);
	  if (frame.raw.length)
	    qlogJSONField_<Facet, Filter, ZtFieldTC::UDT>(
	      s, comma, "raw", frame.raw);
	  break;
	case Zquic::FrameType::NewToken:
	case Zquic::FrameType::PathChallenge:
	case Zquic::FrameType::PathResponse:
	  if (frame.raw.length)
	    qlogJSONField_<Facet, Filter, ZtFieldTC::UDT>(
	      s, comma, "raw", frame.raw);
	  break;
	default:
	  break;
      }
      s << '}';
    }
  };
};

struct QLogKeyDataJSON {
  template <typename O, typename Facet>
  struct Handler {
    template <template <typename> class Filter, typename S>
    static void save(S &s, const QLogKeyData &key)
    {
      using KeyTypeProps =
	ZuTypeList<ZuFieldProp::Enum<ZquicLogQKeyType::JSON>>;
      using TriggerProps =
	ZuTypeList<ZuFieldProp::Enum<ZquicLogQKeyTrigger::JSON>>;

      bool comma = false;
      s << '{';
      qlogJSONField_<Facet, Filter, ZtFieldTC::Int8, KeyTypeProps>(
	s, comma, "key_type", key.keyType);
      if (key.keyPhase != QLogKeyPhaseNull)
	qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	  s, comma, "key_phase", key.keyPhase);
      qlogJSONField_<Facet, Filter, ZtFieldTC::Int8, TriggerProps>(
	s, comma, "trigger", key.trigger);
      s << '}';
    }
  };
};

struct QLogTransportParamsDataJSON {
  template <typename O, typename Facet>
  struct Handler {
    template <template <typename> class Filter, typename S>
    static void save(S &s, const QLogTransportParamsData &params)
    {
      using InitiatorProps =
	ZuTypeList<ZuFieldProp::Enum<ZquicLogQInitiator::JSON>>;
      using BytesHexProps = ZuTypeList<ZuFieldProp::JSON::Hex>;

      bool comma = false;
      s << '{';
      qlogJSONField_<Facet, Filter, ZtFieldTC::Int8, InitiatorProps>(
	s, comma, "initiator", params.initiator);
      if (params.originalDCID.length())
	qlogJSONField_<Facet, Filter, ZtFieldTC::Bytes, BytesHexProps>(
	  s, comma, "original_destination_connection_id",
	  params.originalDCID);
      if (params.initialSCID.length())
	qlogJSONField_<Facet, Filter, ZtFieldTC::Bytes, BytesHexProps>(
	  s, comma, "initial_source_connection_id", params.initialSCID);
      if (params.retrySCID.length())
	qlogJSONField_<Facet, Filter, ZtFieldTC::Bytes, BytesHexProps>(
	  s, comma, "retry_source_connection_id", params.retrySCID);
      if (params.statelessResetToken.length())
	qlogJSONField_<Facet, Filter, ZtFieldTC::Bytes, BytesHexProps>(
	  s, comma, "stateless_reset_token", params.statelessResetToken);
      qlogJSONField_<Facet, Filter, ZtFieldTC::Bool>(
	s, comma, "disable_active_migration", params.disableActiveMigration);
      qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	s, comma, "max_idle_timeout", params.maxIdleTimeout);
      qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	s, comma, "max_udp_payload_size", params.maxUDPPayloadSize);
      qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	s, comma, "ack_delay_exponent", params.ackDelayExponent);
      qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	s, comma, "max_ack_delay", params.maxAckDelay);
      qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	s, comma, "active_connection_id_limit",
	params.activeConnectionIDLimit);
      qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	s, comma, "initial_max_data", params.initialMaxData);
      qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	s, comma, "initial_max_stream_data_bidi_local",
	params.initialMaxStreamDataBidiLocal);
      qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	s, comma, "initial_max_stream_data_bidi_remote",
	params.initialMaxStreamDataBidiRemote);
      qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	s, comma, "initial_max_stream_data_uni",
	params.initialMaxStreamDataUni);
      qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	s, comma, "initial_max_streams_bidi", params.initialMaxStreamsBidi);
      qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	s, comma, "initial_max_streams_uni", params.initialMaxStreamsUni);
      s << '}';
    }
  };
};

template <typename S>
static void qlogJSONVersion_(S &s, uint32_t version)
{
  static constexpr char hex[] = "0123456789abcdef";

  s << '"';
  for (int i = 28; i >= 0; i -= 4)
    s << hex[(version >> i) & 0x0f];
  s << '"';
}

template <typename S>
static void qlogJSONVersionArray_(S &s, const ZquicLogVersionArray &versions)
{
  s << '[';
  for (unsigned i = 0, n = versions.length(); i < n; ++i) {
    if (i) s << ',';
    qlogJSONVersion_(s, versions[i]);
  }
  s << ']';
}

struct QLogVersionDataJSON {
  template <typename O, typename Facet>
  struct Handler {
    template <template <typename> class Filter, typename S>
    static void save(S &s, const QLogVersionData &version)
    {
      bool comma = false;
      s << '{';
      if (version.serverVersions.length()) {
	qlogJSONSep_(s, comma);
	s << "\"server_versions\":";
	qlogJSONVersionArray_(s, version.serverVersions);
      }
      if (version.clientVersions.length()) {
	qlogJSONSep_(s, comma);
	s << "\"client_versions\":";
	qlogJSONVersionArray_(s, version.clientVersions);
      }
      if (version.chosenVersionPresent) {
	qlogJSONSep_(s, comma);
	s << "\"chosen_version\":";
	qlogJSONVersion_(s, version.chosenVersion);
      }
      s << '}';
    }
  };
};

template <typename S>
static void qlogJSONTupleID_(S &s, uint64_t tupleID)
{
  s << '"';
  if (tupleID) s << tupleID;
  s << '"';
}

struct QLogTupleAssignedDataJSON {
  template <typename O, typename Facet>
  struct Handler {
    template <template <typename> class Filter, typename S>
    static void save(S &s, const QLogTupleAssignedData &tuple)
    {
      s << "{\"tuple_id\":";
      qlogJSONTupleID_(s, tuple.tupleID);
      s << '}';
    }
  };
};

struct QLogCIDDataJSON {
  template <typename O, typename Facet>
  struct Handler {
    template <template <typename> class Filter, typename S>
    static void save(S &s, const QLogCIDData &cid)
    {
      using InitiatorProps =
	ZuTypeList<ZuFieldProp::Enum<ZquicLogQInitiator::JSON>>;
      using BytesHexProps = ZuTypeList<ZuFieldProp::JSON::Hex>;

      bool comma = false;
      s << '{';
      qlogJSONField_<Facet, Filter, ZtFieldTC::Int8, InitiatorProps>(
	s, comma, "initiator", cid.initiator);
      if (cid.oldCID.length())
	qlogJSONField_<Facet, Filter, ZtFieldTC::Bytes, BytesHexProps>(
	  s, comma, "old", cid.oldCID);
      if (cid.newCID.length())
	qlogJSONField_<Facet, Filter, ZtFieldTC::Bytes, BytesHexProps>(
	  s, comma, "new", cid.newCID);
      s << '}';
    }
  };
};

static ZquicLogCloseError::T qlogCloseError_(uint64_t code)
{
  switch (code) {
    case Zquic::TransportError::NoError:
      return ZquicLogCloseError::NoError;
    case Zquic::TransportError::InternalError:
      return ZquicLogCloseError::Internal;
    case Zquic::TransportError::ConnectionRefused:
      return ZquicLogCloseError::ConnectionRefused;
    case Zquic::TransportError::FlowControl:
      return ZquicLogCloseError::FlowControl;
    case Zquic::TransportError::StreamLimit:
      return ZquicLogCloseError::StreamLimit;
    case Zquic::TransportError::StreamState:
      return ZquicLogCloseError::StreamState;
    case Zquic::TransportError::FinalSize:
      return ZquicLogCloseError::FinalSize;
    case Zquic::TransportError::FrameEncoding:
      return ZquicLogCloseError::FrameEncoding;
    case Zquic::TransportError::TransportParameter:
      return ZquicLogCloseError::TransportParameter;
    case Zquic::TransportError::ConnectionIDLimit:
      return ZquicLogCloseError::ConnectionIDLimit;
    case Zquic::TransportError::ProtocolViolation:
      return ZquicLogCloseError::ProtocolViolation;
    default:
      return ZquicLogCloseError::Unknown;
  }
}

static ZquicLogCloseError::T qlogCloseError_(
  ZquicLogCloseError::T error, uint64_t code)
{
  return error == ZquicLogCloseError::Unknown ?
    qlogCloseError_(code) : error;
}

struct QLogCloseDataJSON {
  template <typename O, typename Facet>
  struct Handler {
    template <template <typename> class Filter, typename S>
    static void save(S &s, const QLogCloseData &close)
    {
      using InitiatorProps =
	ZuTypeList<ZuFieldProp::Enum<ZquicLogCloseInitiator::JSON>>;
      using TriggerProps =
	ZuTypeList<ZuFieldProp::Enum<ZquicLogCloseTrigger::JSON>>;
      using ReasonProps =
	ZuTypeList<ZuFieldProp::Enum<ZquicLogCloseReason::JSON>>;
      using ErrorProps =
	ZuTypeList<ZuFieldProp::Enum<ZquicLogCloseError::JSON>>;

      ZquicLogCloseError::T connectionError =
	qlogCloseError_(close.connectionError, close.errorCode);
      ZquicLogCloseError::T applicationError = close.applicationError;

      bool comma = false;
      s << '{';
      qlogJSONField_<Facet, Filter, ZtFieldTC::Int8, InitiatorProps>(
	s, comma, "initiator", close.initiator);
      if (connectionError != ZquicLogCloseError::None)
	qlogJSONField_<Facet, Filter, ZtFieldTC::Int8, ErrorProps>(
	  s, comma, "connection_error", connectionError);
      if (applicationError != ZquicLogCloseError::None)
	qlogJSONField_<Facet, Filter, ZtFieldTC::Int8, ErrorProps>(
	  s, comma, "application_error", applicationError);
      if ((connectionError == ZquicLogCloseError::Unknown ||
	  applicationError == ZquicLogCloseError::Unknown) &&
	  close.errorCode)
	qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	  s, comma, "error_code", close.errorCode);
      if (close.reason != ZquicLogCloseReason::None)
	qlogJSONField_<Facet, Filter, ZtFieldTC::Int8, ReasonProps>(
	  s, comma, "reason", close.reason);
      qlogJSONField_<Facet, Filter, ZtFieldTC::Int8, TriggerProps>(
	s, comma, "trigger", close.trigger);
      s << '}';
    }
  };
};

static QLogFrameArray qlogFrames_(
  const ZuArray<ZquicLogFrameEvent, ZquicLogFrameMax> &frames_)
{
  QLogFrameArray frames;
  for (unsigned i = 0, n = frames_.length(); i < n; ++i) {
    const ZquicLogFrameEvent &frame = frames_[i];
    QLogFrameData qframe{.frameType = frame.type};
    switch (frame.type) {
      case Zquic::FrameType::Ack:
	qframe.ackDelay = qlogMS_(frame.ackDelayUS);
	for (unsigned j = 0, n = frame.ackRanges.length(); j < n; ++j) {
	  QLogAckRange range;
	  new (range.push()) uint64_t(frame.ackRanges[j].first);
	  new (range.push()) uint64_t(frame.ackRanges[j].largest);
	  new (qframe.ackedRanges.push()) QLogAckRange{range};
	}
	qframe.ect0 = frame.ect0;
	qframe.ect1 = frame.ect1;
	qframe.ce = frame.ce;
	break;
      case Zquic::FrameType::Crypto:
	qframe.offset = frame.offset;
	qframe.raw.length = frame.length;
	break;
      case Zquic::FrameType::Stream:
	qframe.streamID = frame.streamID;
	qframe.offset = frame.offset;
	qframe.raw.length = frame.length;
	qframe.fin = frame.fin;
	break;
      case Zquic::FrameType::ResetStream:
	qframe.streamID = frame.streamID;
	qframe.errorCode = frame.errorCode;
	qframe.finalSize = frame.length;
	break;
      case Zquic::FrameType::StopSending:
	qframe.streamID = frame.streamID;
	qframe.errorCode = frame.errorCode;
	break;
      case Zquic::FrameType::MaxData:
      case Zquic::FrameType::MaxStreamData:
	qframe.streamID = frame.streamID;
	qframe.maximum = frame.value;
	break;
      case Zquic::FrameType::MaxStreams:
	qframe.streamType = frame.streamType;
	qframe.maximum = frame.value;
	break;
      case Zquic::FrameType::DataBlocked:
      case Zquic::FrameType::StreamDataBlocked:
	qframe.streamID = frame.streamID;
	qframe.limit = frame.value;
	break;
      case Zquic::FrameType::StreamsBlocked:
	qframe.streamType = frame.streamType;
	qframe.limit = frame.value;
	break;
      case Zquic::FrameType::NewConnectionID:
	qframe.sequenceNumber = frame.offset;
	qframe.retirePriorTo = frame.value;
	qframe.connectionIDLength = frame.length;
	qframe.connectionID = frame.connectionID;
	qframe.statelessResetToken = frame.resetToken.bspan();
	break;
      case Zquic::FrameType::RetireConnectionID:
	qframe.sequenceNumber = frame.value;
	break;
      case Zquic::FrameType::ConnectionClose:
      case Zquic::FrameType::ApplicationClose:
	qframe.errorCode = frame.errorCode;
	qframe.raw.length = frame.length;
	break;
      case Zquic::FrameType::NewToken:
      case Zquic::FrameType::PathChallenge:
      case Zquic::FrameType::PathResponse:
	qframe.raw.length = frame.length;
	break;
      default:
	break;
    }
    new (frames.push()) QLogFrameData{qframe};
  }
  return frames;
}

static ZquicLogPacketTrigger::T qlogPacketTrigger_(
  ZquicLogEventName::T name, ZquicLogPacketEvent::Reason::T reason)
{
  switch (name) {
    case ZquicLogEventName::PktBuf:
      switch (reason) {
	case ZquicLogPacketEvent::Reason::MissingKeys:
	  return ZquicLogPacketTrigger::KeysUnavailable;
	default:
	  return ZquicLogPacketTrigger::None;
      }
    case ZquicLogEventName::PktDrop:
      switch (reason) {
	case ZquicLogPacketEvent::Reason::PrepareLong:
	case ZquicLogPacketEvent::Reason::UnsupportedLongType:
	case ZquicLogPacketEvent::Reason::DiscardedSpace:
	  return ZquicLogPacketTrigger::Unsupported;
	case ZquicLogPacketEvent::Reason::ParseLong:
	case ZquicLogPacketEvent::Reason::PacketLength:
	case ZquicLogPacketEvent::Reason::ParseShort:
	case ZquicLogPacketEvent::Reason::InvalidKeyPhase:
	  return ZquicLogPacketTrigger::Invalid;
	case ZquicLogPacketEvent::Reason::MissingKeys:
	  return ZquicLogPacketTrigger::KeyUnavailable;
	case ZquicLogPacketEvent::Reason::Protection:
	  return ZquicLogPacketTrigger::DecryptionFailure;
	case ZquicLogPacketEvent::Reason::Duplicate:
	  return ZquicLogPacketTrigger::Duplicate;
	case ZquicLogPacketEvent::Reason::AntiAmplification:
	case ZquicLogPacketEvent::Reason::ProbeAdmission:
	case ZquicLogPacketEvent::Reason::AppSend:
	  return ZquicLogPacketTrigger::Rejected;
	default:
	  return ZquicLogPacketTrigger::General;
      }
    default:
      return ZquicLogPacketTrigger::None;
  }
}

bool ZquicLogger::writePacketEvent_(
  ZquicLogEventName::T name, const ZquicLogPacketEvent &event, ZuTime time)
{
  QLogPacketEvent qevent{
    qlogTime_(time),
    name,
    QLogPacketData{
      QLogPacketHeader{event.packetType, event.packetNumber},
	      event.packetSpace,
	      QLogRawInfo{event.packetSize, event.payloadSize},
	      event.ecn,
	      qlogPacketTrigger_(name, event.reason),
	      event.bytesInFlight,
      event.frameCount,
      event.framesTruncated,
      qlogFrames_(event.frames),
      event.ackEliciting
    },
    event.metadata
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write(m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeAckEvent_(
  ZquicLogEventName::T name, const ZquicLogAckEvent &event, ZuTime time)
{
  QLogPacketNumberArray packetNumbers;
  for (unsigned i = 0; i < event.packetNumbers.length(); ++i)
    new (packetNumbers.push()) uint64_t(event.packetNumbers[i]);
  if (!packetNumbers.length())
    return true;
  QLogAckEvent qevent{
    qlogTime_(time),
    name,
    QLogAckData{
      event.packetSpace,
      ZuMv(packetNumbers)
    },
    event.metadata
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write(m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

static ZquicLogPacketLostTrigger::T qlogLossTrigger_(
  ZquicLogRecoveryReason::T reason)
{
  switch (reason) {
    case ZquicLogRecoveryReason::TimeThreshold:
      return ZquicLogPacketLostTrigger::TimeThreshold;
    case ZquicLogRecoveryReason::Expired:
      return ZquicLogPacketLostTrigger::PTOExpired;
    default:
      return ZquicLogPacketLostTrigger::ReorderingThreshold;
  }
}

bool ZquicLogger::writePacketLostEvent_(
  const ZquicLogRecoveryEvent &event, ZuTime time)
{
  QLogPacketLostEvent qevent{
    qlogTime_(time),
    ZquicLogEventName::PktLost,
    QLogPacketLostData{
      QLogPacketHeader{
	Zquic::pktTypeFromPktNumSpace(event.packetSpace),
	event.packetNumber
      },
      qlogLossTrigger_(event.reason)
    },
    event.metadata
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write(m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeMarkedForRetransmitEvent_(
  const ZquicLogRecoveryEvent &event, ZuTime time)
{
  if (!event.frames.length()) return true;
  QLogMarkedForRetransmitEvent qevent{
    qlogTime_(time),
    ZquicLogEventName::MarkRetrans,
    QLogMarkedForRetransmitData{
      qlogFrames_(event.frames)
    },
    event.metadata
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write(m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeRecoveryMetricsEvent_(
  const ZquicLogRecoveryEvent &event, ZuTime time)
{
  QLogRecoveryMetricsEvent qevent{
    qlogTime_(time),
    ZquicLogEventName::MetricsUpd,
    QLogRecoveryMetricsData{
      qlogMS_(event.latestRTTUS),
      qlogMS_(event.smoothedRTTUS),
      qlogMS_(event.rttVarianceUS),
      qlogMS_(event.minRTTUS),
      event.cwnd,
      event.ssthresh,
      event.bytesInFlight
    },
    event.metadata
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write(m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

static ZquicLogCongestionState::T qlogCongestionState_(
  const ZquicLogRecoveryEvent &event)
{
  switch (event.reason) {
    case ZquicLogRecoveryReason::Loss:
    case ZquicLogRecoveryReason::PMTUDLoss:
      return ZquicLogCongestionState::Recovery;
    default:
      return event.cwnd < event.ssthresh ?
	ZquicLogCongestionState::SlowStart :
	ZquicLogCongestionState::CongestionAvoidance;
  }
}

static ZquicLogCongestionTrigger::T qlogCongestionTrigger_(
  ZquicLogRecoveryReason::T reason)
{
  switch (reason) {
    case ZquicLogRecoveryReason::PMTUDAck:
      return ZquicLogCongestionTrigger::PMTUDAck;
    case ZquicLogRecoveryReason::Loss:
      return ZquicLogCongestionTrigger::Loss;
    case ZquicLogRecoveryReason::PMTUDLoss:
      return ZquicLogCongestionTrigger::PMTUDLoss;
    default:
      return ZquicLogCongestionTrigger::Ack;
  }
}

bool ZquicLogger::writeCongestionStateEvent_(
  const ZquicLogRecoveryEvent &event, ZuTime time)
{
  QLogCongestionStateEvent qevent{
    qlogTime_(time),
    ZquicLogEventName::CongestionUpd,
    QLogCongestionStateData{
      qlogCongestionState_(event),
      qlogCongestionTrigger_(event.reason)
    },
    event.metadata
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write(m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

static ZquicLogTimerType::T qlogTimerType_(
  ZquicLogRecoveryKind::T kind)
{
  switch (kind) {
    case ZquicLogRecoveryKind::PTO:
      return ZquicLogTimerType::PTO;
    default:
      return ZquicLogTimerType::Loss;
  }
}

static ZquicLogTimerEventType::T qlogTimerEventType_(
  ZquicLogRecoveryReason::T reason)
{
  switch (reason) {
    case ZquicLogRecoveryReason::Armed:
      return ZquicLogTimerEventType::Set;
    case ZquicLogRecoveryReason::Expired:
    case ZquicLogRecoveryReason::Backoff:
    case ZquicLogRecoveryReason::Probe:
      return ZquicLogTimerEventType::Expired;
    default:
      return ZquicLogTimerEventType::Cancelled;
  }
}

static double qlogTimerDelta_(
  const ZquicLogRecoveryEvent &event, ZuTime time)
{
  if (event.reason != ZquicLogRecoveryReason::Armed) return 0;
  uint64_t now = qlogTime_(time);
  if (event.deadlineUS <= now) return 0;
  return qlogMS_(event.deadlineUS - now);
}

bool ZquicLogger::writeTimerEvent_(
  const ZquicLogRecoveryEvent &event, ZuTime time)
{
  QLogTimerEvent qevent{
    qlogTime_(time),
    ZquicLogEventName::TimerUpd,
    QLogTimerData{
      qlogTimerType_(event.kind),
      event.packetSpace,
      qlogTimerEventType_(event.reason),
      qlogTimerDelta_(event, time)
    },
    event.metadata
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write(m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeECNEvent_(
  const ZquicLogECNEvent &event, ZuTime time)
{
  QLogECNEvent qevent{
    qlogTime_(time),
    ZquicLogEventName::ECNUpd,
    QLogECNData{
      ZquicLogECNState::Unknown,
      event.state
    },
    event.metadata
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write(m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

static ZquicLogQVantagePoint::T qlogVantagePoint_(ZuCSpan vantagePoint)
{
  if (vantagePoint == "client") return ZquicLogQVantagePoint::Client;
  if (vantagePoint == "server") return ZquicLogQVantagePoint::Server;
  return ZquicLogQVantagePoint::Unknown;
}

static ZquicLogQKeyType::T qlogKeyType_(
  const ZquicLogSecurityEvent &event, ZquicLogQVantagePoint::T vantagePoint)
{
  bool localClient = vantagePoint != ZquicLogQVantagePoint::Server;
  bool client =
    localClient ?
      event.keyType == ZquicLogSecurityKeyType::TX :
      event.keyType != ZquicLogSecurityKeyType::TX;

  switch (event.packetSpace) {
    case Zquic::PktNumSpace::Initial:
      return client ?
	ZquicLogQKeyType::ClientInitial :
	ZquicLogQKeyType::ServerInitial;
    case Zquic::PktNumSpace::Handshake:
      return client ?
	ZquicLogQKeyType::ClientHandshake :
	ZquicLogQKeyType::ServerHandshake;
    default:
      return client ?
	ZquicLogQKeyType::Client1RTT :
	ZquicLogQKeyType::Server1RTT;
  }
}

static ZquicLogQKeyTrigger::T qlogKeyTrigger_(
  const ZquicLogSecurityEvent &event)
{
  switch (event.trigger) {
    case ZquicLogSecurityTrigger::Local:
      return ZquicLogQKeyTrigger::LocalUpdate;
    case ZquicLogSecurityTrigger::Remote:
    case ZquicLogSecurityTrigger::Peer:
    case ZquicLogSecurityTrigger::Timer:
      return ZquicLogQKeyTrigger::RemoteUpdate;
    default:
      return ZquicLogQKeyTrigger::TLS;
  }
}

bool ZquicLogger::writeKeyEvent_(
  ZquicLogEventName::T name, const ZquicLogSecurityEvent &event, ZuTime time)
{
  ZquicLogQVantagePoint::T vantagePoint;
  {
    Guard guard(m_lock);
    vantagePoint = qlogVantagePoint_(m_vantagePoint);
  }
  QLogKeyEvent qevent{
    qlogTime_(time),
    name,
    QLogKeyData{
      qlogKeyType_(event, vantagePoint),
      event.packetSpace == Zquic::PktNumSpace::AppData ?
	event.value : QLogKeyPhaseNull,
      qlogKeyTrigger_(event)
    },
    event.metadata
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write(m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeTransportParamsEvent_(
  const ZquicLogTransportParamsEvent &event, ZuTime time)
{
  QLogTransportParamsEvent qevent{
    qlogTime_(time),
    ZquicLogEventName::ParamsSet,
    QLogTransportParamsData{
      event.initiator,
      event.originalDCID,
      event.initialSCID,
      event.retrySCID,
      event.statelessResetTokenPresent ?
	event.statelessResetToken.bspan() : ZuBSpan{},
      event.maxIdleTimeout,
      event.maxUDPPayloadSize,
      event.ackDelayExponent,
      event.maxAckDelay,
      event.activeConnectionIDLimit,
      event.initialMaxData,
      event.initialMaxStreamDataBidiLocal,
      event.initialMaxStreamDataBidiRemote,
      event.initialMaxStreamDataUni,
      event.initialMaxStreamsBidi,
      event.initialMaxStreamsUni,
      event.disableActiveMigration
    },
    event.metadata
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write(m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeALPNEvent_(
  const ZquicLogSecurityEvent &event, ZuTime time)
{
  QLogALPNEvent qevent{
    qlogTime_(time),
    ZquicLogEventName::ALPNInfo,
    QLogALPNData{QLogALPNIdentifier{event.alpn}},
    event.metadata
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write(m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeVersionEvent_(
  const ZquicLogVersionEvent &event, ZuTime time)
{
  QLogVersionEvent qevent{
    qlogTime_(time),
    ZquicLogEventName::VersionInfo,
    QLogVersionData{
      event.serverVersions,
      event.clientVersions,
      event.chosenVersion,
      event.chosenVersionPresent
    },
    event.metadata
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write(m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeSecurityEvent_(
  ZquicLogEventName::T name, const ZquicLogSecurityEvent &event, ZuTime time)
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
    },
    event.metadata
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write(m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writePathEvent_(
  ZquicLogEventName::T name, const ZquicLogPathEvent &event, ZuTime time)
{
  QLogPathEvent qevent{
    qlogTime_(time),
    name,
    QLogTupleAssignedData{event.tupleID},
    event.metadata
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write(m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeMTUEvent_(
  const ZquicLogPathEvent &event, ZuTime time)
{
  QLogMTUEvent qevent{
    qlogTime_(time),
    ZquicLogEventName::MTUUpd,
    QLogMTUData{
      event.mtu,
      event.validated
    },
    event.metadata
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write(m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writePathValidationEvent_(
  const ZquicLogPathEvent &event, ZuTime time)
{
  bool success = event.validated &&
    event.action != ZquicLogPathAction::Failed &&
    event.action != ZquicLogPathAction::Expired;
  ZquicLogQVantagePoint::T vantagePoint;
  {
    Guard guard(m_lock);
    vantagePoint = qlogVantagePoint_(m_vantagePoint);
  }
  QLogPathValidationEvent qevent{
    qlogTime_(time),
    ZquicLogEventName::PathValidated,
    QLogPathValidationData{
      success,
      vantagePoint
    },
    event.metadata
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write(m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeCIDEvent_(
  ZquicLogEventName::T name, const ZquicLogCIDEvent &event, ZuTime time)
{
  bool retired =
    event.action == ZquicLogCIDAction::Retired ||
    event.action == ZquicLogCIDAction::Tombstone;
  auto cid = event.connectionID.cspan();
  QLogCIDEvent qevent{
    qlogTime_(time),
    name,
    QLogCIDData{
      ZquicLogQInitiator::T(
	event.local ? ZquicLogQInitiator::Local : ZquicLogQInitiator::Remote),
      retired ? cid : ZuBSpan{},
      retired ? ZuBSpan{} : cid
    },
    event.metadata
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write(m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeStreamEvent_(
  ZquicLogEventName::T name, const ZquicLogStreamEvent &event, ZuTime time)
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
    },
    event.metadata
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write(m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeStreamDataEvent_(
  ZquicLogEventName::T name, const ZquicLogStreamDataEvent &event, ZuTime time)
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
      QLogRawInfo{event.length, event.length}
    },
    event.metadata
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write(m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeConnectionBlockedEvent_(
  ZquicLogEventName::T name, const ZquicLogBlockedEvent &event, ZuTime time)
{
  QLogConnectionBlockedEvent qevent{
    qlogTime_(time),
    name,
    QLogConnectionBlockedData{
      event.oldState,
      event.newState,
      event.reason
    },
    event.metadata
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write(m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeStreamBlockedEvent_(
  ZquicLogEventName::T name, const ZquicLogBlockedEvent &event, ZuTime time)
{
  QLogStreamBlockedEvent qevent{
    qlogTime_(time),
    name,
    QLogStreamBlockedData{
      event.oldState,
      event.newState,
      event.streamID,
      event.reason
    },
    event.metadata
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write(m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeCloseEvent_(
  ZquicLogEventName::T name, const ZquicLogCloseEvent &event, ZuTime time)
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
      event.errorCode
    },
    event.metadata
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write(m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeConnectionStateEvent_(
  ZquicLogEventName::T name,
  const ZquicLogConnectionStateEvent &event, ZuTime time)
{
  QLogConnectionStateEvent qevent{
    qlogTime_(time),
    name,
    QLogConnectionStateData{
      event.oldState,
      event.newState
    },
    event.metadata
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!m_sink.write(m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

#undef QLogEventFmt

#endif /* Zquic_DEBUG */
