//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <unistd.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include <iostream>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/Zquic.hh>

using namespace ZuTestUtil;

namespace {

static constexpr unsigned RuntimeServerLinkCapacity = 20;
static constexpr unsigned RuntimeServerMultiConnections = 17;

template <typename Link>
Zquic::RuntimeDiag runtimeDiag(const Link &link)
{
  return ZmBlock<Zquic::RuntimeDiag>{}(
    [&link](auto wake) { link->runtimeDiag(ZuMv(wake)); });
}

template <typename Link>
Zquic::EndpointDiag cxnDiag(const Link &link)
{
  return ZmBlock<Zquic::EndpointDiag>{}(
    [&link](auto wake) { link->cxnDiag(ZuMv(wake)); });
}

template <typename Server>
Zquic::EndpointDiag endpointDiag(Server &server)
{
  return ZmBlock<Zquic::EndpointDiag>{}(
    [&server](auto wake) { server.endpointDiag(ZuMv(wake)); });
}

template <typename Server>
Zquic::AddressValidationDiag addressValidationDiag(Server &server)
{
  return ZmBlock<Zquic::AddressValidationDiag>{}(
    [&server](auto wake) { server.addressValidationDiag(ZuMv(wake)); });
}

struct RuntimeClient : public Zquic::Client<RuntimeClient> {
  struct Link;
  struct Stream;

  enum TokenRewrite {
    TokenNone,
    TokenMalformed,
    TokenAuth,
    TokenExpired,
    TokenAddress,
    TokenPolicy,
    TokenKind
  };

  bool sendPkt(const ZmRef<ZiIOBuf> &buf) {
    unsigned rewrite = tokenRewrite;
    if (!rewrite || !buf || !buf->length) return true;
    auto packet = buf->cspan();
    if (!Zquic::Pkt::isLong(packet)) return true;
    Zquic::LongHdr h;
    if (Zquic::Pkt::parseLong(packet, h) < 0 ||
	h.type != Zquic::PktType::Initial ||
	!h.tokenLength)
      return true;

    ZiSockAddr addr{
      rewrite == TokenAddress ? ZiIP("127.0.0.2") : ZiIP("127.0.0.1"),
      12345
    };
    Zquic::TokenKind::T kind = rewrite == TokenKind ?
      Zquic::TokenKind::T(3) : Zquic::TokenKind::NewToken;
    Zquic::TokenBytes token;
    if (!Zquic::AddressToken::encode(
	  token, kind, tokenSecret, addr, h.dcid, {},
	  rewrite == TokenExpired ? tokenNow - 120 : tokenNow, false))
      return true;
    if (rewrite == TokenMalformed)
      token[0] = 'X';
    else if (rewrite == TokenAuth)
      token[token.length() - 1] ^= 1;
    if (token.length() != h.tokenLength) {
      ++tokenRewriteLengthMismatch;
      return true;
    }
    memcpy(buf->data_() + h.tokenOffset, token.data(), token.length());
    tokenRewrite = TokenNone;
    ++tokenRewritesApplied;
    return true;
  }

