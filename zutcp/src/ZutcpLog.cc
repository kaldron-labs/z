//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC qlog

#include <zlib/ZmSingleton.hh>

#include <zlib/ZfJSON.hh>
#include <zlib/ZtBuiltin.hh>

#include <zlib/Zutcp.hh>

#ifdef Zutcp_DEBUG

namespace ZutcpLog_ {

#define ZutcpLogEnumImpl(ID) \
  ZtEnumImplStruct(ID); \
  ZtEnumImplStruct(ID, JSON)

ZutcpLogEnumImpl(RecKind);
ZutcpLogEnumImpl(RecReason);
ZutcpLogEnumImpl(ECNState);
ZutcpLogEnumImpl(ECNReason);
ZutcpLogEnumImpl(Initiator);
ZutcpLogEnumImpl(PathKind);
ZutcpLogEnumImpl(PathAction);
ZutcpLogEnumImpl(PathReason);
ZutcpLogEnumImpl(CIDKind);
ZutcpLogEnumImpl(CIDAction);
ZutcpLogEnumImpl(CIDReason);
ZutcpLogEnumImpl(BlockedState);
ZutcpLogEnumImpl(BlockedReason);
ZutcpLogEnumImpl(CloseInitiator);
ZutcpLogEnumImpl(CloseTrigger);
ZutcpLogEnumImpl(CloseReason);
ZutcpLogEnumImpl(CloseError);
ZutcpLogEnumImpl(EvtName);

#undef ZutcpLogEnumImpl

ZtEnumImplStruct(Reason);
ZtEnumImplStruct(Reason, JSON);

} // namespace ZutcpLog_

using namespace ZutcpLog_;

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props = ZuTypeList<>,
  typename S, typename T>
static void qlogJSONField(S &, bool &, ZuCSpan, const T &);

struct QLogVantage {
  Zutcp::Vantage::T	type = Zutcp::Vantage::Unknown;
};

ZfStruct(, (QLogVantage, JSON),
  (type, (Enum<Zutcp::Vantage::JSON>),		Int8));

struct QLogImplementation {
  ZuCSpan	name;
  ZuCSpan	version;
};

ZfStruct(, (QLogImplementation, JSON),
  (name,,	String),
  (version,,	String));

struct QLogReferenceTime {
  ZuCSpan	clockType;
  ZuCSpan	epoch;
};

ZfStruct(, (QLogReferenceTime, JSON),
  (clockType, (JSON::ID<"clock_type">),		String),
  (epoch,,					String));

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
	qlogJSONField<Facet, Filter, ZfFieldTC::Bytes, BytesHexProps>(
	  s, comma, "ODCID", fields.origDCID);
      if (fields.groupID)
	qlogJSONField<Facet, Filter, ZfFieldTC::Bytes, BytesHexProps>(
	  s, comma, "group_id", fields.groupID);
      if (fields.dcid)
	qlogJSONField<Facet, Filter, ZfFieldTC::Bytes, BytesHexProps>(
	  s, comma, "DCID", fields.dcid);
      if (fields.scid)
	qlogJSONField<Facet, Filter, ZfFieldTC::Bytes, BytesHexProps>(
	  s, comma, "SCID", fields.scid);
      qlogJSONField<Facet, Filter, ZfFieldTC::String>(
	s, comma, "time_format", fields.timeFormat);
      qlogJSONField<Facet, Filter, ZfFieldTC::UDT>(
	s, comma, "reference_time", fields.referenceTime);
      s << '}';
    }
  };
};

inline QLogCommonFieldsJSON ZfJSON_Fmt(QLogCommonFields *);

namespace Zutcp {
ZfStruct(, (LinkInfo, JSON),
  (origDCID, (JSON::ID<"ODCID">, JSON::Hex, JSON::Opt),		Bytes),
  (groupID, (JSON::ID<"group_id">, JSON::Hex, JSON::Opt),	Bytes),
  (dcid, (JSON::ID<"DCID">, JSON::Hex, JSON::Opt),		Bytes),
  (scid, (JSON::ID<"SCID">, JSON::Hex, JSON::Opt),		Bytes));
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
      qlogJSONField<Facet, Filter, ZfFieldTC::UInt64>(
	s, comma, "time", event.time);
      qlogJSONField<Facet, Filter, ZfFieldTC::Int8, NameProps>(
	s, comma, "name", event.name);
      qlogJSONField<Facet, Filter, ZfFieldTC::UDT>(
	s, comma, "data", event.data);
      if (event.linkInfo)
	qlogJSONField<Facet, Filter, ZfFieldTC::UDT>(
	  s, comma, "common_fields", event.linkInfo);
      s << '}';
    }
  };
};

#define QLogEvtFmt(Type) inline QLogEvtJSON<Type> ZfJSON_Fmt(Type *)

ZuDerive(QLogEvtSchemas,
  (ZuArray<ZuCSpan, 2>));
inline ZfJSON::AsArray<ZfFieldTC::String> ZfJSON_Fmt(QLogEvtSchemas *);

struct QLogTrace {
  QLogVantage	vantage;
  QLogCommonFields	commonFields;
  QLogEvtSchemas	eventSchemas;
};

ZfStruct(, (QLogTrace, JSON),
  (vantage, (JSON::ID<"vantage_point">),		UDT),
  (commonFields, (JSON::ID<"common_fields">),		UDT),
  (eventSchemas, (JSON::ID<"event_schemas">),		UDT));

struct QLogHeader {
  ZuCSpan	fileSchema;
  ZuCSpan	serializationFormat;
  ZuCSpan	title;
  QLogImplementation	implementation;
  QLogTrace	trace;
};

ZfStruct(, (QLogHeader, JSON),
  (fileSchema, (JSON::ID<"file_schema">),			String),
  (serializationFormat, (JSON::ID<"serialization_format">),	String),
  (title,,							String),
  (implementation,,						UDT),
  (trace,,							UDT));

struct QLogEndpointInfo {
  bool		present = false;
  ZiIP		ip;
  uint16_t	port = 0;

  bool operator !() const { return !present; }
  ZuOpBool
};

ZfStruct(, (QLogEndpointInfo, JSON),
  (ip, (JSON::ID<"ip_v4">),		UDT),
  (port, (JSON::ID<"port_v4">),		UInt16));

struct QLogCxnStartedData {
  QLogEndpointInfo local;
  QLogEndpointInfo remote;
};

