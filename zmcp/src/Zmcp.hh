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
#include <zlib/ZuDerive.hh>
#include <zlib/ZuMatcher.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuSwitch.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuUnion.hh>
#include <zlib/ZuUnroll.hh>

#include <zlib/ZmHeap.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZfJSON.hh>

#include <zlib/Zjrpc.hh>
#include <zlib/ZjrpcCompletion.hh>
#include <zlib/ZjrpcHTTP.hh>

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

using ModernVersion = ZuStringT<"2026-07-28">;
using LegacyVersion = ZuStringT<"2025-11-25">;

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

namespace Default {
  enum {
    MaxSessions = 1U << 12,
    SessionIDBytes = 16 // 128 bits from the TLS CSPRNG
  };
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

ZtEnumNS(ZmcpAPI, Era, int8_t, Unknown, Modern, Legacy);

namespace ErrorCode {
  enum {
    Internal = -32000
  };
}

struct ToolAnnotationsShape {
  bool readOnlyHint = false;
  bool destructiveHint = true;
  bool idempotentHint = false;
  bool openWorldHint = true;
};
ZfStruct(ZmcpAPI, (ToolAnnotationsShape, JSON),
  (readOnlyHint, (Mutable),		Bool),
  (destructiveHint, (Mutable),		Bool),
  (idempotentHint, (Mutable),		Bool),
  (openWorldHint, (Mutable),		Bool));

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

struct Request : public Zjrpc::Request {
  using ToolID = void;
  using OperationID = void;
  using Title = void;
  using Description = void;
  using Annotations = void;
};

struct Response : public Zjrpc::Response {
  enum { Status = 200 };
};

template <typename U> using GetToolID = typename U::ToolID;
template <typename U> using GetOperationID = typename U::OperationID;
template <typename U> using GetTitle = typename U::Title;
template <typename U> using GetDescription = typename U::Description;
template <typename U> using GetAnnotations = typename U::Annotations;

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
consteval bool validToolID()
{
  return Zjrpc::validName<ID_>();
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
  ZuAssert((Zjrpc::validName<GetOperationID<Req>>() && ...));
  ZuAssert((IsObjectStructured<typename Req::Object>{} && ...));
  ZuAssert((ResponsesValid_<Zjrpc::GetResponses<Req>>::Valid && ...));
  ZuAssert(((Req::ResponseBody == Zjrpc::BodyPolicy::Fixed ||
	Req::ResponseBody == Zjrpc::BodyPolicy::SSE) && ...));
  ZuAssert(ZuTypeUnique<ToolIDs>::N == ToolIDs::N);
};

struct PeerInfo {
  ZuCSpan name;
  ZuCSpan version;

  explicit operator bool() const { return true; }
};
ZfStruct(ZmcpAPI, (PeerInfo, JSON),
  (name, (Mutable),		String),
  (version, (Mutable),		String));

struct ServerMeta {
  PeerInfo serverInfo;

  explicit operator bool() const { return true; }
};
ZfStruct(ZmcpAPI, (ServerMeta, JSON),
  (serverInfo,
    (Mutable, JSON::ID<"io.modelcontextprotocol/serverInfo">),	UDT));

using ServerMetaOpt = ZfJSON::Union<ServerMeta>;

inline ServerMetaOpt serverMeta(int era)
{
  if (era != Era::Modern) return {};
  return ServerMeta{{"zmcp", Z_VERNAME}};
}

struct EmptyContent {
  const Zjrpc::EmptyObject *begin() const {
    static const Zjrpc::EmptyObject empty;
    return &empty;
  }
  const Zjrpc::EmptyObject *end() const { return begin(); }
  const Zjrpc::EmptyObject &operator [](unsigned) const {
    return *begin();
  }

  friend inline ZfJSON::AsArray<ZfFieldTC::UDT>
    ZfJSON_Fmt(EmptyContent *) { return {}; }
};

struct StructuredResultShape {
  int code = 0;
  Zjrpc::EmptyObject data;
};
ZfStruct(ZmcpAPI, (StructuredResultShape, JSON),
  (code, (Mutable),	Int32),
  (data, (Mutable),	UDT));

struct StructuredEmpty {
  int code = 0;
};
ZfStruct(ZmcpAPI, (StructuredEmpty, JSON),
  (code, (Mutable),	Int32));

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
  (meta, (Mutable, JSON::Opt, JSON::ID<"_meta">),	UDT),
  (resultType, (Mutable, JSON::Opt),			String),
  (content, (Mutable),					UDT),
  (structuredContent, (Mutable),			UDT),
  (isError, (Mutable),					Bool));

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
inline void saveToolError(S &s, const Zjrpc::ID &id, int code, int era)
{
  StructuredEmpty structured{code};
  Zjrpc::saveResult(s, id, CallResult{era, structured, true});
}

