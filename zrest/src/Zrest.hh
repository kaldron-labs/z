//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z REST library

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

ZtEnumNS(ZrestAPI, BodyPolicy, int8_t, None, JSON, URI, Raw);

// default headers based on body policy
template <typename Impl, unsigned = Impl::BodyPolicy>
struct Headers_ {
  using T = ZuTypeList<>;
};
template <typename Impl>
struct Headers_<Impl, BodyPolicy::JSON> {
  using T = ZhttpHeaders(("content-type", "application/json"));
};
template <typename Impl>
struct Headers_<Impl, BodyPolicy::URI> {
  using T = ZhttpHeaders(("content-type", "application/x-www-form-urlencoded"));
};
template <typename Impl>
using Headers = typename Headers_<Impl>::T;

// signing buffer
ZuDerive(SignBuf, (ZtArray<char, ZtArrayHeapID<"Zrest.SignBuf">>));

// default request compile-time metadata
struct Request {
  enum { Method = Zhttp::Method::GET };
  using Path = ZuStringT<"/">;

  enum { Query = QueryPolicy::None };
  enum { SignQuery = 0 };
  enum { Body = BodyPolicy::None };
  enum { SignBody = 0 };

  static constexpr unsigned SignQueryBufSize = 1<<10; // 1k
  static constexpr unsigned SignBodyBufSize = 1<<10; // 1k

  using Query_URI_Facet = ZuFacet::URI;
  using Body_URI_Facet = ZuFacet::IncrementalURI;
  using Body_JSON_Facet = ZuFacet::JSON;
};

// default response compile-time metadata
struct Response {
  enum { Status = 200 };

  enum { Body = BodyPolicy::None };

  using Body_JSON_Facet = ZuFacet::JSON;
};

// request parser
// CRTP - implementation must conform to the following interface:
#if 0
struct Impl : public ReqParser<Impl, Object> {
  using Responses = ZuTypeList<...>;

  const auto &queryObject(Object *object); // optional

  const auto &bodyObject(Object *object); // optional
};
#endif
template <typename Impl, typename Object>
struct ReqParser : public Request, public Zhttp::Parser {
  auto impl() { return static_cast<Impl *>(this); }
  auto impl() const { return static_cast<const Impl *>(this); }

  using Headers = Zrest::Headers<Impl>;

  ZmRef<Object> 	object;
  unsigned		bodyLength = 0;

  void init() { object = new Object(); }

  const auto &queryObject(Object *object) { return *object; }

  const auto &bodyObject(Object *object) { return *object; }

  bool operation(Zhttp::Method::T, Zhttp::Target &target) {
    impl()->init();
    if constexpr (Impl::Query == QueryPolicy::URI) {
      using Path = Impl::Path;
      using Query_URI_Facet = Impl::Query_URI_Facet;
      auto span = target.path;
      span.offset(Path{}().length());
      auto scan = ZfURI::scan(span);
      auto handler = ZfURI::handler<Object, Query_URI_Facet>(scan.p<1>());
      handler.load(impl()->queryObject());
    } else if constexpr (Impl::Query == QueryPolicy::Raw) {
      using Path = Impl::Path;
      auto span = target.path;
      span.offset(Path{}().length());
      impl()->queryObject() = span;
    }
    return true;
  }

  bool bodyInfo(Zhttp::BodyType::T type, uint64_t length) {
    bodyLength = length;
    return type != Zhttp::BodyType::Streamed;
  }

  template <typename Rx> bool body(Rx &rx) {
    if constexpr (Impl::Body == BodyPolicy::None)
      return false;
    else {
      rx.consume(
	[this](ZuSpan<uint8_t> span) {
	  return span.length() < bodyLength ? 0U : bodyLength;
	},
	[this](ZuSpan<uint8_t> span) {
	  if constexpr (Impl::Body == BodyPolicy::JSON) {
	    using Body_JSON_Facet = Impl::Body_JSON_Facet;
	    auto scan = ZfJSON::scan(span);
	    auto handler = ZfJSON::handler<Object, Body_JSON_Facet>((*scan.p<1>())[0]);
	    handler.load(impl()->bodyObject());
	  } else if constexpr (Impl::Body == BodyPolicy::URI) {
	    using Body_URI_Facet = Impl::Body_URI_Facet;
	    auto scan = ZfURI::scan(span);
	    auto handler = ZfURI::handler<Object, Body_URI_Facet>(scan.p<1>());
	    handler.load(impl()->bodyObject());
	  } else if constexpr (Impl::Body == BodyPolicy::Raw) {
	    impl()->bodyObject() = span;
	  }
	});
      return true;
    }
  }

