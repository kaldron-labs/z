//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library
// - HTTP/1.1, HTTP/2, and HTTP/3
// - caller is responsible for body decompression (if required)

#ifndef Zhttp_HH
#define Zhttp_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <string.h>

#include <zlib/ZuBox.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuFmt.hh>
#include <zlib/ZuStream.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuSwitch.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZuUnion.hh>
#include <zlib/ZuUnroll.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtScratch.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZiAssert.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiTxStream.hh>

#include <zlib/ZhttpCore.hh>
#include <zlib/ZhttpURL.hh>
#include <zlib/ZhttpH1.hh>
#include <zlib/ZhttpH2.hh>
#include <zlib/ZhttpHPack.hh>
#include <zlib/ZhttpH3.hh>
#include <zlib/ZhttpTransport.hh>

namespace Zhttp {

constexpr unsigned NullSlot = ZuCmp<unsigned>::null();

using ContentLengthPlaceholder =
  ZuStringT<"0000000000">;
// Fixed bodies are retained until their Content-Length has been patched.
// Larger bodies must use a streaming body policy.
constexpr uint64_t FixedBodyMax = uint32_t(-1);

inline ZuCSpan contentLengthPad()
{
  return ContentLengthPlaceholder{}();
}

inline void contentLengthSet(ZuSpan<uint8_t> span, uint64_t length)
{
  ZiAssert(span.length() == ContentLengthPlaceholder{}().length(),
    "Zhttp", (), "invalid Content-Length placeholder", return);
  ZiAssert(length <= FixedBodyMax, "Zhttp", (),
    "fixed body exceeds 32-bit Content-Length", return);
  ZuStream out{span};
  out << ZuBoxed(length).fmt<
    ZuFmt::Right<ContentLengthPlaceholder{}().length()>>();
}

template <typename L>
inline void contentLengthSet(L &&l, uint64_t length)
{
  l.template operator()<ZuStringT<"content-length">>(
    [length](ZuSpan<uint8_t> span) {
      contentLengthSet(span, length);
    });
}

// HTTP hub/link application contract
//
// Hubs:
//   init(params) -> start() -> process links -> stop(done) -> final()
// done is called only after ingress is disabled and Rx/Tx work and links have
// drained. final() is only valid from done (or after the main-thread blocking
// stop() wrapper returns); no caller may infer completion from a posted stop.
//
// Client links:
//   connect() -> connected() -> txStream()/process() -> disconnected()
//
// Server links:
//   connected() -> txStream()/process(initial message) -> disconnected()
//
// H2/H3 header encoding owns connection compression state on Tx. Post the
// builder work to Tx, then use transmit_(builder). Application txStream()
// remains available for body/transport output without header encoding.
//
// The process callback is installed by CRTP composition before connected()
// returns and before received application bytes are dispatched. TCP and TLS
// links represent transport connections. An HTTP/3 application link
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

// Parser and Builder are application base classes with side-effect-safe
// defaults. Applications can derive from them and override only the types and
// callbacks they need. The client and server facades wrap application objects
// in protocol CRTP adapters; the protocol invokes only the callbacks applicable
// to the selected request or response role and version. Every lambda call is
// synchronous. Printable Builder values retain their actual type. Received
// spans and body Rx streams are borrowed only for the duration of the callback.
//
// An application Parser is default-constructed, with no constructor arguments,
// and retained for the lifetime of its Session (or the equivalent H1
// operation).  It is serially reused for messages and is never reconstructed
// between them.  Before each message the facade calls init(context), where the
// context is the client request Builder or server application.  Every message
// completion calls complete(...) followed immediately by reset().  reset()
// must restore exactly the same application Parser state as immediately after
// default construction.  The default init() and reset() are suitable for
// stateless Parsers.
struct Parser { // base class with defaulted types and member functions
  // Alternating Key, Values pairs. Values is ZuTypeList<> for a run-time
  // value, or a non-empty list of values eligible for static dispatch.
  using HdrCatalog = DefltHdrCatalog;
  using Headers = typename HdrCatalog::List;

  Parser() = default;

  template <typename Context>
  void init(Context &) { }

  // Informational response field sections are suppressed by default. Return
  // true to receive their status() and header() callbacks.
  constexpr bool enable1xx() const { return false; }

  // First protocol callback for a request, called exactly once after the
  // start line or pseudo-headers are validated and before any header().
  // Return true to accept the operation or false to reject the message.
  // Rejection suppresses all later callbacks except complete(false).
  bool operation(Method::T, Target &) { return true; }		// requests only

  // First protocol callback for each response field section, called after
  // its :status or status line is validated and before that section's
  // header() callbacks. Informational sections may precede the final one.
  void status(unsigned) { }				// responses only

