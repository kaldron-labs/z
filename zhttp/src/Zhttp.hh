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

#include <zlib/ZuICmp.hh>
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
#include <zlib/ZhttpH3.hh>
#include <zlib/ZhttpTransport.hh>

namespace Zhttp {

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
// Parser lifecycle is facade-specific rather than part of this common callback
// contract: Server obtains a request Parser from App::parser() for each request;
// Client calls ResParser::init() before each response message.
struct Parser { // base class with defaulted types and member functions
  using Headers = ZuTypeList<>; // ZhttpHeaders(...);

  // Informational response field sections are suppressed by default. Return
  // true to receive their status() and header() callbacks.
  constexpr bool enable1xx() const { return false; }

  // First protocol callback for a request, called exactly once after the
  // start line or pseudo-headers are validated and before any header().
  void operation(Method::T, const Target &) { }	// requests only

  // First protocol callback for each response field section, called after
  // its :status or status line is validated and before that section's
  // header() callbacks. Informational sections may precede the final one.
  void status(unsigned) { }				// responses only

  // Declared run-time value, declared fixed value, undeclared key/value.
  // Initial-section callbacks follow operation()/status(); trailer callbacks
  // follow bodyInfo() and any body() prompts.
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuBSpan value) { }
  template <typename Key, typename Value>
  void header(Zhttp::FieldSection::T) { }
  void header(Zhttp::FieldSection::T, ZuBSpan key, ZuBSpan value) { }

  // Called once after the final initial field section and before body().
  void bodyInfo(BodyType::T, uint64_t length) { }

  // Synchronous queue prompt; incomplete application framing may remain
  // queued for a later decoded-body append. Return true to continue parsing,
  // or false to reject the message. Rejection suppresses later message
  // callbacks except for exactly one eventual complete(false).
  template <typename Rx> bool body(Rx &) { return false; }

  // Last callback for the message. A validation or framing failure may
  // short-circuit the successful ordering above and report false.  LinkRef is
  // an owning concrete-link handle and may be moved out for asynchronous work.
  template <typename LinkRef>
  void complete(LinkRef &&, bool ok) { }
};

struct Builder {
  using Headers = ZuTypeList<>; // ZhttpHeaders(...);
  using Trailers = ZuTypeList<>; // ZhttpHeaders(...);	// optional

  // May be non-constexpr for a type-erased Builder. The value is fixed from
  // reset() until message construction completes.
  constexpr BodyPolicy::T bodyPolicy() const { return BodyPolicy::None; }

  // May be constructed once and retained across messages. Called exactly
  // once before each message, including the first, to clear per-message
  // construction state while preserving the configured request/response.
  void reset() { }

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

  template <typename Key, typename L> void header(L &&l) const { } // l(value)
  template <typename L> void header(L &&l) const { }		     // l(key, value)

  // Called only for body-bearing policies. emit(write) must be called
  // once for required bodies, zero or one times for optional bodies.
  // body(emit) will not be called for messages without a body.
  // write(bodyStream) returns true on success, false on failure.
  template <typename Emit> void body(Emit &&emit) const { }

  // Called only for fixed policies, synchronously after body output.
  // l.template operator()<Key>(patcher), patcher(ZuSpan<uint8_t> value).
  // There is no contentLength() callback; emit a Content-Length Placeholder
  // from header<Key>(), then overwrite its mutable span here. Failing to
  // overwrite the complete placeholder is an application error.
  template <typename L> void bodyHdrs(L &&l) const { }
};

// Low-level protocol Parser CRTP contract for the H1/H2/H3 aliases below.
// The role facades wrap plain application Parser sinks in these
// adapters; application sinks do not derive from them. The protocol invokes
// only the callbacks applicable to the selected request/response role and
// version. Inherited defaults are side-effect-safe; an adapter which
// overrides reset() must call Base::reset(). All callbacks are synchronous
// and Rx-shard-affine. Target and received spans are borrowed for the
// duration of the callback. `ProtocolParser` in the API sketch denotes the
// selected alias below.
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
  typename Headers = ZuTypeList<>>