  void reset() {
    object = nullptr;
    bodyLength = 0;
  }

  // Impl should implement complete(link, ok)
};

// request builder
// CRTP - implementation must conform to the following interface:
#if 0
struct Impl : public ReqBuilder<Impl, Object> {
  using Responses = ZuTypeList<...>;

  const auto &queryObject(const Object *); // optional
  template <typename S>
  void signQuery(S &, const Object *, ZuCSpan); // optional

  const auto &bodyObject(const Object *); // optional
  template <typename S>
  void signBody(S &, const Object *, ZuCSpan); // optional
};
#endif
template <typename Impl, typename Object>
struct ReqBuilder : public Request, public Zhttp::Builder {
  auto impl() { return static_cast<Impl *>(this); }
  auto impl() const { return static_cast<const Impl *>(this); }

  using Headers = Zrest::Headers<Impl>;

  ZmRef<const Object>	object;

  template <typename ObjectRef>
  void init(ObjectRef &&object_) { object = ZuFwd<ObjectRef>(object_); }

  const auto &queryObject(const Object *object) { return *object; }
  template <typename S>
  void signQuery(S &, const Object *, ZuCSpan) { }

  const auto &bodyObject(const Object *object) { return *object; }
  template <typename S>
  void signBody(S &, const Object *, ZuCSpan) { }

  template <typename Emit> void operation(Emit &&emit) {
    if constexpr (Impl::Query == QueryPolicy::None) {
      using Path = Impl::Path;
      emit(Impl::Method, [](auto &s) { s << Path{}(); });
    } else if constexpr (Impl::Query == QueryPolicy::URI && !Impl::SignQuery) {
      emit(Impl::Method, [this](auto &s) {
	using Path = Impl::Path;
	using Query_URI_Facet = Impl::Query_URI_Facet;
	s << Path{}();
	ZfURI::save<Query_URI_Facet>(s, impl()->queryObject());
      });
    } else if constexpr (Impl::Query == QueryPolicy::URI /* && Impl::SignQuery */) {
      using Path = Impl::Path;
      using Query_URI_Facet = Impl::Query_URI_Facet;
      auto buf = ZtScratch(SignBuf, Impl::SignQueryBufSize);
      const auto &query = impl()->queryObject();
      buf << Path{}();
      ZfURI::save<Query_URI_Facet>(buf, query);
      impl()->signQuery(buf, object, buf.cspan());
      emit(Impl::Method, [&buf](auto &s) { s << buf; });
    } else if constexpr (Impl::Query == QueryPolicy::Raw) {
      emit(Impl::Method, [this](auto &s) {
	using Path = Impl::Path;
	s << Path{}() << impl()->queryObject();
      });
    }
  }

  template <typename Emit> void body(Emit &&emit) {
    if constexpr (Impl::Body == BodyPolicy::None) {
      return;
    } else if constexpr (Impl::Body == BodyPolicy::JSON && !Impl::SignBody) {
      emit([this](auto &s) {
	using Body_JSON_Facet = Impl::Body_JSON_Facet;
	ZfJSON::save<Body_JSON_Facet>(s, impl()->bodyObject());
      });
    } else if constexpr (Impl::Body == BodyPolicy::JSON /* && Impl::SignBody */) {
      using Body_JSON_Facet = Impl::Body_JSON_Facet;
      auto buf = ZtScratch(SignBuf, Impl::SignBodyBufSize);
      const auto &body = impl()->bodyObject();
      ZfJSON::save<Body_JSON_Facet>(buf, body);
      impl()->signBody(buf, object, buf.cspan());
      emit([&buf](auto &s) { s << buf; });
    } else if constexpr (Impl::Body == BodyPolicy::URI && !Impl::SignBody) {
      emit([this](auto &s) {
	using Body_URI_Facet = Impl::Body_URI_Facet;
	ZfURI::saveBody<Body_URI_Facet>(s, impl()->bodyObject());
      });
    } else if constexpr (Impl::Body == BodyPolicy::URI /* && Impl::SignBody */) {
      using Body_URI_Facet = Impl::Body_URI_Facet;
      auto buf = ZtScratch(SignBuf, Impl::SignBodyBufSize);
      const auto &body = impl()->bodyObject();
      ZfURI::saveBody<Body_URI_Facet>(buf, body);
      impl()->signBody(buf, object, buf.cspan());
      emit([&buf](auto &s) { s << buf; });
    } else if constexpr (Impl::Body == BodyPolicy::Raw) {
      emit([this](auto &s) { s << impl()->bodyObject(); });
    }
  }

