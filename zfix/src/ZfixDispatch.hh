//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Schema-aware catalog selection and typed decoding

#ifndef ZfixDispatch_HH
#define ZfixDispatch_HH

#ifndef ZfixLib_HH
#include <zlib/ZfixLib.hh>
#endif

#include <zlib/ZuCmp.hh>
#include <zlib/ZuIntrin.hh>
#include <zlib/ZuSwitch.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuUnroll.hh>
#include <zlib/ZmLHash.hh>
#include <zlib/ZtBuiltin.hh>
#include <zlib/ZtScratch.hh>
#include <zlib/Zfix.hh>
#include <zlib/ZfixFrame.hh>

namespace Zfix::Dispatch_ {

template <typename O, typename Facet> struct Source {
  using Object = O;
  using Mapping = Facet;
};

template <typename Sources, bool Envelope = false, unsigned Select = 0> struct Scope {
friend Fmt::AsObject Zfix_Fmt(Scope *);
};

template <typename Sources> struct Array {
  Scope<Sources> &operator [](unsigned) const;
friend Fmt::AsArray<> Zfix_Fmt(Array *);
};

template <typename L, typename Facet> struct Leaf {
  using Field = typename L::Field;
  using Mapping = Facet;
};

template <typename Source> struct Leaves {
  template <typename L> using Bind = Leaf<L, typename Source::Mapping>;
  using T = ZuTypeMap<Bind,
    typename Fmt::Schema<typename Source::Object, typename Source::Mapping>::Leaves>;
};

template <typename S> using SourceLeaves = typename Leaves<S>::T;
template <typename Sources> using RawLeaves =
  ZuTypeApply<ZuTypeConcat, ZuTypeMap<SourceLeaves, Sources>>;

template <typename L> using IsGroup =
  Fmt::GroupField<typename L::Field, typename L::Mapping>;
template <typename L> using GroupSource = Source<
  Fmt::GroupElem<typename L::Field::T>, typename L::Mapping>;
template <unsigned Code_> struct WireType { enum { Code = Code_ }; };

template <typename List, bool Group = IsGroup<ZuType<0, List>>{}>
struct Layout {
  using T = ZuIf<bool(Fmt::lenTag<typename ZuType<0, List>::Field>()), ZuBSpan, ZuCSpan>;
  using Type = WireType<Fmt::lenTag<typename ZuType<0, List>::Field>() ?
    ZfFieldTC::Bytes : ZfFieldTC::String>;
};

template <typename List> struct Layout<List, true> {
  using T = Array<ZuTypeUnique<ZuTypeMap<GroupSource, List>>>;
  using Type = WireType<ZfFieldTC::UDT>;
};

template <typename List> struct Field : Layout<List> {
  using First = typename ZuType<0, List>::Field;
  using Props = typename First::Props;

  static consteval bool compatible() {
    bool ok = true;
    ZuUnroll::all<List>([&ok]<typename L>() {
      if (bool(IsGroup<L>{}) != bool(IsGroup<ZuType<0, List>>{}) ||
	  Fmt::lenTag<typename L::Field>() != Fmt::lenTag<First>()) ok = false;
    });
    return ok;
  }

  ZuAssert(compatible());
};

template <unsigned Tag, typename List> consteval unsigned offset() {
  unsigned out = UINT_MAX;
  ZuUnroll::all<List>([&out]<typename L>() {
    if constexpr (Fmt::tag<typename L::Field>() == Tag)
      if (out == UINT_MAX) out = ZuTypeIndex<L, List>{};
  });
  return out;
}
template <typename Raw_> struct Fields {
  using Raw = ZuTypeUnique<Raw_>;
  template <typename L> using First = ZuBool<
    offset<Fmt::tag<typename L::Field>(), Raw>() == ZuTypeIndex<L, Raw>{}>;
  template <typename L> struct Project {
    template <typename R> using Same = ZuBool<
      Fmt::tag<typename L::Field>() == Fmt::tag<typename R::Field>()>;
    using T = Field<ZuTypeGrep<Same, Raw>>;
  };

  template <typename L> using Projected = typename Project<L>::T;
  using T = ZuTypeMap<Projected, ZuTypeGrep<First, Raw>>;
};

template <unsigned Tag> struct Key { };

template <unsigned Tag> struct KeyField {
  using T = ZuCSpan;
  using Type = WireType<ZfFieldTC::String>;
  using Props = ZuTypeList<ZuFieldProp::FIX::ID<Tag>>;
};

} // Zfix::Dispatch_

