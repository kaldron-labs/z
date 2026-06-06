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

struct RuntimeClient : public Zquic::Client<RuntimeClient> { };
struct RuntimeServer : public Zquic::Server<RuntimeServer> { };

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
    " packetsTx=" << uint64_t(d.packetsTx) <<
    " packetsRx=" << uint64_t(d.packetsRx) <<
    " initialTx=" << uint64_t(d.initialPacketsTx) <<
    " initialRx=" << uint64_t(d.initialPacketsRx) <<
    " handshakeTx=" << uint64_t(d.handshakePacketsTx) <<
    " handshakeRx=" << uint64_t(d.handshakePacketsRx) <<
    " shortTx=" << uint64_t(d.shortPacketsTx) <<
    " shortRx=" << uint64_t(d.shortPacketsRx) <<
    " framesRx=" << uint64_t(d.framesRx) <<
    " cryptoFramesTx=" << uint64_t(d.cryptoFramesTx) <<
    " cryptoFramesRx=" << uint64_t(d.cryptoFramesRx) <<
    " cryptoBytesTx=" << uint64_t(d.cryptoBytesTx) <<
    " cryptoBytesRx=" << uint64_t(d.cryptoBytesRx) <<
    " ackTx=" << uint64_t(d.ackFramesTx) <<
    " ackRx=" << uint64_t(d.ackFramesRx) <<
    " handshakeDoneTx=" << uint64_t(d.handshakeDoneFramesTx) <<
    " handshakeDoneRx=" << uint64_t(d.handshakeDoneFramesRx) <<
    " packetParseErrors=" << uint64_t(d.packetParseErrors) <<
    " packetProtectionFailures=" << uint64_t(d.packetProtectionFailures) <<
    " tlsFailures=" << uint64_t(d.tlsFailures) <<
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
  ZuCHECK(!uninitialized.connect(
      ZiIP("127.0.0.1"), 0, ZiIP("127.0.0.1"), 4433),
    "uninitialized runtime client opened an endpoint");

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
  ZuCHECK(server.listen(ZiIP("127.0.0.1"), 0),
    "runtime server listen failed");
  ZuCHECK(waitUntil([&server]() { return server.listening(); }),
    "runtime server endpoint did not become ready");
  ZuCHECK(server.runtimeDiag().endpointReady == 1 &&
      !server.runtimeDiag().endpointFailures &&
      server.endpointDiag().packetRxBufAllocs == 1 &&
      !server.endpointDiag().packetTxBufAllocs &&
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
  ZuCHECK(client.connect(
      ZiIP("127.0.0.1"), 0, ZiIP("127.0.0.1"), server.local().port()),
    "runtime client connect failed");
  ZuCHECK(waitUntil([&client]() { return client.ready(); }),
    "runtime client endpoint did not become ready");
  ZuCHECK(client.runtimeDiag().endpointReady == 1 &&
      !client.runtimeDiag().endpointFailures &&
      client.endpointDiag().packetRxBufAllocs >= 1,
    "runtime client endpoint diagnostics mismatch");

  bool established = waitUntil([&client, &server]() {
      return client.established() && server.established();
    });
  if (!established) {
    dumpRuntimeDiag("client", client.runtimeDiag(), client.crypto().diag());
    dumpRuntimeDiag("server", server.runtimeDiag(), server.crypto().diag());
  }
  ZuCHECK(established, "runtime UDP QUIC handshake did not establish");
  ZuCHECK(client.crypto().oneRTTReady() && server.crypto().oneRTTReady() &&
      client.crypto().negotiatedProtocol() == "h3" &&
      server.crypto().negotiatedProtocol() == "h3",
    "runtime TLS/ALPN state mismatch");
  ZuCHECK(client.runtimeDiag().handshakeComplete == 1,
    "runtime client handshake completion diagnostics mismatch");
  ZuCHECK(server.runtimeDiag().handshakeComplete == 1,
    "runtime server handshake completion diagnostics mismatch");
  ZuCHECK(client.runtimeDiag().protectedPacketsTx &&
      client.runtimeDiag().protectedPacketsRx &&
      server.runtimeDiag().protectedPacketsTx &&
      server.runtimeDiag().protectedPacketsRx,
    "runtime protected packet diagnostics mismatch");
  ZuCHECK(client.runtimeDiag().cryptoFramesTx &&
      client.runtimeDiag().cryptoFramesRx &&
      server.runtimeDiag().cryptoFramesTx &&
      server.runtimeDiag().cryptoFramesRx,
    "runtime CRYPTO frame diagnostics mismatch");
  ZuCHECK(server.runtimeDiag().handshakeDoneFramesTx == 1,
    "runtime server HANDSHAKE_DONE Tx diagnostics mismatch");
  ZuCHECK(client.runtimeDiag().handshakeDoneFramesRx == 1,
    "runtime client HANDSHAKE_DONE Rx diagnostics mismatch");
  ZuCHECK(!client.runtimeDiag().packetParseErrors,
    "runtime client packet parse diagnostics mismatch");
  ZuCHECK(!server.runtimeDiag().packetParseErrors,
    "runtime server packet parse diagnostics mismatch");

  ZuCHECK(client.sendBidi("client-bidi") && client.sendUni("client-uni"),
    "runtime client stream send failed");
  ZuCHECK(server.sendBidi("server-bidi") && server.sendUni("server-uni"),
    "runtime server stream send failed");
  ZuCHECK(waitUntil([&client, &server]() {
      return server.runtimeDiag().streamBytesRx == 21 &&
	client.runtimeDiag().streamBytesRx == 21;
    }), "runtime protected stream bytes did not arrive");
  ZuCHECK(client.runtimeDiag().streamFramesTx == 2 &&
      server.runtimeDiag().streamFramesTx == 2 &&
      client.runtimeDiag().streamFramesRx == 2 &&
      server.runtimeDiag().streamFramesRx == 2 &&
      client.runtimeDiag().shortPacketsTx >= 2 &&
      server.runtimeDiag().shortPacketsTx >= 2,
    "runtime stream diagnostics mismatch");

  client.close();
  server.close();
  ZuCHECK(!client.connected() && !server.connected(),
    "runtime endpoints remained connected after close");

  client.final();
  server.final();
  mx.stop();
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testRuntimeEndpointOpen);
}
