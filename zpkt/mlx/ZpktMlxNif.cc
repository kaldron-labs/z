//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZpktNif.hh>

#include "ZpktMlx.hh"
#include "ZpktMlxEnv.hh"
#include "ZpktMlxNif.hh"

namespace Zpkt::Mlx {

static ZeException deviceError(const char *operation, int code)
{
  return ZeEXCEPT(Error, "Zpkt.Mlx", ([operation, code](auto &s) {
    s << operation << ": " << ZeError{code};
  }));
}

Nif::Nif(
  Env &env, ZiMultiplex *mx, const NifCf &config, const NifInfo &info, bool loopback) :
  Zpkt::Nif{mx, info}, m_env{env}, m_config{config}
{
  ZmAssert_(mx && mx->state() == ZmEngineState::Stopped);
  if (loopback) {
    nifInfo({0, 1500, 1, {0x02, 0, 0, 0, 0, 1}});
    return;
  }
  if (!info.ifindex || info.ifindex > INT_MAX || !info.mtu)
    throw deviceError("validate mlx interface geometry", EINVAL);
  auto discovered = findPort(info.ifindex);
  if (discovered.is<ZeException>()) throw ZuMv(discovered).p<ZeException>();
  auto port = discovered.p<Port>();
  if (port.number > UINT8_MAX) throw deviceError("validate mlx RDMA port number", EINVAL);
  auto opened = m_env.context(port.device);
  if (opened.is<ZeException>()) throw ZuMv(opened).p<ZeException>();
  auto context = opened.p<ibv_context *>();
  ibv_port_attr attr{};
  if (int rc = ibv_query_port(context, uint8_t(port.number), &attr))
    throw deviceError("query mlx RDMA port", rc);
  if (attr.link_layer != IBV_LINK_LAYER_ETHERNET)
    throw deviceError("require an Ethernet mlx RDMA port", ENOTSUP);
  m_ingressFD = nifClaimIngress(info.ifindex);
  if (m_ingressFD < 0) throw deviceError("claim mlx netdev ingress", -m_ingressFD);
  m_pd = ibv_alloc_pd(context);
  if (!m_pd) {
    int rc = errno;
    ::close(m_ingressFD);
    throw deviceError("allocate mlx protection domain", rc);
  }
  m_port = port.number;
  rxQueues(1);
}

Nif::~Nif()
{
  ZmAssert_(state() == ZmEngineState::Stopped);
  if (mx()) final();
  ZmAssert_(!mx());
}

ZmRef<Zi::IOBufPool> Nif::bufPool(
  Zi::IOBufType::T type, unsigned count, unsigned size,
  unsigned headRoom, unsigned tailRoom, unsigned align)
{
  return new BufPool{type, count, size, headRoom, tailRoom, align};
}

void Nif::reserveTxPools(ZuSpan<Zi::IOBufPool *const> pools)
{
  ZmAssert_(mx() && state() == ZmEngineState::Stopped && !allocated());
  m_reserves.null();
  for (auto pool : pools) {
    auto native = dynamic_cast<BufPool *>(pool);
    ZmAssert_(native && (native->type == Zi::IOBufType::TxTCP ||
      native->type == Zi::IOBufType::TxUDP));
    m_reserves.push(ZmRef<BufPool>{native});
  }
}

Nif::Result Nif::alloc(Zi::IOBufPool *rxPool, const Zi::IOBufPoolList &txPools)
{
  if (!mx() || m_finalizing || state() != ZmEngineState::Stopped)
    return deviceError("allocate mlx packet pools while stopped", EBUSY);
  bool hadPools = allocated();
  auto result = m_pools.alloc(rxPool, txPools, m_reserves, mtu(), m_pd);
  if (!hadPools && result.is<void>()) m_pools.rx()->returned([this] { returned(); });
  return result;
}

void Nif::final()
{
  ZmAssert_(state() == ZmEngineState::Stopped);
  ZmGuard guard(m_observe);
  if (!mx()) return;
  if (int rc = m_queue.close()) { error(deviceError("close mlx queues", rc)); return; }
  m_steering = 0;
  m_txPending = 0;
  if (!m_pools.released()) {
    error(deviceError("mlx packet buffers still held", EBUSY));
    return;
  }
  m_finalizing = true;
  auto result = m_pools.final();
  if (result.is<ZeException>()) { error(ZuMv(result).p<ZeException>()); return; }
  if (m_pd) {
    if (int rc = ibv_dealloc_pd(m_pd)) {
      error(deviceError("release mlx protection domain", rc));
      return;
    }
    m_pd = nullptr;
  }
  if (m_ingressFD >= 0) { ::close(m_ingressFD); m_ingressFD = -1; }
  m_reserves.null();
  resetInfo();
  error({});
  final_();
}

int Nif::activate()
{
  auto &pool = *m_pools.rx();
  if (m_config.rxDepth > pool.count) return EINVAL;
  int rc = m_queue.close(); // retry any incomplete teardown before using backing
  if (rc) return rc;
  if (m_port) {
    rc = m_queue.start(m_pd, m_port, m_config.rxDepth, m_config.txDepth);
    if (!rc) rc = m_queue.refill(pool);
    if (!rc && m_queue.rxPosted() != m_config.rxDepth) rc = ENOBUFS;
    if (!rc) rc = m_queue.steer(m_port);
    m_steering = m_queue.steered();
    if (rc) {
      if (int cleanup = m_queue.close())
	ZiLogEvent(deviceError("clean up failed mlx start", cleanup));
      m_steering = m_queue.steered();
      return rc;
    }
  } else {
    rc = m_loop.start(pool, m_config.rxDepth, m_config.txDepth);
  }
  return rc;
}

void Nif::start_()
{
  error({});
  if (!mx() || m_finalizing || !mx()->running() || !allocated() || !receiving()) {
    error(deviceError("start requires allocated pools, a running mux and Rx callback", EINVAL));
  } else if (int fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC); fd < 0) {
    error(deviceError("create mlx wake eventfd", errno));
  } else {
    wakeFD(fd);
    if (int rc = activate()) {
      error(deviceError(m_port ? "activate mlx raw packet queues" : "start mlx software loop", rc));
      closeWake();
    } else {
      resetWake();
      zeroCopy(bool(m_port));
      m_serviceError = 0;
      m_rxPackets = m_rxBytes = m_discards = 0;
      m_txPackets = m_txBytes = m_txRetired = m_txRetiredBytes = 0;
      m_txPending = 0;
      m_gate = ((m_gate.load_() & RunMask) + RunStep) | Ready;
      Engine::started(true);
      return;
    }
  }
  clearCallbacks();
  Engine::started(false);
}

