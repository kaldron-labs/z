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
#include <zlib/ZmAssert.hh>
#include <zlib/ZtArray.hh>
#include <zlib/Zxdp.hh>
#include <zlib/ZxdpBPF.hh>
#include <zlib/ZxdpUMem.hh>
#include <zlib/ZxdpRetire.hh>

namespace Zxdp {

// One kernel ring set per hardware Rx queue, sharing one UMEM registration.
struct Queue : public ZmNHeap<Queue> {
friend inline ZuStringT<"Zxdp.Queue"> ZmNHeapID(Queue *);
  Xsk		socket;
  KernelRetirement retirement;
  unsigned	rxHeld = 0, quota = 0;
  bool		published = false;

  ~Queue() { ZmAssert_(socket.fd() < 0); }
};

struct InstanceData : public ZmNHeap<InstanceData>, public Instance {
friend inline ZuStringT<"Zxdp.Instance"> ZmNHeapID(InstanceData *);
  // Bound each service/retirement turn independently of arena capacity.
  enum { Quantum = 64, End = unsigned(-1) };
  enum { Cold, Active, Closing, Closed };
  using Indices = ZtArray<unsigned, ZtArrayHeapID<"Zxdp.RxSlots">>;
  using Packets = ZtArray<uint8_t *, ZtArrayHeapID<"Zxdp.TxActive">>;

  using Queues = ZtArray<Queue *, ZtArrayHeapID<"Zxdp.Queues">>;
  BufPool	*txPools, *rxPool;
  RecvFn	receive;
  LinkState	link;
  unsigned	interface, spin;
  ZmRef<UMem>	arena;
  BPFProgram	program;
  BPFAttach	attachment;
  Queues	queues;
  ZmAtomic<unsigned> state{Cold};
  int		events = -1, wakeFD = -1;
  ZmAtomic<int> ioError{0};
  alignas(Zm::CacheLineSize) ZmAtomic<uint64_t> wakes{0};
  unsigned	closeQueue = 0;
  alignas(Zm::CacheLineSize) Indices rxSlots;
  Indices	rxActive;
  unsigned	rxHeld = 0, nextQueue = 0;
  alignas(Zm::CacheLineSize) Packets txActive;
  unsigned	txHeld = 0;

  InstanceData(Params params) : Instance{this}, txPools{params.txPools}, rxPool{params.rxPool},
    receive{ZuMv(params.rx)},
    interface{params.interface}, spin{params.spin} { }
  ~InstanceData() override {
    ZmAssert_(state == Closed);
    ZmAssert_(!queues.length());
  }

