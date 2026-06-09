//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zhttp3.hh>

#include "ZhttpCaddyInterop.hh"

using namespace ZuTestUtil;

namespace {

using Zhttp::Test::CaddyProcess;
using Zhttp::Test::TempDir;
using Zhttp::Test::cspan;
using Zhttp::Test::printFile;
using Zhttp::Test::runCurlH3;
using Zhttp::Test::waitCaddyReady;
using Zhttp::Test::waitUntil;
using Zhttp::Test::writeCaddyfile;

struct RuntimeClient : public Zquic::Client<RuntimeClient> {
  struct Link;
  struct Stream;
};
struct RuntimeServer : public Zquic::Server<RuntimeServer> {
  struct Link;
  struct Stream;

  ZmRef<Link> link();
  ZmRef<Link> accepted(const Zquic::InitialInfo &);
  ZiIP localIP() const { return ZiIP("127.0.0.1"); }

  ZmRef<Link> link_;
  ZmAtomic<unsigned> acceptedCount = 0;
};
struct H3App { };

ZuCSpan headerBytes_(const Zhttp::H3::HeaderBytes &b)
{
  return ZuCSpan{reinterpret_cast<const char *>(b.data()), b.length()};
}

void appendHeaderBytes_(
  Zhttp::H3::HeaderBytes &out, const Zhttp::H3::HeaderBytes &in)
{
  for (unsigned i = 0; i < in.length(); ++i) out.push(in[i]);
}

bool writeControlStream_(
  Zhttp::H3::HeaderBytes &out, Zhttp::H3::Connection &h3)
{
  out.length(0);
  uint8_t t[8];
  int n = Zquic::VarInt::encode(t, sizeof(t), 0);
  if (n < 0) return false;
  for (int i = 0; i < n; ++i) out.push(t[i]);

  Zhttp::H3::HeaderBytes settings;
  if (h3.encodeSettings(settings) < 0) return false;
  appendHeaderBytes_(out, settings);
  return true;
}

struct RuntimeClient::Stream :
  public Zquic::CliStream<RuntimeClient::Link, RuntimeClient::Stream> {
  using Base =
    Zquic::CliStream<RuntimeClient::Link, RuntimeClient::Stream>;
  using Base::Base;

  int process(Zquic::RxStream &) { return 0; }
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
  void streamed(ZmRef<Stream>) { }

  ZmAtomic<unsigned> connectedCount = 0;
  ZmAtomic<unsigned> disconnectedCount = 0;
  ZmAtomic<unsigned> connectFailures = 0;
};

struct RuntimeServer::Stream :
  public Zquic::SrvStream<RuntimeServer::Link, RuntimeServer::Stream> {
  using Base =
    Zquic::SrvStream<RuntimeServer::Link, RuntimeServer::Stream>;
  using Base::Base;

  int process(Zquic::RxStream &) { return 0; }
};

struct RuntimeServer::Link :
  public Zquic::SrvLink<RuntimeServer, RuntimeServer::Link,
    RuntimeServer::Stream> {
  using Base = Zquic::SrvLink<RuntimeServer, RuntimeServer::Link,
    RuntimeServer::Stream>;
  using Base::Base;

  Link(RuntimeServer *app) : Base{app} { }

  void connected(const char *, int) { ++connectedCount; }
  void disconnected() { ++disconnectedCount; }
  void streamed(ZmRef<Stream>) { }

  ZmAtomic<unsigned> connectedCount = 0;
  ZmAtomic<unsigned> disconnectedCount = 0;
};

ZmRef<RuntimeServer::Link> RuntimeServer::link()
{
  return link_;
}

ZmRef<RuntimeServer::Link> RuntimeServer::accepted(
  const Zquic::InitialInfo &)
{
  link_ = new Link{this};
  ++acceptedCount;
  return link_;
}

struct ZhttpRuntimeClient : public Zquic::Client<ZhttpRuntimeClient> {
  struct Link;
  struct Stream;