template <typename S, typename Res>
inline void saveToolReply(
    S &s, const Zjrpc::ID &id, const Zjrpc::Reply<Res> &reply, int era)
{
  if constexpr (!ZuIsSame<void, typename Res::Body>{}) {
    StructuredResult structured{Res::Status, reply.bodyObject()};
    Zjrpc::saveResult(s, id, CallResult{
      era, structured, Res::Status < 200 || Res::Status >= 300});
  } else {
    StructuredEmpty structured{Res::Status};
    Zjrpc::saveResult(s, id, CallResult{
      era, structured, Res::Status < 200 || Res::Status >= 300});
  }
}

template <typename Res> using ToolReplyT = Zjrpc::Reply<Res>;

template <typename Req>
struct ReplyUnion_ {
  using Replies = ZuTypeMap<ToolReplyT, Zjrpc::GetResponses<Req>>;
  using T = ZuTypeApply<ZuUnion, typename Replies::template Unshift<void>>;
};
template <typename Req> using ReplyUnion = typename ReplyUnion_<Req>::T;

struct StructuredReplyShape {
  int code = ZuCmp<int>::null();
  Zjrpc::EmptyObject data;
};
ZfStruct(ZmcpAPI, (StructuredReplyShape, JSON),
  (code, (Mutable),			Int32),
  (data, (Mutable, JSON::Opt),		UDT));

template <typename Req>
struct StructuredReply :
    public Zjrpc::StructModel<StructuredReply<Req>, StructuredReplyShape> {
  using Base = Zjrpc::StructModel<StructuredReply<Req>, StructuredReplyShape>;
  using Data = Zjrpc::ReplyData<Req>;

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
  (structuredContent, (Mutable),	UDT));