  bool init(ZuCSpan mask, ZtString<> &error) {
    if (!interface || !rxPool || !receive) {
      error = "interface, Rx pool and receive callback are required";
      return false;
    }
    Filter filter;
    if (!parseFilter(mask, filter, error) || !queryLink(interface, link, error)) return false;
    if (link.occupied()) { error = "interface has an unrelated XDP program"; return false; }
    if (!link.rxQueues || rxPool->count < link.rxQueues) {
      error = "Rx pool must provision every interface queue";
      return false;
    }
    arena = UMem::create(Params{.interface = interface, .txPools = txPools, .rxPool = rxPool},
      link.mtu, error);
    if (!arena) return false;
    const auto &layout = arena->layout();
    auto round = [](unsigned n) -> unsigned {
      if (n > (unsigned(1)<<31)) return 0;
      if (n < 2) return 2;
      return unsigned(1)<<(32 - __builtin_clz(n - 1));
    };
    auto r = round(layout.rxCount/link.rxQueues + !!(layout.rxCount%link.rxQueues));
    auto t = round(layout.txCount);
    if (!r || !t) { error = "AF_XDP ring capacity overflow"; return false; }
    rxSlots.length(rxPool->count);
    rxActive.length(rxPool->count);
    txActive.length(layout.txCount);
    for (auto &index : rxSlots) index = End;
    queues.length(link.rxQueues);
    for (auto &queue : queues) queue = nullptr;
    events = epoll_create1(EPOLL_CLOEXEC);
    if (events < 0) { error = "AF_XDP epoll creation failed"; return false; }
    wakeFD = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wakeFD < 0) { error = "AF_XDP wake eventfd creation failed"; return false; }
    epoll_event wakeEvent{};
    wakeEvent.events = EPOLLIN;
    wakeEvent.data.u32 = End;
    if (epoll_ctl(events, EPOLL_CTL_ADD, wakeFD, &wakeEvent)) {
      error = "AF_XDP wake registration failed";
      return false;
    }
    if (!program.load(link.rxQueues, filter, error) ||
        !attachment.attach(interface, program, error)) return false;
    for (unsigned i = 0; i < link.rxQueues; ++i) {
      auto queue = queues[i] = new Queue;
      queue->quota = layout.rxCount/link.rxQueues + (i < layout.rxCount%link.rxQueues);
      if (!queue->socket.open(arena, interface, i, r, i ? 2 : t, error,
          i ? queues[0]->socket.fd() : -1) ||
          !program.publish(i, queue->socket.fd(), error)) return false;
      epoll_event event{};
      event.events = EPOLLIN;
      event.data.u32 = i;
      if (epoll_ctl(events, EPOLL_CTL_ADD, queue->socket.fd(), &event)) {
        error = "AF_XDP readiness registration failed";
        return false;
      }
    }
    state = Active;
    for (unsigned i = 0; i < link.rxQueues; ++i)
      replenish(*queues[i], queues[i]->quota);
    return true;
  }

  void replenish(Queue &queue, unsigned budget) {
    auto &socket = queue.socket;
    const auto &layout = arena->layout();
    auto available = socket.fill().available();
    if (budget > available) budget = available;
    auto room = queue.quota - queue.rxHeld;
    if (budget > room) budget = room;
    while (budget--) {
      auto p = rxPool->alloc();
      if (!p) break;
      auto id = rxPool->slot(p);
      ZmAssert_(rxSlots[id] == End && socket.fill().reserve(1) == 1);
      rxSlots[id] = rxHeld;
      rxActive[rxHeld++] = id;
      socket.fill().entry(0) = uint64_t(id) * layout.chunk;
      socket.fill().commit();
      ++queue.rxHeld;
      queue.published = true;
    }
  }

  void kick() {
    auto &socket = queues[0]->socket;
    if (!socket.tx().needWakeup()) return;
    if (sendto(socket.fd(), nullptr, 0, MSG_DONTWAIT, nullptr, 0) < 0 &&
        errno != EAGAIN && errno != EBUSY && errno != ENOBUFS && errno != EINTR) {
      ioError = errno;
      state = Closing;
      notify();
    }
  }

  void notify() {
    if (wakeFD < 0) return;
    // The count preserves distinct scheduler wake pulses while the eventfd
    // coalesces readiness. Publish it before interrupting a blocked receiver.
    ++wakes;
    uint64_t one = 1;
    while (::write(wakeFD, &one, sizeof(one)) < 0) {
      if (errno == EINTR) continue;
      ZmAssert_(errno == EAGAIN); // already readable on eventfd saturation
      break;
    }
  }

  void drainWake() {
    uint64_t value;
    while (::read(wakeFD, &value, sizeof(value)) < 0) {
      if (errno == EINTR) continue;
      ZmAssert_(errno == EAGAIN);
      break;
    }
  }

  bool woken() {
    if (!uint64_t(wakes)) return false;
    --wakes; // only the single Rx owner consumes pulses
    drainWake();
    return true;
  }

  unsigned receiveQueue(unsigned q);

