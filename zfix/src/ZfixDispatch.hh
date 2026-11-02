//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Unique-field catalog selection and ordinary typed decoding

#ifndef ZfixDispatch_HH
#define ZfixDispatch_HH

#ifndef ZfixLib_HH
#include <zlib/ZfixLib.hh>
#endif

#include <zlib/ZuSwitch.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuUnroll.hh>
#include <zlib/Zfix.hh>
#include <zlib/ZfixFrame.hh>

namespace Zfix::Dispatch_ {

// Catalog traits shared by session inspection and trace metadata.
template <typename O, typename Facet> struct Source {
  using Object = O;
  using Mapping = Facet;
};
template <typename E> using EntrySource = Source<typename E::Object, typename E::Mapping>;

template <typename Messages, bool Secondary = false>
ZuInline constexpr int find(ZuCSpan type) {
  int found = -1;
  ZuUnroll::all<Messages>([&found, type]<typename E>() {
    constexpr auto Key = [] {
      if constexpr (Secondary) return E::Discrim::Value;
      else return E::MsgType;
    }();
    // A shared MsgType selects its first catalog representative. Secondary
    // values are unique. Both comparisons use compile-time wire strings.
    if (found < 0 && type.length() == Key.length() && type.template match<Key>())
      found = ZuTypeIndex<E, Messages>{};
  });
  return found;
}

template <typename E, typename Fn>
Fmt::Error decode(const Fmt::FrameView &frame, Fn &fn, Fmt::Limits limits) {
  Fmt::MessageHandler<typename E::Object, typename E::Mapping> handler{frame, limits};
  if (!handler.valid()) return handler.error();
  Fmt::FieldPos selector{UINT_MAX, 0};
  auto object = [&handler, &selector]() {
    (void)selector;
    if constexpr (E::Discrim::Tag)
      return handler.template selected<E::Discrim::Tag>(selector);
    else return handler.ctor();
  }();
  if (auto error = handler.error()) return error;
  if constexpr (E::Discrim::Tag) {
    if (selector.offset == UINT_MAX) return Fmt::Error{Fmt::ErrorCode::Required, E::Discrim::Tag};
    ZuCSpan key{frame.content.data() + selector.offset, selector.length};
    if (key.length() != E::Discrim::Value.length() ||
	!key.template match<E::Discrim::Value>())
      return Fmt::Error{Fmt::ErrorCode::Unresolved, E::Discrim::Tag,
	unsigned(frame.content.data() - frame.wire.data()) + selector.offset};
  }
  return fn(static_cast<E *>(nullptr), object);
}

template <typename Representative, typename Messages> struct Candidates {
  template <typename E> using Same = ZuBool<E::MsgType == Representative::MsgType>;
  using T = ZuTypeGrep<Same, Messages>;
};

inline Fmt::Error wireError(Fmt::Error error, const Fmt::FrameView &frame) {
  if (error && error.code != Fmt::ErrorCode::Required)
    error.offset += unsigned(frame.content.data() - frame.wire.data());
  return error;
}

template <typename> struct CandidateFields;
template <typename ...S> struct CandidateFields<ZuTypeList<S...>> {
  using T = ZuTypeConcat<typename Fmt::WireFields<
    typename S::Object, typename S::Mapping>::T...>;
};

template <typename Representative, typename Messages, typename Fn>
Fmt::Error select(const Fmt::FrameView &frame, Fn &fn, Fmt::Limits limits) {
  using Entries = typename Candidates<Representative, Messages>::T;
  enum { Tag = Representative::Discrim::Tag };
  using Sources = ZuTypeUnique<ZuTypeMap<EntrySource, Entries>>;
  using S = ZuType<0, Sources>;
  if constexpr (Sources::N == 1 && Fmt::liveRead<typename S::Object, typename S::Mapping>()) {
    Fmt::MessageHandler<typename S::Object, typename S::Mapping> handler{frame, limits};
    Fmt::FieldPos selector{UINT_MAX, 0};
    auto object = handler.template selected<Tag>(selector);
    if (!handler.valid()) return handler.error();
    if (selector.offset == UINT_MAX) return {Fmt::ErrorCode::Required, Tag};
    int selected = find<Entries, true>({frame.content.data() + selector.offset, selector.length});
    if (selected < 0) return wireError({Fmt::ErrorCode::Unresolved, Tag, selector.offset}, frame);
    return ZuSwitch::dispatch<Entries::N>(selected,
      [&object, &fn]<typename I>(I) {
	using E = ZuType<I{}, Entries>;
	return fn(static_cast<E *>(nullptr), object);
      });
  }
  using Inspect = Fmt::InspectFields<typename CandidateFields<Sources>::T>;
  Fmt::FieldPos selector;
  if (auto error = Inspect::inspect(frame.content, Tag, selector, limits))
    return wireError(error, frame);
  if (selector.offset == UINT_MAX) return {Fmt::ErrorCode::Required, Tag};
  int selected = find<Entries, true>(
    {frame.content.data() + selector.offset, selector.length});
  if (selected < 0) return wireError(
    {Fmt::ErrorCode::Unresolved, Tag, selector.offset}, frame);
  return ZuSwitch::dispatch<Entries::N>(selected,
    [&frame, &fn, limits]<typename I>(I) {
      return decode<ZuType<I{}, Entries>>(frame, fn, limits);
    });
}
} // Zfix::Dispatch_

