//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <iostream>

#ifndef _WIN32
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zhttp3.hh>

#include "ZhttpCaddyInterop.hh"

using namespace ZuTestUtil;

namespace {

constexpr unsigned FallbackBufSize = 8<<10;
constexpr unsigned FallbackMaxBody = 64<<10;

using IOBufAlloc =
  ZiIOBufAlloc<FallbackBufSize, FallbackMaxBody, "Zhttp.Fallback.Buf">;
using ResponseRx =
  Zhttp::RxMsg<Zhttp::Response<>, Zhttp::Body<FallbackMaxBody>>;

#ifndef _WIN32
int listenLoopback_(unsigned &port)
{
  port = 0;
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;

  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0 ||
      ::listen(fd, 1) < 0) {
    ::close(fd);
    return -1;
  }

  socklen_t len = sizeof(addr);
  if (::getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &len) < 0) {
    ::close(fd);
    return -1;
  }
  port = ntohs(addr.sin_port);
  return fd;
}

bool sendAll_(int fd, const uint8_t *data, unsigned len)
{
  unsigned o = 0;
  while (o < len) {
    ssize_t n = ::send(fd, data + o, len - o, 0);
    if (n <= 0) return false;
    o += unsigned(n);
  }
  return true;
}

bool readRequest_(int fd, uint8_t *buf, unsigned size, unsigned &len)
{
  len = 0;
  while (len < size) {
    ssize_t n = ::recv(fd, buf + len, size - len, 0);
    if (n <= 0) return false;
    len += unsigned(n);
    if (len >= 4 &&
	::memmem(buf, len, "\r\n\r\n", 4))
      return true;
  }
  return false;
}

bool recvAll_(int fd, uint8_t *buf, unsigned size, unsigned &len)
{
  len = 0;
  while (len < size) {
    ssize_t n = ::recv(fd, buf + len, size - len, 0);
    if (n < 0) return false;
    if (!n) return true;
    len += unsigned(n);
  }
  return false;
}

bool localhostHost_(ZuCSpan host)
{
  if (host == "localhost") return true;
  if (host.length() > 10 &&
      !memcmp(host.data(), "localhost:", 10))
    return true;
  if (host == "127.0.0.1") return true;
  if (host.length() > 10 &&
      !memcmp(host.data(), "127.0.0.1:", 10))
    return true;
  return false;
}

int connectLoopback_(unsigned port)
{
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(port);
  if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

pid_t startZhttpH1Server_(
    int listener, ZuCSpan expectedPath, ZuCSpan responseBody)
{
  pid_t pid = ::fork();
  if (pid) return pid;

  int fd = ::accept(listener, nullptr, nullptr);
  ::close(listener);
  if (fd < 0) _exit(2);

  uint8_t reqBuf[FallbackBufSize];
  unsigned reqLen = 0;
  if (!readRequest_(fd, reqBuf, sizeof(reqBuf), reqLen)) {
    ::close(fd);
    _exit(3);
  }

  Zhttp::Request<ZuStringTL<"Host">> req;
  int used = req.parse(ZuSpan<uint8_t>{reqBuf, reqLen});
  if (used <= 0 ||
      req.method != Zhttp::Method::GET ||
      req.protocol != "HTTP/1.1" ||
      req.path != expectedPath ||
      !localhostHost_(req.key(0))) {
    ::close(fd);
    _exit(4);
  }

  Zhttp::Builder<true> resp{new IOBufAlloc()};
  resp.response(200, "OK", [](auto &) { });
  *(resp.buf) << responseBody;
  auto out = resp.finish();
  bool sent = sendAll_(fd, out->data(), out->length);
  ::shutdown(fd, SHUT_RDWR);
  ::close(fd);
  _exit(sent ? 0 : 5);
}

bool waitServerOK_(pid_t pid)
{
  int status = 0;
  return ::waitpid(pid, &status, 0) == pid &&
    Zhttp::Test::systemOK(status);
}

bool runCurlZhttpH1_(unsigned port, ZuCSpan expectedBody)
{
  Zhttp::Test::TempDir temp;
  if (!temp.init("ZhttpFallbackCurl")) return false;
  auto body = temp.pathOf("curl.body");
  auto version = temp.pathOf("curl.version");
  auto err = temp.pathOf("curl.err");

  ZtString<> cmd;
  cmd <<
    "curl --http1.1 -k --fail -sS --connect-timeout 2 --max-time 5 "
    "-o " << body << " -w '%{http_version}' "
    "http://127.0.0.1:" << port << "/zhttp-fallback "
    ">" << version << " 2>" << err;
  if (!Zhttp::Test::systemOK(::system(cmd.data()))) return false;

  cmd.length(0);
  cmd << "grep -qx '" << expectedBody << "' " << body <<
    " && grep -qx '1.1' " << version;
  return Zhttp::Test::systemOK(::system(cmd.data()));
}
#else
int listenLoopback_(unsigned &port) { port = 0; return -1; }
pid_t startZhttpH1Server_(int, ZuCSpan, ZuCSpan) { return -1; }
bool waitServerOK_(pid_t) { return false; }
bool runCurlZhttpH1_(unsigned, ZuCSpan) { return false; }
#endif

} // namespace

