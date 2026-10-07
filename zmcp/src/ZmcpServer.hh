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

#include <zlib/ZhttpServer.hh>

#include <zlib/ZtlsRandom.hh>

namespace Zmcp {

class ServerConfig : public Zhttp::ServerConfig {
public:
  const Limits &limits() const { return m_limits; }
  ZuCSpan endpoint() const { return m_endpoint; }
  bool legacySessions() const { return m_legacySessions; }
  unsigned legacyLifetime() const { return m_legacyLifetime; }
  bool absentOrigin() const { return m_absentOrigin; }

  ServerConfig &limits(Limits v) { m_limits = ZuMv(v); return *this; }
  ServerConfig &endpoint(ZuCSpan v) { m_endpoint = v; return *this; }
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
  HTTPValue	m_endpoint = "/mcp";
  Limits	m_limits;
  unsigned	m_legacyLifetime = 0;
  bool		m_legacySessions = true;
  bool		m_absentOrigin = false;
};

template <typename Message>
struct HTTPFixedBuilder : public Zhttp::ResBuilder {
  using Headers = ZhttpHeaders(
    ("content-type", "application/json"), "content-length");
  using ContentLength = ZuStringT<"content-length">;
  using Zhttp::ResBuilder::header;

  Message message;
  uint64_t maxBodyBytes = Default::MaxJSONBytes;
  mutable uint64_t bodyLength = 0;

  Zhttp::BodyPolicy::T bodyPolicy() const {
    return Zhttp::BodyPolicy::Fixed;
  }
  Zhttp::Method::T method() const { return Zhttp::Method::POST; }
  unsigned status() const { return 200; }

  template <typename Key, typename L>
  void header(L &&l) const {
    if constexpr (ZuIsSame<Key, ContentLength>{})
      l(Zhttp::contentLengthPad());
  }
  template <typename L> void header(L &&) const { }

  template <typename Emit>
  void body(Emit &&emit) const {
    emit([this](auto &s) {
      try {
	HTTPOutput out{s, maxBodyBytes};
	message.write(out);
	out.flush();
	bodyLength = s.produced();
	return out ? Zhttp::WriteOutcome::End : Zhttp::WriteOutcome::Abort;
      } catch (...) {
	return Zhttp::WriteOutcome::Abort;
      }
    });
  }

  template <typename L>
  void bodyHdrs(L &&l) const {
    Zhttp::contentLengthSet(l, bodyLength);
  }
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

inline uintptr_t TransportContextEntry_KeyAxor(
    const TransportContextEntry &entry) {
  return entry.id;
}

ZmHashDerive(TransportContextHash, TransportContextEntry,
  (ZmHashNode<TransportContextEntry,
    ZmHashKey<TransportContextEntry_KeyAxor,
      ZmHashLock<ZmNoLock,
        ZmHashHeapID<"Zmcp.HTTP.Context">>>>));

template <typename Res>
using ReplyMessage = ToolReplyMessage<Res>;

template <typename Reqs>
struct ResponseMessages {
  using Responses = ZuTypeUnique<ZuTypeApply<
    ZuTypeConcat, ZuTypeMap<GetResponses, Reqs>>>;
  using Replies = ZuTypeMap<ReplyMessage, Responses>;
  using Builtins = ZuTypeList<
    EmptyResultMessage, DiscoverMessage, InitializeMessage,
    ToolsListMessage<Reqs>, ErrorMessage, ToolErrorMessage,
    ProgressMessage, LogMessage>;
  using T = typename Builtins::template Push<Replies>;
};

using SSEBuf = ZiIOBufAlloc<ZiIOBuf_DefltSize,
  ZiIOBuf_DefltMaxSize, "Zmcp.HTTP.SSE">;

struct SSERecord {
  ZmRef<ZiIOBuf> buf;
  bool terminal = false;
};

template <typename Token, typename Res, typename Heap = ZuVoid>
class CompleteAction_ : public Heap, public ZmObject {
public:
  CompleteAction_(Token *token_, ToolReply<Res> reply_) :
    m_token{token_}, m_reply{ZuMv(reply_)} { }

  Token *token() const { return m_token.ptr(); }
  ToolReply<Res> reply() { return ZuMv(m_reply); }

private:
  ZmRef<Token>	m_token;
  ToolReply<Res>	m_reply;
};

template <typename Token, typename Res>
ZuDerive(CompleteActionHeap,
  (ZmHeap<"Zmcp.HTTP.Complete", CompleteAction_<Token, Res>>));

template <typename Token, typename Res>
ZuDerive(CompleteAction, (CompleteAction_<Token, Res,
  CompleteActionHeap<Token, Res>>));

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
  ErrorString	m_message;
  double	m_value;
  double	m_total;
};

template <typename Token>
ZuDerive(ProgressActionHeap,
  (ZmHeap<"Zmcp.HTTP.Progress", ProgressAction_<Token>>));

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
  ErrorString	m_level;
  ErrorString	m_data;
  ErrorString	m_logger;
};

template <typename Token>
ZuDerive(LogActionHeap, (ZmHeap<"Zmcp.HTTP.Log", LogAction_<Token>>));

template <typename Token>
ZuDerive(LogAction, (LogAction_<Token, LogActionHeap<Token>>));

struct WorkEntry {
  using CloseFn = void (*)(void *);
  using ReleaseFn = void (*)(void *);

  uint64_t	generation = 0;
  void		*object = nullptr;
  CloseFn	closeFn = nullptr;
  ReleaseFn	releaseFn = nullptr;

  WorkEntry() = default;
  WorkEntry(const WorkEntry &) = delete;
  WorkEntry &operator =(const WorkEntry &) = delete;
  WorkEntry(WorkEntry &&entry) :
    generation{entry.generation}, object{entry.object},
    closeFn{entry.closeFn}, releaseFn{entry.releaseFn} {
    entry.object = nullptr;
  }
  WorkEntry &operator =(WorkEntry &&entry) {
    if (this == &entry) return *this;
    release_();
    generation = entry.generation;
    object = entry.object;
    closeFn = entry.closeFn;
    releaseFn = entry.releaseFn;
    entry.object = nullptr;
    return *this;
  }

  template <typename Work>
  WorkEntry(uint64_t generation_, ZmRef<Work> work) :
    generation{generation_}, object{ZuMv(work).release()},
    closeFn{[](void *object_) {
      static_cast<Work *>(object_)->close_();
    }},
    releaseFn{[](void *object_) {
      auto work_ = static_cast<Work *>(object_);
      if (work_->deref()) delete work_;
    }} { }

  ~WorkEntry() { release_(); }

  void close() { if (object) closeFn(object); }
  void release_() {
    if (!object) return;
    auto object_ = object;
    object = nullptr;
    releaseFn(object_);
  }
};

inline uint64_t WorkEntry_KeyAxor(const WorkEntry &entry) {
  return entry.generation;
}

ZmHashDerive(WorkHash, WorkEntry,
  (ZmHashNode<WorkEntry,
    ZmHashKey<WorkEntry_KeyAxor,
      ZmHashLock<ZmNoLock,
        ZmHashHeapID<"Zmcp.HTTP.Works">>>>));

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

struct PendingEntry {
  using CancelFn = bool (*)(void *, const ID &, ZuCSpan);
  using CloseFn = void (*)(void *);

  ID		id;
  void		*object = nullptr;
  CancelFn	cancelFn = nullptr;
  CloseFn	closeFn = nullptr;

  template <typename Work>
  PendingEntry(ID id_, Work *work) :
    id{ZuMv(id_)}, object{work},
    cancelFn{[](void *object_, const ID &id_, ZuCSpan reason) {
      return static_cast<Work *>(object_)->cancel_(id_, reason);
    }},
    closeFn{[](void *object_) {
      static_cast<Work *>(object_)->close_();
    }} { }

