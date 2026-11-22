//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Private DPDK Ethernet device and queue service.

#ifndef ZxdpDpdkInstance_HH
#define ZxdpDpdkInstance_HH

#ifndef ZxdpLib_HH
#include <zlib/ZxdpLib.hh>
#endif

#include <stdint.h>

#include <rte_ethdev.h>
#include <rte_ether.h>

#include <zlib/ZuTime.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmLock.hh>
#include <zlib/ZtArray.hh>
#include <zlib/Zxdp.hh>
#include <zlib/ZxdpDpdkPool.hh>

namespace Zxdp::Dpdk {

class EAL;

class Instance final : public ZmNHeap<Instance>, public Zxdp::Instance {
friend inline ZuStringT<"Zxdp.Dpdk.Instance"> ZmNHeapID(Instance *);

public:
  using ZmNHeap<Instance>::operator new;
  using ZmNHeap<Instance>::operator delete;

  explicit Instance(EAL &eal) : m_eal{eal} { }
  ~Instance() override;

  ZmRef<Zi::IOBufPool> bufPool(
    Zi::IOBufType::T, unsigned count, unsigned size,
    unsigned headRoom, unsigned tailRoom, unsigned align) override;
  ZuUnion<void, ZeException> init(ZiMultiplex *, const Zxdp::Params &) override;
  void reserveTxPools(ZuSpan<Zi::IOBufPool *const>) override;
  ZuUnion<void, ZeException> alloc(Zi::IOBufPool *, const Zi::IOBufPoolList &) override;
  void final() override;
  ZeException error() const override { return m_error; }
  void status() override;

  ZiMultiplex *mx() const override { return m_mx; }
  unsigned ifindex() const override { return m_config.ifindex; }
  unsigned rxQueues() const override { return m_mx ? 1 : 0; }
  bool allocated() const override { return !!m_rxPool; }
  bool ready() const override { return Engine::state() == ZmEngineState::Running && m_active; }
  unsigned mtu() const override { return m_mtu; }
  bool zeroCopy() const override { return m_ring; }
  ZuBSpan mac() const override { return m_mac.addr_bytes; }

  bool txReady(uint8_t *) const override;
  bool send(uint8_t *, unsigned) override;
  bool tcpChksumOffload() override { return ready() && m_tcpOffload; }
  void rx() override;
  void wake() override;
  unsigned pollTx() override;
  unsigned allQueues(Ztc::QueueMgr::AllFn) const override;

private:
  using Result = ZuUnion<void, ZeException>;
  using Pools = ZtArray<ZmRef<BufPool>, ZtArrayHeapID<"Zxdp.Dpdk.Pools">>;
  using Pending = ZtArray<BufPool::Slot *, ZtArrayHeapID<"Zxdp.Dpdk.TxPending">>;
  enum { Burst = 32, NoPort = RTE_MAX_ETHPORTS };

  void start_() override;
  void stop_() override;
  Result activate();
  Result checkPort() const;
  void quiesceRx(unsigned left);
  void retire();
  void finishStop(unsigned left);
  unsigned reapTx(unsigned budget);
  void cleanTx();
  unsigned receive(bool deliver);
  unsigned park(unsigned delay);
  void returned();

  EAL &		m_eal;
  ZiMultiplex	*m_mx = nullptr;
  Dpdk::Params	m_config;
  rte_ether_addr m_mac{};
  unsigned	m_mtu = 0, m_appTx = 0, m_afPacketMinRxSize = 0;
  uint16_t	m_port = NoPort;
  bool		m_ring = false, m_pcap = false;
  bool		m_tcpOffload = false;
  int		m_wakeFD = -1;

  ZmRef<BufPool>	m_rxPool;
  Pools		m_txPools, m_reserves;
  AddressIndex	m_txIndex;
  ZeException	m_error;

  RecvFn	m_receive;
  StopFn	m_txStopped;
  ZmAtomic<unsigned> m_stopping{0}, m_active{0};
  alignas(Zm::CacheLineSize) ZmAtomic<uint64_t> m_wakes{0};
  ZmAtomic<unsigned> m_parking{0}, m_rxReturned{0};
  mutable ZmAtomic<unsigned> m_rxDepthRequested{0};
  ZmAtomic<unsigned> m_rxDepth{0};
  int		m_rxFD = -1;

  alignas(Zm::CacheLineSize) Pending m_pending;
  unsigned	m_pendingCount = 0, m_reapCursor = 0;
  int		m_txCleanup = 0;
  ZuTime	m_stopDeadline;

  mutable ZmLock m_queueLock;
  bool		m_started = false;
  uint16_t	m_rxDesc = 0, m_txDesc = 0;
};

} // namespace Zxdp::Dpdk
#endif /* ZxdpDpdkInstance_HH */
