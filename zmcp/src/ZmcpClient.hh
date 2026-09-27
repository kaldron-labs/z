//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z MCP client library

#ifndef ZmcpClient_HH
#define ZmcpClient_HH

#ifndef ZmcpLib_HH
#include <zlib/ZmcpLib.hh>
#endif

#include <limits.h>

#include <zlib/Zmcp.hh>

#include <zlib/ZuBase64.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuPtr.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmPQueue.hh>

#include <zlib/ZtScratch.hh>

#include <zlib/ZiIOBuf.hh>

#include <zlib/ZhttpClient.hh>

namespace Zmcp {

using HeaderEncodingPrefix = ZuStringT<"=?base64?">;
using HeaderEncodingSuffix = ZuStringT<"?=">;
using ParameterHeaderPrefix = ZuStringT<"Mcp-Param-">;
ZuDerive(HeaderValueScratch, (ZtBArray<ZtArrayHeapID<"Zmcp.Header.Value",
  ZtArraySharded<true>>>));
ZuDerive(HeaderNameScratch, (ZtBArray<ZtArrayHeapID<"Zmcp.Header.Name",
  ZtArraySharded<true>>>));
ZuDerive(HeaderIntScratch, (ZtBArray<ZtArrayHeapID<"Zmcp.Header.Integer",
  ZtArraySharded<true>>>));

inline bool plainHeaderValue(ZuCSpan value)
{
  auto n = value.length();
  if (n) {
    auto first = value[0];
    auto last = value[n - 1];
    if (first == ' ' || first == '\t' || last == ' ' || last == '\t')
      return false;
  }
  for (unsigned i = 0; i < n; ++i) {
    auto c = uint8_t(value[i]);
    if (c != '\t' && (c < 0x20 || c > 0x7e)) return false;
  }
  auto prefix = HeaderEncodingPrefix{}();
  auto suffix = HeaderEncodingSuffix{}();
  unsigned prefixLength = prefix.length();
  unsigned suffixLength = suffix.length();
  return n < prefixLength + suffixLength ||
    ZuCSpan{value.data(), prefixLength} != prefix ||
    ZuCSpan{value.data() + n - suffixLength, suffixLength} != suffix;
}

inline bool validHeaderName(ZuCSpan name)
{
  if (!name) return false;
  for (unsigned i = 0, n = name.length(); i < n; ++i) {
    auto c = name[i];
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
	(c >= 'a' && c <= 'z')) continue;
    switch (c) {
      case '!': case '#': case '$': case '%': case '&': case '\'':
      case '*': case '+': case '-': case '.': case '^': case '_':
      case '`': case '|': case '~': continue;
      default: return false;
    }
  }
  return true;
}

template <typename L>
inline void headerValue(ZuCSpan value, L &&l)
{
  if (plainHeaderValue(value)) {
    l(value);
    return;
  }
  auto prefix = HeaderEncodingPrefix{}();
  auto suffix = HeaderEncodingSuffix{}();
  unsigned overhead = prefix.length() + suffix.length();
  uint64_t encoded_ = ZuBase64::enclen(value.length());
  if (encoded_ > UINT_MAX - overhead) return;
  unsigned encoded = encoded_;
  auto buffer = ZtScratch(HeaderValueScratch, encoded + overhead);
  buffer << prefix;
  unsigned offset = buffer.length();
  buffer.length(offset + encoded);
  encoded = ZuBase64::encode(
    ZuSpan<uint8_t>{buffer}.offset(offset), ZuBSpan{value});
  buffer.length(offset + encoded);
  buffer << suffix;
  l(ZuBSpan{buffer});
}

template <unsigned Code, bool Optional, typename L, typename V>
inline void parameterHeader(ZuCSpan name, const V &value, L &&l)
{
  if (!validHeaderName(name)) return;
  auto prefix = ParameterHeaderPrefix{}();
  auto header = ZtScratch(HeaderNameScratch, name.length() + prefix.length());
  header << prefix << name;
  auto emit = [&header, &l](ZuCSpan rendered) {
    headerValue(rendered, [&header, &l](const auto &encoded) {
      l(ZuBSpan{header}, encoded);
    });
  };
  if constexpr (Code == ZfFieldTC::CString || Code == ZfFieldTC::String) {
    ZuCSpan rendered{value};
    if (rendered || !Optional) emit(rendered);
  } else if constexpr (Code == ZfFieldTC::Bool) {
    emit(value ? ZuCSpan{"true"} : ZuCSpan{"false"});
  } else {
    if (ZuNull(value)) return;
    constexpr uint64_t SafeInteger = 9'007'199'254'740'991ULL;
    using T = ZuDecay<V>;
    if constexpr (ZuTraits<T>::IsSigned) {
      if (int64_t(value) < -int64_t(SafeInteger) ||
	  int64_t(value) > int64_t(SafeInteger)) return;
    } else {
      if (uint64_t(value) > SafeInteger) return;
    }
    auto boxed = ZuBoxed(value);
    auto rendered = ZtScratch(HeaderIntScratch,
      ZuPrint<decltype(boxed)>::length(boxed));
    rendered << boxed;
    emit(rendered);
  }
}

template <typename O, typename L>
inline void parameterHeaders(const O &object, L &&l)
{
  using Fields = ZuFields<O, ZuFacet::JSON>;
  ZuUnroll::all<Fields>([&object, &l]<typename Field>() {
    using Props = typename Field::Props;
    using Header = ZuFieldProp::MCP::GetHeader<Props>;
    enum { Code = Field::Type::Code };
    if constexpr (!ZuIsSame<void, Header>{}) {
      if constexpr (IsParameterHeader<Code, Props>{})
	parameterHeader<Code, ZuFieldProp::JSON::GetOptional<Props>{}>(
	  Header{}(), Field::get(object), l);
    } else if constexpr (Code == ZfFieldTC::UDT) {
      using T = typename Field::T;
      if constexpr (bool(ZuFields<T, ZuFacet::JSON>::N))
	parameterHeaders(Field::get(object), l);
    }
  });
}

class ClientConfig : public Zhttp::Config {
public:
  const Limits &limits() const { return m_limits; }

  ClientConfig &limits(Limits v) { m_limits = ZuMv(v); return *this; }

  ZuCSpan endpoint() const { return m_endpoint; }
  ClientConfig &endpoint(ZuCSpan v) {
    m_endpoint = v;
    return *this;
  }

  bool legacySessions() const { return m_legacySessions; }
  ClientConfig &legacySessions(bool v) {
    m_legacySessions = v;
    return *this;
  }

private:
  HTTPValue	m_endpoint{"/mcp"};
  Limits	m_limits;
  bool		m_legacySessions = true;
};

template <typename Message>
struct HTTPRequestBuilder : public Zhttp::ReqBuilder {
  using Headers = ZuTypeConcat<ZhttpHeaders(
    "content-type", "accept", "content-length"), RoutingHeaders>;
  using ContentType = ZuStringT<"content-type">;
  using Accept = ZuStringT<"accept">;
  using ContentLength = ZuStringT<"content-length">;
  using Version = ProtocolVersion;
  using Session = SessionID;
  using Method = MethodHeader;
  using Name = NameHeader;
  using Zhttp::ReqBuilder::header;

  Message message;
  HTTPValue endpoint;
  HTTPValue sessionID;
  uint64_t sequence = 0;
  uint64_t maxBodyBytes = Default::MaxJSONBytes;
  mutable uint64_t bodyLength = 0;
  int era = Era::Modern;

  Zhttp::BodyPolicy::T bodyPolicy() const {
    return empty_(message, 0) ?
      Zhttp::BodyPolicy::None : Zhttp::BodyPolicy::Fixed;
  }
  uint64_t key() const { return sequence; }

  template <typename L>
  void operation(L &&l) const {
    l(empty_(message, 0) ? Zhttp::Method::DELETE : Zhttp::Method::POST,
      [this](auto &&emit) {
      emit([this](auto &out) { out << endpoint; });
    });
  }

  template <typename Key, typename L>
  void header(L &&l) const {
    if constexpr (ZuIsSame<Key, ContentType>{}) {
      if (!empty_(message, 0)) l("application/json");
    } else if constexpr (ZuIsSame<Key, Accept>{}) {
      if (!empty_(message, 0)) l("application/json, text/event-stream");
    } else if constexpr (ZuIsSame<Key, ContentLength>{})
      l(empty_(message, 0) ?
	ZuCSpan{"0"} : Zhttp::contentLengthPad());
    else if constexpr (ZuIsSame<Key, Version>{})
      l(era == Era::Legacy ? LegacyVersion{}() : ModernVersion{}());
    else if constexpr (ZuIsSame<Key, Session>{}) {
      if (sessionID) l(sessionID);
    } else if constexpr (ZuIsSame<Key, Method>{}) {
      if (era == Era::Modern) l(message.method());
    } else if constexpr (ZuIsSame<Key, Name>{}) {
      if (era != Era::Modern) return;
      auto name = message.name();
      if (name) headerValue(name, ZuFwd<L>(l));
    }
  }
  template <typename L>
  void header(L &&l) const {
    if (era == Era::Modern) message.header(ZuFwd<L>(l));
  }

  template <typename Emit>
  void body(Emit &&emit) const {
    if (empty_(message, 0)) return;
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
    if (empty_(message, 0)) return;
    Zhttp::contentLengthSet(l, bodyLength);
  }

  template <typename M,
    typename = decltype(ZuDeclVal<const M &>().empty(), bool())>
  static bool empty_(const M &message_, int) {
    return message_.empty();
  }
  template <typename M>
  static bool empty_(const M &, long) { return false; }
};

struct DiscoverRequestMessage {
  ID id;
  int era = Era::Modern;

  static ZuCSpan method() { return "server/discover"; }
  static ZuCSpan name() { return {}; }