void Nif::stop_()
{
  m_gate.xchAnd(~uint64_t(Ready));
  if (m_port) {
    if (int rc = m_queue.unsteer()) m_serviceError.cmpXch(rc, 0);
    m_steering = m_queue.steered();
  }
  signal();
  mx()->rxRun([this] {
    mx()->txRun([this] {
      if (!m_port) {
	drainTx();
	finishCallbacks();
      } else {
	drainTx();
	int rc = m_queue.close();
	m_steering = m_queue.steered();
	m_txPending = m_queue.txPending();
	if (rc) {
	  m_serviceError.cmpXch(rc, 0);
	  clearCallbacks();
	} else {
	  finishCallbacks();
	}
      }
      mx()->rxRun([this] { finishStop(); });
    });
  });
}

void Nif::fail(int code)
{
  if (m_serviceError.cmpXch(code, 0)) return;
  m_gate.xchAnd(~uint64_t(Ready));
  signal();
  if (!stopping()) mx()->rxRun([this] {
    if (!stopping() && state() != ZmEngineState::Stopped) stop(StartFn{});
  });
}

void Nif::finishStop()
{
  uint64_t gate = m_gate;
  while (gate & WriterMask) {
    auto observed = m_gate.cmpXch(gate | FinishWaiting, gate);
    if (observed == gate) return;
    gate = observed;
  }
  m_gate.xchAnd(~uint64_t(FinishWaiting));
  if (!m_port) m_loop.stop(*m_pools.rx());
  closeWake();
  zeroCopy(false);
  if (int rc = m_serviceError) error(deviceError("service mlx packet queues", rc));
  Engine::stopped(!failed());
}

bool Nif::admitWriter()
{
  uint64_t gate = m_gate;
  uint64_t run = gate & RunMask;
  while ((gate & Ready) && (gate & RunMask) == run) {
    ZmAssert_((gate & WriterMask) != WriterMask);
    auto observed = m_gate.cmpXch(gate + Writer, gate);
    if (observed == gate) return true;
    gate = observed;
  }
  return false;
}

