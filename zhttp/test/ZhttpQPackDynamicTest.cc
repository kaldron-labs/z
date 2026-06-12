//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zhttp.hh>

using namespace ZuTestUtil;

static ZuCSpan span(const Zhttp::H3::HeaderBytes &bytes)
{
  return ZuCSpan{reinterpret_cast<const char *>(bytes.data()), bytes.length()};
}

static void appendSpan(Zhttp::H3::HeaderBytes &bytes, ZuCSpan s)
{
  for (unsigned i = 0; i < s.length(); ++i) bytes.push(uint8_t(s[i]));
}

namespace {

using StreamAlloc = ZiIOBufAlloc<256, 4096, "ZhttpQPackDynamicTest.Buf">;
using BuilderHeaders = ZhttpHeaders("accept");

struct CaptureTxStream : public Zi::TxStream<CaptureTxStream> {
  using Base = Zi::TxStream<CaptureTxStream>;

  CaptureTxStream() : Base(4096, 0, 0) { }

  ZmRef<ZiIOBuf> allocBuf_(unsigned headRoom) {
    ZmRef<ZiIOBuf> buf = new StreamAlloc{};
    buf->skip = headRoom;
    buf->length = 0;
    return buf;
  }

  void sendBuf_(ZmRef<ZiIOBuf> buf) {
    if (!buf) return;
    for (unsigned i = 0; i < buf->length; ++i) bytes.push(buf->data()[i]);
  }

  Zhttp::H3::HeaderBytes	bytes;
};

struct CaptureEncoderTx : public Zhttp::H3::QPackEncoderTx {
  void write(ZuCSpan span) override { appendSpan(bytes, span); }

  Zhttp::H3::HeaderBytes	bytes;
};

struct BuilderState :
  public Zhttp::H3::Builder<BuilderState, BuilderHeaders> {
  using Base = Zhttp::H3::Builder<BuilderState, BuilderHeaders>;

  const Zhttp::H3::Params &h3Params() const { return params; }
  Zhttp::H3::QPackTxTable *qpackTx() { return &tx; }
  Zhttp::H3::QPackEncoderTx *qpackEncoderTx() { return &encoder; }
  uint64_t streamID() const { return id; }
  unsigned status() const { return 200; }

  template <typename L>
  void operation(L &&l) const {
    l(Zhttp::Method::GET, path, query);
  }
  template <typename L> void host(L &&l) const { l("example.com"); }
  template <typename Key, typename L>
  void header(L &&l) const {
    if constexpr (ZuIsSame<Key, ZuStringT<"accept">>{})
      l("application/json");
    else
      l("");
  }

  Zhttp::H3::Params	params;
  Zhttp::H3::QPackTxTable tx;
  CaptureEncoderTx	encoder;
  uint64_t		id = 1;
  ZuCSpan		path = "/sample";
  ZuCSpan		query = "";
};

static ZuCSpan captureSpan(const CaptureTxStream &stream)
{
  return ZuCSpan{
    reinterpret_cast<const char *>(stream.bytes.data()), stream.bytes.length()};
}

static bool headersPayload(ZuCSpan bytes, ZuCSpan &payload)
{
  unsigned o = 0;
  uint64_t type = 0, len = 0;
  if (Zhttp::H3::decodeVar(bytes, o, type) < 0 ||
      Zhttp::H3::decodeVar(bytes, o, len) < 0 ||
      type != 0x01 || bytes.length() != o + len)
    return false;
  payload = ZuCSpan{bytes.data() + o, unsigned(len)};
  return true;
}

} // namespace

static void appendHuffmanString(
  Zhttp::H3::HeaderBytes &bytes, uint8_t prefix, unsigned prefixBits,
  ZuCSpan s)
{
  Zhttp::H3::HeaderBytes encoded;
  encoded.length(Zhttp::H3::HPack::enclen(s.length()));
  uint64_t n = Zhttp::H3::HPack::encode(
    ZuSpan<uint8_t>{encoded.data(), encoded.length()},
    ZuBSpan{reinterpret_cast<const uint8_t *>(s.data()), s.length()});
  encoded.length(n);
  bytes.push(prefix | uint8_t(encoded.length()));
  appendSpan(bytes, span(encoded));
}