  template <typename S>
  void write(S &out) const {
    saveRequest(out, id, method(), MetaParams{clientMeta(era)});
  }
};

struct InitializeRequestMessage {
  ID id;

  static ZuCSpan method() { return "initialize"; }
  static ZuCSpan name() { return {}; }

  template <typename S>
  void write(S &out) const {
    saveRequest(out, id, method(), InitializeParams{
      LegacyVersion{}(), {}, {"zmcp", Z_VERNAME}});
  }
};

struct InitializedMessage {
  static ZuCSpan method() { return "notifications/initialized"; }
  static ZuCSpan name() { return {}; }

  template <typename S>
  void write(S &out) const {
    saveNotification(out, method(), EmptyObject{});
  }
};

struct DeleteRequestMessage {
  static ZuCSpan method() { return {}; }
  static ZuCSpan name() { return {}; }
  bool empty() const { return true; }

  template <typename S>
  void write(S &) const { }
};

struct PingRequestMessage {
  ID id;
  int era = Era::Unknown;

  static ZuCSpan method() { return "ping"; }
  static ZuCSpan name() { return {}; }

  template <typename S>
  void write(S &out) const {
    saveRequest(out, id, method(), MetaParams{clientMeta(era)});
  }
};

struct SetLevelRequestMessage {
  ID id;
  ErrorString level;

  static ZuCSpan method() { return "logging/setLevel"; }
  static ZuCSpan name() { return {}; }

  template <typename S>
  void write(S &out) const {
    saveRequest(out, id, method(), SetLevelParams{level});
  }
};

struct CancelledRequestMessage {
  ID requestID;
  ErrorString reason;
  int era = Era::Unknown;

  static ZuCSpan method() { return "notifications/cancelled"; }
  static ZuCSpan name() { return {}; }

  template <typename S>
  void write(S &out) const {
    saveNotification(out, method(), CancelledParams{
      requestID, reason, clientMeta(era)});
  }
};

struct ToolsListRequestMessage {
  ID id;
  int era = Era::Unknown;

  static ZuCSpan method() { return "tools/list"; }
  static ZuCSpan name() { return {}; }

  template <typename S>
  void write(S &out) const {
    saveRequest(out, id, method(), MetaParams{clientMeta(era)});
  }
};

template <typename Reqs>
struct ToolCallRequestMessage {
  ID id;
  ToolsCallParams<Reqs> params;

  static ZuCSpan method() { return "tools/call"; }
  ZuCSpan name() const {
    ZuCSpan out;
    if constexpr (Reqs::N) {
      int index = params.active();
      if (index < 0) return out;
      ZuSwitch::dispatch<Reqs::N>(index, [&out](auto I) {
	using Req = ZuType<I, Reqs>;
	out = GetToolID<Req>{}();
      });
    }
    return out;
  }

  bool streaming() const {
    bool out = false;
    if constexpr (Reqs::N) {
      int index = params.active();
      if (index < 0) return false;
      ZuSwitch::dispatch<Reqs::N>(index, [&out](auto I) {
	using Req = ZuType<I, Reqs>;
	out = Req::ResponseBody == BodyPolicy::SSE;
      });
    }
    return out;
  }

  bool idempotent() const {
    bool out = false;
    if constexpr (Reqs::N) {
      int index = params.active();
      if (index < 0) return false;
      ZuSwitch::dispatch<Reqs::N>(index, [&out](auto I) {
	using Req = ZuType<I, Reqs>;
	out = ToolIdempotent<Req>{};
      });
    }
    return out;
  }

  template <typename S>
  void write(S &out) const {
    saveRequest(out, id, method(), params);
  }

  template <typename L>
  void header(L &&l) const {
    if constexpr (Reqs::N) {
      int index = params.active();
      if (index < 0) return;
      ZuSwitch::dispatch<Reqs::N>(index, [this, &l](auto I) {
	using Req = ZuType<I, Reqs>;
	parameterHeaders(
	  params.arguments().template p<ToolArg<Req>>().object(), l);
      });
    }
  }
};

namespace ClientState {
  enum { Fresh, Discovering, Initializing, Ready, Closed };
}

struct PendingEntry {
  using ProcessFn = bool (*)(void *, const ZfJSON::AnyNode *);
  using ErrorFn = void (*)(void *, const Error &);
  using FailedFn = void (*)(void *);
  using ReleaseFn = void (*)(void *);

  ID		id;
  void		*object = nullptr;
  ProcessFn	processFn = nullptr;
  ErrorFn	errorFn = nullptr;
  FailedFn	failedFn = nullptr;
  ReleaseFn	releaseFn = nullptr;

  PendingEntry() = default;
  PendingEntry(const PendingEntry &) = delete;
  PendingEntry &operator =(const PendingEntry &) = delete;
  PendingEntry(PendingEntry &&entry) :
      id{ZuMv(entry.id)}, object{entry.object}, processFn{entry.processFn},
      errorFn{entry.errorFn}, failedFn{entry.failedFn},
      releaseFn{entry.releaseFn} {
    entry.object = nullptr;
  }
  PendingEntry &operator =(PendingEntry &&entry) {
    if (this == &entry) return *this;
    release_();
    id = ZuMv(entry.id);
    object = entry.object;
    processFn = entry.processFn;
    errorFn = entry.errorFn;
    failedFn = entry.failedFn;
    releaseFn = entry.releaseFn;
    entry.object = nullptr;
    return *this;
  }

  template <typename Req, typename Call>
  PendingEntry(Req *, ID id_, ZmRef<Call> call) :
      id{ZuMv(id_)}, object{ZuMv(call).release()},
      processFn{[](void *object_, const ZfJSON::AnyNode *node) {
	auto reply = loadToolReply<Req>(node);
	if (!reply.type()) return false;
	reply.dispatch([object_](auto, const auto &value) {
	  static_cast<Call *>(object_)->process(value);
	});
	return true;
      }},
      errorFn{error_<Call>}, failedFn{failed_<Call>},
      releaseFn{release_<Call>} { }

  template <typename Call>
  PendingEntry(ID id_, ZmRef<Call> call) :
      id{ZuMv(id_)}, object{ZuMv(call).release()},
      processFn{[](void *object_, const ZfJSON::AnyNode *) {
	static_cast<Call *>(object_)->process();
	return true;
      }},
      errorFn{error_<Call>}, failedFn{failed_<Call>},
      releaseFn{release_<Call>} { }

  ~PendingEntry() { release_(); }

  bool process(const ZfJSON::AnyNode *node) {
    return object && processFn(object, node);
  }
  void error(const Error &error_) {
    if (object) errorFn(object, error_);
  }
  void failed() {
    if (object) failedFn(object);
  }

  template <typename Call>
  static void error_(void *object, const Error &error) {
    static_cast<Call *>(object)->failed(error);
  }
  template <typename Call>
  static void failed_(void *object) {
    static_cast<Call *>(object)->failed();
  }
  template <typename Call>
  static void release_(void *object) {
    auto call = static_cast<Call *>(object);
    if (call->deref()) delete call;
  }

  void release_() {
    if (!object) return;
    auto object_ = object;
    object = nullptr;
    releaseFn(object_);
  }

};

inline const ID &PendingEntry_KeyAxor(const PendingEntry &entry) {
  return entry.id;
}

ZmHashDerive(PendingHash, PendingEntry,
  (ZmHashNode<PendingEntry,
    ZmHashKey<PendingEntry_KeyAxor,
	ZmHashLock<ZmNoLock,
	  ZmHashHeapID<"Zmcp.Pending">>>>));

class PendingCalls {
public:
  enum { Open, Closing, Closed };

  PendingCalls(unsigned maxPending = Default::MaxPending) :
    m_maxPending{maxPending} { }

  unsigned count() const { return m_hash.count_(); }
  int state() const { return m_state; }
  bool contains(const ID &id) { return bool(m_hash.findPtr(id)); }

  bool init(unsigned maxPending) {
    if (!maxPending || m_state != Open || m_hash.count_()) return false;
    m_maxPending = maxPending;
    return true;
  }

  template <typename Req, typename Call>
  bool add(ID id, ZmRef<Call> call) {
    if (m_state != Open || id.absent() || !call ||
	m_hash.count_() >= m_maxPending ||
	m_hash.findPtr(id)) return false;
    m_hash.add(PendingEntry{
      static_cast<Req *>(nullptr), ZuMv(id), ZuMv(call)});
    return true;
  }

  template <typename Call>
  bool add(ID id, ZmRef<Call> call) {
    if (m_state != Open || id.absent() || !call ||
	m_hash.count_() >= m_maxPending ||
	m_hash.findPtr(id)) return false;
    m_hash.add(PendingEntry{ZuMv(id), ZuMv(call)});
    return true;
  }

  template <typename Reqs>
  bool receive(const Envelope<Reqs> &envelope) {
    if (envelope.kind != MessageKind::Result &&
	envelope.kind != MessageKind::Error) return true;
    auto entry = m_hash.del(envelope.id());
    if (!entry) return true;
    try {
      if (envelope.kind == MessageKind::Error) {
	entry->error(loadError(raw(envelope.error())));
	return true;
      }
      if (entry->process(raw(envelope.result()))) return true;
      entry->failed();
    } catch (...) {
      return false;
    }
    return false;
  }

  bool fail(const ID &id) {
    auto entry = m_hash.del(id);
    if (!entry) return false;
    try {
      entry->failed();
    } catch (...) {
    }
    return true;
  }

  bool close(unsigned limit) {
    if (m_state == Open) m_state = Closing;
    unsigned visited = 0;
    while (visited < limit) {
      auto entry = take_();
      if (!entry) {
	m_state = Closed;
	return true;
      }
      ++visited;
      try {
	entry->failed();
      } catch (...) {
      }
    }
    if (m_hash.count_()) return false;
    m_state = Closed;
    return true;
  }

private:
  PendingHash::NodeMvRef take_() {
    auto i = m_hash.iter();
    if (!i()) return decltype(i.del()){};
    return i.del();
  }