  // Declared run-time value, declared fixed value, undeclared key/value.
  // Initial-section callbacks follow operation()/status(); trailer callbacks
  // follow bodyInfo() and any body() prompts.
  template <typename Key, typename Value>
  void header(Zhttp::FieldSection::T) { }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) { }
  void header(Zhttp::FieldSection::T, ZuBSpan key, ZuSpan<uint8_t> value) { }

  // Called once after the final initial field section and before body().
  // Return true to accept the body framing or false to reject the message.
  // Rejection has the same consequences as body() returning false.
  bool bodyInfo(BodyType::T, uint64_t) { return true; }

  // Synchronous queue prompt; incomplete application framing may remain
  // queued for a later decoded-body append. Return true to continue parsing,
  // or false to reject the message. Rejection suppresses later message
  // callbacks except for exactly one eventual complete(false).
  template <typename Rx> bool body(Rx &) { return false; }

  // Last callback for the message, called exactly once.  This includes
  // rejection by operation(), bodyInfo(), or body().  A validation or framing
  // failure may short-circuit the successful ordering above and report false.
  // Link is a borrowed concrete-link pointer valid for the callback.  An
  // application that retains it for asynchronous work must explicitly acquire
  // ownership.
  template <typename Link>
  void complete(Link *, bool ok) { }

  void reset() { }
};

struct Builder {
  // Alternating Key, Values pairs. Values is ZuTypeList<> for a run-time
  // value, or a singleton list containing the fixed value.
  using HdrCatalog = DefltHdrCatalog;
  using Headers = typename HdrCatalog::List;

  // default header section scratch buffer size for H2/H3
  static constexpr unsigned HdrBufSize = 1<<10; // 1k scratch buffer

  // Builders provide only the initial field section; outbound trailers are
  // unsupported.  A Builder instance represents exactly one message.

  // May be non-constexpr for a type-erased Builder. The value is fixed for
  // the lifetime of this single-message Builder.
  constexpr BodyPolicy::T bodyPolicy() const { return BodyPolicy::None; }

  // Request start line / pseudo-headers. l(method, emit) is called exactly
  // once.  The protocol calls emit(write), then write(tx) writes the complete
  // path plus any query directly to the protocol transmit sink.
  template <typename Emit>
  void operation(Emit &&l) const {
    l(Method::GET, [](auto &&emit) {
      emit([](auto &tx) { tx << '/'; });
    });
  }

  // Response status code; ignored for requests.
  unsigned status() const { return 200; }

  // For each declared Key, header<Key>(l) is called exactly once per message;
  // it may synchronously call l(value) zero or one times.  For each fixed
  // (Key, Value) declaration,
  // header<Key, Value>(l) is called; it may synchronously call l() zero or one
  // times to control emission.  Its keyed provider may emit one additional
  // value.
  //
  // The non-template header(l) provider is called exactly once per message,
  // unconditionally.  It may synchronously call l(key, value) zero or more
  // times, with different keys and values.  This inherited fallback
  // deliberately does nothing.
  //
  // Only fixed declarations seed HPACK.  Fixed declarations and the names of
  // runtime-valued declarations may seed QPACK.  Values produced by either
  // provider are never seed inputs; no attempt is made to compress a declared
  // runtime value, although its name may use a static or frozen-table index.
  template <typename Key, typename Value, typename L>
  constexpr void header(L &&l) const { l(); }
  template <typename Key, typename L>
  void header(L &&) const { }
  template <typename L> void header(L &&l) const { }

  // Called only for body-bearing policies. During body(emit), emit(write) must
  // initially be called once for required bodies and zero or one times for
  // optional bodies. A streaming writer which returns Stream permits later
  // sequential calls through a retained emitter. body(emit) is not called for
  // messages without a body.
  //
  // emit(write) invokes write(bodyStream) synchronously on the caller's
  // current shard; the library never retains write. write returns End when
  // the body is complete, Stream when a streaming response remains open,
  // Abort to intentionally abort it, or Failed on application failure.
  // Stream and Abort are valid only for streaming server responses. After a
  // Stream result the application may retain emit and invoke it again, but is
  // responsible for dispatching every invocation to the response Tx shard.
  // Immediate sequential turns are permitted after Stream; zero-byte Stream
  // turns are valid and the initial one commits the response headers. A
  // streaming response must not supply Content-Length.
  // The emitter is invalidated by close() or by destruction of the response
  // Builder implementation; invoking an invalid emitter returns false without
  // invoking its writer. The transmit queue is bounded, and queue refusal
  // fails the stream. Fixed bodies are limited to FixedBodyMax; larger bodies
  // must stream.
  template <typename Emit> void body(Emit &&emit) const { }

  // The server ResBuilder base adds close(), called exactly once when a
  // streaming response producer becomes unusable. The application must
  // destroy any retained emitter there and must not invoke it from close().

