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

#include <zlib/ZuBox.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuFmt.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuStream.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZiAssert.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZhttpCore.hh>
#include <zlib/ZhttpFields.hh>
#include <zlib/ZhttpProfiles.hh>
#include <zlib/ZhttpConfig.hh>
#include <zlib/ZhttpURL.hh>
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

// Endpoint lifecycle is init -> start -> stop(done) -> final. The stop
// continuation follows the existing Rx/Tx drains and final permitted callbacks.
// Application callbacks receive borrowed channels; Route retains the actual
// owning node for asynchronous server response work. Native transport, wire and
// compression machinery remains private to the library.
//
// Rx owns request/response parsing and stream input. Tx owns builders, producers
// and queues. Initialization and finalization remain on the control thread.

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
  using Headers = HdrCatalog::List;

  Parser() = default;

  template <typename Context>
  void init(Context &) { }

  bool finishBody() { return true; }

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
  // The channel is borrowed for the duration of the callback. An
  // application that retains it for asynchronous work must explicitly acquire
  // ownership.
  template <typename Link>
  void complete(Link *, bool ok) { }

  void reset() { }
};

struct Builder {
  bool streamResponse() const { return false; }
  // Alternating Key, Values pairs. Values is ZuTypeList<> for a run-time
  // value, or a singleton list containing the fixed value.
  using HdrCatalog = DefltHdrCatalog;
  using Headers = HdrCatalog::List;

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


template <typename App, typename = void>
struct ResponseHeaderSets { using T = ZuTypeList<>; };
template <typename App>
struct ResponseHeaderSets<App, decltype(
  sizeof(typename App::ResponseHeaders), void())> {
  using T = App::ResponseHeaders;
};

template <typename HdrCatalog, typename Sets,
  bool = bool(ZuTypeIn<HdrCatalog, Sets>{})>
struct HeaderPlan_ {
  enum { Valid = 0, Index = unsigned(-1) };
};
template <typename HdrCatalog, typename Sets>
struct HeaderPlan_<HdrCatalog, Sets, true> {
  enum { Valid = 1, Index = ZuTypeIndex<HdrCatalog, Sets>{} };
};
template <typename HdrCatalog, typename Sets, bool = bool(Sets::N)>
struct HeaderPlan {
  enum { Valid = 0, Index = unsigned(-1) };
};
template <typename HdrCatalog, typename Sets>
struct HeaderPlan<HdrCatalog, Sets, true> :
    public HeaderPlan_<HdrCatalog, Sets> {
};

template <
  typename HdrCatalog,
  unsigned N = HeaderList<HdrCatalog>::N>
class HeaderSpans {
  using List = HeaderList<HdrCatalog>;
  using Keys = List::Keys;
  template <typename Key>
  struct Mutable : public ZuBool<!List::template Value<
    ZuTypeIndex<Key, Keys>{}>::N> { };
  using MutableKeys = ZuTypeGrep<Mutable, Keys>;
  template <typename> using Span = ZuSpan<uint8_t>;
  using Slots = ZuTypeApply<ZuTuple, ZuTypeMap<Span, MutableKeys>>;

  ZuAssert((ZuTypeUnique<Keys>::N == Keys::N),
    "mutable header spans require unique keys");

public:
  template <typename Key>
  void record(ZuSpan<uint8_t> span) {
    constexpr unsigned I = ZuTypeIndex<Key, Keys>{};
    using Value = List::template Value<I>;
    ZuAssert((!Value::N),
      "only runtime-valued declared headers have mutable spans");
    m_slots.template p<ZuTypeIndex<Key, MutableKeys>{}>() = span;
  }
  template <typename Key>
  void recordOffset(uint64_t offset, unsigned length) {
    constexpr unsigned I = ZuTypeIndex<Key, Keys>{};
    using Value = List::template Value<I>;
    ZuAssert((!Value::N),
      "only runtime-valued declared headers have mutable spans");
    m_slots.template p<ZuTypeIndex<Key, MutableKeys>{}>() =
      {reinterpret_cast<uint8_t *>(uintptr_t(offset + 1)), length};
  }
  void resolve(uint8_t *base) {
    ZuUnroll::all<MutableKeys::N>([this, base](auto i) {
      auto &span = m_slots.template p<i>();
      auto encoded = uintptr_t(span.data());
      if (encoded) span = {base + encoded - 1, span.length()};
    });
  }

