//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Raw AF_XDP arena, pool and packet facilities.

#ifndef Zpkt_HH
#define Zpkt_HH

#ifndef ZpktLib_HH
#include <zlib/ZpktLib.hh>
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
#include <zlib/ZiIOContext.hh>

namespace Zpkt {

struct NifInfo {
  unsigned ifindex = 0, mtu = 0, rxQueues = 0;
  uint8_t mac[6]{};
};

using RecvFn = ZmFn<void(uint8_t *, unsigned, ZiRxBurst *), ZmFnHeapID<"Zpkt.Receive">>;
using StopFn = ZmFn<void(), ZmFnHeapID<"Zpkt.TxStopped">>;
using StartFn = ZmFn<void(bool), ZmFnHeapID<"Zpkt.Start">>;

class Env;

class Nif;

class ZpktAPI DeviceEngine : private ZmEngine<DeviceEngine> {
  using Base = ZmEngine<DeviceEngine>;
  friend Base;
  friend class Nif;

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
  ZmQueueDerive(Completions, Completion, ZmQueueHeapID<"Zpkt.Control">);
  using SpawnFn = ZmFn<void(), ZmFnHeapID<"Zpkt.Control">>;
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
class ZpktAPI Nif : public ZmPolymorph, public DeviceEngine {
friend class DeviceEngine;
friend class Env;

protected:
  using Engine = DeviceEngine;

public:
  ~Nif() override = default;
  Env *env() const { return m_env; }
  ZuCSpan name() const { return m_name; }
  Nif(const Nif &) = delete;
  Nif &operator =(const Nif &) = delete;

  // Pools retain backing independently of the device.
  virtual ZmRef<Zi::IOBufPool> bufPool(Zi::IOBufType::T type, unsigned count, unsigned size,
    unsigned headRoom = 0, unsigned tailRoom = 0,
    unsigned align = ZiIOBuf_DefltAlign) = 0;
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

  // Tx owner: encode only when no prior submission owns this slot. This
  // reserves nothing; encode and send on the same owner without resubmission.
  virtual bool txReady(uint8_t *packet) const = 0;
  // False acquires no kernel lease. After true, keep bytes immutable until
  // kernel retirement. One descriptor may be outstanding for each slot.
  virtual bool send(uint8_t *packet, unsigned length) = 0;
  // Active TCP checksum offload configuration, queried after start.
  virtual bool tcpChksumOffload() = 0;
  virtual void rx() = 0; // Rx owner, until wake() or device failure
  virtual void wake() = 0;
  virtual unsigned pollTx() = 0;
  virtual unsigned allQueues(Ztc::QueueMgr::AllFn) const { return 0; }

protected:
  Nif() = default;
  void retireNif();
  void resetRun() { takeRunFns(); m_everStarted = 0; }

  static ZuTime monotonicNow();
  static void signalWake(int fd, const ZmAtomic<unsigned> &parking);
  static void drainWake(int fd);
  static bool takeWake(ZmAtomic<uint64_t> &wakes);

private:
  virtual void start_() = 0;
  virtual void stop_() = 0;

  Env		*m_env = nullptr;
  ZtString<ZtStringHeapID<"Zpkt.CfString">> m_name;
  ZmAtomic<unsigned> m_everStarted{0};

public:
  virtual void status() = 0;
};

} // namespace Zpkt
#endif /* Zpkt_HH */
