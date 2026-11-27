//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Copying AF_PACKET service over application packet pools.

#ifndef ZpktAfpNif_HH
#define ZpktAfpNif_HH

#ifndef ZpktLib_HH
#include <zlib/ZpktLib.hh>
#endif

#include <linux/if_packet.h>
#include <sys/socket.h>

#include <zlib/ZmLock.hh>
#include <zlib/Zpkt.hh>

#include "ZpktAFPCf.hh"
#include "ZpktAfpPool.hh"

namespace Zpkt::Afp {

class Nif final : public ZmNHeap<Nif>, public Zpkt::Nif {
friend inline ZuStringT<"Zpkt.Afp.Nif"> ZmNHeapID(Nif *);

public:
  using ZmNHeap<Nif>::operator new;
  using ZmNHeap<Nif>::operator delete;
  using Result = PoolSet::Result;

  Nif(ZiMultiplex *, const NifCf &, const NifInfo &);
  ~Nif() override;

  ZmRef<Zi::IOBufPool> bufPool(
    Zi::IOBufType::T, unsigned count, unsigned size,
    unsigned headRoom, unsigned tailRoom, unsigned align) override;
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
  enum { RxBurst = 32 };
  enum { Ready = 1, Armed = 2, FinishWaiting = 4, Writer = 8 };
  enum { Receive = 1, Transmit, Wait };
  enum { BypassOff, BypassOn, BypassUnsupported };
  static constexpr uint64_t WriterMask = uint64_t(UINT32_MAX) & ~(uint64_t(Writer) - 1);
  static constexpr uint64_t RunStep = uint64_t(1) << 32;
  static constexpr uint64_t RunMask = ~(RunStep - 1);

  void start_() override;
  void stop_() override;
  Result activate();
  int openSocket();
  int bindSocket(unsigned protocol);
  void closeRun();
  void finishTx();
  void finishStop();
  void failure(unsigned operation, int error);
  void serviceFailure();
  void recordFailure();

  bool admitWriter(bool replenish);
  void releaseWriter();
  void closeAdmission();
  void returned();
  void signal();
  void armReturn();
  void waitRx(bool depleted);
  unsigned receive();
  unsigned rxLength(unsigned i) const;
  Slot *txSlot(uint8_t *) const;
  unsigned send(ZuSpan<const TxPacket>, Zu::IO::Result &);
  uint64_t sampleDrops() const; // caller holds m_observe

  struct RxControl {
    alignas(cmsghdr) unsigned char data[CMSG_SPACE(sizeof(tpacket_auxdata))];
  };
  struct RxVectors {
    mmsghdr	msg[RxBurst]{};
    iovec	iov[RxBurst]{};
    sockaddr_ll	addr[RxBurst]{};
    RxControl	control[RxBurst];
  };
  struct TxVectors {
    mmsghdr	msg[MaxTxBatch]{};
    iovec	iov[MaxTxBatch]{};
    Slot	*slots[MaxTxBatch];
  };
  NifCf			m_config;
  PoolSet		m_pools;
  PoolSet::PoolRefs	m_reserves;
  int			m_fd = -1, m_ingressFD = -1;
  unsigned		m_bypass = BypassOff, m_rxBufferBytes = 0;
  mutable ZmLock	m_observe;
  mutable uint64_t	m_drops = 0;

  // High bits distinguish runs. The low word carries admission, arm and the
  // writer count so stop and replenishment cannot both claim the same arm.
  // FinishWaiting passes stop completion to the last writer when necessary.
  alignas(Zm::CacheLineSize) ZmAtomic<uint64_t> m_gate{0};
  ZmAtomic<uint64_t>	m_failure{0};
  bool			m_errorRecorded = false; // Rx owner

  alignas(Zm::CacheLineSize) RxVectors m_rx;
  uint64_t		m_rxPackets = 0, m_rxBytes = 0, m_rxDiscard = 0;

  alignas(Zm::CacheLineSize) TxVectors m_tx;
  ZmAtomic<unsigned>	m_pending{0};
  uint64_t		m_txAccepted = 0, m_txBytes = 0, m_txRetired = 0, m_txRetiredBytes = 0;
};

} // namespace Zpkt::Afp
#endif /* ZpktAfpNif_HH */