  Zquic::TokenSecret tokenSecret;
  uint64_t tokenNow = 0;
  ZmAtomic<unsigned> tokenRewrite = TokenNone;
  ZmAtomic<unsigned> tokenRewritesApplied = 0;
  ZmAtomic<unsigned> tokenRewriteLengthMismatch = 0;
};
struct RuntimeServerLink;
struct RuntimeServerStream;
struct RuntimeServer :
    public Zquic::Server<RuntimeServer, RuntimeServerLink> {
  using Link = RuntimeServerLink;
  using Stream = RuntimeServerStream;

  ZmRef<Link> link(unsigned i = unsigned(-1));
  ZmRef<Link> accepted(const Zquic::InitialInfo &);
  void clearLinks();
  ZiIP localIP() const { return ZiIP("127.0.0.1"); }
  bool sendPkt(const ZmRef<ZiIOBuf> &buf) {
    if (!buf || !buf->length) return true;
    auto packet = buf->cspan();
    if (Zquic::Pkt::isLong(packet)) {
      Zquic::LongHdr h;
      if (Zquic::Pkt::parseLong(packet, h) < 0) return true;
      if (h.type == Zquic::PktType::Retry) {
	++retryPkts;
	return true;
      }
      if (h.type == Zquic::PktType::Initial && dropNextInitial) {
	--dropNextInitial;
	++droppedInitial;
	return false;
      }
      if (h.type == Zquic::PktType::Handshake && dropNextHandshake) {
	--dropNextHandshake;
	++droppedHandshake;
	return false;
      }
      return true;
    }
    ++shortPkts;
    if (!dropNextShort) return true;
    dropNextShort = 0;
    ++droppedShort;
    return false;
  }
  bool sendFrame(const Zquic::SentFrameRef &ref) {
    if (ref.kind != Zquic::SentFrameKind::Stream) return true;
    ++streamFrames;
    if (dropStreamID < 0 || ref.streamID != uint64_t(dropStreamID))
      return true;
    dropStreamID = -1;
    ++droppedStream;
    return false;
  }

  ZmRef<Link> link_;
  ZmRef<Link> links_[RuntimeServerLinkCapacity];
  ZmAtomic<unsigned> acceptedCount = 0;
  ZmAtomic<unsigned> retryPkts = 0;
  ZmAtomic<unsigned> dropNextInitial = 0;
  ZmAtomic<unsigned> droppedInitial = 0;
  ZmAtomic<unsigned> dropNextHandshake = 0;
  ZmAtomic<unsigned> droppedHandshake = 0;
  ZmAtomic<unsigned> dropNextShort = 0;
  ZmAtomic<unsigned> droppedShort = 0;
  ZmAtomic<unsigned> shortPkts = 0;
  ZmAtomic<int64_t> dropStreamID = -1;
  ZmAtomic<unsigned> droppedStream = 0;
  ZmAtomic<unsigned> streamFrames = 0;
};

struct RuntimeClient::Stream :
  public Zquic::CliStream<RuntimeClient::Link, RuntimeClient::Stream> {
  using Base =
    Zquic::CliStream<RuntimeClient::Link, RuntimeClient::Stream>;
  using Base::Base;

  int process(Zquic::RxStream &) { ++processed; return 0; }

  unsigned processed = 0;
};

struct RuntimeClient::Link :
  public Zquic::CliLink<RuntimeClient, RuntimeClient::Link,
    RuntimeClient::Stream> {
  using Base = Zquic::CliLink<RuntimeClient, RuntimeClient::Link,
    RuntimeClient::Stream>;
  using Base::Base;

  Link(RuntimeClient *app) : Base{app} { }

  void connected(Zi::Connected) { ++connectedCount; }
  void disconnected(bool) { ++disconnectedCount; }
  void connectFailed(bool) { ++connectFailures; }
  void streamed(ZmRef<Stream>) { ++streamedCount; }
  void forceCloseTimeout() {
    app()->txInvoke(this, [link = ZmRef<Link>{this}]() mutable {
      link->Base::closeTimeout_();
      return link;
    });
  }

  ZmAtomic<unsigned> connectedCount = 0;
  ZmAtomic<unsigned> disconnectedCount = 0;
  ZmAtomic<unsigned> connectFailures = 0;
  ZmAtomic<unsigned> streamedCount = 0;
};

struct RuntimeServerStream :
  public Zquic::SrvStream<RuntimeServerLink, RuntimeServerStream> {
  using Base =
    Zquic::SrvStream<RuntimeServerLink, RuntimeServerStream>;
  using Base::Base;

  int process(Zquic::RxStream &) { ++processed; return 0; }

  unsigned processed = 0;
};

struct RuntimeServerLink :
  public Zquic::SrvLink<RuntimeServer, RuntimeServerLink,
    RuntimeServerStream> {
  using Base = Zquic::SrvLink<RuntimeServer, RuntimeServerLink,
    RuntimeServerStream>;
  using Base::Base;

  RuntimeServerLink(RuntimeServer *app) : Base{app} { }

  void connected(Zi::Connected) { ++connectedCount; }
	void disconnected(bool) { ++disconnectedCount; }
	void streamed(ZmRef<Stream>) { ++streamedCount; }
  void forceCloseTimeout() {
    app()->txInvoke(this, [link = ZmRef<RuntimeServerLink>{this}]() mutable {
      link->Base::closeTimeout_();
      return link;
    });
  }

	ZmAtomic<unsigned> connectedCount = 0;
	ZmAtomic<unsigned> disconnectedCount = 0;
	ZmAtomic<unsigned> streamedCount = 0;
};

ZmRef<RuntimeServer::Link> RuntimeServer::link(unsigned i)
{
  if (i != unsigned(-1))
    return i < RuntimeServerLinkCapacity ? links_[i] : nullptr;
  return link_;
}

ZmRef<RuntimeServer::Link> RuntimeServer::accepted(const Zquic::InitialInfo &)
{
  unsigned i = acceptedCount;
  if (i >= RuntimeServerLinkCapacity) return nullptr;
  links_[i] = new Link{this};
  link_ = links_[i];
  ++acceptedCount;
  return link_;
}

void RuntimeServer::clearLinks()
{
  link_ = nullptr;
  for (unsigned i = 0; i < RuntimeServerLinkCapacity; ++i)
    links_[i] = nullptr;
}

template <typename L>
bool waitUntil(L l)
{
  for (unsigned i = 0; i < 10000; ++i) {
    if (l()) return true;
    usleep(1000);
  }
  return false;
}

void waitThread(ZiMultiplex &mx, unsigned thread)
{
	ZmBlock<>{}([&mx, thread](auto wake) {
	  mx.invoke([wake = ZuMv(wake)]() mutable { wake(); }, thread);
	});
}

void waitDisconnect(ZiMultiplex &mx)
{
  waitThread(mx, mx.txThread());
  waitThread(mx, mx.rxThread());
  waitThread(mx, mx.txThread());
}

ZtString<> readFile_(ZuCSpan path)
{
  ZtString<> data;
  ZiFile file{Zi::Path{path}, ZiFile::ReadOnly | ZiFile::GC};
  if (!file) return data;
  auto size = file.size();
  if (size <= 0 || size > (4<<20)) return data;
  data.length(unsigned(size));
  int n = file.read(data.data(), data.length());
  if (n <= 0)
    data.length(0);
  else
    data.length(unsigned(n));
  return data;
}

void dumpRuntimeDiag(
  const char *name, const Zquic::RuntimeDiag &d, const Zquic::CryptoDiag &c)
{
  std::cout <<
    "# " << name <<
    " endpointReady=" << uint64_t(d.rx.endpointReady) <<
    " datagramsRx=" << uint64_t(d.rx.datagramsRx) <<
    " bytesRx=" << uint64_t(d.rx.bytesRx) <<
    " bytesTx=" << uint64_t(d.tx.bytesTx) <<
    " packetsTx=" << uint64_t(d.tx.packetsTx) <<
    " packetsRx=" << uint64_t(d.rx.packetsRx) <<
    " framesRx=" << uint64_t(d.rx.framesRx) <<
    " cryptoBytesTx=" << uint64_t(d.tx.cryptoBytesTx) <<
    " cryptoBytesRx=" << uint64_t(d.rx.cryptoBytesRx) <<
    " streamBytesTx=" << uint64_t(d.tx.streamBytesTx) <<
    " streamBytesRx=" << uint64_t(d.rx.streamBytesRx) <<
    " ptoCount=" << uint64_t(d.tx.ptoCount) <<
    " retransmittedFrames=" << uint64_t(d.tx.retransmittedFrames) <<
    " failures=" << uint64_t(d.failures()) <<
    " handshakeComplete=" << uint64_t(d.rx.handshakeComplete) <<
    " tlsHandled=" << c.tlsMessagesHandled <<
    " tlsEmitted=" << c.tlsMessagesEmitted <<
    " secrets=" << c.secretsInstalled <<
    '\n';
}

struct TempDir {
  char		path[PATH_MAX]{};
  ZtString<>	certPath;
  ZtString<>	keyPath;

  ~TempDir() { cleanup(); }

  static bool keep()
  {
    return !!getenv("ZQUIC_TEST_KEEP");
  }

  bool init()
  {
    strcpy(path, "/tmp/ZquicRuntimeTest.XXXXXX");
    if (!mkdtemp(path)) return false;
    certPath << static_cast<const char *>(path) << "/cert.pem";
    keyPath << static_cast<const char *>(path) << "/key.pem";

    ZtString<> cmd;
    cmd <<
      "openssl req -x509 -newkey rsa:2048 -nodes -days 1 "
      "-subj /CN=localhost "
      "-addext basicConstraints=critical,CA:TRUE "
      "-addext keyUsage=critical,digitalSignature,keyEncipherment,keyCertSign "
      "-addext subjectAltName=DNS:localhost,IP:127.0.0.1 "
      "-keyout " << keyPath << ' ' <<
      "-out " << certPath << " >/dev/null 2>&1";
    return systemOK(system(cmd.data()));
  }

  ZtString<> pathOf(const char *name) const
  {
    ZtString<> s;
    s << static_cast<const char *>(path) << '/' << name;
    return s;
  }

  static bool systemOK(int status)
  {
    return status != -1 && WIFEXITED(status) && !WEXITSTATUS(status);
  }

  void cleanup()
  {
    if (!path[0]) return;
    if (keep()) return;
    const char *names[] = {
      "cert.pem", "key.pem", "endpoint.sqlog", "retry.sqlog",
      "rejected-token.sqlog", "policy-token.sqlog",
      "idle.sqlog", nullptr };
    for (auto name = names; *name; ++name) {
      auto p = pathOf(*name);
      unlink(p.data());
    }
    rmdir(path);
    path[0] = 0;
  }
};

} // namespace

