//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <net/if.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>

#include <zlib/ZuBox.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuTime.hh>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmLock.hh>
#include <zlib/ZmGuard.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>

#include <zlib/Zpkt.hh>
#include <zlib/ZpktXDPCf.hh>
#include <zlib/ZpktNif.hh>
#include <zlib/ZpktBPF.hh>
#include <zlib/ZpktUMem.hh>
#include <zlib/ZpktPool.hh>
#include <zlib/ZpktTx.hh>

#ifdef ZPKT_XDP_TEST
extern "C" void ZpktTestXdpDesc(const xdp_desc &);
extern "C" void ZpktTestXdpPublish(unsigned, uint32_t, bool);
extern "C" bool ZpktTestXdpCQ();
#endif

namespace Zpkt {
namespace Xdp {
ZfStructImpl(NifCf, Cf);
ZfStructImpl(PluginCf, Cf);
} // Xdp
namespace Xdp {

// Linux shares FILL/CQ only among sockets bound to the same (device, queue).
// Keep a nonpublished owner on each tuple and reopen its service socket per run.
struct Queue : public ZmNHeap<Queue> {
friend inline ZuStringT<"Zpkt.Queue"> ZmNHeapID(Queue *);
  Xsk owner, service;
  unsigned rxHeld = 0, quota = 0;
  bool published = false;
};

class Nif final : public ZmNHeap<Nif>, public Zpkt::Nif {
friend inline ZuStringT<"Zpkt.Nif"> ZmNHeapID(Nif *);

public:
  using ZmNHeap<Nif>::operator new;
  using ZmNHeap<Nif>::operator delete;
  Nif(ZiMultiplex *mx, const NifCf &config, const NifInfo &info) :
    Zpkt::Nif{mx, info}, m_rxSpin{config.rxSpin}, m_txSpin{config.txSpin}
  {
    ZmAssert_(mx && mx->state() == ZmEngineState::Stopped);
    if (!config.txSid || config.txSid > mx->params().nThreads() ||
	config.txSid == mx->rxThread() || config.txSid == mx->txThread())
      throw ZeEXCEPT(Error, "Zpkt", "invalid AF_XDP sender slot");
    auto opened = nifOpen();
    if (opened.is<ZeException>()) throw ZuMv(opened).p<ZeException>();
    int fd = opened.p<int>();
    int wakeFD = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wakeFD < 0) {
      auto error = ZeLastError;
      ::close(fd);
      throw ZeEXCEPT(Error, "Zpkt", ([error](auto &out) {
	out << "AF_XDP wake eventfd creation failed: " << error;
      }));
    }
    m_nifFD = fd;
    this->wakeFD(wakeFD);
    m_txSid = config.txSid;
  }
  ~Nif() override { if (mx()) final(); }

  void final() override {
    ZmAssert_(Engine::state() == ZmEngineState::Stopped);
    ZmAssert_(m_ioState == Cold || m_ioState == Closed);
    ZmGuard guard(m_queueLock);
    // A transient failure in stop may have left the expected-program FD open.
    // Keep it for a later final() retry if this attempt also fails.
    auto detached = m_attachment.detach();
    for (auto i = m_queues.length(); i; --i) delete m_queues[i - 1];
    m_queues.null();
    m_umemFD = -1;
    if (m_rxPool) m_rxPool->final(m_rxHeld);
    for (auto &pool : m_txPools) {
      auto native = static_cast<BufPool *>(pool.ptr());
      unsigned outstanding = 0;
      for (unsigned i = 0; i < m_txHeld; ++i)
	outstanding += m_arena->pool(m_txActive[i]) == native;
      native->final(outstanding);
    }
    m_rxHeld = m_txHeld = 0;
    m_rxSlots.null(); m_txActive.null();
    m_txPools.null();
    m_txReserves.null();
    m_rxPool = nullptr;
    m_arena = nullptr;
    clearCallbacks();
    closeWake();
    if (m_nifFD >= 0) { ::close(m_nifFD); m_nifFD = -1; }
    resetInfo();
    m_txSid = 0;
    m_refillNext = m_nextQueue = m_nAppTxPools = 0;
    resetWake();
    m_ioError = 0;
    m_txConsumed = 0;
    m_txExit = m_txExited = 1;
    error(detached.is<ZeException>() ? ZuMv(detached).p<ZeException>() : ZeException{});
    m_startFailed = m_ingressUncertain = false;
    m_ioState = Cold;
    if (!failed()) final_();
  }