struct QLogCxnStartedDataJSON;
inline QLogCxnStartedDataJSON ZfJSON_Fmt(QLogCxnStartedData *);

struct QLogCxnStartedEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::CxnStarted;
  QLogCxnStartedData data;
  Zutcp::LinkInfo linkInfo;
};

QLogEvtFmt(QLogCxnStartedEvt);

struct QLogCxnStateData {
  Zutcp::LinkState::T oldState = Zutcp::LinkState::Starting;
  Zutcp::LinkState::T newState = Zutcp::LinkState::Handshaking;
};

ZfStruct(, (QLogCxnStateData, JSON),
  (oldState, (JSON::ID<"old">,
    Enum<Zutcp::LinkState::JSON>),	Int8),
  (newState, (JSON::ID<"new">,
    Enum<Zutcp::LinkState::JSON>),	Int8));

struct QLogCxnStateEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::CxnStateUpd;
  QLogCxnStateData data;
  Zutcp::LinkInfo linkInfo;
};

QLogEvtFmt(QLogCxnStateEvt);

struct QLogRawInfo {
  uint64_t	length = 0;
  uint64_t	payloadLength = 0;
};

ZfStruct(, (QLogRawInfo, JSON),
  (length,,						UInt64),
  (payloadLength, (JSON::ID<"payload_length">),		UInt64));

ZuDerive(QLogRawInfoArray,
  (ZuArray<QLogRawInfo, 1>));
inline ZfJSON::AsArray<ZfFieldTC::UDT> ZfJSON_Fmt(QLogRawInfoArray *);

ZuDerive(QLogECNArray,
  (ZuArray<Zutcp::EcnMark::T, 1>));
inline ZfJSON::AsArray<
  ZfFieldTC::Int8,
  ZuTypeList<ZuFieldProp::Enum<Zutcp::EcnMark::JSON>>> ZfJSON_Fmt(
    QLogECNArray *);

ZuDerive(QLogAckRange,
  (ZuArray<uint64_t, 2>));
inline ZfJSON::AsArray<ZfFieldTC::UInt64> ZfJSON_Fmt(QLogAckRange *);

ZuDerive(QLogAckRangeArray,
  (ZuArray<QLogAckRange, AckRangeMax>));
inline ZfJSON::AsArray<ZfFieldTC::UDT> ZfJSON_Fmt(QLogAckRangeArray *);

struct QLogDatagramData {
	uint16_t	count = 0;
	QLogRawInfoArray raw;
	QLogECNArray	ecn;
};

ZfStruct(, (QLogDatagramData, JSON),
  (count,,	UInt16),
  (raw,,	UDT),
  (ecn,,	UDT));

struct QLogDatagramEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::UDPTx;
  QLogDatagramData data;
  Zutcp::LinkInfo linkInfo;
};

QLogEvtFmt(QLogDatagramEvt);

struct QLogFrameData {
	Zutcp::FrameType::T frameType = Zutcp::FrameType::Unknown;
  uint64_t	offset = 0;
  QLogRawInfo	raw;
  double	ackDelay = 0;
  QLogAckRangeArray ackedRanges;
  uint64_t	maximum = 0;
  uint64_t	limit = 0;
  uint64_t	errorCode = 0;
  uint64_t	finalSize = 0;
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
inline QLogFrameDataJSON ZfJSON_Fmt(QLogFrameData *);

ZuDerive(QLogFrameArray,
  (ZuArray<QLogFrameData, FrameMax>));
inline ZfJSON::AsArray<ZfFieldTC::UDT> ZfJSON_Fmt(QLogFrameArray *);

struct QLogPacketHeader {
	Zutcp::PktType::T packetType = Zutcp::PktType::Initial;
	uint64_t	packetNumber = 0;
};

ZfStruct(, (QLogPacketHeader, JSON),
  (packetType, (JSON::ID<"packet_type">, Enum<Zutcp::PktType::JSON>),		Int8),
  (packetNumber, (JSON::ID<"packet_number">),					UInt64));

struct PktTrigger {
  ZtEnum(ZutcpAPI, PktTrigger, int8_t,
    None, Backpressure, KeysUnavailable, InternalError, Rejected, Unsupported,
    Invalid, Duplicate, ConnectionUnknown, DecryptionFailure, KeyUnavailable,
    General);
  ZtEnumMap(ZutcpAPI, PktTrigger, JSON,
    "", "backpressure", "keys_unavailable", "internal_error", "rejected",
    "unsupported", "invalid", "duplicate", "connection_unknown",
    "decryption_failure", "key_unavailable", "general");
};

struct QLogPacketData {
	QLogPacketHeader header;
	Zutcp::PktNumSpace::T packetSpace = Zutcp::PktNumSpace::Initial;
	QLogRawInfo	raw;
	Zutcp::EcnMark::T ecn = Zutcp::EcnMark::N;
  PktTrigger::T trigger = PktTrigger::None;
  uint64_t	bytesInFlight = 0;
  uint8_t	frameCount = 0;
  bool		framesTruncated = false;
  QLogFrameArray frames;
  bool		ackEliciting = false;
};

struct QLogPacketDataJSON;
inline QLogPacketDataJSON ZfJSON_Fmt(QLogPacketData *);

struct QLogPacketEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::PktSent;
  QLogPacketData data;
  Zutcp::LinkInfo linkInfo;
};

QLogEvtFmt(QLogPacketEvt);

ZuDerive(QLogPktNumArray,
  (ZuArray<uint64_t, AckPacketMax>));
inline ZfJSON::AsArray<ZfFieldTC::UInt64> ZfJSON_Fmt(
  QLogPktNumArray *);

struct QLogAckData {
	Zutcp::PktNumSpace::T packetSpace = Zutcp::PktNumSpace::Initial;
	QLogPktNumArray packetNumbers;
};

ZfStruct(, (QLogAckData, JSON),
  (packetSpace, (JSON::ID<"packet_number_space">,
    Enum<Zutcp::PktNumSpace::JSON>),			Int8),
  (packetNumbers, (JSON::ID<"packet_numbers">),		UDT));

struct QLogAckEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::PktsAcked;
  QLogAckData data;
  Zutcp::LinkInfo linkInfo;
};

QLogEvtFmt(QLogAckEvt);

struct PktLostTrigger {
  ZtEnum(ZutcpAPI, PktLostTrigger, int8_t,
    ReorderThresh, TimeThreshold, PTOExpired);
  ZtEnumMap(ZutcpAPI, PktLostTrigger, JSON,
    "reordering_threshold", "time_threshold", "pto_expired");
};

