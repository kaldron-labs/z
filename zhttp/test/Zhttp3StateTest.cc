//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zhttp3.hh>

using namespace ZuTestUtil;

namespace {

using TestStreamBufAlloc = Zquic::StreamBufAlloc<>;

struct TestStream :
    public Zquic::Stream<TestStream, TestStreamBufAlloc, TestStreamBufAlloc> {
  using Base = Zquic::Stream<TestStream, TestStreamBufAlloc,
    TestStreamBufAlloc>;
  using Base::Base;

  int process(Zquic::RxStream &) { return 0; }
};

void putValue(Zhttp::H3::HeaderBytes &out, ZuCSpan value)
{
  out.push(uint8_t(value.length()));
  for (unsigned i = 0; i < value.length(); ++i)
    out.push(uint8_t(value[i]));
}

void appendBytes(
  Zhttp::H3::HeaderBytes &out, const Zhttp::H3::HeaderBytes &in)
{
  for (unsigned i = 0; i < in.length(); ++i) out.push(in.data()[i]);
}

} // namespace

void testCriticalStreams()
{
  ZuTestScope(testCriticalStreams);

  Zhttp::H3::Connection h3;
  ZuCHECK(h3.acceptPeerStream(Zhttp::H3::H3StreamType::Control),
    "peer control stream rejected");
  ZuCHECK(!h3.acceptPeerStream(Zhttp::H3::H3StreamType::Control),
    "duplicate peer control stream accepted");
  ZuCHECK(h3.acceptPeerStream(Zhttp::H3::H3StreamType::QPackEncoder),
    "peer QPACK encoder rejected");
  ZuCHECK(h3.acceptPeerStream(Zhttp::H3::H3StreamType::QPackDecoder),
    "peer QPACK decoder rejected");
  h3.goaway(7);
  ZuCHECK(h3.goawaySent() && h3.goawayID() == 7, "GOAWAY state mismatch");
  ZuCHECK(h3.peerStreamClosed(Zhttp::H3::H3StreamType::Control) ==
      Zhttp::H3::H3Error::ClosedCriticalStream &&
      h3.peerControlClosed(),
    "control stream closure error mismatch");
  ZuCHECK(h3.peerStreamClosed(Zhttp::H3::H3StreamType::QPackEncoder) ==
      Zhttp::H3::H3Error::QPackEncoderStreamError &&
      h3.peerQPackEncoderClosed(),
    "QPACK encoder stream closure error mismatch");
  ZuCHECK(h3.peerStreamClosed(Zhttp::H3::H3StreamType::QPackDecoder) ==
      Zhttp::H3::H3Error::QPackDecoderStreamError &&
      h3.peerQPackDecoderClosed(),
    "QPACK decoder stream closure error mismatch");
}

void testPseudoHeaderValidation()
{
  ZuTestScope(testPseudoHeaderValidation);

  Zhttp::H3::Header req[] = {
    { ":method", "GET" },
    { ":scheme", "https" },
    { ":authority", "localhost" },
    { ":path", "/v1" },
    { "accept", "application/json" }
  };
  ZuCHECK(Zhttp::H3::Connection::validateRequest(
    ZuSpan<Zhttp::H3::Header>{req, 5}), "valid request rejected");
  Zhttp::H3::Header badReq[] = {
    { "accept", "application/json" },
    { ":path", "/late" }
  };
  ZuCHECK(!Zhttp::H3::Connection::validateRequest(
    ZuSpan<Zhttp::H3::Header>{badReq, 2}), "late pseudo-header accepted");

  Zhttp::H3::Header resp[] = {
    { ":status", "204" },
    { "server", "z" }
  };
  ZuCHECK(Zhttp::H3::Connection::validateResponse(
    ZuSpan<Zhttp::H3::Header>{resp, 2}), "valid response rejected");
  Zhttp::H3::Header badResp[] = { { ":status", "20x" } };
  ZuCHECK(!Zhttp::H3::Connection::validateResponse(
    ZuSpan<Zhttp::H3::Header>{badResp, 1}), "bad response status accepted");
  Zhttp::H3::Header badRespRange[] = { { ":status", "000" } };
  ZuCHECK(!Zhttp::H3::Connection::validateResponse(
    ZuSpan<Zhttp::H3::Header>{badRespRange, 1}),
    "out-of-range response status accepted");
}