void testRxTable()
{
  ZuTestScope(testRxTable);

  Zhttp::H3::QPackRxTable table;
  table.maxCapacityBytes_ = 128;
  ZuCHECK(table.setCapacity(128), "rx capacity setup failed");
  ZuCHECK(table.insert({":authority", "www.example.com"}) &&
      table.insert({":path", "/sample/path"}) &&
      table.insertCount() == 2 && table.count() == 2,
    "rx insert failed");

  Zhttp::H3::Header h;
  ZuCHECK(table.lookupAbs(0, h) &&
      h.name == ":authority" && h.value == "www.example.com",
    "rx absolute lookup failed");
  ZuCHECK(table.lookupRelative(table.insertCount(), 0, h) &&
      h.name == ":path" && h.value == "/sample/path",
    "rx relative lookup failed");
  ZuCHECK(table.lookupPostBase(0, 1, h) &&
      h.name == ":path" && h.value == "/sample/path",
    "rx post-base lookup failed");

  ZuCHECK(table.duplicate(0) && table.insertCount() == 3,
    "rx duplicate failed");
  table.setCapacity(48);
  ZuCHECK(table.used() <= table.capacity(), "rx eviction failed");
  ZuCHECK(!table.lookupAbs(0, h), "rx lookup ignored eviction");
  auto count = table.insertCount();
  ZuCHECK(!table.insert({"x-large", "012345678901234567890123456789"}) &&
      table.insertCount() == count,
    "rx oversized insert changed state");
}

void testDynamicFieldSectionDecode()
{
  ZuTestScope(testDynamicFieldSectionDecode);

  Zhttp::H3::QPackRxTable table;
  table.maxCapacityBytes_ = 256;
  table.setCapacity(256);
  table.insert({":authority", "www.example.com"});
  table.insert({":path", "/sample/path"});

  Zhttp::H3::FieldSectionPrefix prefix;
  prefix.requiredInsertCount = table.insertCount();
  prefix.base = table.insertCount();

  Zhttp::H3::HeaderBytes bytes;
  ZuCHECK(Zhttp::H3::QPack::encodeFieldSectionPrefix(
      bytes, prefix, table.maxCapacity()) > 0,
    "dynamic relative prefix encode failed");
  bytes.push(0x80);
  bytes.push(0x81);
  unsigned seen = 0;
  bool sawPath = false, sawAuthority = false;
  ZuCHECK(Zhttp::H3::decodeLiteralDynamic(
      span(bytes), table,
      [&](Zhttp::H3::Header h) {
	if (seen == 0 && h.name == ":path" && h.value == "/sample/path")
	  sawPath = true;
	if (seen == 1 && h.name == ":authority" &&
	    h.value == "www.example.com")
	  sawAuthority = true;
	++seen;
      }) == int(bytes.length()) &&
      seen == 2 && sawPath && sawAuthority,
    "dynamic relative indexed field decode failed");

  bytes.length(0);
  prefix.base = 0;
  ZuCHECK(Zhttp::H3::QPack::encodeFieldSectionPrefix(
      bytes, prefix, table.maxCapacity()) > 0,
    "dynamic post-base prefix encode failed");
  bytes.push(0x10);
  bytes.push(0x11);
  seen = 0;
  sawPath = sawAuthority = false;
  ZuCHECK(Zhttp::H3::decodeLiteralDynamic(
      span(bytes), table,
      [&](Zhttp::H3::Header h) {
	if (seen == 0 && h.name == ":authority" &&
	    h.value == "www.example.com")
	  sawAuthority = true;
	if (seen == 1 && h.name == ":path" && h.value == "/sample/path")
	  sawPath = true;
	++seen;
      }) == int(bytes.length()) &&
      seen == 2 && sawPath && sawAuthority,
    "dynamic post-base indexed field decode failed");
}

