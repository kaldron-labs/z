//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP/2 frame and connection-control test

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZhttpH2.hh>
#include <zlib/ZhttpHPack.hh>

using namespace ZuTestUtil;

namespace {

using Bytes =
  ZtArray<uint8_t, ZtArrayHeapID<"Zhttp.H2FrameTest.Bytes">>;

void append(Bytes &out, ZuBSpan value)
{
  for (unsigned i = 0, n = value.length(); i < n; ++i) out.push(value[i]);
}

void frame(
  Bytes &out, uint8_t type, uint8_t flags, uint32_t streamID,
  ZuBSpan payload = {})
{
  uint32_t length = payload.length();
  Zhttp::H2::putHeader(out, {
    .length = length,
    .streamID = streamID,
    .type = type,
    .flags = flags
  });
  append(out, payload);
}

struct Cxn : public Zhttp::H2::Connection<Cxn> {
  void h2Error(Zhttp::H2::Error::T value) {
    if (!errorCalls++) firstError = value;
  }
  void h2Setting(uint16_t key, uint32_t value) {
    ++settingCalls;
    lastSetting = key;
    lastValue = value;
  }
  void h2Settings() { ++settingsCalls; }
  void h2SettingsAck() { ++settingsAckCalls; }
  void h2Headers(uint32_t stream, ZuSpan<uint8_t> value) {
    headerStream = stream;
    headers << value;
    if (decodeHPack &&
	hpack.process(value, [this](Zhttp::H2::DecodedField field) {
	  ++fieldCalls;
	  lastName = field.name;
	  lastValue_ = field.value;
	}) < 0)
      hpackFailed = true;
  }
  void h2HeadersEnd(uint32_t stream, bool endStream) {
    headerStream = stream;
    headerEndStream = endStream;
    ++headersEndCalls;
    if (decodeHPack && !hpack.finish()) hpackFailed = true;
  }
  bool h2DataBegin(uint32_t stream, uint32_t length) {
    dataStream = stream;
    dataFrameLength = length;
    return dataAllowed;
  }
  void h2Data(uint32_t stream, ZuBSpan value) {
    dataStream = stream;
    data << value;
  }
  void h2DataEnd(
    uint32_t stream, bool endStream, uint32_t length,
    unsigned prefix, unsigned pad)
  {
    dataStream = stream;
    dataFrameLength = length;
    dataPrefix = prefix;
    dataPad = pad;
    dataEndStream = endStream;
    ++dataEndCalls;
  }
  void h2Ping(ZuBSpan value) {
    ++pingCalls;
    ping.length(0);
    ping << value;
  }
  void h2PingAck(ZuBSpan) { ++pingAckCalls; }
  void h2Reset(uint32_t stream, Zhttp::H2::Error::T value) {
    resetStream = stream;
    resetError = value;
  }
  void h2WindowUpdate(uint32_t stream, uint32_t value) {
    windowStream = stream;
    window = value;
  }
  void h2Goaway(uint32_t stream, Zhttp::H2::Error::T value) {
    goawayStream = stream;
    goawayError = value;
  }

