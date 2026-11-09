//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Application-owned encoded bodies and complete transport output construction

#ifndef ZfixTxMsg_HH
#define ZfixTxMsg_HH

#ifndef ZfixLib_HH
#include <zlib/ZfixLib.hh>
#endif

#include <zlib/ZmRef.hh>
#include <zlib/ZiIOBuf.hh>

#include <zlib/ZfixNotice.hh>
#include <zlib/ZfixSession.hh>
#include <zlib/ZfixLinkState.hh>
#include <zlib/ZfixDispatch.hh>

namespace Zfix {

class TxBody;

struct NoSign {
  enum { SignMsg = 0 };
  template <typename S>
  void signMsg(S &, const TxBody *, TxContext, ZuCSpan) const { }
};

inline ZuCSpan beginString(const TxConfig &config) {
  return config.beginString.data() ? ZuCSpan{config.beginString} :
    Version::Wire::v2s(config.identity.version);
}

template <int32_t Tag> auto sessionValue(const TxConfig &config, const TxContext &context) {
  if constexpr (Tag == 49) return ZuCSpan{config.identity.local};
  else if constexpr (Tag == 56) return ZuCSpan{config.identity.peer};
  else if constexpr (Tag == 50) return config.identity.localSub.length() ?
    ZuCSpan{config.identity.localSub} : ZuCSpan{};
  else if constexpr (Tag == 57) return config.identity.peerSub.length() ?
    ZuCSpan{config.identity.peerSub} : ZuCSpan{};
  else if constexpr (Tag == 142) return config.identity.localLocation.length() ?
    ZuCSpan{config.identity.localLocation} : ZuCSpan{};
  else if constexpr (Tag == 143) return config.identity.peerLocation.length() ?
    ZuCSpan{config.identity.peerLocation} : ZuCSpan{};
  else if constexpr (Tag == 1128) return config.appVersion.data() ? ZuCSpan{config.appVersion} :
    (config.mode == Transport::FIXT11 ? AppVersion::Wire::v2s(config.appVerID) : ZuCSpan{});
  else if constexpr (Tag == 34) return context.seqNo;
  else if constexpr (Tag == 347) return config.encoding.length() ? ZuCSpan{config.encoding} : ZuCSpan{};
  else if constexpr (Tag == 43) return context.possDup || context.replay;
  else if constexpr (Tag == 52) return context.sendingTime;
  else if constexpr (Tag == 122) return context.origSendingTime;
}

template <typename F> bool sessionEnabled(const TxConfig &config, const TxContext &context) {
  if constexpr (ZuFieldProp::FIX::GetOptional<typename F::Props>{}) {
    auto value = sessionValue<Fmt::tag<F>()>(config, context);
    if constexpr (Fmt::tag<F>() == 43) return bool(value);
    else if constexpr (Fmt::tag<F>() == 52 || Fmt::tag<F>() == 122) return *value;
    else return !ZuNull(value);
  } else return true;
}

template <typename F, typename Facet, typename Out>
void saveSessionField(Out &out, const TxConfig &config, const TxContext &context) {
  if (!sessionEnabled<F>(config, context)) return;
  const auto &value = sessionValue<Fmt::tag<F>()>(config, context);
  if constexpr (Fmt::Binary<F>{}) Fmt::saveBinary<F>(out, value);
  else {
    out.field(Fmt::tag<F>());
    Fmt::saveScalar<Facet, F::Type::Code, typename F::Props>(out, value);
    out.end();
  }
}

template <typename Facet> class ConfiguredSink {
public:
  ConfiguredSink(Fmt::Sink &sink, const TxConfig &config, const TxContext &context) :
    m_sink{sink}, m_config{config}, m_context{context} { }
  bool failed() const { return m_sink.failed(); }
  const Fmt::Error &error() const { return m_sink.error(); }
  unsigned length() const { return m_sink.length(); }
  void fail(unsigned code = Fmt::ErrorCode::Output) { m_sink.fail(code); }
  bool flush() const { return m_sink.flush(); }
  void field(int32_t tag) { m_sink.field(tag); }
  void end() { m_sink.end(); }
  bool group(uint64_t count) { return m_sink.group(count); }
  void endGroup() { m_sink.endGroup(); }
  void entry(unsigned offset, int32_t &delimiter) { m_sink.entry(offset, delimiter); }
  template <typename T> ConfiguredSink &operator <<(const T &value) {
    m_sink << value; return *this;
  }
  template <typename F, typename O> decltype(auto) value(const O &o) {
    if constexpr (!Fmt::Component<F, Facet>{} && !Fmt::GroupField<F, Facet>{}) {
      if constexpr (sessionTag(Fmt::tag<F>())) return sessionValue<Fmt::tag<F>()>(m_config, m_context);
      else return F::get(o);
    } else return F::get(o);
  }
  template <typename F, typename O> bool enabled(const O &) {
    if constexpr (!Fmt::Component<F, Facet>{} && !Fmt::GroupField<F, Facet>{}) {
      if constexpr (sessionTag(Fmt::tag<F>())) return sessionEnabled<F>(m_config, m_context);
      else return true;
    } else return true;
  }
private:
  Fmt::Sink &m_sink;
  const TxConfig &m_config;
  const TxContext &m_context;
};

namespace TxFrame_ {

// Ordinary Rx construction validates both constructor arguments and fields
// assigned afterward; final frame inspection needs the same presence policy.
template <typename F> using FrameField = ZuBool<
  ZfFieldFilter::Init<F>{} || ZfFieldFilter::Ctor<F>{}>;

template <typename Key> Fmt::Error match(ZuCSpan bytes, Fmt::FieldPos selector) {
  if (selector.offset == UINT_MAX) return {Fmt::ErrorCode::Required, Key::Tag};
  if (ZuCSpan{bytes.data() + selector.offset, selector.length} != ZuCSpan{Key::Value})
    return {Fmt::ErrorCode::Unresolved, Key::Tag, selector.offset};
  return {};
}
template <typename Entry> consteval bool bodyContains() {
  bool found = false;
  using S = Fmt::Schema<typename Entry::Object, typename Entry::Mapping>;
  ZuUnroll::all<typename S::Leaves>([&found]<typename L>() {
    if (Fmt::tag<typename L::Field>() == Entry::Discrim::Tag ||
        Fmt::payloadTag<typename L::Field>() == Entry::Discrim::Tag) found = true;
  });
  return found;
}

// Encode may add an unmodelled root key; history restore preserves exact bytes.
template <typename Entry>
Fmt::Error bodySelector(ZuCSpan bytes, Fmt::Limits limits, Fmt::Sink *out = nullptr) {
  using Key = typename Entry::Discrim;
  if constexpr (!Key::Tag || Fmt::reserved(Key::Tag) || sessionTag(Key::Tag)) return {};
  else {
    Fmt::FieldPos selector;
    auto error = Fmt::inspect<typename Entry::Object, typename Entry::Mapping>(
      bytes, Key::Tag, selector, limits);
    if (error) return error;
    if constexpr (!bodyContains<Entry>()) {
      if (out && selector.offset == UINT_MAX) {
	out->field(Key::Tag); *out << Key::Value; out->end(); return out->error();
      }
    }
    return match<Key>(bytes, selector);
  }
}
template <typename Entry>
Fmt::Error signedFrame(const Fmt::FrameView &frame, Fmt::Limits limits) {
  using O = Entry::Object;
  using Facet = Entry::Mapping;
  Fmt::Error error;
  enum { Tag = Entry::Discrim::Tag };
  Fmt::Read input{frame.content, limits, error};
  input.selectTag = Tag;
  unsigned offset = 0;
  Fmt::Scope scope{nullptr, Fmt::scopeContains<O, Facet, Tag>, 0, 0};
  Fmt::readScope<O, Facet, FrameField, false, Tag, false>(
    input, nullptr, offset, scope, false);
  if (!error) {
    if constexpr (Tag) error = match<typename Entry::Discrim>(frame.content, input.selector);
  }
  if (error) return Dispatch_::wireError(error, frame);
  return {};
}
} // TxFrame_

// Body encoding does not assign a sequence or sample a clock. The application
// identity is opaque to FIX and survives control traffic and replay. Controls
// may carry an opaque publication marker in id(); neither is a wire sequence. Const
// references may cross shards; only the Tx owner builds mutable output frames.
class ZfixAPI TxBody : public ZiIOBuf {
public:
  using Ref = ZmRef<const TxBody>;

