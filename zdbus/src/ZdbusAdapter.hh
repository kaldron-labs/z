//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Application headers/body adapters over the direct ZfDBUS codec

#ifndef ZdbusAdapter_HH
#define ZdbusAdapter_HH

#ifndef ZdbusLib_HH
#include <zlib/ZdbusLib.hh>
#endif

#include <zlib/ZuTL.hh>
#include <zlib/ZuUnroll.hh>
#include <zlib/ZfDBUS.hh>
#include <zlib/ZdbusMessage.hh>

namespace Zdbus_ {

namespace Header {
  struct Path { enum { Code = HeaderCode::Path, Type = ZfDBUS::Type::ObjectPath }; };
  struct Interface { enum { Code = HeaderCode::Interface, Type = ZfDBUS::Type::String }; };
  struct Member { enum { Code = HeaderCode::Member, Type = ZfDBUS::Type::String }; };
  struct ErrorName { enum { Code = HeaderCode::ErrorName, Type = ZfDBUS::Type::String }; };
  struct Destination { enum { Code = HeaderCode::Destination, Type = ZfDBUS::Type::String }; };
}

template <typename Key_, typename Fixed_ = ZuVoid>
struct HeaderEntry {
  using Key = Key_;
  using Fixed = Fixed_;
};

using NoHeaders = ZuTypeList<>;

// These are restricted to application-controlled routing fields.
ZdbusExtern bool setHeader(HeadSpec &, unsigned code, ZuCSpan,
  unsigned &seen);
ZdbusExtern ZuCSpan headerValue(const HeaderView &, unsigned code);

template <typename Impl, typename L, typename = void>
struct HasRuntimeHeader : ZuFalse { };
template <typename Impl, typename L>
struct HasRuntimeHeader<Impl, L, decltype(
  ZuDeclVal<const Impl *>()->header(ZuDeclVal<L>()), void())> : ZuTrue { };

template <typename Impl, typename Key, typename L, typename = void>
struct HasFixedHeader : ZuFalse { };
template <typename Impl, typename Key, typename L>
struct HasFixedHeader<Impl, Key, L, decltype(
  ZuDeclVal<const Impl *>()->template header<Key>(ZuDeclVal<L>()),
  void())> : ZuTrue { };

template <typename Impl, typename Key, typename = void>
struct HasParseHeader : ZuFalse { };
template <typename Impl, typename Key>
struct HasParseHeader<Impl, Key, decltype(
  ZuDeclVal<Impl *>()->template header<Key>(ZuCSpan{}), void())> : ZuTrue { };

template <typename Impl, typename = void>
struct HasUnknownHeader : ZuFalse { };
template <typename Impl>
struct HasUnknownHeader<Impl, decltype(
  ZuDeclVal<Impl *>()->header(unsigned{}, ZfDBUS::Any{}), void())> : ZuTrue { };

template <typename Key, typename = void>
struct HeaderKeyOK : ZuFalse { };
template <typename Key>
struct HeaderKeyOK<Key, decltype(Key::Code, void())> : ZuTrue { };

struct Builder {
  using Body_DBUS_Facet = ZuFacet::DBUS;
  template <typename Object>
  const Object &bodyObject(const Object *object) const { return *object; }
  template <typename Key, typename Emit>
  void header(Emit &&) const { }
  template <typename Emit>
  void header(Emit &&) const { }
};

struct Parser {
  using Body_DBUS_Facet = ZuFacet::DBUS;
  template <typename Object>
  Object &bodyObject(Object *object) { return *object; }
  template <typename Key>
  void header(ZuCSpan) { }
  void header(unsigned, ZfDBUS::Any) { }
};

template <typename L>
bool eachHeader(ZuBSpan bytes, const FrameInfo &info, L &&callback)
{
  auto fields = ZuBSpan{bytes.begin() + Wire::PrefixSize,
    unsigned(sizeof(uint32_t) + info.fieldsLength)};
  ZfDBUS::Reader reader{{fields, "a(yv)", Wire::PrefixSize, info.order}};
  uint32_t length;
  if (!reader.integer(length) || length != info.fieldsLength ||
      !reader.align(8)) return false;
  while (reader.p != reader.end) {
    if (!reader.align(8)) return false;
    if (reader.p == reader.end) break;
    unsigned code = *reader.p++;
    ZuBSpan sig;
    if (!code || !ZfDBUS::text(reader, ZfDBUS::Type::Signature, sig))
      return false;
    auto begin = reader.p;
    unsigned offset = reader.view.offset +
      unsigned(begin - reader.view.bytes.begin());
    auto p = sig.begin();
    if (!ZfDBUS::skipValue(reader, p, sig.end(), 0) || p != sig.end())
      return false;
    ZfDBUS::Any value{{begin, unsigned(reader.p - begin)},
      ZuCSpan{sig}, offset, info.order};
    callback(code, value);
  }
  return bool(reader.result) && reader.p == reader.end;
}

template <unsigned Kind, typename Impl, typename Object, typename Headers>
struct BuildAdapter : public Builder {
  const Object	*object = nullptr;