  template <typename Link, typename Response>
  void process(Link *link, const Response &response) {
    object->process(link, response.object.ptr());
  }

  template <typename Link>
  void failed(Link *link) { object->failed(link); }
};

// response parser
// CRTP - implementation must conform to the following interface:
#if 0
struct Impl : public ReqParser<Impl, Object> {
  const auto &bodyObject(const Object *object); // optional
};
#endif
template <typename Impl, typename Object>
struct ResParser : public Response, public Zhttp::Parser {
  auto impl() { return static_cast<Impl *>(this); }
  auto impl() const { return static_cast<const Impl *>(this); }

  using Headers = Zrest::Headers<Impl>;

  ZmRef<Object>	object;
  unsigned	bodyLength = 0;

  void init() { object = new Object(); }

  const auto &bodyObject(const Object *object) { return *object; }

  bool bodyInfo(Zhttp::BodyType::T type, uint64_t length) {
    bodyLength = length;
    return type != Zhttp::BodyType::Streamed;
  }

  template <typename Rx> bool body(Rx &rx) {
    if constexpr (Impl::Body == BodyPolicy::None)
      return false;
    else {
      rx.consume(
	[this](ZuSpan<uint8_t> span) {
	  return span.length() < bodyLength ? 0U : bodyLength;
	},
	[this](ZuSpan<uint8_t> span) {
	  if constexpr (Impl::Body == BodyPolicy::JSON) {
	    using Body_JSON_Facet = Impl::Body_JSON_Facet;
	    auto scan = ZfJSON::scan(span);
	    auto handler = ZfJSON::handler<Object, Body_JSON_Facet>((*scan.p<1>())[0]);
	    handler.load(impl()->bodyObject());
	  } else if constexpr (Impl::Body == BodyPolicy::URI) {
	    using Body_URI_Facet = Impl::Body_URI_Facet;
	    auto scan = ZfURI::scan(span);
	    auto handler = ZfURI::handler<Object, Body_URI_Facet>(scan.p<1>());
	    handler.load(impl()->bodyObject());
	  } else if constexpr (Impl::Body == BodyPolicy::Raw) {
	    impl()->bodyObject() = span;
	  }
	});
      return true;
    }
  }

  void reset() {
    object = nullptr;
    bodyLength = 0;
  }

  // Impl should implement complete(link, ok)
};

// response builder
// CRTP - implementation must conform to the following interface:
#if 0
struct Impl : public ReqParser<Impl, Object> {
  const auto &bodyObject(const Object *); // optional
  template <typename S>
  void signBody(S &, const Object *, ZuCSpan); // optional
};
#endif
template <typename Impl, typename Object>
struct ResBuilder : public Response, public Zhttp::Builder {
  auto impl() { return static_cast<Impl *>(this); }
  auto impl() const { return static_cast<const Impl *>(this); }

  using Headers = Zrest::Headers<Impl>;

  ZmRef<const Object>	object;

  template <typename ObjectRef>
  void init(ObjectRef &&object_) { object = ZuFwd<ObjectRef>(object_); }

  const auto &bodyObject(const Object *object) { return *object; }
  template <typename S>
  void signBody(S &, const Object *, ZuCSpan) { }

  unsigned status() const { return Response::Status; }

