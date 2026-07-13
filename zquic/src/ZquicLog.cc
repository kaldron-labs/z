//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC qlog

#include <zlib/ZmSingleton.hh>

#include <zlib/ZtJSON.hh>
#include <zlib/ZtBuiltin.hh>

#include <zlib/Zquic.hh>

#ifdef Zquic_DEBUG

using namespace ZquicLog_;

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props = ZuTypeList<>,
  typename S, typename T>
static void qlogJSONField_(S &, bool &, ZuCSpan, const T &);

struct QLogVantage {
  Zquic::Vantage::T	type = Zquic::Vantage::Unknown;
};

ZtStruct((QLogVantage, JSON),
  (((type), (Enum<Zquic::Vantage::JSON>)), (Int8)));

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
  ZuBSpan	origDCID;
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
      if (fields.origDCID)
	qlogJSONField_<Facet, Filter, ZtFieldTC::Bytes, BytesHexProps>(
	  s, comma, "ODCID", fields.origDCID);
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

namespace Zquic {
ZtStruct((LinkInfo, JSON),
  (((origDCID), (JSON::ID<"ODCID">, JSON::Hex, JSON::Opt)), (Bytes)),
  (((groupID), (JSON::ID<"group_id">, JSON::Hex, JSON::Opt)), (Bytes)),
  (((dcid), (JSON::ID<"DCID">, JSON::Hex, JSON::Opt)), (Bytes)),
  (((scid), (JSON::ID<"SCID">, JSON::Hex, JSON::Opt)), (Bytes)));
}

template <typename Event>
struct QLogEvtJSON {
  template <typename O, typename Facet>
  struct Handler {
    template <template <typename> class Filter, typename S>
    static void save(S &s, const Event &event)
    {
      using NameProps =
	ZuTypeList<ZuFieldProp::Enum<EvtName::JSON>>;

      bool comma = false;
      s << '{';
      qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	s, comma, "time", event.time);
      qlogJSONField_<Facet, Filter, ZtFieldTC::Int8, NameProps>(
	s, comma, "name", event.name);
      qlogJSONField_<Facet, Filter, ZtFieldTC::UDT>(
	s, comma, "data", event.data);
      if (event.linkInfo)
	qlogJSONField_<Facet, Filter, ZtFieldTC::UDT>(
	  s, comma, "common_fields", event.linkInfo);
      s << '}';
    }
  };
};

#define QLogEvtFmt(Type) inline QLogEvtJSON<Type> ZtJSON_Fmt(Type *)

ZuDerive(QLogEvtSchemas,
  (ZuArray<ZuCSpan, 2>));
inline ZtJSON::AsArray<ZtFieldTC::String> ZtJSON_Fmt(QLogEvtSchemas *);

struct QLogTrace {
  QLogVantage	vantage;
  QLogCommonFields	commonFields;
  QLogEvtSchemas	eventSchemas;
};

ZtStruct((QLogTrace, JSON),
  (((vantage), (JSON::ID<"vantage_point">)), (UDT)),
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

struct QLogCxnStartedData {
  QLogEndpointInfo local;
  QLogEndpointInfo remote;
};

struct QLogCxnStartedDataJSON;
inline QLogCxnStartedDataJSON ZtJSON_Fmt(QLogCxnStartedData *);

struct QLogCxnStartedEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::CxnStarted;
  QLogCxnStartedData data;
  Zquic::LinkInfo linkInfo;
};

QLogEvtFmt(QLogCxnStartedEvt);

struct QLogCxnStateData {
  Zquic::LinkState::T oldState = Zquic::LinkState::Starting;
  Zquic::LinkState::T newState = Zquic::LinkState::Handshaking;
};

ZtStruct((QLogCxnStateData, JSON),
  (((oldState), (JSON::ID<"old">,
    Enum<Zquic::LinkState::JSON>)), (Int8)),
  (((newState), (JSON::ID<"new">,
    Enum<Zquic::LinkState::JSON>)), (Int8)));

struct QLogCxnStateEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::CxnStateUpd;
  QLogCxnStateData data;
  Zquic::LinkInfo linkInfo;
};

QLogEvtFmt(QLogCxnStateEvt);

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
  (ZuArray<QLogAckRange, AckRangeMax>));
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

struct QLogDatagramEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::UDPTx;
  QLogDatagramData data;
  Zquic::LinkInfo linkInfo;
};

QLogEvtFmt(QLogDatagramEvt);

struct QLogFrameData {
	Zquic::FrameType::T frameType = Zquic::FrameType::Unknown;
  uint64_t	streamID = 0;
  uint64_t	offset = 0;
  QLogRawInfo	raw;
  double	ackDelay = 0;
  QLogAckRangeArray ackedRanges;
  StreamType::T streamType = StreamType::Duplex;
  uint64_t	maximum = 0;
  uint64_t	limit = 0;
  uint64_t	errorCode = 0;
  uint64_t	finalSize = 0;
  uint64_t	sequenceNumber = 0;
  uint64_t	retirePriorTo = 0;
  uint64_t	cxnIDLength = 0;
  ZuBSpan	cxnID;
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
  (ZuArray<QLogFrameData, FrameMax>));
inline ZtJSON::AsArray<ZtFieldTC::UDT> ZtJSON_Fmt(QLogFrameArray *);

struct QLogPacketHeader {
	Zquic::PktType::T packetType = Zquic::PktType::Initial;
	uint64_t	packetNumber = 0;
};

ZtStruct((QLogPacketHeader, JSON),
  (((packetType), (JSON::ID<"packet_type">, Enum<Zquic::PktType::JSON>)),
      (Int8)),
  (((packetNumber), (JSON::ID<"packet_number">)), (UInt64)));

struct PktTrigger {
  ZtEnum(PktTrigger, int8_t,
    None, Backpressure, KeysUnavailable, InternalError, Rejected, Unsupported,
    Invalid, Duplicate, ConnectionUnknown, DecryptionFailure, KeyUnavailable,
    General);
  ZtEnumMap(PktTrigger, JSON,
    "", "backpressure", "keys_unavailable", "internal_error", "rejected",
    "unsupported", "invalid", "duplicate", "connection_unknown",
    "decryption_failure", "key_unavailable", "general");
};

struct QLogPacketData {
	QLogPacketHeader header;
	Zquic::PktNumSpace::T packetSpace = Zquic::PktNumSpace::Initial;
	QLogRawInfo	raw;
	Zquic::EcnMark::T ecn = Zquic::EcnMark::N;
  PktTrigger::T trigger = PktTrigger::None;
  uint64_t	bytesInFlight = 0;
  uint8_t	frameCount = 0;
  bool		framesTruncated = false;
  QLogFrameArray frames;
  bool		ackEliciting = false;
};

struct QLogPacketDataJSON;
inline QLogPacketDataJSON ZtJSON_Fmt(QLogPacketData *);

struct QLogPacketEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::PktSent;
  QLogPacketData data;
  Zquic::LinkInfo linkInfo;
};

QLogEvtFmt(QLogPacketEvt);

ZuDerive(QLogPktNumArray,
  (ZuArray<uint64_t, AckPacketMax>));
inline ZtJSON::AsArray<ZtFieldTC::UInt64> ZtJSON_Fmt(
  QLogPktNumArray *);

struct QLogAckData {
	Zquic::PktNumSpace::T packetSpace = Zquic::PktNumSpace::Initial;
	QLogPktNumArray packetNumbers;
};