void testRuntimeEndpointOpen()
{
  ZuTestScope(testRuntimeEndpointOpen);

  TempDir temp;
  ZuCHECK(temp.init(), "runtime temporary TLS certificate generation failed");
  ZtString<> qlogPath = temp.pathOf("endpoint.sqlog");

  ZiMultiplex mx(
      ZiMxParams()
	.scheduler([](auto &s) {
	  s.nThreads(5)
	    .thread(1, [](auto &t) { t.isolated(1); })
	    .thread(2, [](auto &t) { t.isolated(1); })
	    .thread(3, [](auto &t) { t.isolated(1); })
	    .thread(4, [](auto &t) { t.isolated(1); }); })
	.rxThread(1).txThread(2));

  bool mxStarted = mx.start();
  ZuCHECK(mxStarted, "runtime multiplexer start failed");
  if (!mxStarted) return;

  RuntimeServer server;
  ZuCHECK(server.init(
      Zquic::ServerParams(&mx, "3", "4")
	.certPath(temp.certPath.cspan())
	.keyPath(temp.keyPath.cspan())
	.qlog(true)
	.qlogPath(qlogPath.cspan())
	.qlogThread("zquic-endpoint-qlog")
	.qlogRingSize(1<<16)
	.maxData(32768)
	.maxStreamData(8192)
	.maxStreamsBidi(8)
	.maxStreamsUni(8)
	.alpn(ZuSpan<ZuCSpan>{"h3"})),
    "runtime server init failed");
  ZuCHECK(server.start(),
    "runtime server listen failed");
  ZuCHECK(waitUntil([&server]() { return server.listening(); }),
    "runtime server endpoint did not become ready");
  ZuCHECK(server.listening() &&
      !endpointDiag(server).failures &&
      server.local().port(),
    "runtime server endpoint diagnostics mismatch");
	  if (!server.listening()) {
	    server.stop();
	    server.final();
	    mx.stop();
	    return;
	  }

  RuntimeClient client;
  ZuCHECK(client.init(
      Zquic::ClientParams(&mx, "3", "4")
	.caPath(temp.certPath.cspan())
	.maxData(32768)
	.maxStreamData(8192)
	.maxStreamsBidi(8)
	.maxStreamsUni(8)
	.alpn(ZuSpan<ZuCSpan>{"h3"})),
    "runtime client init failed");
  ZmRef<RuntimeClient::Link> clientLink = new RuntimeClient::Link{&client};
  clientLink->connect(Zquic::Host{"127.0.0.1"}, server.local().port());
  ZuCHECK(waitUntil([&clientLink]() { return clientLink->ready(); }),
    "runtime client link did not become ready");
  ZuCHECK(runtimeDiag(clientLink).rx.endpointReady == 1 &&
      !runtimeDiag(clientLink).failures() &&
      !cxnDiag(clientLink).failures,
    "runtime client link diagnostics mismatch");
	  if (!clientLink->ready()) {
	    client.final();
	    server.stop();
	    server.final();
	    mx.stop();
	    return;
	  }

  ZmRef<RuntimeServer::Link> serverLink;
  bool established = waitUntil([&server, &clientLink, &serverLink]() {
      if (!serverLink) serverLink = server.link();
      return serverLink && clientLink->established() &&
	serverLink->established();
    });
  if (!established) {
    dumpRuntimeDiag(
      "client", runtimeDiag(clientLink), clientLink->crypto().diag());
    if (serverLink)
      dumpRuntimeDiag(
	"server", runtimeDiag(serverLink), serverLink->crypto().diag());
  }
  ZuCHECK(established, "runtime UDP QUIC handshake did not establish");
	  if (!established || !serverLink) {
	    client.final();
	    server.stop();
	    server.final();
	    mx.stop();
	    return;
	  }
  ZuCHECK(server.acceptedCount == 1, "runtime server accept count mismatch");
  ZuCHECK(clientLink->crypto().oneRTTReady() &&
      serverLink->crypto().oneRTTReady() &&
      clientLink->crypto().negotiatedProtocol() == "h3" &&
      serverLink->crypto().negotiatedProtocol() == "h3",
    "runtime TLS/ALPN state mismatch");
  ZuCHECK(runtimeDiag(clientLink).rx.handshakeComplete == 1 &&
      clientLink->connectedCount == 1,
    "runtime client handshake completion diagnostics mismatch");
  ZuCHECK(runtimeDiag(serverLink).rx.handshakeComplete == 1,
    "runtime server handshake completion diagnostics mismatch");
  ZuCHECK(runtimeDiag(clientLink).tx.packetsTx &&
      runtimeDiag(clientLink).rx.packetsRx &&
      runtimeDiag(serverLink).tx.packetsTx &&
      runtimeDiag(serverLink).rx.packetsRx,
    "runtime packet diagnostics mismatch");
  ZuCHECK(runtimeDiag(clientLink).tx.cryptoBytesTx &&
      runtimeDiag(clientLink).rx.cryptoBytesRx &&
      runtimeDiag(serverLink).tx.cryptoBytesTx &&
      runtimeDiag(serverLink).rx.cryptoBytesRx,
    "runtime CRYPTO byte diagnostics mismatch");
  ZuCHECK(!runtimeDiag(clientLink).failures(),
    "runtime client failure diagnostics mismatch");
  ZuCHECK(!runtimeDiag(serverLink).failures(),
    "runtime server failure diagnostics mismatch");

  auto clientBidi = clientLink->stream(Zi::StreamType::Duplex);
  auto clientUni = clientLink->stream(Zi::StreamType::Simplex);
  ZuCHECK(clientLink->send(clientBidi, "client-bidi") &&
      clientLink->send(clientUni, "client-uni"),
    "runtime client stream send failed");
  auto serverBidi = serverLink->stream(Zi::StreamType::Duplex);
  auto serverUni = serverLink->stream(Zi::StreamType::Simplex);
  ZuCHECK(serverLink->send(serverBidi, "server-bidi") &&
      serverLink->send(serverUni, "server-uni"),
    "runtime server stream send failed");
  bool streamsArrived = waitUntil([&clientLink, &serverLink]() {
      return runtimeDiag(serverLink).rx.streamBytesRx >= 21 &&
	runtimeDiag(clientLink).rx.streamBytesRx >= 21;
    });
  if (!streamsArrived) {
    dumpRuntimeDiag(
      "client", runtimeDiag(clientLink), clientLink->crypto().diag());
    dumpRuntimeDiag(
      "server", runtimeDiag(serverLink), serverLink->crypto().diag());
  }
  ZuCHECK(streamsArrived, "runtime protected stream bytes did not arrive");
  ZuCHECK(runtimeDiag(clientLink).tx.streamBytesTx == 21 &&
      runtimeDiag(serverLink).tx.streamBytesTx == 21 &&
      runtimeDiag(clientLink).tx.packetsTx >= 2 &&
      runtimeDiag(serverLink).tx.packetsTx >= 2,
    "runtime stream diagnostics mismatch");
  auto serverRxBidi = serverLink->findStream(0);
  auto serverRxUni = serverLink->findStream(2);
  auto clientRxBidi = clientLink->findStream(1);
  auto clientRxUni = clientLink->findStream(3);
  ZuCHECK(serverRxBidi && serverRxBidi->link() == serverLink.ptr() &&
      serverRxBidi->processed >= 1 &&
      serverRxUni && serverRxUni->link() == serverLink.ptr() &&
      serverRxUni->processed >= 1 &&
      clientRxBidi && clientRxBidi->link() == clientLink.ptr() &&
      clientRxBidi->processed >= 1 &&
      clientRxUni && clientRxUni->link() == clientLink.ptr() &&
      clientRxUni->processed >= 1 &&
      serverLink->streamedCount == 2 &&
      clientLink->streamedCount == 2,
    "runtime stream objects did not process received STREAM frames");

	ZmBlock<>{}([&clientLink](auto wake) {
	  clientLink->disconnect([wake = ZuMv(wake)]() mutable { wake(); });
	});
	waitDisconnect(mx);
	ZuCHECK(serverLink->closed(), "runtime server did not observe client close");
	serverLink->forceCloseTimeout();
	ZuCHECK(server.stop(), "runtime server stop failed");
	waitDisconnect(mx);
	ZuCHECK(!clientLink->cxn() && clientLink->disconnectedCount == 1 &&
	    !server.connected(),
	  "runtime endpoints remained connected after close");
  waitDisconnect(mx);

  serverRxBidi = nullptr;
  serverRxUni = nullptr;
  clientRxBidi = nullptr;
  clientRxUni = nullptr;
  clientLink = nullptr;
  serverLink = nullptr;
  server.clearLinks();
  client.final();
  server.final();

#ifdef Zquic_DEBUG
  ZtString<> qlog = readFile_(qlogPath.cspan());
  ZuCHECK(qlog, "endpoint runtime qlog output missing");
  ZuCHECK(qlog.find<"\"type\":\"server\"">() >= 0,
    "endpoint runtime qlog missing server vantage point");
  ZuCHECK(qlog.find<"common_fields">() >= 0 &&
      qlog.find<"ODCID">() >= 0 &&
      qlog.find<"group_id">() >= 0 &&
      qlog.find<"DCID">() >= 0 &&
      qlog.find<"SCID">() >= 0,
    "endpoint runtime qlog missing connection linkInfo fields");
  ZuCHECK(qlog.find<"\"ODCID\":\"\"">() < 0 &&
      qlog.find<"\"group_id\":\"\"">() < 0 &&
      qlog.find<"\"DCID\":\"\"">() < 0 &&
      qlog.find<"\"SCID\":\"\"">() < 0,
    "endpoint runtime qlog has empty connection linkInfo");
  ZuCHECK(qlog.find<"quic:stream_state_updated">() >= 0,
    "endpoint runtime qlog missing stream_state_updated");
  ZuCHECK(qlog.find<"quic:udp_datagrams_sent">() >= 0,
    "endpoint runtime qlog missing datagrams_sent");
  ZuCHECK(qlog.find<"quic:packets_acked">() >= 0,
    "endpoint runtime qlog missing packets_acked");
  ZuCHECK(qlog.find<"quic:parameters_set">() >= 0 &&
      qlog.find<"max_udp_payload_size">() >= 0 &&
      qlog.find<"initial_max_data">() >= 0,
    "endpoint runtime qlog missing transport parameters");
  ZuCHECK(qlog.find<"quic:alpn_information">() >= 0 &&
      qlog.find<"\"chosen_alpn\":{\"string_value\":\"h3\"}">() >= 0,
    "endpoint runtime qlog missing ALPN selection");
  ZuCHECK(qlog.find<"packet_number_space">() >= 0,
    "endpoint runtime qlog missing ACK packet_number_space");
  ZuCHECK(qlog.find<"packet_numbers">() >= 0,
    "endpoint runtime qlog missing ACK packet_numbers");
  ZuCHECK(qlog.find<"quic:stream_data_moved">() >= 0,
    "endpoint runtime qlog missing stream_data_moved");
  ZuCHECK(qlog.find<"stream_id">() >= 0,
    "endpoint runtime qlog missing stream_id");
  ZuCHECK(qlog.find<"application">() >= 0 &&
      qlog.find<"transport">() >= 0 &&
      qlog.find<"network">() >= 0,
    "endpoint runtime qlog missing stream data movement locations");
  ZuCHECK(qlog.find<"\"from\":\"transport\"">() >= 0 &&
      qlog.find<"\"to\":\"application\"">() >= 0,
    "endpoint runtime qlog missing transport-to-application movement");
  ZuCHECK(qlog.find<"raw">() >= 0 && qlog.find<"length">() >= 0,
    "endpoint runtime qlog missing stream data raw length");
  ZuCHECK(qlog.find<"client-bidi">() < 0 &&
      qlog.find<"client-uni">() < 0 &&
      qlog.find<"server-bidi">() < 0 &&
      qlog.find<"server-uni">() < 0,
    "endpoint runtime qlog leaked application payload");
  ZuCHECK(qlog.find<"local_open">() >= 0 || qlog.find<"peer_open">() >= 0,
    "endpoint runtime qlog missing stream open reason");
  ZuCHECK(qlog.find<"quic:connection_closed">() >= 0,
    "endpoint runtime qlog missing connection_closed");
  ZuCHECK(qlog.find<"peer_close_frame">() >= 0 ||
      qlog.find<"local_close">() >= 0,
    "endpoint runtime qlog missing close reason");
  ZuCHECK(qlog.find<"drain_expired">() >= 0 &&
      qlog.find<"aborted">() >= 0,
    "endpoint runtime qlog missing close/drain expiry");
  if (!TempDir::keep()) unlink(qlogPath.data());
#endif

  mx.stop();
}