  TxBody(uint8_t *data, unsigned size, Zi::IOBufAllocator_ *allocator,
      void *owner = nullptr, unsigned length = 0) :
    ZiIOBuf{data, size, allocator, owner, length} { }

  // Rounded pool object plus jumbo heap prefix/cache class, for factory bodies.
  uint64_t memory() const;

  uint64_t id() const { return m_id; }
  ZuCSpan msgType() const { return {data() + m_slots * sizeof(Fmt::FieldPos), m_typeLen}; }

  ZuCSpan body() const {
    return {data() + m_bodyOffset, length - m_bodyOffset};
  }
  Fmt::FieldPos *ranges() { return reinterpret_cast<Fmt::FieldPos *>(data()); }
  const Fmt::FieldPos *ranges() const { return reinterpret_cast<const Fmt::FieldPos *>(data()); }
  void emit(Fmt::Sink &out, const TxConfig &config, TxContext context) const {
    if (!m_emit) { out.fail(Fmt::ErrorCode::Unresolved); return; }
    m_emit(out, *this, config, context);
  }

  bool administrative() const { return m_admin; }
  bool validates() const { return m_validate; }

  Fmt::Error validate(const Fmt::FrameView &frame, Fmt::Limits limits) const {
    return m_validate ? m_validate(frame, limits) : Fmt::Error{};
  }