void testTxTable()
{
  ZuTestScope(testTxTable);

  Zhttp::H3::QPackTxTable table;
  ZuCHECK(table.setMaxCapacity(128) && table.setCapacity(128),
    "tx capacity setup failed");
  uint64_t abs = uint64_t(-1);
  ZuCHECK(table.insert({"accept", "application/json"}, &abs) && !abs,
    "tx insert failed");
  ZuCHECK(table.find("accept", "application/json") &&
      table.find(
	ZuCSpan{"accept"}, ZuCSpan{"application/json"})->abs == abs,
    "tx exact lookup by temporary spans failed");
  table.insertCountIncrement(1);
  Zhttp::H3::Header h;
  ZuCHECK(table.lookupAbs(0, h) &&
      h.name == "accept" && h.value == "application/json",
    "tx absolute lookup failed");
  ZuCHECK(table.insert({"server", "z"}, &abs) && abs == 1,
    "tx second insert failed");
  table.setCapacity(48);
  ZuCHECK(table.used() <= table.capacity(), "tx eviction failed");
}

void testTxEvictReferenced()
{
  ZuTestScope(testTxEvictReferenced);

  Zhttp::H3::QPackTxTable table;
  ZuCHECK(table.setMaxCapacity(128) && table.setCapacity(128),
    "tx capacity setup failed");
  uint64_t abs0 = uint64_t(-1), abs1 = uint64_t(-1);
  ZuCHECK(table.insert({"a", "b"}, &abs0) && !abs0 &&
      table.insert({"c", "d"}, &abs1) && abs1 == 1,
    "tx referenced eviction insert setup failed");
  Zhttp::H3::QPackTxRefs refs;
  refs.push(abs0);
  table.trackSection(7, refs);
  Zhttp::H3::Header h;
  ZuCHECK(!table.setCapacity(48) && table.capacity() == 128 &&
      table.lookupAbs(abs0, h) && h.name == "a" && h.value == "b",
    "tx capacity reduction evicted referenced entry");
  ZuCHECK(table.sectionAck(7) && table.setCapacity(48) &&
      table.used() <= table.capacity() && !table.lookupAbs(abs0, h) &&
      table.lookupAbs(abs1, h) && h.name == "c" && h.value == "d",
    "tx eviction after section ack failed");
}