  template <typename App>
  void patch(App &app) {
    auto patch = [this]<typename Key, typename P>(P &&patcher) {
      constexpr unsigned I = ZuTypeIndex<Key, Keys>{};
      using Value = List::template Value<I>;
      ZuAssert((!Value::N),
	"bodyHdrs cannot mutate a fixed declared header value");
      ZuFwd<P>(patcher)(m_slots.template p<ZuTypeIndex<Key, MutableKeys>{}>());
    };
    if constexpr (HasBuilderBodyHdrs<App, decltype(patch)>{})
      app.bodyHdrs(ZuMv(patch));
  }

private:
  Slots m_slots{};
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

// Builder is the existing typed request/response Ops adapter, including existing
// content-length suppression and Alt-Svc injection; those rules stay there.
template <typename Builder>
struct BuilderFields {
  using Catalog = typename Builder::HdrCatalog;
  using List = HeaderList<Catalog>;

  Builder *builder;
  const CatalogView *catalog;

  void fields(FieldOutput &out) {
    ZuUnroll::all<List::N>([this, &out](auto I) {
      static constexpr unsigned ID = I;
      using Key = typename List::template Key<I>;
      using Values = typename List::template Value<I>;
      const auto &meta = catalog->fields[I];
      if constexpr (Values::N) {
	using Fixed = HeaderValue<Values>;
	builderFixedHeader<Key, Fixed>(builder, [&out, &meta]() {
	  const auto &value = Fixed{}();
	  out.field(FieldForm::Fixed, ID, meta.classification,
	    PrintView::borrow(meta.name), PrintView::borrow(value));
	}, 0);
	builder->template header<Key>([&out, &meta]<typename V>(V &&value) {
	  out.field(FieldForm::Name, ID, meta.classification,
	    PrintView::borrow(meta.name), PrintView::borrow(value));
	});
      } else {
	builder->template header<Key>([this, &out, &meta]<typename V>(V &&value) {
	  auto span = out.field(FieldForm::Mutable, ID, meta.classification,
	    PrintView::borrow(meta.name), PrintView::borrow(value));
	  builder->template headerOffset<Key>(span.offset, span.length);
	});
      }
    });
    builder->header([&out]<typename K, typename V>(K &&key, V &&value) {
      out.field(FieldForm::Literal, -1, nullptr,
	PrintView::borrow(key), PrintView::borrow(value));
    });
  }
  FieldProvider provider() {
    return {this,
      [](void *ctx, FieldOutput &out) {
	static_cast<BuilderFields *>(ctx)->fields(out);
      },
      [](void *ctx, uint8_t *base) {
	static_cast<BuilderFields *>(ctx)->builder->headerBase(base);
      },
      [](void *ctx) { static_cast<BuilderFields *>(ctx)->builder->patch(); }};
  }
};


template <typename P>
auto parserBodyMax(P &parser, uint64_t max, int) ->
    decltype(parser.bodyMax(max), void()) {
  parser.bodyMax(max);
}
template <typename P>
void parserBodyMax(P &, uint64_t, long) { }

template <typename P>
auto parserBodyInfo(P &parser, Channel *channel, BodyType::T type,
    uint64_t length, int) -> decltype(parser.bodyInfo(channel, type, length)) {
  return parser.bodyInfo(channel, type, length);
}
template <typename P>
bool parserBodyInfo(P &parser, Channel *, BodyType::T type, uint64_t length, long) {
  return parser.bodyInfo(type, length);
}

template <typename P, typename Context, typename = void>
struct HasParserOwner : ZuFalse { };
template <typename P, typename Context>
struct HasParserOwner<P, Context, decltype(ZuDeclVal<P &>().initOwner(
    ZuDeclVal<Context &>()), void())> : ZuTrue { };

template <typename P, typename Context, typename = void>
struct HasParserInfo : ZuFalse { };
template <typename P, typename Context>
struct HasParserInfo<P, Context, decltype(ZuDeclVal<P &>().connectedInfo(
    ZuDeclVal<Context &>(), ZuDeclVal<Channel *>(),
    ZuDeclVal<const ConnectedInfo &>()), void())> : ZuTrue { };
template <typename P, typename = void>
struct HasParserDown : ZuFalse { };
template <typename P>
struct HasParserDown<P, decltype(ZuDeclVal<P &>().disconnected(ZuDeclVal<Channel *>(), false),
  void())> : ZuTrue { };

// The actual typed parser object belongs to this session/operation storage.
// The native owner creates it once and destroys it only after its shard drains.
template <typename P, typename Context, auto ContextAxor = ZuDefaultAxor()>
struct TypedParser : public ZmNHeap<TypedParser<P, Context, ContextAxor>> {
  P parser;

