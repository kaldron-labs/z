//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuDerive.hh>
#include "pinghttp.hh"

#include <zlib/ZuBase64URL.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmPQueue.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiResolver.hh>

#include <zlib/ZhttpClient.hh>
#include <zlib/ZhttpDiscovery.hh>
#include <zlib/ZhttpURL.hh>
#include <zlib/ZrestClient.hh>
#include <zlib/ZtlsMD.hh>

namespace Zum {
namespace PingHTTP_ {

enum {
  RequestTimeout = 15,
  ResponseMax = 64U<<10,
  Concurrency = 32,
  DestinationMax = 8
};

template <unsigned Status_>
struct ResponseData : public ZmNHeap<ResponseData<Status_>>, public ZmObject  {
friend inline ZuStringT<"Zum.PingHTTP.Response"> ZmNHeapID(ResponseData *);

  enum { Status = Status_ };
  String body;
  ~ResponseData() {
    if (body.mutable_()) ZuClear(body);
  }
  ResponseData &operator =(ZuSpan<uint8_t> data) {
    body = data;
    return *this;
  }
};

template <unsigned Status_>
struct Response : public Zrest::ResParser<Response<Status_>,
    ResponseData<Status_>> {
  enum { Status = Status_, Body = Zrest::BodyPolicy::Raw };

  bool bodyInfo(Zhttp::BodyType::T type, uint64_t length) {
    if (type == Zhttp::BodyType::Fixed && length > ResponseMax) return false;
    m_fixed = type == Zhttp::BodyType::Fixed;
    m_remaining = length;
    return true;
  }

  template <typename Rx> bool body(Rx &rx) {
    while (rx) {
      int64_t consumed = rx.consume(
	[this](ZuSpan<uint8_t> span) -> int64_t {
	  uint64_t n = span.length();
	  if (m_fixed && n > m_remaining) n = m_remaining;
	  if (this->object->body.length() + n > ResponseMax) return -1;
	  return n;
	},
	[this](ZuSpan<uint8_t> span) {
	  this->object->body << span;
	  if (m_fixed) m_remaining -= span.length();
	});
      if (consumed < 0) return false;
      if (!consumed) break;
    }
    return true;
  }

private:
  uint64_t m_remaining = 0;
  bool m_fixed = false;
};

using Responses = ZuTypeList<Response<200>, Response<201>, Response<204>,
  Response<400>, Response<401>, Response<403>, Response<404>, Response<405>,
  Response<409>, Response<412>, Response<428>, Response<415>, Response<422>, Response<429>, Response<500>,
  Response<501>, Response<502>, Response<503>, Response<504>>;

struct CallReturn {
  PingHTTPState *state;
  ~CallReturn();
};

struct Call : public ZmNHeap<Call>, public ZmObject, public CallReturn {
friend inline ZuStringT<"Zum.pinghttp.Call"> ZmNHeapID(Call *);

  mutable ServiceHTTPRequest request;
  mutable ServiceHTTPResponse response;
  mutable String	target;
  mutable ServiceHTTPDoneFn complete;
  mutable ZmAtomic<unsigned> done = 0;

  ~Call() {
    if (request.authorization.mutable_()) ZuClear(request.authorization);
    if (request.body.mutable_()) ZuClear(request.body);
    if (response.body.mutable_()) ZuClear(response.body);
  }

  Call(PingHTTPState *state_, ServiceHTTPRequest request_, ServiceHTTPDoneFn fn) :
    CallReturn{state_}, request{ZuMv(request_)}, complete{ZuMv(fn)} { }

  void finish(unsigned status, String value) const;

  template <typename Link, typename Value>
  void process(Link *, const Value *value) const {
    finish(Value::Status, value->body);
  }
  template <typename Link> void failed(Link *) const {
    finish(0, {});
  }
};

template <typename Impl, Zhttp::Method::T Method_, unsigned Body_>
struct Request : public Zrest::ReqBuilder<Impl, Call> {
  using Base = Zrest::ReqBuilder<Impl, Call>;
  using Base::header;
  enum { Method = Method_, Exact = 1, Query = Zrest::QueryPolicy::Raw,
    Body = Body_ };
  using Path = ZuStringT<"/">;
  using Headers = ZhttpHeaders(
    "content-type", "authorization", "if-match", "idempotency-key", "content-length");
  using Responses = PingHTTP_::Responses;

