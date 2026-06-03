//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZtString.hh>
#include <zlib/Zquic.hh>

#include "ZquicH3Lite.hh"
#include "ZquicInteropTest.hh"

using namespace ZuTestUtil;

namespace {

using Zquic::Test::CaddyProcess;
using Zquic::Test::TempDir;
using Zquic::Test::cspan;
using Zquic::Test::haveCaddy;
using Zquic::Test::haveCurlH3;
using Zquic::Test::loopbackPort;
using Zquic::Test::printFile;
using Zquic::Test::runCurlH3;
using Zquic::Test::waitCaddyReady;
using Zquic::Test::waitUntil;
using Zquic::Test::writeCaddyfile;

struct RuntimeClient : public Zquic::Client<RuntimeClient> { };
struct RuntimeServer : public Zquic::Server<RuntimeServer> { };

ZuCSpan bytesCSpan_(const Zquic::H3Lite::Bytes &b)
{
  return ZuCSpan{reinterpret_cast<const char *>(b.data()), b.length()};
}

bool writeControlStream_(Zquic::H3Lite::Bytes &out)
{
  out.length(0);
  uint8_t t[8];
  int n = Zquic::VarInt::encode(t, sizeof(t), 0);
  if (n < 0) return false;
  for (int i = 0; i < n; ++i) out.push(t[i]);

  Zquic::H3Lite::Bytes settings;
  if (Zquic::H3Lite::MessageCodec::writeSettings(settings) < 0)
    return false;
  for (unsigned i = 0; i < settings.length(); ++i) out.push(settings[i]);
  return true;
}

struct H3RuntimeClient : public Zquic::Client<H3RuntimeClient> {
  void zquicStream(uint64_t streamID, uint64_t offset, ZuCSpan payload, bool)
  {
    if (streamID != 0) return;
    uint64_t seen = responseOffset;
    if (offset < seen) return;
    if (offset != seen) {
      responseErrors = 1;
      return;
    }
    responseOffset = seen + payload.length();
    if (h3.consumeResponse(payload, response) < 0) {
      static const char hex[] = "0123456789abcdef";
      std::cout << "# H3Lite response decode failed streamID=" << streamID <<
	" offset=" << offset << " len=" << payload.length() <<
	" payload=";
      for (unsigned i = 0; i < payload.length() && i < 160; ++i) {
	uint8_t b = uint8_t(payload[i]);
	std::cout << hex[b >> 4] << hex[b & 0x0f];
      }
      if (payload.length() > 160) std::cout << "...";
      std::cout << '\n';
      responseErrors = 1;
      return;
    }
    responseSeen = 1;
  }

  Zquic::H3Lite::Client	h3;
  Zquic::H3Lite::Response response;
  ZmAtomic<unsigned>	responseSeen = 0;
  ZmAtomic<unsigned>	responseErrors = 0;
  ZmAtomic<uint64_t>	responseOffset = 0;
};

struct H3RuntimeServer : public Zquic::Server<H3RuntimeServer> {
  void zquicStream(uint64_t streamID, uint64_t, ZuCSpan payload, bool)
  {
    ++streamsSeen;
    lastStreamID = streamID;
    if (!(streamID & 3)) {
      ++requestStreamsSeen;
      lastRequestStreamID = streamID;
    }
    if (streamID != 0) return;
    Zquic::H3Lite::Bytes response;
    if (h3.respond(payload, response, "zquic-h3-ok") < 0) {
      static const char hex[] = "0123456789abcdef";
      std::cout << "# H3Lite request decode failed streamID=" << streamID <<
	" len=" << payload.length() << " payload=";
      for (unsigned i = 0; i < payload.length() && i < 160; ++i) {
	uint8_t b = uint8_t(payload[i]);
	std::cout << hex[b >> 4] << hex[b & 0x0f];
      }
      if (payload.length() > 160) std::cout << "...";
      std::cout << '\n';
      requestErrors = 1;
      return;
    }
    if (!sendStream(streamID, bytesCSpan_(response))) {
      requestErrors = 1;
      return;
    }
    requestSeen = 1;
  }