void testRequestStreamAdmission()
{
  ZuTestScope(testRequestStreamAdmission);

  Zhttp::H3::Connection h3;
  ZuCHECK(h3.acceptRequestStream(0) &&
      h3.acceptRequestStream(4) &&
      h3.acceptRequestStream(8),
    "client bidi request streams were rejected");
  ZuCHECK(!h3.acceptRequestStream(1) &&
      !h3.acceptRequestStream(2) &&
      !h3.acceptRequestStream(3),
    "non-request stream IDs were accepted as requests");

  Zhttp::H3::H3Frame settings;
  settings.type = Zhttp::H3::H3FrameType::Settings;
  ZuCHECK(h3.receiveControlFrame(settings),
    "request admission test SETTINGS setup failed");

  Zhttp::H3::Connection peer;
  Zhttp::H3::HeaderBytes goaway;
  ZuCHECK(peer.encodeGoaway(goaway, 8) > 0,
    "request admission GOAWAY encode failed");
  Zhttp::H3::H3Frame frame;
  unsigned used = 0;
  ZuCSpan goawaySpan{
    reinterpret_cast<const char *>(goaway.data()), goaway.length()};
  ZuCHECK(!Zhttp::H3::H3FrameCodec::parse(goawaySpan, frame, used) &&
      h3.receiveControlFrame(frame) &&
      h3.peerGoawayReceived(),
    "request admission GOAWAY receive failed");
  ZuCHECK(h3.acceptRequestStream(4) &&
      !h3.acceptRequestStream(8) &&
      !h3.acceptRequestStream(12),
    "GOAWAY request stream cutoff mismatch");
}

void testUnknownFrameSkip()
{
  ZuTestScope(testUnknownFrameSkip);

  uint8_t frame[] = { 0x21, 0x03, 'a', 'b', 'c' };
  Zhttp::H3::H3Frame parsed;
  unsigned used = 0;
  ZuCHECK(!Zhttp::H3::H3FrameCodec::parse(
    ZuCSpan{reinterpret_cast<const char *>(frame), sizeof(frame)},
    parsed, used), "unknown H3 frame parse failed");
  ZuCHECK(parsed.type == Zhttp::H3::H3FrameType::Unknown &&
    parsed.payload == "abc" && used == sizeof(frame),
    "unknown H3 frame skip mismatch");
}

void testControlFrameState()
{
  ZuTestScope(testControlFrameState);

  Zhttp::H3::Connection h3;
  Zhttp::H3::H3Frame frame;
  frame.type = Zhttp::H3::H3FrameType::Data;
  ZuCHECK(!h3.receiveControlFrame(frame),
    "control stream accepted DATA before SETTINGS");
  frame.type = Zhttp::H3::H3FrameType::Settings;
  ZuCHECK(h3.receiveControlFrame(frame) && h3.peerSettingsReceived(),
    "control stream rejected initial SETTINGS");
  ZuCHECK(!h3.receiveControlFrame(frame),
    "control stream accepted duplicate SETTINGS");
  frame.type = Zhttp::H3::H3FrameType::Headers;
  ZuCHECK(!h3.receiveControlFrame(frame),
    "control stream accepted HEADERS");
  frame.type = Zhttp::H3::H3FrameType::CancelPush;
  ZuCHECK(!h3.receiveControlFrame(frame),
    "control stream accepted CANCEL_PUSH while push is disabled");
  frame.type = Zhttp::H3::H3FrameType::PushPromise;
  ZuCHECK(!h3.receiveControlFrame(frame),
    "control stream accepted PUSH_PROMISE while push is disabled");
  frame.type = Zhttp::H3::H3FrameType::MaxPushID;
  ZuCHECK(!h3.receiveControlFrame(frame),
    "control stream accepted MAX_PUSH_ID while push is disabled");
  frame.type = Zhttp::H3::H3FrameType::Unknown;
  frame.payload = "abc";
  ZuCHECK(h3.receiveControlFrame(frame),
    "control stream rejected unknown frame after SETTINGS");

  Zhttp::H3::Connection local;
  Zhttp::H3::HeaderBytes goaway;
  ZuCHECK(local.encodeGoaway(goaway, 11) > 0 &&
      local.goawaySent() &&
      local.goawayID() == 11,
    "GOAWAY frame encode failed");
  Zhttp::H3::H3Frame goawayFrame;
  unsigned goawayUsed = 0;
  ZuCSpan goawaySpan{
    reinterpret_cast<const char *>(goaway.data()), goaway.length()};
  ZuCHECK(!Zhttp::H3::H3FrameCodec::parse(
      goawaySpan, goawayFrame, goawayUsed) &&
      goawayFrame.type == Zhttp::H3::H3FrameType::Goaway &&
      goawayUsed == goaway.length(),
    "GOAWAY frame parse failed");
  ZuCHECK(h3.receiveControlFrame(goawayFrame) &&
      h3.peerGoawayReceived() &&
      h3.peerGoawayID() == 11,
    "control stream GOAWAY state mismatch");
}