namespace Zfix::Fmt {

template <unsigned Tag, typename Facet>
struct Schema<Dispatch_::Key<Tag>, Facet> :
  Schema_<Dispatch_::Key<Tag>, Facet, ZuTypeList<Dispatch_::KeyField<Tag>>> { };

// This is a compile-time projection of ordinary FIX fields. TagIndex,
// parseScope, parseGroup and all byte/depth/entry bounds remain the codec's.
template <typename Sources, bool Envelope, unsigned Select, typename Facet>
struct Schema<Dispatch_::Scope<Sources, Envelope, Select>, Facet> :
  Schema_<Dispatch_::Scope<Sources, Envelope, Select>, Facet,
    typename Dispatch_::Fields<Dispatch_::RawLeaves<Sources>>::T, Envelope> {
  using Base = Schema_<Dispatch_::Scope<Sources, Envelope, Select>, Facet,
    typename Dispatch_::Fields<Dispatch_::RawLeaves<Sources>>::T, Envelope>;
  enum { Ordered = false, SelectTag = Select };

  static unsigned region(unsigned field) {
    if constexpr (!Envelope) return 1;
    unsigned header = Schema<Header, ZuFacet::FIX>::Leaves::N;
    unsigned trailer = Schema<Trailer, ZuFacet::FIX>::Leaves::N;
    if (field < header) return 0;
    if (field < Base::Leaves::N - trailer) return 1;
    return 2;
  }
};

} // Zfix::Fmt

