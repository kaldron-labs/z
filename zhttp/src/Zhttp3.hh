//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP/3 over Zquic

#ifndef Zhttp3_HH
#define Zhttp3_HH

#ifndef ZhttpQPack_HH
#include <zlib/ZhttpQPack.hh>
#endif

#include <zlib/Zquic.hh>
#include <zlib/ZquicFrame.hh>

namespace Zhttp { namespace H3 {

struct H3FrameType {
  ZtEnum(H3FrameType, int8_t,
    Data, Headers, CancelPush, Settings, PushPromise, Goaway, MaxPushID,
    Unknown);
};

struct H3StreamType {
  ZtEnum(H3StreamType, int8_t, Control, Push, QPackEncoder, QPackDecoder);
};

struct MessagePart {
  ZtEnum(MessagePart, int8_t, None, Headers, Data, Trailers);
};

struct H3Setting {
  ZtEnum(H3Setting, int8_t,
    QPackMaxTableCapacity, MaxFieldSectionSize, QPackBlockedStreams,
    Unknown);
};

struct ErrorCodec {
  static uint64_t code(H3Error::T);
  static H3Error::T fromCode(uint64_t);
  static ZuCSpan name(H3Error::T);
};

struct Settings {
  uint64_t	qpackMaxTableCapacity = 0;
  uint64_t	maxFieldSectionSize = 0;
  uint64_t	qpackBlockedStreams = 0;
  bool		hasQPackMaxTableCapacity = false;
  bool		hasMaxFieldSectionSize = false;
  bool		hasQPackBlockedStreams = false;
};

struct FallbackProtocol {
  ZtEnum(FallbackProtocol, int8_t, None, HTTP3, HTTP11);
};

struct FallbackReason {
  ZtEnum(FallbackReason, int8_t,
    None, HTTP3Disabled, HTTP3Unavailable, ALPNRejected, ConnectFailed,
    HTTP11Disabled);
};

struct FallbackDecision {
  FallbackProtocol::T	protocol = FallbackProtocol::None;
  FallbackReason::T	reason = FallbackReason::None;
  H3Error::T		error = H3Error::NoError;

  bool fallback() const { return protocol == FallbackProtocol::HTTP11; }
  bool failed() const { return protocol == FallbackProtocol::None; }
};

class FallbackPolicy {
public:
  FallbackPolicy &&http3(bool v) { m_http3 = v; return ZuMv(*this); }
  FallbackPolicy &&http11(bool v) { m_http11 = v; return ZuMv(*this); }

  bool http3() const { return m_http3; }
  bool http11() const { return m_http11; }

  FallbackDecision select(
      bool h3Available, bool h3ALPNAccepted, bool h3Connected,
      H3Error::T connectError = H3Error::ConnectError) const {
    if (!m_http3)
      return fallback_(FallbackReason::HTTP3Disabled,
	H3Error::VersionFallback);
    if (!h3Available)
      return fallback_(FallbackReason::HTTP3Unavailable,
	H3Error::VersionFallback);
    if (!h3ALPNAccepted)
      return fallback_(FallbackReason::ALPNRejected,
	H3Error::VersionFallback);
    if (!h3Connected)
      return fallback_(FallbackReason::ConnectFailed, connectError);
    return FallbackDecision{FallbackProtocol::HTTP3,
      FallbackReason::None, H3Error::NoError};
  }

private:
  FallbackDecision fallback_(
      FallbackReason::T reason, H3Error::T error) const {
    if (!m_http11)
      return FallbackDecision{FallbackProtocol::None,
	FallbackReason::HTTP11Disabled, error};
    return FallbackDecision{FallbackProtocol::HTTP11, reason, error};
  }

