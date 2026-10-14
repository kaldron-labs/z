//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z REST client library

#ifndef ZrestClient_HH
#define ZrestClient_HH

#include <zlib/Zrest.hh>
#include <zlib/ZrestInput.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmPQueue.hh>

#include <zlib/ZhttpClient.hh>
#include <zlib/ZhttpSSE.hh>

namespace Zrest {

template <typename Impl, typename Object_>
struct ReqBuilder : public Request, public Zhttp::Builder {
  using Request::Headers;
  using Object = Object_;

  using Zhttp::Builder::header;
  using ContentLength = ZuStringT<"content-length">;

  auto impl() { return static_cast<Impl *>(this); }
  auto impl() const { return static_cast<const Impl *>(this); }

  constexpr Zhttp::BodyPolicy::T bodyPolicy() const {
    ZuAssert(Impl::Body != BodyPolicy::SSE,
      "SSE is a response representation");
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
      auto buf = ZtScratch(SignBufScratch, Impl::SignQueryBufSize);
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
      auto buf = ZtScratch(SignBufScratch, Impl::SignBodyBufSize);
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
      auto buf = ZtScratch(SignBufScratch, Impl::SignBodyBufSize);
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
      auto buf = ZtScratch(SignBufScratch, Impl::SignBodyBufSize);
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

template <typename Impl, typename Object_>
struct ResParser : public Response, public Zhttp::Parser {
  using Response::Headers;
  using Object = Object_;

  auto impl() { return static_cast<Impl *>(this); }
  auto impl() const { return static_cast<const Impl *>(this); }

  ZmRef<Object>	object;
  unsigned	bodyLength = 0;
  uint64_t	inputMax = 0;
  Input		input;
  ZmRef<BorrowedData> borrowed;
  bool		streamed = false;
  bool		decoded = true;
  bool		bodySeen = false;
  bool		finished = false;
  bool		bodyValid = false;

  void init() { object = new Object(); }
  void bodyMax(uint64_t max) { inputMax = max; }

  auto &bodyObject(Object *object) { return *object; }

  template <typename Span>
  bool retainHeader(Span input, Span &output) {
    if constexpr (!Impl::Borrowed) { output = input; return true; }
    else return backing_().retain(input, output);
  }

  bool bodyInfo(Zhttp::BodyType::T type, uint64_t length) {
    if constexpr (Impl::Body == BodyPolicy::SSE) return false;
    streamed = type == Zhttp::BodyType::Streamed;
    decoded = true;
    bodySeen = false;
    finished = false;
    bodyValid = false;
    if constexpr (Impl::Body == BodyPolicy::None ||
	Impl::Body == BodyPolicy::Zero)
      return type != Zhttp::BodyType::Fixed || !length;
    else {
      if (!inputMax) return false;
      if (streamed) {
	if constexpr (Impl::Body == BodyPolicy::URI) return false;
	else return input.begin(length, inputMax);
      }
      if (length > UINT_MAX || (inputMax && length > inputMax)) return false;
      bodyLength = unsigned(length);
      if constexpr (Impl::Borrowed) return input.begin(length, inputMax);
      return true;
    }
  }

  template <typename Rx> bool body(Rx &rx) {
    if constexpr (Impl::Body == BodyPolicy::None ||
	Impl::Body == BodyPolicy::Zero)
      return false;
    else {
      if (streamed || Impl::Borrowed) return input.body(rx);
      rx.consume(
	[this](ZuSpan<uint8_t> span) {
	  return span.length() < bodyLength ? 0U : bodyLength;
	},
	[this](ZuSpan<uint8_t> span) {
	  decoded = decodeBody_(span);
	  bodySeen = true;
	});
      return decoded;
    }
  }

  bool finishBody() {
    if (finished) return bodyValid;
    finished = true;
    if constexpr (Impl::Body == BodyPolicy::None ||
	Impl::Body == BodyPolicy::Zero) return bodyValid = bool(object);
    if (streamed || Impl::Borrowed) {
      if (!input.buffer()) return false;
      decoded = decodeBody_(input.buffer()->span());
      bodySeen = true;
    }
    if constexpr (Impl::Body == BodyPolicy::JSON ||
	Impl::Body == BodyPolicy::URI)
      return bodyValid = decoded && bodySeen && bool(object);
    else return bodyValid = decoded && bool(object);
  }

  void reset() {
    object = nullptr;
    bodyLength = 0;
    inputMax = 0;
    input.reset();
    borrowed = nullptr;
    streamed = false;
    decoded = true;
    bodySeen = false;
    finished = false;
    bodyValid = false;
  }

private:
  BorrowedData &backing_() {
    if (!borrowed) borrowed = new BorrowedData{};
    return *borrowed;
  }

  bool decodeBody_(ZuSpan<uint8_t> span) {
    if constexpr (Impl::Body == BodyPolicy::JSON) {
      using Body_JSON_Facet = Impl::Body_JSON_Facet;
      auto scan = ZfJSON::scan(span);
      if (!jsonTail(span, scan.p<0>()) || !scan.p<1>() ||
	  scan.p<1>()->data<ZfJSON::AnyNode::Array>().length() != 1) {
	object = nullptr;
	return false;
      }
      auto handler = ZfJSON::handler<Object, Body_JSON_Facet>(
	(*scan.p<1>())[0]);
      if (!handler.valid) {
	object = nullptr;
	return false;
      }
      handler.load(impl()->bodyObject(object.ptr()));
      if constexpr (Impl::Borrowed)
	backing_().json = ZuMv(scan.p<1>());
    } else if constexpr (Impl::Body == BodyPolicy::URI) {
      using Body_URI_Facet = Impl::Body_URI_Facet;
      auto scan = ZfURI::scan(span, true);
      if (scan.p<0>() < 0 || !scan.p<1>()) {
	object = nullptr;
	return false;
      }
      auto handler = ZfURI::handler<Object, Body_URI_Facet>(scan.p<1>());
      if (!handler.valid) {
	object = nullptr;
	return false;
      }
      handler.load(impl()->bodyObject(object.ptr()));
      if constexpr (Impl::Borrowed)
	backing_().body = ZuMv(scan.p<1>());
    } else if constexpr (Impl::Body == BodyPolicy::Raw)
      impl()->bodyObject(object.ptr()) = span;
    return true;
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

  Zhttp::BodyPolicy::T bodyPolicy() const {
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
	using Values = Zhttp::HeaderValues<Key, ReqHdrs>;
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

  template <typename Link, typename Response>
  void process(Link *link, Response &&response) {
    u.cdispatch([link, &response](auto, const auto &request) {
      request.process(link, response);
    });
  }

  template <typename Link>
  void failed(Link *link) {
    u.cdispatch([link](auto, const auto &request) {
      request.failed(link);
    });
  }
};

template <typename Catalog_, typename Builder_>
struct MResParser : public Zhttp::Parser {
  using Catalog = Catalog_;
  using Builder = Builder_;
  using Reqs = typename Catalog::List;
  using AllResponses = GetAllResponses<Reqs>;
  using SSEResponses = ZuTypeGrep<SSEResponse, AllResponses>;
  static constexpr bool HaveSSE = SSEResponses::N;
  using HdrCatalog = typename Catalog::ResHeaders;
  using Headers = typename HdrCatalog::List;
  using Union = GetUnion<AllResponses>;
  Union		u;
  Builder	*builder = nullptr;
  uint64_t	inputMax = 0;
  bool		headersValid = true;
  Zhttp::SSEDecoder sse;
  uint64_t	sseBytes = 0;
  unsigned	statusCode = 0;
  bool		sseContentType = false;
  uint64_t	redirectBytes = 0;
  bool		redirect = false;
  bool		redirectValid = true;

  static bool redirectStatus_(unsigned code) {
    switch (code) {
      case 301: case 302: case 303: case 307: case 308:
	return true;
      default:
	return false;
    }
  }

  template <typename B>
  static auto sseLimits_(B *builder, int) -> decltype(builder->limits()) {
    return builder->limits();
  }
  template <typename B>
  static Limits sseLimits_(B *, long) { return {}; }

  template <typename B, typename Res>
  static bool sseEvent_(B *builder, Zhttp::SSEEvent event,
      unsigned status, Res &response) {
    return builder->receiveSSE(ZuMv(event), status, response);
  }

  template <typename B>
  static void sseDone_(B *builder) { builder->sseCompleted(); }

  void init(Builder &builder_) { builder = &builder_; }
  void bodyMax(uint64_t max) { inputMax = max; }

  bool status(unsigned code) {
    statusCode = code;
    sseContentType = false;
    redirect = false;
    auto i = builder->u.type() - 1;
    int j = Catalog::resMatch(i, code);
    if (j < 0) return redirect = redirectStatus_(code);
    return ZuSwitch::dispatch<AllResponses::N>(unsigned(j),
      [this](auto J) -> bool {
        using Res = ZuType<J, AllResponses>;
	auto response = new (u.template new_<J + 1, true>()) Res();
	response->init();
	response->bodyMax(inputMax);
        return true;
      });
  }

  template <typename Key, typename Value>
  void header(Zhttp::FieldSection::T section) {
    if constexpr (Key{}() == "content-type")
      sseContentType = Value{}() == "text/event-stream";
    u.dispatch([this, section](auto I, auto &response) {
      using ResHdrs = GetHdrs<typename Union::template Type<I>>;
      using ResHdrKeys = GetHdrKeys<ResHdrs>;
      using ResHdrValues = GetHdrValues<ResHdrs>;
      if constexpr (ZuTypeIn<Key, ResHdrKeys>{}) {
	using Values = ZuType<ZuTypeIndex<Key, ResHdrKeys>{}, ResHdrValues>;
	if constexpr (ZuTypeIn<Value, Values>{})
	  response.template header<Key, Value>(section);
	else {
	  auto fixed = Value{}();
	  auto value = ZtScratch(HeaderScratch, fixed.length());
	  value = fixed;
	  ZuSpan<uint8_t> stable;
	  if (!response.retainHeader(value.span(), stable)) {
	    headersValid = false;
	    return;
	  }
	  response.template header<Key>(section, stable);
	}
      } else {
	auto fixed = Value{}();
	auto value = ZtScratch(HeaderScratch, fixed.length());
	value = fixed;
	ZuSpan<uint8_t> stable;
	if (!response.retainHeader(value.span(), stable)) {
	  headersValid = false;
	  return;
	}
	response.header(section, Key{}(), stable);
      }
    });
  }
  template <typename Key>
  void header(Zhttp::FieldSection::T section, ZuSpan<uint8_t> value) {
    if constexpr (Key{}() == "content-type")
      sseContentType = value == "text/event-stream";
    u.dispatch([this, section, &value](auto I, auto &response) {
      using ResHdrs = GetHdrs<typename Union::template Type<I>>;
      using ResHdrKeys = GetHdrKeys<ResHdrs>;
      ZuSpan<uint8_t> stable;
      if (!response.retainHeader(value, stable)) {
	headersValid = false;
	return;
      }
      if constexpr (ZuTypeIn<Key, ResHdrKeys>{})
	response.template header<Key>(section, stable);
      else
	response.header(section, Key{}(), stable);
    });
  }
  void header(
      Zhttp::FieldSection::T section,
      ZuBSpan key, ZuSpan<uint8_t> value) {
    if (key == "content-type")
      sseContentType = value == "text/event-stream";
    u.dispatch([this, section, &key, &value](auto, auto &response) {
      ZuBSpan stableKey;
      ZuSpan<uint8_t> stableValue;
      if (!response.retainHeader(key, stableKey) ||
	  !response.retainHeader(value, stableValue)) {
	headersValid = false;
	return;
      }
      response.header(section, stableKey, stableValue);
    });
  }

  bool bodyInfo(Zhttp::BodyType::T type, uint64_t length) {
    if (redirect) {
      redirectBytes = 0;
      redirectValid = true;
      return inputMax && length <= inputMax;
    }
    return u.dispatch([this, type, length](auto, auto &response) {
      using Res = ZuDecay<decltype(response)>;
      if constexpr (HaveSSE && Res::Body == BodyPolicy::SSE) {
	if (!sseContentType || !inputMax || length > inputMax) return false;
	auto limits = sseLimits_(builder, 0);
	sse.reset(limits.maxSSELineBytes, limits.maxSSEEventBytes);
	sseBytes = 0;
	return true;
      } else return response.bodyInfo(type, length);
    });
  }

  template <typename Rx> bool body(Rx &rx) {
    if (redirect) {
	rx.consume([](ZuSpan<uint8_t> span) -> int64_t {
	  return span.length();
	}, [this](ZuSpan<uint8_t> span) {
	  if (span.length() > inputMax - redirectBytes) redirectValid = false;
	  else redirectBytes += span.length();
	});
      return redirectValid;
    }
    return u.dispatch([this, &rx](auto, auto &response) {
      using Res = ZuDecay<decltype(response)>;
      if constexpr (HaveSSE && Res::Body == BodyPolicy::SSE) {
	rx.consume([](ZuSpan<uint8_t> span) -> int64_t {
	  return span.length();
	}, [this, &response](ZuSpan<uint8_t> span) {
	  if (span.length() > inputMax - sseBytes) {
	    sse.close();
	    return;
	  }
	  sseBytes += span.length();
	  (void)sse.feed(span, [this, &response](Zhttp::SSEEvent event) {
	    if (!sseEvent_(builder, ZuMv(event), statusCode, response))
	      sse.close();
	  });
	});
	return sse.state() == Zhttp::SSEDecoder::Open;
      } else return response.body(rx);
    });
  }

  template <typename Link>
  void complete(Link *link, bool ok) {
    if (redirect) { builder->failed(link); return; }
    bool decoded = u.type() && u.dispatch([this](auto, auto &response) {
      using Res = ZuDecay<decltype(response)>;
      if constexpr (HaveSSE && Res::Body == BodyPolicy::SSE)
	return sse.complete() && sseContentType;
      else return response.finishBody();
    });
    if (!ok || !decoded || !headersValid)
      builder->failed(link);
    else
      u.dispatch([this, link](auto, auto &response) {
	using Res = ZuDecay<decltype(response)>;
	if constexpr (HaveSSE && Res::Body == BodyPolicy::SSE)
	  sseDone_(builder);
	else builder->process(link, ZuMv(response));
      });
  }

  void reset() {
    u.null();
    inputMax = 0;
    headersValid = true;
    sse.close();
    sseBytes = 0;
    statusCode = 0;
    sseContentType = false;
    redirectBytes = 0;
    redirect = false;
    redirectValid = true;
  }
};

template <typename App, typename Catalog> class Client;
template <typename App, typename Catalog> struct ClientPool;

template <typename Call, typename = void>
struct HasFailureDetail : public ZuFalse { };
template <typename Call>
struct HasFailureDetail<Call, decltype(
  ZuDeclVal<Call &>().failed(ZuDeclVal<const Failure &>()), void())> :
  public ZuTrue { };

template <typename App, typename Catalog>
struct ClientReq : public ZmObject, public MReqBuilder<Catalog> {
  using Base = MReqBuilder<Catalog>;
  using HdrCatalog = typename Base::HdrCatalog;
  using Responses = GetAllResponses<typename Catalog::List>;
  using Result = GetUnion<Responses>;

  Client<App, Catalog> *client = nullptr;
  uint64_t id = 0;
  Result selected;
  Zhttp::Result httpResult;
  Failure failure;
  void *call = nullptr;
  bool (*processFn)(void *, unsigned, void *) = nullptr;
  bool (*sseFn)(void *, unsigned, Zhttp::SSEEvent,
    unsigned, const void *) = nullptr;
  void (*sseDoneFn)(void *) = nullptr;
  void (*failedFn)(void *, const Failure &) = nullptr;
  void (*releaseFn)(void *) = nullptr;
  bool completed_ = false;
  bool decodeFailed = false;
  bool sseSelected = false;

  ~ClientReq() { releaseCall(); }
  uint64_t key() const { return id; }
  uint64_t length() const { return 1; }

  template <typename Req, typename Call>
  void init(ZmRef<typename Req::Object> object, ZmRef<Call> call_) {
    ZuAssert((ZuTypeIn<Req, typename Catalog::List>{}));
    Base::template init<Req>(object.ptr());
    call = ZuMv(call_).release();
    processFn = [](void *call_, unsigned index, void *response) -> bool {
      return ZuSwitch::dispatch<Responses::N>(index,
	[call_, response](auto I) -> bool {
	  using Res = ZuType<I, Responses>;
	  if constexpr (ZuTypeIn<Res, typename Req::Responses>{} &&
	      Res::Body != BodyPolicy::SSE) {
	    static_cast<Call *>(call_)->process(
	      Reply<Res>{ZuMv(*static_cast<Res *>(response))});
	    return true;
	  } else return false;
	});
    };
    if constexpr (ZuTypeGrep<SSEResponse,
	  typename Req::Responses>::N) {
      sseFn = [](void *call_, unsigned index, Zhttp::SSEEvent event,
	  unsigned status, const void *response) -> bool {
	return ZuSwitch::dispatch<Responses::N>(index,
	  [call_, &event, status, response](auto I) -> bool {
	    using Res = ZuType<I, Responses>;
	    if constexpr (ZuTypeIn<Res, typename Req::Responses>{} &&
		Res::Body == BodyPolicy::SSE)
	      return static_cast<Call *>(call_)->receiveSSE(
		ZuMv(event), SSEMeta<Res>{status,
		  *static_cast<const Res *>(response)});
	    else return false;
	  }, false);
      };
      sseDoneFn = [](void *call_) {
	static_cast<Call *>(call_)->process();
      };
    }
    failedFn = [](void *call_, const Failure &failure_) {
      if constexpr (HasFailureDetail<Call>{})
	static_cast<Call *>(call_)->failed(failure_);
      else static_cast<Call *>(call_)->failed();
    };
    releaseFn = [](void *call_) {
      auto call = static_cast<Call *>(call_);
      if (call->deref()) delete call;
    };
  }

  template <typename Link, typename Res>
  void process(Link *, Res &&response) {
    selected = ZuMv(response);
    decodeFailed = false;
    sseSelected = false;
  }
  template <typename Link>
  void failed(Link *) {
    decodeFailed = true;
    sseSelected = false;
    selected.null();
  }

  const Limits &limits() const {
    if (client) return client->limits();
    static const Limits defaults;
    return defaults;
  }
  template <typename Res>
  bool receiveSSE(Zhttp::SSEEvent event, unsigned status,
      const Res &response) {
    if (!sseFn || !call) return false;
    constexpr unsigned I = ZuTypeIndex<Res, Responses>{};
    try { return sseFn(call, I, ZuMv(event), status, &response); }
    catch (...) { return false; }
  }
  void sseCompleted() {
    selected.null();
    decodeFailed = false;
    sseSelected = true;
  }

  void completed(const Zhttp::Result &result) {
    httpResult = result;
    completed_ = true;
  }
  void retired() {
    if (client) client->retired(this);
  }

  void deliver() {
    auto call_ = call;
    if (!call_) return;
    try {
      if (completed_ && httpResult.ok() && !decodeFailed && sseSelected) {
	if (sseDoneFn) sseDoneFn(call_);
	else failedFn(call_, failure);
      } else if (completed_ && httpResult.ok() &&
	  !decodeFailed && selected.type()) {
	bool delivered = selected.dispatch([this, call_](auto I, auto &response) {
	  if constexpr (I > 0)
	    return processFn(call_, unsigned(I) - 1, &response);
	  else return false;
	});
	if (!delivered) {
	  failure.http = httpResult;
	  failure.code = Failure::Code::Status;
	  failedFn(call_, failure);
	}
      } else {
	if (completed_) {
	  failure.http = httpResult;
	  if (decodeFailed) failure.code = Failure::Code::Decode;
	  else if (httpResult.ok()) failure.code = Failure::Code::Status;
	  else if (httpResult.code == Zhttp::ResultCode::TimedOut)
	    failure.code = Failure::Code::Timeout;
	  else if (httpResult.code == Zhttp::ResultCode::Cancelled)
	    failure.code = Failure::Code::Stopped;
	  else failure.code = Failure::Code::Transport;
	}
	failedFn(call_, failure);
      }
    } catch (...) { }
    releaseCall();
  }

  void releaseCall() {
    if (!call) return;
    auto call_ = call;
    call = nullptr;
    releaseFn(call_);
  }
};

template <typename App, typename Catalog>
using ClientReqQ = ZmPQueue<ClientReq<App, Catalog>,
  ZmPQueueOverlap<false, ZmPQueueNode<ClientReq<App, Catalog>,
    ZmPQueueHeapID<"Zrest.Client.Request">>>>;
template <typename App, typename Catalog>
using ClientTxQ = ZmPQTx<ClientPool<App, Catalog>, ClientReqQ<App, Catalog>,
  ZmPQTxOrdered<false>>;

template <typename App, typename Catalog, typename Heap = ZuVoid>
struct ClientPool_ : public Heap,
    public Zhttp::Pool<Client<App, Catalog>, ClientTxQ<App, Catalog>,
      MResParser<Catalog, ClientReq<App, Catalog>>> {
  using Base = Zhttp::Pool<Client<App, Catalog>, ClientTxQ<App, Catalog>,
    MResParser<Catalog, ClientReq<App, Catalog>>>;
  using Queue = ClientReqQ<App, Catalog>;
  using Request = typename Queue::Node;

  ClientPool_(Client<App, Catalog> *client) : Base{client} { }
  Queue *txQueue() { return &m_requests; }
  void archive_(Request *) { }
  ZmRef<Request> retrieve_(typename Queue::Key, typename Queue::Key) {
    return {};
  }
private:
  Queue m_requests;
};
template <typename App, typename Catalog>
ZuDerive(ClientPool, (ClientPool_<App, Catalog,
  ZmHeap<"Zrest.Client.Pool", ClientPool_<App, Catalog>>>));

template <typename App, typename Catalog>
class Client : public Zhttp::Client<Client<App, Catalog>,
    ClientPool<App, Catalog>> {
  using HTTP = Zhttp::Client<Client, ClientPool<App, Catalog>>;
  using Request = typename ClientReqQ<App, Catalog>::Node;
public:
  using HTTP::start;
  using HTTP::stop;
  using HTTP::pool;

  ~Client() { final(); }

  bool init(const Zhttp::HubConfig &hub, unsigned poolCount,
      ClientConfig config, App *app, const Zhttp::TCPConfig &tcp = {},
      const Zhttp::H2Config &h2 = {}, const Zhttp::QUICConfig &quic = {}) {
    if (m_mx || !app || !hub.mx() || !valid(config.limits()) ||
	!HTTP::init(hub, poolCount, config, tcp, h2, quic)) return false;
    m_mx = hub.mx();
    m_owner = hub.rxThread() ? m_mx->sid(hub.rxThread()) : m_mx->rxThread();
    m_app = app;
    m_limits = config.limits();
    return true;
  }

  bool start() {
    if (!m_mx || m_up || !HTTP::start()) return false;
    m_up = 1;
    return true;
  }
  template <typename Done>
  void start(Done &&done) {
    if (!m_mx || m_up) { done(false); return; }
    HTTP::start([this, done = ZuFwd<Done>(done)](bool ok) mutable {
      if (ok) m_up = 1;
      done(ok);
    });
  }

  template <typename Req, typename Call>
  bool call(unsigned pool, ZmRef<typename Req::Object> object,
      ZmRef<Call> call_) {
    ZuAssert((ZuTypeIn<Req, typename Catalog::List>{}));
    if (!m_up || !object || !call_ || !HTTP::config(pool)) return false;
    bool owner = m_mx->invoked(m_owner);
    if (!owner && ++m_ingress > m_limits.maxPending) {
      --m_ingress;
      return false;
    }
    ZmRef<Request> request = new Request{};
    request->template init<Req>(ZuMv(object), ZuMv(call_));
    request->id = ++m_next;
    if (!request->id) request->id = ++m_next;
    if (owner) { admit_(pool, ZuMv(request), false); return true; }
    m_mx->run([this, pool, request = ZuMv(request)]() mutable {
      admit_(pool, ZuMv(request), true);
    }, m_owner);
    return true;
  }

  void retired(ClientReq<App, Catalog> *request) {
    --m_pending;
    request->client = nullptr;
    request->deliver();
  }

  bool stop() {
    return ZmBlock<bool>{}([this](auto wake) mutable {
      this->stop(ZuMv(wake));
    });
  }
  template <typename Done>
  void stop(Done &&done) {
    if (!m_mx) { done(true); return; }
    m_up = 0;
    HTTP::stop([this, done = ZuFwd<Done>(done)](bool ok) mutable {
      m_mx->run([this, done = ZuMv(done), ok]() mutable {
	drain_([done = ZuMv(done), ok]() mutable { done(ok); });
      }, m_owner);
    });
  }
  void final() {
    if (!m_mx) return;
    (void)stop();
    HTTP::final();
    m_mx = nullptr;
    m_app = nullptr;
  }
  bool up() const { return bool(m_up); }
  const Limits &limits() const { return m_limits; }

private:
  template <typename Done>
  void drain_(Done done) {
    if (!m_ingress.load_() && !m_pending.load_()) { done(); return; }
    m_mx->run([this, done = ZuMv(done)]() mutable {
      drain_(ZuMv(done));
    }, m_owner);
  }

  void admit_(unsigned pool, ZmRef<Request> request, bool ingress) {
    if (!m_up) {
      if (ingress) --m_ingress;
      request->failure.code = Failure::Code::Stopped;
      request->deliver();
      return;
    }
    if (++m_pending > m_limits.maxPending) {
      --m_pending;
      if (ingress) --m_ingress;
      request->failure.code = Failure::Code::Limit;
      request->deliver();
      return;
    }
    if (ingress) --m_ingress;
    request->client = this;
    if (HTTP::send(pool, request)) return;
    --m_pending;
    request->client = nullptr;
    request->failure.code = Failure::Code::Stopped;
    request->deliver();
  }

  ZiMultiplex *m_mx = nullptr;
  App *m_app = nullptr;
  Limits m_limits;
  unsigned m_owner = 0;
  ZmAtomic<unsigned> m_up = 0;
  ZmAtomic<unsigned> m_ingress = 0;
  ZmAtomic<unsigned> m_pending = 0;
  ZmAtomic<uint64_t> m_next = 0;
};

} // Zrest

#endif /* ZrestClient_HH */
