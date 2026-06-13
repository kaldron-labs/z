//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>
#include <string.h>

#include <zlib/ZuDerive.hh>
#include <zlib/ZmList.hh>

#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiRxStream.hh>
#include <zlib/Zhttp.hh>
#include <zlib/ZtString.hh>

inline void out(const char *s) { std::cout << s << '\n'; }

#define CHECK(x) ((x) ? out("OK  " #x) : out("NOK " #x))

char response_[] =
"HTTP/1.1 200 OK\r\n"
"content-length: 13\r\n"
"referrer-policy: no-referrer\r\n"
"\r\n"
"hello, world!";

char request_[] =
"GET / HTTP/1.1\r\n"
"host: foo.com\r\n"
"user-agent: zhttptest/1.0\r\n"
"accept: */*\r\n"
"\r\n";

char requestQuery_[] =
"GET /search?q=z HTTP/1.1\r\n"
"host: foo.com\r\n"
"user-agent: zhttptest/1.0\r\n"
"accept: */*\r\n"
"\r\n";

char requestRuntime_[] =
"GET / HTTP/1.1\r\n"
"host: foo.com\r\n"
"user-agent: zhttptest/1.0\r\n"
"accept: */*\r\n"
"x-runtime-key: runtime-value\r\n"
"\r\n";

constexpr unsigned BufSize = 8<<10;
constexpr unsigned MaxBufSize = 100<<20;

ZuDerive(RxQueue,
  (ZmList<ZiIOBuf, ZmListNode<ZiIOBuf, ZmListHeapID<"">>>));
using RxBufAlloc = Zi::IOBufAlloc<RxQueue::Node, BufSize, MaxBufSize,
  ZuStringT<"Zhttp.Buf">>;
using RxStream = ZiRxStream<RxQueue>;
using IOBufAlloc = ZiIOBufAlloc<BufSize, MaxBufSize, "Zhttp.Buf">;
using BodyData = ZtString<ZtStringHeapID<"Zhttp.Test.BodyData">>;