  bool	m_http3 = true;
  bool	m_http11 = true;
};

struct RequestParams {
  Method::T	method = Method::GET;
  ZuCSpan	scheme = "https";
  ZuCSpan	authority;
  ZuCSpan	path = "/";
  ZuSpan<Header> headers;
};

struct ResponseParams {
  unsigned	status = 200;
  ZuSpan<Header> headers;
};

struct DecodedRequest {
  Method::T	method = -1;
  ZuCSpan	scheme;
  ZuCSpan	authority;
  ZuCSpan	path;
  Headers	headers;
  HeaderBytes	storage;
};

struct DecodedResponse {
  unsigned	status = 0;
  Headers	headers;
  HeaderBytes	storage;
};

struct DecodedTrailers {
  Headers	headers;
  HeaderBytes	storage;
};

struct H3Frame {
  H3FrameType::T	type = H3FrameType::Unknown;
  ZuCSpan		payload;
};

using DiagText = ZtString<ZtStringHeapID<"Zhttp.H3.DiagText">>;

struct Diag {
  static ZuCSpan frameTypeName(H3FrameType::T);
  static ZuCSpan streamTypeName(H3StreamType::T);
  static ZuCSpan settingName(H3Setting::T);
  static ZuCSpan messagePartName(MessagePart::T);

  void noteFrameRx(const H3Frame &frame) {
    ++framesRx;
    bytesRx += frame.payload.length();
    notePayloadRx_(frame.type, frame.payload.length());
  }
  void noteFrameTx(H3FrameType::T type, unsigned payloadBytes) {
    ++framesTx;
    bytesTx += payloadBytes;
    notePayloadTx_(type, payloadBytes);
  }
  void noteHeaderRx(unsigned bytes) {
    ++headersRx;
    headerBytesRx += bytes;
  }
  void noteHeaderTx(unsigned bytes) {
    ++headersTx;
    headerBytesTx += bytes;
  }
  void noteBodyRx(unsigned bytes) {
    ++dataFramesRx;
    bodyBytesRx += bytes;
  }
  void noteBodyTx(unsigned bytes) {
    ++dataFramesTx;
    bodyBytesTx += bytes;
  }
  void noteQPackInstructionRx() { ++qpackInstructionsRx; }
  void noteQPackInstructionTx() { ++qpackInstructionsTx; }
  void noteError(H3Error::T error) {
    lastError = error;
    ++errors;
  }

  static void frameSummary(const H3Frame &, DiagText &);
  static DiagText frameSummary(const H3Frame &frame) {
    DiagText out;
    frameSummary(frame, out);
    return out;
  }
  static void fieldSectionSummary(ZuSpan<Header>, DiagText &);
  static DiagText fieldSectionSummary(ZuSpan<Header> fields) {
    DiagText out;
    fieldSectionSummary(fields, out);
    return out;
  }
  void summary(DiagText &) const;
  DiagText summary() const {
    DiagText out;
    summary(out);
    return out;
  }

  uint64_t	framesRx = 0;
  uint64_t	framesTx = 0;
  uint64_t	bytesRx = 0;
  uint64_t	bytesTx = 0;
  uint64_t	headersRx = 0;
  uint64_t	headersTx = 0;
  uint64_t	headerBytesRx = 0;
  uint64_t	headerBytesTx = 0;
  uint64_t	dataFramesRx = 0;
  uint64_t	dataFramesTx = 0;
  uint64_t	bodyBytesRx = 0;
  uint64_t	bodyBytesTx = 0;
  uint64_t	controlFramesRx = 0;
  uint64_t	controlFramesTx = 0;
  uint64_t	goawayRx = 0;
  uint64_t	goawayTx = 0;
  uint64_t	qpackInstructionsRx = 0;
  uint64_t	qpackInstructionsTx = 0;
  uint64_t	errors = 0;
  H3Error::T	lastError = H3Error::NoError;

private:
  void notePayloadRx_(H3FrameType::T, unsigned);
  void notePayloadTx_(H3FrameType::T, unsigned);
};

struct H3FrameCodec {
  static int write(H3FrameType::T, ZuCSpan, HeaderBytes &);
  static int parse(ZuCSpan, H3Frame &, unsigned &);
  static int writeSettingsPayload(const Settings &, HeaderBytes &);
  static int writeSettings(const Settings &, HeaderBytes &);
  static int parseSettings(ZuCSpan, Settings &);
};

class MessageStream {
public:
  bool headersReceived() const { return m_headersReceived; }
  bool dataReceived() const { return m_dataReceived; }
  bool trailersReceived() const { return m_trailersReceived; }
  bool closed() const { return m_closed; }
  bool cancelled() const { return m_cancelled; }
  uint64_t bodyBytes() const { return m_bodyBytes; }
  unsigned dataFrames() const { return m_dataFrames; }
  uint64_t cancelCode() const { return m_cancelCode; }
  H3Error::T error() const { return m_error; }
  MessagePart::T lastPart() const { return m_lastPart; }

