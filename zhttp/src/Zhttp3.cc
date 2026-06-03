//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/Zhttp3.hh>

namespace Zhttp { namespace H3 {

static uint64_t wireType_(H3FrameType::T t)
{
  if (t == H3FrameType::Data) return 0x00;
  if (t == H3FrameType::Headers) return 0x01;
  if (t == H3FrameType::CancelPush) return 0x03;
  if (t == H3FrameType::Settings) return 0x04;
  if (t == H3FrameType::PushPromise) return 0x05;
  if (t == H3FrameType::Goaway) return 0x07;
  if (t == H3FrameType::MaxPushID) return 0x0d;
  return uint64_t(-1);
}

static H3FrameType::T frameType_(uint64_t t)
{
  switch (t) {
    case 0x00: return H3FrameType::Data;
    case 0x01: return H3FrameType::Headers;
    case 0x03: return H3FrameType::CancelPush;
    case 0x04: return H3FrameType::Settings;
    case 0x05: return H3FrameType::PushPromise;
    case 0x07: return H3FrameType::Goaway;
    case 0x0d: return H3FrameType::MaxPushID;
    default: return H3FrameType::Unknown;
  }
}

static H3Setting::T setting_(uint64_t id)
{
  if (id == 0x01) return H3Setting::QPackMaxTableCapacity;
  if (id == 0x06) return H3Setting::MaxFieldSectionSize;
  if (id == 0x07) return H3Setting::QPackBlockedStreams;
  return H3Setting::Unknown;
}

static bool qpackEncoderStreamInstruction_(QPackInstruction::T type)
{
  return type == QPackInstruction::SetCapacity ||
    type == QPackInstruction::InsertWithNameRef ||
    type == QPackInstruction::InsertWithoutNameRef ||
    type == QPackInstruction::Duplicate;
}

static bool qpackDecoderStreamInstruction_(QPackInstruction::T type)
{
  return type == QPackInstruction::SectionAck ||
    type == QPackInstruction::StreamCancellation ||
    type == QPackInstruction::InsertCountIncrement;
}

ZuCSpan Diag::frameTypeName(H3FrameType::T type)
{
  if (type == H3FrameType::Data) return "data";
  if (type == H3FrameType::Headers) return "headers";
  if (type == H3FrameType::CancelPush) return "cancel_push";
  if (type == H3FrameType::Settings) return "settings";
  if (type == H3FrameType::PushPromise) return "push_promise";
  if (type == H3FrameType::Goaway) return "goaway";
  if (type == H3FrameType::MaxPushID) return "max_push_id";
  return "unknown";
}

ZuCSpan Diag::streamTypeName(H3StreamType::T type)
{
  if (type == H3StreamType::Control) return "control";
  if (type == H3StreamType::Push) return "push";
  if (type == H3StreamType::QPackEncoder) return "qpack_encoder";
  if (type == H3StreamType::QPackDecoder) return "qpack_decoder";
  return "unknown";
}

ZuCSpan Diag::settingName(H3Setting::T setting)
{
  if (setting == H3Setting::QPackMaxTableCapacity)
    return "qpack_max_table_capacity";
  if (setting == H3Setting::MaxFieldSectionSize)
    return "max_field_section_size";
  if (setting == H3Setting::QPackBlockedStreams)
    return "qpack_blocked_streams";
  return "unknown";
}

ZuCSpan Diag::messagePartName(MessagePart::T part)
{
  if (part == MessagePart::None) return "none";
  if (part == MessagePart::Headers) return "headers";
  if (part == MessagePart::Data) return "data";
  if (part == MessagePart::Trailers) return "trailers";
  return "unknown";
}

void Diag::notePayloadRx_(H3FrameType::T type, unsigned bytes)
{
  if (type == H3FrameType::Headers) noteHeaderRx(bytes);
  else if (type == H3FrameType::Data) noteBodyRx(bytes);
  else if (type == H3FrameType::Settings) ++controlFramesRx;
  else if (type == H3FrameType::Goaway) {
    ++controlFramesRx;
    ++goawayRx;
  }
}

void Diag::notePayloadTx_(H3FrameType::T type, unsigned bytes)
{
  if (type == H3FrameType::Headers) noteHeaderTx(bytes);
  else if (type == H3FrameType::Data) noteBodyTx(bytes);
  else if (type == H3FrameType::Settings) ++controlFramesTx;
  else if (type == H3FrameType::Goaway) {
    ++controlFramesTx;
    ++goawayTx;
  }
}

void Diag::frameSummary(const H3Frame &frame, DiagText &out)
{
  out.length(0);
  out << "type=" << frameTypeName(frame.type) <<
    " payload=" << frame.payload.length();
}

void Diag::fieldSectionSummary(ZuSpan<Header> fields, DiagText &out)
{
  unsigned bytes = 0;
  for (auto &field : fields) bytes += field.name.length() + field.value.length();
  out.length(0);
  out << "fields=" << fields.length() << " bytes=" << bytes;
  if (fields.length())
    out << " first=" << fields[0].name;
}

void Diag::summary(DiagText &out) const
{
  out.length(0);
  out << "framesRx=" << framesRx <<
    " framesTx=" << framesTx <<
    " bytesRx=" << bytesRx <<
    " bytesTx=" << bytesTx <<
    " headersRx=" << headersRx <<
    " headersTx=" << headersTx <<
    " headerBytesRx=" << headerBytesRx <<
    " headerBytesTx=" << headerBytesTx <<
    " dataFramesRx=" << dataFramesRx <<
    " dataFramesTx=" << dataFramesTx <<
    " bodyBytesRx=" << bodyBytesRx <<
    " bodyBytesTx=" << bodyBytesTx <<
    " controlFramesRx=" << controlFramesRx <<
    " controlFramesTx=" << controlFramesTx <<
    " goawayRx=" << goawayRx <<
    " goawayTx=" << goawayTx <<
    " qpackInstructionsRx=" << qpackInstructionsRx <<
    " qpackInstructionsTx=" << qpackInstructionsTx <<
    " errors=" << errors <<
    " lastError=" << ErrorCodec::name(lastError);
}

uint64_t ErrorCodec::code(H3Error::T error)
{
  if (error == H3Error::NoError) return 0x0100;
  if (error == H3Error::GeneralProtocol) return 0x0101;
  if (error == H3Error::Internal) return 0x0102;
  if (error == H3Error::StreamCreation) return 0x0103;
  if (error == H3Error::ClosedCriticalStream) return 0x0104;
  if (error == H3Error::FrameUnexpected) return 0x0105;
  if (error == H3Error::FrameError) return 0x0106;
  if (error == H3Error::ExcessiveLoad) return 0x0107;
  if (error == H3Error::IDError) return 0x0108;
  if (error == H3Error::SettingsError) return 0x0109;
  if (error == H3Error::MissingSettings) return 0x010a;
  if (error == H3Error::RequestRejected) return 0x010b;
  if (error == H3Error::RequestCancelled) return 0x010c;
  if (error == H3Error::RequestIncomplete) return 0x010d;
  if (error == H3Error::MessageError) return 0x010e;
  if (error == H3Error::ConnectError) return 0x010f;
  if (error == H3Error::VersionFallback) return 0x0110;
  if (error == H3Error::QPackDecompressionFailed) return 0x0200;
  if (error == H3Error::QPackEncoderStreamError) return 0x0201;
  if (error == H3Error::QPackDecoderStreamError) return 0x0202;
  return uint64_t(-1);
}

H3Error::T ErrorCodec::fromCode(uint64_t code)
{
  if (code == 0x0100) return H3Error::NoError;
  if (code == 0x0101) return H3Error::GeneralProtocol;
  if (code == 0x0102) return H3Error::Internal;
  if (code == 0x0103) return H3Error::StreamCreation;
  if (code == 0x0104) return H3Error::ClosedCriticalStream;
  if (code == 0x0105) return H3Error::FrameUnexpected;
  if (code == 0x0106) return H3Error::FrameError;
  if (code == 0x0107) return H3Error::ExcessiveLoad;
  if (code == 0x0108) return H3Error::IDError;
  if (code == 0x0109) return H3Error::SettingsError;
  if (code == 0x010a) return H3Error::MissingSettings;
  if (code == 0x010b) return H3Error::RequestRejected;
  if (code == 0x010c) return H3Error::RequestCancelled;
  if (code == 0x010d) return H3Error::RequestIncomplete;
  if (code == 0x010e) return H3Error::MessageError;
  if (code == 0x010f) return H3Error::ConnectError;
  if (code == 0x0110) return H3Error::VersionFallback;
  if (code == 0x0200) return H3Error::QPackDecompressionFailed;
  if (code == 0x0201) return H3Error::QPackEncoderStreamError;
  if (code == 0x0202) return H3Error::QPackDecoderStreamError;
  return H3Error::GeneralProtocol;
}

ZuCSpan ErrorCodec::name(H3Error::T error)
{
  if (error == H3Error::NoError) return "H3_NO_ERROR";
  if (error == H3Error::GeneralProtocol) return "H3_GENERAL_PROTOCOL_ERROR";
  if (error == H3Error::Internal) return "H3_INTERNAL_ERROR";
  if (error == H3Error::StreamCreation) return "H3_STREAM_CREATION_ERROR";
  if (error == H3Error::ClosedCriticalStream) return "H3_CLOSED_CRITICAL_STREAM";
  if (error == H3Error::FrameUnexpected) return "H3_FRAME_UNEXPECTED";
  if (error == H3Error::FrameError) return "H3_FRAME_ERROR";
  if (error == H3Error::ExcessiveLoad) return "H3_EXCESSIVE_LOAD";
  if (error == H3Error::IDError) return "H3_ID_ERROR";
  if (error == H3Error::SettingsError) return "H3_SETTINGS_ERROR";
  if (error == H3Error::MissingSettings) return "H3_MISSING_SETTINGS";
  if (error == H3Error::RequestRejected) return "H3_REQUEST_REJECTED";
  if (error == H3Error::RequestCancelled) return "H3_REQUEST_CANCELLED";
  if (error == H3Error::RequestIncomplete) return "H3_REQUEST_INCOMPLETE";
  if (error == H3Error::MessageError) return "H3_MESSAGE_ERROR";
  if (error == H3Error::ConnectError) return "H3_CONNECT_ERROR";
  if (error == H3Error::VersionFallback) return "H3_VERSION_FALLBACK";
  if (error == H3Error::QPackDecompressionFailed)
    return "QPACK_DECOMPRESSION_FAILED";
  if (error == H3Error::QPackEncoderStreamError)
    return "QPACK_ENCODER_STREAM_ERROR";
  if (error == H3Error::QPackDecoderStreamError)
    return "QPACK_DECODER_STREAM_ERROR";
  return "H3_UNKNOWN_ERROR";
}

static bool forbiddenSetting_(uint64_t id)
{
  return id >= 0x02 && id <= 0x05;
}

static int getVar_(ZuCSpan in, unsigned &o, uint64_t &v)
{
  unsigned n = 0;
  if (Zquic::VarInt::decode(
	ZuCSpan{in.data() + o, in.length() - o}, v, n) < 0)
    return -1;
  o += n;
  return 0;
}

static int putSetting_(HeaderBytes &out, uint64_t id, uint64_t value)
{
  if (CodecBytes::putVar(out, id) < 0 ||
      CodecBytes::putVar(out, value) < 0)
    return -1;
  return 0;
}

static void append_(HeaderBytes &out, const HeaderBytes &in)
{
  for (unsigned i = 0; i < in.length(); ++i) out.push(in.data()[i]);
}

static bool dynamicEligible_(const Params &params, Header h)
{
  return params.qpackTableCapacity() &&
    params.indexAllowed(h.name) &&
    !params.neverIndex(h.name) &&
    QPack::staticIndex(h.name, h.value) < 0;
}

static int encodeInsertInstruction_(HeaderBytes &out, Header h)
{
  uint64_t nameIndex = 0;
  if (QPack::staticNameIndex(h.name, nameIndex))
    return QPack::encodeInsertWithNameRef(out, nameIndex, false, h.value);
  return QPack::encodeInsertLiteral(out, h);
}

static int encodeHeaderBlockDynamic_(
  HeaderBytes &fields, HeaderBytes &encoderInstructions,
  ZuSpan<Header> headers, const Params &params, DynamicState &state,
  Diag &diag)
{
  fields.length(0);
  encoderInstructions.length(0);

  unsigned headerBytes = 0;
  for (auto &h : headers) {
    if (params.neverIndex(h.name) && params.qpackTableCapacity() &&
	params.indexAllowed(h.name))
      return -1;
    headerBytes += h.name.length() + h.value.length();
    if (headerBytes > params.maxHeaderListSize()) return -1;
    if (!dynamicEligible_(params, h) ||
	state.table().find(h.name, h.value) >= 0)
      continue;

    HeaderBytes instruction;
    if (encodeInsertInstruction_(instruction, h) < 0 || !state.insert(h))
      continue;
    append_(encoderInstructions, instruction);
    diag.noteQPackInstructionTx();
  }

  bool dynamicSeen = false;
  for (auto &h : headers)
    if (dynamicEligible_(params, h) &&
	state.table().find(h.name, h.value) >= 0) {
      dynamicSeen = true;
      break;
    }

  FieldSectionPrefix prefix;
  if (dynamicSeen) prefix.requiredInsertCount = state.insertCount();
  if (QPack::encodeFieldSectionPrefix(fields, prefix) < 0) return -1;

  for (auto &h : headers) {
    int dynamicIndex =
      dynamicEligible_(params, h) ? state.table().find(h.name, h.value) : -1;
    if (dynamicIndex >= 0) {
      if (QPack::encodeDynamicIndexed(
	  fields, uint64_t(dynamicIndex - 1)) < 0)
	return -1;
      continue;
    }
    if (QPack::encodeFieldLine(fields, h, params) < 0) return -1;
  }
  return fields.length();
}

int H3FrameCodec::write(H3FrameType::T type, ZuCSpan payload, HeaderBytes &out)
{
  out.length(0);
  uint64_t t = wireType_(type);
  if (t == uint64_t(-1)) return -1;
  if (CodecBytes::putVar(out, t) < 0 ||
      CodecBytes::putVar(out, payload.length()) < 0)
    return -1;
  CodecBytes::putSpan(out, payload);
  return out.length();
}

int H3FrameCodec::parse(ZuCSpan in, H3Frame &frame, unsigned &used)
{
  uint64_t t = 0, len = 0;
  unsigned n = 0, o = 0;
  if (Zquic::VarInt::decode(in, t, n) < 0) return -1;
  o += n;
  if (Zquic::VarInt::decode(ZuCSpan{in.data() + o, in.length() - o}, len, n) < 0)
    return -1;
  o += n;
  if (in.length() < o + len) return -1;
  frame.type = frameType_(t);
  frame.payload = ZuCSpan{in.data() + o, unsigned(len)};
  used = o + len;
  return 0;
}

int H3FrameCodec::writeSettingsPayload(
  const Settings &settings, HeaderBytes &out)
{
  out.length(0);
  if (settings.hasQPackMaxTableCapacity &&
      putSetting_(out, 0x01, settings.qpackMaxTableCapacity) < 0)
    return -1;
  if (settings.hasMaxFieldSectionSize &&
      putSetting_(out, 0x06, settings.maxFieldSectionSize) < 0)
    return -1;
  if (settings.hasQPackBlockedStreams &&
      putSetting_(out, 0x07, settings.qpackBlockedStreams) < 0)
    return -1;
  return out.length();
}

int H3FrameCodec::writeSettings(const Settings &settings, HeaderBytes &out)
{
  HeaderBytes payload;
  if (writeSettingsPayload(settings, payload) < 0) return -1;
  return write(H3FrameType::Settings,
    ZuCSpan{reinterpret_cast<const char *>(payload.data()), payload.length()},
    out);
}

int H3FrameCodec::parseSettings(ZuCSpan in, Settings &settings)
{
  settings = {};
  unsigned o = 0;
  while (o < in.length()) {
    uint64_t id = 0, value = 0;
    if (getVar_(in, o, id) < 0 || getVar_(in, o, value) < 0)
      return -1;
    if (forbiddenSetting_(id)) return -1;

    H3Setting::T setting = setting_(id);
    if (setting == H3Setting::QPackMaxTableCapacity) {
      if (settings.hasQPackMaxTableCapacity) return -1;
      settings.hasQPackMaxTableCapacity = true;
      settings.qpackMaxTableCapacity = value;
    } else if (setting == H3Setting::MaxFieldSectionSize) {
      if (settings.hasMaxFieldSectionSize) return -1;
      settings.hasMaxFieldSectionSize = true;
      settings.maxFieldSectionSize = value;
    } else if (setting == H3Setting::QPackBlockedStreams) {
      if (settings.hasQPackBlockedStreams) return -1;
      settings.hasQPackBlockedStreams = true;
      settings.qpackBlockedStreams = value;
    }
  }
  return int(o);
}

bool MessageStream::receiveFrame(const H3Frame &frame, bool fin)
{
  if (m_cancelled) {
    m_error = H3Error::RequestCancelled;
    return false;
  }
  if (m_closed) {
    m_error = H3Error::FrameUnexpected;
    return false;
  }

  if (frame.type == H3FrameType::Unknown) {
    if (fin) m_closed = true;
    return true;
  }

  if (frame.type == H3FrameType::Headers) {
    if (!m_headersReceived) {
      m_headersReceived = true;
      m_lastPart = MessagePart::Headers;
      if (fin) m_closed = true;
      return true;
    }
    if (m_trailersReceived) {
      m_error = H3Error::FrameUnexpected;
      return false;
    }
    m_trailersReceived = true;
    m_closed = true;
    m_lastPart = MessagePart::Trailers;
    return true;
  }

  if (frame.type == H3FrameType::Data) {
    if (!m_headersReceived || m_trailersReceived) {
      m_error = H3Error::FrameUnexpected;
      return false;
    }
    if (m_bodyBytes > uint64_t(-1) - frame.payload.length()) {
      m_error = H3Error::ExcessiveLoad;
      return false;
    }
    m_dataReceived = true;
    ++m_dataFrames;
    m_bodyBytes += frame.payload.length();
    m_lastPart = MessagePart::Data;
    if (fin) m_closed = true;
    return true;
  }

  m_error = H3Error::FrameUnexpected;
  return false;
}

void MessageStream::cancel()
{
  cancel(ErrorCodec::code(H3Error::RequestCancelled));
}

void MessageStream::cancel(uint64_t appError)
{
  m_cancelled = true;
  m_closed = true;
  m_error = H3Error::RequestCancelled;
  m_cancelCode = appError;
}

bool Connection::acceptPeerStream(H3StreamType::T type)
{
  if (type == H3StreamType::Control) {
    if (m_peerControlOpened) return false;
    m_peerControlOpened = true;
    return true;
  }
  if (type == H3StreamType::QPackEncoder) {
    if (m_peerQPackEncoderOpened) return false;
    m_peerQPackEncoderOpened = true;
    return true;
  }
  if (type == H3StreamType::QPackDecoder) {
    if (m_peerQPackDecoderOpened) return false;
    m_peerQPackDecoderOpened = true;
    return true;
  }
  return false;
}

bool Connection::acceptRequestStream(uint64_t id) const
{
  if (!Zquic::StreamID::client(id) || !Zquic::StreamID::bidi(id))
    return false;
  return !m_peerGoawayReceived || id < m_peerGoawayID;
}

H3Error::T Connection::peerStreamClosed(H3StreamType::T type)
{
  if (type == H3StreamType::Control) {
    m_peerControlClosed = true;
    return H3Error::ClosedCriticalStream;
  }
  if (type == H3StreamType::QPackEncoder) {
    m_peerQPackEncoderClosed = true;
    return H3Error::QPackEncoderStreamError;
  }
  if (type == H3StreamType::QPackDecoder) {
    m_peerQPackDecoderClosed = true;
    return H3Error::QPackDecoderStreamError;
  }
  return H3Error::GeneralProtocol;
}

bool Connection::receiveControlFrame(const H3Frame &frame)
{
  m_diag.noteFrameRx(frame);
  if (!m_peerSettingsReceived) {
    if (frame.type != H3FrameType::Settings) {
      m_diag.noteError(H3Error::MissingSettings);
      return false;
    }
    Settings settings;
    if (H3FrameCodec::parseSettings(frame.payload, settings) < 0) {
      m_diag.noteError(H3Error::SettingsError);
      return false;
    }
    m_peerSettings = settings;
    m_peerSettingsReceived = true;
    return true;
  }
  if (frame.type == H3FrameType::Settings ||
      frame.type == H3FrameType::Data ||
      frame.type == H3FrameType::Headers ||
      frame.type == H3FrameType::CancelPush ||
      frame.type == H3FrameType::PushPromise ||
      frame.type == H3FrameType::MaxPushID) {
    m_diag.noteError(H3Error::FrameUnexpected);
    return false;
  }
  if (frame.type == H3FrameType::Goaway) {
    uint64_t id = 0;
    unsigned n = 0;
    if (Zquic::VarInt::decode(frame.payload, id, n) < 0 ||
	n != frame.payload.length()) {
      m_diag.noteError(H3Error::FrameError);
      return false;
    }
    if (m_peerGoawayReceived && id > m_peerGoawayID) {
      m_diag.noteError(H3Error::IDError);
      return false;
    }
    m_peerGoawayReceived = true;
    m_peerGoawayID = id;
  }
  return true;
}

Settings Connection::localSettings() const
{
  Settings settings;
  settings.hasQPackMaxTableCapacity = true;
  settings.qpackMaxTableCapacity = m_params.qpackTableCapacity();
  settings.hasMaxFieldSectionSize = true;
  settings.maxFieldSectionSize = m_params.maxHeaderListSize();
  settings.hasQPackBlockedStreams = true;
  settings.qpackBlockedStreams = m_params.qpackBlockedStreams();
  return settings;
}

int Connection::encodeSettings(HeaderBytes &out) const
{
  HeaderBytes payload;
  if (H3FrameCodec::writeSettingsPayload(localSettings(), payload) < 0) {
    m_diag.noteError(H3Error::SettingsError);
    return -1;
  }
  int n = H3FrameCodec::write(
    H3FrameType::Settings,
    ZuCSpan{reinterpret_cast<const char *>(payload.data()), payload.length()},
    out);
  if (n >= 0)
    m_diag.noteFrameTx(H3FrameType::Settings, payload.length());
  else
    m_diag.noteError(H3Error::FrameError);
  return n;
}

int Connection::encodeGoaway(HeaderBytes &out, uint64_t id)
{
  HeaderBytes payload;
  if (CodecBytes::putVar(payload, id) < 0) return -1;
  goaway(id);
  int n = H3FrameCodec::write(
    H3FrameType::Goaway,
    ZuCSpan{reinterpret_cast<const char *>(payload.data()), payload.length()},
    out);
  if (n >= 0)
    m_diag.noteFrameTx(H3FrameType::Goaway, payload.length());
  else
    m_diag.noteError(H3Error::FrameError);
  return n;
}

int Connection::encodeRequest(HeaderBytes &out, const RequestParams &params)
{
  Header pseudo[] = {
    { ":method", Method::name(params.method) },
    { ":scheme", params.scheme },
    { ":authority", params.authority },
    { ":path", params.path }
  };
  Headers headers;
  for (auto &h : pseudo) headers.push(h);
  for (auto &h : params.headers) headers.push(h);
  if (!validateRequest(ZuSpan<Header>{headers.data(), headers.length()})) {
    m_diag.noteError(H3Error::MessageError);
    return -1;
  }
  HeaderBytes fields;
  if (QPack::encodeLiteral(
	fields, ZuSpan<Header>{headers.data(), headers.length()}, m_params) < 0) {
    m_diag.noteError(H3Error::MessageError);
    return -1;
  }
  int n = H3FrameCodec::write(
    H3FrameType::Headers,
    ZuCSpan{reinterpret_cast<const char *>(fields.data()), fields.length()},
    out);
  if (n >= 0)
    m_diag.noteFrameTx(H3FrameType::Headers, fields.length());
  else
    m_diag.noteError(H3Error::FrameError);
  return n;
}

int Connection::encodeRequestDynamic(
  HeaderBytes &out, HeaderBytes &encoderInstructions,
  const RequestParams &params)
{
  Header pseudo[] = {
    { ":method", Method::name(params.method) },
    { ":scheme", params.scheme },
    { ":authority", params.authority },
    { ":path", params.path }
  };
  Headers headers;
  for (auto &h : pseudo) headers.push(h);
  for (auto &h : params.headers) headers.push(h);
  if (!validateRequest(ZuSpan<Header>{headers.data(), headers.length()})) {
    m_diag.noteError(H3Error::MessageError);
    return -1;
  }
  HeaderBytes fields;
  if (encodeHeaderBlockDynamic_(
	fields, encoderInstructions,
	ZuSpan<Header>{headers.data(), headers.length()},
	m_params, m_qpackEncoder, m_diag) < 0) {
    m_diag.noteError(H3Error::MessageError);
    return -1;
  }
  int n = H3FrameCodec::write(
    H3FrameType::Headers,
    ZuCSpan{reinterpret_cast<const char *>(fields.data()), fields.length()},
    out);
  if (n >= 0)
    m_diag.noteFrameTx(H3FrameType::Headers, fields.length());
  else
    m_diag.noteError(H3Error::FrameError);
  return n;
}

static bool status_(ZuCSpan v)
{
  return v.length() == 3 &&
    v[0] >= '0' && v[0] <= '9' &&
    v[1] >= '0' && v[1] <= '9' &&
    v[2] >= '0' && v[2] <= '9';
}

static unsigned statusValue_(ZuCSpan v)
{
  if (!status_(v)) return 0;
  return
    unsigned(v[0] - '0') * 100U +
    unsigned(v[1] - '0') * 10U +
    unsigned(v[2] - '0');
}

bool Connection::validateRequest(ZuSpan<Header> headers)
{
  bool method = false, scheme = false, authority = false, path = false;
  bool regular = false;
  for (auto &h : headers) {
    bool pseudo = h.name.length() && h.name[0] == ':';
    if (pseudo && regular) return false;
    if (!pseudo) { regular = true; continue; }
    if (h.name == ":method") {
      if (method || !h.value.length()) return false;
      method = true;
    } else if (h.name == ":scheme") {
      if (scheme || !h.value.length()) return false;
      scheme = true;
    } else if (h.name == ":authority") {
      if (authority) return false;
      authority = true;
    } else if (h.name == ":path") {
      if (path || !h.value.length()) return false;
      path = true;
    } else
      return false;
  }
  return method && scheme && authority && path;
}

bool Connection::validateResponse(ZuSpan<Header> headers)
{
  bool status = false;
  bool regular = false;
  for (auto &h : headers) {
    bool pseudo = h.name.length() && h.name[0] == ':';
    if (pseudo && regular) return false;
    if (!pseudo) { regular = true; continue; }
    unsigned v = statusValue_(h.value);
    if (h.name != ":status" || status || v < 100 || v > 999)
      return false;
    status = true;
  }
  return status;
}

bool Connection::validateTrailers(ZuSpan<Header> headers)
{
  for (auto &h : headers)
    if (h.name.length() && h.name[0] == ':') return false;
  return true;
}

int Connection::encodeResponse(HeaderBytes &out, const ResponseParams &params)
{
  if (params.status < 100 || params.status > 999) {
    m_diag.noteError(H3Error::MessageError);
    return -1;
  }
  char status[4] = {
    char('0' + ((params.status / 100) % 10)),
    char('0' + ((params.status / 10) % 10)),
    char('0' + (params.status % 10)),
    0
  };
  Header pseudo[] = { { ":status", ZuCSpan{status, 3} } };
  Headers headers;
  for (auto &h : pseudo) headers.push(h);
  for (auto &h : params.headers) headers.push(h);
  if (!validateResponse(ZuSpan<Header>{headers.data(), headers.length()})) {
    m_diag.noteError(H3Error::MessageError);
    return -1;
  }
  HeaderBytes fields;
  if (QPack::encodeLiteral(
	fields, ZuSpan<Header>{headers.data(), headers.length()}, m_params) < 0) {
    m_diag.noteError(H3Error::MessageError);
    return -1;
  }
  int n = H3FrameCodec::write(
    H3FrameType::Headers,
    ZuCSpan{reinterpret_cast<const char *>(fields.data()), fields.length()},
    out);
  if (n >= 0)
    m_diag.noteFrameTx(H3FrameType::Headers, fields.length());
  else
    m_diag.noteError(H3Error::FrameError);
  return n;
}

int Connection::encodeResponseDynamic(
  HeaderBytes &out, HeaderBytes &encoderInstructions,
  const ResponseParams &params)
{
  if (params.status < 100 || params.status > 999) {
    m_diag.noteError(H3Error::MessageError);
    return -1;
  }
  char status[4] = {
    char('0' + ((params.status / 100) % 10)),
    char('0' + ((params.status / 10) % 10)),
    char('0' + (params.status % 10)),
    0
  };
  Header pseudo[] = { { ":status", ZuCSpan{status, 3} } };
  Headers headers;
  for (auto &h : pseudo) headers.push(h);
  for (auto &h : params.headers) headers.push(h);
  if (!validateResponse(ZuSpan<Header>{headers.data(), headers.length()})) {
    m_diag.noteError(H3Error::MessageError);
    return -1;
  }
  HeaderBytes fields;
  if (encodeHeaderBlockDynamic_(
	fields, encoderInstructions,
	ZuSpan<Header>{headers.data(), headers.length()},
	m_params, m_qpackEncoder, m_diag) < 0) {
    m_diag.noteError(H3Error::MessageError);
    return -1;
  }
  int n = H3FrameCodec::write(
    H3FrameType::Headers,
    ZuCSpan{reinterpret_cast<const char *>(fields.data()), fields.length()},
    out);
  if (n >= 0)
    m_diag.noteFrameTx(H3FrameType::Headers, fields.length());
  else
    m_diag.noteError(H3Error::FrameError);
  return n;
}

int Connection::encodeTrailers(HeaderBytes &out, ZuSpan<Header> headers)
{
  if (!validateTrailers(headers)) {
    m_diag.noteError(H3Error::MessageError);
    return -1;
  }
  HeaderBytes fields;
  if (QPack::encodeLiteral(fields, headers, m_params) < 0) {
    m_diag.noteError(H3Error::MessageError);
    return -1;
  }
  int n = H3FrameCodec::write(
    H3FrameType::Headers,
    ZuCSpan{reinterpret_cast<const char *>(fields.data()), fields.length()},
    out);
  if (n >= 0)
    m_diag.noteFrameTx(H3FrameType::Headers, fields.length());
  else
    m_diag.noteError(H3Error::FrameError);
  return n;
}

int Connection::encodeTrailersDynamic(
  HeaderBytes &out, HeaderBytes &encoderInstructions, ZuSpan<Header> headers)
{
  if (!validateTrailers(headers)) {
    m_diag.noteError(H3Error::MessageError);
    return -1;
  }
  HeaderBytes fields;
  if (encodeHeaderBlockDynamic_(
	fields, encoderInstructions, headers,
	m_params, m_qpackEncoder, m_diag) < 0) {
    m_diag.noteError(H3Error::MessageError);
    return -1;
  }
  int n = H3FrameCodec::write(
    H3FrameType::Headers,
    ZuCSpan{reinterpret_cast<const char *>(fields.data()), fields.length()},
    out);
  if (n >= 0)
    m_diag.noteFrameTx(H3FrameType::Headers, fields.length());
  else
    m_diag.noteError(H3Error::FrameError);
  return n;
}

int Connection::encodeData(HeaderBytes &out, ZuCSpan data) const
{
  int n = H3FrameCodec::write(H3FrameType::Data, data, out);
  if (n >= 0)
    m_diag.noteFrameTx(H3FrameType::Data, data.length());
  else
    m_diag.noteError(H3Error::FrameError);
  return n;
}

int Connection::decodeData(const H3Frame &frame, ZuCSpan &data) const
{
  data = {};
  m_diag.noteFrameRx(frame);
  if (frame.type != H3FrameType::Data) {
    m_diag.noteError(H3Error::FrameUnexpected);
    return -1;
  }
  data = frame.payload;
  return int(frame.payload.length());
}

static void prepareDecodedStorage_(HeaderBytes &storage, unsigned maxHeaderBytes)
{
  storage.length(0);
  storage.size(maxHeaderBytes ? maxHeaderBytes : 1);
}

static ZuCSpan saveDecodedSpan_(HeaderBytes &storage, ZuCSpan s)
{
  unsigned o = storage.length();
  for (unsigned i = 0; i < s.length(); ++i) storage.push(uint8_t(s[i]));
  return ZuCSpan{
    reinterpret_cast<const char *>(storage.data() + o), s.length()};
}

static Header saveDecodedHeader_(HeaderBytes &storage, Header h)
{
  ZuCSpan name = saveDecodedSpan_(storage, h.name);
  ZuCSpan value = saveDecodedSpan_(storage, h.value);
  return Header{name, value};
}

int Connection::decodeRequest(
  const H3Frame &frame, DecodedRequest &out, uint64_t insertCount) const
{
  m_diag.noteFrameRx(frame);
  if (frame.type != H3FrameType::Headers) {
    m_diag.noteError(H3Error::FrameUnexpected);
    return -1;
  }
  out = {};
  prepareDecodedStorage_(out.storage, m_params.maxHeaderListSize());

  Headers headers;
  int n = QPack::decodeLiteral(
    frame.payload,
    [&headers, &out](Header h) {
      headers.push(saveDecodedHeader_(out.storage, h));
    },
    m_params, insertCount);
  if (n < 0 || n != int(frame.payload.length()) ||
      !validateRequest(ZuSpan<Header>{headers.data(), headers.length()})) {
    m_diag.noteError(H3Error::QPackDecompressionFailed);
    return -1;
  }

  for (auto &h : headers) {
    if (h.name == ":method") {
      out.method = Method::lookup(h.value);
      if (out.method < 0) {
	m_diag.noteError(H3Error::MessageError);
	return -1;
      }
    } else if (h.name == ":scheme")
      out.scheme = h.value;
    else if (h.name == ":authority")
      out.authority = h.value;
    else if (h.name == ":path")
      out.path = h.value;
    else
      out.headers.push(h);
  }
  return n;
}

int Connection::decodeRequestDynamic(
  const H3Frame &frame, DecodedRequest &out) const
{
  m_diag.noteFrameRx(frame);
  if (frame.type != H3FrameType::Headers) {
    m_diag.noteError(H3Error::FrameUnexpected);
    return -1;
  }
  out = {};
  prepareDecodedStorage_(out.storage, m_params.maxHeaderListSize());

  Headers headers;
  int n = decodeLiteralDynamic(
    frame.payload, m_qpackDecoder.table(),
    [&headers, &out](Header h) {
      headers.push(saveDecodedHeader_(out.storage, h));
    },
    m_params, m_qpackDecoder.insertCount());
  if (n < 0 || n != int(frame.payload.length()) ||
      !validateRequest(ZuSpan<Header>{headers.data(), headers.length()})) {
    m_diag.noteError(H3Error::QPackDecompressionFailed);
    return -1;
  }

  for (auto &h : headers) {
    if (h.name == ":method") {
      out.method = Method::lookup(h.value);
      if (out.method < 0) {
	m_diag.noteError(H3Error::MessageError);
	return -1;
      }
    } else if (h.name == ":scheme")
      out.scheme = h.value;
    else if (h.name == ":authority")
      out.authority = h.value;
    else if (h.name == ":path")
      out.path = h.value;
    else
      out.headers.push(h);
  }
  return n;
}

int Connection::decodeResponse(
  const H3Frame &frame, DecodedResponse &out, uint64_t insertCount) const
{
  m_diag.noteFrameRx(frame);
  if (frame.type != H3FrameType::Headers) {
    m_diag.noteError(H3Error::FrameUnexpected);
    return -1;
  }
  out = {};
  prepareDecodedStorage_(out.storage, m_params.maxHeaderListSize());

  Headers headers;
  int n = QPack::decodeLiteral(
    frame.payload,
    [&headers, &out](Header h) {
      headers.push(saveDecodedHeader_(out.storage, h));
    },
    m_params, insertCount);
  if (n < 0 || n != int(frame.payload.length()) ||
      !validateResponse(ZuSpan<Header>{headers.data(), headers.length()})) {
    m_diag.noteError(H3Error::QPackDecompressionFailed);
    return -1;
  }

  for (auto &h : headers) {
    if (h.name == ":status") {
      out.status = statusValue_(h.value);
      if (out.status < 100 || out.status > 999) {
	m_diag.noteError(H3Error::MessageError);
	return -1;
      }
    } else
      out.headers.push(h);
  }
  return n;
}

int Connection::decodeResponseDynamic(
  const H3Frame &frame, DecodedResponse &out) const
{
  m_diag.noteFrameRx(frame);
  if (frame.type != H3FrameType::Headers) {
    m_diag.noteError(H3Error::FrameUnexpected);
    return -1;
  }
  out = {};
  prepareDecodedStorage_(out.storage, m_params.maxHeaderListSize());

  Headers headers;
  int n = decodeLiteralDynamic(
    frame.payload, m_qpackDecoder.table(),
    [&headers, &out](Header h) {
      headers.push(saveDecodedHeader_(out.storage, h));
    },
    m_params, m_qpackDecoder.insertCount());
  if (n < 0 || n != int(frame.payload.length()) ||
      !validateResponse(ZuSpan<Header>{headers.data(), headers.length()})) {
    m_diag.noteError(H3Error::QPackDecompressionFailed);
    return -1;
  }

  for (auto &h : headers) {
    if (h.name == ":status")
      out.status = statusValue_(h.value);
    else
      out.headers.push(h);
  }
  return n;
}

int Connection::decodeTrailers(
  const H3Frame &frame, DecodedTrailers &out, uint64_t insertCount) const
{
  m_diag.noteFrameRx(frame);
  if (frame.type != H3FrameType::Headers) {
    m_diag.noteError(H3Error::FrameUnexpected);
    return -1;
  }
  out = {};
  prepareDecodedStorage_(out.storage, m_params.maxHeaderListSize());

  Headers headers;
  int n = QPack::decodeLiteral(
    frame.payload,
    [&headers, &out](Header h) {
      headers.push(saveDecodedHeader_(out.storage, h));
    },
    m_params, insertCount);
  if (n < 0 || n != int(frame.payload.length()) ||
      !validateTrailers(ZuSpan<Header>{headers.data(), headers.length()})) {
    m_diag.noteError(H3Error::QPackDecompressionFailed);
    return -1;
  }

  for (auto &h : headers) out.headers.push(h);
  return n;
}

int Connection::decodeTrailersDynamic(
  const H3Frame &frame, DecodedTrailers &out) const
{
  m_diag.noteFrameRx(frame);
  if (frame.type != H3FrameType::Headers) {
    m_diag.noteError(H3Error::FrameUnexpected);
    return -1;
  }
  out = {};
  prepareDecodedStorage_(out.storage, m_params.maxHeaderListSize());

  Headers headers;
  int n = decodeLiteralDynamic(
    frame.payload, m_qpackDecoder.table(),
    [&headers, &out](Header h) {
      headers.push(saveDecodedHeader_(out.storage, h));
    },
    m_params, m_qpackDecoder.insertCount());
  if (n < 0 || n != int(frame.payload.length()) ||
      !validateTrailers(ZuSpan<Header>{headers.data(), headers.length()})) {
    m_diag.noteError(H3Error::QPackDecompressionFailed);
    return -1;
  }
  for (auto &h : headers) out.headers.push(h);
  return n;
}

int Connection::receiveQPackEncoderStream(ZuCSpan in)
{
  unsigned o = 0;
  while (o < in.length()) {
    QPackDecodedInstruction instruction;
    int n = QPack::decodeInstructionOne(
      ZuCSpan{in.data() + o, in.length() - o}, instruction);
    if (n <= 0) {
      m_diag.noteError(H3Error::QPackEncoderStreamError);
      return -1;
    }
    if (!applyQPackEncoderInstruction(instruction)) return -1;
    o += unsigned(n);
  }
  return int(o);
}

int Connection::receiveQPackDecoderStream(ZuCSpan in)
{
  unsigned o = 0;
  while (o < in.length()) {
    QPackDecodedInstruction instruction;
    int n = QPack::decodeInstructionOne(
      ZuCSpan{in.data() + o, in.length() - o}, instruction);
    if (n <= 0) {
      m_diag.noteError(H3Error::QPackDecoderStreamError);
      return -1;
    }
    if (!applyQPackDecoderInstruction(instruction)) return -1;
    o += unsigned(n);
  }
  return int(o);
}

bool Connection::applyQPackEncoderInstruction(
  const QPackDecodedInstruction &instruction)
{
  if (!qpackEncoderStreamInstruction_(instruction.type)) {
    m_diag.noteError(H3Error::QPackEncoderStreamError);
    return false;
  }
  if (!m_qpackDecoder.applyInstruction(instruction)) {
    m_diag.noteError(H3Error::QPackEncoderStreamError);
    return false;
  }
  m_diag.noteQPackInstructionRx();
  return true;
}

bool Connection::applyQPackDecoderInstruction(
  const QPackDecodedInstruction &instruction)
{
  if (!qpackDecoderStreamInstruction_(instruction.type)) {
    m_diag.noteError(H3Error::QPackDecoderStreamError);
    return false;
  }
  if (!m_qpackEncoder.applyInstruction(instruction)) {
    m_diag.noteError(H3Error::QPackDecoderStreamError);
    return false;
  }
  m_diag.noteQPackInstructionRx();
  return true;
}

}} // namespace Zhttp::H3
