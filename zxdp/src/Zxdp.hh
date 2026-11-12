//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Raw AF_XDP arena, pool and packet facilities.

#ifndef Zxdp_HH
#define Zxdp_HH

#ifndef ZxdpLib_HH
#include <zlib/ZxdpLib.hh>
#endif

#include <stdint.h>
#include <zlib/ZuSpan.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmPolymorph.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZxdpAlloc.hh>

namespace Zxdp {

using RecvFn = ZmFn<void(uint8_t *, unsigned), ZmFnHeapID<"Zxdp.Receive">>;
// Canonical backing and retired submission count. Keep repeated submissions
// immutable until all completions; close may coalesce their remaining counts.
using CompleteFn = ZmFn<void(uint8_t *, unsigned), ZmFnHeapID<"Zxdp.Complete">>;

struct Params {
  unsigned interface = 0; // network-interface index in the current namespace
  ZuCSpan netmask;       // optional destination CIDR; empty takes all traffic
  BufPool *txPools = nullptr;
  BufPool *rxPool = nullptr;
  RecvFn rx;
  CompleteFn txCompleted;
  unsigned spin = 1000; // empty receive probe rounds before blocking in epoll
};

// The caller drives Rx/Tx on their respective single owners. Allocation/free
// may run on other threads. Backing stays owned by the caller through completion.
class ZxdpAPI Instance : public ZmPolymorph {
friend struct InstanceData;

public:
  // Normally null on failure. A non-ready handle with an error retains any
  // pending kernel rollback; keep it and retry close until it completes.
  static ZmRef<Instance> create(Params, ZtString<> &error);
  ~Instance() override;
  Instance(const Instance &) = delete;
  Instance &operator =(const Instance &) = delete;

  bool ready() const;
  unsigned ifindex() const;
  unsigned mtu() const;
  bool zeroCopy() const;
  const uint8_t *mac() const;
  bool send(uint8_t *packet, unsigned length);
  // Process packets on the Rx owner until woken (or the device fails).
  // Idle service spins Params::spin rounds, then blocks internally in epoll.
  void rx();
  // Any thread, including the receive callback. Each posted wake returns one
  // rx() call; a wake before entry remains pending. No per-wake allocation.
  void wake();
  unsigned pollTx();

  // Wake/join Rx and quiesce Tx and all wake callers first. A false result preserves the handle for
  // retry; only successful kernel retirement permits pending backing to return.
  bool close(ZtString<> &error);

private:
  explicit Instance(InstanceData *data) : m_data{data} { }
  InstanceData *m_data;
};

} // namespace Zxdp
#endif /* Zxdp_HH */
