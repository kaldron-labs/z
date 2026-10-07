//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// JSON-RPC wire declarations and codecs

#ifndef Zjrpc_HH
#define Zjrpc_HH

#ifndef ZjrpcLib_HH
#include <zlib/ZjrpcLib.hh>
#endif

#include <stdint.h>
#include <limits.h>

#include <zlib/ZuAssert.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZuUnion.hh>
#include <zlib/ZuUnroll.hh>
#include <zlib/Zu_ntoa.hh>
#include <zlib/Zu_aton.hh>

#include <zlib/ZmObject.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZtEnum.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZfJSON.hh>

namespace Zjrpc {

using JSONRPCVersion = ZuStringT<"2.0">;

// Defaults bound one indivisible protocol turn.  Applications may tune down or
// up explicitly; the message limit also bounds one unpaginated response.
// Preserve the existing MCP message, queue and scheduling budgets on extraction.
namespace Default {
  enum {
    MaxJSONBytes = 16U << 20,
    MaxLineBytes = MaxJSONBytes,
    MaxSSELineBytes = 64U << 10,
    MaxSSEEventBytes = MaxJSONBytes,
    MaxPending = 1U << 12,
    // A bounded recent-use window, independent of live work admission.
    HistSize = 1U << 10,
    // SSE framing plus the decimal event sequence; 64 exceeds the 33-byte
    // maximum currently emitted around one JSON payload.
    SSEFrameOverhead = 64,
    MaxQueue = 1U << 10,
    MaxQueueBytes = 64U << 20,
    WorkBatch = 64
  };
}

struct Limits {
  unsigned maxJSONBytes = Default::MaxJSONBytes;
  unsigned maxLineBytes = Default::MaxLineBytes;
  unsigned maxSSELineBytes = Default::MaxSSELineBytes;
  unsigned maxSSEEventBytes = Default::MaxSSEEventBytes;
  unsigned maxPending = Default::MaxPending;
  unsigned maxQueue = Default::MaxQueue;
  unsigned maxQueueBytes = Default::MaxQueueBytes;
  unsigned workBatch = Default::WorkBatch;
};

inline bool valid(const Limits &limits)
{
  return limits.maxJSONBytes && limits.maxLineBytes &&
    limits.maxSSELineBytes && limits.maxSSEEventBytes &&
    limits.maxPending && limits.maxQueue &&
    limits.maxQueueBytes && limits.workBatch;
}

ZtEnumNS(ZjrpcAPI, BodyPolicy, int8_t, Fixed, SSE);
ZtEnumNS(ZjrpcAPI, MessageKind, int8_t,
  Unusable, Request, Notification, Result, Error);

namespace ErrorCode {
  enum {
    Parse = -32700, InvalidRequest = -32600, MethodNotFound = -32601,
    InvalidParams = -32602, Internal = -32603, Server = -32000
  };
}

struct EmptyObject;

struct Request {
  using Headers = ZuTypeList<>;
  using Object = EmptyObject;
  using Method = void;
  using Responses = ZuTypeList<>;
  // Set when deferred handlers/results retain views into the decoded input.
  // Owned parameter types need no retained buffer/tree after dispatch.
  enum { ResponseBody = BodyPolicy::Fixed, Borrowed = 0 };
};

struct Response {
  using Headers = ZuTypeList<>;
  using Description = void;
  using Body = void;
};

template <typename U> using GetResponses = typename U::Responses;
ZuDerive(IDString, (ZtString<ZtStringHeapID<"Zjrpc.ID">>));
ZuDerive(ErrorString, (ZtString<ZtStringHeapID<"Zjrpc.Error">>));

struct IDFmt;

struct Null {
  bool operator !() const { return false; }
  bool operator ==(const Null &) const { return true; }
  int cmp(const Null &) const { return 0; }
  uint32_t hash() const { return 0; }
};

ZuDerive(ID, (ZuUnion<void, Null, int64_t, IDString>));

struct IDFmt {
  template <typename O, typename Facet>
  struct Handler {
    using Props = ZuTypeList<>;

    const ZfJSON::AnyNode *node;
    int64_t number = 0;
    bool		valid = false;

    template <template <typename> class Filter, typename S>
    static void save(S &s, const O &id) {
      if (id.template is<void>()) { s << "null"; return; }
      id.cdispatch([&s](auto I, const auto &value) {
	if constexpr (I == ID::Index<Null>{})
	  s << "null";
	else if constexpr (I == ID::Index<int64_t>{})
	  ZfJSON::saveValue<Facet, Filter,
	    ZfFieldTC::Int64, ZuTypeList<ZuFieldProp::JSON::Opt>>(s, value);
	else if constexpr (I == ID::Index<IDString>{})
	  ZfJSON::saveValue<Facet, Filter,
	    ZfFieldTC::String, Props>(s, value);
      });
    }