template <typename Req>
struct CallReply : public Zjrpc::StructModel<CallReply<Req>, CallReplyShape> {
  using Base = Zjrpc::StructModel<CallReply<Req>, CallReplyShape>;
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
  using Responses = Zjrpc::GetResponses<Req>;
  ReplyUnion<Req> out;
  auto reply = Zjrpc::loadObject<CallReply<Req>>(node);
  int code = reply.structuredContent().code();
  const auto *data = Zjrpc::raw(reply.structuredContent().data());
  int index = -1;
  ZuUnroll::all<Responses>([code, &index]<typename Res>() {
    if (code == Res::Status) index = ZuTypeIndex<Res, Responses>{};
  });
  if (index < 0) return out;
  ZuSwitch::dispatch<Responses::N>(index, [&out, data](auto I) {
    using Res = ZuType<I, Responses>;
    if constexpr (ZuIsSame<void, typename Res::Body>{})
      out = Zjrpc::Reply<Res>{};
    else if (data) {
	using Body = typename Res::Body;
	if constexpr (ZuIsBase<Body, ZmObject>{}) {
	  ZmRef<Body> body = new Body();
	  auto handler = ZfJSON::handler<Body>(data);
	  if (!handler.valid) return;
	  handler.load(*body);
	  out = Zjrpc::Reply<Res>{ZuMv(body)};
	} else {
	  auto handler = ZfJSON::handler<Body>(data);
	  if (!handler.valid) return;
	  out = Zjrpc::Reply<Res>{handler.ctor()};
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
  using Value = Zjrpc::ObjectValue<Object>;

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

using EmptyOpt = ZfJSON::Union<Zjrpc::EmptyObject>;
using PeerInfoOpt = ZfJSON::Union<PeerInfo>;

struct ClientMeta {
  ZuCSpan protocolVersion;
  EmptyOpt clientCapabilities;
  PeerInfoOpt clientInfo;
  Zjrpc::ID progressToken;
  ZuCSpan logLevel;

  explicit operator bool() const { return true; }
};
ZfStruct(ZmcpAPI, (ClientMeta, JSON),
  (protocolVersion,
    (Mutable, JSON::Opt,
      JSON::ID<"io.modelcontextprotocol/protocolVersion">),	String),
  (clientCapabilities,
    (Mutable, JSON::Opt,
      JSON::ID<"io.modelcontextprotocol/clientCapabilities">),	UDT),
  (clientInfo,
    (Mutable, JSON::Opt,
      JSON::ID<"io.modelcontextprotocol/clientInfo">),		UDT),
  (progressToken, (Mutable, JSON::Opt),				UDT),
  (logLevel,
    (Mutable, JSON::Opt,
      JSON::ID<"io.modelcontextprotocol/logLevel">),		String));

using ClientMetaOpt = ZfJSON::Union<ClientMeta>;

inline ClientMetaOpt clientMeta(
    int era, const Zjrpc::ID *progressToken = nullptr,
    int logLevel = LogLevel::Disabled)
{
  if (era != Era::Modern &&
      (!progressToken || progressToken->is<void>()) &&
      logLevel == LogLevel::Disabled) return {};
  ClientMeta meta;
  if (era == Era::Modern) {
    meta.protocolVersion = ModernVersion{}();
    meta.clientCapabilities = Zjrpc::EmptyObject{};
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
  (meta, (Mutable, JSON::Opt, JSON::ID<"_meta">),	UDT));

struct InitializeParams {
  ZuCSpan protocolVersion;
  Zjrpc::EmptyObject capabilities;
  PeerInfo clientInfo;
};
ZfStruct(ZmcpAPI, (InitializeParams, JSON),
  (protocolVersion, (Mutable),		String),
  (capabilities, (Mutable),		UDT),
  (clientInfo, (Mutable),		UDT));

struct SetLevelParams {
  ZuCSpan level;
};
ZfStruct(ZmcpAPI, (SetLevelParams, JSON),
  (level, (Mutable),		String));

struct CancelledParams {
  Zjrpc::ID requestID;
  ZuCSpan reason;
  ClientMetaOpt meta;
};
ZfStruct(ZmcpAPI, (CancelledParams, JSON),
  (requestID, (Mutable, JSON::ID<"requestId">),		UDT),
  (reason, (Mutable, JSON::Opt),			String),
  (meta, (Mutable, JSON::Opt, JSON::ID<"_meta">),	UDT));

struct ProgressParams {
  Zjrpc::ID token;
  double progress = 0;
  double total = ZuCmp<double>::null();
  ZuCSpan message;
};
ZfStruct(ZmcpAPI, (ProgressParams, JSON),
  (token, (Mutable, JSON::ID<"progressToken">),		UDT),
  (progress, (Mutable),					Float),
  (total, (Mutable, JSON::Opt),				Float),
  (message, (Mutable, JSON::Opt),			String));

struct LogParams {
  ZuCSpan level;
  ZuCSpan logger;
  ZuCSpan data;
};
ZfStruct(ZmcpAPI, (LogParams, JSON),
  (level, (Mutable),			String),
  (logger, (Mutable, JSON::Opt),	String),
  (data, (Mutable),			String));

struct RxLogParams {
  ZuCSpan level;
  ZuCSpan logger;
};
ZfStruct(ZmcpAPI, (RxLogParams, JSON),
  (level, (Mutable),			String),
  (logger, (Mutable, JSON::Opt),	String));

struct ToolsCallShape {
  ZuCSpan name;
  ZfJSON::Union<Zjrpc::EmptyObject> arguments;
  ClientMetaOpt meta;
};
ZfStruct(ZmcpAPI, (ToolsCallShape, JSON),
  (name, (Mutable),					String),
  (arguments, (Mutable),				UDT),
  (meta, (Mutable, JSON::Opt, JSON::ID<"_meta">),	UDT));

template <typename Reqs_>
struct ToolsCallParams :
    public Zjrpc::StructModel<ToolsCallParams<Reqs_>, ToolsCallShape> {
  using Base = Zjrpc::StructModel<ToolsCallParams<Reqs_>, ToolsCallShape>;
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
  Zjrpc::ID progressToken;
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
  Zjrpc::EmptyObject minimum;
  Zjrpc::EmptyObject maximum;
  Zjrpc::EmptyObject deflt;
  ZuCSpan mcpHeader;
};
ZfStruct(ZmcpAPI, (ScalarShape, JSON),
  (type, (Mutable),							String),
  (enum_, (Mutable, JSON::Opt, JSON::ID<"enum">),			UDT),
  (minimum, (Mutable, JSON::Opt),					UDT),
  (maximum, (Mutable, JSON::Opt),					UDT),
  (deflt, (Mutable, JSON::Opt, JSON::ID<"default">),			UDT),
  (mcpHeader, (Mutable, JSON::Opt, JSON::ID<"x-mcp-header">),		String));

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
  Zjrpc::EmptyObject items;
};
ZfStruct(ZmcpAPI, (ArrayShape, JSON),
  (type, (Mutable),		String),
  (items, (Mutable),		UDT));

template <typename Item>
struct Array : public ZuStructShim<Array<Item>, ArrayShape> {
  ZuCSpan type() const { return "array"; }
  Schema<Item> items() const { return {}; }
};

struct MapShape {
  ZuCSpan type;
  Zjrpc::EmptyObject additionalProperties;
};
ZfStruct(ZmcpAPI, (MapShape, JSON),
  (type, (Mutable),			String),
  (additionalProperties, (Mutable),	UDT));

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
    typename Types::template Unshift<Zjrpc::EmptyObject>>;

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
  Zjrpc::EmptyObject properties;
  EmptyNames required;
  bool additionalProperties = false;
};
ZfStruct(ZmcpAPI, (ObjectShape, JSON),
  (type, (Mutable),			String),
  (properties, (Mutable),		UDT),
  (required, (Mutable, JSON::Opt),	UDT),
  (additionalProperties, (Mutable),	Bool));

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
  (value, (Mutable, JSON::ID<"const">),		Int32));

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
  Zjrpc::EmptyObject code;
  Zjrpc::EmptyObject data;
};
ZfStruct(ZmcpAPI, (ResponsePropertiesShape, JSON),
  (code, (Mutable),			UDT),
  (data, (Mutable, JSON::Opt),		UDT));

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
  Zjrpc::EmptyObject properties;
  EmptyNames required;
  bool additionalProperties = false;
  ZuCSpan description;
};
ZfStruct(ZmcpAPI, (ResponseShape, JSON),
  (type, (Mutable),				String),
  (properties, (Mutable),			UDT),
  (required, (Mutable),				UDT),
  (additionalProperties, (Mutable),		Bool),
  (description, (Mutable, JSON::Opt),		String));

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
    typename Values::template Unshift<Zjrpc::EmptyObject>>;

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
  Zjrpc::EmptyObject oneOf;
};
ZfStruct(ZmcpAPI, (OutputShape, JSON),
  (oneOf, (Mutable),		UDT));