void testInteropPrerequisites()
{
  ZuTestScope(testInteropPrerequisites);

  ZuCHECK(Zhttp::Test::haveCurlH3(),
    "curl with HTTP3/ngtcp2/nghttp3 is required for fallback tests");
  ZuCHECK(Zhttp::Test::haveCaddy(),
    "caddy is required for fallback tests");
}

void testCurlCaddyHttp11Fallback()
{
  ZuTestScope(testCurlCaddyHttp11Fallback);

  auto decision = Zhttp::H3::FallbackPolicy{}.select(false, true, true);
  ZuCHECK(decision.protocol == Zhttp::H3::FallbackProtocol::HTTP11 &&
      decision.reason == Zhttp::H3::FallbackReason::HTTP3Unavailable &&
      decision.fallback(),
    "Zhttp fallback policy did not select HTTP/1.1 before interop");
  ZuCHECK(Zhttp::Test::runCaddyCurl(
      "\"h1\"", "--http1.1", "1.1", "zhttp-h1-ok", true),
    "HTTP/1.1 fallback route failed");
}

void testCurlZhttpHttp11Fallback()
{
  ZuTestScope(testCurlZhttpHttp11Fallback);

  unsigned port = 0;
  int listener = listenLoopback_(port);
  ZuCHECK(listener >= 0 && port,
    "curl->Zhttp fallback HTTP/1.1 listener setup failed");
  if (listener < 0) return;

  pid_t pid = startZhttpH1Server_(
    listener, "/zhttp-fallback", "zhttp-h1-ok");
  ZuCHECK(pid > 0, "curl->Zhttp fallback HTTP/1.1 server fork failed");
  if (pid <= 0) {
    ::close(listener);
    return;
  }
  ::close(listener);

  ZuCHECK(runCurlZhttpH1_(port, "zhttp-h1-ok"),
    "curl HTTP/1.1 request to local Zhttp fallback route failed");
  ZuCHECK(waitServerOK_(pid),
    "curl->Zhttp fallback server did not parse request and send response");
}

void testZhttpClientHttp11Fallback()
{
  ZuTestScope(testZhttpClientHttp11Fallback);

  auto decision = Zhttp::H3::FallbackPolicy{}.select(false, true, true);
  ZuCHECK(decision.protocol == Zhttp::H3::FallbackProtocol::HTTP11 &&
      decision.reason == Zhttp::H3::FallbackReason::HTTP3Unavailable &&
      decision.fallback(),
    "Zhttp client fallback policy did not select HTTP/1.1");

  unsigned port = 0;
  int listener = listenLoopback_(port);
  ZuCHECK(listener >= 0 && port,
    "Zhttp client fallback HTTP/1.1 listener setup failed");
  if (listener < 0) return;

  pid_t pid = startZhttpH1Server_(
    listener, "/zhttp-fallback", "zhttp-h1-ok");
  ZuCHECK(pid > 0, "Zhttp client fallback HTTP/1.1 server fork failed");
  if (pid <= 0) {
    ::close(listener);
    return;
  }
  ::close(listener);

  int fd = connectLoopback_(port);
  ZuCHECK(fd >= 0, "Zhttp client fallback connect failed");
  if (fd < 0) {
    waitServerOK_(pid);
    return;
  }

  Zhttp::Builder<false> req{new IOBufAlloc()};
  req.request(Zhttp::Method::GET, "/zhttp-fallback", "localhost",
    [](auto &builder) {
      *(builder.buf) << "User-Agent: ZhttpFallbackTest/1.0\r\n";
    });
  auto out = req.finish();
  ZuCHECK(sendAll_(fd, out->data(), out->length),
    "Zhttp client fallback request send failed");
  ::shutdown(fd, SHUT_WR);

  uint8_t respBuf[FallbackBufSize];
  unsigned respLen = 0;
  ZuCHECK(recvAll_(fd, respBuf, sizeof(respBuf), respLen) && respLen,
    "Zhttp client fallback response receive failed");
  ::close(fd);

  ZmRef<ZiIOBuf> rxBuf = new IOBufAlloc();
  ResponseRx rx{rxBuf};
  int consumed = rx.process(
    ZuSpan<uint8_t>{respBuf, respLen}, []() { return true; });
  if (!(consumed == int(respLen) &&
	rx.header.protocol == "HTTP/1.1" &&
	rx.header.code == 200 &&
	rx.body.contentLength == 11 &&
	rx.body.complete &&
	rx.body.span == "zhttp-h1-ok")) {
    std::cout <<
      "# fallback response diag:"
      " consumed=" << consumed <<
      " respLen=" << respLen <<
      " protocol=" << rx.header.protocol <<
      " code=" << rx.header.code <<
      " contentLength=" << rx.body.contentLength <<
      " bodyComplete=" << rx.body.complete <<
      " body='" << rx.body.span << "'\n";
  }
  ZuCHECK(consumed == int(respLen) &&
      rx.header.protocol == "HTTP/1.1" &&
      rx.header.code == 200 &&
      rx.body.contentLength == 11 &&
      rx.body.complete &&
      rx.body.span == "zhttp-h1-ok",
    "Zhttp client fallback response parse mismatch");
  ZuCHECK(waitServerOK_(pid),
    "Zhttp client fallback server did not parse request and send response");
}

