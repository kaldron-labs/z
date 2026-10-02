//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z MCP library - shared client/server core

#ifndef Zmcp_HH
#define Zmcp_HH

#ifndef ZmcpLib_HH
#include <zlib/ZmcpLib.hh>
#endif

#include <stdint.h>

#include <zlib/ZuAssert.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuHash.hh>
#include <zlib/ZuID.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuStream.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZuUnion.hh>
#include <zlib/ZuUnroll.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmLock.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmScheduler.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZfJSON.hh>

#include <zlib/ZiAssert.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiMultiplex.hh>

#include <zlib/Zhttp.hh>

namespace ZuFieldProp::MCP {

template <ZuString ID_> struct Header { };

template <typename Props, bool = HasValue<Props, Header>{}>
struct GetHeader_ { using T = void; };
template <typename Props>
struct GetHeader_<Props, true> { using T = GetValue<Props, Header>; };
template <typename Props>
using GetHeader = typename GetHeader_<Props>::T;

} // ZuFieldProp::MCP

namespace Zmcp {

template <typename Headers_>
struct HTTPHdrCatalog {
  using List = Headers_;
  static int nameMatch(ZuBSpan key) {
    return Zhttp::Fields::nameMatch<HTTPHdrCatalog>(key);
  }
  static int valueMatch(unsigned key, ZuBSpan value) {
    return Zhttp::Fields::valueMatch<HTTPHdrCatalog>(key, value);
  }
};

using ModernVersion = ZuStringT<"2026-07-28">;
using LegacyVersion = ZuStringT<"2025-11-25">;
using JSONRPCVersion = ZuStringT<"2.0">;

using LogLevels = ZuStringTL<
  "debug", "info", "notice", "warning",
  "error", "critical", "alert", "emergency">;
struct LogLevelIDs { using Keys = LogLevels; };

namespace LogLevel {
  enum { Disabled = -1, Debug, Info, Notice, Warning,
    Error, Critical, Alert, Emergency };
}

inline int logLevel(ZuCSpan name)
{
  constexpr auto matcher = ZuMatcher<LogLevelIDs>();
  return matcher.exact(name);
}

template <typename L>
inline void logLevelName(int level, L &&l)
{
  if (level < LogLevel::Debug || level > LogLevel::Emergency) return;
  ZuSwitch::dispatch<LogLevels::N>(level, [&l](auto I) {
    using Name = ZuType<I, LogLevels>;
    l(Name{}());
  });
}

using ProtocolVersion = ZuStringT<"mcp-protocol-version">;
using SessionID = ZuStringT<"mcp-session-id">;
using MethodHeader = ZuStringT<"mcp-method">;
using NameHeader = ZuStringT<"mcp-name">;
using SessionHeader = ZhttpHeaders("mcp-session-id");
using SessionHeaders = ZhttpHeaders(
  "mcp-protocol-version", "mcp-session-id");
using RoutingHeaders = ZuTypeConcat<SessionHeaders,
  ZhttpHeaders("mcp-method", "mcp-name")>;

template <unsigned Code, typename Props>
using IsParameterHeader = ZuBool<
  !ZuFieldProp::HasEnum<Props>{} && !ZuFieldProp::HasFlags<Props>{} &&
  (Code == ZfFieldTC::CString || Code == ZfFieldTC::String ||
   Code == ZfFieldTC::Bool ||
   Code == ZfFieldTC::Int8 || Code == ZfFieldTC::Int16 ||
   Code == ZfFieldTC::Int32 || Code == ZfFieldTC::Int64 ||
   Code == ZfFieldTC::UInt8 || Code == ZfFieldTC::UInt16 ||
   Code == ZfFieldTC::UInt32 || Code == ZfFieldTC::UInt64)>;

// Stable catalog data is compile-time static and is safe to cache for 24 hours.
enum { CatalogTTL = 86'400'000 };

// Defaults bound one indivisible protocol turn.  Applications may tune down or
// up explicitly, accepting that the configured message limit is also a latency
// bound for an unpaginated tools/list response.
namespace Default {
  enum {
    MaxJSONBytes = 16U << 20,
    MaxLineBytes = MaxJSONBytes,
    MaxSSELineBytes = 64U << 10,
    MaxSSEEventBytes = MaxJSONBytes,
    MaxPending = 1U << 12,
    MaxSessions = 1U << 12,
    SessionIDBytes = 16, // 128 bits from the TLS CSPRNG
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
  unsigned maxSessions = Default::MaxSessions;
  unsigned maxQueue = Default::MaxQueue;
  unsigned maxQueueBytes = Default::MaxQueueBytes;
  unsigned workBatch = Default::WorkBatch;
};

inline bool valid(const Limits &limits)
{
  return limits.maxJSONBytes && limits.maxLineBytes &&
    limits.maxSSELineBytes && limits.maxSSEEventBytes &&
    limits.maxPending && limits.maxSessions && limits.maxQueue &&
    limits.maxQueueBytes && limits.workBatch;
}

struct TransportTag { };
struct SessionTag { };
struct StreamTag { };

// Borrowed application context for one dispatch.  The application knows the
// concrete types returned by its open() overloads; these pointers must not be
// retained beyond the handler call.
class Context {
public:
  Context() = default;
  Context(void *transport_, void *session_, void *stream_ = nullptr) :
    m_transport{transport_}, m_session{session_}, m_stream{stream_} { }

  template <typename T> T *transport() const {
    return static_cast<T *>(m_transport);
  }
  template <typename T> T *session() const {
    return static_cast<T *>(m_session);
  }
  template <typename T> T *stream() const {
    return static_cast<T *>(m_stream);
  }

  void *transport() const { return m_transport; }
  void *session() const { return m_session; }
  void *stream() const { return m_stream; }

private:
  void	*m_transport = nullptr;
  void	*m_session = nullptr;
  void	*m_stream = nullptr;
};

ZtEnumNS(ZmcpAPI, BodyPolicy, int8_t, Fixed, SSE);
ZtEnumNS(ZmcpAPI, Era, int8_t, Unknown, Modern, Legacy);
ZtEnumNS(ZmcpAPI, MessageKind, int8_t,
  Unusable, Request, Notification, Result, Error);

namespace ErrorCode {
  enum {
    Internal = -32000,
    MethodNotFound = -32601
  };
}

ZuDerive(IDString, (ZtString<ZtStringHeapID<"Zmcp.ID">>));
ZuDerive(ErrorString, (ZtString<ZtStringHeapID<"Zmcp.Error">>));

struct IDFmt;

struct ID : public ZuUnion<void, int64_t, IDString> {
  ZuDerive_(ID, (ZuUnion<void, int64_t, IDString>));

  enum { Absent, Integer, String };

  bool absent() const { return type() == Absent; }
  bool integer() const { return type() == Integer; }
  bool string() const { return type() == String; }

  bool equals(const ID &id) const {
    if (type() != id.type()) return false;
    switch (type()) {
      case Integer: return p<int64_t>() == id.p<int64_t>();
      case String: return p<IDString>() == id.p<IDString>();
      default: return true;
    }
  }
  friend bool operator ==(const ID &l, const ID &r) {
    return l.equals(r);
  }
  uint32_t hash() const {
    switch (type()) {
      case Integer: return ZuHash<int64_t>::hash(p<int64_t>());
      case String: return ZuHash<IDString>::hash(p<IDString>());
      default: return 0;
    }
  }

  friend ZuDefaultRDecayer ZuRDecayer(ID *);
  friend IDFmt ZfJSON_Fmt(ID *);
};

struct IDFmt {
  template <typename O, typename Facet>
  struct Handler {
    const ZfJSON::AnyNode *node;
    bool		valid = false;

    template <template <typename> class Filter, typename S>
    static void save(S &s, const O &id) {
      switch (id.type()) {
	case O::Integer:
	  ZfJSON::saveValue<Facet, Filter,
	    ZfFieldTC::Int64, ZuTypeList<>>(s, id.template p<int64_t>());
	  break;
	case O::String:
	  ZfJSON::saveValue<Facet, Filter,
	    ZfFieldTC::String, ZuTypeList<>>(s, id.template p<IDString>());
	  break;
	default: s << "null"; break;
      }
    }

    Handler(const ZfJSON::AnyNode *node_) : node{node_} {
      valid = node && (node->template has<ZfJSON::AnyNode::Null>() ||
	node->template has<ZfJSON::AnyNode::Number>() ||
	node->template has<ZfJSON::AnyNode::String>());
    }
    O ctor() const {
      if (!node) return {};
      if (node->template has<ZfJSON::AnyNode::String>())
	return IDString{node->template data<ZfJSON::AnyNode::String>()};
      if (node->template has<ZfJSON::AnyNode::Number>())
	return ZfJSON::loadValue<Facet, ZfFieldFilter::Load,
	  ZfFieldTC::Int64, ZuTypeList<>, int64_t>(
	    const_cast<ZfJSON::AnyNode *>(node));
      return {};
    }
    O *alloc() const {
      if (!node) return new O{};
      if (node->template has<ZfJSON::AnyNode::String>())
	return new O{IDString{node->template data<ZfJSON::AnyNode::String>()}};
      if (node->template has<ZfJSON::AnyNode::Number>())
	return new O{ZfJSON::loadValue<Facet, ZfFieldFilter::Load,
	  ZfFieldTC::Int64, ZuTypeList<>, int64_t>(
	    const_cast<ZfJSON::AnyNode *>(node))};
      return new O{};
    }
    void new_(void *p) const { new (p) O{ctor()}; }
    void load(O &id) const { id = ctor(); }
    void update(O &id) const { id = ctor(); }
  };
};

inline IDFmt ZfJSON_Fmt(ID *) { return {}; }

struct Error {
  ErrorString message;
  int code = ErrorCode::Internal;
};
ZfStruct(ZmcpAPI, (Error, JSON),
  (((code), (Mutable)),		Int32),
  (((message), (Mutable)),	String));

struct ToolAnnotationsShape {
  bool readOnlyHint = false;
  bool destructiveHint = true;
  bool idempotentHint = false;
  bool openWorldHint = true;
};
ZfStruct(ZmcpAPI, (ToolAnnotationsShape, JSON),
  (((readOnlyHint), (Mutable)),		Bool),
  (((destructiveHint), (Mutable)),	Bool),
  (((idempotentHint), (Mutable)),	Bool),
  (((openWorldHint), (Mutable)),	Bool));

template <bool ReadOnly_, bool Destructive_ = true,
  bool Idempotent_ = false, bool OpenWorld_ = true>
struct ToolAnnotations :
    public ZuStructShim<ToolAnnotations<ReadOnly_, Destructive_,
	Idempotent_, OpenWorld_>, ToolAnnotationsShape> {
  enum {
    ReadOnly = ReadOnly_,
    Destructive = Destructive_,
    Idempotent = ReadOnly_ || Idempotent_,
    OpenWorld = OpenWorld_
  };

  bool readOnlyHint() const { return ReadOnly; }
  bool destructiveHint() const { return Destructive; }
  bool idempotentHint() const { return Idempotent; }
  bool openWorldHint() const { return OpenWorld; }
};

struct Request {
  using Object = void;
  using ToolID = void;
  using OperationID = void;
  using Title = void;
  using Description = void;
  using Annotations = void;
  using Responses = ZuTypeList<>;

  enum { ResponseBody = BodyPolicy::Fixed };
};

struct Response {
  using Description = void;
  using Body = void;
  enum { Status = 200 };
};

struct EmptyObject {
  explicit operator bool() const { return true; }
  friend inline ZfJSON::AsObject ZfJSON_Fmt(EmptyObject *) { return {}; }
};
ZfStruct(ZmcpAPI, (EmptyObject, JSON));

template <typename Object, bool = ZuIsBase<Object, ZmObject>{}>
struct ToolObject_ { using T = Object; };
template <typename Object>
struct ToolObject_<Object, true> { using T = ZmRef<Object>; };
template <typename Object> using ToolObject = typename ToolObject_<Object>::T;

template <typename Res, bool = ZuIsSame<void, typename Res::Body>{}>
struct ToolReply_ {
  using Response = Res;
  using Body = typename Res::Body;
  using Value = ToolObject<Body>;

  Value body;

  ToolReply_(Value body_) : body{ZuMv(body_)} { }

  const Body &bodyObject() const {
    if constexpr (ZuIsBase<Body, ZmObject>{}) return *body;
    else return body;
  }
};
template <typename Res>
struct ToolReply_<Res, true> {
  using Response = Res;
  using Body = void;
};
template <typename Res> using ToolReply = ToolReply_<Res>;

template <typename U> using GetToolID = typename U::ToolID;
template <typename U> using GetOperationID = typename U::OperationID;
template <typename U> using GetTitle = typename U::Title;
template <typename U> using GetDescription = typename U::Description;
template <typename U> using GetAnnotations = typename U::Annotations;
template <typename U> using GetResponses = typename U::Responses;

template <typename Annotations_, typename = void>
struct AnnotationsIdempotent : public ZuFalse { };
template <typename Annotations_>
struct AnnotationsIdempotent<Annotations_,
  decltype(Annotations_::Idempotent, void())> :
  public ZuBool<Annotations_::Idempotent> { };

template <typename Req>
using ToolIdempotent = AnnotationsIdempotent<GetAnnotations<Req>>;

template <typename Res> using GetStatus = ZuInt<Res::Status>;

template <typename ID_>
consteval bool validID()
{
  if constexpr (ZuIsSame<void, ID_>{}) return false;
  else return bool(ID_{}().length());
}

template <typename ID_>
consteval bool validToolID()
{
  return validID<ID_>();
}

template <typename> struct StructuredFmt_ : public ZuFalse { };
template <> struct StructuredFmt_<ZfJSON::AsObject> : public ZuTrue { };
template <unsigned Code, typename Props>
struct StructuredFmt_<ZfJSON::AsArray<Code, Props>> : public ZuTrue { };
template <unsigned Code, typename Props>
struct StructuredFmt_<ZfJSON::AsMap<Code, Props>> : public ZuTrue { };

template <typename T>
using IsStructured = ZuBool<
  bool(ZuFields<T, ZuFacet::JSON>::N) || bool(StructuredFmt_<ZfJSON::As<T>>{})>;

template <typename T>
using IsObjectStructured = ZuBool<
  !ZuIsSame<void, ZuStructured<T>>{} &&
  (!StructuredFmt_<ZfJSON::As<T>>{} ||
    ZuIsSame<ZfJSON::As<T>, ZfJSON::AsObject>{})>;

template <typename Res, typename = void>
struct ResponseValid : public ZuFalse { };
template <typename Res>
struct ResponseValid<Res,
  decltype(Res::Status, ZuDeclVal<typename Res::Body *>(), void())> :
  public ZuBool<
    Res::Status >= 100 && Res::Status <= 599 &&
    (ZuIsSame<void, typename Res::Body>{} ||
      IsStructured<typename Res::Body>{})> { };

template <typename> struct ResponsesValid_;
template <typename ...Res>
struct ResponsesValid_<ZuTypeList<Res...>> {
  using Statuses = ZuTypeList<GetStatus<Res>...>;
  enum {
    Status = (ResponseValid<Res>{} && ...),
    Unique = ZuTypeUnique<Statuses>::N == Statuses::N,
    Valid = sizeof...(Res) && Status && Unique
  };
};

template <typename> struct Contract;
template <typename ...Req>
struct Contract<ZuTypeList<Req...>> {
  using Requests = ZuTypeList<Req...>;
  using ToolIDs = ZuTypeList<GetToolID<Req>...>;
  using Keys = ToolIDs;