template <typename Req>
struct Output : public ZuStructShim<Output<Req>, OutputShape> {
  ResponseList_<Zjrpc::GetResponses<Req>> oneOf() const { return {}; }
};

struct OperationMeta {
  ZuCSpan operationID;
};
ZfStruct(ZmcpAPI, (OperationMeta, JSON),
  (operationID,
    (Mutable, JSON::ID<"operationId">),		String));

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
  Zjrpc::EmptyObject annotations;
  Zjrpc::EmptyObject inputSchema;
  Zjrpc::EmptyObject outputSchema;
  OperationMeta meta;
};
ZfStruct(ZmcpAPI, (ToolShape, JSON),
  (name, (Mutable),				String),
  (title, (Mutable, JSON::Opt),			String),
  (description, (Mutable, JSON::Opt),		String),
  (annotations, (Mutable, JSON::Opt),		UDT),
  (inputSchema, (Mutable),			UDT),
  (outputSchema, (Mutable),			UDT),
  (meta, (Mutable, JSON::ID<"_meta">),		UDT));

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
    typename Values::template Unshift<Zjrpc::EmptyObject>>;
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
  (tools, (Mutable),					UDT),
  (meta, (Mutable, JSON::Opt, JSON::ID<"_meta">),	UDT),
  (resultType, (Mutable, JSON::Opt),			String),
  (cacheScope, (Mutable, JSON::Opt),			String),
  (ttlMs, (Mutable, JSON::Opt),				UInt64));

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

namespace Token_ {

template <typename Token, typename Heap = ZuVoid>
class ProgressAction_ : public Heap, public ZmObject {
public:
  ProgressAction_(
      Token *token_, double value_, double total_, ZuCSpan message_) :
    m_token{token_}, m_message{message_},
    m_value{value_}, m_total{total_} { }