  bool receiveFrame(const H3Frame &, bool fin = false);
  template <typename L>
  bool receiveFrame(const H3Frame &frame, L body, bool fin = false) {
    if (!receiveFrame(frame, fin)) return false;
    if (frame.type == H3FrameType::Data) body(frame.payload);
    return true;
  }
  void cancel();
  void cancel(uint64_t);
  template <typename Stream>
  void cancelTransport(Stream &stream) {
    cancel();
    stream.reset(m_cancelCode);
    stream.stop(m_cancelCode);
  }
  template <typename Stream>
  void cancelTransport(Stream &stream, uint64_t appError) {
    cancel(appError);
    stream.reset(appError);
    stream.stop(appError);
  }

private:
  bool		m_headersReceived = false;
  bool		m_dataReceived = false;
  bool		m_trailersReceived = false;
  bool		m_closed = false;
  bool		m_cancelled = false;
  H3Error::T	m_error = H3Error::NoError;
  MessagePart::T m_lastPart = MessagePart::None;
  uint64_t	m_bodyBytes = 0;
  unsigned	m_dataFrames = 0;
  uint64_t	m_cancelCode = 0;
};

class Connection {
public:
  explicit Connection(Params params = {}) :
    m_params{ZuMv(params)},
    m_qpackEncoder{m_params.qpackTableCapacity(),
      m_params.qpackBlockedStreams()},
    m_qpackDecoder{m_params.qpackTableCapacity(),
      m_params.qpackBlockedStreams()} { }

  const Params &params() const { return m_params; }
  DynamicState &qpackEncoderState() { return m_qpackEncoder; }
  const DynamicState &qpackEncoderState() const { return m_qpackEncoder; }
  DynamicState &qpackDecoderState() { return m_qpackDecoder; }
  const DynamicState &qpackDecoderState() const { return m_qpackDecoder; }
  bool settingsSent() const { return m_settingsSent; }
  bool controlOpened() const { return m_controlOpened; }
  bool qpackEncoderOpened() const { return m_qpackEncoderOpened; }
  bool qpackDecoderOpened() const { return m_qpackDecoderOpened; }
  bool peerControlOpened() const { return m_peerControlOpened; }
  bool peerQPackEncoderOpened() const { return m_peerQPackEncoderOpened; }
  bool peerQPackDecoderOpened() const { return m_peerQPackDecoderOpened; }
  bool peerControlClosed() const { return m_peerControlClosed; }
  bool peerQPackEncoderClosed() const { return m_peerQPackEncoderClosed; }
  bool peerQPackDecoderClosed() const { return m_peerQPackDecoderClosed; }
  bool peerSettingsReceived() const { return m_peerSettingsReceived; }
  const Settings &peerSettings() const { return m_peerSettings; }
  bool goawaySent() const { return m_goawaySent; }
  uint64_t goawayID() const { return m_goawayID; }
  bool peerGoawayReceived() const { return m_peerGoawayReceived; }
  uint64_t peerGoawayID() const { return m_peerGoawayID; }
  const Diag &diag() const { return m_diag; }
  Diag &diag() { return m_diag; }