  // Called exactly once for a successfully produced fixed body, synchronously
  // after body output and initial-header emission, but before those header
  // bytes become externally visible.
  // l.template operator()<Key>(patcher), patcher(ZuSpan<uint8_t> value).
  // There is no contentLength() callback; emit mutable Content-Length data
  // from header<Key>() with contentLengthPad(), then overwrite its
  // span here with contentLengthSet(). Failing to overwrite the complete
  // value is an application error.
  template <typename L> void bodyHdrs(L &&l) const { }
};

template <typename Key, typename Value, typename B, typename L,
  typename = decltype(ZuDeclVal<B * &>()->template header<Key,
    Value>(ZuFwd<L>(ZuDeclVal<L &>())), void())>
void builderFixedHeader(B *builder, L &&l, int) {
  builder->template header<Key, Value>(ZuFwd<L>(l));
}
template <typename Key, typename Value, typename B, typename L>
void builderFixedHeader(B *, L &&l, ...) { ZuFwd<L>(l)(); }

// Low-level protocol Parser CRTP contract for the H1/H2/H3 aliases below.
// The role facades wrap plain application Parser sinks in these
// adapters; application sinks do not derive from them. The protocol invokes
// only the callbacks applicable to the selected request/response role and
// version. Inherited defaults are side-effect-safe. All callbacks are
// synchronous and Rx-shard-affine. Target and received spans are borrowed for
// the duration of the callback. `ProtocolParser` in the API sketch denotes
// the selected alias below.
#if 0
struct ParserAdapter : public ProtocolParser<ParserAdapter, Headers> {
  using Base = ProtocolParser<ParserAdapter, Headers>;
  using State = typename Base::State;