  ZuAssert((validToolID<GetToolID<Req>>() && ...));
  ZuAssert((validID<GetOperationID<Req>>() && ...));
  ZuAssert((IsObjectStructured<typename Req::Object>{} && ...));
  ZuAssert((ResponsesValid_<GetResponses<Req>>::Valid && ...));
  ZuAssert(((Req::ResponseBody == BodyPolicy::Fixed ||
	Req::ResponseBody == BodyPolicy::SSE) && ...));
  ZuAssert(ZuTypeUnique<ToolIDs>::N == ToolIDs::N);
};

inline const ZfJSON::AnyNode *member(
    const ZfJSON::AnyNode *node, ZuCSpan name)
{
  if (!node || !node->template has<ZfJSON::AnyNode::Object>()) return nullptr;
  const auto &fields = node->template data<ZfJSON::AnyNode::Object>();
  for (unsigned i = 0, n = fields.length(); i < n; ++i)
    if (fields[i].template p<0>() == name)
      return fields[i].template p<1>();
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
ZfStruct(ZmcpAPI, (RequestShape, JSON),
  (((jsonrpc), (Mutable)),	String),
  (((id), (Mutable)),		UDT),
  (((method), (Mutable)),	String),
  (((params), (Mutable)),	UDT));

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
ZfStruct(ZmcpAPI, (NotificationShape, JSON),
  (((jsonrpc), (Mutable)),	String),
  (((method), (Mutable)),	String),
  (((params), (Mutable)),	UDT));

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
ZfStruct(ZmcpAPI, (ResultShape, JSON),
  (((jsonrpc), (Mutable)),	String),
  (((id), (Mutable)),		UDT),
  (((result), (Mutable)),	UDT));

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
ZfStruct(ZmcpAPI, (ErrorShape, JSON),
  (((jsonrpc), (Mutable)),	String),
  (((id), (Mutable)),		UDT),
  (((error), (Mutable)),	UDT));

struct ErrorView : public ZuStructShim<ErrorView, ErrorShape> {
  const ID &id_;
  Error error_;

  ErrorView(const ID &id, Error error) :
    id_{id}, error_{ZuMv(error)} { }

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
inline void saveError(S &s, const ID &id, int code, ZuCSpan message)
{
  ZfJSON::save(s, ErrorView{id, Error{ErrorString{message}, code}});
}

struct PeerInfo {
  ZuCSpan name;
  ZuCSpan version;

  explicit operator bool() const { return true; }
};
ZfStruct(ZmcpAPI, (PeerInfo, JSON),
  (((name), (Mutable)),		String),
  (((version), (Mutable)),	String));

struct ServerMeta {
  PeerInfo serverInfo;

  explicit operator bool() const { return true; }
};
ZfStruct(ZmcpAPI, (ServerMeta, JSON),
  (((serverInfo),
    (Mutable, JSON::ID<"io.modelcontextprotocol/serverInfo">)),	UDT));

using ServerMetaOpt = ZfJSON::Union<ServerMeta>;

inline ServerMetaOpt serverMeta(int era)
{
  if (era != Era::Modern) return {};
  return ServerMeta{{"zmcp", Z_VERNAME}};
}

struct EmptyContent {
  const EmptyObject *begin() const {
    static const EmptyObject empty;
    return &empty;
  }
  const EmptyObject *end() const { return begin(); }
  const EmptyObject &operator [](unsigned) const {
    return *begin();
  }

  friend inline ZfJSON::AsArray<ZfFieldTC::UDT>
    ZfJSON_Fmt(EmptyContent *) { return {}; }
};

struct StructuredResultShape {
  int code = 0;
  EmptyObject data;
};
ZfStruct(ZmcpAPI, (StructuredResultShape, JSON),
  (((code), (Mutable)),	Int32),
  (((data), (Mutable)),	UDT));

struct StructuredEmpty {
  int code = 0;
};
ZfStruct(ZmcpAPI, (StructuredEmpty, JSON),
  (((code), (Mutable)),	Int32));

template <typename Body>
struct StructuredResult :
    public ZuStructShim<StructuredResult<Body>, StructuredResultShape> {
  const Body &data_;
  int code_;

  StructuredResult(int code, const Body &data) : data_{data}, code_{code} { }

  int code() const { return code_; }
  const Body &data() const { return data_; }
};

struct CallResultShape {
  ServerMetaOpt meta;
  ZuCSpan resultType;
  EmptyContent content;
  StructuredEmpty structuredContent;
  bool isError = false;
};
ZfStruct(ZmcpAPI, (CallResultShape, JSON),
  (((meta), (Mutable, JSON::Opt, JSON::ID<"_meta">)),	UDT),
  (((resultType), (Mutable, JSON::Opt)),		String),
  (((content), (Mutable)),				UDT),
  (((structuredContent), (Mutable)),			UDT),
  (((isError), (Mutable)),				Bool));

template <typename Structured>
struct CallResult :
    public ZuStructShim<CallResult<Structured>, CallResultShape> {
  ServerMetaOpt meta_;
  ZuCSpan resultType_;
  const Structured &structured_;
  bool error_;

  CallResult(int era, const Structured &structured, bool error) :
    meta_{serverMeta(era)},
    resultType_{era == Era::Modern ? ZuCSpan{"complete"} : ZuCSpan{}},
    structured_{structured}, error_{error} { }

  const ServerMetaOpt &meta() const { return meta_; }
  ZuCSpan resultType() const { return resultType_; }
  EmptyContent content() const { return {}; }
  const Structured &structuredContent() const { return structured_; }
  bool isError() const { return error_; }
};

template <typename S>
inline void saveToolError(S &s, const ID &id, int code, int era)
{
  StructuredEmpty structured{code};
  saveResult(s, id, CallResult{era, structured, true});
}

template <typename S, typename Res>
inline void saveToolReply(
    S &s, const ID &id, const ToolReply<Res> &reply, int era)
{
  if constexpr (!ZuIsSame<void, typename Res::Body>{}) {
    StructuredResult structured{Res::Status, reply.bodyObject()};
    saveResult(s, id, CallResult{
      era, structured, Res::Status < 200 || Res::Status >= 300});
  } else {
    StructuredEmpty structured{Res::Status};
    saveResult(s, id, CallResult{
      era, structured, Res::Status < 200 || Res::Status >= 300});
  }
}

template <typename Res> using ToolReplyT = ToolReply<Res>;

template <typename Req>
struct ReplyUnion_ {
  using Replies = ZuTypeMap<ToolReplyT, GetResponses<Req>>;
  using T = ZuTypeApply<ZuUnion, typename Replies::template Unshift<void>>;
};
template <typename Req> using ReplyUnion = typename ReplyUnion_<Req>::T;

template <typename U>
using ResponseBody = typename U::Body;

template <typename U>
using NonVoid = ZuBool<!ZuIsSame<U, void>{}>;

template <typename Req>
using ReplyBodies = ZuTypeUnique<ZuTypeGrep<
  NonVoid, ZuTypeMap<ResponseBody, GetResponses<Req>>>>;

template <typename Req>
using ReplyData = ZuTypeApply<ZfJSON::Union, ReplyBodies<Req>>;

struct StructuredReplyShape {
  int code = ZuCmp<int>::null();
  EmptyObject data;
};
ZfStruct(ZmcpAPI, (StructuredReplyShape, JSON),
  (((code), (Mutable)),			Int32),
  (((data), (Mutable, JSON::Opt)),	UDT));

template <typename Req>
struct StructuredReply :
    public StructModel<StructuredReply<Req>, StructuredReplyShape> {
  using Base = StructModel<StructuredReply<Req>, StructuredReplyShape>;
  using Data = ReplyData<Req>;

  template <typename Facet>
  friend typename Base::template Fields<Facet>
    ZuFields_(StructuredReply *, Facet *);
  friend StructuredReply ZuStructured_(StructuredReply *);

  int code() const { return code_; }
  void code(int v) { code_ = v; }

  const Data &data() const { return data_; }
  Data &data() { return data_; }
  void data(Data v) { data_ = ZuMv(v); }

  int	code_ = ZuCmp<int>::null();
  Data	data_;
};

struct CallReplyShape {
  StructuredReplyShape structuredContent;
};
ZfStruct(ZmcpAPI, (CallReplyShape, JSON),
  (((structuredContent), (Mutable)),	UDT));

template <typename Req>
struct CallReply : public StructModel<CallReply<Req>, CallReplyShape> {
  using Base = StructModel<CallReply<Req>, CallReplyShape>;
  using Structured = StructuredReply<Req>;

  template <typename Facet>
  friend typename Base::template Fields<Facet> ZuFields_(CallReply *, Facet *);
  friend CallReply ZuStructured_(CallReply *);

  const Structured &structuredContent() const { return structuredContent_; }
  Structured &structuredContent() { return structuredContent_; }
  void structuredContent(Structured v) { structuredContent_ = ZuMv(v); }

  Structured structuredContent_;
};

template <typename Req>
inline ReplyUnion<Req> loadToolReply(const ZfJSON::AnyNode *node)
{
  using Responses = GetResponses<Req>;
  ReplyUnion<Req> out;
  auto reply = loadObject<CallReply<Req>>(node);
  int code = reply.structuredContent().code();
  const auto *data = raw(reply.structuredContent().data());
  int index = -1;
  ZuUnroll::all<Responses>([code, &index]<typename Res>() {
    if (code == Res::Status) index = ZuTypeIndex<Res, Responses>{};
  });
  if (index < 0) return out;
  ZuSwitch::dispatch<Responses::N>(index, [&out, data](auto I) {
    using Res = ZuType<I, Responses>;
    if constexpr (ZuIsSame<void, typename Res::Body>{})
      out = ToolReply<Res>{};
    else if (data) {
	using Body = typename Res::Body;
	if constexpr (ZuIsBase<Body, ZmObject>{}) {
	  ZmRef<Body> body = new Body();
	  auto handler = ZfJSON::handler<Body>(data);
	  if (!handler.valid) return;
	  handler.load(*body);
	  out = ToolReply<Res>{ZuMv(body)};
	} else {
	  auto handler = ZfJSON::handler<Body>(data);
	  if (!handler.valid) return;
	  out = ToolReply<Res>{handler.ctor()};
	}
    }
  });
  return out;
}

template <typename Req> struct ToolArgFmt;

template <typename Req>
struct ToolArg {
  using Object = typename Req::Object;
  enum { Ref = ZuIsBase<Object, ZmObject>{} };
  using Value = ToolObject<Object>;

  Value value;

  ToolArg() {
    if constexpr (Ref) value = new Object();
  }
  ToolArg(Value value_) : value{ZuMv(value_)} { }

  Object &object() {
    if constexpr (Ref) return *value;
    else return value;
  }
  const Object &object() const {
    if constexpr (Ref) return *value;
    else return value;
  }

  friend inline ToolArgFmt<Req> ZfJSON_Fmt(ToolArg *) { return {}; }
};

template <typename Req>
struct ToolArgFmt {
  template <typename Arg, typename Facet>
  struct Handler {
    using Object = typename Arg::Object;
    using ObjectHandler =
      typename ZfJSON::As<Object>::template Handler<Object, Facet>;

    const ZfJSON::AnyNode *node;
    bool		valid = false;

    template <template <typename> class Filter, typename S>
    static void save(S &s, const Arg &arg) {
      ObjectHandler::template save<Filter>(s, arg.object());
    }

    Handler(const ZfJSON::AnyNode *node_) : node{node_} {
      valid = ObjectHandler{node}.valid;
    }
    Arg ctor() const {
      ZmAssert_(valid);
      auto handler = ObjectHandler{node};
      if constexpr (Arg::Ref) {
	Arg arg;
	handler.load(arg.object());
	return arg;
      } else
	return Arg{handler.ctor()};
    }
    Arg *alloc() const {
      if constexpr (Arg::Ref) {
	auto arg = new Arg();
	ObjectHandler{node}.load(arg->object());
	return arg;
      } else
	return new Arg{ObjectHandler{node}.ctor()};
    }
    void new_(void *p) const {
      if constexpr (Arg::Ref) {
	auto arg = new (p) Arg();
	ObjectHandler{node}.load(arg->object());
      } else
	new (p) Arg{ObjectHandler{node}.ctor()};
    }
    void load(Arg &arg) const { ObjectHandler{node}.load(arg.object()); }
    void update(Arg &arg) const { ObjectHandler{node}.update(arg.object()); }
  };
};

template <typename Req> using ToolArgT = ToolArg<Req>;

template <typename Reqs>
struct ToolArgs_ {
  using Types = ZuTypeMap<ToolArgT, Reqs>;
  using T = ZuTypeApply<ZfJSON::Union, Types>;
};
template <typename Reqs> using ToolArgs = typename ToolArgs_<Reqs>::T;

using EmptyOpt = ZfJSON::Union<EmptyObject>;
using PeerInfoOpt = ZfJSON::Union<PeerInfo>;

struct ClientMeta {
  ZuCSpan protocolVersion;
  EmptyOpt clientCapabilities;
  PeerInfoOpt clientInfo;
  ID progressToken;
  ZuCSpan logLevel;

  explicit operator bool() const { return true; }
};
ZfStruct(ZmcpAPI, (ClientMeta, JSON),
  (((protocolVersion),
    (Mutable, JSON::Opt,
      JSON::ID<"io.modelcontextprotocol/protocolVersion">)),	String),
  (((clientCapabilities),
    (Mutable, JSON::Opt,
      JSON::ID<"io.modelcontextprotocol/clientCapabilities">)),	UDT),
  (((clientInfo),
    (Mutable, JSON::Opt,
      JSON::ID<"io.modelcontextprotocol/clientInfo">)),		UDT),
  (((progressToken), (Mutable, JSON::Opt)),			UDT),
  (((logLevel),
    (Mutable, JSON::Opt,
      JSON::ID<"io.modelcontextprotocol/logLevel">)),		String));

using ClientMetaOpt = ZfJSON::Union<ClientMeta>;

inline ClientMetaOpt clientMeta(
    int era, const ID *progressToken = nullptr,
    int logLevel = LogLevel::Disabled)
{
  if (era != Era::Modern &&
      (!progressToken || progressToken->absent()) &&
      logLevel == LogLevel::Disabled) return {};
  ClientMeta meta;
  if (era == Era::Modern) {
    meta.protocolVersion = ModernVersion{}();
    meta.clientCapabilities = EmptyObject{};
    meta.clientInfo = PeerInfo{"zmcp", Z_VERNAME};
  }
  if (progressToken) meta.progressToken = *progressToken;
  logLevelName(logLevel, [&meta](ZuCSpan name) { meta.logLevel = name; });
  return meta;
}

struct MetaParams {
  ClientMetaOpt meta;
};
ZfStruct(ZmcpAPI, (MetaParams, JSON),
  (((meta), (Mutable, JSON::Opt, JSON::ID<"_meta">)),	UDT));

struct InitializeParams {
  ZuCSpan protocolVersion;
  EmptyObject capabilities;
  PeerInfo clientInfo;
};
ZfStruct(ZmcpAPI, (InitializeParams, JSON),
  (((protocolVersion), (Mutable)),	String),
  (((capabilities), (Mutable)),		UDT),
  (((clientInfo), (Mutable)),		UDT));

struct SetLevelParams {
  ZuCSpan level;
};
ZfStruct(ZmcpAPI, (SetLevelParams, JSON),
  (((level), (Mutable)),	String));

struct CancelledParams {
  ID requestID;
  ZuCSpan reason;
  ClientMetaOpt meta;
};
ZfStruct(ZmcpAPI, (CancelledParams, JSON),
  (((requestID), (Mutable, JSON::ID<"requestId">)),	UDT),
  (((reason), (Mutable, JSON::Opt)),			String),
  (((meta), (Mutable, JSON::Opt, JSON::ID<"_meta">)),	UDT));

struct ProgressParams {
  ID token;
  double progress = 0;
  double total = ZuCmp<double>::null();
  ZuCSpan message;
};
ZfStruct(ZmcpAPI, (ProgressParams, JSON),
  (((token), (Mutable, JSON::ID<"progressToken">)),	UDT),
  (((progress), (Mutable)),				Float),
  (((total), (Mutable, JSON::Opt)),			Float),
  (((message), (Mutable, JSON::Opt)),			String));

struct LogParams {
  ZuCSpan level;
  ZuCSpan logger;
  ZuCSpan data;
};
ZfStruct(ZmcpAPI, (LogParams, JSON),
  (((level), (Mutable)),		String),
  (((logger), (Mutable, JSON::Opt)),	String),
  (((data), (Mutable)),			String));

struct RxLogParams {
  ZuCSpan level;
  ZuCSpan logger;
};
ZfStruct(ZmcpAPI, (RxLogParams, JSON),
  (((level), (Mutable)),		String),
  (((logger), (Mutable, JSON::Opt)),	String));

struct ToolsCallShape {
  ZuCSpan name;
  ZfJSON::Union<EmptyObject> arguments;
  ClientMetaOpt meta;
};
ZfStruct(ZmcpAPI, (ToolsCallShape, JSON),
  (((name), (Mutable)),					String),
  (((arguments), (Mutable)),				UDT),
  (((meta), (Mutable, JSON::Opt, JSON::ID<"_meta">)),	UDT));

template <typename Reqs_>
struct ToolsCallParams :
    public StructModel<ToolsCallParams<Reqs_>, ToolsCallShape> {
  using Base = StructModel<ToolsCallParams<Reqs_>, ToolsCallShape>;
  using Requests = Reqs_;
  using ToolIDs = typename Contract<Requests>::ToolIDs;
  using Arguments = ToolArgs<Requests>;

  template <typename Facet>
  friend typename Base::template Fields<Facet>
    ZuFields_(ToolsCallParams *, Facet *);
  friend ToolsCallParams ZuStructured_(ToolsCallParams *);

  ZuCSpan name() const {
    if constexpr (!Requests::N) {
      return name_;
    } else {
      int index = active();
      if (index < 0) return name_;
      ZuCSpan name;
      ZuSwitch::dispatch<Requests::N>(index, [&name](auto I) {
	using Req = ZuType<I, Requests>;
	name = GetToolID<Req>{}();
      });
      return name;
    }
  }
  void name(ZuCSpan v) { name_ = v; }

  const Arguments &arguments() const { return arguments_; }
  Arguments &arguments() { return arguments_; }
  void arguments(Arguments v) { arguments_ = ZuMv(v); }

  ClientMetaOpt meta() const {
    return clientMeta(era, &progressToken, logLevel);
  }
  void meta(ClientMetaOpt v) {
    if (!v.template is<const ZfJSON::AnyNode *>()) return;
    auto node = v.template p<const ZfJSON::AnyNode *>();
    if (!node) return;
    auto handler = ZfJSON::handler<ClientMeta>(node);
    if (!handler.valid) return;
    auto meta = handler.ctor();
    progressToken = ZuMv(meta.progressToken);
    logLevel = Zmcp::logLevel(meta.logLevel);
  }

  ZuCSpan name_;
  Arguments arguments_;
  ID progressToken;
  int logLevel = LogLevel::Disabled;
  int era = Era::Unknown;

  // Returns the catalog index, -1 for an unknown name, and -2 when the raw
  // arguments node cannot be narrowed safely.
  int narrow() {
    if constexpr (!Requests::N) {
      return -1;
    } else {
      int index = match();
      if (index < 0) return index;
      if (!arguments_.template is<const ZfJSON::AnyNode *>()) return -2;
      const auto *node = arguments_.template p<const ZfJSON::AnyNode *>();
      if (!node) return -2;
      ZuSwitch::dispatch<Requests::N>(index, [this, node, &index](auto I) {
	using Req = ZuType<I, Requests>;
	using Arg = ToolArg<Req>;
	auto handler = ZfJSON::handler<Arg>(node);
	if (!handler.valid) { index = -2; return; }
	arguments_ = handler.ctor();
      });
      return index;
    }
  }

  int match() const {
    if constexpr (!Requests::N) {
      return -1;
    } else if constexpr (Requests::N == 1) {
      using Req = ZuType<0, Requests>;
      return name_ == GetToolID<Req>{}() ? 0 : -1;
    } else {
      constexpr auto matcher = ZuMatcher<Contract<Requests>>();
      return matcher.exact(name_);
    }
  }

  int active() const {
    unsigned type = arguments_.type();
    return type >= 2 && type < Arguments::N ? int(type - 2) : -1;
  }
};

template <typename Reqs>
using AllResponses = ZuTypeApply<
  ZuTypeConcat, ZuTypeMap<GetResponses, Reqs>>;

template <typename Reqs>
using ResponseBodies = ZuTypeGrep<
  NonVoid, ZuTypeMap<ResponseBody, AllResponses<Reqs>>>;

template <typename Reqs>
using ResultTypes = ZuTypeUnique<
  typename ResponseBodies<Reqs>::template Unshift<EmptyObject>>;

template <typename Reqs>
using ParamsObject = ZfJSON::Union<EmptyObject, ToolsCallParams<Reqs>>;

template <typename Reqs>
using ResultObject = ZuTypeApply<ZfJSON::Union, ResultTypes<Reqs>>;

using ErrorObject = ZfJSON::Union<Error>;

struct EnvelopeShape {
  ID id;
  ZuCSpan method;
  ZfJSON::Union<EmptyObject> params;
  ZfJSON::Union<EmptyObject> result;
  ErrorObject error;
};
ZfStruct(ZmcpAPI, (EnvelopeShape, JSON),
  (((id), (Mutable)),		UDT),
  (((method), (Mutable)),	String),
  (((params), (Mutable)),	UDT),
  (((result), (Mutable)),	UDT),
  (((error), (Mutable)),	UDT));

template <typename Reqs>
struct Envelope : public StructModel<Envelope<Reqs>, EnvelopeShape> {
  using Base = StructModel<Envelope<Reqs>, EnvelopeShape>;
  using Params = ParamsObject<Reqs>;
  using Result = ResultObject<Reqs>;

  template <typename Facet>
  friend typename Base::template Fields<Facet> ZuFields_(Envelope *, Facet *);
  friend Envelope ZuStructured_(Envelope *);

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

  const ErrorObject &error() const { return error_; }
  ErrorObject &error() { return error_; }
  void error(ErrorObject v) { error_ = ZuMv(v); }

  ID		id_;
  ZuCSpan	method_;
  Params	params_;
  Result	result_;
  ErrorObject	error_;
  int		kind = MessageKind::Unusable;
};

template <typename Reqs>
struct Parsed {
  ZuPtr<ZfJSON::AnyNode> root;
  Envelope<Reqs> envelope;
  int consumed = -1;

  explicit operator bool() const {
    return envelope.kind != MessageKind::Unusable;
  }
  bool close() const { return !operator bool(); }
};

template <typename Reqs>
inline Parsed<Reqs> parse(ZuSpan<char> input, unsigned maxBytes)
{
  Parsed<Reqs> out;
  if (ZuUnlikely(input.length() > maxBytes)) return out;
  auto scanned = ZfJSON::scan(input);
  out.consumed = scanned.p<0>();
  out.root = ZuMv(scanned.p<1>());
  if (ZuUnlikely(out.consumed < 0 || !out.root ||
      !out.root->template has<ZfJSON::AnyNode::Array>() ||
      !out.root->template data<ZfJSON::AnyNode::Array>().length())) {
    out.root = nullptr;
    return out;
  }
  const ZfJSON::AnyNode *node = (*out.root)[0];
  if (ZuUnlikely(!node ||
      !node->template has<ZfJSON::AnyNode::Object>())) return out;
  auto handler = ZfJSON::handler<Envelope<Reqs>>(node);
  if (!handler.valid) return out;
  handler.load(out.envelope);
  if (out.envelope.method())
    out.envelope.kind = out.envelope.id().absent() ?
	MessageKind::Notification : MessageKind::Request;
  else if (raw(out.envelope.result()))
    out.envelope.kind = MessageKind::Result;
  else if (raw(out.envelope.error()))
    out.envelope.kind = MessageKind::Error;
  return out;
}

ZuDerive(SchemaString, (ZtString<ZtStringHeapID<"Zmcp.Schema">>));

namespace Schema_ {

template <typename> struct FmtInfo {
  enum { Kind = 0, Code = 0 };
  using Props = ZuTypeList<>;
};
template <unsigned Code_, typename Props_>
struct FmtInfo<ZfJSON::AsArray<Code_, Props_>> {
  enum { Kind = 1, Code = Code_ };
  using Props = Props_;
};
template <unsigned Code_, typename Props_>
struct FmtInfo<ZfJSON::AsMap<Code_, Props_>> {
  enum { Kind = 2, Code = Code_ };
  using Props = Props_;
};

} // Schema_

namespace SchemaModel_ {

struct EmptyNames {
  unsigned length() const { return 0; }
  ZuCSpan operator [](unsigned) const { return {}; }
  explicit operator bool() const { return false; }

  struct Traits : public ZuBaseTraits<EmptyNames> {
    using Elem = ZuCSpan;
    enum { IsArray = 1, IsSpan = 0 };
    static unsigned length(const EmptyNames &v) { return v.length(); }
  };
  friend Traits ZuTraitsType(EmptyNames *);

  friend inline ZfJSON::AsArray<ZfFieldTC::String>
    ZfJSON_Fmt(EmptyNames *) { return {}; }
};

template <typename Desc>
struct EnumNames {
  struct Iterator {
    unsigned i;
    int operator -(Iterator r) const { return int(i) - int(r.i); }
    ZuCSpan operator *() const { return EnumNames{}[i]; }
  };

  unsigned length() const {
    if constexpr (ZuFieldProp::HasEnum<typename Desc::Props>{}) {
      using Map = ZuFieldProp::GetEnum<typename Desc::Props>;
      using Names = typename Map::Names;
      return Names::N;
    } else {
      return 0;
    }
  }
  ZuCSpan operator [](unsigned i) const {
    if constexpr (ZuFieldProp::HasEnum<typename Desc::Props>{}) {
      using Map = ZuFieldProp::GetEnum<typename Desc::Props>;
      return Map::v2s(int(i));
    } else {
      return {};
    }
  }
  Iterator begin() const { return {0}; }
  Iterator end() const { return {length()}; }

  explicit operator bool() const { return length(); }

  struct Traits : public ZuBaseTraits<EnumNames> {
    using Elem = ZuCSpan;
    enum { IsArray = 1, IsSpan = 0 };
    static unsigned length(const EnumNames &v) { return v.length(); }
  };
  friend Traits ZuTraitsType(EnumNames *);

  friend inline ZfJSON::AsArray<ZfFieldTC::String>
    ZfJSON_Fmt(EnumNames *) { return {}; }
};

template <typename Desc, int Kind>
struct ScalarValueFmt {
  template <typename O, typename>
  struct Handler {
    template <template <typename> class Filter, typename S>
    static void save(S &s, const O &) {
      if constexpr (Kind == 0) {
	if constexpr (ZuFieldProp::HasRange<typename Desc::Props>{}) {
	  using Range = ZuFieldProp::GetRange<typename Desc::Props>;
	  ZfJSON::saveValue<ZuFacet::JSON, Filter,
	    Desc::Code, ZuTypeList<>>(s, Range::minimum());
	}
      } else if constexpr (Kind == 1) {
	if constexpr (ZuFieldProp::HasRange<typename Desc::Props>{}) {
	  using Range = ZuFieldProp::GetRange<typename Desc::Props>;
	  ZfJSON::saveValue<ZuFacet::JSON, Filter,
	    Desc::Code, ZuTypeList<>>(s, Range::maximum());
	}
      } else {
	ZfJSON::saveValue<ZuFacet::JSON, Filter,
	  Desc::Code, typename Desc::Props>(s, Desc::deflt());
      }
    }
  };
};

template <typename Desc, int Kind>
struct ScalarValue {
  explicit operator bool() const {
    if constexpr (Kind < 2)
      return ZuFieldProp::HasRange<typename Desc::Props>{};
    else
      return Desc::HasDefault;
  }
  friend inline ScalarValueFmt<Desc, Kind> ZfJSON_Fmt(ScalarValue *) {
    return {};
  }
};

struct ScalarShape {
  ZuCSpan type;
  EmptyNames enum_;
  EmptyObject minimum;
  EmptyObject maximum;
  EmptyObject deflt;
  ZuCSpan mcpHeader;
};
ZfStruct(ZmcpAPI, (ScalarShape, JSON),
  (((type), (Mutable)),							String),
  (((enum_), (Mutable, JSON::Opt, JSON::ID<"enum">)),			UDT),
  (((minimum), (Mutable, JSON::Opt)),					UDT),
  (((maximum), (Mutable, JSON::Opt)),					UDT),
  (((deflt), (Mutable, JSON::Opt, JSON::ID<"default">)),		UDT),
  (((mcpHeader), (Mutable, JSON::Opt, JSON::ID<"x-mcp-header">)),	String));

template <typename Desc>
struct Scalar : public ZuStructShim<Scalar<Desc>, ScalarShape> {
  ZuCSpan type() const {
    if constexpr (Desc::Code == ZfFieldTC::CString ||
	Desc::Code == ZfFieldTC::String ||
	Desc::Code == ZfFieldTC::Bytes ||
	Desc::Code == ZfFieldTC::Time ||
	Desc::Code == ZfFieldTC::DateTime ||
	ZuFieldProp::HasEnum<typename Desc::Props>{} ||
	ZuFieldProp::HasFlags<typename Desc::Props>{})
      return "string";
    else if constexpr (Desc::Code == ZfFieldTC::Bool)
      return "boolean";
    else if constexpr (Desc::Code == ZfFieldTC::Float ||
	Desc::Code == ZfFieldTC::Fixed ||
	Desc::Code == ZfFieldTC::Decimal)
      return "number";
    else
      return "integer";
  }
  EnumNames<Desc> enum_() const { return {}; }
  ScalarValue<Desc, 0> minimum() const { return {}; }
  ScalarValue<Desc, 1> maximum() const { return {}; }
  ScalarValue<Desc, 2> deflt() const { return {}; }
  ZuCSpan mcpHeader() const {
    using Header = ZuFieldProp::MCP::GetHeader<typename Desc::Props>;
    using Props = typename Desc::Props;
    if constexpr (ZuIsSame<void, Header>{} ||
	!IsParameterHeader<Desc::Code, Props>{})
      return {};
    else return Header{}();
  }
};

template <unsigned Code_, typename Props_, typename T_>
struct ValueDesc {
  enum { Code = Code_, HasDefault = 0 };
  using Props = Props_;
  using T = T_;
  static T deflt() { return {}; }
};

template <typename Field>
struct FieldDesc {
  enum {
    Code = Field::Type::Code,
    HasDefault =
      !ZuTypeIn<ZuFieldProp::Required, typename Field::Props>{} &&
      Code != ZfFieldTC::UDT && !ZfFieldTC::IsVec<Code>{}
  };
  using Props = typename Field::Props;
  using T = typename Field::T;
  static decltype(auto) deflt() { return Field::deflt(); }
};

template <typename Desc> struct Schema;
template <typename Field> struct FieldSchema;

struct ArrayShape {
  ZuCSpan type;
  EmptyObject items;
};
ZfStruct(ZmcpAPI, (ArrayShape, JSON),
  (((type), (Mutable)),		String),
  (((items), (Mutable)),	UDT));

template <typename Item>
struct Array : public ZuStructShim<Array<Item>, ArrayShape> {
  ZuCSpan type() const { return "array"; }
  Schema<Item> items() const { return {}; }
};

struct MapShape {
  ZuCSpan type;
  EmptyObject additionalProperties;
};
ZfStruct(ZmcpAPI, (MapShape, JSON),
  (((type), (Mutable)),			String),
  (((additionalProperties), (Mutable)),	UDT));

template <typename Val>
struct Map : public ZuStructShim<Map<Val>, MapShape> {
  ZuCSpan type() const { return "object"; }
  Schema<Val> additionalProperties() const { return {}; }
};

template <typename Field>
using FieldSchemaT = FieldSchema<Field>;

template <typename Fields>
struct Properties_;

template <typename ...Field>
struct Properties_<ZuTypeList<Field...>> {
  using Key = ZuCSpan;
  using Types = ZuTypeList<FieldSchema<Field>...>;
  using Val = ZuTypeApply<ZfJSON::Union,
    typename Types::template Unshift<EmptyObject>>;

  struct Node {
    Key key_;
    Val val_;
    Key key() const { return key_; }
    const Val &val() const { return val_; }
  };
  struct Iterator {
    Node node;
    unsigned i = 0;
    Node *operator ()() {
      if (i >= sizeof...(Field)) return nullptr;
      if constexpr (sizeof...(Field))
	ZuSwitch::dispatch<sizeof...(Field)>(i++, [this](auto I) {
	  using F = ZuType<I, ZuTypeList<Field...>>;
	  node.key_ = ZuFieldProp::JSON::GetID<F>{}().cspan();
	  node.val_ = FieldSchema<F>{};
	});
      return &node;
    }
  };

  Iterator citer() const { return {}; }
  friend inline ZfJSON::AsMap<ZfFieldTC::UDT>
    ZfJSON_Fmt(Properties_ *) { return {}; }
};

template <typename Field>
using IsRequired = ZuTypeIn<ZuFieldProp::Required, typename Field::Props>;

template <typename Fields>
using RequiredFields = ZuTypeGrep<IsRequired, Fields>;

template <typename Fields>
struct Required {
  using Reqs = RequiredFields<Fields>;
  struct Iterator {
    unsigned i;
    int operator -(Iterator r) const { return int(i) - int(r.i); }
    ZuCSpan operator *() const {
      ZuCSpan out;
      if constexpr (Reqs::N)
	ZuSwitch::dispatch<Reqs::N>(i, [&out](auto I) {
	  using Field = ZuType<I, Reqs>;
	  out = ZuFieldProp::JSON::GetID<Field>{}().cspan();
	});
      return out;
    }
  };
  Iterator begin() const { return {0}; }
  Iterator end() const { return {Reqs::N}; }
  ZuCSpan operator [](unsigned i) const {
    ZuCSpan out;
    if constexpr (Reqs::N)
      ZuSwitch::dispatch<Reqs::N>(i, [&out](auto I) {
	using Field = ZuType<I, Reqs>;
	out = ZuFieldProp::JSON::GetID<Field>{}().cspan();
      });
    return out;
  }
  explicit operator bool() const { return Reqs::N; }
  friend inline ZfJSON::AsArray<ZfFieldTC::String>
    ZfJSON_Fmt(Required *) { return {}; }
};

struct ObjectShape {
  ZuCSpan type;
  EmptyObject properties;
  EmptyNames required;
  bool additionalProperties = false;
};
ZfStruct(ZmcpAPI, (ObjectShape, JSON),
  (((type), (Mutable)),			String),
  (((properties), (Mutable)),		UDT),
  (((required), (Mutable, JSON::Opt)),	UDT),
  (((additionalProperties), (Mutable)),	Bool));

template <typename O>
struct Object : public ZuStructShim<Object<O>, ObjectShape> {
  using Fields = ZuFields<O, ZuFacet::JSON>;

  ZuCSpan type() const { return "object"; }
  Properties_<Fields> properties() const { return {}; }
  Required<Fields> required() const { return {}; }
  bool additionalProperties() const { return false; }
};

template <typename Desc>
struct SchemaFmt {
  template <typename O, typename Facet>
  struct Handler {
    template <template <typename> class Filter, typename S>
    static void save(S &s, const O &) {
      enum { Code = Desc::Code };
      using Props = typename Desc::Props;
      using T = typename Desc::T;
      if constexpr (ZfFieldTC::IsVec<Code>{}) {
	using Elem = ZuDecay<decltype(ZuDeclVal<const T &>()[0])>;
	using Item = ValueDesc<ZfFieldTC::Elem<Code>{}, Props, Elem>;
	ZfJSON::save<Facet, Filter>(s, Array<Item>{});
      } else if constexpr (Code != ZfFieldTC::UDT) {
	ZfJSON::save<Facet, Filter>(s, Scalar<Desc>{});
      } else {
	using Info = Schema_::FmtInfo<ZfJSON::As<T>>;
	if constexpr (Info::Kind == 1) {
	  using Elem = ZuDecay<decltype(ZuDeclVal<const T &>()[0])>;
	  using Item = ValueDesc<Info::Code, typename Info::Props, Elem>;
	  ZfJSON::save<Facet, Filter>(s, Array<Item>{});
	} else if constexpr (Info::Kind == 2) {
	  using Item = ValueDesc<Info::Code, typename Info::Props,
	    typename T::Val>;
	  ZfJSON::save<Facet, Filter>(s, Map<Item>{});
	} else
	  ZfJSON::save<Facet, Filter>(s, Object<T>{});
      }
    }
  };
};

template <typename Desc>
struct Schema {
  friend inline SchemaFmt<Desc> ZfJSON_Fmt(Schema *) { return {}; }
};

template <typename Field>
struct FieldSchema {
  friend inline SchemaFmt<FieldDesc<Field>>
    ZfJSON_Fmt(FieldSchema *) { return {}; }
};

template <typename O>
using ObjectDesc = ValueDesc<ZfFieldTC::UDT, ZuTypeList<>, O>;

struct ConstShape {
  int value;
};
ZfStruct(ZmcpAPI, (ConstShape, JSON),
  (((value), (Mutable, JSON::ID<"const">)),	Int32));

template <typename Res>
struct Const : public ZuStructShim<Const<Res>, ConstShape> {
  int value() const { return Res::Status; }
};

template <typename Res>
struct ResponseDataFmt {
  template <typename O, typename Facet>
  struct Handler {
    template <template <typename> class Filter, typename S>
    static void save(S &s, const O &) {
      if constexpr (!ZuIsSame<void, typename Res::Body>{})
	ZfJSON::save<Facet, Filter>(s,
	  Schema<ObjectDesc<typename Res::Body>>{});
    }
  };
};

template <typename Res>
struct ResponseData {
  explicit operator bool() const {
    return !ZuIsSame<void, typename Res::Body>{};
  }
  friend inline ResponseDataFmt<Res> ZfJSON_Fmt(ResponseData *) {
    return {};
  }
};

struct ResponsePropertiesShape {
  EmptyObject code;
  EmptyObject data;
};
ZfStruct(ZmcpAPI, (ResponsePropertiesShape, JSON),
  (((code), (Mutable)),			UDT),
  (((data), (Mutable, JSON::Opt)),	UDT));

template <typename Res>
struct ResponseProperties :
    public ZuStructShim<ResponseProperties<Res>, ResponsePropertiesShape> {
  Const<Res> code() const { return {}; }
  ResponseData<Res> data() const { return {}; }
};

struct ResponseRequiredIterator {
  unsigned i;
  int operator -(ResponseRequiredIterator r) const {
    return int(i) - int(r.i);
  }
  ZuCSpan operator *() const {
    return i ? ZuCSpan{"data"} : ZuCSpan{"code"};
  }
};

template <typename Res>
struct ResponseRequired {
  enum { N = ZuIsSame<void, typename Res::Body>{} ? 1 : 2 };
  ResponseRequiredIterator begin() const { return {0}; }
  ResponseRequiredIterator end() const { return {N}; }
  ZuCSpan operator [](unsigned i) const { return i ? "data" : "code"; }
  friend inline ZfJSON::AsArray<ZfFieldTC::String>
    ZfJSON_Fmt(ResponseRequired *) { return {}; }
};

struct ResponseShape {
  ZuCSpan type;
  EmptyObject properties;
  EmptyNames required;
  bool additionalProperties = false;
  ZuCSpan description;
};
ZfStruct(ZmcpAPI, (ResponseShape, JSON),
  (((type), (Mutable)),				String),
  (((properties), (Mutable)),			UDT),
  (((required), (Mutable)),			UDT),
  (((additionalProperties), (Mutable)),		Bool),
  (((description), (Mutable, JSON::Opt)),	String));

template <typename Res>
struct ResponseSchema :
    public ZuStructShim<ResponseSchema<Res>, ResponseShape> {
  ZuCSpan type() const { return "object"; }
  ResponseProperties<Res> properties() const { return {}; }
  ResponseRequired<Res> required() const { return {}; }
  bool additionalProperties() const { return false; }
  ZuCSpan description() const {
    if constexpr (ZuIsSame<void, GetDescription<Res>>{}) return {};
    else return GetDescription<Res>{}();
  }
};

template <typename Responses>
struct ResponseList_;

template <typename ...Res>
struct ResponseList_<ZuTypeList<Res...>> {
  using Values = ZuTypeList<ResponseSchema<Res>...>;
  using Value = ZuTypeApply<ZfJSON::Union,
    typename Values::template Unshift<EmptyObject>>;

  struct Iterator {
    unsigned i;
    int operator -(Iterator r) const { return int(i) - int(r.i); }
    Value operator *() const { return {}; }
  };
  Iterator begin() const { return {0}; }
  Iterator end() const { return {sizeof...(Res)}; }
  Value operator [](unsigned i) const {
    Value out;
    ZuSwitch::dispatch<sizeof...(Res)>(i, [&out](auto I) {
      using R = ZuType<I, ZuTypeList<Res...>>;
      out = ResponseSchema<R>{};
    });
    return out;
  }
  friend inline ZfJSON::AsArray<ZfFieldTC::UDT>
    ZfJSON_Fmt(ResponseList_ *) { return {}; }
};

struct OutputShape {
  EmptyObject oneOf;
};
ZfStruct(ZmcpAPI, (OutputShape, JSON),
  (((oneOf), (Mutable)),	UDT));

template <typename Req>
struct Output : public ZuStructShim<Output<Req>, OutputShape> {
  ResponseList_<GetResponses<Req>> oneOf() const { return {}; }
};

struct OperationMeta {
  ZuCSpan operationID;
};
ZfStruct(ZmcpAPI, (OperationMeta, JSON),
  (((operationID),
    (Mutable, JSON::ID<"operationId">)),	String));

template <typename Req>
struct AnnotationsFmt {
  template <typename O, typename Facet>
  struct Handler {
    template <template <typename> class Filter, typename S>
    static void save(S &s, const O &) {
      if constexpr (!ZuIsSame<void, GetAnnotations<Req>>{})
	ZfJSON::save<Facet, Filter>(s, Req::annotations());
    }
  };
};

template <typename Req>
struct Annotations {
  explicit operator bool() const {
    return !ZuIsSame<void, GetAnnotations<Req>>{};
  }
  friend inline AnnotationsFmt<Req> ZfJSON_Fmt(Annotations *) { return {}; }
};

struct ToolShape {
  ZuCSpan name;
  ZuCSpan title;
  ZuCSpan description;
  EmptyObject annotations;
  EmptyObject inputSchema;
  EmptyObject outputSchema;
  OperationMeta meta;
};
ZfStruct(ZmcpAPI, (ToolShape, JSON),
  (((name), (Mutable)),				String),
  (((title), (Mutable, JSON::Opt)),		String),
  (((description), (Mutable, JSON::Opt)),	String),
  (((annotations), (Mutable, JSON::Opt)),	UDT),
  (((inputSchema), (Mutable)),			UDT),
  (((outputSchema), (Mutable)),			UDT),
  (((meta), (Mutable, JSON::ID<"_meta">)),	UDT));

template <typename Req>
struct Tool : public ZuStructShim<Tool<Req>, ToolShape> {
  ZuCSpan name() const { return GetToolID<Req>{}(); }
  ZuCSpan title() const {
    if constexpr (ZuIsSame<void, GetTitle<Req>>{}) return {};
    else return GetTitle<Req>{}();
  }
  ZuCSpan description() const {
    if constexpr (ZuIsSame<void, GetDescription<Req>>{}) return {};
    else return GetDescription<Req>{}();
  }
  Annotations<Req> annotations() const { return {}; }
  Schema<ObjectDesc<typename Req::Object>> inputSchema() const { return {}; }
  Output<Req> outputSchema() const { return {}; }
  OperationMeta meta() const { return {GetOperationID<Req>{}()}; }
};

template <typename Reqs>
struct ToolList_;

template <typename ...Req>
struct ToolList_<ZuTypeList<Req...>> {
  using Values = ZuTypeList<Tool<Req>...>;
  using Value = ZuTypeApply<ZfJSON::Union,
    typename Values::template Unshift<EmptyObject>>;
  struct Iterator {
    unsigned i;
    int operator -(Iterator r) const { return int(i) - int(r.i); }
    Value operator *() const { return {}; }
  };
  Iterator begin() const { return {0}; }
  Iterator end() const { return {sizeof...(Req)}; }
  Value operator [](unsigned i) const {
    Value out;
    if constexpr (sizeof...(Req))
      ZuSwitch::dispatch<sizeof...(Req)>(i, [&out](auto I) {
	using R = ZuType<I, ZuTypeList<Req...>>;
	out = Tool<R>{};
      });
    return out;
  }
  friend inline ZfJSON::AsArray<ZfFieldTC::UDT>
    ZfJSON_Fmt(ToolList_ *) { return {}; }
};

struct ToolsResultShape {
  EmptyContent tools;
  ServerMetaOpt meta;
  ZuCSpan resultType;
  ZuCSpan cacheScope;
  uint64_t ttlMs = 0;
};
ZfStruct(ZmcpAPI, (ToolsResultShape, JSON),
  (((tools), (Mutable)),				UDT),
  (((meta), (Mutable, JSON::Opt, JSON::ID<"_meta">)),	UDT),
  (((resultType), (Mutable, JSON::Opt)),		String),
  (((cacheScope), (Mutable, JSON::Opt)),		String),
  (((ttlMs), (Mutable, JSON::Opt)),			UInt64));

template <typename Reqs>
struct ToolsResult :
    public ZuStructShim<ToolsResult<Reqs>, ToolsResultShape> {
  int era;

  ToolsResult(int era_) : era{era_} { }

  ToolList_<Reqs> tools() const { return {}; }
  ServerMetaOpt meta() const { return serverMeta(era); }
  ZuCSpan resultType() const {
    return era == Era::Modern ? ZuCSpan{"complete"} : ZuCSpan{};
  }
  ZuCSpan cacheScope() const {
    return era == Era::Modern ? ZuCSpan{"public"} : ZuCSpan{};
  }
  uint64_t ttlMs() const { return era == Era::Modern ? CatalogTTL : 0; }
};

} // SchemaModel_

template <typename O, typename S>
inline void emitSchema(S &s)
{
  ZfJSON::save(s, SchemaModel_::Schema<SchemaModel_::ObjectDesc<O>>{});
}

template <typename O>
inline SchemaString schema()
{
  SchemaString out;
  emitSchema<O>(out);
  return out;
}

template <typename Reqs, typename S>
inline void emitToolsList(S &s, int era)
{
  ZfJSON::save(s, SchemaModel_::ToolsResult<Reqs>{era});
}

template <typename Req, typename Owner, typename Heap = ZuVoid>
class Completion_ : public Heap, public ZmObject {
public:
  Completion_(
      Owner *owner_, ID id_, ID progressToken_, int logLevel_,
      uint64_t generation_) :
    m_owner{owner_}, m_id{ZuMv(id_)},
    m_progressToken{ZuMv(progressToken_)}, m_generation{generation_},
    m_logLevel{logLevel_} { }

  const ID &id() const { return m_id; }
  const ID &progressToken() const { return m_progressToken; }
  uint64_t generation() const { return m_generation; }
  bool live() const { return m_owner; }
  bool cancelled() const { return m_cancelled; }

  bool progress(
      double progress_, double total = ZuCmp<double>::null(),
      ZuCSpan message = {}) {
    auto owner = m_owner;
    if (!owner || m_progressToken.absent()) return false;
    return owner->template progress<Req>(
      this, progress_, total, message);
  }

  bool log(ZuCSpan level, ZuCSpan data, ZuCSpan logger = {}) {
    auto owner = m_owner;
    int severity = logLevel(level);
    if (!owner || m_logLevel == LogLevel::Disabled ||
	severity < m_logLevel) return false;
    return owner->template log<Req>(this, level, data, logger);
  }

  template <typename Res>
  bool complete(ToolReply<Res> reply) {
    ZuAssert((ZuTypeIn<Res, GetResponses<Req>>{}));
    auto owner = m_owner;
    if (!owner) return false;
    return owner->template complete<Req, Res>(this, ZuMv(reply));
  }

private:
  friend Owner;
  friend struct CompletionEntry;

  void invalidate_() { m_owner = nullptr; }
  void cancel_(ZuCSpan reason) {
    if (m_cancelled || !m_owner) return;
    m_cancelled = true;
    m_owner->template cancelled<Req>(this, reason);
  }

  Owner		*m_owner;
  ID		m_id;
  ID		m_progressToken;
  uint64_t	m_generation;
  int		m_logLevel;
  bool		m_cancelled = false;
};

template <typename Req, typename Owner>
ZuDerive(CompletionHeap, (ZmHeap<"Zmcp.Completion", Completion_<Req, Owner>>));

template <typename Req, typename Owner>
ZuDerive(Completion, (Completion_<Req, Owner,
  CompletionHeap<Req, Owner>>));

struct CompletionEntry {
  using InvalidateFn = void (*)(void *);
  using CancelFn = void (*)(void *, ZuCSpan);
  using ReleaseFn = void (*)(void *);

  ID		id;
  uint64_t	generation = 0;
  void		*object = nullptr;
  InvalidateFn	invalidateFn = nullptr;
  CancelFn	cancelFn = nullptr;
  ReleaseFn	releaseFn = nullptr;

  CompletionEntry() = default;
  CompletionEntry(const CompletionEntry &) = delete;
  CompletionEntry &operator =(const CompletionEntry &) = delete;
  CompletionEntry(CompletionEntry &&entry) :
      id{ZuMv(entry.id)}, generation{entry.generation},
      object{entry.object}, invalidateFn{entry.invalidateFn},
      cancelFn{entry.cancelFn}, releaseFn{entry.releaseFn} {
    entry.object = nullptr;
  }
  CompletionEntry &operator =(CompletionEntry &&entry) {
    if (this == &entry) return *this;
    release_();
    id = ZuMv(entry.id);
    generation = entry.generation;
    object = entry.object;
    invalidateFn = entry.invalidateFn;
    cancelFn = entry.cancelFn;
    releaseFn = entry.releaseFn;
    entry.object = nullptr;
    return *this;
  }

  template <typename Token>
  CompletionEntry(ID id_, uint64_t generation_, ZmRef<Token> token) :
      id{ZuMv(id_)}, generation{generation_},
      object{ZuMv(token).release()},
      invalidateFn{[](void *object_) {
	static_cast<Token *>(object_)->invalidate_();
      }},
      cancelFn{[](void *object_, ZuCSpan reason) {
	static_cast<Token *>(object_)->cancel_(reason);
      }},
      releaseFn{[](void *object_) {
	auto token_ = static_cast<Token *>(object_);
	if (token_->deref()) delete token_;
      }} { }

  ~CompletionEntry() { release_(); }

  void invalidate() { if (object) invalidateFn(object); }
  void cancel(ZuCSpan reason) { if (object) cancelFn(object, reason); }

  void release_() {
    if (!object) return;
    auto object_ = object;
    object = nullptr;
    releaseFn(object_);
  }

};

inline const ID &CompletionEntry_KeyAxor(const CompletionEntry &entry) {
  return entry.id;
}

ZmHashDerive(CompletionHash, CompletionEntry,
  (ZmHashNode<CompletionEntry,
    ZmHashKey<CompletionEntry_KeyAxor,
	ZmHashLock<ZmNoLock,
	  ZmHashHeapID<"Zmcp.Completions">>>>));

namespace CompletionState {
  enum { Open, Closing, Closed };
}

template <typename Impl>
class CompletionSet {
public:
  CompletionSet(unsigned maxPending = Default::MaxPending) :
    m_maxPending{maxPending} { }
  ~CompletionSet() { drain(); }

  Impl *impl() { return static_cast<Impl *>(this); }

  unsigned count() const { return m_hash.count_(); }
  int state() const { return m_state; }

  template <typename Req>
  ZmRef<Completion<Req, CompletionSet>> make(
      const ID &id, const ID &progressToken = {},
      int logLevel = LogLevel::Disabled) {
    using Token = Completion<Req, CompletionSet>;
    if (m_state != CompletionState::Open || id.absent() ||
	m_hash.count_() >= m_maxPending ||
	m_hash.findPtr(id)) return {};
    uint64_t generation = m_generation++;
    ZmRef<Token> token = new Token{
      this, id, progressToken, logLevel, generation};
    m_hash.add(CompletionEntry{id, generation, token});
    impl()->made(static_cast<Req *>(nullptr), token.ptr());
    return token;
  }

  template <typename Req, typename Res>
  bool complete(
      Completion_<Req, CompletionSet, CompletionHeap<Req, CompletionSet>>
	*token,
      ToolReply<Res> reply) {
    return impl()->completion(token, ZuMv(reply));
  }

  template <typename Req>
  void cancelled(
      Completion_<Req, CompletionSet, CompletionHeap<Req, CompletionSet>>
	*token, ZuCSpan reason) {
    try {
      impl()->cancelled(static_cast<Req *>(nullptr), token, reason);
    } catch (...) {
    }
  }

  template <typename Req>
  bool progress(
      Completion_<Req, CompletionSet, CompletionHeap<Req, CompletionSet>>
	*token,
      double progress_, double total, ZuCSpan message) {
    return impl()->progression(token, progress_, total, message);
  }

  template <typename Req>
  bool log(
      Completion_<Req, CompletionSet, CompletionHeap<Req, CompletionSet>>
	*token,
      ZuCSpan level, ZuCSpan data, ZuCSpan logger) {
    return impl()->logging(token, level, data, logger);
  }

  bool cancel(const ID &id, ZuCSpan reason = {}) {
    auto entry = m_hash.findPtr(id);
    if (!entry) return false;
    entry->cancel(reason);
    return true;
  }

  void cancelAll(ZuCSpan reason = {}) {
    CompletionEntry *entry = nullptr;
    {
      auto i = m_hash.iter();
      if (auto node = i()) entry = &node->data();
    }
    if (entry) entry->cancel(reason);
    ZiAssert(m_hash.count_() <= 1, "Zmcp", (),
	"more than one completion for one request", return);
  }

  bool fail(const ID &id) {
    auto entry = m_hash.del(id);
    if (!entry) return false;
    entry->invalidate();
    return true;
  }

  bool close(unsigned limit) {
    if (m_state == CompletionState::Open) m_state = CompletionState::Closing;
    unsigned visited = 0;
    while (visited < limit) {
      auto entry = take_();
      if (!entry) {
	m_state = CompletionState::Closed;
	return true;
      }
      ++visited;
      entry->invalidate();
    }
    if (m_hash.count_()) return false;
    m_state = CompletionState::Closed;
    return true;
  }

  void drain() {
    bool closed = close(1);
    ZiAssert(closed, "Zmcp", (),
	"more than one completion for one request", return);
  }

protected:
  template <typename Req, typename Res>
  bool complete_(
      Completion_<Req, CompletionSet, CompletionHeap<Req, CompletionSet>>
	*token,
      ToolReply<Res> reply) {
    auto entry = m_hash.findPtr(token->id());
    if (!entry || entry->object != token ||
	entry->generation != token->generation()) return false;
    auto removed = m_hash.delNode(entry);
    token->invalidate_();
    try {
      impl()->complete(token->id(), ZuMv(reply));
    } catch (...) {
      return false;
    }
    return true;
  }

  template <typename Req>
  bool progress_(
      Completion_<Req, CompletionSet, CompletionHeap<Req, CompletionSet>>
	*token,
      double progress_, double total, ZuCSpan message) {
    try {
      return impl()->progress(
	static_cast<Req *>(nullptr), token,
	progress_, total, message);
    } catch (...) {
      return false;
    }
  }

  template <typename Req>
  bool log_(
      Completion_<Req, CompletionSet, CompletionHeap<Req, CompletionSet>>
	*token,
      ZuCSpan level, ZuCSpan data, ZuCSpan logger) {
    try {
      return impl()->log(
	static_cast<Req *>(nullptr), token, level, data, logger);
    } catch (...) {
      return false;
    }
  }

private:
  CompletionHash::NodeMvRef take_() {
    auto i = m_hash.iter();
    if (!i()) return decltype(i.del()){};
    return i.del();
  }

  CompletionHash	m_hash;
  uint64_t	m_generation = 1;
  unsigned	m_maxPending;
  int		m_state = CompletionState::Open;
};

namespace LegacyState {
  enum { Fresh, Initializing, Ready, Closing, Closed };
}

using MethodIDs = ZuStringTL<
  "server/discover",
  "initialize",
  "notifications/initialized",
  "ping",
  "tools/list",
  "tools/call",
  "notifications/cancelled",
  "notifications/progress",
  "notifications/message",
  "logging/setLevel">;
struct MethodID { using Keys = MethodIDs; };

struct EmptyResult {
  ServerMetaOpt meta;
  ZuCSpan resultType;
};
ZfStruct(ZmcpAPI, (EmptyResult, JSON),
  (((meta), (Mutable, JSON::Opt, JSON::ID<"_meta">)),	UDT),
  (((resultType), (Mutable, JSON::Opt)),		String));

inline EmptyResult emptyResult(int era)
{
  return {serverMeta(era),
    era == Era::Modern ? ZuCSpan{"complete"} : ZuCSpan{}};
}

struct SupportedVersions {
  struct Iterator {
    unsigned i;

    int operator -(Iterator r) const { return int(i) - int(r.i); }
    ZuCSpan operator *() const {
      return i ? LegacyVersion{}() : ModernVersion{}();
    }
  };

  Iterator begin() const { return {0}; }
  Iterator end() const { return {2}; }
  ZuCSpan operator [](unsigned i) const {
    return i ? LegacyVersion{}() : ModernVersion{}();
  }

  friend inline ZfJSON::AsArray<ZfFieldTC::String>
    ZfJSON_Fmt(SupportedVersions *) { return {}; }
};

struct RxVersions :
    public ZtArray<ZuCSpan, ZtArrayHeapID<"Zmcp.Rx.Versions">> {
  ZuDerive_(RxVersions,
    (ZtArray<ZuCSpan, ZtArrayHeapID<"Zmcp.Rx.Versions">>));
  friend inline ZfJSON::AsArray<ZfFieldTC::String>
    ZfJSON_Fmt(RxVersions *) { return {}; }
};

struct DiscoverRxResult {
  RxVersions supportedVersions;
};
ZfStruct(ZmcpAPI, (DiscoverRxResult, JSON),
  (((supportedVersions), (Mutable)),	UDT));

struct ToolsCapability {
  EmptyObject tools;
};
ZfStruct(ZmcpAPI, (ToolsCapability, JSON),
  (((tools), (Mutable)),	UDT));

struct LegacyCapabilities {
  EmptyObject tools;
  EmptyObject logging;
};
ZfStruct(ZmcpAPI, (LegacyCapabilities, JSON),
  (((tools), (Mutable)),	UDT),
  (((logging), (Mutable)),	UDT));

struct DiscoverResult {
  SupportedVersions supportedVersions;
  ToolsCapability capabilities;
  ServerMetaOpt meta;
  ZuCSpan resultType;
  ZuCSpan cacheScope;
  int ttlMs = CatalogTTL;
};
ZfStruct(ZmcpAPI, (DiscoverResult, JSON),
  (((supportedVersions), (Mutable)),		UDT),
  (((capabilities), (Mutable)),			UDT),
  (((meta), (Mutable, JSON::ID<"_meta">)),	UDT),
  (((resultType), (Mutable)),			String),
  (((cacheScope), (Mutable)),			String),
  (((ttlMs), (Mutable)),			Int32));

struct InitializeResult {
  ZuCSpan protocolVersion;
  LegacyCapabilities capabilities;
  PeerInfo serverInfo;
};
ZfStruct(ZmcpAPI, (InitializeResult, JSON),
  (((protocolVersion), (Mutable)),	String),
  (((capabilities), (Mutable)),		UDT),
  (((serverInfo), (Mutable)),		UDT));

struct EmptyResultMessage {
  ID id;
  int era = Era::Modern;

  template <typename S>
  void write(S &out) const {
    saveResult(out, id, emptyResult(era));
  }
};

struct DiscoverMessage {
  ID id;

  template <typename S>
  void write(S &out) const {
    saveResult(out, id, DiscoverResult{
      {}, {}, serverMeta(Era::Modern), "complete", "public", CatalogTTL});
  }
};

struct InitializeMessage {
  ID id;

  template <typename S>
  void write(S &out) const {
    saveResult(out, id, InitializeResult{
      LegacyVersion{}(), {}, {"zmcp", Z_VERNAME}});
  }
};

template <typename Reqs>
struct ToolsListMessage {
  ID id;
  int era = Era::Modern;

  template <typename S>
  void write(S &out) const {
    saveResult(out, id, SchemaModel_::ToolsResult<Reqs>{era});
  }
};

struct ErrorMessage {
  ID id;
  ZuCSpan message;
  int code = ErrorCode::Internal;

  template <typename S>
  void write(S &out) const { saveError(out, id, code, message); }
};

struct ToolErrorMessage {
  ID id;
  int code = ErrorCode::Internal;
  int era = Era::Modern;

  template <typename S>
  void write(S &out) const { saveToolError(out, id, code, era); }
};

struct ProgressMessage {
  ID token;
  ErrorString message;
  double progress = 0;
  double total = ZuCmp<double>::null();

  template <typename S>
  void write(S &out) const {
    saveNotification(out, "notifications/progress",
      ProgressParams{token, progress, total, message});
  }
};

struct LogMessage {
  ErrorString level;
  ErrorString data;
  ErrorString logger;

  template <typename S>
  void write(S &out) const {
    saveNotification(out, "notifications/message",
      LogParams{level, logger, data});
  }
};

template <typename Res>
struct ToolReplyMessage {
  ID id;
  ToolReply<Res> reply;
  int era = Era::Modern;

  template <typename S>
  void write(S &out) const { saveToolReply(out, id, reply, era); }
};

template <typename Reqs>
class Peer {
public:
  Peer() = default;
  Peer(Limits limits) : m_limits{limits} { }

  int era() const { return m_era; }
  int legacyState() const { return m_legacyState; }

  void statelessLegacy() {
    if (m_legacyState == LegacyState::Closed) return;
    m_era = Era::Legacy;
    m_legacyState = LegacyState::Ready;
  }

  void close() {
    m_legacyState = LegacyState::Closed;
  }

  template <typename S, typename Tool>
  bool receive(ZuSpan<char> input, S &out, Tool &&tool) {
    return dispatch(input, [&out](const auto &message) {
      message.write(out);
    }, ZuFwd<Tool>(tool));
  }

  template <typename Emit, typename Tool>
  bool dispatch(ZuSpan<char> input, Emit &&emit, Tool &&tool) {
    if (m_legacyState == LegacyState::Closed) return false;
    auto parsed = parse<Reqs>(input, m_limits.maxJSONBytes);
    if (!parsed) return false;
    return dispatch(parsed.envelope, ZuFwd<Emit>(emit), ZuFwd<Tool>(tool));
  }

  template <typename Emit, typename Tool>
  bool dispatch(const Envelope<Reqs> &envelope, Emit &&emit, Tool &&tool) {
    if (m_legacyState == LegacyState::Closed) return false;
    auto call = [this, &emit, &tool](const Envelope<Reqs> &envelope) {
      call_(emit, envelope, tool);
    };
    auto cancel = [](const Envelope<Reqs> &) { };
    return route_(envelope, emit, tool, call, cancel);
  }

  template <typename Completions, typename Emit, typename Tool>
  bool dispatchAsync(
      ZuSpan<char> input, Completions &completions,
      Emit &&emit, Tool &&tool) {
    if (m_legacyState == LegacyState::Closed) return false;
    auto parsed = parse<Reqs>(input, m_limits.maxJSONBytes);
    if (!parsed) return false;
    return dispatchAsync(
      parsed.envelope, completions, ZuFwd<Emit>(emit), ZuFwd<Tool>(tool));
  }

  template <typename Completions, typename Emit, typename Tool>
  bool dispatchAsync(
      const Envelope<Reqs> &envelope, Completions &completions,
      Emit &&emit, Tool &&tool) {
    if (m_legacyState == LegacyState::Closed) return false;
    auto call = [this, &completions, &emit, &tool](
        const Envelope<Reqs> &envelope) {
      callAsync_(completions, emit, envelope, tool);
    };
    auto cancel = [&completions](const Envelope<Reqs> &envelope) {
      auto params = loadObject<CancelledParams>(raw(envelope.params()));
      if (!params.requestID.absent())
	(void)completions.cancel(params.requestID, params.reason);
    };
    return route_(envelope, emit, tool, call, cancel);
  }

private:
  template <typename Emit, typename Tool, typename Call, typename Cancel>
  bool route_(
      const Envelope<Reqs> &envelope, Emit &emit, Tool &,
      Call &call, Cancel &cancel) {
    if (envelope.kind != MessageKind::Request &&
	envelope.kind != MessageKind::Notification)
      return true;
    constexpr auto matcher = ZuMatcher<MethodID>();
    int method = matcher.exact(envelope.method());
    switch (method) {
      case 0: discover_(emit, envelope.id()); break;
      case 1: initialize_(emit, envelope.id()); break;
      case 2:
	if (m_era == Era::Legacy &&
	    m_legacyState == LegacyState::Initializing)
	  m_legacyState = LegacyState::Ready;
	break;
      case 3:
	if (envelope.kind == MessageKind::Request)
	  emptyResult_(emit, envelope.id());
	break;
      case 4:
	if (usable_() && envelope.kind == MessageKind::Request)
	  tools_(emit, envelope.id());
	else
	  unsupported_(emit, envelope);
	break;
      case 5:
	if (usable_() && envelope.kind == MessageKind::Request)
	  call(envelope);
	else
	  unsupported_(emit, envelope);
	break;
      case 6:
	cancel(envelope);
	break;
      case 7:
      case 8:
	break;
      case 9:
	if (m_era == Era::Legacy && envelope.kind == MessageKind::Request) {
	  auto params = loadObject<SetLevelParams>(raw(envelope.params()));
	  if (params.level) m_logLevel = Zmcp::logLevel(params.level);
	  emptyResult_(emit, envelope.id());
	} else
	  unsupported_(emit, envelope);
	break;
      default: unsupported_(emit, envelope); break;
    }
    return true;
  }
  bool usable_() const {
    return m_era != Era::Legacy || m_legacyState == LegacyState::Ready;
  }

  template <typename Emit>
  void discover_(Emit &emit, const ID &id) {
    m_era = Era::Modern;
    emit(DiscoverMessage{id});
  }

  template <typename Emit>
  void initialize_(Emit &emit, const ID &id) {
    m_era = Era::Legacy;
    m_legacyState = LegacyState::Initializing;
    emit(InitializeMessage{id});
  }

  template <typename Emit>
  void emptyResult_(Emit &emit, const ID &id) const {
    emit(EmptyResultMessage{id, responseEra_()});
  }

  template <typename Emit>
  void tools_(Emit &emit, const ID &id) const {
    emit(ToolsListMessage<Reqs>{id,
      m_era == Era::Legacy ? Era::Legacy : Era::Modern});
  }

  template <typename Emit>
  static void unsupported_(Emit &emit, const Envelope<Reqs> &envelope) {
    if (envelope.kind == MessageKind::Request)
      emit(ErrorMessage{
	envelope.id(), "Method not found", ErrorCode::MethodNotFound});
  }

  template <typename Emit, typename Tool>
  void call_(Emit &emit, const Envelope<Reqs> &envelope, Tool &tool) const {
    if constexpr (!Reqs::N) {
      emit(ToolErrorMessage{envelope.id(), 404, responseEra_()});
      return;
    } else {
      using Params = ToolsCallParams<Reqs>;
      auto handler = ZfJSON::handler<Params>(raw(envelope.params()));
      if (!handler.valid) {
	emit(ToolErrorMessage{envelope.id(), 404, responseEra_()});
	return;
      }
      auto params = handler.ctor();
      int index = params.match();
      if (index < 0) {
	emit(ToolErrorMessage{envelope.id(), 404, responseEra_()});
	return;
      }
      if (params.narrow() < 0) {
	emit(ToolErrorMessage{
	  envelope.id(), ErrorCode::Internal, responseEra_()});
	return;
      }
      int era = responseEra_();
      bool completed = false;
      try {
	ZuSwitch::dispatch<Reqs::N>(index,
	    [&emit, &envelope, &params, &tool, &completed, era](auto I) {
	  static constexpr unsigned ReqI = I;
	  using Req = ZuType<ReqI, Reqs>;
	  const auto &request =
	    params.arguments().template p<ToolArg<Req>>().object();
	  auto complete = [&emit, &envelope, &completed, era](auto &&reply) {
	    using Reply = ZuDecay<decltype(reply)>;
	    using Res = typename Reply::Response;
	    ZuAssert((ZuTypeIn<Res, GetResponses<Req>>{}));
	    if (ZuUnlikely(completed)) return;
	    completed = true;
	    emit(ToolReplyMessage<Res>{
	      envelope.id(), ZuFwd<decltype(reply)>(reply), era});
	  };
	  tool(static_cast<Req *>(nullptr), request, complete);
	});
      } catch (const Error &error) {
	if (!completed)
	  emit(ToolErrorMessage{
	    envelope.id(), error.code, responseEra_()});
	return;
      } catch (...) {
	if (!completed)
	  emit(ToolErrorMessage{
	    envelope.id(), ErrorCode::Internal, responseEra_()});
	return;
      }
      if (!completed)
	emit(ToolErrorMessage{
	  envelope.id(), ErrorCode::Internal, responseEra_()});
    }
  }

  template <typename Completions, typename Emit, typename Tool>
  void callAsync_(
      Completions &completions, Emit &emit,
      const Envelope<Reqs> &envelope, Tool &tool) {
    if constexpr (!Reqs::N) {
      emit(ToolErrorMessage{envelope.id(), 404, responseEra_()});
      return;
    } else {
      using Params = ToolsCallParams<Reqs>;
      auto handler = ZfJSON::handler<Params>(raw(envelope.params()));
      if (!handler.valid) {
	emit(ToolErrorMessage{envelope.id(), 404, responseEra_()});
	return;
      }
      auto params = handler.ctor();
      int index = params.match();
      if (index < 0) {
	emit(ToolErrorMessage{envelope.id(), 404, responseEra_()});
	return;
      }
      if (params.narrow() < 0) {
	emit(ToolErrorMessage{
	  envelope.id(), ErrorCode::Internal, responseEra_()});
	return;
      }
      int logLevel_ = m_era == Era::Legacy ? m_logLevel : params.logLevel;
      int era = responseEra_();
      try {
	ZuSwitch::dispatch<Reqs::N>(index,
	    [&completions, &emit, &envelope, &params, &tool,
	      logLevel_, era](auto I) {
	  using Req = ZuType<I, Reqs>;
	  const auto &request =
	    params.arguments().template p<ToolArg<Req>>().object();
	  auto completion = completions.template make<Req>(
	    envelope.id(), params.progressToken, logLevel_);
	  if (!completion) {
	    emit(ToolErrorMessage{
	      envelope.id(), ErrorCode::Internal, era});
	    return;
	  }
	  tool(static_cast<Req *>(nullptr), request, ZuMv(completion));
	});
      } catch (const Error &error) {
	(void)completions.fail(envelope.id());
	emit(ToolErrorMessage{
	  envelope.id(), error.code, responseEra_()});
      } catch (...) {
	(void)completions.fail(envelope.id());
	emit(ToolErrorMessage{
	  envelope.id(), ErrorCode::Internal, responseEra_()});
      }
    }
  }

  int responseEra_() const {
    return m_era == Era::Legacy ? Era::Legacy : Era::Modern;
  }

  Limits	m_limits;
  int		m_era = Era::Unknown;
  int		m_legacyState = LegacyState::Fresh;
  int		m_logLevel = LogLevel::Disabled;
};

ZuDerive(SSELine, (ZtString<ZtStringHeapID<"Zmcp.SSE.Line">>));
ZuDerive(SSEData, (ZtString<ZtStringHeapID<"Zmcp.SSE.Data">>));
ZuDerive(SSEID, (ZtString<ZtStringHeapID<"Zmcp.SSE.ID">>));

struct SSEEvent {
  ZuCSpan data;
  ZuCSpan id;
  int64_t retry = -1;
};

class SSEDecoder {
public:
  enum { Open, Closed };

  SSEDecoder() = default;
  SSEDecoder(unsigned maxLine, unsigned maxData) { reset(maxLine, maxData); }

  void reset(unsigned maxLine, unsigned maxData) {
    m_line.length_(0);
    reset_();
    m_maxLine = maxLine;
    m_maxData = maxData;
    m_state = Open;
  }

  int state() const { return m_state; }
  void close() {
    m_state = Closed;
    m_line.length_(0);
    reset_();
  }

  template <typename L>
  bool feed(ZuCSpan input, L &&l) {
    if (m_state == Closed) return false;
    unsigned n = input.length();
    unsigned begin = 0;
    for (unsigned i = 0; i < n; ++i) {
      if (input[i] != '\n') continue;
      if (!append_(ZuCSpan(input.data() + begin, i - begin))) return false;
      line_(l);
      if (m_state == Closed) return false;
      begin = i + 1;
    }
    if (begin == n) return true;
    return append_(ZuCSpan(input.data() + begin, n - begin));
  }

private:
  bool append_(ZuCSpan span) {
    unsigned lineLength = m_line.length();
    if (ZuUnlikely(lineLength > m_maxLine ||
	span.length() > m_maxLine - lineLength)) {
      close();
      return false;
    }
    m_line << span;
    return true;
  }

  void reset_() {
    m_data.length_(0);
    m_id.length_(0);
    m_retry = -1;
  }

  template <typename L>
  void line_(L &l) {
    ZuCSpan line{m_line};
    line.chomp([](char c) { return c == '\r'; });
    if (!line) {
      if (m_data.length()) l(SSEEvent{m_data, m_id, m_retry});
      reset_();
      m_line.length_(0);
      return;
    }
    if (line[0] == ':') {
      m_line.length_(0);
      return;
    }
    unsigned lineLength = line.length();
    unsigned colon = 0;
    while (colon < lineLength && line[colon] != ':') ++colon;
    ZuCSpan field{line.data(), colon};
    ZuCSpan value;
    if (colon < lineLength) {
      value = ZuCSpan(line.data() + colon + 1,
	  lineLength - colon - 1);
      if (value && value[0] == ' ') value.offset(1);
    }
    if (field == "data") {
      unsigned dataLength = m_data.length();
      unsigned valueLength = value.length();
      unsigned separator = bool(dataLength);
      if (ZuUnlikely(separator > m_maxData ||
	  valueLength > m_maxData - separator ||
	  dataLength > m_maxData - separator - valueLength)) {
	close();
	return;
      }
      if (dataLength) m_data << '\n';
      m_data << value;
    } else if (field == "id") {
      m_id = value;
    } else if (field == "retry") {
      int64_t retry = ZuBox<int64_t>{value};
      if (retry >= 0) m_retry = retry;
    }
    m_line.length_(0);
  }

  SSELine	m_line;
  SSEData	m_data;
  SSEID		m_id;
  unsigned	m_maxLine = 0;
  unsigned	m_maxData = 0;
  int64_t	m_retry = -1;
  int		m_state = Open;
};

template <typename S>
inline void saveSSE(S &s, ZuCSpan id, int64_t retry, ZuCSpan json)
{
  if (id) s << "id: " << id << '\n';
  if (retry >= 0) s << "retry: " << retry << '\n';
  s << "data: " << json << "\n\n";
}

ZuDerive(HTTPBodyBuf, (ZiIOBufAlloc<
    ZiIOBuf_DefltSize, ZiIOBuf_DefltMaxSize, "Zmcp.HTTP.Body">));
ZuDerive(HTTPValue, (ZtString<ZtStringHeapID<"Zmcp.HTTP.Value">>));

template <typename U, typename = void>
struct AppHeaders_ { using T = ZuTypeList<>; };
template <typename U>
struct AppHeaders_<U, decltype(sizeof(typename U::Headers), void())> {
  using T = typename U::Headers;
};
template <typename U> using AppHeaders = typename AppHeaders_<U>::T;

struct HTTPHeader {
  ZuCSpan value;
  unsigned count = 0;

  explicit operator bool() const { return count; }
};

struct HTTPHeaderSlot {
  HTTPValue value;
  unsigned count = 0;
};

template <typename> using HTTPHeaderSlot_ = HTTPHeaderSlot;

template <typename Headers>
class HTTPHeaders {
  using Keys = ZuTypeSlice<2, 0, Headers>;
  using Slots = ZuTypeApply<ZuTuple, ZuTypeMap<HTTPHeaderSlot_, Keys>>;

public:
  template <typename Key>
  void put(ZuCSpan value) {
    auto &slot = m_slots.template p<ZuTypeIndex<Key, Keys>{}>();
    slot.value = value;
    ++slot.count;
  }

  template <typename Key>
  HTTPHeader get() const {
    const auto &slot = m_slots.template p<ZuTypeIndex<Key, Keys>{}>();
    return {slot.value, slot.count};
  }

private:
  Slots m_slots;
};

struct HTTPMeta {
  HTTPValue origin;
  HTTPValue sessionID;
  HTTPValue version;
  HTTPValue method;
  HTTPValue name;
};

namespace ParserState {
  enum { Empty, Receiving, OriginRejected, Accepted, OverLimit };
}

template <typename Impl>
class HTTPParser : public Zhttp::Parser {
public:
  using Headers = ZuTypeConcat<ZhttpHeaders("origin"), RoutingHeaders>;
  using HdrCatalog = HTTPHdrCatalog<Headers>;
  using Origin = ZuStringT<"origin">;
  using Session = SessionID;
  using Version = ProtocolVersion;
  using Method = MethodHeader;
  using Name = NameHeader;

  Impl *impl() { return static_cast<Impl *>(this); }
  const Impl *impl() const { return static_cast<const Impl *>(this); }

  template <typename Context>
  void init(Context &) { }

  bool operation(Zhttp::Method::T method, Zhttp::Target &target) {
    if (target.path != impl()->endpoint()) return false;
    if (method != Zhttp::Method::POST && method != Zhttp::Method::DELETE)
      return false;
    m_method = method;
    m_state = ParserState::Receiving;
    return true;
  }

  template <typename Key, typename Value>
  void header(Zhttp::FieldSection::T) { }

  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (ZuIsSame<Key, Origin>{})
      m_meta.origin = value;
    else if constexpr (ZuIsSame<Key, Session>{})
      m_meta.sessionID = value;
    else if constexpr (ZuIsSame<Key, Version>{})
      m_meta.version = value;
    else if constexpr (ZuIsSame<Key, Method>{})
      m_meta.method = value;
    else if constexpr (ZuIsSame<Key, Name>{})
      m_meta.name = value;
  }

  void header(Zhttp::FieldSection::T, ZuBSpan, ZuSpan<uint8_t>) { }

  bool bodyInfo(Zhttp::BodyType::T, uint64_t length) {
    if (!impl()->origin(m_meta.origin)) {
      m_state = ParserState::OriginRejected;
      return false;
    }
    if (ZuUnlikely(length > impl()->limits().maxJSONBytes)) {
      m_state = ParserState::OverLimit;
      return false;
    }
    m_body = new HTTPBodyBuf{};
    if (length && !m_body->alloc(length)) {
      m_state = ParserState::OverLimit;
      return false;
    }
    return true;
  }

  template <typename Rx>
  bool body(Rx &rx) {
    if (m_state != ParserState::Receiving || !m_body) return false;
    rx.consume(
      [](ZuSpan<uint8_t> span) -> int64_t { return span.length(); },
	[this](ZuSpan<uint8_t> span) {
	  unsigned max = impl()->limits().maxJSONBytes;
	  unsigned bodyLength = m_body->length;
	  if (ZuUnlikely(bodyLength > max ||
	      span.length() > max - bodyLength)) {
	    m_state = ParserState::OverLimit;
	    m_body = nullptr;
	    return;
	  }
	  m_body->append(span);
	});
    return m_state == ParserState::Receiving;
  }

  template <typename Link>
  void complete(Link *link, bool ok) {
    if (m_state == ParserState::OriginRejected) {
      impl()->originHTTP(link);
      return;
    }
    if (m_state == ParserState::OverLimit) {
      impl()->corruptHTTP(link);
      return;
    }
    if (!ok) return;
    if (m_method == Zhttp::Method::DELETE) {
      impl()->deleteHTTP(link, m_meta);
      return;
    }
    impl()->receiveHTTP(link, m_body, ZuMv(m_meta));
  }

  void reset() {
    m_body = nullptr;
    m_meta = {};
    m_method = {};
    m_state = ParserState::Empty;
  }

private:
  ZmRef<ZiIOBuf>	m_body;
  HTTPMeta	m_meta;
  Zhttp::Method::T m_method{};
  int		m_state = ParserState::Empty;

};

struct HTTPResponseMeta {
  HTTPValue sessionID;
  HTTPValue version;
};

template <typename Impl>
class HTTPResponseParser : public Zhttp::Parser {
public:
  using Headers = ZuTypeConcat<ZhttpHeaders(
    ("content-type", ("application/json", "text/event-stream"))),
    SessionHeaders>;
  using HdrCatalog = HTTPHdrCatalog<Headers>;
  using ContentType = ZuStringT<"content-type">;
  using JSONContent = ZuStringT<"application/json">;
  using SSEContent = ZuStringT<"text/event-stream">;
  using Session = SessionID;
  using Version = ProtocolVersion;

  Impl *impl() { return static_cast<Impl *>(this); }
  const Impl *impl() const { return static_cast<const Impl *>(this); }

  void streaming(bool v) { m_streaming = v; }
  void status(unsigned status) { m_status = status; }

  template <typename Key, typename Value>
  void header(Zhttp::FieldSection::T) {
    if constexpr (ZuIsSame<Key, ContentType>{}) {
      if constexpr (ZuIsSame<Value, JSONContent>{})
	m_streaming = false;
      else if constexpr (ZuIsSame<Value, SSEContent>{})
	m_streaming = true;
    }
  }

  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (ZuIsSame<Key, Session>{})
      m_meta.sessionID = value;
    else if constexpr (ZuIsSame<Key, Version>{})
      m_meta.version = value;
  }

  void header(Zhttp::FieldSection::T, ZuBSpan, ZuSpan<uint8_t>) { }

  bool bodyInfo(Zhttp::BodyType::T, uint64_t length) {
    if (m_status == 202 && !length) {
      m_state = ParserState::Accepted;
      return true;
    }
    m_state = ParserState::Receiving;
    if (m_streaming) {
      m_sse.reset(
	impl()->limits().maxSSELineBytes,
	impl()->limits().maxSSEEventBytes);
      return true;
    }
    if (ZuUnlikely(length > impl()->limits().maxJSONBytes)) {
      m_state = ParserState::OverLimit;
      return false;
    }
    m_body = new HTTPBodyBuf{};
    if (length && !m_body->alloc(length)) {
      m_state = ParserState::OverLimit;
      return false;
    }
    return true;
  }

  template <typename Rx>
  bool body(Rx &rx) {
    if (m_state != ParserState::Receiving) return false;
    rx.consume(
      [](ZuSpan<uint8_t> span) -> int64_t { return span.length(); },
      [this](ZuSpan<uint8_t> span) {
	if (m_streaming) {
	  bool ok = m_sse.feed(span, [this](const SSEEvent &event) {
	    if (!impl()->receiveSSE(event, m_meta)) {
	      m_state = ParserState::OverLimit;
	      m_sse.close();
	    }
	  });
	  if (ZuUnlikely(!ok)) m_state = ParserState::OverLimit;
	  return;
	}
	unsigned max = impl()->limits().maxJSONBytes;
	unsigned bodyLength = m_body->length;
	if (ZuUnlikely(bodyLength > max ||
	    span.length() > max - bodyLength)) {
	  m_state = ParserState::OverLimit;
	  m_body = nullptr;
	  return;
	}
	m_body->append(span);
      });
    return m_state == ParserState::Receiving;
  }

  template <typename Link>
  void complete(Link *link, bool ok) {
    if (m_state == ParserState::OverLimit) {
      impl()->corruptHTTPResponse(link);
      return;
    }
    if (!ok) {
      impl()->failedHTTPResponse(link);
      return;
    }
    if (m_status == 202 &&
	(m_state == ParserState::Empty ||
	  m_state == ParserState::Accepted)) {
      impl()->acceptedHTTPResponse(link, m_status, m_meta);
      return;
    }
    if (m_streaming)
      impl()->completeHTTPResponse(link, m_status, m_meta);
    else
      impl()->receiveHTTPResponse(link, m_status, m_body, m_meta);
  }

  void reset() {
    m_body = nullptr;
    m_meta = {};
    m_status = 0;
    m_streaming = false;
    m_state = ParserState::Empty;
    m_sse.close();
  }

private:
  ZmRef<ZiIOBuf>	m_body;
  HTTPResponseMeta m_meta;
  SSEDecoder		m_sse;
  unsigned		m_status = 0;
  bool			m_streaming = false;
  int			m_state = ParserState::Empty;

};

template <typename Out>
class HTTPOutput {
public:
  HTTPOutput(Out &out_, uint64_t max_) : m_out{out_}, m_max{max_} { }

