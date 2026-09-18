//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

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

template <unsigned Status_, typename Heap>
struct ResponseData_ : public Heap, public ZmObject  {
  enum { Status = Status_ };
  String body;
  ~ResponseData_() {
    if (body.mutable_()) ZuClear(body.data(), body.length());
  }
  ResponseData_ &operator =(ZuSpan<uint8_t> data) {
    body = data;
    return *this;
  }
};
template <unsigned Status_>
using ResponseData = ResponseData_<Status_,
  ZmHeap<"Zum.PingHTTP.Response", ResponseData_<Status_, ZuVoid>>>;

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

template <typename Heap>
struct Call_ : public Heap, public ZmObject  {
  mutable String	contentType;
  mutable String	authorization;
  String ifMatch;
  String idempotencyKey;
  mutable String	target;
  mutable String	body;
  ServiceManifest	manifest;
  mutable ServiceHTTPDoneFn complete;
  mutable ZmAtomic<unsigned> done = 0;

  ~Call_() {
    if (authorization.mutable_())
      ZuClear(authorization.data(), authorization.length());
    if (body.mutable_()) ZuClear(body.data(), body.length());
  }

  void finish(unsigned status, String value) const {
    if (done.cmpXch(1, 0)) return;
    if (authorization.mutable_())
      ZuClear(authorization.data(), authorization.length());
    authorization.null();
    if (body.mutable_()) ZuClear(body.data(), body.length());
    body.null();
    auto fn = ZuMv(complete);
    if (fn) fn(ServiceHTTPResponse{status, ZuMv(value)});
  }

  template <typename Link, typename Value>
  void process(Link *, const Value *value) const {
    finish(Value::Status, value->body);
  }
  template <typename Link> void failed(Link *) const {
    finish(0, {});
  }
};
using Call = Call_<ZmHeap<"Zum.pinghttp.Call", Call_<ZuVoid>>>;

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
  const String &bodyObject(const Call *call) const { return call->body; }
  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (Key{}() == "content-type")
      l(this->object->contentType);
    else if constexpr (Key{}() == "authorization")
      l(this->object->authorization);
    else if constexpr (Key{}() == "if-match") l(this->object->ifMatch);
    else if constexpr (Key{}() == "idempotency-key") l(this->object->idempotencyKey);
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
    return call->manifest.catalog;
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
	  buf, call->manifest.revision);
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

class Client;
class Pool;
template <typename Heap = ZuVoid> class Pool_;
ZmPQueueDerive(ReqBuilderQ, ReqBuilder_,
  ZmPQueueOverlap<false, ZmPQueueNode<ReqBuilder_,
    ZmPQueueHeapID<"Zum.PingHTTP.Request">>>);
using ReqBuilder = ReqBuilderQ::Node;
ZuDerive(TxQ, (ZmPQTx<Pool, ReqBuilderQ, ZmPQTxOrdered<false>>));

template <typename Heap>
class Pool_ : public Heap, public Zhttp::Pool<Client, TxQ, ResParser> {
  using Base = Zhttp::Pool<Client, TxQ, ResParser>;
public:
  Pool_(Client *client) : Base{client} { }
  ReqBuilderQ *txQueue() { return &m_requests; }
  void archive_(ReqBuilder *) { }
  ZmRef<ReqBuilder> retrieve_(ReqBuilderQ::Key, ReqBuilderQ::Key) {
    return {};
  }
private:
  ReqBuilderQ m_requests;
};

using PoolHeap = ZmHeap<"Zum.PingHTTP.Pool", Pool_<>>;
class Pool : public Pool_<PoolHeap> {
public:
  using Pool_<PoolHeap>::Pool_;
};