  auto impl() const { return static_cast<const Impl *>(this); }
  void init(const Object *object_) { object = object_; }

  bool headers(HeadSpec &head) const {
    unsigned seen = 0;
    bool ok = true;
    ZuUnroll::all<Headers>([this, &head, &seen, &ok]<typename Entry>() {
      using Key = typename Entry::Key;
      using Fixed = typename Entry::Fixed;
      ZuAssert((HeaderKeyOK<Key>{}));
      if constexpr (!ZuIsSame<Fixed, ZuVoid>{}) {
        if (!setHeader(head, Key::Code, Fixed{}(), seen)) ok = false;
      } else {
        auto emit = [&head, &seen, &ok](ZuCSpan value) {
          if (!setHeader(head, Key::Code, value, seen)) ok = false;
        };
        if constexpr (HasFixedHeader<Impl, Key, decltype(emit)>{})
          impl()->template header<Key>(emit);
      }
    });
    auto emit = [&head, &seen, &ok]<typename Key>(Key, ZuCSpan value) {
      if constexpr (HeaderKeyOK<Key>{}) {
        if (!setHeader(head, Key::Code, value, seen)) ok = false;
      } else ok = false;
    };
    if constexpr (HasRuntimeHeader<Impl, decltype(emit)>{})
      impl()->header(emit);
    return ok;
  }

  BuildResult build(uint32_t serial, uint32_t replySerial = 0,
    ZuCSpan destination = {}, unsigned limit = Wire::MaxMessageSize) const {
    ZmAssert_(object);
    HeadSpec head;
    head.type = Kind;
    head.serial = serial;
    head.replySerial = replySerial;
    head.destination = destination;
    if (!headers(head)) return {{}, {}, BuildError::Header};
    const auto &body = impl()->bodyObject(object);
    using Body = ZuDecay<decltype(body)>;
    using Facet = typename Impl::Body_DBUS_Facet;
    return message<Body, Facet>(head, body, limit);
  }
};

namespace ParseError { enum { OK, Type, Body, Header }; }

struct ParseResult {
  ZmRef<ZiIOBuf>	frame;
  ZfDBUS::Result	codec;
  int			error = ParseError::OK;

  explicit operator bool() const { return error == ParseError::OK; }
};

template <unsigned Kind, typename Impl, typename Object, typename Headers>
struct ParseAdapter : public Parser {
  using ObjectT = Object;
  using HeaderCatalog = Headers;

  Object	*object = nullptr;
  // Retains all borrowed header and body spans until reset or destruction.
  ZmRef<ZiIOBuf> frame;

  auto impl() { return static_cast<Impl *>(this); }
  void init(Object *object_) { object = object_; }
  void reset() { frame = {}; }