  const String &queryObject(const Call *call) const { return call->target; }
  const String &bodyObject(const Call *call) const { return call->request.body; }
  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (Key{}() == "content-type")
      l(this->object->request.contentType);
    else if constexpr (Key{}() == "authorization")
      l(this->object->request.authorization);
    else if constexpr (Key{}() == "if-match") l(this->object->request.ifMatch);
    else if constexpr (Key{}() == "idempotency-key") l(this->object->request.idempotence);
    else Base::template header<Key>(ZuFwd<L>(l));
  }
};

struct GET : public Request<GET, Zhttp::Method::GET,
    Zrest::BodyPolicy::None> { };
struct POST : public Request<POST, Zhttp::Method::POST,
    Zrest::BodyPolicy::Raw> { };
struct PUT : public Request<PUT, Zhttp::Method::PUT,
    Zrest::BodyPolicy::JSON> {
  enum { SignBody = 1 };
  static constexpr unsigned SignBodyBufSize = ResponseMax;

  const CatalogData &bodyObject(const Call *call) const {
    return call->request.manifest.catalog;
  }
  template <typename S>
  void prefixBody(S &s, const Call *) const {
    s << "{\"catalog\":";
  }
  template <typename S>
  void signBody(S &buf, const Call *call, ZuCSpan body) const {
    ZuBArray<Ztls::MD<>::Size> digest;
    digest.length(Ztls::MD<>::Size);
    Ztls::MD<> md;
    md.update(ZuBSpan{body});
    md.finish(digest);
    String encoded;
    encoded.length(ZuBase64URL::enclen(digest.length()));
    encoded.length(ZuBase64URL::encode(encoded.span(), digest));
    buf << ",\"digest\":";
    ZfJSON::quote(buf, encoded);
    buf << ",\"revision\":";
    ZfJSON::saveValue<
	ZuFacet::JSON, ZfFieldFilter::Save, ZfFieldTC::UInt64,
	ZuTypeList<ZuFieldProp::JSON::String<>>>(
	  buf, call->request.manifest.revision);
    buf << '}';
  }
};
using Requests = ZuTypeList<GET, POST, PUT>;
ZrestCatalogDerive(Catalog, Requests);
ZrestCatalogImpl(Catalog)

struct ReqBuilder_ : public ZmObject, public Zrest::MReqBuilder<Catalog> {
  using Reqs = Requests;
  uint64_t id = 0;
  uint64_t key() const { return id; }
  uint64_t length() const { return 1; }

  void selected(const Zhttp::Endpoint *endpoint, uint64_t, uint64_t,
      unsigned, unsigned, Zhttp::Transport::T transport,
      Zhttp::Version::T version) {
    ZiLOG(Debug, "zumd", ([ip = endpoint->ip, port = endpoint->port,
        transport, version](auto &s) {
      s << "service HTTP destination selected " << ip << ':' << port
	<< " transport=" << Zhttp::Transport{}.name(transport)
	<< " HTTP=" << Zhttp::Version{}.name(version);
    }));
  }

  void completed(const Zhttp::Result &result) {
    u.cdispatch([&result](auto, const auto &request) {
      if (!request.object->done.load_())
        request.object->finish(result.status, {});
    });
  }
};

struct ResParser : public Zrest::MResParser<Catalog, ReqBuilder_> { };

class ClientBase;
class Pool;
ZmPQueueDerive(ReqBuilderQ, ReqBuilder_,
  ZmPQueueOverlap<false, ZmPQueueNode<ReqBuilder_,
    ZmPQueueHeapID<"Zum.PingHTTP.Request">>>);
using ReqBuilder = ReqBuilderQ::Node;
ZuDerive(TxQ, (ZmPQTx<Pool, ReqBuilderQ, ZmPQTxOrdered<false>>));

class Pool : public ZmNHeap<Pool>, public Zhttp::Pool<ClientBase, TxQ, ResParser,
  Zhttp::Profiles<Zhttp::H1TCP, Zhttp::H1TLS, Zhttp::H2TLS>> {
friend inline ZuStringT<"Zum.PingHTTP.Pool"> ZmNHeapID(Pool *);

  using Base = Zhttp::Pool<ClientBase, TxQ, ResParser, Zhttp::Profiles<Zhttp::H1TCP,
    Zhttp::H1TLS, Zhttp::H2TLS>>;
public:
  Pool(ClientBase *client) : Base{client} { }
  ReqBuilderQ *txQueue() { return &m_requests; }
  void archive_(ReqBuilder *) { }
  ZmRef<ReqBuilder> retrieve_(ReqBuilderQ::Key, ReqBuilderQ::Key) {
    return {};
  }
private:
  ReqBuilderQ m_requests;
};

class ClientBase : public ZmObject, public Zhttp::Client<ClientBase, Pool> {
public:
  using Base = Zhttp::Client<ClientBase, Pool>;

