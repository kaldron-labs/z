//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z REST library - shared client/server core

#ifndef Zrest_HH
#define Zrest_HH

#include <zlib/ZrestLib.hh>

#include <zlib/ZuAssert.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuIntrin.hh>
#include <zlib/ZuMatcher.hh>
#include <zlib/ZuSwitch.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuUnroll.hh>

#include <zlib/ZmLHash.hh>

#include <zlib/ZtEnum.hh>
#include <zlib/ZtScratch.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZfStruct.hh>
#include <zlib/ZfURI.hh>
#include <zlib/ZfJSON.hh>

#include <zlib/Zhttp.hh>
#include <zlib/ZhttpClient.hh>
#include <zlib/ZhttpServer.hh>

namespace Zrest {

ZtEnumNS(ZrestAPI, QueryPolicy, int8_t, None, URI, Raw);

ZtEnumNS(ZrestAPI, BodyPolicy, int8_t, None, Zero, JSON, URI, Raw, SSE);

namespace Default {
  enum {
    MaxPending = 1U << 12,
    MaxQueue = 1U << 10,
    MaxQueueBytes = 64U << 20,
    WorkBatch = 64,
    MaxSSELineBytes = 64U << 10,
    MaxSSEEventBytes = 16U << 20
  };
}

struct Limits {
  unsigned maxPending = Default::MaxPending;
  unsigned maxQueue = Default::MaxQueue;
  unsigned maxQueueBytes = Default::MaxQueueBytes;
  unsigned workBatch = Default::WorkBatch;
  unsigned maxSSELineBytes = Default::MaxSSELineBytes;
  unsigned maxSSEEventBytes = Default::MaxSSEEventBytes;
};

inline bool valid(const Limits &limits)
{
  return limits.maxPending && limits.maxQueue && limits.maxQueueBytes &&
    limits.workBatch && limits.maxSSELineBytes && limits.maxSSEEventBytes;
}

struct Failure {
  struct Code { enum { Stopped, Limit, Transport, Timeout, Decode, Status }; };
  int code = Code::Transport;
  Zhttp::Result http;
};

ZuDerive(ContextString,
  (ZtString<ZtStringHeapID<"Zrest.Context">>));

struct RequestContext {
  ContextString authority;
  ContextString target;
  ContextString root;
  Zhttp::Scheme::T scheme = Zhttp::Scheme::http;
  bool decoded = false;
  bool ok() const { return decoded; }
};

class ClientConfig : public Zhttp::Config {
public:
  const Limits &limits() const { return m_limits; }
  Zhttp::Config &http() { return *this; }
  const Zhttp::Config &http() const { return *this; }
  template <typename L>
  ClientConfig &&http(L &&l) {
    ZuFwd<L>(l)(static_cast<Zhttp::Config &>(*this));
    return ZuMv(*this);
  }
  ClientConfig &limits(Limits value) { m_limits = value; return *this; }
private:
  Limits m_limits;
};

class ServerConfig : public Zhttp::ServerConfig {
public:
  const Limits &limits() const { return m_limits; }
  Zhttp::ServerConfig &http() { return *this; }
  const Zhttp::ServerConfig &http() const { return *this; }
  template <typename L>
  ServerConfig &&http(L &&l) {
    ZuFwd<L>(l)(static_cast<Zhttp::ServerConfig &>(*this));
    return ZuMv(*this);
  }
  ServerConfig &limits(Limits value) { m_limits = value; return *this; }
private:
  Limits m_limits;
};

template <typename Impl, unsigned = Impl::Body>
struct Headers_ {
  using T = ZuTypeList<>;
};
template <typename Impl>
struct Headers_<Impl, BodyPolicy::Zero> {
  using T = ZhttpHeaders("content-length");
};
template <typename Impl>
struct Headers_<Impl, BodyPolicy::Raw> {
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
struct Headers_<Impl, BodyPolicy::SSE> {
  using T = ZhttpHeaders(("content-type", "text/event-stream"));
};
template <typename Impl>
using Headers = typename Headers_<Impl>::T;

ZuDerive(SignBufScratch, (ZtArray<char, ZtArrayHeapID<"Zrest.SignBuf",
  ZtArraySharded<true>>>));
ZuDerive(HeaderScratch, (ZtBArray<ZtArrayHeapID<"Zrest.Header.Value",
  ZtArraySharded<true>>>));

struct DefltHdrs { };

// Advance path past n complete leading components, retaining the slash before
// the remaining path.  The zero case is an inlined no-op for literal dispatch.
ZrestExtern bool skip(ZuSpan<uint8_t> &path, unsigned n);

// Split an origin-form path at its first component.  root excludes the leading
// slash; suffix begins with '/', '?' or is empty, and aliases path.
ZrestExtern bool splitRoot(
  ZuSpan<uint8_t> path, ZuBSpan &root, ZuSpan<uint8_t> &suffix);

struct Request {
  using Headers = DefltHdrs;
  using Object = void;
  enum { Borrowed = 0 };