  template <typename Entry>
  static Ref encode(const typename Entry::Object &o, uint64_t id,
      Fmt::Error &error, Fmt::Limits limits = {}) {
    if (Session::type(Entry::MsgType)) {
      error = {Fmt::ErrorCode::Reserved, 35}; return {};
    }
    return encodeRaw<Entry>(o, id, false, error, limits);
  }

  // External managers choose their own control schemas and administrative
  // classification; raw conversion reserves no built-in session MsgTypes.
  template <typename Entry>
  static Ref encodeRaw(const typename Entry::Object &o, uint64_t id, bool admin,
      Fmt::Error &error, Fmt::Limits limits = {}) {
    using S = Fmt::Schema<typename Entry::Object, typename Entry::Mapping>;
    auto buf = start(Entry::MsgType, S::Leaves::N, id, admin, error, limits);
    if (!buf) return {};
    auto cfg = limits;
    cfg.frameBytes -= buf->m_typeLen;
    EncodeSink<Entry> out{buf->storage(), cfg, grow, buf.ptr()};
    Fmt::save<typename Entry::Mapping, ArchiveField>(out, o);
    error = out.error();
    if (!error) error = TxFrame_::bodySelector<Entry>(out.data(), cfg, &out);
    if (error) return {};
    buf->template entry_<Entry>();
    buf->length = buf->m_bodyOffset + out.length();
    return buf;
  }

  // Archive reload validates the application-selected schema and retains exact
  // bytes. Store the entry/facet identity with history; no union trial decoding.
  template <typename Entry>
  static Ref restore(ZuCSpan bytes, uint64_t id, Fmt::Error &error, Fmt::Limits limits = {}) {
    if (Session::type(Entry::MsgType)) { error = {Fmt::ErrorCode::Reserved, 35}; return {}; }
    return restore_<Entry>(bytes, id, false, error, limits);
  }

  template <typename Entry>
  static Ref restoreControl(ZuCSpan bytes, uint64_t id,
      Fmt::Error &error, Fmt::Limits limits = {}) {
    if (!Session::type(Entry::MsgType)) { error = {Fmt::ErrorCode::Unresolved, 35}; return {}; }
    return restore_<Entry>(bytes, id, true, error, limits);
  }

  template <typename Entry>
  static Ref restoreRaw(ZuCSpan bytes, uint64_t id, bool admin,
      Fmt::Error &error, Fmt::Limits limits = {}) {
    return restore_<Entry>(bytes, id, admin, error, limits);
  }

  static Ref restoreControl(ZuCSpan msgType, ZuCSpan bytes, uint64_t id,
    Fmt::Error &, Fmt::Limits limits = {});
  static Ref control(const Session::Msg &, Fmt::Error &, unsigned mode = Transport::FIX4,
    Fmt::Limits limits = {}, uint64_t id = 0);

private:
  template <typename Entry>
  static Ref restore_(ZuCSpan bytes, uint64_t id, bool admin,
      Fmt::Error &error, Fmt::Limits limits) {
    error = {};
    using O = Entry::Object;
    using Facet = Entry::Mapping;
    if constexpr (Fmt::Format<O, Facet>::Kind != Fmt::Kind::Object) {
      error = {Fmt::ErrorCode::Unresolved, 35}; return {};
    } else {
      using Key = typename Entry::Discrim;
      enum { Select = Key::Tag && !Fmt::reserved(Key::Tag) && !sessionTag(Key::Tag) ? Key::Tag : 0 };
      if constexpr (Select) {
	Fmt::Read input{bytes, limits, error};
	input.selectTag = Select;
	unsigned offset = 0;
	Fmt::Scope scope{nullptr, Fmt::scopeContains<O, Facet, Select>, 0, 0};
	Fmt::readScope<O, Facet, ArchiveField, false, Select, false>(
	  input, nullptr, offset, scope, false);
	if (!error) error = TxFrame_::match<Key>(bytes, input.selector);
	if (error) return {};
      }
      auto buf = archive_<Entry>(bytes, id, admin, error, limits);
      return buf;
    }
  }