  bool failed() const { return m_overflow || m_out.failed(); }
  explicit operator bool() const { return !failed(); }
  uint64_t produced() const { return m_produced; }

  bool flush() {
    if (!m_out.flush()) m_overflow = true;
    return !failed();
  }

  HTTPOutput &operator <<(ZuBSpan value) {
    if (failed()) return *this;
    unsigned length = value.length();
    if (ZuUnlikely(length > m_max - m_produced)) {
      m_overflow = true;
      return *this;
    }
    m_out << value;
    if (m_out.failed()) { m_overflow = true; return *this; }
    m_produced += length;
    return *this;
  }

  template <typename C, typename = ZuSame<C, char>>
  HTTPOutput & operator <<(C value) {
    return *this << ZuSpan{&value, 1};
  }

  template <typename R,
    typename = ZuIfT<
      (ZuTraits<R>::IsPrimitive && ZuTraits<R>::IsReal && !ZuIsSame<R, char>{}) ||
      (ZuPrint<R>::OK && !ZuPrint<R>::String)>>
  HTTPOutput &
  operator <<(const R &value) {
    if constexpr (ZuTraits<R>::IsPrimitive && ZuTraits<R>::IsReal && !ZuIsSame<R, char>{}) {
      return append_(ZuBoxed(value));
    } else {
      return append_(value);
    }
  }

private:

  template <typename P,
    typename = ZuIfT<(ZuPrint<P>::Delegate) || (ZuPrint<P>::Buffer)>>
  HTTPOutput &append_(const P &value) {
    if constexpr (ZuPrint<P>::Delegate) {
      if (!failed()) ZuPrint<P>::print(*this, value);
      return *this;
    } else {
      if (failed()) return *this;
      uint64_t length = ZuPrint<P>::length(value);
      if (ZuUnlikely(length > m_max - m_produced)) {
	m_overflow = true;
	return *this;
      }
      m_out << value;
      if (m_out.failed()) { m_overflow = true; return *this; }
      m_produced += length;
      return *this;
    }
  }

  Out		&m_out;
  uint64_t	m_max;
  uint64_t	m_produced = 0;
  bool		m_overflow = false;
};

namespace Private {
  ZmcpExtern uintptr_t stdioThread();
  ZmcpExtern void interruptStdio(uintptr_t);
  ZmcpExtern void closeStdioThread(uintptr_t);
}

class StdioConfig {
public:
  StdioConfig() :
    m_input{ZiFile::stdIn().handle()},
    m_output{ZiFile::stdOut().handle()} { }

  const Limits &limits() const { return m_limits; }
  ZuCSpan rxThread() const { return m_rxThread; }
  ZuCSpan txThread() const { return m_txThread; }
  Zi::Handle input() const { return m_input; }
  Zi::Handle output() const { return m_output; }