template <typename Heap>
class Client_ : public Heap, public ZmObject,
    public Zhttp::Client<Client_<Heap>, Pool> {
public:
  using Base = Zhttp::Client<Client_<Heap>, Pool>;

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
        config, Zhttp::TCPConfig{}, Zhttp::H2Config{}.caPath(caPath),
        Zhttp::QUICConfig{})) return false;
    Base::txErrorFn(ZiTxErrorFn{[](ZeException &e) {
      ZiLOG(Error, "zumd", ([e](auto &s) {
	s << "service HTTP transmit error: " << e;
      }));
      return false;
    }});
    return Base::pool(0, Zhttp::Destination{url.origin()}) && Base::start();
  }

  bool origin(const Zhttp::URLView &url) const {
    return (url.scheme == Zhttp::Scheme::https) == m_secure && url.host == m_host &&
      url.port == m_port && url.ipv6Literal == m_ipv6Literal;
  }

  void send(ServiceHTTPRequest request, ServiceHTTPDoneFn complete,
      const Zhttp::URLView &url) {
    ZmRef<Call> call = new Call{};
    call->contentType = ZuMv(request.contentType);
    call->authorization = ZuMv(request.authorization);
    call->body = ZuMv(request.body);
    call->manifest = ZuMv(request.manifest);
    call->ifMatch = ZuMv(request.ifMatch);
    call->idempotencyKey = ZuMv(request.idempotencyKey);
    call->complete = ZuMv(complete);
    if (url.path) {
      auto path = url.path;
      if (path[0] == '/') path.offset(1);
      call->target = path;
    }
    if (url.hasQuery) call->target << '?' << url.query;
    ZmRef<ReqBuilder> builder = new ReqBuilder{};
    builder->id = ++m_id;
    switch (request.method) {
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
using Client = Client_<ZmHeap<"Zum.pinghttp.Client", Client_<ZuVoid>>>;


} // namespace PingHTTP_

template <typename Heap>
class PingHTTPState_ : public Heap, public ZmObject  {
public:
  bool add_(ZiMultiplex *mx, const Zhttp::URLView &url, ZuCSpan caPath) {
    if (!url.host || url.hasFragment ||
        (url.scheme != Zhttp::Scheme::https &&
          !(url.scheme == Zhttp::Scheme::http &&
            (url.host == "localhost" || url.host == "127.0.0.1" || url.host == "::1"))))
      return false;
    for (auto &client: m_clients)
      if (client->origin(url)) return true;
    if (m_clients.length() >= PingHTTP_::DestinationMax) return false;
    ZmRef<PingHTTP_::Client> client = new PingHTTP_::Client{};
    if (!client->init(mx, url, caPath)) return false;
    m_clients.push(ZuMv(client));
    return true;
  }

  bool add_(ZiMultiplex *mx, ZuCSpan origin, ZuCSpan caPath) {
    Zhttp::URL parsed{origin};
    return parsed.ok() && add_(mx, parsed.url(), caPath);
  }

  bool init(ZiMultiplex *mx, ZuCSpan issuer, ZuCSpan managementIssuer,
      ZuCSpan management, ZuCSpan caPath) {
    m_mx = mx;
    m_caPath = caPath;
    m_resolverOwned = !ZiResolver::instance()->initialized();
    if (m_resolverOwned)
      ZiResolver::init(ZiResolverParams{}.timeoutMS(2000).tries(2));
    ZiResolver::start();
    if (!add_(mx, issuer, caPath) || !add_(mx, managementIssuer, caPath) ||
	!add_(mx, management, caPath)) {
      final();
      return false;
    }
    return true;
  }

  void send(ServiceHTTPRequest request, ServiceHTTPDoneFn complete) {
    Zhttp::URL parsed{request.url};
    auto url = parsed.url();
    if (!parsed.ok() || url.hasFragment) {
      complete(ServiceHTTPResponse{});
      return;
    }
    ZmRef<PingHTTP_::Client> client;
    for (auto &candidate: m_clients)
      if (candidate->origin(url)) { client = candidate; break; }
    if (!client) {
      if (!add_(m_mx, url, m_caPath)) {
	complete(ServiceHTTPResponse{});
	return;
      }
      for (auto &candidate: m_clients)
	if (candidate->origin(url)) { client = candidate; break; }
      if (!client) { complete(ServiceHTTPResponse{}); return; }
    }
    client->txRun(0, [client, request = ZuMv(request),
        complete = ZuMv(complete)]() mutable {
      Zhttp::URL parsed{request.url};
      auto url = parsed.url();
      if (!parsed.ok() || url.hasFragment || !client->origin(url)) {
	complete(ServiceHTTPResponse{});
	return;
      }
      client->send(ZuMv(request), ZuMv(complete), url);
    });
  }

  void final() {
    for (auto &client: m_clients) {
      client->stop();
      client->final();
    }
    m_clients.null();
    if (m_resolverOwned) {
      ZiResolver::stop();
      ZiResolver::final();
      m_resolverOwned = false;
    }
    m_caPath.null();
    m_mx = nullptr;
  }
private:
  ZtArray<ZmRef<PingHTTP_::Client>> m_clients;
  ZiMultiplex *m_mx = nullptr;
  String m_caPath;
  bool m_resolverOwned = false;
};
using PingHTTPState = PingHTTPState_<ZmHeap<"Zum.pinghttp.PingHTTPState", PingHTTPState_<ZuVoid>>>;

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