  Zquic::H3Lite::Server	h3;
  ZmAtomic<unsigned>	requestSeen = 0;
  ZmAtomic<unsigned>	requestErrors = 0;
  ZmAtomic<unsigned>	streamsSeen = 0;
  ZmAtomic<unsigned>	requestStreamsSeen = 0;
  ZmAtomic<uint64_t>	lastStreamID = 0;
  ZmAtomic<uint64_t>	lastRequestStreamID = 0;
};

} // namespace

void testInteropPrerequisites()
{
  ZuTestScope(testInteropPrerequisites);

  ZuCHECK(haveCurlH3(),
    "curl with HTTP3/ngtcp2/nghttp3 is required for lightweight HTTP/3 interop tests");
  ZuCHECK(haveCaddy(),
    "caddy is required for lightweight HTTP/3 interop tests");
}

void testRuntimeTrafficGuard()
{
  ZuTestScope(testRuntimeTrafficGuard);

  TempDir temp;
  ZuCHECK(temp.init(), "H3 interop temporary TLS certificate generation failed");

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
  ZuCHECK(mxStarted, "H3 interop traffic guard multiplexer start failed");
  if (!mxStarted) return;

  RuntimeServer server;
  ZuCHECK(server.init(
      Zquic::ServerParams(&mx, "3", "4")
	.certPath(cspan(temp.certPath))
	.keyPath(cspan(temp.keyPath))
	.maxData(32768)
	.maxStreamData(8192)
	.maxStreamsBidi(8)
	.maxStreamsUni(8)
	.alpn(ZuSpan<ZuCSpan>{"h3"})),
    "H3 interop traffic guard server init failed");
  ZuCHECK(server.listen(ZiIP("127.0.0.1"), 0),
    "H3 interop traffic guard server listen failed");
  ZuCHECK(waitUntil([&server]() { return server.listening(); }),
    "H3 interop traffic guard server did not become ready");

  RuntimeClient client;
  ZuCHECK(client.init(
      Zquic::ClientParams(&mx, "3", "4")
	.caPath(cspan(temp.certPath))
	.maxData(32768)
	.maxStreamData(8192)
	.maxStreamsBidi(8)
	.maxStreamsUni(8)
	.alpn(ZuSpan<ZuCSpan>{"h3"})),
    "H3 interop traffic guard client init failed");
  ZuCHECK(client.connect(
      ZiIP("127.0.0.1"), 0, ZiIP("127.0.0.1"), server.local().port()),
    "H3 interop traffic guard client connect failed");
  ZuCHECK(waitUntil([&client]() { return client.ready(); }),
    "H3 interop traffic guard client did not become ready");
  ZuCHECK(waitUntil([&client, &server]() {
      return client.established() && server.established();
    }), "H3 interop traffic guard did not establish Zquic");
  ZuCHECK(client.crypto().negotiatedProtocol() == "h3" &&
      server.crypto().negotiatedProtocol() == "h3",
    "H3 interop traffic guard ALPN mismatch");
  ZuCHECK(client.sendBidi("h3-lite-request"),
    "H3 interop traffic guard stream send failed");
  ZuCHECK(waitUntil([&server]() {
      return server.runtimeDiag().streamBytesRx == 15;
    }), "H3 interop traffic guard did not deliver stream bytes");
  ZuCHECK(server.runtimeDiag().protectedPacketsRx &&
      client.runtimeDiag().protectedPacketsTx &&
      server.runtimeDiag().cryptoFramesRx &&
      client.runtimeDiag().cryptoFramesTx &&
      !server.runtimeDiag().packetParseErrors,
    "H3 interop traffic guard diagnostics mismatch");

  client.final();
  server.final();
  mx.stop();
}