  Zhttp::H3::Connection h3;
  Zhttp::H3::DecodedResponse response;
  ZtString<>	body;
  ZmAtomic<unsigned>	responseHeadersSeen = 0;
  ZmAtomic<unsigned>	responseSeen = 0;
  ZmAtomic<unsigned>	responseErrors = 0;
  ZmAtomic<uint64_t>	responseOffset = 0;
};

struct ZhttpRuntimeClient::Stream :
  public Zquic::CliStream<ZhttpRuntimeClient::Link,
    ZhttpRuntimeClient::Stream> {
  using Base = Zquic::CliStream<ZhttpRuntimeClient::Link,
    ZhttpRuntimeClient::Stream>;
  using Base::Base;

  int process(Zquic::RxStream &) { return 0; }
};

struct ZhttpRuntimeClient::Link :
  public Zquic::CliLink<ZhttpRuntimeClient, ZhttpRuntimeClient::Link,
    ZhttpRuntimeClient::Stream> {
  using Base = Zquic::CliLink<ZhttpRuntimeClient,
    ZhttpRuntimeClient::Link, ZhttpRuntimeClient::Stream>;
  using Base::Base;

  Link(ZhttpRuntimeClient *app) : Base{app} { }

  void connected(const char *, int) { ++connectedCount; }
  void disconnected() { ++disconnectedCount; }
  void connectFailed(bool) { ++connectFailures; }
  void streamed(ZmRef<Stream>) { }

  void streamFrame(uint64_t streamID, uint64_t offset, ZuCSpan payload, bool)
  {
    auto app_ = this->app();
    if (streamID != 0) return;
    uint64_t seen = app_->responseOffset;
    if (offset < seen) return;
    if (offset != seen) {
      app_->responseErrors = 1;
      return;
    }
    app_->responseOffset = seen + payload.length();
    if (!payload.length()) return;
    unsigned o = 0;
    while (o < payload.length()) {
      Zhttp::H3::H3Frame frame;
      unsigned used = 0;
      if (Zhttp::H3::H3FrameCodec::parse(
	    ZuCSpan{payload.data() + o, payload.length() - o},
	    frame, used) < 0 || !used) {
	app_->responseErrors = 1;
	return;
      }
      o += used;
      if (frame.type == Zhttp::H3::H3FrameType::Headers) {
	if (app_->h3.decodeResponse(frame, app_->response) < 0) {
	  app_->responseErrors = 1;
	  return;
	}
	app_->responseHeadersSeen = 1;
      } else if (frame.type == Zhttp::H3::H3FrameType::Data) {
	ZuCSpan data;
	if (app_->h3.decodeData(frame, data) < 0) {
	  app_->responseErrors = 1;
	  return;
	}
	app_->body << data;
      }
    }
    app_->responseSeen = 1;
  }

  ZmAtomic<unsigned> connectedCount = 0;
  ZmAtomic<unsigned> disconnectedCount = 0;
  ZmAtomic<unsigned> connectFailures = 0;
};

struct ZhttpRuntimeServer : public Zquic::Server<ZhttpRuntimeServer> {
  struct Link;
  struct Stream;

  ZmRef<Link> link();
  ZmRef<Link> accepted(const Zquic::InitialInfo &);
  ZiIP localIP() const { return ZiIP("127.0.0.1"); }

