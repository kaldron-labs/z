//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z REST library

#ifndef Zrest_HH
#define Zrest_HH

#include <zlib/ZrestLib.hh>

#include <zlib/ZuDerive.hh>

#include <zlib/ZiRx.hh>

#include <zlib/Ztls.hh>

#include <zlib/ZfCf.hh>
#include <zlib/ZvEngine.hh>

#include <zlib/Zhttp.hh>

namespace Zrest {

ZtEnumNS(ZrestAPI, QueryPolicy, None, URI, Raw);

ZtEnumNS(ZrestAPI, BodyPolicy, None, JSON, URI, Raw);

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

#define Zrest_Response_(Status, Response) \
  ZuUnsigned<Status>, Response
#define Zrest_Response(SR) \
  ZuPP_Defer(Zrest_Response_)(ZuPP_Strip(SR))
#define ZrestResponses(...) \
  ZuTypeList<ZuPP_Eval_(ZuPP_MapComma(Zrest_Response,  __VA_ARGS__))>

// default request compile-time metadata
// - implementation must define Responses
struct Request {
  enum { Method = Zhttp::Method::Get };
  using Path = ZtStringT<"/">;

  enum { Query = QueryPolicy::None };
  enum { SignQuery = 0 };
  enum { Body = BodyPolicy::None };
  enum { SignBody = 0 };

  static constexpr unsigned SignQueryBufSize = 1<<10; // 1k
  static constexpr unsigned SignBodyBufSize = 1<<10; // 1k

  using Query_URI_Facet = ZuFacet::URI;
  using Body_URI_Facet = ZuFacet::IncrementalURI;
  using Body_JSON_Facet = ZuFacet::JSON;

  // using Responses = ZrestResponses(...);
};

// default response compile-time metadata
struct Response {
  enum { Status = 200 };

  enum { Body = BodyPolicy::None };

  using Body_JSON_Facet = ZuFacet::JSON;
};

// request parser
template <typename Impl, typename Object>
struct ReqParser : public Request, public Zhttp::Parser {
  auto impl() { return static_cast<Impl *>(this); }
  auto impl() const { return static_cast<const Impl *>(this); }

  using Headers = Zrest::Headers<Impl>;

  ZmRef<Object> 	object;
  unsigned		bodyLength = 0;

  const auto &queryObject(Object *object) { return *object; }

  const auto &bodyObject(Object *object) { return *object; }

  bool operation(Zhttp::Method::T, const Zhttp::Target &target) {
    if constexpr (Impl::Query == QueryPolicy::None) {
      return true;
    } else if constexpr (Impl::Query == QueryPolicy::URI) {
      auto span = target.pathQuery;
      span.offset(Impl::Path{}().length());
      ZfJSON::handler<Object, typename Impl::Query_URI_Facet> handler(span);
      handler.load(impl()->queryObject());
    } else if constexpr (Impl::Query == QueryPolicy::Raw) {
      auto span = target.pathQuery;
      span.offset(Impl::Path{}().length());
      impl()->queryObject() = span;
    }
    return true;
  }

  void bodyInfo(BodyType::T type, uint64_t length) {
    bodyLength = length;
  }