ZtStruct((QLogAckData, JSON),
  (((packetSpace), (JSON::ID<"packet_number_space">,
    Enum<Zquic::PktNumSpace::JSON>)),
      (Int8)),
  (((packetNumbers), (JSON::ID<"packet_numbers">)), (UDT)));

struct QLogAckEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::PktsAcked;
  QLogAckData data;
  Zquic::LinkInfo linkInfo;
};

QLogEvtFmt(QLogAckEvt);

struct PktLostTrigger {
  ZtEnum(PktLostTrigger, int8_t,
    ReorderThresh, TimeThreshold, PTOExpired);
  ZtEnumMap(PktLostTrigger, JSON,
    "reordering_threshold", "time_threshold", "pto_expired");
};

struct QLogPktLostData {
  QLogPacketHeader header;
  PktLostTrigger::T trigger =
    PktLostTrigger::ReorderThresh;
};

ZtStruct((QLogPktLostData, JSON),
  (((header)), (UDT)),
  (((trigger), (Enum<PktLostTrigger::JSON>)), (Int8)));

struct QLogPktLostEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::PktLost;
  QLogPktLostData data;
  Zquic::LinkInfo linkInfo;
};

QLogEvtFmt(QLogPktLostEvt);

struct QLogMarkRetransData {
  QLogFrameArray frames;
};

ZtStruct((QLogMarkRetransData, JSON),
  (((frames)), (UDT)));

struct QLogMarkRetransEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::MarkRetrans;
  QLogMarkRetransData data;
  Zquic::LinkInfo linkInfo;
};

QLogEvtFmt(QLogMarkRetransEvt);

struct QLogRecMetricsData {
  double	latestRTT = 0;
  double	smoothedRTT = 0;
  double	rttVariance = 0;
  double	minRTT = 0;
  uint64_t	congestionWindow = 0;
  uint64_t	ssthresh = 0;
  uint64_t	bytesInFlight = 0;
};

ZtStruct((QLogRecMetricsData, JSON),
  (((latestRTT), (JSON::ID<"latest_rtt">)), (Float)),
  (((smoothedRTT), (JSON::ID<"smoothed_rtt">)), (Float)),
  (((rttVariance), (JSON::ID<"rtt_variance">)), (Float)),
  (((minRTT), (JSON::ID<"min_rtt">)), (Float)),
  (((congestionWindow), (JSON::ID<"congestion_window">)), (UInt64)),
  (((ssthresh)), (UInt64)),
  (((bytesInFlight), (JSON::ID<"bytes_in_flight">)), (UInt64)));

struct QLogRecMetricsEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::MetricsUpd;
  QLogRecMetricsData data;
  Zquic::LinkInfo linkInfo;
};

QLogEvtFmt(QLogRecMetricsEvt);

struct CongState {
  ZtEnum(CongState, int8_t,
    SlowStart, CongAvoid, AppLimited, Recovery);
  ZtEnumMap(CongState, JSON,
    "slow_start", "congestion_avoidance", "application_limited",
    "recovery");
};

struct CongTrigger {
  ZtEnum(CongTrigger, int8_t,
    Ack, PMTUDAck, Loss, PMTUDLoss);
  ZtEnumMap(CongTrigger, JSON,
    "ack", "pmtud_ack", "loss", "pmtud_loss");
};

struct QLogCongStateData {
  CongState::T newState =
    CongState::CongAvoid;
  CongTrigger::T trigger = CongTrigger::Ack;
};

ZtStruct((QLogCongStateData, JSON),
  (((newState), (JSON::ID<"new">,
    Enum<CongState::JSON>)), (Int8)),
  (((trigger), (Enum<CongTrigger::JSON>)), (Int8)));

struct QLogCongStateEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::CongestionUpd;
  QLogCongStateData data;
  Zquic::LinkInfo linkInfo;
};

QLogEvtFmt(QLogCongStateEvt);

struct TimerType {
  ZtEnum(TimerType, int8_t,
    Loss, PTO);
  ZtEnumMap(TimerType, JSON,
    "loss_timeout", "pto");
};

struct TimerEvt {
  ZtEnum(TimerEvt, int8_t,
    Set, Expired, Cancelled);
  ZtEnumMap(TimerEvt, JSON,
    "set", "expired", "cancelled");
};

struct QLogTimerData {
  TimerType::T timerType = TimerType::Loss;
  Zquic::PktNumSpace::T packetSpace = Zquic::PktNumSpace::Initial;
  TimerEvt::T eventType = TimerEvt::Set;
  double	delta = 0;
};

ZtStruct((QLogTimerData, JSON),
  (((timerType), (JSON::ID<"timer_type">,
    Enum<TimerType::JSON>)), (Int8)),
  (((packetSpace), (JSON::ID<"packet_number_space">,
    Enum<Zquic::PktNumSpace::JSON>)), (Int8)),
  (((eventType), (JSON::ID<"event_type">,
    Enum<TimerEvt::JSON>)), (Int8)),
  (((delta)), (Float)));

struct QLogTimerEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::TimerUpd;
  QLogTimerData data;
  Zquic::LinkInfo linkInfo;
};

QLogEvtFmt(QLogTimerEvt);

struct QLogECNData {
  ECNState::T oldState = ECNState::Unknown;
  ECNState::T newState = ECNState::Unknown;
};

ZtStruct((QLogECNData, JSON),
  (((oldState), (JSON::ID<"old">, Enum<ECNState::JSON>)), (Int8)),
  (((newState), (JSON::ID<"new">, Enum<ECNState::JSON>)), (Int8)));

struct QLogECNEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::ECNUpd;
  QLogECNData data;
  Zquic::LinkInfo linkInfo;
};

QLogEvtFmt(QLogECNEvt);

struct QLogSecData {
	SecKind::T kind = SecKind::TLS;
	Zquic::PktNumSpace::T packetSpace = Zquic::PktNumSpace::Initial;
  SecKeyType::T keyType = SecKeyType::None;
  SecTrigger::T trigger = SecTrigger::None;
  ZeString	alpn;
  SecReason::T reason = SecReason::None;
  uint64_t	value = 0;
  bool		success = true;
};

ZtStruct((QLogSecData, JSON),
  (((kind), (Enum<SecKind::JSON>)), (Int8)),
  (((packetSpace), (JSON::ID<"packet_number_space">,
    Enum<Zquic::PktNumSpace::JSON>)), (Int8)),
  (((keyType), (JSON::ID<"key_type">, JSON::Opt, Enum<SecKeyType::JSON>)),
      (Int8)),
  (((trigger), (JSON::Opt, Enum<SecTrigger::JSON>)), (Int8)),
  (((alpn), (JSON::Opt)), (String)),
  (((reason), (JSON::Opt, Enum<SecReason::JSON>)), (Int8)),
  (((value)), (UInt64)),
  (((success)), (Bool)));

struct QLogSecEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::TLSAlert;
  QLogSecData data;
  Zquic::LinkInfo linkInfo;
};

QLogEvtFmt(QLogSecEvt);

static constexpr uint64_t QLogKeyPhaseNull = ~uint64_t{0};

struct QLogKeyData {
  KeyType::T keyType = KeyType::Client1RTT;
  uint64_t	keyPhase = QLogKeyPhaseNull;
  KeyTrigger::T trigger = KeyTrigger::TLS;
};

struct QLogKeyDataJSON;
inline QLogKeyDataJSON ZtJSON_Fmt(QLogKeyData *);

struct QLogKeyEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::KeyUpd;
  QLogKeyData data;
  Zquic::LinkInfo linkInfo;
};

