//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// RFC 6455 protocol, URI, handshake, and stream tests

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmFn.hh>
#include <zlib/ZmList.hh>

#include <zlib/ZtArray.hh>

#include <zlib/Zws.hh>
#include "../../zhttp/src/ZhttpWire.hh"
#include "../../zhttp/src/ZhttpNative.hh"

using namespace ZuTestUtil;

namespace ZwsProtocolTest_ {

using BufAlloc = Zi::IOBufAlloc<ZiIOBuf, 256, 1<<20,
  ZuStringT<"ZwsProtocolTest.Buf">>;

struct WireTx : public ZiTxStream<WireTx> {
  using Base = ZiTxStream<WireTx>;
  using Bufs = ZtArray<ZmRef<ZiIOBuf>, ZtArrayHeapID<"ZwsTest.Wire">>;

  WireTx(
      unsigned size = 256, unsigned headRoom = 0,
      unsigned tailRoom = 0) :
    Base{size, headRoom, tailRoom} { }

  ~WireTx() { this->flush(); }

  ZmRef<ZiIOBuf> allocBuf_(unsigned headRoom) {
    // Admission can reject a request before any Z heap allocation.
    if (!admitOK) return nullptr;
    ZmRef<ZiIOBuf> buf = new BufAlloc{nullptr, 0};
    buf->skip = headRoom;
    return buf;
  }
  bool sendBuf_(ZmRef<ZiIOBuf> buf, bool final) {
    ++sends;
    if (!sendOK || !buf || !buf->length) return false;
    parts.push(Zi::RxRange{buf, 0, buf->length});
    bufs.push(ZuMv(buf));
    finals.push(uint8_t(final));
    return true;
  }
  bool sendRange_(Zi::RxRange range) {
    if (!sendOK) return false;
    parts.push(ZuMv(range));
    ++sends;
    return true;
  }
  bool flush() {
    ++flushCalls;
    return Base::flush();
  }

  ZtArray<Zi::RxRange, ZtArrayHeapID<"ZwsTest.Parts">> parts;
  Bufs					bufs;
  ZtArray<uint8_t, ZtArrayHeapID<"ZwsTest.Final">> finals;
  unsigned				flushCalls = 0;
  unsigned sends = 0;
  bool admitOK = true;
  bool sendOK = true;
};

struct FakeMx {
  using Fn = ZmFn<void(), ZmFnHeapID<"ZwsTest.Timer">>;
  struct Entry {
    void	*timer = nullptr;
    Fn		fn;
  };

  FakeMx() { entries.length(4); }

  template <typename Timer>
  void del(Timer *timer) {
    const unsigned n = entries.length();
    for (unsigned i = 0; i < n; ++i)
      if (entries[i].timer == timer) {
	entries[i].fn = {};
	return;
      }
  }
  template <typename Timer, typename Time, typename Arm>
  void add(
      Timer *timer, Time, int, Arm &&arm, unsigned) {
    ++adds;
    Entry *entry = nullptr;
    const unsigned n = entries.length();
    for (unsigned i = 0; i < n; ++i)
      if (entries[i].timer == timer) {
	entry = &entries[i];
	break;
      } else if (!entry && !entries[i].timer)
	entry = &entries[i];
    ZmAssert(entry);
    entry->timer = timer;
    ZuFwd<Arm>(arm)([entry](auto fn) {
      entry->fn = Fn{ZuMv(fn)};
      return false;
    });
  }

  unsigned active() const {
    unsigned active = 0;
    const unsigned n = entries.length();
    for (unsigned i = 0; i < n; ++i)
      active += !!entries[i].fn;
    return active;
  }
  bool fire() {
    const unsigned n = entries.length();
    for (unsigned i = 0; i < n; ++i)
      if (entries[i].fn) {
	Fn fn = ZuMv(entries[i].fn);
	entries[i].fn = {};
	fn();
	return true;
      }
    return false;
  }

  ZuArray<Entry, 4>	entries;
  unsigned		adds = 0;
};

struct FakeHub {
  FakeMx *mx() { return &mx_; }
  unsigned rxThread() const { return 0; }
  template <typename L>
  void rxRun(L &&l) { ZuFwd<L>(l)(); }

  FakeMx mx_;
};

const Zhttp::ChannelOps &channelOps();

struct Link {
  Link() : m_channel{channelOps()} { m_channel.ctx = this; }
  Zhttp::Channel &channel() { return m_channel; }
  Zhttp::Channel m_channel;
  bool local = true;
  bool peer = true;
  bool ended = false;
  bool endAfterFlush = false;
  bool reset = false;
  unsigned endCalls = 0;
  unsigned resetCalls = 0;
  WireTx tx;
  FakeHub hub;

