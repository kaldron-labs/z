//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// HTTP native transport trait and public configuration contract

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZiRxStream.hh>
#include <zlib/ZiTxStream.hh>

#include <zlib/ZhttpService.hh>
#include <zlib/ZhttpStream.hh>

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
  void sendBuf_(ZmRef<ZiIOBuf>, bool);
};

struct BodyRxOwner {
  Zi::RxRefill rxRefill_();
  ZuSpan<uint8_t> rxSpan_();
  unsigned rxAdvance_(unsigned);
  void rxCancel_();
};
using BodyRx = ZiRxLayer<BodyRxOwner>;

struct ErrorRxOwner {
  bool	error = true;

  Zi::RxRefill rxRefill_() {
    if (error) {
      error = false;
      return {0, Zi::RxRefill::Error};
    }
    return {};
  }
  ZuBSpan rxSpan_() { return {}; }
  unsigned rxAdvance_(unsigned) { return 0; }
  void rxCancel_() { }
};

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
  ZuDeclVal<Stream &>().input(),
  ZuDeclVal<Stream &>().available(),
  ZuDeclVal<Stream &>().consume(Frame{}, Data{}),
  ZuDeclVal<Stream &>().complete(),
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
  public Message::template Builder<
    BodyBuilder<Message>, ZuTypeList<>, ZuTypeList<>, true, false> {
  using Base = typename Message::template Builder<
    BodyBuilder, ZuTypeList<>, ZuTypeList<>, true, false>;
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

static_assert(HasBodyTx<BodyTx>{});
static_assert(HasFinalSend<BodyTx>{});
static_assert(HasBodyRx<BodyRx>{});
static_assert(HasLinkBody<
  BodyLink<Zhttp::H1TCP>, ProfileBuilder<Zhttp::H1TCP>>{});
static_assert(HasLinkBody<
  BodyLink<Zhttp::H1TLS>, ProfileBuilder<Zhttp::H1TLS>>{});
static_assert(HasLinkBody<
  BodyLink<Zhttp::H2TLS>, ProfileBuilder<Zhttp::H2TLS>>{});
static_assert(HasLinkBody<
  BodyLink<Zhttp::H3QUIC>, ProfileBuilder<Zhttp::H3QUIC>>{});
static_assert(SyntheticMessage::Multiplexed);
static_assert(HasLinkBody<
  BodyLink<SyntheticProfile>, SyntheticBuilder>{});

template <typename Profile, typename = void>
struct HasProfileTraits : public ZuFalse { };
template <typename Profile>
struct HasProfileTraits<Profile,
  decltype(sizeof(Zhttp::ProfileTraits<Profile>), void())> : public ZuTrue { };

using Invalid =
  Zhttp::Profile<Zhttp::QUIC, Zhttp::Version::H1>;
static_assert(Zhttp::IsProfile<Zhttp::H1TCP>{});
static_assert(Zhttp::IsProfile<Zhttp::H1TLS>{});
static_assert(Zhttp::IsProfile<Zhttp::H2TLS>{});
static_assert(Zhttp::IsProfile<Zhttp::H3QUIC>{});
static_assert(!Zhttp::IsProfile<Invalid>{});
static_assert(HasProfileTraits<Zhttp::H1TCP>{});
static_assert(!HasProfileTraits<Invalid>{});

struct Link {
  Tx txStream() { return {}; }
  int process(Rx &) { return 0; }
  void connected(Zhttp::ConnectedInfo) { }
  void disconnected(bool) { }
};

static_assert(Zhttp::Transport_::HasTxStream<Link>{});
static_assert(Zhttp::Transport_::HasProcess<Link, Rx>{});
static_assert(Zhttp::Transport_::HasConnected<
  Link, Zhttp::ConnectedInfo>{});
static_assert(Zhttp::Transport_::HasDisconnected<Link>{});
using Contract =
  Zhttp::Transport_::LinkContract<Link, Rx, Zhttp::ConnectedInfo>;
static_assert(sizeof(Contract) == 1);

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
    void sendBuf_(ZmRef<ZiIOBuf> buf, bool) {
      ++link->handoffs;
      link->wire << ZuCSpan{buf->cspan()};
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
    bool input = rx.input();
    events |= rx.events();
    if (input) {
      (void)rx.consume(
	[](ZuBSpan span) -> int64_t { return span.length(); },
	[this](ZuBSpan span) { data << ZuCSpan{span}; });
      events |= rx.events();
    }
    return result;
  }

  ZtString<ZtStringHeapID<"Zhttp.Contract.StreamData">> data;
  Zi::RxEvent::T events{};
  unsigned	calls = 0;
  int		result = 1;
};

