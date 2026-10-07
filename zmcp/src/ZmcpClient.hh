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
#include <zlib/ZjrpcPending.hh>
#include <zlib/ZjrpcDispatch.hh>
#include <zlib/ZjrpcWS.hh>
#include <zlib/ZjrpcStdio.hh>

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
  const Zjrpc::Limits &limits() const { return m_limits; }

  ClientConfig &limits(Zjrpc::Limits v) { m_limits = ZuMv(v); return *this; }

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
  Zjrpc::HTTPValue	m_endpoint{"/mcp"};
  Zjrpc::Limits	m_limits;
  bool		m_legacySessions = true;
};

template <typename Message>
struct HTTPRequestBuilder : public Zjrpc::HTTPRequestBuilder<Message> {
  using Base = Zjrpc::HTTPRequestBuilder<Message>;
  using Headers = ZuTypeConcat<ZhttpHeaders(
    "content-type", "accept", "content-length"), RoutingHeaders>;
  using ContentType = ZuStringT<"content-type">;
  using Accept = ZuStringT<"accept">;
  using ContentLength = ZuStringT<"content-length">;
  using Version = ProtocolVersion;
  using Session = SessionID;
  using Method = MethodHeader;
  using Name = NameHeader;
  using Base::header;
  using Base::message;
  using Base::endpoint;
  Zjrpc::HTTPValue sessionID;
  int era = Era::Modern;

  Zhttp::BodyPolicy::T bodyPolicy() const {
    return empty_(message, 0) ?
      Zhttp::BodyPolicy::None : Zhttp::BodyPolicy::Fixed;
  }

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
      auto method = message.method();
      if (era == Era::Modern && method) l(method);
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
    Base::body(ZuFwd<Emit>(emit));
  }

  template <typename L>
  void bodyHdrs(L &&l) const {
    if (empty_(message, 0)) return;
    Base::bodyHdrs(ZuFwd<L>(l));
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
  Zjrpc::ID id;
  int era = Era::Modern;

  static ZuCSpan method() { return "server/discover"; }
  static ZuCSpan name() { return {}; }

  template <typename S>
  void write(S &out) const {
    Zjrpc::saveRequest(out, id, method(), MetaParams{clientMeta(era)});
  }
};

struct InitializeRequestMessage {
  Zjrpc::ID id;

  static ZuCSpan method() { return "initialize"; }
  static ZuCSpan name() { return {}; }

  template <typename S>
  void write(S &out) const {
    Zjrpc::saveRequest(out, id, method(), InitializeParams{
      LegacyVersion{}(), {}, {"zmcp", Z_VERNAME}});
  }
};

struct InitializedMessage {
  static ZuCSpan method() { return "notifications/initialized"; }
  static ZuCSpan name() { return {}; }

  template <typename S>
  void write(S &out) const {
    Zjrpc::saveNotification(out, method(), Zjrpc::EmptyObject{});
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
  Zjrpc::ID id;
  int era = Era::Unknown;

  static ZuCSpan method() { return "ping"; }
  static ZuCSpan name() { return {}; }

  template <typename S>
  void write(S &out) const {
    Zjrpc::saveRequest(out, id, method(), MetaParams{clientMeta(era)});
  }
};

struct SetLevelRequestMessage {
  Zjrpc::ID id;
  Zjrpc::ErrorString level;

  static ZuCSpan method() { return "logging/setLevel"; }
  static ZuCSpan name() { return {}; }

  template <typename S>
  void write(S &out) const {
    Zjrpc::saveRequest(out, id, method(), SetLevelParams{level});
  }
};

struct CancelledRequestMessage {
  Zjrpc::ID requestID;
  Zjrpc::ErrorString reason;
  int era = Era::Unknown;

  static ZuCSpan method() { return "notifications/cancelled"; }
  static ZuCSpan name() { return {}; }

  template <typename S>
  void write(S &out) const {
    Zjrpc::saveNotification(out, method(), CancelledParams{
      requestID, reason, clientMeta(era)});
  }
};

struct ToolsListRequestMessage {
  Zjrpc::ID id;
  int era = Era::Unknown;

  static ZuCSpan method() { return "tools/list"; }
  static ZuCSpan name() { return {}; }

  template <typename S>
  void write(S &out) const {
    Zjrpc::saveRequest(out, id, method(), MetaParams{clientMeta(era)});
  }
};

template <typename Reqs>
struct ToolCallRequestMessage {
  Zjrpc::ID id;
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
	out = Req::ResponseBody == Zjrpc::BodyPolicy::SSE;
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
    Zjrpc::saveRequest(out, id, method(), params);
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

template <typename Req>
struct BatchRequest : public ToolCallRequestMessage<ZuTypeList<Req>> {
  using Base = ToolCallRequestMessage<ZuTypeList<Req>>;
  BatchRequest(Zjrpc::ID id, Zjrpc::ObjectValue<typename Req::Object> object,
      int era = Era::Modern, Zjrpc::ID progress = {}, int level = LogLevel::Disabled) :
    Base{ZuMv(id), {}} {
    this->params.arguments() = ToolArg<Req>{ZuMv(object)};
    this->params.era = era;
    this->params.progressToken = ZuMv(progress);
    this->params.logLevel = level;
  }
  template <typename S>
  void write(S &out) const {
    if (this->id.template is<void>())
      Zjrpc::saveNotification(out, Base::method(), this->params);
    else Base::write(out);
  }
};

template <typename Catalog>
using Batch = Zjrpc::Batch<Catalog, BatchRequest>;

namespace ClientState {
  enum { Fresh, Discovering, Initializing, Ready, Closed };
}

struct ToolReplyDecode {
  template <typename Req, typename Call>
  static bool process(Call &call, const ZfJSON::AnyNode *node) {
    auto reply = loadToolReply<Req>(node);
    if (!reply.type()) return false;
    reply.dispatch([&call](auto, const auto &value) { call.process(value); });
    return true;
  }
};

namespace Client_ {

template <typename App,
  typename = decltype(ZuDeclVal<App * &>()->progress(ZuDeclVal<const Zjrpc::ID &>(),
    ZuDeclVal<double &>(), ZuDeclVal<double &>(), ZuDeclVal<ZuCSpan &>()), void())>
void progress(
    App *app, const Zjrpc::ID &token, double value, double total,
    ZuCSpan message, int)
{
  app->progress(token, value, total, message);
}
template <typename App>
void progress(App *, const Zjrpc::ID &, double, double, ZuCSpan, long) { }

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

template <typename App>
bool receive(
    App *app, Zjrpc::PendingCalls &pending, const Zjrpc::Envelope &envelope)
{
  if (envelope.kind != Zjrpc::MessageKind::Notification &&
      envelope.kind != Zjrpc::MessageKind::Request)
    return pending.receive(envelope);
  try {
    if (envelope.method() == "notifications/progress") {
      auto params = Zjrpc::loadObject<ProgressParams>(Zjrpc::raw(envelope.params()));
      progress(app, params.token, params.progress, params.total,
	params.message, 0);
    } else if (envelope.method() == "notifications/message") {
      auto node = Zjrpc::raw(envelope.params());
      auto params = Zjrpc::loadObject<RxLogParams>(node);
      logging(app, params.level, Zjrpc::member(node, "data"), params.logger, 0);
    }
  } catch (...) {
    return false;
  }
  return true;
}

struct ReceivePolicy : public Zjrpc::DispatchPolicy<ZuTypeList<>> {
  template <typename Dispatch, typename Work, typename Emit, typename Tool>
  static bool dispatch(Dispatch &dispatcher, const Zjrpc::Envelope &envelope,
      Work &, Emit &, Tool &) {
    return dispatcher.owner()->notification(envelope);
  }
};

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
  ClientPeer(Zjrpc::Limits limits) : m_limits{limits} { }