  void status() override;
  ZmRef<Zi::IOBufPool> bufPool(
    Zi::IOBufType::T, unsigned count, unsigned size, unsigned headRoom,
    unsigned tailRoom, unsigned align) override;
  bool allocated() const override { return m_umemFD >= 0; }
  bool ready() const override {
    return Engine::state() == ZmEngineState::Running && m_ioState == Active;
  }
  Zu::IO::Result txReady(uint8_t *) const override;
  Zu::IO::Result send(uint8_t *, unsigned) override;
  unsigned send(ZuSpan<const TxPacket>) override;
  void rx() override;
  unsigned pollTx() override { return ready() ? reapTx() : 0; }
  unsigned allQueues(Ztc::QueueMgr::AllFn) const override;

  void reserveTxPools(ZuSpan<Zi::IOBufPool *const> pools) override {
    ZmAssert_(mx() && Engine::state() == ZmEngineState::Stopped && !allocated());
    auto n = pools.length();
    m_txReserves.length(n);
    for (unsigned i = 0; i < n; ++i) m_txReserves[i] = pools[i];
  }

  ZuUnion<void, ZeException> alloc(
    Zi::IOBufPool *rx, const Zi::IOBufPoolList &tx) override
  {
    if (!mx() || Engine::state() != ZmEngineState::Stopped)
      return ZeEXCEPT(Error, "Zpkt", "initialize and stop AF_XDP before allocation");
    if (allocated()) {
      bool same = rx == m_rxPool.ptr() && tx.count_() == m_nAppTxPools;
      auto it = tx.citer();
      while (same) {
	auto pool = it();
	if (!pool) return {};
	bool found = false;
	for (unsigned i = 0; i < m_nAppTxPools; ++i) found |= m_txPools[i].ptr() == pool;
	same = found;
      }
      return ZeEXCEPT(Error, "Zpkt", "conflicting AF_XDP pool allocation");
    }
    auto ingress = dynamic_cast<BufPool *>(rx);
    if (!ingress || ingress->count < rxQueues())
      return ZeEXCEPT(Error, "Zpkt", "Rx pool must provision every interface queue");
    Pools txPools;
    txPools.size(tx.count_() + m_txReserves.length());
    {
      auto it = tx.citer();
      while (auto pool = it()) new (txPools.push()) ZmRef<Zi::IOBufPool>{pool};
    }
    auto appTx = txPools.length();
    for (const auto &reserve : m_txReserves) {
      for (const auto &pool : txPools)
	if (pool.ptr() == reserve.ptr())
	  return ZeEXCEPT(Error, "Zpkt", "duplicate AF_XDP Tx pool");
      new (txPools.push()) ZmRef<Zi::IOBufPool>{reserve};
    }
    auto created = UMem::create(txPools.cspan(), ingress, mtu(),
      tcpChksumAvailable(ifindex()));
    if (created.is<ZeException>()) return ZuMv(created).p<ZeException>();
    auto arena = ZuMv(created).p<ZmRef<UMem>>();
    const auto &layout = arena->layout();
    auto n = rxQueues();
    auto quota = layout.rxCount/n;
    auto extra = layout.rxCount%n;
    auto r = power2(quota + !!extra);
    auto t = power2(layout.txCount);
    if (!r || !t) return ZeEXCEPT(Error, "Zpkt", "AF_XDP ring capacity overflow");
    Queues queues;
    queues.length(n);
    for (unsigned i = 0; i < n; ++i) {
      auto queue = queues[i] = new Queue;
      queue->quota = quota + (i < extra);
      auto opened = queue->owner.owner(arena, ifindex(), i, r, i ? 2 : t,
	i ? queues[0]->owner.fd() : -1);
      if (opened.is<ZeException>()) {
	for (auto j = i + 1; j; --j) delete queues[j - 1];
	return opened;
      }
    }
    for (unsigned i = 0; i < n; ++i) {
      auto &owner = queues[i]->owner;
      auto id = ZuID{} << "xdp." << ZuBoxed(ifindex()) << '.' << ZuBoxed(i) << '.';
      owner.fill().identify(ZuID{mx()->id()}, ZuID{id} << "fill", Ztc::QueueType::Tx);
      if (!i) owner.comp().identify(ZuID{mx()->id()}, ZuID{id} << "cq", Ztc::QueueType::Rx);
    }
    m_rxSlots.length(ingress->count);
    for (auto &index : m_rxSlots) index = End;
    m_txActive.length(layout.txCount);
    m_rxRingCount = r;
    m_txRingCount = t;
    m_queues = ZuMv(queues);
    m_umemFD = m_queues[0]->owner.fd();
    zeroCopy(m_queues[0]->owner.zeroCopy());
    m_rxPool = ingress;
    m_txPools = ZuMv(txPools);
    m_nAppTxPools = appTx;
    m_arena = ZuMv(arena);
    m_rxPool->returned([this] { wake_(); });
    m_arena->publish(m_txPools.cspan(), m_rxPool);
    return {};
  }

private:
  BufPool *txPool(const uint8_t *, unsigned &id) const;
  Zu::IO::Result admitTx(uint8_t *, unsigned, uint8_t *&, TxSlot *&) const;
  void prepareTx(TxRing &, unsigned, uint8_t *, unsigned, uint8_t *, TxSlot &);
  void publishTx(TxRing &, unsigned);
  void start_() override {

    error({});
    if (!allocated() || !mx()->running() || !receiving()) {
      error(ZeEXCEPT(Error, "Zpkt", "allocate AF_XDP, start its mux and supply a receive callback"));
      clearCallbacks();
      Engine::started(false);
      return;
    }
    if (m_txHeld) {
      mx()->txRun([this] { resumeStart(); });
      return;
    }
    activate();
  }