  Zhttp::H3::Connection h3;
  Zhttp::H3::DecodedRequest request;
  ZtString<>	body;
  ZmRef<Link>	link_;
  ZmAtomic<unsigned>	acceptedCount = 0;
  ZmAtomic<unsigned>	requestSeen = 0;
  ZmAtomic<unsigned>	responseSent = 0;
  ZmAtomic<unsigned>	requestErrors = 0;
  ZmAtomic<unsigned>	streamsSeen = 0;
  ZmAtomic<unsigned>	requestStreamsSeen = 0;
  ZmAtomic<uint64_t>	lastStreamID = 0;
  ZmAtomic<uint64_t>	lastRequestStreamID = 0;
  ZmAtomic<uint64_t>	requestOffset = 0;
};

struct ZhttpRuntimeServer::Stream :
  public Zquic::SrvStream<ZhttpRuntimeServer::Link,
    ZhttpRuntimeServer::Stream> {
  using Base = Zquic::SrvStream<ZhttpRuntimeServer::Link,
    ZhttpRuntimeServer::Stream>;
  using Base::Base;

  int process(Zquic::RxStream &) { return 0; }
};

struct ZhttpRuntimeServer::Link :
  public Zquic::SrvLink<ZhttpRuntimeServer, ZhttpRuntimeServer::Link,
    ZhttpRuntimeServer::Stream> {
  using Base = Zquic::SrvLink<ZhttpRuntimeServer,
    ZhttpRuntimeServer::Link, ZhttpRuntimeServer::Stream>;
  using Base::Base;

  Link(ZhttpRuntimeServer *app) : Base{app} { }

  void connected(const char *, int) { ++connectedCount; }
  void disconnected() { ++disconnectedCount; }
  void streamed(ZmRef<Stream>) { }

  void streamFrame(uint64_t streamID, uint64_t offset, ZuCSpan payload, bool)
  {
    auto app_ = this->app();
    ++app_->streamsSeen;
    app_->lastStreamID = streamID;
    if (!(streamID & 3)) {
      ++app_->requestStreamsSeen;
      app_->lastRequestStreamID = streamID;
    }
    if (streamID != 0) return;
    uint64_t seen = app_->requestOffset;
    if (offset < seen) return;
    if (offset != seen) {
      app_->requestErrors = 1;
      return;
    }
    app_->requestOffset = seen + payload.length();
    if (!payload.length() || app_->responseSent) return;
    unsigned o = 0;
    while (o < payload.length()) {
      Zhttp::H3::H3Frame frame;
      unsigned used = 0;
      if (Zhttp::H3::H3FrameCodec::parse(
	    ZuCSpan{payload.data() + o, payload.length() - o},
	    frame, used) < 0 || !used) {
	app_->requestErrors = 1;
	return;
      }
      o += used;
      if (frame.type == Zhttp::H3::H3FrameType::Headers) {
	if (app_->h3.decodeRequest(frame, app_->request) < 0) {
	  app_->requestErrors = 1;
	  return;
	}
	app_->requestSeen = 1;
      } else if (frame.type == Zhttp::H3::H3FrameType::Data) {
	ZuCSpan data;
	if (app_->h3.decodeData(frame, data) < 0) {
	  app_->requestErrors = 1;
	  return;
	}
	app_->body << data;
      }
    }

    Zhttp::H3::ResponseParams resp;
    resp.status = 200;
    Zhttp::H3::HeaderBytes response;
    if (app_->h3.encodeResponse(response, resp) < 0) {
      app_->requestErrors = 1;
      return;
    }
    Zhttp::H3::HeaderBytes data;
    if (app_->h3.encodeData(data, "zhttp-h3-ok") < 0) {
      app_->requestErrors = 1;
      return;
    }
    appendHeaderBytes_(response, data);
    auto stream = this->findStream(int64_t(streamID));
    if (!stream || !this->send(stream, headerBytes_(response))) {
      app_->requestErrors = 1;
      return;
    }
    app_->responseSent = 1;
  }

  ZmAtomic<unsigned> connectedCount = 0;
  ZmAtomic<unsigned> disconnectedCount = 0;
};

ZmRef<ZhttpRuntimeServer::Link> ZhttpRuntimeServer::link()
{
  return link_;
}

ZmRef<ZhttpRuntimeServer::Link> ZhttpRuntimeServer::accepted(
  const Zquic::InitialInfo &)
{
  link_ = new Link{this};
  ++acceptedCount;
  return link_;
}

} // namespace

void testInteropPrerequisites()
{
  ZuTestScope(testInteropPrerequisites);

  ZuCHECK(Zhttp::Test::haveCurlH3(),
    "curl with HTTP3/ngtcp2/nghttp3 is required for HTTP/3 interop tests");
  ZuCHECK(Zhttp::Test::haveCaddy(),
    "caddy is required for HTTP/3 interop tests");
}

void testCaddyH3Fixture()
{
  ZuTestScope(testCaddyH3Fixture);

  ZuCHECK(
    Zhttp::Test::validateCaddyConfig("caddy-h3.json") ||
    Zhttp::Test::validateCaddyConfig("zhttp/test/caddy-h3.json"),
    "Caddy HTTP/3 fixture failed validation");
}

void testCurlCaddyHttp3()
{
  ZuTestScope(testCurlCaddyHttp3);

  ZuCHECK(Zhttp::Test::runCaddyCurl(
      "\"h1\", \"h2\", \"h3\"", "--http3-only", "3", "zhttp-h3-ok"),
    "curl HTTP/3 request to local Caddy failed");
}