using H1RequestParser = H1::Parser<Impl, true, Headers>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>>
using H1ResponseParser = H1::Parser<Impl, false, Headers>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>>
using H2RequestParser = H2::Parser<Impl, true, Headers>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>>
using H2ResponseParser = H2::Parser<Impl, false, Headers>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>>
using H3RequestParser = H3::Parser<Impl, true, Headers>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>>
using H3ResponseParser = H3::Parser<Impl, false, Headers>;

// Low-level protocol Builder CRTP adapters used internally by the role
// facades. Application Builders do not derive from these aliases. The
// adapters provide protocol framing and invoke the application through
// inversion-of-control lambdas. H1 chunked builders emit Trailers from
// finish(); H2/H3 builders emit a trailing HEADERS section.

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>,
  bool HasBody = false, bool Chunked = false>
using H1Request =
  H1::Request<Impl, Headers, Trailers, HasBody, Chunked>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>,
  bool HasBody = false, bool Chunked = false>
using H1Response =
  H1::Response<Impl, Headers, Trailers, HasBody, Chunked>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>,
  bool HasBody = false>
using H2Request =
  H2::Request<Impl, Headers, Trailers, HasBody, false>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>,
  bool HasBody = false>
using H2Response =
  H2::Response<Impl, Headers, Trailers, HasBody, false>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>,
  bool HasBody = false>
using H3Request =
  H3::Request<Impl, Headers, Trailers, HasBody, false>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>,
  bool HasBody = false>
using H3Response =
  H3::Response<Impl, Headers, Trailers, HasBody, false>;

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

  template <typename Impl, typename Headers>
  using RequestParser = H1::Parser<Impl, true, Headers>;
  template <typename Impl, typename Headers>
  using ResponseParser = H1::Parser<Impl, false, Headers>;
  template <
    typename Impl, typename Headers, typename Trailers,
    bool HasBody, bool Chunked>
  using Request =
    H1::Request<Impl, Headers, Trailers, HasBody, Chunked>;
  template <
    typename Impl, typename Headers, typename Trailers,
    bool HasBody, bool Chunked>
  using Response =
    H1::Response<Impl, Headers, Trailers, HasBody, Chunked>;
};

template <> struct HttpTraits<Version::H2> {
  enum {
    ID = Version::H2,
    OneMessagePerLink = true,
    CloseDelimited = false
  };

  template <typename Impl, typename Headers>
  using RequestParser = H2::Parser<Impl, true, Headers>;
  template <typename Impl, typename Headers>
  using ResponseParser = H2::Parser<Impl, false, Headers>;
  template <
    typename Impl, typename Headers, typename Trailers,
    bool HasBody, bool Streaming>
  using Request =
    H2::Request<Impl, Headers, Trailers, HasBody, Streaming>;
  template <
    typename Impl, typename Headers, typename Trailers,
    bool HasBody, bool Streaming>
  using Response =
    H2::Response<Impl, Headers, Trailers, HasBody, Streaming>;
};

template <> struct HttpTraits<Version::H3> {
  enum {
    ID = Version::H3,
    OneMessagePerLink = true,
    CloseDelimited = false
  };

  template <typename Impl, typename Headers>
  using RequestParser = H3::Parser<Impl, true, Headers>;
  template <typename Impl, typename Headers>
  using ResponseParser = H3::Parser<Impl, false, Headers>;
  template <
    typename Impl, typename Headers, typename Trailers,
    bool HasBody, bool Streaming>
  using Request =
    H3::Request<Impl, Headers, Trailers, HasBody, Streaming>;
  template <
    typename Impl, typename Headers, typename Trailers,
    bool HasBody, bool Streaming>
  using Response =
    H3::Response<Impl, Headers, Trailers, HasBody, Streaming>;
};

template <typename Profile, typename Traits>
struct MessageTraits :
  public HttpTraits<Traits::HTTPVersion> {
  using Transport = typename Traits::Transport;
  enum { Multiplexed = Traits::Multiplexed };
};

template <typename U, typename = void>
struct BuilderTrailers { using T = ZuTypeList<>; };
template <typename U>
struct BuilderTrailers<U, decltype(sizeof(typename U::Trailers), void())> {
  using T = typename U::Trailers;
};

