//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - transport-neutral message session utilities

#ifndef ZhttpMessage_HH
#define ZhttpMessage_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/ZhttpTransport.hh>

#include <zlib/ZuUnion.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZuICmp.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>

#include <string.h>

namespace Zhttp {

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

template <
  typename App, typename Request, typename Link, typename Profile,
  typename RequestBuilder, typename ResponseParser>
class ClientMessage;

} // namespace Zhttp

#ifndef Zhttp_HH
#include <zlib/Zhttp.hh>
#endif

#include <zlib/ZuTL.hh>

namespace Zhttp {

template <int> struct MessageVersion;

template <> struct MessageVersion<Version::H1> {
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
  using RequestBuilder =
    H1::RequestBuilder<Impl, Headers, Trailers, HasBody, Chunked>;
  template <
    typename Impl, typename Headers, typename Trailers,
    bool HasBody, bool Chunked>
  using ResponseBuilder =
    H1::ResponseBuilder<Impl, Headers, Trailers, HasBody, Chunked>;
};

template <> struct MessageVersion<Version::H2> {
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
  using RequestBuilder =
    H2::RequestBuilder<Impl, Headers, Trailers, HasBody, Streaming>;
  template <
    typename Impl, typename Headers, typename Trailers,
    bool HasBody, bool Streaming>
  using ResponseBuilder =
    H2::ResponseBuilder<Impl, Headers, Trailers, HasBody, Streaming>;
};

template <> struct MessageVersion<Version::H3> {
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
  using RequestBuilder =
    H3::RequestBuilder<Impl, Headers, Trailers, HasBody, Streaming>;
  template <
    typename Impl, typename Headers, typename Trailers,
    bool HasBody, bool Streaming>
  using ResponseBuilder =
    H3::ResponseBuilder<Impl, Headers, Trailers, HasBody, Streaming>;
};

template <typename Profile, typename Traits>
struct MessageTraits :
  public MessageVersion<Traits::HTTPVersion> {
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
  static_assert(ZuIsSame<R, void>{} || ZuIsSame<R, bool>{},
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

struct HeaderPatchSlot {
  ZtString<ZtStringHeapID<"Zhttp.HeaderPatch.Value">> value;
  ZuSpan<uint8_t>	wire;
  bool		present = false;
  bool		provisioned = false;
  bool		patched = false;
};

struct HeaderPatchWireValue {
  struct Print : public ZuPrintBuffer {
    static unsigned length(const HeaderPatchWireValue &v) {
      return v.slot->value.length();
    }
    static unsigned print(char *data, unsigned,
	const HeaderPatchWireValue &v) {
      auto n = v.slot->value.length();
      if (n) memcpy(data, v.slot->value.data(), n);
      v.slot->wire = {data, n};
      return n;
    }
  };

  HeaderPatchSlot *slot;

  friend Print ZuPrintType(HeaderPatchWireValue *);
};

template <typename Headers>
class HeaderPatches {
  using Keys = ZuTypeSlice<2, 0, Headers>;
  using Values = ZuTypeSlice<2, 1, Headers>;

  template <typename> using SlotT = HeaderPatchSlot;
  using Slots = ZuTypeApply<ZuTuple, ZuTypeMap<SlotT, Keys>>;

public:
  template <typename App>
  bool provision(App &app) {
    m_collected = true;
    bool ok = true;
    ZuUnroll::all<Keys>([this, &app, &ok]<typename Key>() {
      using Value = ZuType<ZuTypeIndex<Key, Keys>{}, Values>;
      if constexpr (ZuIsSame<Value, void>{}) {
	unsigned count = 0;
	app.template header<Key>(
	  [this, &count]<typename V>(V &&v) {
	    ++count;
	    auto &slot = m_slots.template p<ZuTypeIndex<Key, Keys>{}>();
	    if (count > 1) return;
	    slot.present = true;
	    if constexpr (IsHeaderPad<ZuDecay<V>>{}) {
	      slot.value.length(v.length);
	      if (v.length)
		memset(slot.value.data(), v.fill, v.length);
	      slot.provisioned = true;
	    } else
	      slot.value << ZuFwd<V>(v);
	  });
	if (count > 1) ok = false;
      }
    });
    if (!ok) m_valid = false;
    return ok;
  }

  template <bool Wire, typename Key, typename App, typename L>
  void header(App &app, L &&l) {
    if (!m_collected) {
      unsigned count = 0;
      app.template header<Key>([this, &l, &count]<typename V>(V &&v) {
	if (++count == 1) l(ZuFwd<V>(v));
	else m_valid = false;
      });
      return;
    }
    constexpr unsigned I = ZuTypeIndex<Key, Keys>{};
    auto &slot = m_slots.template p<I>();
    if (!slot.present) return;
    if constexpr (Wire)
      l(HeaderPatchWireValue{&slot});
    else
      l(ZuBSpan{slot.value});
  }

  bool valid() const { return m_valid; }
  void invalidate() { m_valid = false; }

  template <typename Key, typename Patcher>
  bool patch(Patcher &&patcher) {
    constexpr unsigned I = ZuTypeIndex<Key, Keys>{};
    static_assert(I < Keys::N, "bodyHdrs key is not declared in Headers");
    using Value = ZuType<I, Values>;
    static_assert(ZuIsSame<Value, void>{},
      "bodyHdrs key must be runtime-valued");
    auto &slot = m_slots.template p<I>();
    if (!slot.provisioned || slot.patched) return false;
    ZuSpan<uint8_t> value = slot.wire ? slot.wire :
      ZuSpan<uint8_t>{slot.value.data(), slot.value.length()};
    ZuFwd<Patcher>(patcher)(value);
    if (slot.wire && slot.value.length())
      memcpy(slot.value.data(), slot.wire.data(), slot.value.length());
    slot.patched = true;
    return true;
  }

  bool validate(uint64_t produced) const {
    bool ok = true;
    ZuUnroll::all<Keys>([this, &ok, produced]<typename Key>() {
      constexpr unsigned I = ZuTypeIndex<Key, Keys>{};
      const auto &slot = m_slots.template p<I>();
      if constexpr (Key{}() == "content-length")
	if (!slot.provisioned) { ok = false; return; }
      if (!slot.provisioned) return;
      if (!slot.patched) { ok = false; return; }
      for (unsigned i = 0; i < slot.value.length(); ++i)
	if (uint8_t(slot.value[i]) == 0xff) { ok = false; return; }
      if constexpr (Key{}() == "content-length") {
	uint64_t n = 0;
	if (!slot.value) { ok = false; return; }
	for (unsigned i = 0; i < slot.value.length(); ++i) {
	  unsigned c = uint8_t(slot.value[i]);
	  if (c < '0' || c > '9') { ok = false; return; }
	  n = n * 10 + c - '0';
	}
	if (n != produced) ok = false;
      }
    });
    return ok;
  }

private:
  Slots	m_slots;
  bool	m_collected = false;
  bool	m_valid = true;
};

// Protocol-neutral client message adapter.  App supplies request intent and
// response handling; HTTP-version-specific builders, parsers, EOF rules, and
// link completion remain library-owned.
template <
  typename App_, typename Request_, typename Link_, typename Profile_,
  typename RequestBuilder_, typename ResponseParser_>
class ClientMessage {
public:
  using App = App_;
  using Request = Request_;
  using Link = Link_;
  using Profile = Profile_;
  using RequestBuilder = RequestBuilder_;
  using ResponseParser = ResponseParser_;
  using ReqHeaders = typename RequestBuilder::Headers;
  using RespHeaders = typename ResponseParser::Headers;
  using ReqTrailers = typename BuilderTrailers<RequestBuilder>::T;
  using Message = MessageTraits<Profile>;
  using BodyPolicy = typename RequestBuilder::BodyPolicy;
  using ReqHeaderKeys = ZuTypeSlice<2, 0, ReqHeaders>;
  enum {
    ReqBody = BodyPolicy::HasBody,
    ReqStreaming = BodyPolicy::Streaming,
    ReqOptional = BodyPolicy::Optional
  };
  static constexpr uint64_t RespBodyMax = ParserBodyMax<ResponseParser>::V;
  static_assert(ReqStreaming || !ReqBody ||
    ZuTypeIn<ZuStringT<"content-length">, ReqHeaderKeys>{},
    "fixed request body requires content-length in Headers");
  static_assert(!ReqStreaming ||
    !ZuTypeIn<ZuStringT<"content-length">, ReqHeaderKeys>{},
    "streaming request body cannot declare content-length");
  static_assert(
    !ZuTypeIn<ZuStringT<"transfer-encoding">, ReqHeaderKeys>{},
    "libZhttp owns request transfer-encoding framing");

private:
  struct ReqOps {
    template <typename L>
    void operation(L &&l) { app.operation(ZuFwd<L>(l)); }
    template <typename L>
    void host(L &&l) { app.host(ZuFwd<L>(l)); }
    template <typename L>
    void protocol(L &&l) { app.protocol(ZuFwd<L>(l)); }
    template <typename Key, typename L>
    void header(L &&l) {
      if (suppressPads) {
	unsigned count = 0;
	app.template header<Key>([this, &l, &count]<typename V>(V &&v) {
	  if (++count > 1) {
	    patches.invalidate();
	    return;
	  }
	  if constexpr (!IsHeaderPad<ZuDecay<V>>{})
	    l(ZuFwd<V>(v));
	});
	return;
      }
      patches.template header<Message::ID == Version::H1, Key>(
	app, ZuFwd<L>(l));
    }
    template <typename L>
    void header(L &&l) {
      app.header([this, &l]<typename K, typename V>(K &&k, V &&v) {
	ZtString<ZtStringHeapID<"Zhttp.RuntimeHeader.Name">> name;
	name << k;
	if (!validRuntimeHeader<ReqHeaders>(
	      ZuCSpan{name}, Message::ID == Version::H1)) {
	  headersOK = false;
	  return;
	}
	l(ZuFwd<K>(k), ZuFwd<V>(v));
      });
    }

    bool provision() { return patches.provision(app); }
    bool patch(uint64_t produced) {
      bool ok = true;
      app.bodyHdrs(
	[this, &ok]<typename Key, typename Patcher>(Patcher &&patcher) {
	  if (!patches.template patch<Key>(ZuFwd<Patcher>(patcher)))
	    ok = false;
	});
      return ok && patches.validate(produced);
    }
    uint64_t contentLength() const { return produced; }
    bool headersValid() const { return headersOK && patches.valid(); }
    RequestBuilder takeApp() { return ZuMv(app); }

    RequestBuilder	app;
    HeaderPatches<ReqHeaders> patches;
    uint64_t		produced = 0;
    bool		headersOK = true;
    bool		suppressPads = false;
  };

  template <bool HasBody, bool Streaming>
  struct Builder_ :
    public Message::template RequestBuilder<
      Builder_<HasBody, Streaming>,
      ReqHeaders, ReqTrailers, HasBody, Streaming>,
    public ReqOps {
    using Base = typename Message::template RequestBuilder<
      Builder_, ReqHeaders, ReqTrailers, HasBody, Streaming>;

    Builder_(RequestBuilder app_, bool suppressPads = false) :
      ReqOps{ZuMv(app_)} {
      this->suppressPads = suppressPads;
    }

    using ReqOps::header;
    using ReqOps::host;
    using ReqOps::operation;
    using ReqOps::protocol;
    using ReqOps::contentLength;

    template <typename Emit>
    void emitBody(Emit &&emit) { this->app.body(ZuFwd<Emit>(emit)); }
  };

  struct ParserSink_ {
    void operation(Method::T, const RequestTarget &) { }
    void status(unsigned value) {
      app->status(*link, *request, sink(), value);
    }
    void contentLength(uint64_t value) {
      app->contentLength(*link, *request, sink(), value);
    }
    void chunked() { app->chunked(*link, *request, sink()); }
    void version(ZuBSpan value) {
      app->version(*link, *request, sink(), value);
    }
    template <typename Key>
    void header(ZuBSpan value) {
      app->template header<Key>(*link, *request, sink(), value);
    }
    template <typename Rx>
    void body(Rx &rx) {
      app->body(*link, *request, sink(), rx);
    }
    template <typename ParserState>
    void complete(typename ParserState::T state) {
      app->template complete<ParserState>(
	*link, *request, sink(), state);
    }

    ResponseParser &sink() { return sink_->template p<1>(); }

    App		*app = nullptr;
    Link	*link = nullptr;
    Request	*request = nullptr;
    ZuUnion<void, ResponseParser> *sink_ = nullptr;
  };

  struct Parser :
    public Message::template ResponseParser<
      Parser, RespHeaders, RespBodyMax>,
    public ParserSink_ {
    using Base = typename Message::template ResponseParser<
      Parser, RespHeaders, RespBodyMax>;
    using State = typename Base::State;

    void operation(Method::T method, const RequestTarget &target) {
      ParserSink_::operation(method, target);
    }
    void complete(typename State::T state) {
      ParserSink_::template complete<State>(state);
    }

    using ParserSink_::body;
    using ParserSink_::chunked;
    using ParserSink_::contentLength;
    using ParserSink_::header;
    using ParserSink_::status;
    using ParserSink_::version;
  };

public:
  using ParserState = typename Parser::State;

  ClientMessage(App *app = nullptr, Link *link = nullptr) :
    m_app{app}, m_link{link} { }

  void bind(Request *request) {
    m_request = request;
    if (!request) return;
    m_response.template p<1>(m_app->responseParser(*request));
    static_cast<ParserSink_ &>(m_parser) = {
      m_app, m_link, request, &m_response};
  }
  void reset() {
    m_parser.reset();
    if constexpr (Message::ID != Version::H1) {
      if (!m_request) return;
      auto app = m_app->requestBuilder(*m_request);
      app.operation([this](Method::T method, auto &&) {
	m_parser.requestMethod(method);
      });
    }
  }

  // Tx-owned synchronous request construction.
  void beginTx() {
    m_commit = {};
    m_txState = TxState::Active;
  }

  void cancelTx() {
    if (m_txState != TxState::Active) return;
    m_commit.reset = m_commit.committed;
    m_txState = TxState::Cancelled;
  }

  BodyCommit commit() const { return m_commit; }

  bool send() {
    if (m_txState != TxState::Active) return false;
    auto app = m_app->requestBuilder(*m_request);
    if constexpr (!ReqBody)
      return send_<false, false>(ZuMv(app));
    else if constexpr (ReqStreaming && ReqOptional)
      return sendOptionalStreaming_(ZuMv(app));
    else if constexpr (ReqStreaming)
      return send_<true, true>(ZuMv(app));
    else
      return sendFixed_(ZuMv(app));
  }

private:
  bool sendOptionalStreaming_(RequestBuilder app) {
    Builder_<true, true> builder{ZuMv(app)};
    auto tx = m_link->transmit(builder);
    bool emitted = false;
    bool duplicate = false;
    bool writerOK = false;
    bool headersOK = false;
    builder.emitBody([
      this, &builder, &tx, &emitted, &duplicate, &writerOK, &headersOK]
      (auto &&write) {
      if (emitted) { duplicate = true; return; }
      emitted = true;
      if (!(headersOK = builder.request(tx) && builder.headersValid()))
	return;
      m_commit.headers = true;
      auto body = builder.body(tx);
      writerOK = invokeBodyWriter(ZuFwd<decltype(write)>(write), body);
      body.flush();
      m_commit.produced = body.produced();
      m_commit.committed = m_commit.produced;
      if (!body.valid()) writerOK = false;
      if (writerOK) builder.finish(tx);
    });
    if (duplicate || (emitted && (!headersOK || !writerOK)))
      return failStreamedTx_();
    if (!emitted)
      return send_<false, false>(builder.takeApp(), true);
    m_link->finish();
    m_commit.final = true;
    m_txState = TxState::Complete;
    return true;
  }

  template <bool HasBody, bool Streaming>
  bool send_(RequestBuilder app, bool suppressPads = false) {
    Builder_<HasBody, Streaming> builder{ZuMv(app), suppressPads};
    auto tx = m_link->transmit(builder);
    if (!builder.request(tx) || !builder.headersValid())
      return failFor_<Streaming>();
    m_commit.headers = true;
    if constexpr (HasBody) {
      auto body = builder.body(tx);
      bool emitted = false;
      bool duplicate = false;
      bool writerOK = false;
      builder.emitBody([&body, &emitted, &duplicate, &writerOK](auto &&write) {
	if (emitted) { duplicate = true; return; }
	emitted = true;
	writerOK = invokeBodyWriter(ZuFwd<decltype(write)>(write), body);
      });
      body.flush();
      m_commit.produced = emitted ? body.produced() : 0;
      m_commit.committed = m_commit.produced;
      if ((!ReqOptional && !emitted) || duplicate ||
	  (emitted && (!writerOK || !body.valid())))
	return failFor_<Streaming>();
      if (emitted && !body.valid()) return failFor_<Streaming>();
    }
    builder.finish(tx);
    m_link->finish();
    m_commit.final = true;
    m_txState = TxState::Complete;
    return true;
  }

  bool sendFixed_(RequestBuilder app) {
    Builder_<true, false> builder{ZuMv(app)};
    auto native = m_link->transmit(builder);
    if constexpr (Message::ID == Version::H2)
      return sendFixedH2_(builder, native);
    else {
      if constexpr (Message::ID == Version::H3)
	builder.deferCompression();
      RetainedBudget budget{.max = m_app->retainedMessageMax()};
      RetainedTx headerTx{native, budget};
      RetainedTx bodyTx{native, budget};
      auto body = builder.body(bodyTx, fixedBodyMax_());
      bool emitted = false;
      bool duplicate = false;
      bool writerOK = false;
      bool headersOK = false;
      builder.emitBody([
        &builder, &headerTx, &body,
        &emitted, &duplicate, &writerOK, &headersOK]
        (auto &&write) {
	(void)headerTx;
	if (emitted) { duplicate = true; return; }
	emitted = true;
	if (!(headersOK = builder.provision())) return;
	if constexpr (Message::ID == Version::H1)
	  if (!(headersOK =
		builder.request(headerTx) && builder.headersValid())) return;
	writerOK = invokeBodyWriter(ZuFwd<decltype(write)>(write), body);
      });
      body.flush();
      if (emitted) {
	builder.produced = body.produced();
	m_commit.produced = builder.produced;
      }
      if ((!ReqOptional && !emitted) || duplicate ||
	  (emitted && (!headersOK || !writerOK || !body.valid())))
	return failTx_();
      if (!emitted)
	return send_<false, false>(builder.takeApp(), true);
      if (!body.valid()) return failTx_();
      if (!builder.patch(builder.produced)) return failTx_();
      if constexpr (Message::ID != Version::H1)
	if (!builder.request(headerTx) || !builder.headersValid())
	  return failTx_();
      builder.finish(bodyTx);
      if (!headerTx.seal() || !bodyTx.seal()) return failTx_();
      headerTx.commit();
      bodyTx.commit();
      m_link->finish();
      m_commit.headers = true;
      m_commit.committed = m_commit.produced;
      m_commit.final = true;
      m_txState = TxState::Complete;
      return true;
    }
  }

  template <typename Builder, typename Tx>
  bool sendFixedH2_(Builder &builder, Tx &tx) {
    tx.defer(m_app->retainedMessageMax());
    auto body = builder.body(tx, fixedBodyMax_());
    bool emitted = false;
    bool duplicate = false;
    bool writerOK = false;
    bool headersOK = false;
    builder.emitBody([
      &builder, &body, &emitted, &duplicate, &writerOK, &headersOK]
      (auto &&write) {
      if (emitted) { duplicate = true; return; }
      emitted = true;
      if (!(headersOK = builder.provision())) return;
      writerOK = invokeBodyWriter(ZuFwd<decltype(write)>(write), body);
    });
    body.flush();
    if (emitted) {
      builder.produced = body.produced();
      m_commit.produced = builder.produced;
    }
    if ((!ReqOptional && !emitted) || duplicate ||
	(emitted && (!headersOK || !writerOK || !body.valid())))
      return failTx_();
    if (!emitted)
	return send_<false, false>(builder.takeApp(), true);
    if (!body.valid()) return failTx_();
    if (!builder.patch(builder.produced)) return failTx_();
    if (!builder.request(tx) || !builder.headersValid()) return failTx_();
    m_commit.headers = true;
    builder.finish(tx);
    if (!tx.valid()) return failTx_();
    tx.commit();
    m_link->finish();
    m_commit.committed = m_commit.produced;
    m_commit.final = true;
    m_txState = TxState::Complete;
    return true;
  }

public:
  template <typename Rx>
  int process(Rx &rx) {
    auto state = m_link->receive(m_parser, rx);
    if (state == ParserState::Error) return -1;
    if (state == ParserState::Complete) return 1;
    if (m_app->done(*m_request)) return -1;
    if constexpr (Message::ID == Version::H1)
      return m_parser.progressed();
    return 0;
  }

  void eof() {
    if constexpr (Message::CloseDelimited) m_parser.eof();
  }

private:
  uint64_t fixedBodyMax_() const {
    uint64_t n = m_app->retainedBodyMax();
    if (n > m_app->retainedMessageMax())
      n = m_app->retainedMessageMax();
    if (n > uint32_t(-1)) n = uint32_t(-1);
    return n;
  }

  struct TxState {
    enum { Idle, Active, Complete, Failed, Cancelled };
  };

  bool failTx_() {
    m_commit.discarded = m_commit.produced;
    m_txState = TxState::Failed;
    return false;
  }

  bool failStreamedTx_() {
    m_commit.discarded = m_commit.produced > m_commit.committed ?
      m_commit.produced - m_commit.committed : 0;
    m_commit.reset = m_commit.committed;
    m_link->disconnect();
    m_txState = TxState::Failed;
    return false;
  }

  template <bool Streaming>
  bool failFor_() {
    if constexpr (Streaming)
      return failStreamedTx_();
    else
      return failTx_();
  }

  App		*m_app = nullptr;
  Link		*m_link = nullptr;
  Request	*m_request = nullptr;
  Parser	m_parser;
  ZuUnion<void, ResponseParser> m_response;
  BodyCommit	m_commit;
  int8_t	m_txState = TxState::Idle;
};

template <typename Impl, typename Parser_, typename Message_>
struct ServerSession {
  using Parser = Parser_;
  using State = typename Parser::State;
  using Message = Message_;

  Parser	parser;
  bool		complete = false;

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  void connected(auto &) { }
  void disconnected(auto &, bool) { }

  template <typename Link, typename Rx>
  int process(Link &link, Rx &rx) {
    if (complete) return 1;
    auto state = link.receive(parser, rx);
    if (state == State::Error) return impl()->error(link, parser);
    if (state != State::Complete) {
      if constexpr (Message::ID == Version::H1)
	return parser.progressed();
      return 0;
    }
    int rc = impl()->request(link, parser);
    if constexpr (Message::OneMessagePerLink)
      complete = true;
    else
      parser.reset();
    return rc;
  }

  template <typename Link>
  int error(Link &, Parser &) { return -1; }
  template <typename Link>
  int request(Link &, Parser &) { return 1; }
};

} // namespace Zhttp

#endif /* ZhttpMessage_HH */
