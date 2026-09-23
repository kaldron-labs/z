//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Request-specific typed return and named-error dispatch

#ifndef ZdbusCatalog_HH
#define ZdbusCatalog_HH

#ifndef ZdbusLib_HH
#include <zlib/ZdbusLib.hh>
#endif

#include <zlib/ZuSwitch.hh>
#include <zlib/ZfDBUS.hh>
#include <zlib/ZdbusAdapter.hh>

namespace Zdbus_ {

template <typename Req, typename = void>
struct GetResponses_ { using T = NoHeaders; };
template <typename Req>
struct GetResponses_<Req, decltype(
  ZuDeclVal<typename Req::Responses *>(), void())> {
  using T = typename Req::Responses;
};
template <typename Req>
using GetResponses = typename GetResponses_<Req>::T;

template <typename Req, typename = void>
struct GetErrors_ { using T = NoHeaders; };
template <typename Req>
struct GetErrors_<Req, decltype(
  ZuDeclVal<typename Req::Errors *>(), void())> {
  using T = typename Req::Errors;
};
template <typename Req>
using GetErrors = typename GetErrors_<Req>::T;

template <typename Key, typename Headers>
struct FixedHeader_;
template <typename Key>
struct FixedHeader_<Key, ZuTypeList<>> { using T = ZuVoid; };
template <typename Key, typename Entry, typename ...Tail>
struct FixedHeader_<Key, ZuTypeList<Entry, Tail...>> {
  using T = ZuIf<ZuIsSame<Key, typename Entry::Key>{},
    typename Entry::Fixed,
    typename FixedHeader_<Key, ZuTypeList<Tail...>>::T>;
};
template <typename Key, typename Headers>
using FixedHeader = typename FixedHeader_<Key, Headers>::T;

template <typename Key, typename Headers>
struct HeaderCount_;
template <typename Key>
struct HeaderCount_<Key, ZuTypeList<>> { enum { N = 0 }; };
template <typename Key, typename Entry, typename ...Tail>
struct HeaderCount_<Key, ZuTypeList<Entry, Tail...>> {
  enum { N = unsigned(ZuIsSame<Key, typename Entry::Key>{}) +
    HeaderCount_<Key, ZuTypeList<Tail...>>::N };
};
template <typename Key, typename Headers>
constexpr unsigned HeaderCount = HeaderCount_<Key, Headers>::N;

template <typename Schema>
using ErrorName = FixedHeader<Header::ErrorName,
  typename Schema::HeaderCatalog>;

template <typename Schema>
struct Parsed {
  ZmRef<ZiIOBuf>	frame;
  FrameInfo		info;
  typename Schema::ObjectT object;
};

struct RemoteError {
  ZmRef<ZiIOBuf>	frame;
  FrameInfo		info;

  ZuCSpan name() const { return info.headers.errorName; }
  ZuCSpan signature() const { return info.headers.signature; }
  ZuBSpan body() const { return frame->cspan(info.bodyOffset); }
};

namespace TypedError {
  enum { Local, Type, Signature, Body };
}

struct TypedFault {
  ZfDBUS::Result codec;
  int error = TypedError::Local;
  int local = 0;
};

template <typename Schema>
using BodyObject = ZuDecay<decltype(ZuDeclVal<Schema *>()->bodyObject(
  ZuDeclVal<typename Schema::ObjectT *>()))>;

template <ZfDBUS::SigConst Signature>
struct SignatureTag { };

template <typename Schema>
using BodySignatureTag = SignatureTag<ZfDBUS::signatureConst<
  BodyObject<Schema>, typename Schema::Body_DBUS_Facet>()>;

template <typename Schema>
ZuCSpan bodySignature()
{
  using Body = BodyObject<Schema>;
  using Facet = typename Schema::Body_DBUS_Facet;
  static constexpr auto sig = ZfDBUS::signatureConst<Body, Facet>();
  return {sig.bytes, sig.length};
}

template <typename Schemas>
int signatureMatch(ZuCSpan signature)
{
  int match = -1;
  unsigned index = 0;
  ZuUnroll::all<Schemas>([&match, &index, signature]<typename Schema>() {
    if (bodySignature<Schema>() == signature) match = int(index);
    ++index;
  });
  return match;
}

template <typename Errors>
int errorMatch(ZuCSpan name)
{
  int match = -1;
  unsigned index = 0;
  ZuUnroll::all<Errors>([&match, &index, name]<typename Schema>() {
    using Fixed = FixedHeader<Header::ErrorName,
      typename Schema::HeaderCatalog>;
    ZuAssert((!ZuIsSame<Fixed, ZuVoid>{}));
    if (Fixed{}() == name) match = int(index);
    ++index;
  });
  return match;
}

template <typename Schema, typename Fn>
void parseTyped(ZmRef<ZiIOBuf> frame, FrameInfo info, Fn &fn)
{
  Parsed<Schema> value;
  value.frame = ZuMv(frame);
  value.info = info;
  Schema parser;
  parser.init(&value.object);
  auto result = parser.load(value.frame, info);
  if (!result) {
    fn(TypedFault{result.codec, TypedError::Body, 0});
    return;
  }
  fn(ZuMv(value));
}

template <typename Req, typename Fn>
void dispatchReply(ZmRef<ZiIOBuf> frame, FrameInfo info,
  int localError, Fn &fn)
{
  if (localError || !frame) {
    fn(TypedFault{{}, TypedError::Local, localError});
    return;
  }
  switch (info.type) {
    case MessageType::MethodReturn: {
      using Responses = GetResponses<Req>;
      ZuAssert((ZuTypeUnique<Responses>::N == Responses::N));
      using Signatures = ZuTypeMap<BodySignatureTag, Responses>;
      ZuAssert((ZuTypeUnique<Signatures>::N == Responses::N));
      int match = signatureMatch<Responses>(info.headers.signature);
      if (match < 0) {
        fn(TypedFault{{}, TypedError::Signature, 0});
        return;
      }
      if constexpr (Responses::N)
        ZuSwitch::dispatch<Responses::N>(unsigned(match),
          [&frame, &info, &fn](auto I) {
            using Schema = ZuType<I, Responses>;
            parseTyped<Schema>(ZuMv(frame), info, fn);
          });
      return;
    }
    case MessageType::Error: {
      using Errors = GetErrors<Req>;
      using Names = ZuTypeMap<ErrorName, Errors>;
      ZuAssert((ZuTypeUnique<Names>::N == Errors::N));
      int match = errorMatch<Errors>(info.headers.errorName);
      if (match == -1) {
        fn(RemoteError{ZuMv(frame), info});
        return;
      }
      if constexpr (Errors::N)
        ZuSwitch::dispatch<Errors::N>(unsigned(match),
          [&frame, &info, &fn](auto I) {
            using Schema = ZuType<I, Errors>;
            if (bodySignature<Schema>() != info.headers.signature) {
              fn(TypedFault{{}, TypedError::Signature, 0});
              return;
            }
            parseTyped<Schema>(ZuMv(frame), info, fn);
          });
      return;
    }
    default:
      fn(TypedFault{{}, TypedError::Type, 0});
  }
}

} // Zdbus_

template <typename Schema>
using ZdbusParsed = Zdbus_::Parsed<Schema>;
using ZdbusRemoteError = Zdbus_::RemoteError;
using ZdbusTypedFault = Zdbus_::TypedFault;

#endif /* ZdbusCatalog_HH */