  PendingHash	m_hash;
  unsigned	m_maxPending;
  int		m_state = Open;
};

namespace Client_ {

template <typename App,
  typename = decltype(ZuDeclVal<App * &>()->progress(ZuDeclVal<const ID &>(),
    ZuDeclVal<double &>(), ZuDeclVal<double &>(), ZuDeclVal<ZuCSpan &>()), void())>
void progress(
    App *app, const ID &token, double value, double total,
    ZuCSpan message, int)
{
  app->progress(token, value, total, message);
}
template <typename App>
void progress(App *, const ID &, double, double, ZuCSpan, long) { }

template <typename App,
  typename = decltype(ZuDeclVal<App * &>()->logging(ZuDeclVal<ZuCSpan &>(),
    ZuDeclVal<const ZfJSON::AnyNode * &>(), ZuDeclVal<ZuCSpan &>()), void())>
void logging(
    App *app, ZuCSpan level, const ZfJSON::AnyNode *data,
    ZuCSpan logger, int)
{
  app->logging(level, data, logger);
}
template <typename App>
void logging(
    App *, ZuCSpan, const ZfJSON::AnyNode *, ZuCSpan, long) { }

template <typename App, typename Reqs>
bool receive(
    App *app, PendingCalls &pending, const Envelope<Reqs> &envelope)
{
  if (envelope.kind != MessageKind::Notification &&
      envelope.kind != MessageKind::Request)
    return pending.receive(envelope);
  try {
    if (envelope.method() == "notifications/progress") {
      auto params = loadObject<ProgressParams>(raw(envelope.params()));
      progress(app, params.token, params.progress, params.total,
	params.message, 0);
    } else if (envelope.method() == "notifications/message") {
      auto node = raw(envelope.params());
      auto params = loadObject<RxLogParams>(node);
      logging(app, params.level, member(node, "data"), params.logger, 0);
    }
  } catch (...) {
    return false;
  }
  return true;
}

} // Client_

class CatalogCache {
public:
  explicit operator bool() const { return m_result; }
  const ZfJSON::AnyNode *result() const { return m_result; }

  void clear() {
    m_result = nullptr;
    m_root = nullptr;
    m_input = nullptr;
  }

  void store(
      ZmRef<ZiIOBuf> input, ZuPtr<ZfJSON::AnyNode> root,
      const ZfJSON::AnyNode *result) {
    m_input = ZuMv(input);
    m_root = ZuMv(root);
    m_result = result;
  }

private:
  ZmRef<ZiIOBuf>	m_input;
  ZuPtr<ZfJSON::AnyNode>	m_root;
  const ZfJSON::AnyNode	*m_result = nullptr;
};

template <typename Reqs>
class ClientPeer {
public:
  ClientPeer() = default;
  ClientPeer(Limits limits) : m_limits{limits} { }

  int state() const { return m_state; }
  int era() const { return m_era; }
  bool ready() const { return m_state == ClientState::Ready; }
  const ZfJSON::AnyNode *toolCatalog() const { return m_cache.result(); }

  void discardCatalog() {
    m_toolsID = ID{};
    m_cache.clear();
  }

  void close() { m_state = ClientState::Closed; }

  template <typename Emit>
  bool reinitialize(Emit &&emit) {
    if (m_state != ClientState::Ready || m_era != Era::Legacy) return false;
    discardCatalog();
    m_initializeID = next_();
    if (m_initializeID.absent()) return false;
    m_state = ClientState::Initializing;
    emit(InitializeRequestMessage{m_initializeID});
    return true;
  }

  template <typename Emit>
  bool probe(Emit &&emit) {
    if (m_state != ClientState::Fresh) return false;
    m_probeID = next_();
    if (m_probeID.absent()) return false;
    m_state = ClientState::Discovering;
    emit(DiscoverRequestMessage{m_probeID});
    return true;
  }

  template <typename Emit>
  bool fallback(Emit &&emit) {
    if (m_state != ClientState::Discovering) return false;
    return initialize_(ZuFwd<Emit>(emit));
  }

  template <typename Emit>
  bool receive(ZuSpan<char> input, Emit &&emit) {
    auto received = [](const Envelope<Reqs> &) { return true; };
    return receive_(input, nullptr, ZuFwd<Emit>(emit), received);
  }

  template <typename Emit>
  bool receive(ZmRef<ZiIOBuf> input, Emit &&emit) {
    if (!input) return false;
    auto span = ZuSpan<char>{input->span()};
    auto received = [](const Envelope<Reqs> &) { return true; };
    return receive_(span, &input, ZuFwd<Emit>(emit), received);
  }

  template <typename Emit, typename Receive>
  bool receive(
      ZmRef<ZiIOBuf> input, Emit &&emit, Receive &&receive) {
    if (!input) return false;
    auto span = ZuSpan<char>{input->span()};
    return receive_(
      span, &input, ZuFwd<Emit>(emit), ZuFwd<Receive>(receive));
  }

  template <typename Emit>
  ID tools(Emit &&emit) {
    if (!ready()) return {};
    if (m_cache) return {};
    if (!m_toolsID.absent()) return m_toolsID;
    m_toolsID = next_();
    if (!m_toolsID.absent())
      emit(ToolsListRequestMessage{m_toolsID, m_era});
    return m_toolsID;
  }

  template <typename Req, typename Emit>
  ID call(ToolObject<typename Req::Object> object, Emit &&emit) {
    return call_<Req>(
      ZuMv(object), {}, LogLevel::Disabled, ZuFwd<Emit>(emit));
  }

  template <typename Req, typename Emit>
  ID callProgress(
      ToolObject<typename Req::Object> object,
      ID progressToken, Emit &&emit) {
    return call_<Req>(
      ZuMv(object), ZuMv(progressToken), LogLevel::Disabled,
      ZuFwd<Emit>(emit));
  }

  template <typename Req, typename Emit>
  ID callLog(
      ToolObject<typename Req::Object> object,
      int logLevel, Emit &&emit) {
    return call_<Req>(ZuMv(object), {}, logLevel, ZuFwd<Emit>(emit));
  }

  template <typename Req, typename Emit>
  ID callProgressLog(
      ToolObject<typename Req::Object> object,
      ID progressToken, int logLevel,
      Emit &&emit) {
    return call_<Req>(ZuMv(object), ZuMv(progressToken), logLevel,
      ZuFwd<Emit>(emit));
  }

  template <typename Emit>
  ID ping(Emit &&emit) {
    if (!ready()) return {};
    ID id = next_();
    if (!id.absent()) emit(PingRequestMessage{id, m_era});
    return id;
  }

  template <typename Emit>
  ID setLevel(ZuCSpan level, Emit &&emit) {
    if (!ready() || m_era != Era::Legacy) return {};
    ID id = next_();
    if (!id.absent())
      emit(SetLevelRequestMessage{id, ErrorString{level}});
    return id;
  }

private:
  template <typename Req, typename Emit>
  ID call_(
      ToolObject<typename Req::Object> object,
      ID progressToken, int logLevel,
      Emit &&emit) {
    ZuAssert((ZuTypeIn<Req, Reqs>{}));
    if (!ready()) return {};
    ID id = next_();
    if (id.absent()) return id;
    ToolsCallParams<Reqs> params;
    params.arguments() = ToolArg<Req>{ZuMv(object)};
    params.progressToken = ZuMv(progressToken);
    params.logLevel = logLevel;
    params.era = m_era;
    emit(ToolCallRequestMessage<Reqs>{id, ZuMv(params)});
    return id;
  }

  template <typename Emit, typename Receive>
  bool receive_(
      ZuSpan<char> input, ZmRef<ZiIOBuf> *owner,
      Emit &&emit, Receive &&receive) {
    if (m_state == ClientState::Closed) return false;
    auto parsed = parse<Reqs>(input, m_limits.maxJSONBytes);
    if (!parsed) {
      close();
      return false;
    }
    const auto &envelope = parsed.envelope;
    if (m_state == ClientState::Discovering && envelope.id() == m_probeID &&
	(envelope.kind == MessageKind::Result ||
	  envelope.kind == MessageKind::Error)) {
      if (envelope.kind == MessageKind::Result &&
	  supportsModern_(raw(envelope.result()))) {
	m_era = Era::Modern;
	m_state = ClientState::Ready;
	return true;
      }
      return initialize_(ZuFwd<Emit>(emit));
    }
    if (m_state == ClientState::Initializing &&
	envelope.id() == m_initializeID) {
      if (envelope.kind != MessageKind::Result) {
	close();
	return false;
      }
      m_era = Era::Legacy;
      m_state = ClientState::Ready;
      emit(InitializedMessage{});
      return true;
    }
    if (!m_toolsID.absent() && envelope.id() == m_toolsID &&
	(envelope.kind == MessageKind::Result ||
	  envelope.kind == MessageKind::Error)) {
      if (envelope.kind == MessageKind::Result && owner) {
	m_cache.store(
	  ZuMv(*owner), ZuMv(parsed.root), raw(envelope.result()));
	return true;
      }
      m_toolsID = ID{};
    }
    return receive(envelope);
  }
  static bool supportsModern_(const ZfJSON::AnyNode *result) {
    auto discover = loadObject<DiscoverRxResult>(result);
    for (unsigned i = 0, n = discover.supportedVersions.length(); i < n; ++i)
      if (discover.supportedVersions[i] == ModernVersion{}()) return true;
    return false;
  }

  template <typename Emit>
  bool initialize_(Emit &&emit) {
    m_initializeID = next_();
    if (m_initializeID.absent()) return false;
    m_state = ClientState::Initializing;
    emit(InitializeRequestMessage{m_initializeID});
    return true;
  }

