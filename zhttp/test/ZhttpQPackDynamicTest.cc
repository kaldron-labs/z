//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuDerive.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZiRxStream.hh>
#include <zlib/Zhttp.hh>

using namespace ZuTestUtil;

static ZuCSpan span(const Zhttp::H3::HdrBytes &bytes)
{
  return ZuCSpan{reinterpret_cast<const char *>(bytes.data()), bytes.length()};
}

static void appendSpan(Zhttp::H3::HdrBytes &bytes, ZuCSpan s)
{
  for (unsigned i = 0; i < s.length(); ++i) bytes.push(uint8_t(s[i]));
}

static void appendBytes(Zhttp::H3::HdrBytes &bytes, ZuBSpan s)
{
  for (unsigned i = 0; i < s.length(); ++i) bytes.push(s[i]);
}

namespace {

using StreamAlloc = ZiIOBufAlloc<256, 4096, "ZhttpQPackDynamicTest.Buf">;
using BuilderHeaders = ZhttpHeaders("accept");
ZuDerive(RxQueue,
  (ZmList<ZiIOBuf, ZmListNode<ZiIOBuf,
    ZmListHeapID<"">>>));
using RxBufAlloc = Zi::IOBufAlloc<RxQueue::Node, 256, 1<<20,
  ZuStringT<"ZhttpQPackDynamicTest.RxBuf">>;
using RxStream = ZiRxStream<RxQueue>;

static ZmRef<RxQueue::Node> rxBuf(ZuBSpan span)
{
  ZmRef<RxQueue::Node> buf = new RxBufAlloc{};
  auto iobuf = static_cast<ZiIOBuf *>(buf.ptr());
  if (span.length()) iobuf->append(span.data(), span.length());
  return buf;
}

static void putFrame(
  Zhttp::H3::HdrBytes &out, uint64_t type, ZuBSpan payload)
{
  Zhttp::H3::putVar(out, type);
  Zhttp::H3::putVar(out, payload.length());
  appendBytes(out, payload);
}

static void putSetting(
  Zhttp::H3::HdrBytes &out, uint64_t key, uint64_t value)
{
  Zhttp::H3::putVar(out, key);
  Zhttp::H3::putVar(out, value);
}

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

  Zhttp::H3::HdrBytes	bytes;
};

struct CaptureEncoder {
  bool write(ZuBSpan span) {
    if (failWrite >= 0 && unsigned(failWrite) == writes++) return false;
    appendBytes(bytes, span);
    return true;
  }

  Zhttp::H3::HdrBytes	bytes;
  int				failWrite = -1;
  unsigned			writes = 0;
};

struct BuilderState :
  public Zhttp::H3::Builder<BuilderState, BuilderHeaders> {
  using Base = Zhttp::H3::Builder<BuilderState, BuilderHeaders>;

  const Zhttp::H3::Params &h3Params() const { return params; }
  Zhttp::H3::QPackTxTable *qpackTx() { return &tx; }
  bool qpackEncoderWrite(ZuBSpan span) { return encoder.write(span); }
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
  CaptureEncoder	encoder;
  uint64_t		id = 1;
  ZuCSpan		path = "/sample";
  ZuCSpan		query = "";
};

using ParserHeaders = ZhttpHeaders("x-test");

struct ParserStream :
  public Zhttp::H3::Parser<ParserStream, true, ParserHeaders, 1024> {
  using Base = Zhttp::H3::Parser<ParserStream, true, ParserHeaders, 1024>;

  RxStream &rxStream() { return rx; }
  bool resetReceived() const { return false; }
  bool stopReceived() const { return false; }
  bool finReceived() const { return fin; }
  Zhttp::H3::QPackRxTable *qpackRx() { return &qpackRxTable; }
  const Zhttp::H3::Params &h3Params() const { return params; }

  void push(const Zhttp::H3::HdrBytes &bytes) {
    rx.push(rxBuf(ZuBSpan{bytes}));
  }
  void operation(Zhttp::Method::T method_, ZuBSpan path_) {
    method = method_;
    path.length(0);
    path << ZuCSpan{path_};
  }
  template <typename Key> void header(ZuBSpan value) {
    if constexpr (ZuIsSame<Key, ZuStringT<"x-test">>{}) {
      ++xTestCalls;
      xTestLen = value.length();
      if (value.length() <= 4096) {
	xTest.length(0);
	xTest << ZuCSpan{value};
      }
    }
  }
  void contentLength(uint64_t v) { contentLen = v; ++contentLenCalls; }
  void body(ZuBSpan) { ++bodyCalls; }
  void complete(Zhttp::H3::ParserState::T state_) {
    completeState = state_;
    ++completeCalls;
  }

  RxStream			rx;
  Zhttp::H3::QPackRxTable	qpackRxTable;
  Zhttp::H3::Params		params;
  Zhttp::Method::T		method = -1;
  ZtString<>			path;
  ZtString<>			xTest;
  unsigned			xTestCalls = 0;
  unsigned			xTestLen = 0;
  int64_t			contentLen = -1;
  unsigned			contentLenCalls = 0;
  unsigned			bodyCalls = 0;
  unsigned			completeCalls = 0;
  bool				fin = false;
  Zhttp::H3::ParserState::T	completeState =
    Zhttp::H3::ParserState::Initial;
};