using LogicalStream = Zhttp::Stream<StreamLink>;
using Dispatch = Zhttp::StreamDispatch<StreamLink, StreamConsumer>;
static_assert(sizeof(LogicalStream) == sizeof(void *));
static_assert(sizeof(Dispatch) == sizeof(void *) * 2);

struct TxRequest {
  ZuCSpan	body;
  uint64_t	length = 0;
  bool		hasBody = false;
  bool		overproduce = false;
};

struct TxCursor {
  unsigned	offset = 0;
  unsigned	id = 0;
};

struct TxApp {
  using BodyPolicy = Zhttp::Body::OptionalFixed<TxCursor>;

  bool requestHasBody(const TxRequest &request) const {
    return request.hasBody;
  }
  uint64_t requestContentLength(const TxRequest &request) const {
    return request.length;
  }
  TxCursor requestBodyCursor(TxRequest &) {
    return {0, ++cursorCalls};
  }
  unsigned requestBodyBatch() const { return 3; }
  template <typename Tx_>
  int requestBody(
    TxRequest &request, TxCursor &cursor, Tx_ &tx, unsigned batch) {
    ++producerCalls;
    if (cursor.id != activeCursor) {
      activeCursor = cursor.id;
      ++cursorStarts;
    }
    unsigned remaining = request.body.length() - cursor.offset;
    unsigned n = remaining > batch ? batch : remaining;
    if (request.overproduce && remaining > n) ++n;
    tx << ZuCSpan{request.body.data() + cursor.offset, n};
    cursor.offset += n;
    return cursor.offset == request.body.length() ?
      Zhttp::BodyProduce::Done : Zhttp::BodyProduce::More;
  }
  template <typename L>
  void requestOperation(TxRequest &, L &&l) {
    l(Zhttp::Method::PUT, "/", "");
  }
  template <typename L>
  void requestHost(TxRequest &, L &&l) { l("localhost"); }
  template <typename L>
  void requestProtocol(TxRequest &, L &&) { }
  template <typename Key, typename L>
  void requestHeader(TxRequest &, L &&l) { l(""); }

  void responseStatus(auto &, TxRequest &, unsigned) { }
  void responseContentLength(auto &, TxRequest &, uint64_t) { }
  void responseChunked(auto &, TxRequest &) { }
  void responseVersion(auto &, TxRequest &, ZuBSpan) { }
  template <typename Key>
  void responseHeader(auto &, TxRequest &, ZuBSpan) { }
  void responseBody(auto &, TxRequest &, auto &) { }
  template <typename State>
  void responseComplete(auto &, TxRequest &, typename State::T) { }
  bool responseFailed(const TxRequest &) const { return false; }

  unsigned	cursorCalls = 0;
  unsigned	producerCalls = 0;
  unsigned	cursorStarts = 0;
  unsigned	activeCursor = 0;
};

struct StreamTxApp : public TxApp {
  using BodyPolicy = Zhttp::Body::OptionalStream<TxCursor>;

  template <typename L>
  void requestOperation(TxRequest &, L &&l) {
    l(Zhttp::Method::POST, "/stream", "");
  }
};

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
    void sendBuf_(ZmRef<ZiIOBuf> buf, bool) {
      link->wire << ZuCSpan{buf->cspan()};
    }

    TxLink	*link;
  };

  auto transmit(auto &) { return Stream{*this}; }
  void finish() { ++finishes; }

  ZtString<ZtStringHeapID<"Zhttp.Contract.Wire">> wire;
  unsigned	finishes = 0;
};

