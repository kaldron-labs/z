//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z MCP server library

#ifndef ZmcpServer_HH
#define ZmcpServer_HH

#ifndef ZmcpLib_HH
#include <zlib/ZmcpLib.hh>
#endif

#include <limits.h>

#include <zlib/Zmcp.hh>
#include <zlib/ZjrpcInbound.hh>
#include <zlib/ZjrpcDispatch.hh>
#include <zlib/ZjrpcWS.hh>
#include <zlib/ZjrpcStdio.hh>

#include <zlib/ZuDerive.hh>
#include <zlib/ZuObject.hh>
#include <zlib/ZuPtr.hh>
#include <zlib/ZuRef.hh>
#include <zlib/ZuUnion.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmPolymorph.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmScheduler.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtQuote.hh>
#include <zlib/ZtScratch.hh>

#include <zlib/ZhttpServer.hh>

#include <zlib/ZtlsRandom.hh>

namespace Zmcp {

class ServerConfig : public Zhttp::ServerConfig {
public:
  const Zjrpc::Limits &limits() const { return m_limits; }
  ZuCSpan endpoint() const { return m_endpoint; }
  unsigned maxSessions() const { return m_maxSessions; }
  unsigned histSize() const { return m_histSize; }
  bool legacySessions() const { return m_legacySessions; }
  unsigned legacyLifetime() const { return m_legacyLifetime; }
  bool absentOrigin() const { return m_absentOrigin; }

  ServerConfig &limits(Zjrpc::Limits v) { m_limits = ZuMv(v); return *this; }
  ServerConfig &endpoint(ZuCSpan v) { m_endpoint = v; return *this; }
  ServerConfig &histSize(unsigned v) { m_histSize = v; return *this; }
  ServerConfig &maxSessions(unsigned v) { m_maxSessions = v; return *this; }
  ServerConfig &legacySessions(bool v) {
    m_legacySessions = v;
    return *this;
  }
  ServerConfig &legacyLifetime(unsigned seconds) {
    m_legacyLifetime = seconds;
    return *this;
  }
  ServerConfig &absentOrigin(bool v) {
    m_absentOrigin = v;
    return *this;
  }

private:
  Zjrpc::HTTPValue	m_endpoint = "/mcp";
  Zjrpc::Limits	m_limits;
  unsigned	m_maxSessions = Default::MaxSessions;
  unsigned	m_histSize = Zjrpc::Default::HistSize;
  unsigned	m_legacyLifetime = 0;
  bool		m_legacySessions = true;
  bool		m_absentOrigin = false;
};

template <unsigned Status>
struct HTTPEmptyBuilder : public Zhttp::ResBuilder {
  using Headers = ZhttpHeaders(("content-length", "0"));

  Zhttp::BodyPolicy::T bodyPolicy() const {
    return Zhttp::BodyPolicy::None;
  }
  Zhttp::Method::T method() const { return Zhttp::Method::POST; }
  unsigned status() const { return Status; }
};

using HTTPAcceptedBuilder = HTTPEmptyBuilder<202>;
using HTTPForbiddenBuilder = HTTPEmptyBuilder<403>;

namespace Server_ {

class ContextRef {
public:
  ContextRef() = default;
  ContextRef(const ContextRef &) = delete;
  ContextRef &operator =(const ContextRef &) = delete;
  ContextRef(ContextRef &&ref) :
    m_impl{ref.m_impl}, m_object{ref.m_object},
    m_closeFn{ref.m_closeFn}, m_releaseFn{ref.m_releaseFn} {
    ref.m_object = nullptr;
  }
  ContextRef &operator =(ContextRef &&ref) {
    if (this == &ref) return *this;
    close();
    m_impl = ref.m_impl;
    m_object = ref.m_object;
    m_closeFn = ref.m_closeFn;
    m_releaseFn = ref.m_releaseFn;
    ref.m_object = nullptr;
    return *this;
  }
  ~ContextRef() { close(); }

  template <typename Impl, typename Tag, typename Object>
  ContextRef(Impl *impl, Tag, ZuRef<Object> object) :
    m_impl{impl}, m_object{ZuMv(object).release()},
    m_closeFn{[](void *impl_, void *object_) {
      close_(static_cast<Impl *>(impl_), Tag{},
	static_cast<Object *>(object_), 0);
    }},
    m_releaseFn{[](void *object_) {
      auto object = static_cast<Object *>(object_);
      if (object->deref()) delete object;
    }}
  {
    ZuAssert(ZuIsObject<Object>{},
      "Zmcp application context must derive from ZuObject");
  }

  void *ptr() const { return m_object; }
  explicit operator bool() const { return m_object; }

  void close() {
    auto object = m_object;
    if (!object) return;
    m_object = nullptr;
    try {
      m_closeFn(m_impl, object);
    } catch (...) {
    }
    m_releaseFn(object);
  }

private:
  template <typename Impl, typename Tag, typename Object,
    typename = decltype(ZuDeclVal<Impl * &>()->close(ZuDeclVal<Tag &>(),
      ZuDeclVal<Object * &>()), void())>
  static void close_(Impl *impl, Tag tag, Object *object, int) {
    impl->close(tag, object);
  }
  template <typename Impl, typename Tag, typename Object>
  static void close_(Impl *, Tag, Object *, ...) { }

  void	*m_impl = nullptr;
  void	*m_object = nullptr;
  void	(*m_closeFn)(void *, void *) = nullptr;
  void	(*m_releaseFn)(void *) = nullptr;
};

template <typename Impl, typename Tag, typename ...Args,
  typename = decltype(ContextRef{
      ZuDeclVal<Impl * &>(), ZuDeclVal<Tag &>(),
	ZuDeclVal<Impl * &>()->open(ZuDeclVal<Tag &>(),
	ZuFwd<Args>(ZuDeclVal<Args &>())...)})>
ContextRef openContext(Impl *impl, Tag tag, int, Args &&...args)
{
  return ContextRef{
    impl, tag, impl->open(tag, ZuFwd<Args>(args)...)};
}
template <typename Impl, typename Tag, typename ...Args>
ContextRef openContext(Impl *, Tag, long, Args &&...) { return {}; }

struct TransportContextEntry {
  uintptr_t	id = 0;
  ContextRef	context;

  TransportContextEntry() = default;
  TransportContextEntry(uintptr_t id_, ContextRef context_) :
    id{id_}, context{ZuMv(context_)} { }
  TransportContextEntry(const TransportContextEntry &) = delete;
  TransportContextEntry &operator =(const TransportContextEntry &) = delete;
  TransportContextEntry(TransportContextEntry &&) = default;
  TransportContextEntry &operator =(TransportContextEntry &&) = default;
};

inline uintptr_t TransportContext_KeyAxor(
    const TransportContextEntry &entry) {
  return entry.id;
}

ZmHashDerive(TransportContextHash, TransportContextEntry,
  (ZmHashNode<TransportContextEntry,
    ZmHashKey<TransportContext_KeyAxor,
      ZmHashLock<ZmNoLock,
	ZmHashHeapID<"Zmcp.HTTP.Context">>>>));

template <typename Heap = ZuVoid>
struct ContextTable_ : public Heap, public TransportContextHash {
  ZuDerive_(ContextTable_, TransportContextHash)
};
ZuDerive(ContextTableHeap, (ZmHeap<"Zmcp.HTTP.ContextTable", ContextTable_<>>));
ZuDerive(ContextTable, (ContextTable_<ContextTableHeap>));

template <typename Res>
using ReplyMessage = ToolReplyMessage<Res>;

template <typename Reqs>
struct ResponseMessages {
  using Responses = ZuTypeUnique<ZuTypeApply<
    ZuTypeConcat, ZuTypeMap<Zjrpc::GetResponses, Reqs>>>;
  using Replies = ZuTypeMap<ReplyMessage, Responses>;
  using Builtins = ZuTypeList<
    EmptyResultMessage, DiscoverMessage, InitializeMessage,
    ToolsListMessage<Reqs>, ErrorMessage, ToolErrorMessage,
    ProgressMessage, LogMessage>;
  using T = typename Builtins::template Push<Replies>;
};

// The actual route carries its teardown-list node and concrete close callback.
// ZmList supplies the node destructor; no separate route entry is allocated.
struct RouteBase : public ZmObject {
  using CloseFn = void (*)(RouteBase *);

  CloseFn closeFn;
  bool linked = false;

  explicit RouteBase(CloseFn fn) : closeFn{fn} { }
  void close() { closeFn(this); }
};
using RouteQ = ZmList<RouteBase,
  ZmListNode<RouteBase, ZmListHeapID<"">>>;

class HTTPStreamRef {
public:
  using CloseFn = void (*)(void *);
  using ReleaseFn = void (*)(void *);

  HTTPStreamRef() = default;
  HTTPStreamRef(const HTTPStreamRef &) = delete;
  HTTPStreamRef &operator =(const HTTPStreamRef &) = delete;
  HTTPStreamRef(HTTPStreamRef &&ref) :
    m_object{ref.m_object}, m_closeFn{ref.m_closeFn},
    m_releaseFn{ref.m_releaseFn} {
    ref.m_object = nullptr;
  }
  HTTPStreamRef &operator =(HTTPStreamRef &&ref) {
    if (this == &ref) return *this;
    clear();
    m_object = ref.m_object;
    m_closeFn = ref.m_closeFn;
    m_releaseFn = ref.m_releaseFn;
    ref.m_object = nullptr;
    return *this;
  }
  ~HTTPStreamRef() { clear(); }

