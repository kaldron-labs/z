//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>

#include <zlib/ZuBox.hh>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmLock.hh>
#include <zlib/ZmGuard.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiMultiplex.hh>

#include <zlib/Zxdp.hh>
#include <zlib/ZxdpBPF.hh>
#include <zlib/ZxdpUMem.hh>
#include <zlib/ZxdpPool.hh>

namespace Zxdp {

ZtEnumImplNS(Type);

namespace Xdp {

// Linux shares FILL/CQ only among sockets bound to the same (device, queue).
// Keep a nonpublished owner on each tuple and reopen its service socket per run.
struct Queue : public ZmNHeap<Queue> {
friend inline ZuStringT<"Zxdp.Queue"> ZmNHeapID(Queue *);
  Xsk owner, service;
  unsigned rxHeld = 0, quota = 0;
  bool published = false;
};

static ZuTime monotonicNow()
{
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return ZuTime{now};
}

class Instance final : public ZmNHeap<Instance>, public Zxdp::Instance {
friend inline ZuStringT<"Zxdp.Instance"> ZmNHeapID(Instance *);

public:
  using ZmNHeap<Instance>::operator new;
  using ZmNHeap<Instance>::operator delete;
  Instance() = default;
  ~Instance() override { if (m_mx) final(); }

  void final() override {
    ZmAssert_(Engine::state() == ZmEngineState::Stopped);
    ZmAssert_(m_ioState == Cold || m_ioState == Closed);
    ZmGuard guard(m_queueLock);
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
    m_receive = {}; m_txStopped = {};
    m_nextReceive = {}; m_nextTxStopped = {};
    if (m_wakeFD >= 0) { ::close(m_wakeFD); m_wakeFD = -1; }
    if (m_linkFD >= 0) { ::close(m_linkFD); m_linkFD = -1; }
    m_mx = nullptr;
    m_txSid = 0;
    m_link = {};
    m_filter = {};
    m_nextQueue = m_nAppTxPools = 0;
    m_wakes = m_parking = m_rxReturned = 0;
    m_ioError = 0;
    m_txConsumed = 0;
    m_txExit = m_txExited = 1;
    m_error = {};
    m_startFailed = m_everStarted = m_ingressUncertain = false;
    m_stopQueued = StopIdle;
    m_ioState = Cold;
  }

  ZeException error() const override { return m_error; }
  ZmRef<Zi::IOBufPool> bufPool(
    Zi::IOBufType::T, unsigned count, unsigned size, unsigned headRoom,
    unsigned tailRoom, unsigned align) override;
  ZiMultiplex *mx() const override { return m_mx; }
  unsigned ifindex() const override { return m_link.ifindex; }
  unsigned rxQueues() const override { return m_link.rxQueues; }
  bool allocated() const override { return m_umemFD >= 0; }
  bool everStarted() const override { return m_everStarted; }
  bool ready() const override {
    return Engine::state() == ZmEngineState::Running && m_ioState == Active;
  }
  unsigned mtu() const override { return m_link.mtu; }
  bool zeroCopy() const override { return allocated() && m_queues[0]->owner.zeroCopy(); }
  ZuBSpan mac() const override { return m_link.mac; }
  bool send(uint8_t *, unsigned) override;
  void rx() override;
  void wake() override {
    notify();
    if (Engine::stopping() &&
	!m_stopQueued.cmpXch(StopRequested, StopIdle)) Engine::stopped();
  }
  unsigned pollTx() override { return ready() ? reapTx() : 0; }
  unsigned allQueues(Ztc::QueueMgr::AllFn) const override;