QLogEvtFmt(QLogKeyEvt);

struct QLogParamsData {
  Initiator::T initiator = Initiator::Local;
  ZuBSpan	origDCID;
  ZuBSpan	initialSCID;
  ZuBSpan	retrySCID;
  ZuBSpan	statelessResetToken;
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
  bool		disableActiveMigration = false;
};

struct QLogParamsDataJSON;
inline QLogParamsDataJSON ZtJSON_Fmt(QLogParamsData *);

struct QLogParamsEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::ParamsSet;
  QLogParamsData data;
  Zquic::LinkInfo linkInfo;
};

QLogEvtFmt(QLogParamsEvt);

struct QLogALPNID {
  ZeString	stringValue;
};

ZtStruct((QLogALPNID, JSON),
  (((stringValue), (JSON::ID<"string_value">)), (String)));

struct QLogALPNData {
  QLogALPNID chosenALPN;
};

ZtStruct((QLogALPNData, JSON),
  (((chosenALPN), (JSON::ID<"chosen_alpn">)), (UDT)));

struct QLogALPNEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::ALPNInfo;
  QLogALPNData data;
  Zquic::LinkInfo linkInfo;
};

QLogEvtFmt(QLogALPNEvt);

struct QLogVersionData {
  Versions	serverVersions;
  Versions	clientVersions;
  uint32_t	chosenVersion = 0;
  bool		chosenVersionPresent = false;
};

struct QLogVersionDataJSON;
inline QLogVersionDataJSON ZtJSON_Fmt(QLogVersionData *);

struct QLogVersionEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::VersionInfo;
  QLogVersionData data;
  Zquic::LinkInfo linkInfo;
};

QLogEvtFmt(QLogVersionEvt);

struct QLogTupleAssignedData {
  uint64_t	tupleID = 0;
  uint64_t	attemptID = U64Null;
};

struct QLogTupleAssignedDataJSON;
inline QLogTupleAssignedDataJSON ZtJSON_Fmt(QLogTupleAssignedData *);

struct QLogPathEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::TupleAssigned;
  QLogTupleAssignedData data;
  Zquic::LinkInfo linkInfo;
};

QLogEvtFmt(QLogPathEvt);

struct QLogMTUData {
  uint32_t	newMTU = 0;
  uint64_t	attemptID = U64Null;
  bool		done = false;
};

ZtStruct((QLogMTUData, JSON),
  (((newMTU), (JSON::ID<"new">)), (UInt32)),
  (((attemptID), (JSON::ID<"attempt_id">, JSON::Opt)), (UInt64)),
  (((done)), (Bool)));

struct QLogMTUEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::MTUUpd;
  QLogMTUData data;
  Zquic::LinkInfo linkInfo;
};

QLogEvtFmt(QLogMTUEvt);

struct QLogPathValidData {
  bool		success = false;
  uint64_t	attemptID = U64Null;
  Zquic::Vantage::T vantage = Zquic::Vantage::Unknown;
};

ZtStruct((QLogPathValidData, JSON),
  (((success)), (Bool)),
  (((attemptID), (JSON::ID<"attempt_id">, JSON::Opt)), (UInt64)),
  (((vantage), (JSON::ID<"vantage">,
    Enum<Zquic::Vantage::JSON>)), (Int8)));

struct QLogPathValidatedEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::PathValidated;
  QLogPathValidData data;
  Zquic::LinkInfo linkInfo;
};

QLogEvtFmt(QLogPathValidatedEvt);

struct QLogCIDData {
  Initiator::T initiator = Initiator::Remote;
  ZuBSpan	oldCID;
  ZuBSpan	newCID;
  uint64_t	attemptID = U64Null;
};

struct QLogCIDDataJSON;
inline QLogCIDDataJSON ZtJSON_Fmt(QLogCIDData *);

struct QLogCIDEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::CIDUpd;
  QLogCIDData data;
  Zquic::LinkInfo linkInfo;
};

QLogEvtFmt(QLogCIDEvt);

struct QLogMigrationData {
  QLogEndpointInfo	activeLocal;
  QLogEndpointInfo	activeRemote;
  QLogEndpointInfo	candidateLocal;
  QLogEndpointInfo	candidateRemote;
  uint64_t		attemptID = 0;
  uint64_t		peerCIDSeq = U64Null;
  uint64_t		deadlineUS = 0;
  uint32_t		mtu = 0;
  MigrationAction::T	action = MigrationAction::Requested;
  Zquic::MigrationReason::T reason = Zquic::MigrationReason::None;
  Zquic::MigrationState::T state = Zquic::MigrationState::Idle;
  Zquic::PathRole::T	pathRole = Zquic::PathRole::Candidate;
  bool			localRebind = false;
  bool			validated = false;
  bool			closeOnFailure = false;
};

ZtStruct((QLogMigrationData, JSON),
  (((activeLocal), (JSON::ID<"active_local">)), (UDT)),
  (((activeRemote), (JSON::ID<"active_remote">)), (UDT)),
  (((candidateLocal), (JSON::ID<"candidate_local">)), (UDT)),
  (((candidateRemote), (JSON::ID<"candidate_remote">)), (UDT)),
  (((attemptID), (JSON::ID<"attempt_id">)), (UInt64)),
  (((peerCIDSeq), (JSON::ID<"peer_cid_sequence">)), (UInt64)),
  (((deadlineUS), (JSON::ID<"deadline_us">)), (UInt64)),
  (((mtu)), (UInt32)),
  (((action), (Enum<MigrationAction::JSON>)), (Int8)),
  (((reason), (Enum<Zquic::MigrationReason::JSON>)), (Int8)),
  (((state), (Enum<Zquic::MigrationState::JSON>)), (Int8)),
  (((pathRole), (JSON::ID<"path_role">,
    Enum<Zquic::PathRole::JSON>)), (Int8)),
  (((localRebind), (JSON::ID<"local_rebind">)), (Bool)),
  (((validated)), (Bool)),
  (((closeOnFailure), (JSON::ID<"close_on_failure">)), (Bool)));

struct QLogMigrationEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::MigrationUpd;
  QLogMigrationData data;
  Zquic::LinkInfo linkInfo;
};

QLogEvtFmt(QLogMigrationEvt);

struct QLogStreamData {
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
};

ZtStruct((QLogStreamData, JSON),
  (((streamType), (JSON::ID<"stream_type">,
    Enum<StreamType::JSON>)), (Int8)),
  (((oldState), (JSON::ID<"old">,
    Enum<StreamState::JSON>)), (Int8)),
  (((newState), (JSON::ID<"new">,
    Enum<StreamState::JSON>)), (Int8)),
  (((streamSide), (JSON::ID<"stream_side">,
    Enum<StreamSide::JSON>)), (Int8)),
  (((reason), (JSON::Opt, Enum<StreamReason::JSON>)), (Int8)),
  (((streamID), (JSON::ID<"stream_id">)), (UInt64)),
  (((offset)), (UInt64)),
  (((length)), (UInt64)),
  (((errorCode), (JSON::ID<"error_code">)), (UInt64)),
  (((fin)), (Bool)));

struct QLogStreamEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::StreamStateUpd;
  QLogStreamData data;
  Zquic::LinkInfo linkInfo;
};

QLogEvtFmt(QLogStreamEvt);

struct QLogStreamMovedData {
  uint64_t	streamID = 0;
  uint64_t	offset = 0;
  StreamDataLoc::T from = StreamDataLoc::Transport;
  StreamDataLoc::T to = StreamDataLoc::Application;
  StreamDataInfo::T additionalInfo = StreamDataInfo::None;
  QLogRawInfo	raw;
};