struct CxnStream :
  public Zhttp::H3::CxnParser<CxnStream> {
  using Base = Zhttp::H3::CxnParser<CxnStream>;

  RxStream &rxStream() { return rx; }
  bool resetReceived() const { return false; }
  bool stopReceived() const { return false; }
  bool finReceived() const { return false; }
  Zhttp::H3::QPackTxTable *qpackTx() { return &qpackTxTable; }
  void setting(uint64_t, uint64_t) { ++settings; }

  void push(const Zhttp::H3::HdrBytes &bytes) {
    rx.push(rxBuf(ZuBSpan{bytes}));
  }

  RxStream			rx;
  Zhttp::H3::QPackTxTable	qpackTxTable;
  unsigned			settings = 0;
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

static bool txUnchanged(const Zhttp::H3::QPackTxTable &tx)
{
  return !tx.capacity() && !tx.capacitySent && !tx.insertCount() &&
      !tx.knownReceivedCount() &&
      !tx.find("accept", "application/json") &&
      !tx.find(":authority", "example.com");
}

static void appendString(
  Zhttp::H3::HdrBytes &bytes, uint8_t prefix, unsigned prefixBits,
  ZuCSpan s)
{
  Zhttp::H3::putPref(bytes, prefix, prefixBits, s.length());
  appendSpan(bytes, s);
}

} // namespace