  FakeHub *app() { return &hub; }
  bool streamLocalCap() const { return local; }
  bool streamPeerCap() const { return peer; }
  template <typename L>
  void streamTx(L &&l) { ZuFwd<L>(l)(tx); }
  void streamTxEnd() {
    endAfterFlush = !!tx.flushCalls;
    ended = true;
    ++endCalls;
  }
  void streamTxReset() {
    reset = true;
    ++resetCalls;
  }
};

// Concrete-channel fixture over the existing pooled wire sink.
const Zhttp::ChannelOps &channelOps() {
  static const Zhttp::ChannelOps ops{
    .streamLocalCap = [](void *ctx) { return static_cast<Link *>(ctx)->local; },
    .streamPeerCap = [](void *ctx) { return static_cast<Link *>(ctx)->peer; },
    .streamEnd = [](void *ctx) { static_cast<Link *>(ctx)->streamTxEnd(); },
    .streamReset = [](void *ctx) { static_cast<Link *>(ctx)->streamTxReset(); },
    .http10 = [](const void *) { return false; },
    .bodyFramed = [](const void *) { return false; },
    .streamWrite = [](void *ctx, Zhttp::PrintView print) {
      auto &tx = static_cast<Link *>(ctx)->tx;
      Zhttp::Writer out{&tx, Zhttp::Native_::writerOps<WireTx>()};
      print.write(out);
      return out.flush();
    }};
  return ops;
}

using OuterRx = ZiRxStream<ZiRxQueue>;
using OuterBufAlloc = Zi::IOBufAlloc<
	ZiRxQueue::Node, 256, 1<<20, ZuStringT<"ZwsTest.OuterRx">>;

using Bytes = ZtArray<uint8_t, ZtArrayHeapID<"ZwsTest.Bytes">>;

using H1Rx = ZiRxStream<ZiRxQueue>;
using H1BufAlloc = Zi::IOBufAlloc<
	ZiRxQueue::Node, 256, 2048, ZuStringT<"ZwsTest.H1Rx">>;

void push(H1Rx &rx, ZuCSpan data)
{
  ZmRef<ZiRxQueue::Node> buf = new H1BufAlloc{nullptr, 0};
  *buf << data;
  rx.push(ZuMv(buf));
}

Bytes frame(
    Zws::Opcode::T opcode, ZuBSpan payload,
    bool final = true, bool masked = false, uint32_t key = 0x12345678)
{
  Bytes out;
  out << uint8_t((final ? 0x80 : 0) | opcode);
  if (payload.length() < 126)
    out << uint8_t((masked ? 0x80 : 0) | payload.length());
  else if (payload.length() <= 0xffff) {
    out << uint8_t((masked ? 0x80 : 0) | 126) <<
      uint8_t(payload.length()>>8) << uint8_t(payload.length());
  } else {
    out << uint8_t((masked ? 0x80 : 0) | 127);
    for (unsigned i = 0; i < 8; ++i)
      out << uint8_t(payload.length()>>(56 - (i<<3)));
  }
  if (masked)
    out << uint8_t(key>>24) << uint8_t(key>>16) <<
      uint8_t(key>>8) << uint8_t(key);
  unsigned offset = out.length();
  out << payload;
  if (masked) Zws::mask(out.span().offset(offset), key);
  return out;
}

template <bool Server, typename Random = Ztls::Random>
struct App :
  public ZmObject,
  public Zws::Codec<App<Server, Random>, Link, Server, Random> {
  using Base = Zws::Codec<App, Link, Server, Random>;
  using Messages =
    ZtArray<Bytes, ZtArrayHeapID<"ZwsTest.Messages">>;
  using Opcodes =
    ZtArray<Zws::Opcode::T, ZtArrayHeapID<"ZwsTest.Opcodes">>;

  App(Link &link, Random &random, Zws::Config config = {}) :
    Base{link, random, config}, link{link} { }

  int messageStart(Zws::Opcode::T opcode) {
    ++calls;
    ++starts;
    current = {};
    inputData = nullptr;
    opcodes.push(opcode);
    return 1;
  }
  int message(Base::Rx &rx) {
    ++calls;
    ++inputs;
    if (reject) return -1;
    if (recordLength) {
      for (;;) {
	uint64_t remaining = recordLength;
	int64_t n = rx.consume(
	  [&remaining](ZuSpan<uint8_t> span) mutable -> int64_t {
	    if (remaining > span.length()) {
	      remaining -= span.length();
	      return 0;
	    }
	    return remaining;
	  }, [this](ZuSpan<uint8_t> span) {
	    if (!inputData) inputData = span.data();
	    current << span;
	  });
	if (n <= 0) return n < 0 ? -1 : 1;
      }
    }
    return Zhttp::bodyEach(rx, [this](ZuSpan<uint8_t> span) {
      if (!inputData) inputData = span.data();
      current << span;
    }) ? 1 : -1;
  }
  int messageEnd() {
    ++calls;
    ++finals;
    messages.push(ZuMv(current));
    return 1;
  }
  void pong(ZuBSpan payload) { pongPayload = payload; }
  void closed(uint16_t code_, ZuBSpan reason_) {
    closeCode = code_;
    closeReason = reason_;
  }
  void error(Zws::Failure::T) { ++errors; }

  int feed(Bytes &wire) {
    if (wire) {
      ZmRef<ZiRxQueue::Node> buf = new OuterBufAlloc{nullptr, 0};
      *buf << wire;
      auto span = buf->span();
      wireData = span.data();
      rx.push(ZuMv(buf));
    }
    return this->process(link.channel(), rx);
  }
  void peerEnd() {
    Base::peerEnd(link.channel());
  }
  void streamError() {
    Base::streamError(link.channel());
  }

  Link			&link;
  Messages		messages;
  Opcodes		opcodes;
  Bytes			current;
  Bytes			pongPayload;
  Bytes			closeReason;
  uint16_t		closeCode = 0;
  const uint8_t		*inputData = nullptr;
  const uint8_t		*wireData = nullptr;
  unsigned		errors = 0;
  unsigned		calls = 0;
  unsigned		starts = 0;
  unsigned		inputs = 0;
  unsigned		finals = 0;
  unsigned		recordLength = 0;
  bool			reject = false;
  OuterRx		rx;
};

void uri()
{
  ZuTestScopeRT(uri);

  struct Test {
    ZuCSpan input;
    ZuCSpan host;
    ZuCSpan target;
    uint16_t port;
    bool secure;
    bool explicitPort;
  };
  static const Test tests[] = {
    {"ws://Example.COM", "example.com", "/", 80, false, false},
    {"Ws://Example.COM/path", "example.com", "/path", 80, false, false},
    {"WSS://example.com?x=1", "example.com", "/?x=1",
      443, true, false},
    {"WsS://example.com/path", "example.com", "/path", 443, true, false},
    {"wss://[2001:db8::1]:8443/feed?x", "2001:db8::1",
      "/feed?x", 8443, true, true}
  };
  for (const auto &test : tests) {
    Zws::URI uri;
    auto error = Zws::URI::parse(uri, test.input);
    ZuCheckRT(error.ok(), log_(test.input));
    ZuCheckRT(uri.host == test.host, log_(test.input));
    ZuCheckRT(uri.target == test.target, log_(test.input));
    ZuCheckRT(uri.port == test.port, log_(test.input));
    ZuCheckRT(uri.secure() == test.secure, log_(test.input));
    ZuCheckRT(uri.explicitPort == test.explicitPort, log_(test.input));
  }
  static constexpr const char *invalid[] = {
    "http://example.com/", "ws:///", "ws://user@example.com/",
    "ws://example.com/#fragment", "ws://example.com:0/",
    "ws://example.com:65536/", "ws://2001:db8::1/",
    "ws://[not-ipv6]/", "ws://bad%2/example"
  };
  for (auto input : invalid) {
    Zws::URI uri;
    ZuCheckRT(!Zws::URI::parse(uri, input).ok(), log_(input));
  }
}

void handshake()
{
  ZuTestScope(handshake);

  Ztls::Random random;
  ZuCHECK(random.init());
  Zws::HandshakeString key;
  ZuCHECK(Zws::nonce(random, key));
  ZuCHECK(key.length() == 24);
  ZuCHECK(Zws::validKey(key));

  Zws::HandshakeString accept;
  ZuCHECK(Zws::accept(accept, "dGhlIHNhbXBsZSBub25jZQ=="));
  ZuCHECK(accept == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
  ZuCHECK(Zws::validAccept(
    "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=",
    "dGhlIHNhbXBsZSBub25jZQ=="));
  ZuCHECK(Zws::token("keep-alive, Upgrade", "upgrade"));
  ZuCHECK(!Zws::token("upgrader", "upgrade"));
  ZuCHECK(Zws::subprotocol("json, sbe", "sbe"));
  ZuCHECK(!Zws::subprotocol("json, sbe", "SBE"));
  ZuCHECK(!Zws::subprotocol("json, sbe", "json, sbe"));
  ZuCHECK(!Zws::subprotocol("json,,sbe", "sbe"));
  ZuCHECK(!Zws::subprotocol("json,sbe,", "sbe"));
  ZuCHECK(!Zws::subprotocol("json, sbe", "json/sbe"));
}

// Wire adapters are unit-fixture implementation details. The public handshake
// validators contain no native parser or transport types.
template <typename Validator, bool Request>
class H1Parser : public Validator,
  public Zhttp::H1::Parser<H1Parser<Validator, Request>, Request> {
  using Wire = Zhttp::H1::Parser<H1Parser, Request>;
public:
  using State = Wire::State;
  using HdrCatalog = Validator::HdrCatalog;
  using Validator::header;
  template <typename ...Args>
  H1Parser(Args &&...args) : Validator{ZuFwd<Args>(args)...}, m_channel{ops()} {
    m_channel.ctx = this;
  }
  const Zhttp::CatalogView &catalog() const {
    return Zhttp::CatalogData<HdrCatalog>::configure();
  }
  void header(Zhttp::FieldSection::T section, int id,
      ZuBSpan name, ZuSpan<uint8_t> value) {
    Zhttp::parserHeader(static_cast<Validator &>(*this), section, id,
      id < 0 ? -1 : catalog().valueMatch(id, value), name, value);
  }
  bool bodyInfo(Zhttp::BodyType::T type, uint64_t length) {
    return Validator::bodyInfo(&m_channel, type, length);
  }
  void complete(typename State::T) { }
private:
  static const Zhttp::ChannelOps &ops() {
    static const Zhttp::ChannelOps table{
      .http10 = [](const void *ctx) {
	return static_cast<const H1Parser *>(ctx)->Wire::http10();
      },
      .bodyFramed =
	[](const void *ctx) { return static_cast<const H1Parser *>(ctx)->Wire::bodyFramed(); }};
    return table;
  }
  Zhttp::Channel m_channel;
};
using ClientParser = H1Parser<Zws::H1::ClientParser, false>;
using ServerParser = H1Parser<Zws::H1::ServerParser, true>;

template <typename Builder, bool Request>
class H1Builder : public Builder,
  public ZuIf<Request, Zhttp::H1::Request<H1Builder<Builder, Request>, false, false>,
    Zhttp::H1::Response<H1Builder<Builder, Request>, false, false>> {
public:
  template <typename ...Args> H1Builder(Args &&...args) : Builder{ZuFwd<Args>(args)...} { }
  template <typename L> void host(L &&l) {
    if constexpr (Request) Builder::host(ZuFwd<L>(l));
  }
  void fields(Zhttp::FieldOutput &output) {
    Zhttp::BuilderFields<H1Builder> fields{this,
      &Zhttp::CatalogData<typename Builder::HdrCatalog>::configure()};
    fields.fields(output);
  }
  template <typename Key> void headerOffset(uint64_t, unsigned) { }
  void headerBase(uint8_t *) { }
  void patch() { }
};

bool clientHandshake(
    ZuCSpan input, ZuCSpan key, ZuCSpan protocol = {})
{
  H1Rx rx;
  push(rx, input);
  ClientParser parser{key, protocol};
  return parser.process(rx) == ClientParser::State::Complete &&
    parser.valid();
}

bool serverHandshake(ZuCSpan input)
{
  H1Rx rx;
  push(rx, input);
  ServerParser parser;
  return parser.process(rx) == ServerParser::State::Complete &&
    parser.valid();
}

void h1()
{
  ZuTestScopeRT(h1);

  Zws::URI uri;
  ZuCheckRT(Zws::URI::parse(
    uri, "wss://example.com:8443/feed?x=1").ok());
  ZuCSpan key = "dGhlIHNhbXBsZSBub25jZQ==";

  WireTx requestWire{2048};
  H1Builder<Zws::H1::Request, true> request{uri, key, "chat"};
  ZuCheckRT(request.begin(requestWire));
  requestWire.flush();
  ZuCheckRT(requestWire.bufs.length() == 1);
  auto requestBytes = requestWire.bufs[0]->cspan();
  ZuCheckRT(requestBytes.find("GET /feed?x=1 HTTP/1.1\r\n") == 0);
  ZuCheckRT(requestBytes.find("host: example.com:8443\r\n") >= 0);
  ZuCheckRT(requestBytes.find("upgrade: websocket\r\n") >= 0);
  ZuCheckRT(requestBytes.find(
    "sec-websocket-key: dGhlIHNhbXBsZSBub25jZQ==\r\n") >= 0);

  H1Rx requestRx;
  unsigned split = requestBytes.length() / 2;
  auto first = requestBytes;
  auto second = requestBytes;
  first.trunc(split);
  second.offset(split);
  push(requestRx, first);
  push(requestRx, second);
  ServerParser requestParser;
  auto state = requestParser.process(requestRx);
  ZuCheckRT(state == ServerParser::State::Complete);
  ZuCheckRT(requestParser.valid());
  ZuCheckRT(requestParser.host() == "example.com:8443");
  ZuCheckRT(requestParser.target() == "/feed?x=1");
  ZuCheckRT(requestParser.protocols() == "chat");

  Zws::HandshakeString accept;
  ZuCheckRT(Zws::accept(accept, requestParser.key()));
  WireTx responseWire{2048};
  H1Builder<Zws::H1::Response, false> response{accept, "chat"};
  response.begin(responseWire);
  responseWire.flush();

  H1Rx responseRx;
  push(responseRx, responseWire.bufs[0]->cspan());
  ClientParser responseParser{key, "chat"};
  state = responseParser.process(responseRx);
  ZuCheckRT(state == ClientParser::State::Complete);
  ZuCheckRT(responseParser.valid());
  ZuCheckRT(responseParser.selected() == "chat");

  {
    WireTx requestOut{2048};
    H1Builder<Zws::H1::Request, true> optionalRequest{uri, key};
    ZuCheckRT(optionalRequest.begin(requestOut));
    requestOut.flush();
    auto requestBytes = requestOut.bufs[0]->cspan();
    ZuCheckRT(requestBytes.find("sec-websocket-protocol:") < 0);
    ZuCheckRT(serverHandshake(requestBytes));
    WireTx responseOut{2048};
    H1Builder<Zws::H1::Response, false> optionalResponse{accept};
    optionalResponse.begin(responseOut);
    responseOut.flush();
    auto responseBytes = responseOut.bufs[0]->cspan();
    ZuCheckRT(responseBytes.find("sec-websocket-protocol:") < 0);
    ZuCheckRT(clientHandshake(responseBytes, key));
  }

  {
    H1Rx rx;
    push(rx,
      "HTTP/1.1 101 Switching Protocols\r\n"
      "Upgrade: websocket\r\nConnection: Upgrade\r\n"
      "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n");
    ClientParser parser{key};
    ZuCheckRT(
      parser.process(rx) == ClientParser::State::Headers);
    ZuCheckRT(parser.progressed(), log_(
      "incomplete H1 parser reports consumed input"));
    ZuCheckRT(
      parser.process(rx) == ClientParser::State::Headers);
    ZuCheckRT(!parser.progressed(), log_(
      "stalled H1 parser does not report progress"));
    push(rx, "\r\n");
    ZuCheckRT(
      parser.process(rx) == ClientParser::State::Complete);
    ZuCheckRT(parser.progressed());
  }

  {
    auto wsFrame = frame(Zws::Opcode::Text, "coalesced");
    Bytes coalesced;
    coalesced << responseWire.bufs[0]->cspan() << wsFrame;
    H1Rx rx;
    push(rx, coalesced);
    ClientParser parser{key, "chat"};
    ZuCheckRT(parser.process(rx) == ClientParser::State::Complete);
    ZuCheckRT(parser.valid());
    ZuCheckRT(rx.span() == wsFrame, log_(
      "opening parser preserves coalesced WebSocket bytes"));
  }

  {
    static constexpr uint8_t wsFrame[] = {
      0x09, 0x09, 'f', 'r', 'a', 'g', 'm', 'e', 'n', 't', '1'
    };
    Bytes coalesced;
    coalesced << responseWire.bufs[0]->cspan() << ZuBSpan{wsFrame};
    H1Rx rx;
    push(rx, coalesced);
    ClientParser parser{key, "chat"};
    ZuCheckRT(parser.process(rx) == ClientParser::State::Complete);
    ZuCheckRT(parser.valid());
    ZuCheckRT(rx.span() == ZuBSpan{wsFrame}, log_(
      "opening parser does not fold upgraded binary input"));
  }

  H1Rx badRx;
  push(badRx,
    "HTTP/1.1 101 Switching Protocols\r\n"
    "Upgrade: websocket\r\nConnection: Upgrade\r\n"
    "Sec-WebSocket-Accept: bad\r\n\r\n");
  ClientParser bad{key};
  ZuCheckRT(bad.process(badRx) == ClientParser::State::Complete);
  ZuCheckRT(!bad.valid());

  static constexpr const char *invalidClient[] = {
    "HTTP/1.0 101 Switching Protocols\r\n"
      "Upgrade: websocket\r\nConnection: Upgrade\r\n"
      "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n",
    "HTTP/1.1 200 OK\r\n"
      "Upgrade: websocket\r\nConnection: Upgrade\r\n"
      "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n",
    "HTTP/1.1 101 Switching Protocols\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n",
    "HTTP/1.1 101 Switching Protocols\r\n"
      "Upgrade: websocket\r\nConnection: close\r\n"
      "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n",
    "HTTP/1.1 101 Switching Protocols\r\n"
      "Upgrade: websocket\r\nConnection: Upgrade\r\n\r\n",
    "HTTP/1.1 101 Switching Protocols\r\n"
      "Upgrade: websocket\r\nUpgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n",
    "HTTP/1.1 101 Switching Protocols\r\n"
      "Upgrade: websocket\r\nConnection: Upgrade\r\n"
      "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
      "Content-Length: 0\r\n\r\n",
    "HTTP/1.1 101 Switching Protocols\r\n"
      "Upgrade: websocket\r\nConnection: Upgrade\r\n"
      "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
      "Transfer-Encoding: chunked\r\n\r\n",
    "HTTP/1.1 101 Switching Protocols\r\n"
      "Upgrade: websocket\r\nConnection: Upgrade\r\n"
      "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
      "Sec-WebSocket-Extensions: permessage-deflate\r\n\r\n",
    "HTTP/1.1 101 Switching Protocols\r\n"
      "Upgrade: websocket\r\nConnection: Upgrade\r\n"
      "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
      "Sec-WebSocket-Protocol: other\r\n\r\n"
  };
  for (auto input : invalidClient)
    ZuCheckRT(!clientHandshake(input, key));

  static constexpr const char *invalidServer[] = {
    "POST /chat HTTP/1.1\r\nHost: example.com\r\n"
      "Upgrade: websocket\r\nConnection: Upgrade\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n\r\n",
    "GET /chat HTTP/1.0\r\nHost: example.com\r\n"
      "Upgrade: websocket\r\nConnection: Upgrade\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n\r\n",
    "GET /chat HTTP/1.1\r\n"
      "Upgrade: websocket\r\nConnection: Upgrade\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n\r\n",
    "GET /chat HTTP/1.1\r\nHost: example.com\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n\r\n",
    "GET /chat HTTP/1.1\r\nHost: example.com\r\n"
      "Upgrade: websocket\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n\r\n",
    "GET /chat HTTP/1.1\r\nHost: example.com\r\n"
      "Upgrade: websocket\r\nConnection: Upgrade\r\n"
      "Sec-WebSocket-Key: invalid\r\n"
      "Sec-WebSocket-Version: 13\r\n\r\n",
    "GET /chat HTTP/1.1\r\nHost: example.com\r\n"
      "Upgrade: websocket\r\nConnection: Upgrade\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 12\r\n\r\n",
    "GET /chat HTTP/1.1\r\nHost: example.com\r\n"
      "Upgrade: websocket\r\nConnection: Upgrade\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n\r\n",
    "GET /chat HTTP/1.1\r\nHost: example.com\r\n"
      "Upgrade: websocket\r\nConnection: Upgrade\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\nContent-Length: 0\r\n\r\n",
    "GET /chat HTTP/1.1\r\nHost: example.com\r\n"
      "Upgrade: websocket\r\nConnection: Upgrade\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "Transfer-Encoding: chunked\r\n\r\n"
  };
  for (auto input : invalidServer)
    ZuCheckRT(!serverHandshake(input));
}

void header()
{
  ZuTestScopeRT(header);

  Zws::HeaderParser parser;
  Zws::Frame parsed;
  Zws::Failure::T failure = Zws::Failure::None;
  const uint8_t raw[] = {0x82, 126, 0, 126};
  ZuBSpan first{raw, 1}, second{raw + 1, 3};
  ZuCheckRT(parser.process(first, parsed, failure) ==
    Zws::HeaderParser::Result::Wait);
  ZuCheckRT(parser.process(second, parsed, failure) ==
    Zws::HeaderParser::Result::Complete);
  ZuCheckRT(parsed.final && parsed.opcode == Zws::Opcode::Binary &&
    parsed.length == 126 && !parsed.masked);

  const uint8_t nonCanonical[] = {0x82, 126, 0, 125};
  ZuBSpan bad{nonCanonical};
  failure = Zws::Failure::None;
  ZuCheckRT(parser.process(bad, parsed, failure) ==
    Zws::HeaderParser::Result::Error);
  ZuCheckRT(failure == Zws::Failure::NonCanonicalLength);

  parser.reset();
  struct LengthCase {
    uint64_t length;
    unsigned headerLength;
  };
  static constexpr LengthCase lengths[] = {
    {0, 2}, {125, 2}, {126, 4}, {65535, 4}, {65536, 10}
  };
  for (auto test : lengths) {
    parser.reset();
    Bytes payload;
    payload.length(unsigned(test.length), false);
    auto wire = frame(Zws::Opcode::Binary, payload);
    ZuBSpan input = wire;
    failure = Zws::Failure::None;
    ZuCheckRT(parser.process(input, parsed, failure) ==
      Zws::HeaderParser::Result::Complete);
    ZuCheckRT(parsed.length == test.length);
    ZuCheckRT(wire.length() - input.length() == test.headerLength);
  }

  const uint8_t nonCanonical64[] = {
    0x82, 127, 0, 0, 0, 0, 0, 0, 0xff, 0xff
  };
  ZuBSpan bad64{nonCanonical64};
  failure = Zws::Failure::None;
  ZuCheckRT(parser.process(bad64, parsed, failure) ==
    Zws::HeaderParser::Result::Error);
  ZuCheckRT(failure == Zws::Failure::NonCanonicalLength);

  parser.reset();
  const uint8_t overflow[] = {
    0x82, 127, 0x80, 0, 0, 0, 0, 1, 0, 0
  };
  ZuBSpan badOverflow{overflow};
  failure = Zws::Failure::None;
  ZuCheckRT(parser.process(badOverflow, parsed, failure) ==
    Zws::HeaderParser::Result::Error);
  ZuCheckRT(failure == Zws::Failure::InvalidHeader);
}

struct CounterRandom {
  bool random(uint8_t *data) {
    uint32_t value = ++next;
    data[0] = uint8_t(value>>24);
    data[1] = uint8_t(value>>16);
    data[2] = uint8_t(value>>8);
    data[3] = uint8_t(value);
    return true;
  }
  uint32_t next = 0;
};

void tx()
{
  ZuTestScopeRT(tx);

  struct FailedRandom {
    bool random(uint8_t *) { return false; }
  };

  Ztls::Random random;
  ZuCheckRT(random.init());
  {
    WireTx lower{20};
    auto tx = Zws::txLayer<false>(lower, Zws::Opcode::Text);
    tx << "0123456789";
    unsigned flushes = lower.flushCalls;
    tx.flush();
    ZuCheckRT(lower.flushCalls == flushes + 1);
    tx.flush();
    ZuCheckRT(lower.flushCalls == flushes + 1);
    ZuCheckRT(lower.bufs.length() == 1);
    ZuCheckRT(lower.bufs[0]->data()[0] == 0x81);
    ZuCheckRT(lower.bufs[0]->data()[1] == 10);
    ZuCheckRT(lower.bufs[0]->cspan().offset(2) == "0123456789");
    ZuCheckRT(lower.finals.length() == 1 && lower.finals[0], log_(
      "exact-capacity payload emitted one final frame"));
  }
  {
    WireTx lower{20};
    auto tx = Zws::txLayer<false>(lower, Zws::Opcode::Binary);
    tx << "0123456789abcde";
    tx.flush();
    ZuCheckRT(lower.bufs.length() == 2);
    ZuCheckRT(lower.bufs[0]->data()[0] == 0x02);
    ZuCheckRT(lower.bufs[1]->data()[0] == 0x80);
  }
  {
    WireTx lower;
    auto tx = Zws::txLayer<true>(
      lower, Zws::Opcode::Text, &random);
    tx << "hello";
    tx.flush();
    ZuCheckRT(lower.bufs.length() == 1);
    auto wire = lower.bufs[0]->span();
    ZuCheckRT(wire[0] == 0x81 && wire[1] == (0x80 | 5));
    uint32_t key =
      (uint32_t(wire[2])<<24) | (uint32_t(wire[3])<<16) |
      (uint32_t(wire[4])<<8) | wire[5];
    Zws::mask(wire.offset(6), key);
    ZuCheckRT(wire == "hello");
  }
  {
    WireTx lower;
    auto tx = Zws::txLayer<false>(lower, Zws::Opcode::Text);
    tx.flush();
    ZuCheckRT(lower.bufs.length() == 1);
    const uint8_t expected[] = {0x81, 0};
    ZuCheckRT(lower.bufs[0]->cspan() == ZuBSpan(expected));
  }
  {
    static constexpr unsigned lengths[] = {
      0, 1, 124, 125, 126, 127, 65535, 65536
    };
    for (unsigned length : lengths) {
      Bytes payload;
      payload.length(length, false);
      WireTx lower{length + Zws::MaxHeader + 16};
      auto tx = Zws::txLayer<false>(lower, Zws::Opcode::Binary);
      tx << payload;
      tx.flush();
      ZuCheckRT(lower.bufs.length() == 1);
      if (!lower.bufs.length()) continue;
      auto wire = lower.bufs[0]->cspan();
      Zws::HeaderParser parser;
      Zws::Frame parsed;
      Zws::Failure::T failure = Zws::Failure::None;
      ZuCheckRT(parser.process(wire, parsed, failure) ==
	Zws::HeaderParser::Result::Complete);
      ZuCheckRT(parsed.final && parsed.opcode == Zws::Opcode::Binary);
      ZuCheckRT(parsed.length == length && wire.length() == length);
    }
  }
  {
    CounterRandom counter;
    static constexpr unsigned lengths[] = {
      124, 125, 126, 127, 65535, 65536
    };
    for (unsigned length : lengths) {
      Bytes payload;
      payload.length(length, false);
      const unsigned n = payload.length();
      uint8_t *data = payload.data();
      for (unsigned i = 0; i < n; ++i) data[i] = uint8_t(i);
      WireTx lower{length + Zws::MaxHeader + 16};
      auto tx = Zws::txLayer<true>(
	lower, Zws::Opcode::Binary, &counter);
      tx << payload;
      tx.flush();
      ZuCheckRT(lower.bufs.length() == 1);
      if (!lower.bufs.length()) continue;
      auto mutableWire = lower.bufs[0]->span();
      ZuBSpan wire = mutableWire;
      Zws::HeaderParser parser;
      Zws::Frame parsed;
      Zws::Failure::T failure = Zws::Failure::None;
      ZuCheckRT(parser.process(wire, parsed, failure) ==
	Zws::HeaderParser::Result::Complete);
      ZuCheckRT(parsed.final && parsed.masked &&
	parsed.opcode == Zws::Opcode::Binary &&
	parsed.length == length);
      mutableWire.offset(mutableWire.length() - wire.length());
      Zws::mask(mutableWire, parsed.key);
      ZuCheckRT(mutableWire == payload, log_(
	"masked Tx preserves payload across length boundary"));
    }
  }
  {
    CounterRandom counter;
    WireTx lower{20};
    auto tx = Zws::txLayer<true>(
      lower, Zws::Opcode::Binary, &counter);
    tx << "0123456789abcde";
    tx.flush();
    ZuCheckRT(lower.bufs.length() == 3);
    if (lower.bufs.length() == 3) {
      auto one = lower.bufs[0]->cspan();
      auto two = lower.bufs[1]->cspan();
      auto three = lower.bufs[2]->cspan();
      ZuCheckRT(!(one[0] & 0x80) && (one[0] & 0xf) == Zws::Opcode::Binary);
      ZuCheckRT(!(two[0] & 0x80) &&
	(two[0] & 0xf) == Zws::Opcode::Continuation);
      ZuCheckRT((three[0] & 0x80) &&
	(three[0] & 0xf) == Zws::Opcode::Continuation);
      ZuCheckRT((ZuBSpan{one.data() + 2, 4} !=
	ZuBSpan{two.data() + 2, 4}));
      ZuCheckRT((ZuBSpan{two.data() + 2, 4} !=
	ZuBSpan{three.data() + 2, 4}));
    }
  }
  {
    WireTx lower{64, 3, 5};
    auto tx = Zws::txLayer<false>(lower, Zws::Opcode::Text);
    tx << "headroom";
    tx.flush();
    ZuCheckRT(lower.bufs.length() == 1);
    if (lower.bufs.length()) {
      ZuCheckRT(lower.bufs[0]->skip ==
	lower.headRoom() + (10 - 2));
      ZuCheckRT(lower.bufs[0]->avail() >= lower.tailRoom());
      ZuCheckRT(lower.bufs[0]->cspan().offset(2) == "headroom");
    }
  }
  {
    WireTx lower{20};
    auto tx = Zws::txLayer<false>(lower, Zws::Opcode::Ping);
    tx << "0123456789abcde";
    tx.flush();
    ZuCheckRT(!tx.valid());
    ZuCheckRT(!lower.bufs.length());
  }
  {
    WireTx lower;
    lower.admitOK = false;
    auto tx = Zws::txLayer<false>(lower, Zws::Opcode::Text);
    tx << "abc";
    ZuCheckRT(tx.failed() && !tx.flush() && !lower.sends);
  }
  {
    WireTx lower;
    lower.sendOK = false;
    {
      auto tx = Zws::txLayer<false>(lower, Zws::Opcode::Text);
      tx << "abc";
      ZuCheckRT(!tx.flush() && tx.failed() && lower.failed());
      ZuCheckRT(lower.sends == 1 && !tx.flush());
    }
    ZuCheckRT(lower.sends == 1);
  }
  {
    FailedRandom failed;
    WireTx lower;
    auto tx = Zws::txLayer<true>(
      lower, Zws::Opcode::Binary, &failed);
    tx << "not-sent";
    tx.flush();
    ZuCheckRT(!tx.valid());
    ZuCheckRT(!lower.bufs.length());
  }
  {
    WireTx lower;
    {
      auto tx = Zws::txLayer<false>(lower, Zws::Opcode::Text);
      tx << "discard";
      tx.fail();
      ZuCheckRT(!tx.valid() && !tx.flush());
    }
    ZuCheckRT(!lower.sends && !lower.bufs.length(), log_(
      "aborting an application write must not flush a partial message"));
  }
}

void codec()
{
  ZuTestScope(codec);

  Ztls::Random random;
  ZuCHECK(random.init());
  {
    Link link;
    Zws::Config config;
    config.closeTimeout = 0;
    App<false> app{link, random, config};
    bool called = false;
    app.txStream([&called](auto &tx) {
      called = true;
      tx << "early";
      tx.flush();
    });
    ZuCHECK(!called && !link.tx.bufs.length(),
      "application Tx remains disabled before establishment");
    app.up_();
    app.txStream([](auto &tx) {
      tx << "open";
      tx.flush();
    }, Zws::Opcode::Text);
    ZuCHECK(link.tx.bufs.length() == 1);
    ZuCHECK(app.close_());
    unsigned sent = link.tx.bufs.length();
    app.txStream([](auto &tx) {
      tx << "late";
      tx.flush();
    });
    ZuCHECK(link.tx.bufs.length() == sent,
      "application Tx is disabled when close begins");
  }
  {
    Link link;
    App<true> app{link, random};
    app.up_();
    const uint8_t reason[] = {'h', 0xc3, 0xa9};
    ZuCHECK(app.close_(Zws::CloseCode::Normal, reason));
    const uint8_t payload[] = {0x03, 0xe8, 'h', 0xc3, 0xa9};
    ZuCHECK(link.tx.bufs.length() == 1);
    if (link.tx.bufs.length()) {
      auto wire = link.tx.bufs[0]->cspan();
      ZuCHECK(wire.length() == sizeof(payload) + 2 &&
	wire[0] == 0x88 && wire[1] == sizeof(payload) &&
	wire.offset(2) == ZuBSpan{payload},
	"close transmits exact byte reason");
    }
  }
  {
    Link link;
    App<true> app{link, random};
    app.up_();
    uint8_t reason[Zws::MaxControl - 2];
    for (unsigned i = 0; i < sizeof(reason); ++i) reason[i] = 'x';
    ZuCHECK(app.close_(Zws::CloseCode::Normal, reason));
    ZuCHECK(link.tx.bufs.length() == 1);
    if (link.tx.bufs.length()) {
      auto wire = link.tx.bufs[0]->cspan();
      ZuCHECK(wire.length() == Zws::MaxControl + 2 &&
	wire[0] == 0x88 && wire[1] == Zws::MaxControl &&
	wire.offset(4) == ZuBSpan{reason},
	"close accepts the maximum reason length");
    }
  }
  {
    Link link;
    App<true> app{link, random};
    app.up_();
    uint8_t reason[Zws::MaxControl - 1] = {};
    ZuCHECK(!app.close_(Zws::CloseCode::Normal, reason) &&
      !app.close_(1005) && !link.tx.bufs.length(),
      "close rejects overlong reasons and invalid codes");
  }
  {
    Link link;
    App<true> app{link, random};
    app.up_();
    const uint8_t reason[] = {0xc0, 0x80};
    ZuCHECK(app.close_(Zws::CloseCode::Normal, reason) &&
      link.tx.bufs.length() == 1,
      "close reason bytes are application-defined");
  }
  {
    Link link;
    App<false> app{link, random};
    Bytes empty;
    ZuCHECK(app.feed(empty) == 0 && !app.calls);
    auto wire = frame(Zws::Opcode::Text, "hello");
    ZuCHECK(app.feed(wire) > 0);
    ZuCHECK(app.messages.length() == 1 && app.messages[0] == "hello");
    ZuCHECK(app.starts == 1 && app.finals == 1);
    ZuCHECK(app.inputData == app.wireData + 2,
      "move pooled payload storage without copying");
  }
  {
    Link link;
    App<false> app{link, random};
    app.reject = true;
    auto wire = frame(Zws::Opcode::Binary, "rejected");
    ZuCHECK(app.feed(wire) < 0);
    ZuCHECK(app.failure() == Zws::Failure::None);
    ZuCHECK(app.errors == 0);
  }
  {
    Link link;
    App<false> app{link, random};
    auto wire = frame(Zws::Opcode::Binary, "split");
    Bytes first, second;
    first << ZuBSpan{wire.data(), 1};
    second << ZuBSpan{wire.data() + 1, wire.length() - 1};
    ZuCHECK(app.feed(first) > 0 && app.queuedInput() == 1 &&
      !app.messages.length());
    ZuCHECK(app.feed(second) > 0);
    ZuCHECK(app.messages.length() == 1 && app.messages[0] == "split");
    ZuCHECK(app.starts == 1 && app.finals == 1,
      "outer Start is not a WebSocket message boundary");
  }
  {
    Link link;
    App<false> app{link, random};
    auto wire = frame(Zws::Opcode::Binary, "abcdef");
    Bytes first, second;
    first << ZuBSpan{wire.data(), 5};
    second << ZuBSpan{wire.data() + 5, wire.length() - 5};
    ZuCHECK(app.feed(first) > 0);
    ZuCHECK(app.current == "abc" && !app.messages.length(),
      "deliver an incomplete frame payload as it arrives");
    ZuCHECK(app.feed(second) > 0);
    ZuCHECK(app.messages.length() == 1 && app.messages[0] == "abcdef");
  }
  {
    Link link;
    App<false> app{link, random};
    Bytes wire;
    wire << frame(Zws::Opcode::Text, "one") <<
      frame(Zws::Opcode::Binary, "two");
    ZuCHECK(app.feed(wire) > 0);
    ZuCHECK(app.messages.length() == 2 &&
      app.messages[0] == "one" && app.messages[1] == "two");
    ZuCHECK(app.opcodes.length() == 2 &&
      app.opcodes[0] == Zws::Opcode::Text &&
      app.opcodes[1] == Zws::Opcode::Binary);
  }
  {
    Link link;
    App<true> app{link, random};
    Bytes wire;
    wire << frame(Zws::Opcode::Text, "hel", false, true) <<
      frame(Zws::Opcode::Ping, "p", true, true) <<
      frame(Zws::Opcode::Continuation, "lo", true, true);
    ZuCHECK(app.feed(wire) > 0);
    ZuCHECK(app.messages.length() == 1 && app.messages[0] == "hello");
    ZuCHECK(app.starts == 1 && app.finals == 1,
      "control input is not a WebSocket message boundary");
    ZuCHECK(link.tx.bufs.length() == 1);
    if (link.tx.bufs.length()) {
      ZuCHECK(link.tx.bufs[0]->data()[0] == 0x8a);
      ZuCHECK(link.tx.bufs[0]->cspan().offset(2) == "p");
    }
  }
  {
    Link link;
    App<true> app{link, random};
    Bytes wire;
    wire << frame(Zws::Opcode::Text, "fragment1", false, true) <<
      frame(Zws::Opcode::Ping, "ping payload", true, true) <<
      frame(Zws::Opcode::Continuation, "fragment2", true, true);
    const unsigned n = wire.length();
    bool fed = true;
    unsigned failedAt = 0;
    for (unsigned i = 0; i < n; ++i) {
      Bytes octet;
      octet << wire[i];
      if (app.feed(octet) < 0) {
	fed = false;
	failedAt = i;
	break;
      }
    }
    ZuCHECK(fed, failedAt, Zws::Failure{}.name(app.failure()));
    ZuCHECK(app.messages.length() == 1 &&
      app.messages[0] == "fragment1fragment2");
    ZuCHECK(link.tx.bufs.length() == 1);
    if (link.tx.bufs.length()) {
      ZuCHECK(link.tx.bufs[0]->data()[0] == 0x8a);
      ZuCHECK(link.tx.bufs[0]->cspan().offset(2) == "ping payload");
    }
  }
  {
    Link link;
    App<false> app{link, random};
    app.recordLength = 5;
    auto first = frame(Zws::Opcode::Binary, "abc", false);
    auto second = frame(Zws::Opcode::Continuation, "deX");
    auto third = frame(Zws::Opcode::Binary, "12345");
    ZuCHECK(app.feed(first) > 0 && !app.messages.length());
    ZuCHECK(app.feed(second) > 0 && app.messages.length() == 1 &&
      app.messages[0] == "abcde");
    ZuCHECK(app.feed(third) > 0 && app.messages.length() == 2 &&
      app.messages[1] == "12345",
      "unread final bytes are discarded before the next message");
  }
  {
    Link link;
    App<false> app{link, random};
    const uint8_t first[] = {0xe2, 0x82};
    const uint8_t second[] = {0xac};
    Bytes wire;
    wire << frame(Zws::Opcode::Text, first, false) <<
      frame(Zws::Opcode::Continuation, second);
    ZuCHECK(app.feed(wire) > 0);
    const uint8_t expected[] = {0xe2, 0x82, 0xac};
    ZuCHECK(app.messages.length() == 1 &&
      app.messages[0] == ZuBSpan{expected});
  }
  {
    Link link;
    App<false> app{link, random};
    const uint8_t partial[] = {0xe2, 0x82};
    Bytes wire;
    wire << frame(Zws::Opcode::Text, partial, false) <<
      frame(Zws::Opcode::Continuation, {});
    ZuCHECK(app.feed(wire) > 0 && app.messages.length() == 1 &&
      app.messages[0] == ZuBSpan{partial},
      "text bytes are delivered without codec-level UTF-8 validation");
  }
  {
    Link link;
    App<false> app{link, random};
    auto wire = frame(Zws::Opcode::Binary, "masked", true, true);
    ZuCHECK(app.feed(wire) < 0);
    ZuCHECK(app.failure() == Zws::Failure::MaskDirection);
  }
  {
    Link link;
    App<true> app{link, random};
    auto wire = frame(Zws::Opcode::Binary, "unmasked");
    ZuCHECK(app.feed(wire) < 0);
    ZuCHECK(app.failure() == Zws::Failure::MaskDirection);
  }
  {
    Link link;
    App<false> app{link, random};
    const uint8_t badUTF8[] = {0xc0, 0x80};
    auto wire = frame(Zws::Opcode::Text, badUTF8);
    ZuCHECK(app.feed(wire) > 0 && app.messages.length() == 1 &&
      app.messages[0] == ZuBSpan{badUTF8});
  }
  {
    Link link;
    App<true> app{link, random};
    app.up_();
    const uint8_t badUTF8[] = {0xce};
    auto wire = frame(Zws::Opcode::Text, badUTF8, true, true);
    ZuCHECK(app.feed(wire) > 0 && app.messages.length() == 1 &&
      app.messages[0] == ZuBSpan{badUTF8} && !link.ended &&
      !link.tx.bufs.length());
  }
  {
    Link link;
    Zws::Config config;
    config.maxMessage = 4;
    App<false> app{link, random, config};
    Bytes wire;
    wire << frame(Zws::Opcode::Binary, "abc", false) <<
      frame(Zws::Opcode::Continuation, "de");
    ZuCHECK(app.feed(wire) < 0);
    ZuCHECK(app.failure() == Zws::Failure::MessageTooLarge);
  }
  {
    Link link;
    Zws::Config config;
    config.maxQueuedInput = 2;
    App<false> app{link, random, config};
    auto wire = frame(Zws::Opcode::Binary, "abc");
    ZuCHECK(app.feed(wire) < 0);
    ZuCHECK(app.failure() == Zws::Failure::InputPressure);
  }
  {
    Link link;
    Zws::Config config;
    config.maxQueuedInput = 8;
    App<false> app{link, random, config};
    app.recordLength = 100;
    auto first = frame(Zws::Opcode::Binary, "abc", false);
    auto second = frame(Zws::Opcode::Continuation, "defghi");
    ZuCHECK(app.feed(first) > 0 && !app.messages.length());
    ZuCHECK(app.feed(second) < 0 &&
      app.failure() == Zws::Failure::InputPressure,
      "stalled application input is bounded by queued message length");
  }
  {
    Link link;
    App<false> app{link, random};
    auto wire = frame(Zws::Opcode::Continuation, "x");
    ZuCHECK(app.feed(wire) < 0);
    ZuCHECK(app.failure() == Zws::Failure::UnexpectedContinuation);
  }
  {
    Link link;
    App<false> app{link, random};
    Bytes wire;
    wire << frame(Zws::Opcode::Binary, "a", false) <<
      frame(Zws::Opcode::Text, "b");
    ZuCHECK(app.feed(wire) < 0);
    ZuCHECK(app.failure() == Zws::Failure::MissingContinuation);
  }
  {
    Link link;
    App<false> app{link, random};
    auto wire = frame(Zws::Opcode::Ping, "x", false);
    ZuCHECK(app.feed(wire) < 0);
    ZuCHECK(app.failure() == Zws::Failure::ControlFragment);
  }
  {
    Link link;
    App<false> app{link, random};
    Bytes payload;
    payload.length(126, false);
    auto wire = frame(Zws::Opcode::Ping, payload);
    ZuCHECK(app.feed(wire) < 0);
    ZuCHECK(app.failure() == Zws::Failure::ControlTooLarge);
  }
  {
    Link link;
    App<false> app{link, random};
    Bytes wire;
    wire << uint8_t(0xc2) << uint8_t(0);
    ZuCHECK(app.feed(wire) < 0);
    ZuCHECK(app.failure() == Zws::Failure::ReservedBits);
  }
  {
    Link link;
    App<false> app{link, random};
    Bytes wire;
    wire << uint8_t(0x83) << uint8_t(0);
    ZuCHECK(app.feed(wire) < 0);
    ZuCHECK(app.failure() == Zws::Failure::InvalidOpcode);
  }
  {
    Link link;
    App<true> app{link, random};
    const uint8_t close[] = {0x03, 0xe8, 'b', 'y', 'e'};
    auto wire = frame(Zws::Opcode::Close, close, true, true);
    ZuCHECK(app.feed(wire) > 0);
    ZuCHECK(app.peerClosed() && app.closeSent() &&
      link.ended && link.endAfterFlush);
    ZuCHECK(link.endCalls == 1 && !link.resetCalls,
      "peer close response precedes one orderly end");
    ZuCHECK(app.closeCode == Zws::CloseCode::Normal &&
      app.closeReason == "bye");
    ZuCHECK(link.tx.bufs.length() == 1);
    if (link.tx.bufs.length()) {
      ZuCHECK(link.tx.bufs[0]->data()[0] == 0x88);
      ZuCHECK(link.tx.bufs[0]->cspan().offset(2) == ZuBSpan{close});
    }
    unsigned errors = app.errors;
    app.peerEnd();
    ZuCHECK(app.errors == errors,
      "peer end after valid close is not another terminal event");
    app.streamError();
    ZuCHECK(app.errors == errors && link.endCalls == 1,
      "post-terminal stream error is suppressed");
  }
  {
    Link link;
    App<true> app{link, random};
    auto wire = frame(Zws::Opcode::Close, {}, true, true);
    ZuCHECK(app.feed(wire) > 0);
    ZuCHECK(app.closeCode == Zws::CloseCode::NoStatus);
  }
  {
    Link link;
    App<true> app{link, random};
    const uint8_t invalid[] = {0};
    auto wire = frame(Zws::Opcode::Close, invalid, true, true);
    ZuCHECK(app.feed(wire) < 0);
    ZuCHECK(app.failure() == Zws::Failure::InvalidClose);
  }
  {
    Link link;
    App<true> app{link, random};
    const uint8_t invalid[] = {0x03, 0xed};
    auto wire = frame(Zws::Opcode::Close, invalid, true, true);
    ZuCHECK(app.feed(wire) < 0);
    ZuCHECK(app.failure() == Zws::Failure::InvalidClose);
  }
  {
    Link link;
    App<true> app{link, random};
    const uint8_t invalid[] = {0x03, 0xe8, 0xc0, 0x80};
    auto wire = frame(Zws::Opcode::Close, invalid, true, true);
    ZuBSpan reason{invalid};
    reason.offset(2);
    ZuCHECK(app.feed(wire) > 0 && app.peerClosed() &&
      app.closeCode == Zws::CloseCode::Normal &&
      app.closeReason == reason,
      "close reason bytes are delivered to the application");
  }
  {
    Link link;
    App<true> app{link, random};
    ZuCHECK(app.close_());
    const uint8_t close[] = {0x03, 0xe8};
    Bytes wire;
    wire << frame(Zws::Opcode::Text, "ignored", true, true) <<
      frame(Zws::Opcode::Close, close, true, true);
    ZuCHECK(app.feed(wire) > 0);
    ZuCHECK(!app.messages.length() && link.ended &&
      link.endCalls == 1 && !link.resetCalls);
  }
  {
    Link link;
    App<false> app{link, random};
    app.up_();
    app.peerEnd();
    ZuCHECK(app.failure() == Zws::Failure::AbnormalClose);
    ZuCHECK(app.errors == 1);
  }
  {
    Link link;
    App<false> app{link, random};
    app.up_();
    app.streamError();
    ZuCHECK(app.failure() == Zws::Failure::AbnormalClose);
    ZuCHECK(app.errors == 1);
  }
  {
    Link link;
    Zws::Config config;
    config.handshakeTimeout = 1;
    App<false> app{link, random, config};
    app.opening_();
    ZuCHECK(link.hub.mx_.active() == 1);
    ZuCHECK(link.hub.mx_.fire());
    ZuCHECK(app.failure() == Zws::Failure::Timeout);
    ZuCHECK(link.reset && link.resetCalls == 1 && app.errors == 1);
  }
  {
    Link link;
    Zws::Config config;
    config.pingInterval = 0;
    config.pongTimeout = 1;
    App<false> app{link, random, config};
    app.up_();
    ZuCHECK(app.ping_("probe"));
    ZuCHECK(link.tx.bufs.length() == 1);
    ZuCHECK(link.hub.mx_.active() == 1);
    auto wire = frame(Zws::Opcode::Pong, "probe");
    ZuCHECK(app.feed(wire) > 0);
    ZuCHECK(app.pongPayload == "probe");
    ZuCHECK(link.hub.mx_.active() == 0);
  }
  {
    Link link;
    Zws::Config config;
    config.pingInterval = 1;
    config.pongTimeout = 1;
    App<false> app{link, random, config};
    app.up_();
    ZuCHECK(link.hub.mx_.active() == 1);
    ZuCHECK(link.hub.mx_.fire());
    ZuCHECK(link.tx.bufs.length() == 1);
    ZuCHECK(link.hub.mx_.active() == 1);
    ZuCHECK(link.hub.mx_.fire());
    ZuCHECK(app.failure() == Zws::Failure::Timeout);
    ZuCHECK(link.reset && link.resetCalls == 1 && app.errors == 1);
  }
  {
    struct FailedRandom {
      bool random(uint8_t *) { return false; }
    } random_;
    Link link;
    App<false, FailedRandom> app{link, random_};
    app.up_();
    ZuCHECK(!app.ping_("not-sent"));
    ZuCHECK(app.failure() == Zws::Failure::Transmit);
    ZuCHECK(link.reset && link.resetCalls == 1 &&
      !link.tx.bufs.length());
  }
  {
    Link link;
    Zws::Config config;
    config.pingInterval = 1;
    App<false> app{link, random, config};
    app.up_();
    unsigned adds = link.hub.mx_.adds;
    auto wire = frame(Zws::Opcode::Binary, "activity");
    ZuCHECK(app.feed(wire) > 0);
    ZuCHECK(link.hub.mx_.adds == adds + 1,
      "input postpones idle ping");
    ZuCHECK(link.hub.mx_.active() == 1);
  }
  {
    Link link;
    Zws::Config config;
    config.closeTimeout = 1;
    App<false> app{link, random, config};
    app.up_();
    ZuCHECK(app.close_());
    ZuCHECK(link.tx.bufs.length() == 1);
    ZuCHECK(link.hub.mx_.active() == 1);
    ZuCHECK(link.hub.mx_.fire());
    ZuCHECK(app.failure() == Zws::Failure::Timeout);
    ZuCHECK(link.reset && link.resetCalls == 1 && app.errors == 1);
  }
}

template <bool Masked>
void txRangeCase()
{
  ZuTestScope(txRangeCase);
  WireTx lower;
  CounterRandom random;
  ZmRef<ZiIOBuf> source = new BufAlloc{nullptr, 0};
  source->skip = 5;
  *source << "xxpayloadtail";
  auto data = source->data();
  auto out = Zws::txLayer<Masked>(lower, Zws::Opcode::Binary, &random);
  ZuCheck(out.appendRange(source, 2, 7) && out.flush());
  Bytes bytes;
  bool retained = false;
  for (const auto &part : lower.parts) {
    retained |= part.buf == source && part.offset == 7 && part.length == 7;
    bytes << part.cspan();
  }
  if constexpr (Masked) ZuCheck(!retained);
  else ZuCheck(retained);
  ZuCheck(source->data() == data && source->skip == 5 && source->cspan() == "xxpayloadtail");
  ZuBSpan wire = bytes;
  Zws::HeaderParser parser;
  Zws::Frame frame_;
  Zws::Failure::T failure = Zws::Failure::None;
  ZuCheck(parser.process(wire, frame_, failure) == Zws::HeaderParser::Result::Complete);
  ZuCheck(!frame_.final && frame_.length == 7 && frame_.masked == Masked);
  auto prefix = unsigned(wire.data() - bytes.data());
  auto payload = bytes.span().offset(prefix).trunc(7);
  if constexpr (Masked) Zws::mask(payload, frame_.key);
  ZuCheck(payload == "payload");
  wire.offset(7);
  parser.reset();
  ZuCheck(parser.process(wire, frame_, failure) == Zws::HeaderParser::Result::Complete);
  ZuCheck(frame_.final && !frame_.length && frame_.opcode == Zws::Opcode::Continuation);
}

template <bool Masked>
void txMixedRangeCase()
{
  ZuTestScopeRT(txMixedRangeCase);
  WireTx lower;
  CounterRandom random;
  ZmRef<ZiIOBuf> source = new BufAlloc{nullptr, 0};
  *source << "payload";
  auto out = Zws::txLayer<Masked>(lower, Zws::Opcode::Binary, &random);
  out << "pre";
  ZuCheckRT(out.appendRange(source, 0, source->length) && out.flush());
  Bytes bytes;
  for (const auto &part : lower.parts) bytes << part.cspan();
  ZuBSpan wire = bytes;
  Bytes decoded;
  unsigned frames = 0;
  bool ended = false;
  while (wire.length()) {
    Zws::HeaderParser parser;
    Zws::Frame frame_;
    Zws::Failure::T failure = Zws::Failure::None;
    ZuCheckRT(parser.process(wire, frame_, failure) == Zws::HeaderParser::Result::Complete);
    ZuCheckRT(frame_.length <= wire.length(), log_("mixed range frame exceeds available bytes"));
    if (frame_.length > wire.length()) break;
    ZuCheckRT(!ended && frame_.opcode == (frames ? Zws::Opcode::Continuation : Zws::Opcode::Binary));
    auto payload = bytes.span().offset(unsigned(wire.data() - bytes.data()))
      .trunc(unsigned(frame_.length));
    if constexpr (Masked) Zws::mask(payload, frame_.key);
    decoded << payload;
    wire.offset(unsigned(frame_.length));
    ended = frame_.final;
    if (ended) ZuCheckRT(!wire.length());
    ++frames;
  }
  ZuCheckRT(frames == 3 && ended && decoded == "prepayload" && source->cspan() == "payload");
}

struct TCPOwnedLink {
  using TxBufAlloc = Zhttp::Transport_::TCPTxBufAlloc;
  ZmRef<Zi::IOBufAllocator> allocator = new TxBufAlloc;
  ZmRef<Zhttp::Transport_::TCPHeader> head;
  ZtArray<Zi::RxRange, ZtArrayHeapID<"Zws.Test.OwnedParts">> parts;
  unsigned allocated = 0, ended = 0;

  template <bool> ZmRef<ZiIOBuf> allocTxBuf_() {
    ++allocated;
    return allocator->alloc();
  }
  bool startTCP_(ZmRef<Zhttp::Transport_::TCPHeader> h, bool) {
    head = ZuMv(h); return true;
  }
  bool partTCP_(ZmRef<Zhttp::Transport_::TCPHeader>, Zi::RxRange range) {
    parts.push(ZuMv(range)); return true;
  }
  void endTCP_(ZmRef<Zhttp::Transport_::TCPHeader>) { ++ended; }
};

void txOwnedTCP()
{
  ZuTestScopeRT(txOwnedTCP);
  TCPOwnedLink link;
  Zhttp::Transport_::TCPStream<TCPOwnedLink, false> lower{link};
  CounterRandom random;
  ZmRef<ZiIOBuf> first, later;
  {
    auto out = Zws::txLayer<true>(lower, Zws::Opcode::Binary, &random);
    first = out.allocBuf(out.headRoom());
    *first << "first";
    out << first;
    ZuCheckRT(out.flush() && link.allocated == 1 && link.head.ptr() == first.ptr());
  }
  {
    auto out = Zws::txLayer<true>(lower, Zws::Opcode::Binary, &random);
    later = out.allocBuf(out.headRoom());
    *later << "later";
    out << later;
    ZuCheckRT(out.flush() && link.allocated == 2 && link.parts.length() == 1 &&
      link.parts[0].buf.ptr() == later.ptr());
  }
  Bytes replay;
  replay << later->cspan();
  ZuCheckRT(!lower.allocatedBuf_(later.ptr()));
  {
    auto out = Zws::txLayer<true>(lower, Zws::Opcode::Binary, &random);
    out << later;
    ZuCheckRT(out.flush() && link.allocated == 4 && link.parts.length() == 3 &&
      link.parts[1].buf.ptr() != later.ptr() && later->cspan() == replay);
  }
  ZmRef<ZiIOBuf> borrowed = new BufAlloc{nullptr, 0};
  *borrowed << "borrowed";
  {
    auto out = Zws::txLayer<true>(lower, Zws::Opcode::Binary, &random);
    out << borrowed;
    ZuCheckRT(out.flush() && link.allocated == 6 && link.parts.length() == 5 &&
      link.parts[3].buf.ptr() != borrowed.ptr() && borrowed->cspan() == "borrowed");
  }
  // Decode a test-only snapshot; the actual final backing identities above
  // are the zero-copy oracle, and borrowed input remains immutable.
  Bytes bytes;
  bytes << link.head->cspan();
  for (const auto &part : link.parts) bytes << part.cspan();
  ZuBSpan wire = bytes;
  unsigned index = 0;
  while (wire.length()) {
    Zws::HeaderParser parser;
    Zws::Frame frame_;
    Zws::Failure::T failure = Zws::Failure::None;
    bool parsed = parser.process(wire, frame_, failure) ==
      Zws::HeaderParser::Result::Complete;
    ZuCheckRT(parsed && frame_.masked && frame_.length <= wire.length());
    if (!parsed || frame_.length > wire.length()) break;
    auto payload = bytes.span().offset(unsigned(wire.data() - bytes.data()))
      .trunc(unsigned(frame_.length));
    Zws::mask(payload, frame_.key);
    switch (index) {
      case 0: ZuCheckRT(frame_.final && frame_.opcode == Zws::Opcode::Binary && payload == "first"); break;
      case 1: ZuCheckRT(frame_.final && frame_.opcode == Zws::Opcode::Binary && payload == "later"); break;
      case 2: ZuCheckRT(!frame_.final && frame_.opcode == Zws::Opcode::Binary && payload == replay); break;
      case 3: case 5: ZuCheckRT(frame_.final && frame_.opcode == Zws::Opcode::Continuation && !payload.length()); break;
      case 4: ZuCheckRT(!frame_.final && frame_.opcode == Zws::Opcode::Binary && payload == "borrowed"); break;
      default: ZuCheckRT(false); break;
    }
    wire.offset(unsigned(frame_.length)); ++index;
  }
  lower.endTCP_();
  ZuCheckRT(index == 6 && link.ended == 1 && borrowed->cspan() == "borrowed" &&
    later->cspan() == replay);
}

void txOwnedTCPTurns(bool prior)
{
  ZuTestScopeRT(txOwnedTCPTurns);
  TCPOwnedLink link;
  ZmRef<ZiIOBuf> packet;
  {
    Zhttp::Transport_::TCPStream<TCPOwnedLink, true> build{link};
    Zhttp::Writer writer{&build, Zhttp::Native_::writerOps<decltype(build)>()};
    packet = writer.allocBuf(14);
    *packet << "queued";
  } // allocation authority from the build turn has ended
  ZuCheckRT(link.allocated == 1 && !link.head && !link.ended);
  {
    Zhttp::Transport_::TCPStream<TCPOwnedLink, false> send{link};
    CounterRandom random;
    Zhttp::Writer writer{&send, Zhttp::Native_::writerOps<decltype(send)>()};
    auto out = Zws::txLayer<true>(writer, Zws::Opcode::Binary, &random);
    if (prior) out << "pre";
    ZuCheckRT(out.appendOwned_(packet) && out.flush());
    ZuCheckRT(link.allocated == unsigned(prior ? 2 : 1) &&
      (prior ? link.parts.length() == 1 && link.parts[0].buf.ptr() == packet.ptr() :
	link.head.ptr() == packet.ptr()));
    Bytes bytes;
    bytes << link.head->cspan();
    for (const auto &part : link.parts) bytes << part.cspan();
    ZuBSpan wire = bytes;
    Bytes decoded;
    unsigned index = 0;
    while (wire.length()) {
      Zws::HeaderParser parser;
      Zws::Frame frame_;
      Zws::Failure::T failure = Zws::Failure::None;
      bool parsed = parser.process(wire, frame_, failure) ==
	Zws::HeaderParser::Result::Complete;
      ZuCheckRT(parsed && frame_.masked && frame_.length <= wire.length());
      if (!parsed || frame_.length > wire.length()) break;
      auto payload = bytes.span().offset(unsigned(wire.data() - bytes.data()))
	.trunc(unsigned(frame_.length));
      Zws::mask(payload, frame_.key); decoded << payload;
      ZuCheckRT(frame_.opcode == (index ? Zws::Opcode::Continuation : Zws::Opcode::Binary) &&
	frame_.final == (!prior || index == 1));
      wire.offset(unsigned(frame_.length)); ++index;
    }
    ZuCheckRT(index == unsigned(prior ? 2 : 1) && decoded == (prior ? "prequeued" : "queued"));
    ZuCheckRT(!writer.allocatedBuf_(packet.ptr()));
    Bytes original; original << packet->cspan();
    unsigned allocated = link.allocated, parts = link.parts.length();
    auto replay = Zws::txLayer<true>(writer, Zws::Opcode::Binary, &random);
    replay << packet;
    ZuCheckRT(replay.flush() && link.allocated == allocated + 2 &&
      link.parts.length() == parts + 2 && link.parts[parts].buf.ptr() != packet.ptr() &&
      packet->cspan() == original);
  }
  ZuCheckRT(link.ended == 1);
}

void txOwnedNoCapability()
{
  ZuTestScope(txOwnedNoCapability);
  TCPOwnedLink link;
  Zhttp::Transport_::TCPStream<TCPOwnedLink, false> lower{link};
  auto ops = Zhttp::Native_::writerOps<decltype(lower)>();
  ops.allocated = nullptr; ops.takeAllocated = nullptr;
  Zhttp::Writer writer{&lower, ops};
  auto packet = link.allocTxBuf_<false>();
  *packet << "held";
  CounterRandom random;
  auto out = Zws::txLayer<true>(writer, Zws::Opcode::Binary, &random);
  ZuCheck(!writer.allocatedBuf_(packet.ptr()));
  ZuCheck(!out.appendOwned_(packet) && !out.valid() && writer.failed());
  ZuCheck(!link.head && !link.parts.length() && packet->cspan() == "held");
}

} // namespace ZwsProtocolTest_

int main(int argc, char **argv)
{
  using namespace ZwsProtocolTest_;

  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(uri);
  ZuTestCall(handshake);
  ZuTestCall(h1);
  ZuTestCall(header);
  ZuTestCall(tx);
  ZuTestCall(txOwnedTCP);
  ZuTestCall(txOwnedTCPTurns, false);
  ZuTestCall(txOwnedTCPTurns, true);
  ZuTestCall(txOwnedNoCapability);
  ZuTestCall(txRangeCase<false>);
  ZuTestCall(txRangeCase<true>);
  ZuTestCall(txMixedRangeCase<false>);
  ZuTestCall(txMixedRangeCase<true>);
  ZuTestCall(codec);
  return 0;
}