  // identical to app-facing Parser member functions (see above):
  // - enable1xx, operation, status, header, bodyInfo, bool body(Rx &)
  // complete takes the protocol parser state rather than a success boolean;
  // it is called once per parsed message
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
  typename HdrCatalog = DefltHdrCatalog>
using H1RequestParser = H1::Parser<Impl, true, HdrCatalog>;

template <
  typename Impl,
  typename HdrCatalog = DefltHdrCatalog>
using H1ResponseParser = H1::Parser<Impl, false, HdrCatalog>;

template <
  typename Impl,
  typename HdrCatalog = DefltHdrCatalog>
using H2RequestParser = H2::Parser<Impl, true, HdrCatalog>;

template <
  typename Impl,
  typename HdrCatalog = DefltHdrCatalog>
using H2ResponseParser = H2::Parser<Impl, false, HdrCatalog>;

template <
  typename Impl,
  typename HdrCatalog = DefltHdrCatalog>
using H3RequestParser = H3::Parser<Impl, true, HdrCatalog>;

template <
  typename Impl,
  typename HdrCatalog = DefltHdrCatalog>
using H3ResponseParser = H3::Parser<Impl, false, HdrCatalog>;

// Low-level protocol Builder CRTP adapters used internally by the role
// facades. Application Builders do not derive from these aliases. The
// adapters provide protocol framing and invoke the application through
// inversion-of-control lambdas.  Builders emit initial headers only.

template <
  typename Impl,
  typename HdrCatalog = DefltHdrCatalog,
  bool HasBody = false, bool Chunked = false>
using H1Request =
  H1::Request<Impl, HdrCatalog, HasBody, Chunked>;

template <
  typename Impl,
  typename HdrCatalog = DefltHdrCatalog,
  bool HasBody = false, bool Chunked = false>
using H1Response =
  H1::Response<Impl, HdrCatalog, HasBody, Chunked>;

template <
  typename Impl,
  typename HdrCatalog = DefltHdrCatalog,
  bool HasBody = false>
using H2Request =
  H2::Request<Impl, HdrCatalog, HasBody, false>;

template <
  typename Impl,
  typename HdrCatalog = DefltHdrCatalog,
  bool HasBody = false>
using H2Response =
  H2::Response<Impl, HdrCatalog, HasBody, false>;

template <
  typename Impl,
  typename HdrCatalog = DefltHdrCatalog,
  bool HasBody = false>
using H3Request =
  H3::Request<Impl, HdrCatalog, HasBody, false>;

template <
  typename Impl,
  typename HdrCatalog = DefltHdrCatalog,
  bool HasBody = false>
using H3Response =
  H3::Response<Impl, HdrCatalog, HasBody, false>;

// Application message callback contract
//
// Header callbacks precede the first body callback.  Body callbacks receive a
// populated queue-backed ZiRxStream-compatible object on the owning Rx shard.
// Each callback is a synchronous prompt to consume complete application
// frames.  An incomplete trailing frame remains queued for the next prompt;
// the application must not retain the stream reference outside the callback.
//
// Body completion follows successful HTTP framing.  The protocol issues the
// last data prompt before the separate terminal result, then discards any
// unread decoded bytes.  No message callback is made after that result.
//
// Application Tx producers receive a concrete ZiTxStream-compatible body
// stream on the owning Tx shard.  The producer writes entity bytes only;
// libZhttp owns framing and final end-of-stream mapping.

template <
  typename Profile,
  typename Traits = Zhttp::ProfileTraits<Profile>>
struct MessageTraits;

template <int> struct HttpTraits;

template <> struct HttpTraits<Version::H1> {
  enum {
    ID = Version::H1,
    OneMessagePerLink = false,
    CloseDelimited = true
  };

  template <typename Impl, typename HdrCatalog>
  using RequestParser = H1::Parser<Impl, true, HdrCatalog>;
  template <typename Impl, typename HdrCatalog>
  using ResponseParser = H1::Parser<Impl, false, HdrCatalog>;
  template <typename Impl, typename HdrCatalog, bool HasBody, bool Chunked>
  using Request =
    H1::Request<Impl, HdrCatalog, HasBody, Chunked>;
  template <typename Impl, typename HdrCatalog, bool HasBody, bool Chunked>
  using Response =
    H1::Response<Impl, HdrCatalog, HasBody, Chunked>;
};

template <> struct HttpTraits<Version::H2> {
  enum {
    ID = Version::H2,
    OneMessagePerLink = true,
    CloseDelimited = false
  };

  template <typename Impl, typename HdrCatalog>
  using RequestParser = H2::Parser<Impl, true, HdrCatalog>;
  template <typename Impl, typename HdrCatalog>
  using ResponseParser = H2::Parser<Impl, false, HdrCatalog>;
  template <typename Impl, typename HdrCatalog, bool HasBody, bool Streaming>
  using Request =
    H2::Request<Impl, HdrCatalog, HasBody, Streaming>;
  template <typename Impl, typename HdrCatalog, bool HasBody, bool Streaming>
  using Response =
    H2::Response<Impl, HdrCatalog, HasBody, Streaming>;
};

template <> struct HttpTraits<Version::H3> {
  enum {
    ID = Version::H3,
    OneMessagePerLink = true,
    CloseDelimited = false
  };

  template <typename Impl, typename HdrCatalog>
  using RequestParser = H3::Parser<Impl, true, HdrCatalog>;
  template <typename Impl, typename HdrCatalog>
  using ResponseParser = H3::Parser<Impl, false, HdrCatalog>;
  template <typename Impl, typename HdrCatalog, bool HasBody, bool Streaming>
  using Request =
    H3::Request<Impl, HdrCatalog, HasBody, Streaming>;
  template <typename Impl, typename HdrCatalog, bool HasBody, bool Streaming>
  using Response =
    H3::Response<Impl, HdrCatalog, HasBody, Streaming>;
};

template <typename Profile, typename Traits>
struct MessageTraits :
  public HttpTraits<Traits::HTTPVersion> {
  using Transport = typename Traits::Transport;
  enum { Multiplexed = Traits::Multiplexed };
};

template <typename U, typename Emit, typename = void>
struct HasBuilderBody : public ZuFalse { };
template <typename U, typename Emit>
struct HasBuilderBody<U, Emit, decltype(
  ZuDeclVal<U &>().body(ZuDeclVal<Emit>()), void())> : public ZuTrue { };

template <typename U, typename Key, typename L, typename = void>
struct HasBuilderHeader : public ZuFalse { };
template <typename U, typename Key, typename L>
struct HasBuilderHeader<U, Key, L, decltype(
  ZuDeclVal<U &>().template header<Key>(ZuDeclVal<L>()), void())> :
  public ZuTrue { };

template <typename U, typename L, typename = void>
struct HasBuilderBodyHdrs : public ZuFalse { };
template <typename U, typename L>
struct HasBuilderBodyHdrs<U, L, decltype(
  ZuDeclVal<U &>().bodyHdrs(ZuDeclVal<L>()), void())> : public ZuTrue { };

template <typename Write, typename Stream>
WriteOutcome::T invokeBodyWriter(Write &&write, Stream &stream) {
  return ZuFwd<Write>(write)(stream);
}

struct HeaderSeed {
  H3::QPackTxString	name;
  H3::QPackTxString	value;
  uint32_t		size = 0;
  bool			exact = false;
};

using HeaderSeeds =
  ZtArray<HeaderSeed, ZtArrayHeapID<"Zhttp.HeaderSeeds">>;

// Initialization-owned immutable source for connection-local HPACK/QPACK
// bootstrap state.  Application Builder instances are never consulted.
class HeaderSeedCatalog {
public:
  template <typename HdrCatalog>
  void add(const H3::Params &params, uint32_t capacity) {
    using List = HeaderList<HdrCatalog>;
    ZuUnroll::all<List::N>([this, &params, capacity](auto I) {
      using Key = typename List::template Key<I>;
      using Value = typename List::template Value<I>;
      H3::QPackTxString name;
      name << Key{}();
      if (!name || name[0] == ':' || Fields::forbidden(name) ||
	  params.neverIndex(name))
	return;
      if constexpr (!Value::N) {
	uint64_t index = 0;
	if (H3::QPack::staticNameIndex(name, index) || findName_(name)) return;
	add_(ZuMv(name), {}, false, capacity);
      } else {
	H3::QPackTxString value;
	value << HeaderValue<Value>{}();
	if (H3::QPack::staticIndex(name, value) >= 0 ||
	    findExact_(name, value))
	  return;
	removeNameOnly_(name);
	add_(ZuMv(name), ZuMv(value), true, capacity);
      }
    });
  }

