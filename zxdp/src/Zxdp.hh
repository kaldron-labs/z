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

#include <zlib/ZtEnum.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZtcQueue.hh>
#include <zlib/ZiIOBuf.hh>

namespace Zxdp {

using RecvFn = ZmFn<void(uint8_t *, unsigned), ZmFnHeapID<"Zxdp.Receive">>;
using StopFn = ZmFn<void(), ZmFnHeapID<"Zxdp.TxStopped">>;

ZtEnumNS(ZxdpAPI, Type, uint8_t, Xdp);

struct Params_ {
  Zi::IOBufPoolList txPools;
  ZmRef<Zi::IOBufPool> rxPool;
  RecvFn rx;
  unsigned spin = 1000; // idle probes before Rx blocks or Tx service returns
  ZmScheduler *scheduler = nullptr; // caller-owned scheduler
  StopFn txStopped; // optional owner continuation after sender callbacks drain
};

namespace Xdp {
  struct Params : public Params_ {
    unsigned ifindex = 0; // network-interface index in the current network namespace
    ZuCSpan netmask; // optional destination CIDR; empty takes all traffic
    unsigned txSid = 0; // application-assigned sender slot
    ZmSchedTParams txThread = ZmSchedTParams().isolated(true);
  };
}
ZuDerive(Params, (ZuUnion<Xdp::Params>));

// The caller drives Rx/Tx on their respective single owners. Allocation/free
// may run on other threads. Allocation holds backing until explicit pool return;
// an accepted send also holds it until kernel completion or safe close.
class ZxdpAPI Instance : public ZmPolymorph {
public:
  ~Instance() override = default;
  Instance(const Instance &) = delete;
  Instance &operator =(const Instance &) = delete;

  // Pools are independent of the factory instance and can precede init().
  virtual ZmRef<Zi::IOBufPool> bufPool(unsigned count, unsigned size,
    unsigned headRoom = 0, unsigned tailRoom = 0,
    unsigned align = ZiIOBuf_DefltAlign) = 0;
  // Initialize once. Retain the handle after failure and close it to finish
  // rollback. Configuration may expire on return; pool references are retained.
  virtual ZuUnion<void, ZeException> init(const Params &) = 0;

  virtual bool ready() const = 0;
  virtual unsigned mtu() const = 0;
  virtual bool zeroCopy() const = 0;
  virtual ZuBSpan mac() const = 0;

  // False acquires no kernel lease. After true, keep bytes immutable until
  // kernel retirement. One descriptor may be outstanding for each slot.
  virtual bool send(uint8_t *packet, unsigned length) = 0;
  virtual void rx() = 0; // Rx owner, until wake() or device failure
  virtual void wake() = 0;
  virtual unsigned pollTx() = 0;
  virtual bool stopTx() = 0;
  // Quiesce Rx, Tx and wake callers first. True means closed, false means
  // retirement is pending; an error never authorizes releasing kernel backing.
  virtual ZuUnion<bool, ZeException> close() = 0;

  virtual unsigned allQueues(Ztc::QueueMgr::AllFn) const = 0;

protected:
  Instance() = default;
};

ZxdpExtern ZmRef<Instance> instance(Type::T);

} // namespace Zxdp
#endif /* Zxdp_HH */