  bool cancel(ZuCSpan reason) {
    return object && cancelFn(object, id, reason);
  }
  void close() { if (object) closeFn(object); }
};

inline const ID &PendingEntry_KeyAxor(const PendingEntry &entry) {
  return entry.id;
}

ZmHashDerive(PendingHash, PendingEntry,
  (ZmHashNode<PendingEntry,
    ZmHashKey<PendingEntry_KeyAxor,
      ZmHashLock<ZmNoLock,
        ZmHashHeapID<"Zmcp.Server.Pending">>>>));

template <typename Catalog, typename Heap = ZuVoid>
class HTTPSession_ : public Heap, public ZuObject {
public:
  HTTPSession_(HTTPValue id_, Limits limits, ContextRef context_) :
    id{ZuMv(id_)}, peer{limits}, context{ZuMv(context_)} { }

  bool beginClose() {
    if (closing) return false;
    closing = true;
    peer.close();
    stream.close();
    return true;
  }

  bool close(unsigned limit) {
    beginClose();
    unsigned visited = 0;
    while (visited < limit) {
      PendingHash::NodeMvRef entry;
      {
	auto i = pending.iter();
	if (!i()) return true;
	entry = i.del();
      }
      ++visited;
      entry->close();
    }
    return !pending.count_();
  }

  HTTPValue		id;
  Peer<Catalog>		peer;
  HTTPStreamRef		stream;
  PendingHash		pending;
  ContextRef		context;
  ZmScheduler::Timer	timer;
  bool			closing = false;
};

template <typename Catalog>
ZuDerive(HTTPSessionHeap, (ZmHeap<"Zmcp.HTTP.Session", HTTPSession_<Catalog>>));

template <typename Catalog>
ZuDerive(HTTPSession,
  (HTTPSession_<Catalog, HTTPSessionHeap<Catalog>>));

template <typename Catalog>
inline const HTTPValue &HTTPSession_KeyAxor(
    const ZuRef<HTTPSession<Catalog>> &session) {
  return session->id;
}

template <typename Catalog>
using HTTPSessionHash_ = ZmHash<ZuRef<HTTPSession<Catalog>>,
  ZmHashNode<ZuRef<HTTPSession<Catalog>>,
    ZmHashKey<HTTPSession_KeyAxor<Catalog>,
      ZmHashLock<ZmNoLock,
        ZmHashHeapID<"Zmcp.HTTP.Sessions">>>>>;

template <typename Catalog>
struct HTTPSessionHash : public HTTPSessionHash_<Catalog> { };

using SessionEntropy =
  ZtArray<uint8_t, ZtArrayHeapID<"Zmcp.HTTP.SessionID">>;

ZmListDerive(SSEQueue, SSERecord,
  ZmListNode<SSERecord, ZmListHeapID<"Zmcp.SSE.Queue">>);

using ResumeFn = ZmFn<bool(), ZmFnHeapID<"Zmcp.SSE.Resume">>;
using SSECloseFn = ZmFn<void(), ZmFnHeapID<"Zmcp.SSE.Close">>;

struct WriteProbe {
  template <typename Out>
  Zhttp::WriteOutcome::T operator ()(Out &) const {
    return Zhttp::WriteOutcome::End;
  }
};

struct SSEProducerRef {
  void *ptr = nullptr;
  void (*invalidateFn)(void *) = nullptr;

  template <typename P> SSEProducerRef &operator =(P *p) {
    ptr = p;
    invalidateFn = [](void *ptr_) { static_cast<P *>(ptr_)->invalidate(); };
    return *this;
  }
  explicit operator bool() const { return ptr; }
  void invalidate() { if (ptr) invalidateFn(ptr); }
  void clear() { ptr = nullptr; invalidateFn = nullptr; }
};

namespace SSEState {
  enum { Open, TerminalQueued, Failed, Closed };
}

template <typename Owner, typename Emit, typename Heap = ZuVoid>
class SSEProducer_ : public Heap, public ZmPolymorph {
public:
  SSEProducer_(Owner *owner_, Emit emit_) :
    m_owner{owner_}, m_emit{ZuMv(emit_)} { }

  bool resume() {
    auto owner = m_owner;
    if (!owner) return false;
    return m_emit([owner](auto &out) { return owner->write_(out); });
  }
  void invalidate() { m_owner = nullptr; }

private:
  Owner	*m_owner;
  Emit	m_emit;
};

template <typename Owner, typename Emit>
ZuDerive(SSEProducerHeap,
  (ZmHeap<"Zmcp.SSE.Producer", SSEProducer_<Owner, Emit>>));

template <typename Owner, typename Emit>
ZuDerive(SSEProducer, (SSEProducer_<Owner, Emit,
  SSEProducerHeap<Owner, Emit>>));

} // Server_

template <typename Catalog>
class HTTPResponseBuilder : public Zhttp::ResBuilder {
public:
  using Headers = ZuTypeConcat<
    ZhttpHeaders("content-type", "content-length"), SessionHeader>;
  using ContentType = ZuStringT<"content-type">;
  using ContentLength = ZuStringT<"content-length">;
  using Session = SessionID;
  using Messages = typename Server_::ResponseMessages<Catalog>::T;
  using Message = ZuTypeApply<ZuUnion,
    typename Messages::template Unshift<void>>;
  using Zhttp::ResBuilder::header;

  template <typename M>
  void init(M message_) {
    ZuAssert((ZuTypeIn<M, Messages>{}));
    new (m_message.template new_<M, true>()) M{ZuMv(message_)};
    m_status = 200;
  }

  void accepted() {
    m_message.null();
    m_status = 202;
  }
  void forbidden() {
    m_message.null();
    m_status = 403;
  }
  void missing() {
    m_message.null();
    m_status = 404;
  }

  ZuCSpan sessionID() const { return m_sessionID; }
  void sessionID(ZuCSpan id) { m_sessionID = id; }
  void maxBodyBytes(uint64_t bytes) { m_maxBodyBytes = bytes; }

  Zhttp::BodyPolicy::T bodyPolicy() const {
    return m_message.type() ?
      Zhttp::BodyPolicy::Fixed : Zhttp::BodyPolicy::None;
  }
  Zhttp::Method::T method() const { return Zhttp::Method::POST; }
  unsigned status() const { return m_status; }
  bool disconnect() const { return m_disconnect; }

  template <typename Key, typename L>
  void header(L &&l) const {
    if constexpr (ZuIsSame<Key, ContentType>{}) {
      if (m_message.type()) l("application/json");
    } else if constexpr (ZuIsSame<Key, ContentLength>{}) {
      l(m_message.type() ?
	Zhttp::contentLengthPad() : ZuCSpan{"0"});
    } else if constexpr (ZuIsSame<Key, Session>{}) {
      if (m_sessionID) l(m_sessionID);
    }
  }
  template <typename L> void header(L &&) const { }

  template <typename Emit>
  void body(Emit &&emit) const {
    if (!m_message.type()) return;
    emit([this](auto &s) {
      try {
	HTTPOutput out{s, m_maxBodyBytes};
	m_message.cdispatch([&out](auto, const auto &value) {
	  value.write(out);
	});
	out.flush();
	m_bodyLength = s.produced();
	return out ? Zhttp::WriteOutcome::End : Zhttp::WriteOutcome::Abort;
      } catch (...) {
	return Zhttp::WriteOutcome::Abort;
      }
    });
  }

  template <typename L>
  void bodyHdrs(L &&l) const {
    if (!m_message.type()) return;
    Zhttp::contentLengthSet(l, m_bodyLength);
  }

  void close() { m_message.null(); }

private:
  Message	m_message;
  HTTPValue	m_sessionID;
  mutable uint64_t m_bodyLength = 0;
  uint64_t	m_maxBodyBytes = Default::MaxJSONBytes;
  unsigned	m_status = 202;
  bool		m_disconnect = false;
};