  void resumeStart() {
    ZmAssert_(mx()->invoked(mx()->txThread()));
    // A stopped run may still have a kernel loan. Only the Tx owner reads
    // its retained CQ, and each continuation handles at most one quantum.
    if (m_txHeld && cqReady()) reapTx();
    if (m_txHeld && cqReady()) {
      mx()->txRun([this] { resumeStart(); });
      return;
    }
    activate();
  }

  void activate() {
    auto result = open();
    if (result.is<void>()) { Engine::started(true); return; }
    error(ZuMv(result).p<ZeException>());
    m_startFailed = true;
    beginStop();
  }

  void stop_() override { beginStop(); }
  void beginStop() {
    m_ioState = Closing;
    wake();
    m_drainDeadline = monotonicNow() + ZuTime{2};
    if (unsigned(m_txExited)) {
      mx()->rxRun([this] { retireRx(); });
      return;
    }
    // Fence all earlier sender work before the mux owners retire their rings.
    m_txExit = 1;
    mx()->run([this] {
      m_txExited = 1;
      notifyTxStopped();
      mx()->rxRun([this] { retireRx(); });
    }, m_txSid);
  }

  void retireRx() {
    ZmAssert_(mx()->invoked(mx()->rxThread()));
    if (!m_ingressQuiesced) {
      auto result = quiesceIngress();
      if (result.is<ZeException>() && !failed()) error(ZuMv(result).p<ZeException>());
    }
    if (!drainRx()) {
      mx()->rxRun([this] { retireRx(); });
      return;
    }
    mx()->txRun([this] { retireTx(); });
  }

  void retireTx() {
    ZmAssert_(mx()->invoked(mx()->txThread()));
    if (!drainTx()) {
      mx()->txRun([this] { retireTx(); });
      return;
    }
    auto result = close();
    if (result.is<ZeException>() && !failed()) error(ZuMv(result).p<ZeException>());
    clearCallbacks();
    if (m_startFailed) {
      m_startFailed = false;
      Engine::started(false);
    } else Engine::stopped(!failed());
  }

  ZuUnion<void, ZeException> open() {
    ZmGuard guard(m_queueLock);
    ZmAssert_(m_events < 0 && unsigned(m_txExited));
    m_ioState = Closing;
    m_ingressQuiesced = false;
    m_refillNext = m_nextQueue = 0;
    m_rxStarved = false;
    resetWake();
    m_ioError = 0;
    ackWake();
    if (m_ingressUncertain)
      return ZeEXCEPT(Error, "Zpkt", "AF_XDP ingress retirement could not be proved");
    if (m_txHeld)
      return ZeEXCEPT(Error, "Zpkt", "previous AF_XDP Tx loans remain outstanding");
    auto detached = m_attachment.detach();
    if (detached.is<ZeException>()) return detached;
    auto programs = xdpQuery(ifindex(), m_nifFD);
    if (programs.is<ZeException>()) return ZuMv(programs).p<ZeException>();
    if (programs.p<XDPInfo>().occupied())
      return ZeEXCEPT(Error, "Zpkt", "interface has an unrelated XDP program");
    auto ioError = [](const char *op) {
      return ZeEXCEPT(Error, "Zpkt", ([op, error = ZeLastError](auto &out) {
	out << op << ": " << error;
      }));
    };
    m_events = epoll_create1(EPOLL_CLOEXEC);
    if (m_events < 0) return ioError("AF_XDP epoll creation");
    epoll_event wakeEvent{};
    wakeEvent.events = EPOLLIN;
    wakeEvent.data.u32 = End;
    if (epoll_ctl(m_events, EPOLL_CTL_ADD, wakeFD(), &wakeEvent))
      return ioError("AF_XDP wake registration");
    for (unsigned i = 0; i < m_queues.length(); ++i) {
      auto &queue = *m_queues[i];
      auto opened = queue.service.service(ifindex(), i, m_rxRingCount,
	i ? 0 : m_txRingCount, i ? queue.owner.fd() : m_umemFD);
      if (opened.is<ZeException>()) return opened;
      auto id = ZuID{} << "xdp." << ZuBoxed(ifindex()) << '.' << ZuBoxed(i) << '.';
      auto owner = ZuID{mx()->id()};
      queue.service.rx().identify(owner, ZuID{id} << "rx", Ztc::QueueType::Rx,
	queue.service.fd());
      if (!i) queue.service.tx().identify(owner, ZuID{id} << "tx", Ztc::QueueType::Tx);
      epoll_event event{};
      event.events = EPOLLIN;
      event.data.u32 = i;
      if (epoll_ctl(m_events, EPOLL_CTL_ADD, queue.service.fd(), &event))
	return ioError("AF_XDP readiness registration");
    }
    auto loaded = m_program.load(rxQueues());
    if (loaded.is<ZeException>()) return loaded;
    for (unsigned i = 0; i < m_queues.length(); ++i) {
      auto &queue = *m_queues[i];
      auto result = m_program.publish(i, queue.service.fd());
      if (result.is<ZeException>()) return result;
      queue.published = true;
    }
    auto attached = m_attachment.attach(ifindex(), m_program);
    if (attached.is<ZeException>()) return attached;

    tcpChksumOffload(m_queues[0]->owner.txMetadata() &&
      m_queues[0]->service.zeroCopy() && tcpChksumAvailable(ifindex()));
    m_txConsumed = m_queues[0]->service.tx().consumed();
    m_txExit = m_txExited = 0;
    m_ioState = Active;
    for (auto queue : m_queues) replenish(*queue, queue->quota);
    return {};
  }