  template <typename Work>
  void replace(ZmRef<Work> work) {
    HTTPStreamRef old{ZuMv(*this)};
    m_object = ZuMv(work).release();
    m_closeFn = [](void *object) {
      static_cast<Work *>(object)->streamClosed_();
    };
    m_releaseFn = [](void *object) {
      auto work_ = static_cast<Work *>(object);
      if (work_->deref()) delete work_;
    };
    old.close();
  }

  bool is(const void *object) const { return m_object == object; }

  void clear(const void *object) {
    if (m_object == object) clear();
  }

  void close() {
    auto object = m_object;
    auto closeFn = m_closeFn;
    auto releaseFn = m_releaseFn;
    m_object = nullptr;
    if (!object) return;
    closeFn(object);
    releaseFn(object);
  }

  void clear() {
    auto object = m_object;
    auto releaseFn = m_releaseFn;
    m_object = nullptr;
    if (object) releaseFn(object);
  }

private:
  void		*m_object = nullptr;
  CloseFn	m_closeFn = nullptr;
  ReleaseFn	m_releaseFn = nullptr;
};

template <typename Catalog>
struct HTTPSessionData : public ZuObject {
  Zjrpc::HTTPValue		id;
  Peer<Catalog>		peer;
  HTTPStreamRef		stream;
  Zjrpc::InboundCalls	pending;
  ContextRef		context;
  ZmScheduler::Timer	timer;
  bool			closing = false;

  HTTPSessionData(Zjrpc::HTTPValue id_, Zjrpc::Limits limits,
      ContextRef context_, unsigned histSize) :
    id{ZuMv(id_)}, peer{limits}, pending{histSize},
    context{ZuMv(context_)} { }

  bool beginClose() {
    if (closing) return false;
    closing = true;
    peer.close();
    stream.close();
    return true;
  }

  bool close(unsigned limit) {
    beginClose();
    return pending.close(limit);
  }
};

template <typename Catalog>
inline const Zjrpc::HTTPValue &HTTPSession_KeyAxor(
    const HTTPSessionData<Catalog> &session) {
  return session.id;
}

template <typename Catalog>
using HTTPSessionHash_ = ZmHash<HTTPSessionData<Catalog>,
  ZmHashNode<HTTPSessionData<Catalog>,
    ZmHashKey<HTTPSession_KeyAxor<Catalog>,
      ZmHashLock<ZmNoLock,
	ZmHashHeapID<"Zmcp.HTTP.Session">>>>>;

template <typename Catalog>
struct HTTPSessionHash : public HTTPSessionHash_<Catalog> { };

template <typename Catalog, typename Heap = ZuVoid>
struct SessionTable_ : public Heap, public HTTPSessionHash<Catalog> { };
template <typename Catalog>
ZuDerive(SessionTableHeap, (ZmHeap<"Zmcp.HTTP.SessionTable", SessionTable_<Catalog>>));
template <typename Catalog>
ZuDerive(SessionTable, (SessionTable_<Catalog, SessionTableHeap<Catalog>>));
template <typename Catalog>
using HTTPSession = typename HTTPSessionHash<Catalog>::Node;

using SessionEntropy =
  ZtArray<uint8_t, ZtArrayHeapID<"Zmcp.HTTP.SessionID">>;

template <typename Work>
class DispatchWork : public CompletionSet<Work> {
  using Base = CompletionSet<Work>;
public:
  void begin(const Context &context, int era) {
    m_context = context;
    m_era = era == Era::Legacy ? Era::Legacy : Era::Modern;
  }
  void end() { }
  int era() const { return m_era; }
  const Context &appContext() const { return m_context; }
  bool cancel(const Zjrpc::ID &id, ZuCSpan reason = {}) {
    return static_cast<Work *>(this)->cancelPeer(id, reason);
  }

  template <typename Token>
  bool progression(Token *token, double value, double total, ZuCSpan message) {
    auto work = static_cast<Work *>(this);
    ZiAssert(work->owner()->invoked(), "Zmcp", (),
      "progress outside owner shard", return false);
    return work->active() && Base::progress_(token, value, total, message);
  }
  template <typename Token>
  bool logging(Token *token, ZuCSpan level, ZuCSpan data, ZuCSpan logger) {
    auto work = static_cast<Work *>(this);
    ZiAssert(work->owner()->invoked(), "Zmcp", (),
      "logging outside owner shard", return false);
    return work->active() && Base::log_(token, level, data, logger);
  }
  template <typename Req, typename Token>
  bool progress(Req *, Token *token, double value, double total, ZuCSpan message) {
    return static_cast<Work *>(this)->publish(
      ProgressMessage{token->progressToken(), message, value, total}, false);
  }
  template <typename Req, typename Token>
  bool log(Req *, Token *, ZuCSpan level, ZuCSpan data, ZuCSpan logger) {
    return static_cast<Work *>(this)->publish(LogMessage{level, data, logger}, false);
  }
private:
  Context m_context;
  int m_era = Era::Modern;
};

// Channels own one stream context per work; HTTP routes own theirs.
template <typename Work>
class ChannelDispatchWork : public DispatchWork<Work> {
  using Base = DispatchWork<Work>;
public:
  template <typename Impl>
  void begin(const Context &context, Impl *impl, int era) {
    m_stream = openContext(impl, StreamTag{}, 0, context);
    Base::begin(Context{context.transport(), context.session(), m_stream.ptr()}, era);
  }
  void end() { m_stream.close(); }
private:
  ContextRef m_stream;
};

template <typename Catalog>
struct DispatchPolicy : public Zjrpc::DispatchPolicy<Catalog> {
  using Message = Zjrpc::ServerMessage<Catalog, ToolReplyMessage,
    typename ResponseMessages<Catalog>::Builtins>;
  template <typename Work> using Completions = ChannelDispatchWork<Work>;
  template <typename Work> static void begin(Work &work) {
    work.begin(*work.context(), work.owner()->impl(), work.owner()->peer().era());
  }
  template <typename Work> static void end(Work &work) { work.end(); }
  // MCP channel/session teardown invalidates tokens without notifying cancellation.
  template <typename Work> static bool cancelOnClose(const Work &) { return false; }
  static bool valid(const Zjrpc::Envelope &envelope) {
    return envelope.kind != Zjrpc::MessageKind::Unusable &&
      !(envelope.kind == Zjrpc::MessageKind::Request &&
	envelope.id().template is<Zjrpc::Null>());
  }
  template <typename Emit>
  static bool invalid(Emit &emit, const Zjrpc::Parsed &) {
    return emit(Message{}, Zjrpc::RoutePolicy::Abort);
  }
  template <typename Dispatch, typename Work, typename Emit, typename Tool>
  static bool dispatch(Dispatch &dispatcher, const Zjrpc::Envelope &envelope,
      Work &work, Emit &emit, Tool &tool) {
    return dispatcher.owner()->peer().dispatchAsync(envelope, work, emit, tool);
  }
  template <typename Work, typename Req, typename Object, typename Token>
  static void invoke(Work &work, Req *req, const Object &object, Token token) {
    typename Work::OwnerType::Headers headers;
    work.owner()->impl()->tool(req, object, headers, work.appContext(), ZuMv(token));
  }
  template <typename Work, typename Res>
  static void complete(Work &work, const Zjrpc::ID &id, Zjrpc::Reply<Res> reply) {
    work.publish(ToolReplyMessage<Res>{id, ZuMv(reply), work.era()});
  }
};

template <typename Headers, typename Catalog>
struct HTTPDispatchContext {
  Context context;
  const Headers *headers = nullptr;
  Peer<Catalog> *peer = nullptr;
  void *route = nullptr;
  void (*prepare)(void *, int) = nullptr;
  bool (*attached)(void *) = nullptr;
};

template <typename Work>
class HTTPDispatchWork : public DispatchWork<Work> {
  using Base = DispatchWork<Work>;
public:
  template <typename Req, typename Token>
  bool progress(Req *req, Token *token, double value, double total, ZuCSpan message) {
    if constexpr (Req::ResponseBody == Zjrpc::BodyPolicy::SSE)
      return attached() && Base::progress(req, token, value, total, message);
    else return false;
  }
  template <typename Req, typename Token>
  bool log(Req *req, Token *token, ZuCSpan level, ZuCSpan data, ZuCSpan logger) {
    if constexpr (Req::ResponseBody == Zjrpc::BodyPolicy::SSE)
      return attached() && Base::log(req, token, level, data, logger);
    else return false;
  }
private:
  bool attached() const {
    const auto &http = *static_cast<const Work *>(this)->context();
    return http.attached(http.route);
  }
};

template <typename Catalog>
struct HTTPDispatchPolicy : public DispatchPolicy<Catalog> {
  template <typename Work> using Completions = HTTPDispatchWork<Work>;
  template <typename Work> static bool cancelOnClose(const Work &work) {
    return !work.inbound(); // Sessionless HTTP route loss cancels its request.
  }
  template <typename Work> static bool deliverable(Work &work) {
    const auto &http = *work.context();
    return http.attached(http.route);
  }
  template <typename Work> static void begin(Work &work) {
    const auto &http = *work.context();
    work.begin(http.context, http.peer->era());
  }
  template <typename Work, typename Req, typename Token>
  static void made(Work &work, Req *, Token *) {
    const auto &http = *work.context();
    http.prepare(http.route, Req::ResponseBody);
  }
  template <typename Dispatch, typename Work, typename Emit, typename Tool>
  static bool dispatch(Dispatch &, const Zjrpc::Envelope &envelope,
      Work &work, Emit &emit, Tool &tool) {
    return work.context()->peer->dispatchAsync(envelope, work, emit, tool);
  }
  template <typename Work, typename Req, typename Object, typename Token>
  static void invoke(Work &work, Req *req, const Object &object, Token token) {
    work.owner()->impl()->tool(
      req, object, *work.context()->headers, work.appContext(), ZuMv(token));
  }
};

} // Server_

template <typename Catalog>
class HTTPResponseBuilder : public Zhttp::ResBuilder {
public:
  using Headers = ZuTypeConcat<
    ZhttpHeaders("content-type", "content-length"), SessionHeader>;
  using ContentType = ZuStringT<"content-type">;
  using ContentLength = ZuStringT<"content-length">;
  using Session = SessionID;
  using Message = typename Server_::DispatchPolicy<Catalog>::Message;
  using Zhttp::ResBuilder::header;