  template <typename Emit> void body(Emit &&emit) {
    if constexpr (Impl::Body == BodyPolicy::None) {
      return;
    } else if constexpr (Impl::Body == BodyPolicy::JSON && !Impl::SignBody) {
      emit([this](auto &s) {
	using Body_JSON_Facet = Impl::Body_JSON_Facet;
	ZfJSON::save<Body_JSON_Facet>(s, impl()->bodyObject());
      });
    } else if constexpr (Impl::Body == BodyPolicy::JSON /* && Impl::SignBody */) {
      using Body_JSON_Facet = Impl::Body_JSON_Facet;
      auto buf = ZtScratch(SignBuf, Impl::SignBodyBufSize);
      const auto &body = impl()->bodyObject();
      ZfJSON::save<Body_JSON_Facet>(buf, body);
      impl()->signBody(buf, object, buf.cspan());
      emit([&buf](auto &s) { s << buf; });
    } else if constexpr (Impl::Body == BodyPolicy::URI && !Impl::SignBody) {
      emit([this](auto &s) {
	using Body_URI_Facet = Impl::Body_URI_Facet;
	ZfURI::save<Body_URI_Facet>(s, impl()->bodyObject());
      });
    } else if constexpr (Impl::Body == BodyPolicy::URI /* && Impl::SignBody */) {
      using Body_URI_Facet = Impl::Body_URI_Facet;
      auto buf = ZtScratch(SignBuf, Impl::SignBodyBufSize);
      const auto &body = impl()->bodyObject();
      ZfURI::save<Body_URI_Facet>(buf, body);
      impl()->signBody(buf, object, buf.cspan());
      emit([&buf](auto &s) { s << buf; });
    } else if constexpr (Impl::Body == BodyPolicy::Raw) {
      emit([this](auto &s) { s << impl()->bodyObject(); });
    }
  }
};

// compile-time calculate the maximum HdrBufSize for Reqs
template <typename Req>
struct HdrBufSizeAxor_ { using T = ZuUnsigned<Req::HdrBufSize>; };
template <unsigned I>
struct HdrBufSizeAxor_<ZuUnsigned<I>> { using T = ZuUnsigned<I>; };
template <typename U> using HdrBufSizeAxor = typename HdrBufSizeAxor_<U>::T;
template <typename ...Reqs> struct MaxHdrBufSize__;
template <>
struct MaxHdrBufSize__<> {
  using T = ZuUnsigned<0>;
};
template <typename U>
struct MaxHdrBufSize__<U> {
  using T = HdrBufSizeAxor<U>;
};
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

// compile-time processing of REST API metadata
template <typename U> using GetHdrs = typename U::Headers;
template <typename U> using GetHdrKeys = ZuTypeSlice<2, 0, GetHdrs<U>>;
template <typename U> using GetHdrValues = ZuTypeSlice<2, 1, GetHdrs<U>>;

template <typename List>
using GetAllHdrKeys = ZuTypeApply<ZuTypeConcat, ZuTypeMap<GetHdrKeys, List>>;
template <typename List>
using GetUniqueHdrKeys = ZuTypeUnique<GetAllHdrKeys<List>>;

template <typename U>
struct GetHdrKVs_ {
  using Keys = GetHdrKeys<U>;
  using Values = GetHdrValues<U>;
  template <typename Key>
  using Pair = ZuTypeList<Key, ZuType<ZuTypeIndex<Key, Keys>{}, Values>>;
  using T = ZuTypeMap<Pair, Keys>;
};
template <typename U>
using GetHdrKVs = typename GetHdrKVs_<U>::T;
template <typename List>
using GetAllHdrKVs = ZuTypeApply<ZuTypeConcat, ZuTypeMap<GetHdrKVs, List>>;

template <typename KV> using GetKValues = ZuType<1, KV>;

template <typename K>
struct MatchHdrKV {
  template <typename KV, typename = ZuType<0, KV>>
  struct Match_ { using T = ZuFalse; };
  template <typename KV>
  struct Match_<KV, K> { using T = ZuTrue; };
  template <typename KV>
  using Match = typename Match_<KV>::T;
};
template <typename List>
struct MergeHdrs_ {
  template <typename K>
  using HdrKVs = ZuTypeGrep<MatchHdrKV<K>::template Match, GetAllHdrKVs<List>>;
  template <typename K>
  using Values = ZuTypeUnique<ZuTypeApply<ZuTypeConcat, ZuTypeMap<GetKValues, HdrKVs<K>>>>;
  template <typename K>
  using KV = ZuTypeList<K, Values<K>>;
  using T = ZuTypeMap<KV, GetUniqueHdrKeys<List>>;
};
template <typename List>
using MergeHdrs = typename MergeHdrs_<List>::T;

