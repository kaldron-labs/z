//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include "ZumUpstream.hh"

#include <zlib/ZuDerive.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmPQueue.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiResolver.hh>

#include <zlib/ZhttpClient.hh>
#include <zlib/ZhttpURL.hh>
#include <zlib/ZrestClient.hh>

namespace Zum {
namespace Upstream_ {

enum {
  RequestTimeout = 15,
  ResponseMax = 64U<<10,
  Concurrency = 32
};

template <unsigned Status_>
struct ResponseData : public ZumObject {
  enum { Status = Status_ };
  String body;
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
  Response<409>, Response<415>, Response<422>, Response<429>, Response<500>,
  Response<501>, Response<502>, Response<503>, Response<504>>;

struct Call : public ZumObject {
  mutable String	contentType;
  mutable String	authorization;
  mutable String	target;
  mutable String	body;
  mutable OIDCHTTPDoneFn complete;
  mutable ZmAtomic<unsigned> done = 0;

  void finish(unsigned status, String value) const {
    if (done.cmpXch(1, 0)) return;
    if (authorization.mutable_())
      ZuClear(authorization.data(), authorization.length());
    authorization.null();
    if (body.mutable_()) ZuClear(body.data(), body.length());
    body.null();
    auto fn = ZuMv(complete);
    if (fn) fn(status, ZuMv(value));
  }

  template <typename Link, typename Value>
  void process(Link *, const Value *value) const {
    finish(Value::Status, String{value->body});
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
    "content-type", "authorization", "content-length");
  using Responses = Upstream_::Responses;

  const String &queryObject(const Call *call) const { return call->target; }
  const String &bodyObject(const Call *call) const { return call->body; }
  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (Key{}() == "content-type")
      l(this->object->contentType);
    else if constexpr (Key{}() == "authorization")
      l(this->object->authorization);
    else Base::template header<Key>(ZuFwd<L>(l));
  }
};

struct GET : public Request<GET, Zhttp::Method::GET,
    Zrest::BodyPolicy::None> { };
struct POST : public Request<POST, Zhttp::Method::POST,
    Zrest::BodyPolicy::Raw> { };
using Requests = ZuTypeList<GET, POST>;

struct ReqBuilder_ : public ZmObject, public Zrest::MReqBuilder<Requests> {
  using Reqs = Requests;
  uint64_t id = 0;
  uint64_t key() const { return id; }
  uint64_t length() const { return 1; }