ZtStruct((QLogStreamMovedData, JSON),
  (((streamID), (JSON::ID<"stream_id">)), (UInt64)),
  (((offset)), (UInt64)),
  (((from), (Enum<StreamDataLoc::JSON>)), (Int8)),
  (((to), (Enum<StreamDataLoc::JSON>)), (Int8)),
  (((additionalInfo), (JSON::ID<"additional_info">,
    JSON::Opt, Enum<StreamDataInfo::JSON>)), (Int8)),
  (((raw)), (UDT)));

struct QLogStreamMovedEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::StreamDataMoved;
  QLogStreamMovedData data;
  Zquic::LinkInfo linkInfo;
};

QLogEvtFmt(QLogStreamMovedEvt);

struct QLogCxnBlockedData {
  BlockedState::T oldState = BlockedState::Unblocked;
  BlockedState::T newState = BlockedState::Blocked;
  BlockedReason::T reason =
    BlockedReason::CxnFlowCtrl;
};

ZtStruct((QLogCxnBlockedData, JSON),
  (((oldState), (JSON::ID<"old">, Enum<BlockedState::JSON>)), (Int8)),
  (((newState), (JSON::ID<"new">, Enum<BlockedState::JSON>)), (Int8)),
  (((reason), (Enum<BlockedReason::JSON>)), (Int8)));

struct QLogCxnBlockedEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::CxnDataBlockedUpd;
  QLogCxnBlockedData data;
  Zquic::LinkInfo linkInfo;
};

QLogEvtFmt(QLogCxnBlockedEvt);

struct QLogStreamBlockedData {
  BlockedState::T oldState = BlockedState::Unblocked;
  BlockedState::T newState = BlockedState::Blocked;
  uint64_t	streamID = 0;
  BlockedReason::T reason =
    BlockedReason::StreamFlowCtrl;
};

ZtStruct((QLogStreamBlockedData, JSON),
  (((oldState), (JSON::ID<"old">, Enum<BlockedState::JSON>)), (Int8)),
  (((newState), (JSON::ID<"new">, Enum<BlockedState::JSON>)), (Int8)),
  (((streamID), (JSON::ID<"stream_id">)), (UInt64)),
  (((reason), (Enum<BlockedReason::JSON>)), (Int8)));

struct QLogStreamBlockedEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::StreamDataBlockedUpd;
  QLogStreamBlockedData data;
  Zquic::LinkInfo linkInfo;
};

QLogEvtFmt(QLogStreamBlockedEvt);

struct QLogCloseData {
	CloseInitiator::T initiator = CloseInitiator::Local;
	CloseTrigger::T trigger = CloseTrigger::Error;
  CloseReason::T reason = CloseReason::None;
  CloseError::T connectionError = CloseError::None;
  CloseError::T applicationError = CloseError::None;
  uint64_t	errorCode = 0;
};

struct QLogCloseDataJSON;
inline QLogCloseDataJSON ZtJSON_Fmt(QLogCloseData *);

struct QLogCloseEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::CxnClosed;
  QLogCloseData data;
  Zquic::LinkInfo linkInfo;
};

QLogEvtFmt(QLogCloseEvt);

static uint64_t qlogTime_(ZuTime time)
{
  return uint64_t(time.microsecs());
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
  m_path = {};
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
  if (!m_ring.closed()) {
    if (m_thread) {
      m_ring.eof(true);
      // protect against blocking on self-destruction, do NOT use ZmSelf()
      // - ZmSelf() depends on TLS and should not be called during exit
      if (m_thread.tid() != Zm::getTID()) m_thread.join();
    }
    m_ring.close();
  }
}

ZquicLogger *ZquicLogger::instance()
{
  static constexpr auto ctor = []() { return new ZquicLogger(); };
  return
    ZmSingleton<ZquicLogger,
      ZmSingletonCtor<ctor,
	ZmSingletonCleanup<ZmCleanup::Library>>>::instance();
}

bool ZquicLogger::init_(
  Trace &trace, const ZquicLogParams &params, Zquic::Vantage::T vantage)
{
  Guard guard(m_lock);
  if (!params.enabled()) return true;
  if (!m_configured) {
    m_params = params;
    m_recordsEnqueued.store_(0);
    m_recordsWritten.store_(0);
    m_recordsDropped.store_(0);
    m_ringBackPressure.store_(0);
    m_writerFailures.store_(0);
    m_bytesWritten.store_(0);
  }
  if (!trace.configured) ++m_configured;
  trace.sink.final();
  trace.configured = true;
  trace.sinkOpened = false;
  trace.headerWritten = false;
  trace.vantage = vantage;
  trace.params = params;
  return true;
}

void ZquicLogger::start_()
{
  Guard guard(m_lock);
  if (!m_configured) return;
  if (m_thread) return;
  m_ring.init(ZmRingParams{m_params.ringSize()});
  if (m_ring.open(Ring::Read | Ring::Write) != Zu::OK) {
    ++m_writerFailures;
    return;
  }
  m_thread = ZmThread{[this]() { work_(); },
    ZmThreadParams().name("zquic-qlog").priority(ZmThreadPriority::Low)};
}

void ZquicLogger::stop_(bool idleOnly)
{
  ZmThread thread;
  {
    Guard guard(m_lock);
    if (idleOnly && m_configured) return;
    thread = ZuMv(m_thread);
    m_thread = {};
  }
  if (!thread) return;
  while (m_queueCount.load_()) Zm::yield();
  m_ring.eof(true);
  thread.join();		// wait for ring buffer to drain
  m_ring.close();
  m_activeTrace = nullptr;
}