static void appendHuffmanString(
  Zhttp::H3::HdrBytes &bytes, uint8_t prefix, unsigned prefixBits,
  ZuCSpan s)
{
  Zhttp::H3::HdrBytes encoded;
  encoded.length(Zhttp::H3::HPack::enclen(s.length()));
  uint64_t n = Zhttp::H3::HPack::encode(
    ZuSpan<uint8_t>{encoded.data(), encoded.length()},
    ZuBSpan{reinterpret_cast<const uint8_t *>(s.data()), s.length()});
  encoded.length(n);
  Zhttp::H3::putPref(bytes, prefix, prefixBits, encoded.length());
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

  Zhttp::H3::HdrBytes bytes;
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

void testFieldRepresentationGoldens()
{
  ZuTestScope(testFieldRepresentationGoldens);

  Zhttp::H3::QPackRxTable table;
  table.maxCapacityBytes_ = 512;
  table.setCapacity(512);
  table.insert({"x-a", "one"});
  table.insert({"x-b", "two"});

  Zhttp::H3::FieldSectionPrefix prefix;
  prefix.requiredInsertCount = table.insertCount();
  prefix.base = table.insertCount();

  auto decodeOne = [&table](
    Zhttp::H3::HdrBytes &bytes, Zhttp::H3::Header &h,
    Zhttp::H3::QPackFieldFlags &flags) {
    unsigned seen = 0;
    int n = Zhttp::H3::QPack::decodeFieldSection(
      span(bytes), &table,
      [&](Zhttp::H3::Header h_, Zhttp::H3::QPackFieldFlags flags_) {
	h = h_;
	flags = flags_;
	++seen;
      });
    return n == int(bytes.length()) && seen == 1;
  };
  Zhttp::H3::HdrBytes bytes;
  auto reset = [&bytes, &prefix, &table]() -> Zhttp::H3::HdrBytes & {
    bytes.length(0);
    Zhttp::H3::QPack::encodeFieldSectionPrefix(
      bytes, prefix, table.maxCapacity());
    return bytes;
  };

  Zhttp::H3::Header h;
  Zhttp::H3::QPackFieldFlags flags;
  uint64_t nameIndex = 0;
  ZuCHECK(Zhttp::H3::QPack::staticNameIndex(":path", nameIndex),
    "static name index setup failed");

  auto &staticIndexed = reset();
  staticIndexed.push(0xd1);
  ZuCHECK(decodeOne(staticIndexed, h, flags) &&
      h.name == ":method" && h.value == "GET" &&
      flags.staticRef && !flags.dynamicRef && !flags.postBase &&
      !flags.neverIndex,
    "static indexed field flags mismatch");

  auto &dynamicIndexed = reset();
  dynamicIndexed.push(0x80);
  ZuCHECK(decodeOne(dynamicIndexed, h, flags) &&
      h.name == "x-b" && h.value == "two" &&
      flags.dynamicRef && !flags.staticRef && !flags.postBase,
    "dynamic relative indexed field flags mismatch");

  prefix.base = 0;
  auto &postBaseIndexed = reset();
  postBaseIndexed.push(0x10);
  ZuCHECK(decodeOne(postBaseIndexed, h, flags) &&
      h.name == "x-a" && h.value == "one" &&
      flags.dynamicRef && flags.postBase,
    "dynamic post-base indexed field flags mismatch");

  prefix.base = table.insertCount();
  auto &staticName = reset();
  staticName.push(uint8_t(0x50 | nameIndex));
  appendString(staticName, 0x00, 7, "/gold");
  ZuCHECK(decodeOne(staticName, h, flags) &&
      h.name == ":path" && h.value == "/gold" &&
      flags.staticRef && !flags.neverIndex,
    "literal static name reference flags mismatch");

  auto &staticNameNever = reset();
  staticNameNever.push(uint8_t(0x70 | nameIndex));
  appendString(staticNameNever, 0x00, 7, "/never");
  ZuCHECK(decodeOne(staticNameNever, h, flags) &&
      h.name == ":path" && h.value == "/never" &&
      flags.staticRef && flags.neverIndex,
    "literal static name never-index flags mismatch");

  auto &dynamicName = reset();
  dynamicName.push(0x40);
  appendString(dynamicName, 0x00, 7, "dyn");
  ZuCHECK(decodeOne(dynamicName, h, flags) &&
      h.name == "x-b" && h.value == "dyn" &&
      flags.dynamicRef && !flags.neverIndex,
    "literal dynamic name reference flags mismatch");

  auto &dynamicNameNever = reset();
  dynamicNameNever.push(0x60);
  appendString(dynamicNameNever, 0x00, 7, "dyn-never");
  ZuCHECK(decodeOne(dynamicNameNever, h, flags) &&
      h.name == "x-b" && h.value == "dyn-never" &&
      flags.dynamicRef && flags.neverIndex,
    "literal dynamic name never-index flags mismatch");

  prefix.base = 0;
  auto &postBaseName = reset();
  postBaseName.push(0x00);
  appendString(postBaseName, 0x00, 7, "post");
  ZuCHECK(decodeOne(postBaseName, h, flags) &&
      h.name == "x-a" && h.value == "post" &&
      flags.dynamicRef && flags.postBase && !flags.neverIndex,
    "literal post-base name flags mismatch");

  auto &postBaseNameNever = reset();
  postBaseNameNever.push(0x08);
  appendString(postBaseNameNever, 0x00, 7, "post-never");
  ZuCHECK(decodeOne(postBaseNameNever, h, flags) &&
      h.name == "x-a" && h.value == "post-never" &&
      flags.dynamicRef && flags.postBase && flags.neverIndex,
    "literal post-base name never-index flags mismatch");

  prefix.base = table.insertCount();
  auto &literalName = reset();
  appendString(literalName, 0x20, 3, "x-lit");
  appendString(literalName, 0x00, 7, "plain");
  ZuCHECK(decodeOne(literalName, h, flags) &&
      h.name == "x-lit" && h.value == "plain" &&
      !flags.staticRef && !flags.dynamicRef && !flags.neverIndex,
    "literal name flags mismatch");

  auto &literalNameNever = reset();
  appendString(literalNameNever, 0x30, 3, "x-sec");
  appendString(literalNameNever, 0x00, 7, "secret");
  ZuCHECK(decodeOne(literalNameNever, h, flags) &&
      h.name == "x-sec" && h.value == "secret" && flags.neverIndex,
    "literal name never-index flags mismatch");

  auto &huffman = reset();
  appendHuffmanString(huffman, 0x28, 3, "x-h");
  appendHuffmanString(huffman, 0x80, 7, "zip");
  ZuCHECK(decodeOne(huffman, h, flags) &&
      h.name == "x-h" && h.value == "zip",
    "Huffman literal name/value decode mismatch");
}

static void putHeadersFrame(
  Zhttp::H3::HdrBytes &frame, ZuSpan<Zhttp::H3::Header> headers)
{
  Zhttp::H3::HdrBytes payload;
  Zhttp::H3::Params params;
  Zhttp::H3::QPack::encodeLiteral(payload, headers, params);
  putFrame(frame, 0x01, ZuBSpan{payload});
}

void testParserFieldCallbacks()
{
  ZuTestScope(testParserFieldCallbacks);

  ParserStream parser;

  Zhttp::H3::Header initialHeaders[] = {
    {":method", "GET"},
    {":path", "/parser"},
    {"x-test", "initial"}
  };
  Zhttp::H3::HdrBytes frame;
  putHeadersFrame(frame, ZuSpan<Zhttp::H3::Header>{initialHeaders, 3});
  parser.push(frame);
  ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Body &&
      parser.method == Zhttp::Method::GET &&
      parser.path == "/parser" &&
      parser.xTestCalls == 1 && parser.xTest == "initial",
    "parser did not deliver initial pseudo/regular fields");

  Zhttp::H3::Header trailerHeaders[] = {
    {"x-test", "trailer"}
  };
  frame.length(0);
  putHeadersFrame(frame, ZuSpan<Zhttp::H3::Header>{trailerHeaders, 1});
  parser.push(frame);
  ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Trailers &&
      parser.xTestCalls == 2 && parser.xTest == "trailer",
    "parser did not deliver trailer fields");
}