  void openLocalControl() {
    m_controlOpened = true;
    m_settingsSent = true;
  }
  void openLocalQPack() {
    m_qpackEncoderOpened = true;
    m_qpackDecoderOpened = true;
  }
  bool ready() const {
    return m_settingsSent && m_controlOpened &&
      m_qpackEncoderOpened && m_qpackDecoderOpened;
  }
  bool acceptPeerStream(H3StreamType::T);
  bool acceptRequestStream(uint64_t) const;
  H3Error::T peerStreamClosed(H3StreamType::T);
  bool receiveControlFrame(const H3Frame &);
  Settings localSettings() const;
  int encodeSettings(HeaderBytes &) const;
  int encodeGoaway(HeaderBytes &, uint64_t);
  void goaway(uint64_t id) {
    m_goawaySent = true;
    m_goawayID = id;
  }

  int encodeRequest(HeaderBytes &, const RequestParams &);
  int encodeRequestDynamic(HeaderBytes &, HeaderBytes &, const RequestParams &);
  int encodeResponse(HeaderBytes &, const ResponseParams &);
  int encodeResponseDynamic(HeaderBytes &, HeaderBytes &, const ResponseParams &);
  int encodeTrailers(HeaderBytes &, ZuSpan<Header>);
  int encodeTrailersDynamic(HeaderBytes &, HeaderBytes &, ZuSpan<Header>);
  int encodeData(HeaderBytes &, ZuCSpan) const;
  int decodeData(const H3Frame &, ZuCSpan &) const;
  int decodeRequest(const H3Frame &, DecodedRequest &,
    uint64_t insertCount = 0) const;
  int decodeRequestDynamic(const H3Frame &, DecodedRequest &) const;
  int decodeResponse(const H3Frame &, DecodedResponse &,
    uint64_t insertCount = 0) const;
  int decodeResponseDynamic(const H3Frame &, DecodedResponse &) const;
  int decodeTrailers(const H3Frame &, DecodedTrailers &,
    uint64_t insertCount = 0) const;
  int decodeTrailersDynamic(const H3Frame &, DecodedTrailers &) const;
  int receiveQPackEncoderStream(ZuCSpan);
  int receiveQPackDecoderStream(ZuCSpan);
  bool applyQPackEncoderInstruction(const QPackDecodedInstruction &);
  bool applyQPackDecoderInstruction(const QPackDecodedInstruction &);
  static bool validateRequest(ZuSpan<Header>);
  static bool validateResponse(ZuSpan<Header>);
  static bool validateTrailers(ZuSpan<Header>);

private:
  Params	m_params;
  DynamicState	m_qpackEncoder;
  DynamicState	m_qpackDecoder;
  Settings	m_peerSettings;
  bool		m_settingsSent = false;
  bool		m_controlOpened = false;
  bool		m_qpackEncoderOpened = false;
  bool		m_qpackDecoderOpened = false;
  bool		m_peerControlOpened = false;
  bool		m_peerQPackEncoderOpened = false;
  bool		m_peerQPackDecoderOpened = false;
  bool		m_peerControlClosed = false;
  bool		m_peerQPackEncoderClosed = false;
  bool		m_peerQPackDecoderClosed = false;
  bool		m_peerSettingsReceived = false;
  bool		m_goawaySent = false;
  bool		m_peerGoawayReceived = false;
  uint64_t	m_goawayID = 0;
  uint64_t	m_peerGoawayID = 0;
  mutable Diag	m_diag;
};

template <typename Impl, typename ZquicClient>
class Client {
public:
  Client(Impl *impl, ZquicClient *client, Params params = {}) :
    m_impl{impl}, m_client{client}, m_connection{ZuMv(params)} { }

  Connection &connection() { return m_connection; }

private:
  Impl		*m_impl = nullptr;
  ZquicClient	*m_client = nullptr;
  Connection	m_connection;
};

template <typename Impl, typename ZquicServer>
class Server {
public:
  Server(Impl *impl, ZquicServer *server, Params params = {}) :
    m_impl{impl}, m_server{server}, m_connection{ZuMv(params)} { }

  Connection &connection() { return m_connection; }

private:
  Impl		*m_impl = nullptr;
  ZquicServer	*m_server = nullptr;
  Connection	m_connection;
};

}} // namespace Zhttp::H3

#endif /* Zhttp3_HH */