void testGoawaySequence()
{
  ZuTestScope(testGoawaySequence);

  Zhttp::H3::Connection h3;
  Zhttp::H3::H3Frame settings;
  settings.type = Zhttp::H3::H3FrameType::Settings;
  ZuCHECK(h3.receiveControlFrame(settings), "GOAWAY setup SETTINGS failed");

  Zhttp::H3::Connection peer;
  Zhttp::H3::HeaderBytes bytes;
  Zhttp::H3::H3Frame frame;
  unsigned used = 0;

  ZuCHECK(peer.encodeGoaway(bytes, 16) > 0,
    "first GOAWAY encode failed");
  ZuCSpan first{reinterpret_cast<const char *>(bytes.data()), bytes.length()};
  ZuCHECK(!Zhttp::H3::H3FrameCodec::parse(first, frame, used) &&
      h3.receiveControlFrame(frame) &&
      h3.peerGoawayReceived() &&
      h3.peerGoawayID() == 16,
    "first GOAWAY receive failed");

  ZuCHECK(peer.encodeGoaway(bytes, 8) > 0,
    "decreasing GOAWAY encode failed");
  ZuCSpan lower{reinterpret_cast<const char *>(bytes.data()), bytes.length()};
  ZuCHECK(!Zhttp::H3::H3FrameCodec::parse(lower, frame, used) &&
      h3.receiveControlFrame(frame) &&
      h3.peerGoawayID() == 8,
    "decreasing GOAWAY receive failed");
  ZuCHECK(h3.acceptRequestStream(4) &&
      !h3.acceptRequestStream(8) &&
      !h3.acceptRequestStream(12),
    "decreasing GOAWAY cutoff was not applied");

  ZuCHECK(peer.encodeGoaway(bytes, 12) > 0,
    "increasing GOAWAY setup failed");
  ZuCSpan higher{reinterpret_cast<const char *>(bytes.data()), bytes.length()};
  ZuCHECK(!Zhttp::H3::H3FrameCodec::parse(higher, frame, used) &&
      !h3.receiveControlFrame(frame) &&
      h3.peerGoawayID() == 8 &&
      h3.diag().lastError == Zhttp::H3::H3Error::IDError,
    "increasing GOAWAY was accepted");
}

void testSettingsValidation()
{
  ZuTestScope(testSettingsValidation);

  Zhttp::H3::Settings settings;
  settings.hasQPackMaxTableCapacity = true;
  settings.qpackMaxTableCapacity = 256;
  settings.hasMaxFieldSectionSize = true;
  settings.maxFieldSectionSize = 8192;
  settings.hasQPackBlockedStreams = true;
  settings.qpackBlockedStreams = 4;

  Zhttp::H3::HeaderBytes payload;
  ZuCHECK(Zhttp::H3::H3FrameCodec::writeSettingsPayload(
      settings, payload) > 0,
    "SETTINGS payload encode failed");

  Zhttp::H3::H3Frame frame;
  frame.type = Zhttp::H3::H3FrameType::Settings;
  frame.payload =
    ZuCSpan{reinterpret_cast<const char *>(payload.data()), payload.length()};
  Zhttp::H3::Connection h3;
  ZuCHECK(h3.receiveControlFrame(frame) &&
      h3.peerSettingsReceived() &&
      h3.peerSettings().qpackMaxTableCapacity == 256 &&
      h3.peerSettings().maxFieldSectionSize == 8192 &&
      h3.peerSettings().qpackBlockedStreams == 4,
    "peer SETTINGS values were not applied");

  payload.length(0);
  ZuCHECK(Zhttp::H3::CodecBytes::putVar(payload, 0x21) == 0 &&
      Zhttp::H3::CodecBytes::putVar(payload, 1) == 0 &&
      Zhttp::H3::CodecBytes::putVar(payload, 0x07) == 0 &&
      Zhttp::H3::CodecBytes::putVar(payload, 2) == 0,
    "unknown SETTINGS payload setup failed");
  Zhttp::H3::Settings parsed;
  ZuCHECK(Zhttp::H3::H3FrameCodec::parseSettings(
      ZuCSpan{reinterpret_cast<const char *>(payload.data()),
	payload.length()}, parsed) == int(payload.length()) &&
      parsed.hasQPackBlockedStreams &&
      parsed.qpackBlockedStreams == 2,
    "unknown SETTINGS entry was not skipped");

  payload.length(0);
  ZuCHECK(Zhttp::H3::CodecBytes::putVar(payload, 0x01) == 0 &&
      Zhttp::H3::CodecBytes::putVar(payload, 1) == 0 &&
      Zhttp::H3::CodecBytes::putVar(payload, 0x01) == 0 &&
      Zhttp::H3::CodecBytes::putVar(payload, 2) == 0,
    "duplicate SETTINGS payload setup failed");
  ZuCHECK(Zhttp::H3::H3FrameCodec::parseSettings(
      ZuCSpan{reinterpret_cast<const char *>(payload.data()),
	payload.length()}, parsed) < 0,
    "duplicate SETTINGS entry was accepted");

  payload.length(0);
  ZuCHECK(Zhttp::H3::CodecBytes::putVar(payload, 0x02) == 0 &&
      Zhttp::H3::CodecBytes::putVar(payload, 1) == 0,
    "reserved SETTINGS payload setup failed");
  frame.payload =
    ZuCSpan{reinterpret_cast<const char *>(payload.data()), payload.length()};
  Zhttp::H3::Connection bad;
  ZuCHECK(!bad.receiveControlFrame(frame),
    "reserved HTTP/2 SETTINGS identifier was accepted");
}