  int state() const { return m_state; }
  int era() const { return m_era; }
  bool ready() const { return m_state == ClientState::Ready; }
  const ZfJSON::AnyNode *toolCatalog() const { return m_cache.result(); }

  void discardCatalog() {
    m_toolsID = Zjrpc::ID{};
    m_cache.clear();
  }

  void close() { m_state = ClientState::Closed; }

  template <typename Emit>
  bool reinitialize(Emit &&emit) {
    if (m_state != ClientState::Ready || m_era != Era::Legacy) return false;
    discardCatalog();
    m_initializeID = next_();
    if (m_initializeID.is<void>()) return false;
    m_state = ClientState::Initializing;
    emit(InitializeRequestMessage{m_initializeID});
    return true;
  }

  template <typename Emit>
  bool probe(Emit &&emit) {
    if (m_state != ClientState::Fresh) return false;
    m_probeID = next_();
    if (m_probeID.is<void>()) return false;
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
    auto received = [](const Zjrpc::Envelope &) { return true; };
    return receive_(input, nullptr, ZuFwd<Emit>(emit), received);
  }

  template <typename Emit>
  bool receive(ZmRef<ZiIOBuf> input, Emit &&emit) {
    if (!input) return false;
    auto span = ZuSpan<char>{input->span()};
    auto received = [](const Zjrpc::Envelope &) { return true; };
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

  template <typename Emit, typename Receive>
  bool receive(ZmRef<ZiIOBuf> input, Zjrpc::Parsed parsed, Emit &&emit, Receive &&receive) {
    if (!input || m_state == ClientState::Closed) return false;
    if (!parsed) { close(); return false; }
    return parsed_(ZuMv(parsed), &input, ZuFwd<Emit>(emit), ZuFwd<Receive>(receive));
  }

  template <typename Emit>
  Zjrpc::ID tools(Emit &&emit) {
    if (!ready()) return {};
    if (m_cache) return {};
    if (!m_toolsID.is<void>()) return m_toolsID;
    m_toolsID = next_();
    if (!m_toolsID.is<void>())
      emit(ToolsListRequestMessage{m_toolsID, m_era});
    return m_toolsID;
  }

  template <typename Req, typename Emit>
  Zjrpc::ID call(Zjrpc::ObjectValue<typename Req::Object> object, Emit &&emit) {
    return call_<Req>(
      ZuMv(object), {}, LogLevel::Disabled, ZuFwd<Emit>(emit));
  }

  template <typename Req, typename Emit>
  Zjrpc::ID callProgress(
      Zjrpc::ObjectValue<typename Req::Object> object,
      Zjrpc::ID progressToken, Emit &&emit) {
    return call_<Req>(
      ZuMv(object), ZuMv(progressToken), LogLevel::Disabled,
      ZuFwd<Emit>(emit));
  }

  template <typename Req, typename Emit>
  Zjrpc::ID callLog(
      Zjrpc::ObjectValue<typename Req::Object> object,
      int logLevel, Emit &&emit) {
    return call_<Req>(ZuMv(object), {}, logLevel, ZuFwd<Emit>(emit));
  }

  template <typename Req, typename Emit>
  Zjrpc::ID callProgressLog(
      Zjrpc::ObjectValue<typename Req::Object> object,
      Zjrpc::ID progressToken, int logLevel,
      Emit &&emit) {
    return call_<Req>(ZuMv(object), ZuMv(progressToken), logLevel,
      ZuFwd<Emit>(emit));
  }

  template <typename Emit>
  Zjrpc::ID ping(Emit &&emit) {
    if (!ready()) return {};
    Zjrpc::ID id = next_();
    if (!id.is<void>()) emit(PingRequestMessage{id, m_era});
    return id;
  }

  template <typename Emit>
  Zjrpc::ID setLevel(ZuCSpan level, Emit &&emit) {
    if (!ready() || m_era != Era::Legacy) return {};
    Zjrpc::ID id = next_();
    if (!id.is<void>())
      emit(SetLevelRequestMessage{id, Zjrpc::ErrorString{level}});
    return id;
  }

private:
  template <typename Req, typename Emit>
  Zjrpc::ID call_(
      Zjrpc::ObjectValue<typename Req::Object> object,
      Zjrpc::ID progressToken, int logLevel,
      Emit &&emit) {
    ZuAssert((ZuTypeIn<Req, Reqs>{}));
    if (!ready()) return {};
    Zjrpc::ID id = next_();
    if (id.is<void>()) return id;
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
    auto parsed = Zjrpc::parse(input, m_limits.maxJSONBytes);
    if (!parsed) {
      close();
      return false;
    }
    return parsed_(ZuMv(parsed), owner, ZuFwd<Emit>(emit), ZuFwd<Receive>(receive));
  }

  template <typename Emit, typename Receive>
  bool parsed_(Zjrpc::Parsed parsed, ZmRef<ZiIOBuf> *owner, Emit &&emit, Receive &&receive) {
    const auto &envelope = parsed.envelope;
    if (m_state == ClientState::Discovering && envelope.id() == m_probeID &&
	(envelope.kind == Zjrpc::MessageKind::Result ||
	  envelope.kind == Zjrpc::MessageKind::Error)) {
      if (envelope.kind == Zjrpc::MessageKind::Result &&
	  supportsModern_(Zjrpc::raw(envelope.result()))) {
	m_era = Era::Modern;
	m_state = ClientState::Ready;
	return true;
      }
      return initialize_(ZuFwd<Emit>(emit));
    }
    if (m_state == ClientState::Initializing &&
	envelope.id() == m_initializeID) {
      if (envelope.kind != Zjrpc::MessageKind::Result) {
	close();
	return false;
      }
      m_era = Era::Legacy;
      m_state = ClientState::Ready;
      emit(InitializedMessage{});
      return true;
    }
    if (!m_toolsID.is<void>() && envelope.id() == m_toolsID &&
	(envelope.kind == Zjrpc::MessageKind::Result ||
	  envelope.kind == Zjrpc::MessageKind::Error)) {
      if (envelope.kind == Zjrpc::MessageKind::Result && owner) {
	m_cache.store(
	  ZuMv(*owner), ZuMv(parsed.root), Zjrpc::raw(envelope.result()));
	return true;
      }
      m_toolsID = Zjrpc::ID{};
    }
    return receive(envelope);
  }
  static bool supportsModern_(const ZfJSON::AnyNode *result) {
    auto discover = Zjrpc::loadObject<DiscoverRxResult>(result);
    for (unsigned i = 0, n = discover.supportedVersions.length(); i < n; ++i)
      if (discover.supportedVersions[i] == ModernVersion{}()) return true;
    return false;
  }

  template <typename Emit>
  bool initialize_(Emit &&emit) {
    m_initializeID = next_();
    if (m_initializeID.is<void>()) return false;
    m_state = ClientState::Initializing;
    emit(InitializeRequestMessage{m_initializeID});
    return true;
  }

  Zjrpc::ID next_() {
    if (ZuUnlikely(m_nextID == INT64_MAX)) {
      close();
      return {};
    }
    return m_nextID++;
  }

  Zjrpc::ID		m_probeID;
  Zjrpc::ID		m_initializeID;
  Zjrpc::ID		m_toolsID;
  CatalogCache	m_cache;
  int64_t	m_nextID = 1;
  Zjrpc::Limits	m_limits;
  int		m_state = ClientState::Fresh;
  int		m_era = Era::Unknown;
};

namespace ClientControl {
  enum { Ping, SetLevel };
}

template <typename Call, typename Heap = ZuVoid>
struct ControlAction_ : public Heap, public ZmObject {
  Zjrpc::ErrorString	level;
  ZmRef<Call>	call;
  int		kind;

