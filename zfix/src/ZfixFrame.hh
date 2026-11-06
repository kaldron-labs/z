//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Standalone FIX envelope values and framing boundary

#ifndef ZfixFrame_HH
#define ZfixFrame_HH

#ifndef ZfixFmtLib_HH
#include <zlib/ZfixFmtLib.hh>
#endif

#include <zlib/ZuSpan.hh>
#include <zlib/ZuDateTime.hh>
#include <zlib/ZtBuiltin.hh>
#include <zlib/ZfixFmt.hh>

namespace Zfix::Fmt {

namespace FrameState {

  enum { Incomplete, Complete, Error };
}

struct FrameView {
  ZuCSpan	wire;
  ZuCSpan	beginString;
  ZuCSpan	msgType;
  // Bytes after MsgType and before CheckSum; includes remaining header fields.
  ZuCSpan	content;
};

struct FrameResult {
  unsigned	state = FrameState::Incomplete;
  unsigned	consumed = 0;
  unsigned	expected = 0;
  FrameView	view;
  Error		error;
};

// Stateless incremental inspection: append to caller-owned bounded storage and
// retry on Incomplete. Complete consumes exactly one frame, permitting a caller
// to continue over coalesced input. No bytes are retained or allocated here.
ZfixFmtExtern FrameResult frame(ZuCSpan input, Limits limits = {});

// One ordinary message schema consumes the complete bounded content. Session
// metadata is observed independently into retained Rx state, never a second
// public message object or a combined header/body slot layout.
template <typename O, typename Facet = ZuFacet::FIX> class IndexedMessage :
    public Indexed<O, Facet> {
  using Base = Indexed<O, Facet>;
public:
  explicit IndexedMessage(const FrameView &frame, Limits limits = {}) :
    Base{frame.content, limits}, m_frame{frame}, m_limits{limits}, m_offset{frame.content.data() ?
      unsigned(frame.content.data() - frame.wire.data()) : 0} { }
  template <int32_t Tag> O selected(FieldPos &selector) {
    Read input{m_frame.content, m_limits, m_selectedError};
    input.selectTag = Tag;
    unsigned offset = 0;
    Scope scope{nullptr, scopeContains<O, Facet, Tag>, 0, 0};
    readScope<O, Facet, ZfFieldFilter::Init, false, Tag, false>(
      input, nullptr, offset, scope, false);
    selector = input.selector;
    return Base::ctor();
  }
  Error error() const {
    auto e = m_selectedError ? m_selectedError : Base::error();
    if (e && e.code != ErrorCode::Required) e.offset += m_offset;
    return e;
  }
private:
  FrameView m_frame;
  Limits m_limits;
  Error m_selectedError;
  unsigned m_offset;
};

template <typename O, typename Facet> class LiveMessage {
public:
  explicit LiveMessage(const FrameView &frame, Limits limits = {}) :
    m_frame{frame}, m_limits{limits} {
    if (frame.wire.length() > limits.frameBytes) m_error = {ErrorCode::Size};
  }
  bool valid() const { return !m_error; }
  Error error() const { return m_error; }
  template <typename F> ZuCSpan raw() {
    if (m_error) return {};
    IndexedMessage<O, Facet> h{m_frame, m_limits};
    auto value = h.template raw<F>(); m_error = h.error(); return value;
  }
  template <typename ...Args> O ctor(Args &&...args) {
    if constexpr (sizeof...(Args)) {
      IndexedMessage<O, Facet> h{m_frame, m_limits};
      auto o = h.ctor(ZuFwd<Args>(args)...); m_error = h.error(); return o;
    } else {
      O o{}; load_<ZfFieldFilter::Init>(o); return o;
    }
  }
  template <int32_t Tag> O selected(FieldPos &selector) {
    O o{}; load_<ZfFieldFilter::Init, Tag>(o, &selector); return o;
  }
  template <typename ...Args> O *alloc(Args &&...args) {
    if (m_error) return nullptr;
    if constexpr (sizeof...(Args)) {
      IndexedMessage<O, Facet> h{m_frame, m_limits};
      auto o = h.alloc(ZuFwd<Args>(args)...); m_error = h.error(); return o;
    } else {
      O *o = new O{};
      if (!load_<ZfFieldFilter::Init>(*o)) { delete o; return nullptr; }
      return o;
    }
  }
  template <typename ...Args> bool new_(void *ptr, Args &&...args) {
    if (m_error) return false;
    if constexpr (sizeof...(Args)) {
      IndexedMessage<O, Facet> h{m_frame, m_limits};
      bool result = h.new_(ptr, ZuFwd<Args>(args)...); m_error = h.error(); return result;
    } else {
      auto o = new (ptr) O{};
      if (load_<ZfFieldFilter::Init>(*o)) return true;
      o->~O(); return false;
    }
  }
  bool load(O &o) { return load_<ZfFieldFilter::Load>(o); }
  bool update(O &o) { return load_<ZfFieldFilter::Upd>(o); }
private:
  template <template <typename> class Filter, int32_t SelectTag = 0>
  bool load_(O &o, FieldPos *selector = nullptr) {
    if (m_error) return false;
    Error error;
    read<O, Facet, Filter, SelectTag>(o, m_frame.content, error, m_limits, 0, selector);
    if (error && error.code != ErrorCode::Required)
      error.offset += unsigned(m_frame.content.data() - m_frame.wire.data());
    m_error = error; return !error;
  }
  FrameView m_frame;
  Limits m_limits;
  Error m_error;
};

template <typename O, typename Facet = ZuFacet::FIX> class MessageHandler :
    public ZuIf<liveRead<O, Facet>(), LiveMessage<O, Facet>, IndexedMessage<O, Facet>> {
  using Base = ZuIf<liveRead<O, Facet>(), LiveMessage<O, Facet>, IndexedMessage<O, Facet>>;
public:
  using Base::Base;
};

// Incremental progress without input ownership. Input may move as its caller
// grows storage, but prior bytes must remain unchanged and length must grow.
// Reset after consuming a complete frame or abandoning an errored stream.
class ZfixFmtAPI Framer {
public:
  explicit Framer(Limits limits = {}) : m_limits{limits} { }