  ZuUnion<void, ZeException> quiesceIngress() {
    ZmGuard guard(m_queueLock);
    ZeException error;
    bool published = false;
    for (unsigned i = 0; i < m_queues.length(); ++i) {
      auto &queue = *m_queues[i];
      if (!queue.published) continue;
      auto result = m_program.quiesce(i);
      if (result.is<ZeException>() && !error) error = ZuMv(result).p<ZeException>();
      queue.published = false;
      published = true;
    }
    // If a destination could not be deleted, detach this classifier before
    // asking for a grace period. Another owner's classifier is left alone.
    bool retired = !error;
    if (!retired) {
      auto detached = m_attachment.detach();
      retired = detached.is<void>() || !m_attachment.ifindex();
    }
    if (published && retired) {
      auto synchronized = m_program.synchronize();
      if (synchronized.is<ZeException>()) {
	retired = false;
	if (!error) error = ZuMv(synchronized).p<ZeException>();
      }
    }
    m_ingressQuiesced = true;
    m_retireQueue = retired && published ? 0 : m_queues.length();
    if (!retired && m_rxHeld) m_ingressUncertain = true;
    if (error) return error;
    return {};
  }

  bool drainRx() {
    auto count = m_queues.length();
    if (m_retireQueue == count) return true;
    if (consumeRx(*m_queues[m_retireQueue], false) < Quantum) ++m_retireQueue;
    return m_retireQueue == count;
  }

  ZuUnion<void, ZeException> close() {
    ZmGuard guard(m_queueLock);
    for (auto queue : m_queues) {
      // The owner remains bound. FILL and hardware-owned RX slots remain
      // assigned to this queue across runs.
      queue->service.close();
    }
    auto detached = m_attachment.detach();
    if (m_events >= 0) { ::close(m_events); m_events = -1; }
    m_program.reset();
    m_txPending = 0;
    m_ioState = Closed;
    if (detached.is<ZeException>()) return detached;
    if (m_ioError) return ZeEXCEPT(Error, "Zpkt", ([error = ZeError{int(m_ioError)}](auto &out) {
      out << "AF_XDP I/O failed: " << error;
    }));
    return {};
  }

  static int kickTx(Xsk &socket) {
    if (socket.tx().needsKick() &&
	sendto(socket.fd(), nullptr, 0, MSG_DONTWAIT, nullptr, 0) < 0 &&
	errno != EINTR && errno != EAGAIN && errno != EBUSY && errno != ENOBUFS)
      return errno;
    return 0;
  }

  bool drainTx() {
    if (!m_txHeld) return true;
    reapTx();
    if (!m_txHeld) return true;
    auto &service = m_queues[0]->service;
    // A refused restart has no service socket. Preserve the unresolved
    // loan for a later activation rather than waiting on a closed run.
    if (service.fd() < 0) return true;
    if (auto error = kickTx(service)) m_ioError = error;
    if (monotonicNow() < m_drainDeadline) return false;
    // Any loan still outstanding on device failure remains accounted for.
    // Returning it without a CQ would race the kernel's pinned UMEM.
    if (!m_ioError) m_ioError = ETIMEDOUT;
    return true;
  }

  void postRx(Queue &queue, uint8_t *p) {
    auto &fill = queue.owner.fill();
    auto id = m_rxPool->slot(p);
    ZmAssert_(m_rxSlots[id] == End && fill.reserve(1) == 1);
    m_rxSlots[id] = 0;
    ++m_rxHeld;
    fill.entry(0) = uint64_t(id) * m_arena->layout().chunk;
    fill.commit();
    ++queue.rxHeld;
  }