struct QLogPktLostData {
  QLogPacketHeader header;
  PktLostTrigger::T trigger =
    PktLostTrigger::ReorderThresh;
};

ZfStruct(, (QLogPktLostData, JSON),
  (header,,					UDT),
  (trigger, (Enum<PktLostTrigger::JSON>),	Int8));

struct QLogPktLostEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::PktLost;
  QLogPktLostData data;
  Zutcp::LinkInfo linkInfo;
};

QLogEvtFmt(QLogPktLostEvt);

struct QLogMarkRetransData {
  QLogFrameArray frames;
};

ZfStruct(, (QLogMarkRetransData, JSON),
  (frames,,	UDT));

struct QLogMarkRetransEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::MarkRetrans;
  QLogMarkRetransData data;
  Zutcp::LinkInfo linkInfo;
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

ZfStruct(, (QLogRecMetricsData, JSON),
  (latestRTT, (JSON::ID<"latest_rtt">),				Float),
  (smoothedRTT, (JSON::ID<"smoothed_rtt">),			Float),
  (rttVariance, (JSON::ID<"rtt_variance">),			Float),
  (minRTT, (JSON::ID<"min_rtt">),				Float),
  (congestionWindow, (JSON::ID<"congestion_window">),		UInt64),
  (ssthresh,,							UInt64),
  (bytesInFlight, (JSON::ID<"bytes_in_flight">),		UInt64));

struct QLogRecMetricsEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::MetricsUpd;
  QLogRecMetricsData data;
  Zutcp::LinkInfo linkInfo;
};

QLogEvtFmt(QLogRecMetricsEvt);

struct CongState {
  ZtEnum(ZutcpAPI, CongState, int8_t,
    SlowStart, CongAvoid, AppLimited, Recovery);
  ZtEnumMap(ZutcpAPI, CongState, JSON,
    "slow_start", "congestion_avoidance", "application_limited",
    "recovery");
};

struct CongTrigger {
  ZtEnum(ZutcpAPI, CongTrigger, int8_t,
    Ack, PMTUDAck, Loss, PMTUDLoss);
  ZtEnumMap(ZutcpAPI, CongTrigger, JSON,
    "ack", "pmtud_ack", "loss", "pmtud_loss");
};

struct QLogCongStateData {
  CongState::T newState =
    CongState::CongAvoid;
  CongTrigger::T trigger = CongTrigger::Ack;
};

ZfStruct(, (QLogCongStateData, JSON),
  (newState, (JSON::ID<"new">,
    Enum<CongState::JSON>),			Int8),
  (trigger, (Enum<CongTrigger::JSON>),		Int8));

struct QLogCongStateEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::CongestionUpd;
  QLogCongStateData data;
  Zutcp::LinkInfo linkInfo;
};

QLogEvtFmt(QLogCongStateEvt);

struct TimerType {
  ZtEnum(ZutcpAPI, TimerType, int8_t,
    Loss, PTO);
  ZtEnumMap(ZutcpAPI, TimerType, JSON,
    "loss_timeout", "pto");
};

struct TimerEvt {
  ZtEnum(ZutcpAPI, TimerEvt, int8_t,
    Set, Expired, Cancelled);
  ZtEnumMap(ZutcpAPI, TimerEvt, JSON,
    "set", "expired", "cancelled");
};

#define ZutcpLogLocalEnumImpl(ID) \
  ZtEnumImplStruct(ID); \
  ZtEnumImplStruct(ID, JSON)

ZutcpLogLocalEnumImpl(PktTrigger);
ZutcpLogLocalEnumImpl(PktLostTrigger);
ZutcpLogLocalEnumImpl(CongState);
ZutcpLogLocalEnumImpl(CongTrigger);
ZutcpLogLocalEnumImpl(TimerType);
ZutcpLogLocalEnumImpl(TimerEvt);

#undef ZutcpLogLocalEnumImpl

struct QLogTimerData {
  TimerType::T timerType = TimerType::Loss;
  Zutcp::PktNumSpace::T packetSpace = Zutcp::PktNumSpace::Initial;
  TimerEvt::T eventType = TimerEvt::Set;
  double	delta = 0;
};

ZfStruct(, (QLogTimerData, JSON),
  (timerType, (JSON::ID<"timer_type">,
    Enum<TimerType::JSON>),		Int8),
  (packetSpace, (JSON::ID<"packet_number_space">,
    Enum<Zutcp::PktNumSpace::JSON>),	Int8),
  (eventType, (JSON::ID<"event_type">,
    Enum<TimerEvt::JSON>),		Int8),
  (delta,,				Float));

struct QLogTimerEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::TimerUpd;
  QLogTimerData data;
  Zutcp::LinkInfo linkInfo;
};

QLogEvtFmt(QLogTimerEvt);

struct QLogECNData {
  ECNState::T oldState = ECNState::Unknown;
  ECNState::T newState = ECNState::Unknown;
};

ZfStruct(, (QLogECNData, JSON),
  (oldState, (JSON::ID<"old">, Enum<ECNState::JSON>),		Int8),
  (newState, (JSON::ID<"new">, Enum<ECNState::JSON>),		Int8));

struct QLogECNEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::ECNUpd;
  QLogECNData data;
  Zutcp::LinkInfo linkInfo;
};

QLogEvtFmt(QLogECNEvt);

struct QLogParamsData {
  Initiator::T initiator = Initiator::Local;
  ZuBSpan	origDCID;
  ZuBSpan	initialSCID;
  uint64_t	maxIdleTimeout = 0;
  uint64_t	maxPayloadSize = 0;
  uint64_t	ackDelayExponent = 0;
  uint64_t	maxAckDelay = 0;
  uint64_t	initialMaxData = 0;
};

struct QLogParamsDataJSON;
inline QLogParamsDataJSON ZfJSON_Fmt(QLogParamsData *);

struct QLogParamsEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::ParamsSet;
  QLogParamsData data;
  Zutcp::LinkInfo linkInfo;
};

QLogEvtFmt(QLogParamsEvt);

struct QLogVersionData {
  Versions	serverVersions;
  Versions	clientVersions;
  uint32_t	chosenVersion = 0;
  bool		chosenVersionPresent = false;
};