  StdioConfig &limits(Limits v) { m_limits = ZuMv(v); return *this; }
  StdioConfig &rxThread(ZuCSpan v) { m_rxThread = v; return *this; }
  StdioConfig &txThread(ZuCSpan v) { m_txThread = v; return *this; }
  StdioConfig &input(Zi::Handle v) { m_input = v; return *this; }
  StdioConfig &output(Zi::Handle v) { m_output = v; return *this; }

private:
  Limits	m_limits;
  ZuID		m_rxThread;
  ZuID		m_txThread;
  Zi::Handle	m_input = Zi::nullHandle();
  Zi::Handle	m_output = Zi::nullHandle();
};

using StdioBuf = ZiIOBufAlloc<ZiIOBuf_DefltSize,
  ZiIOBuf_DefltMaxSize, "Zmcp.Stdio.Buf">;

class StdioOutput {
public:
  StdioOutput(ZiIOBuf &buf_, uint64_t max_) : m_buf{buf_}, m_max{max_} { }

  bool failed() const { return m_overflow || m_buf.failed(); }
  explicit operator bool() const { return !failed(); }

  StdioOutput &operator <<(ZuBSpan value) {
    if (failed()) return *this;
    unsigned length = value.length();
    unsigned offset = m_buf.length;
    if (ZuUnlikely(offset > m_max || length > m_max - offset)) {
      m_overflow = true;
      return *this;
    }
    if (!m_buf.append(value)) m_overflow = true;
    return *this;
  }

