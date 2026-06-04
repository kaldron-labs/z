//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZtHexDump.hh>

#include <zlib/Zhttp.hh>

inline void out(const char *s) { std::cout << s << '\n'; }

#define CHECK(x) ((x) ? out("OK  " #x) : out("NOK " #x))

char response_[] =
"HTTP/1.1 200 OK\r\n"
"server: nginx\r\n"
"date: Sun, 06 Oct 2024 06:12:39 GMT\r\n"
"content-type: text/html; charset=UTF-8\r\n"
"content-length: 211\r\n"
"connection: keep-alive\r\n"
"x-hacker: If you're reading this, you should visit wpvip.com/careers and apply to join the fun, mention this header.\r\n"
"x-powered-by: WordPress VIP <https://wpvip.com>\r\n"
"host-header: a9130478a60e5f9135f765b23f26593b\r\n"
"x-frame-options: SAMEORIGIN\r\n"
"referrer-policy: no-referrer-when-downgrade\r\n"
"x-content-type-options: nosniff\r\n"
"x-xss-protection: 1; mode=block\r\n"
"content-security-policy: frame-ancestors nypost.com decider.com pagesix.com *.nypost.com *.decider.com *.pagesix.com; form-action 'self' *.nypdev.com nypost.com decider.com pagesix.com *.nypost.com *.decider.com *.pagesix.com\r\n"
"link: <https://nypost.com/wp-json/>; rel=\"https://api.w.org/\"\r\n"
"link: <https://wp.me/b3Qpq>; rel=shortlink\r\n"
"strict-transport-security: max-age=31536000\r\n"
"x-rq: nrt1 123 242 443\r\n"
"accept-ranges: bytes\r\n"
"x-cache: HIT\r\n"
"cache-control: private, no-store\r\n\r\n"

"<!doctype html>\n"
"<html lang=\"en-US\">\n"
"<head prefix=\"og: https://ogp.me/ns# fb: https://ogp.me/ns/fb#\">\n"
"<title>New York Post – Breaking News, Top Headlines, Photos & Videos</title>\n"
"</head>\n"
"<body>\n"
"</body>\n"
"</html>\n";

char request_[] =
"GET / HTTP/1.1\r\n"
"host: foo.com\r\n"
"user-agent: zhttptest/1.0\r\n"
"accept: */*\r\n"
"\r\n";

constexpr unsigned BufSize = 8<<10;	// default built-in buffer size
constexpr unsigned MaxBufSize = 100<<20;// max HTTP body length (100Mb)

using IOBufAlloc = ZiIOBufAlloc<BufSize, MaxBufSize, "Zhttp.Buf">;

struct RequestCtx {
  Zhttp::Method::T	method = -1;
  ZuCSpan		path;
  ZuCSpan		host;
};

struct ResponseCtx {
  int			status = -1;
  ZuCSpan		referrerPolicy;
};

struct TrailerCtx {
  ZuCSpan		serverTiming;
};

using Parser_ = Zhttp::Parser<
  Zhttp::Response<ZuStringTL<"referrer-policy">>,
  Zhttp::Body<MaxBufSize>, ResponseCtx>;

inline ZuSpan<uint8_t> bytes_(char *s) {
  ZuSpan<char> span{s};
  return {reinterpret_cast<uint8_t *>(span.data()), span.length()};
}

inline ZuSpan<uint8_t> bytes_(Zhttp::TrailerBuf &s) {
  return {reinterpret_cast<uint8_t *>(s.data()), s.length()};
}

inline ZuCSpan serverTiming_(Zhttp::TrailerBuf &data) {
  Zhttp::Headers<ZuStringTL<"server-timing">, ZuStringTL<>> trailer;
  TrailerCtx ctx;
  trailer.parse(bytes_(data), ctx,
    [](TrailerCtx &ctx, int i, ZuCSpan s) {
      if (!i) ctx.serverTiming = s;
    },
    [](TrailerCtx &, int) { });
  return ctx.serverTiming;
}