  const Limits &limits() const { return m_limits; }

  FrameResult scan(ZuCSpan input);
  void reset();

private:
  FrameResult result(FrameResult r);
  FrameResult pending() const;
  const Limits	m_limits;
  Error		m_error;
  uint64_t	m_seen = 0;
  unsigned	m_search = 0;
  unsigned	m_valueOffset = 0;
  unsigned	m_fields = 0;
  unsigned	m_expected = 0;
  bool		m_complete = false;
};

struct FrameOutput {
  ZuCSpan	data;
  Error		error;

  explicit operator bool() const { return !error; }
};

// Standalone storage only. Publish data only after finish() succeeds. Encoding
// occurs once in reserved envelope headroom. finish() returns the authoritative
// output subspan, which can start after storage.data(); body bytes never move.
// Both fixed and growing storage append the checksum over original bytes.
class ZfixFmtAPI FrameWriter {
public:
  using Grow = Sink::Grow;
  FrameWriter(ZuSpan<char> storage, ZuCSpan beginString, Limits limits = {});
  FrameWriter(ZuSpan<char> storage, ZuCSpan beginString, Grow grow, void *owner,
    Limits limits = {});
  FrameWriter(const FrameWriter &) = delete;
  FrameWriter &operator =(const FrameWriter &) = delete;

  Sink &body() { return m_body; }

  FrameOutput finish();

  // Consume this span before appending: a growing sink may relocate storage.
  // Coverage is exactly 35 through the final unsigned field, excluding 8/9/10.
  template <typename Signer, typename Body, typename Context>
  FrameOutput finish(const Signer &signer, const Body *body, Context context) {
    if constexpr (Signer::SignMsg)
      if (!m_finished && !m_body.failed()) signer.signMsg(m_body, body, context, m_body.data());
    return finish();
  }

private:
  struct Layout {
    unsigned	prefix;
    unsigned	body;
  };

  static Layout layout(unsigned capacity, unsigned beginLength, Limits);
  FrameWriter(ZuSpan<char>, ZuCSpan, Limits, Layout);
  static ZuSpan<char> growBody(void *, unsigned used, unsigned needed);

  ZuSpan<char>	m_storage;
  ZuCSpan	m_beginString;
  Limits	m_limits;
  unsigned	m_prefix;
  Sink		m_body;
  Grow		m_grow = nullptr;
  void		*m_owner = nullptr;
  Error		m_error;
  bool		m_finished = false;
};

template <typename Entry, template <typename> class Filter = SaveField>
FrameOutput writeFrame(ZuSpan<char> storage, ZuCSpan beginString,
    const typename Entry::Object &object, Limits limits = {}) {
  FrameWriter writer{storage, beginString, limits};
  writer.body().field(35); writer.body() << Entry::MsgType; writer.body().end();
  if (!save<typename Entry::Mapping, Filter>(writer.body(), object))
    return {{}, writer.body().error()};
  return writer.finish();
}
} // Zfix::Fmt

#endif /* ZfixFrame_HH */