  Token *token() const { return m_token.ptr(); }
  double value() const { return m_value; }
  double total() const { return m_total; }
  ZuCSpan message() const { return m_message; }

private:
  ZmRef<Token>	m_token;
  Zjrpc::ErrorString	m_message;
  double	m_value;
  double	m_total;
};

template <typename Token>
ZuDerive(ProgressActionHeap,
  (ZmHeap<"Zmcp.Progress", ProgressAction_<Token>>));

template <typename Token>
ZuDerive(ProgressAction,
  (ProgressAction_<Token, ProgressActionHeap<Token>>));

template <typename Token, typename Heap = ZuVoid>
class LogAction_ : public Heap, public ZmObject {
public:
  LogAction_(
      Token *token_, ZuCSpan level_, ZuCSpan data_, ZuCSpan logger_) :
    m_token{token_}, m_level{level_}, m_data{data_}, m_logger{logger_} { }

  Token *token() const { return m_token.ptr(); }
  ZuCSpan level() const { return m_level; }
  ZuCSpan data() const { return m_data; }
  ZuCSpan logger() const { return m_logger; }

private:
  ZmRef<Token>	m_token;
  Zjrpc::ErrorString	m_level;
  Zjrpc::ErrorString	m_data;
  Zjrpc::ErrorString	m_logger;
};

template <typename Token>
ZuDerive(LogActionHeap, (ZmHeap<"Zmcp.Log", LogAction_<Token>>));

template <typename Token>
ZuDerive(LogAction, (LogAction_<Token, LogActionHeap<Token>>));

} // Token_

template <typename Req, typename Owner, typename Heap = ZuVoid>
class Completion_ : public Heap, public ZmObject,
    public Zjrpc::CompletionCore<Completion_<Req, Owner, Heap>, Req, Owner> {
  using Base = Zjrpc::CompletionCore<Completion_, Req, Owner>;
public:
  Completion_(Owner *owner, Zjrpc::ID id, uint64_t generation,
      Zjrpc::ID progressToken = {}, int logLevel = LogLevel::Disabled) :
    Base{owner, ZuMv(id), generation}, m_progressToken{ZuMv(progressToken)},
    m_logLevel{logLevel} { }

  const Zjrpc::ID &progressToken() const { return m_progressToken; }

  bool progress(double value, double total = ZuCmp<double>::null(),
      ZuCSpan message = {}) {
    if (!this->live() || m_progressToken.template is<void>()) return false;
    if (!this->route().invoked()) {
      using Action = Token_::ProgressAction<Completion_>;
      ZmRef<Action> action = new Action{this, value, total, message};
      this->route().run([action = ZuMv(action)]() mutable {
	action->token()->progress(action->value(), action->total(), action->message());
      });
      return true;
    }
    return this->owner()->template progress<Req>(this, value, total, message);
  }

  bool log(ZuCSpan level, ZuCSpan data, ZuCSpan logger = {}) {
    if (!this->live() || m_logLevel == LogLevel::Disabled ||
	logLevel(level) < m_logLevel) return false;
    if (!this->route().invoked()) {
      using Action = Token_::LogAction<Completion_>;
      ZmRef<Action> action = new Action{this, level, data, logger};
      this->route().run([action = ZuMv(action)]() mutable {
	action->token()->log(action->level(), action->data(), action->logger());
      });
      return true;
    }
    return this->owner()->template log<Req>(this, level, data, logger);
  }

private:
  Zjrpc::ID m_progressToken;
  int m_logLevel;
};

template <typename Req, typename Owner>
ZuDerive(CompletionHeap, (ZmHeap<"Zmcp.Completion", Completion_<Req, Owner>>));

template <typename Req, typename Owner>
ZuDerive(Completion, (Completion_<Req, Owner,
  CompletionHeap<Req, Owner>>));

