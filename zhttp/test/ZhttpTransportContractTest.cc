//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// HTTP native transport trait and public configuration contract

#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZiRxStream.hh>
#include <zlib/ZiTxStream.hh>

#include <zlib/ZhttpClient.hh>
#include <zlib/ZhttpServer.hh>
#include <zlib/ZhttpTransport.hh>

using namespace ZuTestUtil;

namespace ZhttpTransportContractTest_ {

using TCP = Zhttp::Transport_::Traits<Zhttp::TCP>;
using TLS = Zhttp::Transport_::Traits<Zhttp::TLS>;
using QUIC = Zhttp::Transport_::Traits<Zhttp::QUIC>;

struct Rx { };
struct Tx { };

struct BodyTx : public ZiTxStream<BodyTx> {
  using Base = ZiTxStream<BodyTx>;

  BodyTx(unsigned, unsigned, unsigned);

  ZmRef<ZiIOBuf> allocBuf_(unsigned);
  bool sendBuf_(ZmRef<ZiIOBuf>, bool);
};

using BodyRx = Zhttp::BodyRx::Stream;

struct Frame {
  int64_t operator ()(ZuBSpan) const;
};
struct Data {
  void operator ()(ZuSpan<uint8_t>) const;
};

template <typename Stream, typename = void>
struct HasBodyTx : public ZuFalse { };
template <typename Stream>
struct HasBodyTx<Stream, decltype(
  ZuDeclVal<Stream &>() << ZuDeclVal<ZuBSpan>(),
  ZuDeclVal<Stream &>().flush(), void())> : public ZuTrue { };

template <typename Stream, typename = void>
struct HasFinalSend : public ZuFalse { };
template <typename Stream>
struct HasFinalSend<Stream, decltype(
  ZuDeclVal<Stream &>().sendBuf_(
    ZuDeclVal<ZmRef<ZiIOBuf>>(), ZuDeclVal<bool>()),
  void())> : public ZuTrue { };

template <typename Stream, typename = void>
struct HasBodyRx : public ZuFalse { };
template <typename Stream>
struct HasBodyRx<Stream, decltype(
  ZuDeclVal<Stream &>().consume(Frame{}, Data{}),
  ZuDeclVal<Stream &>().empty(),
  ZuDeclVal<Stream &>().length(),
  void())> : public ZuTrue { };

struct MuxTx {
  BodyTx body();
  BodyTx body(uint64_t);
};

template <int Version> struct LinkTx;
template <> struct LinkTx<Zhttp::Version::H1> {
  BodyTx &txStream();
};
template <> struct LinkTx<Zhttp::Version::H2> {
  MuxTx &txStream();
};
template <> struct LinkTx<Zhttp::Version::H3> {
  BodyTx &txStream();
};

template <typename Profile>
struct BodyLink : public LinkTx<Profile::HTTPVersion> {
  BodyRx rxBody();
};

template <typename Message>
struct BodyBuilder :
  public Zhttp::Builder,
  public Message::template Request<
    BodyBuilder<Message>, ZuTypeList<>, true, false> {
  using Base = typename Message::template Request<
    BodyBuilder, ZuTypeList<>, true, false>;
  using Headers = ZuTypeList<>;
  using Base::body;
  uint64_t contentLength() const { return 1; }
};

template <typename Link, typename Builder, typename = void>
struct HasLinkBody : public ZuFalse { };
template <typename Link, typename Builder>
struct HasLinkBody<Link, Builder, decltype(
  ZuDeclVal<Builder &>().body(ZuDeclVal<Link &>().txStream()),
  ZuDeclVal<Link &>().rxBody(), void())> : public ZuTrue { };

template <typename Profile>
using ProfileBuilder =
  BodyBuilder<Zhttp::MessageTraits<Profile>>;

struct SyntheticProfile {
  enum {
    HTTPVersion = Zhttp::Version::H2,
    Multiplexed = true
  };
};
struct SyntheticTraits {
  using Transport = void;
  enum {
    HTTPVersion = Zhttp::Version::H2,
    Multiplexed = true
  };
};
using SyntheticMessage =
  Zhttp::MessageTraits<SyntheticProfile, SyntheticTraits>;
using SyntheticBuilder = BodyBuilder<SyntheticMessage>;

struct ReqWithoutEvents : public Zhttp::ReqBuilder { };
struct ReqWithSelected : public Zhttp::ReqBuilder {
  void selected(
    const Zhttp::Endpoint *, uint64_t, uint64_t, unsigned, unsigned,
    Zhttp::Transport::T, Zhttp::Version::T);
};

ZuAssert(!Zhttp::HasReqSelected<ReqWithoutEvents>{});
ZuAssert(!Zhttp::HasReqCompleted<ReqWithoutEvents>{});
ZuAssert(Zhttp::HasReqSelected<ReqWithSelected>{});
ZuAssert(!Zhttp::HasReqCompleted<ReqWithSelected>{});

ZuAssert(HasBodyTx<BodyTx>{});
ZuAssert(HasFinalSend<BodyTx>{});
ZuAssert(HasBodyRx<BodyRx>{});
ZuAssert((HasLinkBody<
  BodyLink<Zhttp::H1TCP>, ProfileBuilder<Zhttp::H1TCP>>{}));
ZuAssert((HasLinkBody<
  BodyLink<Zhttp::H1TLS>, ProfileBuilder<Zhttp::H1TLS>>{}));
ZuAssert((HasLinkBody<
  BodyLink<Zhttp::H2TLS>, ProfileBuilder<Zhttp::H2TLS>>{}));
ZuAssert((HasLinkBody<
  BodyLink<Zhttp::H3QUIC>, ProfileBuilder<Zhttp::H3QUIC>>{}));
ZuAssert(SyntheticMessage::Multiplexed);
ZuAssert((HasLinkBody<
  BodyLink<SyntheticProfile>, SyntheticBuilder>{}));

template <typename Profile, typename = void>
struct HasProfileTraits : public ZuFalse { };
template <typename Profile>
struct HasProfileTraits<Profile,
  decltype(sizeof(Zhttp::ProfileTraits<Profile>), void())> : public ZuTrue { };

using Invalid =
  Zhttp::Profile<Zhttp::QUIC, Zhttp::Version::H1>;
ZuAssert(Zhttp::IsProfile<Zhttp::H1TCP>{});
ZuAssert(Zhttp::IsProfile<Zhttp::H1TLS>{});
ZuAssert(Zhttp::IsProfile<Zhttp::H2TLS>{});
ZuAssert(Zhttp::IsProfile<Zhttp::H3QUIC>{});
ZuAssert(!Zhttp::IsProfile<Invalid>{});
ZuAssert(HasProfileTraits<Zhttp::H1TCP>{});
ZuAssert(!HasProfileTraits<Invalid>{});

struct Link {
  Tx txStream() { return {}; }
  int process(Rx &) { return 0; }
  void connected(Zhttp::ConnectedInfo) { }
  void disconnected(bool) { }
};

ZuAssert(Zhttp::Transport_::HasTxStream<Link>{});
ZuAssert((Zhttp::Transport_::HasProcess<Link, Rx>{}));
ZuAssert((Zhttp::Transport_::HasConnected<
  Link, Zhttp::ConnectedInfo>{}));
ZuAssert(Zhttp::Transport_::HasDisconnected<Link>{});
using Contract =
  Zhttp::Transport_::LinkContract<Link, Rx, Zhttp::ConnectedInfo>;
ZuAssert(sizeof(Contract) == 1);

using TxBufAlloc =
  ZiIOBufAlloc<64, 256, "Zhttp.Contract.TxBuf">;

struct StreamLink {
  struct Tx : public ZiTxStream<Tx> {
    using Base = ZiTxStream<Tx>;

