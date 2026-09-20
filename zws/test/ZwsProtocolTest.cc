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

using namespace ZuTestUtil;

namespace ZwsProtocolTest_ {

using BufAlloc = ZiIOBufAlloc<256, 1<<20, "ZwsProtocolTest.Buf">;

struct WireTx : public ZiTxStream<WireTx> {
  using Base = ZiTxStream<WireTx>;
  using Bufs = ZtArray<ZmRef<ZiIOBuf>, ZtArrayHeapID<"ZwsTest.Wire">>;

  WireTx(
      unsigned size = 256, unsigned headRoom = 0,
      unsigned tailRoom = 0) :
    Base{size, headRoom, tailRoom} { }

  ZmRef<ZiIOBuf> allocBuf_(unsigned headRoom) {
    ZmRef<ZiIOBuf> buf = new BufAlloc{};
    buf->skip = headRoom;
    return buf;
  }
  bool sendBuf_(ZmRef<ZiIOBuf> buf, bool final) {
    if (!buf || !buf->length) return false;
    bufs.push(ZuMv(buf));
    finals.push(uint8_t(final));
    return true;
  }
  void flush() {
    ++flushCalls;
    Base::flush();
  }

  Bufs					bufs;
  ZtArray<uint8_t, ZtArrayHeapID<"ZwsTest.Final">> finals;
  unsigned				flushCalls = 0;
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

struct Link {
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

using OuterRx = ZiRxStream<ZiRxQueue>;
using OuterBufAlloc = Zi::IOBufAlloc<
	ZiRxQueue::Node, 256, 1<<20, ZuStringT<"ZwsTest.OuterRx">>;

using Bytes = ZtArray<uint8_t, ZtArrayHeapID<"ZwsTest.Bytes">>;

using H1Rx = ZiRxStream<ZiRxQueue>;
using H1BufAlloc = Zi::IOBufAlloc<
	ZiRxQueue::Node, 256, 2048, ZuStringT<"ZwsTest.H1Rx">>;

void push(H1Rx &rx, ZuCSpan data)
{
  ZmRef<ZiRxQueue::Node> buf = new H1BufAlloc{};
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
  int message(typename Base::Rx &rx) {
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
      ZmRef<ZiRxQueue::Node> buf = new OuterBufAlloc{};
      *buf << wire;
      auto span = buf->span();
      wireData = span.data();
      rx.push(ZuMv(buf));
    }
    return this->process(Zhttp::Stream{link}, rx);
  }
  void peerEnd() {
    Base::peerEnd(Zhttp::Stream{link});
  }
  void streamError() {
    Base::streamError(Zhttp::Stream{link});
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
  ZuTestScope(uri);

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
    ZuCHECK(error.ok(), test.input);
    ZuCHECK(uri.host == test.host, test.input);
    ZuCHECK(uri.target == test.target, test.input);
    ZuCHECK(uri.port == test.port, test.input);
    ZuCHECK(uri.secure() == test.secure, test.input);
    ZuCHECK(uri.explicitPort == test.explicitPort, test.input);
  }
  static constexpr const char *invalid[] = {
    "http://example.com/", "ws:///", "ws://user@example.com/",
    "ws://example.com/#fragment", "ws://example.com:0/",
    "ws://example.com:65536/", "ws://2001:db8::1/",
    "ws://[not-ipv6]/", "ws://bad%2/example"
  };
  for (auto input : invalid) {
    Zws::URI uri;
    ZuCHECK(!Zws::URI::parse(uri, input).ok(), input);
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

bool clientHandshake(
    ZuCSpan input, ZuCSpan key, ZuCSpan protocol = {})
{
  H1Rx rx;
  push(rx, input);
  Zws::H1::ClientParser parser{key, protocol};
  return parser.process(rx) == Zws::H1::ClientParser::State::Complete &&
    parser.valid();
}

bool serverHandshake(ZuCSpan input)
{
  H1Rx rx;
  push(rx, input);
  Zws::H1::ServerParser parser;
  return parser.process(rx) == Zws::H1::ServerParser::State::Complete &&
    parser.valid();
}

void h1()
{
  ZuTestScope(h1);

  Zws::URI uri;
  ZuCHECK(Zws::URI::parse(
    uri, "wss://example.com:8443/feed?x=1").ok());
  ZuCSpan key = "dGhlIHNhbXBsZSBub25jZQ==";

  WireTx requestWire{2048};
  Zws::H1::Request request{uri, key, "chat"};
  ZuCHECK(request.begin(requestWire));
  requestWire.flush();
  ZuCHECK(requestWire.bufs.length() == 1);
  auto requestBytes = requestWire.bufs[0]->cspan();
  ZuCHECK(requestBytes.find("GET /feed?x=1 HTTP/1.1\r\n") == 0);
  ZuCHECK(requestBytes.find("host: example.com:8443\r\n") >= 0);
  ZuCHECK(requestBytes.find("upgrade: websocket\r\n") >= 0);
  ZuCHECK(requestBytes.find(
    "sec-websocket-key: dGhlIHNhbXBsZSBub25jZQ==\r\n") >= 0);

  H1Rx requestRx;
  unsigned split = requestBytes.length() / 2;
  auto first = requestBytes;
  auto second = requestBytes;
  first.trunc(split);
  second.offset(split);
  push(requestRx, first);
  push(requestRx, second);
  Zws::H1::ServerParser requestParser;
  auto state = requestParser.process(requestRx);
  ZuCHECK(state == Zws::H1::ServerParser::State::Complete);
  ZuCHECK(requestParser.valid());
  ZuCHECK(requestParser.host() == "example.com:8443");
  ZuCHECK(requestParser.target() == "/feed?x=1");
  ZuCHECK(requestParser.protocols() == "chat");

  Zws::HandshakeString accept;
  ZuCHECK(Zws::accept(accept, requestParser.key()));
  WireTx responseWire{2048};
  Zws::H1::Response response{accept, "chat"};
  response.begin(responseWire);
  responseWire.flush();

  H1Rx responseRx;
  push(responseRx, responseWire.bufs[0]->cspan());
  Zws::H1::ClientParser responseParser{key, "chat"};
  state = responseParser.process(responseRx);
  ZuCHECK(state == Zws::H1::ClientParser::State::Complete);
  ZuCHECK(responseParser.valid());
  ZuCHECK(responseParser.selected() == "chat");

  {
    H1Rx rx;
    push(rx,
      "HTTP/1.1 101 Switching Protocols\r\n"
      "Upgrade: websocket\r\nConnection: Upgrade\r\n"
      "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n");
    Zws::H1::ClientParser parser{key};
    ZuCHECK(
      parser.process(rx) == Zws::H1::ClientParser::State::Headers);
    ZuCHECK(parser.progressed(),
      "incomplete H1 parser reports consumed input");
    ZuCHECK(
      parser.process(rx) == Zws::H1::ClientParser::State::Headers);
    ZuCHECK(!parser.progressed(),
      "stalled H1 parser does not report progress");
    push(rx, "\r\n");
    ZuCHECK(
      parser.process(rx) == Zws::H1::ClientParser::State::Complete);
    ZuCHECK(parser.progressed());
  }

  {
    auto wsFrame = frame(Zws::Opcode::Text, "coalesced");
    Bytes coalesced;
    coalesced << responseWire.bufs[0]->cspan() << wsFrame;
    H1Rx rx;
    push(rx, coalesced);
    Zws::H1::ClientParser parser{key, "chat"};
    ZuCHECK(parser.process(rx) == Zws::H1::ClientParser::State::Complete);
    ZuCHECK(parser.valid());
    ZuCHECK(rx.span() == wsFrame,
      "opening parser preserves coalesced WebSocket bytes");
  }

  {
    static constexpr uint8_t wsFrame[] = {
      0x09, 0x09, 'f', 'r', 'a', 'g', 'm', 'e', 'n', 't', '1'
    };
    Bytes coalesced;
    coalesced << responseWire.bufs[0]->cspan() << ZuBSpan{wsFrame};
    H1Rx rx;
    push(rx, coalesced);
    Zws::H1::ClientParser parser{key, "chat"};
    ZuCHECK(parser.process(rx) == Zws::H1::ClientParser::State::Complete);
    ZuCHECK(parser.valid());
    ZuCHECK(rx.span() == ZuBSpan{wsFrame},
      "opening parser does not fold upgraded binary input");
  }

  H1Rx badRx;
  push(badRx,
    "HTTP/1.1 101 Switching Protocols\r\n"
    "Upgrade: websocket\r\nConnection: Upgrade\r\n"
    "Sec-WebSocket-Accept: bad\r\n\r\n");
  Zws::H1::ClientParser bad{key};
  ZuCHECK(bad.process(badRx) == Zws::H1::ClientParser::State::Complete);
  ZuCHECK(!bad.valid());

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
    ZuCHECK(!clientHandshake(input, key));

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
    ZuCHECK(!serverHandshake(input));
}

void header()
{
  ZuTestScope(header);

  Zws::HeaderParser parser;
  Zws::Frame parsed;
  Zws::Failure::T failure = Zws::Failure::None;
  const uint8_t raw[] = {0x82, 126, 0, 126};
  ZuBSpan first{raw, 1}, second{raw + 1, 3};
  ZuCHECK(parser.process(first, parsed, failure) ==
    Zws::HeaderParser::Result::Wait);
  ZuCHECK(parser.process(second, parsed, failure) ==
    Zws::HeaderParser::Result::Complete);
  ZuCHECK(parsed.final && parsed.opcode == Zws::Opcode::Binary &&
    parsed.length == 126 && !parsed.masked);

  const uint8_t nonCanonical[] = {0x82, 126, 0, 125};
  ZuBSpan bad{nonCanonical};
  failure = Zws::Failure::None;
  ZuCHECK(parser.process(bad, parsed, failure) ==
    Zws::HeaderParser::Result::Error);
  ZuCHECK(failure == Zws::Failure::NonCanonicalLength);

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
    ZuCHECK(parser.process(input, parsed, failure) ==
      Zws::HeaderParser::Result::Complete);
    ZuCHECK(parsed.length == test.length);
    ZuCHECK(wire.length() - input.length() == test.headerLength);
  }

  const uint8_t nonCanonical64[] = {
    0x82, 127, 0, 0, 0, 0, 0, 0, 0xff, 0xff
  };
  ZuBSpan bad64{nonCanonical64};
  failure = Zws::Failure::None;
  ZuCHECK(parser.process(bad64, parsed, failure) ==
    Zws::HeaderParser::Result::Error);
  ZuCHECK(failure == Zws::Failure::NonCanonicalLength);

  parser.reset();
  const uint8_t overflow[] = {
    0x82, 127, 0x80, 0, 0, 0, 0, 1, 0, 0
  };
  ZuBSpan badOverflow{overflow};
  failure = Zws::Failure::None;
  ZuCHECK(parser.process(badOverflow, parsed, failure) ==
    Zws::HeaderParser::Result::Error);
  ZuCHECK(failure == Zws::Failure::InvalidHeader);
}

void tx()
{
  ZuTestScope(tx);

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
  struct FailedRandom {
    bool random(uint8_t *) { return false; }
  };

  Ztls::Random random;
  ZuCHECK(random.init());
  {
    WireTx lower{20};
    auto tx = Zws::txLayer<false>(lower, Zws::Opcode::Text);
    tx << "0123456789";
    unsigned flushes = lower.flushCalls;
    tx.flush();
    ZuCHECK(lower.flushCalls == flushes + 1);
    tx.flush();
    ZuCHECK(lower.flushCalls == flushes + 1);
    ZuCHECK(lower.bufs.length() == 1);
    ZuCHECK(lower.bufs[0]->data()[0] == 0x81);
    ZuCHECK(lower.bufs[0]->data()[1] == 10);
    ZuCHECK(lower.bufs[0]->cspan().offset(2) == "0123456789");
    ZuCHECK(lower.finals.length() == 1 && lower.finals[0],
      "exact-capacity payload emitted one final frame");
  }
  {
    WireTx lower{20};
    auto tx = Zws::txLayer<false>(lower, Zws::Opcode::Binary);
    tx << "0123456789abcde";
    tx.flush();
    ZuCHECK(lower.bufs.length() == 2);
    ZuCHECK(lower.bufs[0]->data()[0] == 0x02);
    ZuCHECK(lower.bufs[1]->data()[0] == 0x80);
  }
  {
    WireTx lower;
    auto tx = Zws::txLayer<true>(
      lower, Zws::Opcode::Text, &random);
    tx << "hello";
    tx.flush();
    ZuCHECK(lower.bufs.length() == 1);
    auto wire = lower.bufs[0]->span();
    ZuCHECK(wire[0] == 0x81 && wire[1] == (0x80 | 5));
    uint32_t key =
      (uint32_t(wire[2])<<24) | (uint32_t(wire[3])<<16) |
      (uint32_t(wire[4])<<8) | wire[5];
    Zws::mask(wire.offset(6), key);
    ZuCHECK(wire == "hello");
  }
  {
    WireTx lower;
    auto tx = Zws::txLayer<false>(lower, Zws::Opcode::Text);
    tx.flush();
    ZuCHECK(lower.bufs.length() == 1);
    const uint8_t expected[] = {0x81, 0};
    ZuCHECK(lower.bufs[0]->cspan() == ZuBSpan(expected));
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
      ZuCHECK(lower.bufs.length() == 1);
      if (!lower.bufs.length()) continue;
      auto wire = lower.bufs[0]->cspan();
      Zws::HeaderParser parser;
      Zws::Frame parsed;
      Zws::Failure::T failure = Zws::Failure::None;
      ZuCHECK(parser.process(wire, parsed, failure) ==
	Zws::HeaderParser::Result::Complete);
      ZuCHECK(parsed.final && parsed.opcode == Zws::Opcode::Binary);
      ZuCHECK(parsed.length == length && wire.length() == length);
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
      ZuCHECK(lower.bufs.length() == 1);
      if (!lower.bufs.length()) continue;
      auto mutableWire = lower.bufs[0]->span();
      ZuBSpan wire = mutableWire;
      Zws::HeaderParser parser;
      Zws::Frame parsed;
      Zws::Failure::T failure = Zws::Failure::None;
      ZuCHECK(parser.process(wire, parsed, failure) ==
	Zws::HeaderParser::Result::Complete);
      ZuCHECK(parsed.final && parsed.masked &&
	parsed.opcode == Zws::Opcode::Binary &&
	parsed.length == length);
      mutableWire.offset(mutableWire.length() - wire.length());
      Zws::mask(mutableWire, parsed.key);
      ZuCHECK(mutableWire == payload,
	"masked Tx preserves payload across length boundary");
    }
  }
  {
    CounterRandom counter;
    WireTx lower{20};
    auto tx = Zws::txLayer<true>(
      lower, Zws::Opcode::Binary, &counter);
    tx << "0123456789abcde";
    tx.flush();
    ZuCHECK(lower.bufs.length() == 3);
    if (lower.bufs.length() == 3) {
      auto one = lower.bufs[0]->cspan();
      auto two = lower.bufs[1]->cspan();
      auto three = lower.bufs[2]->cspan();
      ZuCHECK(!(one[0] & 0x80) && (one[0] & 0xf) == Zws::Opcode::Binary);
      ZuCHECK(!(two[0] & 0x80) &&
	(two[0] & 0xf) == Zws::Opcode::Continuation);
      ZuCHECK((three[0] & 0x80) &&
	(three[0] & 0xf) == Zws::Opcode::Continuation);
      ZuCHECK((ZuBSpan{one.data() + 2, 4} !=
	ZuBSpan{two.data() + 2, 4}));
      ZuCHECK((ZuBSpan{two.data() + 2, 4} !=
	ZuBSpan{three.data() + 2, 4}));
    }
  }
  {
    WireTx lower{64, 3, 5};
    auto tx = Zws::txLayer<false>(lower, Zws::Opcode::Text);
    tx << "headroom";
    tx.flush();
    ZuCHECK(lower.bufs.length() == 1);
    if (lower.bufs.length()) {
      ZuCHECK(lower.bufs[0]->skip ==
	lower.headRoom() + (10 - 2));
      ZuCHECK(lower.bufs[0]->avail() >= lower.tailRoom());
      ZuCHECK(lower.bufs[0]->cspan().offset(2) == "headroom");
    }
  }
  {
    WireTx lower{20};
    auto tx = Zws::txLayer<false>(lower, Zws::Opcode::Ping);
    tx << "0123456789abcde";
    tx.flush();
    ZuCHECK(!tx.valid());
    ZuCHECK(!lower.bufs.length());
  }
  {
    FailedRandom failed;
    WireTx lower;
    auto tx = Zws::txLayer<true>(
      lower, Zws::Opcode::Binary, &failed);
    tx << "not-sent";
    tx.flush();
    ZuCHECK(!tx.valid());
    ZuCHECK(!lower.bufs.length());
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
  ZuTestCall(codec);
  return 0;
}