  ZtString<>		headers;
  ZtString<>		data;
  ZtString<>		ping;
  ZtString<>		lastName;
  ZtString<>		lastValue_;
  Zhttp::H2::HPackDecoder hpack;
  Zhttp::H2::Error::T	firstError = Zhttp::H2::Error::NoError;
  Zhttp::H2::Error::T	resetError = Zhttp::H2::Error::NoError;
  Zhttp::H2::Error::T	goawayError = Zhttp::H2::Error::NoError;
  uint32_t		lastValue = 0;
  uint32_t		headerStream = 0;
  uint32_t		dataStream = 0;
  uint32_t		dataFrameLength = 0;
  uint32_t		resetStream = 0;
  uint32_t		windowStream = 0;
  uint32_t		window = 0;
  uint32_t		goawayStream = 0;
  uint16_t		lastSetting = 0;
  unsigned		errorCalls = 0;
  unsigned		settingCalls = 0;
  unsigned		settingsCalls = 0;
  unsigned		settingsAckCalls = 0;
  unsigned		headersEndCalls = 0;
  unsigned		dataEndCalls = 0;
  unsigned		pingCalls = 0;
  unsigned		pingAckCalls = 0;
  unsigned		fieldCalls = 0;
  unsigned		dataPrefix = 0;
  unsigned		dataPad = 0;
  bool			headerEndStream = false;
  bool			dataEndStream = false;
  bool			dataAllowed = true;
  bool			decodeHPack = false;
  bool			hpackFailed = false;
};

void initial(Bytes &bytes)
{
  Zhttp::H2::putSettingsHeader(bytes);
}

bool fragmented(Cxn &cxn, ZuSpan<uint8_t> input)
{
  for (unsigned i = 0, n = input.length(); i < n; ++i)
    if (cxn.process(ZuSpan<uint8_t>{input.data() + i, 1}) < 0)
      return false;
  return true;
}

void testHeaderAndPreface()
{
  ZuTestScope(testHeaderAndPreface);

  Bytes bytes;
  Zhttp::H2::putPreface(bytes);
  Zhttp::H2::putHeader(bytes, {
    .length = 0x010203,
    .streamID = 0x01020304,
    .type = Zhttp::H2::FrameType::Headers,
    .flags = Zhttp::H2::Flag::EndHeaders
  });
  Zhttp::H2::PrefaceParser preface;
  unsigned offset = 0;
  bool split = true;
  for (unsigned i = 0; i < 24; ++i) {
    unsigned one = i;
    int state = preface.process(ZuBSpan{bytes.data(), i + 1}, one);
    if (state != int(i == 23)) split = false;
  }
  ZuCHECK(split, "preface accepts every one-byte fragment");

  Zhttp::H2::FrameHeaderParser parser;
  Zhttp::H2::FrameHeader header;
  offset = 24;
  ZuCHECK(parser.process(bytes, offset, header) == 1 &&
      header.length == 0x010203 &&
      header.streamID == 0x01020304 &&
      header.type == Zhttp::H2::FrameType::Headers &&
      header.flags == Zhttp::H2::Flag::EndHeaders,
    "frame header round trip");

  Cxn bad;
  bad.init(true);
  uint8_t mismatch = 'X';
  ZuCHECK(bad.process(ZuSpan<uint8_t>{&mismatch, 1}) < 0 &&
      bad.error() == Zhttp::H2::Error::ProtocolError &&
      bad.errorCalls == 1,
    "preface fails at first mismatching byte");
}

void testValidation()
{
  ZuTestScope(testValidation);
  using namespace Zhttp::H2;

  ZuCHECK(validateFrame(
      {.length = 8, .streamID = 0, .type = FrameType::Ping},
      DefltFrameSize) == Error::NoError,
    "valid PING");
  ZuCHECK(validateFrame(
      {.length = 7, .streamID = 0, .type = FrameType::Ping},
      DefltFrameSize) == Error::FrameSizeError,
    "PING length validation");
  ZuCHECK(validateFrame(
      {.length = 0, .streamID = 1, .type = FrameType::Settings},
      DefltFrameSize) == Error::ProtocolError,
    "SETTINGS stream validation");
  ZuCHECK(validateFrame(
      {.length = 6, .streamID = 0, .type = FrameType::Settings,
       .flags = Flag::ACK},
      DefltFrameSize) == Error::FrameSizeError,
    "SETTINGS ACK length validation");
  ZuCHECK(validateFrame(
      {.length = DefltFrameSize + 1, .streamID = 1,
       .type = FrameType::Data},
      DefltFrameSize) == Error::FrameSizeError,
    "configured frame-size validation");

  Settings settings;
  ZuCHECK(settings.apply(
      Setting::InitialWindowSize, MaxWindow, false) == Error::NoError &&
      settings.initialWindowSize == MaxWindow,
    "initial window upper bound");
  ZuCHECK(settings.apply(
      Setting::InitialWindowSize, MaxWindow + 1U, false) ==
	Error::FlowControlError,
    "initial window overflow");
  ZuCHECK(settings.apply(
      Setting::MaxFrameSize_, DefltFrameSize - 1U, false) ==
	Error::ProtocolError,
    "frame size lower bound");
  ZuCHECK(settings.apply(
      Setting::EnablePush, 1, true) == Error::ProtocolError,
    "server cannot send ENABLE_PUSH");
  ZuCHECK(settings.apply(
      Setting::EnableConnectProtocol, 1, false) == Error::NoError &&
      settings.enableConnectProtocol,
    "Extended CONNECT capability is retained");
  ZuCHECK(settings.apply(
      Setting::EnableConnectProtocol, 2, false) == Error::ProtocolError,
    "Extended CONNECT capability is boolean");
  ZuCHECK(settings.apply(0xffff, 7, false) == Error::NoError,
    "unknown setting ignored");
}

void testConnectionControl()
{
  ZuTestScope(testConnectionControl);

  Bytes bytes;
  initial(bytes);
  uint8_t settings[] = {
    0x00, Zhttp::H2::Setting::HeaderTableSize, 0, 0, 0, 32,
    0x00, Zhttp::H2::Setting::MaxConcurrentStreams, 0, 0, 0, 7
  };
  bytes.length(0);
  frame(bytes, Zhttp::H2::FrameType::Settings, 0, 0, settings);

  uint8_t firstHeaders[] = {2, 0, 0, 0, 0, 0, 'a', 'b', 'c', 0, 0};
  frame(bytes, Zhttp::H2::FrameType::Headers,
    Zhttp::H2::Flag::Padded | Zhttp::H2::Flag::Priority |
      Zhttp::H2::Flag::EndStream,
    1, firstHeaders);
  uint8_t continuation[] = {'d', 'e', 'f'};
  frame(bytes, Zhttp::H2::FrameType::Continuation,
    Zhttp::H2::Flag::EndHeaders, 1, continuation);
  uint8_t data[] = {2, 'x', 'y', 0, 0};
  frame(bytes, Zhttp::H2::FrameType::Data,
    Zhttp::H2::Flag::Padded | Zhttp::H2::Flag::EndStream, 1, data);
  uint8_t ping[] = {0, 1, 2, 3, 4, 5, 6, 7};
  frame(bytes, Zhttp::H2::FrameType::Ping, 0, 0, ping);
  uint8_t unknown[] = {1, 2, 3};
  frame(bytes, 0xfe, 0, 0, unknown);

  Cxn cxn;
  ZuCHECK(cxn.init(false) && fragmented(cxn, bytes),
    "fragmented connection control parses");
  ZuCHECK(cxn.settingsCalls == 1 && cxn.settingCalls == 2 &&
      cxn.peerSettings().headerTableSize == 32 &&
      cxn.peerSettings().maxConcurrentStreams == 7,
    "SETTINGS values applied before ACK request");
  ZuCHECK(cxn.headers == "abcdef" && cxn.headersEndCalls == 1 &&
      cxn.headerStream == 1 && cxn.headerEndStream,
    "HEADERS/CONTINUATION sequencing and padding");
  ZuCHECK(cxn.data == "xy" && cxn.dataEndCalls == 1 &&
      cxn.dataFrameLength == 5 &&
      cxn.dataPrefix == 1 && cxn.dataPad == 2 &&
      cxn.dataStream == 1 && cxn.dataEndStream,
    "DATA padding and END_STREAM");
  ZuCHECK(cxn.pingCalls == 1 && cxn.ping.length() == 8 && !cxn.errorCalls,
    "PING and unknown frame handling");
}

void testConnectionErrors()
{
  ZuTestScope(testConnectionErrors);

  {
    Bytes bytes;
    uint8_t ping[] = {'1', '2', '3', '4', '5', '6', '7', '8'};
    frame(bytes, Zhttp::H2::FrameType::Ping, 0, 0,
      ZuBSpan{ping});
    Cxn cxn;
    cxn.init(false);
    ZuCHECK(cxn.process(bytes) < 0 &&
	cxn.error() == Zhttp::H2::Error::ProtocolError,
      "first peer frame must be SETTINGS");
  }
  {
    Bytes bytes;
    initial(bytes);
    uint8_t h[] = {'a'};
    frame(bytes, Zhttp::H2::FrameType::Headers, 0, 1, h);
    frame(bytes, Zhttp::H2::FrameType::Data, 0, 1, h);
    Cxn cxn;
    cxn.init(false);
    ZuCHECK(cxn.process(bytes) < 0 &&
	cxn.error() == Zhttp::H2::Error::ProtocolError &&
	cxn.errorCalls == 1,
      "interleaving during CONTINUATION is one connection error");
  }
  {
    Bytes bytes;
    initial(bytes);
    uint8_t push[] = {0, 0, 0, 2};
    frame(bytes, Zhttp::H2::FrameType::PushPromise,
      Zhttp::H2::Flag::EndHeaders, 1, push);
    Cxn cxn;
    cxn.init(false);
    ZuCHECK(cxn.process(bytes) < 0 &&
	cxn.error() == Zhttp::H2::Error::ProtocolError &&
	cxn.errorCalls == 1,
      "prohibited push is one peer-visible connection error");
  }
  {
    Bytes bytes;
    initial(bytes);
    uint8_t priority[] = {0, 0, 0, 1, 0};
    frame(bytes, Zhttp::H2::FrameType::Priority, 0, 1, priority);
    Cxn cxn;
    cxn.init(false);
    ZuCHECK(cxn.process(bytes) < 0 &&
	cxn.error() == Zhttp::H2::Error::ProtocolError,
      "self-dependent PRIORITY is rejected");
  }
}

void testSettingsTimeoutAndHPack()
{
  ZuTestScope(testSettingsTimeoutAndHPack);

  {
    Bytes bytes;
    initial(bytes);
    Zhttp::H2::putSettingsHeader(bytes, 0, true);
    Cxn cxn;
    cxn.init(false);
    cxn.settingsSent();
    ZuCHECK(cxn.process(bytes) >= 0 && cxn.settingsAckCalls == 1 &&
	cxn.settingsOutstanding() == 0 && !cxn.settingsTimeout(),
      "SETTINGS ACK cancels the configured timeout state");

    Cxn timeout;
    timeout.init(false);
    timeout.settingsSent();
    ZuCHECK(timeout.settingsTimeout() < 0 &&
	timeout.error() == Zhttp::H2::Error::SettingsTimeout &&
	timeout.errorCalls == 1,
      "outstanding SETTINGS timeout is a one-shot connection error");
  }

  {
    Zhttp::H2::HPackEncoder encoder;
    Zhttp::H2::HPackBytes encoded;
    encoder.init(4096);
    encoder.field(encoded, {":status", "200"});
    encoder.field(encoded, {"content-type", "text/plain"});

    Bytes bytes;
    initial(bytes);
    unsigned split = encoded.length() / 2;
    frame(bytes, Zhttp::H2::FrameType::Headers, 0, 1,
      ZuBSpan{encoded.data(), split});
    frame(bytes, Zhttp::H2::FrameType::Continuation,
      Zhttp::H2::Flag::EndHeaders, 1,
      ZuBSpan{encoded.data() + split, encoded.length() - split});

    Cxn cxn;
    cxn.init(false);
    cxn.decodeHPack = true;
    cxn.hpack.init(4096, 1U<<16);
    cxn.hpack.reset();
    ZuCHECK(fragmented(cxn, bytes) && !cxn.hpackFailed &&
	cxn.fieldCalls == 2 && cxn.lastName == "content-type" &&
	cxn.lastValue_ == "text/plain",
      "fragmented HEADERS/CONTINUATION streams directly through HPACK");
    cxn.hpack.final();
    encoder.final();
  }
}

void testWriters()
{
  ZuTestScope(testWriters);

  Bytes bytes;
  Zhttp::H2::putSettingsHeader(bytes, 0, true);
  Zhttp::H2::putGoaway(bytes, 7, Zhttp::H2::Error::Cancel);
  ZuCHECK(bytes.length() == 26 && bytes[3] == Zhttp::H2::FrameType::Settings &&
      bytes[4] == Zhttp::H2::Flag::ACK &&
      bytes[12] == Zhttp::H2::FrameType::Goaway &&
      bytes[21] == 7 && bytes[22] == 0 && bytes[23] == 0 &&
      bytes[24] == 0 && bytes[25] == Zhttp::H2::Error::Cancel,
    "SETTINGS ACK and GOAWAY wire writers");
}

} // namespace

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testHeaderAndPreface);
  ZuTestCall(testValidation);
  ZuTestCall(testConnectionControl);
  ZuTestCall(testConnectionErrors);
  ZuTestCall(testSettingsTimeoutAndHPack);
  ZuTestCall(testWriters);
}