    Tx(StreamLink &link_) : Base{64, 0, 0}, link{&link_} { }
    Tx(Tx &&) = default;
    Tx &operator =(Tx &&) = default;

    ZmRef<ZiIOBuf> allocBuf_(unsigned headRoom) {
      ZmRef<ZiIOBuf> buf = new TxBufAlloc{};
      buf->skip = headRoom;
      buf->length = 0;
      return buf;
    }
    bool sendBuf_(ZmRef<ZiIOBuf> buf, bool) {
      ++link->handoffs;
      link->wire << buf->cspan();
      return true;
    }

    StreamLink	*link;
  };

  bool streamLocalCap() const { return localCap; }
  bool streamPeerCap() const { return peerCap; }
  template <typename L>
  void streamTx(L &&l) {
    Tx tx{*this};
    ZuFwd<L>(l)(tx);
  }
  void streamTxEnd() { ++ends; }
  void streamTxReset() { ++resets; }

  ZtString<ZtStringHeapID<"Zhttp.Contract.StreamWire">> wire;
  unsigned	handoffs = 0;
  unsigned	ends = 0;
  unsigned	resets = 0;
  bool		localCap = true;
  bool		peerCap = false;
};

struct StreamConsumer {
  template <typename Stream, typename Rx>
  int process(Stream, Rx &rx) {
    ++calls;
    Zhttp::bodyEach(rx,
      [this](ZuBSpan span) { data << span; });
    return result;
  }
  template <typename Stream>
  void peerEnd(Stream) { ++peerEnds; }
  template <typename Stream>
  void error(Stream) { ++errors; }

  ZtString<ZtStringHeapID<"Zhttp.Contract.StreamData">> data;
  unsigned	calls = 0;
  unsigned	peerEnds = 0;
  unsigned	errors = 0;
  int		result = 1;
};

using LogicalStream = Zhttp::Stream<StreamLink>;
using Dispatch = Zhttp::StreamDispatch<StreamLink, StreamConsumer>;
ZuAssert(sizeof(LogicalStream) == sizeof(void *));
ZuAssert(sizeof(Dispatch) == sizeof(void *) * 2);

struct TxLink {
  struct Stream : public ZiTxStream<Stream> {
    using Base = ZiTxStream<Stream>;

    Stream(TxLink &link_) : Base{64, 0, 0}, link{&link_} { }
    Stream(Stream &&) = default;
    Stream &operator =(Stream &&) = default;

    ZmRef<ZiIOBuf> allocBuf_(unsigned headRoom) {
      ZmRef<ZiIOBuf> buf = new TxBufAlloc{};
      buf->skip = headRoom;
      buf->length = 0;
      return buf;
    }
    bool sendBuf_(ZmRef<ZiIOBuf> buf, bool) {
      link->wire << buf->cspan();
      return true;
    }