void testRuntimeHandshakeCryptoLoss()
{
  ZuTestScope(testRuntimeHandshakeCryptoLoss);

  TempDir temp;
  ZuCHECK(temp.init(), "loss runtime temporary TLS certificate failed");

  ZiMultiplex mx(
      ZiMxParams()
	.scheduler([](auto &s) {
	  s.nThreads(5)
	    .thread(1, [](auto &t) { t.isolated(1); })
	    .thread(2, [](auto &t) { t.isolated(1); })
	    .thread(3, [](auto &t) { t.isolated(1); })
	    .thread(4, [](auto &t) { t.isolated(1); }); })
	.rxThread(1).txThread(2));

  bool mxStarted = mx.start();
  ZuCHECK(mxStarted, "loss runtime multiplexer start failed");
  if (!mxStarted) return;

  for (unsigned dropHandshake = 0; dropHandshake < 2; ++dropHandshake) {
    RuntimeServer server;
    ZuCHECK(server.init(
	Zquic::ServerParams(&mx, "3", "4")
	  .certPath(temp.certPath.cspan())
	  .keyPath(temp.keyPath.cspan())
	  .maxData(32768)
	  .maxStreamData(8192)
	  .maxStreamsBidi(8)
	  .maxStreamsUni(8)
	  .alpn(ZuSpan<ZuCSpan>{"h3"})),
      "loss runtime server init failed");
    ZuCHECK(server.start(), "loss runtime server listen failed");
    bool listening = waitUntil([&server]() { return server.listening(); });
    ZuCHECK(listening, "loss runtime server did not listen");
	    if (!server.listening()) {
	      server.stop();
	      server.final();
	      continue;
	    }
    if (dropHandshake)
      server.dropNextHandshake = 4;
    else
      server.dropNextInitial = 1;

    RuntimeClient client;
    ZuCHECK(client.init(
	Zquic::ClientParams(&mx, "3", "4")
	  .caPath(temp.certPath.cspan())
	  .maxData(32768)
	  .maxStreamData(8192)
	  .maxStreamsBidi(8)
	  .maxStreamsUni(8)
	  .alpn(ZuSpan<ZuCSpan>{"h3"})),
      "loss runtime client init failed");
    ZmRef<RuntimeClient::Link> clientLink = new RuntimeClient::Link{&client};
    clientLink->connect(Zquic::Host{"127.0.0.1"}, server.local().port());

    ZmRef<RuntimeServer::Link> serverLink;
    bool established = waitUntil([&server, &clientLink, &serverLink]() {
	if (!serverLink) serverLink = server.link();
	return serverLink && clientLink->established() &&
	  serverLink->established();
      });
    if (!established) {
      dumpRuntimeDiag(
	"client", runtimeDiag(clientLink), clientLink->crypto().diag());
      if (serverLink)
	dumpRuntimeDiag(
	  "server", runtimeDiag(serverLink), serverLink->crypto().diag());
    }
    ZuCHECK(established, "loss runtime handshake did not recover");
    ZuCHECK(dropHandshake ? server.droppedHandshake == 4 :
	server.droppedInitial == 1,
      "loss runtime did not drop selected long-header packet");
    ZuCHECK(serverLink && runtimeDiag(serverLink).tx.retransmittedFrames,
      "loss runtime did not retransmit dropped handshake data");

    ZmSemaphore clientClosed;
    clientLink->disconnect([&clientClosed]() { clientClosed.post(); });
    server.stop();
    ZuCHECK(waitUntil([&clientLink]() {
	return !clientLink->cxn();
      }) && !server.connected(),
      "loss runtime endpoints remained connected after close");
    clientClosed.wait();
    waitDisconnect(mx);
    clientLink = nullptr;
    serverLink = nullptr;
    client.final();
    server.final();
  }

  mx.stop();
}

