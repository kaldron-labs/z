//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z REST server library

#ifndef ZrestServer_HH
#define ZrestServer_HH

#include <zlib/Zrest.hh>

#include <zlib/ZhttpServer.hh>

namespace Zrest {

template <typename Impl, typename Object>
struct ReqParser : public Request, public Zhttp::Parser {
  using Request::Headers;

  auto impl() { return static_cast<Impl *>(this); }
  auto impl() const { return static_cast<const Impl *>(this); }

  ZmRef<Object> 	object;
  unsigned		bodyLength = 0;

  void init() { object = new Object(); }

  auto &queryObject(Object *object) { return *object; }
  auto &bodyObject(Object *object) { return *object; }

  bool operation(Zhttp::Method::T, Zhttp::Target &target) {
    impl()->init();
    if constexpr (Impl::Query == QueryPolicy::URI) {
      using Path = Impl::Path;
      using Query_URI_Facet = Impl::Query_URI_Facet;
      auto span = target.path;
      span.offset(Path{}().length());
      auto scan = ZfURI::scan(span);
      auto handler = ZfURI::handler<Object, Query_URI_Facet>(scan.p<1>());
      handler.load(impl()->queryObject(object.ptr()));
    } else if constexpr (Impl::Query == QueryPolicy::Raw) {
      using Path = Impl::Path;
      auto span = target.path;
      span.offset(Path{}().length());
      impl()->queryObject(object.ptr()) = span;
    }
    return true;
  }

  bool bodyInfo(Zhttp::BodyType::T type, uint64_t length) {
    bodyLength = length;
    return type != Zhttp::BodyType::Streamed &&
      (Impl::Body != BodyPolicy::Zero || !length);
  }

  template <typename Rx> bool body(Rx &rx) {
    if constexpr (Impl::Body == BodyPolicy::None ||
	Impl::Body == BodyPolicy::Zero)
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
	    auto handler = ZfJSON::handler<Object, Body_JSON_Facet>(
	      (*scan.p<1>())[0]);
	    handler.load(impl()->bodyObject(object.ptr()));
	  } else if constexpr (Impl::Body == BodyPolicy::URI) {
	    using Body_URI_Facet = Impl::Body_URI_Facet;
	    auto scan = ZfURI::scan(span);
	    auto handler = ZfURI::handler<Object, Body_URI_Facet>(scan.p<1>());
	    handler.load(impl()->bodyObject(object.ptr()));
	  } else if constexpr (Impl::Body == BodyPolicy::Raw) {
	    impl()->bodyObject(object.ptr()) = span;
	  }
	});
      return true;
    }
  }

  void reset() {
    object = nullptr;
    bodyLength = 0;
  }
};

template <typename Impl, typename Object>
struct ResBuilder : public Response, public Zhttp::Builder {
  using Response::Headers;

  using Zhttp::Builder::header;
  using ContentLength = ZuStringT<"content-length">;

  auto impl() { return static_cast<Impl *>(this); }
  auto impl() const { return static_cast<const Impl *>(this); }

  constexpr Zhttp::BodyPolicy::T bodyPolicy() const {
    return Impl::Body == BodyPolicy::None || Impl::Body == BodyPolicy::Zero ?
      Zhttp::BodyPolicy::None : Zhttp::BodyPolicy::Fixed;
  }

  ZmRef<const Object>	object;
  mutable uint64_t	bodyLength = 0;

  void init(Object *object_) { object = object_; }

  const auto &bodyObject(const Object *object) const { return *object; }
  template <typename S>
  void signBody(S &, const Object *, ZuCSpan) const { }

  unsigned status() const { return Impl::Status; }