int main()
{
  using namespace Zhttp;

  CHECK(eoh("\r\n\r") == -1);
  CHECK(eoh("\r\n\r\n") == 4);
  CHECK(eoh("\r\r\n\r\n") == 5);
  CHECK(eoh("\n\r\n\r\n") == 5);
  CHECK(eoh("\r\r\r\n\r\n") == 6);
  CHECK(eoh("\n\n\r\n\r\n") == 6);
  CHECK(eoh("\r\r\r\r\n\r\n") == 7);
  CHECK(eoh("\n\n\n\r\n\r\n") == 7);
  CHECK(eoh("\r\nx\r\r\n\r\n") == 8);
  CHECK(eoh("\r\nx\n\r\n\r\n") == 8);
  CHECK(eoh("\n\rx\r\r\n\r") == -1);
  CHECK(eol("\n") == -1);
  CHECK(eol("\r") == -1);
  CHECK(eol("\r\nx") == 0);
  CHECK(eol("\r\r\nx") == 1);
  CHECK(eol("\n\r\nx") == 1);
  CHECK(eol("\r\n ") == -1);
  CHECK(eol("\r\r\n ") == -1);
  CHECK(eol("\n\r\n ") == -1);
  CHECK(eol("\r\n \r\nx") == 3);
  CHECK(eol("\r\r\n\t\r\nx") == 4);
  CHECK(eol("\n\r\n\r\r\nx") == 1);
  CHECK(eol("\r\r") == -1);
  CHECK(eol("\n\r") == -1);
  CHECK(eok(":") == 0);
  CHECK(eok(": ") == 0);
  CHECK(eok("x: ") == 1);
  CHECK(eok("x:: ") == 1);
  CHECK(eok("x ::") == 2);

  {
    int i = -1;
    split("", [&i](unsigned j, ZuCSpan) { i = j; }); CHECK(i == -1);
    split(" ", [&i](unsigned j, ZuCSpan) { i = j; }); CHECK(i == -1);
    split(",", [&i](unsigned j, ZuCSpan s) {
      CHECK(s == "");
      i = j;
    });
    CHECK(i == 1); i = -1;
    auto check = [&i](unsigned j, ZuCSpan s) {
      CHECK(s == "foo");
      i = j;
    };
    split("foo", check); CHECK(!i); i = -1;
    split(" foo", check); CHECK(!i); i = -1;
    split("foo ", check); CHECK(!i); i = -1;
    split(" foo ", check); CHECK(!i); i = -1;
    auto check2 = [&i](unsigned j, ZuCSpan s) {
      CHECK(s == (!j ? "foo" : "bar"));
      i = j;
    };
    split("foo,bar", check2); CHECK(i == 1); i = -1;
    split("foo ,bar", check2); CHECK(i == 1); i = -1;
    split("foo, bar", check2); CHECK(i == 1); i = -1;
    split("foo , bar", check2); CHECK(i == 1); i = -1;
    split("foo  ,  bar", check2); CHECK(i == 1); i = -1;
    split(" foo  ,  bar ", check2); CHECK(i == 1); i = -1;
  }

  {
    Request<ZuStringTL<"host">> r;
    RequestCtx ctx;
    auto o = r.parse(bytes_(::request_), ctx,
      [](RequestCtx &ctx, Method::T method, ZuCSpan path) {
	ctx.method = method;
	ctx.path = path;
      },
      [](RequestCtx &ctx, int i, ZuCSpan value) {
	if (!i) ctx.host = value;
      },
      [](RequestCtx &, int) { });
    CHECK(o > 0);
    CHECK(o == sizeof(::request_) - 1);
    CHECK(ctx.path == "/");
    CHECK(ctx.method == Zhttp::Method::GET);
    CHECK(ctx.host == "foo.com");
  }
  auto status = [](auto &rx, int status) { rx.context.status = status; };
  auto key = [](auto &rx, int i, ZuCSpan value) {
    if (!i) rx.context.referrerPolicy = value;
  };
  auto kv = [](auto &, int) { };
  auto rcvd = [](auto &) { return true; };
  {
    auto msg = bytes_(::response_);
    ZmRef<ZiIOBuf> buf = new IOBufAlloc();
    Parser_ rx{buf};
    auto o = rx.process(msg, status, key, kv, rcvd);
    CHECK(o == msg.length());
    CHECK(rx.header.status == 200);
    CHECK(rx.context.status == 200);
    CHECK(rx.context.referrerPolicy == "no-referrer-when-downgrade");
    CHECK(rx.body.valid);
    CHECK(!rx.body.chunked);
    CHECK(rx.body.xferEncoding < 0);
    CHECK(rx.body.contentLength == 211);
  }
  { ChunkHdr hdr; CHECK(hdr.parse("Aa0\r\n") == 5 && hdr.length == 0xaa0); }
  { ChunkHdr hdr; CHECK(hdr.parse("Aa0 \r\n") == -1 && !hdr.valid()); }
  { ChunkHdr hdr; CHECK(hdr.parse("aaaaaaaa\r\n") == -1 && !hdr.valid()); }
  { ChunkHdr hdr; CHECK(hdr.parse("aaaaaaaaa\r\n") == -1 && !hdr.valid()); }
  { ChunkHdr hdr; CHECK(hdr.parse("\r\n") == -1 && !hdr.valid()); }
  { ChunkHdr hdr; CHECK(hdr.parse("0\r\n") == 3 && hdr.eob() && hdr.valid()); }
  {
    static char chunked[] =
      "HTTP/1.1 200 OK\r\n"
      "content-type: application/json\r\n"
      "transfer-encoding: chunked\r\n"
      "\r\n"
      "1\r\n" // chunk[0] 1
      "{\r\n"
      "9\r\n" // chunk[1] 9
      "\"x\": 42, \r\n"
      "7\r\n" // chunk[2] 7
      "\"y\": 42\r\n"
      "1\r\n" // chunk[3] 1
      "}\r\n"
      "0\r\n\r\n"; // end chunk, no trailers
    auto msg = bytes_(chunked);
    ZmRef<ZiIOBuf> buf = new IOBufAlloc();
    Parser_ rx{buf};
    auto o = rx.process(msg, status, key, kv, rcvd);
    CHECK(o > 0);
    CHECK(rx.body.complete);
    CHECK(rx.body.chunked);
    CHECK(rx.body.chunkBuf == "0\r\n");
    CHECK(rx.body.chunkTrlr == "\r\n\r\n");
    CHECK(rx.body.chunkTotal == 18);
    CHECK(rx.body.span == "{\"x\": 42, \"y\": 42}");
  }
  {
    static char chunked[] =
      "HTTP/1.1 200 OK\r\n"
      "content-type: application/json\r\n"
      "transfer-encoding: chunked\r\n"
      "\r\n"
      "1\r\n" // chunk[0] 1
      "{\r\n"
      "9\r\n" // chunk[1] 9
      "\"x\": 42, \r\n"
      "7\r\n" // chunk[2] 7
      "\"y\": 42\r\n"
      "1\r\n" // chunk[3] 1
      "}\r\n"
      "0\r\nserver-timing: cpu;dur=2.4\r\n\r\n"; // end chunk, with trailer
    auto msg = bytes_(chunked);
    ZmRef<ZiIOBuf> buf = new IOBufAlloc();
    Parser_ rx{buf};
    auto o = rx.process(msg, status, key, kv, rcvd);
    CHECK(o > 0);
    CHECK(rx.body.complete);
    CHECK(rx.body.chunked);
    CHECK(rx.body.chunkBuf == "0\r\n");
    CHECK(rx.body.chunkTrlr == "server-timing: cpu;dur=2.4\r\n\r\n");
    auto s = serverTiming_(rx.body.chunkTrlr);
    CHECK(s == "cpu;dur=2.4");
    split<';'>(s, [](unsigned i, ZuCSpan s) {
      switch (i) {
	case 0: CHECK(s == "cpu"); break;
	case 1: CHECK(s == "dur=2.4"); break;
      }
    });
    CHECK(rx.body.chunkTotal == 18);
    CHECK(rx.body.span == "{\"x\": 42, \"y\": 42}");
  }
  {
    static char frag0[] =
      "HTTP/1.1 200 OK\r\n"
      "content-type: application/json\r\n"
      "transfer-encoding: chunked\r\n"
      "\r\n";
    static char frag1[] =
      "1\r\n" // chunk[0] 1
      "{\r";
    static char frag2[] =
      "\n"
      "9\r\n" // chunk[1] 9
      "\"x\": 42, \r\n";
    static char frag3[] =
      "7\r\n" // chunk[2] 7
      "\"y\": ";
    static char frag4[] =
      "42\r\n"
      "1\r\n" // chunk[3] 1
      "}\r";
    static char frag5[] =
      "\n"
      "0\r\nserver-timing: "; // end chunk, with trailer
    static char frag6[] =
      "cpu;dur=2.4\r";
    static char frag7[] =
      "\n\r";
    static char frag8[] =
      "\n";
    ZmRef<ZiIOBuf> buf = new IOBufAlloc();
    Parser_ rx{buf};
    auto o = rx.process(bytes_(frag0), status, key, kv, rcvd);
    CHECK(o > 0);
    CHECK(!rx.body.complete);
    o = rx.process(bytes_(frag1), status, key, kv, rcvd);
    CHECK(!rx.body.complete);
    o = rx.process(bytes_(frag2), status, key, kv, rcvd);
    CHECK(!rx.body.complete);
    o = rx.process(bytes_(frag3), status, key, kv, rcvd);
    CHECK(!rx.body.complete);
    o = rx.process(bytes_(frag4), status, key, kv, rcvd);
    CHECK(!rx.body.complete);
    o = rx.process(bytes_(frag5), status, key, kv, rcvd);
    CHECK(!rx.body.complete);
    o = rx.process(bytes_(frag6), status, key, kv, rcvd);
    CHECK(!rx.body.complete);
    o = rx.process(bytes_(frag7), status, key, kv, rcvd);
    CHECK(!rx.body.complete);
    o = rx.process(bytes_(frag8), status, key, kv, rcvd);
    CHECK(rx.body.complete);
    CHECK(rx.body.chunked);
    CHECK(rx.body.chunkBuf == "0\r\n");
    CHECK(rx.body.chunkTrlr == "server-timing: cpu;dur=2.4\r\n\r\n");
    auto s = serverTiming_(rx.body.chunkTrlr);
    CHECK(s == "cpu;dur=2.4");
    split<';'>(s, [](unsigned i, ZuCSpan s) {
      switch (i) {
	case 0: CHECK(s == "cpu"); break;
	case 1: CHECK(s == "dur=2.4"); break;
      }
    });
    CHECK(rx.body.chunkTotal == 18);
    CHECK(rx.body.span == "{\"x\": 42, \"y\": 42}");
  }
  {
    Builder<
      ZuStringTL<>,
      ZuStringTL<"user-agent: zhttptest/1.0", "accept: */*">,
      false> builder{new ZiIOBufAlloc<>()};
    builder.request(
      Method::GET, "/", "foo.com", [](auto &, auto) { return ""; });
    auto buf = builder.finish();
    CHECK(buf->cspan() == ::request_);
  }
  {
    Builder<
      ZuStringTL<>,
      ZuStringTL<
	"user-agent: zhttptest/1.0",
	"accept: */*",
	"content-type: application/json">,
      true> builder{new ZiIOBufAlloc<>()};
    builder.request(
      Method::POST, "/post", "foo.com", [](auto &, auto) { return ""; });
    // x-www-form-urlencoded
    *(builder.buf) << "{ \"a\": 42 }";
    auto buf = builder.finish();
    std::cout << ZuCSpan(buf->cspan()) << '\n';
  }
}