void testMessageStreamState()
{
  ZuTestScope(testMessageStreamState);

  Zhttp::H3::MessageStream stream;
  Zhttp::H3::H3Frame frame;
  frame.type = Zhttp::H3::H3FrameType::Data;
  ZuCHECK(!stream.receiveFrame(frame) &&
      stream.error() == Zhttp::H3::H3Error::FrameUnexpected,
    "message stream accepted DATA before HEADERS");

  Zhttp::H3::MessageStream ok;
  frame.type = Zhttp::H3::H3FrameType::Headers;
  ZuCHECK(ok.receiveFrame(frame) &&
      ok.headersReceived() &&
      ok.lastPart() == Zhttp::H3::MessagePart::Headers,
    "message stream rejected initial HEADERS");
  frame.type = Zhttp::H3::H3FrameType::Data;
  frame.payload = "abc";
  Zhttp::H3::HeaderBytes body;
  ZuCHECK(ok.receiveFrame(frame,
      [&body](ZuCSpan s) {
	for (unsigned i = 0; i < s.length(); ++i)
	  body.push(uint8_t(s[i]));
      }) &&
      ok.dataReceived() &&
      ok.bodyBytes() == 3 &&
      ok.dataFrames() == 1 &&
      ok.lastPart() == Zhttp::H3::MessagePart::Data,
    "message stream rejected DATA after HEADERS");
  ZuCSpan bodySpan{reinterpret_cast<const char *>(body.data()), body.length()};
  ZuCHECK(bodySpan == "abc",
    "message stream DATA callback mismatch");
  frame.type = Zhttp::H3::H3FrameType::Headers;
  ZuCHECK(ok.receiveFrame(frame) &&
      ok.trailersReceived() &&
      ok.closed() &&
      ok.lastPart() == Zhttp::H3::MessagePart::Trailers,
    "message stream rejected receive-side trailers");
  frame.type = Zhttp::H3::H3FrameType::Data;
  ZuCHECK(!ok.receiveFrame(frame) &&
      ok.error() == Zhttp::H3::H3Error::FrameUnexpected,
    "message stream accepted DATA after trailers");

  Zhttp::H3::MessageStream invalid;
  frame.type = Zhttp::H3::H3FrameType::Settings;
  ZuCHECK(!invalid.receiveFrame(frame),
    "message stream accepted control-only SETTINGS frame");

  Zhttp::H3::MessageStream unknown;
  frame.type = Zhttp::H3::H3FrameType::Unknown;
  ZuCHECK(unknown.receiveFrame(frame, true) && unknown.closed(),
    "message stream did not skip unknown frame");

  Zhttp::H3::MessageStream cancelled;
  frame.type = Zhttp::H3::H3FrameType::Headers;
  frame.payload = {};
  ZuCHECK(cancelled.receiveFrame(frame), "message stream rejected headers");
  TestStream quicStream{0};
  cancelled.cancelTransport(quicStream);
  uint64_t cancelCode =
    Zhttp::H3::ErrorCodec::code(Zhttp::H3::H3Error::RequestCancelled);
  ZuCHECK(cancelled.cancelled() &&
      cancelled.closed() &&
      cancelled.error() == Zhttp::H3::H3Error::RequestCancelled &&
      cancelled.cancelCode() == cancelCode,
    "message stream cancellation state mismatch");
  ZuCHECK(quicStream.resetSent() &&
      quicStream.stopSent() &&
      quicStream.appError() == cancelCode,
    "message stream cancellation did not map to reset/stop");
  frame.type = Zhttp::H3::H3FrameType::Data;
  ZuCHECK(!cancelled.receiveFrame(frame) &&
      cancelled.error() == Zhttp::H3::H3Error::RequestCancelled,
    "cancelled message stream accepted DATA");
}