  ZuUnion<void, ZeException> init(ZiMultiplex *mx, const Zxdp::Params &params) override {
    ZmAssert_(mx);
    if (Engine::state() != ZmEngineState::Stopped || mx->state() != ZmEngineState::Stopped)
      return ZeEXCEPT(Error, "Zxdp", "stop AF_XDP before initialization");
    if (!params.is<Params>())
      return ZeEXCEPT(Error, "Zxdp", "AF_XDP instance requires XDP parameters");
    const auto &config = params.p<Params>();
    auto parsed = parseFilter(config.netmask);
    if (parsed.is<ZeException>()) return ZuMv(parsed).p<ZeException>();
    auto &filter = parsed.p<Filter>();
    if (m_mx) {
      if (m_mx == mx && m_link.ifindex == config.ifindex &&
	  m_spin == config.spin && m_txSid == config.txSid &&
	  m_filter.family == filter.family &&
	  !memcmp(m_filter.address, filter.address, sizeof(filter.address)) &&
	  !memcmp(m_filter.mask, filter.mask, sizeof(filter.mask)))
	return {};
      return ZeEXCEPT(Error, "Zxdp", "conflicting AF_XDP initialization");
    }
    ZmAssert_(config.txSid && config.txSid <= mx->params().nThreads() &&
	config.txSid != mx->rxThread() && config.txSid != mx->txThread());
    auto opened = openLink();
    if (opened.is<ZeException>()) return ZuMv(opened).p<ZeException>();
    int linkFD = opened.p<int>();
    auto queried = queryLink(config.ifindex, linkFD);
    if (queried.is<ZeException>()) {
      ::close(linkFD);
      return ZuMv(queried).p<ZeException>();
    }
    int wakeFD = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wakeFD < 0) {
      auto error = ZeLastError;
      ::close(linkFD);
      return ZeEXCEPT(Error, "Zxdp", ([error](auto &out) {
	out << "AF_XDP wake eventfd creation failed: " << error;
      }));
    }
    m_mx = mx;
    m_linkFD = linkFD;
    m_wakeFD = wakeFD;
    m_link = queried.p<LinkState>();
    m_spin = config.spin;
    m_txSid = config.txSid;
    m_filter = filter;
    return {};
  }

  void reserveTxPools(ZuSpan<Zi::IOBufPool *const> pools) override {
    ZmAssert_(m_mx && Engine::state() == ZmEngineState::Stopped && !allocated());
    m_txReserves.length(pools.length());
    for (unsigned i = 0; i < pools.length(); ++i) m_txReserves[i] = pools[i];
  }