  template <typename Rx> bool body(Rx &rx) {
    if constexpr (Impl::Body == BodyPolicy::None)
      return false;
    else {
      if (type == BodyType::Streamed) return false;
      rx.consume(
	[this](ZuBSpan span) {
	  return span.length() < bodyLength ? 0U : bodyLength;
	},
	[this](ZuBSpan span) {
	  if constexpr (Impl::Body == BodyPolicy::JSON) {
	    ZfJSON::handler<Object, typename Impl::Body_JSON_Facet> handler(span);
	    handler.load(impl()->bodyObject());
	  } else if constexpr (Impl::Body == BodyPolicy::URI) {
	    ZfURI::handler<Object, typename Impl::Body_URI_Facet> handler(span);
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
template <typename Impl, typename Object>
struct ReqBuilder : public Request, public Zhttp::Builder {
  auto impl() { return static_cast<Impl *>(this); }
  auto impl() const { return static_cast<const Impl *>(this); }

  using Headers = Zrest::Headers<Impl>;

  ZmRef<const Object>	object;

  const auto &queryObject(const Object *object) { return *object; }
  template <typename S>
  void signQuery(S &, const Object *, ZuCSpan) { }

  const auto &bodyObject(const Object *object) { return *object; }
  template <typename S>
  void signBody(S &, const Object *, ZuCSpan) { }

  template <typename Emit> void operation(Emit &&emit) {
    if constexpr (Impl::Query == QueryPolicy::None) {
      emit(Impl::Method, [](auto &s) { s << Impl::Path{}(); }});
    } else if constexpr (Impl::Query == QueryPolicy::URI && !Impl::SignQuery) {
      emit(Impl::Method, [this](auto &s) {
	s << Impl::Path{}();
	ZfURI::save<typename Impl::Body_URI_Facet>(s, impl()->queryObject());
      });
    } else if constexpr (Impl::Query == QueryPolicy::URI /* && Impl::SignQuery */) {
      auto buf = ZtScratch(SignBuf, SignQueryBufSize);
      const auto &query = impl()->queryObject();
      ZfURI::save<typename Impl::Body_URI_Facet>(buf, query);
      impl()->signQuery(buf, object, buf.cspan());
      emit(Impl::Method, Impl::Path{}(), true, [&buf](auto &s) { s << buf; });
    } else if constexpr (Impl::Query == QueryPolicy::Raw) {
      emit(Impl::Method, Impl::Path{}(), true, [this](auto &s) {
	s << impl()->queryObject();
      });
    }
  }

  template <typename Emit> void body(Emit &&emit) {
    if constexpr (Impl::Body == BodyPolicy::None) {
      return;
    } else if constexpr (Impl::Body == BodyPolicy::JSON && !Impl::SignBody) {
      emit([this](auto &s) {
	ZfJSON::save<typename Impl::Body_JSON_Facet>(s, impl()->bodyObject());
      });
    } else if constexpr (Impl::Body == BodyPolicy::JSON /* && Impl::SignBody */) {
      auto buf = ZtScratch(SignBuf, SignBodyBufSize);
      const auto &body = impl()->bodyObject();
      ZfJSON::save<typename Impl::Body_JSON_Facet>(buf, body);
      impl()->signBody(buf, object, buf.cspan());
      emit([&buf](auto &s) { s << buf; });
    } else if constexpr (Impl::Body == BodyPolicy::URI && !Impl::SignBody) {
      emit([this](auto &s) {
	ZfURI::saveBody<typename Impl::Body_URI_Facet>(s, impl()->bodyObject());
      });
    } else if constexpr (Impl::Body == BodyPolicy::URI /* && Impl::SignBody */) {
      auto buf = ZtScratch(SignBuf, SignBodyBufSize);
      const auto &body = impl()->bodyObject();
      ZfURI::saveBody<typename Impl::Body_URI_Facet>(buf, body);
      impl()->signBody(buf, object, buf.cspan());
      emit([&buf](auto &s) { s << buf; });
    } else if constexpr (Impl::Body == BodyPolicy::Raw) {
      emit([this](auto &s) { s << impl()->bodyObject(); });
    }
  }
};

// response parser
template <typename Impl, typename Object>
struct ResParser : public Response, public Zhttp::Parser {
  auto impl() { return static_cast<Impl *>(this); }
  auto impl() const { return static_cast<const Impl *>(this); }

  using Headers = Zrest::Headers<Impl>;

  ZmRef<Object>	object;
  unsigned	bodyLength = 0;

  const auto &bodyObject(const Object *object) { return *object; }

  void bodyInfo(BodyType::T type, uint64_t length) {
    bodyLength = length;
  }

  template <typename Rx> bool body(Rx &rx) {
    if constexpr (Impl::Body == BodyPolicy::None)
      return false;
    else {
      if (type == BodyType::Streamed) return false;
      rx.consume(
	[this](ZuBSpan span) {
	  return span.length() < bodyLength ? 0U : bodyLength;
	},
	[this](ZuBSpan span) {
	  if constexpr (Impl::Body == BodyPolicy::JSON) {
	    ZfJSON::handler<Object, typename Impl::Body_JSON_Facet> handler(span);
	    handler.load(impl()->bodyObject());
	  } else if constexpr (Impl::Body == BodyPolicy::URI) {
	    ZfURI::handler<Object, Impl::Body_URI_Facet> handler(span);
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
template <typename Impl, typename Object>
struct ResBuilder : public Response, public Zhttp::Builder {
  auto impl() { return static_cast<Impl *>(this); }
  auto impl() const { return static_cast<const Impl *>(this); }

  using Headers = Zrest::Headers<Impl>;

  ZmRef<const Object>	object;

  const auto &bodyObject(const Object *object) { return *object; }
  template <typename S>
  void signBody(S &, const Object *, ZuCSpan) { }

  unsigned status() const { return Response::Status; }

  template <typename Emit> void body(Emit &&emit) {
    if constexpr (Impl::Body == BodyPolicy::None) {
      return;
    } else if constexpr (Impl::Body == BodyPolicy::JSON && !Impl::SignBody) {
      emit([this](auto &s) {
	ZfJSON::save<typename Impl::Body_JSON_Facet>(s, impl()->bodyObject());
      });
    } else if constexpr (Impl::Body == BodyPolicy::JSON /* && Impl::SignBody */) {
      auto buf = ZtScratch(SignBuf, SignBodyBufSize);
      const auto &body = impl()->bodyObject();
      ZfJSON::save<typename Impl::Body_JSON_Facet>(buf, body);
      impl()->signBody(buf, object, buf.cspan());
      emit([&buf](auto &s) { s << buf; });
    } else if constexpr (Impl::Body == BodyPolicy::URI && !Impl::SignBody) {
      emit([this](auto &s) {
	ZfURI::save<typename Impl::Body_URI_Facet>(s, impl()->bodyObject());
      });
    } else if constexpr (Impl::Body == BodyPolicy::URI /* && Impl::SignBody */) {
      auto buf = ZtScratch(SignBuf, SignBodyBufSize);
      const auto &body = impl()->bodyObject();
      ZfURI::save<typename Impl::Body_URI_Facet>(buf, body);
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
template <typename U> using HdrBufSizeAxor = typename Value_<U>::T;
template <typename ...Reqs> struct MaxReqHdrBufSize__;
template <>
struct MaxReqHdrBufSize__<> {
  using T = ZuUnsigned<0>;
};
template <typename U>
struct MaxReqHdrBufSize__<U> {
  using T = HdrBufSizeAxor<U>;
};
template <typename L, typename R>
struct MaxReqHdrBufSize__<L, R> {
  constexpr unsigned L_ = HdrBufSizeAxor<L>{};
  constexpr unsigned R_ = HdrBufSizeAxor<R>{};
  constexpr unsigned M = L_ > R_ ? L_ : R_;
  using T = ZuUnsigned<M>;
};
template <typename ...Reqs>
using MaxReqHdrBufSize_ = typename MaxReqHdrBufSize__<Reqs...>::T;
template <typename ...Reqs>
using MaxReqHdrBufSize = ZuTypeReduce<MaxReqHdrBufSize_, Reqs>;

// monomorphic (type-erased) request builder base
// - takes a typelist of Reqs, each a ReqBuilder<Impl, Object>
// - contains a union of all request types to be used with a specific Client
// - Builder callbacks are forwarded to the right instance
// - application will `struct ReqBuilder_ : public MReqBuilder<Reqs> { ... };`
//   and then define `ReqBuilder` as the queue node, in the usual way
// - ReqBuilder event callbacks (if any are implemented) are handled by
//   the application's centralized callbacks defined in `ReqBuilder_`,
//   in the usual way - they are not forwarded to Reqs
template <typename Reqs>
struct MReqBuilder : public Zhttp::ReqBuilder {
  template <typename U> using ReqHdrs = typename U::Headers;
  using Headers = ZuTypeUnique<ZuTypeApply<ZuTypeConcat, ZuTypeMap<ReqHdrs, Reqs>>>;

  static constexpr unsigned HdrBufSize = ZuTypeApply<MaxReqHdrBufSize, Reqs>{};

  using Union = ZuTypeApply<ZuUnion, Reqs>;

  Union		u;

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

  template <typename Key, typename L> void header(L &&l) const {
    u.cdispatch([&l](auto I, const auto &request) {
      if constexpr (ZuTypeIn<Key, ZuTypeSlice<2, 0, ZuType<I, Reqs>::Headers>>{})
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
template <typename Reqs>
struct MReqParser : public Request, public Zhttp::Parser {
  template <typename U> using ParserHdrs = typename U::Headers;
  using Headers = ZuTypeUnique<ZuTypeApply<ZuTypeConcat, ZuTypeMap<ParserHdrs, Reqs>>>;

  using Union = ZuTypeApply<ZuUnion, Reqs::template Unshift<void>>;

  Union		u;

  bool operation(Method::T method, const Target &target) {
    bool accepted = false;
    ZuSwitch::dispatch<Method::N>(method,
      [this, method, &target, &accepted](auto I) {
      using Reqs = ZuTypeGrep<MethodFilter<I>::template Filter, Reqs>;
      using Paths = ZuTypeMap<ReqPath, Reqs>;
      constexpr auto matcher = ZuMatcher<Paths>();
      auto j = matcher.match(target.path);
      if (j >= 0) {
	ZuSwitch::dispatch<Reqs::N>(j,
	    [this, method, &target, &accepted](auto J) {
	  using Req = ZuType<J, Reqs>;
	  auto parser = new (u.new_<Req, true>()) Req();
	  accepted = parser->operation(method, target);
	});
      }
    });
    return accepted;
  }

  void bodyInfo(BodyType::T type, uint64_t length) {
    u.dispatch([type, length](auto, auto &parser) {
      parser.bodyInfo(type, length);
    });
  }

  template <typename Rx> bool body(Rx &rx) {
    return u.dispatch([&rx](auto, auto &parser) {
      return parser.body(rx);
    });
  }

  template <typename Link>
  void complete(Link *link, bool ok) {
    u.dispatch([&link, ok](auto, auto &parser) {
      parser.complete(link, ok);
    });
  }

  void reset() { u.null(); }
};

// FIXME - MResBuilder<Responses>

// FIXME - MResParser<Reqs>
// - Client needs to call response parser with current request to permit
//   double-dispatch based on request->u.type, then status
//   - this is naturally a TL of TLs, and a ZuUnion of ZuUnions
// - `Parser::status(unsigned)` -> `Parser::status(request *, unsigned)`

// need: ZrestResponses(200, OK, 201, ...) macro to convert to:
// ZuTypeList<ZuUnsigned<200>, OK, ZuUnsigned<201>, ...>
// use a Static LHash to map from status to response type (which is unsigned->unsigned, i.e. just the index within the responses typelist), then regular ZuSwitch

} // Zrest

#endif /* Zrest_HH */