template <typename Impl>
class CompletionSet : public Zjrpc::CompletionSet<
    CompletionSet<Impl>, Impl, Completion> {
public:
  template <typename Req>
  bool progress(
      Completion_<Req, CompletionSet, CompletionHeap<Req, CompletionSet>>
	*token,
      double progress_, double total, ZuCSpan message) {
    return this->impl()->progression(token, progress_, total, message);
  }

  template <typename Req>
  bool log(
      Completion_<Req, CompletionSet, CompletionHeap<Req, CompletionSet>>
	*token,
      ZuCSpan level, ZuCSpan data, ZuCSpan logger) {
    return this->impl()->logging(token, level, data, logger);
  }

protected:
  template <typename Req>
  bool progress_(
      Completion_<Req, CompletionSet, CompletionHeap<Req, CompletionSet>>
	*token,
      double progress_, double total, ZuCSpan message) {
    try {
      return this->impl()->progress(
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
      return this->impl()->log(
	static_cast<Req *>(nullptr), token, level, data, logger);
    } catch (...) {
      return false;
    }
  }


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
  (meta, (Mutable, JSON::Opt, JSON::ID<"_meta">),	UDT),
  (resultType, (Mutable, JSON::Opt),			String));

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
  (supportedVersions, (Mutable),	UDT));

struct ToolsCapability {
  Zjrpc::EmptyObject tools;
};
ZfStruct(ZmcpAPI, (ToolsCapability, JSON),
  (tools, (Mutable),		UDT));

struct LegacyCapabilities {
  Zjrpc::EmptyObject tools;
  Zjrpc::EmptyObject logging;
};
ZfStruct(ZmcpAPI, (LegacyCapabilities, JSON),
  (tools, (Mutable),		UDT),
  (logging, (Mutable),		UDT));

struct DiscoverResult {
  SupportedVersions supportedVersions;
  ToolsCapability capabilities;
  ServerMetaOpt meta;
  ZuCSpan resultType;
  ZuCSpan cacheScope;
  int ttlMs = CatalogTTL;
};
ZfStruct(ZmcpAPI, (DiscoverResult, JSON),
  (supportedVersions, (Mutable),		UDT),
  (capabilities, (Mutable),			UDT),
  (meta, (Mutable, JSON::ID<"_meta">),		UDT),
  (resultType, (Mutable),			String),
  (cacheScope, (Mutable),			String),
  (ttlMs, (Mutable),				Int32));

struct InitializeResult {
  ZuCSpan protocolVersion;
  LegacyCapabilities capabilities;
  PeerInfo serverInfo;
};
ZfStruct(ZmcpAPI, (InitializeResult, JSON),
  (protocolVersion, (Mutable),		String),
  (capabilities, (Mutable),		UDT),
  (serverInfo, (Mutable),		UDT));

struct EmptyResultMessage {
  Zjrpc::ID id;
  int era = Era::Modern;

  template <typename S>
  void write(S &out) const {
    Zjrpc::saveResult(out, id, emptyResult(era));
  }
};

struct DiscoverMessage {
  Zjrpc::ID id;

  template <typename S>
  void write(S &out) const {
    Zjrpc::saveResult(out, id, DiscoverResult{
      {}, {}, serverMeta(Era::Modern), "complete", "public", CatalogTTL});
  }
};

struct InitializeMessage {
  Zjrpc::ID id;

  template <typename S>
  void write(S &out) const {
    Zjrpc::saveResult(out, id, InitializeResult{
      LegacyVersion{}(), {}, {"zmcp", Z_VERNAME}});
  }
};

template <typename Reqs>
struct ToolsListMessage {
  Zjrpc::ID id;
  int era = Era::Modern;

  template <typename S>
  void write(S &out) const {
    Zjrpc::saveResult(out, id, SchemaModel_::ToolsResult<Reqs>{era});
  }
};

struct ErrorMessage {
  Zjrpc::ID id;
  ZuCSpan message;
  int code = ErrorCode::Internal;

  template <typename S>
  void write(S &out) const { Zjrpc::saveError(out, id, code, message); }
};

struct ToolErrorMessage {
  Zjrpc::ID id;
  int code = ErrorCode::Internal;
  int era = Era::Modern;

  template <typename S>
  void write(S &out) const { saveToolError(out, id, code, era); }
};

struct ProgressMessage {
  Zjrpc::ID token;
  Zjrpc::ErrorString message;
  double progress = 0;
  double total = ZuCmp<double>::null();

  template <typename S>
  void write(S &out) const {
    Zjrpc::saveNotification(out, "notifications/progress",
      ProgressParams{token, progress, total, message});
  }
};

struct LogMessage {
  Zjrpc::ErrorString level;
  Zjrpc::ErrorString data;
  Zjrpc::ErrorString logger;