  template <typename Entry> class EncodeSink : public Fmt::Sink {
  public:
    EncodeSink(ZuSpan<char> storage, Fmt::Limits limits, Grow grow, TxBody *body) :
      Sink{storage, limits, grow, body}, m_body{body} { }
    template <typename L> void rangeField(Fmt::FieldPos pos) {
      using Leaves = typename Fmt::Schema<typename Entry::Object, typename Entry::Mapping>::Leaves;
      enum { I = ZuTypeIndex<L, Leaves>{} };
      m_body->ranges()[I] = pos;
    }
  private:
    TxBody *m_body;
  };

  template <typename Entry>
  static ZmRef<TxBody> archive_(ZuCSpan bytes, uint64_t id, bool admin,
      Fmt::Error &error, Fmt::Limits limits) {
    using O = Entry::Object;
    using Facet = Entry::Mapping;
    using S = Fmt::Schema<O, Facet>;
    if (S::Leaves::N > limits.parseBytes/sizeof(Fmt::Position)) {
      error = {Fmt::ErrorCode::Size}; return {};
    }
    auto buf = start(Entry::MsgType, S::Leaves::N, id, admin, error, limits);
    if (!buf) return {};
    if (bytes.length() > limits.frameBytes - buf->m_typeLen ||
	bytes.length() > TxBodyAllocMax() - buf->m_bodyOffset) {
      error = {Fmt::ErrorCode::Size}; return {};
    }
    if (bytes.length() > buf->size - buf->m_bodyOffset &&
	!buf->ensure(uint64_t(buf->m_bodyOffset) + bytes.length())) {
      error = {Fmt::ErrorCode::Size}; return {};
    }
    Fmt::Positions positions;
    positions.length(S::Leaves::N, false);
    for (unsigned i = 0; i < S::Leaves::N; ++i) {
      positions[i].offset = UINT_MAX;
      buf->ranges()[i] = {UINT_MAX, 0};
    }
    Fmt::Parse parsed{bytes, limits, positions, error};
    parsed.ranges = buf->ranges();
    unsigned offset = 0;
    Fmt::Scope scope{nullptr, Fmt::scopeContains<O, Facet, Entry::Discrim::Tag>, 0, 0};
    if (!Fmt::parseScope<O, Facet>(parsed, 0, offset, scope, false)) return {};
    Fmt::ObjectView<O, Facet> input{bytes, positions, 0, error};
    input.template check<ArchiveField>();
    if (error) return {};
    ZuUnroll::all<typename S::Leaves>([&positions, &buf, &error]<typename L>() {
      using F = L::Field;
      enum { I = ZuTypeIndex<L, typename S::Leaves>{} };
      if constexpr (Fmt::Signature<F>{} || sessionTag(Fmt::tag<F>()))
	if (positions[I].offset != UINT_MAX)
	  error = {Fmt::ErrorCode::Reserved, Fmt::tag<F>(), buf->ranges()[I].offset};
    });
    if (error) return {};
    memcpy(buf->data() + buf->m_bodyOffset, bytes.data(), bytes.length());
    buf->length = buf->m_bodyOffset + bytes.length();
    buf->template entry_<Entry>();
    return buf;
  }

  static unsigned TxBodyAllocMax();
  static ZmRef<TxBody> start(ZuCSpan, unsigned slots, uint64_t, bool, Fmt::Error &, Fmt::Limits);
  static ZuSpan<char> grow(void *, unsigned used, unsigned needed);
  ZuSpan<char> storage() { return {data() + m_bodyOffset, size - m_bodyOffset}; }

  template <typename Entry> void entry_() {
    using O = Entry::Object;
    using Facet = Entry::Mapping;
    using S = Fmt::Schema<O, Facet>;
    using Key = Entry::Discrim;
    m_emit = [](Fmt::Sink &out, const TxBody &body, const TxConfig &config, TxContext context) {
      out.field(35); out << Entry::MsgType; out.end();
      auto bytes = body.body();
      unsigned copied = 0, insertion = 0;
      auto flush = [&out, bytes, &copied, &insertion] {
	if (insertion > copied) out << ZuCSpan{bytes.data() + copied, insertion - copied};
	copied = insertion;
      };
      ZuUnroll::all<typename S::Leaves>(
	[&out, &body, &config, context, &insertion, &flush]<typename L>() {
	using F = L::Field;
	if constexpr (Fmt::SaveField<F>{}) {
	  if constexpr (sessionTag(Fmt::tag<F>())) {
	    if (!sessionEnabled<F>(config, context)) return;
	    flush(); saveSessionField<F, Facet>(out, config, context);
	  } else {
	    auto pos = body.ranges()[ZuTypeIndex<L, typename S::Leaves>{}];
	    if (pos.offset != UINT_MAX && pos.offset + pos.length > insertion)
	      insertion = pos.offset + pos.length;
	  }
	}
      });
      insertion = bytes.length(); flush();
    };
    if constexpr (Fmt::hasSignature<O, Facet>() || Key::Tag)
      m_validate = TxFrame_::signedFrame<Entry>;
  }