  const HeaderSeeds &entries() const { return m_entries; }
  bool operator !() const { return !m_entries; }
  ZuOpBool

private:
  bool findName_(ZuBSpan name) const {
    for (unsigned i = 0, n = m_entries.length(); i < n; ++i)
      if (m_entries[i].name == name) return true;
    return false;
  }
  bool findExact_(ZuBSpan name, ZuBSpan value) const {
    for (unsigned i = 0, n = m_entries.length(); i < n; ++i)
      if (m_entries[i].exact && m_entries[i].name == name &&
	  m_entries[i].value == value)
	return true;
    return false;
  }
  void removeNameOnly_(ZuBSpan name) {
    for (unsigned i = 0, n = m_entries.length(); i < n; ++i) {
      if (m_entries[i].exact || m_entries[i].name != name) continue;
      m_entries.splice(i, 1);
      return;
    }
  }
  void add_(H3::QPackTxString name, H3::QPackTxString value,
      bool exact, uint32_t capacity) {
    uint64_t size = uint64_t(name.length()) + value.length() + 32;
    if (size > capacity) return;
    new (m_entries.push()) HeaderSeed{
      ZuMv(name), ZuMv(value), uint32_t(size), exact};
  }

  HeaderSeeds	m_entries;
};

struct HPackSeed {
  H3::QPackTxString	name;
  H3::QPackTxString	value;
  H2::HPackBytes	incremental;
  H2::HPackBytes	fallback;
  uint32_t		size = 0;
};

using HPackSeeds =
  ZtArray<HPackSeed, ZtArrayHeapID<"Zhttp.HPackSeeds">>;

struct HPackSeedPlan {
  HPackSeeds	entries;
};

using HPackSeedPlans =
  ZtArray<HPackSeedPlan, ZtArrayHeapID<"Zhttp.HPackSeedPlans">>;

class HPackSeedCatalog {
public:
  template <typename HdrCatalog>
  void add(uint32_t capacity) {
    HPackSeedPlan plan;
    using List = HeaderList<HdrCatalog>;
    ZuUnroll::all<List::N>([&plan, capacity](auto I) {
      using Key = typename List::template Key<I>;
      using Value = typename List::template Value<I>;
      if constexpr (Value::N) {
	H3::QPackTxString name;
	H3::QPackTxString value;
	name << Key{}();
	value << HeaderValue<Value>{}();
	if (!name || name[0] == ':' || Fields::forbidden(name) ||
	    H2::HPack::staticIndex(name, value) > 0 ||
	    sensitive_(name) || find_(plan.entries, name, value))
	  return;
	uint64_t size = uint64_t(name.length()) + value.length() + 32;
	if (size > capacity) return;
	HPackSeed seed;
	seed.name = ZuMv(name);
	seed.value = ZuMv(value);
	seed.size = uint32_t(size);
	int nameIndex = H2::HPack::staticNameIndex(seed.name);
	if (Compression::putPref(seed.incremental, 0x40, 6,
	      nameIndex > 0 ? unsigned(nameIndex) : 0) < 0 ||
	    (nameIndex <= 0 && Compression::putString(
	      seed.incremental, 0, 7, seed.name) < 0) ||
	    Compression::putString(
	      seed.incremental, 0, 7, seed.value) < 0 ||
	    Compression::putPref(seed.fallback, 0, 4,
	      nameIndex > 0 ? unsigned(nameIndex) : 0) < 0 ||
	    (nameIndex <= 0 && Compression::putString(
	      seed.fallback, 0, 7, seed.name) < 0) ||
	    Compression::putString(seed.fallback, 0, 7, seed.value) < 0)
	  return;
	new (plan.entries.push()) HPackSeed{ZuMv(seed)};
      }
    });
    new (m_plans.push()) HPackSeedPlan{ZuMv(plan)};
  }

  const HPackSeedPlans &plans() const { return m_plans; }
  const HPackSeeds &entries() const {
    static const HPackSeeds empty;
    return m_plans ? m_plans[0].entries : empty;
  }

private:
  static bool sensitive_(ZuBSpan name) {
    return name == "authorization" || name == "cookie" ||
      name == "set-cookie";
  }
  static bool find_(
      const HPackSeeds &entries, ZuBSpan name, ZuBSpan value) {
    for (unsigned i = 0, n = entries.length(); i < n; ++i)
      if (entries[i].name == name && entries[i].value == value)
	return true;
    return false;
  }