void testZhttpRuntimeTrafficGuard()
{
  ZuTestScope(testZhttpRuntimeTrafficGuard);

  Zhttp::Test::TempDir temp;
  ZuCHECK(temp.init("Zhttp3InteropRuntime"),
    "Zhttp H3 runtime guard temporary directory failed");
  ZtString<> certPath;
  ZtString<> keyPath;
  ZuCHECK(Zhttp::Test::writeSelfSignedLocalhostCert(
      temp, certPath, keyPath),
    "Zhttp H3 runtime guard certificate generation failed");

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
  ZuCHECK(mxStarted, "Zhttp H3 runtime guard multiplexer start failed");
  if (!mxStarted) return;

  RuntimeServer zquicServer;
  ZuCHECK(zquicServer.init(
      Zquic::ServerParams(&mx, "3", "4")
	.certPath(cspan(certPath))
	.keyPath(cspan(keyPath))
	.maxData(32768)
	.maxStreamData(8192)
	.maxStreamsBidi(8)
	.maxStreamsUni(8)
	.alpn(ZuSpan<ZuCSpan>{"h3"})),
    "Zhttp H3 runtime guard server init failed");
  ZuCHECK(zquicServer.listen(),
    "Zhttp H3 runtime guard server listen failed");
  ZuCHECK(waitUntil([&zquicServer]() { return zquicServer.listening(); }),
    "Zhttp H3 runtime guard server did not become ready");

  RuntimeClient zquicClient;
  ZuCHECK(zquicClient.init(
      Zquic::ClientParams(&mx, "3", "4")
	.caPath(cspan(certPath))
	.maxData(32768)
	.maxStreamData(8192)
	.maxStreamsBidi(8)
	.maxStreamsUni(8)
	.alpn(ZuSpan<ZuCSpan>{"h3"})),
    "Zhttp H3 runtime guard client init failed");
  ZmRef<RuntimeClient::Link> zquicClientLink =
    new RuntimeClient::Link{&zquicClient};
  zquicClientLink->connect(
    Zquic::Host{"127.0.0.1"}, zquicServer.local().port());
  ZuCHECK(waitUntil([&zquicClientLink]() { return zquicClientLink->ready(); }),
    "Zhttp H3 runtime guard client did not become ready");
  ZmRef<RuntimeServer::Link> zquicServerLink;
  ZuCHECK(waitUntil([&zquicClientLink, &zquicServer, &zquicServerLink]() {
      if (!zquicServerLink) zquicServerLink = zquicServer.link();
      return zquicServerLink && zquicClientLink->established() &&
	zquicServerLink->established();
    }), "Zhttp H3 runtime guard did not establish Zquic");
  ZuCHECK(zquicClientLink->crypto().negotiatedProtocol() == "h3" &&
      zquicServerLink->crypto().negotiatedProtocol() == "h3",
    "Zhttp H3 runtime guard ALPN mismatch");

  H3App clientApp, serverApp;
  Zhttp::H3::Client<H3App, RuntimeClient> h3Client{
    &clientApp, &zquicClient};
  Zhttp::H3::Server<H3App, RuntimeServer> h3Server{
    &serverApp, &zquicServer};
  h3Client.connection().openLocalControl();
  h3Client.connection().openLocalQPack();
  h3Server.connection().openLocalControl();
  h3Server.connection().openLocalQPack();
  ZuCHECK(h3Client.connection().ready() && h3Server.connection().ready(),
    "Zhttp H3 runtime guard control streams not ready");

  auto guardStream = zquicClientLink->stream(Zquic::StreamType::Bidi);
  ZuCHECK(zquicClientLink->send(guardStream, "zhttp-h3-runtime"),
    "Zhttp H3 runtime guard stream send failed");
  ZuCHECK(waitUntil([&zquicServerLink]() {
      return zquicServerLink->runtimeDiag().streamBytesRx == 16;
    }), "Zhttp H3 runtime guard did not deliver stream bytes");
  ZuCHECK(zquicServerLink->runtimeDiag().protectedPacketsRx &&
      zquicClientLink->runtimeDiag().protectedPacketsTx &&
      zquicServerLink->runtimeDiag().cryptoFramesRx &&
      zquicClientLink->runtimeDiag().cryptoFramesTx &&
      !zquicServerLink->runtimeDiag().packetParseErrors,
    "Zhttp H3 runtime guard Zquic diagnostics mismatch");

  Zhttp::H3::RequestParams req;
  req.authority = "localhost";
  req.path = "/zhttp-runtime";
  Zhttp::H3::HeaderBytes bytes;
  ZuCHECK(h3Client.connection().encodeRequest(bytes, req) > 0,
    "Zhttp H3 runtime guard request encode failed");
  Zhttp::H3::H3Frame frame;
  unsigned used = 0;
  ZuCHECK(!Zhttp::H3::H3FrameCodec::parse(
    ZuCSpan{reinterpret_cast<const char *>(bytes.data()), bytes.length()},
    frame, used), "Zhttp H3 runtime guard request frame parse failed");
  Zhttp::H3::DecodedRequest decodedReq;
  ZuCHECK(h3Server.connection().decodeRequest(frame, decodedReq) ==
      int(frame.payload.length()) &&
      decodedReq.method == Zhttp::Method::GET &&
      decodedReq.path == "/zhttp-runtime",
    "Zhttp H3 runtime guard request decode failed");

  Zhttp::H3::ResponseParams resp;
  resp.status = 200;
  ZuCHECK(h3Server.connection().encodeResponse(bytes, resp) > 0,
    "Zhttp H3 runtime guard response encode failed");
  ZuCHECK(!Zhttp::H3::H3FrameCodec::parse(
    ZuCSpan{reinterpret_cast<const char *>(bytes.data()), bytes.length()},
    frame, used), "Zhttp H3 runtime guard response frame parse failed");
  Zhttp::H3::DecodedResponse decodedResp;
  ZuCHECK(h3Client.connection().decodeResponse(frame, decodedResp) ==
      int(frame.payload.length()) &&
      decodedResp.status == 200,
    "Zhttp H3 runtime guard response decode failed");

  zquicClientLink->disconnect();
  zquicServer.close();
  ZuCHECK(waitUntil([&zquicClientLink]() {
      return !zquicClientLink->cxn() &&
	zquicClientLink->disconnectedCount == 1;
    }), "Zhttp H3 runtime guard client link did not disconnect");
  zquicClientLink = nullptr;
  zquicClient.final();
  zquicServer.final();
  mx.stop();
}