void testRuntimeH3LiteRequestResponse()
{
  ZuTestScope(testRuntimeH3LiteRequestResponse);

  TempDir temp;
  ZuCHECK(temp.init(), "H3Lite runtime temporary TLS certificate failed");

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
  ZuCHECK(mxStarted, "H3Lite runtime multiplexer start failed");
  if (!mxStarted) return;

  H3RuntimeServer server;
  ZuCHECK(server.init(
      Zquic::ServerParams(&mx, "3", "4")
	.certPath(cspan(temp.certPath))
	.keyPath(cspan(temp.keyPath))
	.maxData(32768)
	.maxStreamData(8192)
	.maxStreamsBidi(8)
	.maxStreamsUni(8)
	.alpn(ZuSpan<ZuCSpan>{"h3"})),
    "H3Lite runtime server init failed");
  ZuCHECK(server.listen(ZiIP("127.0.0.1"), 0),
    "H3Lite runtime server listen failed");
  ZuCHECK(waitUntil([&server]() { return server.listening(); }),
    "H3Lite runtime server did not become ready");

  H3RuntimeClient client;
  ZuCHECK(client.init(
      Zquic::ClientParams(&mx, "3", "4")
	.caPath(cspan(temp.certPath))
	.maxData(32768)
	.maxStreamData(8192)
	.maxStreamsBidi(8)
	.maxStreamsUni(8)
	.alpn(ZuSpan<ZuCSpan>{"h3"})),
    "H3Lite runtime client init failed");
  ZuCHECK(client.connect(
      ZiIP("127.0.0.1"), 0, ZiIP("127.0.0.1"), server.local().port()),
    "H3Lite runtime client connect failed");
  ZuCHECK(waitUntil([&client]() { return client.ready(); }),
    "H3Lite runtime client did not become ready");
  ZuCHECK(waitUntil([&client, &server]() {
      return client.established() && server.established();
    }), "H3Lite runtime Zquic connection did not establish");

  Zquic::H3Lite::Bytes control;
  ZuCHECK(writeControlStream_(control), "H3Lite control stream encode failed");
  ZuCHECK(client.sendUni(bytesCSpan_(control)) &&
      server.sendUni(bytesCSpan_(control)),
    "H3Lite runtime control stream send failed");

  Zquic::H3Lite::Bytes request;
  ZuCHECK(client.h3.request(request, "localhost", "/zquic-runtime") > 0,
    "H3Lite runtime request encode failed");
  ZuCHECK(client.sendStream(0, bytesCSpan_(request)),
    "H3Lite runtime request stream send failed");

  ZuCHECK(waitUntil([&client, &server]() {
      return client.responseSeen && server.requestSeen;
    }), "H3Lite runtime response did not complete");
  ZuCHECK(!client.responseErrors && !server.requestErrors,
    "H3Lite runtime app callback errors");
  ZuCHECK(client.response.status == 200 &&
      client.response.body == "zquic-h3-ok",
    "H3Lite runtime response mismatch");
  ZuCHECK(client.runtimeDiag().streamFramesTx >= 2 &&
      client.runtimeDiag().streamFramesRx >= 1 &&
      server.runtimeDiag().streamFramesTx >= 2 &&
      server.runtimeDiag().streamFramesRx >= 2 &&
      !client.runtimeDiag().packetParseErrors &&
      !server.runtimeDiag().packetParseErrors,
    "H3Lite runtime Zquic diagnostics mismatch");

  client.final();
  server.final();
  mx.stop();
}

