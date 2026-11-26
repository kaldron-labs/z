//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Private DPDK Ethernet device and queue service.

#ifndef ZpktDpdkNif_HH
#define ZpktDpdkNif_HH

#ifndef ZpktDPDKLib_HH
#include "ZpktDPDKLib.hh"
#endif

#include <stdint.h>

#include <rte_ethdev.h>
#include <rte_ether.h>

#include <zlib/ZuTime.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmLock.hh>
#include <zlib/ZtArray.hh>
#include <zlib/Zpkt.hh>
#include "ZpktDpdkPool.hh"
#include "ZpktDPDKCf.hh"

namespace Zpkt::Dpdk {

class EAL;

class ZpktDPDKAPI Nif final : public ZmNHeap<Nif>, public Zpkt::Nif {
friend inline ZuStringT<"Zpkt.Dpdk.Nif"> ZmNHeapID(Nif *);

public:
  using ZmNHeap<Nif>::operator new;
  using ZmNHeap<Nif>::operator delete;

  Nif(EAL &, ZiMultiplex *, const NifCf &, const NifInfo &);
  ~Nif() override;

  ZmRef<Zi::IOBufPool> bufPool(
    Zi::IOBufType::T, unsigned count, unsigned size,
    unsigned headRoom, unsigned tailRoom, unsigned align) override;
  void reserveTxPools(ZuSpan<Zi::IOBufPool *const>) override;
  ZuUnion<void, ZeException> alloc(Zi::IOBufPool *, const Zi::IOBufPoolList &) override;
  void final() override;
  void status() override;

  bool allocated() const override { return !!m_rxPool; }
  bool ready() const override { return Engine::state() == ZmEngineState::Running && m_active; }

  bool txReady(uint8_t *) const override;
  bool send(uint8_t *, unsigned) override;
  unsigned send(ZuSpan<const TxPacket>) override;
  void rx() override;
  unsigned pollTx() override;
  unsigned allQueues(Ztc::QueueMgr::AllFn) const override;

private:
  using Result = ZuUnion<void, ZeException>;
  using Pools = ZtArray<ZmRef<BufPool>, ZtArrayHeapID<"Zpkt.Dpdk.Pools">>;
  using Pending = ZtArray<BufPool::Slot *, ZtArrayHeapID<"Zpkt.Dpdk.TxPending">>;
  enum { Burst = 32, NoPort = RTE_MAX_ETHPORTS };

  void start_() override;
  void stop_() override;
  Result activate();
  void quiesceRx(unsigned left);
  void retire();
  void finishStop(unsigned left);
  Slot *prepareTx(uint8_t *, unsigned);
  static void refuseTx(Slot *);
  unsigned reapTx(unsigned budget);
  void cleanTx();
  unsigned receive(bool deliver);
  unsigned block(unsigned delay);
  void returned();

  EAL &		m_eal;
  NifCf		m_config;
  unsigned	m_appTx = 0, m_afPacketMinRxSize = 0;
  uint16_t	m_port = NoPort;
  bool		m_ring = false, m_pcap = false;

  ZmRef<BufPool>	m_rxPool;
  Pools		m_txPools, m_reserves;
  AddressIndex	m_txIndex;

  ZmAtomic<unsigned> m_active{0};
  ZmAtomic<unsigned> m_rxReturned{0};
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

} // namespace Zpkt::Dpdk
#endif /* ZpktDpdkNif_HH */