void testBodyTx()
{
  ZuTestScope(testBodyTx);

  using Message = Zhttp::ClientMessage<
    TxApp, TxRequest, TxLink, Zhttp::H1TCP,
    ZuTypeList<>, ZuTypeList<>, 1024>;

  TxApp app;
  TxLink link;
  Message message{&app, &link};
  TxRequest request{"abcdefgh", 8, true};
  message.bind(&request);
  message.startTx();
  ZuCHECK(message.send() == Zhttp::BodySend::More,
    "first bounded body turn did not suspend");
  ZuCHECK(message.send() == Zhttp::BodySend::More,
    "second bounded body turn did not suspend");
  ZuCHECK(message.send() == Zhttp::BodySend::Complete,
    "final bounded body turn did not complete");
  auto commit = message.commit();
  ZuCHECK(app.cursorCalls == 1 && app.producerCalls == 3 &&
      commit.headers && commit.produced == 8 && commit.committed == 8 &&
      !commit.reset && !commit.discarded && commit.final &&
      link.finishes == 1,
    "bounded body accounting mismatch");
  ZuCHECK(link.wire.find("content-length: 8\r\n") >= 0 &&
      link.wire.find("abcdefgh") >= 0,
    "bounded body wire output mismatch");

  unsigned wireLength = link.wire.length();
  message.bind(&request);
  message.startTx();
  while (message.send() == Zhttp::BodySend::More);
  ZuCSpan replay{
    link.wire.data() + wireLength, link.wire.length() - wireLength};
  ZuCHECK(app.cursorCalls == 2 && app.cursorStarts == 2 &&
      replay.find("abcdefgh") >= 0,
    "replayed attempt did not create a fresh byte-zero cursor");

  wireLength = link.wire.length();
  unsigned producerCalls = app.producerCalls;
  TxRequest empty{{}, 0, false};
  message.bind(&empty);
  message.startTx();
  ZuCHECK(message.send() == Zhttp::BodySend::Complete &&
      app.cursorCalls == 2 && app.producerCalls == producerCalls,
    "optional bodyless path created a cursor or producer turn");
  ZuCSpan bodyless{
    link.wire.data() + wireLength, link.wire.length() - wireLength};
  ZuCHECK(bodyless.find("content-length:") < 0,
    "bodyless request emitted body framing");

  TxRequest short_{"abcdefgh", 9, true};
  message.bind(&short_);
  message.startTx();
  ZuCHECK(message.send() == Zhttp::BodySend::More &&
      message.send() == Zhttp::BodySend::More &&
      message.send() == Zhttp::BodySend::Failed &&
      message.commit().produced == 8 &&
      message.commit().committed == 8 &&
      message.commit().reset == 8 &&
      message.commit().discarded == 1 &&
      !message.commit().final,
    "short fixed source was not rejected");

  TxRequest over{"abcdefgh", 8, true, true};
  message.bind(&over);
  message.startTx();
  ZuCHECK(message.send() == Zhttp::BodySend::Failed &&
      message.commit().produced == 4 &&
      message.commit().committed == 4 &&
      message.commit().reset == 4 &&
      message.commit().discarded == 4 &&
      !message.commit().final,
    "over-budget producer was not rejected");

  request = {"abcdefgh", 8, true};
  message.bind(&request);
  message.startTx();
  ZuCHECK(message.send() == Zhttp::BodySend::More,
    "cancellation setup turn failed");
  unsigned calls = app.producerCalls;
  message.cancelTx();
  ZuCHECK(message.send() == Zhttp::BodySend::Cancelled &&
      app.producerCalls == calls &&
      message.commit().produced == 3 &&
      message.commit().committed == 3 &&
      message.commit().reset == 3 &&
      message.commit().discarded == 5,
    "cancelled request invoked its producer again");

  using StreamMessage = Zhttp::ClientMessage<
    StreamTxApp, TxRequest, TxLink, Zhttp::H1TCP,
    ZuTypeList<>, ZuTypeList<>, 1024>;
  StreamTxApp streamApp;
  TxLink streamLink;
  StreamMessage streamMessage{&streamApp, &streamLink};
  TxRequest streamRequest{"abcdefgh", 0, true};
  streamMessage.bind(&streamRequest);
  streamMessage.startTx();
  ZuCHECK(streamMessage.send() == Zhttp::BodySend::More &&
      streamMessage.send() == Zhttp::BodySend::More &&
      streamMessage.send() == Zhttp::BodySend::Complete,
    "streaming POST did not use bounded producer turns");
  ZuCHECK(
    streamLink.wire.find("POST /stream HTTP/1.1\r\n") >= 0 &&
    streamLink.wire.find("transfer-encoding: chunked\r\n") >= 0 &&
    streamLink.wire.find(
      "00000003\r\nabc\r\n00000003\r\ndef\r\n"
      "00000002\r\ngh\r\n0\r\n\r\n") >= 0,
    "streaming POST chunk framing mismatch");
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

  Zhttp::EngineConfig engine{nullptr, "rx", "tx"};
  auto tcpCli = TCP::clientParams(engine, Zhttp::TCPConfig{});
  auto tcpSrv = TCP::serverParams(engine, Zhttp::TCPConfig{});
  ZuCHECK(tcpCli.rxThread == "rx" && tcpCli.txThread == "tx" &&
      tcpSrv.rxThread == "rx" && tcpSrv.txThread == "tx",
    "TCP common parameter mapping mismatch");

  auto tlsCli = TLS::clientParams(
    engine, Zhttp::TLSConfig{}.caPath("ca.pem"));
  auto tlsSrv = TLS::serverParams(
    engine, Zhttp::TLSConfig{}
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

  Zhttp::H2Config force;
  force.policy(Zhttp::H2Policy::Force);
  Zhttp::H2Config prefer;
  prefer.policy(Zhttp::H2Policy::Prefer);
  Zhttp::H2Config disable;
  disable.policy(Zhttp::H2Policy::Disable);
  auto forceCli = Zhttp::TLS_::clientParams(engine, force);
  auto preferSrv = Zhttp::TLS_::serverParams(engine, prefer);
  auto disableCli = Zhttp::TLS_::clientParams(engine, disable);
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

  auto quicCli = QUIC::clientParams(engine, Zhttp::QUICConfig{});
  auto quicSrv = QUIC::serverParams(engine, Zhttp::QUICConfig{});
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

  auto serviceQUIC = Zhttp::ServiceConfig{}.idleTimeout(7)
    .quic(Zhttp::QUICConfig{}).quicEngineConfig();
  auto explicitQUIC = Zhttp::ServiceConfig{}.idleTimeout(7)
    .quic(Zhttp::QUICConfig{}.maxIdleTimeout(2500)).quicEngineConfig();
  ZuCHECK(serviceQUIC.maxIdleTimeout() == 7000 &&
      explicitQUIC.maxIdleTimeout() == 2500,
    "service idle timeout mapping mismatch");

  Zhttp::DiscoveryLimits limits{
    .maxRecords = 3, .maxHints = 5,
    .maxEndpoints = 7, .maxAliasDepth = 2};
  auto agent = Zhttp::AgentConfig{}
    .requestTimeout(13).maxAltSvc(11)
    .bodyTxBatch(4096)
    .discoveryLimits(limits).altSvcCrossHost(true)
    .h2Policy(Zhttp::H2Policy::Disable);
  ZuCHECK(agent.requestTimeout() == 13 &&
      agent.maxAltSvc() == 11 && agent.altSvcCrossHost() &&
      agent.bodyTxBatch() == 4096 &&
      agent.h2Policy() == Zhttp::H2Policy::Disable &&
      agent.discoveryLimits().maxRecords == 3 &&
      agent.discoveryLimits().maxHints == 5 &&
      agent.discoveryLimits().maxEndpoints == 7 &&
      agent.discoveryLimits().maxAliasDepth == 2,
    "agent discovery bounds mapping mismatch");
  ZuCHECK(
    Zhttp::AgentConfig{}.bodyTxBatch() == Zhttp::BodyDeflt::TxBatch,
    "body stream policy defaults mismatch");
  ZuCHECK(
    Zhttp::ServiceConfig{}.tlsConfig().policy() ==
      Zhttp::H2Policy::Prefer &&
    Zhttp::ServiceConfig{}
      .tls(Zhttp::H2Config{}.policy(Zhttp::H2Policy::Force))
      .tlsConfig().policy() == Zhttp::H2Policy::Force,
    "service H2 policy mapping mismatch");
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
    Zhttp::migrationMode("disabled") == Zhttp::Migration::Disabled &&
    Zhttp::migrationMode("passive") == Zhttp::Migration::Passive &&
    Zhttp::migrationMode("active") == Zhttp::Migration::Active,
    "migration configuration vocabulary mismatch");
}