void testRuntimeServerMultiConnection()
{
  ZuTestScope(testRuntimeServerMultiConnection);

  TempDir temp;
  ZuCHECK(temp.init(), "multi runtime temporary TLS certificate failed");

  ZiMultiplex mx(
      ZiMxParams()
	.scheduler([](auto &s) {
	  s.nThreads(5)
	    .thread(1, [](auto &t) { t.isolated(1); })
	    .thread(2, [](auto &t) { t.isolated(1); })
	    .thread(3, [](auto &t) { t.isolated(1); })
	    .thread(4, [](auto &t) { t.isolated(1); }); })
	.rxThread(1).txThread(2));

  bool mxStarted = mx.start();
  ZuCHECK(mxStarted, "multi runtime multiplexer start failed");
  if (!mxStarted) return;

  ZmAtomic<unsigned> serverErrors = 0;
  RuntimeServer server;
  ZuCHECK(server.init(
      Zquic::ServerParams(&mx, "3", "4")
	.certPath(temp.certPath.cspan())
	.keyPath(temp.keyPath.cspan())
	.maxData(32768)
	.maxStreamData(8192)
	.maxStreamsBidi(8)
	.maxStreamsUni(8)
	.alpn(ZuSpan<ZuCSpan>{"h3"})
	.errorFn(Zquic::ErrorFn{[&serverErrors](ZeException) {
	  ++serverErrors;
	}})),
    "multi runtime server init failed");
  ZuCHECK(server.start(), "multi runtime server listen failed");
  ZuCHECK(waitUntil([&server]() { return server.listening(); }),
    "multi runtime server did not become ready");

  RuntimeClient client;
  ZuCHECK(client.init(
      Zquic::ClientParams(&mx, "3", "4")
	.caPath(temp.certPath.cspan())
	.maxData(32768)
	.maxStreamData(8192)
	.maxStreamsBidi(8)
	.maxStreamsUni(8)
	.alpn(ZuSpan<ZuCSpan>{"h3"})),
    "multi runtime client init failed");

  ZmRef<RuntimeClient::Link> clients[RuntimeServerMultiConnections];
  ZmRef<RuntimeServer::Link> serverLinks[RuntimeServerMultiConnections];
  bool established = true;
  for (unsigned i = 0; i < RuntimeServerMultiConnections; ++i) {
    clients[i] = new RuntimeClient::Link{&client};
    clients[i]->connect(Zquic::Host{"127.0.0.1"}, server.local().port());
    if (!waitUntil([&server, &clients, &serverLinks, i]() {
	if (!serverLinks[i]) serverLinks[i] = server.link(i);
	return serverLinks[i] && clients[i]->established() &&
	  serverLinks[i]->established();
      })) {
      established = false;
      break;
    }
  }
  auto &c0 = clients[0];
  auto &c1 = clients[1];
  auto &s0 = serverLinks[0];
  auto &s1 = serverLinks[1];

  if (established)
    established = waitUntil([&server]() {
	return server.acceptedCount == RuntimeServerMultiConnections;
      });
  if (!established) {
    for (unsigned i = 0; i < RuntimeServerMultiConnections; ++i) {
      if (clients[i])
	dumpRuntimeDiag(
	  "client", runtimeDiag(clients[i]), clients[i]->crypto().diag());
      if (serverLinks[i])
	dumpRuntimeDiag(
	  "server", runtimeDiag(serverLinks[i]), serverLinks[i]->crypto().diag());
    }
  }
  ZuCHECK(established,
    "17 runtime connections did not establish");
  ZuCHECK(server.acceptedCount == RuntimeServerMultiConnections &&
      !serverErrors,
    "server live-link table rejected connections past the old cap");
  if (!established || !s0 || !s1) {
    for (unsigned i = 0; i < RuntimeServerMultiConnections; ++i)
      if (clients[i]) clients[i]->disconnect();
    (void)waitUntil([&clients]() {
	for (unsigned i = 0; i < RuntimeServerMultiConnections; ++i)
	  if (clients[i] && clients[i]->cxn()) return false;
	return true;
      });
    server.stop();
    waitDisconnect(mx);
    for (unsigned i = 0; i < RuntimeServerMultiConnections; ++i)
      clients[i] = nullptr;
    for (unsigned i = 0; i < RuntimeServerMultiConnections; ++i)
      serverLinks[i] = nullptr;
    server.clearLinks();
    client.final();
    server.final();
    mx.stop();
    return;
  }

  auto c0s = c0->stream(Zi::StreamType::Duplex);
  auto c1s = c1->stream(Zi::StreamType::Duplex);
  ZuCHECK(c0->send(c0s, "zero") && c1->send(c1s, "one"),
    "multi runtime client stream sends failed");
  ZuCHECK(waitUntil([&s0, &s1]() {
      return runtimeDiag(s0).rx.streamBytesRx == 4 &&
	runtimeDiag(s1).rx.streamBytesRx == 3;
    }), "multi runtime routed stream bytes did not arrive independently");

  uint64_t s0Bytes = runtimeDiag(s0).rx.streamBytesRx;
  uint64_t s1Bytes = runtimeDiag(s1).rx.streamBytesRx;
  uint64_t failures = endpointDiag(server).failures;
  unsigned errors = serverErrors;
  s0->disconnect();
  ZuCHECK(waitUntil([&s0]() {
      return s0->closed() && !s0->established();
    }), "multi runtime server link did not close logically");

  auto c0Stale = c0->stream(Zi::StreamType::Duplex);
  auto c1Live = c1->stream(Zi::StreamType::Duplex);
  ZuCHECK(c0->send(c0Stale, "drop") && c1->send(c1Live, "alive"),
    "multi runtime post-close client stream sends failed");
  ZuCHECK(waitUntil([&server, &s1, failures, s1Bytes]() {
      return endpointDiag(server).failures > failures &&
	runtimeDiag(s1).rx.streamBytesRx >= s1Bytes + 5;
    }), "multi runtime stale route drop or sibling delivery did not happen");
  ZuCHECK(runtimeDiag(s0).rx.streamBytesRx == s0Bytes,
    "closed server link received stale routed data");
  ZuCHECK(serverErrors == errors,
    "stale routed datagram used ErrorFn instead of diagnostics");

  for (unsigned i = 0; i < RuntimeServerMultiConnections; ++i)
    clients[i]->disconnect();
  server.stop();
  ZuCHECK(waitUntil([&clients]() {
      for (unsigned i = 0; i < RuntimeServerMultiConnections; ++i)
	if (clients[i]->cxn()) return false;
      return true;
    }) && !server.connected(),
    "multi runtime endpoints remained connected after close");
  waitDisconnect(mx);

  c0s = nullptr;
  c1s = nullptr;
  c0Stale = nullptr;
  c1Live = nullptr;
  for (unsigned i = 0; i < RuntimeServerMultiConnections; ++i)
    clients[i] = nullptr;
  for (unsigned i = 0; i < RuntimeServerMultiConnections; ++i)
    serverLinks[i] = nullptr;
  server.clearLinks();
  client.final();
  server.final();

  mx.stop();
}