void testDynamicQPackConnectionState()
{
  ZuTestScope(testDynamicQPackConnectionState);

  Zhttp::H3::Params params;
  params.qpackTableCapacity(256).qpackBlockedStreams(2);
  Zhttp::H3::Connection h3{ZuMv(params)};

  Zhttp::H3::HeaderBytes instruction;
  Zhttp::H3::QPackDecodedInstruction decodedInstruction;
  ZuCHECK(Zhttp::H3::QPack::encodeInsertLiteral(
      instruction, {":path", "/dynamic"}) > 0 &&
      Zhttp::H3::QPack::decodeInstruction(
	ZuCSpan{reinterpret_cast<const char *>(instruction.data()),
	  instruction.length()}, decodedInstruction) == int(instruction.length()) &&
      h3.applyQPackEncoderInstruction(decodedInstruction) &&
      h3.qpackDecoderState().insertCount() == 1,
    "dynamic QPACK encoder instruction was not applied to decoder state");

  Zhttp::H3::FieldSectionPrefix prefix;
  prefix.requiredInsertCount = 1;
  prefix.base = 0;
  prefix.baseNegative = false;
  Zhttp::H3::HeaderBytes fields;
  ZuCHECK(Zhttp::H3::QPack::encodeFieldSectionPrefix(fields, prefix) > 0,
    "dynamic request field prefix encode failed");
  fields.push(0xd1);
  fields.push(0xd7);
  fields.push(0x50);
  putValue(fields, "localhost");
  fields.push(0x80);

  Zhttp::H3::H3Frame frame;
  frame.type = Zhttp::H3::H3FrameType::Headers;
  frame.payload =
    ZuCSpan{reinterpret_cast<const char *>(fields.data()), fields.length()};

  Zhttp::H3::DecodedRequest request;
  ZuCHECK(h3.decodeRequestDynamic(frame, request) == int(fields.length()) &&
      request.method == Zhttp::Method::GET &&
      request.scheme == "https" &&
      request.authority == "localhost" &&
      request.path == "/dynamic",
    "dynamic QPACK request decode failed");
  ZuCHECK(h3.decodeRequest(frame, request, h3.qpackDecoderState().insertCount())
      < 0,
    "stateless H3 request decoder accepted dynamic QPACK reference");

  Zhttp::H3::QPackDecodedInstruction ack;
  ack.type = Zhttp::H3::QPackInstruction::SectionAck;
  ack.value = 4;
  ZuCHECK(h3.applyQPackDecoderInstruction(ack) &&
      h3.qpackEncoderState().acknowledgements() == 1,
    "QPACK decoder instruction was not applied to encoder state");
}