  void selected(const Zhttp::Endpoint *endpoint, uint64_t, uint64_t,
      unsigned, unsigned, Zhttp::Transport::T transport,
      Zhttp::Version::T version) {
    ZiLOG(Debug, "zumd", ([ip = endpoint->ip, port = endpoint->port,
        transport, version](auto &s) {
      s << "upstream HTTP selected " << ip << ':' << port
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

struct ResParser : public Zrest::MResParser<ReqBuilder_> { };

class Client;
class Pool;
template <typename Heap = ZuVoid> class Pool_;
ZmPQueueDerive(ReqBuilderQ, ReqBuilder_,
  ZmPQueueOverlap<false, ZmPQueueNode<ReqBuilder_,
    ZmPQueueHeapID<"Zum.Upstream.Request">>>);
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

using PoolHeap = ZmHeap<"Zum.Upstream.Pool", Pool_<>>;
class Pool : public Pool_<PoolHeap> {
public:
  using Pool_<PoolHeap>::Pool_;
};

class Client : public ZumObject, public Zhttp::Client<Client, Pool> {
public:
  using Base = Zhttp::Client<Client, Pool>;

  bool init(ZiMultiplex *mx, const Zhttp::URLView &url, ZuCSpan caPath) {
    m_host = url.host;
    m_port = url.port;
    m_ipv6Literal = url.ipv6Literal;
    auto config = Zhttp::Config().links(2).concurrency(Concurrency)
      .linkMax(Concurrency).requestTimeout(RequestTimeout)
      .maxRetries(1).retainedBodyMax(ResponseMax)
      .protocol(Zhttp::ProtoPolicy::DisableH3)
      .secure(true).tcp(true).tls(true).quic(false);
    if (!Base::init(Zhttp::HubConfig{mx, "rx", "tx"}, 1,
        config, Zhttp::TCPConfig{}, Zhttp::H2Config{}.caPath(caPath),
        Zhttp::QUICConfig{})) return false;
    Base::txErrorFn(ZiTxErrorFn{[](ZeException &e) {
      ZiLOG(Error, "zumd", ([e](auto &s) {
	s << "upstream HTTP transmit error: " << e;
      }));
      return false;
    }});
    return Base::pool(0, Zhttp::Destination{url.origin()});
  }

  bool origin(const Zhttp::URLView &url) const {
    return url.scheme == Zhttp::Scheme::https && url.host == m_host &&
      url.port == m_port && url.ipv6Literal == m_ipv6Literal;
  }

  void send(OIDCHTTPRequest request, OIDCHTTPDoneFn complete,
      const Zhttp::URLView &url) {
    ZmRef<Call> call = new Call{};
    call->contentType = ZuMv(request.contentType);
    call->authorization = ZuMv(request.authorization);
    call->body = ZuMv(request.body);
    call->complete = ZuMv(complete);
    if (url.path) {
      ZuBSpan path = url.path;
      if (path[0] == '/') path.offset(1);
      call->target = path;
    }
    if (url.hasQuery) call->target << '?' << url.query;
    ZmRef<ReqBuilder> builder = new ReqBuilder{};
    builder->id = ++m_id;
    switch (request.method) {
      case OIDCHTTPMethod::GET:
        builder->template init<GET>(call.ptr());
        break;
      case OIDCHTTPMethod::POST:
        builder->template init<POST>(call.ptr());
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
};

struct Entry {
  ZmRef<Client> client;
  unsigned pending = 0;
  bool retiring = false;
};
ZuDerive(Clients,
  (ZtArray<Entry, ZtArrayHeapID<"Zum.Upstream.Clients">>));

} // namespace Upstream_

class UpstreamHTTPState : public ZumObject {
public:
  bool init(ZiMultiplex *mx, unsigned sid, unsigned origins, ZuCSpan caPath) {
    if (!mx || m_up || !sid || !origins || sid > mx->params().nThreads() ||
        sid == mx->rxThread() || sid == mx->txThread()) return false;
    m_mx = mx;
    m_sid = sid;
    m_origins = origins;
    m_caPath = caPath;
    m_resolverOwned = !ZiResolver::instance()->initialized();
    if (m_resolverOwned)
      ZiResolver::init(ZiResolverParams{}.timeoutMS(2000).tries(2));
    ZiResolver::start();
    m_up = true;
    return true;
  }

  void send(OIDCHTTPRequest request, OIDCHTTPDoneFn complete) {
    m_mx->run([this, request = ZuMv(request),
        complete = ZuMv(complete)]() mutable {
      send_(ZuMv(request), ZuMv(complete));
    }, m_sid);
  }

  void final() {
    if (!m_mx) return;
    ZmBlock<bool>{}([this](auto wake) mutable {
      m_mx->run([this, wake = ZuMv(wake)]() mutable {
	m_up = false;
	m_stopDone = ZuMv(wake);
	m_stopping = m_clients.length();
	if (!m_stopping) { stopped_(); return; }
	for (auto &entry: m_clients) {
	  if (entry.retiring) continue;
	  if (!entry.client) { stopOne_(); continue; }
	  entry.client->stop([this](bool) {
	    m_mx->run([this]() {
	      stopOne_();
	    }, m_sid);
	  });
	}
      }, m_sid);
    });
    for (auto &entry: m_clients) if (entry.client) entry.client->final();
    m_clients.init();
    if (m_resolverOwned) {
      ZiResolver::stop();
      ZiResolver::final();
      m_resolverOwned = false;
    }
    m_mx = nullptr;
  }

private:
  void send_(OIDCHTTPRequest request, OIDCHTTPDoneFn complete) {
    if (!m_up || !complete || !request.url) {
      if (complete) complete(0, String{});
      return;
    }
    Zhttp::URL parsed{request.url};
    auto url = parsed.url();
    if (!parsed.ok() || url.scheme != Zhttp::Scheme::https || !url.host ||
        url.hasFragment) {
      complete(0, String{});
      return;
    }
    unsigned count = m_clients.length(), idle = count;
    for (unsigned i = 0; i < count; ++i) {
      const auto &entry = m_clients[i];
      if (entry.retiring) continue;
      if (entry.client && entry.client->origin(url)) {
	dispatch_(i, ZuMv(request), ZuMv(complete), url);
	return;
      }
      if (!entry.pending && idle == count) idle = i;
    }
    if (count < m_origins) {
      m_clients.push(Upstream_::Entry{});
      dispatch_(count, ZuMv(request), ZuMv(complete), url);
      return;
    }
    if (idle == count) { complete(503, String{}); return; }
    auto &entry = m_clients[idle];
    if (!entry.client) {
      dispatch_(idle, ZuMv(request), ZuMv(complete), url);
      return;
    }
    entry.retiring = true;
    entry.client->stop([this, idle, request = ZuMv(request),
        complete = ZuMv(complete)](bool ok) mutable {
      m_mx->run([this, idle, ok, request = ZuMv(request),
          complete = ZuMv(complete)]() mutable {
	auto &entry = m_clients[idle];
	entry.retiring = false;
	if (!m_up) {
	  complete(0, String{});
	  stopOne_();
	  return;
	}
	// The native stop continuation has drained Rx/Tx; finalization does not wait.
	entry.client->final();
	entry.client = nullptr;
	if (!ok) { complete(0, String{}); return; }
	Zhttp::URL parsed{request.url};
	dispatch_(idle, ZuMv(request), ZuMv(complete), parsed.url());
      }, m_sid);
    });
  }

  void dispatch_(unsigned slot, OIDCHTTPRequest request,
      OIDCHTTPDoneFn complete, const Zhttp::URLView &url) {
    auto &entry = m_clients[slot];
    if (!entry.client) {
      ZmRef<Upstream_::Client> candidate = new Upstream_::Client{};
      if (!candidate->init(m_mx, url, m_caPath)) {
        candidate->final();
        complete(0, String{});
        return;
      }
      entry.client = ZuMv(candidate);
    }
    ++entry.pending;
    entry.client->start([this, slot, request = ZuMv(request),
        complete = ZuMv(complete)](bool ok) mutable {
      m_mx->run([this, slot, ok, request = ZuMv(request),
          complete = ZuMv(complete)]() mutable {
	if (!ok || !m_up) {
	  --m_clients[slot].pending;
	  complete(0, String{});
	  return;
	}
	Zhttp::URL parsed{request.url};
	m_clients[slot].client->send(ZuMv(request), [this, slot,
	    complete = ZuMv(complete)](unsigned status, String body) mutable {
	  m_mx->run([this, slot, status, body = ZuMv(body),
	      complete = ZuMv(complete)]() mutable {
	    --m_clients[slot].pending;
	    complete(status, ZuMv(body));
	  }, m_sid);
	}, parsed.url());
      }, m_sid);
    });
  }

  void stopOne_() {
    if (!--m_stopping) stopped_();
  }

  void stopped_() {
    auto complete = ZuMv(m_stopDone);
    complete(true);
  }

  ZiMultiplex		*m_mx = nullptr;
  Upstream_::Clients	m_clients;
  String		m_caPath;
  ZmFn<void(bool)>	m_stopDone;
  unsigned		m_sid = 0;
  unsigned		m_origins = 0;
  unsigned		m_stopping = 0;
  bool			m_resolverOwned = false;
  bool			m_up = false;
};

UpstreamHTTP::UpstreamHTTP() = default;
UpstreamHTTP::~UpstreamHTTP() { final(); }

bool UpstreamHTTP::init(ZiMultiplex *mx, unsigned sid, unsigned origins,
    ZuCSpan caPath)
{
  if (m_state) return false;
  ZmRef<UpstreamHTTPState> state = new UpstreamHTTPState{};
  if (!state->init(mx, sid, origins, caPath)) return false;
  m_state = ZuMv(state);
  return true;
}

OIDCHTTPFn UpstreamHTTP::fn() const
{
  if (!m_state) return {};
  return OIDCHTTPFn{[state = m_state](
      OIDCHTTPRequest request, OIDCHTTPDoneFn complete) mutable {
    state->send(ZuMv(request), ZuMv(complete));
  }};
}

void UpstreamHTTP::final()
{
  if (!m_state) return;
  auto state = ZuMv(m_state);
  state->final();
}

} // namespace Zum