void Nif::releaseWriter()
{
  auto previous = m_gate.xchSub(Writer);
  if ((previous & (WriterMask | FinishWaiting)) == (Writer | FinishWaiting))
    mx()->rxRun([this] { finishStop(); });
}

void Nif::signal()
{
  uint64_t one = 1;
  while (::write(wakeFD(), &one, sizeof(one)) < 0) {
    if (errno == EINTR) continue;
    ZmAssert_(errno == EAGAIN);
    break;
  }
}

void Nif::wake()
{
  if (!admitWriter()) return;
  Zpkt::Nif::wake();
  releaseWriter();
}

void Nif::returned()
{
  if (!admitWriter()) return;
  wake_();
  releaseWriter();
}

unsigned Nif::receive()
{
  RxFrame frames[RxBurst];
  unsigned count = 0, limit = mx()->rxQuantum();
  if (limit > RxBurst) limit = RxBurst;
  if (m_port) {
    int rc = 0;
    count = unsigned(m_queue.receive(frames, limit, rc));
    if (rc) fail(rc);
  } else {
    while (count < limit && m_loop.receive(frames[count])) ++count;
  }

  ZiRxBurst burst;
  unsigned delivered = 0;
  uint64_t bytes = 0;
  for (unsigned i = 0; i < count; ++i) {
    const auto &frame = frames[i];
    if (ready() && frameFitsMTU(frame.data, frame.length, mtu())) {
      recv(frame.data, frame.length, count > 1 ? &burst : nullptr);
      ++delivered;
      bytes += frame.length;
    } else {
      m_pools.rx()->discard(frame.data);
    }
  }
  burst.flush();
  m_rxPackets.xchAdd(delivered);
  m_rxBytes.xchAdd(bytes);
  m_discards.xchAdd(count - delivered);
  return count;
}

void Nif::rx()
{
  unsigned spin = Zi_DefltSpin;
  auto pool = m_pools.rx();
  while (ready()) {
    if (woken()) return;
    if (m_port) {
      if (int rc = m_queue.refill(*pool)) { fail(rc); return; }
    } else {
      m_loop.refill(*pool);
    }
    if (receive()) { spin = Zi_DefltSpin; continue; }
    if (!ready()) break;
    if (spin) { --spin; continue; }

    startBlocking();
    if (m_port)
      if (int rc = m_queue.arm()) fail(rc);
    unsigned posted = m_port ? m_queue.rxPosted() : m_loop.rxPosted();
    if (ready() && !wakePending() && (posted == m_config.rxDepth || !pool->hasFree()) &&
	(m_port ? !receive() : !m_loop.rxReady()) && ready() && !wakePending()) {
      pollfd fds[]{{wakeFD(), POLLIN, 0}, {m_port ? m_queue.fd() : -1, POLLIN, 0}};
      int n = ::poll(fds, m_port ? 2 : 1, -1);
      if (n < 0) {
	if (errno != EINTR) fail(errno);
      } else {
	constexpr short errors = POLLERR | POLLHUP | POLLNVAL;
	if ((fds[0].revents | fds[1].revents) & errors) fail(EIO);
	if (fds[0].revents & POLLIN) ackWake();
	if (fds[1].revents & POLLIN)
	  if (int rc = m_queue.ack()) fail(rc);
      }
    }
    stopBlocking();
    spin = Zi_DefltSpin;
  }
}

Slot *Nif::txSlot(uint8_t *packet) const
{
  auto slot = m_pools.findTx(packet);
  ZmAssert_(slot && (slot->leases.load_() & Slot::Caller));
  return slot;
}

Zu::IO::Result Nif::txReady(uint8_t *packet) const
{
  if (!ready()) return Zi::IOError;
  return txSlot(packet)->leases.load_() & Slot::Submitted ? Zi::NotReady : Zi::OK;
}