  template <typename M>
  void init(M message_) {
    m_message = Message{ZuMv(message_)};
    m_status = 200;
  }

  void accepted() {
    m_message = Message{};
    m_status = 202;
  }
  void forbidden() {
    m_message = Message{};
    m_status = 403;
  }
  void missing() {
    m_message = Message{};
    m_status = 404;
  }

  ZuCSpan sessionID() const { return m_sessionID; }
  void sessionID(ZuCSpan id) { m_sessionID = id; }
  void maxBodyBytes(uint64_t bytes) { m_maxBodyBytes = bytes; }

  Zhttp::BodyPolicy::T bodyPolicy() const {
    return !m_message.empty() ?
      Zhttp::BodyPolicy::Fixed : Zhttp::BodyPolicy::None;
  }
  Zhttp::Method::T method() const { return Zhttp::Method::POST; }
  unsigned status() const { return m_status; }

  template <typename Key, typename L>
  void header(L &&l) const {
    if constexpr (ZuIsSame<Key, ContentType>{}) {
      if (!m_message.empty()) l("application/json");
    } else if constexpr (ZuIsSame<Key, ContentLength>{}) {
      l(!m_message.empty() ?
	Zhttp::contentLengthPad() : ZuCSpan{"0"});
    } else if constexpr (ZuIsSame<Key, Session>{}) {
      if (m_sessionID) l(m_sessionID);
    }
  }
  template <typename L> void header(L &&) const { }

  template <typename Emit>
  void body(Emit &&emit) const {
    if (m_message.empty()) return;
    emit([this](auto &s) {
      return Zjrpc::writeHTTP(s, m_maxBodyBytes, m_bodyLength, [this](auto &out) {
	m_message.write(out);
      });
    });
  }

  template <typename L>
  void bodyHdrs(L &&l) const {
    if (m_message.empty()) return;
    Zhttp::contentLengthSet(l, m_bodyLength);
  }

  void close() { m_message = Message{}; }

private:
  Message	m_message;
  Zjrpc::HTTPValue	m_sessionID;
  mutable uint64_t m_bodyLength = 0;
  uint64_t	m_maxBodyBytes = Zjrpc::Default::MaxJSONBytes;
  unsigned	m_status = 202;
};

template <typename Catalog>
class HTTPSSEBuilder : public Zjrpc::HTTPSSEBuilder {
  using Base = Zjrpc::HTTPSSEBuilder;
public:
  using Headers = ZuTypeConcat<typename Base::Headers, SessionHeader>;
  using Session = SessionID;
  using Messages = ZuTypeConcat<typename Server_::ResponseMessages<Catalog>::T,
    ZuTypeList<Zjrpc::ErrorMessage, Zjrpc::BatchMessage>>;
  using Base::Base;
  using Base::header;

  ZuCSpan sessionID() const { return m_sessionID; }
  void sessionID(ZuCSpan id) { m_sessionID = id; }

  template <typename Key, typename L>
  void header(L &&l) const {
    if constexpr (ZuIsSame<Key, Session>{}) {
      if (m_sessionID) l(m_sessionID);
    } else Base::template header<Key>(ZuFwd<L>(l));
  }

  template <typename M>
  bool emit(M message) {
    ZuAssert((ZuTypeIn<M, Messages>{}));
    enum { Terminal =
      !ZuIsSame<M, ProgressMessage>{} && !ZuIsSame<M, LogMessage>{} };
    return Base::emit(ZuMv(message), Terminal);
  }

private:
  Zjrpc::HTTPValue m_sessionID;
};

template <typename Catalog>
class HTTPBuilder : public ZmObject, public Zhttp::ResBuilder {
public:
  using Headers = ZuTypeConcat<ZhttpHeaders(
    "content-type", "content-length", "cache-control"), SessionHeader>;
  using HdrCatalog = Zjrpc::HTTPHdrCatalog<Headers>;
  using Fixed = HTTPResponseBuilder<Catalog>;
  using Stream = HTTPSSEBuilder<Catalog>;
  using Builder = ZuUnion<void, Fixed, Stream>;
  using Zhttp::ResBuilder::header;

  ~HTTPBuilder() { close(); }

  template <typename Owner>
  void owner(Owner *owner) {
    m_owner = owner;
    m_closedFn = [](void *owner_) {
      static_cast<Owner *>(owner_)->responseClosed_();
    };
  }

  void disown() {
    m_owner = nullptr;
    m_closedFn = nullptr;
  }

  template <typename M>
  Fixed *fixed(M message, uint64_t maxBodyBytes = Zjrpc::Default::MaxJSONBytes) {
    auto fixed_ = new (m_builder.template new_<Fixed, true>()) Fixed{};
    fixed_->init(ZuMv(message));
    fixed_->maxBodyBytes(maxBodyBytes);
    return fixed_;
  }

  Fixed *accepted() {
    auto fixed_ = new (m_builder.template new_<Fixed, true>()) Fixed{};
    fixed_->accepted();
    return fixed_;
  }

  Fixed *forbidden() {
    auto fixed_ = new (m_builder.template new_<Fixed, true>()) Fixed{};
    fixed_->forbidden();
    return fixed_;
  }

  Fixed *missing() {
    auto fixed_ = new (m_builder.template new_<Fixed, true>()) Fixed{};
    fixed_->missing();
    return fixed_;
  }

  Stream *stream(Zjrpc::Limits limits = {}) {
    return new (m_builder.template new_<Stream, true>()) Stream{limits};
  }

  Fixed *fixedPtr() { return m_builder.template ptr<Fixed>(); }
  const Fixed *fixedPtr() const {
    return m_builder.template is<Fixed>() ?
      ZuAddr(m_builder.template p<Fixed>()) : nullptr;
  }
  Stream *streamPtr() { return m_builder.template ptr<Stream>(); }
  const Stream *streamPtr() const {
    return m_builder.template is<Stream>() ?
      ZuAddr(m_builder.template p<Stream>()) : nullptr;
  }

  Zhttp::BodyPolicy::T bodyPolicy() const {
    return m_builder.cdispatch([](auto, const auto &builder) {
      return builder.bodyPolicy();
    });
  }
  Zhttp::Method::T method() const {
    return m_builder.cdispatch([](auto, const auto &builder) {
      return builder.method();
    });
  }
  unsigned status() const {
    if (!m_builder.type()) return 500;
    return m_builder.cdispatch([](auto, const auto &builder) {
      return builder.status();
    });
  }
  bool disconnect() const {
    if (!m_builder.type()) return true;
    return m_builder.cdispatch([](auto, const auto &builder) {
      return builder.disconnect();
    });
  }

  template <typename Key, typename L>
  void header(L &&l) const {
    m_builder.cdispatch([&l](auto, const auto &builder) {
      builder.template header<Key>(ZuFwd<L>(l));
    });
  }
  template <typename L>
  void header(L &&l) const {
    m_builder.cdispatch([&l](auto, const auto &builder) {
      builder.header(ZuFwd<L>(l));
    });
  }

  template <typename Emit>
  void body(Emit &&emit) {
    using Result = decltype(
      ZuDeclVal<Emit &>()(Zjrpc::WriteProbe{}));
    if constexpr (ZuIsSame<Result, bool>{}) {
      if (auto stream_ = streamPtr())
	stream_->body(ZuFwd<Emit>(emit));
    } else {
      if (auto fixed_ = fixedPtr())
	fixed_->body(ZuFwd<Emit>(emit));
    }
  }

  template <typename L>
  void bodyHdrs(L &&l) const {
    if (auto fixed_ = fixedPtr()) fixed_->bodyHdrs(ZuFwd<L>(l));
  }

