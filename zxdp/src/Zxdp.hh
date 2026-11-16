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
#include <zlib/ZuUnion.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmScheduler.hh>
#include <zlib/ZmPolymorph.hh>
#include <zlib/ZtcQueue.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZePlatform.hh>
#include <zlib/ZxdpAlloc.hh>

namespace Zxdp {

using CommitFn = ZmFn<bool()>;

using RecvFn = ZmFn<void(uint8_t *, unsigned), ZmFnHeapID<"Zxdp.Receive">>;
using StopFn = ZmFn<void(), ZmFnHeapID<"Zxdp.TxStopped">>;

struct Params {
  unsigned interface = 0; // network-interface index in the current namespace
  ZuCSpan netmask;       // optional destination CIDR; empty takes all traffic
  BufPool *txPools = nullptr;
  BufPool *rxPool = nullptr;
  RecvFn rx;
  unsigned spin = 1000; // idle probes before Rx blocks or Tx service returns
  ZmScheduler *scheduler = nullptr; // caller-owned scheduler
  unsigned txSid = 0; // application-assigned sender slot
  ZmSchedTParams txThread = ZmSchedTParams().isolated(true); // apply to slot before start
  StopFn txStopped; // optional owner continuation after sender callbacks drain
};

// The caller drives Rx/Tx on their respective single owners. Allocation/free
// may run on other threads. Allocation holds backing until explicit pool return;
// an accepted send also holds it until kernel completion or safe close.
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
  ZuBSpan mac() const;
  // False acquires no kernel lease. After true, keep bytes immutable until
  // kernel retirement. Pool return may precede retirement; recycling waits for
  // both the allocation lease and kernel lease to end. One descriptor per slot.
  // Borrowed synchronous commit hook: false cancels admission without a lease;
  // true promises immediate descriptor publication with no further refusal.
  bool send(uint8_t *packet, unsigned length, const CommitFn &commit = {});
  // Process packets on the Rx owner until woken (or the device fails).
  // Idle service spins Params::spin rounds, then blocks internally in epoll.
  void rx();
  // Any thread, including the receive callback. Each posted wake returns one
  // rx() call; a wake before entry remains pending. No per-wake allocation.
  void wake();
  unsigned pollTx(); // number of kernel submissions retired this turn
  // Synchronous enumeration; consume telemetry inside the callback. Closed
  // instances retain final ring views until destruction, without live mappings.
  unsigned allQueues(Ztc::QueueMgr::AllFn) const;
  bool stopSender(); // quiesce sender slot; completion arrives through txStopped

  // Wake/join Rx and quiesce Tx and all wake callers first. An exception result
  // preserves the handle for retry; only successful kernel retirement permits
  // pending backing to return.
  // Void means success; an empty exception means retirement is pending.
  ZuUnion<void, ZeException> close();

private:
  bool send_(uint8_t *packet, unsigned length, const CommitFn &commit);
  explicit Instance(InstanceData *data) : m_data{data} { }
  InstanceData *m_data;
};

} // namespace Zxdp
#endif /* Zxdp_HH */