void testZhttpRuntimeRequestResponse()
{
  ZuTestScope(testZhttpRuntimeRequestResponse);

  Zhttp::Test::TempDir temp;
  ZuCHECK(temp.init("Zhttp3RuntimeRequest"),
    "Zhttp runtime request temporary directory failed");
  ZtString<> certPath;
  ZtString<> keyPath;
  ZuCHECK(Zhttp::Test::writeSelfSignedLocalhostCert(
      temp, certPath, keyPath),
    "Zhttp runtime request certificate generation failed");

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
  ZuCHECK(mxStarted, "Zhttp runtime request multiplexer start failed");
  if (!mxStarted) return;

  ZhttpRuntimeServer server;
  ZuCHECK(server.init(
      Zquic::ServerParams(&mx, "3", "4")
	.certPath(cspan(certPath))
	.keyPath(cspan(keyPath))
	.maxData(32768)
	.maxStreamData(8192)
	.maxStreamsBidi(8)
	.maxStreamsUni(8)
	.alpn(ZuSpan<ZuCSpan>{"h3"})),
    "Zhttp runtime request server init failed");
  ZuCHECK(server.listen(),
    "Zhttp runtime request server listen failed");
  ZuCHECK(waitUntil([&server]() { return server.listening(); }),
    "Zhttp runtime request server did not become ready");

  ZhttpRuntimeClient client;
  ZuCHECK(client.init(
      Zquic::ClientParams(&mx, "3", "4")
	.caPath(cspan(certPath))
	.maxData(32768)
	.maxStreamData(8192)
	.maxStreamsBidi(8)
	.maxStreamsUni(8)
	.alpn(ZuSpan<ZuCSpan>{"h3"})),
    "Zhttp runtime request client init failed");
  ZmRef<ZhttpRuntimeClient::Link> clientLink =
    new ZhttpRuntimeClient::Link{&client};
  clientLink->connect(Zquic::Host{"127.0.0.1"}, server.local().port());
  ZuCHECK(waitUntil([&clientLink]() { return clientLink->ready(); }),
    "Zhttp runtime request client did not become ready");
  ZmRef<ZhttpRuntimeServer::Link> serverLink;
  ZuCHECK(waitUntil([&clientLink, &server, &serverLink]() {
      if (!serverLink) serverLink = server.link();
      return serverLink && clientLink->established() &&
	serverLink->established();
    }), "Zhttp runtime request Zquic connection did not establish");

  client.h3.openLocalControl();
  client.h3.openLocalQPack();
  server.h3.openLocalControl();
  server.h3.openLocalQPack();
  Zhttp::H3::HeaderBytes control;
  ZuCHECK(writeControlStream_(control, client.h3),
    "Zhttp runtime client control stream encode failed");
  auto clientControl = clientLink->stream(Zquic::StreamType::Uni);
  ZuCHECK(clientLink->send(clientControl, headerBytes_(control)),
    "Zhttp runtime client control stream send failed");
  ZuCHECK(writeControlStream_(control, server.h3),
    "Zhttp runtime server control stream encode failed");
  auto serverControl = serverLink->stream(Zquic::StreamType::Uni);
  ZuCHECK(serverLink->send(serverControl, headerBytes_(control)),
    "Zhttp runtime server control stream send failed");

  Zhttp::H3::RequestParams req;
  req.authority = "localhost";
  req.path = "/zhttp-runtime";
  Zhttp::H3::HeaderBytes request;
  ZuCHECK(client.h3.encodeRequest(request, req) > 0,
    "Zhttp runtime request encode failed");
  Zhttp::H3::HeaderBytes data;
  ZuCHECK(client.h3.encodeData(data, "ping") > 0,
    "Zhttp runtime request DATA encode failed");
  appendHeaderBytes_(request, data);
  auto requestStream = clientLink->stream(Zquic::StreamType::Bidi);
  ZuCHECK(clientLink->send(requestStream, headerBytes_(request)),
    "Zhttp runtime request stream send failed");

  ZuCHECK(waitUntil([&client, &server]() {
      return client.responseSeen && server.responseSent;
    }), "Zhttp runtime response did not complete");
  ZuCHECK(!client.responseErrors && !server.requestErrors,
    "Zhttp runtime callback errors");
  ZuCHECK(server.requestSeen &&
      server.request.method == Zhttp::Method::GET &&
      server.request.path == "/zhttp-runtime" &&
      server.body == "ping",
    "Zhttp runtime decoded request mismatch");
  ZuCHECK(client.responseHeadersSeen &&
      client.response.status == 200 &&
      client.body == "zhttp-h3-ok",
    "Zhttp runtime decoded response mismatch");
  ZuCHECK(client.h3.diag().headersTx &&
      client.h3.diag().headersRx &&
      client.h3.diag().dataFramesTx &&
      client.h3.diag().dataFramesRx &&
      server.h3.diag().headersTx &&
      server.h3.diag().headersRx &&
      server.h3.diag().dataFramesTx &&
      server.h3.diag().dataFramesRx &&
      !clientLink->runtimeDiag().packetParseErrors &&
      !serverLink->runtimeDiag().packetParseErrors,
    "Zhttp runtime H3/Zquic diagnostics mismatch");

  clientLink->disconnect();
  server.close();
  ZuCHECK(waitUntil([&clientLink]() {
      return !clientLink->cxn() && clientLink->disconnectedCount == 1;
    }), "Zhttp runtime request client link did not disconnect");
  clientLink = nullptr;
  client.final();
  server.final();
  mx.stop();
}