template <typename Catalog>
class HTTPSSEBuilder : public Zhttp::ResBuilder {
public:
  using Headers = ZuTypeConcat<ZhttpHeaders(
    ("content-type", "text/event-stream"),
    ("cache-control", "no-cache")), SessionHeader>;
  using ContentType = ZuStringT<"content-type">;
  using CacheControl = ZuStringT<"cache-control">;
  using Session = SessionID;
  using Messages = typename Server_::ResponseMessages<Catalog>::T;
  using Zhttp::ResBuilder::header;

  HTTPSSEBuilder() = default;
  explicit HTTPSSEBuilder(Limits limits_) : m_limits{limits_} { }

  template <typename Owner,
    typename = decltype(ZuDeclVal<Owner * &>()->postSSE_(),
	ZuDeclVal<Owner * &>()->discardSSE_(Server_::SSEQueue{}), void())>
  void owner(Owner *owner_, int) {
    m_owner = owner_;
    m_postFn = [](void *owner__) {
      return static_cast<Owner *>(owner__)->postSSE_();
    };
    m_discardFn = [](void *owner__, Server_::SSEQueue queue) {
      return static_cast<Owner *>(owner__)->discardSSE_(ZuMv(queue));
    };
  }
  template <typename Owner>
  void owner(Owner *, ...) { }

  void disown() {
    m_owner = nullptr;
    m_postFn = nullptr;
    m_discardFn = nullptr;
  }

  int state() const { return m_state; }
  unsigned queued() const { return m_queued; }
  uint64_t queuedBytes() const { return m_queuedBytes; }
  ZuCSpan sessionID() const { return m_sessionID; }
  void sessionID(ZuCSpan id) { m_sessionID = id; }

  Zhttp::BodyPolicy::T bodyPolicy() const {
    return Zhttp::BodyPolicy::Stream;
  }
  Zhttp::Method::T method() const { return Zhttp::Method::POST; }
  unsigned status() const { return 200; }
  bool disconnect() const { return m_state == Server_::SSEState::Failed; }

  template <typename Key, typename L>
  void header(L &&l) const {
    if constexpr (ZuIsSame<Key, ContentType>{})
      l("text/event-stream");
    else if constexpr (ZuIsSame<Key, CacheControl>{})
      l("no-cache");
    else if constexpr (ZuIsSame<Key, Session>{}) {
      if (m_sessionID) l(m_sessionID);
    }
  }
  template <typename L> void header(L &&) const { }

  template <typename M>
  bool emit(M message) {
    ZuAssert((ZuTypeIn<M, Messages>{}));
    enum { Terminal =
      !ZuIsSame<M, ProgressMessage>{} && !ZuIsSame<M, LogMessage>{} };
    return enqueue_(ZuMv(message), Terminal);
  }

  template <typename Emit>
  void body(Emit &&emit_) {
    using Emit_ = ZuDecay<Emit>;
    using Producer = Server_::SSEProducer<HTTPSSEBuilder, Emit_>;
    ZmRef<Producer> producer = new Producer{this, ZuFwd<Emit>(emit_)};
    m_producer = producer.ptr();
    m_resume = Server_::ResumeFn::fn(
      ZuMv(producer), [](Producer *producer_) {
	return producer_->resume();
      });
    if (!m_owner) {
      do {
	auto resume = m_resume;
	if (!resume || !resume()) {
	  fail_();
	  break;
	}
      } while (m_queued && m_state != Server_::SSEState::Closed);
      return;
    }
    {
      auto resume = m_resume;
      if (!resume || !resume()) {
	fail_();
	return;
      }
    }
    if (m_queued && m_state != Server_::SSEState::Closed) post_();
  }

  void resume_() {
    m_posted = false;
    if (!m_resume || m_state == Server_::SSEState::Closed) return;
    unsigned visited = 0;
    do {
      auto resume = m_resume;
      if (!resume || !resume()) {
	fail_();
	return;
      }
      ++visited;
    } while (visited < m_limits.workBatch && m_queued &&
	m_state != Server_::SSEState::Closed);
    if (m_queued && m_state != Server_::SSEState::Closed) post_();
  }

  void close() {
    m_producer.invalidate();
    m_producer.clear();
    m_resume = {};
    Server_::SSEQueue queue{ZuMv(m_queue)};
    m_queued = 0;
    m_queuedBytes = 0;
    m_state = Server_::SSEState::Closed;
    bool discarded = m_owner && m_discardFn &&
      m_discardFn(m_owner, ZuMv(queue));
    disown();
    if (!discarded)
      while (queue.shift()) { }
  }

  template <typename Out>
  Zhttp::WriteOutcome::T write_(Out &out) {
    if (m_state == Server_::SSEState::Failed)
      return Zhttp::WriteOutcome::Abort;
    auto record = m_queue.shift();
    if (!record) {
      return Zhttp::WriteOutcome::Stream;
    }
    --m_queued;
    m_queuedBytes -= record->buf->length;
    out << ZuCSpan{*record->buf};
    return record->terminal ?
      Zhttp::WriteOutcome::End : Zhttp::WriteOutcome::Stream;
  }

private:
  template <typename M>
  bool enqueue_(M message, bool terminal) {
    if (m_state != Server_::SSEState::Open) return false;
    if (m_queued >= m_limits.maxQueue) {
      abort_();
      return false;
    }
    ZmRef<ZiIOBuf> buf = new Server_::SSEBuf{};
    *buf << "id: " << m_sequence++ << "\ndata: ";
    unsigned jsonOffset = buf->length;
    unsigned jsonMax = m_limits.maxJSONBytes < m_limits.maxSSEEventBytes ?
      m_limits.maxJSONBytes : m_limits.maxSSEEventBytes;
    uint64_t outputMax = uint64_t(jsonOffset) + jsonMax;
    if (ZuUnlikely(outputMax > UINT_MAX)) {
      abort_();
      return false;
    }
    StdioOutput out{*buf, outputMax};
    message.write(out);
    if (ZuUnlikely(!out)) {
      abort_();
      return false;
    }
    unsigned jsonLength = buf->length - jsonOffset;
    *buf << "\n\n";
    if (ZuUnlikely(buf->failed() || jsonLength > jsonMax ||
	m_queuedBytes > m_limits.maxQueueBytes ||
	buf->length > m_limits.maxQueueBytes - m_queuedBytes)) {
      abort_();
      return false;
    }
    m_queuedBytes += buf->length;
    ++m_queued;
    m_queue.push(Server_::SSERecord{ZuMv(buf), terminal});
    if (terminal) m_state = Server_::SSEState::TerminalQueued;
    if (m_resume &&
	(m_owner ? !post_() : !m_resume())) {
      fail_();
      return false;
    }
    return true;
  }

  void fail_() {
    if (m_state != Server_::SSEState::Closed)
      m_state = Server_::SSEState::Failed;
  }

  void abort_() {
    fail_();
    if (m_resume) {
      if (m_owner)
	(void)post_();
      else
	(void)m_resume();
    }
  }

  bool post_() {
    if (m_posted) return true;
    if (!m_owner || !m_postFn) return false;
    m_posted = true;
    if (m_postFn(m_owner)) return true;
    m_posted = false;
    return false;
  }

  Limits		m_limits;
  HTTPValue		m_sessionID;
  Server_::SSEQueue	m_queue;
  Server_::ResumeFn	m_resume;
  void			*m_owner = nullptr;
  bool			(*m_postFn)(void *) = nullptr;
  bool			(*m_discardFn)(void *, Server_::SSEQueue) = nullptr;
  Server_::SSEProducerRef m_producer;
  uint64_t		m_sequence = 1;
  uint64_t		m_queuedBytes = 0;
  unsigned		m_queued = 0;
  int			m_state = Server_::SSEState::Open;
  bool			m_posted = false;
};