  ZuUnion<void, ZeException> alloc(
    Zi::IOBufPool *rx, const Zi::IOBufPoolList &tx) override
  {
    if (!m_mx || Engine::state() != ZmEngineState::Stopped)
      return ZeEXCEPT(Error, "Zxdp", "initialize and stop AF_XDP before allocation");
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
      return ZeEXCEPT(Error, "Zxdp", "conflicting AF_XDP pool allocation");
    }
    auto ingress = dynamic_cast<BufPool *>(rx);
    if (!ingress || ingress->count < m_link.rxQueues)
      return ZeEXCEPT(Error, "Zxdp", "Rx pool must provision every interface queue");
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
	  return ZeEXCEPT(Error, "Zxdp", "duplicate AF_XDP Tx pool");
      new (txPools.push()) ZmRef<Zi::IOBufPool>{reserve};
    }
    auto created = UMem::create(txPools.cspan(), ingress, m_link.mtu);
    if (created.is<ZeException>()) return ZuMv(created).p<ZeException>();
    auto arena = ZuMv(created).p<ZmRef<UMem>>();
    const auto &layout = arena->layout();
    auto round = [](unsigned n) -> unsigned {
      if (n > (unsigned(1)<<31)) return 0;
      if (n < 2) return 2;
      return unsigned(1)<<(32 - __builtin_clz(n - 1));
    };
    auto r = round(layout.rxCount/m_link.rxQueues + !!(layout.rxCount%m_link.rxQueues));
    auto t = round(layout.txCount);
    if (!r || !t) return ZeEXCEPT(Error, "Zxdp", "AF_XDP ring capacity overflow");
    Queues queues;
    queues.length(m_link.rxQueues);
    for (auto &queue : queues) queue = nullptr;
    for (unsigned i = 0; i < queues.length(); ++i) {
      auto queue = queues[i] = new Queue;
      queue->quota = layout.rxCount/m_link.rxQueues + (i < layout.rxCount%m_link.rxQueues);
      auto opened = queue->owner.owner(arena, m_link.ifindex, i, r, i ? 2 : t,
	i ? queues[0]->owner.fd() : -1);
      if (opened.is<ZeException>()) {
	for (auto j = i + 1; j; --j) delete queues[j - 1];
	return opened;
      }
    }
    for (unsigned i = 0; i < queues.length(); ++i) {
      auto &owner = queues[i]->owner;
      auto id = ZuID{} << "xdp." << ZuBoxed(m_link.ifindex) << '.' << ZuBoxed(i) << '.';
      owner.fill().identify(ZuID{m_mx->id()}, ZuID{id} << "fill", Ztc::QueueType::Tx);
      if (!i) owner.comp().identify(ZuID{m_mx->id()}, ZuID{id} << "cq", Ztc::QueueType::Rx);
    }
    m_rxSlots.length(ingress->count);
    for (auto &index : m_rxSlots) index = End;
    m_txActive.length(layout.txCount);
    m_rxRingCount = r;
    m_txRingCount = t;
    m_queues = ZuMv(queues);
    m_umemFD = m_queues[0]->owner.fd();
    m_rxPool = ingress;
    m_txPools = ZuMv(txPools);
    m_nAppTxPools = appTx;
    m_arena = ZuMv(arena);
    m_rxPool->returned([this] { returned(); });
    m_arena->publish(m_txPools.cspan(), m_rxPool);
    return {};
  }

  bool start(RecvFn fn, StopFn txStopped) override {
    return ZmBlock<bool>{}([this, &fn, &txStopped](auto wake) {
      start(ZuMv(fn), ZuMv(txStopped), ZuMv(wake));
    });
  }

  void start(RecvFn fn, StopFn txStopped, StartFn completion) override {
    {
      ZmGuard guard(m_callbackLock);
      m_everStarted = true;
      auto state = Engine::state();
      if (state == ZmEngineState::Stopped || state == ZmEngineState::Stopping) {
	m_nextReceive = ZuMv(fn);
	m_nextTxStopped = ZuMv(txStopped);
      }
    }
    Engine::start([completion = ZuMv(completion)](bool ok) {
      if (completion) completion(ok);
    });
  }