    TxLink	*link;
  };

  auto transmit(auto &) { return Stream{*this}; }
  void finish() { ++finishes; }

  ZtString<ZtStringHeapID<"Zhttp.Contract.Wire">> wire;
  unsigned	finishes = 0;
};

struct H2Native {
  using Tx = TxLink::Stream;

  Tx txStream() { return Tx{link}; }
  unsigned dataMaxSize(uint32_t) const { return 64; }
  bool peerExtendedConnect() const { return false; }
  bool localExtendedConnect() const { return false; }
  void sendHeaders(
    uint32_t, Zhttp::H2_::HeaderFrames frames, bool) {
    ++headers;
    headerFrames += frames.length();
  }
  bool sendFrame(uint32_t, ZmRef<ZiIOBuf>) {
    ++headerFrames;
    return true;
  }
  bool sendData(uint32_t, ZmRef<ZiIOBuf>) {
    ++data;
    return true;
  }
  void endData(uint32_t) { ++ends; }

  TxLink	link;
  unsigned headers = 0;
  unsigned headerFrames = 0;
  unsigned data = 0;
  unsigned ends = 0;
};

struct CustomValue {
  template <typename S>
  void print(S &s) const { s << "custom"; }

  friend ZuPrintFn ZuPrintType(CustomValue *);
};

struct CustomTarget {
  template <typename S>
  void print(S &s) const { s << "/printable?"; }

  friend ZuPrintFn ZuPrintType(CustomTarget *);
};

struct QueryPath {
  template <typename S>
  void print(S &s) const { s << "/printable"; }

