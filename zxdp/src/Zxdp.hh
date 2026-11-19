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

#include <zlib/ZuDerive.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuTime.hh>
#include <zlib/ZuUnion.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmEngine.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmPLock.hh>
#include <zlib/ZmQueue.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmPolymorph.hh>

#include <zlib/ZtEnum.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZtcQueue.hh>
#include <zlib/ZiIOBuf.hh>

namespace Zxdp {

using RecvFn = ZmFn<void(uint8_t *, unsigned), ZmFnHeapID<"Zxdp.Receive">>;
using StopFn = ZmFn<void(), ZmFnHeapID<"Zxdp.TxStopped">>;
using StartFn = ZmFn<void(bool), ZmFnHeapID<"Zxdp.Start">>;

ZtEnumNS(ZxdpAPI, Type, uint8_t, Xdp, Dpdk);

namespace Xdp {
  struct Params {
    unsigned ifindex = 0; // network-interface index in the current network namespace
    ZuCSpan netmask; // optional destination CIDR; empty takes all traffic
    unsigned spin = 1000; // idle probes before Rx blocks or Tx service returns
    unsigned txSid = 0; // application-assigned sender slot
  };
}
namespace Dpdk {
  ZuDerive(DeviceString, (ZtString<ZtStringHeapID<"Zxdp.Dpdk.Device">>));

  struct Params_ {
    unsigned spin = 1000;
  };
  struct Params : public Params_ {
    DeviceString	name; // owned Ethernet device name
    unsigned		ifindex = 0;
    unsigned		rxInterval = 50; // maximum idle park in microseconds
  };
}
ZuDerive(Params, (ZuUnion<Xdp::Params, Dpdk::Params>));

class Instance;

class ZxdpAPI DeviceEngine : private ZmEngine<DeviceEngine> {
  using Base = ZmEngine<DeviceEngine>;
  friend Base;
  friend class Instance;

public:
  using Base::state;
  using Base::running;
  using Base::stopping;
  using Base::lock;
  bool stop();
  void stop(StartFn);
  bool stopped() { return Base::stopped(); }

protected:
  struct RunFns {
    RecvFn receive;
    StopFn txStopped;
  };

  void start(StartFn completion) { start({}, ZuMv(completion)); }
  void start(RunFns, StartFn);
  void started(bool ok);
  void stopped(bool ok);
  RunFns takeRunFns();

private:
  DeviceEngine() = default;

  struct Completion {
    StartFn	fn;
    bool	ok = false;
  };
  ZmQueueDerive(Completions, Completion, ZmQueueHeapID<"Zxdp.Control">);
  using SpawnFn = ZmFn<void(), ZmFnHeapID<"Zxdp.Control">>;
  struct Work {
    Completions	completions;
    RunFns	retired;
    SpawnFn	start;
    bool	wake = false;
  };

  template <typename Fn> void control(Fn);
  auto completion(StartFn);
  template <typename Fn> bool spawn(Fn &&fn) {
    m_work->start = ZuFwd<Fn>(fn);
    return true;
  }
  void wake() { m_work->wake = true; }
  void start_();
  void stop_();

  ZmPLock	m_controlLock;
  Work		*m_work = nullptr;
  RunFns	m_nextRun;
};

// The caller drives Rx/Tx on their respective single owners. Allocation/free
// may run on other threads. Allocation holds backing until explicit pool return;
// an accepted send also holds it until kernel completion or safe close.
class ZxdpAPI Instance : public ZmPolymorph, public DeviceEngine {
friend class DeviceEngine;

protected:
  using Engine = DeviceEngine;

public:
  ~Instance() override = default;
  Instance(const Instance &) = delete;
  Instance &operator =(const Instance &) = delete;

  // Pools are independent of the factory instance and can precede init().
  virtual ZmRef<Zi::IOBufPool> bufPool(Zi::IOBufType::T type, unsigned count, unsigned size,
    unsigned headRoom = 0, unsigned tailRoom = 0,
    unsigned align = ZiIOBuf_DefltAlign) = 0;
  // Open the link synchronously. Repeating the successful stopped configuration
  // preserves device state and ownership.
  virtual ZuUnion<void, ZeException> init(ZiMultiplex *, const Params &) = 0;

  // Internal Tx reserves can participate without joining the caller's intrusive list.
  virtual void reserveTxPools(ZuSpan<Zi::IOBufPool *const>) = 0;
  // Allocate and register backing once. The caller's pool nodes stay in its list.
  virtual ZuUnion<void, ZeException> alloc(
    Zi::IOBufPool *rxPool, const Zi::IOBufPoolList &txPools) = 0;

  virtual bool start(RecvFn, StopFn txStopped = {});
  virtual void start(RecvFn, StopFn txStopped, StartFn completion);
  // Stop drains service. While stopped, final releases device and backing resources.
  virtual void final() = 0;
  virtual ZeException error() const = 0; // inspect after start, stop or final

  virtual ZiMultiplex *mx() const = 0;
  virtual unsigned ifindex() const = 0;
  virtual unsigned rxQueues() const = 0;
  virtual bool allocated() const = 0;
  virtual bool everStarted() const { return m_everStarted; }
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
  virtual unsigned allQueues(Ztc::QueueMgr::AllFn) const = 0;

protected:
  Instance() = default;
  void resetRun() { takeRunFns(); m_everStarted = 0; }

  static ZuTime monotonicNow();
  static void signalWake(int fd, const ZmAtomic<unsigned> &parking);
  static void drainWake(int fd);
  static bool takeWake(ZmAtomic<uint64_t> &wakes);

private:
  virtual void start_() = 0;
  virtual void stop_() = 0;

  ZmAtomic<unsigned> m_everStarted{0};

public:
  virtual void status() = 0;
};

ZxdpExtern ZmRef<Instance> instance(Type::T);

} // namespace Zxdp
#endif /* Zxdp_HH */