  HPackSeedPlans	m_plans;
};

ZtEnumStruct(ZhttpAPI, QPackSeedState, uint8_t,
  Unseeded, Seeded, Disabled);

ZtEnumStruct(ZhttpAPI, QPackSeedResult, int8_t,
  Failed, Disabled, Seeded);

template <typename Write>
QPackSeedResult::T installQPackSeeds(
    H3::QPackTxTable &tx, const HeaderSeeds &seeds, Write &&write) {
  uint32_t capacity = tx.effectiveCapacity();
  if (!capacity || !seeds) {
    tx.freeze();
    return QPackSeedResult::Disabled;
  }
  uint64_t used = 0;
  unsigned count = 0, n = seeds.length();
  while (count < n) {
    if (seeds[count].size > capacity - used) break;
    used += seeds[count++].size;
  }
  if (!count) {
    tx.freeze();
    return QPackSeedResult::Disabled;
  }
  H3::HdrBytes bytes;
  H3::HdrBytes insn;
  if (H3::QPack::encodeSetCapacity(insn, capacity) < 0)
    return QPackSeedResult::Failed;
  Compression::putBytes(bytes, insn);
  for (unsigned i = 0; i < count; ++i) {
    insn.length(0);
    uint64_t nameIndex = 0;
    int n = H3::QPack::staticNameIndex(seeds[i].name, nameIndex) ?
      H3::QPack::encodeInsertWithNameRef(
	insn, nameIndex, false, seeds[i].value) :
      H3::QPack::encodeInsertLiteral(
	insn, {seeds[i].name, seeds[i].value});
    if (n < 0) return QPackSeedResult::Failed;
    Compression::putBytes(bytes, insn);
  }
  if (!ZuFwd<Write>(write)(bytes)) return QPackSeedResult::Failed;
  if (!tx.setCapacity(capacity)) return QPackSeedResult::Failed;
  tx.capacitySent = true;
  for (unsigned i = 0; i < count; ++i)
    if (!tx.insertView({seeds[i].name, seeds[i].value}))
      return QPackSeedResult::Failed;
  tx.freeze();
  return QPackSeedResult::Seeded;
}

template <typename App, typename = void>
struct ResponseHeaderSets { using T = ZuTypeList<>; };
template <typename App>
struct ResponseHeaderSets<App, decltype(
  sizeof(typename App::ResponseHeaders), void())> {
  using T = typename App::ResponseHeaders;
};

template <typename App, typename = void>
struct AppHeaderSeeds {
  static const HeaderSeeds &get(const App &) {
    static const HeaderSeeds seeds;
    return seeds;
  }
};
template <typename App>
struct AppHeaderSeeds<App, decltype(
  ZuDeclVal<const App &>().qpackSeeds(), void())> {
  static const HeaderSeeds &get(const App &app) { return app.qpackSeeds(); }
};

template <typename App, typename = void>
struct AppHPackSeedPlans {
  static const HPackSeedPlans &get(const App &) {
    static const HPackSeedPlans plans;
    return plans;
  }
};
template <typename App>
struct AppHPackSeedPlans<App, decltype(
  ZuDeclVal<const App &>().hpackSeedPlans(), void())> {
  static const HPackSeedPlans &get(const App &app) {
    return app.hpackSeedPlans();
  }
};

struct RetainedBudget {
  bool add(uint64_t n) {
    if (n > max - size) {
      valid = false;
      return false;
    }
    size += n;
    return true;
  }

  uint64_t	max = uint64_t(-1);
  uint64_t	size = 0;
  bool		valid = true;
};

struct RetainedEntry {
  RetainedEntry() = default;
  RetainedEntry(ZmRef<ZiIOBuf> buf_, bool final_) :
    buf{ZuMv(buf_)}, final{final_} { }

  ZmRef<ZiIOBuf>	buf;
  bool		final;
};

template <typename Lower>
class RetainedTx : public ZiTxStream<RetainedTx<Lower>> {
  using Base = ZiTxStream<RetainedTx<Lower>>;
  using Entries =
    ZtArray<RetainedEntry, ZtArrayHeapID<"Zhttp.RetainedTx.Entries">>;

public:
  RetainedTx(Lower &lower_, RetainedBudget &budget_) :
    Base{lower_.maxSize(), lower_.headRoom(), lower_.tailRoom()},
    m_lower{lower_}, m_budget{budget_} { }

  ~RetainedTx() { this->flush(); }

  ZmRef<ZiIOBuf> allocBuf_(unsigned headRoom) {
    return m_lower.allocBuf_(headRoom);
  }
  bool sendBuf_(ZmRef<ZiIOBuf> buf, bool final) {
    if (!m_budget.add(buf->length)) {
      m_valid = false;
      return false;
    }
    new (m_entries.push()) RetainedEntry{ZuMv(buf), final};
    return true;
  }