    Handler(const ZfJSON::AnyNode *node_) : node{node_} {
      if (!node) return;
      if (node->template has<ZfJSON::AnyNode::Number>()) {
	auto token = node->template data<ZfJSON::AnyNode::Number>();
	if (token.length() > Zu_ilen<int64_t>()) return;
	int128_t value;
	if (Zu_atoi(value, token.data(), token.length()) != token.length()) return;
	if (value < INT64_MIN || value > INT64_MAX) return;
	number = int64_t(value);
	valid = true;
      } else {
	valid = node->template has<ZfJSON::AnyNode::Null>() ||
	  node->template has<ZfJSON::AnyNode::String>();
      }
    }
    O ctor() const {
      if (!node) return {};
      if (node->template has<ZfJSON::AnyNode::String>())
	return IDString{node->template data<ZfJSON::AnyNode::String>()};
      if (node->template has<ZfJSON::AnyNode::Number>())
	return number;
      if (node->template has<ZfJSON::AnyNode::Null>()) return Null{};
      return {};
    }
    O *alloc() const { return new O{ctor()}; }
    void new_(void *p) const { new (p) O{ctor()}; }
    void load(O &id) const { id = ctor(); }
    void update(O &id) const { id = ctor(); }
  };
};

IDFmt ZfJSON_Fmt(ID *);

struct Error {
  ErrorString message;
  int code = ErrorCode::Server;
  // Arbitrary error data: an optional borrowed JSON node, no typed alternatives.
  ZfJSON::Union<> data;
};
ZfStruct(ZjrpcAPI, (Error, JSON),
  (code, (Mutable),		Int32),
  (message, (Mutable),		String),
  (data, (Mutable, JSON::Opt),	UDT));

struct EmptyObject {
  explicit operator bool() const { return true; }
  friend inline ZfJSON::AsObject ZfJSON_Fmt(EmptyObject *) { return {}; }
};
ZfStruct(ZjrpcAPI, (EmptyObject, JSON));

template <typename Object, bool = ZuIsBase<Object, ZmObject>{}>
struct ObjectValue_ { using T = Object; };
template <typename Object>
struct ObjectValue_<Object, true> { using T = ZmRef<Object>; };
template <typename Object> using ObjectValue = typename ObjectValue_<Object>::T;

template <typename Res, bool = ZuIsSame<void, typename Res::Body>{}>
struct Reply_ {
  using Response = Res;
  using Body = typename Res::Body;
  using Value = ObjectValue<Body>;

  Value body;

  Reply_(Value body_) : body{ZuMv(body_)} { }

  const Body &bodyObject() const {
    if constexpr (ZuIsBase<Body, ZmObject>{}) return *body;
    else return body;
  }
};
template <typename Res>
struct Reply_<Res, true> {
  using Response = Res;
  using Body = void;
};
template <typename Res> using Reply = Reply_<Res>;

template <typename ID_>
consteval bool validName()
{
  if constexpr (ZuIsSame<void, ID_>{}) return false;
  else return bool(ID_{}().length());
}

template <typename U>
using ResponseBody = typename U::Body;

template <typename U>
using NonVoid = ZuBool<!ZuIsSame<U, void>{}>;

template <typename Req>
using ReplyBodies = ZuTypeUnique<ZuTypeGrep<
  NonVoid, ZuTypeMap<ResponseBody, GetResponses<Req>>>>;

// Alternatives come only from the request's compile-time response declarations.
template <typename Req>
using ReplyData = ZuTypeApply<ZfJSON::Union, ReplyBodies<Req>>;

inline const ZfJSON::AnyNode *member(
    const ZfJSON::AnyNode *node, ZuCSpan name)
{
  if (!node || !node->template has<ZfJSON::AnyNode::Object>()) return nullptr;
  const auto &fields = node->template data<ZfJSON::AnyNode::Object>();
  for (unsigned i = 0, n = fields.length(); i < n; ++i) {
    const auto &field = fields[i];
    if (field.template p<0>() == name) return field.template p<1>();
  }
  return nullptr;
}

inline Error loadError(const ZfJSON::AnyNode *node)
{
  if (!node || !node->template has<ZfJSON::AnyNode::Object>()) return {};
  auto handler = ZfJSON::handler<Error>(node);
  if (!handler.valid) return {};
  return handler.ctor();
}

template <typename O>
inline O loadObject(const ZfJSON::AnyNode *node)
{
  if (!node || !node->template has<ZfJSON::AnyNode::Object>()) return {};
  auto handler = ZfJSON::handler<O>(node);
  if (!handler.valid) return {};
  return handler.ctor();
}

// JSON-RPC permits both named and positional parameters. Positional fields use
// declaration order; individual values still use the ordinary ZfJSON loaders.
template <typename O>
struct ParamArray {
  using Metadata = ZfJSON::AsObject::Handler<O, ZuFacet::JSON>;
  using Fields = typename Metadata::LoadFields;
  const ZfJSON::AnyNode *node;