  friend ZuPrintFn ZuPrintType(QueryPath *);
};

using TxHeaders = ZuTypeList<ZuStringT<"x-custom">, void>;
using ContentLength = ZuStringT<"content-length">;
using BodySize = ZuStringT<"x-body-size">;
using FixedHeaders = ZuTypeList<ContentLength, void, BodySize, void>;

struct TxBuilder :
  public Zhttp::Builder,
  public Zhttp::H1::Request<
    TxBuilder, TxHeaders, true, true> {
  using Headers = TxHeaders;
  using Base = Zhttp::H1::Request<TxBuilder, Headers, true, true>;
  using Base::body;
  template <typename L>
  void operation(L &&l) {
    l(Zhttp::Method::POST, [this](auto &&emit) {
      emit([this](auto &tx) { tx << QueryPath{} << '?' << query; });
    });
  }
  template <typename L>
  void host(L &&l) { l("localhost"); }
  template <typename Key, typename L>
  void header(L &&l) { l(CustomValue{}); }
  template <typename L>
  void header(L &&l) { l("x-runtime", CustomValue{}); }

  ZuCSpan query = "q=test&page=2";
};

struct ResponseTxBuilder :
  public Zhttp::Builder,
  public Zhttp::H1::Response<ResponseTxBuilder> {
  using Headers = ZuTypeList<>;

  unsigned status() const { return 204; }
};

struct FixedTxBuilder :
  public Zhttp::Builder,
  public Zhttp::H1::Request<
    FixedTxBuilder, FixedHeaders, true, false> {
  using Headers = FixedHeaders;
  using Base = Zhttp::H1::Request<FixedTxBuilder, Headers, true, false>;
  using Base::body;
  template <typename L>
  void operation(L &&l) {
    l(Zhttp::Method::PUT, [](auto &&emit) {
      emit([](auto &tx) { tx << "/fixed-edge"; });
    });
  }
  template <typename L>
  void host(L &&l) { l("localhost"); }
  template <typename Key, typename L>
  void header(L &&l) {
    app.template header<Key>(ZuFwd<L>(l));
  }
  template <typename L> void header(L &&) { }
  template <typename Key>
  void headerOffset(uint64_t offset, unsigned length) {
    spans.template recordOffset<Key>(offset, length);
  }
  void headerBase(uint8_t *base) { spans.resolve(base); }
  void patch() { spans.patch(app); }

  struct App {
    template <typename Key, typename L>
    void header(L &&l) {
      ++providers;
      if constexpr (ZuIsSame<Key, ContentLength>{})
	l(Zhttp::Placeholder{10, '0'});
      else
	l(CustomValue{});
    }
    template <typename L>
    void bodyHdrs(L &&l) {
      l.template operator()<ContentLength>(
	[this](ZuSpan<uint8_t> span) {
	  contentSpan = span;
	  ZuStream out{span};
	  out << ZuBoxed(contentLength).fmt<ZuFmt::Right<10>>();
	});
      l.template operator()<BodySize>(
	[this](ZuSpan<uint8_t> span) {
	  bodySpan = span;
	  memcpy(span.data(), "000200", 6);
	});
    }

    ZuSpan<uint8_t> contentSpan;
    ZuSpan<uint8_t> bodySpan;
    uint64_t contentLength = 0;
    unsigned providers = 0;
  } app;

  Zhttp::HeaderSpans<FixedHeaders> spans;
};

template <typename T, typename = void>
struct HasBegin : public ZuFalse { };
template <typename T>
struct HasBegin<T, decltype(
  ZuDeclVal<T &>().begin(ZuDeclVal<TxLink::Stream &>()), void())> :
  public ZuTrue { };

template <typename T, typename = void>
struct HasReset : public ZuFalse { };
template <typename T>
struct HasReset<T, decltype(ZuDeclVal<T &>().reset(), void())> :
  public ZuTrue { };

using ReqFacade = Zhttp::H1::Request<
  TxBuilder, TxHeaders, true, true>;
using RespFacade = Zhttp::H1::Response<
  TxBuilder, TxHeaders, true, true>;
ZuAssert(HasBegin<ReqFacade>{});
ZuAssert(HasBegin<RespFacade>{});
ZuAssert(HasReset<Zhttp::Parser>{});
ZuAssert(!HasReset<Zhttp::Builder>{});
ZuAssert(!HasReset<ReqFacade>{});
ZuAssert(!HasReset<RespFacade>{});

void testBodyTx()
{
  ZuTestScope(testBodyTx);
  TxLink link;
  TxBuilder builder;
  auto tx = link.transmit(builder);
  ZuCHECK(builder.begin(tx), "request header construction failed");
  auto body = builder.body(tx);
  body << "abcdefgh";
  body.flush();
  builder.finish(tx);
  link.finish();
  ZuCHECK(body.produced() == 8 && link.finishes == 1,
    "synchronous body accounting mismatch");
  ZuCHECK(link.wire.find(
	"POST /printable?q=test&page=2 HTTP/1.1\r\n") >= 0,
    "request line mismatch");
  ZuCHECK(link.wire.find("transfer-encoding: chunked\r\n") >= 0,
    "streaming request is not chunked");
  ZuCHECK(link.wire.find("x-custom: custom\r\n") >= 0,
    "typed custom header mismatch");
  ZuCHECK(link.wire.find("x-runtime: custom\r\n") >= 0,
    "runtime custom header mismatch");
  ZuCHECK(link.wire.find("abcdefgh") >= 0,
    "body payload mismatch");
  ZuCHECK(link.wire.find("0\r\n") >= 0,
    "chunk terminator mismatch");
}

void testResponseStatusLine()
{
  ZuTestScope(testResponseStatusLine);
  TxLink link;
  ResponseTxBuilder builder;
  auto tx = link.transmit(builder);
  builder.begin(tx);
  builder.finish(tx);
  link.finish();
  ZuCHECK(link.wire.find("HTTP/1.1 204 \r\n\r\n") >= 0,
    "response status line contains a reason phrase");
}

void testFixedPatch()
{
  ZuTestScope(testFixedPatch);
  TxLink link;
  FixedTxBuilder builder;
  auto native = link.transmit(builder);
  Zhttp::RetainedBudget budget{.max = 4096};
  Zhttp::RetainedTx headerTx{native, budget};
  Zhttp::RetainedTx bodyTx{native, budget};

  auto body = builder.body(bodyTx, 200);
  for (unsigned i = 0; i < 200; ++i) body << 'x';
  body.flush();
  ZuCHECK(body.valid() && body.produced() == 200,
    "fixed body accounting mismatch");

  builder.app.contentLength = body.produced();
  ZuCHECK(builder.begin(headerTx), "fixed request rendering failed");
  builder.finish(bodyTx);
  ZuCHECK(headerTx.seal() && bodyTx.seal(),
    "retained fixed message sealing failed");
  ZuCHECK(!link.wire && builder.app.providers == 2 &&
      builder.app.contentSpan.length() == 10 &&
      builder.app.bodySpan.length() == 6,
    "fixed message escaped before one-pass in-place patching");
  headerTx.commit();
  bodyTx.commit();
  link.finish();
  ZuCHECK(link.wire.find("content-length: 0000000200\r\n") >= 0,
    "fixed-width H1 content-length mismatch");
  ZuCHECK(link.wire.find("x-body-size: 000200\r\n") >= 0,
    "ordinary printable H1 patch mismatch");
  ZuCSpan rest{link.wire};
  int first = rest.find("content-length:");
  rest.offset(unsigned(first + 1));
  ZuCHECK(first >= 0 && rest.find("content-length:") < 0,
    "fixed H1 field was rendered more than once");
}

void testH2DeferredState()
{
  ZuTestScope(testH2DeferredState);
  Zhttp::H2::HPackEncoder encoder;
  ZuCHECK(encoder.init(4096), "HPACK encoder initialization failed");
  Zhttp::HPackSeedPlans seeds;
  ZuCHECK(encoder.bind(seeds), "empty HPACK warm-plan binding failed");
  uint64_t before = encoder.table().insertCount();
  {
    H2Native native;
    Zhttp::H2_::HeaderBlock block{native, encoder, 1, 64};
    Zhttp::H2::HPackBytes bytes;
    decltype(block)::HeaderSection section{bytes};
    block.defer(1);
    block.beginHeaders(section, true);
    block.field("x-deferred", "discarded");
    block.endHeaders(true);
    ZuCHECK(!block.valid() && !native.headers,
      "oversized deferred H2 headers became visible");
  }
  ZuCHECK(encoder.table().insertCount() == before,
    "discarded H2 message mutated HPACK state");
  {
    H2Native native;
    Zhttp::H2_::HeaderBlock block{native, encoder, 3, 64};
    Zhttp::H2::HPackBytes bytes;
    decltype(block)::HeaderSection section{bytes};
    block.defer(4096);
    block.beginHeaders(section, true);
    block.field(":path", CustomTarget{});
    block.field("x-deferred", "committed");
    block.endHeaders(true);
    ZuCHECK(block.valid() && !native.headers,
      "deferred H2 headers escaped before commit");
    block.commit();
    ZuCHECK(native.headers == 1 && native.headerFrames,
      "deferred H2 headers did not commit");
  }
  ZuCHECK(encoder.table().insertCount() == before,
    "committed H2 message mutated frozen HPACK state");
  encoder.final();
}

void testHeaderAndRetainedLimits()
{
  ZuTestScope(testHeaderAndRetainedLimits);
  ZuCHECK(!Zhttp::validRuntimeHeader<FixedHeaders>(
      "CONTENT-LENGTH", false) &&
      !Zhttp::validRuntimeHeader<FixedHeaders>(
      "Content-Length", true) &&
      !Zhttp::validRuntimeHeader<ZuTypeList<>>(
      "Transfer-Encoding", true),
    "runtime framing-header validation is not case-insensitive");

  TxLink link;
  FixedTxBuilder builder;
  auto native = link.transmit(builder);
  Zhttp::RetainedBudget budget{.max = 4096};
  Zhttp::RetainedTx headerTx{native, budget};
  Zhttp::RetainedTx bodyTx{native, budget};
  auto body = builder.body(bodyTx, 5);
  body << "123456";
  body.flush();
  ZuCHECK(!body.valid() && body.produced() == 5 && !link.wire,
    "fixed entity crossed its configured cap or became visible");

  TxLink messageLink;
  FixedTxBuilder messageBuilder;
  auto messageNative = messageLink.transmit(messageBuilder);
  Zhttp::RetainedBudget messageBudget{.max = 8};
  Zhttp::RetainedTx messageTx{messageNative, messageBudget};
  ZuCHECK(messageBuilder.begin(messageTx) && !messageTx.seal() &&
      !messageLink.wire,
    "retained-message cap exposed a partial header block");
}

void testTraits()
{
  ZuTestScope(testTraits);

  ZuCHECK(TCP::ID == Zhttp::Transport::TCP &&
      !TCP::Secure && !TCP::Datagram,
    "TCP trait identity mismatch");
  ZuCHECK(TLS::ID == Zhttp::Transport::TLS &&
      TLS::Secure && !TLS::Datagram,
    "TLS trait identity mismatch");
  ZuCHECK(QUIC::ID == Zhttp::Transport::QUIC &&
      QUIC::Secure && QUIC::Datagram,
    "QUIC trait identity mismatch");

  using H1TCP = Zhttp::MessageTraits<Zhttp::H1TCP>;
  using H1TLS = Zhttp::MessageTraits<Zhttp::H1TLS>;
  using H2TLS = Zhttp::MessageTraits<Zhttp::H2TLS>;
  using H3QUIC = Zhttp::MessageTraits<Zhttp::H3QUIC>;
  ZuCHECK(H1TCP::ID == Zhttp::Version::H1 &&
      H1TCP::Transport::ID == Zhttp::Transport::TCP &&
      !H1TCP::Multiplexed && !H1TCP::OneMessagePerLink &&
      H1TCP::CloseDelimited,
    "H1/TCP profile mismatch");
  ZuCHECK(H1TLS::ID == Zhttp::Version::H1 &&
      H1TLS::Transport::ID == Zhttp::Transport::TLS &&
      !H1TLS::Multiplexed && !H1TLS::OneMessagePerLink &&
      H1TLS::CloseDelimited,
    "H1/TLS profile mismatch");
  ZuCHECK(H2TLS::ID == Zhttp::Version::H2 &&
      H2TLS::Transport::ID == Zhttp::Transport::TLS &&
      H2TLS::Multiplexed && H2TLS::OneMessagePerLink &&
      !H2TLS::CloseDelimited,
    "H2/TLS metadata placeholder mismatch");
  ZuCHECK(H3QUIC::ID == Zhttp::Version::H3 &&
      H3QUIC::Transport::ID == Zhttp::Transport::QUIC &&
      H3QUIC::Multiplexed && H3QUIC::OneMessagePerLink &&
      !H3QUIC::CloseDelimited,
    "H3/QUIC profile mismatch");
}

void testParams()
{
  ZuTestScope(testParams);

  Zhttp::HubConfig hub{nullptr, "rx", "tx"};
  auto tcpCli = TCP::clientParams(hub, Zhttp::TCPConfig{});
  auto tcpSrv = TCP::serverParams(hub, Zhttp::TCPConfig{});
  ZuCHECK(tcpCli.rxThread == "rx" && tcpCli.txThread == "tx" &&
      tcpSrv.rxThread == "rx" && tcpSrv.txThread == "tx",
    "TCP common parameter mapping mismatch");

  auto tlsCli = TLS::clientParams(
    hub, Zhttp::TLSConfig{}.caPath("ca.pem"));
  auto tlsSrv = TLS::serverParams(
    hub, Zhttp::TLSConfig{}
      .certPath("cert.pem").keyPath("key.pem"));
  ZuCHECK(tlsCli.caPath() == "ca.pem" &&
      tlsCli.alpn().length() == 1 &&
      tlsCli.alpn()[0] == "http/1.1",
    "TLS client defaults mismatch");
  ZuCHECK(tlsSrv.certPath() == "cert.pem" &&
      tlsSrv.keyPath() == "key.pem" &&
      tlsSrv.alpn().length() == 1 &&
      tlsSrv.alpn()[0] == "http/1.1",
    "TLS server defaults mismatch");

  auto force = Zhttp::H2Config().policy(Zhttp::H2Policy::Force);
  auto prefer = Zhttp::H2Config().policy(Zhttp::H2Policy::Prefer);
  auto disable = Zhttp::H2Config().policy(Zhttp::H2Policy::Disable);
  auto forceCli = Zhttp::TLS_::clientParams(hub, force);
  auto preferSrv = Zhttp::TLS_::serverParams(hub, prefer);
  auto disableCli = Zhttp::TLS_::clientParams(hub, disable);
  ZuCHECK(forceCli.alpn().length() == 1 &&
      forceCli.alpn()[0] == "h2",
    "force-H2 ALPN mismatch");
  ZuCHECK(preferSrv.alpn().length() == 2 &&
      preferSrv.alpn()[0] == "h2" &&
      preferSrv.alpn()[1] == "http/1.1",
    "prefer-H2 ALPN mismatch");
  ZuCHECK(disableCli.alpn().length() == 1 &&
      disableCli.alpn()[0] == "http/1.1",
    "disable-H2 ALPN mismatch");
  ZuCHECK(Zhttp::TLS_::valid(force) &&
      !Zhttp::TLS_::valid(
	Zhttp::H2Config{}.maxFrameSize(
	  Zhttp::H2::DefltFrameSize - 1)) &&
      !Zhttp::TLS_::valid(Zhttp::H2Config{}.maxPending(0)) &&
      !Zhttp::TLS_::valid(Zhttp::H2Config{}.maxStreamID(2)) &&
      !Zhttp::TLS_::valid(Zhttp::H2Config{}.hpackTxCapacity(
	Zhttp::H2Config::MaxHPackCapacity + 1)),
    "shared TLS/H2 validation mismatch");
  auto hpack = Zhttp::H2Config{}.
    hpackRxCapacity(4096).hpackTxCapacity(8192);
  ZuCHECK(hpack.hpackRxCapacity() == 4096 &&
      hpack.hpackTxCapacity() == 8192,
    "H2 HPACK local limits mismatch");
  ZuCHECK(
    Zhttp::TLS_::version("h2", Zhttp::H2Policy::Force) ==
      Zhttp::Version::H2 &&
    Zhttp::TLS_::version("h2", Zhttp::H2Policy::Prefer) ==
      Zhttp::Version::H2 &&
    Zhttp::TLS_::version("http/1.1", Zhttp::H2Policy::Prefer) ==
      Zhttp::Version::H1 &&
    Zhttp::TLS_::version("http/1.1", Zhttp::H2Policy::Disable) ==
      Zhttp::Version::H1 &&
    Zhttp::TLS_::version("http/1.1", Zhttp::H2Policy::Force) < 0 &&
    Zhttp::TLS_::version("h2", Zhttp::H2Policy::Disable) < 0 &&
    Zhttp::TLS_::version({}, Zhttp::H2Policy::Prefer) < 0,
    "shared TLS negotiated-profile classification mismatch");

  auto quicCli = QUIC::clientParams(hub, Zhttp::QUICConfig{});
  auto quicSrv = QUIC::serverParams(hub, Zhttp::QUICConfig{});
  ZuCHECK(quicCli.alpn().length() == 1 && quicCli.alpn()[0] == "h3" &&
      quicSrv.alpn().length() == 1 && quicSrv.alpn()[0] == "h3",
    "H3 ALPN defaults mismatch");
  ZuCHECK(
    quicCli.maxData() == Zhttp::QUICConfig::DefltMaxData &&
    quicCli.maxStreamData() == Zhttp::QUICConfig::DefltMaxStreamData &&
    quicCli.maxStreamsDuplex() ==
      Zhttp::QUICConfig::DefltClientStreams &&
    quicSrv.maxStreamsDuplex() ==
      Zhttp::QUICConfig::DefltServerStreams &&
    quicCli.maxStreamsSimplex() ==
      Zhttp::QUICConfig::DefltControlStreams,
    "H3 transport defaults mismatch");
  auto qpack = Zhttp::QUICConfig{}.
    qpackRxCapacity(4096).qpackTxCapacity(8192).
    qpackRxBlocked(16).qpackTxSections(32);
  ZuCHECK(qpack.qpackValid() &&
      qpack.qpackRxCapacity() == 4096 &&
      qpack.qpackTxCapacity() == 8192 &&
      qpack.qpackRxBlocked() == 16 &&
      qpack.qpackTxSections() == 32 &&
      !Zhttp::QUICConfig{}.
	qpackTxCapacity(Zhttp::QUICConfig::MaxQPackCapacity + 1).
	qpackValid(),
    "H3 QPACK local-limit validation mismatch");

  auto serverQUIC = Zhttp::ServerConfig{}.idleTimeout(7)
    .quic(Zhttp::QUICConfig{}).quicHubConfig();
  auto explicitQUIC = Zhttp::ServerConfig{}.idleTimeout(7)
    .quic(Zhttp::QUICConfig{}.maxIdleTimeout(2500)).quicHubConfig();
  ZuCHECK(serverQUIC.maxIdleTimeout() == 7000 &&
      explicitQUIC.maxIdleTimeout() == 2500,
    "server idle timeout mapping mismatch");

  Zhttp::DiscoveryLimits limits{
    .maxRecords = 3, .maxHints = 5,
    .maxEndpoints = 7, .maxAliasDepth = 2};
  auto client = Zhttp::Config{}
    .requestTimeout(13).maxAltSvc(11)
    .retainedBodyMax(17).retainedMessageMax(23)
    .discoveryLimits(limits).altSvcCrossHost(true)
    .h2Policy(Zhttp::H2Policy::Disable);
  ZuCHECK(client.requestTimeout() == 13 &&
      client.maxAltSvc() == 11 && client.altSvcCrossHost() &&
      client.retainedBodyMax() == 17 &&
      client.retainedMessageMax() == 23 &&
      client.h2Policy() == Zhttp::H2Policy::Disable &&
      client.discoveryLimits().maxRecords == 3 &&
      client.discoveryLimits().maxHints == 5 &&
      client.discoveryLimits().maxEndpoints == 7 &&
      client.discoveryLimits().maxAliasDepth == 2,
    "client discovery bounds mapping mismatch");
  ZuCHECK(
    Zhttp::ServerConfig{}.tlsConfig().policy() ==
      Zhttp::H2Policy::Prefer &&
    Zhttp::ServerConfig{}
      .tls(Zhttp::H2Config{}.policy(Zhttp::H2Policy::Force))
      .tlsConfig().policy() == Zhttp::H2Policy::Force,
    "server H2 policy mapping mismatch");
  auto server = Zhttp::ServerConfig{}
    .retainedBodyMax(29).retainedMessageMax(31);
  ZuCHECK(server.retainedBodyMax() == 29 &&
      server.retainedMessageMax() == 31,
    "server retained-message bounds mapping mismatch");
}

void testMetadata()
{
  ZuTestScope(testMetadata);

  auto tcp = Zhttp::ProfileTraits<Zhttp::H1TCP>::connected(
    Ztcp::Connected{});
  auto tls = Zhttp::ProfileTraits<Zhttp::H1TLS>::connected(
    Ztls::Connected{"http/1.1", 0x304});
  auto h2 = Zhttp::ProfileTraits<Zhttp::H2TLS>::connected(
    Ztls::Connected{"h2", 0x304});
  auto quic = Zhttp::ProfileTraits<Zhttp::H3QUIC>::connected(
    Zquic::Connected{"h3", 1});
  ZuCHECK(tcp.transport == Zhttp::Transport::TCP &&
      !tcp.secure && tcp.httpVersion == Zhttp::Version::H1,
    "TCP connected metadata mismatch");
  ZuCHECK(tls.transport == Zhttp::Transport::TLS &&
      tls.secure && !tls.multiplexed && tls.alpn == "http/1.1",
    "TLS connected metadata mismatch");
  ZuCHECK(h2.transport == Zhttp::Transport::TLS &&
      h2.secure && h2.multiplexed &&
      h2.httpVersion == Zhttp::Version::H2 && h2.alpn == "h2",
    "H2 connected metadata mismatch");
  ZuCHECK(quic.transport == Zhttp::Transport::QUIC &&
      quic.secure && quic.multiplexed &&
      quic.httpVersion == Zhttp::Version::H3 && quic.alpn == "h3",
    "QUIC connected metadata mismatch");
  ZuCHECK(
    Zhttp::Transport{}.name(Zhttp::Transport::QUIC) == "QUIC" &&
    Zhttp::Version{}.name(Zhttp::Version::H3) == "H3" &&
    Zhttp::Migration{}.name(Zhttp::Migration::Active) == "Active" &&
    Zhttp::ProtocolPolicy{}.name(Zhttp::ProtocolPolicy::PreferH3) ==
      "PreferH3" &&
    Zhttp::H2Policy{}.name(Zhttp::H2Policy::Disable) == "Disable" &&
    Zhttp::EndpointSource{}.name(Zhttp::EndpointSource::AltSvc) == "AltSvc" &&
    Zhttp::ResultCode{}.name(Zhttp::ResultCode::TimedOut) == "TimedOut",
    "configuration enum names mismatch");
  ZuCHECK(
    Zhttp::migrationMode("disabled") == Zhttp::Migration::Disabled &&
    Zhttp::migrationMode("passive") == Zhttp::Migration::Passive &&
    Zhttp::migrationMode("active") == Zhttp::Migration::Active,
    "migration configuration vocabulary mismatch");
}

void testBodyRx()
{
  ZuTestScope(testBodyRx);

  auto buf = [](ZuCSpan value) {
    ZmRef<Zhttp::BodyRx::Queue::Node> buf = new Zhttp::BodyRx::BufAlloc{};
    auto io = static_cast<ZiIOBuf *>(buf.ptr());
    memcpy(io->data(), value.data(), value.length());
    buf->length = value.length();
    return buf;
  };

  Zhttp::BodyRx body{5};
  unsigned calls = 0;
  unsigned remaining = 5;
  ZtString<> gathered;
  auto consume = [&calls, &remaining, &gathered](auto &rx) {
    ++calls;
    (void)rx.consume(
      [&remaining](ZuBSpan span) -> int64_t {
	if (remaining > span.length()) {
	  remaining -= span.length();
	  return 0;
	}
	return remaining;
      },
      [&gathered](ZuBSpan span) { gathered << span; });
  };

  ZuCHECK(body.push(buf("abc"), consume), "first body append failed");
  ZuCHECK(calls == 1 && body.rx().length() == 3 &&
      body.received() == 3 && !body.consumed(),
    "incomplete application frame was not retained");
  remaining = 5;
  ZuCHECK(body.push(buf("de"), consume), "second body append failed");
  ZuCHECK(calls == 2 && !body.rx() && body.received() == 5 &&
      body.consumed() == 5 && gathered == "abcde",
    "completed application frame was not consumed across appends");

  unsigned rejectedCalls = 0;
  ZuCHECK(!body.push(buf("x"), [&rejectedCalls](auto &) {
      ++rejectedCalls;
    }) && !rejectedCalls && !body.rx(),
    "body maximum did not reject before queue mutation");

  body.reset(10);
  ZuCHECK(body.push(buf("xyz"), [&calls](auto &) { ++calls; }) &&
      body.rx().length() == 3 && body.discard() == 3 && !body.rx() &&
      body.received() == 3 && body.consumed() == 3,
    "body discard accounting mismatch");

  body.reset();
  body.push(buf("ab"), [](auto &) { });
  body.push(buf("cd"), [](auto &) { });
  ZtString<> eager;
  ZuCHECK(Zhttp::bodyEach(body.rx(),
      [&eager](ZuBSpan span) { eager << span; }) &&
      eager == "abcd" && !body.rx(),
    "bodyEach did not eagerly consume queued spans in order");
  unsigned emptyCalls = 0;
  ZuCHECK(Zhttp::bodyEach(body.rx(), [&emptyCalls](ZuBSpan) {
      ++emptyCalls;
    }) && !emptyCalls,
    "bodyEach did not return immediately for an empty queue");
}

void testStream()
{
  ZuTestScope(testStream);

  StreamLink link;
  Zhttp::Stream stream{link};
  ZuCHECK(stream.localCap() && !stream.peerCap(),
    "logical-stream capabilities mismatch");
  stream.txStream([](auto &tx) {
    tx << ZuCSpan{"a"};
    tx.flush();
    tx << ZuCSpan{"b"} << Zi::flush();
    tx << ZuCSpan{"c"};
  });
  ZuCHECK(link.handoffs == 3 && link.wire == "abc",
    "explicit and residual logical-stream flushes mismatch");
  stream.end();
  stream.reset();
  ZuCHECK(link.ends == 1 && link.resets == 1,
    "logical-stream Tx terminal forwarding mismatch");

  auto buf = [](ZuCSpan value) {
    ZmRef<Zhttp::BodyRx::Queue::Node> buf = new Zhttp::BodyRx::BufAlloc{};
    auto io = static_cast<ZiIOBuf *>(buf.ptr());
    memcpy(io->data(), value.data(), value.length());
    buf->length = value.length();
    return buf;
  };
  StreamConsumer consumer;
  Dispatch dispatch;
  Zhttp::BodyRx body;
  dispatch.init(link, consumer);
  ZuCHECK(body.push(buf("ef"),
      [&dispatch](auto &rx) { dispatch.process(rx); }) &&
      consumer.calls == 1 && consumer.data == "ef",
    "stream dispatch did not consume populated input");

  consumer.result = -1;
  ZuCHECK(body.push(buf("x"),
      [&dispatch](auto &rx) { dispatch.process(rx); }) &&
      consumer.calls == 2 && link.resets == 2,
    "negative stream result did not reset the logical stream");

  dispatch.disable_();
  unsigned calls = consumer.calls;
  ZuCHECK(body.push(buf("y"),
      [&dispatch](auto &rx) { dispatch.process(rx); }) &&
      consumer.calls == calls && body.rx().length() == 1,
    "disabled stream dispatch consumed input");
  body.discard();
  dispatch.final_();

  Dispatch ended;
  ended.init(link, consumer);
  ended.peerEnd();
  ended.final_();
  Dispatch failed;
  failed.init(link, consumer);
  failed.error();
  failed.final_();
  ZuCHECK(consumer.peerEnds == 1 && consumer.errors == 1,
    "logical-stream terminals were not dispatched explicitly");
}

} // namespace ZhttpTransportContractTest_

int main(int argc, char **argv)
{
  using namespace ZhttpTransportContractTest_;

  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testTraits);
  ZuTestCall(testParams);
  ZuTestCall(testMetadata);
  ZuTestCall(testBodyTx);
  ZuTestCall(testResponseStatusLine);
  ZuTestCall(testFixedPatch);
  ZuTestCall(testH2DeferredState);
  ZuTestCall(testHeaderAndRetainedLimits);
  ZuTestCall(testBodyRx);
  ZuTestCall(testStream);
  return 0;
}