void testDynamicQPackEncoding()
{
  ZuTestScope(testDynamicQPackEncoding);

  Zhttp::H3::Params clientParams;
  clientParams.qpackTableCapacity(256).qpackBlockedStreams(4).
    qpackIndex("accept");
  Zhttp::H3::Connection client{ZuMv(clientParams)};

  Zhttp::H3::Header requestHeaders[] = {
    { "accept", "application/json" }
  };
  Zhttp::H3::RequestParams req;
  req.authority = "localhost";
  req.path = "/dynamic-encode";
  req.headers = ZuSpan<Zhttp::H3::Header>{requestHeaders, 1};

  Zhttp::H3::HeaderBytes requestFrame;
  Zhttp::H3::HeaderBytes encoderBytes;
  ZuCHECK(client.encodeRequestDynamic(requestFrame, encoderBytes, req) > 0 &&
      encoderBytes.length() &&
      client.qpackEncoderState().insertCount() == 1 &&
      client.diag().qpackInstructionsTx == 1,
    "dynamic request encode did not emit QPACK encoder instruction");

  Zhttp::H3::QPackDecodedInstruction instruction;
  ZuCHECK(Zhttp::H3::QPack::decodeInstruction(
      ZuCSpan{reinterpret_cast<const char *>(encoderBytes.data()),
	encoderBytes.length()}, instruction) == int(encoderBytes.length()) &&
      instruction.type == Zhttp::H3::QPackInstruction::InsertWithNameRef &&
      instruction.header.value == "application/json",
    "dynamic request encoder instruction decode failed");

  Zhttp::H3::Params serverParams;
  serverParams.qpackTableCapacity(256).qpackBlockedStreams(4).
    qpackIndex("accept");
  Zhttp::H3::Connection server{ZuMv(serverParams)};
  ZuCHECK(server.applyQPackEncoderInstruction(instruction) &&
      server.qpackDecoderState().insertCount() == 1,
    "server did not apply request encoder instruction");

  Zhttp::H3::H3Frame frame;
  unsigned used = 0;
  ZuCSpan requestSpan{
    reinterpret_cast<const char *>(requestFrame.data()), requestFrame.length()};
  ZuCHECK(!Zhttp::H3::H3FrameCodec::parse(requestSpan, frame, used) &&
      used == requestFrame.length() &&
      frame.type == Zhttp::H3::H3FrameType::Headers,
    "dynamic request frame parse failed");
  Zhttp::H3::DecodedRequest decoded;
  ZuCHECK(server.decodeRequestDynamic(frame, decoded) ==
      int(frame.payload.length()) &&
      decoded.path == "/dynamic-encode" &&
      decoded.headers.length() == 1 &&
      decoded.headers[0].name == "accept" &&
      decoded.headers[0].value == "application/json",
    "dynamic encoded request did not decode");
  ZuCHECK(server.decodeRequest(frame, decoded,
      server.qpackDecoderState().insertCount()) < 0,
    "stateless request decoder accepted encoded dynamic reference");

  Zhttp::H3::Params responseParams;
  responseParams.qpackTableCapacity(256).qpackBlockedStreams(4).
    qpackIndex("server");
  Zhttp::H3::Connection responseTx{ZuMv(responseParams)};
  Zhttp::H3::Header responseHeaders[] = { { "server", "zhttp" } };
  Zhttp::H3::ResponseParams resp;
  resp.headers = ZuSpan<Zhttp::H3::Header>{responseHeaders, 1};

  Zhttp::H3::HeaderBytes responseFrame;
  Zhttp::H3::HeaderBytes responseEncoder;
  ZuCHECK(responseTx.encodeResponseDynamic(
      responseFrame, responseEncoder, resp) > 0 &&
      responseEncoder.length() &&
      responseTx.diag().qpackInstructionsTx == 1,
    "dynamic response encode did not emit QPACK instruction");
  ZuCHECK(Zhttp::H3::QPack::decodeInstruction(
      ZuCSpan{reinterpret_cast<const char *>(responseEncoder.data()),
	responseEncoder.length()}, instruction) == int(responseEncoder.length()),
    "dynamic response encoder instruction decode failed");

  Zhttp::H3::Params responseRxParams;
  responseRxParams.qpackTableCapacity(256).qpackBlockedStreams(4).
    qpackIndex("server");
  Zhttp::H3::Connection responseRx{ZuMv(responseRxParams)};
  ZuCHECK(responseRx.applyQPackEncoderInstruction(instruction),
    "response decoder did not apply encoder instruction");
  ZuCSpan responseSpan{
    reinterpret_cast<const char *>(responseFrame.data()),
    responseFrame.length()};
  ZuCHECK(!Zhttp::H3::H3FrameCodec::parse(responseSpan, frame, used),
    "dynamic response frame parse failed");
  Zhttp::H3::DecodedResponse decodedResponse;
  ZuCHECK(responseRx.decodeResponseDynamic(frame, decodedResponse) ==
      int(frame.payload.length()) &&
      decodedResponse.status == 200 &&
      decodedResponse.headers.length() == 1 &&
      decodedResponse.headers[0].name == "server" &&
      decodedResponse.headers[0].value == "zhttp",
    "dynamic encoded response did not decode");

  Zhttp::H3::Header invalidStatus[] = { { ":status", "000" } };
  Zhttp::H3::HeaderBytes invalidFields;
  Zhttp::H3::HeaderBytes invalidFrame;
  ZuCHECK(Zhttp::H3::QPack::encodeLiteral(
      invalidFields, ZuSpan<Zhttp::H3::Header>{invalidStatus, 1}, {}) > 0 &&
      Zhttp::H3::H3FrameCodec::write(
	Zhttp::H3::H3FrameType::Headers,
	ZuCSpan{
	  reinterpret_cast<const char *>(invalidFields.data()),
	  invalidFields.length()},
	invalidFrame) > 0,
    "invalid dynamic response status setup failed");
  ZuCSpan invalidSpan{
    reinterpret_cast<const char *>(invalidFrame.data()),
    invalidFrame.length()};
  ZuCHECK(!Zhttp::H3::H3FrameCodec::parse(invalidSpan, frame, used) &&
      responseRx.decodeResponseDynamic(frame, decodedResponse) < 0,
    "dynamic response decoder accepted out-of-range status");

  Zhttp::H3::Header trailer[] = { { "server", "done" } };
  Zhttp::H3::HeaderBytes trailerFrame;
  Zhttp::H3::HeaderBytes trailerEncoder;
  ZuCHECK(responseTx.encodeTrailersDynamic(
      trailerFrame, trailerEncoder,
      ZuSpan<Zhttp::H3::Header>{trailer, 1}) > 0 &&
      trailerEncoder.length(),
    "dynamic trailer encode failed");

  Zhttp::H3::Params badParams;
  badParams.qpackTableCapacity(128).qpackIndex("cookie");
  Zhttp::H3::Connection bad{ZuMv(badParams)};
  Zhttp::H3::Header cookie[] = { { "cookie", "sensitive" } };
  req.headers = ZuSpan<Zhttp::H3::Header>{cookie, 1};
  ZuCHECK(bad.encodeRequestDynamic(requestFrame, encoderBytes, req) < 0 &&
      bad.diag().lastError == Zhttp::H3::H3Error::MessageError,
    "dynamic encoder accepted sensitive configured header");
}