  ParseResult load(ZmRef<ZiIOBuf> frame_, const FrameInfo &info) {
    ZmAssert_(object);
    if (info.type != Kind) return {{}, {}, ParseError::Type};
    frame = ZuMv(frame_);
    ZuUnroll::all<Headers>([this, &info]<typename Entry>() {
      using Key = typename Entry::Key;
      ZuCSpan value = headerValue(info.headers, Key::Code);
      if (!value.length()) return;
      if constexpr (HasParseHeader<Impl, Key>{})
        impl()->template header<Key>(value);
    });
    auto unknown = [this](unsigned code, ZfDBUS::Any value) {
      if (code <= HeaderCode::UnixFDs) return;
      if constexpr (HasUnknownHeader<Impl>{})
        impl()->header(code, value);
    };
    if (!eachHeader(frame->cspan(), info, unknown))
      return {frame, {}, ParseError::Header};
    auto &body = impl()->bodyObject(object);
    using Body = ZuDecay<decltype(body)>;
    using Facet = typename Impl::Body_DBUS_Facet;
    auto view = ZfDBUS::View{frame->cspan(info.bodyOffset),
      info.headers.signature, info.bodyOffset, info.order};
    auto handler = ZfDBUS::handler<Body, Facet>(view);
    if (!handler) return {frame, handler.result(), ParseError::Body};
    handler.load(body);
    return {frame, handler.result(), ParseError::OK};
  }
};

template <typename Impl, typename Object, typename Headers = NoHeaders>
struct ReqBuilder : public BuildAdapter<MessageType::MethodCall,
  Impl, Object, Headers> { };
template <typename Impl, typename Object, typename Headers = NoHeaders>
struct ResBuilder : public BuildAdapter<MessageType::MethodReturn,
  Impl, Object, Headers> { };
template <typename Impl, typename Object, typename Headers = NoHeaders>
struct ErrBuilder : public BuildAdapter<MessageType::Error,
  Impl, Object, Headers> { };
template <typename Impl, typename Object, typename Headers = NoHeaders>
struct SigBuilder : public BuildAdapter<MessageType::Signal,
  Impl, Object, Headers> { };

template <typename Impl, typename Object, typename Headers = NoHeaders>
struct ReqParser : public ParseAdapter<MessageType::MethodCall,
  Impl, Object, Headers> { };
template <typename Impl, typename Object, typename Headers = NoHeaders>
struct ResParser : public ParseAdapter<MessageType::MethodReturn,
  Impl, Object, Headers> { };
template <typename Impl, typename Object, typename Headers = NoHeaders>
struct ErrParser : public ParseAdapter<MessageType::Error,
  Impl, Object, Headers> { };
template <typename Impl, typename Object, typename Headers = NoHeaders>
struct SigParser : public ParseAdapter<MessageType::Signal,
  Impl, Object, Headers> { };

} // Zdbus_

template <typename Key, typename Value>
using ZdbusHeaderEntry = Zdbus_::HeaderEntry<Key, Value>;
using ZdbusHeaderPath = Zdbus_::Header::Path;
using ZdbusHeaderInterface = Zdbus_::Header::Interface;
using ZdbusHeaderMember = Zdbus_::Header::Member;
using ZdbusHeaderErrorName = Zdbus_::Header::ErrorName;
using ZdbusHeaderDestination = Zdbus_::Header::Destination;

template <typename Impl, typename Object, typename Headers = Zdbus_::NoHeaders>
using ZdbusReqBuilder = Zdbus_::ReqBuilder<Impl, Object, Headers>;
template <typename Impl, typename Object, typename Headers = Zdbus_::NoHeaders>
using ZdbusResBuilder = Zdbus_::ResBuilder<Impl, Object, Headers>;
template <typename Impl, typename Object, typename Headers = Zdbus_::NoHeaders>
using ZdbusErrBuilder = Zdbus_::ErrBuilder<Impl, Object, Headers>;
template <typename Impl, typename Object, typename Headers = Zdbus_::NoHeaders>
using ZdbusSigBuilder = Zdbus_::SigBuilder<Impl, Object, Headers>;
template <typename Impl, typename Object, typename Headers = Zdbus_::NoHeaders>
using ZdbusReqParser = Zdbus_::ReqParser<Impl, Object, Headers>;
template <typename Impl, typename Object, typename Headers = Zdbus_::NoHeaders>
using ZdbusResParser = Zdbus_::ResParser<Impl, Object, Headers>;
template <typename Impl, typename Object, typename Headers = Zdbus_::NoHeaders>
using ZdbusErrParser = Zdbus_::ErrParser<Impl, Object, Headers>;
template <typename Impl, typename Object, typename Headers = Zdbus_::NoHeaders>
using ZdbusSigParser = Zdbus_::SigParser<Impl, Object, Headers>;

#endif /* ZdbusAdapter_HH */
