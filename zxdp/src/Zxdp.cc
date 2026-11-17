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

#include <zlib/ZuBox.hh>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmLock.hh>
#include <zlib/ZmGuard.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/Zxdp.hh>
#include <zlib/ZxdpBPF.hh>
#include <zlib/ZxdpUMem.hh>
#include <zlib/ZxdpRetire.hh>
#include <zlib/ZxdpPool.hh>

namespace Zxdp {

ZtEnumImplNS(Type);

namespace Xdp {

// One kernel ring set per hardware Rx queue, sharing one UMEM registration.
struct Queue : public ZmNHeap<Queue> {
friend inline ZuStringT<"Zxdp.Queue"> ZmNHeapID(Queue *);
  using Retirement = KernelRetirement;
  Xsk		socket;
  Retirement	retirement;
  unsigned	rxHeld = 0, quota = 0;
  bool		published = false;

  ~Queue() { ZmAssert_(socket.fd() < 0); }
};

class Instance final : public ZmNHeap<Instance>, public Zxdp::Instance {
friend inline ZuStringT<"Zxdp.Instance"> ZmNHeapID(Instance *);

public:
  using ZmNHeap<Instance>::operator new;
  using ZmNHeap<Instance>::operator delete;
  Instance() = default;
  ~Instance() override {
    ZmAssert_(m_state == Cold || m_state == Closed);
    for (auto queue : m_queues) delete queue;
  }
  ZmRef<Zi::IOBufPool> bufPool(
    unsigned count, unsigned size, unsigned headRoom, unsigned tailRoom, unsigned align) override;
  bool ready() const override { return m_state == Active; }
  unsigned mtu() const override { return m_link.mtu; }
  bool zeroCopy() const override {
    auto queue = m_queues.length() ? m_queues[0] : nullptr;
    return queue && queue->socket.zeroCopy();
  }
  ZuBSpan mac() const override { return m_link.mac; }
  bool send(uint8_t *, unsigned) override;
  void rx() override;
  void wake() override { notify(); }
  unsigned pollTx() override;
  unsigned allQueues(Ztc::QueueMgr::AllFn) const override;
  ZuUnion<void, ZeException> init(const Zxdp::Params &params) override {
    if (m_state != Cold)
      return ZeEXCEPT(Error, "Zxdp", "AF_XDP instance has already been initialized");
    m_state = Closing;
    if (!params.is<Params>())
      return ZeEXCEPT(Error, "Zxdp", "AF_XDP instance requires XDP parameters");
    const auto &config = params.p<Params>();
    m_ifindex = config.ifindex;
    m_rxPool = dynamic_cast<BufPool *>(config.rxPool.ptr());
    m_receive = config.rx;
    m_txStopped = config.txStopped;
    m_spin = config.spin;
    m_scheduler = config.scheduler;
    m_txSid = config.txSid;
    m_txPools.length(config.txPools.count_());
    {
      auto pools = config.txPools.citer();
      unsigned i = 0;
      while (auto pool = pools()) m_txPools[i++] = static_cast<Zi::IOBufPool *>(pool);
    }
    if (!m_ifindex || !m_txPools.length() || !m_rxPool || !m_receive) {
      return ZeEXCEPT(Error, "Zxdp",
	"interface, Tx pools, Rx pool and receive callback are required");
    }
    if (!m_scheduler || !m_txSid || m_txSid > m_scheduler->params().nThreads()) {
      return ZeEXCEPT(Error, "Zxdp", "AF_XDP requires a scheduler and assigned Tx slot");
    }
    auto parsed = parseFilter(config.netmask);
    if (parsed.is<ZeException>()) return ZuMv(parsed).p<ZeException>();
    auto filter = ZuMv(parsed).p<Filter>();
    auto queried = queryLink(m_ifindex);
    if (queried.is<ZeException>()) return ZuMv(queried).p<ZeException>();
    m_link = ZuMv(queried).p<LinkState>();
    if (m_link.occupied())
      return ZeEXCEPT(Error, "Zxdp", "interface has an unrelated XDP program");
    if (!m_link.rxQueues || m_rxPool->count < m_link.rxQueues) {
      return ZeEXCEPT(Error, "Zxdp", "Rx pool must provision every interface queue");
    }
    auto created = UMem::create(config.txPools, m_rxPool.ptr(), m_link.mtu);
    if (created.is<ZeException>()) return ZuMv(created).p<ZeException>();
    m_arena = ZuMv(created).p<ZmRef<UMem>>();
    const auto &layout = m_arena->layout();
    auto round = [](unsigned n) -> unsigned {
      if (n > (unsigned(1)<<31)) return 0;
      if (n < 2) return 2;
      return unsigned(1)<<(32 - __builtin_clz(n - 1));
    };
    auto r = round(layout.rxCount/m_link.rxQueues + !!(layout.rxCount%m_link.rxQueues));
    auto t = round(layout.txCount);
    if (!r || !t) { return ZeEXCEPT(Error, "Zxdp", "AF_XDP ring capacity overflow"); }
    m_rxSlots.length(m_rxPool->count);
    m_rxActive.length(m_rxPool->count);
    m_txActive.length(layout.txCount);
    for (auto &index : m_rxSlots) index = End;
    m_queues.length(m_link.rxQueues);
    for (auto &queue : m_queues) queue = nullptr;
    m_events = epoll_create1(EPOLL_CLOEXEC);
    if (m_events < 0) { return ZeEXCEPT(Error, "Zxdp", ([error = ZeLastError](auto &out) {
	out << "AF_XDP epoll creation failed: " << error;
      })); }
    m_wakeFD = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (m_wakeFD < 0) { return ZeEXCEPT(Error, "Zxdp", ([error = ZeLastError](auto &out) {
	out << "AF_XDP wake eventfd creation failed: " << error;
      })); }
    epoll_event wakeEvent{};
    wakeEvent.events = EPOLLIN;
    wakeEvent.data.u32 = End;
    if (epoll_ctl(m_events, EPOLL_CTL_ADD, m_wakeFD, &wakeEvent)) {
      return ZeEXCEPT(Error, "Zxdp", ([error = ZeLastError](auto &out) {
	out << "AF_XDP wake registration failed: " << error;
      }));
    }
    auto loaded = m_program.load(m_link.rxQueues, filter);
    if (loaded.is<ZeException>()) return loaded;
    auto attached = m_attachment.attach(m_ifindex, m_program);
    if (attached.is<ZeException>()) return attached;
    for (unsigned i = 0; i < m_link.rxQueues; ++i) {
      auto queue = m_queues[i] = new Queue;
      queue->quota = layout.rxCount/m_link.rxQueues + (i < layout.rxCount%m_link.rxQueues);
      auto opened = queue->socket.open(m_arena, m_ifindex, i, r, i ? 2 : t,
	i ? m_queues[0]->socket.fd() : -1);
      if (opened.is<ZeException>()) return opened;
      auto id = ZuID{} << "xdp." << ZuBoxed(m_ifindex) << '.' << ZuBoxed(i) << '.';
      auto owner = ZuID{m_scheduler->id()};
      queue->socket.rx().identify(owner, ZuID{id} << "rx", Ztc::QueueType::Rx,
	queue->socket.fd());
      queue->socket.fill().identify(owner, ZuID{id} << "fill", Ztc::QueueType::Tx);
      queue->socket.tx().identify(owner, ZuID{id} << "tx", Ztc::QueueType::Tx);
      queue->socket.comp().identify(owner, ZuID{id} << "cq", Ztc::QueueType::Rx);
      auto published = m_program.publish(i, queue->socket.fd());
      if (published.is<ZeException>()) return published;
      epoll_event event{};
      event.events = EPOLLIN;
      event.data.u32 = i;
      if (epoll_ctl(m_events, EPOLL_CTL_ADD, queue->socket.fd(), &event)) {
	return ZeEXCEPT(Error, "Zxdp", ([error = ZeLastError](auto &out) {
	out << "AF_XDP readiness registration failed: " << error;
      }));
      }
    }
    m_txConsumed = m_queues[0]->socket.tx().consumed();
    m_state = Active;
    m_txExit = m_txExited = 0;
    for (unsigned i = 0; i < m_link.rxQueues; ++i)
      replenish(*m_queues[i], m_queues[i]->quota);
    return {};
  }
  ZuUnion<bool, ZeException> close() override {
    if (m_state == Closed) return true;
    m_state = Closing;
    if (!stopTx()) return false;
    if (m_arena && m_rxPool->ready()) {
      m_rxPool->retire();
      for (const auto &pool : m_txPools) static_cast<BufPool *>(pool.ptr())->retire();
    }
    // Retire one queue per call. No packet backing returns before every queue
    // has independently proved the end of kernel access.
    if (m_closeQueue < m_queues.length()) {
      ZmGuard guard(m_queueLock);
      auto queue = m_queues[m_closeQueue];
      if (queue) {
	if (m_program.fd() >= 0) {
	  auto quiesced = m_program.quiesce(m_closeQueue);
	  if (quiesced.is<ZeException>()) return ZuMv(quiesced).p<ZeException>();
	}
	if (queue->published) {
	  auto &retirement = queue->retirement;
	  ZuUnion<int, ZeException> result = retirement.done() ?
	    ZuUnion<int, ZeException>{int(KernelRetirement::Done)} :
	    retirement.waiting() ? retirement.retry() : retirement.begin(queue->socket);
	  if (result.is<ZeException>()) return ZuMv(result).p<ZeException>();
	  if (result.p<int>() != KernelRetirement::Done) return false;
	  retirement.reset();
	} else queue->socket.close();
      }
      if (++m_closeQueue < m_queues.length()) return false;
    }
    if (m_events >= 0) { ::close(m_events); m_events = -1; }
    if (m_wakeFD >= 0) { ::close(m_wakeFD); m_wakeFD = -1; }
    unsigned budget = Quantum;
    while (m_rxHeld && budget--) {
      auto id = m_rxActive[--m_rxHeld];
      m_rxSlots[id] = End;
      m_rxPool->BufPool::free(m_rxPool->backing(id));
    }
    if (m_rxHeld) return false;
    budget = Quantum;
    while (m_txHeld && budget--) {
      auto p = m_txActive[--m_txHeld];
      auto pool = m_arena->pool(p);
      auto &slot = m_arena->txSlot(*pool, pool->slot(p));
      slot.pending = false;
      slot.active = End;
      pool->BufPool::free(p);
    }
    if (m_txHeld) return false;
    auto detached = m_attachment.detach();
    if (m_attachment.ifindex()) {
      if (detached.is<ZeException>()) return ZuMv(detached).p<ZeException>();
      return false;
    }
    m_program.reset();
    m_state = Closed;
    if (m_ioError) {
      return ZeEXCEPT(Error, "Zxdp", ([error = ZeError{int(m_ioError)}](auto &out) {
	out << "AF_XDP I/O failed: " << error;
      }));
    }
    if (detached.is<ZeException>()) return ZuMv(detached).p<ZeException>();
    return true;
  }

