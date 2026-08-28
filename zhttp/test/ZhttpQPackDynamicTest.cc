//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuDerive.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiRxStream.hh>
#include <zlib/Zhttp.hh>

using namespace ZuTestUtil;

static void appendSpan(Zhttp::H3::HdrBytes &bytes, ZuBSpan s)
{
  for (unsigned i = 0, n = s.length(); i < n; ++i) bytes.push(uint8_t(s[i]));
}

static void appendBytes(Zhttp::H3::HdrBytes &bytes, ZuBSpan s)
{
  for (unsigned i = 0, n = s.length(); i < n; ++i) bytes.push(s[i]);
}

namespace ZhttpQPackDynamicTest_ {

using StreamAlloc = ZiIOBufAlloc<256, 4096, "ZhttpQPackDynamicTest.Buf">;
using BuilderHeaders = ZhttpHeaders("accept");
using SeedHeaders = ZhttpHeaders(
  ("x-fixed", "fixed"), "x-runtime",
  ("content-type", "application/json"), ("connection", "close"));

struct CustomTarget {
  template <typename S>
  void print(S &s) const { s << "/printable?"; }

  friend ZuPrintFn ZuPrintType(CustomTarget *);
};
ZuDerive(RxQueue,
  (ZmList<ZiIOBuf, ZmListNode<ZiIOBuf,
    ZmListHeapID<"">>>));
using RxBufAlloc = Zi::IOBufAlloc<RxQueue::Node, 256, 1<<20,
  ZuStringT<"ZhttpQPackDynamicTest.RxBuf">>;
using RxStream = ZiRxStream<RxQueue>;

static ZmRef<RxQueue::Node> rxBuf(ZuBSpan span)
{
  ZmRef<RxQueue::Node> buf = new RxBufAlloc{};
  if (span.length()) buf->append(span);
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

void testVar()
{
  ZuTestScope(testVar);

  static const uint64_t values[] = {
    0x25, 0x1234, 0x12345678, 0x123456789abcdef
  };
  for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
    Zhttp::H3::HdrBytes bytes;
    Zhttp::H3::putVar(bytes, values[i]);
    uint64_t value = 0;
    ZuCHECK(Zhttp::H3::var(bytes, value) && value == values[i],
      "contiguous QUIC varint round trip failed");

    RxStream rx;
    unsigned length = 0;
    if (bytes.length() > 1) {
      unsigned split = bytes.length() - 1;
      rx.push(rxBuf(ZuBSpan{bytes.data(), split}));
      ZuCHECK(!Zhttp::H3::rxVar(rx, 0, value, length),
	"partial reactive QUIC varint was consumed");
      rx.push(rxBuf(ZuBSpan{&bytes[split], 1}));
    } else
      rx.push(rxBuf(ZuBSpan{bytes}));
    ZuCHECK(Zhttp::H3::rxVar(rx, 0, value, length) == 1 &&
	value == values[i] && length == bytes.length(),
      "reactive QUIC varint round trip failed");
  }
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

  bool sendBuf_(ZmRef<ZiIOBuf> buf, bool) {
    if (!buf) return false;
    for (unsigned i = 0; i < buf->length; ++i) bytes.push(buf->data()[i]);
    return true;
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
  public Zhttp::Builder,
  public Zhttp::H3::Request<BuilderState, BuilderHeaders> {
  using Base = Zhttp::H3::Request<BuilderState, BuilderHeaders>;
  using Headers = BuilderHeaders;

  const Zhttp::H3::Params &h3Params() const { return params; }
  Zhttp::H3::QPackTxTable *qpackTx() { return &tx; }
  bool qpackEncoderWrite(ZuBSpan span) { return encoder.write(span); }
  uint64_t streamID() const { return id; }
  unsigned status() const { return 200; }

  template <typename L>
  void operation(L &&l) const {
    if (customTarget)
      l(method, [](auto &&emit) {
	emit([](auto &tx) { tx << CustomTarget{}; });
      });
    else
      l(method, [this](auto &&emit) {
	emit([this](auto &tx) {
	  tx << path;
	  if (query) tx << '?' << query;
	});
      });
  }
  template <typename L> void host(L &&l) const { l("example.com"); }
  template <typename L>
  void protocol(L &&l) const {
    if (protocol_) l(protocol_);
  }
  template <typename Key, typename L>
  void header(L &&l) {
    ++keyedCalls;
    if constexpr (Key{}() == "accept")
      l("application/json");
    else
      l("");
  }
  template <typename L>
  void header(L &&l) {
    ++runtimeProviderCalls;
    if (runtimeHeader) l(runtimeName, runtimeValue);
    if (runtimeHeader2) l("x-runtime-two", "second");
  }

  Zhttp::H3::Params	params;
  Zhttp::H3::QPackTxTable tx;
  CaptureEncoder	encoder;
  uint64_t		id = 1;
  Zhttp::Method::T	method = Zhttp::Method::GET;
  ZuBSpan		path = "/sample";
  ZuBSpan		query = "";
  ZuBSpan		protocol_;
  bool			runtimeHeader = false;
  bool			runtimeHeader2 = false;
  bool			customTarget = false;
  unsigned		keyedCalls = 0;
  unsigned		runtimeProviderCalls = 0;
  ZuBSpan		runtimeName = "server";
  ZuBSpan		runtimeValue = "zhttp-runtime";
};

using ParserHeaders = ZhttpHeaders("x-test");

struct ParserStream;

struct LogicalLink {
  struct Tx { void flush() { } };

  bool streamLocalCap() const { return true; }
  bool streamPeerCap() const { return true; }
  template <typename L>
  void streamTx(L &&l) {
    Tx tx;
    ZuFwd<L>(l)(tx);
  }
  void streamTxEnd() { }
  void streamTxReset() { }
};

struct StreamConsumer {
  template <typename Stream, typename Rx>
  int process(Stream stream, Rx &rx);

  ParserStream	*parser = nullptr;
};

struct ParserStream :
  public Zhttp::Parser,
  public Zhttp::H3::Parser<ParserStream, true, ParserHeaders> {
  using Base = Zhttp::H3::Parser<ParserStream, true, ParserHeaders>;
  using Headers = ParserHeaders;

  ParserStream() : Base{1024}, consumer{this} {
    dispatch.init(link, consumer);
  }
  ~ParserStream() {
    dispatch.disable_();
    dispatch.final_();
  }

  RxStream &rxStream() { return rx; }
  bool retireRx(uint64_t length) { retired += length; return true; }
  void rescheduleDequeue() { ++reschedules; }
  bool resetReceived() const { return reset_; }
  bool stopReceived() const { return stop_; }
  bool finReceived() const { return fin; }
  Zhttp::H3::QPackRxTable *qpackRx() { return &qpackRxTable; }
  const Zhttp::H3::Params &h3Params() const { return params; }

  void push(const Zhttp::H3::HdrBytes &bytes) {
    rx.push(rxBuf(ZuBSpan{bytes}));
  }
  bool operation(
    Zhttp::Method::T method_, Zhttp::Target &target) {
    operationOrder = ++callbackOrder;
    method = method_;
    path.length(0);
    path << target.path;
    protocol_.length(0);
    protocol_ << target.protocol;
    if (protocol_ && acceptStream) {
      ++streamStarts;
      Base::stream();
    }
    return !rejectOperation;
  }
  template <typename Key>
  void header(
      Zhttp::FieldSection::T section, ZuSpan<uint8_t> value) {
    headerSection = section;
    if (!headerOrder) headerOrder = ++callbackOrder;
    if constexpr (Key{}() == "x-test") {
      ++xTestCalls;
      xTestLen = value.length();
      if (value.length() <= 4096) {
	xTest.length(0);
	xTest << value;
      }
    }
  }
  void header(
      Zhttp::FieldSection::T section,
      ZuBSpan name, ZuSpan<uint8_t> value) {
    headerSection = section;
    if (!headerOrder) headerOrder = ++callbackOrder;
    ++runtimeCalls;
    if (name == "host") return;
    runtimeName.length(0);
    runtimeValue.length(0);
    runtimeName << name;
    runtimeValue << value;
  }
  bool bodyInfo(Zhttp::BodyType::T type, uint64_t length) {
    bodyType = type;
    bodyLength = length;
    if (type == Zhttp::BodyType::Fixed) {
      contentLen = length;
      ++contentLenCalls;
    }
    ++headerCalls;
    return true;
  }
  template <typename Rx>
  bool body(Rx &rx) {
    if (!bodyOrder) bodyOrder = ++callbackOrder;
    ++bodyCalls;
    if (rejectBody) return false;
    while (rx) {
      const uint8_t *offered = nullptr;
      if (rx.consume(
	  [this, &offered](ZuSpan<uint8_t> span) -> int64_t {
	    offered = span.data();
	    return partialBody ? 1 : span.length();
	  },
	  [this, &offered](ZuSpan<uint8_t> span) {
	    bodyNoCopy &= span.data() == offered;
	    bodyData << span;
	  }) <= 0)
	return false;
      if (partialBody) break;
    }
    return true;
  }
  template <typename Rx>
  void streamRx_(Rx &rx) { dispatch.process(rx); }
  void streamPeerEnd_() { ++streamEnds; dispatch.disable_(); }
  void streamError_() { ++streamResets; dispatch.disable_(); }
  template <typename Rx>
  void processStream(Rx &rx) {
    while (rx) {
      const uint8_t *offered = nullptr;
      int64_t n = rx.consume(
	  [&offered](ZuSpan<uint8_t> span) -> int64_t {
	    offered = span.data();
	    return 1;
	  },
	  [this, &offered](ZuSpan<uint8_t> span) {
	    streamNoCopy &= span.data() == offered;
	    streamBody << span;
	  });
      if (n <= 0) break;
      if (partialStream) break;
    }
  }
  void complete(Zhttp::H3::ParserState::T state_) {
    completeState = state_;
    ++completeCalls;
  }

  RxStream			rx;
  Zhttp::H3::QPackRxTable	qpackRxTable;
  Zhttp::H3::Params		params;
  Zhttp::StreamDispatch<LogicalLink, StreamConsumer> dispatch;
  StreamConsumer		consumer;
  LogicalLink			link;
  Zhttp::Method::T		method = -1;
  unsigned			callbackOrder = 0;
  unsigned			operationOrder = 0;
  unsigned			headerOrder = 0;
  unsigned			bodyOrder = 0;
  uint64_t			retired = 0;
  unsigned			reschedules = 0;
  ZtString<>			path;
  ZtString<>			xTest;
  unsigned			xTestCalls = 0;
  unsigned			xTestLen = 0;
  unsigned			runtimeCalls = 0;
  int64_t			contentLen = -1;
  unsigned			contentLenCalls = 0;
  unsigned			bodyCalls = 0;
  unsigned			headerCalls = 0;
  uint64_t			bodyLength = 0;
  Zhttp::BodyType::T		bodyType = Zhttp::BodyType::None;
  Zhttp::FieldSection::T	headerSection = Zhttp::FieldSection::Invalid;
  unsigned			streamStarts = 0;
  unsigned			streamEnds = 0;
  unsigned			streamResets = 0;
  unsigned			completeCalls = 0;
  bool				partialBody = false;
  bool				rejectBody = false;
  bool				rejectOperation = false;
  bool				bodyNoCopy = true;
  bool				fin = false;
  bool				reset_ = false;
  bool				stop_ = false;
  bool				acceptStream = false;
  bool				partialStream = false;
  bool				streamNoCopy = true;
  Zhttp::H3::ParserState::T	completeState =
    Zhttp::H3::ParserState::Initial;
  ZtString<>			protocol_;
  ZtString<>			bodyData;
  ZtString<>			streamBody;
  uint64_t			streamError = 0;
  ZtString<>			runtimeName;
  ZtString<>			runtimeValue;
};

struct ResponseParserStream :
  public Zhttp::Parser,
  public Zhttp::H3::Parser<ResponseParserStream, false, ParserHeaders> {
  using Base =
    Zhttp::H3::Parser<ResponseParserStream, false, ParserHeaders>;
  using Headers = ParserHeaders;

  ResponseParserStream() : Base{1024} { }
  RxStream &rxStream() { return rx; }
  bool retireRx(uint64_t length) { retired += length; return true; }
  void rescheduleDequeue() { }
  bool resetReceived() const { return false; }
  bool stopReceived() const { return false; }
  bool finReceived() const { return fin; }
  Zhttp::H3::QPackRxTable *qpackRx() { return &qpackRxTable; }
  const Zhttp::H3::Params &h3Params() const { return params; }
  bool enable1xx() const { return informational; }

  void push(const Zhttp::H3::HdrBytes &bytes) {
    rx.push(rxBuf(ZuBSpan{bytes}));
  }
  bool operation(Zhttp::Method::T, Zhttp::Target &) { return true; }
  void status(unsigned value) {
    statusOrder = ++callbackOrder;
    status_ = value;
    ++statusCalls;
  }
  bool bodyInfo(Zhttp::BodyType::T type, uint64_t length) {
    if (type == Zhttp::BodyType::Fixed) contentLen = length;
    return true;
  }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if (!headerOrder) headerOrder = ++callbackOrder;
    if constexpr (Key{}() == "x-test") xTest = value;
  }
  template <typename Rx> bool body(Rx &rx_) {
    if (!bodyOrder) bodyOrder = ++callbackOrder;
    return Zhttp::bodyEach(
      rx_, [this](ZuSpan<uint8_t> value) { bodyData << value; });
  }
  void complete(Zhttp::H3::ParserState::T state) {
    completeState = state;
    ++completeCalls;
  }

  RxStream			rx;
  Zhttp::H3::QPackRxTable	qpackRxTable;
  Zhttp::H3::Params		params;
  uint64_t			retired = 0;
  uint64_t			contentLen = 0;
  unsigned			status_ = 0;
  unsigned			statusCalls = 0;
  unsigned			callbackOrder = 0;
  unsigned			statusOrder = 0;
  unsigned			headerOrder = 0;
  unsigned			bodyOrder = 0;
  unsigned			completeCalls = 0;
  bool				fin = false;
  bool				informational = false;
  Zhttp::H3::ParserState::T	completeState =
    Zhttp::H3::ParserState::Initial;
  ZtString<>			xTest;
  ZtString<>			bodyData;
};

template <typename Stream, typename Rx>
int StreamConsumer::process(Stream, Rx &rx)
{
  parser->processStream(rx);
  return 1;
}

struct CxnStream :
  public Zhttp::H3::CxnParser<CxnStream> {
  using Base = Zhttp::H3::CxnParser<CxnStream>;

  CxnStream() { qpackTxTable.init(4096, 64); }

  RxStream &rxStream() { return rx; }
  bool retireRx(uint64_t length) { retired += length; return true; }
  void rescheduleDequeue() { ++reschedules; }
  bool resetReceived() const { return false; }
  bool stopReceived() const { return false; }
  bool finReceived() const { return false; }
  bool qpackTxInsn(Zhttp::H3::QPackInsn::T type, uint64_t value) {
    return qpackTxTable.applyDecoder(type, value);
  }
  bool qpackTxMaxCapacity(uint64_t capacity) {
    return capacity <= uint32_t(-1) &&
      qpackTxTable.peerCapacity(uint32_t(capacity));
  }
  bool qpackTxBlocked(uint64_t blocked) {
    if (blocked > uint32_t(-1)) return false;
    qpackTxTable.peerBlocked(uint32_t(blocked));
    return true;
  }
  void setting(uint64_t key, uint64_t value) {
    Base::setting(key, value);
    if (key == 0x08) extendedConnect = int(value);
    ++settings;
  }

  void push(const Zhttp::H3::HdrBytes &bytes) {
    rx.push(rxBuf(ZuBSpan{bytes}));
  }

  RxStream			rx;
  Zhttp::H3::QPackTxTable	qpackTxTable;
  uint64_t			retired = 0;
  unsigned			reschedules = 0;
  unsigned			settings = 0;
  int				extendedConnect = -1;
};

static bool headersPayload(ZuSpan<uint8_t> bytes, ZuSpan<uint8_t> &payload)
{
  unsigned o = 0;
  uint64_t type = 0, len = 0;
  if (Zhttp::H3::var(bytes, o, type) < 0 ||
      Zhttp::H3::var(bytes, o, len) < 0 ||
      type != 0x01 || bytes.length() != o + len)
    return false;
  payload = {bytes.data() + o, unsigned(len)};
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
  ZuBSpan s)
{
  Zhttp::H3::putPref(bytes, prefix, prefixBits, s.length());
  appendSpan(bytes, s);
}

} // namespace ZhttpQPackDynamicTest_

using namespace ZhttpQPackDynamicTest_;

static void appendHuffmanString(
  Zhttp::H3::HdrBytes &bytes, uint8_t prefix, unsigned prefixBits,
  ZuBSpan s)
{
  Zhttp::H3::HdrBytes encoded;
  encoded.length(Zhttp::Compression::Huffman::enclen(s.length()));
  uint64_t n = Zhttp::Compression::Huffman::encode(encoded.span(), s);
  encoded.length(n);
  Zhttp::H3::putPref(bytes, prefix, prefixBits, encoded.length());
  appendSpan(bytes, encoded);
}

void testRxTable()
{
  ZuTestScope(testRxTable);

  Zhttp::H3::QPackRxTable table;
  table.init(128);
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
  table.init(256);
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
      bytes, table,
      [&seen, &sawPath, &sawAuthority](Zhttp::H3::Header h) {
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
      bytes, table,
      [&seen, &sawPath, &sawAuthority](Zhttp::H3::Header h) {
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
  table.init(512);
  table.setCapacity(512);
  table.insert({"x-a", "one"});
  table.insert({"x-b", "two"});

  Zhttp::H3::FieldSectionPrefix prefix;
  prefix.requiredInsertCount = table.insertCount();
  prefix.base = table.insertCount();

  auto decodeOne = [&table](
    Zhttp::H3::HdrBytes &bytes, ZuBSpan name, ZuBSpan value, auto check) {
    unsigned seen = 0;
    bool matched = false;
    int n = Zhttp::H3::QPack::decodeFieldSection(
      bytes, &table,
      [&matched, &seen, &name, &value, &check](
	  Zhttp::H3::DecodedHeader h_, Zhttp::H3::QPackFieldFlags flags_) {
	matched = h_.name == name && h_.value == value && check(flags_);
	++seen;
      });
    return n == int(bytes.length()) && seen == 1 && matched;
  };
  Zhttp::H3::HdrBytes bytes;
  auto reset = [&bytes, &prefix, &table]() -> Zhttp::H3::HdrBytes & {
    bytes.length(0);
    Zhttp::H3::QPack::encodeFieldSectionPrefix(
      bytes, prefix, table.maxCapacity());
    return bytes;
  };

  uint64_t nameIndex = 0;
  ZuCHECK(Zhttp::H3::QPack::staticNameIndex(":path", nameIndex),
    "static name index setup failed");

  auto &staticIndexed = reset();
  staticIndexed.push(0xd1);
  ZuCHECK(decodeOne(staticIndexed, ":method", "GET",
      [](const Zhttp::H3::QPackFieldFlags &flags) {
	return flags.staticRef && !flags.dynamicRef && !flags.postBase &&
	  !flags.neverIndex;
      }),
    "static indexed field flags mismatch");

  auto &dynamicIndexed = reset();
  dynamicIndexed.push(0x80);
  ZuCHECK(decodeOne(dynamicIndexed, "x-b", "two",
      [](const Zhttp::H3::QPackFieldFlags &flags) {
	return flags.dynamicRef && !flags.staticRef && !flags.postBase;
      }),
    "dynamic relative indexed field flags mismatch");

  prefix.base = 0;
  auto &postBaseIndexed = reset();
  postBaseIndexed.push(0x10);
  ZuCHECK(decodeOne(postBaseIndexed, "x-a", "one",
      [](const Zhttp::H3::QPackFieldFlags &flags) {
	return flags.dynamicRef && flags.postBase;
      }),
    "dynamic post-base indexed field flags mismatch");

  prefix.base = table.insertCount();
  auto &staticName = reset();
  staticName.push(uint8_t(0x50 | nameIndex));
  appendString(staticName, 0x00, 7, "/gold");
  ZuCHECK(decodeOne(staticName, ":path", "/gold",
      [](const Zhttp::H3::QPackFieldFlags &flags) {
	return flags.staticRef && !flags.neverIndex;
      }),
    "literal static name reference flags mismatch");

  auto &staticNameNever = reset();
  staticNameNever.push(uint8_t(0x70 | nameIndex));
  appendString(staticNameNever, 0x00, 7, "/never");
  ZuCHECK(decodeOne(staticNameNever, ":path", "/never",
      [](const Zhttp::H3::QPackFieldFlags &flags) {
	return flags.staticRef && flags.neverIndex;
      }),
    "literal static name never-index flags mismatch");

  auto &dynamicName = reset();
  dynamicName.push(0x40);
  appendString(dynamicName, 0x00, 7, "dyn");
  ZuCHECK(decodeOne(dynamicName, "x-b", "dyn",
      [](const Zhttp::H3::QPackFieldFlags &flags) {
	return flags.dynamicRef && !flags.neverIndex;
      }),
    "literal dynamic name reference flags mismatch");

  auto &dynamicNameNever = reset();
  dynamicNameNever.push(0x60);
  appendString(dynamicNameNever, 0x00, 7, "dyn-never");
  ZuCHECK(decodeOne(dynamicNameNever, "x-b", "dyn-never",
      [](const Zhttp::H3::QPackFieldFlags &flags) {
	return flags.dynamicRef && flags.neverIndex;
      }),
    "literal dynamic name never-index flags mismatch");

  prefix.base = 0;
  auto &postBaseName = reset();
  postBaseName.push(0x00);
  appendString(postBaseName, 0x00, 7, "post");
  ZuCHECK(decodeOne(postBaseName, "x-a", "post",
      [](const Zhttp::H3::QPackFieldFlags &flags) {
	return flags.dynamicRef && flags.postBase && !flags.neverIndex;
      }),
    "literal post-base name flags mismatch");

  auto &postBaseNameNever = reset();
  postBaseNameNever.push(0x08);
  appendString(postBaseNameNever, 0x00, 7, "post-never");
  ZuCHECK(decodeOne(postBaseNameNever, "x-a", "post-never",
      [](const Zhttp::H3::QPackFieldFlags &flags) {
	return flags.dynamicRef && flags.postBase && flags.neverIndex;
      }),
    "literal post-base name never-index flags mismatch");

  prefix.base = table.insertCount();
  auto &literalName = reset();
  appendString(literalName, 0x20, 3, "x-lit");
  appendString(literalName, 0x00, 7, "plain");
  ZuCHECK(decodeOne(literalName, "x-lit", "plain",
      [](const Zhttp::H3::QPackFieldFlags &flags) {
	return !flags.staticRef && !flags.dynamicRef && !flags.neverIndex;
      }),
    "literal name flags mismatch");

  auto &literalNameNever = reset();
  appendString(literalNameNever, 0x30, 3, "x-sec");
  appendString(literalNameNever, 0x00, 7, "secret");
  ZuCHECK(decodeOne(literalNameNever, "x-sec", "secret",
      [](const Zhttp::H3::QPackFieldFlags &flags) {
	return flags.neverIndex;
      }),
    "literal name never-index flags mismatch");

  auto &huffman = reset();
  appendHuffmanString(huffman, 0x28, 3, "x-h");
  appendHuffmanString(huffman, 0x80, 7, "zip");
  ZuCHECK(decodeOne(huffman, "x-h", "zip",
      [](const Zhttp::H3::QPackFieldFlags &) { return true; }),
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

static void putLiteralField(
  Zhttp::H3::HdrBytes &payload, ZuBSpan name, ZuBSpan value,
  bool huffmanValue = false, bool huffmanName = false);

static void putRequestPseudos(
  Zhttp::H3::HdrBytes &payload, ZuBSpan path = "/parser")
{
  putLiteralField(payload, ":method", "GET");
  putLiteralField(payload, ":scheme", "https");
  putLiteralField(payload, ":authority", "example.com");
  putLiteralField(payload, ":path", path);
}

void testParserFieldCallbacks()
{
  ZuTestScope(testParserFieldCallbacks);

  ParserStream parser;

  Zhttp::H3::Header initialHeaders[] = {
    {":method", "GET"},
    {":scheme", "https"},
    {":authority", "example.com"},
    {":path", "/parser"},
    {"x-test", "initial"},
    {"x-runtime", "plain"}
  };
  Zhttp::H3::HdrBytes frame;
  putHeadersFrame(frame, ZuSpan<Zhttp::H3::Header>{initialHeaders, 6});
  parser.push(frame);
  ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Body &&
      parser.method == Zhttp::Method::GET &&
      parser.path == "/parser" &&
      parser.xTestCalls == 1 && parser.xTest == "initial" &&
      parser.runtimeCalls == 2 &&
      parser.runtimeName == "x-runtime" &&
      parser.runtimeValue == "plain" && parser.operationOrder &&
      parser.operationOrder < parser.headerOrder &&
      parser.headerSection == Zhttp::FieldSection::Final &&
      parser.headerCalls == 1 &&
      parser.bodyType == Zhttp::BodyType::Streamed && !parser.bodyLength,
    "parser did not deliver initial pseudo/regular fields");

  Zhttp::H3::Header trailerHeaders[] = {
    {"x-test", "trailer"}
  };
  frame.length(0);
  putHeadersFrame(frame, ZuSpan<Zhttp::H3::Header>{trailerHeaders, 1});
  parser.push(frame);
  ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Trailers &&
      parser.xTestCalls == 2 && parser.xTest == "trailer" &&
      parser.headerSection == Zhttp::FieldSection::Trailers,
    "parser did not deliver trailer fields");

  ParserStream invalid;
  invalid.extendedConnect(true);
  Zhttp::H3::Header invalidHeaders[] = {
    {":method", "CONNECT"},
    {":scheme", "https"},
    {":authority", "example.com"},
    {":path", "/chat"},
    {":protocol", "opaque"},
    {"x-test", "initial"},
    {"content-length", "0"}
  };
  frame.length(0);
  putHeadersFrame(frame,
    ZuSpan<Zhttp::H3::Header>{invalidHeaders, 7});
  invalid.push(frame);
  ZuCHECK(invalid.process(invalid) == Zhttp::H3::ParserState::Error &&
      invalid.operationOrder < invalid.headerOrder &&
      invalid.completeCalls == 1 &&
      invalid.completeState == Zhttp::H3::ParserState::Error &&
      invalid.error().code == Zhttp::RequestErrorCode::Malformed &&
      invalid.error().scope == Zhttp::RequestErrorScope::Stream &&
      !invalid.error().responsePossible,
    "post-operation H3 validation failure completes with an error");

  ParserStream oversized;
  Zhttp::H3::Header oversizedHeaders[] = {
    {":method", "POST"},
    {":scheme", "https"},
    {":authority", "example.com"},
    {":path", "/body"},
    {"content-length", "1025"}
  };
  frame.length(0);
  putHeadersFrame(frame,
    ZuSpan<Zhttp::H3::Header>{oversizedHeaders, 5});
  oversized.push(frame);
  ZuCHECK(oversized.process(oversized) == Zhttp::H3::ParserState::Error &&
      oversized.error().code == Zhttp::RequestErrorCode::ContentTooLarge &&
      oversized.error().scope == Zhttp::RequestErrorScope::Request &&
      oversized.error().responsePossible &&
      Zhttp::requestErrorStatus(oversized.error().code) == 413,
    "oversized H3 request classification mismatch");

  ParserStream headersTooLarge;
  headersTooLarge.params.maxHeaderListSize(1);
  frame.length(0);
  putHeadersFrame(frame,
    ZuSpan<Zhttp::H3::Header>{oversizedHeaders, 5});
  headersTooLarge.push(frame);
  ZuCHECK(headersTooLarge.process(headersTooLarge) ==
      Zhttp::H3::ParserState::Error &&
      headersTooLarge.error().code ==
	Zhttp::RequestErrorCode::HeadersTooLarge &&
      headersTooLarge.error().scope == Zhttp::RequestErrorScope::Stream &&
      !headersTooLarge.error().responsePossible &&
      Zhttp::requestErrorStatus(headersTooLarge.error().code) == 431,
    "oversized H3 header classification mismatch");

  ResponseParserStream response;
  Zhttp::H3::Header informationalHeaders[] = {
    {":status", "103"},
    {"x-test", "early"}
  };
  frame.length(0);
  putHeadersFrame(frame,
    ZuSpan<Zhttp::H3::Header>{informationalHeaders, 2});
  Zhttp::H3::Header responseHeaders[] = {
    {":status", "200"},
    {"x-test", "response"},
    {"content-length", "3"}
  };
  Zhttp::H3::HdrBytes finalFrame;
  putHeadersFrame(finalFrame,
    ZuSpan<Zhttp::H3::Header>{responseHeaders, 3});
  appendBytes(frame, finalFrame);
  Zhttp::H3::HdrBytes responseData;
  putFrame(responseData, 0x00, ZuBSpan{"abc"});
  appendBytes(frame, responseData);
  response.fin = true;
  response.push(frame);
  ZuCHECK(response.process(response) == Zhttp::H3::ParserState::Complete &&
      response.status_ == 200 && response.statusCalls == 1 &&
      response.xTest == "response" &&
      response.bodyData == "abc" &&
      response.statusOrder < response.headerOrder &&
      response.headerOrder < response.bodyOrder &&
      response.completeCalls == 1,
    "H3 response status precedes headers and body");

  ResponseParserStream invalidResponse;
  Zhttp::H3::Header invalidResponseHeaders[] = {
    {":status", "200"},
    {"x-test", "response"},
    {"connection", "close"}
  };
  frame.length(0);
  putHeadersFrame(frame,
    ZuSpan<Zhttp::H3::Header>{invalidResponseHeaders, 3});
  invalidResponse.push(frame);
  ZuCHECK(invalidResponse.process(invalidResponse) ==
      Zhttp::H3::ParserState::Error &&
      invalidResponse.statusOrder < invalidResponse.headerOrder &&
      invalidResponse.completeCalls == 1 &&
      invalidResponse.completeState == Zhttp::H3::ParserState::Error,
    "post-status H3 validation failure completes with an error");
}

static Zhttp::H3::HdrBytes messageHeaders(uint64_t length)
{
  Zhttp::H3::Header headers[] = {
    {":method", "POST"},
    {":scheme", "https"},
    {":authority", "example.com"},
    {":path", "/body"},
    {"content-length", {}}
  };
  ZtString<> length_;
  length_ << length;
  headers[4].value = length_;
  Zhttp::H3::HdrBytes frame;
  putHeadersFrame(frame, ZuSpan<Zhttp::H3::Header>{headers, 5});
  return frame;
}

static bool pushSplit(
  ParserStream &parser, const Zhttp::H3::HdrBytes &bytes)
{
  for (unsigned i = 0, n = bytes.length(); i < n; ++i) {
    Zhttp::H3::HdrBytes byte;
    byte.push(bytes[i]);
    parser.push(byte);
    if (parser.process(parser) == Zhttp::H3::ParserState::Error)
      return false;
  }
  return true;
}

void testParserBodyStream()
{
  ZuTestScope(testParserBodyStream);

  {
    ParserStream parser;
    parser.rejectOperation = true;
    parser.h3(
      &parser.qpackRxTable, nullptr, nullptr, &parser.streamError,
      [](void *ptr, uint64_t error) {
	*static_cast<uint64_t *>(ptr) = error;
      }, 1, &parser.params);
    parser.push(messageHeaders(3));
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Error &&
	parser.error().code == Zhttp::RequestErrorCode::OperationRejected &&
	parser.error().scope == Zhttp::RequestErrorScope::Request &&
	parser.error().responsePossible &&
	parser.streamError == Zhttp::H3::RequestCancelled &&
	parser.operationOrder && !parser.headerOrder && !parser.bodyCalls &&
	parser.completeCalls == 1,
      "H3 operation rejection did not complete exactly once");
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Error &&
	parser.completeCalls == 1,
      "H3 operation rejection produced duplicate callbacks");
  }
  {
    ParserStream parser;
    parser.rejectBody = true;
    parser.h3(
      &parser.qpackRxTable, nullptr, nullptr, &parser.streamError,
      [](void *ptr, uint64_t error) {
	*static_cast<uint64_t *>(ptr) = error;
      }, 1, &parser.params);
    parser.push(messageHeaders(3));
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Body,
      "H3 body rejection setup failed");
    Zhttp::H3::HdrBytes data;
    putFrame(data, 0x00, ZuBSpan{"abc"});
    parser.push(data);
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Error &&
	parser.error().code == Zhttp::RequestErrorCode::BodyRejected &&
	parser.error().scope == Zhttp::RequestErrorScope::Request &&
	parser.error().responsePossible &&
	parser.streamError == Zhttp::H3::RequestCancelled &&
	parser.bodyCalls == 1 && parser.completeCalls == 1 && !parser.rx,
      "H3 body rejection was not a request-cancelled stream failure");
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Error &&
	parser.bodyCalls == 1 && parser.completeCalls == 1,
      "H3 body rejection produced duplicate callbacks");

    ParserStream sibling;
    sibling.push(messageHeaders(3));
    ZuCHECK(sibling.process(sibling) == Zhttp::H3::ParserState::Body,
      "H3 sibling setup failed after rejection");
    sibling.push(data);
    sibling.fin = true;
    ZuCHECK(sibling.process(sibling) == Zhttp::H3::ParserState::Complete &&
	sibling.bodyData == "abc" && sibling.completeCalls == 1,
      "independent H3 sibling did not survive body rejection");
  }
  {
    ParserStream parser;
    parser.push(messageHeaders(3));
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Body,
      "H3 ordinary body headers failed");

    Zhttp::H3::HdrBytes empty;
    putFrame(empty, 0x00, ZuBSpan{});
    parser.push(empty);
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Body &&
	!parser.bodyCalls,
      "empty H3 DATA emitted a body callback");

    Zhttp::H3::HdrBytes data;
    putFrame(data, 0x00, ZuBSpan{"abc"});
    parser.push(data);
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Body &&
	parser.bodyCalls == 1 && parser.bodyData == "abc" &&
	parser.bodyNoCopy && parser.operationOrder < parser.headerOrder &&
	parser.headerOrder < parser.bodyOrder,
      "H3 DATA did not transfer native payload storage");

    Zhttp::H3::Header trailers[] = {{"x-test", "done"}};
    Zhttp::H3::HdrBytes trailer;
    putHeadersFrame(trailer, ZuSpan<Zhttp::H3::Header>{trailers, 1});
    parser.push(trailer);
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Trailers &&
	parser.xTest == "done",
      "H3 trailers overtook or lost preceding body input");
    parser.fin = true;
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Complete &&
	parser.completeCalls == 1,
      "H3 FIN did not follow body and trailers exactly once");
  }
  {
    ParserStream parser;
    Zhttp::H3::HdrBytes bytes = messageHeaders(3);
    Zhttp::H3::HdrBytes data;
    putFrame(data, 0x00, ZuBSpan{"abc"});
    appendBytes(bytes, data);
    ZuCHECK(pushSplit(parser, bytes) &&
	parser.bodyData == "abc" && parser.bodyCalls == 1,
	"byte-split H3 DATA was exposed only after the frame completed");
    parser.fin = true;
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Complete,
      "byte-split H3 message did not complete on FIN");
  }
  {
    ParserStream parser;
    parser.partialBody = true;
    parser.push(messageHeaders(3));
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Body,
      "partial H3 body setup failed");
    Zhttp::H3::HdrBytes data;
    putFrame(data, 0x00, ZuBSpan{"abc"});
    uint64_t retired = parser.retired;
    parser.push(data);
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Body &&
	parser.bodyData == "a" && !parser.completeCalls && !parser.rx &&
	parser.retired - retired == data.length() - 2,
	"unconsumed H3 message DATA was not retained for the application");
    parser.fin = true;
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Complete &&
	parser.completeCalls == 1 &&
	parser.retired - retired == data.length(),
	"H3 FIN did not discard unread application body data");
  }
  {
    ParserStream parser;
    parser.push(messageHeaders(3));
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Body,
      "H3 reset setup failed");
    parser.reset_ = true;
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Cancelled &&
	parser.completeCalls == 1,
      "H3 ordinary-body reset was not terminal exactly once");
  }
  {
    ParserStream parser;
    parser.push(messageHeaders(0));
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Body,
      "H3 STOP_SENDING setup failed");
    parser.stop_ = true;
    parser.fin = true;
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Complete &&
	parser.completeCalls == 1,
      "H3 STOP_SENDING incorrectly cancelled the response direction");
    parser.reset_ = true;
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Complete &&
	parser.completeCalls == 1,
      "terminal H3 state regressed after completion");
  }
}