  template <typename Emit> void body(Emit &&emit) const {
    if constexpr (Impl::Body == BodyPolicy::None ||
	Impl::Body == BodyPolicy::Zero) {
      return;
    } else if constexpr (Impl::Body == BodyPolicy::JSON && !Impl::SignBody) {
      emit([this](auto &s) {
	using Body_JSON_Facet = Impl::Body_JSON_Facet;
	ZfJSON::save<Body_JSON_Facet>(s, impl()->bodyObject(object.ptr()));
	s.flush();
	bodyLength = s.produced();
	return Zhttp::WriteOutcome::End;
      });
    } else if constexpr (Impl::Body == BodyPolicy::JSON) {
      using Body_JSON_Facet = Impl::Body_JSON_Facet;
      auto buf = ZtScratch(SignBuf, Impl::SignBodyBufSize);
      const auto &body = impl()->bodyObject(object.ptr());
      ZfJSON::save<Body_JSON_Facet>(buf, body);
      impl()->signBody(buf, object.ptr(), buf.cspan());
      emit([this, &buf](auto &s) {
	s << buf;
	s.flush();
	bodyLength = s.produced();
	return Zhttp::WriteOutcome::End;
      });
    } else if constexpr (Impl::Body == BodyPolicy::URI && !Impl::SignBody) {
      emit([this](auto &s) {
	using Body_URI_Facet = Impl::Body_URI_Facet;
	ZfURI::save<Body_URI_Facet>(s, impl()->bodyObject(object.ptr()));
	s.flush();
	bodyLength = s.produced();
	return Zhttp::WriteOutcome::End;
      });
    } else if constexpr (Impl::Body == BodyPolicy::URI) {
      using Body_URI_Facet = Impl::Body_URI_Facet;
      auto buf = ZtScratch(SignBuf, Impl::SignBodyBufSize);
      const auto &body = impl()->bodyObject(object.ptr());
      ZfURI::save<Body_URI_Facet>(buf, body);
      impl()->signBody(buf, object.ptr(), buf.cspan());
      emit([this, &buf](auto &s) {
	s << buf;
	s.flush();
	bodyLength = s.produced();
	return Zhttp::WriteOutcome::End;
      });
    } else if constexpr (Impl::Body == BodyPolicy::Raw) {
      emit([this](auto &s) {
	s << impl()->bodyObject(object.ptr());
	s.flush();
	bodyLength = s.produced();
	return Zhttp::WriteOutcome::End;
      });
    }
  }

  template <typename Key, typename L>
  void header(L &&l) const {
    if constexpr (ZuIsSame<Key, ContentLength>{}) {
      if constexpr (Impl::Body == BodyPolicy::Zero) l("0");
      else if constexpr (Impl::Body != BodyPolicy::None) l("0000000000");
    }
  }
  template <typename L> void header(L &&) const { }

  template <typename L>
  void bodyHdrs(L &&l) const {
    if constexpr (Impl::Body != BodyPolicy::None &&
	Impl::Body != BodyPolicy::Zero)
      l.template operator()<ContentLength>(
	[bodyLength = bodyLength](ZuSpan<uint8_t> span) {
	  ZuStream s{span};
	  s << ZuBoxed(bodyLength).fmt<ZuFmt::Right<10>>();
	});
  }
};

template <unsigned Method>
struct MethodFilter {
  template <typename Req>
  using Filter = ZuBool<Req::Method == Method>;
};

template <typename Req>
using ReqPath = typename Req::Path;

template <typename Reqs_>
struct MReqParser : public Zhttp::Parser {
  using Reqs = Reqs_;
  using Headers = MergeHdrs<Reqs>;
  using Union = GetUnion<Reqs>;

  Union		u;

  template <typename Server>
  void init(Server &) { }

  bool operation(Zhttp::Method::T method, Zhttp::Target &target) {
    return ZuSwitch::dispatch<Zhttp::Method::N>(method,
	[this, method, &target](auto I) -> bool {
      using MethodReqs = ZuTypeGrep<MethodFilter<I>::template Filter, Reqs>;
      if constexpr (!MethodReqs::N) {
	return false;
      } else {
	using Paths = ZuTypeMap<ReqPath, MethodReqs>;
	constexpr auto matcher = ZuMatcher<Paths>();
	auto j = matcher.match(target.path);
	if (j < 0) return false;
	return ZuSwitch::dispatch<MethodReqs::N>(j,
	    [this, method, &target](auto J) -> bool {
	  using Req = ZuType<J, MethodReqs>;
	  auto request = new (u.template new_<Req, true>()) Req();
	  request->init();
	  if (!request->operation(method, target)) {
	    u = {};
	    return false;
	  }
	  return true;
	});
      }
    });
  }