  void (*m_emit)(Fmt::Sink &, const TxBody &, const TxConfig &, TxContext) = nullptr;
  unsigned m_slots = 0, m_bodyOffset = 0;
  Fmt::Error	(*m_validate)(const Fmt::FrameView &, Fmt::Limits) = nullptr;
  uint64_t	m_id = 0;
  unsigned	m_typeLen = 0;
  bool		m_admin = false;
};

// Each attempt uses a caller-supplied buffer until finish succeeds. A transport may
// then mutate/encrypt/advance it without changing the retained replay body.
class ZfixAPI TxFrame {
public:
  TxFrame(ZmRef<ZiIOBuf> buf, ZuCSpan beginString, Fmt::Limits limits = {});
  TxFrame(const TxFrame &) = delete;
  TxFrame &operator =(const TxFrame &) = delete;

  Fmt::Sink &body() { return m_writer.body(); }

  ZmRef<ZiIOBuf> finish(Fmt::Error &);

  template <typename Signer>
  ZmRef<ZiIOBuf> finish(Fmt::Error &error, const Signer &signer,
      const TxBody *body, TxContext context) {
    return output_(m_writer.finish(signer, body, context), error);
  }

private:
  ZmRef<ZiIOBuf> output_(Fmt::FrameOutput, Fmt::Error &);
  static ZuSpan<char> grow(void *, unsigned used, unsigned needed);

  ZmRef<ZiIOBuf>	m_buf;
  Fmt::FrameWriter	m_writer;
};

template <typename Signer = NoSign>
ZmRef<ZiIOBuf> txFrame(
    ZmRef<ZiIOBuf> buf, const TxBody &body, const TxConfig &config, TxContext context,
    Fmt::Error &error, Fmt::Limits limits = {}, const Signer &signer = {}) {
  if (!buf || buf->failed()) { error = {Fmt::ErrorCode::Output}; return {}; }
  TxFrame output{ZuMv(buf), beginString(config), limits};
  body.emit(output.body(), config, context);
  auto buffer = output.finish(error, signer, &body, context);
  if (!buffer) return {};
  if (!body.validates()) return buffer;
  auto parsed = Fmt::frame(buffer->cspan(), limits);
  if (parsed.state != Fmt::FrameState::Complete) { error = parsed.error; return {}; }
  error = body.validate(parsed.view, limits);
  return error ? ZmRef<ZiIOBuf>{} : ZuMv(buffer);
}

template <typename Entry, typename Signer = NoSign>
Fmt::FrameOutput writeFrame(ZuSpan<char> storage, const TxConfig &config, TxContext context,
    const typename Entry::Object &object, Fmt::Limits limits = {},
    const Signer &signer = {}, const TxBody *body = nullptr) {
  Fmt::FrameWriter writer{storage, beginString(config), limits};
  writer.body().field(35); writer.body() << Entry::MsgType; writer.body().end();
  ConfiguredSink<typename Entry::Mapping> configured{writer.body(), config, context};
  Fmt::save<typename Entry::Mapping>(configured, object);
  return writer.finish(signer, body, context);
}
namespace Session {
template <typename Signer = NoSign>
Fmt::FrameOutput write(ZuSpan<char> storage, const TxConfig &config, TxContext context,
    const Msg &message, unsigned mode = Transport::FIX4, Fmt::Limits limits = {},
    const Signer &signer = {}) {
  Fmt::FrameOutput result;
  auto error = visit(message, mode,
    [storage, &config, context, limits, &signer, &result]<typename O, typename Facet>(const O &o) {
      result = Zfix::writeFrame<MT<O, O::MsgType, Facet>>(
	storage, config, context, o, limits, signer);
      return result.error;
    });
  if (error) result.error = error;
  return result;
}
} // Session
} // Zfix

#endif /* ZfixTxMsg_HH */
