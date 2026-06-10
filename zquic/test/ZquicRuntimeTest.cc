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
  ZiIP localIP() const { return ZiIP("127.0.0.1"); }

  ZmRef<Link> link_;
  ZmRef<Link> links_[4];
  ZmAtomic<unsigned> acceptedCount = 0;
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

  void connected(const char *, int) { ++connectedCount; }
  void disconnected() { ++disconnectedCount; }
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

  void connected(const char *, int) { ++connectedCount; }
  void disconnected() { ++disconnectedCount; }
  void streamed(ZmRef<Stream>) { ++streamedCount; }

  ZmAtomic<unsigned> connectedCount = 0;
  ZmAtomic<unsigned> disconnectedCount = 0;
  ZmAtomic<unsigned> streamedCount = 0;
};

ZmRef<RuntimeServer::Link> RuntimeServer::link(unsigned i)
{
  if (i != unsigned(-1)) return i < 4 ? links_[i] : nullptr;
  return link_;
}

ZmRef<RuntimeServer::Link> RuntimeServer::accepted(const Zquic::InitialInfo &)
{
  unsigned i = acceptedCount;
  if (i >= 4) return nullptr;
  links_[i] = new Link{this};
  link_ = links_[i];
  ++acceptedCount;
  return link_;
}

template <typename L>
bool waitUntil(L l)
{
  for (unsigned i = 0; i < 2000; ++i) {
    if (l()) return true;
    usleep(1000);
  }
  return false;
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

  RuntimeClient uninitialized;
  ZmRef<RuntimeClient::Link> uninitializedLink =
    new RuntimeClient::Link{&uninitialized};
  uninitializedLink->connect(Zquic::Host{"127.0.0.1"}, 4433);
  ZuCHECK(!uninitializedLink->cxn() &&
      uninitializedLink->connectFailures == 1,
    "uninitialized runtime client link opened a UDP socket");

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
  ZuCHECK(server.listen(),
    "runtime server listen failed");
  ZuCHECK(waitUntil([&server]() { return server.listening(); }),
    "runtime server endpoint did not become ready");
  ZuCHECK(server.listening() &&
      !server.endpointDiag().failures &&
      server.local().port(),
    "runtime server endpoint diagnostics mismatch");

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

  auto clientBidi = clientLink->stream(Zquic::StreamType::Bidi);
  auto clientUni = clientLink->stream(Zquic::StreamType::Uni);
  ZuCHECK(clientLink->send(clientBidi, "client-bidi") &&
      clientLink->send(clientUni, "client-uni"),
    "runtime client stream send failed");
  auto serverBidi = serverLink->stream(Zquic::StreamType::Bidi);
  auto serverUni = serverLink->stream(Zquic::StreamType::Uni);
  ZuCHECK(serverLink->send(serverBidi, "server-bidi") &&
      serverLink->send(serverUni, "server-uni"),
    "runtime server stream send failed");
  ZuCHECK(waitUntil([&clientLink, &serverLink]() {
      return serverLink->runtimeDiag().streamBytesRx == 21 &&
	clientLink->runtimeDiag().streamBytesRx == 21;
    }), "runtime protected stream bytes did not arrive");
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
      serverRxBidi->processed == 1 &&
      serverRxUni && serverRxUni->link() == serverLink.ptr() &&
      serverRxUni->processed == 1 &&
      clientRxBidi && clientRxBidi->link() == clientLink.ptr() &&
      clientRxBidi->processed == 1 &&
      clientRxUni && clientRxUni->link() == clientLink.ptr() &&
      clientRxUni->processed == 1 &&
      serverLink->streamedCount == 2 &&
      clientLink->streamedCount == 2,
    "runtime stream objects did not process received STREAM frames");

  clientLink->disconnect();
  server.close();
  ZuCHECK(waitUntil([&clientLink]() {
      return !clientLink->cxn() && clientLink->disconnectedCount == 1;
    }) && !server.connected(),
    "runtime endpoints remained connected after close");

  clientLink = nullptr;
  client.final();
  server.final();
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
  ZuCHECK(server.listen(), "multi runtime server listen failed");
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

  ZmRef<RuntimeClient::Link> c0 = new RuntimeClient::Link{&client};
  ZmRef<RuntimeClient::Link> c1 = new RuntimeClient::Link{&client};
  c0->connect(Zquic::Host{"127.0.0.1"}, server.local().port());
  c1->connect(Zquic::Host{"127.0.0.1"}, server.local().port());

  ZmRef<RuntimeServer::Link> s0;
  ZmRef<RuntimeServer::Link> s1;
  bool established = waitUntil([&server, &c0, &c1, &s0, &s1]() {
      if (!s0) s0 = server.link(0);
      if (!s1) s1 = server.link(1);
      return server.acceptedCount == 2 &&
	s0 && s1 && s0.ptr() != s1.ptr() &&
	c0->established() && c1->established() &&
	s0->established() && s1->established();
    });
  if (!established) {
    dumpRuntimeDiag("client0", c0->runtimeDiag(), c0->crypto().diag());
    dumpRuntimeDiag("client1", c1->runtimeDiag(), c1->crypto().diag());
    if (s0) dumpRuntimeDiag("server0", s0->runtimeDiag(), s0->crypto().diag());
    if (s1) dumpRuntimeDiag("server1", s1->runtimeDiag(), s1->crypto().diag());
  }
  ZuCHECK(established, "multi runtime connections did not establish");

  auto c0s = c0->stream(Zquic::StreamType::Bidi);
  auto c1s = c1->stream(Zquic::StreamType::Bidi);
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
  s0->close();
  ZuCHECK(waitUntil([&s0]() {
      return s0->closed() && !s0->established();
    }), "multi runtime server link did not close logically");

  auto c0Stale = c0->stream(Zquic::StreamType::Bidi);
  auto c1Live = c1->stream(Zquic::StreamType::Bidi);
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

  c0->disconnect();
  c1->disconnect();
  server.close();
  ZuCHECK(waitUntil([&c0, &c1]() {
      return !c0->cxn() && !c1->cxn();
    }) && !server.connected(),
    "multi runtime endpoints remained connected after close");

  c0 = nullptr;
  c1 = nullptr;
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
}