  bool stopTx() override {
    if (unsigned(m_txExited)) return true;
    if (m_scheduler->state() == ZmEngineState::Stopped) {
      m_txPending = 0;
      m_txExit = m_txExited = 1;
      return true;
    }
    // The Tx producer is quiescent. This FIFO barrier drains queued sends;
    // callbacks never enqueue successors, so none can overtake the barrier.
    if (!m_txExit.xch(1)) {
      m_scheduler->run([this, stopped = m_txStopped] {
	// Descriptor accounting ends here; CQ or safe socket retirement still
	// owns every submitted backing lease.
	m_txPending = 0;
	m_txExited = 1;
	if (stopped) stopped();
      }, m_txSid);
      return false;
    }
    return unsigned(m_txExited);
  }

private:

  void replenish(Queue &queue, unsigned budget) {
    auto &socket = queue.socket;
    const auto &layout = m_arena->layout();
    auto available = socket.fill().available();
    if (budget > available) budget = available;
    auto room = queue.quota - queue.rxHeld;
    if (budget > room) budget = room;
    while (budget--) {
      auto p = m_rxPool->BufPool::alloc();
      if (!p) break;
      auto id = m_rxPool->slot(p);
      ZmAssert_(m_rxSlots[id] == End && socket.fill().reserve(1) == 1);
      m_rxSlots[id] = m_rxHeld;
      m_rxActive[m_rxHeld++] = id;
      socket.fill().entry(0) = uint64_t(id) * layout.chunk;
      socket.fill().commit();
      ++queue.rxHeld;
      queue.published = true;
    }
  }

