//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z REST client library

#ifndef ZrestClient_HH
#define ZrestClient_HH

#include <zlib/Zrest.hh>

#include <zlib/ZhttpClient.hh>

namespace Zrest {

template <typename Impl, typename Object>
struct ReqBuilder : public Request, public Zhttp::Builder {
  using Request::Headers;

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

  const auto &queryObject(const Object *object) const { return *object; }
  template <typename S>
  void signQuery(S &, const Object *, ZuCSpan) const { }

  const auto &bodyObject(const Object *object) const { return *object; }
  template <typename S>
  void prefixBody(S &, const Object *) const { }
  template <typename S>
  void signBody(S &, const Object *, ZuCSpan) const { }

  template <typename Emit> void operation(Emit &&emit) const {
    if constexpr (Impl::Query == QueryPolicy::None) {
      using Path = Impl::Path;
      emit(Impl::Method, [](auto &&emit) {
	emit([](auto &s) { s << Path{}(); });
      });
    } else if constexpr (Impl::Query == QueryPolicy::URI && !Impl::SignQuery) {
      emit(Impl::Method, [this](auto &&emit) {
	emit([this](auto &s) {
	  using Path = Impl::Path;
	  using Query_URI_Facet = Impl::Query_URI_Facet;
	  s << Path{}();
	  ZfURI::save<Query_URI_Facet>(s, impl()->queryObject(object.ptr()));
	});
      });
    } else if constexpr (Impl::Query == QueryPolicy::URI) {
      using Path = Impl::Path;
      using Query_URI_Facet = Impl::Query_URI_Facet;
      auto buf = ZtScratch(SignBuf, Impl::SignQueryBufSize);
      const auto &query = impl()->queryObject(object.ptr());
      buf << Path{}();
      ZfURI::save<Query_URI_Facet>(buf, query);
      impl()->signQuery(buf, object.ptr(), buf.cspan());
      emit(Impl::Method, [&buf](auto &&emit) {
	emit([&buf](auto &s) { s << buf; });
      });
    } else if constexpr (Impl::Query == QueryPolicy::Raw) {
      emit(Impl::Method, [this](auto &&emit) {
	emit([this](auto &s) {
	  using Path = Impl::Path;
	  s << Path{}() << impl()->queryObject(object.ptr());
	});
      });
    }
  }

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
      impl()->prefixBody(buf, object.ptr());
      unsigned offset = buf.length();
      ZfJSON::save<Body_JSON_Facet>(buf, body);
      impl()->signBody(buf, object.ptr(), buf.cspan().offset(offset));
      emit([this, &buf](auto &s) {
	s << buf;
	s.flush();
	bodyLength = s.produced();
	return Zhttp::WriteOutcome::End;
      });
    } else if constexpr (Impl::Body == BodyPolicy::URI && !Impl::SignBody) {
      emit([this](auto &s) {
	using Body_URI_Facet = Impl::Body_URI_Facet;
	ZfURI::saveBody<Body_URI_Facet>(s, impl()->bodyObject(object.ptr()));
	s.flush();
	bodyLength = s.produced();
	return Zhttp::WriteOutcome::End;
      });
    } else if constexpr (Impl::Body == BodyPolicy::URI) {
      using Body_URI_Facet = Impl::Body_URI_Facet;
      auto buf = ZtScratch(SignBuf, Impl::SignBodyBufSize);
      const auto &body = impl()->bodyObject(object.ptr());
      impl()->prefixBody(buf, object.ptr());
      unsigned offset = buf.length();
      ZfURI::saveBody<Body_URI_Facet>(buf, body);
      impl()->signBody(buf, object.ptr(), buf.cspan().offset(offset));
      emit([this, &buf](auto &s) {
	s << buf;
	s.flush();
	bodyLength = s.produced();
	return Zhttp::WriteOutcome::End;
      });
    } else if constexpr (Impl::Body == BodyPolicy::Raw && Impl::SignBody) {
      auto buf = ZtScratch(SignBuf, Impl::SignBodyBufSize);
      impl()->prefixBody(buf, object.ptr());
      unsigned offset = buf.length();
      buf << impl()->bodyObject(object.ptr());
      impl()->signBody(buf, object.ptr(), buf.cspan().offset(offset));
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

  template <typename Link, typename Response>
  void process(Link *link, const Response &response) const {
    object->process(link, response.object.ptr());
  }

  template <typename Link>
  void failed(Link *link) const { object->failed(link); }
};

template <typename Impl, typename Object>
struct ResParser : public Response, public Zhttp::Parser {
  using Response::Headers;

  auto impl() { return static_cast<Impl *>(this); }
  auto impl() const { return static_cast<const Impl *>(this); }

  ZmRef<Object>	object;
  unsigned	bodyLength = 0;

  void init() { object = new Object(); }

  auto &bodyObject(Object *object) { return *object; }