  friend inline ZuStringT<"Zhttp.Parser"> ZmNHeapID(TypedParser *);

  static ParserBinding create(const CatalogView &catalog) {
    static constexpr ParserOps ops = []() {
      ParserOps table{
      [](void *ctx, void *context) {
	static_cast<TypedParser *>(ctx)->parser.init(
	  ContextAxor(*static_cast<Context *>(context)));
      },
      [](void *ctx, uint64_t max) {
	parserBodyMax(static_cast<TypedParser *>(ctx)->parser, max, 0);
      },
      [](void *ctx) { delete static_cast<TypedParser *>(ctx); },
      [](const void *ctx) -> bool {
	return static_cast<const TypedParser *>(ctx)->parser.enable1xx();
      },
      [](void *ctx, Method::T method, Target &target) -> bool {
	return static_cast<TypedParser *>(ctx)->parser.operation(method, target);
      },
      [](void *ctx, unsigned status) {
	static_cast<TypedParser *>(ctx)->parser.status(status);
      },
      [](void *ctx, FieldSection::T section, int key, int fixed,
	  ZuBSpan name, ZuSpan<uint8_t> value) {
	parserHeader(static_cast<TypedParser *>(ctx)->parser,
	  section, key, fixed, name, value);
      },
      [](void *ctx, Channel *channel, BodyType::T type, uint64_t length) -> bool {
	return parserBodyInfo(static_cast<TypedParser *>(ctx)->parser,
	  channel, type, length, 0);
      },
      [](void *ctx, Input &input) -> bool {
	return static_cast<TypedParser *>(ctx)->parser.body(input);
      },
      [](void *ctx, Channel *channel, bool ok) {
	static_cast<TypedParser *>(ctx)->parser.complete(channel, ok);
      },
      [](void *ctx) { static_cast<TypedParser *>(ctx)->parser.reset(); }};
      if constexpr (HasParserInfo<P, Context>{}) table.connectedInfo =
	[](void *ctx, void *owner, Channel *channel, const ConnectedInfo &info) {
	  static_cast<TypedParser *>(ctx)->parser.connectedInfo(
	    ContextAxor(*static_cast<Context *>(owner)), channel, info);
	};
      if constexpr (HasParserDown<P>{}) table.disconnected =
	[](void *ctx, Channel *channel, bool peer) {
	  static_cast<TypedParser *>(ctx)->parser.disconnected(channel, peer);
	};
      return table;
    }();
    return {new TypedParser{}, &ops, &catalog};
  }
  static ParserBinding create(const CatalogView &catalog, Context &owner) {
    auto binding = create(catalog);
    if constexpr (HasParserOwner<P, Context>{})
      static_cast<TypedParser *>(binding.ctx)->parser.initOwner(ContextAxor(owner));
    return binding;
  }
};

template <typename B>
struct ResponseFields {
  using HdrCatalog = typename B::HdrCatalog;
  using Spans = HeaderSpans<HdrCatalog>;

  B *builder;
  FieldContext &context;
  Spans spans;