void testParserInvalidFields()
{
  ZuTestScope(testParserInvalidFields);

  {
    ParserStream parser;
    Zhttp::H3::Header initialHeaders[] = {
      {":method", "GET"},
      {":path", "/parser"}
    };
    Zhttp::H3::HdrBytes frame;
    putHeadersFrame(frame, ZuSpan<Zhttp::H3::Header>{initialHeaders, 2});
    parser.push(frame);
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Body,
      "invalid trailer setup failed");

    Zhttp::H3::Header trailerHeaders[] = {
      {":path", "/not-allowed"}
    };
    frame.length(0);
    putHeadersFrame(frame, ZuSpan<Zhttp::H3::Header>{trailerHeaders, 1});
    parser.push(frame);
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Error,
      "parser accepted pseudo-field in trailers");
  }
  {
    ParserStream parser;
    Zhttp::H3::HdrBytes payload;
    payload.push(0);
    payload.push(0);
    payload.push(0x80); // indexed dynamic relative with no dynamic table entry
    Zhttp::H3::HdrBytes frame;
    putFrame(frame, 0x01, ZuBSpan{payload});
    parser.push(frame);
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Error,
      "parser accepted invalid QPACK representation");
  }
}

void testParserStrictContentLength()
{
  ZuTestScope(testParserStrictContentLength);

  static const char *values[] = { "5x", "18446744073709551616" };
  for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
    ParserStream parser;
    Zhttp::H3::Header initialHeaders[] = {
      {":method", "GET"},
      {":path", "/parser"},
      {"content-length", values[i]}
    };
    Zhttp::H3::HdrBytes frame;
    putHeadersFrame(frame, ZuSpan<Zhttp::H3::Header>{initialHeaders, 3});
    parser.push(frame);
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Error,
      "invalid H3 content-length was not rejected");
    ZuCHECK(!parser.contentLenCalls && !parser.bodyCalls &&
	parser.completeCalls == 1 &&
	parser.completeState == Zhttp::H3::ParserState::Error,
      "invalid H3 content-length delivered callbacks");
  }
}

static void putLiteralField(
  Zhttp::H3::HdrBytes &payload, ZuCSpan name, ZuCSpan value,
  bool huffmanValue = false)
{
  appendString(payload, 0x20, 3, name);
  if (huffmanValue)
    appendHuffmanString(payload, 0x80, 7, value);
  else
    appendString(payload, 0x00, 7, value);
}

