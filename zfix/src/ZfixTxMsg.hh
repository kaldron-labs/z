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

#include <zlib/ZfixSession.hh>
#include <zlib/ZfixDispatch.hh>

namespace Zfix {

namespace TxFrame_ {

template <typename Scan>
Fmt::Error inspect(ZuCSpan bytes, Fmt::Position &selector, Fmt::Limits limits) {
  using S = Fmt::Schema<Scan, ZuFacet::FIX>;
  if (bytes.length() > limits.frameBytes) return {Fmt::ErrorCode::Size};
  Fmt::Error error;
  ZtBuiltin<Fmt::Positions, S::Leaves::N> positions;
  Fmt::Parse input{bytes, limits, positions, error};
  input.slots(S::Leaves::N);
  input.selectTag = S::SelectTag; input.selected = &selector;
  unsigned offset = 0;
  Fmt::Scope scope{nullptr, Fmt::scopeContains<Scan, ZuFacet::FIX>, 0, 0};
  if (!error) Fmt::parseScope<Scan, ZuFacet::FIX>(input, 0, offset, scope, false);
  return error;
}
template <typename Key> Fmt::Error match(ZuCSpan bytes, Fmt::Position selector) {
  if (selector.offset == UINT_MAX) return {Fmt::ErrorCode::Required, Key::tag};
  if (ZuCSpan{bytes.data() + selector.offset, selector.length} != ZuCSpan{Key::value})
    return {Fmt::ErrorCode::Unresolved, Key::tag, selector.offset};
  return {};
}
template <typename Entry> struct Body {
  using Key = typename Entry::Discriminator;
  using Sources = ZuTypeList<Dispatch_::EntrySource<Entry>>;

  static consteval bool contains() {
    bool found = false;
    ZuUnroll::all<Dispatch_::RawLeaves<Sources>>([&found]<typename L>() {
      using F = typename L::Field;
      if (Fmt::tag<F>() == Key::tag || Fmt::lenTag<F>() == Key::tag) found = true;
    });
    return found;
  }

  using Scan = Dispatch_::Scope<ZuTypeConcat<Sources,
    ZuIf<contains(), ZuTypeList<>,
      ZuTypeList<Dispatch_::Source<Dispatch_::Key<Key::tag>, ZuFacet::FIX>>>>,
    false, Key::tag>;
};

// Encode may add an unmodelled root key; history restore preserves exact bytes.
template <typename Entry>
Fmt::Error bodySelector(ZuCSpan bytes, Fmt::Limits limits, Fmt::Sink *out = nullptr) {
  using Key = typename Entry::Discriminator;
  if constexpr (!Key::tag || Fmt::reserved(Key::tag)) return {};
  else {
    Fmt::Position selector{UINT_MAX, 0, 0, 0};
    auto error = inspect<typename Body<Entry>::Scan>(bytes, selector, limits);
    if (error) return error;
    if constexpr (!Body<Entry>::contains()) {
      if (out && selector.offset == UINT_MAX) {
	out->field(Key::tag); *out << Key::value; out->end(); return out->error();
      }
    }
    return match<Key>(bytes, selector);
  }
}
template <typename Entry>
Fmt::Error frameSelector(const Fmt::FrameView &frame, Fmt::Limits limits) {
  using Key = typename Entry::Discriminator;
  using Scan = typename Dispatch_::Wire<ZuTypeList<Entry>, Key::tag>::T;
  Fmt::Position selector{UINT_MAX, 0, 0, 0};
  auto error = inspect<Scan>(frame.content, selector, limits);
  if (!error) error = match<Key>(frame.content, selector);
  if (error.offset || error.code == Fmt::ErrorCode::Unresolved)
    error.offset += unsigned(frame.content.data() - frame.wire.data());
  return error;
}
} // TxFrame_

// Body encoding does not assign a sequence or sample a clock. The application
// identity is opaque to FIX and survives control traffic and replay. Controls
// may carry an opaque publication marker in id(); neither is a wire sequence. Const
// references may cross shards; only the Tx owner builds mutable output frames.
class ZfixAPI TxBody : public ZiIOBuf {
public:
  using Ref = ZmRef<const TxBody>;

  TxBody(uint8_t *data, unsigned size) : ZiIOBuf{data, size} { }

  // Rounded pool object plus jumbo heap prefix/cache class, for factory bodies.
  uint64_t memory() const;

  uint64_t id() const { return m_id; }
  ZuCSpan msgType() const { return {data(), m_typeLen}; }

  ZuCSpan body() const {
    return {data() + m_typeLen, length - m_typeLen};
  }

  bool administrative() const { return m_admin; }
  bool validates() const { return m_validate; }

  Fmt::Error validate(const Fmt::FrameView &frame, Fmt::Limits limits) const {
    return m_validate ? m_validate(frame, limits) : Fmt::Error{};
  }

  template <typename Entry>
  static Ref encode(const typename Entry::Object &o, uint64_t id,
      Fmt::Error &error, Fmt::Limits limits = {}) {
    if (Session::type(Entry::msgType)) {
      error = {Fmt::ErrorCode::Reserved, 35}; return {};
    }
    return encodeRaw<Entry>(o, id, false, error, limits);
  }