  template <typename C, typename = ZuSame<C, char>>
  StdioOutput & operator <<(C value) {
    return *this << ZuSpan{&value, 1};
  }

  template <typename R,
    typename = ZuIfT<
      (ZuTraits<R>::IsPrimitive && ZuTraits<R>::IsReal && !ZuIsSame<R, char>{}) ||
      (ZuPrint<R>::OK && !ZuPrint<R>::String)>>
  StdioOutput &
  operator <<(const R &value) {
    if constexpr (ZuTraits<R>::IsPrimitive && ZuTraits<R>::IsReal && !ZuIsSame<R, char>{}) {
      return append_(ZuBoxed(value));
    } else {
      return append_(value);
    }
  }

private:

  template <typename P,
    typename = ZuIfT<(ZuPrint<P>::Delegate) || (ZuPrint<P>::Buffer)>>
  StdioOutput &append_(const P &value) {
    if constexpr (ZuPrint<P>::Delegate) {
      if (!failed()) ZuPrint<P>::print(*this, value);
      return *this;
    } else {
      if (failed()) return *this;
      unsigned length = ZuPrint<P>::length(value);
      unsigned offset = m_buf.length;
      if (ZuUnlikely(offset > m_max || length > m_max - offset)) {
	m_overflow = true;
	return *this;
      }
      auto data = m_buf.ensure(offset + length);
      if (ZuUnlikely(!data)) {
	m_overflow = true;
	return *this;
      }
      ZuSpan<char> span{data + offset, length};
      m_buf.length += ZuPrint<P>::print(span.data(), length, value);
      return *this;
    }
  }