void testParserHeaderScratchAndLimits()
{
  ZuTestScope(testParserHeaderScratchAndLimits);

  ZtString<> longValue;
  for (unsigned i = 0; i < 66000; ++i) longValue << 'a';
  {
    ParserStream parser;
    parser.params.maxHeaderListSize(80000);
    Zhttp::H3::HdrBytes payload;
    payload.push(0);
    payload.push(0);
    putLiteralField(payload, ":method", "GET");
    putLiteralField(payload, ":path", "/parser");
    putLiteralField(payload, "x-test", longValue, true);
    Zhttp::H3::HdrBytes frame;
    putFrame(frame, 0x01, ZuBSpan{payload});
    parser.push(frame);
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Body &&
	parser.xTestCalls == 1 && parser.xTestLen == longValue.length(),
      "large Huffman header value did not decode through dynamic scratch");
  }
  {
    ParserStream parser;
    parser.params.maxHeaderListSize(1024);
    Zhttp::H3::HdrBytes payload;
    payload.push(0);
    payload.push(0);
    putLiteralField(payload, ":method", "GET");
    putLiteralField(payload, ":path", "/parser");
    putLiteralField(payload, "x-test", longValue, true);
    Zhttp::H3::HdrBytes frame;
    putFrame(frame, 0x01, ZuBSpan{payload});
    parser.push(frame);
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Error,
      "oversized Huffman header value was accepted");
  }
  {
    ParserStream parser;
    parser.params.maxHeaderListSize(4096);
    ZtString<> longName;
    longName << "x-long-";
    for (unsigned i = 0; i < 300; ++i) longName << 'A';
    Zhttp::H3::HdrBytes payload;
    payload.push(0);
    payload.push(0);
    putLiteralField(payload, ":method", "GET");
    putLiteralField(payload, ":path", "/parser");
    putLiteralField(payload, longName, "ok");
    Zhttp::H3::HdrBytes frame;
    putFrame(frame, 0x01, ZuBSpan{payload});
    parser.push(frame);
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Body,
      "header name longer than 256 bytes was rejected");
  }
  {
    ParserStream parser;
    parser.params.maxHeaderListSize(24);
    Zhttp::H3::HdrBytes payload;
    payload.push(0);
    payload.push(0);
    putLiteralField(payload, ":method", "GET");
    putLiteralField(payload, ":path", "/parser");
    putLiteralField(payload, "x-test", "value-too-large");
    Zhttp::H3::HdrBytes frame;
    putFrame(frame, 0x01, ZuBSpan{payload});
    parser.push(frame);
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Error,
      "oversized H3 field section was accepted");
  }
}

void testFieldDecodeAllocationDiscipline()
{
  ZuTestScope(testFieldDecodeAllocationDiscipline);

  Zhttp::H3::HdrBytes bytes;
  bytes.push(0);
  bytes.push(0);
  for (unsigned i = 0; i < 20; ++i) {
    appendString(bytes, 0x20, 3, "x");
    appendString(bytes, 0x00, 7, "v");
  }
  const char *begin = reinterpret_cast<const char *>(bytes.data());
  const char *end = begin + bytes.length();
  unsigned seen = 0;
  bool inputBacked = true;
  ZuCHECK(Zhttp::H3::QPack::decodeLiteral(
      span(bytes),
      [&](Zhttp::H3::Header h) {
	inputBacked &= h.name.data() >= begin && h.name.data() < end;
	inputBacked &= h.value.data() >= begin && h.value.data() < end;
	++seen;
      }) == int(bytes.length()) &&
      seen == 20 && inputBacked,
    "non-Huffman field decode did not keep spans input-backed");

  bytes.length(0);
  bytes.push(0);
  bytes.push(0);
  for (unsigned i = 0; i < 20; ++i) {
    appendHuffmanString(bytes, 0x28, 3, "x");
    appendHuffmanString(bytes, 0x80, 7, "v");
  }
  seen = 0;
  ZuCHECK(Zhttp::H3::QPack::decodeLiteral(
      span(bytes),
      [&](Zhttp::H3::Header h) {
	if (h.name == "x" && h.value == "v") ++seen;
      }) == int(bytes.length()) && seen == 20,
    "many Huffman field literals did not decode through scratch path");
}