void testCurlZquicH3Server()
{
  ZuTestScope(testCurlZquicH3Server);

  TempDir temp;
  ZuCHECK(temp.init(), "curl->Zquic H3 temporary TLS certificate failed");

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
  ZuCHECK(mxStarted, "curl->Zquic H3 multiplexer start failed");
  if (!mxStarted) return;

  H3RuntimeServer server;
  ZuCHECK(server.init(
      Zquic::ServerParams(&mx, "3", "4")
	.certPath(cspan(temp.certPath))
	.keyPath(cspan(temp.keyPath))
	.maxData(32768)
	.maxStreamData(8192)
	.maxStreamsBidi(8)
	.maxStreamsUni(8)
	.alpn(ZuSpan<ZuCSpan>{"h3"})),
    "curl->Zquic H3 server init failed");
  ZuCHECK(server.listen(ZiIP("127.0.0.1"), 0),
    "curl->Zquic H3 server listen failed");
  ZuCHECK(waitUntil([&server]() { return server.listening(); }),
    "curl->Zquic H3 server did not become ready");

  bool curlOK = runCurlH3(
    temp, server.local().port(), "/zquic-runtime", "zquic-h3-ok");
  if (!curlOK || !server.requestSeen || server.requestErrors) {
    const auto &d = server.runtimeDiag();
    std::cout <<
      "# zquic server diag:"
      " datagramsRx=" << uint64_t(d.datagramsRx) <<
      " packetsRx=" << uint64_t(d.packetsRx) <<
      " initialRx=" << uint64_t(d.initialPacketsRx) <<
      " handshakeRx=" << uint64_t(d.handshakePacketsRx) <<
      " shortRx=" << uint64_t(d.shortPacketsRx) <<
      " protectedRx=" << uint64_t(d.protectedPacketsRx) <<
      " cryptoRx=" << uint64_t(d.cryptoFramesRx) <<
      " streamRx=" << uint64_t(d.streamFramesRx) <<
      " streamTx=" << uint64_t(d.streamFramesTx) <<
      " tlsFailures=" << uint64_t(d.tlsFailures) <<
      " protectionFailures=" << uint64_t(d.packetProtectionFailures) <<
      " parseErrors=" << uint64_t(d.packetParseErrors) <<
      " handshakeComplete=" << uint64_t(d.handshakeComplete) <<
      " handshakeDoneTx=" << uint64_t(d.handshakeDoneFramesTx) <<
      " appStreams=" << unsigned(server.streamsSeen) <<
      " appReqStreams=" << unsigned(server.requestStreamsSeen) <<
      " lastStreamID=" << uint64_t(server.lastStreamID) <<
      " lastReqStreamID=" << uint64_t(server.lastRequestStreamID) <<
      " requestErrors=" << unsigned(server.requestErrors) <<
      '\n';
  }
  ZuCHECK(curlOK, "curl HTTP/3 request to local Zquic H3 server failed");
  ZuCHECK(waitUntil([&server]() { return server.requestSeen; }),
    "curl->Zquic H3 request did not enter Zquic");
  ZuCHECK(!server.requestErrors &&
      server.runtimeDiag().protectedPacketsRx &&
      server.runtimeDiag().streamFramesRx &&
      server.runtimeDiag().streamFramesTx &&
      server.runtimeDiag().handshakeDoneFramesTx,
    "curl->Zquic H3 diagnostics mismatch");

  server.final();
  mx.stop();
}