  ID next_() {
    if (ZuUnlikely(m_nextID == INT64_MAX)) {
      close();
      return {};
    }
    return m_nextID++;
  }

  ID		m_probeID;
  ID		m_initializeID;
  ID		m_toolsID;
  CatalogCache	m_cache;
  int64_t	m_nextID = 1;
  Limits	m_limits;
  int		m_state = ClientState::Fresh;
  int		m_era = Era::Unknown;
};

namespace ClientControl {
  enum { Ping, SetLevel };
}

template <typename Call, typename Heap = ZuVoid>
struct ControlAction_ : public Heap, public ZmObject {
  ErrorString	level;
  ZmRef<Call>	call;
  int		kind;

  ControlAction_(int kind_, ZuCSpan level_, ZmRef<Call> call_) :
    level{level_}, call{ZuMv(call_)}, kind{kind_} { }
};

template <typename Call>
ZuDerive(ControlHeap, (ZmHeap<"Zmcp.Client.Control", ControlAction_<Call>>));

template <typename Call>
ZuDerive(ControlAction, (ControlAction_<Call, ControlHeap<Call>>));

namespace HTTPClient_ {

namespace ReqType {
  enum {
    Discover, Initialize, Initialized, Ping, SetLevel, ToolsList, ToolCall,
    Cancelled, Delete
  };
}

template <typename Catalog>
class Message {
  using Data = ZuUnion<void,
    DiscoverRequestMessage, InitializeRequestMessage, InitializedMessage,
    PingRequestMessage, SetLevelRequestMessage, ToolsListRequestMessage,
    ToolCallRequestMessage<Catalog>, CancelledRequestMessage,
    DeleteRequestMessage>;

public:
  template <typename M>
  void init(M message) {
    m_data.template p<M>(ZuMv(message));
    if constexpr (ZuIsSame<M, DiscoverRequestMessage>{})
      m_kind = ReqType::Discover;
    else if constexpr (ZuIsSame<M, InitializeRequestMessage>{})
      m_kind = ReqType::Initialize;
    else if constexpr (ZuIsSame<M, InitializedMessage>{})
      m_kind = ReqType::Initialized;
    else if constexpr (ZuIsSame<M, PingRequestMessage>{})
      m_kind = ReqType::Ping;
    else if constexpr (ZuIsSame<M, SetLevelRequestMessage>{})
      m_kind = ReqType::SetLevel;
    else if constexpr (ZuIsSame<M, ToolsListRequestMessage>{})
      m_kind = ReqType::ToolsList;
    else if constexpr (ZuIsSame<M, ToolCallRequestMessage<Catalog>>{})
      m_kind = ReqType::ToolCall;
    else if constexpr (ZuIsSame<M, CancelledRequestMessage>{})
      m_kind = ReqType::Cancelled;
    else
      m_kind = ReqType::Delete;
  }

  int kind() const { return m_kind; }

  const ID *id() const {
    const ID *out = nullptr;
    m_data.cdispatch([&out](auto, const auto &message) {
      using M = ZuDecay<decltype(message)>;
      if constexpr (!ZuIsSame<M, InitializedMessage>{} &&
	  !ZuIsSame<M, CancelledRequestMessage>{} &&
	  !ZuIsSame<M, DeleteRequestMessage>{})
	out = &message.id;
    });
    return out;
  }

  ZuCSpan method() const {
    ZuCSpan out;
    m_data.cdispatch([&out](auto, const auto &message) {
      out = message.method();
    });
    return out;
  }

  ZuCSpan name() const {
    ZuCSpan out;
    m_data.cdispatch([&out](auto, const auto &message) {
      out = message.name();
    });
    return out;
  }

  bool empty() const { return m_kind == ReqType::Delete; }

  bool streaming() const {
    bool out = false;
    m_data.cdispatch([&out](auto, const auto &message) {
      using M = ZuDecay<decltype(message)>;
      if constexpr (ZuIsSame<M, ToolCallRequestMessage<Catalog>>{})
	out = message.streaming();
    });
    return out;
  }

  bool idempotent() const {
    switch (m_kind) {
      case ReqType::Discover:
      case ReqType::Ping:
      case ReqType::SetLevel:
      case ReqType::ToolsList:
      case ReqType::Cancelled:
      case ReqType::Delete:
	return true;
      case ReqType::ToolCall:
	return m_data.template p<ToolCallRequestMessage<Catalog>>()
	  .idempotent();
      default:
	return false;
    }
  }

  template <typename S>
  void write(S &out) const {
    m_data.cdispatch([&out](auto, const auto &message) {
      message.write(out);
    });
  }

  template <typename L>
  void header(L &&l) const {
    m_data.cdispatch([&l](auto, const auto &message) {
      using M = ZuDecay<decltype(message)>;
      if constexpr (ZuIsSame<M, ToolCallRequestMessage<Catalog>>{})
	message.header(l);
    });
  }

private:
  Data	m_data;
  int	m_kind = ReqType::Discover;
};

template <typename Impl, typename Catalog> class Client;

template <typename Impl, typename Catalog>
struct Request_ : public ZmObject,
    public HTTPRequestBuilder<Message<Catalog>> {
  using Base = HTTPRequestBuilder<Message<Catalog>>;
  using BaseKeys = ZuTypeSlice<2, 0, typename Base::Headers>;
  using AppHeaderList = AppHeaders<Impl>;
  using AppKeys = ZuTypeSlice<2, 0, AppHeaderList>;
  using HeaderKeys = ZuTypeConcat<BaseKeys, AppKeys>;

  using Headers = ZuTypeConcat<typename Base::Headers, AppHeaderList>;
  ZuAssert(ZuTypeUnique<HeaderKeys>::N == HeaderKeys::N,
    "Zmcp application header duplicates a protocol header");

  template <typename M>
  void init(
      Client<Impl, Catalog> *client_, M message_, uint64_t sequence_,
      ZuCSpan endpoint_, ZuCSpan sessionID_, int era_, bool serial_) {
    client = client_;
    Base::message.init(ZuMv(message_));
    Base::sequence = sequence_;
    Base::endpoint = endpoint_;
    Base::sessionID = sessionID_;
    Base::maxBodyBytes = client_->limits().maxJSONBytes;
    if constexpr (ZuIsSame<M, InitializeRequestMessage>{} ||
	ZuIsSame<M, InitializedMessage>{} ||
	ZuIsSame<M, DeleteRequestMessage>{})
      Base::era = Era::Legacy;
    else
      Base::era = era_;
    serial = serial_;
  }

  bool idempotent(Zhttp::Method::T) const {
    return Base::message.idempotent();
  }
  bool streaming() const { return Base::message.streaming(); }
  int kind() const { return Base::message.kind(); }
  const ID *id() const { return Base::message.id(); }

  template <typename Key, typename Value, typename L>
  void header(L &&l) const {
    if constexpr (ZuTypeIn<Key, BaseKeys>{})
      Base::template header<Key, Value>(ZuFwd<L>(l));
    else
      l();
  }

  template <typename Key, typename L>
  void header(L &&l) const {
    if constexpr (ZuTypeIn<Key, BaseKeys>{})
      Base::template header<Key>(ZuFwd<L>(l));
    else if (client)
      client->template requestHeader<Key>(ZuFwd<L>(l));
  }

  template <typename L>
  void header(L &&l) const { Base::header(ZuFwd<L>(l)); }

  void response(bool ok) {
    responseOK = responseSeen ? responseOK && ok : ok;
    responseSeen = true;
  }

  void completed(const Zhttp::Result &result) {
    client->completed(this, result);
  }

  Client<Impl, Catalog>	*client = nullptr;
  bool			responseOK = false;
  bool			responseSeen = false;
  bool			serial = false;
  bool			cancelled = false;
};

template <typename Impl, typename Catalog>
class ResponseParser :
    public HTTPResponseParser<ResponseParser<Impl, Catalog>> {
  using Base = HTTPResponseParser<ResponseParser>;
  using Request = Request_<Impl, Catalog>;

public:
  void init(Request &request) {
    m_request = &request;
    Base::streaming(request.streaming());
  }

  const Limits &limits() const { return m_request->client->limits(); }

  bool receiveSSE(const SSEEvent &event, const HTTPResponseMeta &meta) {
    return m_request &&
      m_request->client->receiveSSE(m_request, event, meta);
  }

  template <typename Link>
  void corruptHTTPResponse(Link *link) {
    if (m_request) m_request->response(false);
    if (link) link->disconnect();
  }

  template <typename Link>
  void failedHTTPResponse(Link *) {
    if (m_request) m_request->response(false);
  }

  template <typename Link>
  void acceptedHTTPResponse(
      Link *, unsigned status, const HTTPResponseMeta &meta) {
    if (m_request)
      m_request->client->accepted(m_request, status, meta);
  }

  template <typename Link>
  void completeHTTPResponse(
      Link *, unsigned status, const HTTPResponseMeta &meta) {
    if (m_request)
      m_request->client->streamComplete(m_request, status, meta);
  }

  template <typename Link>
  void receiveHTTPResponse(
      Link *link, unsigned status, ZmRef<ZiIOBuf> body,
      const HTTPResponseMeta &meta) {
    if (m_request &&
	!m_request->client->receive(
	  m_request, status, ZuMv(body), meta) &&link)
      link->disconnect();
  }

  void reset() {
    Base::reset();
    m_request = nullptr;
  }

private:
  Request *m_request = nullptr;
};

template <typename Impl, typename Catalog> struct Pool;
template <typename Impl, typename Catalog, typename Heap> class Pool_;

template <typename Impl, typename Catalog>
using RequestQ = ZmPQueue<Request_<Impl, Catalog>,
  ZmPQueueOverlap<false,
    ZmPQueueNode<Request_<Impl, Catalog>,
      ZmPQueueHeapID<"Zmcp.HTTP.Request">>>>;

template <typename Impl, typename Catalog>
using Request = typename RequestQ<Impl, Catalog>::Node;

template <typename Impl, typename Catalog>
struct SerialRecord {
  ZmRef<Request<Impl, Catalog>> request;
};

template <typename Impl, typename Catalog>
using SerialQ = ZmList<SerialRecord<Impl, Catalog>,
  ZmListNode<SerialRecord<Impl, Catalog>,
    ZmListHeapID<"Zmcp.HTTP.Serial">>>;

template <typename Impl, typename Catalog>
using TxQ = ZmPQTx<Pool<Impl, Catalog>, RequestQ<Impl, Catalog>,
  ZmPQTxOrdered<false>>;

template <typename Impl, typename Catalog, typename Heap = ZuVoid>
class Pool_ : public Heap, public Zhttp::Pool<
    Client<Impl, Catalog>, TxQ<Impl, Catalog>,
    ResponseParser<Impl, Catalog>> {
  using Base = Zhttp::Pool<
    Client<Impl, Catalog>, TxQ<Impl, Catalog>,
    ResponseParser<Impl, Catalog>>;

public:
  Pool_(Client<Impl, Catalog> *client) : Base{client} { }

  RequestQ<Impl, Catalog> *txQueue() { return &m_requests; }
  void archive_(Request<Impl, Catalog> *) { }
  ZmRef<Request<Impl, Catalog>> retrieve_(
      typename RequestQ<Impl, Catalog>::Key,
      typename RequestQ<Impl, Catalog>::Key) { return {}; }

private:
  RequestQ<Impl, Catalog> m_requests;
};
template <typename Impl, typename Catalog>
ZuDerive(PoolHeap, (ZmHeap<"Zmcp.HTTP.Pool", Pool_<Impl, Catalog>>));
template <typename Impl, typename Catalog>
ZuDerive(Pool, (Pool_<Impl, Catalog, PoolHeap<Impl, Catalog>>));

template <typename Req, typename Call, typename Heap = ZuVoid>
struct CallAction_ : public Heap, public ZmObject {
  using Object = ToolObject<typename Req::Object>;