  bool init(ZiMultiplex *mx, const Zhttp::URLView &url, ZuCSpan caPath) {
    m_secure = url.scheme == Zhttp::Scheme::https;
    m_host = url.host;
    m_port = url.port;
    m_ipv6Literal = url.ipv6Literal;
    auto config = Zhttp::Config().links(2).concurrency(Concurrency)
      .linkMax(Concurrency).requestTimeout(RequestTimeout)
      .maxRetries(1).retainedBodyMax(ResponseMax)
      .protocol(Zhttp::ProtoPolicy::DisableH3)
      .secure(url.scheme == Zhttp::Scheme::https).tcp(true)
      .tls(url.scheme == Zhttp::Scheme::https).quic(false);
    if (!Base::init(Zhttp::HubConfig{mx, "rx", "tx"}, 1,
        config, Zhttp::TCPConfig{}, Zhttp::H2Config{}.tls([&caPath](auto &tls) {
          tls.caPath(caPath);
        }),
        Zhttp::QUICConfig{})) return false;
    Base::txErrorFn(ZiTxErrorFn{[](ZeException &e) {
      ZiLOG(Error, "zumd", ([e](auto &s) {
	s << "service HTTP transmit error: " << e;
      }));
      return false;
    }});
    return Base::pool(0, Zhttp::Destination{url.origin()});
  }

  bool origin(const Zhttp::URLView &url) const {
    return (url.scheme == Zhttp::Scheme::https) == m_secure && url.host == m_host &&
      url.port == m_port && url.ipv6Literal == m_ipv6Literal;
  }

  void send(ZmRef<Call> call, const Zhttp::URLView &url) {
    if (url.path) {
      auto path = url.path;
      if (path[0] == '/') path.offset(1);
      call->target = path;
    }
    if (url.hasQuery) call->target << '?' << url.query;
    ZmRef<ReqBuilder> builder = new ReqBuilder{};
    builder->id = ++m_id;
    switch (call->request.method) {
      case ServiceMethod::GET:
        builder->template init<GET>(call.ptr());
        break;
      case ServiceMethod::POST:
        builder->template init<POST>(call.ptr());
        break;
      case ServiceMethod::PUT:
        builder->template init<PUT>(call.ptr());
        break;
      default:
        call->finish(0, {});
        return;
    }
    if (!Base::send(0, ZuMv(builder))) call->finish(0, {});
  }

private:
  String	m_host;
  uint16_t	m_port = 0;
  uint64_t	m_id = 0;
  bool		m_ipv6Literal = false;
  bool m_secure = false;
};

class Client : public ZmNHeap<Client>, public ClientBase {
friend inline ZuStringT<"Zum.PingHTTP.Client"> ZmNHeapID(Client *);
};

} // namespace PingHTTP_

namespace PingHTTPCredit {
  enum : uint64_t { Closed = uint64_t{1} << 62, Count = Closed - 1 };
}

