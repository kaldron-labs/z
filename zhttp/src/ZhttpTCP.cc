//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include "ZhttpServerImpl.hh"
#include "ZhttpClientImpl.hh"

namespace Zhttp {

struct TCPBackend {
  static bool server(ServerCore &, const HubConfig &, unsigned);
  static bool pool(PoolCore &, const PoolInit &, unsigned);
};

bool TCPBackend::server(ServerCore &owner, const HubConfig &hub, unsigned)
{
  auto &server = owner.impl();
  if (!server.config().tcpEnabled()) return true;
  using Hub = ServerImpl::Hub<H1TCP>;
  auto native = static_cast<Hub *>(server.tcpBackend().ctx);
  if (!native) {
    native = new Hub{&server};
    server.tcpBackend().own(native);
  }
  if (server.hubs().init(*native, hub, server.config().tcpConfig())) return true;
  native->final();
  return false;
}

class TCPPool : public ZmNHeap<TCPPool>,
  public ClientPool<PoolImpl, H1TCP, PoolImpl::LiveReq,
    RequestRecord, BoundResParser> {
  friend inline ZuStringT<"Zhttp.TCP.Pool"> ZmNHeapID(TCPPool *);
public:
  TCPPool(PoolImpl *owner) : ClientPool{owner} { }
};

bool TCPBackend::pool(PoolCore &owner, const PoolInit &config, unsigned)
{
  auto &pool = owner.impl();
  if (pool.config().secure()) return true;
  auto native = static_cast<TCPPool *>(pool.tcpBackend().ctx);
  if (!native) {
    native = new TCPPool{&pool};
    pool.tcpBackend().own(native);
    static constexpr PoolBackendOps ops{
      .open = [](void *ctx, void *request, unsigned slot) {
	static_cast<TCPPool *>(ctx)->open(
	  static_cast<PoolImpl::LiveReq *>(request), slot);
      },
      .cancel = [](void *ctx, void *request) {
	return static_cast<TCPPool *>(ctx)->cancel(
	  static_cast<PoolImpl::LiveReq *>(request));
      },
      .txFailed = [](void *ctx, unsigned slot, uint64_t generation) {
	static_cast<TCPPool *>(ctx)->txFailed(slot, generation);
      }
    };
    pool.tcpBackend().ops = &ops;
  }
  if (!pool.hubs().init(*native, config.hub, config.tcp)) {
    native->final();
    return false;
  }
  native->slots(pool.config().links(), pool.config().concurrency());
  return true;
}

const ProfileDesc &h1TCP()
{
  static constexpr ProfileDesc desc{
    Transport::TCP, 1U << Version::H1, TCPBackend::server, TCPBackend::pool};
  return desc;
}

} // namespace Zhttp