ZmRef<RxQueue::Node> mkBuf(const char *s)
{
  unsigned n = static_cast<unsigned>(::strlen(s));
  ZmRef<RxQueue::Node> buf = new RxBufAlloc{};
  auto iobuf = static_cast<ZiIOBuf *>(buf.ptr());
  if (n) {
    ::memcpy(iobuf->data(), s, n);
    iobuf->length = n;
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

using ResponseHeaders = ZuTypeList<
  ZuStringT<"referrer-policy">, void,
  ZuStringT<"server-timing">, void>;
struct ResponseParser :
  public Zhttp::H1::Parser<ResponseParser, false, ResponseHeaders, MaxBufSize> {
  using Base =
    Zhttp::H1::Parser<ResponseParser, false, ResponseHeaders, MaxBufSize>;

  void status(unsigned v) { statusSeen = v; }
  void contentLength(uint64_t v) { contentLengthSeen = v; }
  void chunked() { chunkedSeen = true; }

  template <typename Key>
  void header(ZuBSpan value) {
    if constexpr (ZuIsSame<Key, ZuStringT<"referrer-policy">>{})
      referrerPolicy = value == "no-referrer";
    else if constexpr (ZuIsSame<Key, ZuStringT<"server-timing">>{})
      serverTiming = value == "cpu;dur=2.4";
  }

  void body(ZuBSpan span) {
    bodyBytes += span.length();
    bodyData << span;
  }
  void complete(Zhttp::H1::ParserState::T state_) { completeState = state_; }

  int				statusSeen = -1;
  int64_t			contentLengthSeen = -1;
  bool				referrerPolicy = false;
  bool				serverTiming = false;
  bool				chunkedSeen = false;
  uint64_t			bodyBytes = 0;
  Zhttp::H1::ParserState::T		completeState = Zhttp::H1::ParserState::Initial;
  BodyData			bodyData;
};

using RequestHeaders = ZuTypeList<
  ZuStringT<"user-agent">, ZuStringT<"zhttptest/1.0">,
  ZuStringT<"accept">, ZuStringT<"*/*">>;
struct RequestBuilder :
  public Zhttp::H1::Builder<RequestBuilder, RequestHeaders> {
  using Base = Zhttp::H1::Builder<RequestBuilder, RequestHeaders>;

  template <typename L>
  void operation(L &&l) { l(Zhttp::Method::GET, "/", ""); }
  template <typename L>
  void host(L &&l) { l("foo.com"); }
};

struct QueryRequestBuilder :
  public Zhttp::H1::Builder<QueryRequestBuilder, RequestHeaders> {
  using Base = Zhttp::H1::Builder<QueryRequestBuilder, RequestHeaders>;

  template <typename L>
  void operation(L &&l) { l(Zhttp::Method::GET, "/search", "q=z"); }
  template <typename L>
  void host(L &&l) { l("foo.com"); }
};

struct RuntimeRequestBuilder :
  public Zhttp::H1::Builder<RuntimeRequestBuilder, RequestHeaders> {
  using Base = Zhttp::H1::Builder<RuntimeRequestBuilder, RequestHeaders>;

  template <typename L>
  void operation(L &&l) { l(Zhttp::Method::GET, "/", ""); }
  template <typename L>
  void host(L &&l) { l("foo.com"); }
  template <typename L>
  void header(L &&l) { l("x-runtime-key", "runtime-value"); }
};

int main()
{
  using namespace Zhttp;

  CHECK(eoh("\r\n\r") == -1);
  CHECK(eoh("\r\n\r\n") == 4);
  CHECK(eol("\n") == -1);
  CHECK(eol("\r\nx") == 0);
  CHECK(eol("\r\n ") == -1);
  CHECK(eok(":") == 0);
  CHECK(eok("x: ") == 1);

  {
    int i = -1;
    split("foo, bar", [&i](unsigned j, ZuCSpan s) {
      CHECK(s == (!j ? "foo" : "bar"));
      i = j;
      return true;
    });
    CHECK(i == 1);
  }

  {
    ResponseParser parser;
    RxStream stream;
    stream.push(mkBuf(::response_));
    CHECK(parser.process(stream) == Zhttp::H1::ParserState::Complete);
    CHECK(parser.statusSeen == 200);
    CHECK(parser.contentLengthSeen == 13);
    CHECK(parser.referrerPolicy);
    CHECK(parser.bodyData == "hello, world!");
    CHECK(!stream);
  }

  {
    static char chunked[] =
      "HTTP/1.1 200 OK\r\n"
      "transfer-encoding: chunked\r\n"
      "\r\n"
      "1\r\n"
      "{\r\n"
      "9\r\n"
      "\"x\": 42, \r\n"
      "7\r\n"
      "\"y\": 42\r\n"
      "1\r\n"
      "}\r\n"
      "0\r\nserver-timing: cpu;dur=2.4\r\n\r\n";
    ResponseParser parser;
    RxStream stream;
    stream.push(mkBuf(chunked));
    CHECK(parser.process(stream) == Zhttp::H1::ParserState::Complete);
    CHECK(parser.chunkedSeen);
    CHECK(parser.bodyBytes == 18);
    CHECK(parser.serverTiming);
    CHECK(parser.bodyData == "{\"x\": 42, \"y\": 42}");
    CHECK(!stream);
  }

  {
    RequestBuilder builder;
    BufTx tx{new IOBufAlloc{}};
    builder.request(tx);
    builder.finish(tx);
    CHECK(tx.buf->cspan() == ::request_);
  }

  {
    QueryRequestBuilder builder;
    BufTx tx{new IOBufAlloc{}};
    builder.request(tx);
    builder.finish(tx);
    CHECK(tx.buf->cspan() == ::requestQuery_);
  }

  {
    RuntimeRequestBuilder builder;
    BufTx tx{new IOBufAlloc{}};
    builder.request(tx);
    builder.finish(tx);
    CHECK(tx.buf->cspan() == ::requestRuntime_);
  }
}