template <typename U, typename Emit, typename = void>
struct HasBuilderBody : public ZuFalse { };
template <typename U, typename Emit>
struct HasBuilderBody<U, Emit, decltype(
  ZuDeclVal<U &>().body(ZuDeclVal<Emit>()), void())> : public ZuTrue { };

template <typename U, typename L, typename = void>
struct HasBuilderBodyHdrs : public ZuFalse { };
template <typename U, typename L>
struct HasBuilderBodyHdrs<U, L, decltype(
  ZuDeclVal<U &>().bodyHdrs(ZuDeclVal<L>()), void())> : public ZuTrue { };

template <typename Write, typename Stream>
bool invokeBodyWriter(Write &&write, Stream &stream) {
  return ZuFwd<Write>(write)(stream);
}

template <typename Headers>
bool validRuntimeHeader(ZuCSpan name, bool h1) {
  if (ZuICmp<ZuCSpan>::equals(name, "content-length") ||
      (h1 && ZuICmp<ZuCSpan>::equals(name, "transfer-encoding")))
    return false;
  using Keys = ZuTypeSlice<2, 0, Headers>;
  bool valid = true;
  ZuUnroll::all<Keys>([&valid, name]<typename Key>() {
    if (ZuICmp<ZuCSpan>::equals(name, Key{}())) valid = false;
  });
  return valid;
}

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
    for (unsigned i = 0; i < m_entries.length(); ++i)
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

template <typename Headers>
class HeaderPatches {
  using Keys = ZuTypeSlice<2, 0, Headers>;
  template <typename> using SlotT = ZuSpan<uint8_t>;
  using Slots = ZuTypeApply<ZuTuple, ZuTypeMap<SlotT, Keys>>;

public:
  template <typename Key, typename App, bool Persist>
  struct Value {
    struct Print : public ZuPrintBuffer {
      static unsigned length(const Value &v) {
	return v.placeholder.length;
      }
      static unsigned print(char *data, unsigned, const Value &v) {
	auto n = v.placeholder.length;
	if (n) memset(data, v.placeholder.fill, n);
	ZuSpan<char> chars{data, n};
	ZuSpan<uint8_t> span = chars;
	if constexpr (Persist)
	  *v.span = span;
	else {
	  auto patch = [&span]<typename K, typename P>(P &&patcher) {
	    if constexpr (ZuIsSame<K, Key>{})
	      ZuFwd<P>(patcher)(span);
	  };
	  if constexpr (HasBuilderBodyHdrs<App, decltype(patch)>{})
	    v.app->bodyHdrs(ZuMv(patch));
	}
	return n;
      }
    };

    Placeholder		placeholder;
    ZuSpan<uint8_t>	*span = nullptr;
    App			*app = nullptr;

    friend Print ZuPrintType(Value *);
  };

  template <bool Wire, typename Key, typename App, typename L>
  void header(App &app, L &&l) {
    app.template header<Key>([this, &app, &l]<typename V>(V &&v) {
      if constexpr (IsPlaceholder<ZuDecay<V>>{}) {
	constexpr unsigned I = ZuTypeIndex<Key, Keys>{};
	l(Value<Key, App, Wire>{
	  v, &m_slots.template p<I>(), &app});
      } else
	l(ZuFwd<V>(v));
    });
  }