  template <typename Field>
  auto load() const {
    enum { I = ZuTypeIndex<Field, Fields>{} };
    using Props = typename Field::Props;
    using T = typename Field::T;
    enum { Code = Field::Type::Code };
    const auto &values = node->data<ZfJSON::AnyNode::Array>();
    using R = decltype(ZfJSON::loadValue<ZuFacet::JSON,
      ZfFieldFilter::Load, Code, Props, T>(nullptr));
    if (I < values.length())
      return ZfJSON::loadValue<ZuFacet::JSON,
	ZfFieldFilter::Load, Code, Props, T>(values[I].ptr());
    if constexpr (ZfFieldTC::IsVec<Code>{}) {
      static const ZfJSON::NodeArray empty;
      return R(empty);
    } else {
      return R{Field::deflt()};
    }
  }

  template <typename ...Field>
  struct Ctor {
    static O load(const ParamArray &params) {
      return O(params.template load<Field>()...);
    }
  };
  O ctor() const {
    O object = ZuTypeApply<Ctor, typename Metadata::CtorFields>::load(*this);
    ZuUnroll::all<typename Metadata::InitFields>([this, &object]<typename Field>() {
      Field::set(object, this->template load<Field>());
    });
    return object;
  }
};

template <typename O, typename L>
bool loadParams(const ZfJSON::AnyNode *node, L &&consume)
{
  using Handler = typename ZfJSON::As<O>::template Handler<O, ZuFacet::JSON>;
  if constexpr (ZuIsSame<Handler, ZfJSON::AsObject::Handler<O, ZuFacet::JSON>>{}) {
    if (!node && !ZuFields<O, ZuFacet::JSON>::N) {
      consume(O{});
      return true;
    }
    if (node && node->has<ZfJSON::AnyNode::Array>()) {
      consume(ParamArray<O>{node}.ctor());
      return true;
    }
  }
  auto handler = ZfJSON::handler<O>(node);
  if (!handler.valid) return false;
  consume(handler.ctor());
  return true;
}

template <typename ...Ts>
inline const ZfJSON::AnyNode *raw(const ZfJSON::Union<Ts...> &object)
{
  if (!object.template is<const ZfJSON::AnyNode *>()) return nullptr;
  return object.template p<const ZfJSON::AnyNode *>();
}

struct RequestShape {
  ZuCSpan jsonrpc;
  ID id;
  ZuCSpan method;
  EmptyObject params;
};
ZfStruct(ZjrpcAPI, (RequestShape, JSON),
  (jsonrpc, (Mutable),		String),
  (id, (Mutable),		UDT),
  (method, (Mutable),		String),
  (params, (Mutable),		UDT));

template <typename Params>
struct RequestView : public ZuStructShim<RequestView<Params>, RequestShape> {
  const ID &id_;
  ZuCSpan method_;
  const Params &params_;

  RequestView(const ID &id, ZuCSpan method, const Params &params) :
    id_{id}, method_{method}, params_{params} { }

  ZuCSpan jsonrpc() const { return JSONRPCVersion{}(); }
  const ID &id() const { return id_; }
  ZuCSpan method() const { return method_; }
  const Params &params() const { return params_; }
};

struct NotificationShape {
  ZuCSpan jsonrpc;
  ZuCSpan method;
  EmptyObject params;
};
ZfStruct(ZjrpcAPI, (NotificationShape, JSON),
  (jsonrpc, (Mutable),		String),
  (method, (Mutable),		String),
  (params, (Mutable),		UDT));

template <typename Params>
struct NotificationView :
    public ZuStructShim<NotificationView<Params>, NotificationShape> {
  ZuCSpan method_;
  const Params &params_;

  NotificationView(ZuCSpan method, const Params &params) :
    method_{method}, params_{params} { }

  ZuCSpan jsonrpc() const { return JSONRPCVersion{}(); }
  ZuCSpan method() const { return method_; }
  const Params &params() const { return params_; }
};

struct ResultShape {
  ZuCSpan jsonrpc;
  ID id;
  EmptyObject result;
};
ZfStruct(ZjrpcAPI, (ResultShape, JSON),
  (jsonrpc, (Mutable),		String),
  (id, (Mutable),		UDT),
  (result, (Mutable),		UDT));

template <typename Result>
struct ResultView : public ZuStructShim<ResultView<Result>, ResultShape> {
  const ID &id_;
  const Result &result_;