void testSettingsKeyBoundary()
{
  ZuTestScope(testSettingsKeyBoundary);

  {
    CxnStream stream;
    Zhttp::H3::HdrBytes settings;
    for (unsigned i = 0; i < 40; ++i)
      putSetting(settings, 0x21 + i, i);
    Zhttp::H3::HdrBytes bytes;
    Zhttp::H3::putVar(bytes, 0x00); // control stream
    putFrame(bytes, 0x04, ZuBSpan{settings});
    stream.push(bytes);
    ZuCHECK(stream.process(stream) ==
	Zhttp::H3::CxnState::PeerSettingsReceived &&
	stream.settings == 40,
      "more than 32 unique SETTINGS were not accepted");
  }
  {
    CxnStream stream;
    Zhttp::H3::HdrBytes settings;
    for (unsigned i = 0; i < 33; ++i)
      putSetting(settings, 0x40 + i, i);
    putSetting(settings, 0x40, 99);
    Zhttp::H3::HdrBytes bytes;
    Zhttp::H3::putVar(bytes, 0x00);
    putFrame(bytes, 0x04, ZuBSpan{settings});
    stream.push(bytes);
    ZuCHECK(stream.process(stream) == Zhttp::H3::CxnState::Error,
      "duplicate SETTINGS key after 32 unique keys was accepted");
  }
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
  ZuCHECK(table.insertCount() == 1 && table.used() &&
      table.used() <= table.capacity(),
    "tx insert accounting mismatch");
  ZuCHECK(table.find("accept", "application/json") &&
      table.find(
	ZuCSpan{"accept"}, ZuCSpan{"application/json"})->abs == abs,
    "tx exact lookup by temporary spans failed");
  uint32_t used = table.used();
  ZuCHECK(table.insert({"accept", "application/json"}, &abs) && !abs &&
      table.insertCount() == 1 && table.used() == used,
    "tx duplicate insert changed accounting");
  table.insertCountIncrement(1);
  Zhttp::H3::Header h;
  ZuCHECK(table.lookupAbs(0, h) &&
      h.name == "accept" && h.value == "application/json",
    "tx absolute lookup failed");
  used = table.used();
  uint64_t count = table.insertCount();
  ZuCSpan oversized =
    "012345678901234567890123456789012345678901234567890123456789"
    "012345678901234567890123456789012345678901234567890123456789";
  ZuCHECK(!table.insert({"oversized", oversized}, &abs) &&
      table.insertCount() == count && table.used() == used &&
      !table.find("oversized", oversized) &&
      !table.lookupAbs(count, h),
    "tx failed insert changed state");
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
  ZuCHECK(table.trackSection(7, refs), "tx section tracking setup failed");
  Zhttp::H3::Header h;
  ZuCHECK(!table.setCapacity(48) && table.capacity() == 128 &&
      table.lookupAbs(abs0, h) && h.name == "a" && h.value == "b",
    "tx capacity reduction evicted referenced entry");
  ZuCHECK(table.sectionAck(7) && table.setCapacity(48) &&
      table.used() <= table.capacity() && !table.lookupAbs(abs0, h) &&
      table.lookupAbs(abs1, h) && h.name == "c" && h.value == "d",
    "tx eviction after section ack failed");
}

void testRxTxChurn()
{
  ZuTestScope(testRxTxChurn);

  Zhttp::H3::QPackRxTable rx;
  rx.maxCapacityBytes_ = 96;
  ZuCHECK(rx.setCapacity(96), "rx churn capacity setup failed");
  ZtArray<Zhttp::H3::QPackRxString> rxNames;
  ZtArray<Zhttp::H3::QPackRxString> rxValues;
  for (unsigned i = 0; i < 40; ++i) {
    char name[16], value[16];
    snprintf(name, sizeof(name), "x-rx-%u", i);
    snprintf(value, sizeof(value), "v%u", i);
    new (rxNames.push()) Zhttp::H3::QPackRxString{name};
    new (rxValues.push()) Zhttp::H3::QPackRxString{value};
    ZuCHECK(rx.insert({rxNames[i], rxValues[i]}), "rx churn insert failed");
    ZuCHECK(rx.used() <= rx.capacity(), "rx churn exceeded capacity");
  }
  Zhttp::H3::Header h;
  ZuCHECK(rx.baseAbs() > 0 && rx.insertCount() == 40 &&
      !rx.lookupAbs(rx.baseAbs() - 1, h),
    "rx churn base accounting failed");
  ZuCHECK(rx.lookupAbs(rx.insertCount() - 1, h),
    "rx churn newest lookup failed");

  Zhttp::H3::QPackTxTable tx;
  ZuCHECK(tx.setMaxCapacity(96) && tx.setCapacity(96),
    "tx churn capacity setup failed");
  ZtArray<Zhttp::H3::QPackTxString> txNames;
  ZtArray<Zhttp::H3::QPackTxString> txValues;
  for (unsigned i = 0; i < 40; ++i) {
    char name[16], value[16];
    snprintf(name, sizeof(name), "x-tx-%u", i);
    snprintf(value, sizeof(value), "v%u", i);
    new (txNames.push()) Zhttp::H3::QPackTxString{name};
    new (txValues.push()) Zhttp::H3::QPackTxString{value};
    uint64_t abs = 0;
    ZuCHECK(tx.insert({txNames[i], txValues[i]}, &abs) && abs == i,
      "tx churn insert failed");
    ZuCHECK(tx.used() <= tx.capacity(), "tx churn exceeded capacity");
  }
  ZuCHECK(!tx.lookupAbs(0, h) &&
      tx.lookupAbs(tx.insertCount() - 1, h) &&
      h.name == txNames[39] && h.value == txValues[39] &&
      tx.find(txNames[39], txValues[39]),
    "tx churn lookup failed");
}