void testCurlZhttpH3Server()
{
  ZuTestScope(testCurlZhttpH3Server);

  Zhttp::Test::TempDir temp;
  ZuCHECK(temp.init("ZhttpCurlH3Server"),
    "curl->Zhttp H3 temporary directory failed");
  ZtString<> certPath;
  ZtString<> keyPath;
  ZuCHECK(Zhttp::Test::writeSelfSignedLocalhostCert(
      temp, certPath, keyPath),
    "curl->Zhttp H3 certificate generation failed");

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
  ZuCHECK(mxStarted, "curl->Zhttp H3 multiplexer start failed");
  if (!mxStarted) return;

  ZhttpRuntimeServer server;
  server.h3.openLocalControl();
  server.h3.openLocalQPack();
  ZuCHECK(server.init(
      Zquic::ServerParams(&mx, "3", "4")
	.certPath(cspan(certPath))
	.keyPath(cspan(keyPath))
	.maxData(32768)
	.maxStreamData(8192)
	.maxStreamsBidi(8)
	.maxStreamsUni(8)
	.alpn(ZuSpan<ZuCSpan>{"h3"})),
    "curl->Zhttp H3 server init failed");
  ZuCHECK(server.listen(),
    "curl->Zhttp H3 server listen failed");
  ZuCHECK(waitUntil([&server]() { return server.listening(); }),
    "curl->Zhttp H3 server did not become ready");

  bool curlOK = runCurlH3(
    temp, server.local().port(), "/zhttp-runtime", "zhttp-h3-ok");
  ZmRef<ZhttpRuntimeServer::Link> serverLink = server.link();
  if (!curlOK || !server.requestSeen || server.requestErrors) {
    if (serverLink) {
      const auto &d = serverLink->runtimeDiag();
      std::cout <<
	"# zhttp server diag:"
	" datagramsRx=" << uint64_t(d.datagramsRx) <<
	" packetsRx=" << uint64_t(d.packetsRx) <<
	" protectedRx=" << uint64_t(d.protectedPacketsRx) <<
	" cryptoRx=" << uint64_t(d.cryptoFramesRx) <<
	" streamRx=" << uint64_t(d.streamFramesRx) <<
	" streamTx=" << uint64_t(d.streamFramesTx) <<
	" handshakeDoneTx=" << uint64_t(d.handshakeDoneFramesTx) <<
	" parseErrors=" << uint64_t(d.packetParseErrors) <<
	" appStreams=" << unsigned(server.streamsSeen) <<
	" appReqStreams=" << unsigned(server.requestStreamsSeen) <<
	" lastStreamID=" << uint64_t(server.lastStreamID) <<
	" lastReqStreamID=" << uint64_t(server.lastRequestStreamID) <<
	" requestErrors=" << unsigned(server.requestErrors) <<
	'\n';
    }
  }
  ZuCHECK(curlOK, "curl HTTP/3 request to local Zhttp H3 server failed");
  ZuCHECK(waitUntil([&server]() { return server.requestSeen; }),
    "curl->Zhttp H3 request did not enter Zhttp");
  ZuCHECK(!server.requestErrors &&
      server.request.method == Zhttp::Method::GET &&
      server.request.path == "/zhttp-runtime" &&
      server.responseSent,
    "curl->Zhttp H3 decoded request mismatch");
  ZuCHECK(server.h3.diag().headersRx &&
      server.h3.diag().headersTx &&
      server.h3.diag().dataFramesTx &&
      serverLink &&
      serverLink->runtimeDiag().protectedPacketsRx &&
      serverLink->runtimeDiag().streamFramesRx &&
      serverLink->runtimeDiag().streamFramesTx &&
      serverLink->runtimeDiag().handshakeDoneFramesTx,
    "curl->Zhttp H3 diagnostics mismatch");

  server.final();
  mx.stop();
}