  ResultView(const ID &id, const Result &result) :
    id_{id}, result_{result} { }

  ZuCSpan jsonrpc() const { return JSONRPCVersion{}(); }
  const ID &id() const { return id_; }
  const Result &result() const { return result_; }
};

struct ErrorShape {
  ZuCSpan jsonrpc;
  ID id;
  Error error;
};
ZfStruct(ZjrpcAPI, (ErrorShape, JSON),
  (jsonrpc, (Mutable),		String),
  (id, (Mutable),		UDT),
  (error, (Mutable),		UDT));

struct ErrorView : public ZuStructShim<ErrorView, ErrorShape> {
  const ID &id_;
  const Error &error_;

  ErrorView(const ID &id, const Error &error) :
    id_{id}, error_{error} { }

  ZuCSpan jsonrpc() const { return JSONRPCVersion{}(); }
  const ID &id() const { return id_; }
  const Error &error() const { return error_; }
};

template <typename Impl, typename Shape>
struct StructModel {
  template <typename Base>
  struct Adapter : public Base {
    using Orig = Base;
    template <template <typename> class Override>
    using Adapt = Adapter<Override<Orig>>;
    using O = Impl;
    using T = ZuDecay<decltype(Base::call(ZuDeclVal<const O &>()))>;

    static decltype(auto) get(const O &o) { return Base::call(o); }
    static decltype(auto) get(O &o) { return Base::call(o); }
    static decltype(auto) get(O &&o) { return Base::call(ZuMv(o)); }
    template <typename U>
    static void set(O &o, U &&v) { Base::call(o, ZuFwd<U>(v)); }
  };
  template <typename Field>
  using Map = typename Field::template Adapt<Adapter>;
  template <typename Facet>
  using Fields = ZuTypeMap<Map, ZuFields<Shape, Facet>>;
};

template <typename S, typename Params>
inline void saveRequest(S &s, const ID &id, ZuCSpan method,
    const Params &params)
{
  ZfJSON::save(s, RequestView<Params>{id, method, params});
}

template <typename S, typename Params>
inline void saveNotification(S &s, ZuCSpan method, const Params &params)
{
  ZfJSON::save(s, NotificationView<Params>{method, params});
}

template <typename S, typename Result>
inline void saveResult(S &s, const ID &id, const Result &result)
{
  ZfJSON::save(s, ResultView<Result>{id, result});
}

template <typename S>
inline void saveError(S &s, const ID &id, const Error &error)
{
  ZfJSON::save(s, ErrorView{id, error});
}

template <typename S>
inline void saveError(S &s, const ID &id, int code, ZuCSpan message)
{
  saveError(s, id, Error{ErrorString{message}, code});
}

struct EnvelopeShape {
  ZuCSpan jsonrpc;
  ID id;
  ZuCSpan method;
  ZfJSON::Union<EmptyObject> params;
  ZfJSON::Union<EmptyObject> result;
  ZfJSON::Union<> error;
};
ZfStruct(ZjrpcAPI, (EnvelopeShape, JSON),
  (jsonrpc, (Mutable),		String),
  (id, (Mutable),		UDT),
  (method, (Mutable),		String),
  (params, (Mutable),		UDT),
  (result, (Mutable),		UDT),
  (error, (Mutable),		UDT));

// Payloads remain borrowed JSON nodes until method lookup or pending-call ID
// correlation selects their compile-time decoder. Union<> has no application
// alternatives to dispatch; typed outbound payloads use the views above.
struct Envelope : public StructModel<Envelope, EnvelopeShape> {
  using Base = StructModel<Envelope, EnvelopeShape>;
  using Params = ZfJSON::Union<>;
  using Result = ZfJSON::Union<>;

  ZuCSpan jsonrpc_;
  ID		id_;
  ZuCSpan	method_;
  Params	params_;
  Result	result_;
  ZfJSON::Union<>	error_;
  int		kind = MessageKind::Unusable;