struct QLogVersionDataJSON;
inline QLogVersionDataJSON ZfJSON_Fmt(QLogVersionData *);

struct QLogVersionEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::VersionInfo;
  QLogVersionData data;
  Zutcp::LinkInfo linkInfo;
};

QLogEvtFmt(QLogVersionEvt);

struct QLogTupleAssignedData {
  uint64_t	tupleID = 0;
};

struct QLogTupleAssignedDataJSON;
inline QLogTupleAssignedDataJSON ZfJSON_Fmt(QLogTupleAssignedData *);

struct QLogPathEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::TupleAssigned;
  QLogTupleAssignedData data;
  Zutcp::LinkInfo linkInfo;
};

QLogEvtFmt(QLogPathEvt);

struct QLogMTUData {
  uint32_t	newMTU = 0;
  bool		done = false;
};

ZfStruct(, (QLogMTUData, JSON),
  (newMTU, (JSON::ID<"new">),				UInt32),
  (done,,						Bool));

struct QLogMTUEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::MTUUpd;
  QLogMTUData data;
  Zutcp::LinkInfo linkInfo;
};

QLogEvtFmt(QLogMTUEvt);

struct QLogCIDData {
  Initiator::T initiator = Initiator::Remote;
  ZuBSpan	oldCID;
  ZuBSpan	newCID;
};

struct QLogCIDDataJSON;
inline QLogCIDDataJSON ZfJSON_Fmt(QLogCIDData *);

struct QLogCIDEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::CIDUpd;
  QLogCIDData data;
  Zutcp::LinkInfo linkInfo;
};

QLogEvtFmt(QLogCIDEvt);

struct QLogCxnBlockedData {
  BlockedState::T oldState = BlockedState::Unblocked;
  BlockedState::T newState = BlockedState::Blocked;
  BlockedReason::T reason =
    BlockedReason::CxnFlowCtrl;
};

ZfStruct(, (QLogCxnBlockedData, JSON),
  (oldState, (JSON::ID<"old">, Enum<BlockedState::JSON>),	Int8),
  (newState, (JSON::ID<"new">, Enum<BlockedState::JSON>),	Int8),
  (reason, (Enum<BlockedReason::JSON>),				Int8));

struct QLogCxnBlockedEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::CxnDataBlockedUpd;
  QLogCxnBlockedData data;
  Zutcp::LinkInfo linkInfo;
};

QLogEvtFmt(QLogCxnBlockedEvt);

struct QLogCloseData {
	CloseInitiator::T initiator = CloseInitiator::Local;
	CloseTrigger::T trigger = CloseTrigger::Error;
  CloseReason::T reason = CloseReason::None;
  CloseError::T connectionError = CloseError::None;
  CloseError::T applicationError = CloseError::None;
  uint64_t	errorCode = 0;
};

struct QLogCloseDataJSON;
inline QLogCloseDataJSON ZfJSON_Fmt(QLogCloseData *);

struct QLogCloseEvt {
  uint64_t	time = 0;
  EvtName::T name = EvtName::CxnClosed;
  QLogCloseData data;
  Zutcp::LinkInfo linkInfo;
};

QLogEvtFmt(QLogCloseEvt);

static uint64_t qlogTime(ZuTime time)
{
  return uint64_t(time.microsecs());
}

static double qlogMS(uint64_t us)
{
  return double(us) / 1000.0;
}

bool ZutcpLogSink::init(ZuCSpan path, unsigned age)
{
  final();
  if (path)
    m_path = path;
  else
    m_path = "zutcp.sqlog";
  ZiFile::age(m_path, age);
  m_file.open(m_path, ZiFile::Write | ZiFile::GC);
  return !!m_file;
}

void ZutcpLogSink::final()
{
  if (m_file) m_file.close();
  m_path = {};
}

bool ZutcpLogSink::write(ZuCSpan s)
{
  return m_file && m_file.write(s.data(), s.length()) == Zi::OK;
}

ZutcpLogger::ZutcpLogger()
{
}

ZutcpLogger::~ZutcpLogger()
{
  // Do not use ZmSelf(): thread-local storage may already be in teardown.
  if (m_thread && m_thread.tid() != Zm::getTID()) {
    Engine::stop();
    m_thread.join();
  }
  if (!m_ring.closed()) m_ring.close();
}

ZutcpLogger *ZutcpLogger::instance()
{
  static constexpr auto ctor = []() { return new ZutcpLogger(); };
  return
    ZmSingleton<ZutcpLogger,
      ZmSingletonCtor<ctor,
	ZmSingletonCleanup<ZmCleanup::Library>>>::instance();
}

bool ZutcpLogger::init_(
  Trace &trace, const ZutcpLogParams &params, Zutcp::Vantage::T vantage)
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

void ZutcpLogger::start_()
{
  bool ok = false;
  {
    Guard guard(m_lock);
    if (m_configured) {
      m_ring.init(ZmRingParams{m_params.ringSize()});
      ok = m_ring.open(Ring::Read | Ring::Write) == Zu::OK;
      if (!ok) ++m_writerFailures;
    }
  }
  started(ok);
  if (ok) {
    work_();
    Engine::stopped();
  }
}

void ZutcpLogger::wake()
{
  Guard guard(m_lock);
  m_ring.eof(true);
}

void ZutcpLogger::stop_()
{
  {
    Guard guard(m_lock);
    m_ring.close();
    m_activeTrace = nullptr;
  }
  Engine::stopped(true);
}

void ZutcpLogger::stopIdle_(CtrlFn fn)
{
  {
    Guard guard(m_lock);
    if (m_configured) {
      guard.unlock();
      fn(true);
      return;
    }
  }
  Engine::stop(ZuMv(fn));
}

void ZutcpLogger::final_(Trace &trace)
{
  closeTrace_(trace);
}

ZutcpLogDiag ZutcpLogger::diag_() const
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