void testZhttpH3ClientCaddy()
{
  ZuTestScope(testZhttpH3ClientCaddy);

  Zhttp::Test::TempDir temp;
  ZuCHECK(temp.init("ZhttpH3ClientCaddy"),
    "Zhttp H3 client->Caddy temporary directory failed");
  ZtString<> certPath;
  ZtString<> keyPath;
  ZuCHECK(Zhttp::Test::writeSelfSignedLocalhostCert(
      temp, certPath, keyPath),
    "Zhttp H3 client->Caddy certificate generation failed");

  unsigned port = Zhttp::Test::loopbackPort();
  ZuCHECK(port, "Zhttp H3 client->Caddy loopback port allocation failed");
  if (!port) return;

  auto caddyfile = temp.pathOf("Caddyfile");
  ZuCHECK(writeCaddyfile(
      caddyfile, port, certPath, keyPath, "/zhttp-interop", "caddy-h3-ok"),
    "Zhttp H3 client->Caddy Caddyfile generation failed");

  CaddyProcess caddy;
  ZuCHECK(caddy.start(temp, cspan(caddyfile)),
    "Zhttp H3 client->Caddy start failed");
  ZuCHECK(waitCaddyReady(port, "/zhttp-interop"),
    "Zhttp H3 client->Caddy did not become ready");

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
  ZuCHECK(mxStarted, "Zhttp H3 client->Caddy multiplexer start failed");
  if (!mxStarted) return;

  ZhttpRuntimeClient client;
  ZuCHECK(client.init(
      Zquic::ClientParams(&mx, "3", "4")
	.caPath(cspan(certPath))
	.maxData(32768)
	.maxStreamData(8192)
	.maxStreamsBidi(8)
	.maxStreamsUni(8)
	.alpn(ZuSpan<ZuCSpan>{"h3"})),
    "Zhttp H3 client->Caddy client init failed");
  ZmRef<ZhttpRuntimeClient::Link> clientLink =
    new ZhttpRuntimeClient::Link{&client};
  clientLink->connect(Zquic::Host{"127.0.0.1"}, port);
  ZuCHECK(waitUntil([&clientLink]() { return clientLink->ready(); }),
    "Zhttp H3 client->Caddy client did not become ready");
  ZuCHECK(waitUntil([&clientLink]() { return clientLink->established(); }),
    "Zhttp H3 client->Caddy QUIC connection did not establish");

  client.h3.openLocalControl();
  client.h3.openLocalQPack();
  Zhttp::H3::HeaderBytes control;
  ZuCHECK(writeControlStream_(control, client.h3),
    "Zhttp H3 client->Caddy control stream encode failed");
  auto clientControl = clientLink->stream(Zquic::StreamType::Uni);
  ZuCHECK(clientLink->send(clientControl, headerBytes_(control)),
    "Zhttp H3 client->Caddy control stream send failed");

  Zhttp::H3::RequestParams req;
  req.authority = "localhost";
  req.path = "/zhttp-interop";
  Zhttp::H3::HeaderBytes request;
  ZuCHECK(client.h3.encodeRequest(request, req) > 0,
    "Zhttp H3 client->Caddy request encode failed");
  auto requestStream = clientLink->stream(Zquic::StreamType::Bidi);
  ZuCHECK(clientLink->send(requestStream, headerBytes_(request)),
    "Zhttp H3 client->Caddy request stream send failed");

  ZuCHECK(waitUntil([&client]() { return client.responseSeen; }),
    "Zhttp H3 client->Caddy response did not arrive");
  if (!client.responseSeen || client.responseErrors) {
    const auto &d = clientLink->runtimeDiag();
    std::cout <<
      "# zhttp client diag:"
      " datagramsRx=" << uint64_t(d.datagramsRx) <<
      " packetsRx=" << uint64_t(d.packetsRx) <<
      " protectedRx=" << uint64_t(d.protectedPacketsRx) <<
      " cryptoRx=" << uint64_t(d.cryptoFramesRx) <<
      " streamRx=" << uint64_t(d.streamFramesRx) <<
      " streamTx=" << uint64_t(d.streamFramesTx) <<
      " handshakeDoneRx=" << uint64_t(d.handshakeDoneFramesRx) <<
      " parseErrors=" << uint64_t(d.packetParseErrors) <<
      " responseErrors=" << unsigned(client.responseErrors) <<
      '\n';
    printFile("caddy log", caddy.logPath);
  }
  ZuCHECK(!client.responseErrors &&
      client.responseHeadersSeen &&
      client.response.status == 200 &&
      client.body == "caddy-h3-ok",
    "Zhttp H3 client->Caddy response mismatch");
  ZuCHECK(client.h3.diag().headersTx &&
      client.h3.diag().headersRx &&
      client.h3.diag().dataFramesRx &&
      clientLink->runtimeDiag().protectedPacketsRx &&
      clientLink->runtimeDiag().streamFramesRx &&
      clientLink->runtimeDiag().streamFramesTx &&
      clientLink->runtimeDiag().handshakeDoneFramesRx,
    "Zhttp H3 client->Caddy diagnostics mismatch");

  clientLink->disconnect();
  ZuCHECK(waitUntil([&clientLink]() {
      return !clientLink->cxn() && clientLink->disconnectedCount == 1;
    }), "Zhttp H3 client->Caddy client link did not disconnect");
  clientLink = nullptr;
  client.final();
  mx.stop();
}