template <typename Req> using GetResponses = typename Req::Responses;
template <typename Res> using GetStatus = ZuUnsigned<Res::Status>;
template <typename Req> using GetStatuses = ZuTypeMap<GetStatus, GetResponses<Req>>;
template <typename Reqs>
using GetAllResponses = ZuTypeApply<ZuTypeConcat, ZuTypeMap<GetResponses, Reqs>>;

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
template <typename Req> struct GetResLookup_ {
  using Statuses = GetStatuses<Req>;
  using Responses = GetResponses<Req>;
  enum { N = Responses::N }; // == Statuses::N
  enum { Bits = ZuIntrin::log2(N) };
  using Hash_ = ZmLHashKV<unsigned, unsigned, ZmLHashStatic<Bits, ZmLHashLocal<>>>;
  struct Hash : public Hash_ {
    using Hash_::add;
    Hash() {
      for (unsigned i = 0; i < N; i++)
	ZuSwitch::dispatch<N>(i, [this](auto I) {
	  add(ZuType<I, Statuses>{}(), I);
	});
    }
  };
};
template <typename Req>
using GetResLookup = typename GetResLookup_<Req>::T;
template <typename Reqs>
using GetReqLookup = ZuTypeApply<ZuTuple, ZuTypeMap<GetResLookup, Reqs>>;

template <typename List>
using GetUnion = ZuTypeApply<ZuUnion, typename List::template Unshift<void>>;

// monomorphic (type-erased) request builder base
// - takes a typelist of Reqs, each a ReqBuilder<Impl, Object>
// - contains a union of all request types to be used with a specific Client
// - Builder callbacks are forwarded to the right instance
// - application will `struct ReqBuilder_ : public MReqBuilder<Reqs> { ... };`
//   and then define `ReqBuilder` as the queue node, in the usual way
// - ReqBuilder event callbacks (if any are implemented) are handled by
//   the application's centralized callbacks defined in `ReqBuilder_`,
//   in the usual way - they are not forwarded to Reqs
template <typename Reqs_>
struct MReqBuilder : public Zhttp::ReqBuilder {
  using Reqs = Reqs_;

  using Headers = MergeHdrs<Reqs>;

  static constexpr unsigned HdrBufSize = ZuTypeApply<MaxHdrBufSize, Reqs>{};

  using Union = GetUnion<Reqs>;

  Union		u;

  template <typename Builder, typename ObjectRef>
  void init(ObjectRef &&object) {
    auto builder = new (u.template new_<Builder>()) Builder();
    builder->init(ZuFwd<ObjectRef>(object));
  }

  BodyPolicy::T bodyPolicy() const {
    return u.cdispatch([](auto, const auto &request) {
      return request.bodyPolicy();
    });
  }

  template <typename Emit> void operation(Emit &&emit) const {
    u.cdispatch([&emit](auto, const auto &request) {
      request.operation(ZuFwd<Emit>(emit));
    });
  }

  template <typename Key, typename Value, typename L> void header(L &&l) const {
    u.cdispatch([&l](auto I, const auto &request) {
      using ReqHdrs = typename Union::template Type<I>::Headers;
      using ReqHdrKeys = GetHdrKeys<ReqHdrs>;
      using ReqHdrValues = GetHdrValues<ReqHdrs>;
      if constexpr (ZuTypeIn<Key, ReqHdrKeys>{}) {
	using Values = ZuType<ZuTypeIndex<Key, ReqHdrKeys>{}, ReqHdrValues>;
	if constexpr (ZuTypeIn<Value, Values>{})
	  request.template header<Key, Value>(ZuFwd<L>(l));
      }
    });
  }
  template <typename Key, typename L> void header(L &&l) const {
    u.cdispatch([&l](auto I, const auto &request) {
      using ReqHdrs = typename Union::template Type<I>::Headers;
      using ReqHdrKeys = GetHdrKeys<ReqHdrs>;
      if constexpr (ZuTypeIn<Key, ReqHdrKeys>{})
	request.template header<Key>(ZuFwd<L>(l));
    });
  }
  template <typename L> void header(L &&l) const {
    u.cdispatch([&l](auto, const auto &request) {
      request.header(ZuFwd<L>(l));
    });
  }