namespace Zfix::Dispatch_ {

template <typename E> using EntrySource = Source<typename E::Object, typename E::Mapping>;
template <typename Entries, unsigned Tag> struct Wire {
  using Sources = ZuTypeConcat<ZuTypeList<Source<Fmt::Header, ZuFacet::FIX>>,
    ZuTypeMap<EntrySource, Entries>, ZuTypeList<Source<Fmt::Trailer, ZuFacet::FIX>>>;

  static consteval bool contains() {
    bool out = false;
    ZuUnroll::all<RawLeaves<Sources>>([&out]<typename L>() {
      using F = typename L::Field;
      if (Fmt::tag<F>() == Tag || Fmt::lenTag<F>() == Tag) out = true;
    });
    return out;
  }

  // Keep header/body/trailer regions contiguous when adding an otherwise
  // unmodelled selector. Existing data/length fields retain their wire shape.
  using T = Scope<ZuTypeConcat<ZuTypeList<Source<Fmt::Header, ZuFacet::FIX>>,
    ZuTypeMap<EntrySource, Entries>,
    ZuIf<contains(), ZuTypeList<>, ZuTypeList<Source<Key<Tag>, ZuFacet::FIX>>>,
    ZuTypeList<Source<Fmt::Trailer, ZuFacet::FIX>>>, true, Tag>;
};

template <typename O, typename Facet, typename Scan>
bool project(const Fmt::Positions &input, unsigned from, Fmt::Parse &out,
    unsigned to, bool entry = false) {
  using S = Fmt::Schema<O, Facet>;
  using W = Fmt::Schema<Scan, ZuFacet::FIX>;
  unsigned previous = 0;
  ZuUnroll::all<typename S::Leaves>([&input, from, &out, to, entry, &previous]<typename L>() {
    if (out.error) return;
    using F = typename L::Field;
    constexpr unsigned i = offset<Fmt::tag<F>(), typename W::Leaves>();
    ZuAssert(i != UINT_MAX);
    constexpr unsigned j = ZuTypeIndex<L, typename S::Leaves>{};
    auto pos = input[from + i];
    if (pos.offset == UINT_MAX) return;
    if (entry && pos.offset < previous) {
      out.fail(Fmt::ErrorCode::Group, Fmt::tag<F>(), pos.offset); return;
    }
    previous = pos.offset;
    if constexpr (Fmt::GroupField<F, Facet>{}) {
      using E = Fmt::GroupElem<typename F::T>;
      using WF = typename ZuType<i, typename W::Leaves>::Field;
      using Child = Fmt::GroupElem<typename WF::T>;
      constexpr unsigned n = Fmt::Schema<E, Facet>::Leaves::N;
      constexpr unsigned stride = Fmt::Schema<Child, ZuFacet::FIX>::Leaves::N;
      if (pos.count > out.limits.parseBytes / (sizeof(Fmt::Position) * uint64_t(n))) {
	out.fail(Fmt::ErrorCode::Size, Fmt::tag<F>(), pos.offset); return;
      }
      unsigned child = out.slots(pos.count * n);
      if (out.error) return;
      for (unsigned k = 0; k < pos.count && !out.error; ++k)
	project<E, Facet, Child>(input, pos.child + k * stride, out, child + k * n, true);
      pos.child = child;
    }
    out.positions[to + j] = pos;
  });
  return !out.error;
}

template <typename Messages, bool Secondary = false> struct Index : ZmLHashKV<ZuCSpan, unsigned,
  ZmLHashStatic<(Messages::N > 1 ? ZuIntrin::log2(Messages::N) + 2 : 2), ZmLHashLocal<>>> {
  using Base = ZmLHashKV<ZuCSpan, unsigned,
    ZmLHashStatic<(Messages::N > 1 ? ZuIntrin::log2(Messages::N) + 2 : 2), ZmLHashLocal<>>>;

  Index() {
    ZuUnroll::all<Messages>([this]<typename E>() {
      unsigned i = ZuTypeIndex<E, Messages>{};
      if constexpr (Secondary) {
	ZuCSpan key = E::Discriminator::value; this->add(key, i);
      } else {
	ZuCSpan key = E::msgType;
	if (ZuCmp<unsigned>::null(Base::findVal(key))) this->add(key, i);
      }
    });
  }

  int find(ZuCSpan type) const {
    auto i = Base::findVal(type); return ZuCmp<unsigned>::null(i) ? -1 : int(i);
  }
};

template <typename Messages, bool Secondary = false> const auto &index() {
  static const Index<Messages, Secondary> value;
  return value;
}

template <typename E, typename Fn>
Fmt::Error decode(const Fmt::FrameView &frame, Fn &fn, Fmt::Limits limits) {
  Fmt::MessageHandler<typename E::Object, typename E::Mapping> handler{frame, limits};
  if (!handler.valid()) return handler.error();
  auto object = handler.ctor();
  if (!handler.valid()) return handler.error();
  return fn(static_cast<E *>(nullptr), object, handler.header(), handler.trailer());
}

template <typename Representative, typename Messages> struct Candidates {
  template <typename E> using Same = ZuBool<E::msgType == Representative::msgType>;
  using T = ZuTypeGrep<Same, Messages>;
};

inline Fmt::Error wireError(Fmt::Error error, const Fmt::FrameView &frame) {
  if (error && error.code != Fmt::ErrorCode::Required)
    error.offset += unsigned(frame.content.data() - frame.wire.data());
  return error;
}

template <typename E, typename Fn>
Fmt::Error decodeParsed(const Fmt::FrameView &frame, const Fmt::Positions &positions,
    Fn &fn) {
  using O = typename E::Object;
  using Facet = typename E::Mapping;
  using S = Fmt::Schema<Fmt::Wire_<O>, Facet>;
  Fmt::Error error;
  Fmt::Header header;
  header.beginString = frame.beginString; header.msgType = frame.msgType;
  error = Fmt::loadHeader_(frame.content, positions, header, error);
  Fmt::Trailer trailer;
  Fmt::ObjectView<Fmt::Trailer, ZuFacet::FIX>{frame.content, positions,
    S::HeaderN + S::BodyN, error}.load(trailer);
  trailer.checksum = frame.trailer.checksum;
  if (error) return wireError(error, frame);
  Fmt::ObjectView<O, Facet> view{frame.content, positions, S::HeaderN, error};
  auto object = view.ctor();
  if (error) return wireError(error, frame);
  return fn(static_cast<E *>(nullptr), object, header, trailer);
}

template <typename E, typename Scan, typename Fn>
Fmt::Error decode(const Fmt::FrameView &frame, const Fmt::Positions &positions,
    Fn &fn, Fmt::Limits limits) {
  using O = typename E::Object;
  using Facet = typename E::Mapping;
  using S = Fmt::Schema<Fmt::Wire_<O>, Facet>;
  Fmt::Error error;
  // Account both live position arrays at projection's peak. No payload or
  // business object is copied from another candidate or decoded speculatively.
  uint64_t used = positions.length() * uint64_t(sizeof(Fmt::Position));
  if (used > limits.parseBytes) return {Fmt::ErrorCode::Size};
  limits.parseBytes -= used;
  // Projection cannot add positions. Reserve its permitted upper bound once
  // on stack, falling back to the same named heap for larger group trees.
  auto capacity = positions.length();
  auto maximum = limits.parseBytes/sizeof(Fmt::Position);
  if (capacity > maximum) capacity = maximum;
  auto selected = ZtScratch(Fmt::Positions, capacity);
  Fmt::Parse out{frame.content, limits, selected, error};
  out.slots(S::Leaves::N);
  if (!error) project<Fmt::Wire_<O>, Facet, Scan>(positions, 0, out, 0);
  if (error) return wireError(error, frame);
  return decodeParsed<E>(frame, selected, fn);
}

template <typename Representative, typename Messages, typename Fn>
Fmt::Error select(const Fmt::FrameView &frame, Fn &fn, Fmt::Limits limits) {
  using Entries = typename Candidates<Representative, Messages>::T;
  constexpr unsigned tag = Representative::Discriminator::tag;
  using Combined = Wire<Entries, tag>;
  enum { Direct = ZuTypeUnique<ZuTypeMap<EntrySource, Entries>>::N == 1 &&
    Combined::contains() };
  // Identical object/facet candidates have one ordinary wire layout. Parse its
  // actual group ordering directly, avoiding a second projected position tree.
  using Scan = ZuIf<Direct, Fmt::Wire_<typename Representative::Object>, typename Combined::T>;
  using Facet = ZuIf<Direct, typename Representative::Mapping, ZuFacet::FIX>;
  using S = Fmt::Schema<Scan, Facet>;
  Fmt::Error error;
  Fmt::Position selector{UINT_MAX, 0, 0, 0};
  ZtBuiltin<Fmt::Positions, S::Leaves::N> positions;
  Fmt::Parse input{frame.content, limits, positions, error};
  input.slots(S::Leaves::N);
  input.selectTag = tag; input.selected = &selector;
  unsigned offset = 0;
  Fmt::Scope scope{nullptr, Fmt::scopeContains<Scan, Facet>, 0, 0};
  if (!error) Fmt::parseScope<Scan, Facet, tag>(input, 0, offset, scope, false);
  unsigned prefix = unsigned(frame.content.data() - frame.wire.data());
  if (error) { error.offset += prefix; return error; }
  if (selector.offset == UINT_MAX) return {Fmt::ErrorCode::Required, tag};
  int selected = index<Entries, true>().find(
    {frame.content.data() + selector.offset, selector.length});
  if (selected < 0) return {Fmt::ErrorCode::Unresolved, tag, prefix + selector.offset};
  if constexpr (Direct) return ZuSwitch::dispatch<Entries::N>(selected,
    [&frame, &positions, &fn]<typename I>(I) {
      using E = ZuType<I{}, Entries>;
      return decodeParsed<E>(frame, positions, fn);
    });
  else return ZuSwitch::dispatch<Entries::N>(selected,
    [&frame, &positions, &fn, limits]<typename I>(I) {
      return decode<ZuType<I{}, Entries>, Scan>(frame, positions, fn, limits);
    });
}
} // Zfix::Dispatch_