  bool seal() {
    this->flush();
    return valid();
  }
  bool valid() const { return m_valid && m_budget.valid; }
  void commit() {
    if (!valid()) return;
    for (unsigned i = 0, n = m_entries.length(); i < n; ++i)
      if (!m_lower.sendBuf_(ZuMv(m_entries[i].buf), m_entries[i].final))
	break;
    m_entries.length(0);
  }

private:
  Lower		&m_lower;
  RetainedBudget &m_budget;
  Entries	m_entries;
  bool		m_valid = true;
};

template <
  typename HdrCatalog,
  unsigned N = HeaderList<HdrCatalog>::N>
class HeaderSpans {
  using List = HeaderList<HdrCatalog>;
  using Keys = typename List::Keys;

  ZuAssert((ZuTypeUnique<Keys>::N == Keys::N),
    "mutable header spans require unique keys");

public:
  template <typename Key>
  void record(ZuSpan<uint8_t> span) {
    constexpr unsigned I = ZuTypeIndex<Key, Keys>{};
    using Value = typename List::template Value<I>;
    ZuAssert((!Value::N),
      "only runtime-valued declared headers have mutable spans");
    m_slots[I] = span;
  }
  template <typename Key>
  void recordOffset(uint64_t offset, unsigned length) {
    constexpr unsigned I = ZuTypeIndex<Key, Keys>{};
    using Value = typename List::template Value<I>;
    ZuAssert((!Value::N),
      "only runtime-valued declared headers have mutable spans");
    m_slots[I] = {reinterpret_cast<uint8_t *>(uintptr_t(offset + 1)), length};
  }
  void resolve(uint8_t *base) {
    for (unsigned i = 0; i < N; ++i) {
      auto encoded = uintptr_t(m_slots[i].data());
      if (encoded) m_slots[i] = {base + encoded - 1, m_slots[i].length()};
    }
  }

  template <typename App>
  void patch(App &app) {
    auto patch = [this]<typename Key, typename P>(P &&patcher) {
      constexpr unsigned I = ZuTypeIndex<Key, Keys>{};
      using Value = typename List::template Value<I>;
      ZuAssert((!Value::N),
	"bodyHdrs cannot mutate a fixed declared header value");
      ZuFwd<P>(patcher)(m_slots[I]);
    };
    if constexpr (HasBuilderBodyHdrs<App, decltype(patch)>{})
      app.bodyHdrs(ZuMv(patch));
  }

private:
  ZuSpan<uint8_t>	m_slots[N]{};
};

template <typename HdrCatalog>
class HeaderSpans<HdrCatalog, 0> {
public:
  template <typename Key>
  void record(ZuSpan<uint8_t>) {
    ZuAssert((!ZuIsSame<Key, Key>{}),
      "header span key is not declared");
  }
  template <typename Key>
  void recordOffset(uint64_t, unsigned) {
    ZuAssert((!ZuIsSame<Key, Key>{}),
      "header span key is not declared");
  }
  void resolve(uint8_t *) { }
  template <typename App>
  void patch(App &) { }
};

// Shared compile-time outbound message mechanics.  Ops keeps request/response
// accounting, failure, empty-body, and budget policy in the owning component.
template <typename Message, typename Ops>
class MessageTx {
public:
  MessageTx(Ops &ops) : m_ops{&ops} { }

  template <typename Builder>
  bool streaming(Builder &builder, bool optional) {
    auto &link = m_ops->link();
    auto tx = link.transmit_(builder);
    bool emitted = false;
    bool duplicate = false;
    WriteOutcome::T outcome = WriteOutcome::Failed;
    bool headersOK = false;
    uint64_t produced = 0;
    if (optional) {
      builder.emitBody([
	this, &builder, &tx, &emitted, &duplicate, &outcome,
	&headersOK, &produced](auto &&write) {
	if (emitted) { duplicate = true; return; }
	emitted = true;
	if (!(headersOK = begin_(builder, tx))) return;
	m_ops->headers();
	auto body = builder.body(tx);
	outcome = invokeBodyWriter(ZuFwd<decltype(write)>(write), body);
	body.flush();
	produced = body.produced();
	m_ops->template produced<true>(produced);
	if (!body.valid()) outcome = WriteOutcome::Failed;
	if (outcome == WriteOutcome::End) builder.finish(tx);
      });
      if (!emitted) return m_ops->empty(builder.appBuilder());
    } else {
      if (!(headersOK = begin_(builder, tx)))
	return m_ops->template fail<true>();
      m_ops->headers();
      builder.emitBody([
	this, &builder, &tx, &emitted, &duplicate, &outcome, &produced](
	    auto &&write) {
	if (emitted) { duplicate = true; return; }
	emitted = true;
	auto body = builder.body(tx);
	outcome = invokeBodyWriter(ZuFwd<decltype(write)>(write), body);
	body.flush();
	produced = body.produced();
	m_ops->template produced<true>(produced);
	if (!body.valid()) outcome = WriteOutcome::Failed;
      });
      if (emitted && outcome == WriteOutcome::End) builder.finish(tx);
    }
    if (!validCardinality_(optional, emitted, duplicate) ||
	!writerEnded_(outcome) ||
	!headersOK)
      return m_ops->template fail<true>();
    if (!m_ops->complete(produced)) return false;
    link.finish();
    return true;
  }