  template <typename Emit> void body(Emit &&emit) const {
    u.cdispatch([&emit](auto, const auto &request) {
      request.body(ZuFwd<Emit>(emit));
    });
  }

  template <typename L> void bodyHdrs(L &&l) const {
    u.cdispatch([&l](auto, const auto &request) {
      request.bodyHdrs(ZuFwd<L>(l));
    });
  }

  // see ZhttpClient.hh ReqBuilder contract for optional callbacks

};

template <unsigned Method>
struct MethodFilter {
  template <typename Req>
  using Filter = ZuBool<Req::Method == Method>;
};

template <typename Req>
using ReqPath = typename Req::Path;

// monomorphic (type-erased) request parser base
// - takes a typelist of Reqs, each a ReqParser<Impl, Object>
// - contains a union of all request parsers to be used with a specific Server
// - Parser callbacks are forwarded to the right instance
// - application will `struct ReqParser : public MReqParser<Reqs> { ... };`
template <typename Reqs_>
struct MReqParser : public Zhttp::Parser {
  using Reqs = Reqs_;

  using Headers = MergeHdrs<Reqs>;

  using Union = GetUnion<Reqs>;

  Union		u;

  template <typename Server>
  void init(Server &) { } // can be overridden

  bool operation(Zhttp::Method::T method, Zhttp::Target &target) {
    return ZuSwitch::dispatch<Zhttp::Method::N>(method, [this, method, &target](auto I) -> bool {
      using Reqs = ZuTypeGrep<MethodFilter<I>::template Filter, Reqs>;
      using Paths = ZuTypeMap<ReqPath, Reqs>;
      constexpr auto matcher = ZuMatcher<Paths>();
      auto j = matcher.match(target.path);
      if (j < 0) return false;
      return ZuSwitch::dispatch<Reqs::N>(j, [this, method, &target](auto J) -> bool {
	using Req = ZuType<J, Reqs>;
	auto request = new (u.template new_<Req, true>()) Req();
	request->init();
	if (!request->operation(method, target)) {
	  u = {};
	  return false;
	}
	return true;
      });
    });
  }

  template <typename Key, typename Value>
  bool header(Zhttp::FieldSection::T section) {
    u.dispatch([section](auto I, auto &request) {
      using ReqHdrs = typename Union::template Type<I>::Headers;
      using ReqHdrKeys = GetHdrKeys<ReqHdrs>;
      using ReqHdrValues = GetHdrValues<ReqHdrs>;
      if constexpr (ZuTypeIn<Key, ReqHdrKeys>{}) {
	using Values = ZuType<ZuTypeIndex<Key, ReqHdrKeys>{}, ReqHdrValues>;
	if constexpr (ZuTypeIn<Value, Values>{})
	  request.template header<Key, Value>(section);
	else {
	  using Storage =
	    ZtBArray<ZtArrayHeapID<"Zrest.Header.Value">>;
	  auto fixed = Value{}();
	  auto value = ZtScratch(
	    Storage, fixed.length(), fixed.length());
	  value = fixed;
	  request.template header<Key>(section, value.span());
	}
      } else {
	using Storage =
	  ZtBArray<ZtArrayHeapID<"Zrest.Header.Value">>;
	auto fixed = Value{}();
	auto value = ZtScratch(
	  Storage, fixed.length(), fixed.length());
	value = fixed;
	request.header(section, Key{}(), value.span());
      }
    });
  }
  template <typename Key>
  void header(
      Zhttp::FieldSection::T section, ZuSpan<uint8_t> value) {
    u.dispatch([section, &value](auto I, auto &request) {
      using ReqHdrs = typename Union::template Type<I>::Headers;
      using ReqHdrKeys = GetHdrKeys<ReqHdrs>;
      if constexpr (ZuTypeIn<Key, ReqHdrKeys>{}) {
	request.template header<Key>(section, value);
      } else {
	request.header(section, Key{}(), value);
      }
    });
  }
  void header(
      Zhttp::FieldSection::T section,
      ZuBSpan key, ZuSpan<uint8_t> value) {
    u.dispatch([section, &key, &value](auto I, auto &request) {
      request.header(section, key, value);
    });
  }