class PingHTTPState : public ZmNHeap<PingHTTPState>, public ZmObject,
    private ZmEngine<PingHTTPState> {
friend inline ZuStringT<"Zum.PingHTTP.State"> ZmNHeapID(PingHTTPState *);
friend ZmEngine<PingHTTPState>;

  using Engine = ZmEngine<PingHTTPState>;
  using Call = PingHTTP_::Call;
  using Work = ZmRef<Call>;

public:
  using Engine::start;
  using Engine::stop;
  using Engine::state;

  template <typename L> void post(L &&fn) {
    m_mx->run(ZuFwd<L>(fn), m_mx->rxThread());
  }

  bool init(ZiMultiplex *mx, ZuCSpan issuer, ZuCSpan managementIssuer,
      ZuCSpan management, ZuCSpan caPath) {
    m_mx = mx;
    m_caPath = caPath;
    m_resolverOwned = !ZiResolver::instance()->initialized();
    if (m_resolverOwned)
      ZiResolver::init(ZiResolverParams{}.timeoutMS(2000).tries(2));
    if (!add_(issuer) || !add_(managementIssuer) || !add_(management)) {
      final();
      return false;
    }
    return true;
  }

  void send(ServiceHTTPRequest request, ServiceHTTPDoneFn complete) {
    uint64_t count;
    do {
      count = m_credit.load_();
      if (count & PingHTTPCredit::Closed) {
        complete(ServiceHTTPResponse{});
        return;
      }
    } while (m_credit.cmpXch(count + 1, count) != count);
    Work call = new Call{this, ZuMv(request), ZuMv(complete)};
    post([this, call = ZuMv(call)]() mutable { send_(ZuMv(call)); });
  }

  void retire() {
    post([this]() { --m_credit; drain_(); });
  }

  void final() {
    ZmAssert_(Engine::state() == ZmEngineState::Stopped);
    ZmAssert_(!(m_credit.load_() & PingHTTPCredit::Count));
    ZmAssert_(!m_children && !m_draining);
    for (auto &client: m_clients) client->final();
    m_clients.null();
    if (m_resolverOwned) {
      ZiResolver::final();
      m_resolverOwned = false;
    }
    m_caPath.null();
    m_mx = nullptr;
  }

private:
  bool add_(const Zhttp::URLView &url) {
    if (!url.host || url.hasFragment ||
        (url.scheme != Zhttp::Scheme::https &&
          !(url.scheme == Zhttp::Scheme::http &&
            (url.host == "localhost" || url.host == "127.0.0.1" || url.host == "::1"))))
      return false;
    for (auto &client: m_clients)
      if (client->origin(url)) return true;
    if (m_clients.length() >= PingHTTP_::DestinationMax) return false;
    ZmRef<PingHTTP_::Client> client = new PingHTTP_::Client{};
    if (!client->init(m_mx, url, m_caPath)) {
      client->final();
      return false;
    }
    m_clients.push(ZuMv(client));
    return true;
  }

  bool add_(ZuCSpan origin) {
    Zhttp::URL parsed{origin};
    return parsed.ok() && add_(parsed.url());
  }

  void start_() {
    if (!m_mx) { Engine::started(false); return; }
    if (m_resolverOwned) {
      ZiResolver::start([this](bool ok) {
        if (!ok) { Engine::started(false); return; }
        post([this]() { startClient_(0); });
      });
      return;
    }
    if (ZiResolver::instance()->state() != ZmEngineState::Running) {
      Engine::started(false);
      return;
    }
    post([this]() { startClient_(0); });
  }

  void startClient_(unsigned offset) {
    if (offset == m_clients.length()) {
      m_credit = 0;
      Engine::started(true);
      return;
    }
    m_clients[offset]->start([this, offset](bool ok) {
      post([this, offset, ok]() {
        if (!ok) { stopClients_(true); return; }
        startClient_(offset + 1);
      });
    });
  }

  void stop_() {
    m_credit |= PingHTTPCredit::Closed;
    post([this]() { stopClients_(false); });
  }

  void stopClients_(bool failed) {
    m_stopOK = true;
    m_draining = true;
    m_children = m_clients.length();
    // At most DestinationMax (eight) children, including a possible dynamic
    // destination whose start is in progress. Join each child's engine.
    for (auto &client: m_clients) {
      client->stop([this, failed](bool ok) {
        post([this, failed, ok]() {
          m_stopOK &= ok;
          --m_children;
          drain_(failed);
        });
      });
    }
    drain_(failed);
  }

  void drain_(bool failed = false) {
    if (!m_draining || m_children ||
        (m_credit.load_() & PingHTTPCredit::Count)) return;
    m_draining = false;
    if (m_resolverOwned) {
      ZiResolver::stop([this, failed](bool ok) {
        post([this, failed, ok]() {
          m_stopOK &= ok;
          finish_(failed);
        });
      });
      return;
    }
    finish_(failed);
  }

  void finish_(bool failed) {
    if (failed) Engine::started(false);
    else Engine::stopped(m_stopOK);
  }

  void send_(Work call) {
    if (m_credit.load_() & PingHTTPCredit::Closed) { call->finish(0, {}); return; }
    Zhttp::URL parsed{call->request.url};
    auto url = parsed.url();
    if (!parsed.ok() || url.hasFragment || !add_(url)) {
      call->finish(0, {});
      return;
    }
    PingHTTP_::Client *client = nullptr;
    for (auto &candidate: m_clients)
      if (candidate->origin(url)) { client = candidate; break; }
    ZmAssert_(client);
    if (client->state() == ZmEngineState::Running) {
      sendReady_(client, ZuMv(call));
      return;
    }
    client->start([this, client, call = ZuMv(call)](bool ok) mutable {
      auto work = ZuMv(call);
      if (!work) return;
      post([this, client, work = ZuMv(work), ok]() mutable {
        if (!ok) { work->finish(0, {}); return; }
        sendReady_(client, ZuMv(work));
      });
    });
  }

  void sendReady_(PingHTTP_::Client *client, Work work) {
    if (m_credit.load_() & PingHTTPCredit::Closed) {
      work->finish(0, {});
      return;
    }
    client->txRun(0, [client, work = ZuMv(work)]() mutable {
      Zhttp::URL parsed{work->request.url};
      client->send(ZuMv(work), parsed.url());
    });
  }

private:
  ZtArray<ZmRef<PingHTTP_::Client>,
    ZtArrayHeapID<"Zum.PingHTTP.Clients">> m_clients;
  ZiMultiplex *m_mx = nullptr;
  String m_caPath;
  bool m_resolverOwned = false;
  unsigned m_children = 0;
  bool m_stopOK = true;
  bool m_draining = false;
  ZmAtomic<uint64_t> m_credit = PingHTTPCredit::Closed;
};