  bool bodyInfo(Zhttp::BodyType::T type, uint64_t length) {
    bodyLength = length;
    if constexpr (Impl::Body == BodyPolicy::None ||
	Impl::Body == BodyPolicy::Zero)
      return type != Zhttp::BodyType::Fixed || !length;
    else
      return type != Zhttp::BodyType::Streamed;
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
	    auto scan = ZfURI::scan(span, true);
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

template <typename Catalog_>
struct MReqBuilder : public Zhttp::ReqBuilder {
  using Catalog = Catalog_;
  using Reqs = typename Catalog::List;
  using HdrCatalog = typename Catalog::ReqHeaders;
  using Headers = typename HdrCatalog::List;
  static constexpr unsigned HdrBufSize = ZuTypeApply<MaxHdrBufSize, Reqs>{};
  using Union = GetUnion<Reqs>;

  Union		u;

  template <typename Builder, typename Object>
  void init(Object *object) {
    auto builder = new (u.template new_<Builder>()) Builder();
    builder->init(object);
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
      using ReqHdrs = GetHdrs<typename Union::template Type<I>>;
      using ReqHdrKeys = GetHdrKeys<ReqHdrs>;
      if constexpr (ZuTypeIn<Key, ReqHdrKeys>{}) {
	using Values = GetKValues<Key, ReqHdrs>;
	if constexpr (ZuTypeIn<Value, Values>{})
	  request.template header<Key, Value>(ZuFwd<L>(l));
      }
    });
  }
  template <typename Key, typename L> void header(L &&l) const {
    u.cdispatch([&l](auto I, const auto &request) {
      using ReqHdrs = GetHdrs<typename Union::template Type<I>>;
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
};

template <typename Catalog_, typename Builder_>
struct MResParser : public Zhttp::Parser {
  using Catalog = Catalog_;
  using Builder = Builder_;
  using Reqs = typename Catalog::List;
  using AllResponses = GetAllResponses<Reqs>;
  using HdrCatalog = typename Catalog::ResHeaders;
  using Headers = typename HdrCatalog::List;
  using Union = GetUnion<AllResponses>;
  Union		u;
  const Builder	*builder = nullptr;

  void init(const Builder &builder_) { builder = &builder_; }

  bool status(unsigned code) {
    auto i = builder->u.type() - 1;
    int j = Catalog::resMatch(i, code);
    if (j < 0) return false;
    return ZuSwitch::dispatch<AllResponses::N>(unsigned(j),
      [this](auto J) -> bool {
        using Res = ZuType<J, AllResponses>;
	auto response = new (u.template new_<J + 1, true>()) Res();
	response->init();
        return true;
      });
  }

  template <typename Key, typename Value>
  void header(Zhttp::FieldSection::T section) {
    u.dispatch([section](auto I, auto &response) {
      using ResHdrs = GetHdrs<typename Union::template Type<I>>;
      using ResHdrKeys = GetHdrKeys<ResHdrs>;
      using ResHdrValues = GetHdrValues<ResHdrs>;
      if constexpr (ZuTypeIn<Key, ResHdrKeys>{}) {
	using Values = ZuType<ZuTypeIndex<Key, ResHdrKeys>{}, ResHdrValues>;
	if constexpr (ZuTypeIn<Value, Values>{})
	  response.template header<Key, Value>(section);
	else {
	  using Storage = ZtBArray<ZtArrayHeapID<"Zrest.Header.Value">>;
	  auto fixed = Value{}();
	  auto value = ZtScratch(Storage, fixed.length());
	  value = fixed;
	  response.template header<Key>(section, value.span());
	}
      } else {
	using Storage = ZtBArray<ZtArrayHeapID<"Zrest.Header.Value">>;
	auto fixed = Value{}();
	auto value = ZtScratch(Storage, fixed.length());
	value = fixed;
	response.header(section, Key{}(), value.span());
      }
    });
  }
  template <typename Key>
  void header(Zhttp::FieldSection::T section, ZuSpan<uint8_t> value) {
    u.dispatch([section, &value](auto I, auto &response) {
      using ResHdrs = GetHdrs<typename Union::template Type<I>>;
      using ResHdrKeys = GetHdrKeys<ResHdrs>;
      if constexpr (ZuTypeIn<Key, ResHdrKeys>{})
	response.template header<Key>(section, value);
      else
	response.header(section, Key{}(), value);
    });
  }
  void header(
      Zhttp::FieldSection::T section,
      ZuBSpan key, ZuSpan<uint8_t> value) {
    u.dispatch([section, &key, &value](auto, auto &response) {
      response.header(section, key, value);
    });
  }

  bool bodyInfo(Zhttp::BodyType::T type, uint64_t length) {
    return u.dispatch([type, length](auto, auto &response) {
      return response.bodyInfo(type, length);
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
      if (!ok || !u.type())
	builder.failed(link);
      else
	u.cdispatch([&builder, &link](auto, const auto &response) {
	  builder.process(link, response);
	});
    });
  }

  void reset() { u.null(); }
};

} // Zrest

#endif /* ZrestClient_HH */