  void bodyInfo(Zhttp::BodyType::T type, uint64_t length) {
    u.dispatch([type, length](auto, auto &request) {
      request.bodyInfo(type, length);
    });
  }

  template <typename Rx> bool body(Rx &rx) {
    return u.dispatch([&rx](auto, auto &request) {
      return request.body(rx);
    });
  }

  template <typename Link>
  void complete(Link *link, bool ok) {
    u.dispatch([&link, ok](auto, auto &request) {
      request.complete(link, ok);
    });
  }

  void reset() { u.null(); }
};

// monomorphic (type-erased) response builder base
// - contains a union of all response types for all requests
// - Builder callbacks are forwarded to the right instance
// - application will `struct ResBuilder_ : public MResBuilder<Responses> { ... };`
//   and then define `ResBuilder` as the queue node, in the usual way
// - ResBuilder event callbacks (if any are implemented) are handled by
//   the application's centralized callbacks defined in `ResBuilder_`,
//   in the usual way - they are not forwarded
template <typename Parser_> // pass MReqParser
struct MResBuilder : public Zhttp::Builder {
  using Parser = Parser_;

  using Reqs = typename Parser::Reqs;

  using AllResponses = GetAllResponses<Reqs>;

  using Headers = MergeHdrs<AllResponses>;

  static constexpr unsigned HdrBufSize = ZuTypeApply<MaxHdrBufSize, AllResponses>{};

  using Union = GetUnion<AllResponses>;

  Union		u;

  // app should call init<Response>(parser)
  template <typename Res, typename Object>
  void init(const Parser &parser, Object *object) {
    ZuSwitch::dispatch<Reqs::N>(parser.u.type, [this, object](auto I) {
      using Req = ZuType<I, Reqs>;
      constexpr unsigned J = GetResIndex<Reqs, Req, Res>{};
      auto response = new (u.template new_<J, true>()) Res();
      response->init(object);
    });
  }

  BodyPolicy::T bodyPolicy() const {
    return u.cdispatch([](auto, const auto &response) {
      return response.bodyPolicy();
    });
  }

  unsigned status() const {
    return u.cdispatch([](auto, const auto &response) {
      return response.status();
    });
  }

  template <typename Key, typename Value, typename L> void header(L &&l) const {
    u.cdispatch([&l](auto I, const auto &response) {
      using ResHdrs = typename Union::template Type<I>::Headers;
      using ResHdrKeys = GetHdrKeys<ResHdrs>;
      using ResHdrValues = GetHdrValues<ResHdrs>;
      if constexpr (ZuTypeIn<Key, ResHdrKeys>{}) {
	using Values = ZuType<ZuTypeIndex<Key, ResHdrKeys>{}, ResHdrValues>;
	if constexpr (ZuTypeIn<Value, Values>{})
	  response.template header<Key, Value>(ZuFwd<L>(l));
      }
    });
  }
  template <typename Key, typename L> void header(L &&l) const {
    u.cdispatch([&l](auto I, const auto &response) {
      using ResHdrs = typename Union::template Type<I>::Headers;
      using ResHdrKeys = GetHdrKeys<ResHdrs>;
      if constexpr (ZuTypeIn<Key, ResHdrKeys>{})
	response.template header<Key>(ZuFwd<L>(l));
    });
  }
  template <typename L> void header(L &&l) const {
    u.cdispatch([&l](auto, const auto &response) {
      response.header(ZuFwd<L>(l));
    });
  }

  template <typename Emit> void body(Emit &&emit) const {
    u.cdispatch([&emit](auto, const auto &response) {
      response.body(ZuFwd<Emit>(emit));
    });
  }

  template <typename L> void bodyHdrs(L &&l) const {
    u.cdispatch([&l](auto, const auto &response) {
      response.bodyHdrs(ZuFwd<L>(l));
    });
  }

