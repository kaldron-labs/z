//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library
// - HTTP 1.1 and HTTP/3
// - HTTP 1.1:
//   - optionally chunked body
//   - optional chunked trailers (rarely used feature)
// - caller is responsible for body decompression (if required)

#ifndef Zhttp_HH
#define Zhttp_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/ZuString.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuMatcher.hh>
#include <zlib/ZuSwitch.hh>
#include <zlib/ZuUnroll.hh>

#include <zlib/ZtLocalArray.hh>

#include <zlib/ZiAssert.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiTxStream.hh>

#include <zlib/ZhttpUtil.hh>
#include <zlib/ZhttpHPack.hh>
#include <zlib/ZhttpQPack.hh>

// Headers typelist definition, e.g.
// - keys (variable values):
//   ZhttpHeaders("content-type", "server");
// - keys + values:
//   ZhttpHeaders(("user-agent", "zhttp"), ("accept", "*/*"));
#define Zhttp_HdrValue(Value) ZuStringT<Value>
#define Zhttp_HdrValues_(...) \
  ZuPP_Eval__(ZuPP_MapComma(Zhttp_HdrValue,  __VA_ARGS__))
#define Zhttp_HdrValues(Values) \
  ZuPP_Defer(Zhttp_HdrValues_)(ZuPP_Strip(Values))
#define Zhttp_Header_1(Key) \
  ZuStringT<Key>, void
#define Zhttp_Header_2(Key, Values) \
  ZuStringT<Key>, ZuTypeList<Zhttp_HdrValues(Values)>
#define Zhttp_Header_N(_0, _1, Fn, ...) Fn
#define Zhttp_Header_(...) \
  Zhttp_Header_N(__VA_ARGS__, \
    Zhttp_Header_2(__VA_ARGS__), \
    Zhttp_Header_1(__VA_ARGS__))
#define Zhttp_Header(KV) \
  ZuPP_Defer(Zhttp_Header_)(ZuPP_Strip(KV))
#define ZhttpHeaders(...) \
  ZuTypeList<ZuPP_Eval_(ZuPP_MapComma(Zhttp_Header,  __VA_ARGS__))>

namespace Zhttp {

constexpr unsigned DefltMaxHdr = (1<<16);	// 64K default
constexpr unsigned DefltMaxBody = (1<<20);	// 1M default

namespace Method {
  ZtEnum(Method, int8_t,
    GET, POST, PUT, DELETE, PATCH, HEAD, OPTIONS, CONNECT, TRACE);
}

// deprecated transfer-encoding compression
namespace XferCompression {
  ZtEnum(XferCompression, int8_t, compress, deflate, gzip);
}

// HTTP/1 Parser

// CRTP - implementation may implement the following callbacks:
#if 0
struct Impl : public Parser<Impl, ...> {
  using Base = Parser<Impl, ...>;

  // optional - if implemented, must call Base::reset()
  void reset();

  // optional - requests
  void operation(Method::T method, ZuBSpan path);

  // optional - responses
  void status(unsigned);

  // optional - header key
  template <typename Key> void header(ZuBSpan value);

  // optional - header key+value
  template <typename Key, typename Value> void header();

  // optional - content-length header
  void contentLength(uint64_t);

  // optional - transfer-encoding compression
  // - this will only be called if compress/deflate/gzip is specified
  // - this is HTTP 1.1 only and not mainstream
  void xferCompression(XferCompression::T);

  // optional - transfer-encoding: chunked
  void chunked();

  // optional - body data
  void body(ZuBSpan);

  // optional - end of message (end of body, or end of header if no body)
void complete(ParserState::T);
};
#endif

// For HTTP/1 responses framed by connection close (no Content-Length and no
// transfer-encoding: chunked), call parser.eof() from the connection close path
// after all received bytes have been passed to parser.process().

} // Zhttp

#include <zlib/ZhttpH1.hh>
#include <zlib/ZhttpH3.hh>
#include <zlib/ZhttpH1Session.hh>
#include <zlib/ZhttpH3Session.hh>

namespace Zhttp {

template <typename Impl, typename Headers = ZuTypeList<>,
  uint64_t MaxBody = DefltMaxBody>
using H1ReqParser = H1::Parser<Impl, true, Headers, MaxBody>;

template <typename Impl, typename Headers = ZuTypeList<>,
  uint64_t MaxBody = DefltMaxBody>
using H1RespParser = H1::Parser<Impl, false, Headers, MaxBody>;

template <typename Impl, typename Headers = ZuTypeList<>,
  uint64_t MaxBody = DefltMaxBody>
using H3ReqParser = H3::Parser<Impl, true, Headers, MaxBody>;

template <typename Impl, typename Headers = ZuTypeList<>,
  uint64_t MaxBody = DefltMaxBody>
using H3RespParser = H3::Parser<Impl, false, Headers, MaxBody>;

template <typename Impl, typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>, bool HasBody = false, bool Chunked = false>
using H1ReqBuilder = H1::Builder<Impl, Headers, Trailers, HasBody, Chunked>;

template <typename Impl, typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>, bool HasBody = false, bool Chunked = false>
using H1RespBuilder = H1::Builder<Impl, Headers, Trailers, HasBody, Chunked>;

template <typename Impl, typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>, bool HasBody = false, bool Chunked = false>
using H3ReqBuilder = H3::Builder<Impl, Headers, Trailers, HasBody, Chunked>;

template <typename Impl, typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>, bool HasBody = false, bool Chunked = false>
using H3RespBuilder = H3::Builder<Impl, Headers, Trailers, HasBody, Chunked>;


} // Zhttp

#endif /* Zhttp_HH */
