//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include "ZhttpTLSImpl.hh"
#include "ZhttpClientImpl.hh"

namespace Zhttp {

template <typename Profile>
class TLSPool final : public ZmNHeap<TLSPool<Profile>>,
  public H1ClientPool<PoolImpl, Profile, PoolImpl::LiveReq,
    RequestRecord, BoundResParser> {
  friend inline ZuStringT<"Zhttp.TLS.Pool"> ZmNHeapID(TLSPool *);
public:
  using Base = H1ClientPool<PoolImpl, Profile, PoolImpl::LiveReq,
    RequestRecord, BoundResParser>;
  TLSPool(PoolImpl *owner) : Base{owner} { }
};

class TLSBothPool final : public ZmNHeap<TLSBothPool>,
  public TLSClientPool<PoolImpl, PoolImpl::LiveReq, RequestRecord, BoundResParser> {
  friend inline ZuStringT<"Zhttp.TLS.Shared"> ZmNHeapID(TLSBothPool *);
public:
  TLSBothPool(PoolImpl *owner) : TLSClientPool{owner} { }
};

// Keep native hub contexts until endpoint destruction, including across final()
// and policy changes, as the original by-value hubs did for retained links.
struct TLSState : public ZmNHeap<TLSState> {
  Backend h1, h2, both;

  friend inline ZuStringT<"Zhttp.TLS.State"> ZmNHeapID(TLSState *);
};

struct TLSBackend {
  static bool server(ServerCore &, const HubConfig &, unsigned);
  static bool pool(PoolCore &, const PoolInit &, unsigned);

  template <typename Native>
  static bool server_(ServerImpl &server, Backend &context,
      const HubConfig &hub, const H2Config &config) {
    auto native = static_cast<Native *>(context.ctx);
    if (!native) { native = new Native{&server}; context.own(native); }
    server.tlsBackend().ctx = native;
    if (server.hubs().init(*native, hub, config)) return true;
    native->final();
    return false;
  }
  template <typename Native, bool H1 = false>
  static bool pool_(PoolImpl &pool, Backend &context, const PoolInit &config, H2Config tls) {
    auto native = static_cast<Native *>(context.ctx);
    if (!native) {
      native = new Native{&pool};
      context.own(native);
    }
    static const PoolBackendOps ops = []() {
	PoolBackendOps ops{
	  [](void *ctx, void *request, unsigned slot) {
	    static_cast<Native *>(ctx)->open(static_cast<PoolImpl::LiveReq *>(request), slot);
	  },
	  [](void *ctx, void *request) {
	    return static_cast<Native *>(ctx)->cancel(static_cast<PoolImpl::LiveReq *>(request));
	  }, nullptr};
	ops.txFailed = [](void *ctx, unsigned slot, uint64_t generation) {
	  static_cast<Native *>(ctx)->txFailed(slot, generation);
	};
	return ops;
      }();
    pool.tlsBackend().ops = &ops;
    pool.tlsBackend().ctx = native;
    if (!pool.hubs().init(*native, config.hub, tls)) { native->final(); return false; }
    if constexpr (H1) native->slots(pool.config().links(), pool.config().concurrency());
    return true;
  }
};

bool TLSBackend::server(ServerCore &owner, const HubConfig &hub, unsigned versions)
{
  auto &server = owner.impl();
  if (!server.config().tlsEnabled()) return true;
  H2Config tls = server.config().tlsConfig();
  if (versions == (1U << Version::H1)) tls.policy(H2Policy::Disable);
  else if (versions == (1U << Version::H2)) tls.policy(H2Policy::Force);
  auto state = static_cast<TLSState *>(server.tlsBackend().allocation);
  if (!state) { state = new TLSState; server.tlsBackend().own(state); }
  if (tls.policy() == H2Policy::Disable) {
    if (!TLS_::valid(tls)) { server.hubs().final(); return false; }
    return server_<ServerImpl::Hub<H1TLS>>(server, state->h1, hub, tls);
  }
  owner.seeds(&server, [](void *ctx, const CatalogView &catalog) {
    auto &server = *static_cast<ServerImpl *>(ctx);
    server.hpackCatalog().add(catalog, server.config().tlsConfig().hpackTxCapacity());
  });
  if (tls.policy() == H2Policy::Force)
    return server_<ServerImpl::TLSHub>(server, state->h2, hub, tls);
  return server_<ServerImpl::TLSHub>(server, state->both, hub, tls);
}

bool TLSBackend::pool(PoolCore &owner, const PoolInit &config, unsigned versions)
{
  auto &pool = owner.impl();
  if (!pool.config().secure() || !pool.config().tls()) return true;
  H2Config tls = config.tls;
  if (versions == (1U << Version::H1)) tls.policy(H2Policy::Disable);
  else if (versions == (1U << Version::H2)) tls.policy(H2Policy::Force);
  auto state = static_cast<TLSState *>(pool.tlsBackend().allocation);
  if (!state) { state = new TLSState; pool.tlsBackend().own(state); }
  if (tls.policy() == H2Policy::Disable)
    return pool_<TLSPool<H1TLS>, true>(pool, state->h1, config, ZuMv(tls));
  pool.hpackCatalog().add(owner.requestCatalog(), tls.hpackTxCapacity());
  if (tls.policy() == H2Policy::Force)
    return pool_<TLSBothPool>(pool, state->h2, config, ZuMv(tls));
  return pool_<TLSBothPool>(pool, state->both, config, ZuMv(tls));
}

const ProfileDesc &h1TLS()
{
  static constexpr ProfileDesc desc{
    Transport::TLS, 1U << Version::H1, TLSBackend::server, TLSBackend::pool};
  return desc;
}
const ProfileDesc &h2TLS()
{
  static constexpr ProfileDesc desc{
    Transport::TLS, 1U << Version::H2, TLSBackend::server, TLSBackend::pool};
  return desc;
}

} // namespace Zhttp