void testZquicH3ClientCaddy()
{
  ZuTestScope(testZquicH3ClientCaddy);

  TempDir temp;
  ZuCHECK(temp.init(), "Zquic H3 client->Caddy temporary TLS certificate failed");

  unsigned port = loopbackPort();
  ZuCHECK(port, "Zquic H3 client->Caddy loopback port allocation failed");
  if (!port) return;

  auto caddyfile = temp.pathOf("Caddyfile");
  ZuCHECK(writeCaddyfile(
      caddyfile, port, temp.certPath, temp.keyPath,
      "/zquic-interop", "caddy-h3-ok"),
    "Zquic H3 client->Caddy Caddyfile generation failed");

  CaddyProcess caddy;
  ZuCHECK(caddy.start(temp, cspan(caddyfile)),
    "Zquic H3 client->Caddy start failed");
  ZuCHECK(waitCaddyReady(port, "/zquic-interop"),
    "Zquic H3 client->Caddy did not become ready");

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
  ZuCHECK(mxStarted, "Zquic H3 client->Caddy multiplexer start failed");
  if (!mxStarted) return;

  H3RuntimeClient client;
  ZuCHECK(client.init(
      Zquic::ClientParams(&mx, "3", "4")
	.caPath(cspan(temp.certPath))
	.maxData(32768)
	.maxStreamData(8192)
	.maxStreamsBidi(8)
	.maxStreamsUni(8)
	.alpn(ZuSpan<ZuCSpan>{"h3"})),
    "Zquic H3 client->Caddy client init failed");
  ZuCHECK(client.connect(
      ZiIP("127.0.0.1"), 0, ZiIP("127.0.0.1"), port),
    "Zquic H3 client->Caddy connect failed");
  ZuCHECK(waitUntil([&client]() { return client.ready(); }),
    "Zquic H3 client->Caddy client did not become ready");
  ZuCHECK(waitUntil([&client]() { return client.established(); }),
    "Zquic H3 client->Caddy QUIC connection did not establish");

  Zquic::H3Lite::Bytes control;
  ZuCHECK(writeControlStream_(control),
    "Zquic H3 client->Caddy control stream encode failed");
  ZuCHECK(client.sendUni(bytesCSpan_(control)),
    "Zquic H3 client->Caddy control stream send failed");

  Zquic::H3Lite::Bytes request;
  ZuCHECK(client.h3.request(request, "localhost", "/zquic-interop") > 0,
    "Zquic H3 client->Caddy request encode failed");
  ZuCHECK(client.sendStream(0, bytesCSpan_(request)),
    "Zquic H3 client->Caddy request stream send failed");

  ZuCHECK(waitUntil([&client]() { return client.responseSeen; }),
    "Zquic H3 client->Caddy response did not arrive");
  if (!client.responseSeen || client.responseErrors) {
    const auto &d = client.runtimeDiag();
    std::cout <<
      "# zquic client diag:"
      " datagramsRx=" << uint64_t(d.datagramsRx) <<
      " packetsRx=" << uint64_t(d.packetsRx) <<
      " initialRx=" << uint64_t(d.initialPacketsRx) <<
      " handshakeRx=" << uint64_t(d.handshakePacketsRx) <<
      " shortRx=" << uint64_t(d.shortPacketsRx) <<
      " protectedRx=" << uint64_t(d.protectedPacketsRx) <<
      " cryptoRx=" << uint64_t(d.cryptoFramesRx) <<
      " streamRx=" << uint64_t(d.streamFramesRx) <<
      " streamTx=" << uint64_t(d.streamFramesTx) <<
      " tlsFailures=" << uint64_t(d.tlsFailures) <<
      " protectionFailures=" << uint64_t(d.packetProtectionFailures) <<
      " parseErrors=" << uint64_t(d.packetParseErrors) <<
      " responseErrors=" << unsigned(client.responseErrors) <<
      '\n';
    printFile("caddy log", caddy.logPath);
  }
  ZuCHECK(!client.responseErrors &&
      client.response.status == 200 &&
      client.response.body == "caddy-h3-ok",
    "Zquic H3 client->Caddy response mismatch");
  bool diagOK = client.runtimeDiag().protectedPacketsRx &&
    client.runtimeDiag().streamFramesRx &&
    client.runtimeDiag().streamFramesTx &&
    client.runtimeDiag().handshakeDoneFramesRx;
  if (!diagOK) {
    const auto &d = client.runtimeDiag();
    std::cout <<
      "# zquic client final diag:"
      " datagramsRx=" << uint64_t(d.datagramsRx) <<
      " packetsRx=" << uint64_t(d.packetsRx) <<
      " initialRx=" << uint64_t(d.initialPacketsRx) <<
      " handshakeRx=" << uint64_t(d.handshakePacketsRx) <<
      " shortRx=" << uint64_t(d.shortPacketsRx) <<
      " protectedRx=" << uint64_t(d.protectedPacketsRx) <<
      " cryptoRx=" << uint64_t(d.cryptoFramesRx) <<
      " streamRx=" << uint64_t(d.streamFramesRx) <<
      " streamTx=" << uint64_t(d.streamFramesTx) <<
      " handshakeDoneRx=" << uint64_t(d.handshakeDoneFramesRx) <<
      " tlsFailures=" << uint64_t(d.tlsFailures) <<
      " protectionFailures=" << uint64_t(d.packetProtectionFailures) <<
      " parseErrors=" << uint64_t(d.packetParseErrors) <<
      '\n';
  }
  ZuCHECK(diagOK,
    "Zquic H3 client->Caddy diagnostics mismatch");

  client.final();
  mx.stop();
}

void testSettingsFrame()
{
  ZuTestScope(testSettingsFrame);

  Zquic::H3Lite::Bytes bytes;
  Zquic::H3Lite::Settings settings;
  settings.maxFieldSectionSize = 4096;
  ZuCHECK(Zquic::H3Lite::MessageCodec::writeSettings(bytes, settings) > 0,
    "H3Lite SETTINGS write failed");

  Zquic::H3Lite::Frame frame;
  unsigned used = 0;
  ZuCHECK(!Zquic::H3Lite::FrameCodec::parse(
    ZuCSpan{reinterpret_cast<const char *>(bytes.data()), bytes.length()},
    frame, used), "H3Lite SETTINGS parse failed");
  ZuCHECK(frame.type == Zquic::H3Lite::FrameType::Settings,
    "H3Lite SETTINGS type mismatch");
  ZuCHECK(used == bytes.length(), "H3Lite SETTINGS consumed length mismatch");
}