Zu::IO::Result Nif::send(uint8_t *packet, unsigned length)
{
  if (!ready()) return Zi::IOError;
  auto slot = txSlot(packet);
  unsigned offset = uintptr_t(packet) - uintptr_t(slot->packet());
  if (length > slot->pool->size - offset || (!m_port && length > m_pools.rx()->size) ||
      !frameFitsMTU(packet, length, mtu())) return Zi::IOError;
  if (m_port) {
    int rc = m_queue.send(slot, packet, length);
    if (rc == EAGAIN || rc == ENOMEM || rc == ENOBUFS) return Zi::NotReady;
    if (rc) { fail(rc); return Zi::IOError; }
  } else {
    if (!m_loop.send(slot, packet, length)) return Zi::NotReady;
    wake_(); // Teardown crosses the Tx owner before closing the eventfd.
  }
  ++m_txPending;
  ++m_txPackets;
  m_txBytes.xchAdd(length);
  return Zi::OK;
}

unsigned Nif::send(ZuSpan<const TxPacket> frames)
{
  unsigned limit = frames.length();
  if (limit > MaxTxBatch) limit = MaxTxBatch;
  unsigned i = 0;
  for (; i < limit && send(frames[i].data, frames[i].length) == Zi::OK; ++i) { }
  return i;
}

unsigned Nif::pollTx()
{
  return ready() ? drainTx() : 0;
}

unsigned Nif::drainTx()
{
  unsigned count = 0;
  uint64_t bytes = 0;
  if (m_port) {
    Completion retired[MaxTxBatch];
    for (;;) {
      int rc = 0;
      int n = m_queue.retire(retired, MaxTxBatch, rc);
      for (int i = 0; i < n; ++i) {
	bytes += retired[i].length;
	retired[i].slot->release(Slot::Submitted);
      }
      count += unsigned(n);
      if (rc) { fail(rc); break; }
      if (n < MaxTxBatch) break;
    }
  } else {
    Completion retired;
    while (m_loop.retire(retired)) {
      bytes += retired.length;
      retired.slot->release(Slot::Submitted);
      ++count;
    }
  }
  m_txPending.xchSub(count);
  m_txRetired.xchAdd(count);
  m_txRetiredBytes.xchAdd(bytes);
  return count;
}

void Nif::status()
{
  ZmGuard guard(m_observe);
  ZiLOG(Info, "Zpkt.Mlx", ([
    name = CfString{name()}, active = ready(), port = m_port,
    steering = unsigned(m_steering), rxDepth = m_config.rxDepth, txDepth = m_config.txDepth,
    pending = unsigned(m_txPending), discards = uint64_t(m_discards),
    error = int(m_serviceError)
  ](auto &s) {
    s << "mlx " << name << (port ? " physical " : " software loop ")
      << (active ? "running" : "stopped") << " RDMA port " << ZuBoxed(port)
      << " catch-all steering " << (steering ? "on" : "off")
      << " Rx depth " << ZuBoxed(rxDepth) << " Tx depth " << ZuBoxed(txDepth)
      << " pending Tx " << ZuBoxed(pending) << " local discards " << ZuBoxed(discards)
      << " service error " << ZuBoxed(error);
  }));
}

struct QueueView : public Ztc::Queue {
  Ztc::QueueTelemetry	data;
  TelKey telKey() const override { return {data.ownerID, data.id, data.type}; }
  void telemetry(Ztc::QueueTelemetry &dst) const override { dst = data; }
};

unsigned Nif::allQueues(Ztc::QueueMgr::AllFn fn) const
{
  QueueView rx, tx;
  {
    ZmGuard guard(m_observe);
    if (!ready()) return 0;
    rx.data.ownerID = tx.data.ownerID = ZuID{mx()->id()};
    auto id = ZuID{} << "mlx." << ZuBoxed(ifindex()) << '.';
    rx.data.id = ZuID{id} << "rx";
    tx.data.id = ZuID{id} << "tx";
    rx.data.type = Ztc::QueueType::Rx;
    tx.data.type = Ztc::QueueType::Tx;
    rx.data.size = m_config.rxDepth;
    rx.data.outCount = m_rxPackets.load_();
    rx.data.outBytes = m_rxBytes.load_();
    tx.data.size = m_config.txDepth;
    tx.data.count = m_txPending.load_();
    tx.data.inCount = m_txPackets.load_();
    tx.data.inBytes = m_txBytes.load_();
    tx.data.outCount = m_txRetired.load_();
    tx.data.outBytes = m_txRetiredBytes.load_();
  }
  fn(&rx);
  fn(&tx);
  return 2;
}

} // namespace Zpkt::Mlx