void testInstructionEncoding()
{
  ZuTestScope(testInstructionEncoding);

  Zhttp::H3::HeaderBytes bytes;
  Zhttp::H3::QPackDecodedInstruction decoded;

  ZuCHECK(Zhttp::H3::QPack::encodeSetCapacity(bytes, 10) > 0 &&
      bytes.length() == 1 && bytes[0] == 0x2a &&
      Zhttp::H3::QPack::decodeEncoderInstructionOne(
	span(bytes), decoded) == int(bytes.length()) &&
      decoded.type == Zhttp::H3::QPackInstruction::SetCapacity &&
      decoded.value == 10,
    "set capacity instruction mismatch");

  ZuCHECK(Zhttp::H3::QPack::encodeInsertWithNameRef(
      bytes, 46, false, "txt") > 0 &&
      bytes.length() == 5 && bytes[0] == 0xee && bytes[1] == 3 &&
      Zhttp::H3::QPack::decodeEncoderInstructionOne(
	span(bytes), decoded) == int(bytes.length()) &&
      decoded.type == Zhttp::H3::QPackInstruction::InsertWithNameRef &&
      !decoded.nameRefDynamic && decoded.value == 46 &&
      decoded.header.value == "txt",
    "insert with static name reference mismatch");

  ZuCHECK(Zhttp::H3::QPack::encodeInsertLiteral(
      bytes, {"accept", "json"}) > 0 &&
      bytes[0] == 0x46 &&
      Zhttp::H3::QPack::decodeEncoderInstructionOne(
	span(bytes), decoded) == int(bytes.length()) &&
      decoded.type == Zhttp::H3::QPackInstruction::InsertWithoutNameRef &&
      decoded.header.name == "accept" &&
      decoded.header.value == "json",
    "insert literal mismatch");

  ZuCHECK(Zhttp::H3::QPack::encodeDuplicate(bytes, 0) > 0 &&
      bytes.length() == 1 && bytes[0] == 0x00 &&
      Zhttp::H3::QPack::decodeEncoderInstructionOne(
	span(bytes), decoded) == int(bytes.length()) &&
      decoded.type == Zhttp::H3::QPackInstruction::Duplicate &&
      !decoded.value,
    "duplicate instruction mismatch");

  ZuCHECK(Zhttp::H3::QPack::encodeSectionAck(bytes, 4) > 0 &&
      bytes.length() == 1 && bytes[0] == 0x84 &&
      Zhttp::H3::QPack::decodeDecoderInstructionOne(
	span(bytes), decoded) == int(bytes.length()) &&
      decoded.type == Zhttp::H3::QPackInstruction::SectionAck &&
      decoded.value == 4,
    "section ack instruction mismatch");

  ZuCHECK(Zhttp::H3::QPack::encodeStreamCancellation(bytes, 4) > 0 &&
      bytes.length() == 1 && bytes[0] == 0x44 &&
      Zhttp::H3::QPack::decodeDecoderInstructionOne(
	span(bytes), decoded) == int(bytes.length()) &&
      decoded.type == Zhttp::H3::QPackInstruction::StreamCancellation &&
      decoded.value == 4,
    "stream cancellation instruction mismatch");

  ZuCHECK(Zhttp::H3::QPack::encodeInsertCountIncrement(bytes, 1) > 0 &&
      bytes.length() == 1 && bytes[0] == 0x01 &&
      Zhttp::H3::QPack::decodeDecoderInstructionOne(
	span(bytes), decoded) == int(bytes.length()) &&
      decoded.type == Zhttp::H3::QPackInstruction::InsertCountIncrement &&
      decoded.value == 1,
    "insert count increment instruction mismatch");
  ZuCHECK(Zhttp::H3::QPack::encodeInsertCountIncrement(bytes, 0) < 0,
    "zero insert count increment was encoded");
}

void testHuffmanInstructionStorage()
{
  ZuTestScope(testHuffmanInstructionStorage);

  Zhttp::H3::HeaderBytes bytes;
  appendHuffmanString(bytes, 0x60, 5, "accept");
  appendHuffmanString(bytes, 0x80, 7, "gzip");

  Zhttp::H3::QPackDecodedInstruction decoded;
  ZuCHECK(Zhttp::H3::QPack::decodeEncoderInstructionOne(
      span(bytes), decoded) == int(bytes.length()) &&
      decoded.type == Zhttp::H3::QPackInstruction::InsertWithoutNameRef &&
      decoded.header.name == "accept" && decoded.header.value == "gzip",
    "Huffman insert literal instruction decode failed");
  bytes.length(0);
  ZuCHECK(decoded.header.name == "accept" && decoded.header.value == "gzip",
    "Huffman instruction spans did not survive input buffer reuse");
}