  bool close(ZtString<> &error) {
    error = {};
    if (state == Closed) return true;
    state = Closing;
    if (arena && rxPool->ready()) arena->retire(txPools, rxPool);
    // Retire one queue per call. No packet backing returns before every queue
    // has independently proved the end of kernel access.
    if (closeQueue < queues.length()) {
      auto queue = queues[closeQueue];
      if (queue) {
        if (program.fd() >= 0 && !program.quiesce(closeQueue, error)) return false;
        if (queue->published) {
          auto &retirement = queue->retirement;
          auto result = retirement.done() ? KernelRetirement::Done :
            retirement.waiting() ? retirement.retry(error) :
            retirement.begin(queue->socket, error);
          if (result != KernelRetirement::Done) return false;
          retirement.reset();
        } else queue->socket.close();
        delete queue;
        queues[closeQueue] = nullptr;
      }
      if (++closeQueue < queues.length()) return false;
    }
    queues = {};
    if (events >= 0) { ::close(events); events = -1; }
    if (wakeFD >= 0) { ::close(wakeFD); wakeFD = -1; }
    unsigned budget = Quantum;
    while (rxHeld && budget--) {
      auto id = rxActive[--rxHeld];
      rxSlots[id] = End;
      rxPool->free(rxPool->backing(id));
    }
    if (rxHeld) return false;
    budget = Quantum;
    while (txHeld && budget--) {
      auto p = txActive[--txHeld];
      auto pool = arena->pool(p);
      auto &slot = arena->txSlot(*pool, pool->slot(p));
      slot.pending = false;
      slot.active = End;
      pool->free(p);
    }
    if (txHeld) return false;
    bool detached = attachment.detach(error);
    if (attachment.ifindex()) return false;
    program.reset();
    state = Closed;
    if (ioError) {
      error = "AF_XDP I/O failed: ";
      error << strerror(ioError);
      return false;
    }
    return detached;
  }
};

ZmRef<Instance> Instance::create(Params params, ZtString<> &error)
{
  auto mask = params.netmask;
  ZmRef<InstanceData> instance = new InstanceData{ZuMv(params)};
  if (instance->init(mask, error)) return instance;
  auto cause = ZuMv(error);
  ZtString<> cleanup;
  if (!instance->close(cleanup) && instance->state != InstanceData::Closed) {
    error = ZuMv(cause);
    error << "; rollback pending: " << cleanup;
    return instance;
  }
  error = ZuMv(cause);
  return nullptr;
}
Instance::~Instance() = default;
bool Instance::ready() const { return m_data->state == InstanceData::Active; }
unsigned Instance::ifindex() const { return m_data->interface; }
unsigned Instance::mtu() const { return m_data->link.mtu; }
bool Instance::zeroCopy() const { return (m_data->queues.length() && m_data->queues[0] && m_data->queues[0]->socket.zeroCopy()); }
const uint8_t *Instance::mac() const { return m_data->link.mac; }
bool Instance::close(ZtString<> &error) { return m_data->close(error); }

bool Instance::send(uint8_t *packet, unsigned length)
{
  if (!ready() || !packet || !length) return false;
  auto pool = m_data->arena->pool(packet);
  if (!pool || pool == m_data->rxPool) return false;
  auto id = pool->slot(packet);
  if (id == BufPool::NoSlot) return false;
  auto canonical = pool->backing(id);
  auto wire = unsigned(packet - canonical);
  if (length > pool->size - wire) return false;
  auto &slot = m_data->arena->txSlot(*pool, id);
  if (slot.pending) return false;
  auto &ring = m_data->queues[0]->socket.tx();
  if (m_data->txHeld == ring.count() || !ring.reserve(1)) return false;
  slot.wire = wire;
  slot.active = m_data->txHeld;
  m_data->txActive[m_data->txHeld++] = canonical;
  slot.pending = true;
  ++slot.refs;
  auto address = uint64_t(packet - m_data->arena->data());
  ring.entry(0) = {address, length, 0};
  ring.commit();
  m_data->kick();
  return true;
}

void Instance::wake() { m_data->notify(); }