  ControlAction_(int kind_, ZuCSpan level_, ZmRef<Call> call_) :
    level{level_}, call{ZuMv(call_)}, kind{kind_} { }
};

template <typename Call>
ZuDerive(ControlHeap, (ZmHeap<"Zmcp.Client.Control", ControlAction_<Call>>));

template <typename Call>
ZuDerive(ControlAction, (ControlAction_<Call, ControlHeap<Call>>));

template <typename Req, typename Call, typename Heap = ZuVoid>
struct PeerCallAction_ : public Heap, public ZmObject {
  using Object = Zjrpc::ObjectValue<typename Req::Object>;

  Object		object;
  ZmRef<Call>	call;
  Zjrpc::ID	progress;
  int		logLevel;

  PeerCallAction_(
      Object object_, ZmRef<Call> call_, Zjrpc::ID progress_, int logLevel_) :
    object{ZuMv(object_)}, call{ZuMv(call_)}, progress{ZuMv(progress_)},
    logLevel{logLevel_} { }

};

template <typename Req, typename Call>
ZuDerive(PeerCallHeap,
  (ZmHeap<"Zmcp.Client.Call", PeerCallAction_<Req, Call>>));

template <typename Req, typename Call>
ZuDerive(PeerCallAction,
  (PeerCallAction_<Req, Call, PeerCallHeap<Req, Call>>));

template <typename Heap = ZuVoid>
struct CancelAction_ : public Heap, public ZmObject {
  Zjrpc::ID		id;
  Zjrpc::ErrorString	reason;
  CancelAction_(Zjrpc::ID id_, ZuCSpan reason_) :
    id{ZuMv(id_)}, reason{reason_} { }
};
ZuDerive(CancelActionHeap, (ZmHeap<"Zmcp.Client.Cancel", CancelAction_<>>));
ZuDerive(CancelAction, (CancelAction_<CancelActionHeap>));

template <typename Derived, typename Impl, typename Catalog>
class Client : public Zjrpc::BatchCaller<Derived>,
    public Zjrpc::Dispatcher<Derived, Impl, ZuTypeList<>, ZuVoid, Client_::ReceivePolicy> {
  friend Derived;
  using Calls = Zjrpc::BatchCaller<Derived>;
  using Dispatch = Zjrpc::Dispatcher<Derived, Impl, ZuTypeList<>, ZuVoid, Client_::ReceivePolicy>;
protected:
  using Calls::pendingCalls;
  using Calls::ingress;
public:
  Impl *impl() const { return m_impl; }
  bool notification(const Zjrpc::Envelope &envelope) {
    return Client_::receive(m_impl, pendingCalls(), envelope);
  }
  bool up() const { return m_up.load_(); }
  const Zjrpc::Limits &limits() const { return m_limits; }
  bool invoked() const { return m_mx && m_mx->invoked(m_ownerThread); }
  template <typename L>
  void continue_(L &&l) { m_mx->run(ZuFwd<L>(l), m_ownerThread); }
  bool send(Zjrpc::BatchWire message) {
    return m_peer.ready() && static_cast<Derived *>(this)->send_(ZuMv(message));
  }
  bool prepareBatch(Batch<Catalog> &batch) {
    if (!m_peer.ready()) return false;
    for (auto &item : batch.items()) item.dispatch([this](auto, auto &message) {
      if constexpr (!ZuIsSame<ZuDecay<decltype(message)>, Zjrpc::BatchWire>{})
	message.params.era = m_peer.era();
    });
    return true;
  }

  bool tools() {
    if (!m_mx || !m_up.load_()) return false;
    if (++ingress() > m_limits.maxPending) {
      --ingress();
      return false;
    }
    bool posted = ownerRun([this]() {
      --ingress();
      if (const auto *catalog = m_peer.toolCatalog()) {
	app_([this, catalog]() { tools_(m_impl, catalog, 0); });
	return;
      }
      bool sent = true;
      Zjrpc::ID id = m_peer.tools([this, &sent](const auto &message) {
	if (sent) sent = static_cast<Derived *>(this)->send_(message);
      });
      if ((id.is<void>() && !m_peer.toolCatalog()) || !sent)
	app_([this]() { toolsFailed_(m_impl, 0); });
    });
    if (!posted) --ingress();
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

  bool cancel(Zjrpc::ID id, ZuCSpan reason = {}) {
    if (!m_mx || !m_up.load_() || id.is<void>()) return false;
    if (++ingress() > m_limits.maxPending) {
      --ingress();
      return false;
    }
    ZmRef<CancelAction> action =
      new CancelAction{ZuMv(id), reason};
    bool posted = ownerRun([this, action = ZuMv(action)]() mutable {
      --ingress();
      bool ok = pendingCalls().contains(action->id) &&
	static_cast<Derived *>(this)->send_(CancelledRequestMessage{
	  action->id, action->reason, m_peer.era()});
      app_([this, action, ok]() {
	cancelled_(m_impl, action->id, ok, 0);
      });
    });
    if (!posted) --ingress();
    return posted;
  }

  template <typename Req, typename Call>
  bool call(Zjrpc::ObjectValue<typename Req::Object> object, ZmRef<Call> call) {
    return callProgressLog<Req>(
      ZuMv(object), {}, LogLevel::Disabled, ZuMv(call));
  }

  template <typename Req, typename Call>
  bool callProgress(
      Zjrpc::ObjectValue<typename Req::Object> object,
      Zjrpc::ID progress, ZmRef<Call> call) {
    return callProgressLog<Req>(
      ZuMv(object), ZuMv(progress), LogLevel::Disabled, ZuMv(call));
  }

  template <typename Req, typename Call>
  bool callLog(
      Zjrpc::ObjectValue<typename Req::Object> object,
      ZuCSpan level, ZmRef<Call> call) {
    return callProgressLog<Req>(
      ZuMv(object), {}, logLevel(level), ZuMv(call));
  }

  template <typename Req, typename Call>
  bool callProgressLog(
      Zjrpc::ObjectValue<typename Req::Object> object,
      Zjrpc::ID progress, int logLevel,
      ZmRef<Call> call) {
    if (!m_mx || !m_up.load_() || !call) return false;
    if (++ingress() > m_limits.maxPending) {
      --ingress();
      return false;
    }
    using Action = PeerCallAction<Req, Call>;
    ZmRef<Action> action = new Action{
      ZuMv(object), ZuMv(call), ZuMv(progress), logLevel};
    bool posted = ownerRun([this, action = ZuMv(action)]() mutable {
      --ingress();
      call_<Req>(ZuMv(action->object),
	ZuMv(action->progress), action->logLevel, ZuMv(action->call));
    });
    if (!posted) --ingress();
    return posted;
  }

  template <typename L>
  bool ownerRun(L &&l) {
    if (!m_mx || !m_up.load_()) return false;
    if (invoked()) l();
    else m_mx->run(ZuFwd<L>(l), m_ownerThread);
    return true;
  }

  bool discardCatalog() {
    if (!m_mx || !m_up.load_()) return false;
    if (++ingress() > m_limits.maxPending) {
      --ingress();
      return false;
    }
    bool posted = ownerRun([this]() {
      --ingress();
      m_peer.discardCatalog();
    });
    if (!posted) --ingress();
    return posted;
  }

protected:
  bool receive(ZmRef<ZiIOBuf> body) {
    ZiAssert(invoked(), "Zmcp", (),
	"client frame outside owner shard", return false);
    if (!m_up.load_() || !body) return false;
    bool sent = true;
    bool wasReady = m_peer.ready();
    const auto *catalog = m_peer.toolCatalog();
    auto emit = [this, &sent](const auto &message) {
      if (sent) sent = static_cast<Derived *>(this)->send_(message);
    };
    bool ok = receiveWire_(ZuMv(body), emit) && sent;
    if (!ok) {
      static_cast<Derived *>(this)->fail_();
      return false;
    }
    if (!wasReady && m_peer.ready()) {
      try { m_impl->ready(m_peer.era()); } catch (...) { static_cast<Derived *>(this)->fail_(); }
    }
    if (!catalog)
      if (const auto *catalog_ = m_peer.toolCatalog())
	app_([this, catalog_]() { tools_(m_impl, catalog_, 0); });
    return m_up.load_();
  }


private:
  template <typename Call>
  bool control_(int kind, ZuCSpan level, ZmRef<Call> call) {
    if (!m_mx || !m_up.load_() || !call) return false;
    if (++ingress() > m_limits.maxPending) {
      --ingress();
      return false;
    }
    ZmRef<ControlAction<Call>> action =
      new ControlAction<Call>{kind, level, ZuMv(call)};
    bool posted = ownerRun([this, action = ZuMv(action)]() mutable {
      --ingress();
      controlTx_(action->kind, action->level, ZuMv(action->call));
    });
    if (!posted) --ingress();
    return posted;
  }

  template <typename Req, typename Call>
  void call_(
      Zjrpc::ObjectValue<typename Req::Object> object,
      Zjrpc::ID progress, int logLevel,
      ZmRef<Call> call) {
    ZiAssert(invoked(), "Zmcp", (),
	"client call outside owner shard", return);
    if (!m_up.load_() || !m_peer.ready()) {
      try { call->failed(); } catch (...) { }
      return;
    }
    bool added = false;
    auto emit = [this, &call, &added](const auto &message) {
      if (!pendingCalls().template add<Req, ToolReplyDecode>(message.id, call)) return;
      added = true;
      if (!static_cast<Derived *>(this)->send_(message)) {
	(void)pendingCalls().fail(message.id);
	static_cast<Derived *>(this)->fail_();
	return;
      }
      app_([call, id = message.id]() {
	started_(call.ptr(), id, 0);
      });
    };
    if (progress.is<void>() && logLevel == LogLevel::Disabled)
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
    ZiAssert(invoked(), "Zmcp", (),
	"client control outside owner shard", return);
    if (!m_up.load_() || !m_peer.ready()) {
      try { call->failed(); } catch (...) { }
      return;
    }
    bool added = false;
    auto emit = [this, &call, &added](const auto &message) {
      if (!pendingCalls().add(message.id, call)) return;
      added = true;
      if (!static_cast<Derived *>(this)->send_(message)) {
	(void)pendingCalls().fail(message.id);
	static_cast<Derived *>(this)->fail_();
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

  template <typename Call,
    typename = decltype(ZuDeclVal<Call * &>()->started(ZuDeclVal<const Zjrpc::ID &>()), void())>
  static void started_(Call *call, const Zjrpc::ID &id, int) {
    call->started(id);
  }
  template <typename Call>
  static void started_(Call *, const Zjrpc::ID &, long) { }

  template <typename App,
    typename = decltype(ZuDeclVal<App * &>()->tools(
      ZuDeclVal<const ZfJSON::AnyNode * &>()), void())>
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
    typename = decltype(ZuDeclVal<App * &>()->cancelled(ZuDeclVal<const Zjrpc::ID &>(),
      ZuDeclVal<bool &>()), void())>
  static void cancelled_(App *app, const Zjrpc::ID &id, bool ok, int) {
    app->cancelled(id, ok);
  }
  template <typename App>
  static void cancelled_(App *, const Zjrpc::ID &, bool, long) { }

protected:
  template <typename L>
  void app_(L &&l) {
    try {
      l();
    } catch (...) {
      static_cast<Derived *>(this)->fail_();
    }
  }

  template <typename Emit>
  bool receiveWire_(ZmRef<ZiIOBuf> body, Emit &&emit) {
    if (!body || !up()) return false;
    auto parsed = Zjrpc::parse(body->span(), m_limits.maxJSONBytes);
    if (!parsed) { m_peer.close(); return false; }
    if (parsed.batch()) return Dispatch::receive(ZuMv(body), ZuMv(parsed),
      [](auto message, int policy) {
	return message.empty() && policy != Zjrpc::RoutePolicy::Abort;
      }, nullptr);
    return m_peer.receive(ZuMv(body), ZuMv(parsed), ZuFwd<Emit>(emit),
      [this](const Zjrpc::Envelope &envelope) { return notification(envelope); });
  }
  bool closeInput_() { return Dispatch::close(); }

  bool initPeer_(Zjrpc::Limits limits) {
    m_limits = limits;
    m_peer = ClientPeer<Catalog>{limits};
    return pendingCalls().init(limits.maxPending);
  }

  bool closePeer_() {
    m_up = false;
    m_peer.close();
    if (ingress().load_()) return false;
    bool done = closeInput_();
    done = pendingCalls().close(m_limits.workBatch) && done;
    return done && !ingress().load_();
  }

private:
  Impl			*m_impl = nullptr;
  ZiMultiplex		*m_mx = nullptr;
  ClientPeer<Catalog>	m_peer;
  Zjrpc::Limits		m_limits;
  unsigned		m_ownerThread = 0;
  ZmAtomic<unsigned>	m_up = 0;
  ZmAtomic<unsigned>	m_done = 0;
};

namespace HTTPClient_ {

namespace ReqType {
  // Values are the Message::Data union indexes.
  enum {
    None, Discover, Initialize, Initialized, Ping, SetLevel, ToolsList, ToolCall,
    Cancelled, Delete, Batch
  };
}

template <typename Catalog>
class Message {
  using Data = ZuUnion<void,
    DiscoverRequestMessage, InitializeRequestMessage, InitializedMessage,
    PingRequestMessage, SetLevelRequestMessage, ToolsListRequestMessage,
    ToolCallRequestMessage<Catalog>, CancelledRequestMessage,
    DeleteRequestMessage, Zjrpc::BatchWire>;

public:
  template <typename M>
  void init(M message) {
    m_data.template p<M>(ZuMv(message));
  }

  int kind() const { return m_data.type(); }

  const Zjrpc::ID *id() const {
    const Zjrpc::ID *out = nullptr;
    m_data.cdispatch([&out](auto I, const auto &message) {
      using M = typename Data::template Type<I>;
      if constexpr (!ZuIsSame<M, InitializedMessage>{} &&
	  !ZuIsSame<M, CancelledRequestMessage>{} &&
	  !ZuIsSame<M, DeleteRequestMessage>{})
	out = &message.id;
    });
    return out;
  }

  ZuCSpan method() const {
    ZuCSpan out;
    m_data.cdispatch([&out](auto I, const auto &message) {
      using M = typename Data::template Type<I>;
      if constexpr (!ZuIsSame<M, Zjrpc::BatchWire>{}) out = message.method();
    });
    return out;
  }

  ZuCSpan name() const {
    ZuCSpan out;
    m_data.cdispatch([&out](auto I, const auto &message) {
      using M = typename Data::template Type<I>;
      if constexpr (!ZuIsSame<M, Zjrpc::BatchWire>{}) out = message.name();
    });
    return out;
  }

  bool empty() const { return kind() == ReqType::Delete; }

  bool streaming() const {
    if (kind() == ReqType::Batch) return true;
    bool out = false;
    m_data.cdispatch([&out](auto I, const auto &message) {
      using M = typename Data::template Type<I>;
      if constexpr (ZuIsSame<M, ToolCallRequestMessage<Catalog>>{})
	out = message.streaming();
    });
    return out;
  }

  bool idempotent() const {
    switch (kind()) {
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
    m_data.cdispatch([&l](auto I, const auto &message) {
      using M = typename Data::template Type<I>;
      if constexpr (ZuIsSame<M, ToolCallRequestMessage<Catalog>>{})
	message.header(l);
    });
  }

private:
  Data	m_data;
};

template <typename Impl, typename Catalog> class Client;

// The serial queue owns the same request allocation later used by RequestQ.
// Its inner node has no allocator; two links avoid a separate wrapper/ref node
// for queued legacy/discovery requests. Both memberships share one refcount.
using SerialQ = ZmList<ZmObject,
  ZmListNode<ZmObject, ZmListHeapID<"">>>;

// Stack the active-ID hash on the serial link, sharing the request's refcount.
// The message owns its ID; this inner node and its allocator never own a copy.
struct ActiveEntry { const Zjrpc::ID *id = nullptr; };
inline const Zjrpc::ID &ActiveEntry_KeyAxor(const ActiveEntry &entry) {
  return *entry.id;
}
ZmHashDerive(ActiveHash, ActiveEntry,
  (ZmHashNode<SerialQ::Node,
    ZmHashKey<ActiveEntry_KeyAxor,
      ZmHashLock<ZmNoLock, ZmHashHeapID<"">>>>));

template <typename Heap = ZuVoid>
struct ActiveTable_ : public Heap, public ActiveHash {
  ZuDerive_(ActiveTable_, ActiveHash)
};
ZuDerive(ActiveTableHeap, (ZmHeap<"Zmcp.HTTP.ActiveTable", ActiveTable_<>>));
ZuDerive(ActiveTable, (ActiveTable_<ActiveTableHeap>));

template <typename Impl, typename Catalog>
struct Request_ : public ActiveHash::Node,
    public HTTPRequestBuilder<Message<Catalog>> {
  using Base = HTTPRequestBuilder<Message<Catalog>>;
  using Base::key;
  using BaseKeys = ZuTypeSlice<2, 0, typename Base::Headers>;
  using AppHeaderList = Zjrpc::AppHeaders<Impl>;
  using AppKeys = ZuTypeSlice<2, 0, AppHeaderList>;
  using HeaderKeys = ZuTypeConcat<BaseKeys, AppKeys>;

  using Headers = ZuTypeConcat<typename Base::Headers, AppHeaderList>;
  using HdrCatalog = Zjrpc::HTTPHdrCatalog<Headers>;
  Client<Impl, Catalog>	*client = nullptr;
  bool			responseOK = false;
  bool			responseSeen = false;
  bool			serial = false;
  bool			cancelled = false;

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
  const Zjrpc::ID *id() const { return Base::message.id(); }

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

  const Zjrpc::Limits &limits() const { return m_request->client->limits(); }

  bool receiveSSE(Zjrpc::SSEEvent event, const HTTPResponseMeta &meta) {
    return m_request &&
      m_request->client->receiveSSE(m_request, ZuMv(event), meta);
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
  using Object = Zjrpc::ObjectValue<typename Req::Object>;

  Object		object;
  ZmRef<Call>	call;
  Zjrpc::ID	progress;
  int		logLevel;

  CallAction_(
      Object object_, ZmRef<Call> call_, Zjrpc::ID progress_, int logLevel_) :
    object{ZuMv(object_)}, call{ZuMv(call_)}, progress{ZuMv(progress_)},
    logLevel{logLevel_} { }
};

template <typename Req, typename Call>
ZuDerive(CallHeap, (ZmHeap<"Zmcp.HTTP.Call", CallAction_<Req, Call>>));

template <typename Req, typename Call>
ZuDerive(CallAction, (CallAction_<Req, Call, CallHeap<Req, Call>>));


template <typename Impl, typename Catalog>
class Client : public Zhttp::Client<
    Client<Impl, Catalog>, Pool<Impl, Catalog>>,
    public Zmcp::Client<Client<Impl, Catalog>, Impl, Catalog> {
  using Base = Zhttp::Client<Client, Pool<Impl, Catalog>>;
  using Common = Zmcp::Client<Client, Impl, Catalog>;
  friend Zjrpc::BatchCaller<Client>;
  friend Zjrpc::Dispatcher<Client, Impl, ZuTypeList<>, ZuVoid, Client_::ReceivePolicy>;
  friend Common;
  using Common::m_impl;
  using Common::m_mx;
  using Common::pendingCalls;
  using Common::m_peer;
  using Common::m_limits;
  using Common::m_ownerThread;
  using Common::m_up;
  using Common::m_done;
  using Common::ingress;
  using Common::app_;
  using Req = Request<Impl, Catalog>;
  using ReqBase = Request_<Impl, Catalog>;

public:
  using Common::send;
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
    if (!m_ownerThread || !Zjrpc::valid(m_limits))
      goto invalid;
    if (!Common::initPeer_(m_limits)) goto invalid;
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

  using Common::limits;

  template <typename Key, typename L>
  void requestHeader(L &&l) const {
    requestHeader_<Key>(m_impl, ZuFwd<L>(l), 0);
  }

  bool tools() {
    if (!m_mx || !m_up.load_() || m_terminating.load_()) return false;
    if (++ingress() > m_limits.maxPending) {
      --ingress();
      return false;
    }
    ownerRun_([this]() {
      --ingress();
      if (const auto *catalog = m_peer.toolCatalog()) {
	app_([this, catalog]() { tools_(m_impl, catalog, 0); });
	return;
      }
      bool sent = true;
      Zjrpc::ID id = m_peer.tools([self = this, &sent](auto message) {
	sent = self->send_(ZuMv(message));
      });
      if ((id.is<void>() && !m_peer.toolCatalog()) || !sent)
	app_([this]() { toolsFailed_(m_impl, 0); });
    });
    return true;
  }

  bool discardCatalog() {
    if (!m_mx || !m_up.load_() || m_terminating.load_()) return false;
    if (++ingress() > m_limits.maxPending) {
      --ingress();
      return false;
    }
    ownerRun_([this]() {
      --ingress();
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
    if (++ingress() > m_limits.maxPending) {
      --ingress();
      return false;
    }
    ownerRun_([this]() {
      --ingress();
      terminate_();
    });
    return true;
  }

  template <typename R, typename Call>
  bool call(Zjrpc::ObjectValue<typename R::Object> object, ZmRef<Call> call) {
    return callProgressLog<R>(
      ZuMv(object), {}, LogLevel::Disabled, ZuMv(call));
  }

  template <typename R, typename Call>
  bool callProgress(
      Zjrpc::ObjectValue<typename R::Object> object, Zjrpc::ID progress, ZmRef<Call> call) {
    return callProgressLog<R>(
      ZuMv(object), ZuMv(progress), LogLevel::Disabled, ZuMv(call));
  }

  template <typename R, typename Call>
  bool callLog(
      Zjrpc::ObjectValue<typename R::Object> object, ZuCSpan level,
      ZmRef<Call> call) {
    return callProgressLog<R>(
      ZuMv(object), {}, logLevel(level), ZuMv(call));
  }

  template <typename R, typename Call>
  bool callProgressLog(
      Zjrpc::ObjectValue<typename R::Object> object, Zjrpc::ID progress, int logLevel,
      ZmRef<Call> call) {
    if (!m_mx || !m_up.load_() || m_terminating.load_() || !call)
      return false;
    if (++ingress() > m_limits.maxPending) {
      --ingress();
      return false;
    }
    using Action = CallAction<R, Call>;
    ZmRef<Action> action = new Action{
      ZuMv(object), ZuMv(call), ZuMv(progress), logLevel};
    ownerRun_([this, action = ZuMv(action)]() mutable {
      --ingress();
      call_<R>(ZuMv(action->object), ZuMv(action->progress), action->logLevel,
	ZuMv(action->call));
    });
    return true;
  }

  bool cancel(Zjrpc::ID id, ZuCSpan reason = {}) {
    if (!m_mx || !m_up.load_() || m_terminating.load_() || id.is<void>())
      return false;
    if (++ingress() > m_limits.maxPending) {
      --ingress();
      return false;
    }
    ZmRef<CancelAction> action = new CancelAction{ZuMv(id), reason};
    ownerRun_([this, action = ZuMv(action)]() mutable {
      --ingress();
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
      ReqBase *request, Zjrpc::SSEEvent event,
      const HTTPResponseMeta &meta) {
    if (!request) return false;
    session_(request, meta);
    bool ok = receive_(ZuMv(event.body));
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
	if (const Zjrpc::ID *id = request->id()) {
	  (void)m_active->del(*id);
	  if (pendingCalls().fail(*id)) ok = false;
	}
	break;
      case ReqType::Ping:
      case ReqType::SetLevel:
	if (const Zjrpc::ID *id = request->id())
	  if (pendingCalls().fail(*id)) ok = false;
	break;
      case ReqType::Batch:
	if (!ok)
	  if (const Zjrpc::ID *id = request->id()) (void)pendingCalls().fail(*id);
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
    typename = decltype(ZuDeclVal<App * &>()->template header<Key>(
      ZuFwd<L>(ZuDeclVal<L &>())), void())>
  static void requestHeader_(App *app, L &&l, int) {
    app->template header<Key>(ZuFwd<L>(l));
  }
  template <typename Key, typename App, typename L>
  static void requestHeader_(App *, L &&, long) { }

  template <typename Call>
  bool control_(int kind, ZuCSpan level, ZmRef<Call> call) {
    if (!m_mx || !m_up.load_() || m_terminating.load_() || !call)
      return false;
    if (++ingress() > m_limits.maxPending) {
      --ingress();
      return false;
    }
    ZmRef<ControlAction<Call>> action =
      new ControlAction<Call>{kind, level, ZuMv(call)};
    ownerRun_([this, action = ZuMv(action)]() mutable {
      --ingress();
      controlTx_(action->kind, action->level, ZuMv(action->call));
    });
    return true;
  }

  template <typename L>
  void ownerRun_(L &&l) {
    if (m_mx->invoked(m_ownerThread)) l();
    else m_mx->run(ZuFwd<L>(l), m_ownerThread);
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
  bool send_(M message) {
    bool serial = !m_peer.ready() || m_peer.era() == Era::Legacy;
    bool queued = serial && (m_current || m_drainingSerial);
    if (queued && m_serial.count_() >= m_limits.maxQueue) return false;
    ZmRef<Req> request = new Req{};
    request->init(this, ZuMv(message), m_sequence++, m_endpoint,
      m_sessionID, m_peer.era(), serial);
    if constexpr (ZuIsSame<M, ToolCallRequestMessage<Catalog>>{}) {
      request->ActiveHash::Node::data().id = request->id();
      m_active->addNode(request.ptr());
    }
    if (queued) {
      if (request->kind() == ReqType::Initialized)
	m_serial.unshiftNode(SerialQ::NodeRef{ZuMv(request)});
      else
	m_serial.pushNode(SerialQ::NodeRef{ZuMv(request)});
      return true;
    }
    if (serial) m_current = request->ReqBase::sequence;
    auto ptr = request.ptr();
    if (Base::send(0, ZuMv(request))) return true;
    if constexpr (ZuIsSame<M, ToolCallRequestMessage<Catalog>>{})
      (void)m_active->delNode(ptr);
    return false;
  }

  bool receive_(ZmRef<ZiIOBuf> body) {
    const auto *catalog = m_peer.toolCatalog();
    bool sent = true;
    auto emit = [self = this, &sent](auto message) {
      if (sent) sent = self->send_(ZuMv(message));
    };
    bool ok = Common::receiveWire_(ZuMv(body), emit) && sent;
    if (!catalog)
      if (const auto *catalog_ = m_peer.toolCatalog())
	app_([this, catalog_]() { tools_(m_impl, catalog_, 0); });
    return ok;
  }

  template <typename App,
    typename = decltype(ZuDeclVal<App * &>()->tools(
      ZuDeclVal<const ZfJSON::AnyNode * &>()), void())>
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
      Zjrpc::ObjectValue<typename R::Object> object, Zjrpc::ID progress, int logLevel,
      ZmRef<Call> call) {
    if (!m_up.load_() || !m_peer.ready()) {
      try { call->failed(); } catch (...) { }
      return;
    }
    bool added = false;
    auto emit = [this, &call, &added](auto message) {
      Zjrpc::ID id = message.id;
      if (!pendingCalls().template add<R, ToolReplyDecode>(id, call)) return;
      added = true;
      if (!send_(ZuMv(message))) {
	(void)pendingCalls().fail(id);
	return;
      }
      app_([call, id]() { started_(call.ptr(), id, 0); });
    };
    if (progress.is<void>() && logLevel == LogLevel::Disabled)
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
      Zjrpc::ID id = message.id;
      if (!pendingCalls().add(id, call)) return;
      added = true;
      if (!send_(ZuMv(message))) {
	(void)pendingCalls().fail(id);
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

  bool cancel_(const Zjrpc::ID &id, ZuCSpan reason) {
    auto entry = m_active->findPtr(id);
    if (!entry) return false;
    auto request = static_cast<ReqBase *>(entry);
    request->cancelled = true;
    if (request->serial && request->ReqBase::sequence != m_current) {
      (void)m_active->del(id);
      (void)pendingCalls().fail(id);
      return true;
    }
    if (m_peer.era() == Era::Legacy) {
      if (!send_(CancelledRequestMessage{id, reason, m_peer.era()}))
	return false;
    }
    return Base::cancel(0, request->ReqBase::sequence);
  }

  template <typename Call,
    typename = decltype(ZuDeclVal<Call * &>()->started(ZuDeclVal<const Zjrpc::ID &>()), void())>
  static void started_(Call *call, const Zjrpc::ID &id, int) {
    call->started(id);
  }
  template <typename Call>
  static void started_(Call *, const Zjrpc::ID &, long) { }

  template <typename App,
    typename = decltype(ZuDeclVal<App * &>()->cancelled(ZuDeclVal<const Zjrpc::ID &>(),
      ZuDeclVal<bool &>()), void())>
  static void cancelled_(App *app, const Zjrpc::ID &id, bool ok, int) {
    app->cancelled(id, ok);
  }
  template <typename App>
  static void cancelled_(App *, const Zjrpc::ID &, bool, long) { }

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
      ZmRef<Req> request = ZuMv(next);
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
	if (!ingress().load_() && Common::closeInput_() &&
	    pendingCalls().close(m_limits.workBatch) && !ingress().load_()) m_stopPhase = 3;
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
    auto i = m_active->iter();
    if (!i()) return decltype(i.del()){};
    return i.del();
  }

  ZmRef<ActiveTable>	m_active = new ActiveTable{};
  SerialQ m_serial;
  ZmFn<void(bool)>	m_stopFn;
  Zjrpc::HTTPValue		m_endpoint;
  Zjrpc::HTTPValue		m_sessionID;
  uint64_t		m_sequence = 1;
  uint64_t		m_current = 0;
  int			m_stopPhase = 0;
  bool			m_legacySessions = true;
  bool			m_started = false;
  bool			m_drainingSerial = false;
  bool			m_ready = false;
  bool			m_failure = false;

  ZmAtomic<unsigned>	m_terminating = 0;
};

} // HTTPClient_

template <typename Impl, typename Catalog>
using HTTPClient = HTTPClient_::Client<Impl, Catalog>;

template <typename Impl, typename Catalog>
class IOClient : public Zmcp::Client<IOClient<Impl, Catalog>, Impl, Catalog> {
  using Common = Zmcp::Client<IOClient, Impl, Catalog>;
  friend Zjrpc::BatchCaller<IOClient>;
  friend Zjrpc::Dispatcher<IOClient, Impl, ZuTypeList<>, ZuVoid, Client_::ReceivePolicy>;
  friend Common;
  using IO = Zjrpc::IOLink<IOClient>;
  using Common::m_impl;
  using Common::m_mx;
  using Common::pendingCalls;
  using Common::m_peer;
  using Common::m_limits;
  using Common::m_ownerThread;
  using Common::m_up;
  using Common::m_done;
  using Common::ingress;
  using Common::app_;
public:
  IOClient() = default;
  ~IOClient() { final(); }

  bool init(ZiMultiplex *mx, Zjrpc::StdioConfig config, Impl *impl) {
    if (m_mx || !mx || !impl || !mx->txThread()) return false;
    m_impl = impl;
    m_mx = mx;
    m_ownerThread = mx->txThread();
    m_limits = config.limits();
    if (!m_limits.maxPending || !m_limits.workBatch) goto invalid;
    if (!Common::initPeer_(m_limits)) goto invalid;
    new (m_stdio.template new_<IO>()) IO{
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
    if (!m_mx || !m_stdio.template ptr<IO>() || !m_up.load_() || m_started || m_done.load_())
      return false;
    return ZmBlock<bool>{}([this](auto wake) mutable {
      m_mx->run([this, wake = ZuMv(wake)]() mutable {
	bool started = m_stdio.template p<IO>().start_();
	bool ok = started;
	m_started = started;
	if (started) {
	  bool sent = false;
	  ok = m_peer.probe([this, &sent](const auto &message) {
	    sent = m_stdio.template p<IO>().send_(message);
	  }) && sent;
	}
	if (!ok) {
	  m_failure = true;
	  m_up = false;
	  if (started) m_stdio.template p<IO>().stop_();
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
	if (m_started) m_stdio.template p<IO>().stop_();
	else done_(false);
      }, m_ownerThread);
    });
  }

  void final() {
    if (!m_mx) return;
    (void)stop();
    m_stdio.null();
    m_impl = nullptr;
    m_mx = nullptr;
    m_ownerThread = 0;
  }

  bool stdioFrame(ZmRef<ZiIOBuf> body) { return Common::receive(ZuMv(body)); }
  void stdioClosed() { done_(false); }
  void stdioFailed() { done_(true); }

private:
  using Common::invoked;
  template <typename M>
  bool send_(const M &message) { return m_stdio.template p<IO>().send_(message); }

  void fail_() {
    ZiAssert(this->invoked(), "Zmcp", (),
	"stdio client failure outside owner shard", return);
    if (m_done.load_() || m_failure) return;
    m_failure = true;
    m_up = false;
    m_peer.close();
    m_stdio.template p<IO>().stop_();
  }

  void done_(bool failed) {
    ZiAssert(this->invoked(), "Zmcp", (),
	"stdio client completion outside owner shard", return);
    if (m_done.load_()) return;
    m_failure |= failed;
    m_up = false;
    m_peer.close();
    drain_();
  }

  void drain_() {
    if (!Common::closePeer_()) {
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

  ZuUnion<void, IO> m_stdio;
  ZmFn<void(bool)>	m_stopFn;
  bool			m_started = false;
  bool			m_failure = false;

};

template <typename Impl, typename Catalog, typename Profile = Zhttp::H1TCP>
class WSClient : public Client<WSClient<Impl, Catalog, Profile>, Impl, Catalog>,
    public Zjrpc::WSIO<WSClient<Impl, Catalog, Profile>> {
  using Common = Client<WSClient, Impl, Catalog>;
  friend Zjrpc::BatchCaller<WSClient>;
  friend Zjrpc::Dispatcher<WSClient, Impl, ZuTypeList<>, ZuVoid, Client_::ReceivePolicy>;
  using IO = Zjrpc::WSIO<WSClient>;
  friend Common;
  friend IO;
  using Common::m_impl;
  using Common::m_mx;
  using Common::pendingCalls;
  using Common::m_peer;
  using Common::m_limits;
  using Common::m_ownerThread;
  using Common::m_up;
  using Common::m_done;
  using Common::invoked;
public:
  using LinkState = Zjrpc::WSClientState;
  using WS = Zws::Client<WSClient, Profile>;
  using Link = typename WS::Link;
  using Config = typename WS::Config;

  WSClient() : m_ws{this} { }
  ~WSClient() { final(); }
  bool init(const Zhttp::HubConfig &hub, Config profile, Zjrpc::WSConfig config, Impl *impl) {
    if (m_mx || !hub.mx() || !impl || !Zjrpc::valid(config.limits())) return false;
    if (!Common::initPeer_(config.limits())) return false;
    m_impl = impl;
    m_mx = hub.mx();
    m_ownerThread = hub.txThread() ? m_mx->sid(hub.txThread()) : m_mx->txThread();
    if (!m_ws.init(hub, ZuMv(profile), config.binding())) {
      m_impl = nullptr;
      m_mx = nullptr;
      return false;
    }
    return true;
  }
  bool start() {
    if (!m_mx || m_started || m_stopping.load_() || !m_ws.start()) return false;
    m_started = true;
    return true;
  }
  template <typename ...Args>
  bool connect(const Zws::URI &uri, Args &&...args) {
    if (!m_started || m_link || m_stopping.load_()) return false;
    m_link = new Link{&m_ws, uri, ZuFwd<Args>(args)...};
    if constexpr (Zhttp::ProfileTraits<Profile>::Multiplexed)
      m_link->connect(uri.host, uri.port);
    else
      m_link->connect();
    return true;
  }
  bool stop() {
    if (!m_mx) return true;
    m_stopping = true;
    m_up = false;
    ZmBlock<>{}([this](auto wake) mutable {
      m_ws.txRun([this, wake = ZuMv(wake)]() mutable {
	m_stop = ZuMv(wake);
	if (m_link && !m_down)
	  m_ws.rxRun([this]() { IO::stopWS_(*m_link); });
	else close_([this]() { stopped_(); });
      });
    });
    bool ok = !m_started || m_ws.stop();
    m_started = false;
    return ok && !m_failure;
  }
  void final() {
    if (!m_mx) return;
    (void)stop();
    m_link = nullptr;
    m_ws.final();
    m_impl = nullptr;
    m_mx = nullptr;
    m_ownerThread = 0;
  }
  WS &ws() { return m_ws; }
  const WS &ws() const { return m_ws; }
  using Common::limits;
  bool wsAccept() const { return !m_stopping.load_(); }

  void connected(Link &link, Zhttp::ConnectedInfo info) {
    link.state().rx.open();
    if (!wsAccept()) { link.close(Zws::CloseCode::GoingAway); return; }
    m_up = true;
    ZmRef<Zjrpc::WSOpen<Link>> action = new Zjrpc::WSOpen<Link>{&link, ZuMv(info)};
    m_ws.txRun([this, action = ZuMv(action)]() mutable {
      if (!m_up.load_() || !wsAccept()) return;
      try {
	Zws::H1_::connected(*m_impl, *action->link, ZuMv(action->info), 0);
	bool sent = false;
	if (!m_peer.probe([this, &sent](const auto &message) { sent = this->send_(message); }) ||
	    !sent) fail_();
      } catch (...) { fail_(); }
    });
  }
  void connectFailed(Link &link, bool) {
    link.state().rx.close();
    m_up = false;
    m_ws.txRun([this]() {
      m_down = true;
      m_failure = true;
      close_([this]() { stopped_(); });
    });
  }
  void disconnected(Link &link, bool) {
    link.state().rx.close();
    m_up = false;
    m_ws.txRun([this]() {
      m_down = true;
      close_([this]() { stopped_(); });
    });
  }
  void closed(Link &link, uint16_t code, ZuBSpan reason) {
    m_up = false;
    IO::template control_<true>(link, code, reason);
  }
  void error(Link &link, Zws::Failure::T failure) {
    m_up = false;
    m_ws.txRun([this, link = ZmRef{&link}, failure]() mutable {
      m_failure = true;
      try { Zws::H1_::error(*m_impl, *link, failure, 0); } catch (...) { }
    });
  }
  void wsFrame_(Link &, ZmRef<ZiIOBuf> body) {
    if (m_up.load_() && wsAccept()) (void)Common::receive(ZuMv(body));
  }

private:
  void stopped_() {
    if (!m_stop) return;
    auto done = ZuMv(m_stop);
    m_ws.rxRun([this, done = ZuMv(done)]() mutable {
      m_ws.txRun([done = ZuMv(done)]() mutable { done(); });
    });
  }
  template <typename M>
  bool send_(const M &message) {
    return m_up.load_() && wsAccept() && m_link && IO::sendWS_(*m_link, message);
  }
  void fail_() {
    ZiAssert(this->invoked(), "Zmcp", (), "WS failure outside owner shard", return);
    m_failure = true;
    m_up = false;
    m_peer.close();
    if (m_link) m_link->close(Zws::CloseCode::Internal);
  }
  template <typename Done>
  void close_(Done done) {
    if (!Common::closePeer_()) {
      m_ws.txRun([this, done = ZuMv(done)]() mutable { close_(ZuMv(done)); });
      return;
    }
    if (!m_done.load_()) {
      m_done = true;
      try {
	if (m_failure) m_impl->failed();
	else m_impl->closed();
      } catch (...) { }
    }
    done();
  }

  WS m_ws;
  ZmRef<Link> m_link;
  ZmFn<void()> m_stop;
  ZmAtomic<unsigned> m_stopping = 0;
  bool m_started = false;
  bool m_failure = false;
  bool m_down = false;
};

} // Zmcp

#endif /* ZmcpClient_HH */