void testInstructionParserSplit()
{
  ZuTestScope(testInstructionParserSplit);

  Zhttp::H3::HeaderBytes encoderBytes;
  Zhttp::H3::HeaderBytes decoderBytes;
  Zhttp::H3::QPack::encodeInsertLiteral(encoderBytes, {"accept", "json"});
  Zhttp::H3::QPack::encodeSectionAck(decoderBytes, 9);

  Zhttp::H3::QPackInsnParser encoder;
  Zhttp::H3::QPackInsnParser decoder;
  unsigned encoderApplied = 0, decoderApplied = 0;
  auto decodeEncoder = [](ZuCSpan bytes, Zhttp::H3::QPackDecodedInstruction &i) {
    return Zhttp::H3::QPack::decodeEncoderInstructionOne(bytes, i);
  };
  auto decodeDecoder = [](ZuCSpan bytes, Zhttp::H3::QPackDecodedInstruction &i) {
    return Zhttp::H3::QPack::decodeDecoderInstructionOne(bytes, i);
  };
  auto applyEncoder = [&encoderApplied](
    const Zhttp::H3::QPackDecodedInstruction &i) {
    if (i.type == Zhttp::H3::QPackInstruction::InsertWithoutNameRef &&
	i.header.name == "accept" && i.header.value == "json")
      ++encoderApplied;
    return true;
  };
  auto applyDecoder = [&decoderApplied](
    const Zhttp::H3::QPackDecodedInstruction &i) {
    if (i.type == Zhttp::H3::QPackInstruction::SectionAck && i.value == 9)
      ++decoderApplied;
    return true;
  };

  ZuCHECK(encoder.parse(
      ZuBSpan{encoderBytes.data(), 1}, decodeEncoder, applyEncoder) &&
      !encoderApplied,
    "split encoder instruction was applied early");
  ZuCHECK(decoder.parse(
      ZuBSpan{decoderBytes.data(), decoderBytes.length()},
      decodeDecoder, applyDecoder) && decoderApplied == 1,
    "decoder parser did not stay independent of partial encoder parser");
  ZuCHECK(encoder.parse(
      ZuBSpan{encoderBytes.data() + 1, encoderBytes.length() - 1},
      decodeEncoder, applyEncoder) && encoderApplied == 1,
    "split encoder instruction did not complete");
}

void testBuilderPeerCapacity()
{
  ZuTestScope(testBuilderPeerCapacity);

  BuilderState builder;
  builder.params.qpackTableCapacity(256);
  CaptureTxStream stream;
  builder.request(stream);
  ZuCHECK(!builder.encoder.bytes.length() && !builder.tx.capacity() &&
      !builder.tx.insertCount(),
    "builder emitted dynamic QPACK before peer capacity");

  ZuCSpan payload;
  ZuCHECK(headersPayload(captureSpan(stream), payload),
    "builder did not emit a valid HEADERS frame");
  unsigned seen = 0;
  ZuCHECK(Zhttp::H3::QPack::decodeLiteral(
      payload,
      [&seen](Zhttp::H3::Header h) {
	if (h.name == "accept" && h.value == "application/json") ++seen;
      }) == int(payload.length()) && seen == 1,
    "builder static/literal HEADERS payload did not decode");

  BuilderState dynamicBuilder;
  dynamicBuilder.params.qpackTableCapacity(256);
  ZuCHECK(dynamicBuilder.tx.setMaxCapacity(256),
    "builder tx max capacity setup failed");
  CaptureTxStream dynamicStream;
  dynamicBuilder.request(dynamicStream);
  ZuCHECK(dynamicBuilder.encoder.bytes.length() &&
      dynamicBuilder.tx.capacity() == 256 &&
      dynamicBuilder.tx.insertCount() > 0,
    "builder did not commit dynamic QPACK after peer capacity");
}

void testBuilderQueryPath()
{
  ZuTestScope(testBuilderQueryPath);

  BuilderState builder;
  builder.path = "/sample";
  builder.query = "q=1";
  CaptureTxStream stream;
  builder.request(stream);

  ZuCSpan payload;
  ZuCHECK(headersPayload(captureSpan(stream), payload),
    "builder query path did not emit a valid HEADERS frame");
  bool sawPath = false;
  ZuCHECK(Zhttp::H3::QPack::decodeLiteral(
      payload,
      [&sawPath](Zhttp::H3::Header h) {
	if (h.name == ":path" && h.value == "/sample?q=1") sawPath = true;
      }) == int(payload.length()) && sawPath,
    "builder query path did not decode as a segmented :path value");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testRxTable);
  ZuTestCall(testDynamicFieldSectionDecode);
  ZuTestCall(testTxTable);
  ZuTestCall(testTxEvictReferenced);
  ZuTestCall(testInstructionEncoding);
  ZuTestCall(testHuffmanInstructionStorage);
  ZuTestCall(testInstructionParserSplit);
  ZuTestCall(testBuilderPeerCapacity);
  ZuTestCall(testBuilderQueryPath);
}