  CallAction_(
      Object object_, ZmRef<Call> call_, ID progress_, int logLevel_) :
    object{ZuMv(object_)}, call{ZuMv(call_)}, progress{ZuMv(progress_)},
    logLevel{logLevel_} { }

  Object	object;
  ZmRef<Call>	call;
  ID		progress;
  int		logLevel;
};

template <typename Req, typename Call>
ZuDerive(CallHeap, (ZmHeap<"Zmcp.HTTP.Call", CallAction_<Req, Call>>));

template <typename Req, typename Call>
ZuDerive(CallAction, (CallAction_<Req, Call, CallHeap<Req, Call>>));

struct ActiveEntry {
  ID		id;
  uint64_t	sequence = 0;
  void		*request = nullptr;
};

inline const ID &ActiveEntry_KeyAxor(const ActiveEntry &entry) {
  return entry.id;
}

ZmHashDerive(ActiveHash, ActiveEntry,
  (ZmHashNode<ActiveEntry,
    ZmHashKey<ActiveEntry_KeyAxor,
	ZmHashLock<ZmNoLock,
	  ZmHashHeapID<"Zmcp.HTTP.Active">>>>));

template <typename Heap = ZuVoid>
struct CancelAction_ : public Heap, public ZmObject {
  CancelAction_(ID id_, ZuCSpan reason_) :
    id{ZuMv(id_)}, reason{reason_} { }