PingHTTP_::CallReturn::~CallReturn()
{
  state->retire();
}

void PingHTTP_::Call::finish(unsigned status, String value) const
{
  if (done.cmpXch(1, 0)) return;
  if (request.authorization.mutable_()) ZuClear(request.authorization);
  request.authorization.null();
  if (request.body.mutable_()) ZuClear(request.body);
  request.body.null();
  response = ServiceHTTPResponse{status, ZuMv(value)};
  ZmRef<Call> call = const_cast<Call *>(this);
  state->post([call = ZuMv(call)]() mutable {
    auto complete = ZuMv(call->complete);
    if (complete) complete(ZuMv(call->response));
  });
}

PingHTTP::PingHTTP() = default;
PingHTTP::~PingHTTP() { final(); }

bool PingHTTP::init(ZiMultiplex *mx, ZuCSpan issuer,
    ZuCSpan managementIssuer, ZuCSpan management, ZuCSpan caPath)
{
  if (m_state) return false;
  ZmRef<PingHTTPState> state = new PingHTTPState{};
  if (!state->init(mx, issuer, managementIssuer, management, caPath))
    return false;
  m_state = ZuMv(state);
  return true;
}

bool PingHTTP::start() { return m_state && m_state->start(); }
void PingHTTP::start(CtrlFn fn)
{
  if (!m_state) { fn(false); return; }
  m_state->start(ZuMv(fn));
}
bool PingHTTP::stop() { return !m_state || m_state->stop(); }
void PingHTTP::stop(CtrlFn fn)
{
  if (!m_state) { fn(true); return; }
  m_state->stop(ZuMv(fn));
}
int PingHTTP::state() const
{
  return m_state ? m_state->state() : ZmEngineState::Stopped;
}

ServiceHTTPFn PingHTTP::fn() const
{
  if (!m_state) return {};
  return ServiceHTTPFn{[state = m_state](
      ServiceHTTPRequest request, ServiceHTTPDoneFn complete) mutable {
    state->send(ZuMv(request), ZuMv(complete));
  }};
}

void PingHTTP::final()
{
  if (!m_state) return;
  auto state = ZuMv(m_state);
  state->final();
}

} // namespace Zum