namespace Zfix {

// Header-prefix inspection uses the same catalog's known scope boundaries.
// Unsupported MsgTypes retain the codec's conservative prefix-only behavior.
template <typename Dictionary>
Fmt::Error catalogHeader(const Fmt::FrameView &frame, Fmt::Header &header,
    Fmt::Limits limits = {}) {
  using Messages = typename Dictionary::Messages;
  if constexpr (!Messages::N) return Fmt::readHeader(frame, header, limits);
  else {
    int i = Dispatch_::index<Messages>().find(frame.msgType);
    if (i < 0) return Fmt::readHeader(frame, header, limits);
    return ZuSwitch::dispatch<Messages::N>(i, [&frame, &header, limits]<typename I>(I) {
      using E = ZuType<I{}, Messages>;
      if constexpr (!E::Discriminator::tag)
	return Fmt::readHeader<typename E::Object, typename E::Mapping>(frame, header, limits);
      else {
	using Entries = typename Dispatch_::Candidates<E, Messages>::T;
	using Scan = typename Dispatch_::Wire<Entries, E::Discriminator::tag>::T;
	return Fmt::readHeader_(frame, header,
	  Fmt::scopeContains<Scan, ZuFacet::FIX>, limits);
      }
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
    int i = Dispatch_::index<Messages>().find(frame.msgType);
    if (i < 0) return {Fmt::ErrorCode::Unresolved, 35};
    return ZuSwitch::dispatch<Messages::N>(i, [&frame, &fn, limits]<typename I>(I) {
      using E = ZuType<I{}, Messages>;
      if constexpr (!E::Discriminator::tag) return Dispatch_::decode<E>(frame, fn, limits);
      else return Dispatch_::select<E, Messages>(frame, fn, limits);
    });
  }
}
} // Zfix

#endif /* ZfixDispatch_HH */