  template <typename Facet>
  friend typename Base::template Fields<Facet> ZuFields_(Envelope *, Facet *);
  friend Envelope ZuStructured_(Envelope *);

  ZuCSpan jsonrpc() const { return jsonrpc_; }
  void jsonrpc(ZuCSpan v) { jsonrpc_ = v; }

  const ID &id() const { return id_; }
  ID &id() { return id_; }
  void id(ID v) { id_ = ZuMv(v); }

  ZuCSpan method() const { return method_; }
  void method(ZuCSpan v) { method_ = v; }

  const Params &params() const { return params_; }
  Params &params() { return params_; }
  void params(Params v) { params_ = ZuMv(v); }

  const Result &result() const { return result_; }
  Result &result() { return result_; }
  void result(Result v) { result_ = ZuMv(v); }

  const ZfJSON::Union<> &error() const { return error_; }
  ZfJSON::Union<> &error() { return error_; }
  void error(ZfJSON::Union<> v) { error_ = ZuMv(v); }


};

inline Envelope decode(const ZfJSON::AnyNode *node)
{
  Envelope out;
  auto handler = ZfJSON::handler<Envelope>(node);
  if (!handler.valid) return out;
  const auto &fields = node->data<ZfJSON::AnyNode::Object>();
  auto value = [&handler, &fields]<typename Field>() -> const ZfJSON::AnyNode * {
    using Adapted = Envelope::Base::Map<Field>;
    enum { I = ZuTypeIndex<Adapted, typename decltype(handler)::SaveFields>{} };
    int index = handler.lookup[I];
    return index < 0 ? nullptr : fields[index].template p<1>().ptr();
  };
  auto version = value.template operator()<ZfField(EnvelopeShape, jsonrpc)>();
  if (!version || !version->has<ZfJSON::AnyNode::String>() ||
      version->data<ZfJSON::AnyNode::String>() != JSONRPCVersion{}())
    return out;
  auto id = value.template operator()<ZfField(EnvelopeShape, id)>();
  auto idHandler = ZfJSON::handler<ID>(id);
  if (id && !idHandler.valid) return out;
  auto method = value.template operator()<ZfField(EnvelopeShape, method)>();
  auto params = value.template operator()<ZfField(EnvelopeShape, params)>();
  auto result = value.template operator()<ZfField(EnvelopeShape, result)>();
  auto error = value.template operator()<ZfField(EnvelopeShape, error)>();
  if (method) {
    if (!method->has<ZfJSON::AnyNode::String>() || result || error ||
	(params && !params->has<ZfJSON::AnyNode::Object>() &&
	  !params->has<ZfJSON::AnyNode::Array>())) return out;
    out.kind = id ? MessageKind::Request : MessageKind::Notification;
  } else {
    if (!id || bool(result) == bool(error)) return out;
    if (error && !error->has<ZfJSON::AnyNode::Object>()) return out;
    out.kind = result ? MessageKind::Result : MessageKind::Error;
  }
  out.jsonrpc(version->data<ZfJSON::AnyNode::String>());
  if (id) out.id(idHandler.ctor());
  if (method) out.method(method->data<ZfJSON::AnyNode::String>());
  if (params) out.params(params);
  if (result) out.result(result);
  if (error) out.error(error);
  return out;
}

struct Parsed {
  ZuPtr<ZfJSON::AnyNode> root;
  Envelope envelope;
  int consumed = -1;

  const ZfJSON::AnyNode *value() const {
    return root ? (*root)[0].ptr() : nullptr;
  }
  bool batch() const {
    auto node = value();
    return node && node->has<ZfJSON::AnyNode::Array>();
  }
  explicit operator bool() const {
    return envelope.kind != MessageKind::Unusable || batch();
  }
  bool close() const { return !operator bool(); }
};

inline Parsed parse(ZuSpan<char> input, unsigned maxBytes)
{
  Parsed out;
  if (ZuUnlikely(input.length() > maxBytes)) return out;
  auto scanned = ZfJSON::scan(input);
  out.consumed = scanned.p<0>();
  out.root = ZuMv(scanned.p<1>());
  if (out.consumed < 0 || !out.root ||
      !out.root->has<ZfJSON::AnyNode::Array>() ||
      out.root->data<ZfJSON::AnyNode::Array>().length() != 1) {
    out.root = nullptr;
    return out;
  }
  out.envelope = decode(out.value());
  return out;
}

} // Zjrpc

#endif /* Zjrpc_HH */