  enum { Method = Zhttp::Method::GET };
  using Path = ZuStringT<"/">;
  enum { Exact = 0 };

  enum { Query = QueryPolicy::None };
  enum { SignQuery = 0 };
  static constexpr uint64_t QueryLimit = 0;
  enum { Body = BodyPolicy::None };
  // Signed bodies are bracketed by prefixBody() and signBody(); the span
  // passed to signBody() excludes the prefix.
  enum { SignBody = 0 };
  static constexpr uint64_t BodyLimit = 0;

  static constexpr unsigned SignQueryBufSize = 1<<10;
  static constexpr unsigned SignBodyBufSize = 1<<10;

  using Query_URI_Facet = ZuFacet::URI;
  using Body_URI_Facet = ZuFacet::URI;
  using Body_JSON_Facet = ZuFacet::JSON;
};

struct Response {
  using Headers = DefltHdrs;
  using Object = void;
  enum { Borrowed = 0 };

  enum { Status = 200 };

  enum { Body = BodyPolicy::None };
  // Signed bodies are bracketed by prefixBody() and signBody(); the span
  // passed to signBody() excludes the prefix.
  enum { SignBody = 0 };

  static constexpr unsigned SignBodyBufSize = 1<<10;

  using Body_JSON_Facet = ZuFacet::JSON;
  using Body_URI_Facet = ZuFacet::URI;
};

// A typed response value.  On reception the codec owns the selected parsed
// response and any assembled entity storage; on transmission it owns the
// response object supplied by the application.
template <typename Res>
struct Reply {
  using Response = Res;
  using Object = typename Res::Object;

  Res codec;

  Reply() = default;
  Reply(Res &&codec_) : codec{ZuMv(codec_)} { }

  template <typename O = Object, ZuIfT<!ZuIsSame<O, void>{}, int> = 0>
  Reply(ZmRef<O> object) { codec.init(object.ptr()); }