void testParserHuffmanRuntimeHeader()
{
  ZuTestScope(testParserHuffmanRuntimeHeader);

  ParserStream parser;
  Zhttp::H3::HdrBytes payload;
  payload.push(0);
  payload.push(0);
  putRequestPseudos(payload);
  putLiteralField(payload, "x-hpack-key", "hpack-value", true, true);
  Zhttp::H3::HdrBytes frame;
  putFrame(frame, 0x01, ZuBSpan{payload});
  parser.push(frame);
  ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Body &&
      parser.runtimeCalls == 2 &&
      parser.runtimeName == "x-hpack-key" &&
      parser.runtimeValue == "hpack-value",
    "Huffman literal runtime header was not delivered");
}

void testParserInvalidFields()
{
  ZuTestScope(testParserInvalidFields);

  {
    ParserStream parser;
    Zhttp::H3::Header initialHeaders[] = {
      {":method", "GET"},
      {":scheme", "https"},
      {":authority", "example.com"},
      {":path", "/parser"}
    };
    Zhttp::H3::HdrBytes frame;
    putHeadersFrame(frame, ZuSpan<Zhttp::H3::Header>{initialHeaders, 4});
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
      {":scheme", "https"},
      {":authority", "example.com"},
      {":path", "/parser"},
      {"content-length", values[i]}
    };
    Zhttp::H3::HdrBytes frame;
    putHeadersFrame(frame, ZuSpan<Zhttp::H3::Header>{initialHeaders, 5});
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
  Zhttp::H3::HdrBytes &payload, ZuBSpan name, ZuBSpan value,
  bool huffmanValue, bool huffmanName)
{
  if (huffmanName)
    appendHuffmanString(payload, 0x28, 3, name);
  else
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
    putRequestPseudos(payload);
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
    putRequestPseudos(payload);
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
    for (unsigned i = 0; i < 300; ++i) longName << 'a';
    Zhttp::H3::HdrBytes payload;
    payload.push(0);
    payload.push(0);
    putRequestPseudos(payload);
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
    putRequestPseudos(payload);
    putLiteralField(payload, "x-test", "value-too-large");
    Zhttp::H3::HdrBytes frame;
    putFrame(frame, 0x01, ZuBSpan{payload});
    parser.push(frame);
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Error,
      "oversized H3 field section was accepted");
  }
}