private:
  void start_() override {
    {
      ZmGuard guard(m_callbackLock);
      m_receive = ZuMv(m_nextReceive);
      m_txStopped = ZuMv(m_nextTxStopped);
    }
    m_stopQueued = StopIdle;
    m_error = {};
    if (!allocated() || !m_mx->running() || !m_receive) {
      m_error = ZeEXCEPT(Error, "Zxdp", "allocate AF_XDP, start its mux and supply a receive callback");
      m_receive = {}; m_txStopped = {};
      Engine::started(false);
      return;
    }
    auto result = open();
    if (result.is<void>()) { Engine::started(true); return; }
    m_error = ZuMv(result).p<ZeException>();
    m_startFailed = true;
    m_stopQueued = StopRequested;
    beginStop();
  }

  void stop_() override { beginStop(); }
  void beginStop() {
    m_ioState = Closing;
    notify();
    m_drainDeadline = monotonicNow() + ZuTime{2};
    if (unsigned(m_txExited)) {
      m_stopQueued = StopRetiring;
      retire();
      return;
    }
    // The caller's Tx owner is quiescent. Drain already queued sender work
    // before the stop continuation takes responsibility for TX/CQ.
    m_txExit = 1;
    m_mx->run([this] {
      m_txExited = 1;
      if (m_txStopped) m_txStopped();
      m_stopQueued = StopRetiring;
      retire();
    }, m_txSid);
  }

  void retire() {
    if (!m_ingressQuiesced) {
      auto result = quiesceIngress();
      if (result.is<ZeException>() && !m_error) m_error = ZuMv(result).p<ZeException>();
    }
    if (!drainTx()) {
      m_mx->push([this] { retire(); }, m_txSid);
      return;
    }
    auto result = close();
    if (result.is<ZeException>() && !m_error) m_error = ZuMv(result).p<ZeException>();
    m_receive = {}; m_txStopped = {};
    if (m_startFailed) {
      m_startFailed = false;
      Engine::started(false);
    } else Engine::stopped(!m_error);
  }

  ZuUnion<void, ZeException> open() {
    ZmGuard guard(m_queueLock);
    ZmAssert_(m_events < 0 && unsigned(m_txExited));
    m_ioState = Closing;
    m_ingressQuiesced = false;
    m_nextQueue = 0;
    m_wakes = m_parking = m_rxReturned = 0;
    m_ioError = 0;
    drainWake();
    if (m_ingressUncertain)
      return ZeEXCEPT(Error, "Zxdp", "AF_XDP ingress retirement could not be proved");
    if (m_txHeld) {
      while (m_queues[0]->owner.comp().available()) reapTx();
      if (m_txHeld)
	return ZeEXCEPT(Error, "Zxdp", "previous AF_XDP Tx loans remain outstanding");
    }
    auto detached = m_attachment.detach();
    if (detached.is<ZeException>()) return detached;
    auto queried = queryLink(m_link.ifindex, m_linkFD);
    if (queried.is<ZeException>()) return ZuMv(queried).p<ZeException>();
    auto current = queried.p<LinkState>();
    if (current.mtu != m_link.mtu || current.rxQueues != m_link.rxQueues ||
	memcmp(current.mac, m_link.mac, sizeof(current.mac)))
      return ZeEXCEPT(Error, "Zxdp", "AF_XDP interface geometry changed after initialization");
    if (current.occupied())
      return ZeEXCEPT(Error, "Zxdp", "interface has an unrelated XDP program");
    auto ioError = [](const char *op) {
      return ZeEXCEPT(Error, "Zxdp", ([op, error = ZeLastError](auto &out) {
	out << op << ": " << error;
      }));
    };
    m_events = epoll_create1(EPOLL_CLOEXEC);
    if (m_events < 0) return ioError("AF_XDP epoll creation");
    epoll_event wakeEvent{};
    wakeEvent.events = EPOLLIN;
    wakeEvent.data.u32 = End;
    if (epoll_ctl(m_events, EPOLL_CTL_ADD, m_wakeFD, &wakeEvent))
      return ioError("AF_XDP wake registration");
    for (unsigned i = 0; i < m_queues.length(); ++i) {
      auto &queue = *m_queues[i];
      auto opened = queue.service.service(m_link.ifindex, i, m_rxRingCount,
	i ? 0 : m_txRingCount, i ? queue.owner.fd() : m_umemFD);
      if (opened.is<ZeException>()) return opened;
      auto id = ZuID{} << "xdp." << ZuBoxed(m_link.ifindex) << '.' << ZuBoxed(i) << '.';
      auto owner = ZuID{m_mx->id()};
      queue.service.rx().identify(owner, ZuID{id} << "rx", Ztc::QueueType::Rx,
	queue.service.fd());
      if (!i) queue.service.tx().identify(owner, ZuID{id} << "tx", Ztc::QueueType::Tx);
      epoll_event event{};
      event.events = EPOLLIN;
      event.data.u32 = i;
      if (epoll_ctl(m_events, EPOLL_CTL_ADD, queue.service.fd(), &event))
	return ioError("AF_XDP readiness registration");
    }
    auto loaded = m_program.load(m_link.rxQueues, m_filter);
    if (loaded.is<ZeException>()) return loaded;
    for (unsigned i = 0; i < m_queues.length(); ++i) {
      auto &queue = *m_queues[i];
      auto result = m_program.publish(i, queue.service.fd());
      if (result.is<ZeException>()) return result;
      queue.published = true;
    }
    auto attached = m_attachment.attach(m_link.ifindex, m_program);
    if (attached.is<ZeException>()) return attached;

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
    if (retired)
      for (auto queue : m_queues)
	while (consumeRx(*queue, false)) { }
    else if (m_rxHeld)
      m_ingressUncertain = true;
    if (error) return error;
    return {};
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
    if (m_ioError) return ZeEXCEPT(Error, "Zxdp", ([error = ZeError{int(m_ioError)}](auto &out) {
      out << "AF_XDP I/O failed: " << error;
    }));
    return {};
  }

  bool drainTx() {
    if (!m_txHeld) return true;
    reapTx();
    if (!m_txHeld) return true;
    auto &socket = m_queues[0]->service;
    if (socket.fd() >= 0 && socket.tx().published()) {
      if (sendto(socket.fd(), nullptr, 0, MSG_DONTWAIT, nullptr, 0) < 0 &&
	  errno != EINTR && errno != EAGAIN && errno != EBUSY && errno != ENOBUFS)
	m_ioError = errno;
    }
    if (monotonicNow() < m_drainDeadline) return false;
    // Any loan still outstanding on device failure remains accounted for.
    // Returning it without a CQ would race the kernel's pinned UMEM.
    if (!m_ioError) m_ioError = ETIMEDOUT;
    return true;
  }

  void replenish(Queue &queue, unsigned budget) {
    auto &fill = queue.owner.fill();
    auto available = fill.available();
    if (budget > available) budget = available;
    auto room = queue.quota - queue.rxHeld;
    if (budget > room) budget = room;
    while (budget--) {
      auto p = m_rxPool->BufPool::alloc();
      if (!p) break;
      auto id = m_rxPool->slot(p);
      ZmAssert_(m_rxSlots[id] == End && fill.reserve(1) == 1);
      m_rxSlots[id] = 0;
      ++m_rxHeld;
      fill.entry(0) = uint64_t(id) * m_arena->layout().chunk;
      fill.commit();
      ++queue.rxHeld;
    }
  }

  void serveTx() {
    if (unsigned(m_txExit) || unsigned(m_ioState) != Active) return;
    auto &socket = m_queues[0]->service;
    auto &ring = socket.tx();
    while (!unsigned(m_txExit)) {
      if (sendto(socket.fd(), nullptr, 0, MSG_DONTWAIT, nullptr, 0) < 0 &&
	  errno != EINTR && errno != EAGAIN && errno != EBUSY && errno != ENOBUFS) {
	m_ioError = errno;
	m_ioState = Closing;
	notify();
	return;
      }
      auto consumed = ring.consumed();
      unsigned n = uint32_t(consumed - m_txConsumed);
      // Keep ownership during a short idle spin. The producer counts each
      // descriptor before publishing it, including work not yet consumed.
      for (unsigned i = 0; i < m_spin && m_txPending.load_() <= n; ++i)
	if (unsigned(m_txExit)) return;
      if (unsigned(m_txExit)) return;
      if (!n) continue;
      m_txConsumed = consumed;
      // The RMW closes the last-drain race: an earlier increment keeps this
      // invocation active; a later zero-to-one increment queues its successor.
      if (!(m_txPending -= n)) return;
    }
  }

  void notify() {
    if (m_wakeFD < 0) return;
    // Count every scheduler pulse, but interrupt the kernel only when Rx has
    // published a wait attempt. The ordered increment pairs with parking.xch
    // and the receiver's final pulse check; neither side can miss the other.
    ++m_wakes;
    signalRx();
  }

  void returned() {
    if (m_ioState != Active || m_mx->invoked(m_mx->rxThread()) || m_rxReturned.xch(1)) return;
    signalRx();
  }

  void signalRx() {
    if (!unsigned(m_parking)) return;
    uint64_t one = 1;
    while (::write(m_wakeFD, &one, sizeof(one)) < 0) {
      if (errno == EINTR) continue;
      ZmAssert_(errno == EAGAIN); // already readable on eventfd saturation
      break;
    }
  }

  void drainWake() {
    uint64_t value;
    while (::read(m_wakeFD, &value, sizeof(value)) < 0) {
      if (errno == EINTR) continue;
      ZmAssert_(errno == EAGAIN);
      break;
    }
  }

  bool woken() {
    if (!uint64_t(m_wakes)) return false;
    --m_wakes; // only the single Rx owner consumes pulses
    return true;
  }

  unsigned receiveQueue(unsigned q);
  unsigned consumeRx(Queue &, bool deliver);
  unsigned reapTx();


  enum { Quantum = 64, End = unsigned(-1) };
  enum { Cold, Active, Closing, Closed };
  enum { StopIdle, StopRequested, StopRetiring }; // one sender-side retirement chain
  using Indices = ZtArray<unsigned, ZtArrayHeapID<"Zxdp.RxSlots">>;
  using Packets = ZtArray<uint8_t *, ZtArrayHeapID<"Zxdp.TxActive">>;
  using Queues = ZtArray<Queue *, ZtArrayHeapID<"Zxdp.Queues">>;
  using Pools = ZtArray<ZmRef<Zi::IOBufPool>, ZtArrayHeapID<"Zxdp.Pools">>;
  Pools		m_txPools, m_txReserves;
  unsigned	m_nAppTxPools = 0;
  using RxPool = ZmRef<BufPool>;
  RxPool	m_rxPool;
  RecvFn	m_receive, m_nextReceive;
  StopFn	m_txStopped, m_nextTxStopped;
  ZmLock	m_callbackLock;
  LinkState	m_link;
  Filter	m_filter;
  ZeException	m_error;
  bool		m_startFailed = false, m_everStarted = false;
  bool		m_ingressQuiesced = true, m_ingressUncertain = false;
  ZmAtomic<unsigned> m_stopQueued{StopIdle};
  unsigned	m_spin = 1000, m_rxRingCount = 0, m_txRingCount = 0;
  ZiMultiplex	*m_mx = nullptr;
  unsigned	m_txSid = 0;
  ZmRef<UMem>	m_arena;
  BPFProgram	m_program;
  BPFAttach	m_attachment;
  Queues	m_queues;
  mutable ZmLock m_queueLock; // enumeration versus initialization and mapping retirement
  int		m_events = -1, m_wakeFD = -1, m_linkFD = -1, m_umemFD = -1;
  ZmAtomic<unsigned> m_ioState{Cold}, m_txExit{1}, m_txExited{1};
  ZmAtomic<int> m_ioError{0};
  ZuTime	m_drainDeadline;
  alignas(Zm::CacheLineSize) ZmAtomic<unsigned> m_txPending{0};
  alignas(Zm::CacheLineSize) ZmAtomic<uint64_t> m_wakes{0};
  ZmAtomic<unsigned> m_parking{0}, m_rxReturned{0};
  alignas(Zm::CacheLineSize) Indices m_rxSlots;
  unsigned	m_rxHeld = 0, m_nextQueue = 0;
  alignas(Zm::CacheLineSize) Packets m_txActive;
  unsigned	m_txHeld = 0;
  // Sender-only accounting must not invalidate shared immutable packet views.
  alignas(Zm::CacheLineSize) uint32_t m_txConsumed = 0;
};

