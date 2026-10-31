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

#include <zlib/ZuCmp.hh>
#include <zlib/ZuIntrin.hh>
#include <zlib/ZuSwitch.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuUnroll.hh>
#include <zlib/ZmLHash.hh>
#include <zlib/Zfix.hh>
#include <zlib/ZfixFrame.hh>

namespace Zfix::Dispatch_ {

// Catalog traits shared by header inspection and trace metadata.
template <typename O, typename Facet> struct Source {
  using Object = O;
  using Mapping = Facet;
};
template <typename E> using EntrySource = Source<typename E::Object, typename E::Mapping>;

template <typename Messages, bool Secondary = false> struct Index : ZmLHashKV<ZuCSpan, unsigned,
  ZmLHashStatic<(Messages::N > 1 ? ZuIntrin::log2(Messages::N) + 2 : 2), ZmLHashLocal<>>> {
  using Base = ZmLHashKV<ZuCSpan, unsigned,
    ZmLHashStatic<(Messages::N > 1 ? ZuIntrin::log2(Messages::N) + 2 : 2), ZmLHashLocal<>>>;

  Index() {
    ZuUnroll::all<Messages>([this]<typename E>() {
      unsigned i = ZuTypeIndex<E, Messages>{};
      if constexpr (Secondary) {
	ZuCSpan key = E::Discrim::Value; this->add(key, i);
      } else {
	ZuCSpan key = E::MsgType;
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
Fmt::Error decode(const Fmt::FrameView &frame, Fn &fn, Fmt::Limits limits,
    const Fmt::Header *header) {
  Fmt::MessageHandler<typename E::Object, typename E::Mapping> handler{frame, limits, header};
  if (!handler.valid()) return handler.error();
  auto object = handler.ctor();
  if (!handler.valid()) return handler.error();
  return fn(static_cast<E *>(nullptr), object, handler.header(), handler.trailer());
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

// Header-prefix inspection needs only root tag membership, not a merged schema.
template <typename Entries> bool bodyContains(unsigned tag) {
  bool found = Fmt::scopeContains<Fmt::Trailer, ZuFacet::FIX>(tag);
  ZuUnroll::all<Entries>([&found, tag]<typename E>() {
    if (tag == E::Discrim::Tag ||
        Fmt::scopeContains<typename E::Object, typename E::Mapping>(tag)) found = true;
  });
  return found;
}

template <typename Representative, typename Messages, typename Fn>
Fmt::Error select(const Fmt::FrameView &frame, Fn &fn, Fmt::Limits limits,
    const Fmt::Header *header) {
  using Entries = typename Candidates<Representative, Messages>::T;
  enum { Tag = Representative::Discrim::Tag };
  Fmt::FieldPos selector;
  if (auto error = Fmt::inspect(frame.content, Tag, selector, limits, header ? header->bytes : 0))
    return wireError(error, frame);
  if (selector.offset == UINT_MAX) return {Fmt::ErrorCode::Required, Tag};
  int selected = index<Entries, true>().find(
    {frame.content.data() + selector.offset, selector.length});
  if (selected < 0) return wireError(
    {Fmt::ErrorCode::Unresolved, Tag, selector.offset}, frame);
  return ZuSwitch::dispatch<Entries::N>(selected,
    [&frame, &fn, limits, header]<typename I>(I) {
      return decode<ZuType<I{}, Entries>>(frame, fn, limits, header);
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
      if constexpr (!E::Discrim::Tag)
	return Fmt::readHeader<typename E::Object, typename E::Mapping>(frame, header, limits);
      else {
	using Entries = typename Dispatch_::Candidates<E, Messages>::T;
	return Fmt::readHeader_(frame, header, Dispatch_::bodyContains<Entries>, limits);
      }
    });
  }
}

// FrameView must come from a successful frame()/Framer result. The callback
// receives borrowed decoded values only after complete selected-type validation.
// It returns Error for application refusal; dispatch retains no input bytes.
// An optional header is a successful catalogHeader() result for this frame.
template <typename Dictionary, typename Fn>
Fmt::Error dispatch(const Fmt::FrameView &frame, Fn &&fn, Fmt::Limits limits = {},
    const Fmt::Header *header = nullptr) {
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
    return ZuSwitch::dispatch<Messages::N>(i, [&frame, &fn, limits, header]<typename I>(I) {
      using E = ZuType<I{}, Messages>;
      if constexpr (!E::Discrim::Tag) return Dispatch_::decode<E>(frame, fn, limits, header);
      else return Dispatch_::select<E, Messages>(frame, fn, limits, header);
    });
  }
}
} // Zfix

#endif /* ZfixDispatch_HH */
