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

#include <zlib/ZuDerive.hh>
#include <zlib/ZmList.hh>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiRxStream.hh>
#include <zlib/Zhttp.hh>
#include <zlib/ZtString.hh>

#include "ZhttpTestUtil.hh"

using namespace ZuTestUtil;

namespace {

constexpr unsigned FallbackBufSize = 8<<10;
constexpr unsigned FallbackMaxBody = 64<<10;

using IOBufAlloc =
  ZiIOBufAlloc<FallbackBufSize, FallbackMaxBody, "Zhttp.Fallback.Buf">;

ZuDerive(RxQueue,
  (ZmList<ZiIOBuf, ZmListNode<ZiIOBuf, ZmListHeapID<"">>>));
using RxBufAlloc = Zi::IOBufAlloc<RxQueue::Node, FallbackBufSize,
  FallbackMaxBody, ZuStringT<"Zhttp.Fallback.RxBuf">>;
using RxStream = ZiRxStream<RxQueue>;
using BodyData = ZtString<ZtStringHeapID<"Zhttp.Fallback.BodyData">>;

ZmRef<RxQueue::Node> mkBuf(const uint8_t *data, unsigned len)
{
  ZmRef<RxQueue::Node> buf = new RxBufAlloc{};
  auto iobuf = static_cast<ZiIOBuf *>(buf.ptr());
  if (len) {
    ::memcpy(iobuf->data(), data, len);
    iobuf->length = len;
  }
  return buf;
}

struct BufTx {
  ZmRef<ZiIOBuf> buf;

  BufTx(ZmRef<ZiIOBuf> buf_) : buf{ZuMv(buf_)} { }

  template <typename V>
  BufTx &operator <<(V &&v) {
    *buf << ZuFwd<V>(v);
    return *this;
  }

  void flush() { }
};

struct RequestCtx {
  Zhttp::Method::T	method = -1;
  ZuCSpan		path;
  ZuCSpan		host;
};

struct ResponseCtx {
  int			status = -1;
};

using RequestHeaders = ZuTypeList<ZuStringT<"host">, void>;
struct RequestRx :
  public Zhttp::H1::Parser<RequestRx, true, RequestHeaders, FallbackMaxBody> {
  using Base =
    Zhttp::H1::Parser<RequestRx, true, RequestHeaders, FallbackMaxBody>;

  void operation(Zhttp::Method::T method_, ZuCSpan path_) {
    method = method_;
    path.length(0);
    path << path_;
  }

  template <typename Key>
  void header(ZuBSpan value) {
    if constexpr (Key{}() == "host") {
      host.length(0);
      host << ZuCSpan{
	reinterpret_cast<const char *>(value.data()), value.length()};
    }
  }

  Zhttp::Method::T	method = -1;
  ZtString<>		path;
  ZtString<>		host;
};

struct ResponseRx :
  public Zhttp::H1::Parser<ResponseRx, false, ZuTypeList<>, FallbackMaxBody> {
  using Base = Zhttp::H1::Parser<ResponseRx, false, ZuTypeList<>, FallbackMaxBody>;

  void status(unsigned v) { statusSeen = v; }
  void contentLength(uint64_t v) { contentLengthSeen = v; }
  void body(ZuBSpan span) { bodyData << span; }
  void complete(Zhttp::H1::ParserState::T state_) { completeState = state_; }

  int				statusSeen = -1;
  int64_t			contentLengthSeen = -1;
  Zhttp::H1::ParserState::T		completeState = Zhttp::H1::ParserState::Initial;
  BodyData			bodyData;
};

struct ResponseBuilder :
  public Zhttp::H1::Builder<ResponseBuilder, ZuTypeList<>, ZuTypeList<>, true> {
  using Base =
    Zhttp::H1::Builder<ResponseBuilder, ZuTypeList<>, ZuTypeList<>, true>;

  ResponseBuilder(uint64_t contentLength_) : contentLength_{contentLength_} { }

  unsigned status() { return 200; }
  template <typename L> void reason(L &&l) { l("OK"); }
  uint64_t contentLength() { return contentLength_; }

  uint64_t contentLength_;
};

using RequestBuilderHeaders =
  ZuTypeList<ZuStringT<"user-agent">, ZuStringT<"ZhttpFallbackTest/1.0">>;
struct RequestBuilder :
  public Zhttp::H1::Builder<RequestBuilder, RequestBuilderHeaders> {
  using Base = Zhttp::H1::Builder<RequestBuilder, RequestBuilderHeaders>;

  template <typename L>
  void operation(L &&l) { l(Zhttp::Method::GET, "/zhttp-fallback", ""); }
  template <typename L>
  void host(L &&l) { l("localhost"); }
};

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

  RequestRx req;
  RxStream reqStream;
  reqStream.push(mkBuf(reqBuf, reqLen));
  auto reqState = req.process(reqStream);
  if (reqState != Zhttp::H1::ParserState::Complete ||
      req.method != Zhttp::Method::GET ||
      req.path != expectedPath ||
      !localhostHost_(req.host)) {
    ::close(fd);
    _exit(4);
  }

  ResponseBuilder resp{responseBody.length()};
  BufTx tx{new IOBufAlloc{}};
  resp.response(tx);
  tx << responseBody;
  resp.finish(tx);
  bool sent = sendAll_(fd, tx.buf->data(), tx.buf->length);
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

  RequestBuilder req;
  BufTx tx{new IOBufAlloc{}};
  req.request(tx);
  req.finish(tx);
  ZuCHECK(sendAll_(fd, tx.buf->data(), tx.buf->length),
    "Zhttp client fallback request send failed");
  ::shutdown(fd, SHUT_WR);

  uint8_t respBuf[FallbackBufSize];
  unsigned respLen = 0;
  ZuCHECK(recvAll_(fd, respBuf, sizeof(respBuf), respLen) && respLen,
    "Zhttp client fallback response receive failed");
  ::close(fd);

  ResponseRx rx;
  RxStream stream;
  stream.push(mkBuf(respBuf, respLen));
  auto respState = rx.process(stream);
  if (!(respState == Zhttp::H1::ParserState::Complete &&
	rx.statusSeen == 200 &&
	rx.contentLengthSeen == 11 &&
	rx.bodyData == "zhttp-h1-ok" &&
	!stream)) {
    std::cout <<
      "# fallback response diag:"
      " state=" << int(respState) <<
      " respLen=" << respLen <<
      " status=" << rx.statusSeen <<
      " contentLength=" << rx.contentLengthSeen <<
      " body='" << rx.bodyData << "'\n";
  }
  ZuCHECK(respState == Zhttp::H1::ParserState::Complete &&
      rx.statusSeen == 200 &&
      rx.contentLengthSeen == 11 &&
      rx.bodyData == "zhttp-h1-ok" &&
      !stream,
    "Zhttp client fallback response parse mismatch");
  ZuCHECK(waitServerOK_(pid),
    "Zhttp client fallback server did not parse request and send response");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testInteropPrerequisites);
  ZuTestCall(testCurlZhttpHttp11Fallback);
  ZuTestCall(testZhttpClientHttp11Fallback);
}