void testClientServerExchange()
{
  ZuTestScope(testClientServerExchange);

  Zquic::H3Lite::Client client;
  Zquic::H3Lite::Server server;
  Zquic::H3Lite::Bytes request, response;
  ZuCHECK(client.request(request, "localhost", "/ready") > 0,
    "H3Lite request encode failed");
  ZuCHECK(server.respond(
    ZuCSpan{reinterpret_cast<const char *>(request.data()), request.length()},
    response, "ok") > 0, "H3Lite server response failed");

  Zquic::H3Lite::Response r;
  ZuCHECK(!client.consumeResponse(
    ZuCSpan{reinterpret_cast<const char *>(response.data()), response.length()},
    r), "H3Lite client response consume failed");
  ZuCHECK(r.status == 200, "H3Lite status mismatch");
  ZuCHECK(r.body == "ok", "H3Lite body mismatch");

  Zquic::H3Lite::Bytes post;
  ZuCHECK(client.post(post, "localhost", "/echo", "ping") > 0,
    "H3Lite POST encode failed");
  Zquic::H3Lite::Request parsed;
  ZuCHECK(!server.consumeRequest(
      ZuCSpan{reinterpret_cast<const char *>(post.data()), post.length()},
      parsed) &&
      parsed.method == "POST" &&
      parsed.path == "/echo" &&
      parsed.body == "ping",
    "H3Lite POST request body parse failed");
}

static void appendBytes_(Zquic::H3Lite::Bytes &out, const Zquic::H3Lite::Bytes &in)
{
  for (unsigned i = 0; i < in.length(); ++i) out.push(in[i]);
}

void testGracefulClose()
{
  ZuTestScope(testGracefulClose);

  Zquic::H3Lite::Client client;
  Zquic::H3Lite::Server server;
  Zquic::H3Lite::Bytes close;
  ZuCHECK(server.gracefulClose(close, 8) > 0,
    "H3Lite GOAWAY encode failed");

  uint64_t cutoff = 0;
  ZuCHECK(!client.consumeGracefulClose(
      ZuCSpan{reinterpret_cast<const char *>(close.data()), close.length()},
      cutoff) &&
      cutoff == 8,
    "H3Lite GOAWAY decode failed");

  ZuCHECK(Zquic::H3Lite::MessageCodec::requestAccepted(4, cutoff) &&
      !Zquic::H3Lite::MessageCodec::requestAccepted(8, cutoff) &&
      !Zquic::H3Lite::MessageCodec::requestAccepted(12, cutoff) &&
      !Zquic::H3Lite::MessageCodec::requestAccepted(1, cutoff) &&
      !Zquic::H3Lite::MessageCodec::requestAccepted(2, cutoff),
    "H3Lite GOAWAY request admission mismatch");

  Zquic::H3Lite::Bytes lower;
  ZuCHECK(server.gracefulClose(lower, 4) > 0,
    "H3Lite second GOAWAY encode failed");
  appendBytes_(close, lower);
  cutoff = 0;
  ZuCHECK(!client.consumeGracefulClose(
      ZuCSpan{reinterpret_cast<const char *>(close.data()), close.length()},
      cutoff) &&
      cutoff == 4,
    "H3Lite decreasing GOAWAY sequence rejected");

  Zquic::H3Lite::Bytes higher;
  ZuCHECK(server.gracefulClose(higher, 16) > 0,
    "H3Lite increasing GOAWAY setup failed");
  appendBytes_(lower, higher);
  ZuCHECK(client.consumeGracefulClose(
      ZuCSpan{reinterpret_cast<const char *>(lower.data()), lower.length()},
      cutoff) < 0,
    "H3Lite increasing GOAWAY sequence accepted");

  Zquic::H3Lite::Bytes response;
  ZuCHECK(Zquic::H3Lite::MessageCodec::writeResponse(
      response, 200, "ok") > 0,
    "H3Lite invalid GOAWAY setup failed");
  ZuCHECK(server.consumeGracefulClose(
      ZuCSpan{reinterpret_cast<const char *>(response.data()),
	response.length()}, cutoff) < 0,
    "H3Lite GOAWAY parser accepted request-stream frames");
}

