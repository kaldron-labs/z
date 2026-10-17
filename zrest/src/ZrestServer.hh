//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z REST server library

#ifndef ZrestServer_HH
#define ZrestServer_HH

#include <zlib/Zrest.hh>
#include <zlib/ZrestInput.hh>
#include <zlib/ZrestSSE.hh>

#include <zlib/ZhttpServer.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmScheduler.hh>

#include <zlib/ZtArray.hh>

namespace Zrest {

template <typename Impl, typename Object_>
struct ReqParser : public Request, public Zhttp::Parser {
  using Request::Headers;
  using Object = Object_;

  auto impl() { return static_cast<Impl *>(this); }
  auto impl() const { return static_cast<const Impl *>(this); }

  ZmRef<Object> 	object;
  unsigned		bodyLength = 0;
  uint64_t		inputMax = 0;
  Input		input;
  ZmRef<BorrowedData> borrowed;
  bool		streamed = false;
  bool		decoded = true;
  bool		bodySeen = false;
  bool		finished = false;
  bool		bodyValid = false;

  void init() { object = new Object(); }
  void bodyMax(uint64_t max) { inputMax = max; }

  auto &queryObject(Object *object) { return *object; }
  auto &bodyObject(Object *object) { return *object; }

  bool retainTarget(Zhttp::Target &target) {
    if constexpr (!Impl::Borrowed) return true;
    else {
      ZuSpan<uint8_t> path;
      if (!backing_().retain(target.path, path)) return false;
      target.path = path;
      return true;
    }
  }

  template <typename Span>
  bool retainHeader(Span input, Span &output) {
    if constexpr (!Impl::Borrowed) { output = input; return true; }
    else return backing_().retain(input, output);
  }

  bool operation(Zhttp::Method::T, Zhttp::Target &target) {
    if constexpr (Impl::Query == QueryPolicy::URI) {
      using Path = Impl::Path;
      using Query_URI_Facet = Impl::Query_URI_Facet;
      auto span = target.path;
      span.offset(Path{}().length());
      if (Impl::QueryLimit && span.length() > Impl::QueryLimit) return false;
      auto scan = ZfURI::scan(span);
      if (scan.p<0>() < 0 || !scan.p<1>()) return false;
      auto handler = ZfURI::handler<Object, Query_URI_Facet>(scan.p<1>());
      if (!handler.valid) return false;
      handler.load(impl()->queryObject(object.ptr()));
      if constexpr (Impl::Borrowed)
	backing_().query = ZuMv(scan.p<1>());
    } else if constexpr (Impl::Query == QueryPolicy::Raw) {
      using Path = Impl::Path;
      auto span = target.path;
      span.offset(Path{}().length());
      if (Impl::QueryLimit && span.length() > Impl::QueryLimit) return false;
      impl()->queryObject(object.ptr()) = span;
    }
    return true;
  }