void testQPackStreamBytes()
{
  ZuTestScope(testQPackStreamBytes);

  Zhttp::H3::Params params;
  params.qpackTableCapacity(128).qpackBlockedStreams(2);
  Zhttp::H3::Connection decoderSide{ZuMv(params)};

  Zhttp::H3::HeaderBytes stream;
  Zhttp::H3::HeaderBytes instruction;
  ZuCHECK(Zhttp::H3::QPack::encodeSetCapacity(instruction, 128) > 0,
    "QPACK stream set-capacity instruction encode failed");
  appendBytes(stream, instruction);
  ZuCHECK(Zhttp::H3::QPack::encodeInsertLiteral(
      instruction, {"accept", "application/json"}) > 0,
    "QPACK stream insert instruction encode failed");
  appendBytes(stream, instruction);

  ZuCHECK(decoderSide.receiveQPackEncoderStream(
      ZuCSpan{reinterpret_cast<const char *>(stream.data()),
	stream.length()}) == int(stream.length()) &&
      decoderSide.qpackDecoderState().table().find(
	"accept", "application/json") == 1 &&
      decoderSide.diag().qpackInstructionsRx == 2,
    "QPACK encoder stream bytes were not applied");

  Zhttp::H3::Params encoderParams;
  encoderParams.qpackTableCapacity(128).qpackBlockedStreams(2);
  Zhttp::H3::Connection encoderSide{ZuMv(encoderParams)};
  ZuCHECK(encoderSide.qpackEncoderState().insert(
      {"accept", "application/json"}),
    "QPACK decoder stream setup insert failed");
  stream.length(0);
  ZuCHECK(Zhttp::H3::QPack::encodeSectionAck(instruction, 4) > 0,
    "QPACK section-ack instruction encode failed");
  appendBytes(stream, instruction);
  ZuCHECK(Zhttp::H3::QPack::encodeInsertCountIncrement(instruction, 1) > 0,
    "QPACK insert-count increment encode failed");
  appendBytes(stream, instruction);
  ZuCHECK(encoderSide.receiveQPackDecoderStream(
      ZuCSpan{reinterpret_cast<const char *>(stream.data()),
	stream.length()}) == int(stream.length()) &&
      encoderSide.qpackEncoderState().acknowledgements() == 1 &&
      encoderSide.qpackEncoderState().knownReceivedCount() == 1 &&
      encoderSide.diag().qpackInstructionsRx == 2,
    "QPACK decoder stream bytes were not applied");

  uint8_t bad[] = { 0xff };
  ZuCHECK(decoderSide.receiveQPackEncoderStream(
      ZuCSpan{reinterpret_cast<const char *>(bad), sizeof(bad)}) < 0 &&
      decoderSide.diag().lastError ==
	Zhttp::H3::H3Error::QPackEncoderStreamError,
    "malformed QPACK encoder stream bytes were accepted");

  Zhttp::H3::Connection wrongEncoderStream;
  ZuCHECK(Zhttp::H3::QPack::encodeSectionAck(instruction, 1) > 0,
    "QPACK wrong encoder-stream instruction setup failed");
  ZuCHECK(wrongEncoderStream.receiveQPackEncoderStream(
      ZuCSpan{reinterpret_cast<const char *>(instruction.data()),
	instruction.length()}) < 0 &&
      wrongEncoderStream.diag().lastError ==
	Zhttp::H3::H3Error::QPackEncoderStreamError &&
      !wrongEncoderStream.diag().qpackInstructionsRx,
    "QPACK encoder stream accepted decoder-stream instruction");

  Zhttp::H3::Params wrongDecoderParams;
  wrongDecoderParams.qpackTableCapacity(128);
  Zhttp::H3::Connection wrongDecoderStream{ZuMv(wrongDecoderParams)};
  ZuCHECK(Zhttp::H3::QPack::encodeSetCapacity(instruction, 64) > 0,
    "QPACK wrong decoder-stream instruction setup failed");
  ZuCHECK(wrongDecoderStream.receiveQPackDecoderStream(
      ZuCSpan{reinterpret_cast<const char *>(instruction.data()),
	instruction.length()}) < 0 &&
      wrongDecoderStream.diag().lastError ==
	Zhttp::H3::H3Error::QPackDecoderStreamError &&
      !wrongDecoderStream.diag().qpackInstructionsRx &&
      wrongDecoderStream.qpackEncoderState().table().capacity() == 128,
    "QPACK decoder stream accepted encoder-stream instruction");
}

void testErrorCodec()
{
  ZuTestScope(testErrorCodec);

  ZuCHECK(Zhttp::H3::ErrorCodec::code(Zhttp::H3::H3Error::NoError) ==
      0x0100 &&
      Zhttp::H3::ErrorCodec::fromCode(0x0100) ==
	Zhttp::H3::H3Error::NoError &&
      Zhttp::H3::ErrorCodec::name(Zhttp::H3::H3Error::NoError) ==
	"H3_NO_ERROR",
    "H3 no-error mapping mismatch");
  ZuCHECK(Zhttp::H3::ErrorCodec::code(
	 Zhttp::H3::H3Error::FrameUnexpected) == 0x0105 &&
      Zhttp::H3::ErrorCodec::fromCode(0x0105) ==
	Zhttp::H3::H3Error::FrameUnexpected &&
      Zhttp::H3::ErrorCodec::name(Zhttp::H3::H3Error::FrameUnexpected) ==
	"H3_FRAME_UNEXPECTED",
    "H3 frame-unexpected mapping mismatch");
  ZuCHECK(Zhttp::H3::ErrorCodec::code(
	 Zhttp::H3::H3Error::QPackDecompressionFailed) == 0x0200 &&
      Zhttp::H3::ErrorCodec::fromCode(0x0200) ==
	Zhttp::H3::H3Error::QPackDecompressionFailed &&
      Zhttp::H3::ErrorCodec::name(
	Zhttp::H3::H3Error::QPackEncoderStreamError) ==
	"QPACK_ENCODER_STREAM_ERROR",
    "QPACK error mapping mismatch");
  ZuCHECK(Zhttp::H3::ErrorCodec::fromCode(0x99) ==
      Zhttp::H3::H3Error::GeneralProtocol,
    "unknown H3 error code fallback mismatch");
}