  ZiIOBuf	&m_buf;
  uint64_t	m_max;
  bool		m_overflow = false;
};

class StdioFramer {
public:
  enum { Open, Closed };

  StdioFramer(unsigned maxLine) : m_maxLine{maxLine} { }

  int state() const { return m_state; }

  void close() {
    m_state = Closed;
    m_tail = nullptr;
  }

  template <typename L>
  bool feed(ZuCSpan input, L &&l) {
    if (m_state == Closed) return false;
    unsigned n = input.length();
    unsigned begin = 0;
    for (unsigned i = 0; i < n; ++i) {
      if (input[i] != '\n') continue;
      unsigned length = i - begin;
      unsigned tail = m_tail ? m_tail->length : 0;
      if (ZuUnlikely(tail > m_maxLine || length > m_maxLine - tail)) {
	close();
	return false;
      }
      ZmRef<ZiIOBuf> frame = new StdioBuf{};
      if (m_tail) frame->append(m_tail->cspan());
      frame->append(ZuBSpan{input}.offset(begin).trunc(length));
      if (frame->length && frame->end()[-1] == '\r') --frame->length;
      m_tail = nullptr;
      l(ZuMv(frame));
      begin = i + 1;
    }
    if (begin == n) return true;
    unsigned length = n - begin;
    unsigned tail = m_tail ? m_tail->length : 0;
    if (ZuUnlikely(tail > m_maxLine || length > m_maxLine - tail)) {
      close();
      return false;
    }
    if (!m_tail) m_tail = new StdioBuf{};
    m_tail->append(ZuBSpan{input}.offset(begin).trunc(length));
    return true;
  }

  bool eof() {
    if (m_tail && m_tail->length) {
      close();
      return false;
    }
    close();
    return true;
  }

private:
  ZmRef<ZiIOBuf>	m_tail;
  unsigned	m_maxLine;
  int		m_state = Open;
};

inline ZmRef<ZiIOBuf> stdioFrame(ZuCSpan message)
{
  ZmRef<ZiIOBuf> frame = new StdioBuf{};
  *frame << message << '\n';
  if (frame->failed()) return nullptr;
  return frame;
}

inline ZmRef<ZiIOBuf> stdioFrame(ZuCSpan message, unsigned maxLine)
{
  ZmRef<ZiIOBuf> frame = new StdioBuf{};
  StdioOutput out{*frame, uint64_t(maxLine) + 1};
  out << message << '\n';
  if (!out) return nullptr;
  return frame;
}

template <typename M>
inline ZmRef<ZiIOBuf> stdioFrame(const M &message, unsigned maxLine)
{
  ZmRef<ZiIOBuf> frame = new StdioBuf{};
  StdioOutput out{*frame, uint64_t(maxLine) + 1};
  message.write(out);
  out << '\n';
  if (!out) return nullptr;
  return frame;
}

template <typename Impl>
class StdioResponder : public CompletionSet<StdioResponder<Impl>> {
  using Base = CompletionSet<StdioResponder<Impl>>;

public:
  StdioResponder(Impl *impl_, Limits limits_) :
    Base{limits_.maxPending}, m_impl{impl_} { }

  bool terminal() const { return m_terminal; }
  void era(int era_) { m_era = era_; }

  template <typename Req, typename Token>
  void made(Req *req, Token *token) { m_impl->made(req, token); }

  bool cancel(const ID &id, ZuCSpan reason = {}) {
    return m_impl->cancel(id, reason);
  }

  bool cancelTx_(const ID &id, ZuCSpan reason = {}) {
    return Base::cancel(id, reason);
  }

  template <typename Token, typename Res>
  bool completion(Token *token, ToolReply<Res> reply) {
    return m_impl->completion(this, token, ZuMv(reply));
  }

  template <typename Token>
  bool progression(
      Token *token, double value, double total, ZuCSpan message) {
    return m_impl->progression(this, token, value, total, message);
  }

  template <typename Token>
  bool logging(
      Token *token, ZuCSpan level, ZuCSpan data, ZuCSpan logger) {
    return m_impl->logging(this, token, level, data, logger);
  }

  template <typename Token, typename Res>
  bool completeTx_(Token *token, ToolReply<Res> reply) {
    return Base::complete_(token, ZuMv(reply));
  }

  template <typename Token>
  bool progressTx_(
      Token *token, double value, double total, ZuCSpan message) {
    return Base::progress_(token, value, total, message);
  }

  template <typename Token>
  bool logTx_(
      Token *token, ZuCSpan level, ZuCSpan data, ZuCSpan logger) {
    return Base::log_(token, level, data, logger);
  }

  template <typename Res>
  void complete(const ID &id, ToolReply<Res> reply) {
    send_(ToolReplyMessage<Res>{id, ZuMv(reply), m_era}, true);
  }

  template <typename Req, typename Token>
  void cancelled(Req *req, Token *token, ZuCSpan reason) {
    m_impl->cancelled(req, token, reason);
  }

  template <typename Req, typename Token>
  bool progress(
      Req *, Token *token,
      double value, double total, ZuCSpan message) {
    return send_(ProgressMessage{
      token->progressToken(), message, value, total}, false);
  }

  template <typename Req, typename Token>
  bool log(
      Req *, Token *, ZuCSpan level, ZuCSpan data, ZuCSpan logger) {
    return send_(LogMessage{level, data, logger}, false);
  }