  bool bodyInfo(Zhttp::BodyType::T type, uint64_t length) {
    ZuAssert(Impl::Body != BodyPolicy::SSE,
      "SSE is a response representation");
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
      uint64_t max = Impl::BodyLimit ? Impl::BodyLimit : inputMax;
      if (streamed) {
	if constexpr (Impl::Body == BodyPolicy::URI) return false;
	else return input.begin(length, max);
      }
      if (length > UINT_MAX || (max && length > max)) return false;
      bodyLength = unsigned(length);
      if constexpr (Impl::Borrowed) return input.begin(length, max);
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

template <typename Impl, typename Object_>
struct ResBuilder : public Response, public Zhttp::Builder {
  using Response::Headers;
  using Object = Object_;

  using Zhttp::Builder::header;
  using ContentLength = ZuStringT<"content-length">;

  auto impl() { return static_cast<Impl *>(this); }
  auto impl() const { return static_cast<const Impl *>(this); }

  constexpr Zhttp::BodyPolicy::T bodyPolicy() const {
    ZuAssert(Impl::Body != BodyPolicy::SSE,
      "use SSEBuilder for an SSE response");
    return Impl::Body == BodyPolicy::None || Impl::Body == BodyPolicy::Zero ?
      Zhttp::BodyPolicy::None : Zhttp::BodyPolicy::Fixed;
  }

  ZmRef<const Object>	object;
  mutable uint64_t	bodyLength = 0;
  bool		disconnect_ = false;
  bool		terminal_ = false;

  void init(Object *object_) { object = object_; }

  const auto &bodyObject(const Object *object) const { return *object; }
  template <typename S>
  void prefixBody(S &, const Object *) const { }
  template <typename S>
  void signBody(S &, const Object *, ZuCSpan) const { }

  unsigned status() const { return Impl::Status; }
  bool disconnect() const { return disconnect_; }
  void close() { }

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
	ZfURI::save<Body_URI_Facet>(s, impl()->bodyObject(object.ptr()));
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
      ZfURI::save<Body_URI_Facet>(buf, body);
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
};

template <typename Impl>
struct SSEBuilder : public Response, public Zhttp::Builder {
  using Response::Headers;
  using Object = void;
  using Zhttp::Builder::header;

  ZmRef<SSEProducer> producer;
  bool disconnect_ = false;
  bool terminal_ = false;

  void init(ZmRef<SSEProducer> producer_) {
    producer = ZuMv(producer_);
  }
  Zhttp::BodyPolicy::T bodyPolicy() const {
    return Zhttp::BodyPolicy::Stream;
  }
  unsigned status() const { return Impl::Status; }
  bool disconnect() const {
    return disconnect_ || (producer && producer->failed());
  }
  void close() { if (producer) producer->close(); }

  template <typename L> void header(L &&) const { }
  template <typename Emit>
  void body(Emit &&emit) const {
    if (producer) producer->start(ZuFwd<Emit>(emit));
  }
  template <typename L> void bodyHdrs(L &&) const { }
};

struct MReqPathProjection {
  Zhttp::Target &target;
  ZuSpan<uint8_t> saved;

  MReqPathProjection(Zhttp::Target &target_, ZuSpan<uint8_t> path_):
    target(target_), saved(target_.path) { target.path = path_; }
  ~MReqPathProjection() { target.path = saved; }
};

template <typename Catalog_, unsigned Skip_ = 0>
struct MReqPolicy {
  using Catalog = Catalog_;
  using Reqs = Catalog::List;
  using Union = GetUnion<Reqs>;
  enum { Skip = Skip_ };

  template <typename Alt>
  using Hdrs = GetHdrs<Alt>;

  template <typename Parser, typename Server>
  void init(Parser &, Server &) { }
  template <typename Parser>
  void init(Parser &) { }

  template <typename Parser>
  bool operation(Parser &parser, Zhttp::Method::T method,
      Zhttp::Target &target) {
    return ZuSwitch::dispatch<Zhttp::Method::N>(method,
	[&parser, method, &target](auto I) -> bool {
      static constexpr unsigned MethodI = I;
      using MethodReqs =
	ZuTypeGrep<MethodFilter<MethodI>::template Filter, Reqs>;
      if constexpr (!MethodReqs::N) {
	return false;
      } else {
	auto path = target.path;
	if constexpr (Skip)
	  if (ZuUnlikely(!skip(path, Skip))) return false;
	int j = Catalog::reqMatch(method, path);
	if (j < 0) return false;
	return ZuSwitch::dispatch<MethodReqs::N>(unsigned(j),
	    [&parser, method, &target, path](auto J) -> bool {
	  using Req = ZuType<J, MethodReqs>;
	  if constexpr (Req::Exact) {
	    using ReqPath = Req::Path;
	    constexpr unsigned n = ReqPath{}().length();
	    if (path.length() != n &&
		(Req::Query == QueryPolicy::None ||
		 path.length() <= n || path[n] != '?')) return false;
	  }
	  auto request = new (parser.u.template new_<Req, true>()) Req();
	  request->init();
	  bool ok;
	  {
	    MReqPathProjection projected(target, path);
	    ok = request->retainTarget(target) &&
	      request->operation(method, target);
	  }
	  if (!ok) {
	    parser.u.null();
	    return false;
	  }
	  return true;
	}, false);
      }
    }, false);
  }

  template <typename Alt>
  static bool complete(Alt &request, bool ok) {
    return ok && request.object;
  }
};

template <typename Catalog_, typename Server_ = ZuVoid>
struct MReqRootPolicy {
  using Catalog = Catalog_;
  using Server = Server_;
  using Roots = Catalog::List;
  using Parsers = GetRootParsers<Roots>;
  using Union = GetUnion<Parsers>;
  Server *server = nullptr;

  template <typename Alt>
  using Hdrs = Alt::Headers;

  template <typename Parser>
  void init(Parser &, Server &server_) { server = &server_; }
  template <typename Parser>
  void init(Parser &) { }

  template <typename Parser>
  bool operation(Parser &parser, Zhttp::Method::T method,
      Zhttp::Target &target) {
    if constexpr (!ZuIsSame<Server, ZuVoid>{})
      if (!server) return false;
    ZuBSpan root;
    ZuSpan<uint8_t> suffix;
    if (ZuUnlikely(!splitRoot(target.path, root, suffix))) return false;
    int i = Catalog::rootMatch(root);
    if (i < 0) return false;
    return ZuSwitch::dispatch<Roots::N>(unsigned(i),
	[&, method, suffix](auto I) -> bool {
      using Root = ZuType<I, Roots>;
      using Child = Root::Parser;
      auto child = new (parser.u.template new_<I + 1, true>()) Child();
      if constexpr (ZuIsSame<Server, ZuVoid>{}) child->init();
      else child->init(*server);
      child->bodyMax(parser.inputMax);
      bool ok;
      {
	MReqPathProjection projected(target, suffix);
	ok = child->operation(method, target);
      }
      if (!ok) {
	parser.u.null();
	return false;
      }
      return true;
	}, false);
  }

  template <typename Alt>
  static bool complete(Alt &, bool ok) { return ok; }
};

template <typename Catalog_, typename Policy_ = MReqPolicy<Catalog_>>
struct MReqParser : public Zhttp::Parser {
  using Catalog = Catalog_;
  using Policy = Policy_;
  using HdrCatalog = Catalog::ReqHeaders;
  using Headers = HdrCatalog::List;
  using Union = Policy::Union;

  Union		u;
  Policy		policy;
  uint64_t	inputMax = 0;
  bool		headersValid = true;

  template <typename Server>
  void init(Server &server) { policy.init(*this, server); }
  void init() { policy.init(*this); }

  void bodyMax(uint64_t max) { inputMax = max; }

  template <typename Span>
  bool retainHeader(Span input, Span &output) {
    output = input;
    return true;
  }

  bool operation(Zhttp::Method::T method, Zhttp::Target &target) {
    if (!policy.operation(*this, method, target)) return false;
    u.dispatch([this](auto, auto &request) {
      request.bodyMax(inputMax);
    });
    return true;
  }

  bool finishBody() {
    return headersValid && u.dispatch([](auto, auto &request) {
      return request.finishBody();
    });
  }

  template <typename Key, typename Value>
  void header(Zhttp::FieldSection::T section) {
    u.dispatch([this, section](auto I, auto &request) {
      using ReqHdrs = Policy::template Hdrs<
	typename Union::template Type<I>>;
      using ReqHdrKeys = GetHdrKeys<ReqHdrs>;
      if constexpr (ZuTypeIn<Key, ReqHdrKeys>{}) {
	using Values = Zhttp::HeaderValues<Key, ReqHdrs>;
	if constexpr (ZuTypeIn<Value, Values>{})
	  request.template header<Key, Value>(section);
	else {
	  auto fixed = Value{}();
	  auto value = ZtScratch(HeaderScratch, fixed.length());
	  value = fixed;
	  ZuSpan<uint8_t> stable;
	  if (!request.retainHeader(value.span(), stable)) {
	    headersValid = false;
	    return;
	  }
	  request.template header<Key>(section, stable);
	}
      } else {
	auto fixed = Value{}();
	auto value = ZtScratch(HeaderScratch, fixed.length());
	value = fixed;
	ZuSpan<uint8_t> stable;
	if (!request.retainHeader(value.span(), stable)) {
	  headersValid = false;
	  return;
	}
	request.header(section, Key{}(), stable);
      }
    });
  }
  template <typename Key>
  void header(Zhttp::FieldSection::T section, ZuSpan<uint8_t> value) {
    u.dispatch([this, section, &value](auto I, auto &request) {
      using ReqHdrs = Policy::template Hdrs<
	typename Union::template Type<I>>;
      using ReqHdrKeys = GetHdrKeys<ReqHdrs>;
      ZuSpan<uint8_t> stable;
      if (!request.retainHeader(value, stable)) {
	headersValid = false;
	return;
      }
      if constexpr (ZuTypeIn<Key, ReqHdrKeys>{})
	request.template header<Key>(section, stable);
      else
	request.header(section, Key{}(), stable);
    });
  }
  void header(Zhttp::FieldSection::T section,
      ZuBSpan key, ZuSpan<uint8_t> value) {
    u.dispatch([this, section, &key, &value](auto, auto &request) {
      ZuBSpan stableKey;
      ZuSpan<uint8_t> stableValue;
      if (!request.retainHeader(key, stableKey) ||
	  !request.retainHeader(value, stableValue)) {
	headersValid = false;
	return;
      }
      request.header(section, stableKey, stableValue);
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
    u.dispatch([link, ok](auto I, auto &request) {
      bool decoded = request.finishBody();
      request.complete(link, Policy::template complete<
	      typename Union::template Type<I>>(request, ok && decoded));
    });
  }

  void reset() { u.null(); inputMax = 0; headersValid = true; }
};

template <typename Catalog_>
struct MResBuilder : public Zhttp::ResBuilder {
  using Catalog = Catalog_;
  using Reqs = Catalog::List;
  using AllResponses = GetAllResponses<Reqs>;
  using HdrCatalog = Catalog::ResHeaders;
  using Headers = HdrCatalog::List;
  static constexpr unsigned HdrBufSize =
    ZuTypeApply<MaxHdrBufSize, AllResponses>{};
  using Union = GetUnion<AllResponses>;

  Union		u;

  template <typename Res, typename Req, typename Object>
  void init(Object *object) {
    ZuAssert((ZuTypeIn<Res, GetResponses<Req>>{}));
    constexpr unsigned J = GetResIndex<Reqs, Res>{};
    auto response = new (u.template new_<J + 1, true>()) Res();
    response->init(object);
  }

  template <typename Req, typename Res>
  void init(Reply<Res> reply) {
    ZuAssert((ZuTypeIn<Res, GetResponses<Req>>{}));
    constexpr unsigned J = GetResIndex<Reqs, Res>{};
    new (u.template new_<J + 1, true>()) Res{ZuMv(reply.codec)};
  }

  template <typename Res, typename Parser, typename Object>
  void init(const Parser &parser, Object *object) {
    ZuSwitch::dispatch<Reqs::N>(parser.u.type() - 1,
	[this, object](auto I) {
      using Req = ZuType<I, Reqs>;
      if constexpr (ZuTypeIn<Res, GetResponses<Req>>{})
	this->template init<Res, Req>(object);
    });
  }

  Zhttp::BodyPolicy::T bodyPolicy() const {
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
      if constexpr (ZuTypeIn<Key, ResHdrKeys>{}) {
	using Values = Zhttp::HeaderValues<Key, ResHdrs>;
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

  void close() {
    u.dispatch([](auto, auto &response) { response.close(); });
  }

  template <typename L> void bodyHdrs(L &&l) const {
    u.cdispatch([&l](auto, const auto &response) {
      response.bodyHdrs(ZuFwd<L>(l));
    });
  }
};

template <typename Req, typename Owner>
class Completion : public ZmNHeap<Completion<Req, Owner>>, public ZmObject {
friend inline ZuStringT<"Zrest.Completion"> ZmNHeapID(Completion *);

  friend Owner;
public:
  Completion(Owner *owner, void *work,
      ZmScheduler *scheduler, unsigned thread) :
    m_owner{owner}, m_work{work},
    m_scheduler{scheduler}, m_thread{thread} { }

  bool live() const {
    return m_owner.load_() && !m_claim.load_();
  }
  bool cancelled() const { return m_cancelled.load_(); }

  ZmRef<SSEProducer> producer() {
    if (!live() || !m_scheduler->invoked(m_thread)) return {};
    auto owner = m_owner.load_();
    return owner ? owner->template producer<Req>(this) :
      ZmRef<SSEProducer>{};
  }

  template <typename Res>
  bool complete(Reply<Res> reply) {
    ZuAssert((ZuTypeIn<Res, GetResponses<Req>>{}));
    if (!m_owner.load_() || m_claim.cmpXch(1, 0)) return false;
    if (m_scheduler->invoked(m_thread))
      return complete_<Res>(ZuMv(reply));
    {
      ZmRef<Completion> self{this};
      m_scheduler->run([self = ZuMv(self), reply = ZuMv(reply)]() mutable {
	(void)self->template complete_<Res>(ZuMv(reply));
      }, m_thread);
    }
    return true;
  }

  void invalidate(bool cancelled = false) {
    if (cancelled) m_cancelled = 1;
    m_owner = nullptr;
    m_work = nullptr;
    m_claim = 2;
  }

private:
  template <typename Res>
  bool complete_(Reply<Res> reply) {
    if (auto owner = m_owner.load_())
      return owner->template complete<Req, Res>(this, ZuMv(reply));
    return false;
  }

  ZmAtomic<Owner *> m_owner;
  void *m_work = nullptr; // owner shard only
  ZmScheduler *m_scheduler = nullptr;
  unsigned m_thread = 0;
  ZmAtomic<unsigned> m_claim = 0;
  ZmAtomic<unsigned> m_cancelled = 0;
};

namespace Server_ {

template <typename Res, typename = void>
struct HasDisconnect : public ZuFalse { };
template <typename Res>
struct HasDisconnect<Res, decltype(
  ZuDeclVal<const Res &>().disconnect(), void())> : public ZuTrue { };

template <typename App, typename Arg, typename = void>
struct HasConnected : public ZuFalse { };
template <typename App, typename Arg>
struct HasConnected<App, Arg, decltype(
  ZuDeclVal<App &>().connected(ZuDeclVal<Arg>()), void())> : public ZuTrue { };
template <typename App, typename Arg, typename = void>
struct HasDisconnected : public ZuFalse { };
template <typename App, typename Arg>
struct HasDisconnected<App, Arg, decltype(
  ZuDeclVal<App &>().disconnected(ZuDeclVal<Arg>()), void())> : public ZuTrue { };
template <typename App, typename = void>
struct HasResponseComplete : public ZuFalse { };
template <typename App>
struct HasResponseComplete<App, decltype(
  ZuDeclVal<App &>().responseComplete(), void())> : public ZuTrue { };

template <typename Catalog>
struct ResBuilder : public ZmObject, public MResBuilder<Catalog> {
  using Base = MResBuilder<Catalog>;

  ~ResBuilder() { completed({Zhttp::ResponseCode::Discarded}); }

  template <typename Req, typename Res, typename Work>
  void init(Reply<Res> reply, Zhttp::Method::T method, Work *work) {
    Base::template init<Req>(ZuMv(reply));
    m_method = method;
    m_work = work;
    ZmREF(work);
    m_done = [](void *ptr, const void *owner,
	const Zhttp::ResponseResult &result) {
      auto work = static_cast<Work *>(ptr);
      try { work->retired(result); } catch (...) { }
#ifdef ZmObject_DEBUG
      ZmRef__::ZmDEREF_(work, owner);
#else
      ZmRef__::ZmDEREF_(work);
#endif
    };
  }

  Zhttp::Method::T method() const { return m_method; }
  bool disconnect() const {
    return this->u.cdispatch([](auto, const auto &response) {
      using Res = ZuDecay<decltype(response)>;
      if constexpr (HasDisconnect<Res>{}) return response.disconnect();
      else return false;
    });
  }
  void close() { Base::close(); }
  void completed(const Zhttp::ResponseResult &result) {
    if (!m_work) return;
    auto work = m_work;
    m_work = nullptr;
    m_done(work, this, result);
  }

private:
  void *m_work = nullptr;
  void (*m_done)(void *, const void *,
    const Zhttp::ResponseResult &) = nullptr;
  Zhttp::Method::T m_method = Zhttp::Method::GET;
};

template <typename Catalog>
using ResQ = Zhttp::ResponseQ<ResBuilder<Catalog>,
  ZuStringT<"Zrest.Server.Response">>;

template <typename Owner, typename Catalog, typename Response>
struct Work : public ZmObject {
  using Reqs = Catalog::List;
  using Union = GetUnion<Reqs>;

  Work(Owner *owner, ZmScheduler *scheduler, unsigned thread) :
    m_owner{owner}, m_scheduler{scheduler}, m_thread{thread} { }
  ~Work() { releaseToken_(); }

  template <typename Req>
  void init(Req &&request, RequestContext context,
      Zhttp::Route<Response> route) {
    ZuAssert((ZuTypeIn<ZuDecay<Req>, Reqs>{}));
    new (u.template new_<ZuDecay<Req>, true>()) ZuDecay<Req>{ZuMv(request)};
    m_context = ZuMv(context);
    m_route = ZuMv(route);
  }

  template <typename App>
  void dispatch(App *app) {
    u.dispatch([this, app](auto I, auto &request) {
      using Req = Union::template Type<I>;
      using Token = Completion<Req, Owner>;
      ZmRef<Token> token = new Token{
	m_owner.load_(), this, m_scheduler, m_thread};
      ZmMVREF(token.ptr(), &token, this);
      m_token = ZuMv(token).release();
      m_tokenRelease = [](void *ptr, const void *owner) {
#ifdef ZmObject_DEBUG
	ZmRef__::ZmDEREF_(static_cast<Token *>(ptr), owner);
#else
	ZmRef__::ZmDEREF_(static_cast<Token *>(ptr));
#endif
      };
      m_tokenInvalidate = [](void *ptr, bool cancelled) {
	static_cast<Token *>(ptr)->invalidate(cancelled);
      };
      app->request(static_cast<Req *>(nullptr),
	static_cast<const Req &>(request), m_context,
	ZmRef<Token>{static_cast<Token *>(m_token)});
    });
  }

  template <typename Req, typename Res>
  bool complete(Completion<Req, Owner> *token, Reply<Res> reply) {
    ZuAssert((ZuTypeIn<Res, GetResponses<Req>>{}));
    if (m_selected || !m_route || m_token != token ||
	!u.template is<Req>()) return false;
    if constexpr (Res::Body == BodyPolicy::SSE) {
      if (!m_producer ||
	  reply.codec.producer.ptr() != m_producer.ptr()) return false;
    } else if (m_producer) m_producer->close();
    m_terminal = reply.codec.terminal_;
    m_selected = true;
    m_tokenInvalidate(m_token, false);
    releaseToken_();
    ZmRef<Response> response = new Response{};
    response->template init<Req>(ZuMv(reply), m_route.method(),
      static_cast<Owner::WorkNode *>(this));
    m_route.send(ZuMv(response));
    return true;
  }

  template <typename App>
  bool cancel(App *app, ZuCSpan reason) {
    if (m_cancelled) return false;
    m_cancelled = true;
    if (m_producer) m_producer->close();
    if (m_token) {
      m_tokenInvalidate(m_token, true);
      try {
	u.dispatch([this, app, reason](auto I, auto &) {
	  using Req = Union::template Type<I>;
	  using Token = Completion<Req, Owner>;
	  cancelled_(app, static_cast<Req *>(nullptr),
	    static_cast<Token *>(m_token), reason, 0);
	});
      } catch (...) { }
      releaseToken_();
    }
    if (m_selected) return false;
    m_route.close();
    m_route.reset();
    return true;
  }

  void retired(const Zhttp::ResponseResult &result) {
    if (m_scheduler->invoked(m_thread)) {
      if (auto owner = m_owner.load_()) owner->retired(this, result);
    } else {
      using Node = Owner::WorkNode;
      ZmRef<Node> self{static_cast<Node *>(this)};
      m_scheduler->run([self = ZuMv(self), result]() mutable {
	if (auto owner = self->m_owner.load_())
	  owner->retired(self.ptr(), result);
      }, m_thread);
    }
  }

  void detach() {
    if (m_producer) { m_producer->close(); m_producer = nullptr; }
    if (m_token) {
      m_tokenInvalidate(m_token, true);
      releaseToken_();
    }
    m_owner = nullptr;
    m_route.reset();
  }

  bool selected() const { return m_selected; }
  bool terminal() const { return m_terminal; }
  bool cancelled() const { return m_cancelled; }
  ZmRef<SSEProducer> producer(ZmScheduler *scheduler, unsigned thread,
      Limits limits, ZmAtomic<unsigned> *posts) {
    if (m_selected || m_cancelled) return {};
    if (!m_producer)
      m_producer = new SSEProducer{scheduler, thread, limits, posts};
    return m_producer;
  }
  Zhttp::Session session() const { return m_route.session(); }
  const RequestContext &context() const { return m_context; }
  Union u;

private:
  template <typename App, typename Req, typename Token>
  static auto cancelled_(App *app, Req *req, Token *token,
      ZuCSpan reason, int) ->
      decltype(app->cancelled(req, token, reason), void()) {
    app->cancelled(req, token, reason);
  }
  template <typename App, typename Req, typename Token>
  static void cancelled_(App *, Req *, Token *, ZuCSpan, long) { }

  void releaseToken_() {
    if (!m_token) return;
    auto ptr = m_token;
    m_token = nullptr;
    m_tokenRelease(ptr, this);
  }

  ZmAtomic<Owner *> m_owner;
  ZmScheduler *m_scheduler = nullptr;
  unsigned m_thread = 0;
  RequestContext m_context;
  Zhttp::Route<Response> m_route;
  ZmRef<SSEProducer> m_producer;
  void *m_token = nullptr;
  void (*m_tokenRelease)(void *, const void *) = nullptr;
  void (*m_tokenInvalidate)(void *, bool) = nullptr;
  bool m_selected = false;
  bool m_cancelled = false;
  bool m_terminal = false;
};

template <typename Owner, typename Catalog, typename Response>
using WorkQ = ZmList<Work<Owner, Catalog, Response>,
  ZmListNode<Work<Owner, Catalog, Response>,
    ZmListHeapID<"Zrest.Server.Work">>>;

template <typename Owner, typename Decoder, typename Catalog,
    typename Response>
struct Parser : public Decoder {
  using Base = Decoder;
  using HdrCatalog = Decoder::HdrCatalog;

  void init(Owner &owner) {
    m_owner = &owner;
    m_context = {};
    m_method = Zhttp::Method::GET;
    Base::init();
  }

  bool operation(Zhttp::Method::T method, Zhttp::Target &target) {
    m_method = method;
    m_targetScheme = target.scheme;
    m_context.target = target.path;
    if (target.authority.host) m_context.authority << target.authority;
    ZuBSpan root;
    ZuSpan<uint8_t> suffix;
    if (splitRoot(target.path, root, suffix)) m_context.root = root;
    return Base::operation(method, target);
  }

  template <typename Key, typename Value>
  void header(Zhttp::FieldSection::T section) {
    Base::template header<Key, Value>(section);
  }
  template <typename Key>
  void header(Zhttp::FieldSection::T section, ZuSpan<uint8_t> value) {
    if constexpr (Key{}() == "host")
      if (!m_context.authority) m_context.authority = value;
    Base::template header<Key>(section, value);
  }
  void header(Zhttp::FieldSection::T section,
      ZuBSpan key, ZuSpan<uint8_t> value) {
    if (key == "host" && !m_context.authority)
      m_context.authority = value;
    Base::header(section, key, value);
  }

  template <typename Link>
  void complete(Link *link, bool ok) {
    if (!this->u.type()) { if (link) link->disconnect(); return; }
    m_context.scheme = link->session().transport != Zhttp::Transport::TCP ?
      Zhttp::Scheme::https : Zhttp::Scheme::http;
    bool authorityOK = false;
    if (m_context.authority) {
      Zhttp::AuthorityView parsed;
      ZuSpan<uint8_t> value = m_context.authority.span();
      auto error = Zhttp::parseAuthority(parsed, value, 0,
	Zhttp::Scheme::defltPort(m_context.scheme), false);
      authorityOK = error.ok();
      if (authorityOK) {
	ContextString normalized;
	normalized << parsed;
	m_context.authority = ZuMv(normalized);
      }
    }
    m_context.decoded = ok && this->headersValid && this->finishBody() &&
      (m_targetScheme < 0 || m_targetScheme == m_context.scheme) &&
      authorityOK;
    if (!m_context.decoded) { link->disconnect(); return; }
    auto accept = [this, link](auto &request) {
      m_owner->admit(ZuMv(request), ZuMv(m_context),
	Zhttp::Route<Response>{link, m_method});
    };
    visit_(*this, accept);
  }

  void reset() {
    Base::reset();
    m_context = {};
    m_owner = nullptr;
  }

private:
  template <typename Node, typename L>
  static void visit_(Node &node, L &l) {
    node.u.dispatch([&l](auto I, auto &selected) {
      using Selected = Node::Union::template Type<I>;
      if constexpr (ZuTypeIn<Selected, typename Catalog::List>{})
	l(selected);
      else visit_(selected, l);
    });
  }

  Owner *m_owner = nullptr;
  RequestContext m_context;
  Zhttp::Method::T m_method = Zhttp::Method::GET;
  Zhttp::Scheme::T m_targetScheme = -1;
};

} // Server_

template <typename App, typename Catalog, typename Decoder, typename Selection>
class Server : public Zhttp::Server<Server<App, Catalog, Decoder, Selection>,
    Server_::Parser<Server<App, Catalog, Decoder, Selection>, Decoder, Catalog,
      typename Server_::ResQ<Catalog>::Node>,
    Server_::ResQ<Catalog>, Selection> {
  using Self = Server<App, Catalog, Decoder, Selection>;
  using ResQ = Server_::ResQ<Catalog>;
  using Parser = Server_::Parser<Self, Decoder, Catalog,
    typename ResQ::Node>;
  using HTTP = Zhttp::Server<Self, Parser, ResQ, Selection>;
public:
  using Response = ResQ::Node;
  using WorkQ = Server_::WorkQ<Self, Catalog, Response>;
  using Work = WorkQ::T;
  using WorkNode = WorkQ::Node;
  using HTTP::start;
  using HTTP::stop;
  using HTTP::connected;
  using HTTP::disconnected;

  ~Server() { final(); }

  bool init(const Zhttp::HubConfig &hub, ServerConfig config, App *app) {
    if (m_mx || !app || !hub.mx() || !valid(config.limits())) return false;
    m_limits = config.limits();
    if (!HTTP::init(hub, ZuMv(config))) {
      HTTP::final();
      return false;
    }
    m_mx = hub.mx();
    m_owner = hub.rxThread() ? m_mx->sid(hub.rxThread()) : m_mx->rxThread();
    m_tx = hub.txThread() ? m_mx->sid(hub.txThread()) : m_mx->txThread();
    m_app = app;
    m_up = 0;
    m_stopping = false;
    m_done = false;
    m_stopOK = true;
    m_ssePosts = 0;
    m_stopFns.clear();
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

  template <typename Req>
  void admit(Req &&request, RequestContext context,
      Zhttp::Route<Response> route) {
    if (!m_up || m_work.count_() >= m_limits.maxPending) {
      route.close();
      return;
    }
    ZmRef<WorkNode> work = new WorkNode{this, m_mx, m_owner};
    work->init(ZuMv(request), ZuMv(context), ZuMv(route));
    m_work.pushNode(work);
    try { work->dispatch(m_app); }
    catch (...) {
      if (work->cancel(m_app, ZuCSpan{"application"}))
	retired(work.ptr(), {Zhttp::ResponseCode::Discarded});
    }
  }

  template <typename Req, typename Res>
  bool complete(Completion<Req, Self> *token, Reply<Res> reply) {
    if (!token || !token->m_work) return false;
    auto work = static_cast<Work *>(token->m_work);
    if (work->template complete<Req, Res>(token, ZuMv(reply))) return true;
    if (work->cancel(m_app, ZuCSpan{"reply"}))
      retired(work, {Zhttp::ResponseCode::Discarded});
    return false;
  }

  template <typename Req>
  ZmRef<SSEProducer> producer(Completion<Req, Self> *token) {
    if (!token || !token->m_work || !m_up) return {};
    auto work = static_cast<Work *>(token->m_work);
    return work->producer(m_mx, m_tx, m_limits, &m_ssePosts);
  }

  void retired(Work *work, const Zhttp::ResponseResult &) {
    if (!work) return;
    bool terminal = work->terminal();
    auto node = static_cast<WorkNode *>(work);
    node->detach();
    (void)m_work.delNode(node);
    if constexpr (Server_::HasResponseComplete<App>{})
      if (terminal && m_app) m_app->responseComplete();
  }

  bool stop() {
    return ZmBlock<bool>{}([this](auto wake) mutable {
      this->stop(ZuMv(wake));
    });
  }
  template <typename Done>
  void stop(Done &&done) {
    if (!m_mx) { done(true); return; }
    if (m_done) { done(m_stopOK); return; }
    m_up = 0;
    StopFn stopFn{ZuFwd<Done>(done)};
    m_mx->run([this, stopFn = ZuMv(stopFn)]() mutable {
      if (m_done) { stopFn(m_stopOK); return; }
      m_stopFns.push(ZuMv(stopFn));
      if (m_stopping) return;
      m_stopping = true;
      cancelAll_();
    }, m_owner);
  }
  void final() {
    if (!m_mx) return;
    if (!m_done) (void)stop();
    HTTP::final();
    m_mx = nullptr;
    m_app = nullptr;
  }
  bool up() const { return m_up.load_(); }
  const Limits &limits() const { return m_limits; }

  void listening(int8_t transport, uint16_t port) {
    post_([this, transport, port]() {
      if (m_app) m_app->listening(transport, port);
    });
  }
  void listenFailed(int8_t transport, bool transient) {
    post_([this, transport, transient]() {
      if (m_app) m_app->listenFailed(transport, transient);
    });
  }
  void connected(int8_t transport) {
    post_([this, transport]() {
      if (m_app) m_app->connected(transport);
    });
  }
  void disconnected(int8_t transport) {
    post_([this, transport]() {
      if (m_app) m_app->disconnected(transport);
    });
  }
  void connected(Zhttp::Session session) {
    if constexpr (Server_::HasConnected<App, Zhttp::Session>{})
      post_([this, session]() {
	if (m_app) m_app->connected(session);
      });
  }
  void disconnected(Zhttp::Session session) {
    post_([this, session]() {
      cancelSession_(session);
      if constexpr (Server_::HasDisconnected<App, Zhttp::Session>{})
	if (m_app) m_app->disconnected(session);
    });
  }

private:
  template <typename L>
  void post_(L &&l) {
    if (!m_mx) return;
    if (m_mx->invoked(m_owner)) l();
    else m_mx->run(ZuFwd<L>(l), m_owner);
  }

  template <typename Match>
  bool cancelBatch_(Match &&match, ZuCSpan reason) {
    ZtArray<ZmRef<WorkNode>,
      ZtArrayHeapID<"Zrest.Server.WorkSnap">> work;
    {
      auto iter = m_work.iter();
      while (auto node = iter()) {
	if (node->cancelled() || !match(node)) continue;
	work.push(ZmRef<WorkNode>{node});
	if (work.length() >= m_limits.workBatch) break;
      }
    }
    for (auto &node : work)
      if (node->cancel(m_app, reason))
	retired(node.ptr(), {Zhttp::ResponseCode::Discarded});
    return bool(work.length());
  }

  void cancelAll_() {
    if (cancelBatch_([](WorkNode *) { return true; },
	ZuCSpan{"stopped"})) {
      m_mx->run([this]() { cancelAll_(); }, m_owner);
      return;
    }
    HTTP::stop([this](bool ok) {
      m_mx->run([this, ok]() {
	m_stopOK = ok;
	drain_();
      }, m_owner);
    });
  }
  void cancelSession_(Zhttp::Session session) {
    if (cancelBatch_([session](WorkNode *work) {
	return work->session() == session;
      }, ZuCSpan{"route"}))
      m_mx->run([this, session]() { cancelSession_(session); }, m_owner);
  }
  void drain_() {
    if (m_work.empty_() && !m_ssePosts.load_()) {
      m_done = true;
      bool ok = m_stopOK;
      auto stopFns = ZuMv(m_stopFns);
      for (auto &done : stopFns) {
	try { done(ok); } catch (...) { }
	// ZtArray relocates ZmFn without destroying its elements.
	done = {};
      }
      return;
    }
    m_mx->run([this]() {
      drain_();
    }, m_owner);
  }

  using StopFn = ZmFn<void(bool), ZmFnHeapID<"Zrest.Server.Stop">>;
  ZiMultiplex *m_mx = nullptr;
  App *m_app = nullptr;
  Limits m_limits;
  WorkQ m_work;
  ZtArray<StopFn> m_stopFns;
  unsigned m_owner = 0;
  unsigned m_tx = 0;
  ZmAtomic<unsigned> m_up = 0;
  ZmAtomic<unsigned> m_ssePosts = 0;
  bool m_stopping = false;
  ZmAtomic<unsigned> m_done = 0;
  bool m_stopOK = true;
};

} // Zrest

#endif /* ZrestServer_HH */