void testDiagnostics()
{
  ZuTestScope(testDiagnostics);

  Zhttp::H3::Connection tx;
  Zhttp::H3::HeaderBytes settings;
  ZuCHECK(tx.encodeSettings(settings) > 0, "SETTINGS diag encode failed");

  Zhttp::H3::Header requestHeaders[] = {
    { "accept", "application/json" }
  };
  Zhttp::H3::RequestParams req;
  req.authority = "localhost";
  req.path = "/diag";
  req.headers = ZuSpan<Zhttp::H3::Header>{requestHeaders, 1};

  Zhttp::H3::HeaderBytes requestFrame;
  ZuCHECK(tx.encodeRequest(requestFrame, req) > 0,
    "request diag encode failed");

  Zhttp::H3::HeaderBytes dataFrame;
  ZuCHECK(tx.encodeData(dataFrame, "hello") > 0, "DATA diag encode failed");

  Zhttp::H3::HeaderBytes goaway;
  ZuCHECK(tx.encodeGoaway(goaway, 16) > 0, "GOAWAY diag encode failed");

  ZuCHECK(tx.diag().framesTx == 4 &&
      tx.diag().headersTx == 1 &&
      tx.diag().dataFramesTx == 1 &&
      tx.diag().bodyBytesTx == 5 &&
      tx.diag().controlFramesTx == 2 &&
      tx.diag().goawayTx == 1,
    "HTTP/3 Tx diagnostic counters mismatch");

  Zhttp::H3::Connection rx;
  Zhttp::H3::H3Frame parsed;
  unsigned used = 0;
  ZuCSpan requestSpan{
    reinterpret_cast<const char *>(requestFrame.data()), requestFrame.length()};
  ZuCHECK(!Zhttp::H3::H3FrameCodec::parse(requestSpan, parsed, used),
    "request diag frame parse failed");
  Zhttp::H3::DecodedRequest decoded;
  ZuCHECK(rx.decodeRequest(parsed, decoded) > 0 &&
      decoded.path == "/diag",
    "request diag decode failed");

  ZuCSpan dataSpan{
    reinterpret_cast<const char *>(dataFrame.data()), dataFrame.length()};
  ZuCHECK(!Zhttp::H3::H3FrameCodec::parse(dataSpan, parsed, used),
    "DATA diag frame parse failed");
  ZuCSpan body;
  ZuCHECK(rx.decodeData(parsed, body) == 5 && body == "hello",
    "DATA diag decode failed");

  ZuCHECK(rx.diag().framesRx == 2 &&
      rx.diag().headersRx == 1 &&
      rx.diag().dataFramesRx == 1 &&
      rx.diag().bodyBytesRx == 5,
    "HTTP/3 Rx diagnostic counters mismatch");

  ZuCHECK(Zhttp::H3::Diag::frameTypeName(Zhttp::H3::H3FrameType::Goaway) ==
      "goaway" &&
      !strcmp(Zhttp::H3Log, "Zhttp.H3") &&
      !strcmp(Zhttp::QPackLog, "Zhttp.QPack") &&
      Zhttp::H3::Diag::streamTypeName(
	Zhttp::H3::H3StreamType::QPackEncoder) == "qpack_encoder" &&
      Zhttp::H3::Diag::settingName(
	Zhttp::H3::H3Setting::QPackBlockedStreams) ==
	"qpack_blocked_streams" &&
      Zhttp::H3::Diag::messagePartName(
	Zhttp::H3::MessagePart::Trailers) == "trailers",
    "HTTP/3 diagnostic stable names mismatch");

  Zhttp::H3::DiagText frameText = Zhttp::H3::Diag::frameSummary(parsed);
  Zhttp::H3::DiagText fieldsText =
    Zhttp::H3::Diag::fieldSectionSummary(
      ZuSpan<Zhttp::H3::Header>{requestHeaders, 1});
  Zhttp::H3::DiagText summary = tx.diag().summary();
  ZuCHECK(strstr(frameText.data(), "type=data") &&
      strstr(frameText.data(), "payload=5") &&
      strstr(fieldsText.data(), "fields=1") &&
      strstr(fieldsText.data(), "first=accept") &&
      strstr(summary.data(), "bodyBytesTx=5") &&
      strstr(summary.data(), "goawayTx=1"),
    "HTTP/3 diagnostic text mismatch");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testCriticalStreams);
  ZuTestCall(testPseudoHeaderValidation);
  ZuTestCall(testRequestStreamAdmission);
  ZuTestCall(testUnknownFrameSkip);
  ZuTestCall(testControlFrameState);
  ZuTestCall(testGoawaySequence);
  ZuTestCall(testSettingsValidation);
  ZuTestCall(testMessageStreamState);
  ZuTestCall(testDynamicQPackConnectionState);
  ZuTestCall(testDynamicQPackEncoding);
  ZuTestCall(testQPackStreamBytes);
  ZuTestCall(testErrorCodec);
  ZuTestCall(testDiagnostics);
}