  template <typename M>
  void emit(M message) {
    enum { Terminal =
      !ZuIsSame<M, ProgressMessage>{} && !ZuIsSame<M, LogMessage>{} };
    (void)send_(ZuMv(message), Terminal);
  }

  void finish() {
    if (!m_terminal && !Base::count()) m_terminal = true;
  }

  void close() {
    Base::drain();
    m_terminal = true;
  }

private:
  template <typename M>
  bool send_(M message, bool terminal) {
    if (m_terminal) return false;
    bool sent = false;
    try {
      sent = m_impl->send(ZuMv(message));
    } catch (...) {
    }
    if (!sent) {
      m_terminal = true;
      try { m_impl->failed(); } catch (...) { }
      return false;
    }
    if (terminal) m_terminal = true;
    return true;
  }

  Impl	*m_impl;
  int	m_era = Era::Modern;
  bool	m_terminal = false;
};

struct StdioRecord {
  ZmRef<ZiIOBuf> buf;
};

template <ZuString ID>
using StdioQueue_ = ZmList<StdioRecord,
  ZmListNode<StdioRecord, ZmListHeapID<ID>>>;

ZuDerive(StdioRxQueue, (StdioQueue_<"Zmcp.Stdio.RxQueue">));
ZuDerive(StdioTxQueue, (StdioQueue_<"Zmcp.Stdio.TxQueue">));

namespace StdioState {
  enum { Initial, Open, Closing, Failed, Closed };
}

namespace StdioOutcome {
  enum { None, EOF_, Failed, Stopped };
}

template <typename Impl>
class StdioIO {
public:
  StdioIO(
      Impl *impl_, ZiMultiplex *mx_,
      unsigned ownerThread_, StdioConfig config) :
    m_impl{impl_}, m_mx{mx_}, m_limits{config.limits()},
    m_framer{m_limits.maxLineBytes}, m_ownerThread{ownerThread_},
    m_rxThread{mx_ ? mx_->sid(config.rxThread()) : 0},
    m_txThread{mx_ ? mx_->sid(config.txThread()) : 0}
  {
    m_input.init(config.input(), ZiFile::ReadOnly | ZiFile::GC);
    m_output.init(config.output(), ZiFile::WriteOnly | ZiFile::GC);
  }

  ~StdioIO() {
    ZiAssert(m_state == StdioState::Initial || m_state == StdioState::Closed,
	"Zmcp", (),
	"destroying active stdio transport", ());
  }

  int state() const { return m_state; }
  unsigned queued() const {
    ZmGuard<ZmPLock> guard(m_txLock);
    return m_txQueued;
  }
  uint64_t queuedBytes() const {
    ZmGuard<ZmPLock> guard(m_txLock);
    return m_txQueuedBytes;
  }

  bool start_() {
    ZiAssert(invoked_(), "Zmcp", (),
	"stdio start outside owner shard", return false);
    if (m_state != StdioState::Initial || !m_impl || !m_mx ||
	!m_ownerThread ||
	!m_rxThread || !m_txThread || m_rxThread == m_txThread ||
	m_ownerThread == m_rxThread || m_ownerThread == m_txThread ||
	m_ownerThread > m_mx->params().nThreads() ||
	m_rxThread > m_mx->params().nThreads() ||
	m_txThread > m_mx->params().nThreads() ||
	m_rxThread == m_mx->rxThread() || m_rxThread == m_mx->txThread() ||
	m_txThread == m_mx->rxThread() || m_txThread == m_mx->txThread() ||
	!m_mx->params().thread(m_rxThread).isolated() ||
	!m_mx->params().thread(m_txThread).isolated() ||
	m_input.handle() == m_output.handle() ||
	!m_input || !m_output || !m_limits.maxLineBytes ||
	!m_limits.maxQueue || !m_limits.maxQueueBytes ||
	!m_limits.workBatch)
      return false;
    m_state = StdioState::Open;
    m_mx->wakeFn(m_rxThread,
      ZmScheduler::WakeFn{this, [](StdioIO *io) { io->wakeRx_(); }});
    m_mx->run([this]() { rxWork_(); }, m_rxThread);
    m_mx->run([this]() { txWork_(); }, m_txThread);
    return true;
  }

  template <typename P>
  bool send_(const P &message) {
    ZiAssert(invoked_(), "Zmcp", (),
	"stdio send outside owner shard", return false);
    try {
      return send_(stdioFrame(message, m_limits.maxLineBytes));
    } catch (...) {
      fail_();
      return false;
    }
  }

  bool send_(ZmRef<ZiIOBuf> frame) {
    ZiAssert(invoked_(), "Zmcp", (),
	"stdio send outside owner shard", return false);
    if (m_state != StdioState::Open || !frame || frame->failed() || !frame->length ||
	frame->end()[-1] != '\n' ||
	frame->length - 1 > m_limits.maxLineBytes)
      return false;
    {
      ZmGuard<ZmPLock> guard(m_txLock);
      if (m_txStopping || m_txQueued >= m_limits.maxQueue ||
	  m_txQueuedBytes > m_limits.maxQueueBytes ||
	  frame->length > m_limits.maxQueueBytes - m_txQueuedBytes)
	return false;
      unsigned length = frame->length;
      try {
	m_txQueue.push(StdioRecord{ZuMv(frame)});
      } catch (...) {
	guard.unlock();
	fail_();
	return false;
      }
      m_txQueuedBytes += length;
      ++m_txQueued;
    }
    m_txSem.post();
    return true;
  }

  void stop_() {
    ZiAssert(invoked_(), "Zmcp", (),
	"stdio stop outside owner shard", return);
    if (m_state == StdioState::Closed ||
	m_state == StdioState::Closing) return;
    if (m_state != StdioState::Failed) m_state = StdioState::Closing;
    stopRx_();
  }

private:
  bool invoked_() const {
    return m_mx && m_mx->invoked(m_ownerThread);
  }

  void wakeRx_() {
    if (!m_rxStopping) return;
    m_input.close();
    ZmGuard<ZmPLock> guard(m_rxLock);
    Private::interruptStdio(m_rxNative);
  }

  void rxWork_() {
    ZiFile input{m_input};
    uintptr_t native = Private::stdioThread();
    {
      ZmGuard<ZmPLock> guard(m_rxLock);
      m_rxNative = native;
    }
    try {
      for (;;) {
	if (!native) {
	  rxOutcome_(StdioOutcome::Failed);
	  break;
	}
	ZmRef<ZiIOBuf> buf = new StdioBuf{};
	int n = input.read(buf->data(), buf->size, false);
	if (m_rxStopping) break;
	if (n <= 0) {
	  rxOutcome_(n == Zi::EndOfFile ?
	    StdioOutcome::EOF_ : StdioOutcome::Failed);
	  break;
	}
	buf->length = unsigned(n);
	if (!rxEnqueue_(ZuMv(buf))) {
	  rxOutcome_(StdioOutcome::Failed);
	  break;
	}
      }
    } catch (...) {
      rxOutcome_(StdioOutcome::Failed);
    }
    if (m_rxStopping) rxOutcome_(StdioOutcome::Stopped);
    {
      ZmGuard<ZmPLock> guard(m_rxLock);
      m_rxNative = 0;
      Private::closeStdioThread(native);
    }
    m_mx->push([this]() { rxExited_(); }, m_rxThread);
  }

  void txWork_() {
    ZiFile output{m_output};
    for (;;) {
      m_txSem.wait();
      StdioRecord record;
      {
	ZmGuard<ZmPLock> guard(m_txLock);
	if (m_txStopping) break;
	auto node = m_txQueue.shift();
	if (!node) {
	  if (m_txClosing) break;
	  continue;
	}
	record = ZuMv(node->data());
      }
      int result = output.write(record.buf->data(), record.buf->length);
      bool drained;
      {
	ZmGuard<ZmPLock> guard(m_txLock);
	--m_txQueued;
	m_txQueuedBytes -= record.buf->length;
	drained = !m_txQueued;
      }
      if (result != Zi::OK) {
	m_txStopping = 1;
	m_mx->run([this]() { txFailed_(); }, m_ownerThread);
	break;
      }
      if (m_txClosing && drained) break;
    }
    m_mx->push([this]() { txExited_(); }, m_txThread);
  }

  bool rxEnqueue_(ZmRef<ZiIOBuf> buf) {
    bool post = false;
    {
      ZmGuard<ZmPLock> guard(m_rxLock);
      if (m_rxStopping || m_rxOutcome ||
	  m_rxQueued >= m_limits.maxQueue ||
	  m_rxQueuedBytes > m_limits.maxQueueBytes ||
	  buf->length > m_limits.maxQueueBytes - m_rxQueuedBytes)
	return false;
      unsigned length = buf->length;
      m_rxQueue.push(StdioRecord{ZuMv(buf)});
      m_rxQueuedBytes += length;
      ++m_rxQueued;
      if (!m_rxDequeuing) post = m_rxDequeuing = true;
    }
    if (post) m_mx->run([this]() { drainRx_(); }, m_ownerThread);
    return true;
  }

  void rxOutcome_(int outcome) {
    bool post = false;
    {
      ZmGuard<ZmPLock> guard(m_rxLock);
      if (!m_rxOutcome) m_rxOutcome = outcome;
      if (!m_rxDequeuing) post = m_rxDequeuing = true;
    }
    if (post) m_mx->run([this]() { drainRx_(); }, m_ownerThread);
  }

  void drainRx_() {
    ZiAssert(invoked_(), "Zmcp", (),
	"stdio drain outside owner shard", return);
    if (m_state == StdioState::Failed) {
      drainFailedRx_();
      return;
    }
    unsigned visited = 0;
    while (visited < m_limits.workBatch) {
      StdioRecord record;
      {
	ZmGuard<ZmPLock> guard(m_rxLock);
	auto node = m_rxQueue.shift();
	if (!node) break;
	record = ZuMv(node->data());
	--m_rxQueued;
	m_rxQueuedBytes -= record.buf->length;
      }
      ++visited;
      bool accepted = true;
      bool framed = false;
      try {
	framed = m_framer.feed(ZuCSpan{record.buf->cspan()},
	  [this, &accepted](ZmRef<ZiIOBuf> frame) {
	    if (!accepted) return;
	    try {
	      accepted = m_impl->stdioFrame(ZuMv(frame));
	    } catch (...) {
	      accepted = false;
	    }
	  });
      } catch (...) {
	accepted = false;
      }
      if (!framed || !accepted) {
	fail_();
	return;
      }
    }

    int outcome = StdioOutcome::None;
    bool repost = false;
    {
      ZmGuard<ZmPLock> guard(m_rxLock);
      if (m_rxQueued) {
	repost = true;
      } else if (m_rxOutcome) {
	outcome = m_rxOutcome;
	m_rxDequeuing = false;
      } else {
	m_rxDequeuing = false;
      }
    }
    if (repost) {
      m_mx->run([this]() { drainRx_(); }, m_ownerThread);
      return;
    }
    if (!outcome) return;
    if (outcome == StdioOutcome::Failed ||
	(outcome == StdioOutcome::EOF_ && !m_framer.eof()))
      fail_();
    else
      rxDrained_(outcome == StdioOutcome::EOF_);
  }

  void fail_() {
    if (m_state == StdioState::Closed) return;
    m_state = StdioState::Failed;
    m_framer.close();
    stopRx_();
    drainFailedRx_();
  }

  void drainFailedRx_() {
    if (m_rxDrained || m_rxFailDraining) return;
    m_rxFailDraining = true;
    unsigned visited = 0;
    bool remaining;
    {
      ZmGuard<ZmPLock> guard(m_rxLock);
      while (visited < m_limits.workBatch) {
	auto node = m_rxQueue.shift();
	if (!node) break;
	++visited;
	--m_rxQueued;
	m_rxQueuedBytes -= node->data().buf->length;
      }
	remaining = m_rxQueued;
	if (!remaining) m_rxDequeuing = false;
    }
    m_rxFailDraining = false;
    if (remaining) {
      m_mx->run([this]() { drainFailedRx_(); }, m_ownerThread);
      return;
    }
    rxDrained_();
  }

  void stopRx_() {
    if (m_rxStopping.cmpXch(1, 0) != 0) return;
    m_mx->run([]() { }, m_rxThread);
  }

  void rxDrained_(bool flush = false) {
    if (m_rxDrained) return;
    m_rxDrained = true;
    m_framer.close();
    if (m_state == StdioState::Open) m_state = StdioState::Closing;
    stopTx_(flush);
    closed_();
  }

  void stopTx_(bool flush = false) {
    if (flush) {
      if (m_txStopping || m_txClosing.cmpXch(1, 0) != 0) return;
      m_txSem.post();
      return;
    }
    if (m_txStopping.cmpXch(1, 0) != 0) return;
    m_output.close();
    m_txSem.post();
  }

  void txFailed_() {
    ZiAssert(invoked_(), "Zmcp", (),
	"stdio failure outside owner shard", return);
    if (m_state != StdioState::Closed) m_state = StdioState::Failed;
    stopRx_();
    stopTx_();
  }

  void rxExited_() {
    m_mx->wakeFn(m_rxThread, {});
    m_mx->run([this]() {
      m_rxExited = true;
      closed_();
    }, m_ownerThread);
  }

  void txExited_() {
    m_mx->run([this]() {
      m_txExited = true;
      drainTx_();
    }, m_ownerThread);
  }

  void drainTx_() {
    unsigned visited = 0;
    bool remaining;
    {
      ZmGuard<ZmPLock> guard(m_txLock);
      while (visited < m_limits.workBatch) {
	auto node = m_txQueue.shift();
	if (!node) break;
	++visited;
	--m_txQueued;
	m_txQueuedBytes -= node->data().buf->length;
      }
      remaining = m_txQueued;
    }
    if (remaining) {
      m_mx->run([this]() { drainTx_(); }, m_ownerThread);
      return;
    }
    m_txDrained = true;
    closed_();
  }

  void closed_() {
    if (m_state == StdioState::Closed || !m_rxDrained || !m_rxExited ||
	!m_txExited || !m_txDrained)
      return;
    bool failed = m_state == StdioState::Failed;
    m_input.close();
    m_output.close();
    m_state = StdioState::Closed;
    if (failed) {
      try { m_impl->stdioFailed(); } catch (...) { }
    } else {
      try { m_impl->stdioClosed(); } catch (...) { }
    }
  }

  Impl			*m_impl;
  ZiMultiplex		*m_mx;
  Limits		m_limits;
  StdioFramer		m_framer;
  ZiFile		m_input;
  ZiFile		m_output;
  unsigned		m_ownerThread;
  unsigned		m_rxThread;
  unsigned		m_txThread;

  alignas(Zm::CacheLineSize)
  mutable ZmPLock	m_rxLock;
  StdioRxQueue		m_rxQueue;
  uintptr_t		m_rxNative = 0;
  uint64_t		m_rxQueuedBytes = 0;
  unsigned		m_rxQueued = 0;
  int			m_rxOutcome = StdioOutcome::None;
  bool			m_rxDequeuing = false;
  ZmAtomic<unsigned>	m_rxStopping = 0;

  alignas(Zm::CacheLineSize)
  mutable ZmPLock	m_txLock;
  StdioTxQueue		m_txQueue;
  ZmSemaphore		m_txSem;
  uint64_t		m_txQueuedBytes = 0;
  unsigned		m_txQueued = 0;
  ZmAtomic<unsigned>	m_txStopping = 0;
  ZmAtomic<unsigned>	m_txClosing = 0;

  int			m_state = StdioState::Initial;
  bool			m_rxDrained = false;
  bool			m_rxFailDraining = false;
  bool			m_rxExited = false;
  bool			m_txExited = false;
  bool			m_txDrained = false;
};

template <typename Impl, typename Heap = ZuVoid>
class StdioIO_ : public Heap, public StdioIO<Impl> {
  using Base = StdioIO<Impl>;

public:
  using Base::Base;
};

template <typename Impl>
ZuDerive(StdioIOHeap, (ZmHeap<"Zmcp.Stdio.IO", StdioIO_<Impl>>));

template <typename Impl>
ZuDerive(StdioIOObj, (StdioIO_<Impl, StdioIOHeap<Impl>>));

} // Zmcp

#endif /* Zmcp_HH */
