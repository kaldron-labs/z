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
#include <zlib/Zquic.hh>

using namespace ZuTestUtil;

namespace {

static constexpr unsigned RuntimeServerLinkCapacity = 20;
static constexpr unsigned RuntimeServerMultiConnections = 17;

struct RuntimeClient : public Zquic::Client<RuntimeClient> {
  struct Link;
  struct Stream;
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
    ZuBSpan packet{buf->data_(), buf->length};
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
  ZmSemaphore done;
  mx.invoke([&done]() { done.post(); }, thread);
  done.wait();
}

void waitDisconnect(ZiMultiplex &mx)
{
  waitThread(mx, mx.txThread());
  waitThread(mx, mx.rxThread());
  waitThread(mx, mx.txThread());
}

ZuCSpan cspan_(const ZtString<> &s)
{
  return ZuCSpan{s.data(), s.length()};
}

void dumpRuntimeDiag(
  const char *name, const Zquic::RuntimeDiag &d, const Zquic::CryptoDiag &c)
{
  std::cout <<
    "# " << name <<
    " endpointReady=" << uint64_t(d.endpointReady) <<
    " datagramsRx=" << uint64_t(d.datagramsRx) <<
    " bytesRx=" << uint64_t(d.bytesRx) <<
    " bytesTx=" << uint64_t(d.bytesTx) <<
    " packetsTx=" << uint64_t(d.packetsTx) <<
    " packetsRx=" << uint64_t(d.packetsRx) <<
    " framesRx=" << uint64_t(d.framesRx) <<
    " cryptoBytesTx=" << uint64_t(d.cryptoBytesTx) <<
    " cryptoBytesRx=" << uint64_t(d.cryptoBytesRx) <<
    " streamBytesTx=" << uint64_t(d.streamBytesTx) <<
    " streamBytesRx=" << uint64_t(d.streamBytesRx) <<
    " ptoCount=" << uint64_t(d.ptoCount) <<
    " retransmittedFrames=" << uint64_t(d.retransmittedFrames) <<
    " failures=" << uint64_t(d.failures) <<
    " handshakeComplete=" << uint64_t(d.handshakeComplete) <<
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
    const char *names[] = { "cert.pem", "key.pem", nullptr };
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
	.certPath(cspan_(temp.certPath))
	.keyPath(cspan_(temp.keyPath))
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
      !server.endpointDiag().failures &&
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
	.caPath(cspan_(temp.certPath))
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
  ZuCHECK(clientLink->runtimeDiag().endpointReady == 1 &&
      !clientLink->runtimeDiag().failures &&
      !clientLink->cxnDiag().failures,
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
      "client", clientLink->runtimeDiag(), clientLink->crypto().diag());
    if (serverLink)
      dumpRuntimeDiag(
	"server", serverLink->runtimeDiag(), serverLink->crypto().diag());
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
  ZuCHECK(clientLink->runtimeDiag().handshakeComplete == 1 &&
      clientLink->connectedCount == 1,
    "runtime client handshake completion diagnostics mismatch");
  ZuCHECK(serverLink->runtimeDiag().handshakeComplete == 1,
    "runtime server handshake completion diagnostics mismatch");
  ZuCHECK(clientLink->runtimeDiag().packetsTx &&
      clientLink->runtimeDiag().packetsRx &&
      serverLink->runtimeDiag().packetsTx &&
      serverLink->runtimeDiag().packetsRx,
    "runtime packet diagnostics mismatch");
  ZuCHECK(clientLink->runtimeDiag().cryptoBytesTx &&
      clientLink->runtimeDiag().cryptoBytesRx &&
      serverLink->runtimeDiag().cryptoBytesTx &&
      serverLink->runtimeDiag().cryptoBytesRx,
    "runtime CRYPTO byte diagnostics mismatch");
  ZuCHECK(!clientLink->runtimeDiag().failures,
    "runtime client failure diagnostics mismatch");
  ZuCHECK(!serverLink->runtimeDiag().failures,
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
      return serverLink->runtimeDiag().streamBytesRx >= 21 &&
	clientLink->runtimeDiag().streamBytesRx >= 21;
    });
  if (!streamsArrived) {
    dumpRuntimeDiag(
      "client", clientLink->runtimeDiag(), clientLink->crypto().diag());
    dumpRuntimeDiag(
      "server", serverLink->runtimeDiag(), serverLink->crypto().diag());
  }
  ZuCHECK(streamsArrived, "runtime protected stream bytes did not arrive");
  ZuCHECK(clientLink->runtimeDiag().streamBytesTx == 21 &&
      serverLink->runtimeDiag().streamBytesTx == 21 &&
      clientLink->runtimeDiag().packetsTx >= 2 &&
      serverLink->runtimeDiag().packetsTx >= 2,
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

  clientLink->disconnect();
  server.stop();
  ZuCHECK(waitUntil([&clientLink]() {
      return !clientLink->cxn() && clientLink->disconnectedCount == 1;
    }) && !server.connected(),
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
	  .certPath(cspan_(temp.certPath))
	  .keyPath(cspan_(temp.keyPath))
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
	  .caPath(cspan_(temp.certPath))
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
	"client", clientLink->runtimeDiag(), clientLink->crypto().diag());
      if (serverLink)
	dumpRuntimeDiag(
	  "server", serverLink->runtimeDiag(), serverLink->crypto().diag());
    }
    ZuCHECK(established, "loss runtime handshake did not recover");
    ZuCHECK(dropHandshake ? server.droppedHandshake == 4 :
	server.droppedInitial == 1,
      "loss runtime did not drop selected long-header packet");
    ZuCHECK(serverLink && serverLink->runtimeDiag().retransmittedFrames,
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
	.certPath(cspan_(temp.certPath))
	.keyPath(cspan_(temp.keyPath))
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
	.caPath(cspan_(temp.certPath))
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
	  "client", clients[i]->runtimeDiag(), clients[i]->crypto().diag());
      if (serverLinks[i])
	dumpRuntimeDiag(
	  "server", serverLinks[i]->runtimeDiag(), serverLinks[i]->crypto().diag());
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
      return s0->runtimeDiag().streamBytesRx == 4 &&
	s1->runtimeDiag().streamBytesRx == 3;
    }), "multi runtime routed stream bytes did not arrive independently");

  uint64_t s0Bytes = s0->runtimeDiag().streamBytesRx;
  uint64_t s1Bytes = s1->runtimeDiag().streamBytesRx;
  uint64_t failures = server.endpointDiag().failures;
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
      return server.endpointDiag().failures > failures &&
	s1->runtimeDiag().streamBytesRx >= s1Bytes + 5;
    }), "multi runtime stale route drop or sibling delivery did not happen");
  ZuCHECK(s0->runtimeDiag().streamBytesRx == s0Bytes,
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
  ZuCHECK(server.init(
      Zquic::ServerParams(&mx, "3", "4")
	.certPath(cspan_(temp.certPath))
	.keyPath(cspan_(temp.keyPath))
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
	.caPath(cspan_(temp.certPath))
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
      "client", clientLink->runtimeDiag(), clientLink->crypto().diag());
    if (serverLink)
      dumpRuntimeDiag(
	"server", serverLink->runtimeDiag(), serverLink->crypto().diag());
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
  Zquic::AddressValidationDiag av = server.addressValidationDiag();
  ZuCHECK(av.retrySent >= 1 && av.retryAccepted == 1 &&
      av.retryRejected >= 1,
    "retry runtime address-validation diagnostics mismatch");
  ZuCHECK(server.acceptedCount == 1,
    "retry runtime server accepted before validated Initial");
  const auto &params = clientLink->crypto().peerTransportParams();
  ZuCHECK(params.originalDCID.length() >= Zquic::MinCIDLength &&
      params.initialSCID.length() >= Zquic::MinCIDLength &&
      params.retrySCID.length() >= Zquic::MinCIDLength,
    "retry runtime transport parameters missing Retry CIDs");
  ZuCHECK(!clientLink->runtimeDiag().failures &&
      !serverLink->runtimeDiag().failures,
    "retry runtime failure diagnostics mismatch");
  ZuCHECK(waitUntil([&clientLink, &serverLink]() {
      return clientLink->runtimeDiag().newTokenRx >= 1 &&
	serverLink->runtimeDiag().newTokenTx >= 1;
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
      "client2", clientLink->runtimeDiag(), clientLink->crypto().diag());
    if (serverLink2)
      dumpRuntimeDiag(
	"server2", serverLink2->runtimeDiag(), serverLink2->crypto().diag());
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
  av = server.addressValidationDiag();
  ZuCHECK(av.newTokenAccepted == 1,
    "retry runtime NEW_TOKEN acceptance diagnostics mismatch");
  ZuCHECK(!clientLink->runtimeDiag().failures &&
      !serverLink2->runtimeDiag().failures,
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
  mx.stop();
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testRuntimeEndpointOpen);
  ZuTestCall(testRuntimeServerMultiConnection);
  ZuTestCall(testRuntimeRetryAddressValidation);
}