void Instance::rx()
{
  unsigned idle = 0, empty = 0;
  while (ready()) {
    if (m_data->woken()) return;
    auto q = m_data->nextQueue++;
    if (m_data->nextQueue == m_data->queues.length()) m_data->nextQueue = 0;
    if (m_data->receiveQueue(q)) { idle = empty = 0; continue; }
    // Check/replenish each hardware queue before parking. This visits queues,
    // never searches packets or buffers, and is independent of arena capacity.
    if (++empty < m_data->queues.length()) continue;
    empty = 0;
    if (idle < m_data->spin) { ++idle; continue; }
    epoll_event event;
    auto n = epoll_wait(m_data->events, &event, 1, -1);
    if (n < 0) {
      if (errno == EINTR) continue;
      m_data->ioError = errno;
      m_data->state = InstanceData::Closing;
      return;
    }
    if (event.data.u32 == InstanceData::End) m_data->drainWake();
    else m_data->nextQueue = event.data.u32;
    idle = 0;
  }
}

unsigned InstanceData::receiveQueue(unsigned q)
{
  replenish(*queues[q], Quantum);
  auto &queue = *queues[q];
  auto &socket = queue.socket;
  if (socket.fill().needWakeup()) {
    pollfd wake{socket.fd(), POLLIN, 0};
    poll(&wake, 1, 0);
  }
  const auto &layout = arena->layout();
  auto &ring = socket.rx();
  unsigned n = 0;
  while (n < Quantum && !wakes.load_() && ring.reserve(1)) {
    auto desc = ring.entry(0);
    ring.commit();
    ++n;
    if (desc.addr >= layout.rxLength) continue;
    auto id = unsigned(desc.addr/layout.chunk);
    if (id >= rxPool->count) continue;
    auto &index = rxSlots[id];
    if (index == End) continue;
    --queue.rxHeld;
    auto last = --rxHeld;
    if (index != last) {
      auto moved = rxActive[last];
      rxActive[index] = moved;
      rxSlots[moved] = index;
    }
    index = End;
    auto p = arena->data() + desc.addr;
    auto slot = rxPool->slot(p);
    auto origin = rxPool->backing(id);
    if (slot != id || !desc.len || desc.options ||
	desc.len > rxPool->size ||
	desc.len > layout.chunk - unsigned(desc.addr & (layout.chunk - 1))) {
      rxPool->free(origin);
      continue;
    }
    receive(p, desc.len);
  }
  replenish(queue, Quantum);
  return n;
}

unsigned Instance::pollTx()
{
  if (!ready()) return 0;
  m_data->kick();
  unsigned count = 0;
  auto &ring = m_data->queues[0]->socket.comp();
  auto n = ring.reserve(InstanceData::Quantum);
  const auto &layout = m_data->arena->layout();
  for (unsigned i = 0; i < n; ++i) {
    auto address = ring.entry(i);
    if (address < layout.rxLength || address >= layout.length) continue;
    auto p = m_data->arena->data() + address;
    auto pool = m_data->arena->pool(p);
    if (!pool) continue;
    auto id = pool->slot(p);
    if (id == BufPool::NoSlot) continue;
    auto canonical = pool->backing(id);
    auto &slot = m_data->arena->txSlot(*pool, id);
    // Generic Linux Tx tags the skb destructor address in bit zero and CQ
    // clears that bit. A slot has only one outstanding descriptor; compare
    // its offset excluding that tag, without a search or extra bookkeeping.
    if (!slot.pending || ((slot.wire ^ unsigned(p - canonical)) & ~1U)) continue;
    slot.pending = false;
    auto last = --m_data->txHeld;
    if (slot.active != last) {
      auto moved = m_data->txActive[last];
      m_data->txActive[slot.active] = moved;
      auto owner = m_data->arena->pool(moved);
      m_data->arena->txSlot(*owner, owner->slot(moved)).active = slot.active;
    }
    slot.active = InstanceData::End;
    pool->free(canonical);
    ++count;
  }
  ring.commit();
  return count;
}

} // namespace Zxdp