namespace Zfix {

namespace Session_ {

template <int32_t Tag, typename Fields> consteval bool hasTag() {
  bool found = false;
  ZuUnroll::all<Fields>([&found]<typename F>() { if (F::Tag == Tag) found = true; });
  return found;
}
template <typename Input, typename Sources> struct Prefix {
  template <typename F> using Wire = Fmt::WireField<Fmt::tag<F>(), Fmt::payloadTag<F>()>;
  using Defaults = ZuTypeMap<Wire, SessionFields<Input>>;
  using Declared = typename Dispatch_::CandidateFields<Sources>::T;
  template <typename F> using Missing = ZuBool<!hasTag<F::Tag, Declared>()>;
  using Fields = ZuTypeUnique<ZuTypeConcat<Declared, ZuTypeGrep<Missing, Defaults>>>;
  using Check = Fmt::InspectFields<Fields>;
  ZuAssert(Check::unique());
  using Reader = unsigned (*)(Prefix &, Fmt::Token &);
  struct Item {
    Reader read;
    bool prefix;
  };

  template <int32_t Tag> static consteval bool accepted() {
    if (sessionTag(Tag)) return true;
    bool accepted = false;
    ZuUnroll::all<Sources>([&accepted]<typename S>() {
      using Leaves = typename Fmt::Schema<typename S::Object, typename S::Mapping>::Leaves;
      int last = -1, index = -1;
      bool group = false;
      ZuUnroll::all<Leaves>([&last, &index, &group]<typename L>() {
	using F = typename L::Field;
	enum { I = ZuTypeIndex<L, Leaves>{} };
	if (sessionTag(Fmt::tag<F>())) last = I;
	if (Fmt::tag<F>() == Tag) { index = I; group = bool(Fmt::GroupField<F, typename S::Mapping>{}); }
      });
      if (index >= 0 && index <= last && !group) accepted = true;
    });
    return accepted;
  }
  template <typename F> struct Handler {
    static unsigned read(Prefix &p, Fmt::Token &token) {
      unsigned value = token.offset;
      unsigned n = F::read(p.input.frame.content, token, p.limits, p.error);
      if (!n) return 0;
      if constexpr (F::Payload) {
	if (!p.template observe<F::Tag>(value, token.countLength) ||
	    !p.template observe<F::Payload>(token.offset, token.length)) return 0;
      } else if (!p.template observe<F::Tag>(token.offset, token.length)) return 0;
      return n;
    }
    inline static const Item item{read, accepted<F::Tag>()};
  };
  enum { Bits = Fields::N > 1 ? ZuIntrin::log2(Fields::N) + 2 : 2 };
  struct Index : ZmLHashKV<int32_t, const Item *, ZmLHashStatic<Bits, ZmLHashLocal<>>> {
    Index() {
      ZuUnroll::all<Fields>([this]<typename F>() { this->add(int32_t(F::Tag), &Handler<F>::item); });
    }
  };
  inline static const Index index{};
  Input &input;
  Fmt::Limits limits;
  Fmt::Error error;
  ZuBitmap<SessionFields<Input>::N> seen;

  template <int32_t Tag> bool observe(unsigned offset, unsigned length) {
    if constexpr (sessionTag(Tag)) {
      using F = SessionField<Input, Tag>;
      enum { I = ZuTypeIndex<F, SessionFields<Input>>{} };
      if (seen.get(I)) {
	if constexpr (Tag == 34) input.seqNo = 0;
	error = {Fmt::ErrorCode::Duplicate, Tag, offset}; return false;
      }
      seen.set(I);
      Fmt::Error converted;
      auto value = Fmt::scalar<F::Type::Code, typename F::Props, typename F::T, ZuFacet::FIX>(
	{input.frame.content.data() + offset, length}, converted);
      if (converted) { error = {converted.code, Tag, offset}; return false; }
      F::set(input, ZuMv(value));
    }
    return true;
  }
  Fmt::Error read() {
    if (input.frame.wire.length() > limits.frameBytes) return {Fmt::ErrorCode::Size};
    unsigned offset = 0;
    while (offset < input.frame.content.length()) {
      Fmt::Token token;
      if (error = Fmt::prefix(input.frame.content, offset, token)) break;
      unsigned value = token.offset;
      auto handler = index.findVal(token.tag);
      if (handler && !handler->prefix) break;
      if (!handler) {
	if constexpr (!Sources::N) break;
	if (Fmt::reserved(token.tag)) { error = {Fmt::ErrorCode::Reserved, token.tag, offset}; break; }
      }
      unsigned consumed = handler ? handler->read(*this, token) :
	Fmt::consume(input.frame.content, token, limits, error);
      if (!consumed) break;
      offset = value + consumed;
    }
    input.sessionBytes = offset;
    if (!error) {
      ZuUnroll::all<SessionFields<Input>>([this]<typename F>() {
	if constexpr (ZuTypeIn<ZuFieldProp::Required, typename F::Props>{})
	  if (!error && !seen.get(ZuTypeIndex<F, SessionFields<Input>>{}))
	    error = {Fmt::ErrorCode::Required, Fmt::tag<F>()};
      });
    }
    if (error && error.code != Fmt::ErrorCode::Required)
      error.offset += unsigned(input.frame.content.data() - input.frame.wire.data());
    return error;
  }
};

} // Session_

// Direct session observation writes into the existing retained Rx item. This
// never constructs a second message representation or scans inside a pair.
template <typename Input>
Fmt::Error readSession(Input &input, Fmt::Limits limits = {}) {
  return Session_::Prefix<Input, ZuTypeList<>>{input, limits}.read();
}
template <typename Dictionary, typename Input>
Fmt::Error catalogSession(Input &input, Fmt::Limits limits = {}) {
  using Messages = typename Dictionary::Messages;
  if constexpr (!Messages::N) return readSession(input, limits);
  else {
    int i = Dispatch_::find<Messages>(input.frame.msgType);
    if (i < 0) return readSession(input, limits);
    return ZuSwitch::dispatch<Messages::N>(i, [&input, limits]<typename I>(I) {
      using E = ZuType<I{}, Messages>;
      using Entries = typename Dispatch_::Candidates<E, Messages>::T;
      using Sources = ZuTypeUnique<ZuTypeMap<Dispatch_::EntrySource, Entries>>;
      return Session_::Prefix<Input, Sources>{input, limits}.read();
    });
  }
}

// FrameView must come from a successful frame()/Framer result. The callback
// receives borrowed decoded values only after complete selected-type validation.
// It returns Error for application refusal; dispatch retains no input bytes.
template <typename Dictionary, typename Fn>
Fmt::Error dispatch(const Fmt::FrameView &frame, Fn &&fn, Fmt::Limits limits = {}) {
  using Messages = typename Dictionary::Messages;
  if (frame.wire.length() > limits.frameBytes) return {Fmt::ErrorCode::Size};
  if (frame.beginString.length() > limits.fieldBytes)
    return {Fmt::ErrorCode::Size, 8, unsigned(frame.beginString.data() - frame.wire.data())};
  if (frame.msgType.length() > limits.fieldBytes)
    return {Fmt::ErrorCode::Size, 35, unsigned(frame.msgType.data() - frame.wire.data())};
  if constexpr (!Messages::N) return {Fmt::ErrorCode::Unresolved, 35};
  else {
    int i = Dispatch_::find<Messages>(frame.msgType);
    if (i < 0) return {Fmt::ErrorCode::Unresolved, 35};
    return ZuSwitch::dispatch<Messages::N>(i, [&frame, &fn, limits]<typename I>(I) {
      using E = ZuType<I{}, Messages>;
      if constexpr (!E::Discrim::Tag) return Dispatch_::decode<E>(frame, fn, limits);
      else return Dispatch_::select<E, Messages>(frame, fn, limits);
    });
  }
}
} // Zfix

#endif /* ZfixDispatch_HH */