  template <typename Key, typename Value, typename L>
  void header(L &&l) {
    if constexpr (Key{}() == "content-length") {
      if (context.rejectContentLength) {
	builderFixedHeader<Key, Value>(builder, [this]() { reject_(); }, 0);
	return;
      }
    }
    builderFixedHeader<Key, Value>(builder, ZuFwd<L>(l), 0);
  }
  template <typename Key, typename L>
  void header(L &&l) {
    if constexpr (Key{}() == "content-length") {
      if (context.rejectContentLength) {
	if constexpr (HasBuilderHeader<B, Key, L &&>{})
	  builder->template header<Key>([this](auto &&) { reject_(); });
	return;
      }
    }
    if constexpr (HasBuilderHeader<B, Key, L &&>{})
      builder->template header<Key>(ZuFwd<L>(l));
  }
  template <typename L>
  void header(L &&l) {
    builder->header([this, &l]<typename K, typename V>(K &&key, V &&value) {
      if (context.rejectContentLength && ZuBSpan{key} == "content-length") {
	reject_();
	return;
      }
      l(ZuFwd<K>(key), ZuFwd<V>(value));
    });
    if (context.altSvc) l("alt-svc", context.altSvc);
  }
  template <typename Key>
  void headerOffset(uint64_t offset, unsigned length) {
    spans.template recordOffset<Key>(offset, length);
  }
  void headerBase(uint8_t *base) { spans.resolve(base); }
  void patch() { spans.patch(*builder); }
  void reject_() {
    context.invalidHeader = true;
    context.reject(context.rejectCtx);
  }
};

// A route transfers the actual outer node reference, with pointer adjustment
// performed before erasure. No embedded record is ever used as an allocation.
template <typename Node>
ResponseRef bindResponse(ZmRef<Node> response, const CatalogView &catalog) {
  using B = ZuDecay<decltype(response->data())>;
  static constexpr ResponseOps ops{
    [](const void *ctx) -> unsigned { return static_cast<const Node *>(ctx)->data().status(); },
    [](const void *ctx) -> Method::T { return static_cast<const Node *>(ctx)->data().method(); },
    [](const void *ctx) -> BodyPolicy::T {
      return static_cast<const Node *>(ctx)->data().bodyPolicy();
    },
    [](const void *ctx) -> bool {
      return static_cast<const Node *>(ctx)->data().streamResponse();
    },
    [](const void *ctx) -> bool { return static_cast<const Node *>(ctx)->data().disconnect(); },
    [](void *ctx, BodyEmitter emit) {
      auto &builder = static_cast<Node *>(ctx)->data();
      if constexpr (HasBuilderBody<B, BodyEmitter &>{}) builder.body(emit);
    },
    [](void *ctx) { static_cast<Node *>(ctx)->data().close(); },
    [](void *ctx, const ResponseResult &result) {
      static_cast<Node *>(ctx)->data().completed(result);
    },
    [](void *ctx, FieldContext &context, void *encoder,
	void (*put)(void *, FieldProvider &)) {
      auto &builder = static_cast<Node *>(ctx)->data();
      ResponseFields<B> fields{&builder, context};
      BuilderFields<ResponseFields<B>> adapter{&fields, builder.record.catalog};
      auto provider = adapter.provider();
      put(encoder, provider);
    }};
  auto &builder = response->data();
  auto &record = builder.record;
  record.bind(response.ptr());
  record.responseOps = &ops;
  record.catalog = &catalog;
  record.hdrBufSize = B::HdrBufSize;
  record.hdrPlan = HeaderPlan<typename B::HdrCatalog,
    typename ResponseHeaderSets<B>::T>::Index;
  return ResponseRef::take(&record, ZuMv(response));
}

template <typename Node>
void Channel::send(ZmRef<Node> response) {
  if (!response) return;
  send(bindResponse(ZuMv(response), responseCatalog()));
}

template <typename L>
void Channel::rxRun(L &&l) { mx()->run(ZuFwd<L>(l), rxThread()); }

template <typename L>
void Channel::txRun(L &&l) { mx()->run(ZuFwd<L>(l), txThread()); }

template <typename Write>
bool Channel::streamTx(Write &&write) {
  auto printer = Builder_::writer(write);
  return m_channelOps->streamWrite(ctx, PrintView::borrow(printer));
}

} // namespace Zhttp

#endif /* Zhttp_HH */