void ZquicLogger::final_(Trace &trace)
{
  closeTrace_(trace);
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

void ZquicLogger::cxnStarted_(Trace &trace, CxnStartedEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    if (!this_->useTrace_(trace)) return;
    this_->writeCxnStarted_(event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZquicLogger::dgramSent_(Trace &trace, DgramEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    if (!this_->useTrace_(trace)) return;
    this_->writeDatagramEvt_(EvtName::UDPTx, event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZquicLogger::dgramRecv_(Trace &trace, DgramEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    if (!this_->useTrace_(trace)) return;
    this_->writeDatagramEvt_(EvtName::UDPRx, event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZquicLogger::pktSent_(Trace &trace, PktEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    if (!this_->useTrace_(trace)) return;
    this_->writePktEvt_(EvtName::PktSent, event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZquicLogger::pktRecv_(Trace &trace, PktEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    if (!this_->useTrace_(trace)) return;
    this_->writePktEvt_(EvtName::PktRecv, event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZquicLogger::pktBuf_(Trace &trace, PktEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    if (!this_->useTrace_(trace)) return;
    this_->writePktEvt_(EvtName::PktBuf, event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZquicLogger::pktDrop_(Trace &trace, PktEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    if (!this_->useTrace_(trace)) return;
    this_->writePktEvt_(EvtName::PktDrop, event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZquicLogger::pktsAcked_(Trace &trace, AckEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    if (!this_->useTrace_(trace)) return;
    this_->writeAckEvt_(EvtName::PktsAcked, event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZquicLogger::pktLost_(Trace &trace, RecLogEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    if (!this_->useTrace_(trace)) return;
    this_->writePktLost_(event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZquicLogger::recPktLost_(Trace &trace, RecLogEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    if (!this_->useTrace_(trace)) return;
    this_->writePktLost_(event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZquicLogger::markRetrans_(Trace &trace, RecLogEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    if (!this_->useTrace_(trace)) return;
    this_->writeMarkRetrans_(event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZquicLogger::metricsUpd_(Trace &trace, RecLogEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    if (!this_->useTrace_(trace)) return;
    this_->writeRecMetrics_(event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZquicLogger::lossTimerUpd_(Trace &trace, RecLogEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    if (!this_->useTrace_(trace)) return;
    this_->writeTimerEvt_(event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZquicLogger::congStateUpd_(Trace &trace, RecLogEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    if (!this_->useTrace_(trace)) return;
    this_->writeCongState_(event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZquicLogger::ecnStateUpd_(Trace &trace, ECNEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    if (!this_->useTrace_(trace)) return;
    this_->writeECNEvt_(event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZquicLogger::keyUpdated_(Trace &trace, SecEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    if (!this_->useTrace_(trace)) return;
    this_->writeKeyEvt_(EvtName::KeyUpd, event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZquicLogger::keyRetired_(Trace &trace, SecEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    if (!this_->useTrace_(trace)) return;
    this_->writeKeyEvt_(EvtName::KeyDiscarded, event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZquicLogger::paramsSet_(Trace &trace, ParamsEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    if (!this_->useTrace_(trace)) return;
    this_->writeParams_(event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZquicLogger::alpnInfo_(Trace &trace, SecEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    if (!this_->useTrace_(trace)) return;
    this_->writeALPNEvt_(event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZquicLogger::versionInfo_(Trace &trace, VersionEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    if (!this_->useTrace_(trace)) return;
    this_->writeVersion_(event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZquicLogger::tlsAlert_(Trace &trace, SecEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    if (!this_->useTrace_(trace)) return;
    this_->writeSecEvt_(EvtName::TLSAlert, event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZquicLogger::secEvt_(
  Trace &trace, EvtName::T name, SecEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, name_ = name, event_ = ZuMv(event), time](
    ZquicLogger *this_) mutable {
    if (!this_->useTrace_(trace)) return;
    this_->writeSecEvt_(name_, event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZquicLogger::pathUpdated_(Trace &trace, PathEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    if (!this_->useTrace_(trace)) return;
    this_->writePathEvt_(EvtName::TupleAssigned, event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZquicLogger::pathValidUpd_(Trace &trace, PathEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    if (!this_->useTrace_(trace)) return;
    this_->writePathValid_(event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZquicLogger::pmtudUpdated_(Trace &trace, PathEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    if (!this_->useTrace_(trace)) return;
    this_->writeMTUEvt_(event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZquicLogger::cidUpdated_(Trace &trace, CIDEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    if (!this_->useTrace_(trace)) return;
    this_->writeCIDEvt_(EvtName::CIDUpd, event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZquicLogger::migrationUpdated_(Trace &trace, MigrationEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](
    ZquicLogger *this_) mutable {
    if (!this_->useTrace_(trace)) return;
    this_->writeMigrationEvt_(event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZquicLogger::streamStateUpd_(Trace &trace, StreamEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    if (!this_->useTrace_(trace)) return;
    this_->writeStreamEvt_(
      EvtName::StreamStateUpd, event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZquicLogger::streamDataMoved_(Trace &trace, StreamDataEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    if (!this_->useTrace_(trace)) return;
    this_->writeStreamData_(
      EvtName::StreamDataMoved, event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZquicLogger::cxnDataBlockedUpd_(Trace &trace,
  BlockedEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    if (!this_->useTrace_(trace)) return;
    this_->writeCxnBlocked_(
      EvtName::CxnDataBlockedUpd, event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZquicLogger::streamDataBlockedUpd_(Trace &trace,
  BlockedEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    if (!this_->useTrace_(trace)) return;
    this_->writeStreamBlocked_(
      EvtName::StreamDataBlockedUpd, event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZquicLogger::cxnClosed_(Trace &trace, CloseEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    if (!this_->useTrace_(trace)) return;
    this_->writeCloseEvt_(EvtName::CxnClosed, event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZquicLogger::cxnStateUpd_(Trace &trace,
  CxnStateEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZquicLogger *this_) mutable {
    if (!this_->useTrace_(trace)) return;
    this_->writeCxnState_(
      EvtName::CxnStateUpd, event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZquicLogger::work_()
{
  for (;;) {
    if (ZuLikely(!m_queueCount.load_())) goto shift;
    if (Fn fn = m_queue.shift()) {
      if (ZuLikely(tryPush_(fn)))
	--m_queueCount;
      else
	m_queue.unshift(ZuMv(fn));
    }
shift:
    if (void *ptr = m_ring.shift()) {
      m_activeTrace = nullptr;
      m_ring.shift2(Fn::invoke(ptr, this));
    } else {
      if (m_ring.readStatus() == Zu::EndOfFile) break;
    }
  }
  m_activeTrace = nullptr;
}

bool ZquicLogger::tryPush_(Fn &fn)
{
  unsigned size = fn.pushSize();
  if (void *ptr = m_ring.tryPush(size)) {
    fn.push(ptr);
    m_ring.push2(ptr, size);
    return true;
  }
  return false;
}

bool ZquicLogger::useTrace_(Trace *trace)
{
  if (!trace || !trace->configured) return false;
  if (!trace->sinkOpened) {
    if (!trace->sink.init(trace->params.path(), trace->params.age())) {
      ++m_writerFailures;
      return false;
    }
    trace->sinkOpened = true;
  }
  m_activeTrace = trace;
  return true;
}

void ZquicLogger::closeTrace_(Trace &trace)
{
  if (trace.sinkOpened)
    trace.sink.final();
  Guard guard(m_lock);
  if (trace.configured && m_configured) --m_configured;
  trace.configured = false;
  trace.sinkOpened = false;
  trace.headerWritten = false;
  trace.vantage = Zquic::Vantage::Unknown;
  trace.params = {};
  if (!m_configured) {
    m_params = {};
  }
}

bool ZquicLogger::write_(Trace *trace, ZuCSpan data)
{
  if (!useTrace_(trace)) return false;
  if (!writeHeader_(trace)) return false;
  if (!trace->sink.write(data)) {
    ++m_writerFailures;
    return false;
  }
  return true;
}

bool ZquicLogger::writeHeader_(Trace *trace)
{
  if (!trace || trace->headerWritten) return true;
  trace->headerWritten = true;
  QLogHeader header{
    "urn:ietf:params:qlog:file:sequential",
    "application/qlog+json-seq",
    "zquic",
    QLogImplementation{"zquic", Z_VERNAME},
    QLogTrace{
      QLogVantage{trace->vantage},
      QLogCommonFields{
	.timeFormat = "relative_to_epoch",
	.referenceTime = QLogReferenceTime{
	  .clockType = "system",
	  .epoch = "1970-01-01T00:00:00.000Z"
	}
      },
      QLogEvtSchemas{
	"urn:ietf:params:qlog:events:quic-12",
	"urn:zlib:zquic:qlog:events:zquic"
      }
    }
  };
  ZeLogBuf buf;
  buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(buf, header);
  buf << '\n';
  if (!trace->sink.write(buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + buf.length());
  return true;
}

struct QLogCxnStartedDataJSON {
  template <typename O, typename Facet>
  struct Handler {
    template <template <typename> class Filter, typename S>
    static void save(S &s, const QLogCxnStartedData &event)
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

bool ZquicLogger::writeCxnStarted_(
  const CxnStartedEvt &event, ZuTime time)
{
  QLogCxnStartedEvt qevent{
    qlogTime_(time),
    EvtName::CxnStarted,
    QLogCxnStartedData{
      event.local ?
	QLogEndpointInfo{true, event.local.ip(), event.local.port()} :
	QLogEndpointInfo{},
      event.remote ?
	QLogEndpointInfo{true, event.remote.ip(), event.remote.port()} :
	QLogEndpointInfo{}
    },
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeDatagramEvt_(
  EvtName::T name, const DgramEvt &event, ZuTime time)
{
  QLogRawInfoArray raw;
  new (raw.push()) QLogRawInfo{event.size, event.size};
  QLogECNArray ecn;
  new (ecn.push()) Zquic::EcnMark::T(event.ecn);
  QLogDatagramEvt qevent{
    qlogTime_(time),
    name,
    QLogDatagramData{1, ZuMv(raw), ZuMv(ecn)},
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
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
	ZuTypeList<ZuFieldProp::Enum<PktTrigger::JSON>>;

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
      if (packet.trigger != PktTrigger::None)
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
	ZuTypeList<ZuFieldProp::Enum<StreamType::JSON>>;
      using BytesHexProps = ZuTypeList<ZuFieldProp::JSON::Hex>;

      bool comma = false;
      s << '{';
      qlogJSONField_<Facet, Filter, ZtFieldTC::Int8, FrameTypeProps>(
	s, comma, "frame_type", frame.frameType);

      switch (frame.frameType) {
	case Zquic::FrameType::Ack:
	  if (!ZuNull(frame.ackDelay) && frame.ackDelay)
	    qlogJSONField_<Facet, Filter, ZtFieldTC::Float>(
	      s, comma, "ack_delay", frame.ackDelay);
	  if (frame.ackedRanges)
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
	case Zquic::FrameType::NewCxnID:
	  qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	    s, comma, "sequence_number", frame.sequenceNumber);
	  qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	    s, comma, "retire_prior_to", frame.retirePriorTo);
	  qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	    s, comma, "connection_id_length", frame.cxnIDLength);
	  if (frame.cxnID)
	    qlogJSONField_<Facet, Filter, ZtFieldTC::Bytes, BytesHexProps>(
	      s, comma, "connection_id", frame.cxnID);
	  if (frame.statelessResetToken)
	    qlogJSONField_<Facet, Filter, ZtFieldTC::Bytes, BytesHexProps>(
	      s, comma, "stateless_reset_token", frame.statelessResetToken);
	  break;
	case Zquic::FrameType::RetireCxnID:
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
	ZuTypeList<ZuFieldProp::Enum<KeyType::JSON>>;
      using TriggerProps =
	ZuTypeList<ZuFieldProp::Enum<KeyTrigger::JSON>>;

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

struct QLogParamsDataJSON {
  template <typename O, typename Facet>
  struct Handler {
    template <template <typename> class Filter, typename S>
    static void save(S &s, const QLogParamsData &params)
    {
      using InitiatorProps =
	ZuTypeList<ZuFieldProp::Enum<Initiator::JSON>>;
      using BytesHexProps = ZuTypeList<ZuFieldProp::JSON::Hex>;

      bool comma = false;
      s << '{';
      qlogJSONField_<Facet, Filter, ZtFieldTC::Int8, InitiatorProps>(
	s, comma, "initiator", params.initiator);
      if (params.origDCID)
	qlogJSONField_<Facet, Filter, ZtFieldTC::Bytes, BytesHexProps>(
	  s, comma, "original_destination_connection_id",
	  params.origDCID);
      if (params.initialSCID)
	qlogJSONField_<Facet, Filter, ZtFieldTC::Bytes, BytesHexProps>(
	  s, comma, "initial_source_connection_id", params.initialSCID);
      if (params.retrySCID)
	qlogJSONField_<Facet, Filter, ZtFieldTC::Bytes, BytesHexProps>(
	  s, comma, "retry_source_connection_id", params.retrySCID);
      if (params.statelessResetToken)
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
	params.activeCxnIDLimit);
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
static void qlogJSONVersionArray_(S &s, const Versions &versions)
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
      if (version.serverVersions) {
	qlogJSONSep_(s, comma);
	s << "\"server_versions\":";
	qlogJSONVersionArray_(s, version.serverVersions);
      }
      if (version.clientVersions) {
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
      if (tuple.attemptID != U64Null)
	s << ",\"attempt_id\":" << tuple.attemptID;
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
	ZuTypeList<ZuFieldProp::Enum<Initiator::JSON>>;
      using BytesHexProps = ZuTypeList<ZuFieldProp::JSON::Hex>;

      bool comma = false;
      s << '{';
      qlogJSONField_<Facet, Filter, ZtFieldTC::Int8, InitiatorProps>(
	s, comma, "initiator", cid.initiator);
      if (cid.oldCID)
	qlogJSONField_<Facet, Filter, ZtFieldTC::Bytes, BytesHexProps>(
	  s, comma, "old", cid.oldCID);
      if (cid.newCID)
	qlogJSONField_<Facet, Filter, ZtFieldTC::Bytes, BytesHexProps>(
	  s, comma, "new", cid.newCID);
      if (cid.attemptID != U64Null)
	qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	  s, comma, "attempt_id", cid.attemptID);
      s << '}';
    }
  };
};

struct QLogCloseDataJSON {
  template <typename O, typename Facet>
  struct Handler {
    template <template <typename> class Filter, typename S>
    static void save(S &s, const QLogCloseData &close)
    {
      using InitiatorProps =
	ZuTypeList<ZuFieldProp::Enum<CloseInitiator::JSON>>;
      using TriggerProps =
	ZuTypeList<ZuFieldProp::Enum<CloseTrigger::JSON>>;
      using ReasonProps =
	ZuTypeList<ZuFieldProp::Enum<CloseReason::JSON>>;
      using ErrorProps =
	ZuTypeList<ZuFieldProp::Enum<CloseError::JSON>>;

      CloseError::T connectionError = close.connectionError;
      if (connectionError == CloseError::Unknown &&
	  close.errorCode <= Zquic::TransportError::ProtViolation)
	connectionError = CloseError::T(close.errorCode);
      CloseError::T applicationError = close.applicationError;

      bool comma = false;
      s << '{';
      qlogJSONField_<Facet, Filter, ZtFieldTC::Int8, InitiatorProps>(
	s, comma, "initiator", close.initiator);
      if (connectionError != CloseError::None)
	qlogJSONField_<Facet, Filter, ZtFieldTC::Int8, ErrorProps>(
	  s, comma, "connection_error", connectionError);
      if (applicationError != CloseError::None)
	qlogJSONField_<Facet, Filter, ZtFieldTC::Int8, ErrorProps>(
	  s, comma, "application_error", applicationError);
      if ((connectionError == CloseError::Unknown ||
	  applicationError == CloseError::Unknown) &&
	  close.errorCode)
	qlogJSONField_<Facet, Filter, ZtFieldTC::UInt64>(
	  s, comma, "error_code", close.errorCode);
      if (close.reason != CloseReason::None)
	qlogJSONField_<Facet, Filter, ZtFieldTC::Int8, ReasonProps>(
	  s, comma, "reason", close.reason);
      qlogJSONField_<Facet, Filter, ZtFieldTC::Int8, TriggerProps>(
	s, comma, "trigger", close.trigger);
      s << '}';
    }
  };
};

static QLogFrameArray qlogFrames_(
  const ZuArray<FrameEvt, FrameMax> &frames_)
{
  QLogFrameArray frames;
  for (unsigned i = 0, n = frames_.length(); i < n; ++i) {
    const FrameEvt &frame = frames_[i];
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
      case Zquic::FrameType::NewCxnID:
	qframe.sequenceNumber = frame.offset;
	qframe.retirePriorTo = frame.value;
	qframe.cxnIDLength = frame.length;
	qframe.cxnID = frame.cxnID;
	qframe.statelessResetToken = frame.resetToken.bspan();
	break;
      case Zquic::FrameType::RetireCxnID:
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

static PktTrigger::T qlogPktTrigger_(
  EvtName::T name, PktEvt::Reason::T reason)
{
  switch (name) {
    case EvtName::PktBuf:
      switch (reason) {
	case PktEvt::Reason::MissingKeys:
	  return PktTrigger::KeysUnavailable;
	default:
	  return PktTrigger::None;
      }
    case EvtName::PktDrop:
      switch (reason) {
	case PktEvt::Reason::PrepareLong:
	case PktEvt::Reason::UnsupportedLongType:
	case PktEvt::Reason::DiscardedSpace:
	  return PktTrigger::Unsupported;
	case PktEvt::Reason::ParseLong:
	case PktEvt::Reason::PacketLength:
	case PktEvt::Reason::ParseShort:
	case PktEvt::Reason::BadKeyPhase:
	  return PktTrigger::Invalid;
	case PktEvt::Reason::MissingKeys:
	  return PktTrigger::KeyUnavailable;
		case PktEvt::Reason::Protection:
		  return PktTrigger::DecryptionFailure;
		case PktEvt::Reason::Duplicate:
		  return PktTrigger::Duplicate;
		case PktEvt::Reason::AfterOneRTT:
		case PktEvt::Reason::FramePolicy:
		  return PktTrigger::Invalid;
		case PktEvt::Reason::AntiAmp:
	case PktEvt::Reason::ProbeAdmit:
	case PktEvt::Reason::AppSend:
	  return PktTrigger::Rejected;
	default:
	  return PktTrigger::General;
      }
    default:
      return PktTrigger::None;
  }
}

bool ZquicLogger::writePktEvt_(
  EvtName::T name, const PktEvt &event, ZuTime time)
{
  QLogPacketEvt qevent{
    qlogTime_(time),
    name,
    QLogPacketData{
      QLogPacketHeader{event.packetType, event.packetNumber},
	      event.packetSpace,
	      QLogRawInfo{event.packetSize, event.payloadSize},
	      event.ecn,
	      qlogPktTrigger_(name, event.reason),
	      event.bytesInFlight,
      event.frameCount,
      event.framesTruncated,
      qlogFrames_(event.frames),
      event.ackEliciting
    },
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeAckEvt_(
  EvtName::T name, const AckEvt &event, ZuTime time)
{
  QLogPktNumArray packetNumbers;
  for (unsigned i = 0; i < event.packetNumbers.length(); ++i)
    new (packetNumbers.push()) uint64_t(event.packetNumbers[i]);
  if (!packetNumbers)
    return true;
  QLogAckEvt qevent{
    qlogTime_(time),
    name,
    QLogAckData{
      event.packetSpace,
      ZuMv(packetNumbers)
    },
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

static PktLostTrigger::T qlogLossTrigger_(
  RecReason::T reason)
{
  switch (reason) {
    case RecReason::TimeThreshold:
      return PktLostTrigger::TimeThreshold;
    case RecReason::Expired:
      return PktLostTrigger::PTOExpired;
    default:
      return PktLostTrigger::ReorderThresh;
  }
}

bool ZquicLogger::writePktLost_(
  const RecLogEvt &event, ZuTime time)
{
  QLogPktLostEvt qevent{
    qlogTime_(time),
    EvtName::PktLost,
    QLogPktLostData{
      QLogPacketHeader{
	Zquic::pktTypeFromSpace(event.packetSpace),
	event.packetNumber
      },
      qlogLossTrigger_(event.reason)
    },
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeMarkRetrans_(
  const RecLogEvt &event, ZuTime time)
{
  if (!event.frames) return true;
  QLogMarkRetransEvt qevent{
    qlogTime_(time),
    EvtName::MarkRetrans,
    QLogMarkRetransData{
      qlogFrames_(event.frames)
    },
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeRecMetrics_(
  const RecLogEvt &event, ZuTime time)
{
  QLogRecMetricsEvt qevent{
    qlogTime_(time),
    EvtName::MetricsUpd,
    QLogRecMetricsData{
      qlogMS_(event.latestRTTUS),
      qlogMS_(event.smoothedRTTUS),
      qlogMS_(event.rttVarianceUS),
      qlogMS_(event.minRTTUS),
      event.cwnd,
      event.ssthresh,
      event.bytesInFlight
    },
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

static CongState::T qlogCongState_(
  const RecLogEvt &event)
{
  switch (event.reason) {
    case RecReason::Loss:
    case RecReason::PMTUDLoss:
      return CongState::Recovery;
    default:
      return event.cwnd < event.ssthresh ?
	CongState::SlowStart :
	CongState::CongAvoid;
  }
}

static CongTrigger::T qlogCongTrigger_(
  RecReason::T reason)
{
  switch (reason) {
    case RecReason::PMTUDAck:
      return CongTrigger::PMTUDAck;
    case RecReason::Loss:
      return CongTrigger::Loss;
    case RecReason::PMTUDLoss:
      return CongTrigger::PMTUDLoss;
    default:
      return CongTrigger::Ack;
  }
}

bool ZquicLogger::writeCongState_(
  const RecLogEvt &event, ZuTime time)
{
  QLogCongStateEvt qevent{
    qlogTime_(time),
    EvtName::CongestionUpd,
    QLogCongStateData{
      qlogCongState_(event),
      qlogCongTrigger_(event.reason)
    },
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

static TimerType::T qlogTimerType_(
  RecKind::T kind)
{
  switch (kind) {
    case RecKind::PTO:
      return TimerType::PTO;
    default:
      return TimerType::Loss;
  }
}

static TimerEvt::T qlogTimerEvtType_(
  RecReason::T reason)
{
  switch (reason) {
    case RecReason::Armed:
      return TimerEvt::Set;
    case RecReason::Expired:
    case RecReason::Backoff:
    case RecReason::Probe:
      return TimerEvt::Expired;
    default:
      return TimerEvt::Cancelled;
  }
}

static double qlogTimerDelta_(
  const RecLogEvt &event, ZuTime time)
{
  if (event.reason != RecReason::Armed) return 0;
  uint64_t now = qlogTime_(time);
  if (event.deadlineUS <= now) return 0;
  return qlogMS_(event.deadlineUS - now);
}

bool ZquicLogger::writeTimerEvt_(
  const RecLogEvt &event, ZuTime time)
{
  QLogTimerEvt qevent{
    qlogTime_(time),
    EvtName::TimerUpd,
    QLogTimerData{
      qlogTimerType_(event.kind),
      event.packetSpace,
      qlogTimerEvtType_(event.reason),
      qlogTimerDelta_(event, time)
    },
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeECNEvt_(
  const ECNEvt &event, ZuTime time)
{
  QLogECNEvt qevent{
    qlogTime_(time),
    EvtName::ECNUpd,
    QLogECNData{
      event.oldState,
      event.state
    },
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

static KeyType::T qlogKeyType_(
  const SecEvt &event, Zquic::Vantage::T vantage)
{
  bool localClient = vantage != Zquic::Vantage::Server;
  bool client =
    localClient ?
      event.keyType == SecKeyType::TX :
      event.keyType != SecKeyType::TX;

  switch (event.packetSpace) {
    case Zquic::PktNumSpace::Initial:
      return client ?
	KeyType::ClientInit :
	KeyType::ServerInit;
    case Zquic::PktNumSpace::Handshake:
      return client ?
	KeyType::ClientHS :
	KeyType::ServerHS;
    default:
      if (event.keyLevel == Zquic::PktKeyLevel::ZeroRTT)
	return client ?
	  KeyType::Client0RTT :
	  KeyType::Server0RTT;
      return client ?
	KeyType::Client1RTT :
	KeyType::Server1RTT;
  }
}

static KeyTrigger::T qlogKeyTrigger_(
  const SecEvt &event)
{
  switch (event.trigger) {
    case SecTrigger::Local:
      return KeyTrigger::LocalUpdate;
    case SecTrigger::Remote:
    case SecTrigger::Peer:
    case SecTrigger::Timer:
      return KeyTrigger::RemoteUpdate;
    default:
      return KeyTrigger::TLS;
  }
}

bool ZquicLogger::writeKeyEvt_(
  EvtName::T name, const SecEvt &event, ZuTime time)
{
  QLogKeyEvt qevent{
    qlogTime_(time),
    name,
    QLogKeyData{
      qlogKeyType_(event, event.linkInfo.vantage),
      event.packetSpace == Zquic::PktNumSpace::AppData &&
	  event.keyLevel != Zquic::PktKeyLevel::ZeroRTT ?
	event.value : QLogKeyPhaseNull,
      qlogKeyTrigger_(event)
    },
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeParams_(
  const ParamsEvt &event, ZuTime time)
{
  QLogParamsEvt qevent{
    qlogTime_(time),
    EvtName::ParamsSet,
    QLogParamsData{
      event.initiator,
      event.origDCID,
      event.initialSCID,
      event.retrySCID,
      event.statelessResetTokenPresent ?
	event.statelessResetToken.bspan() : ZuBSpan{},
      event.maxIdleTimeout,
      event.maxUDPPayloadSize,
      event.ackDelayExponent,
      event.maxAckDelay,
      event.activeCxnIDLimit,
      event.initialMaxData,
      event.initialMaxStreamDataBidiLocal,
      event.initialMaxStreamDataBidiRemote,
      event.initialMaxStreamDataUni,
      event.initialMaxStreamsBidi,
      event.initialMaxStreamsUni,
      event.disableActiveMigration
    },
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeALPNEvt_(
  const SecEvt &event, ZuTime time)
{
  QLogALPNEvt qevent{
    qlogTime_(time),
    EvtName::ALPNInfo,
    QLogALPNData{QLogALPNID{event.alpn}},
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeVersion_(
  const VersionEvt &event, ZuTime time)
{
  QLogVersionEvt qevent{
    qlogTime_(time),
    EvtName::VersionInfo,
    QLogVersionData{
      event.serverVersions,
      event.clientVersions,
      event.chosenVersion,
      event.chosenVersionPresent
    },
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeSecEvt_(
  EvtName::T name, const SecEvt &event, ZuTime time)
{
  QLogSecEvt qevent{
    qlogTime_(time),
    name,
    QLogSecData{
      event.kind,
      event.packetSpace,
      event.keyType,
      event.trigger,
      event.alpn,
      event.reason,
      event.value,
      event.success
    },
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writePathEvt_(
  EvtName::T name, const PathEvt &event, ZuTime time)
{
  QLogPathEvt qevent{
    qlogTime_(time),
    name,
    QLogTupleAssignedData{event.tupleID, event.attemptID},
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeMTUEvt_(
  const PathEvt &event, ZuTime time)
{
  QLogMTUEvt qevent{
    qlogTime_(time),
    EvtName::MTUUpd,
    QLogMTUData{
      event.mtu,
      event.attemptID,
      event.validated
    },
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writePathValid_(
  const PathEvt &event, ZuTime time)
{
  bool success = event.validated &&
    event.action != PathAction::Failed &&
    event.action != PathAction::Expired;
  QLogPathValidatedEvt qevent{
    qlogTime_(time),
    EvtName::PathValidated,
    QLogPathValidData{
      success,
      event.attemptID,
      event.linkInfo.vantage
    },
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeCIDEvt_(
  EvtName::T name, const CIDEvt &event, ZuTime time)
{
  bool retired =
    event.action == CIDAction::Retired ||
    event.action == CIDAction::Tombstone;
  auto cid = event.cxnID.cspan();
  QLogCIDEvt qevent{
    qlogTime_(time),
    name,
    QLogCIDData{
      Initiator::T(
	event.local ? Initiator::Local : Initiator::Remote),
      retired ? cid : ZuBSpan{},
      retired ? ZuBSpan{} : cid,
      event.attemptID
    },
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeMigrationEvt_(
  const MigrationEvt &event, ZuTime time)
{
  QLogMigrationEvt qevent{
    qlogTime_(time),
    EvtName::MigrationUpd,
    QLogMigrationData{
      event.activeLocal ?
	QLogEndpointInfo{
	  true, event.activeLocal.ip(), event.activeLocal.port()} :
	QLogEndpointInfo{},
      event.activeRemote ?
	QLogEndpointInfo{
	  true, event.activeRemote.ip(), event.activeRemote.port()} :
	QLogEndpointInfo{},
      event.candidateLocal ?
	QLogEndpointInfo{
	  true, event.candidateLocal.ip(), event.candidateLocal.port()} :
	QLogEndpointInfo{},
      event.candidateRemote ?
	QLogEndpointInfo{
	  true, event.candidateRemote.ip(), event.candidateRemote.port()} :
	QLogEndpointInfo{},
      event.attemptID,
      event.peerCIDSeq,
      event.deadlineUS,
      event.mtu,
      event.action,
      event.reason,
      event.state,
      event.pathRole,
      event.localRebind,
      event.validated,
      event.closeOnFailure
    },
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeStreamEvt_(
  EvtName::T name, const StreamEvt &event, ZuTime time)
{
  QLogStreamEvt qevent{
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
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeStreamData_(
  EvtName::T name, const StreamDataEvt &event, ZuTime time)
{
  QLogStreamMovedEvt qevent{
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
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeCxnBlocked_(
  EvtName::T name, const BlockedEvt &event, ZuTime time)
{
  QLogCxnBlockedEvt qevent{
    qlogTime_(time),
    name,
    QLogCxnBlockedData{
      event.oldState,
      event.newState,
      event.reason
    },
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeStreamBlocked_(
  EvtName::T name, const BlockedEvt &event, ZuTime time)
{
  QLogStreamBlockedEvt qevent{
    qlogTime_(time),
    name,
    QLogStreamBlockedData{
      event.oldState,
      event.newState,
      event.streamID,
      event.reason
    },
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeCloseEvt_(
  EvtName::T name, const CloseEvt &event, ZuTime time)
{
  QLogCloseEvt qevent{
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
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZquicLogger::writeCxnState_(
  EvtName::T name,
  const CxnStateEvt &event, ZuTime time)
{
  QLogCxnStateEvt qevent{
    qlogTime_(time),
    name,
    QLogCxnStateData{
      event.oldState,
      event.newState
    },
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZtJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

#undef QLogEvtFmt

#endif /* Zquic_DEBUG */