void testZhttpH3RequestResponse()
{
  ZuTestScope(testZhttpH3RequestResponse);

  Zhttp::H3::Connection h3;
  h3.openLocalControl();
  h3.openLocalQPack();
  ZuCHECK(h3.ready(), "H3 connection not ready");

  Zhttp::H3::RequestParams req;
  req.authority = "localhost";
  req.path = "/interop";

  Zhttp::H3::HeaderBytes bytes;
  ZuCHECK(h3.encodeRequest(bytes, req) > 0, "H3 request encode failed");

  Zhttp::H3::H3Frame frame;
  unsigned used = 0;
  ZuCHECK(!Zhttp::H3::H3FrameCodec::parse(
    ZuCSpan{reinterpret_cast<const char *>(bytes.data()), bytes.length()},
    frame, used), "H3 request frame parse failed");
  ZuCHECK(frame.type == Zhttp::H3::H3FrameType::Headers,
    "H3 request frame type mismatch");

  bool seenMethod = false, seenPath = false;
  ZuCHECK(Zhttp::H3::QPack::decodeLiteral(frame.payload,
    [&](Zhttp::H3::Header h) {
      if (h.name == ":method" && h.value == "GET") seenMethod = true;
      if (h.name == ":path" && h.value == "/interop") seenPath = true;
    }) > 0, "H3 request field decode failed");
  ZuCHECK(seenMethod && seenPath, "H3 request pseudo-header mismatch");

  Zhttp::H3::ResponseParams resp;
  resp.status = 204;
  ZuCHECK(h3.encodeResponse(bytes, resp) > 0, "H3 response encode failed");
  ZuCHECK(!Zhttp::H3::H3FrameCodec::parse(
    ZuCSpan{reinterpret_cast<const char *>(bytes.data()), bytes.length()},
    frame, used), "H3 response frame parse failed");

  bool seenStatus = false;
  ZuCHECK(Zhttp::H3::QPack::decodeLiteral(frame.payload,
    [&](Zhttp::H3::Header h) {
      if (h.name == ":status" && h.value == "204") seenStatus = true;
    }) > 0, "H3 response field decode failed");
  ZuCHECK(seenStatus, "H3 response status mismatch");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testInteropPrerequisites);
  ZuTestCall(testCaddyH3Fixture);
  ZuTestCall(testCurlCaddyHttp3);
  ZuTestCall(testZhttpRuntimeTrafficGuard);
  ZuTestCall(testZhttpRuntimeRequestResponse);
  ZuTestCall(testCurlZhttpH3Server);
  ZuTestCall(testZhttpH3ClientCaddy);
  ZuTestCall(testZhttpH3RequestResponse);
}