  void serveTx() {
    if (unsigned(m_txExit) || unsigned(m_state) != Active) return;
    auto &socket = m_queues[0]->socket;
    auto &ring = socket.tx();
    while (!unsigned(m_txExit)) {
      if (sendto(socket.fd(), nullptr, 0, MSG_DONTWAIT, nullptr, 0) < 0 &&
	  errno != EINTR && errno != EAGAIN && errno != EBUSY && errno != ENOBUFS) {
	m_ioError = errno;
	m_state = Closing;
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


  enum { Quantum = 64, End = unsigned(-1) };
  enum { Cold, Active, Closing, Closed };
  using Indices = ZtArray<unsigned, ZtArrayHeapID<"Zxdp.RxSlots">>;
  using Packets = ZtArray<uint8_t *, ZtArrayHeapID<"Zxdp.TxActive">>;
  using Queues = ZtArray<Queue *, ZtArrayHeapID<"Zxdp.Queues">>;
  using Pools = ZtArray<ZmRef<Zi::IOBufPool>, ZtArrayHeapID<"Zxdp.Pools">>;
  Pools		m_txPools;
  using RxPool = ZmRef<BufPool>;
  RxPool	m_rxPool;
  RecvFn	m_receive;
  StopFn	m_txStopped;
  LinkState	m_link;
  unsigned	m_ifindex = 0, m_spin = 1000;
  ZmScheduler	*m_scheduler = nullptr;
  unsigned	m_txSid = 0;
  ZmRef<UMem>	m_arena;
  BPFProgram	m_program;
  BPFAttach	m_attachment;
  Queues	m_queues;
  mutable ZmLock m_queueLock; // cold enumeration versus mapping retirement only
  int		m_events = -1, m_wakeFD = -1;
  ZmAtomic<unsigned> m_state{Cold}, m_txExit{1}, m_txExited{1};
  ZmAtomic<int> m_ioError{0};
  unsigned	m_closeQueue = 0;
  alignas(Zm::CacheLineSize) ZmAtomic<unsigned> m_txPending{0};
  alignas(Zm::CacheLineSize) ZmAtomic<uint64_t> m_wakes{0};
  ZmAtomic<unsigned> m_parking{0};
  alignas(Zm::CacheLineSize) Indices m_rxSlots;
  Indices	m_rxActive;
  unsigned	m_rxHeld = 0, m_nextQueue = 0;
  alignas(Zm::CacheLineSize) Packets m_txActive;
  unsigned	m_txHeld = 0;
  // Sender-only accounting must not invalidate shared immutable packet views.
  alignas(Zm::CacheLineSize) uint32_t m_txConsumed = 0;
};

ZmRef<Zi::IOBufPool> Instance::bufPool(
  unsigned count, unsigned size, unsigned headRoom, unsigned tailRoom, unsigned align)
{
  ZmRef<BufPool> pool = new BufPool{count, size};
  pool->headRoom = headRoom;
  pool->tailRoom = tailRoom;
  pool->align = align;
  return pool;
}

unsigned Instance::allQueues(Ztc::QueueMgr::AllFn fn) const
{
  ZmGuard guard(m_queueLock);
  unsigned count = 0;
  for (auto queue : m_queues) {
    if (!queue) continue;
    auto &socket = queue->socket;
    fn(&socket.rx()); fn(&socket.fill()); fn(&socket.tx()); fn(&socket.comp());
    count += 4;
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
  auto &ring = m_queues[0]->socket.tx();
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
  if (idle) m_scheduler->run([this] { serveTx(); }, m_txSid);
  return true;
}

void Instance::rx()
{
  unsigned idle = 0, empty = 0;
  auto queueCount = m_queues.length(); // frozen until the caller quiesces Rx for close
  while (ready()) {
    if (woken()) return;
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
    if (uint64_t(m_wakes)) { m_parking = 0; continue; }
    epoll_event event;
    auto n = epoll_wait(m_events, &event, 1, -1);
    m_parking = 0;
    if (n < 0) {
      if (errno == EINTR) continue;
      m_ioError = errno;
      m_state = Closing;
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
  auto &socket = queue.socket;
  auto &ring = socket.rx();
  // Empty probes need no service work unless FILL can accept returned backing
  // or the driver requests a wake. Keep both checks live even during long spins.
  if (!ring.available() && !socket.fill().needWakeup() &&
      (queue.rxHeld == queue.quota || m_rxPool->used() >= m_rxPool->count)) return 0;
  replenish(queue, Quantum);
  if (socket.fill().needWakeup()) {
    pollfd wake{socket.fd(), POLLIN, 0};
    poll(&wake, 1, 0);
  }
  const auto &layout = m_arena->layout();
  unsigned n = 0;
  while (n < Quantum && !m_wakes.load_() && ring.reserve(1)) {
    auto desc = ring.entry(0);
    ring.commit();
    ++n;
    if (desc.addr >= layout.rxLength) continue;
    auto id = unsigned(desc.addr/layout.chunk);
    if (id >= m_rxPool->count) continue;
    auto &index = m_rxSlots[id];
    if (index == End) continue;
    --queue.rxHeld;
    auto last = --m_rxHeld;
    if (index != last) {
      auto moved = m_rxActive[last];
      m_rxActive[index] = moved;
      m_rxSlots[moved] = index;
    }
    index = End;
    auto p = m_arena->data() + desc.addr;
    auto slot = m_rxPool->slot(p);
    auto origin = m_rxPool->backing(id);
    if (slot != id || !desc.len || desc.options ||
	desc.len > m_rxPool->size ||
	desc.len > layout.chunk - unsigned(desc.addr & (layout.chunk - 1))) {
      m_rxPool->BufPool::free(origin);
      continue;
    }
    m_receive(p, desc.len);
  }
  if (n) replenish(queue, Quantum);
  return n;
}

unsigned Instance::pollTx()
{
  if (!ready()) return 0;
  unsigned count = 0;
  auto &ring = m_queues[0]->socket.comp();
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