  const Res &response() const { return codec; }
  template <typename O = Object, ZuIfT<!ZuIsSame<O, void>{}, int> = 0>
  const O &bodyObject() const { return *codec.object; }
};

// Valid for the receiveSSE callback; a retained header value must be copied
// unless the response descriptor owns it.
template <typename Res>
struct SSEMeta {
  unsigned status = 0;
  const Res &response;
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

template <typename List>
struct MergeHdrs_ {
  using Hdrs = GetAllHdrs<List>;
  using T = Zhttp::MergeHeaders<Hdrs>;
};
template <typename List>
using MergeHdrs = typename MergeHdrs_<List>::T;

template <typename Req, typename = void>
struct GetResponses_ { using T = ZuTypeList<>; };
template <typename Req>
struct GetResponses_<Req, decltype(
  ZuDeclVal<typename Req::Responses *>(), void())> {
  using T = typename Req::Responses;
};
template <typename Req> using GetResponses = typename GetResponses_<Req>::T;
template <typename Reqs>
using GetAllResponses = ZuTypeUnique<ZuTypeApply<
  ZuTypeConcat, ZuTypeMap<GetResponses, Reqs>>>;
template <typename Reqs, typename Res>
using GetResIndex = ZuTypeIndex<Res, GetAllResponses<Reqs>>;
template <typename Res>
using SSEResponse = ZuBool<Res::Body == BodyPolicy::SSE>;

template <typename List>
using GetUnion = ZuTypeApply<ZuUnion, typename List::template Unshift<void>>;

template <unsigned Method>
struct MethodFilter {
  template <typename Req>
  using Filter = ZuBool<Req::Method == Method>;
};

template <typename Req>
using ReqPath = typename Req::Path;

template <typename Req>
using ExactReq = ZuBool<Req::Exact>;
template <typename Req>
using PrefixReq = ZuBool<!Req::Exact>;

template <typename Reqs, unsigned Method>
struct ReqCatalogMethodValid {
  using MethodReqs = ZuTypeGrep<MethodFilter<Method>::template Filter, Reqs>;
  using ExactReqs = ZuTypeGrep<ExactReq, MethodReqs>;
  using PrefixReqs = ZuTypeGrep<PrefixReq, MethodReqs>;
  using ExactPaths = ZuTypeMap<ReqPath, ExactReqs>;
  using PrefixPaths = ZuTypeMap<ReqPath, PrefixReqs>;
  static constexpr bool value =
    ZuTypeUnique<ExactPaths>::N == ExactReqs::N &&
    ZuTypeUnique<PrefixPaths>::N == PrefixReqs::N;
};

template <typename Reqs, unsigned Method = 0>
struct ReqCatalogValid_ {
  static constexpr bool value =
    ReqCatalogMethodValid<Reqs, Method>::value &&
    ReqCatalogValid_<Reqs, Method + 1>::value;
};
template <typename Reqs>
struct ReqCatalogValid_<Reqs, Zhttp::Method::N> {
  static constexpr bool value = true;
};
template <typename Reqs>
using ReqCatalogValid = ZuBool<ReqCatalogValid_<Reqs>::value>;

template <typename Reqs, unsigned Method>
struct PrefixPathIDs {
  using MethodReqs = ZuTypeGrep<MethodFilter<Method>::template Filter, Reqs>;
  using PrefixReqs = ZuTypeGrep<PrefixReq, MethodReqs>;
  using Keys = ZuTypeMap<ReqPath, PrefixReqs>;
};

template <typename Reqs>
int reqMatch(Zhttp::Method::T method, ZuBSpan path)
{
  ZuAssert((ReqCatalogValid<Reqs>{}));
  return ZuSwitch::dispatch<Zhttp::Method::N>(method,
      [&path](auto I) -> int {
    static constexpr unsigned MethodI = I;
    using MethodReqs =
      ZuTypeGrep<MethodFilter<MethodI>::template Filter, Reqs>;
    if constexpr (!MethodReqs::N) return -1;
    else {
      if (auto query = path.find<"?">(); query >= 0)
	path.trunc(unsigned(query));
      using ExactReqs = ZuTypeGrep<ExactReq, MethodReqs>;
      if constexpr (ExactReqs::N) {
	struct IDs { using Keys = ZuTypeMap<ReqPath, ExactReqs>; };
	static constexpr auto matcher = ZuMatcher<IDs>();
	int request = matcher.exact(path);
	if (request >= 0)
	  return ZuSwitch::dispatch<ExactReqs::N>(unsigned(request), [](auto J) {
	    using Req = ZuType<J, ExactReqs>;
	    return int(ZuTypeIndex<Req, MethodReqs>{});
	  }, -1);
      }
      using IDs = PrefixPathIDs<Reqs, MethodI>;
      using PrefixReqs = typename IDs::PrefixReqs;
      if constexpr (!PrefixReqs::N) return -1;
      else {
	static constexpr auto matcher = ZuMatcher<IDs>();
	int prefix = matcher.match(path);
	if (prefix < 0) return -1;
	return ZuSwitch::dispatch<PrefixReqs::N>(prefix, [](auto J) {
	  using Req = ZuType<J, PrefixReqs>;
	  return int(ZuTypeIndex<Req, MethodReqs>{});
	}, -1);
      }
    }
  }, -1);
}

template <typename Res> using GetStatus = ZuUnsigned<Res::Status>;
template <typename Req>
using GetStatuses = ZuTypeMap<GetStatus, GetResponses<Req>>;

template <typename Req> struct GetResLookup_ {
  using Statuses = GetStatuses<Req>;
  using Responses = GetResponses<Req>;
  enum { N = Responses::N };
  enum { Bits = ZuIntrin::log2(N) };
  using Hash_ =
    ZmLHashKV<unsigned, unsigned, ZmLHashStatic<Bits, ZmLHashLocal<>>>;
  struct Hash : public Hash_ {
    using Hash_::add;
    Hash() {
      for (unsigned i = 0; i < N; i++)
	ZuSwitch::dispatch<N>(i, [this](auto I) {
	  this->add(ZuType<I, Statuses>{}(), I);
	});
    }
  };
  using T = Hash;
};
template <typename Req>
using GetResLookup = typename GetResLookup_<Req>::T;

template <typename Reqs>
int resMatch(unsigned request, unsigned status)
{
  return ZuSwitch::dispatch<Reqs::N>(request,
      [status](auto I) -> int {
    static constexpr unsigned ReqI = I;
    using Req = ZuType<ReqI, Reqs>;
    using Responses = GetResponses<Req>;
    if constexpr (!Responses::N) return -1;
    else {
      static const GetResLookup<Req> lookup;
      unsigned response = lookup.findVal(status);
      if (ZuCmp<unsigned>::null(response)) return -1;
      return ZuSwitch::dispatch<Responses::N>(response,
	[](auto J) -> int {
	  static constexpr unsigned ResI = J;
	  using Res = ZuType<ResI, Responses>;
	  return GetResIndex<Reqs, Res>{};
	}, -1);
    }
  }, -1);
}

template <typename Path_, typename Parser_>
struct ReqRoot {
  using Path = Path_;
  using Parser = Parser_;
};

template <typename Root>
using GetRootParser = typename Root::Parser;
template <typename Roots>
using GetRootParsers = ZuTypeMap<GetRootParser, Roots>;
template <typename Parser>
using GetParserHdrs = typename Parser::Headers;
template <typename Parsers>
struct MergeParserHdrs_ {
  using Hdrs = ZuTypeApply<ZuTypeConcat,
    ZuTypeMap<GetParserHdrs, Parsers>>;
  using T = Zhttp::MergeHeaders<Hdrs>;
};
template <typename Parsers>
using MergeParserHdrs = typename MergeParserHdrs_<Parsers>::T;

template <typename Root> using RootPath = typename Root::Path;

template <typename Roots>
int rootMatch(ZuBSpan root)
{
  if constexpr (!Roots::N) return -1;
  else {
    struct IDs { using Keys = ZuTypeMap<RootPath, Roots>; };
    static constexpr auto matcher = ZuMatcher<IDs>();
    return matcher.exact(root);
  }
}

} // Zrest

#define ZrestCatalogDerive(Name, Reqs) \
  ZhttpHdrCatalogDerive(Name##ReqHeaders, Zrest::MergeHdrs<Reqs>); \
  ZhttpHdrCatalogDerive(Name##ResHeaders, \
    Zrest::MergeHdrs<Zrest::GetAllResponses<Reqs>>); \
  struct Name { \
    using List = Reqs; \
    using ReqHeaders = Name##ReqHeaders; \
    using ResHeaders = Name##ResHeaders; \
    ZuAssert((Zrest::ReqCatalogValid<Reqs>{})); \
    static int reqMatch(Zhttp::Method::T, ZuBSpan); \
    static int resMatch(unsigned, unsigned); \
  }
#define ZrestCatalogImpl(Name) \
  ZhttpHdrCatalogImpl(Name##ReqHeaders) \
  ZhttpHdrCatalogImpl(Name##ResHeaders) \
  int Name::reqMatch(Zhttp::Method::T method, ZuBSpan path) { \
    return Zrest::reqMatch<List>(method, path); \
  } \
  int Name::resMatch(unsigned request, unsigned status) { \
    return Zrest::resMatch<List>(request, status); \
  }

#define ZrestRootCatalogDerive(Name, Roots) \
  ZhttpHdrCatalogDerive(Name##ReqHeaders, \
    Zrest::MergeParserHdrs<Zrest::GetRootParsers<Roots>>); \
  struct Name { \
    using List = Roots; \
    using ReqHeaders = Name##ReqHeaders; \
    static int rootMatch(ZuBSpan); \
  }
#define ZrestRootCatalogImpl(Name) \
  ZhttpHdrCatalogImpl(Name##ReqHeaders) \
  int Name::rootMatch(ZuBSpan root) { \
    return Zrest::rootMatch<List>(root); \
  }

#endif /* Zrest_HH */
