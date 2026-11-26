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
#include <zlib/ZuIOResult.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuTime.hh>
#include <zlib/ZuUnion.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmEngine.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmPolymorph.hh>

#include <zlib/ZtEnum.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZtcQueue.hh>
#include <zlib/ZiIOContext.hh>

namespace Zpkt {

ZuDerive(CfString, (ZtString<ZtStringHeapID<"Zpkt.CfString">>));

ZtFlagsNS(ZpktAPI, NifFlags, uint64_t, ZeroCopy);

struct NifInfo {
  unsigned ifindex = 0, mtu = 0, rxQueues = 0;
  uint8_t mac[6]{};
};

using RecvFn = ZmFn<void(uint8_t *, unsigned, ZiRxBurst *), ZmFnHeapID<"Zpkt.Receive">>;
using StopFn = ZmFn<void(), ZmFnHeapID<"Zpkt.TxStopped">>;
class Env;
class Nif;

// Engine control wakes use ZmEngine's inline stop poll. Scheduler wakes on
// the public Nif interrupt the receive owner independently.
class ZpktAPI DeviceEngine : protected ZmEngine<DeviceEngine> {
  friend ZmEngine<DeviceEngine>;
  friend class Nif;
  void start_();
  void stop_();
};

using StartFn = ZmEngine<DeviceEngine>::CtrlFn;

struct TxPacket {
  uint8_t	*data;
  unsigned	length;
};

enum { MaxTxBatch = 64 };

// The caller drives Rx/Tx on their respective single owners. Allocation/free
// may run on other threads. Allocation holds backing until explicit pool return;
// an accepted send also holds it until kernel completion or safe close.
class ZpktAPI Nif : public ZmPolymorph, protected DeviceEngine {
friend class DeviceEngine;
friend class Env;

  Nif(const Nif &) = delete;
  Nif &operator =(const Nif &) = delete;

public:
  using Engine = DeviceEngine;
  using Engine::state;
  using Engine::running;
  using Engine::stopping;
  using Engine::lock;

protected:
  Nif(ZiMultiplex *, const NifInfo &);
public:
  ~Nif() override = default;

  Env *env() const { return m_env; }
  ZiMultiplex *mx() const { return m_mx; }
  ZuCSpan name() const { return m_name; }
  unsigned ifindex() const { return m_ifindex; }
  unsigned mtu() const { return m_mtu; }
  unsigned rxQueues() const { return m_rxQueues; }
  ZuBSpan mac() const { return m_mac; }

  bool zeroCopy() const { return m_flags & NifFlags::ZeroCopy(); }
  // Active TCP checksum offload configuration, queried after start.
  bool tcpChksumOffload() const { return ready() && m_tcpChksumOffload; }

  bool start(RecvFn, StopFn txStopped = {});
  void start(RecvFn, StopFn txStopped, StartFn completion);
  bool stop();
  void stop(StartFn);
  ZeException error() const { return m_error; } // inspect after start, stop or final

  // Pools retain backing independently of the device.
  virtual ZmRef<Zi::IOBufPool> bufPool(
    Zi::IOBufType::T type, unsigned count, unsigned size,
    unsigned headRoom = 0, unsigned tailRoom = 0,
    unsigned align = ZiIOBuf_DefltAlign) = 0;
  // Internal Tx reserves can participate without joining the caller's intrusive list.
  virtual void reserveTxPools(ZuSpan<Zi::IOBufPool *const>) = 0;
  // Allocate and register backing once. The caller's pool nodes stay in its list.
  virtual ZuUnion<void, ZeException> alloc(
    Zi::IOBufPool *rxPool, const Zi::IOBufPoolList &txPools) = 0;

  // Stop drains service. While stopped, final releases device and backing resources.
  virtual void final() = 0;
  virtual bool allocated() const = 0;
  virtual bool ready() const = 0;

  virtual void rx() = 0; // Rx owner, until wake() or device failure

  // Tx owner: encode only when no prior submission owns this slot. This
  // reserves nothing; encode and send on the same owner without resubmission.
  virtual Zu::IO::Result txReady(uint8_t *packet) const = 0;
  // OK acquires the device lease. Keep bytes immutable until retirement.
  // NotReady indicates temporary pressure. IOError indicates terminal failure.
  virtual Zu::IO::Result send(uint8_t *packet, unsigned length) = 0;
  // Tx owner: accepts exactly the leading entries up to MaxTxBatch.
  // Refused entries retain their caller leases and can be resubmitted.
  virtual unsigned send(ZuSpan<const TxPacket>) = 0;
  virtual unsigned pollTx() = 0;

  virtual void status() = 0;

  virtual void wake();
  virtual unsigned allQueues(Ztc::QueueMgr::AllFn) const { return 0; }

protected:
  void rxQueues(unsigned count) { m_rxQueues = count; }
  void zeroCopy(bool on) {
    constexpr auto bit = NifFlags::ZeroCopy();
    if (on) m_flags |= bit;
    else m_flags &= ~bit;
  }
  void tcpChksumOffload(bool on) { m_tcpChksumOffload = on; }
  bool configuredTCPChksumOffload() const { return m_tcpChksumOffload; }

  void resetInfo();
  void final_();
  void error(ZeException e) { m_error = ZuMv(e); }
  bool failed() const { return bool(m_error); }

  bool receiving() const { return bool(m_receive); }
  void recv(uint8_t *packet, unsigned length, ZiRxBurst *burst) {
    m_receive(packet, length, burst);
  }
  void clearCallbacks() { m_receive = {}; m_txStopped = {}; }
  void notifyTxStopped() { if (m_txStopped) m_txStopped(); }
  void finishCallbacks() {
    auto txStopped = ZuMv(m_txStopped);
    m_receive = {};
    if (txStopped) txStopped();
  }

  static ZuTime monotonicNow();
  int wakeFD() const { return m_wakeFD; }
  void wakeFD(int fd) { m_wakeFD = fd; }
  void closeWake();
  void wake_(); // interrupt a kernel wait without consuming a scheduler turn
  void ackWake();
  void resetWake() { m_wakes = m_blocking = 0; }
  bool wakePending() const { return bool(m_wakes); }
  bool wakePendingRelaxed() const { return m_wakes.load_(); }
  bool woken() {
    if (!m_wakes) return false;
    --m_wakes;
    return true;
  }
  void startBlocking() { m_blocking.xch(1); }
  void stopBlocking() { m_blocking = 0; }

private:
  virtual void start_() = 0;
  virtual void stop_() = 0;

  void startRun();
  void env(Env *env, ZuCSpan name) { m_env = env; m_name = name; }
  void env(Env *env) { m_env = env; }

  struct Callbacks {
    RecvFn		receive;
    StopFn		txStopped;
  };

  ZiMultiplex		*m_mx;
  uint64_t		m_flags = 0;
  unsigned		m_ifindex;
  unsigned		m_mtu;
  unsigned		m_rxQueues;
  uint8_t		m_mac[6];
  bool			m_tcpChksumOffload = false;

  ZeException		m_error;
  RecvFn		m_receive;
  StopFn		m_txStopped;

  int			m_wakeFD = -1;

  // Only the receive owner consumes wakes. Publish blocking before its last
  // check so a concurrent producer either prevents the wait or interrupts it.
  alignas(Zm::CacheLineSize)
  ZmAtomic<uint64_t>	m_wakes{0};
  ZmAtomic<unsigned>	m_blocking{0};

  Env			*m_env = nullptr;
  CfString		m_name;

  Callbacks		m_next;
};

} // namespace Zpkt
#endif /* Zpkt_HH */
