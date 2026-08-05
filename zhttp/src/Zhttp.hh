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

// HTTP hub/link application contract
//
// Hubs:
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
#include <zlib/ZhttpTLSHub.hh>

namespace Zhttp {

// Parser and Builder are plain application structs wrapped in protocol CRTP
// adapters by ClientMessage and Service; neither inherits a Zhttp base.  The
// protocol invokes only the callbacks applicable to the selected request or
// response role and version.  Every lambda call is synchronous.  Printable
// Builder values retain their actual type.  Received spans and body Rx streams
// are borrowed only for the duration of the callback.
#if 0
struct Parser {
  using Headers = ZhttpHeaders(...);
  static constexpr uint64_t BodyMax = DefltMaxBody;

  // Constructed once per logical stream and destroyed once when that stream
  // ends.  reset() is called exactly once before each message, including the
  // first, and clears all per-message application state.
  Parser();
  ~Parser();
  void reset();

  // Called first, before any header() or body() callback.  A subsequent
  // validation failure is reported by complete(false).
  void operation(Method::T, const RequestTarget &);	// requests only
  // Called first, before any header() or body() callback.  A subsequent
  // validation failure is reported by complete(false).
  void status(unsigned);				// responses only
  void version(ZuBSpan);
  void contentLength(uint64_t);
  void xferCompression(XferCompression::T);		// H1 only
  void chunked();

  // declared run-time value, declared fixed value, undeclared key/value
  template <typename Key> void header(ZuBSpan value);
  template <typename Key, typename Value> void header();
  void header(ZuBSpan key, ZuBSpan value);

  // Completed H2/H3 field section; section identifies informational, final,
  // or trailer fields.  endStream is meaningful for the initial section.
  void headers(Fields::Section section, bool endStream);

  // Synchronous queue prompt; incomplete application framing may remain
  // queued for a later decoded-body append.
  template <typename Rx> void body(Rx &);
  void complete(bool ok);
};

struct Builder {
  using Headers = ZhttpHeaders(...);
  using Trailers = ZhttpHeaders(...);	// optional
  using BodyPolicy = Body::None;

  // May be constructed once and retained across messages.  Called exactly
  // once before each message, including the first, to clear per-message
  // construction state while preserving the configured request/response.
  void reset();

  // request start line / pseudo-headers
  // Called exactly once per message; l(method, target).
  template <typename L> void operation(L &&l);
  template <typename L> void host(L &&l);	// l(authority)
  template <typename L> void protocol(L &&l);	// l(value), CONNECT only

  // response start line / pseudo-headers
  unsigned status();
  template <typename L> void reason(L &&l);	// l(value), H1 only

  template <typename Key, typename L> void header(L &&l); // l(value)
  template <typename L> void header(L &&l);		   // l(key, value)

  // Present only for body-bearing policies.  emit(write) is called zero or
  // one times according to BodyPolicy::Optional; write(bodyStream) returns
  // void or bool.
  template <typename Emit> void body(Emit &&emit);

  // Present only for fixed policies; called synchronously after body output.
  // l.template operator()<Key>(patcher), patcher(ZuSpan<uint8_t> value).
  // There is no contentLength() callback; provision Content-Length with
  // HeaderPad from header<Key>(), then patch it here.
  template <typename L> void bodyHdrs(L &&l);

  bool close() const;			// responses only
};

// Extended request Builder contract used by Client.  Request_ is the
// application data stored in the intrusive TxQ::Msg node.  Client calls
// reset() once for every wire request (including replay attempts and
// redirects), then uses the Builder callbacks above.  A failed connection
// which emits no request is not a message.  All lifecycle callbacks are
// synchronous.
struct Request_ : Builder {
  // Monotonic queue identity and discrete-message length.
  uint64_t key() const;
  uint64_t length() const; // returns 1

  // Absolute URL of the submitted request.  Client snapshots it on
  // submission; redirected() receives each subsequently accepted URL.
  Zhttp::URLStorage url;

  // Whether the request semantics permit another attempt after a redirect or
  // an unprocessed failure.  Called before Client decides to replay.
  bool replayable() const;

  // Whether another Builder pass will reproduce the same request, including
  // identical body bytes.  Both replayable() and reproducible() must be true
  // for Client to replay a request.
  bool reproducible() const;

  // A transport connection for the current attempt is ready.  Called before
  // reset() and request construction; info identifies the selected transport
  // and negotiated HTTP version.
  void connected(const ConnectedInfo &);

