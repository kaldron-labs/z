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
  using Zhttp::Builder::header;
  using ContentLength = ZuStringT<"content-length">;

  auto impl() { return static_cast<Impl *>(this); }
  auto impl() const { return static_cast<const Impl *>(this); }

  constexpr Zhttp::BodyPolicy::T bodyPolicy() const {
    return Impl::Body == BodyPolicy::None ?
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
    if constexpr (Impl::Body == BodyPolicy::None) {
      return;
    } else if constexpr (Impl::Body == BodyPolicy::JSON && !Impl::SignBody) {
      emit([this](auto &s) {
	using Body_JSON_Facet = Impl::Body_JSON_Facet;
	ZfJSON::save<Body_JSON_Facet>(s, impl()->bodyObject(object.ptr()));
	s.flush();
	bodyLength = s.produced();
	return true;
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
	return true;
      });
    } else if constexpr (Impl::Body == BodyPolicy::URI && !Impl::SignBody) {
      emit([this](auto &s) {
	using Body_URI_Facet = Impl::Body_URI_Facet;
	ZfURI::saveBody<Body_URI_Facet>(s, impl()->bodyObject(object.ptr()));
	s.flush();
	bodyLength = s.produced();
	return true;
      });
    } else if constexpr (Impl::Body == BodyPolicy::URI) {
      using Body_URI_Facet = Impl::Body_URI_Facet;
      auto buf = ZtScratch(SignBuf, Impl::SignBodyBufSize);
      const auto &body = impl()->bodyObject(object.ptr());
      ZfURI::saveBody<Body_URI_Facet>(buf, body);
      impl()->signBody(buf, object.ptr(), buf.cspan());
      emit([this, &buf](auto &s) {
	s << buf;
	s.flush();
	bodyLength = s.produced();
	return true;
      });
    } else if constexpr (Impl::Body == BodyPolicy::Raw) {
      emit([this](auto &s) {
	s << impl()->bodyObject(object.ptr());
	s.flush();
	bodyLength = s.produced();
	return true;
      });
    }
  }

  template <typename Key, typename L>
  void header(L &&l) const {
    if constexpr (ZuIsSame<Key, ContentLength>{} &&
	Impl::Body != BodyPolicy::None)
      l("0000000000");
  }
  template <typename L> void header(L &&) const { }

  template <typename L>
  void bodyHdrs(L &&l) const {
    if constexpr (Impl::Body != BodyPolicy::None)
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
  auto impl() { return static_cast<Impl *>(this); }
  auto impl() const { return static_cast<const Impl *>(this); }

  ZmRef<Object>	object;
  unsigned	bodyLength = 0;

  void init() { object = new Object(); }

  auto &bodyObject(Object *object) { return *object; }

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

template <typename Reqs_>
struct MReqBuilder : public Zhttp::ReqBuilder {
  using Reqs = Reqs_;
  using Headers = MergeHdrs<Reqs>;
  static constexpr unsigned HdrBufSize = ZuTypeApply<MaxHdrBufSize, Reqs>{};
  using Union = GetUnion<Reqs>;

  Union		u;

  template <typename Builder, typename Object>
  void init(Object *object) {
    auto builder = new (u.template new_<Builder>()) Builder();
    builder->init(object);
  }

  BodyPolicy::T bodyPolicy() const {
    BodyPolicy::T policy = BodyPolicy::None;
    u.cdispatch([&policy](auto, const auto &request) {
      policy = request.bodyPolicy();
    });
    return policy;
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
using GetReqLookup = ZuTypeApply<ZuTuple, ZuTypeMap<GetResLookup, Reqs>>;

template <typename Builder_>
struct MResParser : public Zhttp::Parser {
  using Builder = Builder_;
  using Reqs = typename Builder::Reqs;
  using AllResponses = GetAllResponses<Reqs>;
  using Headers = MergeHdrs<AllResponses>;
  using Union = GetUnion<AllResponses>;
  using Lookup = GetReqLookup<Reqs>;

  inline static Lookup lookup;

  Union		u;
  const Builder	*builder = nullptr;

  void init(const Builder &builder_) { builder = &builder_; }

  bool status(unsigned code) {
    auto i = builder->u.type() - 1;
    return ZuSwitch::dispatch<Reqs::N>(i, [this, code](auto I) -> bool {
      unsigned j = lookup.template p<I>().findVal(code);
      if (ZuCmp<unsigned>::null(j)) return false;
      using Req = ZuType<I, Reqs>;
      using Responses = GetResponses<Req>;
      ZuSwitch::dispatch<Responses::N>(j, [this](auto J) {
	using Res = ZuType<J, Responses>;
	constexpr unsigned K = GetResIndex<Reqs, Req, Res>{};
	auto response = new (u.template new_<K + 1, true>()) Res();
	response->init();
      });
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
	  auto value = ZtScratch(Storage, fixed.length(), fixed.length());
	  value = fixed;
	  response.template header<Key>(section, value.span());
	}
      } else {
	using Storage = ZtBArray<ZtArrayHeapID<"Zrest.Header.Value">>;
	auto fixed = Value{}();
	auto value = ZtScratch(Storage, fixed.length(), fixed.length());
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
    bool accepted = false;
    u.dispatch([type, length, &accepted](auto, auto &response) {
      accepted = response.bodyInfo(type, length);
    });
    return accepted;
  }

  template <typename Rx> bool body(Rx &rx) {
    bool accepted = false;
    u.dispatch([&rx, &accepted](auto, auto &response) {
      accepted = response.body(rx);
    });
    return accepted;
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