  ID		id;
  ErrorString	reason;
};
ZuDerive(CancelActionHeap, (ZmHeap<"Zmcp.HTTP.Cancel", CancelAction_<>>));
ZuDerive(CancelAction, (CancelAction_<CancelActionHeap>));

template <typename Impl, typename Catalog>
class Client : public Zhttp::Client<
    Client<Impl, Catalog>, Pool<Impl, Catalog>> {
  using Base = Zhttp::Client<Client, Pool<Impl, Catalog>>;
  using Req = Request<Impl, Catalog>;
  using ReqBase = Request_<Impl, Catalog>;

public:
  Client() = default;
  ~Client() { final(); }

  bool init(
      const Zhttp::HubConfig &hub, Zhttp::Destination destination,
      ClientConfig config, Impl *impl,
      const Zhttp::TCPConfig &tcp = {}, const Zhttp::H2Config &h2 = {},
      const Zhttp::QUICConfig &quic = {}) {
    if (m_mx || !hub.mx() || !impl || !config.endpoint()) return false;
    m_impl = impl;
    m_mx = hub.mx();
    m_ownerThread = hub.rxThread() ?
      m_mx->sid(hub.rxThread()) : m_mx->rxThread();
    m_limits = config.limits();
    m_endpoint = config.endpoint();
    m_legacySessions = config.legacySessions();
    if (!m_ownerThread || !valid(m_limits))
      goto invalid;
    m_peer = ClientPeer<Catalog>{m_limits};
    if (!m_pending.init(m_limits.maxPending)) goto invalid;
    {
      Zhttp::Config http = config;
      http.retainedBodyMax(m_limits.maxJSONBytes)
	.retainedMessageMax(m_limits.maxSSEEventBytes);
      if (!Base::init(hub, 1, http, tcp, h2, quic) ||
	  !Base::pool(0, ZuMv(destination)))
	goto invalidBase;
    }
    return true;

  invalidBase:
    Base::final();
  invalid:
    m_impl = nullptr;
    m_mx = nullptr;
    m_ownerThread = 0;
    m_endpoint.null();
    return false;
  }

  bool start() {
    if (!m_mx || m_started || m_done.load_() || !Base::start()) return false;
    m_started = true;
    m_up = true;
    ownerRun_([this]() {
      bool sent = false;
      bool ok = m_peer.probe([self = this, &sent](auto message) {
	sent = self->send_(ZuMv(message));
      });
      if (!ok || !sent) fail_();
    });
    return true;
  }

  bool stop() {
    if (!m_mx) return false;
    if (m_done.load_()) return !m_failure;
    m_up = false;
    if (m_started) Base::stop();
    m_started = false;
    return ZmBlock<bool>{}([this](auto wake) mutable {
      ownerRun_([this, wake = ZuMv(wake)]() mutable {
	m_peer.close();
	m_current = 0;
	m_stopFn = ZuMv(wake);
	stopBatch_();
      });
    });
  }

  void final() {
    if (!m_mx) return;
    (void)stop();
    Base::final();
    m_impl = nullptr;
    m_mx = nullptr;
    m_ownerThread = 0;
    m_endpoint.null();
    m_sessionID.null();
  }

  const Limits &limits() const { return m_limits; }

  template <typename Key, typename L>
  void requestHeader(L &&l) const {
    requestHeader_<Key>(m_impl, ZuFwd<L>(l), 0);
  }

  bool tools() {
    if (!m_mx || !m_up.load_() || m_terminating.load_()) return false;
    if (++m_ingress > m_limits.maxPending) {
      --m_ingress;
      return false;
    }
    ownerRun_([this]() {
      --m_ingress;
      if (const auto *catalog = m_peer.toolCatalog()) {
	app_([this, catalog]() { tools_(m_impl, catalog, 0); });
	return;
      }
      bool sent = true;
      ID id = m_peer.tools([self = this, &sent](auto message) {
	sent = self->send_(ZuMv(message));
      });
      if ((id.absent() && !m_peer.toolCatalog()) || !sent)
	app_([this]() { toolsFailed_(m_impl, 0); });
    });
    return true;
  }

  bool discardCatalog() {
    if (!m_mx || !m_up.load_() || m_terminating.load_()) return false;
    if (++m_ingress > m_limits.maxPending) {
      --m_ingress;
      return false;
    }
    ownerRun_([this]() {
      --m_ingress;
      m_peer.discardCatalog();
    });
    return true;
  }

  template <typename Call>
  bool ping(ZmRef<Call> call) {
    return control_(ClientControl::Ping, {}, ZuMv(call));
  }

  template <typename Call>
  bool setLevel(ZuCSpan level, ZmRef<Call> call) {
    return control_(ClientControl::SetLevel, level, ZuMv(call));
  }

  bool terminate() {
    if (!m_mx || !m_up.load_() || m_terminating.cmpXch(1, 0)) return false;
    if (++m_ingress > m_limits.maxPending) {
      --m_ingress;
      return false;
    }
    ownerRun_([this]() {
      --m_ingress;
      terminate_();
    });
    return true;
  }

  template <typename R, typename Call>
  bool call(ToolObject<typename R::Object> object, ZmRef<Call> call) {
    return callProgressLog<R>(
      ZuMv(object), {}, LogLevel::Disabled, ZuMv(call));
  }

  template <typename R, typename Call>
  bool callProgress(
      ToolObject<typename R::Object> object, ID progress, ZmRef<Call> call) {
    return callProgressLog<R>(
      ZuMv(object), ZuMv(progress), LogLevel::Disabled, ZuMv(call));
  }

  template <typename R, typename Call>
  bool callLog(
      ToolObject<typename R::Object> object, ZuCSpan level,
      ZmRef<Call> call) {
    return callProgressLog<R>(
      ZuMv(object), {}, logLevel(level), ZuMv(call));
  }

  template <typename R, typename Call>
  bool callProgressLog(
      ToolObject<typename R::Object> object, ID progress, int logLevel,
      ZmRef<Call> call) {
    if (!m_mx || !m_up.load_() || m_terminating.load_() || !call)
      return false;
    if (++m_ingress > m_limits.maxPending) {
      --m_ingress;
      return false;
    }
    using Action = CallAction<R, Call>;
    ZmRef<Action> action = new Action{
      ZuMv(object), ZuMv(call), ZuMv(progress), logLevel};
    ownerRun_([this, action = ZuMv(action)]() mutable {
      --m_ingress;
      call_<R>(ZuMv(action->object), ZuMv(action->progress), action->logLevel,
	ZuMv(action->call));
    });
    return true;
  }

  bool cancel(ID id, ZuCSpan reason = {}) {
    if (!m_mx || !m_up.load_() || m_terminating.load_() || id.absent())
      return false;
    if (++m_ingress > m_limits.maxPending) {
      --m_ingress;
      return false;
    }
    ZmRef<CancelAction> action = new CancelAction{ZuMv(id), reason};
    ownerRun_([this, action = ZuMv(action)]() mutable {
      --m_ingress;
      bool ok = cancel_(action->id, action->reason);
      app_([this, action, ok]() {
	cancelled_(m_impl, action->id, ok, 0);
      });
    });
    return true;
  }

  bool receive(
      ReqBase *request, unsigned status, ZmRef<ZiIOBuf> body,
      const HTTPResponseMeta &meta) {
    if (!request || !body) return false;
    session_(request, meta);
    if (status < 200 || status >= 300) {
      request->response(false);
      return true;
    }
    bool ok = receive_(ZuMv(body));
    request->response(ok);
    return ok;
  }

  bool receiveSSE(
      ReqBase *request, const SSEEvent &event,
      const HTTPResponseMeta &meta) {
    if (!request) return false;
    session_(request, meta);
    ZmRef<ZiIOBuf> body = new HTTPBodyBuf{};
    if (!body->alloc(event.data.length())) {
      request->response(false);
      return false;
    }
    body->append(event.data);
    bool ok = receive_(ZuMv(body));
    request->response(ok);
    return ok;
  }

  void accepted(
      ReqBase *request, unsigned status, const HTTPResponseMeta &meta) {
    if (!request) return;
    session_(request, meta);
    request->response(status == 202);
  }

  void streamComplete(
      ReqBase *request, unsigned, const HTTPResponseMeta &meta) {
    if (!request) return;
    session_(request, meta);
  }

  void completed(ReqBase *request, const Zhttp::Result &result) {
    if (!request) return;
    bool ok = result.ok() && !result.retries && request->responseOK;
    int kind = request->kind();
    switch (kind) {
      case ReqType::ToolCall:
	if (const ID *id = request->id()) {
	  (void)m_active.del(*id);
	  if (m_pending.fail(*id)) ok = false;
	}
	break;
      case ReqType::Ping:
      case ReqType::SetLevel:
	if (const ID *id = request->id())
	  if (m_pending.fail(*id)) ok = false;
	break;
      case ReqType::Delete:
	if (ok) {
	  m_sessionID.null();
	  m_peer.close();
	  m_ready = false;
	}
	app_([this, ok]() { terminated_(m_impl, ok, 0); });
	break;
    }
    if (request->serial && request->ReqBase::sequence == m_current)
      m_current = 0;
    if (!ok) {
      if (kind == ReqType::Discover && result.ok() &&
	  result.status >= 400 && result.status < 500) {
	bool sent = false;
	bool initialized = m_peer.fallback(
	  [self = this, &sent](auto message) {
	    sent = self->send_(ZuMv(message));
	  });
	if (!initialized || !sent) fail_();
	return;
	}
      if (kind != ReqType::Delete && m_peer.era() == Era::Legacy &&
	  !request->cancelled &&
	  (result.status == 404 || result.status == 410)) {
	m_sessionID.null();
	if (m_up.load_() && m_peer.ready()) {
	  m_ready = false;
	  bool sent = false;
	  bool initialized = m_peer.reinitialize(
	    [self = this, &sent](auto message) {
	      sent = self->send_(ZuMv(message));
	    });
	  if (!initialized || !sent) fail_();
	}
      }
      switch (kind) {
	case ReqType::ToolCall:
	case ReqType::Ping:
	case ReqType::SetLevel:
	case ReqType::Delete:
	  break;
	default:
	  fail_();
      }
    }
    if (!m_up.load_()) return;
    if (!m_current) sendNext_();
  }

private:
  template <typename Key, typename App, typename L,
    typename = decltype(ZuDeclVal<App * &>()->template header<Key>(ZuFwd<L>(ZuDeclVal<L &>())), void())>
  static void requestHeader_(App *app, L &&l, int) {
    app->template header<Key>(ZuFwd<L>(l));
  }
  template <typename Key, typename App, typename L>
  static void requestHeader_(App *, L &&, long) { }

  template <typename Call>
  bool control_(int kind, ZuCSpan level, ZmRef<Call> call) {
    if (!m_mx || !m_up.load_() || m_terminating.load_() || !call)
      return false;
    if (++m_ingress > m_limits.maxPending) {
      --m_ingress;
      return false;
    }
    ZmRef<ControlAction<Call>> action =
      new ControlAction<Call>{kind, level, ZuMv(call)};
    ownerRun_([this, action = ZuMv(action)]() mutable {
      --m_ingress;
      controlTx_(action->kind, action->level, ZuMv(action->call));
    });
    return true;
  }

  template <typename L>
  void ownerRun_(L &&l) {
    if (m_mx->invoked(m_ownerThread)) l();
    else m_mx->run(ZuFwd<L>(l), m_ownerThread);
  }

  template <typename L>
  void app_(L &&l) {
    try {
      l();
    } catch (...) {
      fail_();
    }
  }

  void terminate_() {
    if (m_peer.ready() && m_peer.era() == Era::Legacy &&
      !m_legacySessions) {
      m_peer.close();
      app_([this]() { terminated_(m_impl, true, 0); });
      return;
    }
    bool ok = m_peer.ready() && m_peer.era() == Era::Legacy && m_sessionID &&
      send_(DeleteRequestMessage{});
    if (!ok) app_([this]() { terminated_(m_impl, false, 0); });
  }

  template <typename M>
  bool send_(
      M message, uint64_t *sequence = nullptr,
      ReqBase **request_ = nullptr) {
    ZmRef<Req> request = new Req{};
    bool serial = !m_peer.ready() || m_peer.era() == Era::Legacy;
    request->init(this, ZuMv(message), m_sequence++, m_endpoint,
      m_sessionID, m_peer.era(), serial);
    if (sequence) *sequence = request->ReqBase::sequence;
    if (request_) *request_ = request.ptr();
    if (serial && (m_current || m_drainingSerial)) {
      if (m_serial.count_() >= m_limits.maxQueue) return false;
      if (request->kind() == ReqType::Initialized)
	m_serial.unshift(SerialRecord<Impl, Catalog>{ZuMv(request)});
      else
	m_serial.push(SerialRecord<Impl, Catalog>{ZuMv(request)});
      return true;
    }
    if (serial) m_current = request->ReqBase::sequence;
    return Base::send(0, ZuMv(request));
  }

  bool receive_(ZmRef<ZiIOBuf> body) {
    const auto *catalog = m_peer.toolCatalog();
    bool sent = true;
    auto emit = [self = this, &sent](auto message) {
      if (sent) sent = self->send_(ZuMv(message));
    };
    auto receive = [this](const Envelope<Catalog> &envelope) {
      return Client_::receive(m_impl, m_pending, envelope);
    };
    bool ok = m_peer.receive(ZuMv(body), emit, receive) && sent;
    if (!catalog)
      if (const auto *catalog_ = m_peer.toolCatalog())
	app_([this, catalog_]() { tools_(m_impl, catalog_, 0); });
    return ok;
  }

  template <typename App,
    typename = decltype(ZuDeclVal<App * &>()->tools(ZuDeclVal<const ZfJSON::AnyNode * &>()), void())>
  static void tools_(App *app, const ZfJSON::AnyNode *catalog, int) {
    app->tools(catalog);
  }
  template <typename App>
  static void tools_(App *, const ZfJSON::AnyNode *, long) { }

  template <typename App,
    typename = decltype(ZuDeclVal<App * &>()->toolsFailed(), void())>
  static void toolsFailed_(App *app, int) {
    app->toolsFailed();
  }
  template <typename App>
  static void toolsFailed_(App *, long) { }

  void session_(ReqBase *request, const HTTPResponseMeta &meta) {
    if (request->kind() == ReqType::Initialize &&
	m_legacySessions && meta.sessionID)
      m_sessionID = meta.sessionID;
  }

  template <typename R, typename Call>
  void call_(
      ToolObject<typename R::Object> object, ID progress, int logLevel,
      ZmRef<Call> call) {
    if (!m_up.load_() || !m_peer.ready()) {
      try { call->failed(); } catch (...) { }
      return;
    }
    bool added = false;
    auto emit = [this, &call, &added](auto message) {
      ID id = message.id;
      if (!m_pending.template add<R>(id, call)) return;
      added = true;
      uint64_t sequence = 0;
      ReqBase *request = nullptr;
      if (!send_(ZuMv(message), &sequence, &request)) {
	(void)m_pending.fail(id);
	return;
      }
      m_active.add(ActiveEntry{id, sequence, request});
      app_([call, id]() { started_(call.ptr(), id, 0); });
    };
    if (progress.absent() && logLevel == LogLevel::Disabled)
      (void)m_peer.template call<R>(ZuMv(object), emit);
    else if (logLevel == LogLevel::Disabled)
      (void)m_peer.template callProgress<R>(
	ZuMv(object), ZuMv(progress), emit);
    else
      (void)m_peer.template callProgressLog<R>(
	ZuMv(object), ZuMv(progress), logLevel, emit);
    if (!added) {
      try { call->failed(); } catch (...) { }
    }
  }

  template <typename Call>
  void controlTx_(int kind, ZuCSpan level, ZmRef<Call> call) {
    if (!m_up.load_() || !m_peer.ready()) {
      try { call->failed(); } catch (...) { }
      return;
    }
    bool added = false;
    auto emit = [this, &call, &added](auto message) {
      ID id = message.id;
      if (!m_pending.add(id, call)) return;
      added = true;
      if (!send_(ZuMv(message))) {
	(void)m_pending.fail(id);
	return;
      }
      app_([call, id]() { started_(call.ptr(), id, 0); });
    };
    switch (kind) {
      case ClientControl::Ping: (void)m_peer.ping(emit); break;
      case ClientControl::SetLevel: (void)m_peer.setLevel(level, emit); break;
    }
    if (!added) {
      try { call->failed(); } catch (...) { }
    }
  }

  void ready_() {
    if (m_ready || !m_peer.ready() || m_current || m_drainingSerial ||
	m_serial.count_()) return;
    m_ready = true;
    try { m_impl->ready(m_peer.era()); } catch (...) { fail_(); }
  }

  bool cancel_(const ID &id, ZuCSpan reason) {
    auto entry = m_active.findPtr(id);
    if (!entry) return false;
    auto request = static_cast<ReqBase *>(entry->request);
    if (request) request->cancelled = true;
    if (request && request->serial && entry->sequence != m_current) {
      (void)m_active.del(id);
      (void)m_pending.fail(id);
      return true;
    }
    if (m_peer.era() == Era::Legacy) {
      if (!send_(CancelledRequestMessage{id, reason, m_peer.era()}))
	return false;
    }
    return Base::cancel(0, entry->sequence);
  }

  template <typename Call,
    typename = decltype(ZuDeclVal<Call * &>()->started(ZuDeclVal<const ID &>()), void())>
  static void started_(Call *call, const ID &id, int) {
    call->started(id);
  }
  template <typename Call>
  static void started_(Call *, const ID &, long) { }

  template <typename App,
    typename = decltype(ZuDeclVal<App * &>()->cancelled(ZuDeclVal<const ID &>(),
      ZuDeclVal<bool &>()), void())>
  static void cancelled_(App *app, const ID &id, bool ok, int) {
    app->cancelled(id, ok);
  }
  template <typename App>
  static void cancelled_(App *, const ID &, bool, long) { }

  template <typename App,
    typename = decltype(ZuDeclVal<App * &>()->terminated(ZuDeclVal<bool &>()), void())>
  static void terminated_(App *app, bool ok, int) {
    app->terminated(ok);
  }
  template <typename App>
  static void terminated_(App *, bool, long) { }

  void fail_() {
    if (m_failure || m_done.load_()) return;
    m_failure = true;
    m_up = false;
    m_peer.close();
  }

  void sendNext_() {
    if (!m_up.load_()) {
      m_drainingSerial = false;
      return;
    }
    m_drainingSerial = true;
    unsigned visited = 0;
    while (visited < m_limits.workBatch) {
      auto next = m_serial.shift();
      if (!next) {
	m_drainingSerial = false;
	ready_();
	return;
      }
      ++visited;
      auto request = ZuMv(next->data().request);
      if (request->cancelled) continue;
      if (m_peer.era() == Era::Legacy)
	request->ReqBase::sessionID = m_sessionID;
      m_drainingSerial = false;
      m_current = request->ReqBase::sequence;
      if (!Base::send(0, ZuMv(request))) fail_();
      return;
    }
    m_mx->run([this]() { sendNext_(); }, m_ownerThread);
  }

  void stopBatch_() {
    unsigned visited = 0;
    switch (m_stopPhase) {
      case 0:
	while (visited < m_limits.workBatch) {
	  auto entry = takeActive_();
	  if (!entry) {
	    m_stopPhase = 1;
	    break;
	  }
	  ++visited;
	}
	break;
      case 1:
	while (visited < m_limits.workBatch && m_serial.shift()) ++visited;
	if (!m_serial.count_()) m_stopPhase = 2;
	break;
      case 2:
	if (m_pending.close(m_limits.workBatch)) m_stopPhase = 3;
	break;
    }
    if (m_stopPhase < 3) {
      m_mx->run([this]() { stopBatch_(); }, m_ownerThread);
      return;
    }
    m_done = true;
    try {
      if (m_failure) m_impl->failed();
      else m_impl->closed();
    } catch (...) {
    }
    auto stopFn = ZuMv(m_stopFn);
    m_stopFn = {};
    if (stopFn) stopFn(!m_failure);
  }

  ActiveHash::NodeMvRef takeActive_() {
    auto i = m_active.iter();
    if (!i()) return decltype(i.del()){};
    return i.del();
  }

  Impl			*m_impl = nullptr;
  ZiMultiplex		*m_mx = nullptr;
  PendingCalls		m_pending;
  ActiveHash		m_active;
  ClientPeer<Catalog>	m_peer;
  SerialQ<Impl, Catalog> m_serial;
  ZmFn<void(bool)>	m_stopFn;
  HTTPValue		m_endpoint;
  HTTPValue		m_sessionID;
  Limits		m_limits;
  uint64_t		m_sequence = 1;
  uint64_t		m_current = 0;
  unsigned		m_ownerThread = 0;
  int			m_stopPhase = 0;
  bool			m_legacySessions = true;
  bool			m_started = false;
  bool			m_drainingSerial = false;
  bool			m_ready = false;
  bool			m_failure = false;

  ZmAtomic<unsigned>	m_up = 0;
  ZmAtomic<unsigned>	m_terminating = 0;
  ZmAtomic<unsigned>	m_done = 0;

  ZmAtomic<unsigned>	m_ingress = 0;
};

} // HTTPClient_