static Zhttp::H3::HdrBytes extendedConnectHeaders()
{
  Zhttp::H3::HdrBytes payload;
  payload.push(0);
  payload.push(0);
  putLiteralField(payload, ":method", "CONNECT");
  putLiteralField(payload, ":scheme", "https");
  putLiteralField(payload, ":authority", "example.com");
  putLiteralField(payload, ":path", "/stream");
  putLiteralField(payload, ":protocol", "opaque");
  Zhttp::H3::HdrBytes frame;
  putFrame(frame, 0x01, ZuBSpan{payload});
  return frame;
}

void testExtendedConnectRx()
{
  ZuTestScope(testExtendedConnectRx);

  {
    ParserStream parser;
    parser.acceptStream = true;
    parser.push(extendedConnectHeaders());
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Error &&
	parser.completeCalls == 1,
      "H3 Extended CONNECT was accepted without local capability");
  }
  {
    ParserStream parser;
    parser.acceptStream = true;
    parser.extendedConnect(true);
    parser.push(extendedConnectHeaders());
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Stream &&
	parser.protocol_ == "opaque" && parser.headerCalls == 1,
      "H3 Extended CONNECT did not enter stream state");

    Zhttp::H3::HdrBytes frame;
    putFrame(frame, 0x00, ZuBSpan{"a"});
    parser.push(frame);
    frame.length(0);
    putFrame(frame, 0x00, ZuBSpan{"bc"});
    parser.push(frame);
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Stream &&
	parser.streamBody == "abc" && parser.streamNoCopy,
      "H3 stream DATA was not delivered in order without copying");

    parser.fin = true;
    ZuCHECK(parser.process(parser) ==
	Zhttp::H3::ParserState::RemoteClosed &&
	parser.streamStarts == 1 && parser.streamEnds == 1 &&
	!parser.streamResets &&
	!parser.completeCalls,
      "H3 stream FIN was not ordered after payload");
  }
  {
    ParserStream parser;
    parser.acceptStream = true;
    parser.extendedConnect(true);
    parser.push(extendedConnectHeaders());
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Stream,
      "H3 reset setup did not enter stream state");
    parser.reset_ = true;
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Cancelled &&
	parser.streamResets == 1 && parser.completeCalls == 1,
      "H3 RESET_STREAM did not terminate the stream exactly once");
  }
  {
    ParserStream parser;
    parser.acceptStream = true;
    parser.extendedConnect(true);
    parser.push(extendedConnectHeaders());
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Stream,
      "H3 stop setup did not enter stream state");
    parser.stop_ = true;
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Cancelled &&
	parser.streamResets == 1 && parser.completeCalls == 1,
      "H3 STOP_SENDING did not terminate the stream exactly once");
  }
  {
    ParserStream parser;
    parser.acceptStream = true;
    parser.partialStream = true;
    parser.extendedConnect(true);
    parser.push(extendedConnectHeaders());
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Stream,
      "H3 partial-consume setup did not enter stream state");
    Zhttp::H3::HdrBytes frame;
    putFrame(frame, 0x00, ZuBSpan{"abc"});
    uint64_t retired = parser.retired;
    parser.push(frame);
    ZuCHECK(parser.process(parser) == Zhttp::H3::ParserState::Stream &&
	parser.streamBody == "a" && !parser.streamResets &&
	!parser.completeCalls && !parser.rx &&
	parser.retired - retired == frame.length() - 2,
	"unconsumed H3 stream DATA was not retained for the application");
    parser.fin = true;
    ZuCHECK(parser.process(parser) ==
	Zhttp::H3::ParserState::RemoteClosed &&
	parser.streamEnds == 1 && !parser.streamResets &&
	!parser.completeCalls &&
	parser.retired - retired == frame.length(),
	"H3 stream FIN did not discard unread application data");
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
  ZuSpan<uint8_t> input = bytes;
  const uint8_t *begin = input.data();
  const uint8_t *end = begin + bytes.length();
  unsigned seen = 0;
  bool inputBacked = true;
  ZuCHECK(Zhttp::H3::QPack::decodeLiteral(
      input,
      [&inputBacked, &seen, begin, end](Zhttp::H3::Header h) {
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
      bytes,
      [&seen](Zhttp::H3::Header h) {
	if (h.name == "x" && h.value == "v") ++seen;
      }) == int(bytes.length()) && seen == 20,
    "many Huffman field literals did not decode through scratch path");
}