  template <typename S>
  void write(S &out) const {
    Zjrpc::saveNotification(out, "notifications/message",
      LogParams{level, logger, data});
  }
};

template <typename Res>
struct ToolReplyMessage {
  Zjrpc::ID id;
  Zjrpc::Reply<Res> reply;
  int era = Era::Modern;

  template <typename S>
  void write(S &out) const { saveToolReply(out, id, reply, era); }
};

template <typename Reqs>
class Peer {
public:
  Peer() = default;
  Peer(Zjrpc::Limits limits) : m_limits{limits} { }

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
    auto parsed = Zjrpc::parse(input, m_limits.maxJSONBytes);
    if (!parsed) return false;
    return dispatch(parsed.envelope, ZuFwd<Emit>(emit), ZuFwd<Tool>(tool));
  }

  template <typename Emit, typename Tool>
  bool dispatch(const Zjrpc::Envelope &envelope, Emit &&emit, Tool &&tool) {
    if (m_legacyState == LegacyState::Closed) return false;
    auto call = [this, &emit, &tool](const Zjrpc::Envelope &envelope) {
      call_(emit, envelope, tool);
    };
    auto cancel = [](const Zjrpc::Envelope &) { };
    return route_(envelope, emit, tool, call, cancel);
  }

  template <typename Completions, typename Emit, typename Tool>
  bool dispatchAsync(
      ZuSpan<char> input, Completions &completions,
      Emit &&emit, Tool &&tool) {
    if (m_legacyState == LegacyState::Closed) return false;
    auto parsed = Zjrpc::parse(input, m_limits.maxJSONBytes);
    if (!parsed) return false;
    return dispatchAsync(
      parsed.envelope, completions, ZuFwd<Emit>(emit), ZuFwd<Tool>(tool));
  }

  template <typename Completions, typename Emit, typename Tool>
  bool dispatchAsync(
      const Zjrpc::Envelope &envelope, Completions &completions,
      Emit &&emit, Tool &&tool) {
    if (m_legacyState == LegacyState::Closed) return false;
    auto call = [this, &completions, &emit, &tool](
	const Zjrpc::Envelope &envelope) {
      callAsync_(completions, emit, envelope, tool);
    };
    auto cancel = [&completions](const Zjrpc::Envelope &envelope) {
      auto params = Zjrpc::loadObject<CancelledParams>(Zjrpc::raw(envelope.params()));
      if (!params.requestID.template is<void>())
	(void)completions.cancel(params.requestID, params.reason);
    };
    return route_(envelope, emit, tool, call, cancel);
  }

