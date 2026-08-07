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

// Parser and Builder are plain application structs wrapped in protocol CRTP
// adapters by the client and server facades; neither inherits a Zhttp base. The
// protocol invokes only the callbacks applicable to the selected request or
// response role and version. Every lambda call is synchronous. Printable
// Builder values retain their actual type. Received spans and body Rx streams
// are borrowed only for the duration of the callback.
#if 0
struct Parser {
  using Headers = ZhttpHeaders(...);
  static constexpr uint64_t BodyMax = DefltMaxBody;

  // Constructed once per logical stream and destroyed once when that stream
  // ends. reset() is called exactly once before each message, including the
  // first, and clears all per-message application state.
  Parser();
  ~Parser();
  void reset();

  // Called first, before any header() or body() callback. A subsequent
  // validation failure is reported by complete(false).
  void operation(Method::T, const RequestTarget &);	// requests only
  // Called first, before any header() or body() callback. A subsequent
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
  // or trailer fields. endStream is meaningful for the initial section.
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

  // May be constructed once and retained across messages. Called exactly
  // once before each message, including the first, to clear per-message
  // construction state while preserving the configured request/response.
  void reset();

  template <typename Key, typename L> void header(L &&l); // l(value)
  template <typename L> void header(L &&l);		   // l(key, value)

  // Present only for body-bearing policies. emit(write) is called zero or
  // one times according to BodyPolicy::Optional; write(bodyStream) returns
  // void or bool.
  template <typename Emit> void body(Emit &&emit);

  // Present only for fixed policies; called synchronously after body output.
  // l.template operator()<Key>(patcher), patcher(ZuSpan<uint8_t> value).
  // There is no contentLength() callback; emit a Content-Length Placeholder
  // from header<Key>(), then overwrite its mutable span here. Failing to
  // overwrite the complete placeholder is an application error.
  template <typename L> void bodyHdrs(L &&l);
};
#endif

// Low-level protocol Parser CRTP contract for the H1/H2/H3 aliases below.
// The role facades wrap plain application Parser sinks in these
// adapters; application sinks do not derive from them. The protocol invokes
// only the callbacks applicable to the selected request/response role and
// version. Inherited defaults are side-effect-safe; an adapter which
// overrides reset() must call Base::reset(). All callbacks are synchronous
// and Rx-shard-affine. RequestTarget and received spans are borrowed for the
// duration of the callback. `ProtocolParser` in the API sketch denotes the
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
using H1RequestParser = H1::Parser<Impl, true, Headers, MaxBody>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  uint64_t MaxBody = DefltMaxBody>
using H1ResponseParser = H1::Parser<Impl, false, Headers, MaxBody>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  uint64_t MaxBody = DefltMaxBody>
using H2RequestParser = H2::Parser<Impl, true, Headers, MaxBody>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  uint64_t MaxBody = DefltMaxBody>
using H2ResponseParser = H2::Parser<Impl, false, Headers, MaxBody>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  uint64_t MaxBody = DefltMaxBody>
using H3RequestParser = H3::Parser<Impl, true, Headers, MaxBody>;

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  uint64_t MaxBody = DefltMaxBody>
using H3ResponseParser = H3::Parser<Impl, false, Headers, MaxBody>;

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

  template <
    typename Impl, typename Headers, uint64_t MaxBody>
  using RequestParser = H1::Parser<Impl, true, Headers, MaxBody>;
  template <
    typename Impl, typename Headers, uint64_t MaxBody>
  using ResponseParser = H1::Parser<Impl, false, Headers, MaxBody>;
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

  template <
    typename Impl, typename Headers, uint64_t MaxBody>
  using RequestParser = H2::Parser<Impl, true, Headers, MaxBody>;
  template <
    typename Impl, typename Headers, uint64_t MaxBody>
  using ResponseParser = H2::Parser<Impl, false, Headers, MaxBody>;
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

  template <
    typename Impl, typename Headers, uint64_t MaxBody>
  using RequestParser = H3::Parser<Impl, true, Headers, MaxBody>;
  template <
    typename Impl, typename Headers, uint64_t MaxBody>
  using ResponseParser = H3::Parser<Impl, false, Headers, MaxBody>;
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

template <typename U, typename = void>
struct ParserBodyMax { static constexpr uint64_t V = uint64_t(-1); };
template <typename U>
struct ParserBodyMax<U, decltype((void)U::BodyMax, void())> {
  static constexpr uint64_t V = U::BodyMax;
};

template <typename Write, typename Stream>
bool invokeBodyWriter(Write &&write, Stream &stream) {
  using R = decltype(ZuFwd<Write>(write)(stream));
  ZuAssert((ZuIsSame<R, void>{} || ZuIsSame<R, bool>{}),
    "body writer must return void or bool");
  if constexpr (ZuIsSame<R, void>{}) {
    ZuFwd<Write>(write)(stream);
    return true;
  } else
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
	ZuSpan<uint8_t> span{reinterpret_cast<uint8_t *>(data), n};
	if constexpr (Persist)
	  *v.span = span;
	else
	  v.app->bodyHdrs(
	    [&span]<typename K, typename P>(P &&patcher) {
	      if constexpr (ZuIsSame<K, Key>{})
		ZuFwd<P>(patcher)(span);
	    });
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
    app.bodyHdrs([this]<typename Key, typename P>(P &&patcher) {
      constexpr unsigned I = ZuTypeIndex<Key, Keys>{};
      ZuFwd<P>(patcher)(m_slots.template p<I>());
    });
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
  bool streaming(Builder &builder) {
    auto &link = m_ops->link();
    auto tx = link.transmit(builder);
    bool emitted = false;
    bool duplicate = false;
    bool writerOK = false;
    bool headersOK = false;
    uint64_t produced = 0;
    if constexpr (Builder::Optional) {
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
    if ((!Builder::Optional && !emitted) || duplicate || !writerOK ||
	!headersOK)
      return m_ops->template fail<true>();
    if (!m_ops->complete(produced)) return false;
    link.finish();
    return true;
  }

  template <typename Builder>
  bool fixed(Builder &builder) {
    auto &link = m_ops->link();
    auto native = link.transmit(builder);
    if constexpr (Message::ID == Version::H2)
      return fixedH2_(builder, native);
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
	(void)headerTx;
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
      if ((!Builder::Optional && !emitted) || duplicate ||
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
  bool fixedH2_(Builder &builder, Tx &tx) {
    tx.defer(m_ops->retainedMax());
    auto body = builder.body(tx, m_ops->fixedBodyMax());
    bool emitted = false;
    bool duplicate = false;
    bool writerOK = false;
    bool headersOK = true;
    builder.emitBody([
      &builder, &body, &emitted, &duplicate, &writerOK, &headersOK](
	  auto &&write) {
      if (emitted) { duplicate = true; return; }
      emitted = true;
      writerOK = invokeBodyWriter(ZuFwd<decltype(write)>(write), body);
    });
    body.flush();
    if (emitted) {
      builder.produced = body.produced();
      m_ops->template produced<false>(builder.produced);
    }
    if ((!Builder::Optional && !emitted) || duplicate ||
	(emitted && (!headersOK || !writerOK || !body.valid())))
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