void testBodyRx()
{
  ZuTestScope(testBodyRx);

  uint8_t bytes[] = {'a', 'b', 'c', 'd', 'e'};
  Zhttp::BodyRx body;
  unsigned calls = 0;
  bool ok = body.offer({bytes, unsigned(sizeof(bytes))}, true,
    [&calls](auto &rx) {
      ++calls;
      auto events = rx.events();
      ZuCHECK(events == Zi::RxEvent::Start(),
	"body receive layer did not begin with Start");
      ZuCHECK(rx.input() && rx.available() == 5,
	"body input was not admitted");
      events = rx.events();
      ZuCHECK(events == Zi::RxEvent::Input(),
	"body input did not raise Input exactly once");
      ZuCHECK(!rx.events(), "body input flags did not clear on read");
      ZuCHECK(rx.consume(
	  [](ZuBSpan) -> int64_t { return 2; },
	  [](ZuBSpan span) {
	    ZuCHECK(ZuCSpan(span) == "ab",
	      "first partial body consume mismatch");
	  }) == 2,
	"first partial body consume failed");
      ZuCHECK(!rx.events(), "partial body consume raised a terminal event");
      ZuCHECK(rx.consume(
	  [](ZuBSpan span) -> int64_t { return span.length(); },
	  [](ZuBSpan span) {
	    ZuCHECK(ZuCSpan(span) == "cde",
	      "second partial body consume mismatch");
	  }) == 3,
	"second partial body consume failed");
      events = rx.events();
      ZuCHECK(events == Zi::RxEvent::Final(),
	"final body consume did not raise Final exactly once");
      ZuCHECK(!rx.events(), "body final flag did not clear on read");
    });
  ZuCHECK(ok && calls == 1 && body.consumed() == sizeof(bytes) &&
      body.complete(),
    "body receive layer did not complete exact consumption");

  body.reset();
  ok = body.offer({bytes, unsigned(sizeof(bytes))}, false,
    [](auto &rx) {
      ZuCHECK(rx.input(), "body input was not admitted before cancellation");
      (void)rx.consume(
	[](ZuBSpan) -> int64_t { return 1; },
	[](ZuBSpan) { });
    });
  ZuCHECK(!ok && body.consumed() == 1,
    "partial callback return was not rejected");
  body.cancel();
  ZuCHECK(!body.consumed(), "cancelled body retained consumed state");

  ErrorRxOwner owner;
  ZiRxLayer<ErrorRxOwner> error{owner};
  ZuCHECK(error.events() == Zi::RxEvent::Start(),
    "error layer did not begin with Start");
  ZuCHECK(!error.input() && error.failed(),
    "error refill did not fail the receive layer");
  ZuCHECK(error.events() == Zi::RxEvent::Error(),
    "error refill did not raise Error exactly once");
  ZuCHECK(!error.events(), "receive error flag did not clear on read");
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

  StreamConsumer consumer;
  Dispatch dispatch;
  Zhttp::BodyRx body;
  dispatch.init(link, consumer);
  ZuCHECK(body.start(
      [&dispatch](auto &rx) { dispatch.process(rx); }) &&
      consumer.calls == 1 &&
      consumer.events == Zi::RxEvent::Start(),
    "stream dispatch did not deliver Start");
  consumer.events = {};

  uint8_t bytes[] = {'e', 'f'};
  ZuCHECK(body.offer({bytes, unsigned(sizeof(bytes))}, false,
      [&dispatch](auto &rx) { dispatch.process(rx); }) &&
      consumer.calls == 2 &&
      consumer.events == Zi::RxEvent::Input() &&
      consumer.data == "ef",
    "stream dispatch did not deliver Input");
  consumer.events = {};
  ZuCHECK(body.finish(
      [&dispatch](auto &rx) { dispatch.process(rx); }) &&
      consumer.calls == 3 &&
      consumer.events == Zi::RxEvent::Final(),
    "stream dispatch did not deliver empty Final");

  Zhttp::BodyRx failed;
  consumer.events = {};
  ZuCHECK(failed.start(
      [&dispatch](auto &rx) { dispatch.process(rx); }) &&
      failed.fail(
	[&dispatch](auto &rx) { dispatch.process(rx); }) &&
      consumer.calls == 5 &&
      consumer.events ==
	(Zi::RxEvent::Start() | Zi::RxEvent::Error()),
    "stream dispatch did not deliver Error");

  Zhttp::BodyRx rejected;
  consumer.result = -1;
  int result = 0;
  ZuCHECK(rejected.start(
      [&dispatch, &result](auto &rx) { result = dispatch.process(rx); }) &&
      result == -1 && link.resets == 2,
    "negative stream result did not reset the logical stream");
  unsigned calls = consumer.calls;
  uint8_t rejectedByte = 'x';
  ZuCHECK(!rejected.offer({&rejectedByte, 1}, false,
      [&dispatch](auto &rx) { dispatch.process(rx); }) &&
      consumer.calls == calls && link.resets == 2,
    "terminal stream dispatch admitted or reset a second callback");
  consumer.result = 1;
  rejected.cancel();

  dispatch.disable_();
  calls = consumer.calls;
  Zhttp::BodyRx disabled;
  ZuCHECK(disabled.start(
      [&dispatch](auto &rx) { dispatch.process(rx); }) &&
      consumer.calls == calls,
    "disabled stream dispatch admitted a callback");
  disabled.cancel();
  ZuCHECK(consumer.calls == calls,
    "silent BodyRx cancellation invoked the consumer");
  dispatch.final_();
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
  ZuTestCall(testBodyRx);
  ZuTestCall(testStream);
  return 0;
}