void testTxSectionStress()
{
  ZuTestScope(testTxSectionStress);

  Zhttp::H3::QPackTxTable table;
  ZuCHECK(table.setMaxCapacity(512) && table.setCapacity(512),
    "tx section stress capacity setup failed");
  uint64_t abs[6];
  for (unsigned i = 0; i < 6; ++i) {
    char name[16], value[16];
    snprintf(name, sizeof(name), "x-ref-%u", i);
    snprintf(value, sizeof(value), "v%u", i);
    ZuCHECK(table.insert({name, value}, &abs[i]),
      "tx section stress insert failed");
  }
  for (unsigned i = 0; i < 6; ++i) {
    Zhttp::H3::QPackTxRefs refs;
    refs.push(abs[i]);
    ZuCHECK(table.trackSection(100 + i, refs),
      "tx section stress track failed");
  }
  Zhttp::H3::QPackTxRefs dup;
  dup.push(abs[0]);
  ZuCHECK(!table.trackSection(100, dup),
    "duplicate tx section stream ID was accepted");
  ZuCHECK(table.sectionAck(103) && table.streamCancellation(101) &&
      table.sectionAck(105) && table.sectionAck(100) &&
      table.sectionAck(102) && table.sectionAck(104),
    "out-of-order tx section release failed");
  ZuCHECK(!table.sectionAck(100),
    "duplicate tx section ack was accepted");
}

void testInstructionEncoding()
{
  ZuTestScope(testInstructionEncoding);

  Zhttp::H3::HdrBytes bytes;
  Zhttp::H3::QPackDecodedInsn decoded;

  ZuCHECK(Zhttp::H3::QPack::encodeSetCapacity(bytes, 10) > 0 &&
      bytes.length() == 1 && bytes[0] == 0x2a &&
      Zhttp::H3::QPack::decodeEncoderInsnOne(
	span(bytes), decoded) == int(bytes.length()) &&
      decoded.type == Zhttp::H3::QPackInsn::SetCapacity &&
      decoded.value == 10,
    "set capacity instruction mismatch");

  ZuCHECK(Zhttp::H3::QPack::encodeInsertWithNameRef(
      bytes, 46, false, "txt") > 0 &&
      bytes.length() == 5 && bytes[0] == 0xee && bytes[1] == 3 &&
      Zhttp::H3::QPack::decodeEncoderInsnOne(
	span(bytes), decoded) == int(bytes.length()) &&
      decoded.type == Zhttp::H3::QPackInsn::InsertWithNameRef &&
      !decoded.nameRefDynamic && decoded.value == 46 &&
      decoded.header.value == "txt",
    "insert with static name reference mismatch");

  ZuCHECK(Zhttp::H3::QPack::encodeInsertLiteral(
      bytes, {"accept", "json"}) > 0 &&
      bytes[0] == 0x46 &&
      Zhttp::H3::QPack::decodeEncoderInsnOne(
	span(bytes), decoded) == int(bytes.length()) &&
      decoded.type == Zhttp::H3::QPackInsn::InsertWithoutNameRef &&
      decoded.header.name == "accept" &&
      decoded.header.value == "json",
    "insert literal mismatch");

  ZuCHECK(Zhttp::H3::QPack::encodeSectionAck(bytes, 4) > 0 &&
      bytes.length() == 1 && bytes[0] == 0x84 &&
      Zhttp::H3::QPack::decodeDecoderInsnOne(
	span(bytes), decoded) == int(bytes.length()) &&
      decoded.type == Zhttp::H3::QPackInsn::SectionAck &&
      decoded.value == 4,
    "section ack instruction mismatch");
}

void testHuffmanInstructionStorage()
{
  ZuTestScope(testHuffmanInstructionStorage);

  Zhttp::H3::HdrBytes bytes;
  appendHuffmanString(bytes, 0x60, 5, "accept");
  appendHuffmanString(bytes, 0x80, 7, "gzip");

  Zhttp::H3::QPackDecodedInsn decoded;
  ZuCHECK(Zhttp::H3::QPack::decodeEncoderInsnOne(
      span(bytes), decoded) == int(bytes.length()) &&
      decoded.type == Zhttp::H3::QPackInsn::InsertWithoutNameRef &&
      decoded.header.name == "accept" && decoded.header.value == "gzip",
    "Huffman insert literal instruction decode failed");
  bytes.length(0);
  ZuCHECK(decoded.header.name == "accept" && decoded.header.value == "gzip",
    "Huffman instruction spans did not survive input buffer reuse");
}