  template <typename Key, typename Value>
  void header(Zhttp::FieldSection::T section) {
    u.dispatch([section](auto I, auto &request) {
      using ReqHdrs = GetHdrs<typename Union::template Type<I>>;
      using ReqHdrKeys = GetHdrKeys<ReqHdrs>;
      using ReqHdrValues = GetHdrValues<ReqHdrs>;
      if constexpr (ZuTypeIn<Key, ReqHdrKeys>{}) {
	using Values = ZuType<ZuTypeIndex<Key, ReqHdrKeys>{}, ReqHdrValues>;
	if constexpr (ZuTypeIn<Value, Values>{})
	  request.template header<Key, Value>(section);
	else {
	  using Storage = ZtBArray<ZtArrayHeapID<"Zrest.Header.Value">>;
	  auto fixed = Value{}();
	  auto value = ZtScratch(Storage, fixed.length());
	  value = fixed;
	  request.template header<Key>(section, value.span());
	}
      } else {
	using Storage = ZtBArray<ZtArrayHeapID<"Zrest.Header.Value">>;
	auto fixed = Value{}();
	auto value = ZtScratch(Storage, fixed.length());
	value = fixed;
	request.header(section, Key{}(), value.span());
      }
    });
  }
  template <typename Key>
  void header(Zhttp::FieldSection::T section, ZuSpan<uint8_t> value) {
    u.dispatch([section, &value](auto I, auto &request) {
      using ReqHdrs = GetHdrs<typename Union::template Type<I>>;
      using ReqHdrKeys = GetHdrKeys<ReqHdrs>;
      if constexpr (ZuTypeIn<Key, ReqHdrKeys>{})
	request.template header<Key>(section, value);
      else
	request.header(section, Key{}(), value);
    });
  }
  void header(
      Zhttp::FieldSection::T section,
      ZuBSpan key, ZuSpan<uint8_t> value) {
    u.dispatch([section, &key, &value](auto, auto &request) {
      request.header(section, key, value);
    });
  }

  bool bodyInfo(Zhttp::BodyType::T type, uint64_t length) {
    return u.dispatch([type, length](auto, auto &request) {
      return request.bodyInfo(type, length);
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

template <typename Parser_>
struct MResBuilder : public Zhttp::ResBuilder {
  using Parser = Parser_;
  using Reqs = typename Parser::Reqs;
  using AllResponses = GetAllResponses<Reqs>;
  using Headers = MergeHdrs<AllResponses>;
  static constexpr unsigned HdrBufSize =
    ZuTypeApply<MaxHdrBufSize, AllResponses>{};
  using Union = GetUnion<AllResponses>;

  Union		u;

  template <typename Res, typename Req, typename Object>
  void init(Object *object) {
    constexpr unsigned J = GetResIndex<Reqs, Req, Res>{};
    auto response = new (u.template new_<J + 1, true>()) Res();
    response->init(object);
  }

  template <typename Res, typename Object>
  void init(const Parser &parser, Object *object) {
    ZuSwitch::dispatch<Reqs::N>(parser.u.type() - 1,
	[this, object](auto I) {
      using Req = ZuType<I, Reqs>;
      if constexpr (ZuTypeIn<Res, GetResponses<Req>>{})
	init<Res, Req>(object);
    });
  }

  BodyPolicy::T bodyPolicy() const {
    return u.cdispatch([](auto, const auto &response) {
      return response.bodyPolicy();
    });
  }

  unsigned status() const {
    if (!u.type()) return 500;
    return u.cdispatch([](auto, const auto &response) {
      return response.status();
    });
  }

  template <typename Key, typename Value, typename L> void header(L &&l) const {
    u.cdispatch([&l](auto I, const auto &response) {
      using ResHdrs = GetHdrs<typename Union::template Type<I>>;
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
      using ResHdrs = GetHdrs<typename Union::template Type<I>>;
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
};

} // Zrest

#endif /* ZrestServer_HH */
