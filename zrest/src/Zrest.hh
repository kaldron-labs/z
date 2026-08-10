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

// default request compile-time metadata
struct Request {
  enum { Method = Zhttp::Method::Get };
  using Path = ZtStringT<"/">;

  enum { Query = QueryPolicy::None };
  enum { SignQuery = 0 };
  enum { UpdQuery = 0 };
  enum { Body = BodyPolicy::None };
  enum { SignBody = 0 };
  enum { UpdBody = 0 };

  using URI_Facet = ZuFacet::URI;
  using JSON_Facet = ZuFacet::JSON;
};

// default response compile-time metadata
struct Response {
  enum { Status = 200 };

  enum { Body = BodyPolicy::None };

  using JSON_Facet = ZuFacet::JSON;
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

  void operation(Zhttp::Method::T, const Zhttp::RequestTarget &target) {
    if constexpr (Impl::Query == QueryPolicy::None) {
      return;
    } else if constexpr (Impl::Query == QueryPolicy::URI) {
      // FIXME - URI expects path?query, is this available in H2/H3
      ZfJSON::handler<Object, Impl::URI_Facet> handler(target.???);
      if constexpr (Impl::UpdQuery)
	handler.update(impl()->queryObject());
      else
	handler.load(impl()->queryObject());
    } else if constexpr (Impl::Query == QueryPolicy::Raw) {
      impl()->queryObject() = target.query;
    }
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
	    ZfJSON::handler<Object, Impl::JSON_Facet> handler(span);
	    if constexpr (Impl::UpdBody)
	      handler.update(impl()->bodyObject());
	    else
	      handler.load(impl()->bodyObject());
	  } else if constexpr (Impl::Body == BodyPolicy::JSON) {
	    ZfURI::handler<Object, Impl::URI_Facet> handler(span);
	    if constexpr (Impl::UpdBody)
	      handler.update(impl()->bodyObject());
	    else
	      handler.load(impl()->bodyObject());
	  } else if constexpr (Impl::Body == BodyPolicy::Raw) {
	    impl()->bodyObject() = span;
	  }
	});
      return true;
    }
  }

  // Impl should implement complete
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
      emit(Impl::Method, Impl::Path{}(), false, [](auto &s) { });
    } else if constexpr (Impl::Query == QueryPolicy::URI && !Impl::SignQuery) {
      emit(Impl::Method, Impl::Path{}(), true, [this](auto &s) {
	ZfURI::save<typename Request::URI_Facet>(s, impl()->queryObject());
      });
    } else if constexpr (Impl::Query == QueryPolicy::URI /* && Impl::SignQuery */) {
      auto buf = ZtLocalArray/* FIXME */;
      const auto &query = impl()->queryObject();
      ZfURI::save<typename Request::URI_Facet>(buf, query);
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
	ZfJSON::save<typename Request::JSON_Facet>(s, impl()->bodyObject());
      });
    } else if constexpr (Impl::Body == BodyPolicy::JSON /* && Impl::SignBody */) {
      auto buf = ZtLocalArray/* FIXME */;
      const auto &body = impl()->bodyObject();
      ZfJSON::save<typename Request::JSON_Facet>(buf, body);
      impl()->signBody(buf, object, buf.cspan());
      emit([&buf](auto &s) { s << buf; });
      // FIXME - URI body
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
	  // FIXME - URI
	  if constexpr (Impl::Body == BodyPolicy::JSON) {
	    // FIXME - update
	    ZfJSON::handler<Object, Impl::JSON_Facet>(span).load(impl()->bodyObject());
	  } else if constexpr (Impl::Body == BodyPolicy::Raw) {
	    impl()->bodyObject() = span;
	  }
	});
      return true;
    }
  }

  // Impl should implement complete(bool ok)
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
    } else if constexpr (Impl::Body == BodyPolicy::JSON) {
      emit([this](auto &s) {
	ZfJSON::save<typename Op::JSON_Facet>(s, impl()->bodyObject());
      });
    } else if constexpr (Impl::Body == BodyPolicy::Raw) {
      emit([this](auto &s) { s << impl()->bodyObject(); });
      // FIXME - URI, signed flag, update
    } else if constexpr (Impl::Body == BodyPolicy::SignedJSON) {
      auto buf = ZtLocalArray/* FIXME */;
      const auto &body = impl()->bodyObject();
      ZfJSON::save<typename Op::JSON_Facet>(buf, body);
      impl()->signBody(buf, object, buf.cspan());
      emit([&buf](auto &s) { s << buf; });
    }
  }
};

} // Zrest

#endif /* Zrest_HH */