void testRuntimeRetryAddressValidation()
{
  ZuTestScope(testRuntimeRetryAddressValidation);

  TempDir temp;
  ZuCHECK(temp.init(), "retry runtime temporary TLS certificate failed");

  ZiMultiplex mx(
      ZiMxParams()
	.scheduler([](auto &s) {
	  s.nThreads(5)
	    .thread(1, [](auto &t) { t.isolated(1); })
	    .thread(2, [](auto &t) { t.isolated(1); })
	    .thread(3, [](auto &t) { t.isolated(1); })
	    .thread(4, [](auto &t) { t.isolated(1); }); })
	.rxThread(1).txThread(2));

  bool mxStarted = mx.start();
  ZuCHECK(mxStarted, "retry runtime multiplexer start failed");
  if (!mxStarted) return;

  RuntimeServer server;
  ZtString<> qlogPath = temp.pathOf("retry.sqlog");
  unlink(qlogPath.data());
  ZuCHECK(server.init(
      Zquic::ServerParams(&mx, "3", "4")
	.certPath(temp.certPath.cspan())
	.keyPath(temp.keyPath.cspan())
	.qlog(true)
	.qlogPath(qlogPath.cspan())
	.qlogThread("zquic-retry-qlog")
	.qlogRingSize(1<<16)
	.maxData(32768)
	.maxStreamData(8192)
	.maxStreamsBidi(8)
	.maxStreamsUni(8)
	.retryAddressValidation(true)
	.newTokenAddressValidation(true)
	.addressValidationLifetime(60)
	.alpn(ZuSpan<ZuCSpan>{"h3"})),
    "retry runtime server init failed");
  ZuCHECK(server.start(), "retry runtime server listen failed");
  ZuCHECK(waitUntil([&server]() { return server.listening(); }),
    "retry runtime server did not listen");
	  if (!server.listening()) {
	    server.stop();
	    server.final();
	    mx.stop();
	    return;
	  }

  RuntimeClient client;
  ZuCHECK(client.init(
      Zquic::ClientParams(&mx, "3", "4")
	.caPath(temp.certPath.cspan())
	.maxData(32768)
	.maxStreamData(8192)
	.maxStreamsBidi(8)
	.maxStreamsUni(8)
	.alpn(ZuSpan<ZuCSpan>{"h3"})),
    "retry runtime client init failed");
  ZmRef<RuntimeClient::Link> clientLink = new RuntimeClient::Link{&client};
  clientLink->connect(Zquic::Host{"127.0.0.1"}, server.local().port());

  ZmRef<RuntimeServer::Link> serverLink;
  bool established = waitUntil([&server, &clientLink, &serverLink]() {
      if (!serverLink) serverLink = server.link();
      return serverLink && clientLink->established() &&
	serverLink->established();
    });
  if (!established) {
    dumpRuntimeDiag(
      "client", runtimeDiag(clientLink), clientLink->crypto().diag());
    if (serverLink)
      dumpRuntimeDiag(
	"server", runtimeDiag(serverLink), serverLink->crypto().diag());
  }
  ZuCHECK(established, "retry runtime handshake did not establish");
	  if (!established || !serverLink) {
	    client.final();
	    server.stop();
	    server.final();
	    mx.stop();
	    return;
	  }
  ZuCHECK(server.retryPkts >= 1,
    "retry runtime server did not emit Retry");
  Zquic::AddressValidationDiag av = addressValidationDiag(server);
  ZuCHECK(av.retrySent >= 1 && av.retryAccepted == 1 &&
      av.retryRejected >= 1,
    "retry runtime address-validation diagnostics mismatch");
  ZuCHECK(server.acceptedCount == 1,
    "retry runtime server accepted before validated Initial");
  const auto &params = clientLink->crypto().peerTransportParams();
  ZuCHECK(params.origDCID.length() >= Zquic::MinCIDLength &&
      params.initialSCID.length() >= Zquic::MinCIDLength &&
      params.retrySCID.length() >= Zquic::MinCIDLength,
    "retry runtime transport parameters missing Retry CIDs");
  ZuCHECK(!runtimeDiag(clientLink).failures() &&
      !runtimeDiag(serverLink).failures(),
    "retry runtime failure diagnostics mismatch");
  ZuCHECK(waitUntil([&clientLink, &serverLink]() {
      return runtimeDiag(clientLink).rx.newTokenRx >= 1 &&
	runtimeDiag(serverLink).tx.newTokenTx >= 1;
    }), "retry runtime NEW_TOKEN was not exchanged");

  unsigned retryPkts = server.retryPkts;
  clientLink->disconnect();
  ZuCHECK(waitUntil([&clientLink]() { return !clientLink->cxn(); }),
    "retry runtime first connection did not close");
  waitDisconnect(mx);

  clientLink->connect(Zquic::Host{"127.0.0.1"}, server.local().port());
  ZmRef<RuntimeServer::Link> serverLink2;
  bool established2 = waitUntil([&server, &clientLink, &serverLink2]() {
      if (!serverLink2) serverLink2 = server.link(1);
      return serverLink2 && clientLink->established() &&
	serverLink2->established();
    });
  if (!established2) {
    dumpRuntimeDiag(
      "client2", runtimeDiag(clientLink), clientLink->crypto().diag());
    if (serverLink2)
      dumpRuntimeDiag(
	"server2", runtimeDiag(serverLink2), serverLink2->crypto().diag());
  }
  ZuCHECK(established2,
    "retry runtime NEW_TOKEN reconnect did not establish");
	  if (!established2 || !serverLink2) {
	    client.final();
	    server.stop();
	    server.final();
	    mx.stop();
	    return;
	  }
  ZuCHECK(server.acceptedCount == 2,
    "retry runtime server did not accept NEW_TOKEN reconnect");
  ZuCHECK(server.retryPkts == retryPkts,
    "retry runtime NEW_TOKEN reconnect unexpectedly used Retry");
  av = addressValidationDiag(server);
  ZuCHECK(av.newTokenAccepted == 1,
    "retry runtime NEW_TOKEN acceptance diagnostics mismatch");
  ZuCHECK(!runtimeDiag(clientLink).failures() &&
      !runtimeDiag(serverLink2).failures(),
    "retry runtime NEW_TOKEN reconnect diagnostics mismatch");

  clientLink->disconnect();
  server.stop();
  ZuCHECK(waitUntil([&clientLink]() { return !clientLink->cxn(); }) &&
      !server.connected(),
    "retry runtime endpoints remained connected after close");
  waitDisconnect(mx);

  clientLink = nullptr;
  serverLink = nullptr;
  serverLink2 = nullptr;
  server.clearLinks();
  client.final();
  server.final();

#ifdef Zquic_DEBUG
  ZtString<> qlog = readFile_(qlogPath.cspan());
  ZuCHECK(qlog, "retry runtime qlog output missing");
  ZuCHECK(qlog.find<"zquic:retry_sent">() >= 0,
    "retry runtime qlog missing retry_sent");
  ZuCHECK(qlog.find<"zquic:retry_validated">() >= 0,
    "retry runtime qlog missing retry_validated");
  ZuCHECK(qlog.find<"zquic:token_issued">() >= 0,
    "retry runtime qlog missing token_issued");
  ZuCHECK(qlog.find<"zquic:token_validated">() >= 0,
    "retry runtime qlog missing token_validated");
  ZuCHECK(qlog.find<"zquic:token_rejected">() >= 0,
    "retry runtime qlog missing token_rejected");
  ZuCHECK(qlog.find<"quic:packet_buffered">() >= 0,
    "retry runtime qlog missing packet_buffered");
  ZuCHECK(qlog.find<"address_validation">() >= 0,
    "retry runtime qlog missing address-validation reason");
  ZuCHECK(qlog.find<"missing_token">() >= 0,
    "retry runtime qlog missing missing-token rejection reason");
	  ZuCHECK(qlog.find<"new_token">() >= 0,
	    "retry runtime qlog missing NEW_TOKEN reason");
	  ZuCHECK(qlog.find<"coalescing">() < 0,
	    "retry runtime qlog leaked private coalescing reason");
  if (!TempDir::keep()) unlink(qlogPath.data());
#endif

  mx.stop();
}

