//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z REST library - shared client/server core

#ifndef Zrest_HH
#define Zrest_HH

#include <zlib/ZrestLib.hh>

#include <zlib/ZtEnum.hh>
#include <zlib/ZtScratch.hh>

#include <zlib/ZfStruct.hh>
#include <zlib/ZfURI.hh>
#include <zlib/ZfJSON.hh>

#include <zlib/Zhttp.hh>

namespace Zrest {

ZtEnumNS(ZrestAPI, QueryPolicy, int8_t, None, URI, Raw);

ZtEnumNS(ZrestAPI, BodyPolicy, int8_t, None, Zero, JSON, URI, Raw);

template <typename Impl, unsigned = Impl::Body>
struct Headers_ {
  using T = ZuTypeList<>;
};
template <typename Impl>
struct Headers_<Impl, BodyPolicy::Zero> {
  using T = ZhttpHeaders("content-length");
};
template <typename Impl>
struct Headers_<Impl, BodyPolicy::JSON> {
  using T = ZhttpHeaders(
    ("content-type", "application/json"), "content-length");
};
template <typename Impl>
struct Headers_<Impl, BodyPolicy::URI> {
  using T = ZhttpHeaders(
    ("content-type", "application/x-www-form-urlencoded"),
    "content-length");
};
template <typename Impl>
using Headers = typename Headers_<Impl>::T;

ZuDerive(SignBuf, (ZtArray<char, ZtArrayHeapID<"Zrest.SignBuf">>));

struct DefltHdrs { };

struct Request {
  using Headers = DefltHdrs;

  enum { Method = Zhttp::Method::GET };
  using Path = ZuStringT<"/">;

  enum { Query = QueryPolicy::None };
  enum { SignQuery = 0 };
  enum { Body = BodyPolicy::None };
  enum { SignBody = 0 };

  static constexpr unsigned SignQueryBufSize = 1<<10;
  static constexpr unsigned SignBodyBufSize = 1<<10;

  using Query_URI_Facet = ZuFacet::URI;
  using Body_URI_Facet = ZuFacet::IncrementalURI;
  using Body_JSON_Facet = ZuFacet::JSON;
};

struct Response {
  using Headers = DefltHdrs;

  enum { Status = 200 };

  enum { Body = BodyPolicy::None };
  enum { SignBody = 0 };

  static constexpr unsigned SignBodyBufSize = 1<<10;

  using Body_JSON_Facet = ZuFacet::JSON;
  using Body_URI_Facet = ZuFacet::IncrementalURI;
};

template <typename Req>
struct HdrBufSizeAxor_ { using T = ZuUnsigned<Req::HdrBufSize>; };
template <unsigned I>
struct HdrBufSizeAxor_<ZuUnsigned<I>> { using T = ZuUnsigned<I>; };
template <typename U> using HdrBufSizeAxor = typename HdrBufSizeAxor_<U>::T;
template <typename ...Reqs> struct MaxHdrBufSize__;
template <>
struct MaxHdrBufSize__<> { using T = ZuUnsigned<0>; };
template <typename U>
struct MaxHdrBufSize__<U> { using T = HdrBufSizeAxor<U>; };
template <typename L, typename R>
struct MaxHdrBufSize__<L, R> {
  static constexpr unsigned L_ = HdrBufSizeAxor<L>{};
  static constexpr unsigned R_ = HdrBufSizeAxor<R>{};
  static constexpr unsigned M = L_ > R_ ? L_ : R_;
  using T = ZuUnsigned<M>;
};
template <typename ...Reqs>
using MaxHdrBufSize_ = typename MaxHdrBufSize__<Reqs...>::T;
template <typename ...Reqs>
using MaxHdrBufSize = ZuTypeReduce<MaxHdrBufSize_, Reqs...>;

template <typename U, typename = void>
struct GetHdrs_ { using T = Zrest::Headers<U>; };
template <typename U>
struct GetHdrs_<U, decltype(sizeof(typename U::Headers), void())> {
  using Declared = typename U::Headers;
  using T = ZuIf<ZuIsSame<Declared, DefltHdrs>{},
    Zrest::Headers<U>, Declared>;
};
template <typename U> using GetHdrs = typename GetHdrs_<U>::T;
template <typename Hdrs> using GetHdrKeys = ZuTypeSlice<2, 0, Hdrs>;
template <typename Hdrs> using GetHdrValues = ZuTypeSlice<2, 1, Hdrs>;
template <typename U> using GetTypeHdrKeys = GetHdrKeys<GetHdrs<U>>;

template <typename List>
using GetAllHdrKeys = ZuTypeApply<
  ZuTypeConcat, ZuTypeMap<GetTypeHdrKeys, List>>;
template <typename List>
using GetUniqueHdrKeys = ZuTypeUnique<GetAllHdrKeys<List>>;
template <typename List>
using GetAllHdrs = ZuTypeApply<ZuTypeConcat, ZuTypeMap<GetHdrs, List>>;

template <typename K, typename ...Ts> struct GetKValues_;
template <typename K>
struct GetKValues_<K> { using T = ZuTypeList<>; };
template <typename K, typename K0, typename V0, typename ...Ts>
struct GetKValues_<K, K0, V0, Ts...> {
  using Tail = typename GetKValues_<K, Ts...>::T;
  using T = ZuTypeUnique<ZuIf<ZuIsSame<K, K0>{},
    typename V0::template Push<Tail>, Tail>>;
};
template <typename K, typename ...Ts>
struct GetKValues_<K, ZuTypeList<Ts...>> : public GetKValues_<K, Ts...> { };
template <typename K, typename List>
using GetKValues = typename GetKValues_<K, List>::T;

template <typename Hdrs, typename ...Keys> struct MergeHdrs__;
template <typename Hdrs>
struct MergeHdrs__<Hdrs> { using T = ZuTypeList<>; };
template <typename Hdrs, typename K0, typename ...Keys>
struct MergeHdrs__<Hdrs, K0, Keys...> {
  using T = typename ZuTypeList<K0, GetKValues<K0, Hdrs>>::template Push<
    typename MergeHdrs__<Hdrs, Keys...>::T>;
};
template <typename Hdrs, typename ...Keys>
struct MergeHdrs__<Hdrs, ZuTypeList<Keys...>> :
  public MergeHdrs__<Hdrs, Keys...> { };
template <typename List>
struct MergeHdrs_ {
  using Hdrs = GetAllHdrs<List>;
  using T = typename MergeHdrs__<Hdrs, GetUniqueHdrKeys<List>>::T;
};
template <typename List>
using MergeHdrs = typename MergeHdrs_<List>::T;

template <typename Req> using GetResponses = typename Req::Responses;
template <typename Reqs>
using GetAllResponses = ZuTypeApply<
  ZuTypeConcat, ZuTypeMap<GetResponses, Reqs>>;

template <typename Req, typename Res> struct ReqRes { };
template <typename Req> struct GetReqRes_ {
  template <typename Res> using ReqRes_ = ReqRes<Req, Res>;
  using T = ZuTypeMap<ReqRes_, GetResponses<Req>>;
};
template <typename Req> using GetReqRes = typename GetReqRes_<Req>::T;
template <typename Reqs>
using GetAllReqRes = ZuTypeApply<ZuTypeConcat, ZuTypeMap<GetReqRes, Reqs>>;
template <typename Reqs, typename Req, typename Res>
using GetResIndex = ZuTypeIndex<ReqRes<Req, Res>, GetAllReqRes<Reqs>>;

template <typename List>
using GetUnion = ZuTypeApply<ZuUnion, typename List::template Unshift<void>>;

} // Zrest

#endif /* Zrest_HH */