private:
  template <typename Emit, typename Tool, typename Call, typename Cancel>
  bool route_(
      const Zjrpc::Envelope &envelope, Emit &emit, Tool &,
      Call &call, Cancel &cancel) {
    if (envelope.kind == Zjrpc::MessageKind::Request &&
	envelope.id().template is<Zjrpc::Null>()) return false;
    if (envelope.kind != Zjrpc::MessageKind::Request &&
	envelope.kind != Zjrpc::MessageKind::Notification)
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
	if (envelope.kind == Zjrpc::MessageKind::Request)
	  emptyResult_(emit, envelope.id());
	break;
      case 4:
	if (usable_() && envelope.kind == Zjrpc::MessageKind::Request)
	  tools_(emit, envelope.id());
	else
	  unsupported_(emit, envelope);
	break;
      case 5:
	if (usable_() && envelope.kind == Zjrpc::MessageKind::Request)
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
	if (m_era == Era::Legacy && envelope.kind == Zjrpc::MessageKind::Request) {
	  auto params = Zjrpc::loadObject<SetLevelParams>(Zjrpc::raw(envelope.params()));
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
  void discover_(Emit &emit, const Zjrpc::ID &id) {
    m_era = Era::Modern;
    emit(DiscoverMessage{id});
  }

  template <typename Emit>
  void initialize_(Emit &emit, const Zjrpc::ID &id) {
    m_era = Era::Legacy;
    m_legacyState = LegacyState::Initializing;
    emit(InitializeMessage{id});
  }

  template <typename Emit>
  void emptyResult_(Emit &emit, const Zjrpc::ID &id) const {
    emit(EmptyResultMessage{id, responseEra_()});
  }

  template <typename Emit>
  void tools_(Emit &emit, const Zjrpc::ID &id) const {
    emit(ToolsListMessage<Reqs>{id,
      m_era == Era::Legacy ? Era::Legacy : Era::Modern});
  }

  template <typename Emit>
  static void unsupported_(Emit &emit, const Zjrpc::Envelope &envelope) {
    if (envelope.kind == Zjrpc::MessageKind::Request)
      emit(ErrorMessage{
	envelope.id(), "Method not found", Zjrpc::ErrorCode::MethodNotFound});
  }

  template <typename Emit, typename Tool>
  void call_(Emit &emit, const Zjrpc::Envelope &envelope, Tool &tool) const {
    if constexpr (!Reqs::N) {
      emit(ToolErrorMessage{envelope.id(), 404, responseEra_()});
      return;
    } else {
      using Params = ToolsCallParams<Reqs>;
      auto handler = ZfJSON::handler<Params>(Zjrpc::raw(envelope.params()));
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
	    ZuAssert((ZuTypeIn<Res, Zjrpc::GetResponses<Req>>{}));
	    if (ZuUnlikely(completed)) return;
	    completed = true;
	    emit(ToolReplyMessage<Res>{
	      envelope.id(), ZuFwd<decltype(reply)>(reply), era});
	  };
	  tool(static_cast<Req *>(nullptr), request, complete);
	});
      } catch (const Zjrpc::Error &error) {
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
      const Zjrpc::Envelope &envelope, Tool &tool) {
    if constexpr (!Reqs::N) {
      emit(ToolErrorMessage{envelope.id(), 404, responseEra_()});
      return;
    } else {
      using Params = ToolsCallParams<Reqs>;
      auto handler = ZfJSON::handler<Params>(Zjrpc::raw(envelope.params()));
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
      } catch (const Zjrpc::Error &error) {
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

  Zjrpc::Limits	m_limits;
  int		m_era = Era::Unknown;
  int		m_legacyState = LegacyState::Fresh;
  int		m_logLevel = LogLevel::Disabled;
};

struct HTTPMeta {
  Zjrpc::HTTPValue origin;
  Zjrpc::HTTPValue sessionID;
  Zjrpc::HTTPValue version;
  Zjrpc::HTTPValue method;
  Zjrpc::HTTPValue name;
};

namespace ParserState {
  enum { Empty, Receiving, OriginRejected, Accepted, OverLimit };
}

template <typename Impl>
class HTTPParser : public Zhttp::Parser {
public:
  using Headers = ZuTypeConcat<ZhttpHeaders("origin"), RoutingHeaders>;
  using HdrCatalog = Zjrpc::HTTPHdrCatalog<Headers>;
  using Origin = ZuStringT<"origin">;
  using Session = SessionID;
  using Version = ProtocolVersion;
  using Method = MethodHeader;
  using Name = NameHeader;

  auto impl() { return static_cast<Impl *>(this); }
  auto impl() const { return static_cast<const Impl *>(this); }

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
    if (!m_input.begin(length, impl()->limits().maxJSONBytes)) {
      m_state = ParserState::OverLimit;
      return false;
    }
    return true;
  }

  template <typename Rx>
  bool body(Rx &rx) {
    if (m_state != ParserState::Receiving) return false;
    if (!m_input.body(rx)) m_state = ParserState::OverLimit;
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
    impl()->receiveHTTP(link, m_input.buffer(), ZuMv(m_meta));
  }

  void reset() {
    m_input.reset();
    m_meta = {};
    m_method = {};
    m_state = ParserState::Empty;
  }

private:
  Zjrpc::Input	m_input;
  HTTPMeta	m_meta;
  Zhttp::Method::T m_method{};
  int		m_state = ParserState::Empty;

};

struct HTTPResponseMeta {
  Zjrpc::HTTPValue sessionID;
  Zjrpc::HTTPValue version;
};

template <typename Impl>
class HTTPResponseParser : public Zjrpc::HTTPResponseParser<
    Impl, HTTPResponseMeta, SessionHeaders> {
  using Base = Zjrpc::HTTPResponseParser<Impl, HTTPResponseMeta, SessionHeaders>;
public:
  using Base::header;
  bool acceptedStatus(unsigned status) const { return status == 202; }

  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (ZuIsSame<Key, SessionID>{}) this->meta().sessionID = value;
    else if constexpr (ZuIsSame<Key, ProtocolVersion>{}) this->meta().version = value;
  }
};

} // Zmcp

#endif /* Zmcp_HH */
