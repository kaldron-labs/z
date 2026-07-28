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

#include <zlib/ZuMatcher.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuSwitch.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuUnroll.hh>

#include <zlib/ZtLocalArray.hh>

#include <zlib/ZiAssert.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiTxStream.hh>

#include <zlib/ZhttpConfig.hh>
#include <zlib/ZhttpTypes.hh>
#include <zlib/ZhttpURL.hh>
#include <zlib/ZhttpDiscovery.hh>
#include <zlib/ZhttpUtil.hh>
#include <zlib/ZhttpCompression.hh>
#include <zlib/ZhttpHPack.hh>
#include <zlib/ZhttpQPack.hh>
#include <zlib/ZhttpBody.hh>

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

// HTTP engine/link application contract
//
// Engines:
//   init(params) -> start() -> process links -> stop(done) -> final()
// done is called only after ingress is disabled and Rx/Tx work and links have
// drained.  final() is only valid from done (or after the main-thread blocking
// stop() wrapper returns); no caller may infer completion from a posted stop.
//
// Client links:
//   connect() -> connected() -> txStream()/process() -> disconnected()
//
// Server links:
//   connected() -> txStream()/process(initial message) -> disconnected()
//
// The process callback is installed by CRTP composition before connected()
// returns and before received application bytes are dispatched.  TCP and TLS
// links represent transport connections.  An HTTP/3 application link
// represents a duplex request stream; the physical QUIC connection, H3
// control streams, and QPACK state remain library-owned.
//
// Thread ownership:
//   init/final		caller/control thread
//   start/stop		ZmEngine control path
//   connected/process	Rx thread
//   request/response Tx	Tx thread
//   disconnected	Rx thread, exactly once per application link
//
// Protocol-specific concrete link and Rx/Tx stream types may differ, but they
// must provide the same compile-time operations so one application template
// implements the connection and message processing body for all protocols.
//
// Completion rules:
// - an H1 close-delimited response completes at EOF, before disconnected();
// - an H1 keep-alive link may process multiple messages before disconnected();
// - a TLS handshake failure reports connectFailed(), never connected();
// - an H3 application link becomes connected only after connection control
//   streams are ready and the corresponding request stream exists;
// - peer close/reset during a response completes or fails that application
//   link exactly once, followed by one disconnected() callback.

// HTTP Parser CRTP API
// - consistent contract for H1::Parser and H3::Parser
// - For HTTP/1 responses framed by connection close (no Content-Length and no
//   transfer-encoding: chunked), call parser.eof() from the connection close path
//   after all received bytes have been passed to parser.process().

// CRTP - implementation may implement the following callbacks:
#if 0
struct Impl : public Parser<Impl, ...> {
  using Base = Parser<Impl, ...>;

  // optional - if implemented, must call Base::reset()
  void reset();

  // optional - request callback
  void operation(Method::T method, ZuBSpan path);

  // optional - response callback
  void status(unsigned);

  // optional - header key
  template <typename Key> void header(ZuBSpan value);

  // optional - header key+value
  template <typename Key, typename Value> void header();

  // optional - run-time header key/value
  void header(ZuBSpan key, ZuBSpan value);

  // optional - content-length header
  void contentLength(uint64_t);

  // optional - decoded entity-body input; synchronous and Rx-shard-affine
  template <typename Rx> void body(Rx &);

  // optional - end of stream/message
  void complete(ParserState::T);

  // optional - stream completion predicate
  // - default calls impl()->finReceived()
  bool rxComplete() const;

  // required only when using Base::rxComplete()
  bool finReceived() const;

  // optional - H3/QPACK parameters, defaults to default Params
  const Params &h3Params() const;

  // optional - current stream ID, used for QPACK section acknowledgements
  uint64_t streamID() const;

  // optional - write to local QPACK decoder stream for section acks
  bool qpackDecoderWrite(ZuBSpan);

  // optional - peer dynamic table; nullptr disables dynamic QPACK decoding
  QPackRxTable *qpackRx();

  // optional - post peer decoder instructions and SETTINGS capacity to Tx
  bool qpackTxInsn(QPackInsn::T, uint64_t);
  bool qpackTxMaxCapacity(uint64_t);
};
#endif

// HTTP Builder CRTP API
// - consistent contract for H1::Builder and H3::Builder
// - H1 chunked builders emit Trailers after finish(); H3 builders emit Trailers
//   as a trailing HEADERS frame from finish().