  void replenish(Queue &queue, unsigned budget) {
    if (ZuUnlikely(m_rxStarved)) {
      if (!m_rxPool->hasFree()) return;
      // A busy queue can recycle its own loan on every turn. Give each
      // empty queue a frame before that busy queue takes another one.
      auto n = m_queues.length();
      auto q = m_refillNext;
      for (unsigned i = 0; i < n; ++i) {
	auto &empty = *m_queues[q];
	if (++q == n) q = 0;
	if (!empty.quota || empty.rxHeld || !empty.owner.fill().available()) continue;
	auto p = m_rxPool->BufPool::alloc();
	if (!p) return;
	postRx(empty, p);
	m_refillNext = q;
      }
      m_rxStarved = false;
    }
    auto available = queue.owner.fill().available();
    if (budget > available) budget = available;
    auto room = queue.quota - queue.rxHeld;
    if (budget > room) budget = room;
    while (budget--) {
      auto p = m_rxPool->BufPool::alloc();
      if (!p) {
	if (!queue.rxHeld) m_rxStarved = true;
	break;
      }
      postRx(queue, p);
    }
  }

  void serveTx() {
    if (unsigned(m_txExit) || unsigned(m_ioState) != Active) return;
    auto &socket = m_queues[0]->service;
    auto &ring = socket.tx();
    while (!unsigned(m_txExit)) {
      auto consumed = ring.consumed();
      unsigned n = uint32_t(consumed - m_txConsumed);
      // Keep ownership during a short idle spin. The producer counts each
      // descriptor before publishing it, including work not yet consumed.
      for (unsigned i = 0; i < m_txSpin && m_txPending.load_() <= n; ++i)
	if (unsigned(m_txExit)) return;
      if (unsigned(m_txExit)) return;
      if (n) {
	m_txConsumed = consumed;
	// An earlier increment keeps us active, even before commit. A later
	// zero-to-one increment schedules the successor after commit.
	if (!(m_txPending -= n)) return;
      }
      if (auto error = kickTx(socket)) {
	m_ioError = error;
	m_ioState = Closing;
	wake();
	return;
      }
    }
  }

  unsigned refillQueue(unsigned n) {
    unsigned low = End;
    if (m_rxHeld != m_rxPool->count) {
      auto q = m_refillNext;
      for (unsigned i = 0; i < n; ++i) {
	auto &queue = *m_queues[q];
	// Posted frames still owned by the driver also supply this queue.
	if (queue.quota && queue.rxHeld <= queue.quota / 2 &&
	    queue.owner.fill().available()) {
	  if (!queue.rxHeld) return q;
	  if (low == End) low = q;
	}
	if (++q == n) q = 0;
      }
    }
    return low;
  }

  bool receiveQueue(unsigned q);
  unsigned consumeRx(Queue &, bool deliver);
  bool cqReady() {
#ifdef ZPKT_XDP_TEST
    if (!ZpktTestXdpCQ()) return false;
#endif
    return m_queues[0]->owner.comp().available();
  }
  unsigned reapTx();