  template <typename Builder>
  bool fixed(Builder &builder, bool optional) {
    auto &link = m_ops->link();
    auto native = link.transmit_(builder);
    if constexpr (Message::ID == Version::H2)
      return fixedH2_(builder, native, optional);
    else {
      RetainedBudget budget{.max = m_ops->retainedMax()};
      RetainedTx headerTx{native, budget};
      RetainedTx bodyTx{native, budget};
      auto body = builder.body(bodyTx, m_ops->fixedBodyMax());
      bool emitted = false;
      bool duplicate = false;
      WriteOutcome::T outcome = WriteOutcome::Failed;
      builder.emitBody([
	&body, &emitted, &duplicate, &outcome](auto &&write) {
	if (emitted) { duplicate = true; return; }
	emitted = true;
	outcome = invokeBodyWriter(ZuFwd<decltype(write)>(write), body);
      });
      body.flush();
      if (emitted) {
	builder.produced = body.produced();
	m_ops->template produced<false>(builder.produced);
      }
      if (!validCardinality_(optional, emitted, duplicate) ||
	  (emitted && (!writerEnded_(outcome) || !body.valid())))
	return m_ops->template fail<false>();
      if (!emitted) return m_ops->empty(builder.appBuilder());
      if (!begin_(builder, headerTx)) return m_ops->template fail<false>();
      m_ops->headers();
      builder.finish(bodyTx);
      if (!headerTx.seal() || !bodyTx.seal())
	return m_ops->template fail<false>();
      headerTx.commit();
      bodyTx.commit();
      if (!m_ops->complete(builder.produced)) return false;
      link.finish();
      return true;
    }
  }

private:
  static bool validCardinality_(
      bool optional, bool emitted, bool duplicate) {
    ZiAssert(optional || emitted, "Zhttp", (),
      "required body emitted no writer", return false);
    ZiAssert(!duplicate, "Zhttp", (),
      "body emitted more than one writer", return false);
    return (optional || emitted) && !duplicate;
  }

  static bool writerEnded_(WriteOutcome::T outcome) {
    switch (outcome) {
      case WriteOutcome::End: return true;
      case WriteOutcome::Failed: return false;
      case WriteOutcome::Stream:
      case WriteOutcome::Abort:
	ZiAssert(false, "Zhttp", (),
	  "streaming outcome returned from a single-turn writer", return false);
	return false;
      default:
	ZiAssert(false, "Zhttp", (), "invalid body writer outcome", return false);
	return false;
    }
  }

  template <typename Builder, typename Tx>
  static bool begin_(Builder &builder, Tx &tx) {
    using R = decltype(builder.begin(tx));
    if constexpr (ZuIsSame<R, void>{})
      builder.begin(tx);
    else if (!builder.begin(tx))
      return false;
    return true;
  }

  template <typename Builder, typename Tx>
  bool fixedH2_(Builder &builder, Tx &tx, bool optional) {
    tx.defer(m_ops->retainedMax());
    auto body = builder.body(tx, m_ops->fixedBodyMax());
    bool emitted = false;
    bool duplicate = false;
    WriteOutcome::T outcome = WriteOutcome::Failed;
    builder.emitBody([
      &body, &emitted, &duplicate, &outcome](auto &&write) {
      if (emitted) { duplicate = true; return; }
      emitted = true;
      outcome = invokeBodyWriter(ZuFwd<decltype(write)>(write), body);
    });
    body.flush();
    if (emitted) {
      builder.produced = body.produced();
      m_ops->template produced<false>(builder.produced);
    }
    if (!validCardinality_(optional, emitted, duplicate) ||
	(emitted && (!writerEnded_(outcome) || !body.valid())))
      return m_ops->template fail<false>();
    if (!emitted) return m_ops->empty(builder.appBuilder());
    if (!begin_(builder, tx))
      return m_ops->template fail<false>();
    m_ops->headers();
    builder.finish(tx);
    if (!tx.valid()) return m_ops->template fail<false>();
    tx.commit();
    if (!m_ops->complete(builder.produced)) return false;
    m_ops->link().finish();
    return true;
  }

  Ops	*m_ops;
};

} // namespace Zhttp

#endif /* Zhttp_HH */