  void close() {
    if (m_builder.type()) {
      m_builder.dispatch([](auto, auto &builder) { builder.close(); });
      m_builder.null();
    }
    auto owner = m_owner;
    auto closedFn = m_closedFn;
    disown();
    if (owner) closedFn(owner);
  }

private:
  using ClosedFn = void (*)(void *);

  Builder	m_builder;
  void		*m_owner = nullptr;
  ClosedFn	m_closedFn = nullptr;
};

template <typename Catalog>
using HTTPBuilderQ = ZmList<HTTPBuilder<Catalog>,
  ZmListNode<HTTPBuilder<Catalog>,
    ZmListHeapID<"Zmcp.HTTP.Builder">>>;

template <typename Catalog, typename Impl>
class HTTPResponder {

public:
  using BuilderQ = HTTPBuilderQ<Catalog>;
  using Builder = typename BuilderQ::Node;

  HTTPResponder(Impl *impl_, Zjrpc::Limits limits_) :
    m_impl{impl_}, m_limits{limits_} { }

  bool sent() const { return m_sent; }
  bool streaming() const { return m_streaming; }
  bool terminal() const { return m_terminal; }
  Builder *builder() const { return m_builder.ptr(); }

  void sessionID(ZuCSpan id) { m_sessionID = id; }
  void prepare(int policy) {
    if (policy != Zjrpc::BodyPolicy::SSE || m_builder || m_terminal) return;
    builder_();
    auto stream = m_builder->data().stream(m_limits);
    stream->owner(m_impl, 0);
    stream->sessionID(m_sessionID);
    m_streaming = true;
    send_();
  }

  void emit(typename Server_::DispatchPolicy<Catalog>::Message message) {
    if (m_terminal) return;
    if (message.empty()) { finish(); return; }
    if (m_streaming) {
      message.dispatch([this](auto, auto &value) { this->emit(ZuMv(value)); });
      return;
    }
    // Fixed output retains the whole message, including a borrowed-input pin.
    emitFixed_(ZuMv(message));
  }

  template <typename M>
  void emit(M message) {
    if (m_terminal) return;
    if (m_streaming) {
      auto stream = m_builder->data().streamPtr();
      if (!stream || !stream->emit(ZuMv(message))) {
	fail_();
	return;
      }
      enum { Terminal =
	!ZuIsSame<M, ProgressMessage>{} && !ZuIsSame<M, LogMessage>{} };
      if (Terminal) m_terminal = true;
      return;
    }
    emitFixed_(ZuMv(message));
  }

  void finish() {
    if (m_terminal) return;
    builder_();
    auto fixed = m_builder->data().accepted();
    fixed->sessionID(m_sessionID);
    send_();
    m_terminal = true;
  }

  void missing() {
    if (m_terminal) return;
    builder_();
    auto fixed = m_builder->data().missing();
    fixed->sessionID(m_sessionID);
    send_();
    m_terminal = true;
  }

  void close() {
    releaseBuilder_(true);
    m_terminal = true;
  }

  void closed() {
    releaseBuilder_(false);
    m_terminal = true;
  }

private:
  template <typename M>
  void emitFixed_(M message) {
    builder_();
    auto fixed = m_builder->data().fixed(ZuMv(message), m_limits.maxJSONBytes);
    fixed->sessionID(m_sessionID);
    send_();
    m_terminal = true;
  }

  void builder_() {
    if (m_builder) return;
    m_builder = new Builder{};
    m_builder->data().owner(m_impl);
  }

  void send_() {
    if (m_sent || !m_builder) return;
    m_sent = true;
    bool fixed = m_builder->data().fixedPtr();
    m_impl->send(m_builder);
    if (fixed) {
      m_sentBuilder = m_builder.ptr();
      m_builder = nullptr;
    }
  }

  void fail_() {
    m_terminal = true;
    m_impl->failed();
  }

  void releaseBuilder_(bool close) {
    if (m_builder) {
      m_builder->data().disown();
      if (close) m_builder->data().close();
      m_builder = nullptr;
    }
    if (m_sentBuilder) {
      m_sentBuilder->data().disown();
      m_sentBuilder = nullptr;
    }
  }

  Impl		*m_impl;
  Zjrpc::Limits	m_limits;
  ZmRef<Builder> m_builder;
  Builder	*m_sentBuilder = nullptr;
  Zjrpc::HTTPValue	m_sessionID;
  bool		m_streaming = false;
  bool		m_terminal = false;
  bool		m_sent = false;
};

template <typename Derived, typename Impl, typename Catalog>
class Server {
  friend Derived;
  auto derived() { return static_cast<Derived *>(this); }
  auto derived() const {
    return static_cast<const Derived *>(this);
  }

public:
  using AppHeaderList = Zjrpc::AppHeaders<Impl>;
  using Headers = Zjrpc::HTTPHeaders<AppHeaderList>;

protected:
  Server() = default;

  bool init(ZiMultiplex *mx, unsigned owner, Zjrpc::Limits limits,
      Impl *impl, unsigned histSize) {
    if (!mx || !owner || !impl || !Zjrpc::valid(limits)) return false;
    m_impl = impl;
    m_mx = mx;
    m_txThread = owner;
    m_limits = limits;
    m_histSize = histSize;
    m_up = true;
    return true;
  }

  bool initPeer_(ZiMultiplex *mx, unsigned owner, Zjrpc::Limits limits,
      Impl *impl, unsigned histSize) {
    if (!init(mx, owner, limits, impl, histSize)) return false;
    derived()->m_peer = Peer<Catalog>{limits};
    derived()->m_pending.init(histSize);
    return true;
  }

public:
  Impl *impl() const { return m_impl; }
  ZiMultiplex *mx() const { return m_mx; }
  unsigned ownerThread() const { return m_txThread; }
  Zjrpc::CompletionRoute completionRoute() const { return {m_mx, m_txThread}; }
  bool up() const { return m_up.load_(); }
  bool invoked() const { return m_mx && m_mx->invoked(m_txThread); }
  Peer<Catalog> &peer() { return derived()->m_peer; }
  bool response(const Zjrpc::Envelope &) { return true; }
  template <typename L>
  void continue_(L &&l) { m_mx->run(ZuFwd<L>(l), m_txThread); }

  const Zjrpc::Limits &limits() const { return m_limits; }
  unsigned histSize() const { return m_histSize; }
  void histSize(unsigned v) { if (!m_mx) m_histSize = v; }

  template <typename L>
  bool ownerRun(L &&l) {
    if (!m_mx || !m_up.load_()) return false;
    if (m_mx->invoked(m_txThread))
      l();
    else
      m_mx->run(ZuFwd<L>(l), m_txThread);
    return true;
  }

private:
  template <typename Done>
  void closePeer_(Done done) {
    m_up = false;
    derived()->m_peer.close();
    derived()->closeRpc_([this, done = ZuMv(done)]() mutable {
      (void)derived()->m_pending.close(0);
      derived()->m_context.close();
      done();
    });
  }

  template <typename App,
    typename = decltype(ZuDeclVal<App * &>()->closed(), void())>
  static void closed_(App *app, int) { app->closed(); }
  template <typename App>
  static void closed_(App *, long) { }

  template <typename App,
    typename = decltype(ZuDeclVal<App * &>()->failed(), void())>
  static void failed_(App *app, int) { app->failed(); }
  template <typename App>
  static void failed_(App *, long) { }

  Impl			*m_impl = nullptr;
  ZiMultiplex		*m_mx = nullptr;
  Zjrpc::Limits		m_limits;
  unsigned		m_txThread = 0;
  unsigned		m_histSize = Zjrpc::Default::HistSize;