template <typename Impl, typename Catalog>
using HTTPClient = HTTPClient_::Client<Impl, Catalog>;

template <typename Req, typename Call, typename Heap = ZuVoid>
struct StdioCallAction_ : public Heap, public ZmObject {
  using Object = ToolObject<typename Req::Object>;

  StdioCallAction_(
      Object object_, ZmRef<Call> call_, ID progress_, int logLevel_) :
    object{ZuMv(object_)}, call{ZuMv(call_)}, progress{ZuMv(progress_)},
    logLevel{logLevel_} { }

  Object	object;
  ZmRef<Call>	call;
  ID		progress;
  int		logLevel;
};

template <typename Req, typename Call>
ZuDerive(StdioCallHeap,
  (ZmHeap<"Zmcp.Stdio.Call", StdioCallAction_<Req, Call>>));

template <typename Req, typename Call>
ZuDerive(StdioCallAction,
  (StdioCallAction_<Req, Call, StdioCallHeap<Req, Call>>));

template <typename Impl, typename Catalog>
class Client {
public:
  Client() = default;
  ~Client() { final(); }

  bool init(ZiMultiplex *mx, StdioConfig config, Impl *impl) {
    if (m_mx || !mx || !impl || !mx->txThread()) return false;
    m_impl = impl;
    m_mx = mx;
    m_ownerThread = mx->txThread();
    m_limits = config.limits();
    if (!m_limits.maxPending || !m_limits.workBatch) goto invalid;
    m_peer = ClientPeer<Catalog>{m_limits};
    if (!m_pending.init(m_limits.maxPending)) goto invalid;
    m_stdio = new StdioIOObj<Client>{
      this, m_mx, m_ownerThread, ZuMv(config)};
    m_up = true;
    return true;

  invalid:
    m_impl = nullptr;
    m_mx = nullptr;
    m_ownerThread = 0;
    return false;
  }

  bool start() {
    if (!m_mx || !m_stdio || !m_up.load_() || m_started || m_done.load_())
      return false;
    return ZmBlock<bool>{}([this](auto wake) mutable {
      m_mx->run([this, wake = ZuMv(wake)]() mutable {
	bool started = m_stdio->start_();
	bool ok = started;
	m_started = started;
	if (started) {
	  bool sent = false;
	  ok = m_peer.probe([this, &sent](const auto &message) {
	    sent = m_stdio->send_(message);
	  }) && sent;
	}
	if (!ok) {
	  m_failure = true;
	  m_up = false;
	  if (started) m_stdio->stop_();
	  else done_(true);
	}
	wake(ok);
      }, m_ownerThread);
    });
  }

  bool stop() {
    if (!m_mx) return false;
    if (m_done.load_()) return !m_failure;
    return ZmBlock<bool>{}([this](auto wake) mutable {
      m_mx->run([this, wake = ZuMv(wake)]() mutable {
	if (m_done.load_()) {
	  wake(!m_failure);
	  return;
	}
	m_stopFn = ZuMv(wake);
	m_up = false;
	if (m_started) m_stdio->stop_();
	else done_(false);
      }, m_ownerThread);
    });
  }

  void final() {
    if (!m_mx) return;
    (void)stop();
    m_stdio = nullptr;
    m_impl = nullptr;
    m_mx = nullptr;
    m_ownerThread = 0;
  }

  bool tools() {
    if (!m_mx || !m_up.load_()) return false;
    if (++m_ingress > m_limits.maxPending) {
      --m_ingress;
      return false;
    }
    bool posted = txRun([this]() {
      --m_ingress;
      if (const auto *catalog = m_peer.toolCatalog()) {
	app_([this, catalog]() { tools_(m_impl, catalog, 0); });
	return;
      }
      bool sent = true;
      ID id = m_peer.tools([this, &sent](const auto &message) {
	if (sent) sent = m_stdio->send_(message);
      });
      if ((id.absent() && !m_peer.toolCatalog()) || !sent)
	app_([this]() { toolsFailed_(m_impl, 0); });
    });
    if (!posted) --m_ingress;
    return posted;
  }

  template <typename Call>
  bool ping(ZmRef<Call> call) {
    return control_(ClientControl::Ping, {}, ZuMv(call));
  }

  template <typename Call>
  bool setLevel(ZuCSpan level, ZmRef<Call> call) {
    return control_(ClientControl::SetLevel, level, ZuMv(call));
  }