ZmRef<Zi::IOBufPool> Instance::bufPool(
  Zi::IOBufType::T type, unsigned count, unsigned size, unsigned headRoom,
  unsigned tailRoom, unsigned align)
{
  ZmRef<BufPool> pool = new BufPool{type, count, size};
  pool->headRoom = headRoom;
  pool->tailRoom = tailRoom;
  pool->align = align;
  return pool;
}

unsigned Instance::allQueues(Ztc::QueueMgr::AllFn fn) const
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
bool Instance::send(uint8_t *packet, unsigned length)
{
  if (!ready() || !packet || !length) return false;
  auto pool = m_arena->pool(packet);
  if (!pool || pool == m_rxPool.ptr()) return false;
  auto id = pool->slot(packet);
  if (id == BufPool::NoSlot) return false;
  auto canonical = pool->backing(id);
  auto wire = unsigned(packet - canonical);
  if (length > pool->size - wire) return false;
  auto &slot = m_arena->txSlot(*pool, id);
  if (slot.pending) return false;
  auto &ring = m_queues[0]->service.tx();
  if (m_txHeld == ring.count() || !ring.reserve(1)) return false;
  slot.wire = wire;
  slot.active = m_txHeld;
  m_txActive[m_txHeld++] = canonical;
  slot.pending = true;
  ++slot.refs;
  auto address = uint64_t(packet - m_arena->data());
  ring.entry(0) = {address, length, 0};
  bool idle = !m_txPending++;
  ring.commit();
  if (idle) m_mx->run([this] { serveTx(); }, m_txSid);
  return true;
}