  // see ZhttpServer.hh ResBuilder contract for optional callbacks
};

// monomorphic (type-erased) response parser base
template <typename Builder_> // pass MReqBuilder
struct MResParser : public Zhttp::Parser {
  using Builder = Builder_;

  using Reqs = typename Builder::Reqs;

  using AllResponses = GetAllResponses<Reqs>;

  using Headers = MergeHdrs<AllResponses>;

  using Union = GetUnion<AllResponses>;

  using Lookup = GetReqLookup<Reqs>;

  static Lookup lookup;

  Union		u;
  const Builder	*builder = nullptr;

  void init(const Builder &builder_) {
    builder = &builder_;
  }

  bool status(unsigned code) {
    auto i = builder->u.type;
    return ZuSwitch::dispatch<Reqs::N>(i, [this, code](auto I) -> bool {
      unsigned j = lookup.template p<I>().findVal(code);
      if (ZuCmp<unsigned>::null(j)) return false;
      using Req = typename Union::template Type<I>;
      using Responses = GetResponses<Req>;
      ZuSwitch::dispatch<Responses::N>(j, [this](auto J) {
	using Res = ZuType<J, Responses>;
	constexpr unsigned K = GetResIndex<Reqs, Req, Res>{};
	auto response = new (u.template new_<K, true>()) Res();
	response->init();
      });
      return true;
    });
  }

  template <typename Key, typename Value>
  bool header(Zhttp::FieldSection::T section) {
    u.dispatch([section](auto I, auto &response) {
      using ResHdrs = typename Union::template Type<I>::Headers;
      using ResHdrKeys = GetHdrKeys<ResHdrs>;
      using ResHdrValues = GetHdrValues<ResHdrs>;
      if constexpr (ZuTypeIn<Key, ResHdrKeys>{}) {
	using Values = ZuType<ZuTypeIndex<Key, ResHdrKeys>{}, ResHdrValues>;
	if constexpr (ZuTypeIn<Value, Values>{})
	  response.template header<Key, Value>(section);
	else {
	  using Storage =
	    ZtBArray<ZtArrayHeapID<"Zrest.Header.Value">>;
	  auto fixed = Value{}();
	  auto value = ZtScratch(
	    Storage, fixed.length(), fixed.length());
	  value = fixed;
	  response.template header<Key>(section, value.span());
	}
      } else {
	using Storage =
	  ZtBArray<ZtArrayHeapID<"Zrest.Header.Value">>;
	auto fixed = Value{}();
	auto value = ZtScratch(
	  Storage, fixed.length(), fixed.length());
	value = fixed;
	response.header(section, Key{}(), value.span());
      }
    });
  }
  template <typename Key>
  void header(
      Zhttp::FieldSection::T section, ZuSpan<uint8_t> value) {
    u.dispatch([section, &value](auto I, auto &response) {
      using ResHdrs = typename Union::template Type<I>::Headers;
      using ResHdrKeys = GetHdrKeys<ResHdrs>;
      if constexpr (ZuTypeIn<Key, ResHdrKeys>{}) {
	response.template header<Key>(section, value);
      } else {
	response.header(section, Key{}(), value);
      }
    });
  }
  void header(
      Zhttp::FieldSection::T section,
      ZuBSpan key, ZuSpan<uint8_t> value) {
    u.dispatch([section, &key, &value](auto I, auto &response) {
      response.header(section, key, value);
    });
  }

  void bodyInfo(Zhttp::BodyType::T type, uint64_t length) {
    u.dispatch([type, length](auto, auto &response) {
      response.bodyInfo(type, length);
    });
  }

  template <typename Rx> bool body(Rx &rx) {
    return u.dispatch([&rx](auto, auto &response) {
      return response.body(rx);
    });
  }

  template <typename Link>
  void complete(Link *link, bool ok) {
    builder->u.dispatch([this, &link, ok](auto, auto &builder) {
      if (!ok || !u.type)
	builder.failed();
      else
	u.cdispatch([&builder, &link](auto, const auto &response) {
	  builder.process(link, response);
	});
    });
  }

  void reset() { u.null(); }
};

} // Zrest

#endif /* Zrest_HH */