void testFieldSectionPrefix()
{
  ZuTestScope(testFieldSectionPrefix);

  Zquic::H3Lite::Header headers[] = {
    { ":method", "GET" },
    { ":authority", "localhost" },
    { "x-test", "v" }
  };
  Zquic::H3Lite::Bytes fields;
  ZuCHECK(Zquic::H3Lite::FieldCodec::encode(
      fields, ZuSpan<Zquic::H3Lite::Header>{headers, 3}) > 0,
    "H3Lite field encode failed");
  ZuCHECK(fields.length() > 3 && fields[0] == 0 && fields[1] == 0 &&
      (fields[2] & 0xc0) == 0xc0,
    "H3Lite field encode did not use QPACK static indexed field");
  unsigned n = 0;
  bool sawAuthority = false, sawLiteral = false;
  ZuCHECK(Zquic::H3Lite::FieldCodec::decode(
      ZuCSpan{reinterpret_cast<const char *>(fields.data()), fields.length()},
      [&](Zquic::H3Lite::Header h) {
	if (h.name == ":method" && h.value == "GET") ++n;
	if (h.name == ":authority" && h.value == "localhost")
	  sawAuthority = true;
	if (h.name == "x-test" && h.value == "v") sawLiteral = true;
      }) == int(fields.length()) &&
      n == 1 && sawAuthority && sawLiteral,
    "H3Lite static/literal field-section decode failed");

  uint8_t badRequiredInsertCount[] = { 0x01, 0x00, 0x00 };
  ZuCHECK(Zquic::H3Lite::FieldCodec::decode(
      ZuCSpan{
	reinterpret_cast<const char *>(badRequiredInsertCount),
	sizeof(badRequiredInsertCount)},
      [](Zquic::H3Lite::Header) { }) < 0,
    "H3Lite accepted nonzero required insert count");
  uint8_t badBase[] = { 0x00, 0x01, 0x00 };
  ZuCHECK(Zquic::H3Lite::FieldCodec::decode(
      ZuCSpan{reinterpret_cast<const char *>(badBase), sizeof(badBase)},
      [](Zquic::H3Lite::Header) { }) < 0,
    "H3Lite accepted nonzero field-section base");

  uint8_t badHuffmanName[] = { 0x00, 0x00, 0x29, 0x00, 0x00 };
  ZuCHECK(Zquic::H3Lite::FieldCodec::decode(
      ZuCSpan{reinterpret_cast<const char *>(badHuffmanName),
	sizeof(badHuffmanName)},
      [](Zquic::H3Lite::Header) { }) < 0,
    "H3Lite accepted malformed Huffman-coded literal name");
  uint8_t badDynamicIndexed[] = { 0x00, 0x00, 0x80 };
  ZuCHECK(Zquic::H3Lite::FieldCodec::decode(
      ZuCSpan{reinterpret_cast<const char *>(badDynamicIndexed),
	sizeof(badDynamicIndexed)},
      [](Zquic::H3Lite::Header) { }) < 0,
    "H3Lite accepted dynamic indexed field");
  uint8_t badDynamicNameRef[] = { 0x00, 0x00, 0x40 };
  ZuCHECK(Zquic::H3Lite::FieldCodec::decode(
      ZuCSpan{reinterpret_cast<const char *>(badDynamicNameRef),
	sizeof(badDynamicNameRef)},
      [](Zquic::H3Lite::Header) { }) < 0,
    "H3Lite accepted dynamic name-reference field");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testInteropPrerequisites);
  ZuTestCall(testRuntimeTrafficGuard);
  ZuTestCall(testRuntimeH3LiteRequestResponse);
  ZuTestCall(testCurlZquicH3Server);
  ZuTestCall(testZquicH3ClientCaddy);
  ZuTestCall(testSettingsFrame);
  ZuTestCall(testClientServerExchange);
  ZuTestCall(testGracefulClose);
  ZuTestCall(testFieldSectionPrefix);
}