void Instance::rx()
{
  unsigned idle = 0, empty = 0;
  auto queueCount = m_queues.length(); // frozen until the caller quiesces Rx for close
  while (ready()) {
    if (woken()) return;
    if (m_rxReturned && m_rxReturned.xch(0)) idle = empty = 0;
    auto q = m_nextQueue++;
    if (m_nextQueue == queueCount) m_nextQueue = 0;
    if (receiveQueue(q)) { idle = empty = 0; continue; }
    // Check/replenish each hardware queue before parking. This visits queues,
    // never searches packets or buffers, and is independent of arena capacity.
    if (++empty < queueCount) continue;
    empty = 0;
    if (idle < m_spin) { ++idle; continue; }
    // Publish before rechecking pulses. A producer either sees this wait and
    // writes eventfd, or its already-published pulse prevents the wait entirely.
    m_parking.xch(1);
    if (uint64_t(m_wakes) || unsigned(m_rxReturned) || !ready()) {
      m_parking = 0;
      continue;
    }
    epoll_event event;
    auto n = epoll_wait(m_events, &event, 1, -1);
    m_parking = 0;
    if (n < 0) {
      if (errno == EINTR) continue;
      m_ioError = errno;
      m_ioState = Closing;
      return;
    }
    // A delayed writer can leave stale readiness after its pulse was consumed.
    // Draining readiness never creates a pulse; only woken() permits a return.
    if (event.data.u32 == End) drainWake();
    else m_nextQueue = event.data.u32;
    idle = 0;
  }
}