  enum { Quantum = 64, End = unsigned(-1) };
  enum { Cold, Active, Closing, Closed };
  using Indices = ZtArray<unsigned, ZtArrayHeapID<"Zpkt.RxSlots">>;
  using Packets = ZtArray<uint8_t *, ZtArrayHeapID<"Zpkt.TxActive">>;
  using Queues = ZtArray<Queue *, ZtArrayHeapID<"Zpkt.Queues">>;
  using Pools = ZtArray<ZmRef<Zi::IOBufPool>, ZtArrayHeapID<"Zpkt.Pools">>;
  Pools		m_txPools, m_txReserves;
  unsigned	m_nAppTxPools = 0;
  using RxPool = ZmRef<BufPool>;
  RxPool	m_rxPool;
  bool		m_startFailed = false;
  bool		m_ingressQuiesced = true, m_ingressUncertain = false;
  unsigned	m_rxSpin, m_txSpin;
  unsigned	m_rxRingCount = 0, m_txRingCount = 0;
  unsigned	m_txSid = 0;
  ZmRef<UMem>	m_arena;
  BPFProgram	m_program;
  BPFAttach	m_attachment;
  Queues	m_queues;
  mutable ZmLock m_queueLock; // enumeration versus initialization and mapping retirement
  int		m_events = -1, m_nifFD = -1, m_umemFD = -1;
  ZmAtomic<unsigned> m_ioState{Cold}, m_txExit{1}, m_txExited{1};
  ZmAtomic<int> m_ioError{0};
  ZuTime	m_drainDeadline;
  unsigned	m_retireQueue = 0;
  alignas(Zm::CacheLineSize) ZmAtomic<unsigned> m_txPending{0};
  alignas(Zm::CacheLineSize) Indices m_rxSlots;
  unsigned	m_rxHeld = 0, m_nextQueue = 0, m_refillNext = 0;
  bool		m_rxStarved = false;
  alignas(Zm::CacheLineSize) Packets m_txActive;
  unsigned	m_txHeld = 0;
  // Sender-only accounting must not invalidate shared immutable packet views.
  alignas(Zm::CacheLineSize) uint32_t m_txConsumed = 0;
};

ZmRef<Zi::IOBufPool> Nif::bufPool(
  Zi::IOBufType::T type, unsigned count, unsigned size, unsigned headRoom,
  unsigned tailRoom, unsigned align)
{
  ZmRef<BufPool> pool = new BufPool{type, count, size};
  pool->headRoom = headRoom;
  pool->tailRoom = tailRoom;
  pool->align = align;
  return pool;
}

void Nif::status()
{
  ZmGuard guard(m_queueLock);
  // Six two-digit octets separated by colons, plus the terminator.
  ZuCArray<18> mac;
  for (auto octet : Nif::mac()) {
    if (mac.length()) mac << ':';
    mac << ZuBoxed(unsigned(octet)).hex<false, ZuFmt::Right<2>>();
  }
  ZiLOG(Info, "Zpkt", ([
    index = ifindex(), state = Engine::state(), mtu = mtu(),
    address = mac, allocated = allocated(), copy = !zeroCopy(),
    queues = rxQueues(), rxDesc = m_rxRingCount, txDesc = m_txRingCount,
    tcpOffload = tcpChksumOffload()
  ](auto &out) {
    out << "AF_XDP ifindex " << ZuBoxed(index) << " state " << ZuBoxed(state)
      << " MTU " << ZuBoxed(mtu) << " MAC " << address
      << " mode " << (allocated ? (copy ? "copy" : "zero-copy") : "unallocated")
      << " Rx queues " << ZuBoxed(queues) << " Rx descriptors per queue " << ZuBoxed(rxDesc)
      << " Tx descriptors " << ZuBoxed(txDesc)
      << " TCP checksum " << (tcpOffload ? "offload" : "offload unavailable");
  }));
}

unsigned Nif::allQueues(Ztc::QueueMgr::AllFn fn) const
{
  ZmGuard guard(m_queueLock);
  unsigned count = 0;
  if (m_ioState != Active) return 0;
  for (unsigned i = 0; i < m_queues.length(); ++i) {
    auto &queue = *m_queues[i];
    fn(&queue.service.rx());
    fn(&queue.owner.fill());
    count += 2;
    if (!i) {
      fn(&queue.service.tx());
      fn(&queue.owner.comp());
      count += 2;
    }
  }
  return count;
}
ZuInline BufPool *Nif::txPool(const uint8_t *packet, unsigned &id) const
{
  auto pool = m_arena->pool(packet);
  ZmAssert_(pool && pool != m_rxPool.ptr());
  id = pool->slot(packet);
  ZmAssert_(id != BufPool::NoSlot);
  return pool;
}

Zu::IO::Result Nif::txReady(uint8_t *packet) const
{
  if (!ready()) return Zi::IOError;
  unsigned id;
  auto pool = txPool(packet, id);
  const auto &slot = m_arena->txSlot(*pool, id);
  ZmAssert_(slot.refs.load_() > unsigned(slot.pending));
  return slot.pending ? Zi::NotReady : Zi::OK;
}

ZuInline Zu::IO::Result Nif::admitTx(
  uint8_t *packet, unsigned length, uint8_t *&canonical, TxSlot *&slot) const
{
  if (!ready()) return Zi::IOError;
  unsigned id;
  auto pool = txPool(packet, id);
  canonical = pool->backing(id);
  auto wire = unsigned(packet - canonical);
  if (!length || length > pool->size - wire) return Zi::IOError;
  slot = &m_arena->txSlot(*pool, id);
  ZmAssert_(slot->refs.load_() > unsigned(slot->pending));
  return slot->pending ? Zi::NotReady : Zi::OK;
}

ZuInline void Nif::prepareTx(
  TxRing &ring, unsigned i, uint8_t *packet, unsigned length, uint8_t *canonical, TxSlot &slot)
{
  slot.wire = unsigned(packet - canonical);
  slot.active = m_txHeld;
  m_txActive[m_txHeld++] = canonical;
  slot.pending = true;
  ++slot.refs;
  auto address = uint64_t(packet - m_arena->data());
  unsigned options = 0;
  if (configuredTCPChksumOffload()) {
    TxTCP tcp;
    if (tcp.init(packet, length)) {
      tcp.metadata(packet);
      length = tcp.wire;
      options = XDP_TX_METADATA;
    }
  }
  auto &entry = ring.entry(i);
  entry = {address, length, options};
#ifdef ZPKT_XDP_TEST
  ZpktTestXdpDesc(entry);
#endif
}

ZuInline void Nif::publishTx(TxRing &ring, unsigned count)
{
  auto idle = !m_txPending.xchAdd(count);
  ring.commit(count);
  if (idle) mx()->run([this] { serveTx(); }, m_txSid);
#ifdef ZPKT_XDP_TEST
  Ztc::QueueTelemetry data;
  ring.telemetry(data);
  ZpktTestXdpPublish(count, uint32_t(data.inCount), idle);
#endif
}

Zu::IO::Result Nif::send(uint8_t *packet, unsigned length)
{
  uint8_t *canonical;
  TxSlot *slot;
  auto result = admitTx(packet, length, canonical, slot);
  if (result != Zi::OK) return result;
  auto &ring = m_queues[0]->service.tx();
  if (m_txHeld == ring.count() || !ring.reserve(1)) return Zi::NotReady;
  prepareTx(ring, 0, packet, length, canonical, *slot);
  publishTx(ring, 1);
  return Zi::OK;
}

unsigned Nif::send(ZuSpan<const TxPacket> packets)
{
  unsigned count = packets.length() < MaxTxBatch ? packets.length() : MaxTxBatch;
  if (!count || send(packets[0].data, packets[0].length) != Zi::OK) return 0;
  auto &ring = m_queues[0]->service.tx();
  unsigned accepted = 1, target = 2;
  while (accepted < count) {
    auto wanted = count - accepted;
    if (wanted > target) wanted = target;
    auto available = m_txActive.length() - m_txHeld;
    auto outstanding = ring.count() - m_txHeld;
    if (available > outstanding) available = outstanding;
    auto reserved = ring.reserveUpTo(available < wanted ? available : wanted);
    if (!reserved) break;
    unsigned n = 0;
    for (; n < reserved; ++n) {
      const auto &packet = packets[accepted + n];
      uint8_t *canonical;
      TxSlot *slot;
      if (admitTx(packet.data, packet.length, canonical, slot) != Zi::OK) break;
      prepareTx(ring, n, packet.data, packet.length, canonical, *slot);
    }
    if (n) publishTx(ring, n);
    else ring.commit(0);
    accepted += n;
    if (n < wanted) break;
    if (target < MaxTxBatch) target <<= 1;
  }
  return accepted;
}

void Nif::rx()
{
  unsigned idle = 0, empty = 0;
  auto queueCount = m_queues.length(); // frozen until the caller quiesces Rx for close
  while (ready()) {
    if (woken()) return;
    auto q = m_nextQueue++;
    if (m_nextQueue == queueCount) m_nextQueue = 0;
    if (receiveQueue(q)) { idle = empty = 0; continue; }
    // Check/replenish each hardware queue before blocking. This visits queues,
    // never searches packets or buffers, and is independent of arena capacity.
    if (++empty < queueCount) continue;
    empty = 0;
    if (idle < m_rxSpin) { ++idle; continue; }
    // Publish before rechecking pulses. A producer either sees this wait and
    // writes eventfd, or its already-published pulse prevents the wait entirely.
    startBlocking();
    auto refill = refillQueue(queueCount);
    bool armed = refill != End && !m_rxPool->hasFree();
    if (armed) m_rxPool->armReturn();
    // A return racing the arm either published a free slot before this
    // check or claimed the arm and will interrupt epoll.
    bool returned = m_rxHeld < m_rxPool->count && m_rxPool->hasFree();
    bool refillReady = refill != End && returned;
    epoll_event event;
    int n = 0;
    if (!returned && !wakePending() && ready())
      n = epoll_wait(m_events, &event, 1, -1);
    stopBlocking();
    if (armed && m_rxPool->cancelReturn()) refillReady = true;
    if (n < 0) {
      if (errno == EINTR) continue;
      m_ioError = errno;
      m_ioState = Closing;
      return;
    }
    // A delayed writer can leave stale readiness after its pulse was consumed.
    // Draining readiness never creates a pulse.
    if (n > 0) {
      if (event.data.u32 == End) ackWake();
      else m_nextQueue = event.data.u32;
    }
    if (refillReady) {
      m_nextQueue = refill;
      if (!m_rxStarved) m_refillNext = refill + 1 == queueCount ? 0 : refill + 1;
    }
    idle = 0;
  }
}

bool Nif::receiveQueue(unsigned q)
{
  auto &queue = *m_queues[q];
  auto &fill = queue.owner.fill();
  if (!queue.service.rx().available() && !fill.needWakeup() &&
      (queue.rxHeld == queue.quota || m_rxPool->used() >= m_rxPool->count)) return false;
  const auto held = m_rxHeld;
  replenish(queue, Quantum);
  if (fill.needWakeup()) {
    pollfd wake{queue.service.fd(), POLLIN, 0};
    poll(&wake, 1, 0);
  }
  auto consumed = consumeRx(queue, true);
  if (consumed) replenish(queue, Quantum);
  return consumed || m_rxHeld != held;
}

unsigned Nif::consumeRx(Queue &queue, bool deliver)
{
  auto &ring = queue.service.rx();
  const auto &layout = m_arena->layout();
  unsigned limit = ring.available();
  if (limit > Quantum) limit = Quantum;
  if (!limit) return 0;
  ZiRxBurst burst;
  unsigned n = 0;
  while (n < limit && (!deliver || !wakePendingRelaxed()) && ring.reserve(1)) {
    auto desc = ring.entry(0);
    ring.commit();
    ++n;
    if (ZuUnlikely(desc.addr >= layout.rxLength)) { burst.flush(); continue; }
    auto id = unsigned(desc.addr/layout.chunk);
    if (ZuUnlikely(id >= m_rxPool->count || m_rxSlots[id] == End)) {
      burst.flush();
      continue;
    }
    --queue.rxHeld;
    --m_rxHeld;
    m_rxSlots[id] = End;
    auto p = m_arena->data() + desc.addr;
    auto slot = m_rxPool->slot(p);
    auto origin = m_rxPool->backing(id);
    if (!deliver || slot != id || !desc.len || desc.options ||
	desc.len > m_rxPool->size ||
	desc.len > layout.chunk - unsigned(desc.addr & (layout.chunk - 1))) {
      burst.flush();
      m_rxPool->BufPool::free(origin);
      continue;
    }
    recv(p, desc.len, limit > 1 ? &burst : nullptr);
  }
  return n;
}

unsigned Nif::reapTx()
{
#ifdef ZPKT_XDP_TEST
  if (!ZpktTestXdpCQ()) return 0;
#endif
  unsigned count = 0;
  auto &ring = m_queues[0]->owner.comp();
  auto n = ring.reserve(Quantum);
  const auto &layout = m_arena->layout();
  for (unsigned i = 0; i < n; ++i) {
    auto address = ring.entry(i);
    if (address < layout.rxLength || address >= layout.length) continue;
    auto p = m_arena->data() + address;
    auto pool = m_arena->pool(p);
    if (!pool) continue;
    auto id = pool->slot(p);
    if (id == BufPool::NoSlot) continue;
    auto canonical = pool->backing(id);
    auto &slot = m_arena->txSlot(*pool, id);
    // Generic Linux Tx tags the skb destructor address in bit zero and CQ
    // clears that bit. A slot has only one outstanding descriptor; compare
    // its offset excluding that tag, without a search or extra bookkeeping.
    if (!slot.pending || ((slot.wire ^ unsigned(p - canonical)) & ~1U)) continue;
    slot.pending = false;
    auto last = --m_txHeld;
    if (slot.active != last) {
      auto moved = m_txActive[last];
      m_txActive[slot.active] = moved;
      auto owner = m_arena->pool(moved);
      m_arena->txSlot(*owner, owner->slot(moved)).active = slot.active;
    }
    slot.active = End;
    pool->BufPool::free(canonical);
    ++count;
  }
  ring.commit();
  return count;
}


class Plugin final : public ZmNHeap<Plugin>, public Zpkt::Plugin {
friend inline ZuStringT<"Zpkt.Xdp.Plugin"> ZmNHeapID(Plugin *);

public:
  using ZmNHeap<Plugin>::operator new;
  using ZmNHeap<Plugin>::operator delete;
  Plugin(const ZfCf::AnyNode *cf) {
    ZfCf::handler<PluginCf>(cf).update(m_cf);
  }
  ZuUnion<Zpkt::Nif *, ZeException> nif(
    ZiMultiplex *mx, ZuCSpan name, const NifInfo &info) override
  {
    auto node = m_cf.interfaces->find(name);
    if (!node)
      return ZeEXCEPT(Error, "Zpkt", ([name = ZeString{name}](auto &s) {
	s << "unknown configured AF_XDP interface \"" << name << '"';
      }));
    try { return static_cast<Zpkt::Nif *>(new Nif{mx, node->val(), info}); }
    catch (ZeException error) {
      auto detail = error.fn();
      error.l = ZeMsgFn{[
	name = CfString{name}, detail = ZuMv(detail)
      ](auto &s, const ZeEventInfo &event) {
	s << "AF_XDP interface \"" << name << "\": ";
	detail(s, event);
      }};
      return error;
    }
  }
  ZuUnion<void, ZeException> final() override { return {}; }

private:
  PluginCf m_cf;
};

} // namespace Xdp

} // Zpkt

extern "C" ZpktXDPAPI Zpkt::Plugin *ZpktPlugin(const ZfCf::AnyNode *cf)
{
  return new Zpkt::Xdp::Plugin{cf};
}
