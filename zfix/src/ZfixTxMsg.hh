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
    if (Fmt::tag<typename L::Field>() == Entry::Discrim::Tag) found = true;
  });
  return found;
}

// Encode may add an unmodelled root key; history restore preserves exact bytes.
template <typename Entry>
Fmt::Error bodySelector(ZuCSpan bytes, Fmt::Limits limits, Fmt::Sink *out = nullptr) {
  using Key = typename Entry::Discrim;
  if constexpr (!Key::Tag || Fmt::reserved(Key::Tag)) return {};
  else {
    Fmt::FieldPos selector;
    auto error = Fmt::inspect(bytes, Key::Tag, selector, limits);
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
Fmt::Error frameSelector(const Fmt::FrameView &frame, Fmt::Limits limits) {
  using Key = typename Entry::Discrim;
  Fmt::FieldPos selector;
  auto error = Fmt::inspect(frame.content, Key::Tag, selector, limits);
  if (!error) error = match<Key>(frame.content, selector);
  return Dispatch_::wireError(error, frame);
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
    auto buf = start(Entry::MsgType, id, admin, error, limits);
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
    using O = Entry::Object;
    using Facet = Entry::Mapping;
    if constexpr (Fmt::Format<O, Facet>::Kind != Fmt::Kind::Object) {
      error = {Fmt::ErrorCode::Unresolved, 35}; return {};
    } else {
      using Key = typename Entry::Discrim;
      enum { Select = Key::Tag && !Fmt::reserved(Key::Tag) ? Key::Tag : 0 };
      {
	Fmt::Handler<O, Facet> input{bytes, limits};
	if (!input.check()) { error = input.error(); return {}; }
      }
      if constexpr (Select) {
	error = TxFrame_::bodySelector<Entry>(bytes, limits);
	if (error) return {};
      }
      auto buf = archive_(Entry::MsgType, bytes, id, admin, error, limits);
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
    using Key = typename Entry::Discrim;
    if constexpr (Key::Tag && Fmt::reserved(Key::Tag))
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