unsigned Instance::receiveQueue(unsigned q)
{
  auto &queue = *m_queues[q];
  auto &fill = queue.owner.fill();
  if (!queue.service.rx().available() && !fill.needWakeup() &&
      (queue.rxHeld == queue.quota || m_rxPool->used() >= m_rxPool->count)) return 0;
  replenish(queue, Quantum);
  if (fill.needWakeup()) {
    pollfd wake{queue.service.fd(), POLLIN, 0};
    poll(&wake, 1, 0);
  }
  auto consumed = consumeRx(queue, true);
  if (consumed) replenish(queue, Quantum);
  return consumed;
}

unsigned Instance::consumeRx(Queue &queue, bool deliver)
{
  auto &ring = queue.service.rx();
  const auto &layout = m_arena->layout();
  unsigned n = 0;
  while (n < Quantum && (!deliver || !m_wakes.load_()) && ring.reserve(1)) {
    auto desc = ring.entry(0);
    ring.commit();
    ++n;
    if (desc.addr >= layout.rxLength) continue;
    auto id = unsigned(desc.addr/layout.chunk);
    if (id >= m_rxPool->count || m_rxSlots[id] == End) continue;
    --queue.rxHeld;
    --m_rxHeld;
    m_rxSlots[id] = End;
    auto p = m_arena->data() + desc.addr;
    auto slot = m_rxPool->slot(p);
    auto origin = m_rxPool->backing(id);
    if (!deliver || slot != id || !desc.len || desc.options ||
	desc.len > m_rxPool->size ||
	desc.len > layout.chunk - unsigned(desc.addr & (layout.chunk - 1))) {
      m_rxPool->BufPool::free(origin);
      continue;
    }
    m_receive(p, desc.len);
  }
  return n;
}

unsigned Instance::reapTx()
{
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

} // namespace Xdp

ZmRef<Instance> instance(Type::T type)
{
  switch (type) {
    case Type::Xdp: return new Xdp::Instance;
    default: return nullptr;
  }
}

} // namespace Zxdp