void testInstructionParserSplit()
{
  ZuTestScope(testInstructionParserSplit);

  Zhttp::H3::HdrBytes encoderBytes;
  Zhttp::H3::HdrBytes decoderBytes;
  Zhttp::H3::QPack::encodeInsertLiteral(encoderBytes, {"accept", "json"});
  Zhttp::H3::QPack::encodeSectionAck(decoderBytes, 9);

  Zhttp::H3::QPackInsnParser encoder;
  Zhttp::H3::QPackInsnParser decoder;
  unsigned encoderApplied = 0, decoderApplied = 0;
  auto decodeEncoder = [](ZuCSpan bytes, Zhttp::H3::QPackDecodedInsn &i) {
    return Zhttp::H3::QPack::decodeEncoderInsnOne(bytes, i);
  };
  auto decodeDecoder = [](ZuCSpan bytes, Zhttp::H3::QPackDecodedInsn &i) {
    return Zhttp::H3::QPack::decodeDecoderInsnOne(bytes, i);
  };
  auto applyEncoder = [&encoderApplied](
    const Zhttp::H3::QPackDecodedInsn &i) {
    if (i.type == Zhttp::H3::QPackInsn::InsertWithoutNameRef &&
	i.header.name == "accept" && i.header.value == "json")
      ++encoderApplied;
    return true;
  };
  auto applyDecoder = [&decoderApplied](
    const Zhttp::H3::QPackDecodedInsn &i) {
    if (i.type == Zhttp::H3::QPackInsn::SectionAck && i.value == 9)
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

void testBuilderCommitFailureAtomic()
{
  ZuTestScope(testBuilderCommitFailureAtomic);

  {
    BuilderState builder;
    builder.params.qpackTableCapacity(256);
    ZuCHECK(builder.tx.setMaxCapacity(256),
      "capacity failure max setup failed");
    builder.encoder.failWrite = 0;
    CaptureTxStream stream;
    builder.request(stream);
    ZuCHECK(txUnchanged(builder.tx),
      "capacity write failure changed tx state");
    ZuCHECK(builder.qpackFailure() ==
	Zhttp::H3::QPackBuildFailure::EncoderCapacityWrite,
      "capacity write failure reason mismatch");
  }
  {
    BuilderState builder;
    builder.params.qpackTableCapacity(256);
    ZuCHECK(builder.tx.setMaxCapacity(256),
      "insert failure max setup failed");
    builder.encoder.failWrite = 1;
    CaptureTxStream stream;
    builder.request(stream);
    ZuCHECK(txUnchanged(builder.tx),
      "insert write failure changed tx state");
    ZuCHECK(builder.qpackFailure() ==
	Zhttp::H3::QPackBuildFailure::EncoderInsertWrite,
      "insert write failure reason mismatch");
  }
  {
    BuilderState builder;
    builder.params.qpackTableCapacity(256);
    ZuCHECK(builder.tx.setMaxCapacity(256),
      "success max setup failed");
    CaptureTxStream stream;
    builder.request(stream);
    ZuCHECK(builder.qpackFailure() == Zhttp::H3::QPackBuildFailure::None &&
	builder.tx.capacity() == 256 && builder.tx.capacitySent &&
	builder.tx.insertCount() > 0,
      "successful dynamic build did not commit once");
  }
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
  ZuTestCall(testFieldRepresentationGoldens);
  ZuTestCall(testParserFieldCallbacks);
  ZuTestCall(testParserInvalidFields);
  ZuTestCall(testParserStrictContentLength);
  ZuTestCall(testParserHeaderScratchAndLimits);
  ZuTestCall(testFieldDecodeAllocationDiscipline);
  ZuTestCall(testSettingsKeyBoundary);
  ZuTestCall(testTxTable);
  ZuTestCall(testTxEvictReferenced);
  ZuTestCall(testRxTxChurn);
  ZuTestCall(testTxSectionStress);
  ZuTestCall(testInstructionEncoding);
  ZuTestCall(testHuffmanInstructionStorage);
  ZuTestCall(testInstructionParserSplit);
  ZuTestCall(testBuilderPeerCapacity);
  ZuTestCall(testBuilderCommitFailureAtomic);
  ZuTestCall(testBuilderQueryPath);
}