void testRuntimeRejectedTokenQLog()
{
  ZuTestScope(testRuntimeRejectedTokenQLog);

  TempDir temp;
  ZuCHECK(temp.init(), "rejected-token runtime temporary TLS certificate failed");
  ZtString<> qlogPath = temp.pathOf("rejected-token.sqlog");
  ZtString<> policyQlogPath = temp.pathOf("policy-token.sqlog");
  unlink(qlogPath.data());
  unlink(policyQlogPath.data());

  Zquic::TokenSecret secret;
  secret.length(Zquic::AddressToken::SecretLength);
  for (unsigned i = 0; i < secret.length(); ++i) secret[i] = uint8_t(i + 1);

  ZiMultiplex mx(
      ZiMxParams()
	.scheduler([](auto &s) {
	  s.nThreads(5)
	    .thread(1, [](auto &t) { t.isolated(1); })
	    .thread(2, [](auto &t) { t.isolated(1); })
	    .thread(3, [](auto &t) { t.isolated(1); })
	    .thread(4, [](auto &t) { t.isolated(1); }); })
	.rxThread(1).txThread(2));

  bool mxStarted = mx.start();
  ZuCHECK(mxStarted, "rejected-token runtime multiplexer start failed");
  if (!mxStarted) return;

  RuntimeServer server;
  ZuCHECK(server.init(
      Zquic::ServerParams(&mx, "3", "4")
	.certPath(temp.certPath.cspan())
	.keyPath(temp.keyPath.cspan())
	.qlog(true)
	.qlogPath(qlogPath.cspan())
	.qlogThread("zquic-rejected-token-qlog")
	.qlogRingSize(1<<16)
	.maxData(32768)
	.maxStreamData(8192)
	.maxStreamsBidi(8)
	.maxStreamsUni(8)
	.retryAddressValidation(true)
	.newTokenAddressValidation(true)
	.addressValidationSecret(secret)
	.addressValidationLifetime(60)
	.alpn(ZuSpan<ZuCSpan>{"h3"})),
    "rejected-token runtime server init failed");
  ZuCHECK(server.start(), "rejected-token runtime server listen failed");
  ZuCHECK(waitUntil([&server]() { return server.listening(); }),
    "rejected-token runtime server did not listen");
	  if (!server.listening()) {
	    server.stop();
	    server.final();
	    mx.stop();
	    return;
	  }

  RuntimeClient client;
  client.tokenSecret = secret;
  client.tokenNow = uint64_t(Zm::now().sec());
  ZuCHECK(client.init(
	Zquic::ClientParams(&mx, "3", "4")
	  .caPath(temp.certPath.cspan())
	  .maxData(32768)
	  .maxStreamData(8192)
	  .maxStreamsBidi(8)
	  .maxStreamsUni(8)
	  .alpn(ZuSpan<ZuCSpan>{"h3"})),
    "rejected-token runtime client init failed");
  ZmRef<RuntimeClient::Link> clientLink = new RuntimeClient::Link{&client};

  clientLink->connect(Zquic::Host{"127.0.0.1"}, server.local().port());
  ZmRef<RuntimeServer::Link> serverLink;
  bool established = waitUntil([&server, &clientLink, &serverLink]() {
      if (!serverLink) serverLink = server.link(0);
      return serverLink && clientLink->established() &&
	serverLink->established();
    });
  if (!established) {
    dumpRuntimeDiag(
      "client", runtimeDiag(clientLink), clientLink->crypto().diag());
    if (serverLink)
      dumpRuntimeDiag(
	"server", runtimeDiag(serverLink), serverLink->crypto().diag());
  }
  ZuCHECK(established,
    "rejected-token runtime initial handshake did not establish");
	  if (!established || !serverLink) {
	    client.final();
	    server.stop();
	    server.final();
	    mx.stop();
	    return;
	  }
  ZuCHECK(waitUntil([&clientLink, &serverLink]() {
      return runtimeDiag(clientLink).rx.newTokenRx >= 1 &&
	runtimeDiag(serverLink).tx.newTokenTx >= 1;
    }), "rejected-token runtime NEW_TOKEN was not exchanged");
  clientLink->disconnect();
  ZuCHECK(waitUntil([&clientLink]() { return !clientLink->cxn(); }),
    "rejected-token runtime initial connection did not close");
  waitDisconnect(mx);

  unsigned accepted = 1;
  unsigned modes[] = {
    RuntimeClient::TokenMalformed,
    RuntimeClient::TokenAuth,
    RuntimeClient::TokenExpired,
    RuntimeClient::TokenAddress,
    RuntimeClient::TokenKind
  };
  for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
    client.tokenNow = uint64_t(Zm::now().sec());
    client.tokenRewrite = modes[i];
    clientLink->connect(Zquic::Host{"127.0.0.1"}, server.local().port());

    serverLink = nullptr;
    established = waitUntil([&server, &clientLink, &serverLink, accepted]() {
	if (!serverLink) serverLink = server.link(accepted);
	return serverLink && clientLink->established() &&
	  serverLink->established();
      });
    if (!established) {
      dumpRuntimeDiag(
	"client", runtimeDiag(clientLink), clientLink->crypto().diag());
      if (serverLink)
	dumpRuntimeDiag(
	  "server", runtimeDiag(serverLink), serverLink->crypto().diag());
    }
    ZuCHECK(established, "rejected-token runtime handshake did not establish");
	    if (!established || !serverLink) {
	      break;
    }
    ++accepted;
    clientLink->disconnect();
    ZuCHECK(waitUntil([&clientLink]() { return !clientLink->cxn(); }),
      "rejected-token runtime connection did not close");
    waitDisconnect(mx);
  }
  server.stop();
  ZuCHECK(waitUntil([&server]() { return !server.connected(); }),
    "rejected-token runtime server remained connected");
  waitDisconnect(mx);
  server.clearLinks();
  server.final();

  RuntimeServer policyServer;
  ZuCHECK(policyServer.init(
      Zquic::ServerParams(&mx, "3", "4")
	.certPath(temp.certPath.cspan())
	.keyPath(temp.keyPath.cspan())
	.qlog(true)
	.qlogPath(policyQlogPath.cspan())
	.qlogThread("zquic-rejected-token-qlog")
	.qlogRingSize(1<<16)
	.maxData(32768)
	.maxStreamData(8192)
	.maxStreamsBidi(8)
	.maxStreamsUni(8)
	.retryAddressValidation(true)
	.newTokenAddressValidation(false)
	.addressValidationSecret(secret)
	.addressValidationLifetime(60)
	.alpn(ZuSpan<ZuCSpan>{"h3"})),
    "rejected-token policy server init failed");
  ZuCHECK(policyServer.start(), "rejected-token policy server listen failed");
  ZuCHECK(waitUntil([&policyServer]() { return policyServer.listening(); }),
    "rejected-token policy server did not listen");
  client.tokenNow = uint64_t(Zm::now().sec());
  client.tokenRewrite = RuntimeClient::TokenPolicy;
  clientLink->connect(Zquic::Host{"127.0.0.1"}, policyServer.local().port());
  ZmRef<RuntimeServer::Link> policyLink;
  established = waitUntil([&policyServer, &clientLink, &policyLink]() {
      if (!policyLink) policyLink = policyServer.link(0);
      return policyLink && clientLink->established() &&
	policyLink->established();
    });
  ZuCHECK(established, "rejected-token policy handshake did not establish");
  clientLink->disconnect();
  ZuCHECK(waitUntil([&clientLink]() { return !clientLink->cxn(); }),
    "rejected-token policy connection did not close");
  policyServer.stop();
  waitDisconnect(mx);
  policyLink = nullptr;
  policyServer.clearLinks();
  policyServer.final();
  clientLink = nullptr;
  client.final();