  // External managers choose their own control schemas and administrative
  // classification; raw conversion reserves no built-in session MsgTypes.
  template <typename Entry>
  static Ref encodeRaw(const typename Entry::Object &o, uint64_t id, bool admin,
      Fmt::Error &error, Fmt::Limits limits = {}) {
    auto buf = start(Entry::msgType, id, admin, error, limits);
    if (!buf) return {};
    auto cfg = limits;
    cfg.frameBytes -= buf->m_typeLen;
    Fmt::Sink out{buf->storage(), cfg, grow, buf.ptr()};
    Fmt::save<typename Entry::Mapping>(out, o);
    error = out.error();
    if (!error) error = TxFrame_::bodySelector<Entry>(out.data(), cfg, &out);
    if (error) return {};
    buf->template selector_<Entry>();
    buf->length = buf->m_typeLen + out.length();
    return buf;
  }

  // Archive reload validates the application-selected schema and retains exact
  // bytes. Store the entry/facet identity with history; no union trial decoding.
  template <typename Entry>
  static Ref restore(ZuCSpan bytes, uint64_t id, Fmt::Error &error, Fmt::Limits limits = {}) {
    if (Session::type(Entry::msgType)) { error = {Fmt::ErrorCode::Reserved, 35}; return {}; }
    return restore_<Entry>(bytes, id, false, error, limits);
  }

  template <typename Entry>
  static Ref restoreControl(ZuCSpan bytes, uint64_t id,
      Fmt::Error &error, Fmt::Limits limits = {}) {
    if (!Session::type(Entry::msgType)) { error = {Fmt::ErrorCode::Unresolved, 35}; return {}; }
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
    using O = Entry::Object;
    using Facet = Entry::Mapping;
    if constexpr (Fmt::Format<O, Facet>::Kind != Fmt::Kind::Object) {
      error = {Fmt::ErrorCode::Unresolved, 35}; return {};
    } else {
      using Key = typename Entry::Discriminator;
      enum { Select = Key::tag && !Fmt::reserved(Key::tag) ? Key::tag : 0 };
      Fmt::Position selector;
      {
	Fmt::Handler<O, Facet> input{bytes, limits, ZuUnsigned<Select>{}, selector};
	if (!input.check()) { error = input.error(); return {}; }
      }
      if constexpr (Select) {
	error = TxFrame_::match<Key>(bytes, selector);
	if (error) return {};
      }
      auto buf = archive_(Entry::msgType, bytes, id, admin, error, limits);
      if (buf) buf->template selector_<Entry>();
      return buf;
    }
  }

  static ZmRef<TxBody> archive_(ZuCSpan type, ZuCSpan bytes, uint64_t id, bool admin,
    Fmt::Error &, Fmt::Limits);
  static ZmRef<TxBody> start(ZuCSpan, uint64_t, bool, Fmt::Error &, Fmt::Limits);
  static ZuSpan<char> grow(void *, unsigned used, unsigned needed);

  ZuSpan<char> storage() {
    return {data() + m_typeLen, size - m_typeLen};
  }

  template <typename Entry> void selector_() {
    using Key = typename Entry::Discriminator;
    if constexpr (Key::tag && Fmt::reserved(Key::tag))
      m_validate = TxFrame_::frameSelector<Entry>;
  }

  Fmt::Error	(*m_validate)(const Fmt::FrameView &, Fmt::Limits) = nullptr;
  uint64_t	m_id = 0;
  unsigned	m_typeLen = 0;
  bool		m_admin = false;
};

// Each attempt owns its complete output until finish succeeds. A transport may
// then mutate/encrypt/advance it without changing the retained replay body.
class ZfixAPI TxFrame {
public:
  explicit TxFrame(ZuCSpan beginString, Fmt::Limits limits = {});
  TxFrame(const TxFrame &) = delete;
  TxFrame &operator =(const TxFrame &) = delete;

  Fmt::Sink &body() { return m_writer.body(); }

  ZmRef<ZiIOBuf> finish(Fmt::Error &);

private:
  static ZuSpan<char> grow(void *, unsigned used, unsigned needed);

  ZmRef<ZiIOBuf>	m_buffer;
  Fmt::FrameWriter	m_writer;
};

template <typename HeaderFacet = ZuFacet::FIX>
ZmRef<ZiIOBuf> txFrame(const TxBody &body, Fmt::Header header,
    Fmt::Error &error, Fmt::Limits limits = {}, const Fmt::Trailer &trailer = {}) {
  header.msgType = body.msgType();
  TxFrame output{header.beginString, limits};
  error = Fmt::writeHeader<HeaderFacet>(output.body(), header);
  if (error) return {};
  // One explicit body copy separates immutable replay storage from output
  // owned and potentially encrypted in place by the TCP/TLS transport.
  output.body() << body.body();
  error = Fmt::writeTrailer(output.body(), trailer);
  if (error) return {};
  auto buffer = output.finish(error);
  if (!buffer) return {};
  if (!body.validates()) return buffer;
  auto parsed = Fmt::frame(buffer->cspan(), limits);
  if (parsed.state != Fmt::FrameState::Complete) { error = parsed.error; return {}; }
  error = body.validate(parsed.view, limits);
  return error ? ZmRef<ZiIOBuf>{} : ZuMv(buffer);
}
} // Zfix

#endif /* ZfixTxMsg_HH */