  template <typename App>
  void patch(App &app) {
    auto patch = [this]<typename Key, typename P>(P &&patcher) {
      constexpr unsigned I = ZuTypeIndex<Key, Keys>{};
      ZuFwd<P>(patcher)(m_slots.template p<I>());
    };
    if constexpr (HasBuilderBodyHdrs<App, decltype(patch)>{})
      app.bodyHdrs(ZuMv(patch));
  }

private:
  Slots	m_slots;
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
    auto tx = link.transmit(builder);
    bool emitted = false;
    bool duplicate = false;
    bool writerOK = false;
    bool headersOK = false;
    uint64_t produced = 0;
    if (optional) {
      builder.emitBody([
	this, &builder, &tx, &emitted, &duplicate, &writerOK,
	&headersOK, &produced](auto &&write) {
	if (emitted) { duplicate = true; return; }
	emitted = true;
	if (!(headersOK = begin_(builder, tx))) return;
	m_ops->headers();
	auto body = builder.body(tx);
	writerOK = invokeBodyWriter(ZuFwd<decltype(write)>(write), body);
	body.flush();
	produced = body.produced();
	m_ops->template produced<true>(produced);
	if (!body.valid()) writerOK = false;
	if (writerOK) builder.finish(tx);
      });
      if (!emitted) return m_ops->empty(builder.appBuilder());
    } else {
      if (!(headersOK = begin_(builder, tx)))
	return m_ops->template fail<true>();
      m_ops->headers();
      builder.emitBody([
	this, &builder, &tx, &emitted, &duplicate, &writerOK, &produced](
	    auto &&write) {
	if (emitted) { duplicate = true; return; }
	emitted = true;
	auto body = builder.body(tx);
	writerOK = invokeBodyWriter(ZuFwd<decltype(write)>(write), body);
	body.flush();
	produced = body.produced();
	m_ops->template produced<true>(produced);
	if (!body.valid()) writerOK = false;
      });
      if (emitted && writerOK) builder.finish(tx);
    }
    if ((!optional && !emitted) || duplicate || !writerOK ||
	!headersOK)
      return m_ops->template fail<true>();
    if (!m_ops->complete(produced)) return false;
    link.finish();
    return true;
  }

  template <typename Builder>
  bool fixed(Builder &builder, bool optional) {
    auto &link = m_ops->link();
    auto native = link.transmit(builder);
    if constexpr (Message::ID == Version::H2)
      return fixedH2_(builder, native, optional);
    else {
      if constexpr (Message::ID == Version::H3) builder.deferCompression();
      RetainedBudget budget{.max = m_ops->retainedMax()};
      RetainedTx headerTx{native, budget};
      RetainedTx bodyTx{native, budget};
      auto body = builder.body(bodyTx, m_ops->fixedBodyMax());
      bool emitted = false;
      bool duplicate = false;
      bool writerOK = false;
      bool headersOK = true;
      builder.emitBody([
	&builder, &headerTx, &body,
	&emitted, &duplicate, &writerOK, &headersOK](auto &&write) {
	(void)builder;
	(void)headerTx;
	(void)headersOK;
	if (emitted) { duplicate = true; return; }
	emitted = true;
	if constexpr (Message::ID == Version::H1)
	  if (!(headersOK = begin_(builder, headerTx))) return;
	writerOK = invokeBodyWriter(ZuFwd<decltype(write)>(write), body);
      });
      body.flush();
      if (emitted) {
	builder.produced = body.produced();
	m_ops->template produced<false>(builder.produced);
      }
      if ((!optional && !emitted) || duplicate ||
	  (emitted && (!headersOK || !writerOK || !body.valid())))
	return m_ops->template fail<false>();
      if (!emitted) return m_ops->empty(builder.appBuilder());
      if constexpr (Message::ID == Version::H1)
	builder.patch();
      else
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
  template <typename Builder, typename Tx>
  static bool begin_(Builder &builder, Tx &tx) {
    using R = decltype(builder.begin(tx));
    if constexpr (ZuIsSame<R, void>{})
      builder.begin(tx);
    else if (!builder.begin(tx))
      return false;
    return builder.headersValid();
  }

  template <typename Builder, typename Tx>
  bool fixedH2_(Builder &builder, Tx &tx, bool optional) {
    tx.defer(m_ops->retainedMax());
    auto body = builder.body(tx, m_ops->fixedBodyMax());
    bool emitted = false;
    bool duplicate = false;
    bool writerOK = false;
    builder.emitBody([
      &body, &emitted, &duplicate, &writerOK](auto &&write) {
      if (emitted) { duplicate = true; return; }
      emitted = true;
      writerOK = invokeBodyWriter(ZuFwd<decltype(write)>(write), body);
    });
    body.flush();
    if (emitted) {
      builder.produced = body.produced();
      m_ops->template produced<false>(builder.produced);
    }
    if ((!optional && !emitted) || duplicate ||
	(emitted && (!writerOK || !body.valid())))
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