// CRTP - implementation may implement the following callbacks:
#if 0
struct Impl : public Builder<Impl, Headers, Trailers, HasBody, Chunked> {
  using Base = Builder<Impl, Headers, Trailers, HasBody, Chunked>;

  // optional - if implemented, must call Base::reset()
  void reset();

  // optional - request method/path/query
  template <typename L> void operation(L &&l);
  // l(Method::T method, ZuCSpan path, ZuCSpan query)

  // optional - request host / HTTP/3 :authority
  template <typename L> void host(L &&l);
  // l(ZuCSpan host)

  // optional - response status
  unsigned status();

  // optional - HTTP/1 response reason
  template <typename L> void reason(L &&l);
  // l(ZuCSpan reason)

  // optional - static header key with run-time value
  template <typename Key, typename L> void header(L &&l);
  // l(ZuCSpan value)

  // optional - run-time header key/value pairs
  template <typename L> void header(L &&l);
  // l(ZuCSpan key, ZuCSpan value)

  // optional - body length when HasBody && !Chunked
  uint64_t contentLength();

  // optional - H3 QPACK dynamic table
  H3::QPackTxTable *qpackTx();

  // optional - H3 QPACK encoder-stream write
  bool qpackEncoderWrite(ZuBSpan);

  // optional - H3 QPACK build failure callback
  void qpackFailure(H3::QPackBuildFailure::T);

  // optional - H3 parameters
  const H3::Params &h3Params() const;

  // optional - H3 stream ID used for QPACK section tracking
  uint64_t streamID() const;
};
#endif

} // namespace Zhttp

#include <zlib/ZhttpFields.hh>
#include <zlib/ZhttpH1.hh>
#include <zlib/ZhttpH2.hh>
#include <zlib/ZhttpH2Session.hh>
#include <zlib/ZhttpH2Message.hh>
#include <zlib/ZhttpH3.hh>
#include <zlib/ZhttpH1Session.hh>
#include <zlib/ZhttpH3Session.hh>
#include <zlib/ZhttpMessage.hh>
#include <zlib/ZhttpTunnel.hh>
#include <zlib/ZhttpTLSEngine.hh>

namespace Zhttp {

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  uint64_t MaxBody = DefltMaxBody>
using H1ReqParser = H1::Parser<Impl, true, Headers, MaxBody>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  uint64_t MaxBody = DefltMaxBody>
using H1RespParser = H1::Parser<Impl, false, Headers, MaxBody>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  uint64_t MaxBody = DefltMaxBody>
using H2ReqParser = H2::Parser<Impl, true, Headers, MaxBody>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  uint64_t MaxBody = DefltMaxBody>
using H2RespParser = H2::Parser<Impl, false, Headers, MaxBody>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  uint64_t MaxBody = DefltMaxBody>
using H3ReqParser = H3::Parser<Impl, true, Headers, MaxBody>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  uint64_t MaxBody = DefltMaxBody>
using H3RespParser = H3::Parser<Impl, false, Headers, MaxBody>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>,
  bool HasBody = false, bool Chunked = false>
using H1ReqBuilder = H1::Builder<Impl, Headers, Trailers, HasBody, Chunked>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>,
  bool HasBody = false, bool Chunked = false>
using H1RespBuilder = H1::Builder<Impl, Headers, Trailers, HasBody, Chunked>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>,
  bool HasBody = false>
using H2ReqBuilder = H2::Builder<Impl, Headers, Trailers, HasBody, false>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>,
  bool HasBody = false>
using H2RespBuilder = H2::Builder<Impl, Headers, Trailers, HasBody, false>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>,
  bool HasBody = false>
using H3ReqBuilder = H3::Builder<Impl, Headers, Trailers, HasBody, false>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>,
  bool HasBody = false>
using H3RespBuilder = H3::Builder<Impl, Headers, Trailers, HasBody, false>;

} // namespace Zhttp

#ifndef Zhttp_CORE_ONLY
#include <zlib/ZhttpClient.hh>
#include <zlib/ZhttpClientPool.hh>
#include <zlib/ZhttpAgent.hh>
#include <zlib/ZhttpServer.hh>
#include <zlib/ZhttpEngines.hh>
#include <zlib/ZhttpH2Engine.hh>
#include <zlib/ZhttpH3Engine.hh>
#endif

#endif /* Zhttp_HH */