void testFallbackPolicyShape()
{
  ZuTestScope(testFallbackPolicyShape);

  Zhttp::H3::FallbackPolicy policy;
  auto decision = policy.select(true, true, true);
  ZuCHECK(decision.protocol == Zhttp::H3::FallbackProtocol::HTTP3 &&
      decision.reason == Zhttp::H3::FallbackReason::None &&
      decision.error == Zhttp::H3::H3Error::NoError &&
      !decision.fallback() && !decision.failed(),
    "fallback policy did not select healthy HTTP/3");

  decision = policy.select(false, true, true);
  ZuCHECK(decision.protocol == Zhttp::H3::FallbackProtocol::HTTP11 &&
      decision.reason == Zhttp::H3::FallbackReason::HTTP3Unavailable &&
      decision.error == Zhttp::H3::H3Error::VersionFallback &&
      decision.fallback(),
    "fallback policy did not fall back when HTTP/3 was unavailable");

  decision = policy.select(true, false, true);
  ZuCHECK(decision.protocol == Zhttp::H3::FallbackProtocol::HTTP11 &&
      decision.reason == Zhttp::H3::FallbackReason::ALPNRejected &&
      decision.error == Zhttp::H3::H3Error::VersionFallback &&
      decision.fallback(),
    "fallback policy did not fall back on ALPN rejection");

  decision = policy.select(true, true, false);
  ZuCHECK(decision.protocol == Zhttp::H3::FallbackProtocol::HTTP11 &&
      decision.reason == Zhttp::H3::FallbackReason::ConnectFailed &&
      decision.error == Zhttp::H3::H3Error::ConnectError &&
      decision.fallback(),
    "fallback policy did not fall back on HTTP/3 connect failure");

  decision = Zhttp::H3::FallbackPolicy{}.http3(false).
    select(true, true, true);
  ZuCHECK(decision.protocol == Zhttp::H3::FallbackProtocol::HTTP11 &&
      decision.reason == Zhttp::H3::FallbackReason::HTTP3Disabled &&
      decision.error == Zhttp::H3::H3Error::VersionFallback &&
      decision.fallback(),
    "fallback policy did not fall back when HTTP/3 was disabled");

  decision = Zhttp::H3::FallbackPolicy{}.http11(false).
    select(false, true, true);
  ZuCHECK(decision.protocol == Zhttp::H3::FallbackProtocol::None &&
      decision.reason == Zhttp::H3::FallbackReason::HTTP11Disabled &&
      decision.error == Zhttp::H3::H3Error::VersionFallback &&
      decision.failed(),
    "fallback policy did not fail closed when HTTP/1.1 was disabled");

  Zhttp::H3::Params params;
  params.qpackTableCapacity(0).qpackBlockedStreams(0);
  ZuCHECK(params.qpackTableCapacity() == 0 &&
    params.qpackBlockedStreams() == 0,
    "zero-capacity QPACK fallback profile mismatch");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testInteropPrerequisites);
  ZuTestCall(testCurlCaddyHttp11Fallback);
  ZuTestCall(testCurlZhttpHttp11Fallback);
  ZuTestCall(testZhttpClientHttp11Fallback);
  ZuTestCall(testFallbackPolicyShape);
}
