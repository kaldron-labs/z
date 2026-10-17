//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include "ZhttpServerImpl.hh"
#include "ZhttpClientImpl.hh"

namespace Zhttp {

class QUICPool final : public ZmNHeap<QUICPool>,
  public ClientPool<PoolImpl, H3QUIC, PoolImpl::LiveReq, RequestRecord, BoundResParser> {
  friend inline ZuStringT<"Zhttp.QUIC.Pool"> ZmNHeapID(QUICPool *);
public:
  QUICPool(PoolImpl *owner) : ClientPool{owner} { }
};

struct QUICBackend {
  static bool server(ServerCore &, const HubConfig &, unsigned);
  static bool pool(PoolCore &, const PoolInit &, unsigned);
};

bool QUICBackend::server(ServerCore &owner, const HubConfig &hub, unsigned)
{
  auto &server = owner.impl();
  if (!server.config().quicEnabled()) return true;
  const auto &quic = server.config().quicHubConfig();
  owner.seeds(&server, [](void *ctx, const CatalogView &catalog) {
    auto &server = *static_cast<ServerImpl *>(ctx);
    const auto &quic = server.config().quicHubConfig();
    auto params = H3::Params{}.qpackLimits({quic.qpackRxCapacity(), quic.qpackTxCapacity(),
      quic.qpackRxBlocked(), quic.qpackTxSections()});
    server.qpackCatalog().add(catalog, params, quic.qpackTxCapacity());
  });
  using Hub = ServerImpl::Hub<H3QUIC>;
  auto native = static_cast<Hub *>(server.quicBackend().ctx);
  if (!native) {
    native = new Hub{&server};
    server.quicBackend().own(native);
  }
  if (server.hubs().init(*native, hub, quic)) return true;
  native->final();
  return false;
}

bool QUICBackend::pool(PoolCore &owner, const PoolInit &config, unsigned)
{
  auto &pool = owner.impl();
  if (!pool.config().secure() || !pool.config().quic()) return true;
  const auto &quic = config.quic;
  auto params = H3::Params{}.qpackLimits({quic.qpackRxCapacity(), quic.qpackTxCapacity(),
    quic.qpackRxBlocked(), quic.qpackTxSections()});
  pool.qpackCatalog().add(owner.requestCatalog(), params, quic.qpackTxCapacity());
  auto native = static_cast<QUICPool *>(pool.quicBackend().ctx);
  if (!native) {
    native = new QUICPool{&pool};
    pool.quicBackend().own(native);
    pool.quicBackend().print = [](void *ctx) { static_cast<QUICPool *>(ctx)->printDiag(); };
    static constexpr PoolBackendOps ops{
      [](void *ctx, void *request, unsigned slot) {
	static_cast<QUICPool *>(ctx)->open(static_cast<PoolImpl::LiveReq *>(request), slot);
      },
      [](void *ctx, void *request) {
	return static_cast<QUICPool *>(ctx)->cancel(static_cast<PoolImpl::LiveReq *>(request));
      },
      [](void *ctx, unsigned slot, uint64_t generation) {
	static_cast<QUICPool *>(ctx)->txFailed(slot, generation);
      }};
    pool.quicBackend().ops = &ops;
  }
  if (pool.hubs().init(*native, config.hub, quic)) return true;
  native->final();
  return false;
}

const ProfileDesc &h3QUIC()
{
  static constexpr ProfileDesc desc{
    Transport::QUIC, 1U << Version::H3, QUICBackend::server, QUICBackend::pool};
  return desc;
}

} // namespace Zhttp