  ZmAtomic<unsigned>	m_up = 0;
};

template <typename Impl, typename Catalog>
class IOServer : public Server<IOServer<Impl, Catalog>, Impl, Catalog>,
    public Zjrpc::Dispatcher<IOServer<Impl, Catalog>, Impl, Catalog,
      Context, Server_::DispatchPolicy<Catalog>> {
  using Base = Server<IOServer, Impl, Catalog>;
  using Base::m_impl;
  using Base::m_mx;
  using Base::m_limits;
  using Base::m_txThread;
  using Base::m_histSize;
  using Base::m_up;
  using Base::initPeer_;
  using Base::closePeer_;
  using Base::closed_;
  using Base::failed_;
  using Dispatch = Zjrpc::Dispatcher<IOServer, Impl, Catalog,
    Context, Server_::DispatchPolicy<Catalog>>;
  friend Dispatch;
  friend Base;
  using IO = Zjrpc::IOLink<IOServer>;
public:
  ~IOServer() { this->final(); }

  bool init(ZiMultiplex *mx, Zjrpc::StdioConfig config, Impl *impl) {
    if (this->m_mx || !impl || !mx || !mx->txThread()) return false;
    if (!initPeer_(mx, mx->txThread(), config.limits(), impl, m_histSize)) return false;
    new (m_stdio.template new_<IO>()) IO{
      this, m_mx, m_txThread, ZuMv(config)};
    m_stdioStarted = false;
    m_stdioClosing = false;
    m_stdioDone = false;
    m_stdioFailure = false;
    return true;
  }

  bool start() {
    if (!this->m_stdio.template ptr<IO>() || !this->m_up.load_() ||
	this->m_stdioStarted || this->m_stdioDone.load_()) return false;
    return ZmBlock<bool>{}([this](auto wake) mutable {
      this->m_mx->run([this, wake = ZuMv(wake)]() mutable {
	bool ok = false;
	try {
	  this->m_context = Server_::openContext(
	    this->m_impl, SessionTag{}, 0, true, ZuCSpan{});
	  m_rpcContext = Context{nullptr, this->m_context.ptr()};
	  ok = this->m_stdio.template p<IO>().start_();
	} catch (...) {
	}
	this->m_stdioStarted = ok;
	if (!ok) {
	  this->m_context.close();
	  this->m_up = false;
	  this->m_stdioDone = true;
	}
	wake(ok);
      }, this->m_txThread);
    });
  }

  void stdioClosed() { stdioDone_(false); }
  void stdioFailed() { stdioDone_(true); }

  bool stdioFrame(ZmRef<ZiIOBuf> body) {
    return Dispatch::receive(ZuMv(body), [this](auto message, int policy) {
      if (policy == Zjrpc::RoutePolicy::Abort) { fail_(); return false; }
      return message.empty() || send_(message);
    }, &m_pending, &m_rpcContext);
  }

  bool stop() { return this->stopStdio_(); }

  void final() {
    if (!this->m_mx) return;
    (void)this->stopStdio_();
    this->m_stdio.null();
    this->m_peer.close();
    this->m_impl = nullptr;
    this->m_mx = nullptr;
    this->m_txThread = 0;
  }

private:
  bool stopStdio_() {
    if (!m_mx) return false;
    if (m_stdioDone.load_()) return !m_stdioFailure;
    return ZmBlock<bool>{}([this](auto wake) mutable {
      m_mx->run([this, wake = ZuMv(wake)]() mutable {
	m_stdioStopFn = ZuMv(wake);
	m_up = false;
	m_stdioClosing = true;
	if (m_stdioStarted)
	  m_stdio.template p<IO>().stop_();
	else
	  stdioDone_(false);
      }, m_txThread);
    });
  }

  void failStdio_() {
    ZiAssert(m_mx && m_mx->invoked(m_txThread), "Zmcp", (),
	"stdio failure outside owner shard", return);
    if (m_stdioDone.load_() || m_stdioClosing) return;
    m_stdioFailure = true;
    m_stdioClosing = true;
    m_up = false;
    m_stdio.template p<IO>().stop_();
  }

  void stdioDone_(bool failed) {
    ZiAssert(m_mx && m_mx->invoked(m_txThread), "Zmcp", (),
	"stdio completion outside owner shard", return);
    if (m_stdioDone.load_()) return;
    m_stdioFailure |= failed;
    m_stdioClosing = true;
    m_up = false;
    m_peer.close();
    closePeer_([this]() mutable {
      m_stdioDone = true;
      try {
	if (m_stdioFailure)
	  failed_(m_impl, 0);
	else
	  closed_(m_impl, 0);
      } catch (...) {
      }
      auto stopFn = ZuMv(m_stdioStopFn);
      m_stdioStopFn = {};
      if (stopFn) stopFn(!m_stdioFailure);
    });
  }

  template <typename M>
  bool send_(const M &message) {
    return this->m_up.load_() && m_stdio.template p<IO>().send_(message);
  }
  void fail_() { this->failStdio_(); }

  template <typename Done>
  void closeRpc_(Done done) {
    if (!Dispatch::close()) {
      this->continue_([this, done = ZuMv(done)]() mutable { closeRpc_(ZuMv(done)); });
      return;
    }
    done();
  }

  Context m_rpcContext;
  Zjrpc::InboundCalls	m_pending;
  Peer<Catalog>	m_peer;
  Server_::ContextRef	m_context;
  ZmFn<void(bool)>	m_stdioStopFn;
  bool			m_stdioStarted = false;
  bool			m_stdioClosing = false;
  bool			m_stdioFailure = false;
  ZmAtomic<unsigned>	m_stdioDone = 0;
  ZuUnion<void, IO> m_stdio;
};

template <typename Impl, typename Catalog>
class HTTPServer : public Server<HTTPServer<Impl, Catalog>, Impl, Catalog>,
    public Zjrpc::Dispatcher<HTTPServer<Impl, Catalog>, Impl, Catalog,
      Server_::HTTPDispatchContext<Zjrpc::HTTPHeaders<Zjrpc::AppHeaders<Impl>>, Catalog>,
      Server_::HTTPDispatchPolicy<Catalog>> {
  using Base = Server<HTTPServer, Impl, Catalog>;
  using Base::m_impl;
  using Base::m_mx;
  using Base::m_limits;
  using Base::m_txThread;
  using Base::m_histSize;
  using Base::m_up;
  template <typename Link, typename Heap> class Route_;

  template <typename Link>
  using RouteDefault = Route_<Link, ZuVoid>;

  template <typename Link>
  ZuDerive(RouteHeap, (ZmHeap<"Zmcp.HTTP.Route", RouteDefault<Link>>));

  template <typename Link>
  ZuDerive(Route, (Route_<Link, RouteHeap<Link>>));

  using Session = Server_::HTTPSession<Catalog>;

  using RpcContext = Server_::HTTPDispatchContext<typename Base::Headers, Catalog>;
  using Dispatch = Zjrpc::Dispatcher<HTTPServer, Impl, Catalog,
    RpcContext, Server_::HTTPDispatchPolicy<Catalog>>;
  friend Base;
  friend Dispatch;
public:
  using AppHeaderList = typename Base::AppHeaderList;
  using Headers = typename Base::Headers;
  using ResBuilderQ = HTTPBuilderQ<Catalog>;
  using HTTP = Zhttp::Server<HTTPServer>;

  class Parser : public HTTPParser<Parser> {
    using Base = HTTPParser<Parser>;
    using BaseKeys = ZuTypeSlice<2, 0, typename Base::Headers>;

  public:
    using AppKeys = ZuTypeSlice<2, 0, AppHeaderList>;
    using HeaderKeys = ZuTypeConcat<BaseKeys, AppKeys>;
    using Headers = ZuTypeConcat<typename Base::Headers, AppHeaderList>;
    using HdrCatalog = Zjrpc::HTTPHdrCatalog<Headers>;
    ZuAssert(ZuTypeUnique<HeaderKeys>::N == HeaderKeys::N,
      "Zmcp application header duplicates a protocol header");

    void init(HTTPServer &server) { m_server = &server; }

    ZuCSpan endpoint() const { return m_server->endpoint(); }
    const Zjrpc::Limits &limits() const { return m_server->limits(); }
    bool origin(ZuCSpan origin) const {
      return m_server->origin(origin);
    }

    template <typename Key, typename Value>
    void header(Zhttp::FieldSection::T section) {
      if constexpr (ZuTypeIn<Key, AppKeys>{})
	m_headers.template put<Key>(Value{}());
      Base::template header<Key, Value>(section);
    }

    template <typename Key>
    void header(
	Zhttp::FieldSection::T section, ZuSpan<uint8_t> value) {
      if constexpr (ZuTypeIn<Key, AppKeys>{})
	m_headers.template put<Key>(value);
      Base::template header<Key>(section, value);
    }

    template <typename Link>
    void receiveHTTP(
	Link *link, ZmRef<ZiIOBuf> body, HTTPMeta meta) {
      auto headers = ZuMv(m_headers);
      m_headers = {};
      m_server->receiveHTTP(
	link, ZuMv(body), ZuMv(meta), ZuMv(headers));
    }

    template <typename Link>
    void deleteHTTP(Link *link, const HTTPMeta &meta) {
      m_server->deleteHTTP(link, meta);
    }

    template <typename Link>
    void originHTTP(Link *link) { m_server->originHTTP(link); }

    template <typename Link>
    void corruptHTTP(Link *link) { m_server->corruptHTTP(link); }

    void reset() {
      Base::reset();
      m_headers = {};
      m_server = nullptr;
    }

  private:
    HTTPServer *m_server = nullptr;
    Zjrpc::HTTPHeaders<AppHeaderList> m_headers;
  };
  ZuCSpan endpoint() const { return m_endpoint; }
  bool origin(ZuCSpan origin_) const {
    if (!origin_) return m_absentOrigin;
    try {
      return m_impl->origin(origin_);
    } catch (...) {
      return false;
    }
  }

  template <typename Link>
  void receiveHTTP(
      Link *link, ZmRef<ZiIOBuf> body, HTTPMeta meta,
      Headers headers = {}) {
    if (!link || !body || !m_up.load_()) return;
    ZmRef<Route<Link>> route = new Route<Link>{
      this, link, ZuMv(body), ZuMv(meta), ZuMv(headers)};
    this->ownerRun([route = ZuMv(route)]() mutable { route->run_(); });
  }

  template <typename Link>
  void deleteHTTP(Link *link, const HTTPMeta &meta) {
    if (!link) return;
    Zjrpc::HTTPValue sessionID = meta.sessionID;
    this->ownerRun([this, link = ZmRef(link),
	sessionID = ZuMv(sessionID)]() mutable {
      if (sessionID) {
	auto node = m_sessions->del(sessionID);
	if (node) {
	  ZuRef<Session> session = ZuRef<Session>::acquire(ZuMv(node).release());
	  closeSession_(ZuMv(session), []() { });
	}
      }
      ZmRef<typename ResBuilderQ::Node> response =
	new typename ResBuilderQ::Node{};
      response->data().accepted();
      link->send(ZuMv(response));
    });
  }

  template <typename Link>
  void originHTTP(Link *link) {
    if (!link) return;
    ZmRef<typename ResBuilderQ::Node> response =
      new typename ResBuilderQ::Node{};
    response->data().forbidden();
    link->send(ZuMv(response));
  }

  template <typename Link>
  void corruptHTTP(Link *link) {
    if (!link) return;
    this->ownerRun([link = ZmRef(link)]() mutable { link->disconnect(); });
  }

  bool expireSession(ZuCSpan id) {
    if (!m_mx || !id) return false;
    Zjrpc::HTTPValue owned{id};
    return this->ownerRun([this, owned = ZuMv(owned)]() mutable {
      auto entry = m_sessions->findPtr(owned);
      if (!entry) return;
      auto node = m_sessions->delNode(entry);
      ZuRef<Session> session = ZuRef<Session>::acquire(ZuMv(node).release());
      closeSession_(ZuMv(session), []() { });
    });
  }

  void listening(int transport, unsigned port) {
    try { m_impl->listening(transport, port); } catch (...) { }
  }
  void listenFailed(int transport, bool transient) {
    try { m_impl->listenFailed(transport, transient); } catch (...) { }
  }
  void connected(int transport) {
    try { m_impl->connected(transport); } catch (...) { }
  }
  void disconnected(int transport) {
    try { m_impl->disconnected(transport); } catch (...) { }
  }

  void connected(Zhttp::Session session) {
    if (!m_mx || !session) return;
    m_mx->run([this, session]() {
      if (m_transportContexts->findPtr(session.id)) return;
      try {
	m_transportContexts->add(Server_::TransportContextEntry{
	  session.id, Server_::openContext(
	    m_impl, TransportTag{}, 0, session)});
      } catch (...) {
      }
    }, m_txThread);
  }

  void disconnected(Zhttp::Session session) {
    if (!m_mx || !session) return;
    m_mx->run([this, session]() {
      (void)m_transportContexts->del(session.id);
    }, m_txThread);
  }

  ~HTTPServer() { this->final(); }

  bool init(const Zhttp::HubConfig &hub, ServerConfig config, Impl *impl) {
    if (this->m_mx || !impl || !hub.mx() || !config.endpoint())
      return false;
    unsigned owner = hub.txThread() ?
      hub.mx()->sid(hub.txThread()) : hub.mx()->txThread();
    if (!Base::init(hub.mx(), owner, config.limits(), impl, config.histSize())) return false;
    m_endpoint = config.endpoint();
    m_absentOrigin = config.absentOrigin();
    m_legacySessions = config.legacySessions();
    m_legacyLifetime = config.legacyLifetime();
    m_maxSessions = config.maxSessions();
    m_httpDone = false;
    if (m_legacySessions && !m_rng.init()) return false;
    {
      uint64_t messageMax = uint64_t(m_limits.maxSSEEventBytes) +
	Zjrpc::Default::SSEFrameOverhead;
      if (messageMax < m_limits.maxJSONBytes)
	messageMax = m_limits.maxJSONBytes;
      config.retainedBodyMax(m_limits.maxJSONBytes)
	.retainedMessageMax(messageMax);
    }
    m_up = true;
    if (m_http.init(hub, ZuMv(config), this)) {
      return true;
    }
    m_up = false;
    m_impl = nullptr;
    m_mx = nullptr;
    return false;
  }

  HTTP &http() { return m_http; }
  const HTTP &http() const { return m_http; }

  bool start() { return this->m_http.start(); }

  bool stop() {
    if (this->m_httpDone) return true;
    this->m_up = false;
    bool ok = this->m_http.stop();
    this->closeHTTP_();
    this->m_httpDone = true;
    return ok;
  }

  void final() {
    if (!this->m_mx) return;
    this->m_up = false;
    if (!this->m_httpDone) {
      (void)this->m_http.stop();
      this->closeHTTP_();
      this->m_httpDone = true;
    }
    this->m_http.final();
    this->m_impl = nullptr;
    this->m_mx = nullptr;
    this->m_txThread = 0;
    this->m_endpoint.null();
  }

private:
  Session *session_(ZuCSpan id) {
    return m_sessions->findPtr(id);
  }

  Session *newSession_() {
    if (m_sessions->count_() >= m_maxSessions) return nullptr;
    auto entropy = ZtScratch(Server_::SessionEntropy,
      Default::SessionIDBytes, Default::SessionIDBytes);
    if (!m_rng.random(entropy)) return nullptr;
    Zjrpc::HTTPValue id;
    id << ZtQuote::Hex{entropy};
    if (m_sessions->findPtr(id)) return nullptr;
    auto context = Server_::openContext(
      m_impl, SessionTag{}, 0, false, ZuCSpan{id});
    ZuRef<Session> session = new Session{
      ZuMv(id), m_limits, ZuMv(context), m_histSize};
    auto ptr = session.ptr();
    m_sessions->addNode(ptr);
    if (m_legacyLifetime)
      m_mx->add(&ptr->timer, Zm::now(int(m_legacyLifetime)),
	ZmScheduler::Update,
	[this, session = ptr](auto &&arm) {
	  return arm([this, session]() {
	    expireSession_(session);
	  });
	}, m_txThread);
    return ptr;
  }

  void expireSession_(Session *session) {
    auto entry = m_sessions->findPtr(session->id);
    if (entry != session) return;
    auto node = m_sessions->delNode(entry);
    ZuRef<Session> owned = ZuRef<Session>::acquire(ZuMv(node).release());
    closeSession_(ZuMv(owned), []() { });
  }

  template <typename Done>
  void closeSession_(ZuRef<Session> session, Done done) {
    if (!session->closing) {
      m_mx->del(&session->timer);
      (void)session->close(m_limits.workBatch);
      m_mx->run([
	this, session = ZuMv(session), done = ZuMv(done)]() mutable {
	closeSession_(ZuMv(session), ZuMv(done));
      }, m_txThread);
      return;
    }
    if (session->close(m_limits.workBatch)) {
      done();
      return;
    }
    m_mx->run([
      this, session = ZuMv(session), done = ZuMv(done)]() mutable {
      closeSession_(ZuMv(session), ZuMv(done));
    }, m_txThread);
  }

  void closeHTTP_() {
    if (!m_mx) return;
    ZmBlock<>{}([this](auto wake) mutable {
	m_mx->run([this, wake = ZuMv(wake)]() mutable {
	closeRpc_([this, wake = ZuMv(wake)]() mutable {
	  closeSessions_([this, wake = ZuMv(wake)]() mutable {
	    closeRoutes_([this, wake = ZuMv(wake)]() mutable {
	      closeSSE_([this, wake = ZuMv(wake)]() mutable {
		closeTransportContexts_(ZuMv(wake));
	      });
	    });
	  });
	});
      }, m_txThread);
    });
  }

  template <typename Done>
  void closeSessions_(Done done, unsigned visited = 0) {
    if (visited >= m_limits.workBatch) {
      m_mx->run([this, done = ZuMv(done)]() mutable {
	closeSessions_(ZuMv(done));
      }, m_txThread);
      return;
    }
    auto node = takeSession_();
    if (!node) {
      done();
      return;
    }
    ZuRef<Session> session = ZuRef<Session>::acquire(ZuMv(node).release());
    closeSession_(ZuMv(session), [
      this, done = ZuMv(done), visited]() mutable {
      closeSessions_(ZuMv(done), visited + 1);
    });
  }

  typename Server_::HTTPSessionHash<Catalog>::NodeMvRef takeSession_() {
    auto i = m_sessions->iter();
    if (!i()) return decltype(i.del()){};
    return i.del();
  }
  unsigned maxJSONBytes_() const { return m_limits.maxJSONBytes; }
  bool legacySessions_() const { return m_legacySessions; }
  bool assertTx_() const {
    ZiAssert(this->invoked(), "Zmcp", (),
      "operation outside owner shard", return false);
    return true;
  }
  ZuRef<Zjrpc::HTTPPeer> httpPeer_(const Headers &headers) {
    return Zjrpc::httpPeer(m_impl, headers, 0);
  }

  template <typename L>
  bool postSSE_(L &&l) {
    if (!m_mx || !m_up.load_()) return false;
    m_mx->run(ZuFwd<L>(l), m_txThread);
    return true;
  }
  template <typename Link, typename Heap = ZuVoid>
  class Route_ : public Heap, public Server_::RouteQ::Node {
    using Self = Route_<Link, Heap>;
    using Base = Server_::RouteQ::Node;
    using Responder = HTTPResponder<Catalog, Self>;
    using RpcContext = Server_::HTTPDispatchContext<Headers, Catalog>;

  public:
    Route_(HTTPServer *server, Link *link, ZmRef<ZiIOBuf> body,
	HTTPMeta meta, Headers headers) :
      Base{[](Server_::RouteBase *route) { static_cast<Self *>(route)->close_(); }},
      m_server{server}, m_link{link}, m_body{ZuMv(body)},
      m_meta{ZuMv(meta)}, m_headers{ZuMv(headers)},
      m_responder{this, server->limits()}, m_peer{server->limits()} { }

    void run_() {
      try { run__(); }
      catch (...) { failed(); }
    }

  private:
    void run__() {
      if (!m_server->register_(ZmRef(this))) {
	m_link->disconnect();
	return;
      }
      auto parsed = Zjrpc::parse(ZuSpan<char>{m_body->span()}, m_server->maxJSONBytes_());
      if (!parsed) { failed(); return; }
      Peer<Catalog> *peer = &m_peer;
      if (m_server->legacySessions_()) {
	if (m_meta.sessionID) {
	  m_session = m_server->session_(m_meta.sessionID);
	  if (!m_session) {
	    m_responder.missing();
	    return;
	  }
	} else if (parsed.envelope.kind == Zjrpc::MessageKind::Request &&
	    parsed.envelope.method() == "initialize") {
	  m_session = m_server->newSession_();
	  if (!m_session) {
	    failed();
	    return;
	  }
	}
	if (m_session) {
	  peer = &m_session->peer;
	  m_responder.sessionID(m_session->id);
	}
      } else if (m_meta.version == LegacyVersion{}()) {
	m_peer.statelessLegacy();
      }
      if (!m_session) m_rpcPeer = m_server->httpPeer_(m_headers);
      if (m_session) m_session->stream.replace(ZmRef(this));
      m_rpcContext = RpcContext{
	Context{m_server->transportContext_(m_link->session()),
	  m_session ? m_session->context.ptr() : nullptr},
	&m_headers, peer, this, [](void *route, int policy) {
	  static_cast<Self *>(route)->m_responder.prepare(policy);
	}, [](void *route) { return bool(static_cast<Self *>(route)->m_link); }};
      m_streamContext = Server_::openContext(
	m_server->impl(), StreamTag{}, 0, m_rpcContext.context);
      m_rpcContext.context = Context{m_rpcContext.context.transport(),
	m_rpcContext.context.session(), m_streamContext.ptr()};
      m_link->responseCancel(Zhttp::StreamCancelFn{this, [](Self *self) {
	self->responseCancelled_();
      }});
      if (!m_server->receiveRpc_(ZuMv(m_body), ZuMv(parsed),
	  [self = ZmRef(this)](auto message, int policy) {
	    return self->emit_(ZuMv(message), policy);
	  }, inbound_(), &m_rpcContext, &m_control)) failed();
    }

    bool emit_(typename Server_::DispatchPolicy<Catalog>::Message message, int policy) {
      if (m_closed) return true;
      if (policy == Zjrpc::RoutePolicy::Abort) { failed(); return true; }
      if (!m_link) { finish_(); return true; }
      m_responder.emit(ZuMv(message));
      if (m_responder.terminal() && !m_responder.sent()) finish_();
      return true;
    }

  public:
    void send(ZmRef<typename Responder::Builder> builder) {
      if (m_link) m_link->send(ZuMv(builder));
    }
    bool postSSE_() {
      return m_server->postSSE_([work = ZmRef(this)]() mutable {
	if (!work->live_()) return;
	auto builder = ZmRef(work->m_responder.builder());
	if (!builder) return;
	auto stream = builder->data().streamPtr();
	if (stream) stream->resume_();
      });
    }
    bool discardSSE_(Zjrpc::SSEQueue queue) {
      m_server->discardSSE_(ZuMv(queue));
      return true;
    }
    void failed() { close_(); }

    void streamClosed_() {
      if (m_closed || !m_link) return;
      ZmRef<Self> self{this};
      auto link = ZuMv(m_link);
      link->responseCancel({});
      m_responder.close();
      closeStream_();
      link->disconnect();
      if (!inbound_() || (m_session && m_session->closing)) m_control.close();
    }
    void responseCancelled_() {
      if (!m_server->assertTx_() || m_closed || !m_link) return;
      ZmRef<Self> self{this};
      m_link = nullptr;
      m_responder.close();
      closeStream_();
      if (!inbound_()) { m_control.close(); finish_(); }
    }
    void responseClosed_() {
      if (!m_server->assertTx_() || m_closed) return;
      ZmRef<Self> self{this};
      m_responder.closed();
      closeStream_();
      if (m_link) m_link->responseCancel({});
      m_link = nullptr;
      if (!inbound_()) m_control.close();
      finish_();
    }
    void close_() {
      if (m_closed) return;
      ZmRef<Self> self{this};
      m_closed = true;
      m_control.close();
      m_responder.close();
      closeStream_();
      if (m_link) {
	m_link->responseCancel({});
	m_link->disconnect();
	m_link = nullptr;
      }
      finish_();
    }

  private:
    void closeStream_() {
      // Later batch members can still run for a stable HTTP peer after detach.
      // Do not pass them pointers to contexts already closed with the route.
      m_rpcContext.context = Context{
	m_link ? m_rpcContext.context.transport() : nullptr,
	m_rpcContext.context.session()};
      m_streamContext.close();
    }
    bool live_() const {
      return this->linked;
    }
    void finish_() {
      if (!this->linked) return;
      ZmRef<Self> self{this};
      if (m_session) m_session->stream.clear(this);
      closeStream_();
      m_server->remove_(this);
    }
    Zjrpc::InboundCalls *inbound_() {
      if (m_session) return &m_session->pending;
      return m_rpcPeer ? m_rpcPeer->inbound() : nullptr;
    }

    HTTPServer *m_server;
    ZmRef<Link> m_link;
    ZmRef<ZiIOBuf> m_body;
    HTTPMeta m_meta;
    Headers m_headers;
    Responder m_responder;
    Peer<Catalog> m_peer;
    RpcContext m_rpcContext;
    Server_::ContextRef m_streamContext;
    Zjrpc::WorkControl m_control;
    // Aggregate dispatch and detached output can outlive session membership.
    ZuRef<Session> m_session;
    ZuRef<Zjrpc::HTTPPeer> m_rpcPeer;
    bool m_closed = false;
  };

  template <typename RouteT>
  bool register_(ZmRef<RouteT> route) {
    if (!m_up.load_() || m_routes.count_() >= m_limits.maxPending)
      return false;
    route->linked = true;
    m_routes.pushNode(route.ptr());
    return true;
  }

  void remove_(Server_::RouteQ::Node *route) {
    route->linked = false;
    auto removed = m_routes.delNode(route);
  }

  void discardSSE_(Zjrpc::SSEQueue queue) {
    ZiAssert(m_mx && m_mx->invoked(m_txThread), "Zmcp", (),
	"SSE discard outside owner shard", return);
    if (!queue.count_()) return;
    m_sseGarbage += ZuMv(queue);
    postSSEDrain_();
  }

  void postSSEDrain_() {
    if (m_ssePosted || !m_mx) return;
    m_ssePosted = true;
    m_mx->run([this]() { drainSSE_(); }, m_txThread);
  }

  void drainSSE_() {
    m_ssePosted = false;
    unsigned visited = 0;
    while (visited < m_limits.workBatch && m_sseGarbage.shift())
      ++visited;
    if (m_sseGarbage.count_()) {
      postSSEDrain_();
      return;
    }
    auto closeFn = ZuMv(m_sseCloseFn);
    m_sseCloseFn = {};
    if (closeFn) closeFn();
  }

  template <typename Done>
  void closeSSE_(Done done) {
    ZiAssert(!m_sseCloseFn, "Zmcp", (),
	"duplicate SSE close continuation", return);
    if (!m_sseGarbage.count_() && !m_ssePosted) {
      done();
      return;
    }
    m_sseCloseFn = Zjrpc::SSECloseFn{ZuMv(done)};
    postSSEDrain_();
  }



  void *transportContext_(Zhttp::Session session) {
    auto entry = m_transportContexts->findPtr(session.id);
    return entry ? entry->context.ptr() : nullptr;
  }

  template <typename Done>
  void closeTransportContexts_(Done done) {
    unsigned visited = 0;
    while (visited < m_limits.workBatch) {
      auto entry = takeTransportContext_();
      if (!entry) {
	done();
	return;
      }
      ++visited;
    }
    m_mx->run([this, done = ZuMv(done)]() mutable {
      closeTransportContexts_(ZuMv(done));
    }, m_txThread);
  }

  Server_::TransportContextHash::NodeMvRef takeTransportContext_() {
    auto i = m_transportContexts->iter();
    if (!i()) return decltype(i.del()){};
    return i.del();
  }

  template <typename Done>
  void closeRoutes_(Done done) {
    unsigned visited = 0;
    while (visited < m_limits.workBatch) {
      auto entry = m_routes.headNode();
      if (!entry) {
	done();
	return;
      }
      ++visited;
      entry->close();
    }
    m_mx->run([
      this, done = ZuMv(done)]() mutable { closeRoutes_(ZuMv(done));
    }, m_txThread);
  }

  void fail_() { }
  bool receiveRpc_(ZmRef<ZiIOBuf> body, Zjrpc::Parsed parsed,
      typename Dispatch::Emit emit, Zjrpc::InboundCalls *inbound,
      RpcContext *context, Zjrpc::WorkControl *control) {
    return Dispatch::receive(ZuMv(body), ZuMv(parsed), ZuMv(emit), inbound, context, control);
  }
  template <typename Done>
  void closeRpc_(Done done) {
    if (!Dispatch::close()) {
      this->continue_([this, done = ZuMv(done)]() mutable { closeRpc_(ZuMv(done)); });
      return;
    }
    done();
  }

  Server_::RouteQ m_routes;
  using ContextTable = Server_::ContextTable;
  using SessionTable = Server_::SessionTable<Catalog>;
  ZmRef<ContextTable> m_transportContexts = new ContextTable{};
  ZmRef<SessionTable> m_sessions = new SessionTable{};
  Zjrpc::SSEQueue	m_sseGarbage;
  Zjrpc::SSECloseFn	m_sseCloseFn;
  Ztls::Random		m_rng;
  Zjrpc::HTTPValue		m_endpoint;
  unsigned		m_maxSessions = Default::MaxSessions;
  unsigned		m_legacyLifetime = 0;
  bool			m_absentOrigin = false;
  bool			m_legacySessions = true;
  bool			m_httpDone = false;
  bool			m_ssePosted = false;
  HTTP m_http;
};

template <typename Impl, typename Catalog, typename Profile = Zhttp::H1TCP>
class WSServer : public Server<WSServer<Impl, Catalog, Profile>, Impl, Catalog>,
    public Zjrpc::WSIO<WSServer<Impl, Catalog, Profile>> {
  using Common = Server<WSServer, Impl, Catalog>;
  using IO = Zjrpc::WSIO<WSServer>;
  friend Common;
  friend IO;
public:
  using WS = Zws::Server<WSServer, Profile>;
  using Link = typename WS::Link;
  using Config = typename WS::Config;

  class Connection : public Server<Connection, Impl, Catalog>,
      public Zjrpc::Dispatcher<Connection, Impl, Catalog,
	Context, Server_::DispatchPolicy<Catalog>> {
    using Base = Server<Connection, Impl, Catalog>;
    using Dispatch = Zjrpc::Dispatcher<Connection, Impl, Catalog,
      Context, Server_::DispatchPolicy<Catalog>>;
    friend Dispatch;
    friend Base;
  public:
    bool init(WSServer *server, Link *link) {
      m_server = server;
      m_link = link;
      if (!Base::initPeer_(server->mx(), server->ownerThread(),
	server->limits(), server->impl(), server->histSize())) return false;
      m_context = Server_::openContext(
	server->impl(), SessionTag{}, 0, true, ZuCSpan{});
      m_rpcContext = Context{nullptr, m_context.ptr()};
      return true;
    }
    bool receive(ZmRef<ZiIOBuf> body) {
      return Dispatch::receive(ZuMv(body), [this](auto message, int policy) {
	if (policy == Zjrpc::RoutePolicy::Abort) { fail_(); return false; }
	return message.empty() || send_(message);
      }, &m_pending, &m_rpcContext);
    }
    template <typename Done>
    void close(Done done) { Base::closePeer_(ZuMv(done)); }

  private:
    template <typename M>
    bool send_(const M &message) {
      return this->m_up.load_() && m_server->wsAccept() && m_server->send_(*m_link, message);
    }
    void fail_() {
      this->m_up = false;
      m_peer.close();
      m_link->close(Zws::CloseCode::Internal);
    }

    template <typename Done>
    void closeRpc_(Done done) {
      if (!Dispatch::close()) {
	this->continue_([this, done = ZuMv(done)]() mutable { closeRpc_(ZuMv(done)); });
	return;
      }
      done();
    }

    WSServer *m_server = nullptr;
    Link *m_link = nullptr;
    Context m_rpcContext;
    Zjrpc::InboundCalls m_pending;
    Peer<Catalog> m_peer;
    Server_::ContextRef m_context;
  };
  struct LinkState {
    Zjrpc::WSRx rx;
    ZuUnion<void, Connection> rpc;
  };

  ~WSServer() { final(); }
  bool init(const Zhttp::HubConfig &hub, ZiIP address, unsigned port,
      Config profile, Zjrpc::WSConfig config, Impl *impl) {
    if (this->m_mx || !hub.mx() || !impl || !port || !Zjrpc::valid(config.limits()))
      return false;
    this->m_impl = impl;
    this->m_mx = hub.mx();
    this->m_txThread = hub.txThread() ? this->m_mx->sid(hub.txThread()) : this->m_mx->txThread();
    this->m_limits = config.limits();
    auto transport = new (m_ws.template new_<WS>()) WS{this, ZuMv(address), port};
    if (!transport->init(hub, ZuMv(profile), config.binding())) {
      m_ws.null();
      this->m_impl = nullptr;
      this->m_mx = nullptr;
      return false;
    }
    this->m_up = true;
    return true;
  }
  bool start() {
    if (!this->m_mx || m_started || !wsAccept() || !ws().start()) return false;
    m_started = true;
    return true;
  }
  bool stop() {
    if (!this->m_mx) return true;
    this->m_up = false;
    bool ok = !m_started || ws().stop();
    m_started = false;
    ZmBlock<>{}([this](auto wake) mutable {
      ws().txRun([this, wake = ZuMv(wake)]() mutable {
	m_stop = ZuMv(wake);
	stopped_();
      });
    });
    return ok;
  }
  void final() {
    if (!this->m_mx) return;
    (void)stop();
    ws().final();
    m_ws.null();
    this->m_impl = nullptr;
    this->m_mx = nullptr;
    this->m_txThread = 0;
  }
  WS &ws() { return m_ws.template p<WS>(); }
  const WS &ws() const { return m_ws.template p<WS>(); }
  bool wsAccept() const { return this->m_up.load_(); }

  bool accept(Link &link, ZuBSpan host, ZuBSpan target, ZuBSpan offered,
      Zws::HandshakeString &selected) {
    return wsAccept() && this->m_impl->accept(link, host, target, offered, selected);
  }
  void listening(const ZiListenInfo &info) { Zws::H1_::listening(*this->m_impl, info, 0); }
  void listening() { Zws::H1_::listening(*this->m_impl, 0); }
  void listenFailed(bool transient) { Zws::H1_::listenFailed(*this->m_impl, transient, 0); }
  void connected(Link &link, Zhttp::ConnectedInfo info) {
    link.state().rx.open();
    ZmRef<Zjrpc::WSOpen<Link>> action = new Zjrpc::WSOpen<Link>{&link, ZuMv(info)};
    ws().txRun([this, action = ZuMv(action)]() mutable {
      if (!wsAccept()) return;
      auto link = action->link.ptr();
      auto connection = new (link->state().rpc.template new_<Connection>()) Connection{};
      try {
	if (!connection->init(this, link)) {
	  link->close(Zws::CloseCode::Internal);
	  return;
	}
	Zws::H1_::connected(*this->m_impl, *link, ZuMv(action->info), 0);
      } catch (...) { link->close(Zws::CloseCode::Internal); }
    });
  }
  void disconnected(Link &link, bool peer) {
    link.state().rx.close();
    ws().txRun([this, link = ZmRef{&link}, peer]() mutable {
      ++m_closing;
      if (auto connection = link->state().rpc.template ptr<Connection>()) {
	connection->close([this, link = ZuMv(link), peer]() mutable {
	  disconnected_(ZuMv(link), peer);
	});
      } else disconnected_(ZuMv(link), peer);
    });
  }
  void closed(Link &link, uint16_t code, ZuBSpan reason) {
    IO::template control_<true>(link, code, reason);
  }
  void error(Link &link, Zws::Failure::T failure) {
    ws().txRun([this, link = ZmRef{&link}, failure]() mutable {
      try {
	Zws::H1_::error(*this->m_impl, *link, failure, 0);
      } catch (...) { link->close(Zws::CloseCode::Internal); }
    });
  }
  void wsFrame_(Link &link, ZmRef<ZiIOBuf> body) {
    ZiAssert(this->invoked(), "Zmcp", (), "WS receive outside owner shard", return);
    if (auto connection = link.state().rpc.template ptr<Connection>())
      (void)connection->receive(ZuMv(body));
  }

private:
  Impl *impl() const { return this->m_impl; }
  template <typename M>
  bool send_(Link &link, const M &message) { return IO::sendWS_(link, message); }
  void disconnected_(ZmRef<Link> link, bool peer) {
    --m_closing;
    try { Zws::H1_::disconnected(*this->m_impl, *link, peer, 0); } catch (...) { }
    stopped_();
  }
  void stopped_() {
    if (m_closing || !m_stop) return;
    auto done = ZuMv(m_stop);
    ws().rxRun([this, done = ZuMv(done)]() mutable {
      ws().txRun([done = ZuMv(done)]() mutable { done(); });
    });
  }

  ZuUnion<void, WS> m_ws;
  ZmFn<void()> m_stop;
  unsigned m_closing = 0;
  bool m_started = false;
};

} // Zmcp

#endif /* ZmcpServer_HH */