void testSettingsKeyBoundary()
{
  ZuTestScope(testSettingsKeyBoundary);

  for (unsigned enabled = 0; enabled <= 1; ++enabled) {
    CxnStream stream;
    Zhttp::H3::HdrBytes settings;
    putSetting(settings, 0x08, enabled);
    Zhttp::H3::HdrBytes bytes;
    Zhttp::H3::putVar(bytes, 0x00);
    putFrame(bytes, 0x04, ZuBSpan{settings});
    stream.push(bytes);
    ZuCHECK(stream.process(stream) ==
	Zhttp::H3::CxnState::PeerSettingsReceived &&
	stream.extendedConnect == int(enabled),
      "valid SETTINGS_ENABLE_CONNECT_PROTOCOL value was not retained");
  }
  {
    CxnStream stream;
    Zhttp::H3::HdrBytes bytes;
    Zhttp::H3::putVar(bytes, 0x00);
    putFrame(bytes, 0x04, ZuBSpan{});
    stream.push(bytes);
    ZuCHECK(stream.process(stream) ==
	Zhttp::H3::CxnState::PeerSettingsReceived &&
	stream.extendedConnect < 0,
      "absent SETTINGS_ENABLE_CONNECT_PROTOCOL changed capability");
  }
  {
    CxnStream stream;
    uint32_t orderSlots = stream.qpackTxTable.orderSlots();
    uint32_t exactSlots = stream.qpackTxTable.exactSlots();
    uint32_t nameSlots = stream.qpackTxTable.nameSlots();
    uint32_t sectionSlots = stream.qpackTxTable.sectionSlots();
    Zhttp::H3::HdrBytes settings;
    putSetting(settings, 0x01, uint32_t(-1));
    putSetting(settings, 0x07, 77);
    Zhttp::H3::HdrBytes bytes;
    Zhttp::H3::putVar(bytes, 0x00);
    putFrame(bytes, 0x04, ZuBSpan{settings});
    stream.push(bytes);
    ZuCHECK(stream.process(stream) ==
	Zhttp::H3::CxnState::PeerSettingsReceived &&
	stream.qpackTxTable.peerCapacity() == uint32_t(-1) &&
	stream.qpackTxTable.peerBlocked() == 77 &&
	stream.qpackTxTable.orderSlots() == orderSlots &&
	stream.qpackTxTable.exactSlots() == exactSlots &&
	stream.qpackTxTable.nameSlots() == nameSlots &&
	stream.qpackTxTable.sectionSlots() == sectionSlots,
      "peer QPACK SETTINGS allocated or were not retained");
  }
  {
    CxnStream stream;
    Zhttp::H3::HdrBytes settings;
    putSetting(settings, 0x08, 1);
    putSetting(settings, 0x08, 1);
    Zhttp::H3::HdrBytes bytes;
    Zhttp::H3::putVar(bytes, 0x00);
    putFrame(bytes, 0x04, ZuBSpan{settings});
    stream.push(bytes);
    ZuCHECK(stream.process(stream) == Zhttp::H3::CxnState::Error,
      "duplicate SETTINGS_ENABLE_CONNECT_PROTOCOL was accepted");
  }
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
  {
    CxnStream stream;
    Zhttp::H3::HdrBytes settings;
    putSetting(settings, 0x08, 2);
    Zhttp::H3::HdrBytes bytes;
    Zhttp::H3::putVar(bytes, 0x00);
    putFrame(bytes, 0x04, ZuBSpan{settings});
    stream.push(bytes);
    ZuCHECK(stream.process(stream) == Zhttp::H3::CxnState::Error,
      "invalid SETTINGS_ENABLE_CONNECT_PROTOCOL value was accepted");
  }
}