template <typename Catalog>
class HTTPBuilder : public ZmObject, public Zhttp::ResBuilder {
public:
  using Headers = ZuTypeConcat<ZhttpHeaders(
    "content-type", "content-length", "cache-control"), SessionHeader>;
  using HdrCatalog = HTTPHdrCatalog<Headers>;
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
  Fixed *fixed(M message, uint64_t maxBodyBytes = Default::MaxJSONBytes) {
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

  Stream *stream(Limits limits = {}) {
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
      ZuDeclVal<Emit &>()(Server_::WriteProbe{}));
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
class HTTPResponder : public CompletionSet<HTTPResponder<Catalog, Impl>> {
  using Base = CompletionSet<HTTPResponder<Catalog, Impl>>;

public:
  using BuilderQ = HTTPBuilderQ<Catalog>;
  using Builder = typename BuilderQ::Node;

  HTTPResponder(Impl *impl_, Limits limits_) :
    Base{limits_.maxPending}, m_impl{impl_}, m_limits{limits_} { }

  bool sent() const { return m_sent; }
  bool streaming() const { return m_streaming; }
  bool terminal() const { return m_terminal; }
  Builder *builder() const { return m_builder.ptr(); }

  void sessionID(ZuCSpan id) { m_sessionID = id; }
  void era(int era_) { m_era = era_; }

  template <typename Req, typename Token>
  void made(Req *req, Token *token) {
    m_impl->made(req, token);
    if (m_builder || m_terminal) return;
    builder_();
    if constexpr (Req::ResponseBody == BodyPolicy::SSE) {
      auto stream = m_builder->data().stream(m_limits);
      stream->owner(m_impl, 0);
      stream->sessionID(m_sessionID);
      m_streaming = true;
      send_();
    }
  }

  bool cancel(const ID &id, ZuCSpan reason = {}) {
    return m_impl->cancel(id, reason);
  }

  bool cancelTx_(const ID &id, ZuCSpan reason = {}) {
    return Base::cancel(id, reason);
  }

  template <typename Token, typename Res>
  bool completion(Token *token, ToolReply<Res> reply) {
    return m_impl->completion(
      this, token, ZuMv(reply));
  }

  template <typename Token>
  bool progression(
      Token *token, double value, double total, ZuCSpan message) {
    return m_impl->progression(
      this, token, value, total, message);
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
    if (m_terminal) return;
    if (m_streaming) {
      auto stream = m_builder->data().streamPtr();
      if (!stream || !stream->emit(
          ToolReplyMessage<Res>{id, ZuMv(reply), m_era})) {
	fail_();
	return;
      }
    } else {
      builder_();
      auto fixed = m_builder->data().fixed(
        ToolReplyMessage<Res>{id, ZuMv(reply), m_era},
	m_limits.maxJSONBytes);
      fixed->sessionID(m_sessionID);
      send_();
    }
    m_terminal = true;
  }

  template <typename Req, typename Token>
  void cancelled(Req *req, Token *token, ZuCSpan reason) {
    m_impl->cancelled(req, token, reason);
  }

  template <typename Req, typename Token>
  bool progress(
      Req *, Token *token,
      double value, double total, ZuCSpan message) {
    if (!m_streaming || m_terminal) return false;
    auto stream = m_builder->data().streamPtr();
    if (!stream || !stream->emit(ProgressMessage{
        token->progressToken(), message, value, total})) {
      fail_();
      return false;
    }
    return true;
  }

  template <typename Req, typename Token>
  bool log(
      Req *, Token *, ZuCSpan level, ZuCSpan data, ZuCSpan logger) {
    if (!m_streaming || m_terminal) return false;
    auto stream = m_builder->data().streamPtr();
    if (!stream || !stream->emit(LogMessage{level, data, logger})) {
      fail_();
      return false;
    }
    return true;
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
    builder_();
    auto fixed = m_builder->data().fixed(
      ZuMv(message), m_limits.maxJSONBytes);
    fixed->sessionID(m_sessionID);
    send_();
    m_terminal = true;
  }

  void finish() {
    if (m_terminal || Base::count()) return;
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
    Base::drain();
    releaseBuilder_(true);
    m_terminal = true;
  }

  void detach() {
    releaseBuilder_(true);
    m_terminal = true;
  }

  void closed() {
    releaseBuilder_(false);
    m_terminal = true;
  }

private:
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
  Limits	m_limits;
  ZmRef<Builder> m_builder;
  Builder	*m_sentBuilder = nullptr;
  HTTPValue	m_sessionID;
  int		m_era = Era::Modern;
  bool		m_streaming = false;
  bool		m_terminal = false;
  bool		m_sent = false;
};

template <typename Derived, typename Impl, typename Catalog>
class Server {
  friend Derived;
  Derived *derived() { return static_cast<Derived *>(this); }
  const Derived *derived() const {
    return static_cast<const Derived *>(this);
  }
  template <typename Link, typename Heap> class Work_;
  template <typename Heap> class StdioWork_;

  template <typename Link>
  using WorkDefault = Work_<Link, ZuVoid>;
  using StdioWorkDefault = StdioWork_<ZuVoid>;

  template <typename Link>
  ZuDerive(WorkHeap, (ZmHeap<"Zmcp.HTTP.Work", WorkDefault<Link>>));

  template <typename Link>
  ZuDerive(Work, (Work_<Link, WorkHeap<Link>>));

  ZuDerive(StdioWorkHeap, (ZmHeap<"Zmcp.Stdio.Work", StdioWorkDefault>));

  ZuDerive(StdioWork, (StdioWork_<StdioWorkHeap>));

  using Session = Server_::HTTPSession<Catalog>;

public:
  using ResBuilderQ = HTTPBuilderQ<Catalog>;
  using HTTP = Zhttp::Server<Server>;
  using AppHeaderList = AppHeaders<Impl>;
  using Headers = HTTPHeaders<AppHeaderList>;

  class Parser : public HTTPParser<Parser> {
    using Base = HTTPParser<Parser>;
    using BaseKeys = ZuTypeSlice<2, 0, typename Base::Headers>;

  public:
    using AppKeys = ZuTypeSlice<2, 0, AppHeaderList>;
    using HeaderKeys = ZuTypeConcat<BaseKeys, AppKeys>;
    using Headers = ZuTypeConcat<typename Base::Headers, AppHeaderList>;
    using HdrCatalog = HTTPHdrCatalog<Headers>;
    ZuAssert(ZuTypeUnique<HeaderKeys>::N == HeaderKeys::N,
      "Zmcp application header duplicates a protocol header");

    void init(Server &server) { m_server = &server; }

    ZuCSpan endpoint() const { return m_server->endpoint(); }
    const Limits &limits() const { return m_server->limits(); }
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
    Server *m_server = nullptr;
    HTTPHeaders<AppHeaderList> m_headers;
  };

protected:
  Server() = default;

  bool init(
      const Zhttp::HubConfig &hub, ServerConfig config, Impl *impl) {
    m_impl = impl;
    m_mx = hub.mx();
    m_txThread = hub.txThread() ?
      m_mx->sid(hub.txThread()) : m_mx->txThread();
    m_limits = config.limits();
    derived()->m_endpoint = config.endpoint();
    derived()->m_absentOrigin = config.absentOrigin();
    derived()->m_legacySessions = config.legacySessions();
    derived()->m_legacyLifetime = config.legacyLifetime();
    derived()->m_httpDone = false;
    if (!valid(m_limits)) return false;
    if (derived()->m_legacySessions && !derived()->m_rng.init()) return false;
    {
      uint64_t messageMax = uint64_t(m_limits.maxSSEEventBytes) +
	Default::SSEFrameOverhead;
      if (messageMax < m_limits.maxJSONBytes)
	messageMax = m_limits.maxJSONBytes;
      config.retainedBodyMax(m_limits.maxJSONBytes)
	.retainedMessageMax(messageMax);
    }
    m_up = true;
    if (derived()->m_http.init(hub, ZuMv(config), this)) {
      return true;
    }
    m_up = false;
    m_impl = nullptr;
    m_mx = nullptr;
    return false;
  }

  bool init(
      ZiMultiplex *mx, StdioConfig config, Impl *impl) {
    m_impl = impl;
    m_mx = mx;
    m_txThread = mx->txThread();
    m_limits = config.limits();
    if (!valid(m_limits)) goto invalid;
    derived()->m_stdio = new IOLink<Server>{
      this, m_mx, m_txThread, ZuMv(config)};
    derived()->m_stdioPeer = Peer<Catalog>{m_limits};
    derived()->m_stdioStarted = false;
    derived()->m_stdioClosing = false;
    derived()->m_stdioDone = false;
    derived()->m_stdioFailure = false;
    m_up = true;
    return true;

  invalid:
    m_impl = nullptr;
    m_mx = nullptr;
    m_txThread = 0;
    return false;
  }

public:
  ZuCSpan endpoint() const { return derived()->m_endpoint; }
  const Limits &limits() const { return m_limits; }

  bool origin(ZuCSpan origin_) const {
    if (!origin_) return derived()->m_absentOrigin;
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
    ZmRef<Work<Link>> work = new Work<Link>{
      this, link, ZuMv(body), ZuMv(meta), ZuMv(headers)};
    txRun([work = ZuMv(work)]() mutable { work->run_(); });
  }

  template <typename Link>
  void deleteHTTP(Link *link, const HTTPMeta &meta) {
    if (!link) return;
    HTTPValue sessionID = meta.sessionID;
    txRun([this, link = ZmRef(link),
	sessionID = ZuMv(sessionID)]() mutable {
      if (sessionID) {
	auto node = derived()->m_sessions.del(sessionID);
	if (node) {
	  ZuRef<Session> session = ZuMv(node->data());
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
    txRun([link = ZmRef(link)]() mutable { link->disconnect(); });
  }

  bool expireSession(ZuCSpan id) {
    if (!m_mx || !id) return false;
    HTTPValue owned{id};
    return txRun([this, owned = ZuMv(owned)]() mutable {
      auto entry = derived()->m_sessions.findPtr(owned);
      if (!entry) return;
      auto node = derived()->m_sessions.delNode(entry);
      ZuRef<Session> session = ZuMv(node->data());
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
      if (derived()->m_transportContexts.findPtr(session.id)) return;
      try {
	derived()->m_transportContexts.add(Server_::TransportContextEntry{
	  session.id, Server_::openContext(
	    m_impl, TransportTag{}, 0, session)});
      } catch (...) {
      }
    }, m_txThread);
  }

  void disconnected(Zhttp::Session session) {
    if (!m_mx || !session) return;
    m_mx->run([this, session]() {
      (void)derived()->m_transportContexts.del(session.id);
    }, m_txThread);
  }

  bool stdioFrame(ZmRef<ZiIOBuf> body) {
    ZiAssert(m_mx && m_mx->invoked(m_txThread), "Zmcp", (),
	"stdio frame outside owner shard", return false);
    if (!m_up.load_() || !body) return false;
    ZmRef<StdioWork> work = new StdioWork{this, ZuMv(body)};
    work->run_();
    return true;
  }

  void stdioClosed() { stdioDone_(false); }
  void stdioFailed() { stdioDone_(true); }

  template <typename L>
  bool txRun(L &&l) {
    if (!m_mx || !m_up.load_()) return false;
    if (m_mx->invoked(m_txThread))
      l();
    else
      m_mx->run(ZuFwd<L>(l), m_txThread);
    return true;
  }

  void closeWorks() {
    if (!m_mx) return;
    ZmBlock<>{}([this](auto wake) {
      m_mx->run([this, wake = ZuMv(wake)]() mutable {
	closeWorks_([this, wake = ZuMv(wake)]() mutable {
	  closeSSE_(ZuMv(wake));
	});
      }, m_txThread);
    });
  }

private:
  Session *session_(ZuCSpan id) {
    auto node = derived()->m_sessions.findPtr(id);
    return node ? node->data().ptr() : nullptr;
  }

  Session *newSession_() {
    if (derived()->m_sessions.count_() >= m_limits.maxSessions) return nullptr;
    Server_::SessionEntropy entropy;
    entropy.length(Default::SessionIDBytes, false);
    if (!derived()->m_rng.random(entropy)) return nullptr;
    HTTPValue id;
    id << ZtQuote::Hex{entropy};
    if (derived()->m_sessions.findPtr(id)) return nullptr;
    auto context = Server_::openContext(
      m_impl, SessionTag{}, 0, false, ZuCSpan{id});
    ZuRef<Session> session = new Session{
      ZuMv(id), m_limits, ZuMv(context)};
    auto ptr = session.ptr();
    derived()->m_sessions.add(ZuMv(session));
    if (derived()->m_legacyLifetime)
      m_mx->add(&ptr->timer, Zm::now(int(derived()->m_legacyLifetime)),
	ZmScheduler::Update,
	[this, session = ptr](auto &&arm) {
	  return arm([this, session]() {
	    expireSession_(session);
	  });
	}, m_txThread);
    return ptr;
  }

  void expireSession_(Session *session) {
    auto entry = derived()->m_sessions.findPtr(session->id);
    if (!entry || entry->data().ptr() != session) return;
    auto node = derived()->m_sessions.delNode(entry);
    ZuRef<Session> owned = ZuMv(node->data());
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
	closeSessions_([this, wake = ZuMv(wake)]() mutable {
	  closeWorks_([this, wake = ZuMv(wake)]() mutable {
	    closeSSE_([this, wake = ZuMv(wake)]() mutable {
	      closeTransportContexts_(ZuMv(wake));
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
    ZuRef<Session> session = ZuMv(node->data());
    closeSession_(ZuMv(session), [
      this, done = ZuMv(done), visited]() mutable {
      closeSessions_(ZuMv(done), visited + 1);
    });
  }

  typename Server_::HTTPSessionHash<Catalog>::NodeMvRef takeSession_() {
    auto i = derived()->m_sessions.iter();
    if (!i()) return decltype(i.del()){};
    return i.del();
  }

  bool stopStdio_() {
    if (!m_mx) return false;
    if (derived()->m_stdioDone.load_()) return !derived()->m_stdioFailure;
    return ZmBlock<bool>{}([this](auto wake) mutable {
      m_mx->run([this, wake = ZuMv(wake)]() mutable {
	derived()->m_stdioStopFn = ZuMv(wake);
	m_up = false;
	derived()->m_stdioClosing = true;
	if (derived()->m_stdioStarted)
	  derived()->m_stdio->stop_();
	else
	  stdioDone_(false);
      }, m_txThread);
    });
  }

  template <typename M>
  bool sendStdio_(M message) {
    ZiAssert(m_mx && m_mx->invoked(m_txThread), "Zmcp", (),
	"stdio send outside owner shard", return false);
    return m_up.load_() && derived()->m_stdio &&
      derived()->m_stdio->send_(message);
  }

  void failStdio_() {
    ZiAssert(m_mx && m_mx->invoked(m_txThread), "Zmcp", (),
	"stdio failure outside owner shard", return);
    if (derived()->m_stdioDone.load_() || derived()->m_stdioClosing) return;
    derived()->m_stdioFailure = true;
    derived()->m_stdioClosing = true;
    m_up = false;
    derived()->m_stdio->stop_();
  }

  void stdioDone_(bool failed) {
    ZiAssert(m_mx && m_mx->invoked(m_txThread), "Zmcp", (),
	"stdio completion outside owner shard", return);
    if (derived()->m_stdioDone.load_()) return;
    derived()->m_stdioFailure |= failed;
    derived()->m_stdioClosing = true;
    m_up = false;
    derived()->m_stdioPeer.close();
    closeWorks_([this]() mutable {
      derived()->m_stdioContext.close();
      derived()->m_stdioDone = true;
      try {
	if (derived()->m_stdioFailure)
	  failed_(m_impl, 0);
	else
	  closed_(m_impl, 0);
      } catch (...) {
      }
      auto stopFn = ZuMv(derived()->m_stdioStopFn);
      derived()->m_stdioStopFn = {};
      if (stopFn) stopFn(!derived()->m_stdioFailure);
    });
  }

  bool up_() const { return m_up.load_(); }
  unsigned maxJSONBytes_() const { return m_limits.maxJSONBytes; }
  bool legacySessions_() const { return derived()->m_legacySessions; }
  bool txInvoked_() const {
    return m_mx && m_mx->invoked(m_txThread);
  }
  bool assertTx_() const {
    ZiAssert(txInvoked_(), "Zmcp", (),
      "operation outside owner shard", return false);
    return true;
  }
  Server_::ContextRef openStreamContext_(const Context &context) {
    return Server_::openContext(m_impl, StreamTag{}, 0, context);
  }
  template <typename Req, typename Request, typename Completion>
  void tool_(Req *req, const Request &request,
      const Headers &headers, const Context &context, Completion completion) {
    m_impl->tool(req, request, headers, context, ZuMv(completion));
  }
  template <typename Req, typename Token>
  void cancelled_(Req *req, Token *token, ZuCSpan reason) {
    m_impl->cancelled(req, token, reason);
  }
  template <typename L>
  bool postSSE_(L &&l) {
    if (!m_mx || !m_up.load_()) return false;
    m_mx->run(ZuFwd<L>(l), m_txThread);
    return true;
  }
  void *stdioContext_() const { return derived()->m_stdioContext.ptr(); }
  Era::T stdioEra_() const { return derived()->m_stdioPeer.era(); }
  template <typename Responder_, typename Emit, typename Tool>
  bool stdioDispatch_(ZuSpan<char> span,
      Responder_ &responder, Emit &&emit, Tool &&tool) {
    return derived()->m_stdioPeer.dispatchAsync(
      span, responder, ZuFwd<Emit>(emit), ZuFwd<Tool>(tool));
  }
  template <typename WorkT>
  void stdioPendingAdd_(const ID &id, WorkT *work) {
    derived()->m_stdioPending.add(Server_::PendingEntry{id, work});
  }
  void stdioPendingDel_(const ID &id, const void *work) {
    auto entry = derived()->m_stdioPending.findPtr(id);
    if (entry && entry->object == work)
      (void)derived()->m_stdioPending.delNode(entry);
  }

  template <typename Link, typename Heap = ZuVoid>
  class Work_ : public Heap, public ZmObject {
    using Self = Work_<Link, Heap>;
    using Responder = HTTPResponder<Catalog, Self>;

  public:
    Work_(
        Server *server_, Link *link_, ZmRef<ZiIOBuf> body_,
        HTTPMeta meta_, Headers headers_) :
      m_server{server_}, m_link{link_}, m_body{ZuMv(body_)},
      m_meta{ZuMv(meta_)}, m_headers{ZuMv(headers_)},
      m_responder{this, server_->limits()},
      m_peer{server_->limits()} { }

    void run_() {
      try {
	run__();
      } catch (...) {
	failed();
      }
    }

  private:
    void run__() {
      if (!m_server->register_(m_generation, ZmRef(this))) {
	m_link->disconnect();
	return;
      }
      auto span = ZuSpan<char>{m_body->span()};
      auto parsed = parse<Catalog>(span, m_server->maxJSONBytes_());
      if (!parsed) {
	failed();
	return;
      }
      Peer<Catalog> *peer = &m_peer;
      if (m_server->legacySessions_()) {
	if (m_meta.sessionID) {
	  m_session = m_server->session_(m_meta.sessionID);
	  if (!m_session) {
	    m_responder.missing();
	    return;
	  }
	} else if (parsed.envelope.kind == MessageKind::Request &&
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
	  m_session->stream.replace(ZmRef(this));
	}
      } else if (m_meta.version == LegacyVersion{}()) {
	m_peer.statelessLegacy();
      }
      m_responder.era(
	peer->era() == Era::Legacy ?
	  Era::Legacy : Era::Modern);
      m_context = Context{
	m_server->transportContext_(m_link->session()),
	m_session ? m_session->context.ptr() : nullptr};
      m_streamContext = m_server->openStreamContext_(m_context);
      m_context = Context{
	m_context.transport(), m_context.session(), m_streamContext.ptr()};
      auto emit = [this](auto message) {
	m_responder.emit(ZuMv(message));
      };
      auto tool = [this](auto *req, const auto &request, auto completion) {
	m_server->tool_(req, request, m_headers, m_context, ZuMv(completion));
      };
      m_link->responseCancel(Zhttp::StreamCancelFn{
	static_cast<Self *>(this), [](Self *self) {
	  self->responseCancelled_();
	}});
      if (!peer->dispatchAsync(
	  parsed.envelope, m_responder, emit, tool)) {
	failed();
	return;
      }
      m_responder.finish();
      if (m_responder.terminal() && !m_responder.sent()) finish_();
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

    bool discardSSE_(Server_::SSEQueue queue) {
      m_server->discardSSE_(ZuMv(queue));
      return true;
    }

    template <typename Req, typename Token>
    void made(Req *, Token *token) {
      if (!m_session) return;
      m_requestID = token->id();
      m_session->pending.add(Server_::PendingEntry{m_requestID, this});
    }

    bool cancel(const ID &id, ZuCSpan reason) {
      if (!m_session) return m_responder.cancelTx_(id, reason);
      auto entry = m_session->pending.findPtr(id);
      return entry && entry->cancel(reason);
    }

    bool cancel_(const ID &id, ZuCSpan reason) {
      if (!live_() || !m_responder.cancelTx_(id, reason)) return false;
      if (!m_link) {
	ZmRef<Self> self{this};
	m_responder.close();
	m_streamContext.close();
	finish_();
      }
      return true;
    }

    template <typename Token, typename Res>
    bool completion(
        Responder *, Token *token, ToolReply<Res> reply) {
      if (!m_generation || !m_server->up_()) return false;
      if (m_server->txInvoked_())
	return complete_(token, ZuMv(reply));
      using Action = Server_::CompleteAction<Token, Res>;
      ZmRef<Action> action = new Action{token, ZuMv(reply)};
      return m_server->txRun([
	work = ZmRef(this), action = ZuMv(action)]() mutable {
	work->complete_(action->token(), action->reply());
      });
    }

    template <typename Token>
    bool progression(
        Responder *, Token *token,
        double value, double total, ZuCSpan message) {
      if (!m_generation || !m_server->up_()) return false;
      if (m_server->txInvoked_())
	return m_responder.progressTx_(token, value, total, message);
      using Action = Server_::ProgressAction<Token>;
      ZmRef<Action> action = new Action{token, value, total, message};
      return m_server->txRun([
	work = ZmRef(this), action = ZuMv(action)]() mutable {
	if (!work->live_()) return;
	(void)work->m_responder.progressTx_(
	  action->token(), action->value(),
	  action->total(), action->message());
      });
    }

    template <typename Token>
    bool logging(
        Responder *, Token *token,
        ZuCSpan level, ZuCSpan data, ZuCSpan logger) {
      if (!m_generation || !m_server->up_()) return false;
      if (m_server->txInvoked_())
	return m_responder.logTx_(token, level, data, logger);
      using Action = Server_::LogAction<Token>;
      ZmRef<Action> action = new Action{token, level, data, logger};
      return m_server->txRun([
	work = ZmRef(this), action = ZuMv(action)]() mutable {
	if (!work->live_()) return;
	(void)work->m_responder.logTx_(
	  action->token(), action->level(),
	  action->data(), action->logger());
      });
    }

    template <typename Req, typename Token>
    void cancelled(Req *req, Token *token, ZuCSpan reason) {
      m_server->cancelled_(req, token, reason);
    }

    void failed() {
      if (m_closed) return;
      if (m_link) m_link->disconnect();
      finish_();
    }

    void streamClosed_() {
      if (m_closed || !m_link) return;
      if (!m_session) m_responder.cancelAll("stream closed");
      m_responder.detach();
      m_streamContext.close();
      m_link->responseCancel({});
      m_link->disconnect();
      m_link = nullptr;
    }

    void responseCancelled_() {
      if (!m_server->assertTx_()) return;
      if (m_closed || !m_link) return;
      ZmRef<Self> self{this};
      m_link = nullptr;
      if (m_session) {
	m_responder.detach();
	m_streamContext.close();
	return;
      }
      m_responder.cancelAll("stream closed");
      m_responder.close();
      m_streamContext.close();
      finish_();
    }

    void responseClosed_() {
      if (!m_server->assertTx_()) return;
      if (m_closed) return;
      ZmRef<Self> self{this};
      if (!m_session) m_responder.cancelAll("stream closed");
      m_responder.closed();
      m_streamContext.close();
      if (m_link) m_link->responseCancel({});
      m_link = nullptr;
      finish_();
    }

    void close_() {
      if (m_closed) return;
      ZmRef<Self> self{this};
      m_closed = true;
      pendingDone_();
      if (!m_session) m_responder.cancelAll("stream closed");
      sessionDone_();
      m_responder.close();
      m_streamContext.close();
      if (m_link) {
	m_link->responseCancel({});
	m_link->disconnect();
	m_link = nullptr;
      }
      uint64_t generation = m_generation;
      m_generation = 0;
      if (generation) m_server->remove_(generation);
    }

  private:
    template <typename Token, typename Res>
    bool complete_(Token *token, ToolReply<Res> reply) {
      if (!live_()) return false;
      if (!m_link) {
	ZmRef<Self> self{this};
	m_responder.close();
	m_streamContext.close();
	finish_();
	return false;
      }
      ZmRef<Self> self{this};
      pendingDone_();
      bool ok = m_responder.completeTx_(token, ZuMv(reply));
      if (m_responder.terminal() && !m_responder.sent()) finish_();
      return ok;
    }

    bool live_() const {
      return m_generation &&
	m_server->contains_(m_generation, this);
    }

    void finish_() {
      if (!m_generation) return;
      pendingDone_();
      sessionDone_();
      m_streamContext.close();
      ZmRef<Self> self{this};
      uint64_t generation = m_generation;
      m_generation = 0;
      m_server->remove_(generation);
    }

    void sessionDone_() {
      if (!m_session) return;
      m_session->stream.clear(this);
      m_session = nullptr;
    }

    void pendingDone_() {
      if (!m_session || m_requestID.is<void>()) return;
      auto entry = m_session->pending.findPtr(m_requestID);
      if (entry && entry->object == this)
	(void)m_session->pending.delNode(entry);
      m_requestID.null();
    }

    Server		*m_server;
    ZmRef<Link>		m_link;
    ZmRef<ZiIOBuf>	m_body;
    HTTPMeta		m_meta;
    Headers		m_headers;
    Responder		m_responder;
    Peer<Catalog>	m_peer;
    Context		m_context;
    Server_::ContextRef	m_streamContext;
    Session		*m_session = nullptr;
    ID			m_requestID;
    uint64_t		m_generation = 0;
    bool			m_closed = false;
  };

  template <typename Heap = ZuVoid>
  class StdioWork_ : public Heap, public ZmObject {
    using Self = StdioWork_<Heap>;
    using Responder = StdioResponder<Self>;

  public:
    StdioWork_(Server *server_, ZmRef<ZiIOBuf> body_) :
      m_server{server_}, m_body{ZuMv(body_)},
      m_responder{this, server_->limits()} { }

    void run_() {
      try {
	run__();
      } catch (...) {
	failed();
      }
    }

  private:
    void run__() {
      m_context = Context{nullptr, m_server->stdioContext_()};
      m_streamContext = m_server->openStreamContext_(m_context);
      m_context = Context{
	nullptr, m_context.session(), m_streamContext.ptr()};
      auto span = ZuSpan<char>{m_body->span()};
      m_responder.era(
	m_server->stdioEra_() == Era::Legacy ?
	  Era::Legacy : Era::Modern);
      auto emit = [this](auto message) {
	m_responder.emit(ZuMv(message));
      };
      auto tool = [this](auto *req, const auto &request, auto completion) {
	Headers headers;
	m_server->tool_(
	  req, request, headers, m_context, ZuMv(completion));
      };
      if (!m_server->stdioDispatch_(
	  span, m_responder, emit, tool) || m_registrationFailed) {
	failed();
	return;
      }
      m_responder.finish();
      if (m_responder.terminal()) finish_();
    }

  public:

    template <typename M>
    bool send(M message) {
      return m_server->sendStdio_(ZuMv(message));
    }

    template <typename Req, typename Token>
    void made(Req *, Token *token) {
      if (!m_server->register_(m_generation, ZmRef(this))) {
	m_registrationFailed = true;
	return;
      }
      m_requestID = token->id();
      m_server->stdioPendingAdd_(m_requestID, this);
    }

    bool cancel(const ID &id, ZuCSpan reason) {
      return m_server->cancelStdio_(id, reason);
    }

    bool cancel_(const ID &id, ZuCSpan reason) {
      return live_() && m_responder.cancelTx_(id, reason);
    }

    template <typename Token, typename Res>
    bool completion(
	Responder *, Token *token, ToolReply<Res> reply) {
      if (!live_()) return false;
      if (m_server->txInvoked_())
	return complete_(token, ZuMv(reply));
      using Action = Server_::CompleteAction<Token, Res>;
      ZmRef<Action> action = new Action{token, ZuMv(reply)};
      return m_server->txRun([
	work = ZmRef(this), action = ZuMv(action)]() mutable {
	work->complete_(action->token(), action->reply());
      });
    }

    template <typename Token>
    bool progression(
	Responder *, Token *token,
	double value, double total, ZuCSpan message) {
      if (!live_()) return false;
      if (m_server->txInvoked_())
	return m_responder.progressTx_(token, value, total, message);
      using Action = Server_::ProgressAction<Token>;
      ZmRef<Action> action = new Action{token, value, total, message};
      return m_server->txRun([
	work = ZmRef(this), action = ZuMv(action)]() mutable {
	if (!work->live_()) return;
	(void)work->m_responder.progressTx_(
	  action->token(), action->value(),
	  action->total(), action->message());
      });
    }

    template <typename Token>
    bool logging(
	Responder *, Token *token,
	ZuCSpan level, ZuCSpan data, ZuCSpan logger) {
      if (!live_()) return false;
      if (m_server->txInvoked_())
	return m_responder.logTx_(token, level, data, logger);
      using Action = Server_::LogAction<Token>;
      ZmRef<Action> action = new Action{token, level, data, logger};
      return m_server->txRun([
	work = ZmRef(this), action = ZuMv(action)]() mutable {
	if (!work->live_()) return;
	(void)work->m_responder.logTx_(
	  action->token(), action->level(),
	  action->data(), action->logger());
      });
    }

    template <typename Req, typename Token>
    void cancelled(Req *req, Token *token, ZuCSpan reason) {
      m_server->cancelled_(req, token, reason);
    }

    void failed() {
      if (m_closed) return;
      finish_();
      m_server->failStdio_();
    }

    void close_() {
      if (m_closed) return;
      m_closed = true;
      pendingDone_();
      m_generation = 0;
      m_responder.close();
      m_streamContext.close();
    }

  private:
    template <typename Token, typename Res>
    bool complete_(Token *token, ToolReply<Res> reply) {
      if (!live_()) return false;
      pendingDone_();
      bool ok = m_responder.completeTx_(token, ZuMv(reply));
      if (m_responder.terminal()) finish_();
      return ok;
    }

    bool live_() const {
      return m_generation &&
	m_server->contains_(m_generation, this);
    }

    void finish_() {
      if (!m_generation) return;
      pendingDone_();
      m_streamContext.close();
      ZmRef<Self> self{this};
      uint64_t generation = m_generation;
      m_generation = 0;
      m_server->remove_(generation);
    }

    void pendingDone_() {
      if (m_requestID.is<void>()) return;
      m_server->stdioPendingDel_(m_requestID, this);
      m_requestID.null();
    }

    Server		*m_server;
    ZmRef<ZiIOBuf>	m_body;
    Responder		m_responder;
    Context		m_context;
    Server_::ContextRef	m_streamContext;
    ID			m_requestID;
    uint64_t		m_generation = 0;
    bool			m_registrationFailed = false;
    bool			m_closed = false;
  };

  template <typename WorkT>
  bool register_(uint64_t &generation, ZmRef<WorkT> work) {
    if (!m_up.load_() || m_works.count_() >= m_limits.maxPending)
      return false;
    generation = m_generation++;
    if (!generation) {
      m_up = false;
      return false;
    }
    m_works.add(Server_::WorkEntry{generation, ZuMv(work)});
    return true;
  }

  template <typename WorkT>
  bool contains_(uint64_t generation, WorkT *work) const {
    auto entry = m_works.findPtr(generation);
    return entry && entry->object == work;
  }

  void remove_(uint64_t generation) {
    (void)m_works.del(generation);
  }

  void discardSSE_(Server_::SSEQueue queue) {
    ZiAssert(m_mx && m_mx->invoked(m_txThread), "Zmcp", (),
	"SSE discard outside owner shard", return);
    if (!queue.count_()) return;
    derived()->m_sseGarbage += ZuMv(queue);
    postSSEDrain_();
  }

  void postSSEDrain_() {
    if (derived()->m_ssePosted || !m_mx) return;
    derived()->m_ssePosted = true;
    m_mx->run([this]() { drainSSE_(); }, m_txThread);
  }

  void drainSSE_() {
    derived()->m_ssePosted = false;
    unsigned visited = 0;
    while (visited < m_limits.workBatch && derived()->m_sseGarbage.shift())
      ++visited;
    if (derived()->m_sseGarbage.count_()) {
      postSSEDrain_();
      return;
    }
    auto closeFn = ZuMv(derived()->m_sseCloseFn);
    derived()->m_sseCloseFn = {};
    if (closeFn) closeFn();
  }

  template <typename Done>
  void closeSSE_(Done done) {
    ZiAssert(!derived()->m_sseCloseFn, "Zmcp", (),
	"duplicate SSE close continuation", return);
    if (!derived()->m_sseGarbage.count_() && !derived()->m_ssePosted) {
      done();
      return;
    }
    derived()->m_sseCloseFn = Server_::SSECloseFn{ZuMv(done)};
    postSSEDrain_();
  }

  bool cancelStdio_(const ID &id, ZuCSpan reason) {
    auto entry = derived()->m_stdioPending.findPtr(id);
    return entry && entry->cancel(reason);
  }

  void *transportContext_(Zhttp::Session session) {
    auto entry = derived()->m_transportContexts.findPtr(session.id);
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
    auto i = derived()->m_transportContexts.iter();
    if (!i()) return decltype(i.del()){};
    return i.del();
  }

  template <typename Done>
  void closeWorks_(Done done) {
    unsigned visited = 0;
    while (visited < m_limits.workBatch) {
      auto entry = takeWork_();
      if (!entry) {
	done();
	return;
      }
      ++visited;
      entry->close();
    }
    m_mx->run([
      this, done = ZuMv(done)]() mutable { closeWorks_(ZuMv(done));
    }, m_txThread);
  }

  Server_::WorkHash::NodeMvRef takeWork_() {
    auto i = m_works.iter();
    if (!i()) return decltype(i.del()){};
    return i.del();
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
  Server_::WorkHash	m_works;
  Limits		m_limits;
  uint64_t		m_generation = 1;
  unsigned		m_txThread = 0;

  ZmAtomic<unsigned>	m_up = 0;
};

template <typename Impl, typename Catalog>
class IOServer : public Server<IOServer<Impl, Catalog>, Impl, Catalog> {
  using Base = Server<IOServer, Impl, Catalog>;
  friend Base;
public:
  ~IOServer() { this->final(); }

  bool init(ZiMultiplex *mx, StdioConfig config, Impl *impl) {
    if (this->m_mx || !impl || !mx || !mx->txThread()) return false;
    return Base::init(mx, ZuMv(config), impl);
  }

  bool start() {
    if (!this->m_stdio || !this->m_up.load_() ||
	this->m_stdioStarted || this->m_stdioDone.load_()) return false;
    return ZmBlock<bool>{}([this](auto wake) mutable {
      this->m_mx->run([this, wake = ZuMv(wake)]() mutable {
	bool ok = false;
	try {
	  this->m_stdioContext = Server_::openContext(
	    this->m_impl, SessionTag{}, 0, true, ZuCSpan{});
	  ok = this->m_stdio->start_();
	} catch (...) {
	}
	this->m_stdioStarted = ok;
	if (!ok) {
	  this->m_stdioContext.close();
	  this->m_up = false;
	  this->m_stdioDone = true;
	}
	wake(ok);
      }, this->m_txThread);
    });
  }

  bool stop() { return this->stopStdio_(); }

  void final() {
    if (!this->m_mx) return;
    (void)this->stopStdio_();
    this->m_stdio = nullptr;
    this->m_stdioPeer.close();
    this->m_impl = nullptr;
    this->m_mx = nullptr;
    this->m_txThread = 0;
  }

private:
  Server_::PendingHash	m_stdioPending;
  Peer<Catalog>	m_stdioPeer;
  Server_::ContextRef	m_stdioContext;
  ZmFn<void(bool)>	m_stdioStopFn;
  bool			m_stdioStarted = false;
  bool			m_stdioClosing = false;
  bool			m_stdioFailure = false;
  ZmAtomic<unsigned>	m_stdioDone = 0;
  ZuPtr<IOLink<Base>> m_stdio;
};

template <typename Impl, typename Catalog>
class HTTPServer : public Server<HTTPServer<Impl, Catalog>, Impl, Catalog> {
  using Base = Server<HTTPServer, Impl, Catalog>;
  friend Base;
public:
  ~HTTPServer() { this->final(); }

  bool init(const Zhttp::HubConfig &hub, ServerConfig config, Impl *impl) {
    if (this->m_mx || !impl || !hub.mx() || !config.endpoint())
      return false;
    return Base::init(hub, ZuMv(config), impl);
  }

  typename Base::HTTP &http() { return m_http; }
  const typename Base::HTTP &http() const { return m_http; }

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
  Server_::TransportContextHash m_transportContexts;
  Server_::HTTPSessionHash<Catalog> m_sessions;
  Server_::SSEQueue	m_sseGarbage;
  Server_::SSECloseFn	m_sseCloseFn;
  Ztls::Random		m_rng;
  HTTPValue		m_endpoint;
  unsigned		m_legacyLifetime = 0;
  bool			m_absentOrigin = false;
  bool			m_legacySessions = true;
  bool			m_httpDone = false;
  bool			m_ssePosted = false;
  typename Base::HTTP m_http;
};

} // Zmcp

#endif /* ZmcpServer_HH */