#ifdef Zquic_DEBUG
  ZtString<> qlog = readFile_(qlogPath.cspan());
  ZtString<> policyQlog = readFile_(policyQlogPath.cspan());
  ZuCHECK(qlog, "rejected-token runtime qlog output missing");
  ZuCHECK(policyQlog, "rejected-token policy qlog output missing");
  ZuCHECK(qlog.find<"zquic:token_rejected">() >= 0,
    "rejected-token runtime qlog missing token_rejected");
  ZuCHECK(qlog.find<"malformed">() >= 0,
    "rejected-token runtime qlog missing malformed reason");
  ZuCHECK(qlog.find<"auth">() >= 0,
    "rejected-token runtime qlog missing auth reason");
  ZuCHECK(qlog.find<"expired">() >= 0,
    "rejected-token runtime qlog missing expired reason");
  ZuCHECK(qlog.find<"address">() >= 0,
    "rejected-token runtime qlog missing address reason");
  ZuCHECK(qlog.find<"kind">() >= 0,
    "rejected-token runtime qlog missing kind reason");
  ZuCHECK(policyQlog.find<"new_token_policy">() >= 0,
    "rejected-token runtime qlog missing NEW_TOKEN policy reason");
  if (!TempDir::keep()) {
    unlink(qlogPath.data());
    unlink(policyQlogPath.data());
  }
#endif

  mx.stop();
}

void testRuntimeIdleTimeoutQLog()
{
  ZuTestScope(testRuntimeIdleTimeoutQLog);

  TempDir temp;
  ZuCHECK(temp.init(), "idle runtime temporary TLS certificate failed");
  ZtString<> qlogPath = temp.pathOf("idle.sqlog");
  unlink(qlogPath.data());

  ZiMultiplex mx(
      ZiMxParams()
	.scheduler([](auto &s) {
	  s.nThreads(5)
	    .thread(1, [](auto &t) { t.isolated(1); })
	    .thread(2, [](auto &t) { t.isolated(1); })
	    .thread(3, [](auto &t) { t.isolated(1); })
	    .thread(4, [](auto &t) { t.isolated(1); }); })
	.rxThread(1).txThread(2));

  bool mxStarted = mx.start();
  ZuCHECK(mxStarted, "idle runtime multiplexer start failed");
  if (!mxStarted) return;

  RuntimeServer server;
  ZuCHECK(server.init(
      Zquic::ServerParams(&mx, "3", "4")
	.certPath(temp.certPath.cspan())
	.keyPath(temp.keyPath.cspan())
	.qlog(true)
	.qlogPath(qlogPath.cspan())
	.qlogThread("zquic-idle-qlog")
	.qlogRingSize(1<<16)
	.maxData(32768)
	.maxStreamData(8192)
	.maxStreamsBidi(8)
	.maxStreamsUni(8)
	.maxIdleTimeout(1)
	.alpn(ZuSpan<ZuCSpan>{"h3"})),
    "idle runtime server init failed");
  ZuCHECK(server.start(), "idle runtime server listen failed");
  ZuCHECK(waitUntil([&server]() { return server.listening(); }),
    "idle runtime server did not listen");
	  if (!server.listening()) {
	    server.stop();
	    server.final();
	    mx.stop();
	    return;
	  }

  RuntimeClient client;
  ZuCHECK(client.init(
      Zquic::ClientParams(&mx, "3", "4")
	.caPath(temp.certPath.cspan())
	.maxData(32768)
	.maxStreamData(8192)
	.maxStreamsBidi(8)
	.maxStreamsUni(8)
	.maxIdleTimeout(1)
	.alpn(ZuSpan<ZuCSpan>{"h3"})),
    "idle runtime client init failed");
  ZmRef<RuntimeClient::Link> clientLink = new RuntimeClient::Link{&client};
  clientLink->connect(Zquic::Host{"127.0.0.1"}, server.local().port());

  ZmRef<RuntimeServer::Link> serverLink;
  bool established = waitUntil([&server, &clientLink, &serverLink]() {
      if (!serverLink) serverLink = server.link();
      return serverLink && clientLink->established() &&
	serverLink->established();
    });
  if (!established) {
    dumpRuntimeDiag(
      "client", runtimeDiag(clientLink), clientLink->crypto().diag());
    if (serverLink)
      dumpRuntimeDiag(
	"server", runtimeDiag(serverLink), serverLink->crypto().diag());
  }
  ZuCHECK(established, "idle runtime handshake did not establish");
	  if (!established || !serverLink) {
	    client.final();
	    server.stop();
	    server.final();
	    mx.stop();
	    return;
	  }

  ZuCHECK(waitUntil([&clientLink]() { return !clientLink->cxn(); }),
    "idle runtime client did not close on idle timeout");
  server.stop();
  waitDisconnect(mx);

  clientLink = nullptr;
  serverLink = nullptr;
  server.clearLinks();
  client.final();
  server.final();

#ifdef Zquic_DEBUG
  ZtString<> qlog = readFile_(qlogPath.cspan());
  ZuCHECK(qlog, "idle runtime qlog output missing");
  ZuCHECK(qlog.find<"quic:connection_closed">() >= 0,
    "idle runtime qlog missing connection_closed");
  ZuCHECK(qlog.find<"idle_timeout">() >= 0,
    "idle runtime qlog missing idle_timeout trigger");
  ZuCHECK(qlog.find<"idle">() >= 0,
    "idle runtime qlog missing idle reason");
  ZuCHECK(qlog.find<"no_error">() >= 0,
    "idle runtime qlog missing no_error close code");
  if (!TempDir::keep()) unlink(qlogPath.data());
#endif

  mx.stop();
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testRuntimeEndpointOpen);
  ZuTestCall(testRuntimeServerMultiConnection);
  ZuTestCall(testRuntimeRetryAddressValidation);
  ZuTestCall(testRuntimeRejectedTokenQLog);
  ZuTestCall(testRuntimeIdleTimeoutQLog);
}
