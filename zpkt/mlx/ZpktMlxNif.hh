//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Mellanox packet interface and owner handoff.

#ifndef ZpktMlxNif_HH
#define ZpktMlxNif_HH

#ifndef ZpktLib_HH
#include <zlib/ZpktLib.hh>
#endif

#include <zlib/ZmLock.hh>

#include "ZpktMLXCf.hh"
#include "ZpktMlxLoop.hh"
#include "ZpktMlxQueue.hh"

namespace Zpkt::Mlx {

class Env;

class Nif final : public ZmNHeap<Nif>, public Zpkt::Nif {
friend inline ZuStringT<"Zpkt.Mlx.Nif"> ZmNHeapID(Nif *);

public:
  using ZmNHeap<Nif>::operator new;
  using ZmNHeap<Nif>::operator delete;
  using Result = PoolSet::Result;

  Nif(Env &, ZiMultiplex *, const NifCf &, const NifInfo &, bool loopback);
  ~Nif() override;

  ZmRef<Zi::IOBufPool> bufPool(
    Zi::IOBufType::T, unsigned, unsigned, unsigned, unsigned, unsigned) override;
  void reserveTxPools(ZuSpan<Zi::IOBufPool *const>) override;
  Result alloc(Zi::IOBufPool *, const Zi::IOBufPoolList &) override;
  bool allocated() const override { return m_pools.allocated(); }
  bool ready() const override { return uint64_t(m_gate) & Ready; }
  void final() override;

  void rx() override;
  void wake() override;
  Zu::IO::Result txReady(uint8_t *) const override;
  Zu::IO::Result send(uint8_t *, unsigned) override;
  unsigned send(ZuSpan<const TxPacket>) override;
  unsigned pollTx() override;
  void status() override;
  unsigned allQueues(Ztc::QueueMgr::AllFn) const override;

private:
  enum { RxBurst = 32, Ready = 1, FinishWaiting = 2, Writer = 4 };
  static constexpr uint64_t RunStep = uint64_t(1) << 32;
  static constexpr uint64_t RunMask = ~(RunStep - 1);
  static constexpr uint64_t WriterMask = uint64_t(UINT32_MAX) & ~(uint64_t(Writer) - 1);

  void start_() override;
  void stop_() override;
  void finishStop();
  void fail(int);
  unsigned drainTx();
  int activate();
  void signal();
  void returned();
  void releaseWriter();
  bool admitWriter();
  unsigned receive();
  Slot *txSlot(uint8_t *) const;

  Env			&m_env;
  NifCf			m_config;
  PoolSet		m_pools;
  PoolSet::PoolRefs	m_reserves;
  Loop			m_loop;
  Queue			m_queue;
  ibv_pd		*m_pd = nullptr;
  int			m_ingressFD = -1;
  unsigned		m_port = 0;
  bool			m_finalizing = false;
  mutable ZmLock	m_observe;
  ZmAtomic<int>		m_serviceError{0};
  ZmAtomic<unsigned>	m_steering{0};
  alignas(Zm::CacheLineSize) ZmAtomic<uint64_t> m_rxPackets{0}, m_rxBytes{0}, m_discards{0};
  alignas(Zm::CacheLineSize) ZmAtomic<uint64_t> m_txPackets{0}, m_txBytes{0};
  ZmAtomic<uint64_t>	m_txRetired{0}, m_txRetiredBytes{0};
  ZmAtomic<unsigned>	m_txPending{0};
  alignas(Zm::CacheLineSize) ZmAtomic<uint64_t> m_gate{0};
};

} // namespace Zpkt::Mlx
#endif /* ZpktMlxNif_HH */