  // The current attempt's connection ended; peer is true when the peer
  // initiated the disconnect.  No callback is made without a bound request.
  void disconnected(bool peer);

  // Connection establishment failed.  transient classifies whether Client
  // may retry subject to its configured limit and the replay predicates.
  void connectFailed(bool transient);

  // Client selected a concrete endpoint for the attempt.  This precedes
  // connection establishment and may occur more than once across attempts.
  void selected(const Endpoint &);

  // Client accepted a redirect to url.  Update any request construction state
  // which operation(), host(), protocol(), or header() derives from the URL.
  void redirected(const URL &);

  // Reports each typed attempt/request transition.  Multiple observations may
  // precede the single terminal completed() callback.
  void observed(const ClientEvent &);

  // Exactly one terminal result for the submitted request, after its final
  // observed Completed or Cancelled/Completed transition.
  void completed(const Result &);
};

// Extended response Parser contract used by Client.  One ResParser is
// constructed for each reusable ClientMessage stream.  init() is called once
// before every response message, including the first, and before status(),
// header(), or body(); it clears per-response state and binds the response to
// its submitted request.  init() calls Parser::reset() itself when that reset
// is needed; Client does not call Parser::reset() in addition to init().  The
// same object can therefore serve many messages.
struct ResParser : Parser {
  void init(const Request_ &request);
};
#endif

// Low-level protocol Parser CRTP contract for the H1/H2/H3 aliases below.
// ClientMessage and Service wrap plain application Parser sinks in these
// adapters; application sinks do not derive from them.  The protocol invokes
// only the callbacks applicable to the selected request/response role and
// version.  Inherited defaults are side-effect-safe; an adapter which
// overrides reset() must call Base::reset().  All callbacks are synchronous
// and Rx-shard-affine.  RequestTarget and received spans are borrowed for the
// duration of the callback.  `ProtocolParser` in the API sketch denotes the
// selected alias below.
#if 0
struct ParserAdapter : public ProtocolParser<ParserAdapter, Headers, MaxBody> {
  using Base = ProtocolParser<ParserAdapter, Headers, MaxBody>;
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
using H1ResParser = H1::Parser<Impl, false, Headers, MaxBody>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  uint64_t MaxBody = DefltMaxBody>
using H2ReqParser = H2::Parser<Impl, true, Headers, MaxBody>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  uint64_t MaxBody = DefltMaxBody>
using H2ResParser = H2::Parser<Impl, false, Headers, MaxBody>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  uint64_t MaxBody = DefltMaxBody>
using H3ReqParser = H3::Parser<Impl, true, Headers, MaxBody>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  uint64_t MaxBody = DefltMaxBody>
using H3ResParser = H3::Parser<Impl, false, Headers, MaxBody>;

// Low-level protocol Builder CRTP adapters used internally by ClientMessage
// and Service.  Application Builders do not derive from these aliases.  The
// adapters provide protocol framing and invoke the application through
// inversion-of-control lambdas.  H1 chunked builders emit Trailers from
// finish(); H2/H3 builders emit a trailing HEADERS section.

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>,
  bool HasBody = false, bool Chunked = false>
using H1ReqBuilder =
  H1::ReqBuilder<Impl, Headers, Trailers, HasBody, Chunked>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>,
  bool HasBody = false, bool Chunked = false>
using H1ResBuilder =
  H1::ResBuilder<Impl, Headers, Trailers, HasBody, Chunked>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>,
  bool HasBody = false>
using H2ReqBuilder =
  H2::ReqBuilder<Impl, Headers, Trailers, HasBody, false>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>,
  bool HasBody = false>
using H2ResBuilder =
  H2::ResBuilder<Impl, Headers, Trailers, HasBody, false>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>,
  bool HasBody = false>
using H3ReqBuilder =
  H3::ReqBuilder<Impl, Headers, Trailers, HasBody, false>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>,
  bool HasBody = false>
using H3ResBuilder =
  H3::ResBuilder<Impl, Headers, Trailers, HasBody, false>;

} // namespace Zhttp

#ifndef Zhttp_CORE_ONLY
#include <zlib/ZhttpClientHub.hh>
#include <zlib/ZhttpClientPool.hh>
#include <zlib/ZhttpClient.hh>
#include <zlib/ZhttpServer.hh>
#include <zlib/ZhttpHubs.hh>
#include <zlib/ZhttpH2Hub.hh>
#include <zlib/ZhttpH3Hub.hh>
#endif

#endif /* Zhttp_HH */