void testTxTable()
{
  ZuTestScope(testTxTable);

  Zhttp::H3::QPackTxTable table;
  ZuCHECK(table.init(128, 8) && table.peerCapacity(128) &&
      table.setCapacity(128),
    "tx capacity setup failed");
  uint64_t abs = uint64_t(-1);
  ZuCHECK(table.insert({"accept", "application/json"}, &abs) && !abs,
    "tx insert failed");
  ZuCHECK(table.insertCount() == 1 && table.used() &&
      table.used() <= table.capacity(),
    "tx insert accounting mismatch");
  ZuCHECK(table.find("accept", "application/json") &&
      table.find("accept", "application/json")->abs == abs,
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
  ZuBSpan oversized =
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

void testTxNameLookup()
{
  ZuTestScope(testTxNameLookup);

  Zhttp::H3::QPackTxTable table;
  uint64_t first = 0, second = 0;
  ZuCHECK(table.init(256, 8) && table.peerCapacity(256) &&
      table.setCapacity(256) &&
      table.insert({"x-name", "one"}, &first) &&
      table.insert({"x-name", "two"}, &second),
    "dynamic name table setup failed");
  ZuCHECK(table.findName("x-name") &&
      table.findName("x-name")->abs == second,
    "dynamic name lookup did not select newest entry");
  ZuCHECK(table.insertCountIncrement(1) &&
      table.findName("x-name", table.insertCount()) &&
      table.findName("x-name", table.insertCount())->abs == first,
    "dynamic name lookup did not fall back to older eligible entry");
  ZuCHECK(table.insertCountIncrement(1) &&
      table.findName("x-name", table.insertCount())->abs == second,
    "dynamic name lookup did not advance to newly eligible entry");
  ZuCHECK(!table.nameResized(),
    "dynamic name lookup resized bounded name storage");

  Zhttp::H3::QPackTxTable churn;
  ZuCHECK(churn.init(4096, 8) && churn.peerCapacity(4096) &&
      churn.setCapacity(4096),
    "dynamic name churn setup failed");
  bool inserted = true;
  for (unsigned i = 0; i < 300; ++i) {
    char value[24];
    snprintf(value, sizeof(value), "v%u", i);
    const char *value_ = value;
    if (!churn.insert({"x-repeat", value_})) {
      inserted = false;
      break;
    }
  }
  auto newest = churn.findName("x-repeat");
  ZuCHECK(inserted && newest && newest->value == "v299" &&
      !churn.exactResized() && !churn.nameResized(),
    "eviction/compaction did not preserve the newest name mapping");
  ZuCHECK(churn.setCapacity(64) &&
      (newest = churn.findName("x-repeat")) &&
      newest->value == "v299",
    "capacity reduction left a stale same-name mapping");
}

void testBoundedStorage()
{
  ZuTestScope(testBoundedStorage);

  Zhttp::H3::QPackRxTable rx;
  Zhttp::H3::QPackTxTable tx;
  ZuCHECK(rx.init(0) && tx.init(0, 0) &&
      !rx.slots() && !tx.orderSlots() && !tx.exactSlots() &&
      !tx.nameSlots() &&
      !tx.sectionSlots(),
    "zero QPACK limits allocated dynamic storage");

  ZuCHECK(rx.init(31) && !rx.maxEntries() && !rx.slots(),
    "31-byte Rx capacity allocated an impossible entry");
  ZuCHECK(rx.init(32) && rx.maxEntries() == 1 && rx.slots() >= 1,
    "32-byte Rx capacity did not allocate one entry slot");

  enum { Capacity = 32768, Entries = 300, Sections = 128 };
  ZuCHECK(rx.init(Capacity) && rx.setCapacity(Capacity),
    "bounded Rx setup failed");
  uint32_t rxSlots = rx.slots();
  for (unsigned i = 0; i < Entries; ++i) {
    char name[24], value[24];
    snprintf(name, sizeof(name), "x-rx-bound-%u", i);
    snprintf(value, sizeof(value), "v%u", i);
    ZuCHECK(rx.insert({name, value}),
      "bounded Rx churn insert failed");
  }
  ZuCHECK(rx.slots() == rxSlots,
    "bounded Rx churn changed array capacity");

  ZuCHECK(tx.init(Capacity, Sections),
    "bounded Tx setup failed");
  uint32_t orderSlots = tx.orderSlots();
  uint32_t exactSlots = tx.exactSlots();
  uint32_t nameSlots = tx.nameSlots();
  uint32_t sectionSlots = tx.sectionSlots();
  ZuCHECK(tx.peerCapacity(uint32_t(-1)) &&
      tx.orderSlots() == orderSlots && tx.exactSlots() == exactSlots &&
      tx.nameSlots() == nameSlots &&
      tx.sectionSlots() == sectionSlots &&
      tx.effectiveCapacity() == Capacity &&
      tx.setCapacity(Capacity),
    "peer capacity changed locally bounded storage");
  uint64_t newest = 0;
  for (unsigned i = 0; i < Entries; ++i) {
    char name[24], value[24];
    snprintf(name, sizeof(name), "x-tx-bound-%u", i);
    snprintf(value, sizeof(value), "v%u", i);
    ZuCHECK(tx.insert({name, value}, &newest),
      "bounded Tx churn insert failed");
  }
  ZuCHECK(tx.orderSlots() == orderSlots &&
      tx.exactSlots() == exactSlots && !tx.exactResized() &&
      tx.nameSlots() == nameSlots && !tx.nameResized(),
    "bounded Tx churn grew or resized entry storage");

  for (unsigned i = 0; i < Sections; ++i) {
    ZuCHECK(tx.registerSection(1000 + i),
      "bounded section insertion failed");
  }
  ZuCHECK(!tx.registerSection(2000) &&
      tx.sectionCount() == Sections &&
      tx.sectionSlots() == sectionSlots && !tx.sectionResized(),
    "section bound grew or resized section storage");
  for (unsigned i = Sections; i; --i)
    ZuCHECK(tx.sectionAck(999 + i), "bounded section release failed");
  ZuCHECK(!tx.sectionCount() && !tx.sectionResized(),
    "section churn retained state or resized");

  ZuCHECK(tx.peerCapacity(64) && tx.capacity() <= 64 &&
      tx.orderSlots() == orderSlots && tx.exactSlots() == exactSlots &&
      tx.nameSlots() == nameSlots,
    "peer capacity reduction reallocated Tx storage");
  rx.final();
  tx.final();
  ZuCHECK(!rx.slots() && !tx.orderSlots() && !tx.exactSlots() &&
      !tx.nameSlots() &&
      !tx.sectionSlots() && rx.init(0) && tx.init(0, 0),
    "QPACK init/final retained dynamic storage");
}

void testRxTxChurn()
{
  ZuTestScope(testRxTxChurn);

  Zhttp::H3::QPackRxTable rx;
  rx.init(96);
  ZuCHECK(rx.setCapacity(96), "rx churn capacity setup failed");
  for (unsigned i = 0; i < 40; ++i) {
    char name[16], value[16];
    snprintf(name, sizeof(name), "x-rx-%u", i);
    snprintf(value, sizeof(value), "v%u", i);
    const char *name_ = name, *value_ = value;
    ZuCHECK(rx.insert({name_, value_}),
      "rx churn insert failed");
    ZuCHECK(rx.used() <= rx.capacity(), "rx churn exceeded capacity");
  }
  Zhttp::H3::Header h;
  ZuCHECK(rx.baseAbs() > 0 && rx.insertCount() == 40 &&
      !rx.lookupAbs(rx.baseAbs() - 1, h),
    "rx churn base accounting failed");
  ZuCHECK(rx.lookupAbs(rx.insertCount() - 1, h),
    "rx churn newest lookup failed");

  Zhttp::H3::QPackTxTable tx;
  ZuCHECK(tx.init(96, 8) && tx.peerCapacity(96) &&
      tx.setCapacity(96),
    "tx churn capacity setup failed");
  for (unsigned i = 0; i < 40; ++i) {
    char name[16], value[16];
    snprintf(name, sizeof(name), "x-tx-%u", i);
    snprintf(value, sizeof(value), "v%u", i);
    const char *name_ = name, *value_ = value;
    uint64_t abs = 0;
    ZuCHECK(tx.insert({name_, value_}, &abs) &&
	abs == i,
      "tx churn insert failed");
    ZuCHECK(tx.used() <= tx.capacity(), "tx churn exceeded capacity");
  }
  ZuBSpan name{"x-tx-39"};
  ZuBSpan value{"v39"};
  ZuCHECK(!tx.lookupAbs(0, h), "tx churn failed to evict oldest");
  ZuCHECK(tx.lookupAbs(tx.insertCount() - 1, h),
    "tx churn newest lookup failed");
  ZuCHECK(h.name == name && h.value == value,
    "tx churn newest value mismatch");
  ZuCHECK(tx.find(name, value), "tx churn hash lookup failed");
}

void testTxSectionStress()
{
  ZuTestScope(testTxSectionStress);

  Zhttp::H3::QPackTxTable table;
  ZuCHECK(table.init(512, 8) && table.peerCapacity(512) &&
      table.setCapacity(512),
    "tx section stress capacity setup failed");
  uint64_t abs[6];
  for (unsigned i = 0; i < 6; ++i) {
    char name[16], value[16];
    snprintf(name, sizeof(name), "x-ref-%u", i);
    snprintf(value, sizeof(value), "v%u", i);
    const char *name_ = name, *value_ = value;
    ZuCHECK(table.insert({name_, value_}, &abs[i]),
      "tx section stress insert failed");
  }
  for (unsigned i = 0; i < 6; ++i) {
    ZuCHECK(table.registerSection(100 + i),
      "tx section stress track failed");
  }
  ZuCHECK(!table.registerSection(100),
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
      Zhttp::H3::QPack::decodeEncoderInsn(
	bytes, decoded) == int(bytes.length()) &&
      decoded.type == Zhttp::H3::QPackInsn::SetCapacity &&
      decoded.value == 10,
    "set capacity instruction mismatch");

  ZuCHECK(Zhttp::H3::QPack::encodeInsertWithNameRef(
      bytes, 46, false, "txt") > 0 &&
      bytes.length() == 5 && bytes[0] == 0xee && bytes[1] == 3 &&
      Zhttp::H3::QPack::decodeEncoderInsn(
	bytes, decoded) == int(bytes.length()) &&
      decoded.type == Zhttp::H3::QPackInsn::InsertWithNameRef &&
      !decoded.nameRefDynamic && decoded.value == 46 &&
      decoded.header.value == "txt",
    "insert with static name reference mismatch");

  ZuCHECK(Zhttp::H3::QPack::encodeInsertLiteral(
      bytes, {"accept", "json"}) > 0 &&
      bytes[0] == 0x46 &&
      Zhttp::H3::QPack::decodeEncoderInsn(
	bytes, decoded) == int(bytes.length()) &&
      decoded.type == Zhttp::H3::QPackInsn::InsertWithoutNameRef &&
      decoded.header.name == "accept" &&
      decoded.header.value == "json",
    "insert literal mismatch");

  ZuCHECK(Zhttp::H3::QPack::encodeSectionAck(bytes, 4) > 0 &&
      bytes.length() == 1 && bytes[0] == 0x84 &&
      Zhttp::H3::QPack::decodeDecoderInsn(
	bytes, decoded) == int(bytes.length()) &&
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
  ZuCHECK(Zhttp::H3::QPack::decodeEncoderInsn(
      bytes, decoded) == int(bytes.length()) &&
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
  auto decodeEncoder = [](ZuBSpan bytes, Zhttp::H3::QPackDecodedInsn &i) {
    return Zhttp::H3::QPack::decodeEncoderInsn(bytes, i);
  };
  auto decodeDecoder = [](ZuBSpan bytes, Zhttp::H3::QPackDecodedInsn &i) {
    return Zhttp::H3::QPack::decodeDecoderInsn(bytes, i);
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

void testInitializationSeeds()
{
  ZuTestScope(testInitializationSeeds);

  Zhttp::HeaderSeedCatalog catalog;
  Zhttp::H3::Params params;
  catalog.add<SeedHeaders>(params, 512);
  const auto &seeds = catalog.entries();
  ZuCHECK(seeds.length() == 2 &&
      seeds[0].name == "x-fixed" && seeds[0].value == "fixed" &&
      seeds[0].exact && seeds[1].name == "x-runtime" &&
      !seeds[1].value && !seeds[1].exact,
    "initialization catalog did not classify fixed/name-only seeds");

  Zhttp::H3::QPackTxTable tx;
  ZuCHECK(tx.init(512, 8) && tx.peerCapacity(512),
    "seed table setup failed");
  Zhttp::H3::HdrBytes bytes;
  auto result = Zhttp::installQPackSeeds(
    tx, seeds, [&bytes](ZuBSpan span) {
      appendBytes(bytes, span);
      return true;
    });
  ZuCHECK(result == Zhttp::QPackSeedResult::Seeded && bytes &&
      tx.capacity() == 512 && tx.insertCount() == 2 &&
      !tx.knownReceivedCount(),
    "initialization seeds were not installed exactly once");
  auto count = tx.insertCount();
  auto used = tx.used();
  auto slots = tx.orderSlots();
  ZuCHECK(tx.find("x-fixed", "fixed") && tx.findName("x-runtime") &&
      tx.insertCount() == count && tx.used() == used &&
      tx.orderSlots() == slots,
    "read-only seed lookup mutated the QPACK table");
  ZuCHECK(tx.insertCountIncrement(count) &&
      tx.knownReceivedCount() == count && tx.insertCount() == count &&
      tx.used() == used && tx.orderSlots() == slots,
    "seed acknowledgement changed immutable table storage");

  BuilderState emitter;
  emitter.runtimeHeader = true;
  emitter.runtimeName = "x-runtime";
  emitter.runtimeValue = "varying";
  emitter.params.qpackTxCapacity(512);
  ZuCHECK(emitter.tx.init(512, 8) && emitter.tx.peerCapacity(512) &&
      Zhttp::installQPackSeeds(
        emitter.tx, seeds, [&emitter](ZuBSpan span) {
	  return emitter.encoder.write(span);
	}) == Zhttp::QPackSeedResult::Seeded && emitter.tx.frozen(),
    "builder seed table did not freeze after cold-path installation");
  CaptureTxStream beforeAck;
  emitter.begin(beforeAck);
  ZuSpan<uint8_t> payload;
  Zhttp::H3::EncodedFieldSectionPrefix prefix;
  ZuCHECK(headersPayload(beforeAck.bytes, payload) &&
      Zhttp::H3::QPack::decodeFieldSectionPrefix(payload, prefix) > 0 &&
      !prefix.encodedInsertCount && !emitter.tx.sectionCount(),
    "H3 emission referenced an unacknowledged seed");
  auto immutableCount = emitter.tx.insertCount();
  auto immutableUsed = emitter.tx.used();
  auto immutableSlots = emitter.tx.orderSlots();
  ZuCHECK(emitter.tx.insertCountIncrement(immutableCount),
    "seed acknowledgement failed");
  emitter.id = 3;
  CaptureTxStream afterAck;
  emitter.begin(afterAck);
  ZuCHECK(headersPayload(afterAck.bytes, payload) &&
      Zhttp::H3::QPack::decodeFieldSectionPrefix(payload, prefix) > 0 &&
      !prefix.encodedInsertCount && !emitter.tx.sectionCount() &&
      emitter.tx.insertCount() == immutableCount &&
      emitter.tx.used() == immutableUsed &&
      emitter.tx.orderSlots() == immutableSlots,
    "runtime header referenced an acknowledged name seed");

  Zhttp::H3::QPackTxTable failed;
  ZuCHECK(failed.init(512, 8) && failed.peerCapacity(512) &&
      Zhttp::installQPackSeeds(
        failed, seeds, [](ZuBSpan) { return false; }) ==
      Zhttp::QPackSeedResult::Failed && !failed.capacity() &&
      !failed.insertCount() && !failed.used(),
    "failed seed write mutated connection table state");
}

void testBuilderPeerCapacity()
{
  ZuTestScope(testBuilderPeerCapacity);

  BuilderState builder;
  builder.params.qpackTxCapacity(256);
  CaptureTxStream stream;
  builder.begin(stream);
  ZuCHECK(!builder.encoder.bytes.length() && !builder.tx.capacity() &&
      !builder.tx.insertCount(),
    "builder emitted dynamic QPACK before peer capacity");

  ZuSpan<uint8_t> payload;
  ZuCHECK(headersPayload(stream.bytes, payload),
    "builder did not emit a valid HEADERS frame");
  unsigned seen = 0;
  ZuCHECK(Zhttp::H3::QPack::decodeLiteral(
      payload,
      [&seen](Zhttp::H3::Header h) {
	if (h.name == "accept" && h.value == "application/json") ++seen;
      }) == int(payload.length()) && seen == 1,
    "builder static/literal HEADERS payload did not decode");

  BuilderState dynamicBuilder;
  dynamicBuilder.params.qpackTxCapacity(256);
  ZuCHECK(dynamicBuilder.tx.init(256, 16) &&
      dynamicBuilder.tx.peerCapacity(256),
    "builder tx max capacity setup failed");
  CaptureTxStream dynamicStream;
  dynamicBuilder.begin(dynamicStream);
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
    builder.params.qpackTxCapacity(256);
    ZuCHECK(builder.tx.init(256, 16) && builder.tx.peerCapacity(256),
      "capacity failure max setup failed");
    builder.encoder.failWrite = 0;
    CaptureTxStream stream;
    builder.begin(stream);
    ZuCHECK(txUnchanged(builder.tx),
      "capacity write failure changed tx state");
    ZuCHECK(builder.qpackFailure() ==
	Zhttp::H3::QPackBuildFailure::HeadersPayloadEmit,
      "capacity write failure reason mismatch");
  }
  {
    BuilderState builder;
    builder.params.qpackTxCapacity(256);
    ZuCHECK(builder.tx.init(256, 16) && builder.tx.peerCapacity(256),
      "insert failure max setup failed");
    builder.encoder.failWrite = 1;
    CaptureTxStream stream;
    builder.begin(stream);
    ZuCHECK(txUnchanged(builder.tx),
      "insert write failure changed tx state");
    ZuCHECK(builder.qpackFailure() ==
	Zhttp::H3::QPackBuildFailure::HeadersPayloadEmit,
      "insert write failure reason mismatch");
  }
  {
    BuilderState builder;
    builder.params.qpackTxCapacity(256);
    ZuCHECK(builder.tx.init(256, 16) && builder.tx.peerCapacity(256),
      "success max setup failed");
    CaptureTxStream stream;
    builder.begin(stream);
    ZuCHECK(builder.qpackFailure() == Zhttp::H3::QPackBuildFailure::None &&
	builder.tx.capacity() == 256 && builder.tx.capacitySent &&
	builder.tx.insertCount() > 0,
      "successful dynamic build did not commit once");
  }
}

void testBuilderSectionFallback()
{
  ZuTestScope(testBuilderSectionFallback);

  BuilderState builder;
  builder.params.qpackTxCapacity(256);
  ZuCHECK(builder.tx.init(256, 1) && builder.tx.peerCapacity(256),
    "section fallback setup failed");

  CaptureTxStream first;
  builder.begin(first);
  ZuCHECK(builder.tx.insertCount() &&
      builder.tx.insertCountIncrement(builder.tx.insertCount()),
    "section fallback dynamic table setup failed");

  builder.id = 3;
  CaptureTxStream second;
  builder.begin(second);
  ZuCHECK(builder.tx.sectionCount() == 1,
    "section fallback did not occupy the configured section slot");

  builder.id = 5;
  CaptureTxStream third;
  builder.begin(third);
  ZuSpan<uint8_t> payload;
  Zhttp::H3::EncodedFieldSectionPrefix prefix;
  ZuCHECK(headersPayload(third.bytes, payload) &&
      Zhttp::H3::QPack::decodeFieldSectionPrefix(payload, prefix) > 0 &&
      !prefix.encodedInsertCount && builder.tx.sectionCount() == 1 &&
      !builder.tx.sectionResized(),
    "denied section tracking emitted a dynamic reference");
  ZuCHECK(builder.tx.sectionAck(3) && !builder.tx.sectionCount(),
    "section fallback acknowledgement failed");
}

void testBuilderQueryPath()
{
  ZuTestScope(testBuilderQueryPath);

  BuilderState builder;
  builder.path = "/sample";
  builder.query = "q=1";
  CaptureTxStream stream;
  builder.begin(stream);

  ZuSpan<uint8_t> payload;
  ZuCHECK(headersPayload(stream.bytes, payload),
    "builder query path did not emit a valid HEADERS frame");
  bool sawPath = false;
  ZuCHECK(Zhttp::H3::QPack::decodeLiteral(
      payload,
      [&sawPath](Zhttp::H3::Header h) {
	if (h.name == ":path" && h.value == "/sample?q=1") sawPath = true;
      }) == int(payload.length()) && sawPath,
    "builder query path did not decode as a segmented :path value");

  BuilderState custom;
  custom.customTarget = true;
  CaptureTxStream customStream;
  custom.begin(customStream);
  ZuCHECK(headersPayload(customStream.bytes, payload),
    "custom printable target did not emit a valid HEADERS frame");
  sawPath = false;
  ZuCHECK(Zhttp::H3::QPack::decodeLiteral(
      payload,
      [&sawPath](Zhttp::H3::Header h) {
	if (h.name == ":path" && h.value == "/printable?") sawPath = true;
      }) == int(payload.length()) && sawPath,
    "custom printable target did not survive QPACK count/encode");
}

void testBuilderCardinality()
{
  ZuTestScope(testBuilderCardinality);

  BuilderState builder;
  builder.runtimeHeader = true;
  builder.runtimeHeader2 = true;
  builder.runtimeName = "x-runtime-one";
  builder.runtimeValue = "first";
  CaptureTxStream stream;
  ZuCHECK(builder.begin(stream) && builder.keyedCalls == 1 &&
      builder.runtimeProviderCalls == 1,
    "H3 header providers were not entered exactly once");

  ZuSpan<uint8_t> payload;
  unsigned accept = 0, first = 0, second = 0;
  ZuCHECK(headersPayload(stream.bytes, payload) &&
      Zhttp::H3::QPack::decodeLiteral(
        payload, [&accept, &first, &second](Zhttp::H3::Header h) {
	  if (h.name == "accept" && h.value == "application/json") ++accept;
	  if (h.name == "x-runtime-one" && h.value == "first") ++first;
	  if (h.name == "x-runtime-two" && h.value == "second") ++second;
	}) == int(payload.length()) && accept == 1 && first == 1 && second == 1 &&
      builder.keyedCalls == 1 && builder.runtimeProviderCalls == 1,
    "H3 callbacks were replayed or did not encode each field once");
}

void testBuilderExtendedConnect()
{
  ZuTestScope(testBuilderExtendedConnect);

  BuilderState disabled;
  disabled.method = Zhttp::Method::CONNECT;
  disabled.path = "/stream";
  disabled.protocol_ = "opaque";
  CaptureTxStream disabledStream;
  ZuCHECK(!disabled.begin(disabledStream) && !disabledStream.bytes,
    "H3 builder accepted Extended CONNECT without peer capability");

  BuilderState enabled;
  enabled.method = Zhttp::Method::CONNECT;
  enabled.path = "/stream";
  enabled.protocol_ = "opaque";
  enabled.h3(nullptr, nullptr, nullptr, enabled.id, true);
  CaptureTxStream stream;
  ZuCHECK(enabled.begin(stream),
    "H3 builder rejected negotiated Extended CONNECT");
  ZuSpan<uint8_t> payload;
  bool sawMethod = false, sawProtocol = false;
  ZuCHECK(headersPayload(stream.bytes, payload) &&
      Zhttp::H3::QPack::decodeLiteral(
      payload,
      [&sawMethod, &sawProtocol](Zhttp::H3::Header h) {
	if (h.name == ":method" && h.value == "CONNECT") sawMethod = true;
	if (h.name == ":protocol" && h.value == "opaque") sawProtocol = true;
      }) == int(payload.length()) && sawMethod && sawProtocol,
    "H3 Extended CONNECT pseudo-headers did not round-trip");
}

void testBuilderRuntimeHeaders()
{
  ZuTestScope(testBuilderRuntimeHeaders);

  BuilderState builder;
  builder.runtimeHeader = true;
  CaptureTxStream stream;
  builder.begin(stream);

  ZuSpan<uint8_t> payload;
  ZuCHECK(headersPayload(stream.bytes, payload),
    "runtime header builder did not emit a valid HEADERS frame");
  bool sawRuntime = false;
  ZuCHECK(Zhttp::H3::QPack::decodeLiteral(
      payload,
      [&sawRuntime](Zhttp::H3::Header h) {
	if (h.name == "server" && h.value == "zhttp-runtime")
	  sawRuntime = true;
      }) == int(payload.length()) && sawRuntime,
    "runtime header did not decode from H3 HEADERS payload");

  BuilderState dynamicBuilder;
  dynamicBuilder.runtimeHeader = true;
  dynamicBuilder.params.qpackTxCapacity(256);
  ZuCHECK(dynamicBuilder.tx.init(256, 16) &&
      dynamicBuilder.tx.peerCapacity(256),
    "runtime header builder tx max capacity setup failed");
  CaptureTxStream dynamicStream;
  dynamicBuilder.begin(dynamicStream);
  ZuCHECK(!dynamicBuilder.encoder.bytes && !dynamicBuilder.tx.capacity() &&
      !dynamicBuilder.tx.insertCount() &&
      headersPayload(dynamicStream.bytes, payload),
    "runtime header mutated the dynamic table during emission");
  sawRuntime = false;
  ZuCHECK(Zhttp::H3::QPack::decodeLiteral(
      payload,
      [&sawRuntime](Zhttp::H3::Header h) {
	if (h.name == "server" && h.value == "zhttp-runtime")
	  sawRuntime = true;
      }) == int(payload.length()) && sawRuntime,
    "runtime header did not remain literal on the hot path");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZiLog::init("ZhttpQPackDynamicTest");
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZuTestMain();
  ZuTestCall(testVar);
  ZuTestCall(testRxTable);
  ZuTestCall(testDynamicFieldSectionDecode);
  ZuTestCall(testFieldRepresentationGoldens);
  ZuTestCall(testParserFieldCallbacks);
  ZuTestCall(testParserBodyStream);
  ZuTestCall(testParserHuffmanRuntimeHeader);
  ZuTestCall(testParserInvalidFields);
  ZuTestCall(testParserStrictContentLength);
  ZuTestCall(testParserHeaderScratchAndLimits);
  ZuTestCall(testExtendedConnectRx);
  ZuTestCall(testFieldDecodeAllocationDiscipline);
  ZuTestCall(testSettingsKeyBoundary);
  ZuTestCall(testTxTable);
  ZuTestCall(testTxNameLookup);
  ZuTestCall(testBoundedStorage);
  ZuTestCall(testRxTxChurn);
  ZuTestCall(testTxSectionStress);
  ZuTestCall(testInstructionEncoding);
  ZuTestCall(testHuffmanInstructionStorage);
  ZuTestCall(testInstructionParserSplit);
  ZuTestCall(testInitializationSeeds);
  ZuTestCall(testBuilderQueryPath);
  ZuTestCall(testBuilderCardinality);
  ZuTestCall(testBuilderExtendedConnect);
  ZuTestCall(testBuilderRuntimeHeaders);
  ZiLog::stop();
}