  bool cancel(ID id, ZuCSpan reason = {}) {
    if (!m_mx || !m_up.load_() || id.absent()) return false;
    if (++m_ingress > m_limits.maxPending) {
      --m_ingress;
      return false;
    }
    ZmRef<HTTPClient_::CancelAction> action =
      new HTTPClient_::CancelAction{ZuMv(id), reason};
    bool posted = txRun([this, action = ZuMv(action)]() mutable {
      --m_ingress;
      bool ok = m_pending.contains(action->id) &&
	m_stdio->send_(CancelledRequestMessage{
	  action->id, action->reason, m_peer.era()});
      app_([this, action, ok]() {
	cancelled_(m_impl, action->id, ok, 0);
      });
    });
    if (!posted) --m_ingress;
    return posted;
  }

  template <typename Req, typename Call>
  bool call(ToolObject<typename Req::Object> object, ZmRef<Call> call) {
    return callProgressLog<Req>(
      ZuMv(object), {}, LogLevel::Disabled, ZuMv(call));
  }

  template <typename Req, typename Call>
  bool callProgress(
      ToolObject<typename Req::Object> object,
      ID progress, ZmRef<Call> call) {
    return callProgressLog<Req>(
      ZuMv(object), ZuMv(progress), LogLevel::Disabled, ZuMv(call));
  }

  template <typename Req, typename Call>
  bool callLog(
      ToolObject<typename Req::Object> object,
      ZuCSpan level, ZmRef<Call> call) {
    return callProgressLog<Req>(
      ZuMv(object), {}, logLevel(level), ZuMv(call));
  }

  template <typename Req, typename Call>
  bool callProgressLog(
      ToolObject<typename Req::Object> object,
      ID progress, int logLevel,
      ZmRef<Call> call) {
    if (!m_mx || !m_up.load_() || !call) return false;
    if (++m_ingress > m_limits.maxPending) {
      --m_ingress;
      return false;
    }
    using Action = StdioCallAction<Req, Call>;
    ZmRef<Action> action = new Action{
      ZuMv(object), ZuMv(call), ZuMv(progress), logLevel};
    bool posted = txRun([this, action = ZuMv(action)]() mutable {
      --m_ingress;
      call_<Req>(ZuMv(action->object),
	ZuMv(action->progress), action->logLevel, ZuMv(action->call));
    });
    if (!posted) --m_ingress;
    return posted;
  }

  bool stdioFrame(ZmRef<ZiIOBuf> body) {
    ZiAssert(invoked_(), "Zmcp", (),
	"stdio client frame outside owner shard", return false);
    if (!m_up.load_() || !body) return false;
    bool sent = true;
    bool wasReady = m_peer.ready();
    const auto *catalog = m_peer.toolCatalog();
    auto emit = [this, &sent](const auto &message) {
      if (sent) sent = m_stdio->send_(message);
    };
    auto receive = [this](const Envelope<Catalog> &envelope) {
      return Client_::receive(m_impl, m_pending, envelope);
    };
    bool ok = m_peer.receive(
      ZuMv(body), emit, receive) && sent;
    if (!ok) {
      fail_();
      return false;
    }
    if (!wasReady && m_peer.ready()) {
      try { m_impl->ready(m_peer.era()); } catch (...) { fail_(); }
    }
    if (!catalog)
      if (const auto *catalog_ = m_peer.toolCatalog())
	app_([this, catalog_]() { tools_(m_impl, catalog_, 0); });
    return m_up.load_();
  }

  void stdioClosed() { done_(false); }
  void stdioFailed() { done_(true); }

  template <typename L>
  bool txRun(L &&l) {
    if (!m_mx || !m_up.load_()) return false;
    if (invoked_()) l();
    else m_mx->run(ZuFwd<L>(l), m_ownerThread);
    return true;
  }

  bool discardCatalog() {
    if (!m_mx || !m_up.load_()) return false;
    if (++m_ingress > m_limits.maxPending) {
      --m_ingress;
      return false;
    }
    bool posted = txRun([this]() {
      --m_ingress;
      m_peer.discardCatalog();
    });
    if (!posted) --m_ingress;
    return posted;
  }

private:
  template <typename Call>
  bool control_(int kind, ZuCSpan level, ZmRef<Call> call) {
    if (!m_mx || !m_up.load_() || !call) return false;
    if (++m_ingress > m_limits.maxPending) {
      --m_ingress;
      return false;
    }
    ZmRef<ControlAction<Call>> action =
      new ControlAction<Call>{kind, level, ZuMv(call)};
    bool posted = txRun([this, action = ZuMv(action)]() mutable {
      --m_ingress;
      controlTx_(action->kind, action->level, ZuMv(action->call));
    });
    if (!posted) --m_ingress;
    return posted;
  }

  bool invoked_() const {
    return m_mx && m_mx->invoked(m_ownerThread);
  }

  template <typename L>
  void app_(L &&l) {
    try {
      l();
    } catch (...) {
      fail_();
    }
  }

  template <typename Req, typename Call>
  void call_(
      ToolObject<typename Req::Object> object,
      ID progress, int logLevel,
      ZmRef<Call> call) {
    ZiAssert(invoked_(), "Zmcp", (),
	"stdio client call outside owner shard", return);
    if (!m_up.load_() || !m_peer.ready()) {
      try { call->failed(); } catch (...) { }
      return;
    }
    bool added = false;
    auto emit = [this, &call, &added](const auto &message) {
      if (!m_pending.template add<Req>(message.id, call)) return;
      added = true;
      if (!m_stdio->send_(message)) {
	(void)m_pending.fail(message.id);
	fail_();
	return;
      }
      app_([call, id = message.id]() {
	started_(call.ptr(), id, 0);
      });
    };
    if (progress.absent() && logLevel == LogLevel::Disabled)
      (void)m_peer.template call<Req>(ZuMv(object), emit);
    else if (logLevel == LogLevel::Disabled)
      (void)m_peer.template callProgress<Req>(
	ZuMv(object), ZuMv(progress), emit);
    else
      (void)m_peer.template callProgressLog<Req>(
	ZuMv(object), ZuMv(progress), logLevel, emit);
    if (!added) {
      try { call->failed(); } catch (...) { }
    }
  }

  template <typename Call>
  void controlTx_(int kind, ZuCSpan level, ZmRef<Call> call) {
    ZiAssert(invoked_(), "Zmcp", (),
	"stdio client control outside owner shard", return);
    if (!m_up.load_() || !m_peer.ready()) {
      try { call->failed(); } catch (...) { }
      return;
    }
    bool added = false;
    auto emit = [this, &call, &added](const auto &message) {
      if (!m_pending.add(message.id, call)) return;
      added = true;
      if (!m_stdio->send_(message)) {
	(void)m_pending.fail(message.id);
	fail_();
	return;
      }
      app_([call, id = message.id]() {
	started_(call.ptr(), id, 0);
      });
    };
    switch (kind) {
      case ClientControl::Ping: (void)m_peer.ping(emit); break;
      case ClientControl::SetLevel: (void)m_peer.setLevel(level, emit); break;
    }
    if (!added) {
      try { call->failed(); } catch (...) { }
    }
  }

  void fail_() {
    ZiAssert(invoked_(), "Zmcp", (),
	"stdio client failure outside owner shard", return);
    if (m_done.load_() || m_failure) return;
    m_failure = true;
    m_up = false;
    m_peer.close();
    m_stdio->stop_();
  }

  template <typename Call,
    typename = decltype(ZuDeclVal<Call * &>()->started(ZuDeclVal<const ID &>()), void())>
  static void started_(Call *call, const ID &id, int) {
    call->started(id);
  }
  template <typename Call>
  static void started_(Call *, const ID &, long) { }

  template <typename App,
    typename = decltype(ZuDeclVal<App * &>()->tools(ZuDeclVal<const ZfJSON::AnyNode * &>()), void())>
  static void tools_(App *app, const ZfJSON::AnyNode *catalog, int) {
    app->tools(catalog);
  }
  template <typename App>
  static void tools_(App *, const ZfJSON::AnyNode *, long) { }

  template <typename App,
    typename = decltype(ZuDeclVal<App * &>()->toolsFailed(), void())>
  static void toolsFailed_(App *app, int) {
    app->toolsFailed();
  }
  template <typename App>
  static void toolsFailed_(App *, long) { }

  template <typename App,
    typename = decltype(ZuDeclVal<App * &>()->cancelled(ZuDeclVal<const ID &>(),
      ZuDeclVal<bool &>()), void())>
  static void cancelled_(App *app, const ID &id, bool ok, int) {
    app->cancelled(id, ok);
  }
  template <typename App>
  static void cancelled_(App *, const ID &, bool, long) { }

  void done_(bool failed) {
    ZiAssert(invoked_(), "Zmcp", (),
	"stdio client completion outside owner shard", return);
    if (m_done.load_()) return;
    m_failure |= failed;
    m_up = false;
    m_peer.close();
    drain_();
  }

  void drain_() {
    if (!m_pending.close(m_limits.workBatch)) {
      m_mx->run([this]() { drain_(); }, m_ownerThread);
      return;
    }
    m_done = true;
    try {
      if (m_failure) m_impl->failed();
      else m_impl->closed();
    } catch (...) {
    }
    auto stopFn = ZuMv(m_stopFn);
    m_stopFn = {};
    if (stopFn) stopFn(!m_failure);
  }

  Impl			*m_impl = nullptr;
  ZiMultiplex		*m_mx = nullptr;
  ZuPtr<StdioIOObj<Client>> m_stdio;
  PendingCalls		m_pending;
  ClientPeer<Catalog>	m_peer;
  ZmFn<void(bool)>	m_stopFn;
  Limits		m_limits;
  unsigned		m_ownerThread = 0;
  bool			m_started = false;
  bool			m_failure = false;

  ZmAtomic<unsigned>	m_up = 0;
  ZmAtomic<unsigned>	m_done = 0;

  ZmAtomic<unsigned>	m_ingress = 0;
};

} // Zmcp

#endif /* ZmcpClient_HH */