void ZutcpLogger::cxnStarted_(Trace &trace, CxnStartedEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writeCxnStarted_(event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZutcpLogger::dgramSent_(Trace &trace, DgramEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writeDatagramEvt_(EvtName::UDPTx, event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZutcpLogger::dgramRecv_(Trace &trace, DgramEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writeDatagramEvt_(EvtName::UDPRx, event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZutcpLogger::pktSent_(Trace &trace, PktEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writePktEvt_(EvtName::PktSent, event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZutcpLogger::pktRecv_(Trace &trace, PktEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writePktEvt_(EvtName::PktRecv, event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZutcpLogger::pktBuf_(Trace &trace, PktEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writePktEvt_(EvtName::PktBuf, event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZutcpLogger::pktDrop_(Trace &trace, PktEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writePktEvt_(EvtName::PktDrop, event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZutcpLogger::pktsAcked_(Trace &trace, AckEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writeAckEvt_(EvtName::PktsAcked, event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZutcpLogger::pktLost_(Trace &trace, RecLogEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writePktLost_(event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZutcpLogger::recPktLost_(Trace &trace, RecLogEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writePktLost_(event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZutcpLogger::markRetrans_(Trace &trace, RecLogEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writeMarkRetrans_(event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZutcpLogger::metricsUpd_(Trace &trace, RecLogEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writeRecMetrics_(event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZutcpLogger::lossTimerUpd_(Trace &trace, RecLogEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writeTimerEvt_(event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZutcpLogger::congStateUpd_(Trace &trace, RecLogEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writeCongState_(event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZutcpLogger::ecnStateUpd_(Trace &trace, ECNEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writeECNEvt_(event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}





void ZutcpLogger::paramsSet_(Trace &trace, ParamsEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writeParams_(event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}



void ZutcpLogger::versionInfo_(Trace &trace, VersionEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writeVersion_(event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}





void ZutcpLogger::pathUpdated_(Trace &trace, PathEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writePathEvt_(EvtName::TupleAssigned, event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}



void ZutcpLogger::pmtudUpdated_(Trace &trace, PathEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writeMTUEvt_(event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZutcpLogger::cidUpdated_(Trace &trace, CIDEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writeCIDEvt_(EvtName::CIDUpd, event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}



void ZutcpLogger::cxnDataBlockedUpd_(Trace &trace,
  BlockedEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writeCxnBlocked_(
      EvtName::CxnDataBlockedUpd, event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZutcpLogger::cxnClosed_(Trace &trace, CloseEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writeCloseEvt_(EvtName::CxnClosed, event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZutcpLogger::cxnStateUpd_(Trace &trace,
  CxnStateEvt event)
{
  if (!enabled_()) return;
  ZuTime time = Zm::now();
  auto fn_ = [trace = &trace, event_ = ZuMv(event), time](ZutcpLogger *this_) mutable {
    if (!this_->useTrace(trace)) return;
    this_->writeCxnState_(
      EvtName::CxnStateUpd, event_, time);
  };
  Fn fn{fn_};
  log__(fn);
}

void ZutcpLogger::work_()
{
  bool draining = false;
  for (;;) {
    if (ZuLikely(!m_queueCount.load_())) goto shift;
    if (Fn fn = m_queue.shift()) {
      if (ZuLikely(tryPush_(fn)))
	--m_queueCount;
      else
	m_queue.unshift(ZuMv(fn));
    }
shift:
    if (draining && !m_queueCount.load_()) m_ring.eof(true);
    if (void *ptr = m_ring.shift()) {
      m_activeTrace = nullptr;
      m_ring.shift2(Fn::invoke(ptr, this));
    } else {
      if (m_ring.readStatus() == Zu::EndOfFile) {
	if (!m_queueCount.load_()) break;
	// Reopen admission only for the writer's overflow drain.
	draining = true;
	m_ring.eof(false);
      }
    }
  }
  m_activeTrace = nullptr;
}

bool ZutcpLogger::tryPush_(Fn &fn)
{
  unsigned size = fn.pushSize();
  if (void *ptr = m_ring.tryPush(size)) {
    fn.push(ptr);
    m_ring.push2(ptr, size);
    return true;
  }
  return false;
}

bool ZutcpLogger::useTrace(Trace *trace)
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

void ZutcpLogger::closeTrace_(Trace &trace)
{
  if (trace.sinkOpened)
    trace.sink.final();
  Guard guard(m_lock);
  if (trace.configured && m_configured) --m_configured;
  trace.configured = false;
  trace.sinkOpened = false;
  trace.headerWritten = false;
  trace.vantage = Zutcp::Vantage::Unknown;
  trace.params = {};
  if (!m_configured) {
    m_params = {};
  }
}

bool ZutcpLogger::write_(Trace *trace, ZuCSpan data)
{
  if (!useTrace(trace)) return false;
  if (!writeHeader_(trace)) return false;
  if (!trace->sink.write(data)) {
    ++m_writerFailures;
    return false;
  }
  return true;
}

bool ZutcpLogger::writeHeader_(Trace *trace)
{
  if (!trace || trace->headerWritten) return true;
  trace->headerWritten = true;
  QLogHeader header{
    "urn:ietf:params:qlog:file:sequential",
    "application/qlog+json-seq",
    "zutcp",
    QLogImplementation{"zutcp", Z_VERNAME},
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
	"urn:zlib:zutcp:qlog:events:zutcp"
      }
    }
  };
  ZeLogBuf buf;
  buf << char(0x1e);
  ZfJSON::save<ZuFacet::JSON>(buf, header);
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
	qlogJSONField<Facet, Filter, ZfFieldTC::UDT>(
	  s, comma, "local", event.local);
      if (event.remote)
	qlogJSONField<Facet, Filter, ZfFieldTC::UDT>(
	  s, comma, "remote", event.remote);
      s << '}';
    }
  };
};

bool ZutcpLogger::writeCxnStarted_(
  const CxnStartedEvt &event, ZuTime time)
{
  QLogCxnStartedEvt qevent{
    qlogTime(time),
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
  ZfJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZutcpLogger::writeDatagramEvt_(
  EvtName::T name, const DgramEvt &event, ZuTime time)
{
  QLogRawInfoArray raw;
  new (raw.push()) QLogRawInfo{event.size, event.size};
  QLogECNArray ecn;
  new (ecn.push()) Zutcp::EcnMark::T(event.ecn);
  QLogDatagramEvt qevent{
    qlogTime(time),
    name,
    QLogDatagramData{1, ZuMv(raw), ZuMv(ecn)},
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZfJSON::save<ZuFacet::JSON>(m_buf, qevent);
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
static void qlogJSONSep(S &s, bool &comma)
{
  if (comma) s << ',';
  comma = true;
}

template <
  typename Facet, template <typename> class Filter,
  unsigned TypeCode, typename Props,
  typename S, typename T>
static void qlogJSONField(S &s, bool &comma, ZuCSpan id, const T &v)
{
  qlogJSONSep(s, comma);
  s << '"';
  s << id;
  s << "\":";
  ZfJSON::saveValue<Facet, Filter, TypeCode, Props>(s, v);
}

struct QLogPacketDataJSON {
  template <typename O, typename Facet>
  struct Handler {
    template <template <typename> class Filter, typename S>
    static void save(S &s, const QLogPacketData &packet)
    {
      using PktSpaceProps =
	ZuTypeList<ZuFieldProp::Enum<Zutcp::PktNumSpace::JSON>>;
      using ECNProps =
	ZuTypeList<ZuFieldProp::Enum<Zutcp::EcnMark::JSON>>;
      using TriggerProps =
	ZuTypeList<ZuFieldProp::Enum<PktTrigger::JSON>>;

      bool comma = false;
      s << '{';
      qlogJSONField<Facet, Filter, ZfFieldTC::UDT>(
	s, comma, "header", packet.header);
      qlogJSONField<Facet, Filter, ZfFieldTC::Int8, PktSpaceProps>(
	s, comma, "packet_number_space", packet.packetSpace);
      qlogJSONField<Facet, Filter, ZfFieldTC::UDT>(
	s, comma, "raw", packet.raw);
      qlogJSONField<Facet, Filter, ZfFieldTC::Int8, ECNProps>(
	s, comma, "ecn", packet.ecn);
      if (packet.trigger != PktTrigger::None)
	qlogJSONField<Facet, Filter, ZfFieldTC::Int8, TriggerProps>(
	  s, comma, "trigger", packet.trigger);
      qlogJSONField<Facet, Filter, ZfFieldTC::UInt64>(
	s, comma, "bytes_in_flight", packet.bytesInFlight);
      qlogJSONField<Facet, Filter, ZfFieldTC::UInt8>(
	s, comma, "frame_count", packet.frameCount);
      qlogJSONField<Facet, Filter, ZfFieldTC::Bool>(
	s, comma, "frames_truncated", packet.framesTruncated);
      qlogJSONField<Facet, Filter, ZfFieldTC::UDT>(
	s, comma, "frames", packet.frames);
      qlogJSONField<Facet, Filter, ZfFieldTC::Bool>(
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
	ZuTypeList<ZuFieldProp::Enum<Zutcp::FrameType::JSON>>;

      bool comma = false;
      s << '{';
      qlogJSONField<Facet, Filter, ZfFieldTC::Int8, FrameTypeProps>(
	s, comma, "frame_type", frame.frameType);

      switch (frame.frameType) {
	case Zutcp::FrameType::Ack:
	  if (!ZuNull(frame.ackDelay) && frame.ackDelay)
	    qlogJSONField<Facet, Filter, ZfFieldTC::Float>(
	      s, comma, "ack_delay", frame.ackDelay);
	  if (frame.ackedRanges)
	    qlogJSONField<Facet, Filter, ZfFieldTC::UDT>(
	      s, comma, "acked_ranges", frame.ackedRanges);
	  if (frame.ect0)
	    qlogJSONField<Facet, Filter, ZfFieldTC::UInt64>(
	      s, comma, "ect0", frame.ect0);
	  if (frame.ect1)
	    qlogJSONField<Facet, Filter, ZfFieldTC::UInt64>(
	      s, comma, "ect1", frame.ect1);
	  if (frame.ce)
	    qlogJSONField<Facet, Filter, ZfFieldTC::UInt64>(
	      s, comma, "ce", frame.ce);
	  break;
	case Zutcp::FrameType::Data:
	  if (frame.offset)
	    qlogJSONField<Facet, Filter, ZfFieldTC::UInt64>(
	      s, comma, "offset", frame.offset);
	  qlogJSONField<Facet, Filter, ZfFieldTC::UDT>(
	    s, comma, "raw", frame.raw);
	  if (frame.fin)
	    qlogJSONField<Facet, Filter, ZfFieldTC::Bool>(
	      s, comma, "fin", frame.fin);
	  break;
	case Zutcp::FrameType::Reset:
	  qlogJSONField<Facet, Filter, ZfFieldTC::UInt64>(
	    s, comma, "error_code", frame.errorCode);
	  qlogJSONField<Facet, Filter, ZfFieldTC::UInt64>(
	    s, comma, "final_size", frame.finalSize);
	  break;
	case Zutcp::FrameType::StopSending:
	  qlogJSONField<Facet, Filter, ZfFieldTC::UInt64>(
	    s, comma, "error_code", frame.errorCode);
	  break;
	case Zutcp::FrameType::MaxData:
	  qlogJSONField<Facet, Filter, ZfFieldTC::UInt64>(
	    s, comma, "maximum", frame.maximum);
	  break;
	case Zutcp::FrameType::DataBlocked:
	  qlogJSONField<Facet, Filter, ZfFieldTC::UInt64>(
	    s, comma, "limit", frame.limit);
	  break;
	case Zutcp::FrameType::ConnectionClose:
	case Zutcp::FrameType::ApplicationClose:
	  qlogJSONField<Facet, Filter, ZfFieldTC::UInt64>(
	    s, comma, "error_code", frame.errorCode);
	  if (frame.raw.length)
	    qlogJSONField<Facet, Filter, ZfFieldTC::UDT>(
	      s, comma, "raw", frame.raw);
	  break;
	default:
	  break;
      }
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
      qlogJSONField<Facet, Filter, ZfFieldTC::Int8, InitiatorProps>(
	s, comma, "initiator", params.initiator);
      if (params.origDCID)
	qlogJSONField<Facet, Filter, ZfFieldTC::Bytes, BytesHexProps>(
	  s, comma, "original_destination_connection_id",
	  params.origDCID);
      if (params.initialSCID)
	qlogJSONField<Facet, Filter, ZfFieldTC::Bytes, BytesHexProps>(
	  s, comma, "initial_source_connection_id", params.initialSCID);
      qlogJSONField<Facet, Filter, ZfFieldTC::UInt64>(
	s, comma, "max_idle_timeout", params.maxIdleTimeout);
      qlogJSONField<Facet, Filter, ZfFieldTC::UInt64>(
	s, comma, "max_payload_size", params.maxPayloadSize);
      qlogJSONField<Facet, Filter, ZfFieldTC::UInt64>(
	s, comma, "ack_delay_exponent", params.ackDelayExponent);
      qlogJSONField<Facet, Filter, ZfFieldTC::UInt64>(
	s, comma, "max_ack_delay", params.maxAckDelay);
      qlogJSONField<Facet, Filter, ZfFieldTC::UInt64>(
	s, comma, "initial_max_data", params.initialMaxData);
      s << '}';
    }
  };
};

template <typename S>
static void qlogJSONVersion(S &s, uint32_t version)
{
  s << '"' <<
    ZuBoxed(version).hex<false, ZuFmt::Right<8, '0'>>() <<
    '"';
}

template <typename S>
static void qlogJSONVersionArray(S &s, const Versions &versions)
{
  s << '[';
  for (unsigned i = 0, n = versions.length(); i < n; ++i) {
    if (i) s << ',';
    qlogJSONVersion(s, versions[i]);
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
	qlogJSONSep(s, comma);
	s << "\"server_versions\":";
	qlogJSONVersionArray(s, version.serverVersions);
      }
      if (version.clientVersions) {
	qlogJSONSep(s, comma);
	s << "\"client_versions\":";
	qlogJSONVersionArray(s, version.clientVersions);
      }
      if (version.chosenVersionPresent) {
	qlogJSONSep(s, comma);
	s << "\"chosen_version\":";
	qlogJSONVersion(s, version.chosenVersion);
      }
      s << '}';
    }
  };
};

template <typename S>
static void qlogJSONTupleID(S &s, uint64_t tupleID)
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
      qlogJSONTupleID(s, tuple.tupleID);
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
      qlogJSONField<Facet, Filter, ZfFieldTC::Int8, InitiatorProps>(
	s, comma, "initiator", cid.initiator);
      if (cid.oldCID)
	qlogJSONField<Facet, Filter, ZfFieldTC::Bytes, BytesHexProps>(
	  s, comma, "old", cid.oldCID);
      if (cid.newCID)
	qlogJSONField<Facet, Filter, ZfFieldTC::Bytes, BytesHexProps>(
	  s, comma, "new", cid.newCID);
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
	  close.errorCode <= Zutcp::TransportError::ProtViolation)
	connectionError = CloseError::T(close.errorCode);
      CloseError::T applicationError = close.applicationError;

      bool comma = false;
      s << '{';
      qlogJSONField<Facet, Filter, ZfFieldTC::Int8, InitiatorProps>(
	s, comma, "initiator", close.initiator);
      if (connectionError != CloseError::None)
	qlogJSONField<Facet, Filter, ZfFieldTC::Int8, ErrorProps>(
	  s, comma, "connection_error", connectionError);
      if (applicationError != CloseError::None)
	qlogJSONField<Facet, Filter, ZfFieldTC::Int8, ErrorProps>(
	  s, comma, "application_error", applicationError);
      if ((connectionError == CloseError::Unknown ||
	  applicationError == CloseError::Unknown) &&
	  close.errorCode)
	qlogJSONField<Facet, Filter, ZfFieldTC::UInt64>(
	  s, comma, "error_code", close.errorCode);
      if (close.reason != CloseReason::None)
	qlogJSONField<Facet, Filter, ZfFieldTC::Int8, ReasonProps>(
	  s, comma, "reason", close.reason);
      qlogJSONField<Facet, Filter, ZfFieldTC::Int8, TriggerProps>(
	s, comma, "trigger", close.trigger);
      s << '}';
    }
  };
};

static QLogFrameArray qlogFrames(
  const ZuArray<FrameEvt, FrameMax> &frames_)
{
  QLogFrameArray frames;
  for (unsigned i = 0, n = frames_.length(); i < n; ++i) {
    const FrameEvt &frame = frames_[i];
    QLogFrameData qframe{.frameType = frame.type};
    switch (frame.type) {
      case Zutcp::FrameType::Ack:
	qframe.ackDelay = qlogMS(frame.ackDelayUS);
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
      case Zutcp::FrameType::Data:
	qframe.offset = frame.offset;
	qframe.raw.length = frame.length;
	qframe.fin = frame.fin;
	break;
      case Zutcp::FrameType::Reset:
	qframe.errorCode = frame.errorCode;
	qframe.finalSize = frame.length;
	break;
      case Zutcp::FrameType::StopSending:
	qframe.errorCode = frame.errorCode;
	break;
      case Zutcp::FrameType::MaxData:
	qframe.maximum = frame.value;
	break;
      case Zutcp::FrameType::DataBlocked:
	qframe.limit = frame.value;
	break;
      case Zutcp::FrameType::ConnectionClose:
      case Zutcp::FrameType::ApplicationClose:
	qframe.errorCode = frame.errorCode;
	qframe.raw.length = frame.length;
	break;
      default:
	break;
    }
    new (frames.push()) QLogFrameData{qframe};
  }
  return frames;
}

static PktTrigger::T qlogPktTrigger(
  EvtName::T name, Reason::T reason)
{
  switch (name) {
    case EvtName::PktBuf:
      return PktTrigger::None;
    case EvtName::PktDrop:
      switch (reason) {
	case Reason::PrepareLong:
	case Reason::UnsupportedLongType:
	case Reason::DiscardedSpace:
	  return PktTrigger::Unsupported;
	case Reason::ParseLong:
	case Reason::PacketLength:
	case Reason::ParseShort:
	case Reason::FramePolicy:
	  return PktTrigger::Invalid;
	case Reason::Duplicate:
	  return PktTrigger::Duplicate;
	case Reason::AntiAmp:
	case Reason::ProbeAdmit:
	case Reason::AppSend:
	  return PktTrigger::Rejected;
	default:
	  return PktTrigger::General;
      }
    default:
      return PktTrigger::None;
  }
}

bool ZutcpLogger::writePktEvt_(
  EvtName::T name, const PktEvt &event, ZuTime time)
{
  QLogPacketEvt qevent{
    qlogTime(time),
    name,
    QLogPacketData{
      QLogPacketHeader{event.packetType, event.packetNumber},
	      event.packetSpace,
	      QLogRawInfo{event.packetSize, event.payloadSize},
	      event.ecn,
	      qlogPktTrigger(name, event.reason),
	      event.bytesInFlight,
      event.frameCount,
      event.framesTruncated,
      qlogFrames(event.frames),
      event.ackEliciting
    },
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZfJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZutcpLogger::writeAckEvt_(
  EvtName::T name, const AckEvt &event, ZuTime time)
{
  QLogPktNumArray packetNumbers;
  for (unsigned i = 0, n = event.packetNumbers.length(); i < n; ++i)
    new (packetNumbers.push()) uint64_t(event.packetNumbers[i]);
  if (!packetNumbers)
    return true;
  QLogAckEvt qevent{
    qlogTime(time),
    name,
    QLogAckData{
      event.packetSpace,
      ZuMv(packetNumbers)
    },
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZfJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

static PktLostTrigger::T qlogLossTrigger(
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

bool ZutcpLogger::writePktLost_(
  const RecLogEvt &event, ZuTime time)
{
  QLogPktLostEvt qevent{
    qlogTime(time),
    EvtName::PktLost,
    QLogPktLostData{
      QLogPacketHeader{
	Zutcp::pktTypeFromSpace(event.packetSpace),
	event.packetNumber
      },
      qlogLossTrigger(event.reason)
    },
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZfJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZutcpLogger::writeMarkRetrans_(
  const RecLogEvt &event, ZuTime time)
{
  if (!event.frames) return true;
  QLogMarkRetransEvt qevent{
    qlogTime(time),
    EvtName::MarkRetrans,
    QLogMarkRetransData{
      qlogFrames(event.frames)
    },
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZfJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZutcpLogger::writeRecMetrics_(
  const RecLogEvt &event, ZuTime time)
{
  QLogRecMetricsEvt qevent{
    qlogTime(time),
    EvtName::MetricsUpd,
    QLogRecMetricsData{
      qlogMS(event.latestRTTUS),
      qlogMS(event.smoothedRTTUS),
      qlogMS(event.rttVarianceUS),
      qlogMS(event.minRTTUS),
      event.cwnd,
      event.ssthresh,
      event.bytesInFlight
    },
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZfJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

static CongState::T qlogCongState(
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

static CongTrigger::T qlogCongTrigger(
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

bool ZutcpLogger::writeCongState_(
  const RecLogEvt &event, ZuTime time)
{
  QLogCongStateEvt qevent{
    qlogTime(time),
    EvtName::CongestionUpd,
    QLogCongStateData{
      qlogCongState(event),
      qlogCongTrigger(event.reason)
    },
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZfJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

static TimerType::T qlogTimerType(
  RecKind::T kind)
{
  switch (kind) {
    case RecKind::PTO:
      return TimerType::PTO;
    default:
      return TimerType::Loss;
  }
}

static TimerEvt::T qlogTimerEvtType(
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

static double qlogTimerDelta(
  const RecLogEvt &event, ZuTime time)
{
  if (event.reason != RecReason::Armed) return 0;
  uint64_t now = qlogTime(time);
  if (event.deadlineUS <= now) return 0;
  return qlogMS(event.deadlineUS - now);
}

bool ZutcpLogger::writeTimerEvt_(
  const RecLogEvt &event, ZuTime time)
{
  QLogTimerEvt qevent{
    qlogTime(time),
    EvtName::TimerUpd,
    QLogTimerData{
      qlogTimerType(event.kind),
      event.packetSpace,
      qlogTimerEvtType(event.reason),
      qlogTimerDelta(event, time)
    },
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZfJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZutcpLogger::writeECNEvt_(
  const ECNEvt &event, ZuTime time)
{
  QLogECNEvt qevent{
    qlogTime(time),
    EvtName::ECNUpd,
    QLogECNData{
      event.oldState,
      event.state
    },
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZfJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}







bool ZutcpLogger::writeParams_(
  const ParamsEvt &event, ZuTime time)
{
  QLogParamsEvt qevent{
    qlogTime(time),
    EvtName::ParamsSet,
    QLogParamsData{
      event.initiator,
      event.origDCID,
      event.initialSCID,
      event.maxIdleTimeout,
      event.maxPayloadSize,
      event.ackDelayExponent,
      event.maxAckDelay,
      event.initialMaxData,

    },
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZfJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}



bool ZutcpLogger::writeVersion_(
  const VersionEvt &event, ZuTime time)
{
  QLogVersionEvt qevent{
    qlogTime(time),
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
  ZfJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}



bool ZutcpLogger::writePathEvt_(
  EvtName::T name, const PathEvt &event, ZuTime time)
{
  QLogPathEvt qevent{
    qlogTime(time),
    name,
    QLogTupleAssignedData{event.tupleID},
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZfJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZutcpLogger::writeMTUEvt_(
  const PathEvt &event, ZuTime time)
{
  QLogMTUEvt qevent{
    qlogTime(time),
    EvtName::MTUUpd,
    QLogMTUData{
      event.mtu,
      event.validated
    },
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZfJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}



bool ZutcpLogger::writeCIDEvt_(
  EvtName::T name, const CIDEvt &event, ZuTime time)
{
  bool retired =
    event.action == CIDAction::Tombstone;
  auto cid = event.cxnID.cspan();
  QLogCIDEvt qevent{
    qlogTime(time),
    name,
    QLogCIDData{
      Initiator::T(
	event.local ? Initiator::Local : Initiator::Remote),
      retired ? cid : ZuBSpan{},
      retired ? ZuBSpan{} : cid,
    },
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZfJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}



bool ZutcpLogger::writeCxnBlocked_(
  EvtName::T name, const BlockedEvt &event, ZuTime time)
{
  QLogCxnBlockedEvt qevent{
    qlogTime(time),
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
  ZfJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZutcpLogger::writeCloseEvt_(
  EvtName::T name, const CloseEvt &event, ZuTime time)
{
  QLogCloseEvt qevent{
    qlogTime(time),
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
  ZfJSON::save<ZuFacet::JSON>(m_buf, qevent);
  m_buf << '\n';
  if (!write_(m_activeTrace, m_buf.cspan())) {
    ++m_writerFailures;
    return false;
  }
  ++m_recordsWritten;
  m_bytesWritten.store_(m_bytesWritten.load_() + m_buf.length());
  return true;
}

bool ZutcpLogger::writeCxnState_(
  EvtName::T name,
  const CxnStateEvt &event, ZuTime time)
{
  QLogCxnStateEvt qevent{
    qlogTime(time),
    name,
    QLogCxnStateData{
      event.oldState,
      event.newState
    },
    event.linkInfo
  };
  m_buf.length(0);
  m_buf << char(0x1e);
  ZfJSON::save<ZuFacet::JSON>(m_buf, qevent);
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

#endif /* Zutcp_DEBUG */
