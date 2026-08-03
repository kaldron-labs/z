//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library
// - HTTP/1.1, HTTP/2, and HTTP/3
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
#include <zlib/ZuSwitch.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuUnroll.hh>

#include <zlib/ZtScratch.hh>

#include <zlib/ZiAssert.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiTxStream.hh>

#include <zlib/ZhttpConfig.hh>
#include <zlib/ZhttpTypes.hh>
#include <zlib/ZhttpURL.hh>
#include <zlib/ZhttpAltSvc.hh>
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

} // namespace Zhttp

#include <zlib/ZhttpFields.hh>
#include <zlib/ZhttpH1.hh>
#include <zlib/ZhttpH2.hh>
#include <zlib/ZhttpH2Stream.hh>
#include <zlib/ZhttpH2Message.hh>
#include <zlib/ZhttpH3.hh>
#include <zlib/ZhttpH3Cxn.hh>
#include <zlib/ZhttpMessage.hh>
#include <zlib/ZhttpStream.hh>
#include <zlib/ZhttpH1Stream.hh>
#include <zlib/ZhttpTLSEngine.hh>

namespace Zhttp {

// Low-level protocol Parser CRTP contract for the H1/H2/H3 aliases below.
// ClientMessage and Service wrap plain application Parser sinks in these
// adapters; application sinks do not derive from them.  The protocol invokes
// only the callbacks applicable to the selected request/response role and
// version.  Inherited defaults are side-effect-safe; an adapter which
// overrides reset() must call Base::reset().  All callbacks are synchronous
// and Rx-shard-affine.  RequestTarget and received spans are borrowed for the
// duration of the callback.  `Parser` in the API sketch denotes the selected
// alias below.
#if 0
struct ParserImpl : public Parser<ParserImpl, Headers, MaxBody> {
  using Base = Parser<ParserImpl, Headers, MaxBody>;
  using State = typename Base::State;

  void reset();

  // request or response start line / pseudo-headers
  void operation(Method::T method, const RequestTarget &target);
  void status(unsigned);

  // H1 protocol and transfer-coding notifications
  void version(ZuBSpan);
  void xferCompression(XferCompression::T);
  void chunked();

  // declared run-time value, declared fixed value, undeclared key/value
  template <typename Key> void header(ZuBSpan value);
  template <typename Key, typename Value> void header();
  void header(ZuBSpan key, ZuBSpan value);
  void contentLength(uint64_t);

  // H2/H3 initial field section, populated decoded-body queue, completion
  void headers(Fields::Section, bool endStream);
  // body() is a synchronous prompt; incomplete application framing remains
  // queued and is presented again after a later complete HTTP frame append.
  template <typename Rx> void body(Rx &);
  void complete(State::T);

  // H3 stream/QPACK integration
  bool rxComplete() const;		// defaults to finReceived()
  bool finReceived() const;
  const H3::Params &h3Params() const;
  uint64_t streamID() const;
  bool qpackDecoderWrite(ZuBSpan);
  H3::QPackRxTable *qpackRx();
  bool qpackTxInsn(H3::QPackInsn::T, uint64_t);
  bool qpackTxMaxCapacity(uint64_t);
  bool qpackTxBlocked(uint64_t);
};
#endif

// For an H1 response framed by connection close (neither Content-Length nor
// transfer-encoding: chunked), pass all received bytes to process(), then call
// eof() from the connection-close path.

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

// Low-level protocol Builder CRTP adapters used internally by ClientMessage
// and Service.  Application Builders are plain structural types; their
// contracts are documented beside those consuming templates.  They do not
// derive from these aliases.  The adapters provide protocol framing, invoke
// the application through inversion-of-control lambdas, and preserve the
// actual types of printable targets, authorities, reasons, and header values.
// H1 chunked builders emit Trailers from finish(); H2/H3 builders emit a
// trailing HEADERS section.

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>,
  bool HasBody = false, bool Chunked = false>
using H1ReqBuilder =
  H1::RequestBuilder<Impl, Headers, Trailers, HasBody, Chunked>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>,
  bool HasBody = false, bool Chunked = false>
using H1RespBuilder =
  H1::ResponseBuilder<Impl, Headers, Trailers, HasBody, Chunked>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>,
  bool HasBody = false>
using H2ReqBuilder =
  H2::RequestBuilder<Impl, Headers, Trailers, HasBody, false>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>,
  bool HasBody = false>
using H2RespBuilder =
  H2::ResponseBuilder<Impl, Headers, Trailers, HasBody, false>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>,
  bool HasBody = false>
using H3ReqBuilder =
  H3::RequestBuilder<Impl, Headers, Trailers, HasBody, false>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>,
  bool HasBody = false>
using H3RespBuilder =
  H3::ResponseBuilder<Impl, Headers, Trailers, HasBody, false>;

} // namespace Zhttp

#ifndef Zhttp_CORE_ONLY
#include <zlib/ZhttpClientEngine.hh>
#include <zlib/ZhttpClientPool.hh>
#include <zlib/ZhttpClient.hh>
#include <zlib/ZhttpServer.hh>
#include <zlib/ZhttpEngines.hh>
#include <zlib/ZhttpH2Engine.hh>
#include <zlib/ZhttpH3Engine.hh>
#endif

#endif /* Zhttp_HH */
